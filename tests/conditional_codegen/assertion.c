#include "../test.h"

test({
  assert_log_includes(comp_stdout.items, "Generated 64-bit int type",
                      "Expected 64-bit type generation at comptime");

  assert_log_includes(exec_stdout.items, "VALUE=999",
                      "Expected conditional value generation");

  assert_log_includes(exec_stdout.items, "TYPE_SIZE=8",
                      "Expected 64-bit type size");
})
