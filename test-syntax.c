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

int fib(int n) {
  if (n <= 1)
    return n;
  return fib(n - 1) + fib(n - 2);
}

// int ass = comptime{5};
// int ass2 = comptime 10;
// _Comptime({fprintf(_Comptime_FP, "\n#include <stdio.h>\n")});

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

  int m = add(int, 5, 10);
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
