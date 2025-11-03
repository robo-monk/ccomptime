
#include "./test.h"

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

static const char *test_files[] = {
    "tests/simple_comptype",

    "tests/simple_inline_comptime",   "tests/auto_header_injection",

    "tests/top_level_comptime_block", "tests/comptime_type",
    "tests/macro_repeated_arg",
};

static const size_t test_files_len = sizeof(test_files) / sizeof(test_files[0]);

int main(int argc, char **argv) {
  struct timespec t0;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  nob_minimal_log_level = NOB_INFO;
  NOB_GO_REBUILD_URSELF_PLUS(argc, argv,
                             //
                             "tests/test.h"
                             //
  );

  Nob_Procs procs = {0};
  nob_log(INFO, "Compiling tests...");
  for (size_t i = 0; i < test_files_len; i++) {
    size_t mark = nob_temp_save();

    Nob_Cmd compile_cmd = {0};
    nob_cmd_append(&compile_cmd, "./build/ccomptime");
    nob_cmd_append(&compile_cmd, "clang");
    nob_cmd_append(&compile_cmd,
                   nob_temp_sprintf("%s/%s", test_files[i], "main.c"));

    nob_cmd_append(&compile_cmd, "-o",
                   nob_temp_sprintf("%s/%s", test_files[i], "out"));

    nob_cmd_run(&compile_cmd, .async = &procs,
                .stdout_path =
                    nob_temp_sprintf("%s/%s", test_files[i], "comp-stdout.txt"),
                .stderr_path = nob_temp_sprintf("%s/%s", test_files[i],
                                                "comp-stderr.txt"));

    const char *assertion_path =
        nob_temp_sprintf("%s/%s", test_files[i], "assertion.c");

    if (!nob_file_exists(assertion_path)) {
      nob_log(ERROR,
              "Please include a `assertion.c` in the test. Not found for %s",
              test_files[i]);
      continue;
    }

    Nob_Cmd assert_compile_cmd = {0};
    nob_cmd_append(&assert_compile_cmd, "clang");
    nob_cmd_append(&assert_compile_cmd, "-O1");
    nob_cmd_append(&assert_compile_cmd, assertion_path);
    nob_cmd_append(&assert_compile_cmd, "-o",
                   nob_temp_sprintf("%s/%s", test_files[i], "assertion"));
    nob_cmd_run(&assert_compile_cmd, .async = &procs);

    nob_temp_rewind(mark);
  }

  struct {
    size_t count, capacity;
    TestResult *items;
  } assertions = {0};

  int comp_success = 0;
  for (size_t i = 0; i < procs.count; ++i) {
    if (!nob_proc_wait(procs.items[i])) {
      TestResult t = (TestResult){.error = "failed to compile", .success = 0};
      nob_da_append(&assertions, t);
    }
  }
  procs.count = 0;
  // nob_procs_flush(&procs);

  nob_log(INFO, "Running tests...");
  for (size_t i = 0; i < test_files_len; i++) {
    size_t mark = nob_temp_save();
    Nob_Cmd exe_cmd = {0};
    nob_cmd_append(&exe_cmd, nob_temp_sprintf("%s/%s", test_files[i], "out"));
    nob_cmd_run(&exe_cmd, .async = &procs,
                .stdout_path =
                    nob_temp_sprintf("%s/%s", test_files[i], "exec-stdout.txt"),
                .stderr_path = nob_temp_sprintf("%s/%s", test_files[i],
                                                "exec-stderr.txt"));

    nob_temp_rewind(mark);
  }

  for (size_t i = 0; i < procs.count; ++i) {
    if (!nob_proc_wait(procs.items[i])) {
      TestResult t = (TestResult){.error = "failed to run test", .success = 0};
      nob_da_append(&assertions, t);
    }
  }
  procs.count = 0;

  nob_log(INFO, "Running assertions...");
  int passed = 0;
  int failed = 0;

  for (size_t i = 0; i < test_files_len; i++) {
    size_t mark = nob_temp_save();
    Nob_Cmd assert_run_cmd = {0};
    nob_cmd_append(&assert_run_cmd,
                   nob_temp_sprintf("%s/%s", test_files[i], "assertion"));

    int result = nob_cmd_run(&assert_run_cmd, .async = &procs);
    if (!result) {
      nob_log(INFO, "assertion failed for test");
      continue;
    }

    nob_temp_rewind(mark);
  }

  for (size_t i = 0; i < procs.count; ++i) {
    if (!nob_proc_wait(procs.items[i])) {
      TestResult t = (TestResult){.error = "failed to run assertion for test",
                                  .success = 0};
      nob_da_append(&assertions, t);
    }
  }
  procs.count = 0;

  // parse results
  for (size_t i = 0; i < test_files_len; i++) {
    Nob_String_Builder results = {0};
    nob_read_entire_file(
        nob_temp_sprintf("%s/%s", test_files[i], "assertion-results.txt"),
        &results);

    TestResults tr = {0};
    TestResults_parse(&results, &tr);
    nob_da_foreach(TestResult, it, &tr) { nob_da_append(&assertions, *it); }
  }

  // cleanup
  for (size_t i = 0; i < test_files_len; i++) {
    size_t mark = nob_temp_save();
    nob_delete_file(nob_temp_sprintf("%s/%s", test_files[i], "assertion"));
    nob_delete_file(
        nob_temp_sprintf("%s/%s", test_files[i], "assertion-results.txt"));
    nob_delete_file(
        nob_temp_sprintf("%s/%s", test_files[i], "exec-stdout.txt"));
    nob_delete_file(
        nob_temp_sprintf("%s/%s", test_files[i], "exec-stderr.txt"));
    nob_delete_file(
        nob_temp_sprintf("%s/%s", test_files[i], "comp-stderr.txt"));
    nob_delete_file(
        nob_temp_sprintf("%s/%s", test_files[i], "comp-stdout.txt"));
    nob_delete_file(nob_temp_sprintf("%s/%s", test_files[i], "out"));

    nob_temp_rewind(mark);
  }

  double elapsed_ms = ms_since(t0);

  nob_da_foreach(TestResult, it, &assertions) {
    log_test_result(*it);
    if (it->success) {
      passed++;
    } else {
      failed++;
    }
  }

  printf("\n\n================================ TEST SUMMARY "
         "================================\n");
  printf("Total    : %d\n", failed + passed);
  printf(GREEN("Passed   : %d\n"), passed);
  if (failed) {
    printf(RED("Failed   : %d\n"), failed);
  } else {
    printf("Failed   : 0\n");
  }
  printf("Duration : %.2f ms\n\n", elapsed_ms);

  if (failed) {
    return 1;
  }

  return 0;
}
