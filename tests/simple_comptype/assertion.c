#include "../test.h"

test({
  assert_log_includes(comp_stdout.items,
                      "This thing is going to run during comptime",
                      "comptime output expected");

  assert_log_includes(exec_stdout.items, "=> 58!", "Expected 58 in the stdout");
})
