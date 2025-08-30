#include "../ccomptime.h"
#include "stdio.h"

$comptime_ctx(
    void helper() { printf("helper called\n"); }

    void on_exit() { printf("we niggas are exiting and shit"); });

// #ifdef COMPTIME
// void helper() { printf("helper called\n"); }
// #endif
//
//
int result = 0;

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

int main() {
  CCT_DO({ printf("hello from: comptime from main(%d) \n\n", result); });

  printf("runtime: hello!");
  return 0;
}
