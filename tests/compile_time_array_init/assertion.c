#include "../test.h"

test({
  assert_log_includes(exec_stdout.items, "Loaded 10 prime numbers",
                      "Expected to load 10 prime numbers");
  assert_log_includes(exec_stdout.items, "Sum of primes: 129",
                      "Expected sum to be computed at compile time");
  assert_log_includes(exec_stdout.items, "ARRAY_INIT_VALID=1",
                      "Expected array initialization to be valid");
})
