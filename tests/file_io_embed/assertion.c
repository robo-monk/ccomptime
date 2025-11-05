#include "../test.h"

test({
  assert_log_includes(exec_stdout.items, "APP_NAME=MyApplication",
                      "Expected embedded config to contain APP_NAME");
  assert_log_includes(exec_stdout.items, "CONFIG_VALID=1",
                      "Expected config validation to pass");
})
