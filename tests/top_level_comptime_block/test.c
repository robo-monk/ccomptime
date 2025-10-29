#include "../test.h"

TEST({
  COMPILE_CASE();
  log_test_result(stdout_includes_else_fail(
      comp_stdout.items, "This gets executed during comptime!", __FILE__,
      "Expected stdout during comptime"));
})
