#include <stdio.h>
#include <stdlib.h>

#include "../../ccomptime.h"
#include "main.c.h"

int main(void) {
  _Comptime({
    FILE *f = fopen("tests/compile_time_array_init/primes.txt", "r");
    if (!f) {
      fprintf(stderr, "Failed to open primes.txt\n");
      return;
    }

    char line[256];
    int numbers[100];
    int count = 0;

    // Read all numbers
    while (fgets(line, sizeof(line), f) && count < 100) {
      int num = atoi(line);
      if (num > 0) {
        numbers[count++] = num;
      }
    }
    fclose(f);

    // Generate array
    _ComptimeCtx.TopLevel.appendf("static const int prime_numbers[] = {");
    for (int i = 0; i < count; i++) {
      if (i > 0) _ComptimeCtx.TopLevel.appendf(", ");
      _ComptimeCtx.TopLevel.appendf("%d", numbers[i]);
    }
    _ComptimeCtx.TopLevel.appendf("};\n");
    _ComptimeCtx.TopLevel.appendf("static const int prime_count = %d;\n", count);

    // Also generate sum at compile time
    int sum = 0;
    for (int i = 0; i < count; i++) {
      sum += numbers[i];
    }
    _ComptimeCtx.TopLevel.appendf("static const int prime_sum = %d;\n", sum);
  });
  printf("Loaded %d prime numbers\n", prime_count);
  printf("Primes: ");
  for (int i = 0; i < prime_count; i++) {
    printf("%d ", prime_numbers[i]);
  }
  printf("\n");

  printf("Sum of primes: %d\n", prime_sum);

  // Verify
  int runtime_sum = 0;
  for (int i = 0; i < prime_count; i++) {
    runtime_sum += prime_numbers[i];
  }

  if (prime_count == 10 && runtime_sum == prime_sum && prime_sum == 129) {
    printf("ARRAY_INIT_VALID=1\n");
    return 0;
  }

  printf("ARRAY_INIT_VALID=0\n");
  return 1;
}
