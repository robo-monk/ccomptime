#include <stdio.h>

#include "../../ccomptime.h"
#include "main.c.h"

int main(void) {
  // First comptime block: Generate helper functions and compute values
  _Comptime({
    // Generate helper functions
    _ComptimeCtx.TopLevel.appendf(
      "static int add_numbers(int a, int b) {\n"
      "  return a + b;\n"
      "}\n\n"
    );

    _ComptimeCtx.TopLevel.appendf(
      "static int multiply_numbers(int a, int b) {\n"
      "  return a * b;\n"
      "}\n\n"
    );

    // Compute values at compile time and generate constants
    int sum = 10 + 15;  // Manual computation since we can't call generated functions yet
    int product = 6 * 7;
    int combined = sum + product;

    _ComptimeCtx.TopLevel.appendf("static const int precomputed_sum = %d;\n", sum);
    _ComptimeCtx.TopLevel.appendf("static const int precomputed_product = %d;\n", product);

    // Generate struct with computed values
    _ComptimeCtx.TopLevel.appendf(
      "typedef struct {\n"
      "  int sum_field;\n"
      "  int product_field;\n"
      "  int combined;\n"
      "} ComputedData;\n\n"
    );

    _ComptimeCtx.TopLevel.appendf(
      "static const ComputedData data = {%d, %d, %d};\n",
      sum, product, combined
    );
  });

  printf("Sum: %d\n", precomputed_sum);
  printf("Product: %d\n", precomputed_product);

  printf("Data.sum_field: %d\n", data.sum_field);
  printf("Data.product_field: %d\n", data.product_field);
  printf("Data.combined: %d\n", data.combined);

  // Compute at runtime to verify
  int runtime_result = add_numbers(10, 15) + multiply_numbers(6, 7);

  // Nested inline comptime
  int nested_value = _Comptime({
    int base = 5;
    int doubled = base * 2;
    _ComptimeCtx.Inline.appendf("%d", doubled);
  });

  printf("Nested inline value: %d\n", nested_value);

  // Verify all nested blocks worked correctly
  if (precomputed_sum == 25 &&
      precomputed_product == 42 &&
      data.combined == 67 &&
      runtime_result == 67 &&
      nested_value == 10) {
    printf("NESTED_COMPTIME_VALID=1\n");
    return 0;
  }

  printf("NESTED_COMPTIME_VALID=0\n");
  return 1;
}
