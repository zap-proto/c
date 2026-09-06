/* SPDX-License-Identifier: BSD-3-Clause-Eco */

#include "emit.h"

#include "parse.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(char *err, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(err, ERRLEN, fmt, ap);
	va_end(ap);
	return -1;
}

/* size is the fixed section a struct occupies: the furthest byte any field
 * reaches. Offsets are the author's, so nothing is rounded. */
static long long size_of(const struct zap_struct *s)
{
	long long size = 0;
	size_t i;

	for (i = 0; i < s->n; i++) {
		long long end = s->field[i].offset + slot_size(s->field[i].type);

		if (end > size)
			size = end;
	}
	return size;
}

/* one_struct reads the layout the schema declares and refuses one that
 * cannot be read: a field with no width, a negative offset, or two fields
 * claiming the same byte. The author owns the layout; a typo in it should
 * not quietly become code.
 *
 * Overlap is decided by comparing the spans themselves, so a struct that
 * declares a byte two billion in costs no more to read than one that does
 * not. The byte reported is the first one a field finds already taken, and
 * the field named is the one that took it - the same pair a walk in
 * declaration order would name.
 */
static int one_struct(const struct zap_struct *s, char *err)
{
	long long size = size_of(s);
	size_t i, j;

	if (!size)
		return fail(err, "struct %s: no fields", s->name);
	for (i = 0; i < s->n; i++) {
		const struct zap_field *f = &s->field[i];
		long long sz = slot_size(f->type);
		long long hit = -1;
		const char *owner = NULL;

		if (!sz)
			return fail(err, "struct %s field %s: unsupported type %s",
				    s->name, f->name, kind_name(f->type.kind));
		if (f->offset < 0)
			return fail(err, "struct %s field %s: negative offset %lld",
				    s->name, f->name, f->offset);
		for (j = 0; j < i; j++) {
			const struct zap_field *e = &s->field[j];
			long long lo = e->offset > f->offset ? e->offset : f->offset;
			long long ehi = e->offset + slot_size(e->type);

			if (lo >= ehi || lo >= f->offset + sz)
				continue;	/* the spans do not meet */
			if (hit < 0 || lo < hit) {
				hit = lo;
				owner = e->name;
			}
		}
		if (owner)
			return fail(err,
				    "struct %s: field %s at offset %lld overlaps field %s",
				    s->name, f->name, hit, owner);
	}
	return 0;
}

/* one_iface reads a service: every payload must name a struct the same file
 * declares, and no method name may repeat. */
static int one_iface(const struct zap_file *f, const struct zap_iface *in,
		     char *err)
{
	size_t i, j, k;

	if (!in->n)
		return fail(err, "interface %s: no methods", in->name);
	for (i = 0; i < in->n; i++) {
		const struct zap_method *m = &in->method[i];
		const struct param *p[2];
		int has[2];
		int side;

		for (j = 0; j < i; j++)
			if (strcmp(in->method[j].name, m->name) == 0)
				return fail(err, "interface %s: duplicate method %s",
					    in->name, m->name);
		p[0] = &m->request;
		p[1] = &m->response;
		has[0] = m->has_request;
		has[1] = m->has_response;
		for (side = 0; side < 2; side++) {
			int known = 0;

			if (!has[side])
				continue;
			for (k = 0; k < f->structs; k++)
				if (strcmp(f->strct[k].name, p[side]->type) == 0)
					known = 1;
			if (!known)
				return fail(err,
					    "interface %s method %s: unknown struct \"%s\" in param \"%s\"",
					    in->name, m->name, p[side]->type,
					    p[side]->name);
		}
	}
	return 0;
}

int check(const struct zap_file *f, char err[ERRLEN])
{
	size_t i;

	for (i = 0; i < f->structs; i++)
		if (one_struct(&f->strct[i], err))
			return -1;
	for (i = 0; i < f->ifaces; i++)
		if (one_iface(f, &f->iface[i], err))
			return -1;
	return 0;
}

/* --- writing ------------------------------------------------------------ */

/* guard turns a source name into an include guard: upper case, every
 * character that cannot be in an identifier replaced by '_'. */
static void guard(const char *src, char *out, size_t n)
{
	size_t i;

	for (i = 0; i + 1 < n && src[i]; i++) {
		char c = src[i];

		if (c >= 'a' && c <= 'z')
			c = (char)(c - 'a' + 'A');
		else if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')))
			c = '_';
		out[i] = c;
	}
	out[i] = '\0';
}

static void words(FILE *out, struct doc d, const char *pad)
{
	size_t i;

	for (i = 0; i < d.n; i++)
		fprintf(out, "%s/* %s */\n", pad, d.line[i]);
}

/* name_of joins the package and a declaration into the one name generated
 * code uses, so two schemas in one program never collide. */
static void name_of(const struct zap_file *f, const char *n, char *out, size_t sz)
{
	snprintf(out, sz, "%s_%s", f->package, n);
}

/* view writes the struct's type and the byte each of its fields sits at.
 * Every view type is written before any reader, so a field may name a
 * struct declared further down the file. */
static void view(FILE *out, const struct zap_file *f,
		 const struct zap_struct *s)
{
	char t[256];
	size_t i;

	name_of(f, s->name, t, sizeof(t));
	words(out, s->doc, "");
	fprintf(out, "struct %s {\n\tstruct zap_obj o;\n};\n\n", t);
	for (i = 0; i < s->n; i++)
		fprintf(out, "#define %s_%s_at %lld\n", t, s->field[i].name,
			s->field[i].offset);
	fprintf(out, "#define %s_size %lld\n\n", t, size_of(s));
}

static void reader(FILE *out, const struct zap_file *f,
		   const struct zap_struct *s)
{
	char t[256];
	size_t i;

	name_of(f, s->name, t, sizeof(t));

	fprintf(out, "/* %s_of reads b in place. Returns 0, or -1 when the head\n"
		     " * does not hold. */\n", t);
	fprintf(out, "static inline int %s_of(struct %s *v, struct zap_msg *m,\n"
		     "\t\t\tconst void *b, size_t n)\n{\n", t, t);
	fprintf(out, "\tif (zap_open(m, b, n))\n\t\treturn -1;\n");
	fprintf(out, "\tv->o = zap_root(m);\n\treturn 0;\n}\n\n");

	for (i = 0; i < s->n; i++) {
		const struct zap_field *fl = &s->field[i];
		char elem[256];

		words(out, fl->doc, "");
		switch (fl->type.kind) {
		case KIND_BOOL:
			fprintf(out, "static inline int %s_%s(struct %s v)\n"
				     "{\n\treturn zap_bool(v.o, %s_%s_at);\n}\n",
				t, fl->name, t, t, fl->name);
			break;
		case KIND_U8:
		case KIND_U16:
		case KIND_U32:
		case KIND_U64:
		case KIND_I8:
		case KIND_I16:
		case KIND_I32:
		case KIND_I64: {
			static const char *const ctype[] = {
				"uint8_t", "uint16_t", "uint32_t", "uint64_t",
				"int8_t", "int16_t", "int32_t", "int64_t"
			};
			static const char *const call[] = {
				"u8", "u16", "u32", "u64", "i8", "i16", "i32", "i64"
			};
			int k = fl->type.kind - KIND_U8;

			fprintf(out, "static inline %s %s_%s(struct %s v)\n"
				     "{\n\treturn zap_%s(v.o, %s_%s_at);\n}\n",
				ctype[k], t, fl->name, t, call[k], t, fl->name);
			break;
		}
		case KIND_F32:
			fprintf(out, "static inline float %s_%s(struct %s v)\n"
				     "{\n\treturn zap_f32(v.o, %s_%s_at);\n}\n",
				t, fl->name, t, t, fl->name);
			break;
		case KIND_F64:
			fprintf(out, "static inline double %s_%s(struct %s v)\n"
				     "{\n\treturn zap_f64(v.o, %s_%s_at);\n}\n",
				t, fl->name, t, t, fl->name);
			break;
		case KIND_TEXT:
		case KIND_BYTES:
			fprintf(out, "static inline struct zap_bytes %s_%s(struct %s v)\n"
				     "{\n\treturn zap_bytes_at(v.o, %s_%s_at);\n}\n",
				t, fl->name, t, t, fl->name);
			break;
		case KIND_FIXED:
			fprintf(out, "static inline struct zap_bytes %s_%s(struct %s v)\n"
				     "{\n\treturn zap_fixed(v.o, %s_%s_at, %lld);\n}\n",
				t, fl->name, t, t, fl->name, fl->type.fixed);
			break;
		case KIND_LIST:
			fprintf(out, "static inline struct zap_list %s_%s(struct %s v)\n"
				     "{\n\treturn zap_list_at(v.o, %s_%s_at);\n}\n",
				t, fl->name, t, t, fl->name);
			break;
		case KIND_STRUCT:
			name_of(f, fl->type.name, elem, sizeof(elem));
			fprintf(out, "static inline struct %s %s_%s(struct %s v)\n"
				     "{\n\tstruct %s n;\n\n"
				     "\tn.o = zap_obj_at(v.o, %s_%s_at);\n"
				     "\treturn n;\n}\n",
				elem, t, fl->name, t, elem, t, fl->name);
			break;
		default:
			break;
		}
	}
	fprintf(out, "\n");
}

static void service(FILE *out, const struct zap_file *f,
		    const struct zap_iface *in)
{
	char t[256];
	size_t i;

	name_of(f, in->name, t, sizeof(t));
	words(out, in->doc, "");
	fprintf(out, "/* Ordinals follow declaration order, so adding a method\n"
		     " * never renumbers one that is already on the wire. */\n");
	for (i = 0; i < in->n; i++) {
		const struct zap_method *m = &in->method[i];

		words(out, m->doc, "");
		fprintf(out, "#define %s_%s %d", t, m->name, m->ordinal);
		if (m->has_request || m->has_response) {
			fprintf(out, "\t/*");
			if (m->has_request)
				fprintf(out, " %s: %s", m->request.name,
					m->request.type);
			if (m->has_response)
				fprintf(out, " -> %s: %s", m->response.name,
					m->response.type);
			fprintf(out, " */");
		}
		fprintf(out, "\n");
	}
	fprintf(out, "\n");
}

int emit(const struct zap_file *f, FILE *out, char err[ERRLEN])
{
	char g[256];
	size_t i;

	if (check(f, err))
		return -1;

	guard(f->source, g, sizeof(g));
	fprintf(out, "/* Generated by zapgen. DO NOT EDIT. */\n");
	fprintf(out, "/* source: %s */\n\n", f->source);
	words(out, f->doc, "");
	if (f->doc.n)
		fprintf(out, "\n");
	fprintf(out, "#ifndef ZAP_%s_H\n#define ZAP_%s_H\n\n", g, g);
	fprintf(out, "#include <zap_view.h>\n\n");
	fprintf(out, "#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n");

	for (i = 0; i < f->structs; i++)
		view(out, f, &f->strct[i]);
	for (i = 0; i < f->structs; i++)
		reader(out, f, &f->strct[i]);
	for (i = 0; i < f->ifaces; i++)
		service(out, f, &f->iface[i]);

	fprintf(out, "#ifdef __cplusplus\n}\n#endif\n\n#endif\n");
	return 0;
}
