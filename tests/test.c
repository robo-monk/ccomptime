
#include "./test.h"

#define NOB_IMPLEMENTATION
#include "../nob.h"

static int total_tests_run = 0;

#undef TEST
#define TEST(X)                                                                \
  total_tests_run++;                                                           \
  for (int __i = 0; __i < 1; __i++)                                            \
  X

static inline double ms_since(struct timespec start) {
  struct timespec end;
  clock_gettime(CLOCK_MONOTONIC, &end);
  return (end.tv_sec - start.tv_sec) * 1000.0 +
         (end.tv_nsec - start.tv_nsec) / 1e6;
}

int main(int argc, char **argv) {
  struct timespec t0;
  clock_gettime(CLOCK_MONOTONIC, &t0);

  nob_minimal_log_level = NOB_INFO;
  NOB_GO_REBUILD_URSELF_PLUS(
      argc, argv,
      //
      "tests/test.h", "tests/simple_inline_comptime/test.c",
      "tests/auto_header_injection/test.c",
      "tests/top_level_comptime_block/test.c", "tests/comptime_type/test.c",
      "tests/macro_repeated_arg/test.c", "tests/include_header_macro/test.c"
      //
  );

#include "./auto_header_injection/test.c"
#include "./comptime_type/test.c"
#include "./macro_repeated_arg/test.c"
#include "./polymorphic_result_type/test.c"
#include "./simple_comptype/test.c"
#include "./simple_inline_comptime/test.c"
#include "./top_level_comptime_block/test.c"

  int passed = total_tests_run - total_tests_failed;
  double elapsed_ms = ms_since(t0);

  printf("\n\n================================ TEST SUMMARY "
         "================================\n");
  printf("Total    : %d\n", total_tests_failed + total_tests_succeeded);
  printf(GREEN("Passed   : %d\n"), total_tests_succeeded);
  if (total_tests_failed) {
    printf(RED("Failed   : %d\n"), total_tests_failed);
  } else {
    printf("Failed   : 0\n");
  }
  printf("Duration : %.2f ms\n\n", elapsed_ms);

  if (total_tests_failed) {
    return 1;
  }
  return 0;
}
