/* SPDX-License-Identifier: BSD-3-Clause-Eco */
/*
 * arena.h - one lifetime for everything a parse produces.
 *
 * A schema is read, projected and dropped. Nothing outlives the file it
 * came from, so every node and every name is cut from the same arena and
 * released in one call. There is no per-node ownership to get wrong.
 */

#ifndef ZAPGEN_ARENA_H
#define ZAPGEN_ARENA_H

#include <stddef.h>

struct arena;

struct arena *arena_new(void);
void arena_free(struct arena *a);

/* arena_get returns n zeroed bytes, or NULL when the allocation fails. */
void *arena_get(struct arena *a, size_t n);

/* arena_str copies n bytes and terminates them. */
char *arena_str(struct arena *a, const char *s, size_t n);

#endif
