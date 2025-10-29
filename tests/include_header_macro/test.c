#include "../test.h"

TEST({
  COMPILE_CASE();
  EXEC_CASE();
  log_test_result(stdout_includes_else_fail(
      exec_stdout.items, "ANSWER=5", __FILE__,
      "Expected _Comptime in included header to execute during compilation"));
})
