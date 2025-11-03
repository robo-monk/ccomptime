#include "../test.h"

test({
  assert_log_includes(
      exec_stdout.items, "SUM=6",
      "Expected macro argument reused across _Comptime calls to expand twice");
})
