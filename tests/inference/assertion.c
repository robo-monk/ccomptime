#include "../test.h"

test({
  assert_log_includes(exec_stdout.items, "hi = hello there", "Expected 'hi' to be printed");
  assert_log_includes(exec_stdout.items, "yes = 10", "Expected 'yes' to be printed");
})
