
> **Docs:** [ZAP C SDK](https://zap-proto.dev/docs/sdks/c) · part of the [ZAP Protocol](https://zap-proto.io)

zapc-c
========

This is the C language plugin (`zapc-c`) and runtime for [ZAP](https://zap-proto.io),
an efficient protocol for sharing data and capabilities.

> ## Security warning!

> The generated code assumes all input to be trusted. Do NOT use with
> untrusted input! There is currently no code in place to check if
> structures/pointers are within bounds.

This repository contains the code generator plugin together with the runtime
library required to use it.

## Building on Linux

```sh
git clone --recurse-submodules git@github.com:zap-proto/c.git zap-c
cd zap-c
autoreconf -f -i -s
./configure
make
make check
```

## Building with Meson

```sh
git clone --recurse-submodules git@github.com:zap-proto/c.git zap-c
cd zap-c
meson setup build
meson compile -C build
build/zap-test
```

## Usage

### Reading a `.zap` schema: `zapgen`

`zapgen` reads a ZAP schema and writes the C it describes. Schemas are
written whitespace-significant — indentation opens a block, and each field
takes the next free byte after the one before it:

```
package echo

struct Ping
    Seq    u64
    Sender bytes_fixed[32]
    Note   text

interface Echo
    ping(req: Ping) returns (resp: Pong)
```

```sh
zapgen echo.zap          # writes echo.zap.h beside the input
zapgen -o gen echo.zap   # writes it into gen/
zapgen -c echo.zap       # reads the schema and says nothing if it holds
zapgen -d echo.zap       # writes the brace form of the schema to stdout
```

The brace form — `struct S { A u8 @0 }`, every field carrying its byte —
is the same language said explicitly, and a file already written that way
passes through `zapgen -d` byte for byte. The two styles may be mixed one
declaration at a time.

The generated header gives each struct a view over the message bytes and
one reader per field, each taking its value from a fixed offset. Reading is
in place: nothing is copied, and no reader can step outside the message.
Include `<zap_view.h>` — it comes with the library — and the header the
schema produced.

This is the same schema language, read the same way, as
[ZAP for Go](https://github.com/zap-proto/go); the two front ends are
checked against each other on every schema in the estate.

### Generating C code from a Cap'n Proto schema

The `compiler` directory contains the C language plugin (`zapc-c`) for use with
the `zap` tool.

`zap` will by default search `$PATH` for `zapc-c` — if it's on your PATH, you
can generate code for your schema as follows:

```sh
zap compile -o c myschema.zap
```

Otherwise, you can specify the path to the c plugin:

```sh
zap compile -o ./zapc-c myschema.zap
```

`zap` generates a C struct that corresponds to each ZAP struct, along with
read/write functions that convert to/from ZAP wire form.

If you want accessor functions for struct members, use attribute `fieldgetset`
in your `.zap` file as follows:

```zap
using C = import "/c.zap";

$C.fieldgetset;

struct MyStruct {}
```

### Example C code

See the unit tests in [`tests/example-test.cpp`](tests/example-test.cpp).
The example schema file is [`tests/addressbook.zap`](tests/addressbook.zap).
The tests are written in C++, but only use C features.

You need to compile these runtime library files and link them into your own
project's binaries:

* [`lib/zap.c`](lib/zap.c)
* [`lib/zap-malloc.c`](lib/zap-malloc.c)
* [`lib/zap-stream.c`](lib/zap-stream.c)

Your include path must contain the runtime library directory
[`lib`](lib). Header file [`lib/zap_c.h`](lib/zap_c.h) contains the public
interfaces of the library.

Using make-based builds, make may try to compile `${x}.zap` from `${x}.zap.c`
using its built-in rule for compiling `${y}` from `${y}.c`. You can either
disable make's built-in compile rules or just this specific case with the no-op
rule: `%.zap: ;`.

For further reference, please see the other unit tests in [`tests`](tests), and
header file [`lib/zap_c.h`](lib/zap_c.h).
