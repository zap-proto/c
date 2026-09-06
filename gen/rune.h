/* SPDX-License-Identifier: BSD-3-Clause-Eco */
/*
 * rune.h - what counts as a letter or a digit in an identifier, and how a
 * UTF-8 byte run decodes into the code point that is asked about.
 *
 * The tables in rune.c come from Go's unicode package (see rune-gen), so
 * an identifier this parser accepts is exactly an identifier the Go
 * reference parser accepts.
 */

#ifndef ZAPGEN_RUNE_H
#define ZAPGEN_RUNE_H

#include <stddef.h>
#include <stdint.h>

/* span is one half-open Unicode range with a stride, mirroring the shape
 * of a Go unicode.Range16 / Range32 entry. */
struct span {
	uint32_t lo, hi, stride;
};

int rune_letter(uint32_t r);
int rune_digit(uint32_t r);

/* rune_read decodes one UTF-8 code point from src[0:len] and stores its
 * width in *size. An invalid or truncated sequence decodes to U+FFFD with
 * width 1, matching utf8.DecodeRune. Returns 0 with *size 0 at the end of
 * the input. */
uint32_t rune_read(const char *src, size_t len, size_t *size);

#endif
