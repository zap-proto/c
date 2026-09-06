/* SPDX-License-Identifier: BSD-3-Clause-Eco */
/*
 * test.c - what the front end must do, said as cases.
 *
 * The golden pairs below are the same ones the Go reference pins, so a
 * change that moves one language's answer shows up as a failure in the
 * other's suite rather than as a schema that means two things.
 */

#include "desugar.h"
#include "emit.h"
#include "parse.h"

/* Generated from gen/echo.zap by the generator built alongside this
 * test, so a build that cannot generate its own bindings, or generates
 * bindings that do not compile, fails here. */
#include "echo.zap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;

static void same(const char *what, const char *in, const char *want)
{
	char err[ERRLEN], *got = NULL;
	size_t n = 0;

	if (desugar(in, strlen(in), &got, &n, err)) {
		printf("FAIL %s: desugar: %s\n", what, err);
		fails++;
		return;
	}
	if (n != strlen(want) || memcmp(got, want, n) != 0) {
		printf("FAIL %s\n  got  %s\n  want %s\n", what, got, want);
		fails++;
	}
	free(got);
}

static void refused(const char *what, const char *in, const char *because)
{
	char err[ERRLEN], *got = NULL;
	size_t n = 0;

	if (!desugar(in, strlen(in), &got, &n, err)) {
		printf("FAIL %s: accepted, want refusal\n", what);
		fails++;
		free(got);
		return;
	}
	if (because && !strstr(err, because)) {
		printf("FAIL %s: refused with \"%s\", want \"%s\"\n",
		       what, err, because);
		fails++;
	}
}

/* laid_out reads src and checks each field's name and byte, in order. */
static void laid_out(const char *what, const char *src, const char *want)
{
	char err[ERRLEN], got[512] = "";
	struct zap_file *f;
	size_t i, j;

	f = parse("t.zap", src, strlen(src), err);
	if (!f) {
		printf("FAIL %s: parse: %s\n", what, err);
		fails++;
		return;
	}
	for (i = 0; i < f->structs; i++)
		for (j = 0; j < f->strct[i].n; j++)
			snprintf(got + strlen(got), sizeof(got) - strlen(got),
				 "%s@%lld ", f->strct[i].field[j].name,
				 f->strct[i].field[j].offset);
	for (i = 0; i < f->ifaces; i++)
		for (j = 0; j < f->iface[i].n; j++)
			snprintf(got + strlen(got), sizeof(got) - strlen(got),
				 "%s#%d ", f->iface[i].method[j].name,
				 f->iface[i].method[j].ordinal);
	if (strcmp(got, want) != 0) {
		printf("FAIL %s\n  got  \"%s\"\n  want \"%s\"\n", what, got, want);
		fails++;
	}
	file_free(f);
}

static void turned_away(const char *what, const char *src, const char *because)
{
	char err[ERRLEN];
	struct zap_file *f = parse("t.zap", src, strlen(src), err);

	if (f && check(f, err) == 0) {
		printf("FAIL %s: accepted, want refusal\n", what);
		fails++;
		file_free(f);
		return;
	}
	if (because && !strstr(err, because)) {
		printf("FAIL %s: refused with \"%s\", want \"%s\"\n",
		       what, err, because);
		fails++;
	}
	file_free(f);
}

int main(void)
{
	/* A file already in braces comes back exactly as it went in. */
	same("brace is identity",
	     "package p\nstruct S {\n    A u8 @0\n}\n",
	     "package p\nstruct S {\n    A u8 @0\n}\n");
	same("header gains a brace, fields gain offsets",
	     "package p\nstruct S\n    A u8\n    B u32\n",
	     "package p\nstruct S {\n    A u8 @0\n    B u32 @1\n}\n");
	same("an explicit offset is kept and moves the cursor",
	     "package p\nstruct S\n    A u8 @4\n    B u32\n",
	     "package p\nstruct S {\n    A u8 @4\n    B u32 @5\n}\n");
	same("an aliased type is sized through its name",
	     "package p\ntype id32 = bytes_fixed[32]\nstruct S\n    A id32\n    B u32\n",
	     "package p\ntype id32 = bytes_fixed[32]\nstruct S {\n    A id32 @0\n    B u32 @32\n}\n");
	same("a list and a bytes tail are eight-byte pairs",
	     "package p\nstruct S\n    L list<Foo>\n    M bytes\n    N u8\n",
	     "package p\nstruct S {\n    L list<Foo> @0\n    M bytes @8\n    N u8 @16\n}\n");
	same("a nested struct is a four-byte offset",
	     "package p\nstruct S\n    F Foo\n    G u32\n",
	     "package p\nstruct S {\n    F Foo @0\n    G u32 @4\n}\n");
	same("blank lines and comments pass through",
	     "package p\n\nstruct S\n    # leading comment\n    A u8\n\n    B u8\n",
	     "package p\n\nstruct S {\n    # leading comment\n    A u8 @0\n\n    B u8 @1\n}\n");
	same("each struct starts its cursor again",
	     "package p\nstruct A\n    X u32\nstruct B\n    Y u32\n",
	     "package p\nstruct A {\n    X u32 @0\n}\nstruct B {\n    Y u32 @0\n}\n");
	same("a remark after a field is cut before the offset",
	     "package p\nstruct S\n    A u8  # the a field\n",
	     "package p\nstruct S {\n    A u8 @0\n}\n");
	same("both styles live in one file",
	     "package p\nstruct A {\n    X u8 @0\n}\nstruct B\n    Y u8\n",
	     "package p\nstruct A {\n    X u8 @0\n}\nstruct B {\n    Y u8 @0\n}\n");
	same("a missing final newline stays missing",
	     "package p\nstruct S\n    A u8",
	     "package p\nstruct S {\n    A u8 @0\n}");

	/* A field may be named for a keyword. A header is exactly the opener
	 * plus one identifier plus the end of the line; anything after that
	 * makes the line a field. */
	same("a field named struct",
	     "package p\nstruct S\n    struct u8 @0\n",
	     "package p\nstruct S {\n    struct u8 @0\n}\n");
	same("a field named interface",
	     "package p\nstruct S\n    interface text @8\n",
	     "package p\nstruct S {\n    interface text @8\n}\n");
	same("a field named interface without an offset",
	     "package p\nstruct S\n    interface text\n    B u8\n",
	     "package p\nstruct S {\n    interface text @0\n    B u8 @8\n}\n");
	same("a field named type is not read as an alias",
	     "package p\nstruct S\n    type u8 @0\n",
	     "package p\nstruct S {\n    type u8 @0\n}\n");
	same("a field named type moves the cursor by its width",
	     "package p\nstruct S\n    type u32\n    B u8\n",
	     "package p\nstruct S {\n    type u32 @0\n    B u8 @4\n}\n");
	same("a field named type inside braces",
	     "package p\nstruct S {\n    type u8 @0\n}\n",
	     "package p\nstruct S {\n    type u8 @0\n}\n");
	same("a real alias and a field named type in one file",
	     "package p\ntype id32 = bytes_fixed[32]\nstruct S\n    type id32\n    B u8\n",
	     "package p\ntype id32 = bytes_fixed[32]\nstruct S {\n    type id32 @0\n    B u8 @32\n}\n");

	/* A header the desugar does not know is left alone, so the parser is
	 * the one that names what is wrong with it. */
	same("a glued identifier passes through",
	     "package p\nstructFoo\n    A u8\n",
	     "package p\nstructFoo\n    A u8\n");
	same("a bare keyword passes through",
	     "package p\nstruct\n    A u8\n",
	     "package p\nstruct\n    A u8\n");

	refused("an offset that is not a number", "package p\nstruct S\n    A u8 @x\n",
		"not a number");
	refused("an unterminated fixed width", "package p\nstruct S\n    A bytes_fixed[\n",
		"expected integer");
	/* An unchecked accumulator would turn each of these into 0, aliasing
	 * onto the first byte of the struct. */
	refused("twenty nines", "package p\nstruct S\n    A u8 @99999999999999999999\n",
		"out of range");
	refused("two to the sixty-four", "package p\nstruct S\n    A u8 @18446744073709551616\n",
		"out of range");
	refused("past the bound", "package p\nstruct S\n    A u8 @9999999999\n",
		"out of range");
	same("at the bound", "package p\nstruct S\n    A u8 @2147483647\n",
	     "package p\nstruct S {\n    A u8 @2147483647\n}\n");

	laid_out("offsets read back",
		 "package p\ntype id32 = bytes_fixed[32]\nstruct S\n    A u32\n    B id32\n    C bytes\n",
		 "A@0 B@4 C@36 ");
	laid_out("keyword names read back",
		 "package p\nstruct S\n    type u32\n    struct u8 @4\n    interface text\n",
		 "type@0 struct@4 interface@5 ");
	laid_out("ordinals follow declaration order",
		 "package p\nstruct Q\n    A u8\ninterface I\n    a(req: Q)\n    b(req: Q) returns (resp: Q)\n    c()\n",
		 "A@0 a#1 b#2 c#3 ");

	turned_away("no package", "struct S\n    A u8\n", "expected `package`");
	turned_away("a struct with no fields", "package p\nstruct S {\n}\n", "no fields");
	turned_away("two fields on one byte",
		    "package p\nstruct S {\n    A u64 @0\n    B u8 @4\n}\n", "overlaps");
	turned_away("a payload naming a struct that is not there",
		    "package p\nstruct S\n    A u8\ninterface I\n    m(req: Nope)\n",
		    "unknown struct");
	turned_away("a method declared twice",
		    "package p\nstruct S\n    A u8\ninterface I\n    m(req: S)\n    m(req: S)\n",
		    "duplicate method");
	turned_away("two payloads in one direction",
		    "package p\nstruct S\n    A u8\ninterface I\n    m(a: S, b: S)\n",
		    "one struct payload");
	turned_away("an alias declared twice",
		    "package p\ntype x = u8\ntype x = u16\nstruct S\n    A x\n",
		    "duplicate type alias");

	/* The generated readers, on a message that never opened. Nothing here
	 * may reach outside the buffer: a field beyond the message reads as
	 * zero and a pointer that leads nowhere reads as null. This is the
	 * shape the sanitizer build watches. */
	{
		struct echo_Ping p;
		struct zap_msg m;
		struct echo_Site s;
		static const unsigned char stub[4] = {'Z', 'A', 'P', 0};

		if (echo_Ping_of(&p, &m, stub, sizeof(stub)) != -1) {
			printf("FAIL a four-byte buffer opened as a message\n");
			fails++;
		}
		memset(&p, 0, sizeof(p));
		if (echo_Ping_Seq(p) || echo_Ping_Hops(p) || echo_Ping_Live(p) ||
		    echo_Ping_Drift(p) != 0.0 ||
		    echo_Ping_Sender(p).p || echo_Ping_Note(p).p ||
		    echo_Ping_Payload(p).p || zap_len(echo_Ping_Trail(p)) ||
		    zap_list_bytes(echo_Ping_Trail(p), 0).p) {
			printf("FAIL a reader answered from outside the message\n");
			fails++;
		}
		s = echo_Ping_Origin(p);
		if (!zap_null(s.o) || echo_Site_Host(s) || echo_Site_Port(s)) {
			printf("FAIL a nested struct read from nowhere\n");
			fails++;
		}
		if (echo_Echo_ping != 1 || echo_Echo_notify != 2 ||
		    echo_Echo_health != 3) {
			printf("FAIL ordinals do not follow declaration order\n");
			fails++;
		}
	}

	if (fails) {
		printf("%d case(s) failed\n", fails);
		return 1;
	}
	printf("zapgen: all cases hold\n");
	return 0;
}
