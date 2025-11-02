#include "../test.h"

TEST({
  COMPILE_CASE();
  // log_test_result(stdout_includes_else_fail(
  //     comp_stdout.items, "Generating polymorphic type Result", __FILE__,
  //     "comptime output expected"));

  log_test_result(stdout_includes_else_fail(comp_stdout.items,
                                            "Hello from comptime", __FILE__,
                                            "comptime output expected"));

  EXEC_CASE();
  log_test_result(stdout_includes_else_fail(
      exec_stdout.items, "yep : 42", __FILE__, "Expected yep : 42 in stdout"));
})
