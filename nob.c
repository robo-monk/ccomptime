// nob.c — builds the ccomptime-clang driver
#define NOB_IMPLEMENTATION
#include "nob.h"

int main(int argc, char **argv) {
  NOB_GO_REBUILD_URSELF(argc, argv);

  if (!nob_mkdir_if_not_exists("build"))
    return 1;

  Nob_Cmd cmd = {0};
  nob_cmd_append(&cmd, "clang");
  nob_cc_flags(&cmd);
  nob_cc_output(&cmd, "build/ccomptime");
#if defined(_WIN32) && !defined(__clang__)
  nob_cmd_append(&cmd, "/O2");
#else
  nob_cmd_append(&cmd, "-O3");
#endif
  nob_cc_inputs(&cmd, "ccomptime.c");
  if (!nob_cmd_run(&cmd))
    return 1;

  nob_log(NOB_INFO, "Built build/ccomptime");
  return 0;
}
