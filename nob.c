#define NOB_IMPLEMENTATION
#include "nob.h"

#define BUILD_DIR "build/"
#define TS_RT_SRC "deps/tree_sitter/src/lib.c"
#define TS_RT_INC "deps/tree_sitter/include"
#define GRAMMAR_PARSER "deps/tree_sitter/parser.c"
#define LIB_RT_A BUILD_DIR "libtree-sitter.a"
#define LIB_GRAMMAR_A BUILD_DIR "libts-c-grammar.a"
#define APP_OUT BUILD_DIR "ccomptime"
#define APP_SRCS                                                               \
  (const char *[]) {                                                           \
    "main.c", "comptime_common.c", "macro_expansion.c", "tree_passes.c"        \
  }
#define APP_SRCS_COUNT 4

static bool build_tree_sitter_runtime(void) {
  // build/libtree-sitter.a <= lib/src/lib.c
  Nob_Cmd cmd = {0};

  // Compile object
  const char *obj = BUILD_DIR "tree_sitter_runtime.o";
  nob_log(INFO, "Compiling runtime: %s", TS_RT_SRC);
  nob_cc(&cmd);
  nob_cmd_append(&cmd, "-O3", "-c", TS_RT_SRC, "-I", TS_RT_INC, "-o", obj);
  if (!nob_cmd_run(&cmd))
    return false;

  // Archive .a
  nob_log(INFO, "Archiving %s", LIB_RT_A);
  nob_cmd_append(&cmd, "ar", "rcs", LIB_RT_A, obj);
  if (!nob_cmd_run(&cmd))
    return false;

  return true;
}

static bool build_grammar_archive(void) {
  // build/libts-c-grammar.a <= parser.c [+ scanner.c if exists]
  Nob_Cmd cmd = {0};
  const char *obj_parser = BUILD_DIR "c_parser.o";

  int has_parser = file_exists(GRAMMAR_PARSER);
  if (has_parser <= 0) {
    nob_log(ERROR, "Missing %s (grammar parser.c).", GRAMMAR_PARSER);
    return false;
  }

  nob_log(INFO, "Compiling grammar: %s", GRAMMAR_PARSER);
  nob_cc(&cmd);
  nob_cmd_append(&cmd, "-O3", "-c", GRAMMAR_PARSER, "-I", TS_RT_INC, "-o",
                 obj_parser);
  if (!nob_cmd_run(&cmd))
    return false;

  // Archive .a
  nob_log(INFO, "Archiving %s", LIB_GRAMMAR_A);
  nob_cmd_append(&cmd, "ar", "rcs", LIB_GRAMMAR_A, obj_parser);
  if (!nob_cmd_run(&cmd))
    return false;

  return true;
}

static bool link_app(const char *extra_cflags, int debug) {
  Nob_Cmd cmd = {0};
  nob_log(INFO, "Linking %s", APP_OUT);

  // Compiler + common flags
  nob_cc(&cmd);
  nob_cc_flags(&cmd);

  nob_cmd_append(&cmd, "-std=c11", "-Wall", "-Wextra");

  // const bool DEVELOPMENT = true;
  if (debug) {
    nob_cmd_append(&cmd, "-fsanitize=address,undefined", "-g");
    nob_cmd_append(&cmd, "-O0");
  } else {
    nob_cmd_append(&cmd, "-O3");
  }

  if (extra_cflags && *extra_cflags)
    nob_cmd_append(&cmd, extra_cflags);

  // Includes
  nob_cmd_append(&cmd, "-I", TS_RT_INC);

  // Output
  nob_cmd_append(&cmd, "-o", APP_OUT);

  // Inputs: app + static libs
  const char **app_srcs = APP_SRCS;
  for (size_t i = 0; i < APP_SRCS_COUNT; i++) {
    nob_cmd_append(&cmd, app_srcs[i]);
  }
  nob_cmd_append(&cmd, LIB_RT_A, LIB_GRAMMAR_A);

  return nob_cmd_run(&cmd);
}

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
  // Rebuild self if build.c, nob.h changed
  NOB_GO_REBUILD_URSELF_PLUS(argc, argv, "nob.c");

  int test = 0;
  if (argc > 1 && strcmp(argv[1], "test") == 0) {
    test = 1;
    nob_log(INFO, "Running in test mode");
  }

  // Simple CLI: ./nob [clean]
  if (argc > 1 && strcmp(argv[1], "clean") == 0) {
    nob_log(INFO, "Cleaning %s", BUILD_DIR);
    // nob_remove_dir_recursive(BUILD_DIR);
    Cmd cmd = {0};
    nob_cmd_append(&cmd, "rm", "-rf", BUILD_DIR);
    if (!nob_cmd_run(&cmd))
      return 1;
    return 0;
  }

  if (!nob_mkdir_if_not_exists(BUILD_DIR))
    return 1;

  // Build libtree-sitter.a if missing
  int has_rt = file_exists(LIB_RT_A);
  if (has_rt < 0)
    return 1;
  if (has_rt == 0) {
    nob_log(INFO, "%s not found; building…", LIB_RT_A);
    if (!build_tree_sitter_runtime())
      return 1;
  } else {
    nob_log(INFO, "Found %s", LIB_RT_A);
  }

  // Build grammar .a if missing
  int has_grammar = file_exists(LIB_GRAMMAR_A);
  if (has_grammar < 0)
    return 1;
  if (has_grammar == 0) {
    nob_log(INFO, "%s not found; building…", LIB_GRAMMAR_A);
    if (!build_grammar_archive())
      return 1;
  } else {
    nob_log(INFO, "Found %s", LIB_GRAMMAR_A);
  }

  // Link app
  if (!link_app(NULL, test))
    return 1;

  nob_log(INFO, "Built %s", APP_OUT);

  if (test) {
    nob_log(INFO, "Running tests…");
    nob_log(INFO, "Compiling test runner");
    // compiling test runner
    Nob_Cmd cmd = {0};
    nob_cc(&cmd);
    nob_cc_flags(&cmd);
    nob_cmd_append(&cmd, "tests/test.c", "-o", BUILD_DIR "test");
    cmd_run(&cmd);

    // now run the test runner
    nob_log(INFO, "Running test runner");
    nob_cmd_append(&cmd, BUILD_DIR "test");
    cmd_run(&cmd);
  }
  return 0;
}
