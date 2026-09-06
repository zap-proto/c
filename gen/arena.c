/* SPDX-License-Identifier: BSD-3-Clause-Eco */

#include "arena.h"

#include <stdlib.h>
#include <string.h>

#define CHUNK (64 * 1024)

struct chunk {
	struct chunk *next;
	size_t len, cap;
	char data[];
};

struct arena {
	struct chunk *head;
};

struct arena *arena_new(void)
{
	return calloc(1, sizeof(struct arena));
}

void arena_free(struct arena *a)
{
	struct chunk *c, *next;

	if (!a)
		return;
	for (c = a->head; c; c = next) {
		next = c->next;
		free(c);
	}
	free(a);
}

void *arena_get(struct arena *a, size_t n)
{
	struct chunk *c;
	size_t cap;
	void *p;

	n = (n + 15) & ~(size_t)15;
	if (!a->head || a->head->cap - a->head->len < n) {
		cap = n > CHUNK ? n : CHUNK;
		c = calloc(1, sizeof(*c) + cap);
		if (!c)
			return NULL;
		c->cap = cap;
		c->next = a->head;
		a->head = c;
	}
	c = a->head;
	p = c->data + c->len;
	c->len += n;
	return p;
}

char *arena_str(struct arena *a, const char *s, size_t n)
{
	char *p = arena_get(a, n + 1);

	if (!p)
		return NULL;
	memcpy(p, s, n);
	p[n] = '\0';
	return p;
}
