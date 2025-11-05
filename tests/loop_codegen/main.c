#include <stdio.h>

#include "../../ccomptime.h"
#include "main.c.h"

int main(void) {
  // Generate multiple getter functions at compile time
  _Comptime({
    const int num_getters = 5;
    for (int i = 0; i < num_getters; i++) {
      _ComptimeCtx.TopLevel.appendf(
        "static inline int get_value_%d() { return %d; }\n",
        i, i * 10
      );
    }
    printf("Generated %d getter functions at comptime\n", num_getters);
  });

  // Generate a struct with multiple fields using a loop
  _Comptime({
    _ComptimeCtx.TopLevel.appendf("typedef struct {\n");
    for (int i = 0; i < 3; i++) {
      _ComptimeCtx.TopLevel.appendf("  int field_%d;\n", i);
    }
    _ComptimeCtx.TopLevel.appendf("} GeneratedStruct;\n");
    printf("Generated struct with 3 fields at comptime\n");
  });

  // Use the generated functions
  int sum = get_value_0() + get_value_1() + get_value_2() +
            get_value_3() + get_value_4();

  // Use the generated struct
  GeneratedStruct s = {.field_0 = 1, .field_1 = 2, .field_2 = 3};

  printf("SUM=%d\n", sum);
  printf("STRUCT_SUM=%d\n", s.field_0 + s.field_1 + s.field_2);

  return (sum == 100 && s.field_0 + s.field_1 + s.field_2 == 6) ? 0 : 1;
}
