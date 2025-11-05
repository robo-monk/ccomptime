#include "../test.h"

test({
  assert_log_includes(comp_stdout.items, "Generated nested struct types at comptime",
                      "Expected nested struct generation at comptime");

  assert_log_includes(comp_stdout.items, "Generated Vector3D type at comptime",
                      "Expected Vector3D type generation at comptime");

  assert_log_includes(exec_stdout.items, "PARTICLE_POS_X=10",
                      "Expected correct particle position X");

  assert_log_includes(exec_stdout.items, "PARTICLE_POS_Y=20",
                      "Expected correct particle position Y");

  assert_log_includes(exec_stdout.items, "PARTICLE_MASS=100",
                      "Expected correct particle mass");

  assert_log_includes(exec_stdout.items, "VECTOR_X=1.0",
                      "Expected correct vector X component");

  assert_log_includes(exec_stdout.items, "VECTOR_Z=3.0",
                      "Expected correct vector Z component");
})
