#include "./test.h"
#define NOB_IMPLEMENTATION
#include "../nob.h"

#undef TEST
#define TEST(X)                                                                \
  for (int __i = 0; __i < 1; __i++)                                            \
  X

int main(int argc, char **argv) {
  nob_minimal_log_level = NOB_INFO;
  NOB_GO_REBUILD_URSELF_PLUS(argc, argv,
                             //
                             "tests/simple_inline_comptime/test.c",
                             "tests/top_level_comptime_block/test.c"
                             //
  );
#include "./simple_inline_comptime/test.c"
#include "./top_level_comptime_block/test.c"
  // #include "./top_level_comptime_block/test.c"
}
