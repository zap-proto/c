/* SPDX-License-Identifier: BSD-3-Clause-Eco */
/*
 * zapgen - read a .zap schema, write the C it describes.
 *
 * The schema may be written either way. The whitespace-significant form is
 * the one authors write; the brace form is what the grammar reads, and a
 * file already in it passes through untouched.
 *
 *	zapgen schema.zap		 write schema.zap.h beside the input
 *	zapgen -o gen schema.zap	 write it into gen/
 *	zapgen -c schema.zap		 read it and say nothing if it holds
 *	zapgen -d schema.zap		 write the brace form to stdout
 */

#include "desugar.h"
#include "emit.h"
#include "parse.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(void)
{
	fputs("usage: zapgen [-o DIR] [-c] [-d] SCHEMA.zap\n"
	      "  -o DIR  write into DIR (default: the input's directory)\n"
	      "  -c      read the schema only; write nothing\n"
	      "  -d      write the brace form of the schema to stdout\n",
	      stderr);
}

static char *slurp(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	size_t cap = 1 << 16, n = 0;
	char *buf;

	if (!f)
		return NULL;
	buf = malloc(cap);
	for (;;) {
		size_t got;

		if (!buf) {
			fclose(f);
			return NULL;
		}
		if (n == cap) {
			char *p = realloc(buf, cap *= 2);

			if (!p) {
				free(buf);
				fclose(f);
				return NULL;
			}
			buf = p;
		}
		got = fread(buf + n, 1, cap - n, f);
		n += got;
		if (got == 0)
			break;
	}
	fclose(f);
	*len = n;
	return buf;
}

/* out_path is the input path with ".h" appended, in dir when one is given.
 * A schema named foo.zap yields foo.zap.h, matching what the other
 * generators in this repository name their output. */
static void out_path(const char *in, const char *dir, char *out, size_t n)
{
	const char *base = in, *p;

	for (p = in; *p; p++)
		if (*p == '/' || *p == '\\')
			base = p + 1;
	if (dir)
		snprintf(out, n, "%s/%s.h", dir, base);
	else
		snprintf(out, n, "%s.h", in);
}

int main(int argc, char **argv)
{
	const char *dir = NULL, *in = NULL;
	int only_read = 0, only_desugar = 0, i;
	char err[ERRLEN], path[1024];
	struct zap_file *f;
	char *src, *brace;
	size_t len, bracelen;
	FILE *out;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
			dir = argv[++i];
		} else if (strcmp(argv[i], "-c") == 0) {
			only_read = 1;
		} else if (strcmp(argv[i], "-d") == 0) {
			only_desugar = 1;
		} else if (argv[i][0] == '-' && argv[i][1]) {
			usage();
			return 2;
		} else if (!in) {
			in = argv[i];
		} else {
			usage();
			return 2;
		}
	}
	if (!in) {
		usage();
		return 2;
	}

	src = slurp(in, &len);
	if (!src) {
		fprintf(stderr, "zapgen: cannot read %s\n", in);
		return 1;
	}

	if (only_desugar) {
		if (desugar(src, len, &brace, &bracelen, err)) {
			fprintf(stderr, "zapgen: %s\n", err);
			free(src);
			return 1;
		}
		fwrite(brace, 1, bracelen, stdout);
		free(brace);
		free(src);
		return 0;
	}

	f = parse(in, src, len, err);
	free(src);
	if (!f) {
		fprintf(stderr, "zapgen: %s\n", err);
		return 1;
	}

	if (only_read) {
		int rc = check(f, err);

		if (rc)
			fprintf(stderr, "zapgen: %s\n", err);
		file_free(f);
		return rc ? 1 : 0;
	}

	out_path(in, dir, path, sizeof(path));
	out = fopen(path, "w");
	if (!out) {
		fprintf(stderr, "zapgen: cannot write %s\n", path);
		file_free(f);
		return 1;
	}
	if (emit(f, out, err)) {
		fprintf(stderr, "zapgen: %s\n", err);
		fclose(out);
		remove(path);
		file_free(f);
		return 1;
	}
	fclose(out);
	file_free(f);
	return 0;
}
