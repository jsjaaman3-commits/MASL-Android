# Examples status

`language_basics/arithmetic_pipeline.masl` is a syntax-only specimen accepted
by the MASL lexer and parser. Run it with `maslc parse` to validate its syntax
and construct the AST. It is **not executable yet**: semantic analysis,
bytecode generation, the VM, and the simulation runtime have not been
implemented.

The minimum executable program and the Mini GPU example will be added only
after a real end-to-end execution path exists. This avoids presenting parser
fixtures as simulations.
