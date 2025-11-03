#include "../test.h"

test({
  assert_log_includes(comp_stdout.items, "Hello from comptime",
                      "comptime output expected");

  assert_log_includes(exec_stdout.items, "yep : 42",
                      "Expected yep : 42 in stdout");
})
