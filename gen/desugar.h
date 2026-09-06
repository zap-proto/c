/* SPDX-License-Identifier: BSD-3-Clause-Eco */
/*
 * desugar.h - the whitespace-significant form, written out in braces.
 *
 * Runs BEFORE the parser, so the proven brace grammar stays untouched. The
 * transform is a near-identity: it only ADDS the two tokens the brace
 * grammar requires where the whitespace form leaves them out.
 *
 *   - A block header (`struct <Id>`) not already ended by '{' gains one,
 *     and a matching '}' is placed at the header's indent where the
 *     indented body ends.
 *   - A field written `Name Type` with no trailing '@N' gains `@<off>`,
 *     the running byte offset accumulated from the declared slot width of
 *     the fields before it in the same struct. An explicit '@N' is kept
 *     and resets the cursor to N + width.
 *
 * So a pure-brace file - every header ending in '{', every field carrying
 * '@N' - comes back byte for byte unchanged, and the two styles may be
 * mixed one top-level declaration at a time.
 */

#ifndef ZAPGEN_DESUGAR_H
#define ZAPGEN_DESUGAR_H

#include <stddef.h>

/* desugar writes the brace form of src into *out (malloc'd, NUL-terminated,
 * *outlen bytes) and returns 0. On failure it returns -1 and writes the
 * reason into err. */
int desugar(const char *src, size_t len, char **out, size_t *outlen,
	    char err[512]);

#endif
