/* SPDX-License-Identifier: BSD-3-Clause-Eco */

#include "parse.h"

#include "desugar.h"
#include "rune.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

long long slot_size(struct zap_type t)
{
	switch (t.kind) {
	case KIND_BOOL:
	case KIND_U8:
	case KIND_I8:
		return 1;
	case KIND_U16:
	case KIND_I16:
		return 2;
	case KIND_U32:
	case KIND_I32:
	case KIND_F32:
		return 4;
	case KIND_U64:
	case KIND_I64:
	case KIND_F64:
		return 8;
	case KIND_FIXED:
		return t.fixed;
	case KIND_BYTES:
	case KIND_TEXT:
	case KIND_LIST:
		return 8;
	case KIND_STRUCT:
		return 4;
	default:
		return 0;
	}
}

const char *kind_name(enum kind k)
{
	switch (k) {
	case KIND_BOOL:	  return "bool";
	case KIND_U8:	  return "u8";
	case KIND_U16:	  return "u16";
	case KIND_U32:	  return "u32";
	case KIND_U64:	  return "u64";
	case KIND_I8:	  return "i8";
	case KIND_I16:	  return "i16";
	case KIND_I32:	  return "i32";
	case KIND_I64:	  return "i64";
	case KIND_F32:	  return "f32";
	case KIND_F64:	  return "f64";
	case KIND_BYTES:  return "bytes";
	case KIND_FIXED:  return "bytes_fixed";
	case KIND_TEXT:	  return "text";
	case KIND_LIST:	  return "list";
	case KIND_STRUCT: return "struct";
	default:	  return "invalid";
	}
}

/* --- the reader ---------------------------------------------------------
 *
 * One cursor over the desugared source. `doc` holds the comment lines read
 * since the last thing that was not a comment; a declaration takes them, a
 * blank line drops them.
 */

struct reader {
	const char *src;
	size_t len, pos;
	int line;
	const char *name;	/* file name, for messages */
	struct arena *a;
	struct zap_file *f;
	const char **doc;
	size_t docn, doccap;
	char *err;		/* ERRLEN bytes, owned by the caller */
	int failed;
};

static void fail(struct reader *r, const char *fmt, ...)
{
	char msg[ERRLEN - 64];
	va_list ap;

	if (r->failed)
		return;
	r->failed = 1;
	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	snprintf(r->err, ERRLEN, "%s:%d: %s", r->name, r->line, msg);
}

/* grow returns room for cap items, holding the n items already in old. An
 * empty array is NULL, so the copy is only made when there is something to
 * copy - reading zero bytes from nowhere is not reading nothing. */
static void *grow(struct arena *a, const void *old, size_t n, size_t cap,
		  size_t item)
{
	void *p = arena_get(a, cap * item);

	if (p && n)
		memcpy(p, old, n * item);
	return p;
}

/* push_doc appends one comment line to the block being accumulated. */
static void push_doc(struct reader *r, const char *s, size_t n)
{
	if (r->docn == r->doccap) {
		size_t cap = r->doccap ? r->doccap * 2 : 8;
		const char **p = realloc(r->doc, cap * sizeof(*p));

		if (!p)
			return;
		r->doc = p;
		r->doccap = cap;
	}
	r->doc[r->docn++] = arena_str(r->a, s, n);
}

/* take_doc hands the accumulated block to the declaration being parsed and
 * clears it. An empty block stays empty, so "undocumented" is not the same
 * as "documented with nothing". */
static struct doc take_doc(struct reader *r)
{
	struct doc d = {NULL, 0};
	size_t i;

	if (!r->docn)
		return d;
	d.line = arena_get(r->a, r->docn * sizeof(*d.line));
	if (!d.line)
		return d;
	for (i = 0; i < r->docn; i++)
		d.line[i] = r->doc[i];
	d.n = r->docn;
	r->docn = 0;
	return d;
}

static void trim(const char **s, size_t *n)
{
	while (*n && (**s == ' ' || **s == '\t' || **s == '\r')) {
		(*s)++;
		(*n)--;
	}
	while (*n && ((*s)[*n - 1] == ' ' || (*s)[*n - 1] == '\t' ||
		      (*s)[*n - 1] == '\r'))
		(*n)--;
}

/* at_line_start reports whether only whitespace separates pos from the
 * start of its line. A '#' that fails this test remarks on the code to its
 * left rather than documenting what follows. */
static int at_line_start(struct reader *r)
{
	size_t i = r->pos;

	while (i > 0) {
		char c = r->src[--i];

		if (c == '\n')
			return 1;
		if (c != ' ' && c != '\t' && c != '\r')
			return 0;
	}
	return 1;
}

static void skip_space(struct reader *r)
{
	int blank = 1;

	while (r->pos < r->len) {
		char c = r->src[r->pos];

		if (c == '\n') {
			if (blank)
				r->docn = 0;
			blank = 1;
			r->line++;
			r->pos++;
		} else if (c == ' ' || c == '\t' || c == '\r') {
			r->pos++;
		} else if (c == '#') {
			int doc = at_line_start(r);
			size_t start = r->pos + 1, n;
			const char *s;

			while (r->pos < r->len && r->src[r->pos] != '\n')
				r->pos++;
			if (doc) {
				s = r->src + start;
				n = r->pos - start;
				trim(&s, &n);
				push_doc(r, s, n);
			}
			blank = 0;
		} else {
			return;
		}
	}
}

static int ident_start(uint32_t c) { return c == '_' || rune_letter(c); }
static int ident_rune(uint32_t c)
{
	return c == '_' || rune_letter(c) || rune_digit(c);
}

/* peek_word reports whether the upcoming bytes are word followed by a rune
 * that cannot continue an identifier. It does not advance. */
static int peek_word(struct reader *r, const char *word)
{
	size_t n = strlen(word), size;

	if (r->pos + n > r->len)
		return 0;
	if (memcmp(r->src + r->pos, word, n) != 0)
		return 0;
	if (r->pos + n == r->len)
		return 1;
	return !ident_rune(rune_read(r->src + r->pos + n, r->len - r->pos - n,
				     &size));
}

static const char *read_ident(struct reader *r)
{
	size_t start = r->pos, size;
	uint32_t c;

	if (r->pos >= r->len)
		return NULL;
	c = rune_read(r->src + r->pos, r->len - r->pos, &size);
	if (!ident_start(c))
		return NULL;
	r->pos += size;
	while (r->pos < r->len) {
		c = rune_read(r->src + r->pos, r->len - r->pos, &size);
		if (!ident_rune(c))
			break;
		r->pos += size;
	}
	return arena_str(r->a, r->src + start, r->pos - start);
}

/* A name that could not be held is a name the reader does not have, and
 * every caller already treats a missing name as a failure to read. */

/* read_int reads an unsigned decimal. A value too large to hold is an
 * error, never a wrap: an offset that wrapped would alias another field in
 * the zero-copy layout. */
static int read_int(struct reader *r, long long *out)
{
	size_t start = r->pos;
	unsigned long long n = 0;

	while (r->pos < r->len && r->src[r->pos] >= '0' && r->src[r->pos] <= '9') {
		unsigned d = (unsigned)(r->src[r->pos] - '0');

		if (n > (0x7FFFFFFFFFFFFFFFULL - d) / 10) {
			fail(r, "bad integer: value out of range");
			return -1;
		}
		n = n * 10 + d;
		r->pos++;
	}
	if (r->pos == start) {
		fail(r, "expected integer");
		return -1;
	}
	*out = (long long)n;
	return 0;
}

static int expect(struct reader *r, const char *lit)
{
	size_t n = strlen(lit), i;

	if (r->pos + n > r->len || memcmp(r->src + r->pos, lit, n) != 0) {
		fail(r, "expected \"%s\"", lit);
		return -1;
	}
	for (i = 0; i < n; i++)
		if (lit[i] == '\n')
			r->line++;
	r->pos += n;
	return 0;
}

/* --- grammar ----------------------------------------------------------- */

static int parse_type(struct reader *r, struct zap_type *out);

static const struct {
	const char *word;
	enum kind kind;
} primitives[] = {
	{"bool", KIND_BOOL},
	{"u8", KIND_U8},   {"u16", KIND_U16}, {"u32", KIND_U32}, {"u64", KIND_U64},
	{"i8", KIND_I8},   {"i16", KIND_I16}, {"i32", KIND_I32}, {"i64", KIND_I64},
	{"f32", KIND_F32}, {"f64", KIND_F64},
	{"bytes", KIND_BYTES}, {"text", KIND_TEXT},
};

static int parse_type(struct reader *r, struct zap_type *out)
{
	const char *name;
	size_t i;

	memset(out, 0, sizeof(*out));

	if (peek_word(r, "list")) {
		struct zap_type *elem = arena_get(r->a, sizeof(*elem));

		r->pos += 4;
		skip_space(r);
		if (expect(r, "<"))
			return -1;
		skip_space(r);
		if (parse_type(r, elem))
			return -1;
		skip_space(r);
		if (expect(r, ">"))
			return -1;
		out->kind = KIND_LIST;
		out->elem = elem;
		return 0;
	}
	if (peek_word(r, "bytes_fixed")) {
		long long n;

		r->pos += 11;
		skip_space(r);
		if (expect(r, "["))
			return -1;
		skip_space(r);
		if (read_int(r, &n))
			return -1;
		if (n <= 0) {
			fail(r, "bytes_fixed[N] must have N > 0");
			return -1;
		}
		skip_space(r);
		if (expect(r, "]"))
			return -1;
		out->kind = KIND_FIXED;
		out->fixed = n;
		return 0;
	}
	for (i = 0; i < sizeof(primitives) / sizeof(primitives[0]); i++) {
		if (peek_word(r, primitives[i].word)) {
			r->pos += strlen(primitives[i].word);
			out->kind = primitives[i].kind;
			return 0;
		}
	}
	name = read_ident(r);
	if (!name) {
		fail(r, "expected type");
		return -1;
	}
	if (r->f) {
		for (i = 0; i < r->f->aliases; i++) {
			if (strcmp(r->f->alias[i].name, name) == 0) {
				*out = r->f->alias[i].type;
				return 0;
			}
		}
	}
	out->kind = KIND_STRUCT;
	out->name = name;
	return 0;
}

/* alias := 'type' Ident '=' Type */
static int parse_alias(struct reader *r)
{
	const char *name;
	struct zap_type t;
	struct alias *a;
	size_t i;

	r->pos += 4;
	skip_space(r);
	name = read_ident(r);
	if (!name) {
		fail(r, "expected alias name after `type`");
		return -1;
	}
	skip_space(r);
	if (expect(r, "="))
		return -1;
	skip_space(r);
	if (parse_type(r, &t))
		return -1;
	for (i = 0; i < r->f->aliases; i++) {
		if (strcmp(r->f->alias[i].name, name) == 0) {
			fail(r, "duplicate type alias \"%s\"", name);
			return -1;
		}
	}
	a = grow(r->a, r->f->alias, r->f->aliases, r->f->aliases + 1, sizeof(*a));
	if (!a) {
		fail(r, "out of memory");
		return -1;
	}
	a[r->f->aliases].name = name;
	a[r->f->aliases].type = t;
	r->f->alias = a;
	r->f->aliases++;
	return 0;
}

/* field := Ident Type '@' Int */
static int parse_field(struct reader *r, struct zap_field *out)
{
	memset(out, 0, sizeof(*out));
	out->doc = take_doc(r);
	out->name = read_ident(r);
	if (!out->name) {
		fail(r, "expected field name");
		return -1;
	}
	skip_space(r);
	if (parse_type(r, &out->type))
		return -1;
	skip_space(r);
	if (expect(r, "@"))
		return -1;
	skip_space(r);
	return read_int(r, &out->offset);
}

/* strct := 'struct' Ident '{' Field* '}' */
static int parse_struct(struct reader *r, struct zap_struct *out)
{
	size_t cap = 0;

	memset(out, 0, sizeof(*out));
	out->doc = take_doc(r);
	r->pos += 6;
	skip_space(r);
	out->name = read_ident(r);
	if (!out->name) {
		fail(r, "expected struct name");
		return -1;
	}
	skip_space(r);
	if (expect(r, "{"))
		return -1;
	for (;;) {
		skip_space(r);
		if (r->pos >= r->len) {
			fail(r, "unterminated struct \"%s\"", out->name);
			return -1;
		}
		if (r->src[r->pos] == '}') {
			r->pos++;
			return 0;
		}
		if (out->n == cap) {
			struct zap_field *p;

			cap = cap ? cap * 2 : 8;
			p = grow(r->a, out->field, out->n, cap, sizeof(*p));
			if (!p) {
				fail(r, "out of memory");
				return -1;
			}
			out->field = p;
		}
		if (parse_field(r, &out->field[out->n]))
			return -1;
		out->n++;
	}
}

/* params := '(' (Ident ':' Ident)? ')' -- at most one struct payload per
 * direction, because a ZAP method carries one. */
static int parse_params(struct reader *r, int *has, struct param *out)
{
	*has = 0;
	if (expect(r, "("))
		return -1;
	skip_space(r);
	if (r->pos < r->len && r->src[r->pos] == ')') {
		r->pos++;
		return 0;
	}
	out->name = read_ident(r);
	if (!out->name) {
		fail(r, "expected parameter name");
		return -1;
	}
	skip_space(r);
	if (expect(r, ":"))
		return -1;
	skip_space(r);
	out->type = read_ident(r);
	if (!out->type) {
		fail(r, "expected parameter type");
		return -1;
	}
	skip_space(r);
	if (r->pos < r->len && r->src[r->pos] == ',') {
		fail(r, "method params carry exactly one struct payload per direction");
		return -1;
	}
	if (expect(r, ")"))
		return -1;
	*has = 1;
	return 0;
}

/* method := Ident '(' Param? ')' ('returns' '(' Param? ')')? */
static int parse_method(struct reader *r, int ordinal, struct zap_method *out)
{
	memset(out, 0, sizeof(*out));
	out->doc = take_doc(r);
	out->ordinal = ordinal;
	out->name = read_ident(r);
	if (!out->name) {
		fail(r, "expected method name");
		return -1;
	}
	skip_space(r);
	if (parse_params(r, &out->has_request, &out->request))
		return -1;
	skip_space(r);
	if (peek_word(r, "returns")) {
		r->pos += 7;
		skip_space(r);
		if (parse_params(r, &out->has_response, &out->response))
			return -1;
	}
	return 0;
}

/* iface := 'interface' Ident '{' Method* '}' -- ordinals are 1, 2, 3, ...
 * in declaration order, so appending a method never renumbers an older one. */
static int parse_iface(struct reader *r, struct zap_iface *out)
{
	size_t cap = 0;
	int ordinal = 1;

	memset(out, 0, sizeof(*out));
	out->doc = take_doc(r);
	r->pos += 9;
	skip_space(r);
	out->name = read_ident(r);
	if (!out->name) {
		fail(r, "expected interface name");
		return -1;
	}
	skip_space(r);
	if (expect(r, "{"))
		return -1;
	for (;;) {
		skip_space(r);
		if (r->pos >= r->len) {
			fail(r, "unterminated interface \"%s\"", out->name);
			return -1;
		}
		if (r->src[r->pos] == '}') {
			r->pos++;
			return 0;
		}
		if (out->n == cap) {
			struct zap_method *p;

			cap = cap ? cap * 2 : 8;
			p = grow(r->a, out->method, out->n, cap, sizeof(*p));
			if (!p) {
				fail(r, "out of memory");
				return -1;
			}
			out->method = p;
		}
		if (parse_method(r, ordinal, &out->method[out->n]))
			return -1;
		out->n++;
		ordinal++;
	}
}

/* file := 'package' Ident (Alias | Struct | Interface)* */
static int parse_file(struct reader *r)
{
	size_t scap = 0, icap = 0;

	skip_space(r);
	if (!peek_word(r, "package")) {
		fail(r, "expected `package` declaration");
		return -1;
	}
	r->f->doc = take_doc(r);
	r->pos += 7;
	skip_space(r);
	r->f->package = read_ident(r);
	if (!r->f->package) {
		fail(r, "expected package name after `package`");
		return -1;
	}
	for (;;) {
		skip_space(r);
		if (r->pos >= r->len)
			return 0;
		if (peek_word(r, "struct")) {
			if (r->f->structs == scap) {
				struct zap_struct *p;

				scap = scap ? scap * 2 : 8;
				p = grow(r->a, r->f->strct, r->f->structs, scap,
					 sizeof(*p));
				if (!p) {
					fail(r, "out of memory");
					return -1;
				}
				r->f->strct = p;
			}
			if (parse_struct(r, &r->f->strct[r->f->structs]))
				return -1;
			r->f->structs++;
		} else if (peek_word(r, "interface")) {
			if (r->f->ifaces == icap) {
				struct zap_iface *p;

				icap = icap ? icap * 2 : 8;
				p = grow(r->a, r->f->iface, r->f->ifaces, icap,
					 sizeof(*p));
				if (!p) {
					fail(r, "out of memory");
					return -1;
				}
				r->f->iface = p;
			}
			if (parse_iface(r, &r->f->iface[r->f->ifaces]))
				return -1;
			r->f->ifaces++;
		} else if (peek_word(r, "type")) {
			if (parse_alias(r))
				return -1;
		} else {
			fail(r, "expected `struct`, `interface`, or `type` at top level");
			return -1;
		}
	}
}

static const char *basename_of(const char *name)
{
	const char *p;

	for (p = name + strlen(name); p > name; p--)
		if (p[-1] == '/' || p[-1] == '\\')
			return p;
	return name;
}

struct zap_file *parse(const char *name, const char *src, size_t len,
		       char err[ERRLEN])
{
	struct arena *a = arena_new();
	struct reader r;
	struct zap_file *f;
	char *sugar = NULL;
	size_t sugarlen = 0;

	err[0] = '\0';
	if (!a) {
		snprintf(err, ERRLEN, "out of memory");
		return NULL;
	}
	if (desugar(src, len, &sugar, &sugarlen, err)) {
		char msg[ERRLEN];

		snprintf(msg, sizeof(msg), "%s: %s", basename_of(name), err);
		snprintf(err, ERRLEN, "%s", msg);
		arena_free(a);
		return NULL;
	}

	f = arena_get(a, sizeof(*f));
	if (!f) {
		snprintf(err, ERRLEN, "out of memory");
		free(sugar);
		arena_free(a);
		return NULL;
	}
	f->arena = a;
	f->source = arena_str(a, basename_of(name), strlen(basename_of(name)));

	memset(&r, 0, sizeof(r));
	r.src = sugar;
	r.len = sugarlen;
	r.line = 1;
	r.name = name;
	r.a = a;
	r.f = f;
	r.err = err;

	if (parse_file(&r)) {
		free(r.doc);
		free(sugar);
		arena_free(a);
		return NULL;
	}
	free(r.doc);
	free(sugar);
	return f;
}

void file_free(struct zap_file *f)
{
	if (f)
		arena_free(f->arena);
}

int type_text(struct arena *a, const struct zap_file *f, const char *expr,
	      struct zap_type *out, char err[ERRLEN])
{
	struct reader r;

	memset(&r, 0, sizeof(r));
	r.src = expr;
	r.len = strlen(expr);
	/* No file and no line: this reader is sizing a type expression lifted
	 * out of a field, so there is no position in the source to name. */
	r.line = 0;
	r.name = "";
	r.a = a;
	r.f = (struct zap_file *)f;
	r.err = err;
	err[0] = '\0';
	return parse_type(&r, out);
}
