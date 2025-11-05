#include "../test.h"

test({
  assert_log_includes(exec_stdout.items, "Platform: 64-bit",
                      "Expected platform detection to work");
  assert_log_includes(exec_stdout.items, "Computed value: 42",
                      "Expected optimized function to be generated");
  assert_log_includes(exec_stdout.items, "CONDITIONAL_GEN_VALID=1",
                      "Expected conditional generation to be valid");
})
