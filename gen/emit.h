/* SPDX-License-Identifier: BSD-3-Clause-Eco */
/*
 * emit.h - the schema, said in C.
 *
 * One header per schema. Every struct becomes a view over the message
 * bytes and one reader per field that takes its value from a fixed offset,
 * so a read is an offset and a load, never a parse. Every interface becomes
 * its method ordinals, which declaration order fixes for good.
 */

#ifndef ZAPGEN_EMIT_H
#define ZAPGEN_EMIT_H

#include "schema.h"

#include <stdio.h>

/* emit writes the C header for f to out. Returns 0, or -1 with the reason
 * in err when the schema declares a layout that cannot be read (a field
 * overlapping another, a struct with no fields, a method naming a struct
 * the file does not declare). */
int emit(const struct zap_file *f, FILE *out, char err[512]);

/* check runs the same reading of the schema without writing anything. */
int check(const struct zap_file *f, char err[512]);

#endif
