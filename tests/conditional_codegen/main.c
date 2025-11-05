#include <stdio.h>
#include <stdbool.h>

#include "../../ccomptime.h"
#include "main.c.h"

#define USE_64BIT 1

int main(void) {
  // Generate different types based on compile-time conditions
  _Comptime({
    #if USE_64BIT
      _ComptimeCtx.TopLevel.appendf("typedef long long my_int_t;");
      printf("Generated 64-bit int type\n");
    #else
      _ComptimeCtx.TopLevel.appendf("typedef int my_int_t;");
      printf("Generated 32-bit int type\n");
    #endif
  });

  // Generate different values based on conditions
  int value = _Comptime({
    bool debug_mode = true;
    if (debug_mode) {
      _ComptimeCtx.Inline.appendf("999");
    } else {
      _ComptimeCtx.Inline.appendf("0");
    }
  });

  my_int_t x = 42;
  printf("VALUE=%d\n", value);
  printf("TYPE_SIZE=%zu\n", sizeof(my_int_t));
  printf("X=%lld\n", (long long)x);

  return (value == 999 && sizeof(my_int_t) == 8) ? 0 : 1;
}
