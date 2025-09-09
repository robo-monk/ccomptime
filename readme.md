# ccomptime
> This software is unfinished

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

### Syntax

**Compile-time execution:**
```c
comptime {
    printf("This runs at compile time!\n");
}
```

## Example

```c
#include "ccomptime.h"

void generate_file() {
    FILE *f = fopen("generated.h", "w");
    fprintf(f, "#define MAGIC_NUMBER %d\n", 42);
    fclose(f);
}

long long fibo(long long n) {
  if (n <= 1)
    return n;
  long long a = 0, b = 1, c;
  for (long long i = 2; i <= n; i++) {
    c = a + b;
    a = b;
    b = c;
  }
  return b;
}

comptime { generate_file(); }

int main() {
    long long res = comptime_inline_infer(fibo(34 + 35)); // value will be computed at compile time and inlined here

    printf("Res is %llu\n", res);
    return 0;
}
```

The compile-time code runs during compilation, generating `generated.h` before the final binary is built.

## How it works

1. Preprocesses source files to find `comptime` blocks
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
