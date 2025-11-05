#include <stdio.h>

#include "../../ccomptime.h"
#include "main.c.h"

int main(void) {
  // Generate an array initializer at compile time
  int primes[] = _Comptime({
    int prime_list[] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29};
    _ComptimeCtx.Inline.appendf("{");
    for (int i = 0; i < 10; i++) {
      _ComptimeCtx.Inline.appendf("%d", prime_list[i]);
      if (i < 9) _ComptimeCtx.Inline.appendf(", ");
    }
    _ComptimeCtx.Inline.appendf("}");
    printf("Generated prime array at comptime\n");
  });

  // Generate array size as constant
  const int array_size = _Comptime(_ComptimeCtx.Inline.appendf("10"));

  // Compute sum at runtime
  int sum = 0;
  for (int i = 0; i < array_size; i++) {
    sum += primes[i];
  }

  printf("ARRAY_SIZE=%d\n", array_size);
  printf("SUM=%d\n", sum);
  printf("FIRST=%d\n", primes[0]);
  printf("LAST=%d\n", primes[9]);

  return (sum == 129 && primes[0] == 2 && primes[9] == 29) ? 0 : 1;
}
