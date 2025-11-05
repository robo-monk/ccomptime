#include "../test.h"

test({
  assert_log_includes(comp_stdout.items, "Read value 42 from file at compile time",
                      "Expected file read output at comptime");

  assert_log_includes(exec_stdout.items, "MAGIC_NUMBER=42",
                      "Expected embedded value from file");
})
