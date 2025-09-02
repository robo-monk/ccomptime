# ccomptime
> This software is unfinished

A drop-in replacement for `clang` that adds compile-time code execution to C.

## Overview

`ccomptime` allows you to run C code at compile time using the `$comptime()` directive. The code executes during compilation and can generate files, perform calculations, or modify the compilation process.

## Usage

### Building
```bash
cc -O2 -o ccomptime-clang ccomptime-clang.c
```

### Using as compiler
Replace `clang` with `ccomptime-clang`:
```bash
./ccomptime-clang -o program test/main.c
```

### Syntax

**Compile-time execution:**
```c
$comptime(printf("This runs at compile time!\n"););
```

## Example

```c
#include "ccomptime.h"

void generate_file() {
    FILE *f = fopen("generated.h", "w");
    fprintf(f, "#define MAGIC_NUMBER %d\n", 42);
    fclose(f);
}

int main() {
    $comptime(generate_file(););
    printf("Runtime code here\n");
    return 0;
}
```

The compile-time code runs during compilation, generating `generated.h` before the final binary is built.

## How it works

1. Preprocesses source files to find `$comptime()` blocks
2. Extracts compile-time code into a separate "runner" program
3. Compiles and executes the runner to generate replacements
4. Rewrites the original source with results
5. Compiles the final program with regular clang

## Related Projects

While several languages and tools offer compile-time execution, `ccomptime` is unique in bringing full compile-time code execution to C:

### Similar Approaches
- **Zig's `comptime`** - Built-in compile-time execution with similar capabilities, but requires using Zig
- **C++ `constexpr`** - Compile-time function evaluation, but limited to pure functions (no I/O)
- **Template metaprogramming** - C++ templates can generate code, but with complex syntax and limited scope
- **Build system generators** - Tools like CMake or custom scripts, but separate from compilation

### What Makes This Different
- **True compile-time execution in C** - Full C language access during compilation
- **I/O capabilities** - Can generate files, network requests, or any operation (unlike `constexpr`)
- **Drop-in replacement** - Works with existing C codebases without language changes
- **Integrated compilation** - Part of the compilation process, not a separate build step
