#include "../test.h"

test({
  assert_log_includes(exec_stdout.items, "Loaded 3 users from CSV",
                      "Expected to load 3 users from CSV");
  assert_log_includes(exec_stdout.items, "User 0: id=1 name=Alice age=25",
                      "Expected Alice with correct data");
  assert_log_includes(exec_stdout.items, "CSV_PARSE_VALID=1",
                      "Expected CSV parsing to generate valid structs");
})
