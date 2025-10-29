#include "../test.h"

TEST({
  COMPILE_CASE();
  log_test_result(stdout_includes_else_fail(
      comp_stdout.items, "This thing is going to run during comptime", __FILE__,
      "comptime output expected"));

  EXEC_CASE();
  log_test_result(stdout_includes_else_fail(
      exec_stdout.items, "=> 58!", __FILE__, "Expected 58 in the stdout"));
})
