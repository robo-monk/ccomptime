#include "../test.h"

TEST({
  COMPILE_CASE();
  EXEC_CASE();
  log_test_result(
      stdout_includes_else_fail(exec_stdout.items, "COMPTIME_VALUE=42",
                                __FILE__, "COMPTIME_VALUE does not equal 42"));
})
