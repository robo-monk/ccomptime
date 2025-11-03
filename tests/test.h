#ifndef _TEST_H
#define NOB_IMPLEMENTATION
#include "../nob.h"

#define _TEST_H

const static char *CCOMPTIME_BIN = "./build/ccomptime";

#include "../ansi.h"
#include <assert.h>
#include <string.h>

typedef struct {
  int success;
  char *message;
  char *error;
} TestResult;

typedef struct {
  size_t capacity, count;
  TestResult *items;
} TestResults;

static long parse_long_fallback_zero(const char *p, size_t n) {
  if (!p || n == 0)
    return 0;
  char buf[64];
  if (n >= sizeof(buf))
    n = sizeof(buf) - 1;
  memcpy(buf, p, n);
  buf[n] = '\0';
  char *end = NULL;
  errno = 0;
  long v = strtol(buf, &end, 10);
  if (errno != 0 || end == buf)
    return 0;
  return v;
}

#define FIELD_SEP '\x1F' // ASCII Unit Separator (␟)
#define RECORD_SEP '\x1E'

void TestResults_serialize(const TestResults results, String_Builder *out) {
  da_foreach(TestResult, it, &results) {
    sb_appendf(out, "%d%c%s%c%s%c%c", it->success, FIELD_SEP,
               it->error == NULL ? "" : it->error, FIELD_SEP,
               it->message == NULL ? "" : it->message, FIELD_SEP, RECORD_SEP);
  }
}

void TestResults_parse(String_Builder *file, TestResults *tr) {
  enum {
    EXPECT_FIELD_SUCCESS_OR_END,
    EXPECT_FIELD_ERROR,
    EXPECT_FIELD_MESSAGE
  } state = EXPECT_FIELD_SUCCESS_OR_END;

  char *cursor = file->items;

  TestResult building_tr = {0};

  while (*cursor != EOF && (cursor - file->items < file->count)) {
    if (state == EXPECT_FIELD_SUCCESS_OR_END) {
      if (*cursor == '0') {
        building_tr.success = 0;
      } else if (*cursor == '1') {
        building_tr.success = 1;
      } else {
        assert(0);
      }
      cursor++;
      assert(*cursor == FIELD_SEP);
      state = EXPECT_FIELD_ERROR;
    } else {
      assert(state == EXPECT_FIELD_ERROR || state == EXPECT_FIELD_MESSAGE);
      assert(*cursor != FIELD_SEP && *cursor != RECORD_SEP);

      char *end = cursor;
      while (*end != FIELD_SEP) {
        end++;
      }

      assert(*end == FIELD_SEP);

      String_Builder sb = {0};
      sb_append_buf(&sb, cursor, end - cursor);
      sb_append_null(&sb);

      cursor = end;
      if (state == EXPECT_FIELD_ERROR) {
        building_tr.error = sb.items;
        assert(*cursor == FIELD_SEP);
        state = EXPECT_FIELD_MESSAGE;
      } else if (state == EXPECT_FIELD_MESSAGE) {
        building_tr.message = sb.items;
        assert(*cursor == FIELD_SEP);
        nob_da_append(tr, building_tr);
        cursor++;
        if (*cursor == RECORD_SEP) {
          state = EXPECT_FIELD_SUCCESS_OR_END;
        } else {
          break;
        }
      }
    }

    cursor++;
  }
}

#define TEST(x)                                                                \
  void _() {                                                                   \
    for (;;) {                                                                 \
      x                                                                        \
    }                                                                          \
  }

static int total_tests_failed = 0;
static int total_tests_succeeded = 0;

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
    total_tests_succeeded++;
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

#define READ_STDOUTS()                                                         \
  Nob_String_Builder comp_stdout = {0};                                        \
  Nob_String_Builder comp_stderr = {0};                                        \
  Nob_String_Builder exec_stdout = {0};                                        \
  Nob_String_Builder exec_stderr = {0};                                        \
  nob_read_entire_file(r("comp-stdout.txt"), &comp_stdout);                    \
  nob_read_entire_file(r("comp-stderr.txt"), &comp_stderr);                    \
  nob_read_entire_file(r("exec-stdout.txt"), &exec_stdout);                    \
  nob_read_entire_file(r("exec-stderr.txt"), &exec_stderr);                    \
  nob_sb_append_null(&exec_stdout);                                            \
  nob_sb_append_null(&exec_stderr);                                            \
  nob_sb_append_null(&comp_stdout);                                            \
  nob_sb_append_null(&comp_stderr);

#define COMPILE_CASE()                                                         \
  const char *test_dir = get_parent_dir(__FILE__);                             \
  Nob_Cmd cmd = {0};                                                           \
  nob_cmd_append(&cmd, CCOMPTIME_BIN);                                         \
  nob_cmd_append(&cmd, "clang");                                               \
  nob_cc_flags(&cmd);                                                          \
  nob_cmd_append(&cmd, r("main.c"));                                           \
  nob_cmd_append(&cmd, "-o", r("out"));                                        \
  nob_cmd_append(&cmd, "-O0");                                                 \
  nob_cmd_append(&cmd, "-comptime-debug");                                     \
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
                                 .message = (char *)test_dir,                  \
                                 .error = "failed to compile test"});          \
    break;                                                                     \
  };

#define EXEC_CASE()                                                            \
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
                                 .message = (char *)test_dir,                  \
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

#define INIT_TEST_ENV()                                                        \
  READ_STDOUTS();                                                              \
  TestResults results = {0};

#define CLOSE_TEST_ENV()                                                       \
  Nob_String_Builder out = {0};                                                \
  TestResults_serialize(results, &out);                                        \
  nob_write_entire_file(r("assertion-results.txt"), out.items, out.count);     \
  return 0;

#define assert_log_includes(log_file, message, error)                          \
  da_append(&results,                                                          \
            stdout_includes_else_fail(log_file, message, __FILE__, error));

#define test(x)                                                                \
  int main() {                                                                 \
    INIT_TEST_ENV();                                                           \
    if (1)                                                                     \
      x;                                                                       \
    CLOSE_TEST_ENV()                                                           \
  }
