#include "../test.h"

test({
  assert_log_includes(exec_stdout.items, "Sum: 25",
                      "Expected first comptime block to compute sum correctly");
  assert_log_includes(exec_stdout.items, "Product: 42",
                      "Expected second comptime block to compute product correctly");
  assert_log_includes(exec_stdout.items, "Data.combined: 67",
                      "Expected third comptime block to use previous values");
  assert_log_includes(exec_stdout.items, "Nested inline value: 10",
                      "Expected nested inline comptime to work");
  assert_log_includes(exec_stdout.items, "NESTED_COMPTIME_VALID=1",
                      "Expected all nested comptime blocks to work correctly");
})
