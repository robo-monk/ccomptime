#include "../test.h"

test({
  assert_log_includes(exec_stdout.items, "GREETING=Hello from comptime!",
                      "Expected greeting string from comptime");

  assert_log_includes(exec_stdout.items, "BUFFER=Generated at compile time",
                      "Expected buffer string from comptime");
})
