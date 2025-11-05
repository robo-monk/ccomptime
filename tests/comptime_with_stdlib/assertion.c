#include "../test.h"

test({
  assert_log_includes(comp_stdout.items, "Computed PI approximation:",
                      "Expected PI computation at comptime");

  assert_log_includes(comp_stdout.items, "Generated enum with 3 person IDs",
                      "Expected enum generation at comptime");

  assert_log_includes(comp_stdout.items, "Compilation timestamp:",
                      "Expected timestamp at comptime");

  assert_log_includes(exec_stdout.items, "PI=3.14",
                      "Expected PI value in output");

  assert_log_includes(exec_stdout.items, "PERSON_ALICE=0",
                      "Expected ALICE enum value");

  assert_log_includes(exec_stdout.items, "PERSON_BOB=1",
                      "Expected BOB enum value");

  assert_log_includes(exec_stdout.items, "PERSON_CHARLIE=2",
                      "Expected CHARLIE enum value");
})
