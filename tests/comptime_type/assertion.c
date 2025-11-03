#include "../test.h"

test({
  assert_log_includes(exec_stdout.items, "VALUE=7",
                      "Expected _ComptimeType to materialize struct literal");
})
