/* SPDX-License-Identifier: BSD-3-Clause-Eco */
/*
 * parse.h - read a .zap file into the shape it names.
 *
 * Whitespace-significant source is desugared into the canonical brace form
 * first (see desugar.h); pure-brace source passes through that step
 * byte for byte, so the brace grammar below never sees the difference.
 */

#ifndef ZAPGEN_PARSE_H
#define ZAPGEN_PARSE_H

#include "arena.h"
#include "schema.h"

#define ERRLEN 512

/* parse reads src (len bytes) as the contents of the file named name.
 * Returns a zap_file on success, or NULL with err set. The result owns an
 * arena; release it with file_free. */
struct zap_file *parse(const char *name, const char *src, size_t len,
		       char err[ERRLEN]);

void file_free(struct zap_file *f);

/* type_text parses one type expression on its own, in the arena a, with
 * aliases resolved against the file f (which may be NULL). It is the one
 * place a type is read, so a field's width is never sized by a second
 * reading of the same text. */
int type_text(struct arena *a, const struct zap_file *f, const char *expr,
	      struct zap_type *out, char err[ERRLEN]);

#endif
