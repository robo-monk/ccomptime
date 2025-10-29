#include "../test.h"

TEST({
  COMPILE_CASE();
  EXEC_CASE();
  log_test_result(stdout_includes_else_fail(
      exec_stdout.items, "SUM=6", __FILE__,
      "Expected macro argument reused across _Comptime calls to expand twice"));
})
