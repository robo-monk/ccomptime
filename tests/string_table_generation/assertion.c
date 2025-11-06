#include "../test.h"

test({
  assert_log_includes(exec_stdout.items, "Code 0: ERR_SUCCESS",
                      "Expected enum value 0 to map to ERR_SUCCESS");
  assert_log_includes(exec_stdout.items, "Code 4: ERR_TIMEOUT",
                      "Expected enum value 4 to map to ERR_TIMEOUT");
  assert_log_includes(exec_stdout.items, "STRING_TABLE_VALID=1",
                      "Expected string table lookup to work correctly");
})
