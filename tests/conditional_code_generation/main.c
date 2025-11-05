#include <stdio.h>
#include <string.h>
#include <time.h>

#include "../../ccomptime.h"
#include "main.c.h"

int main(void) {
  _Comptime({
    // Check if a feature file exists to enable optional features
    FILE *feature_file = fopen("tests/conditional_code_generation/enable_debug.flag", "r");
    int debug_enabled = (feature_file != NULL);
    if (feature_file) fclose(feature_file);

    // Generate debug functions only if flag exists
    if (debug_enabled) {
      _ComptimeCtx.TopLevel.appendf(
        "static void debug_log(const char* msg) {\n"
        "  printf(\"[DEBUG] %%s\\n\", msg);\n"
        "}\n"
      );
      _ComptimeCtx.TopLevel.appendf("#define DEBUG_ENABLED 1\n");
    } else {
      _ComptimeCtx.TopLevel.appendf(
        "static void debug_log(const char* msg) { (void)msg; }\n"
      );
      _ComptimeCtx.TopLevel.appendf("#define DEBUG_ENABLED 0\n");
    }

    // Generate different code based on pointer size
    if (sizeof(void*) == 8) {
      _ComptimeCtx.TopLevel.appendf("#define PLATFORM_BITS 64\n");
      _ComptimeCtx.TopLevel.appendf("static const char* platform_name = \"64-bit\";\n");
    } else {
      _ComptimeCtx.TopLevel.appendf("#define PLATFORM_BITS 32\n");
      _ComptimeCtx.TopLevel.appendf("static const char* platform_name = \"32-bit\";\n");
    }

    // Generate optimized function based on compile-time computation
    int threshold = 100;
    if (threshold > 50) {
      _ComptimeCtx.TopLevel.appendf(
        "static int compute_value(int x) {\n"
        "  return x * 2; // Optimized for high threshold\n"
        "}\n"
      );
    } else {
      _ComptimeCtx.TopLevel.appendf(
        "static int compute_value(int x) {\n"
        "  return x; // Simple for low threshold\n"
        "}\n"
      );
    }
  });
  debug_log("Starting program");

  printf("Platform: %s (%d-bit)\n", platform_name, PLATFORM_BITS);
  printf("Debug enabled: %d\n", DEBUG_ENABLED);

  int result = compute_value(21);
  printf("Computed value: %d\n", result);

  // Verify conditional compilation worked
  if (result == 42 && PLATFORM_BITS == 64) {
    printf("CONDITIONAL_GEN_VALID=1\n");
    return 0;
  }

  printf("CONDITIONAL_GEN_VALID=0\n");
  return 1;
}
