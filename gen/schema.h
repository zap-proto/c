/* SPDX-License-Identifier: BSD-3-Clause-Eco */
/*
 * schema.h - the shape a .zap file names.
 *
 * One source file yields one zap_file: a package name, a set of type
 * aliases, and the structs and interfaces declared under them. The types
 * mirror the Go reference AST field for field, so the two front ends
 * describe the same schema in the same terms.
 */

#ifndef ZAPGEN_SCHEMA_H
#define ZAPGEN_SCHEMA_H

#include <stddef.h>

enum kind {
	KIND_NONE,
	KIND_BOOL,
	KIND_U8,
	KIND_U16,
	KIND_U32,
	KIND_U64,
	KIND_I8,
	KIND_I16,
	KIND_I32,
	KIND_I64,
	KIND_F32,
	KIND_F64,
	KIND_BYTES,	/* variable-length bytes */
	KIND_FIXED,	/* bytes_fixed[N] */
	KIND_TEXT,	/* variable-length UTF-8 */
	KIND_LIST,	/* list<T> */
	KIND_STRUCT	/* nested struct by name */
};

/* zap_type is the resolved type of a field. At most one of fixed / elem /
 * name carries detail, chosen by kind. */
struct zap_type {
	enum kind kind;
	long long fixed;	/* bytes_fixed[N] */
	struct zap_type *elem;	/* list<T> */
	const char *name;	/* nested struct by name */
};

/* doc is the comment block written above a declaration, carried through so
 * the words in the schema reach whatever the schema is projected into. */
struct doc {
	const char **line;
	size_t n;
};

struct zap_field {
	struct doc doc;
	const char *name;
	struct zap_type type;
	long long offset;
};

struct zap_struct {
	struct doc doc;
	const char *name;
	struct zap_field *field;
	size_t n;
};

/* param is one method payload: a name bound to a struct declared in the
 * same file. */
struct param {
	const char *name;
	const char *type;
};

struct zap_method {
	struct doc doc;
	const char *name;
	int ordinal;		/* 1-based, assigned by declaration order */
	int has_request;
	struct param request;
	int has_response;
	struct param response;
};

struct zap_iface {
	struct doc doc;
	const char *name;
	struct zap_method *method;
	size_t n;
};

struct alias {
	const char *name;
	struct zap_type type;
};

struct zap_file {
	struct doc doc;
	const char *package;
	const char *source;	/* basename of the input, for the emitted header */
	struct alias *alias;
	size_t aliases;
	struct zap_struct *strct;
	size_t structs;
	struct zap_iface *iface;
	size_t ifaces;
	void *arena;		/* owns every allocation reachable from here */
};

/* slot_size is the width a field of this type occupies in the fixed
 * section: a variable-length tail is {relOff u32, length u32}, a nested
 * struct is {relOff u32}, bytes_fixed[N] is N bytes inline. Zero means the
 * type has no layout, which is an error wherever a width is required. */
long long slot_size(struct zap_type t);

/* kind_name is the schema's own spelling of a kind, for error messages. */
const char *kind_name(enum kind k);

#endif
