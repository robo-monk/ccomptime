# ccomptime
> This software is unfinished and highly unstable

A drop-in replacement for `clang` that adds compile-time code execution to C.

## Overview

`ccomptime` allows you to run C code at compile time using `comptime`, `inline_comptime` directives. The code executes during compilation and can generate code, files, and anything really. You keep all the footguns C gives you even during compilation time!


## Usage

### Building
```bash
cc nob.c -o nob
./nob
```

### Using as compiler
Prepend your clang command with the `ccomptime` cli.
Thats it, ccomptime will "highjack" the compilation process
```bash
./ccomptime clang -o program test/main.c
```

## How it works

1. Preprocesses source files to find `comptime` blocks
2. Extracts compile-time code into a separate "runner" program
3. Compiles and executes the runner to generate the header file with macros replacing each comptime block in the original file
5. Compiles the final program with regular clang with the generated .h file included

## Related Projects

While several languages and tools offer compile-time execution, `ccomptime` is unique in bringing full compile-time code execution to C:

### Similar Approaches
- **Zig's `comptime`** - Built-in compile-time execution with similar capabilities, but requires using Zig and also bad support for simple IO operations
- **C++ `constexpr`** - Compile-time function evaluation, but limited to pure functions (no I/O)
- **Template metaprogramming** - C++ templates can generate code, but eeeeeeeeeeeh
- **Build system generators** - Tools like CMake or custom scripts, but separate from compilation and also eeeeeeeeeeeeeeeeeeeeeeeeeh
