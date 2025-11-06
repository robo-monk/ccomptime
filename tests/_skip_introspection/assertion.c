#include "../test.h"

test({
  assert_log_includes(exec_stdout.items, "Loaded 10 prime numbers",
                      "Expected to load 10 prime numbers");
})
