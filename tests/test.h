#ifndef _TEST_H
#define _TEST_H

const static char *CCOMPTIME_BIN = "./build/ccomptime";

#include "../ansi.h"
#include "../nob.h"
#include <assert.h>
#include <string.h>

typedef struct {
  int success;
  char *message;
  char *error;
} TestResult;

#define TEST(x)                                                                \
  void _() {                                                                   \
    for (;;) {                                                                 \
      x                                                                        \
    }                                                                          \
  }

static int total_tests_failed = 0;

const char *get_parent_dir(const char *filepath) {
  static char buffer[1024];
  const char *last_slash = strrchr(filepath, '/');

#ifdef _WIN32
  const char *last_bslash = strrchr(filepath, '\\');
  if (last_bslash && (!last_slash || last_bslash > last_slash))
    last_slash = last_bslash;
#endif

  if (!last_slash) {
    // No slash found → current directory
    return ".";
  }

  size_t len = last_slash - filepath;
  if (len >= sizeof(buffer))
    len = sizeof(buffer) - 1;

  memcpy(buffer, filepath, len);
  buffer[len] = '\0';
  return buffer;
}

const char *resolve(const char *FILE_NAME, const char *path) {
  return nob_temp_sprintf("%s/%s", get_parent_dir(FILE_NAME), path);
}

#define r(relative) resolve(__FILE__, relative)

void log_test_result(TestResult tr) {
  if (tr.success) {
    printf(GREEN("[PASS]") " %s\n", tr.message);
  } else {
    total_tests_failed++;
    printf(RED("[FAIL]") " %s " BOLD("%s\n"), tr.message, tr.error);
  }
}

TestResult stdout_includes_else_fail(char *stdout, char *includes,
                                     char *message, char *error) {
  if (strstr(stdout, includes))
    return (TestResult){.message = message, .error = error, .success = 1};

  return (TestResult){.message = message, .error = error, .success = 0};
}

#define COMPILE_CASE()                                                         \
  const char *test_dir = get_parent_dir(__FILE__);                             \
  nob_log(INFO, "test dir :: %s", test_dir);                                   \
  Nob_Cmd cmd = {0};                                                           \
  nob_cmd_append(&cmd, CCOMPTIME_BIN);                                         \
  nob_cmd_append(&cmd, "clang");                                               \
  nob_cc_flags(&cmd);                                                          \
  nob_cmd_append(&cmd, r("main.c"));                                           \
  nob_cmd_append(&cmd, "-o", r("out"));                                        \
  int result = nob_cmd_run(&cmd, .stdout_path = r("comp-stdout.txt"),          \
                           .stderr_path = r("comp-stderr.txt"));               \
  Nob_String_Builder comp_stdout = {0};                                        \
  Nob_String_Builder comp_stderr = {0};                                        \
  nob_read_entire_file(r("comp-stdout.txt"), &comp_stdout);                    \
  nob_read_entire_file(r("comp-stderr.txt"), &comp_stderr);                    \
  nob_sb_append_null(&comp_stdout);                                            \
  nob_sb_append_null(&comp_stderr);                                            \
  if (!result) {                                                               \
    fprintf(stderr, RED("\n\n%s\n\n"), comp_stderr.items);                     \
    log_test_result((TestResult){.success = false,                             \
                                 .message = test_dir,                          \
                                 .error = "failed to compile test"});          \
    break;                                                                     \
  };                                                                           \
  int COMPILED = 1;

#define EXEC_CASE()                                                            \
  assert(COMPILED && "must compile first use COMPILE_CASE");                   \
  nob_cmd_append(&cmd, r("out"));                                              \
  int exec_result = nob_cmd_run(&cmd, .stdout_path = r("exec-stdout.txt"),     \
                                .stderr_path = r("exec-stderr.txt"));          \
  Nob_String_Builder exec_stdout = {0};                                        \
  Nob_String_Builder exec_stderr = {0};                                        \
  nob_read_entire_file(r("exec-stdout.txt"), &exec_stdout);                    \
  nob_read_entire_file(r("exec-stderr.txt"), &exec_stderr);                    \
  nob_sb_append_null(&exec_stdout);                                            \
  nob_sb_append_null(&exec_stderr);                                            \
  if (!exec_result) {                                                          \
    fprintf(stdout, GRAY("\n\n%s"), exec_stdout.items);                        \
    fprintf(stderr, RED("\n\n%s\n\n"), exec_stderr.items);                     \
    log_test_result((TestResult){.success = false,                             \
                                 .message = test_dir,                          \
                                 .error = "failed to exec test"});             \
    break;                                                                     \
  };

#endif

// #define _CONCAT_(x, y) x##y
// #define CONCAT(x, y) _CONCAT_(x, y)

// #ifndef COMPILE_CALLBACK
// #define COMPILE_CALLBACK void callback(Nob_Cmd cmd)
// #endif

// #define _EVAL_FN_NAME CONCAT(eval, __COUNTER__)
// #define _RUN_FN_NAME CONCAT(run, __COUNTER__)
// #define _EVAL CONCAT(run, __COUNTER__)
// #define _TEST_CASE_FN_NAME CONCAT(test_case, __COUNTER__)

// #ifndef EVAL_CALLBACK
// #define EVAL_CALLBACK \
//   TestResult _EVAL_FN_NAME(char *exec_stdout, char *exec_stderr)
// #endif

// #ifndef TEST_CASE
// #define TEST_CASE char *_TEST_CASE_FN_NAME()
// #endif
