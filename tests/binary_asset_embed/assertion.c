#include "../test.h"

test({
  assert_log_includes(exec_stdout.items, "Icon data size:",
                      "Expected correct binary data size");
  assert_log_includes(exec_stdout.items, "BINARY_EMBED_VALID=1",
                      "Expected binary data to be correctly embedded");
})
