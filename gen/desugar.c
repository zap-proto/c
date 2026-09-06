/* SPDX-License-Identifier: BSD-3-Clause-Eco */

#include "desugar.h"

#include "arena.h"
#include "parse.h"
#include "schema.h"
#include "str.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A slice of the source: no copies are made until the output is written. */
struct span_str {
	const char *p;
	size_t n;
};

enum line_kind {
	LINE_CLEAR,	/* blank or a full-line '#' comment */
	LINE_OTHER,	/* package, type alias, brace lines */
	LINE_HEAD,	/* whitespace-form block header */
	LINE_FIELD	/* whitespace-form field */
};

struct line {
	struct span_str raw;
	struct span_str body;	/* raw, trimmed */
	int indent;		/* leading spaces; tabs count one column each */
	enum line_kind kind;
	const char *keyword;	/* for LINE_HEAD: the opener that started it */
};

/* maxoff bounds a declared byte offset. A struct's fixed section is
 * addressed by these in a zero-copy layout, so an offset far past any real
 * message is a typo, not a layout. Rejecting it keeps the read total: no
 * overflow, no silent wrap onto offset 0. */
#define MAXOFF 2147483648ULL

struct work {
	struct line *line;
	size_t n;
	struct str out;
	size_t written;		/* elements joined so far, for the separator */
	struct arena *a;
	/* aliases, as the text on the right of `type X = ...`, so a field of
	 * an aliased type can be sized by the one real type parser. */
	struct span_str *aname, *aexpr;
	size_t aliases;
	char *err;
};

/* --- slices ------------------------------------------------------------- */

static struct span_str span(const char *p, size_t n)
{
	struct span_str s = {p, n};
	return s;
}

static int span_is(struct span_str s, const char *lit)
{
	size_t n = strlen(lit);

	return s.n == n && memcmp(s.p, lit, n) == 0;
}

static int space(char c) { return c == ' ' || c == '\t'; }

static struct span_str trim(struct span_str s)
{
	while (s.n && (space(s.p[0]) || s.p[0] == '\r')) {
		s.p++;
		s.n--;
	}
	while (s.n && (space(s.p[s.n - 1]) || s.p[s.n - 1] == '\r'))
		s.n--;
	return s;
}

/* cut removes a trailing '#' comment, and the space before it. */
static struct span_str cut(struct span_str s)
{
	size_t i;

	for (i = 0; i < s.n; i++)
		if (s.p[i] == '#')
			return trim(span(s.p, i));
	return s;
}

/* word splits the leading whitespace-delimited token off s. */
static struct span_str word(struct span_str s, struct span_str *rest)
{
	size_t i = 0, j;

	while (i < s.n && space(s.p[i]))
		i++;
	for (j = i; j < s.n; j++) {
		if (space(s.p[j])) {
			size_t k = j;

			while (k < s.n && space(s.p[k]))
				k++;
			*rest = span(s.p + k, s.n - k);
			return span(s.p + i, j - i);
		}
	}
	*rest = span(s.p + s.n, 0);
	return span(s.p + i, s.n - i);
}

/* after returns the text following kw, with the separating space consumed,
 * and whether s started with that keyword as a whole token. */
static int after(struct span_str s, const char *kw, struct span_str *rest)
{
	size_t n = strlen(kw);

	if (s.n < n || memcmp(s.p, kw, n) != 0)
		return 0;
	if (s.n == n)
		return 0;	/* a bare keyword names nothing */
	if (!space(s.p[n]))
		return 0;	/* `structFoo` is one identifier, not the keyword */
	*rest = trim(span(s.p + n, s.n - n));
	return 1;
}

/* an identifier here is ASCII: `[A-Za-z_]\w*`. The desugar decides shape,
 * not meaning; the parser holds the full grammar. */
static int is_ident(struct span_str s)
{
	size_t i;

	if (!s.n)
		return 0;
	for (i = 0; i < s.n; i++) {
		char c = s.p[i];

		if (c == '_' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
			continue;
		if (c >= '0' && c <= '9') {
			if (i == 0)
				return 0;
			continue;
		}
		return 0;
	}
	return 1;
}

/* --- line facts --------------------------------------------------------- */

/* blockers are the keywords that open a braceless block. A struct body holds
 * fields, whose offsets are assigned; an interface body holds methods, which
 * carry no offset and pass through as written. */
static const char *const blockers[] = {"struct", "interface"};

/* opener reports whether body is a braceless block header and which keyword
 * opened it. A header is EXACTLY `<opener>` + one identifier + end of line,
 * once a trailing comment is cut. It deliberately does not match a field
 * whose NAME is `struct` or `interface` and that carries a type or an
 * offset after it (`struct u8 @0`), nor `structFoo`, nor `struct S {`. Those
 * fall through and are read as ordinary fields. */
static const char *opener(struct span_str body)
{
	struct span_str rest, id, tail;
	size_t i;

	body = cut(body);
	if (body.n && body.p[body.n - 1] == '{')
		return NULL;
	for (i = 0; i < sizeof(blockers) / sizeof(blockers[0]); i++) {
		if (!after(body, blockers[i], &rest))
			continue;
		id = word(rest, &tail);
		if (is_ident(id) && tail.n == 0)
			return blockers[i];
	}
	return NULL;
}

/* `package` is the one token that begins a file-scope construct and so can
 * never name a field. A real header is already claimed by opener, and a
 * top-level alias is excluded by the '=' test below - so `struct`,
 * `interface`, `type` and the rest ARE valid field names. */
static int reserved(struct span_str s) { return span_is(s, "package"); }

static int is_field(struct span_str body)
{
	struct span_str b = cut(body), name, rest, type;
	size_t i;

	for (i = 0; i < b.n; i++)
		if (b.p[i] == '{' || b.p[i] == '}' || b.p[i] == '=')
			return 0;
	name = word(b, &rest);
	if (!name.n || !rest.n || reserved(name))
		return 0;
	type = word(rest, &rest);
	return type.n != 0;
}

/* delta is the net '{' minus '}' on a line, ignoring anything commented. */
static int delta(struct span_str raw)
{
	int d = 0;
	size_t i;

	for (i = 0; i < raw.n; i++) {
		if (raw.p[i] == '#')
			return d;
		if (raw.p[i] == '{')
			d++;
		else if (raw.p[i] == '}')
			d--;
	}
	return d;
}

static struct line classify(struct span_str raw)
{
	struct line ln;
	size_t i = 0;

	memset(&ln, 0, sizeof(ln));
	ln.raw = raw;
	while (i < raw.n && space(raw.p[i]))
		i++;
	ln.indent = (int)i;
	ln.body = trim(raw);
	if (!ln.body.n || ln.body.p[0] == '#') {
		ln.kind = LINE_CLEAR;
		return ln;
	}
	ln.keyword = opener(ln.body);
	if (ln.keyword)
		ln.kind = LINE_HEAD;
	else if (is_field(ln.body))
		ln.kind = LINE_FIELD;
	else
		ln.kind = LINE_OTHER;
	return ln;
}

/* --- writing ------------------------------------------------------------ */

static void put(struct work *w, struct span_str s)
{
	if (w->written++)
		str_add(&w->out, "\n", 1);
	str_add(&w->out, s.p, (int)s.n);
}

static void indent(struct str *s, int n)
{
	while (n-- > 0)
		str_add(s, " ", 1);
}

/* --- offsets ------------------------------------------------------------ */

static int fail(struct work *w, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(w->err, ERRLEN, fmt, ap);
	va_end(ap);
	return -1;
}

/* number reads an unsigned decimal, rejecting anything else. An offset past
 * MAXOFF, or one that would overflow, is refused outright: an unchecked
 * accumulator turns @18446744073709551616 into 0, aliasing field offset 0. */
static int number(struct work *w, struct span_str s, long long *out)
{
	unsigned long long n = 0;
	size_t i;

	if (!s.n)
		return fail(w, "empty integer");
	for (i = 0; i < s.n; i++) {
		unsigned d;

		if (s.p[i] < '0' || s.p[i] > '9')
			return fail(w, "not a number: \"%.*s\"", (int)s.n, s.p);
		d = (unsigned)(s.p[i] - '0');
		if (n > (~0ULL - d) / 10)
			return fail(w, "offset \"%.*s\" out of range", (int)s.n, s.p);
		n = n * 10 + d;
	}
	if (n > MAXOFF)
		return fail(w, "offset \"%.*s\" out of range (max %llu)",
			    (int)s.n, s.p, MAXOFF);
	*out = (long long)n;
	return 0;
}

/* width is the fixed-section width of the type text, read by the real type
 * parser so layout is sized in exactly one place. A named alias is sized by
 * the text it stands for; a list or a nested struct is a fixed-width
 * pointer, which slot_size already says. */
static int width(struct work *w, struct span_str type, long long *out)
{
	char expr[256], err[ERRLEN];
	struct span_str e = type;
	struct zap_type t;
	size_t i;

	for (i = 0; i < w->aliases; i++) {
		if (w->aname[i].n == type.n &&
		    memcmp(w->aname[i].p, type.p, type.n) == 0) {
			e = w->aexpr[i];
			break;
		}
	}
	if (e.n >= sizeof(expr))
		return fail(w, "type \"%.*s\" is too long", (int)e.n, e.p);
	memcpy(expr, e.p, e.n);
	expr[e.n] = '\0';
	if (type_text(w->a, NULL, expr, &t, err))
		return fail(w, "%s", err[0] ? err : "bad type");
	*out = slot_size(t);
	if (!*out)
		return fail(w, "type \"%.*s\" has zero slot width",
			    (int)type.n, type.p);
	return 0;
}

/* field writes one whitespace-form field line, keeping or assigning its
 * '@N' and advancing the cursor. */
static int field(struct work *w, struct line ln, long long *cursor)
{
	struct span_str b = cut(ln.body), name, type, rest;
	long long off = 0, sz;
	int has = 0;
	char buf[64];

	name = word(b, &rest);
	if (!name.n || !rest.n)
		return fail(w, "malformed field \"%.*s\"", (int)b.n, b.p);
	type = word(rest, &rest);
	if (!type.n)
		return fail(w, "field \"%.*s\" missing type", (int)name.n, name.p);
	rest = trim(rest);
	if (rest.n) {
		if (rest.p[0] != '@')
			return fail(w, "field \"%.*s\": unexpected trailing \"%.*s\"",
				    (int)name.n, name.p, (int)rest.n, rest.p);
		if (number(w, trim(span(rest.p + 1, rest.n - 1)), &off)) {
			char msg[ERRLEN];

			snprintf(msg, sizeof(msg), "field \"%.*s\": bad @offset: %s",
				 (int)name.n, name.p, w->err);
			snprintf(w->err, ERRLEN, "%s", msg);
			return -1;
		}
		has = 1;
	}
	if (width(w, type, &sz)) {
		char msg[ERRLEN];

		snprintf(msg, sizeof(msg), "field \"%.*s\": %s",
			 (int)name.n, name.p, w->err);
		snprintf(w->err, ERRLEN, "%s", msg);
		return -1;
	}
	if (!has)
		off = *cursor;
	*cursor = off + sz;

	if (w->written++)
		str_add(&w->out, "\n", 1);
	indent(&w->out, ln.indent);
	str_add(&w->out, name.p, (int)name.n);
	str_add(&w->out, " ", 1);
	str_add(&w->out, type.p, (int)type.n);
	snprintf(buf, sizeof(buf), " @%lld", off);
	str_add(&w->out, buf, -1);
	return 0;
}

/* --- the walk ----------------------------------------------------------- */

/* how a block's body lines are read. */
enum mode {
	AT_FILE,	/* file scope: no enclosing block */
	AT_STRUCT,	/* fields, offsets assigned */
	AT_IFACE	/* methods, written through as they stand */
};

static int walk(struct work *w, size_t start, size_t end, enum mode mode);

/* extent returns the index one past the last line of a block whose header
 * sits at head: the run of following lines indented deeper, with blank and
 * comment lines absorbed so they neither open nor close a block. */
static size_t extent(struct work *w, size_t start, size_t end, int head)
{
	size_t i = start;

	while (i < end) {
		if (w->line[i].kind == LINE_CLEAR) {
			i++;
			continue;
		}
		if (w->line[i].indent <= head)
			break;
		i++;
	}
	/* Trim trailing blank lines back out, so a blank line between two
	 * top-level structs closes the first cleanly. */
	while (i > start && w->line[i - 1].kind == LINE_CLEAR)
		i--;
	return i;
}

/* header opens a braceless block, walks its indented body in the matching
 * mode, closes the brace, and returns the index one past the body. */
static size_t header(struct work *w, size_t i, size_t end, int *bad)
{
	struct line ln = w->line[i];
	struct span_str h = cut(ln.body);
	size_t body_end;

	if (w->written++)
		str_add(&w->out, "\n", 1);
	indent(&w->out, ln.indent);
	str_add(&w->out, h.p, (int)h.n);
	str_add(&w->out, " {", 2);

	body_end = extent(w, i + 1, end, ln.indent);
	if (walk(w, i + 1, body_end,
		 strcmp(ln.keyword, "interface") == 0 ? AT_IFACE : AT_STRUCT)) {
		*bad = 1;
		return body_end;
	}
	if (w->written++)
		str_add(&w->out, "\n", 1);
	indent(&w->out, ln.indent);
	str_add(&w->out, "}", 1);
	return body_end;
}

/* brace copies lines from i verbatim - the line opens at least one '{' -
 * until the running depth is back to zero. */
static size_t brace(struct work *w, size_t i, size_t end)
{
	int depth = 0;

	while (i < end) {
		put(w, w->line[i].raw);
		depth += delta(w->line[i].raw);
		i++;
		if (depth <= 0)
			break;
	}
	return i;
}

static int walk(struct work *w, size_t start, size_t end, enum mode mode)
{
	long long cursor = 0;	/* the struct's running field offset */
	size_t i = start;

	while (i < end) {
		struct line ln = w->line[i];

		if (ln.kind == LINE_CLEAR) {
			put(w, ln.raw);
			i++;
			continue;
		}
		/* A line that opens a literal brace block is brace syntax:
		 * copy it and everything to its matching '}' as written, so
		 * brace declarations stay byte-identical. */
		if (delta(ln.raw) > 0) {
			i = brace(w, i, end);
			continue;
		}
		/* A whitespace header opens a sub-block only at file scope.
		 * Inside a struct the same line is a field named `interface`,
		 * not a service; inside an interface it is a method. */
		if (ln.kind == LINE_HEAD && mode == AT_FILE) {
			int bad = 0;

			i = header(w, i, end, &bad);
			if (bad)
				return -1;
			continue;
		}
		if (mode == AT_STRUCT) {
			if (field(w, ln, &cursor))
				return -1;
		} else {
			/* File scope or interface body: written through as it
			 * stands. At file scope an un-headered field-shaped
			 * line is left for the parser to name precisely. */
			put(w, ln.raw);
		}
		i++;
	}
	return 0;
}

/* aliases records every top-level `type X = <expr>` so a field's offset can
 * be sized through the name. An alias is ONLY a top-level construct, so a
 * field literally named `type` - under a header or inside braces - is not
 * read as one; it stays a field and is sized on the field path. */
static int aliases(struct work *w)
{
	int depth = 0;
	size_t i;

	w->aname = arena_get(w->a, w->n * sizeof(*w->aname) + 1);
	w->aexpr = arena_get(w->a, w->n * sizeof(*w->aexpr) + 1);
	if (!w->aname || !w->aexpr)
		return fail(w, "out of memory");
	for (i = 0; i < w->n; i++) {
		struct line ln = w->line[i];
		int top = depth == 0 && ln.indent == 0;
		struct span_str body, rest, name, expr;
		size_t j, eq;

		depth += delta(ln.raw);
		if (!top)
			continue;
		body = cut(ln.body);
		if (!after(body, "type", &rest))
			continue;
		eq = rest.n;
		for (j = 0; j < rest.n; j++) {
			if (rest.p[j] == '=') {
				eq = j;
				break;
			}
		}
		if (eq == rest.n)
			return fail(w, "alias \"%.*s\" missing '='",
				    (int)body.n, body.p);
		name = trim(span(rest.p, eq));
		expr = trim(span(rest.p + eq + 1, rest.n - eq - 1));
		if (!name.n || !expr.n)
			return fail(w, "malformed alias \"%.*s\"",
				    (int)body.n, body.p);
		w->aname[w->aliases] = name;
		w->aexpr[w->aliases] = expr;
		w->aliases++;
	}
	return 0;
}

/* split cuts src into lines, dropping one trailing empty element so a file
 * ending in a newline comes back the same length. */
static size_t split(const char *src, size_t len, struct line **out,
		    struct arena *a)
{
	size_t n = 0, i, start = 0;
	struct line *ln;

	for (i = 0; i < len; i++)
		if (src[i] == '\n')
			n++;
	if (!len || src[len - 1] != '\n')
		n++;	/* the last, unterminated line */
	if (!len)
		n = 0;

	ln = arena_get(a, (n + 1) * sizeof(*ln));
	if (!ln) {
		*out = NULL;
		return 0;
	}
	n = 0;
	for (i = 0; i < len; i++) {
		if (src[i] == '\n') {
			ln[n++] = classify(span(src + start, i - start));
			start = i + 1;
		}
	}
	if (start < len)
		ln[n++] = classify(span(src + start, len - start));
	*out = ln;
	return n;
}

int desugar(const char *src, size_t len, char **out, size_t *outlen,
	    char err[ERRLEN])
{
	struct work w;
	int rc = -1;

	memset(&w, 0, sizeof(w));
	str_init(&w.out, 0);
	w.err = err;
	err[0] = '\0';
	w.a = arena_new();
	if (!w.a) {
		snprintf(err, ERRLEN, "out of memory");
		goto done;
	}
	w.n = split(src, len, &w.line, w.a);
	if (w.n && !w.line) {
		snprintf(err, ERRLEN, "out of memory");
		goto done;
	}

	/* Aliases first: the offset cursor sizes an aliased field through
	 * the real type parser, so there is one way to size a type. */
	if (aliases(&w))
		goto done;
	if (walk(&w, 0, w.n, AT_FILE))
		goto done;
	/* Keep the source's final-newline state, so a pure-brace file comes
	 * back byte for byte. */
	if (len && src[len - 1] == '\n')
		str_add(&w.out, "\n", 1);

	*out = malloc((size_t)w.out.len + 1);
	if (!*out) {
		snprintf(err, ERRLEN, "out of memory");
		goto done;
	}
	memcpy(*out, w.out.str, (size_t)w.out.len);
	(*out)[w.out.len] = '\0';
	*outlen = (size_t)w.out.len;
	rc = 0;
done:
	str_release(&w.out);
	arena_free(w.a);
	return rc;
}
