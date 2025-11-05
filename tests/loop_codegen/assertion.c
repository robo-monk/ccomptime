#include "../test.h"

test({
  assert_log_includes(comp_stdout.items, "Generated 5 getter functions at comptime",
                      "Expected loop-based function generation at comptime");

  assert_log_includes(comp_stdout.items, "Generated struct with 3 fields at comptime",
                      "Expected loop-based struct generation at comptime");

  assert_log_includes(exec_stdout.items, "SUM=100",
                      "Expected sum of generated getter values");

  assert_log_includes(exec_stdout.items, "STRUCT_SUM=6",
                      "Expected sum of struct fields");
})
