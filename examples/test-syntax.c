#include "./test-syntax.c.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define NOB_IMPLEMENTATION
#include "nob.h"

#include "ccomptime.h"

#define HASHMAP_IMPLEMENTATION
#include "deps/hashmap/hashmap.h"

// #define comptime_log(x) _Comptime({ printf("%s\n", x); })

HashMap _func_defs = {0};
void _polymorphic_add(_ComptimeCtx _ComptimeCtx, const char *t, const char *a,
                      const char *b) {
  // const char *func_hash = t;

  const char *func_sign = nob_temp_sprintf("%s add_%s(%s a, %s b)", t, t, t, t);
  if (!hashmap_get(&_func_defs, (char *)func_sign)) {
    hashmap_put(&_func_defs, (char *)func_sign, (void *)1);
    fprintf(_Comptime_FP, "\n%s { return a + b; }\n", func_sign);
  }

  _ComptimeCtx.Inline.appendf("add_%s(%s,%s)", t, a, b);
}

#define add(type, a, b)                                                        \
  _Comptime(_polymorphic_add(_ComptimeCtx, #type, #a, #b));

union {
  int a;
  float b;
} my_union;

void _polymorphic_result(_ComptimeCtx _ComptimeCtx, const char *t,
                         const char *e) {
  const char *struct_sign =
      nob_temp_sprintf("typedef struct { union {%s value; %s error;} as; bool "
                       "is_ok; } Result_%s_%s;",
                       t, e, t, e);

  if (!hashmap_get(&_func_defs, (char *)struct_sign)) {
    hashmap_put(&_func_defs, (char *)struct_sign, (void *)1);
    printf("\n ~~ Generating struct sign %s\n", struct_sign);
    fprintf(_Comptime_FP, "\n%s\n", struct_sign);
  }

  _ComptimeCtx.Inline.appendf("Result_%s_%s", t, e);
}

#define Result(T, E) _ComptimeType(_polymorphic_result(_ComptimeCtx, #T, #E))

Result(int, int) test5() { return {.is_ok = true, .value = 5}; }

int fib(int n) {
  if (n <= 1)
    return n;
  return fib(n - 1) + fib(n - 2);
}

int main() {
  // comptime_log("~> Hello, World!");
  // _Comptime(printf("Hello, World!\n"));
  // int x = _Comptime({
  //   printf("wooo$$$");
  //   fprintf(_Comptime_FP,
  //           "\nvoid hello_world() { printf(\"what in the fuck\"); }\n");
  //   int result = fib(42);
  //   char *output = malloc(512);
  //   snprintf(output, 512, "%d", result);
  //   // return _InlineC(output);
  // });

  // int m = add(int, 5, 10);
}
// #define test1 x
// #define test3(...) \
//   comptime { 0 + 0 }

// #define test2(a, b, c, drip)                                                   \
//   comptime {                                                                   \
//     int y = a[0] + b[0];                                                       \
//     int x = c + drip;                                                          \
//   };gg

// int main() {
//   // test1;
//   test2("hello_there", "aaaa", 3, 4);

//   int result = comptime{42};
// }

// test2("x", "y", 69, 420);
// int calc() { return 14; }

// int a = comptime calc();

// int test(int x) { return x * 2; }

// comptime { printf("insane amounts of sex;\n"); }

// int main() {
//   comptime { printf("insane amounts of sex;\n"); }

//   int b = comptime test(5);
//   return 0;
// }


_Comptime({ printf("Hi from comptime!\n") }); // this will get executed during compile time

int main(void) { 
    int large_fibo = _Comptime({
        _ComptimeCtx.Inline.appendf("%d", fibonacci(42));
    }); // 267914296 will be baked in to the executable
    return 0;
}
