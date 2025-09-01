#include "../ccomptime.h"
#define NOB_IMPLEMENTATION
#include "../nob.h"
#include "stdio.h"

// $comptime_ctx(
//     void helper() { printf("helper called\n"); }

//     void on_exit() { printf("we niggas are exiting and shit"); });

// int result = 0;

int fibonacci(int n) {
  if (n < 2)
    return n;
  return fibonacci(n - 1) + fibonacci(n - 2);
}

$comptime({
  int i = 0;
  while (i++ < 10) {
    const int r = fibonacci(i);
    printf("fibfib(%d) = %d\n", i, result);
    result += r;
  }

  printf("\n\n --- FINAL RESULT IS ... %d  ----\n", result);
});

$comptime({
  printf("> COMPTIME VEC ;: \n");
  for (int i = 0; i < 10; i++) {
    $$(nob_temp_sprintf("void vector_%d(){}", i));
  }
});

int result = 0;
int main() {
  CCT_DO({ printf("hello from: comptime from main(%d) \n\n", result); });
  printf("hello from runtime space");
  return 42;
}
