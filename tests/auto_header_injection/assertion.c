#include "../test.h"

test({
  assert_log_includes(exec_stdout.items, "COMPTIME_VALUE=42",
                      "COMPTIME_VALUE does not equal 42");
})
