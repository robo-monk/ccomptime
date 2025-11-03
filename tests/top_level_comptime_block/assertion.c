#include "../test.h"

test({
  assert_log_includes(comp_stdout.items, "This gets executed during comptime!",
                      "Expected stdout during comptime");
})
