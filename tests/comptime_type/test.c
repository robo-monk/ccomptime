#include "../test.h"

TEST({
  COMPILE_CASE();
  EXEC_CASE();
  log_test_result(stdout_includes_else_fail(
      exec_stdout.items, "VALUE=7", __FILE__,
      "Expected _ComptimeType to materialize struct literal"));
})
