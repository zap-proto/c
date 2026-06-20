
# Regenerating schema support

The files `schema.zap`, `c.zap`, and `c++.zap` define the ZAP schema and the
C-plugin annotations. `schema.zap.c` / `schema.zap.h` are generated from
`schema.zap` and must be regenerated whenever the schema changes.

`schema.zap` references the in-tree copy of `c++.zap`
(`using Cxx = import "c++.zap";` at the top of the file).

Regenerate the schema support:

    $ zap compile -o ./zapc-c compiler/schema.zap

Then regenerate again, based on the previously regenerated schema, to confirm
the output is stable (a fixpoint).

You can always inspect the `zapc` formatted output during debugging:

    $ zap compile -ozapc-zapc compiler/schema.zap
