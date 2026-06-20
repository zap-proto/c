
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

### Generating C code from a `.zap` schema file

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
