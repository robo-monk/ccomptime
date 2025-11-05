#include "../test.h"

test({
  assert_log_includes(comp_stdout.items, "Generated prime array at comptime",
                      "Expected array generation at comptime");

  assert_log_includes(exec_stdout.items, "ARRAY_SIZE=10",
                      "Expected correct array size");

  assert_log_includes(exec_stdout.items, "SUM=129",
                      "Expected correct sum of primes");

  assert_log_includes(exec_stdout.items, "FIRST=2",
                      "Expected first prime to be 2");

  assert_log_includes(exec_stdout.items, "LAST=29",
                      "Expected last prime to be 29");
})
