#include "ansi.h"
#include <stdint.h>
#include <string.h>
// #define NOB_IMPLEMENTATION
#include "nob.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

static bool has_suffix(const char *s, const char *suf) {
  size_t ns = strlen(s), nf = strlen(suf);
  return ns >= nf && memcmp(s + ns - nf, suf, nf) == 0;
}
static bool has_prefix(const char *s, const char *pre) {
  size_t np = strlen(pre);
  return strncmp(s, pre, np) == 0;
}

void print_usage(int argc, char **argv) {
  (void)argc;

  printf(BOLD("-- ccomptime™ v0.0.1 --") "\nUSAGE"
                                         ": %s [clang-like "
                                         "args] file.c ...\n",
         argv[0]);
}
typedef enum {
  CliComptimeFlag_Debug = 1u << 0,
  CliComptimeFlag_KeepInter = 1u << 1,
  CliComptimeFlag_NoLogs = 1u << 2,
} CliComptimeFlag;
typedef struct {
  int *items;
  size_t count, capacity;
} ArgIndexList;

typedef enum {
  Compiler_Invalid = -1,
  Compiler_CLANG,
  Compiler_GCC,
  Compile_TCC,
  _Compiler_Len
} Compiler;

static const char *CCompiler_Map[] = {"clang", "gcc", "tcc"};

static_assert(NOB_ARRAY_LEN(CCompiler_Map) == _Compiler_Len,
              "supported_compilers length must match CCompiler enum");

static Compiler parse_compiler_name(const char *name) {
  for (size_t i = 0; i < _Compiler_Len; i++) {
    if (strcmp(name, CCompiler_Map[i]) == 0) {
      return (Compiler)i;
    }
  }

  return Compiler_Invalid;
}

typedef struct {
  int argc;
  char **argv;
  Compiler compiler;
  ArgIndexList input_files;
  ArgIndexList output_files;
  ArgIndexList flags;
  u_int32_t cct_flags;
} CliArgs;

typedef struct {
  String_Builder *raw_source;
  String_Builder *preprocessed_source;

  const char *input_path;
  const char *runner_exepath;
  const char *runner_defs_path;
  const char *runner_main_path;
  const char *runner_next_nodes_path;

  const char *final_out_path;
  const char *gen_header_path;
  const char *comptime_safe_path;
  CliArgs *parsed_argv;
} Context;

static char *leaky_sprintf(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));

char *leaky_sprintf(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int n = vsnprintf(NULL, 0, fmt, args);
  va_end(args);

  NOB_ASSERT(n >= 0);
  char *result = (char *)malloc(n + 1);
  NOB_ASSERT(result != NULL && "Extend the size of the temporary allocator");
  va_start(args, fmt);
  vsnprintf(result, n + 1, fmt, args);
  va_end(args);
  return result;
}

static void Context_fill_paths(Context *ctx, const char *original_source) {
  ctx->runner_defs_path = leaky_sprintf("%sc-runner-defs.c", original_source);
  ctx->runner_main_path = leaky_sprintf("%sc-runner-main.c", original_source);
  ctx->runner_next_nodes_path = leaky_sprintf("%sc-runner-next-nodes.c", original_source);
  ctx->comptime_safe_path = leaky_sprintf("%somptime_safe.c", original_source);

#ifdef _WIN32
  ctx->runner_exepath = leaky_sprintf("%sct-runner.exe", original_source);
#else
  ctx->runner_exepath = leaky_sprintf("%sct-runner", original_source);
#endif
  ctx->final_out_path = leaky_sprintf("%sct-final.c", original_source);
  ctx->gen_header_path = leaky_sprintf("%s.h", original_source);
}

#define TOOL_FLAG_PREFIX "-comptime"

CliArgs CliArgs_parse(int argc, char **argv) {
  CliArgs parsed_argv = {
      .argv = argv,
      .argc = argc,
      .input_files = {0},
      .output_files = {0},
      .cct_flags = 0,
      .flags = {0},
  };

  parsed_argv.compiler = parse_compiler_name(argv[1]);

  if (parsed_argv.compiler == Compiler_Invalid) {
    print_usage(argc, argv);
    fprintf(stderr, RED("Error: "));
    fprintf(stderr, BOLD("First arg must be a supported compiler (one of: "));
    for (size_t i = 0; i < _Compiler_Len; i++) {
      fprintf(stderr, BOLD("%s"), CCompiler_Map[i]);
      if (i < _Compiler_Len - 1) {
        fprintf(stderr, BOLD(", "));
      }
    }
    fprintf(stderr, BOLD(")\n"));

    exit(EXIT_FAILURE);
  }

  // for (int i = 2; i < argc; i++) {
  int i = 2;
  while (i < argc) {
    if (argv[i][0] == '-') {
      if (argv[i][1] == 'o' && strlen(argv[i]) == 2) {
        da_append(&parsed_argv.output_files, i);
        da_append(&parsed_argv.output_files, i + 1);
        i++;
      } else if (has_prefix(argv[i], TOOL_FLAG_PREFIX)) {
        const char *flag = argv[i] + (NOB_ARRAY_LEN(TOOL_FLAG_PREFIX) - 1);

        nob_log(INFO, "Found -cct flag (%s) (%d)", flag,
                strcmp(flag, "-keep-inter"));

        if (strcmp(flag, "-debug") == 0) {
          parsed_argv.cct_flags |= CliComptimeFlag_Debug;
        } else if (strcmp(flag, "-no-logs") == 0) {
          parsed_argv.cct_flags |= CliComptimeFlag_NoLogs;
        } else if (strcmp(flag, "-keep-inter") == 0) {
          parsed_argv.cct_flags |= CliComptimeFlag_KeepInter;
        } else {
          // nob_log(ERROR, "Unknown -comptime flag: %s", flag);
          nob_log(ERROR, "Unknown -comptime flag: %s", flag);
          exit(1);
        }
      } else {
        da_append(&parsed_argv.flags, i);
      }

    } else if (has_suffix(argv[i], ".c") || has_suffix(argv[i], ".C")) {
      nob_log(INFO, "Detected input file: %s", argv[i]);
      da_append(&parsed_argv.input_files, i);
    } else {
      nob_log(INFO, "Detected other flag: %s", argv[i]);
      da_append(&parsed_argv.flags, i);
    }

    i++;
  }

  return parsed_argv;
}

static const char *Parsed_Argv_compiler_name(CliArgs *pa) {
  return CCompiler_Map[pa->compiler];
}

void cmd_append_inputs_except(CliArgs *pa, const char *skip_input, Cmd *cmd) {
  nob_da_foreach(int, index, &pa->input_files) {
    const char *f = pa->argv[*index];
    if (strcmp(f, skip_input) == 0) {
      nob_log(INFO, "~~ skipping %s", f);
      continue;
    }
    nob_log(INFO, "~~ append input %s", f);
    nob_cmd_append(cmd, f);
  }
}

void cmd_append_arg_indeces(CliArgs *pa, ArgIndexList *pointers, Cmd *cmd) {
  if (!pointers->items)
    return;
  nob_da_foreach(int, index, pointers) {
    const char *f = pa->argv[*index];
    nob_cmd_append(cmd, f);
  }
}

int cli(int argc, char **argv, CliArgs *parsed_argv) {
  nob_minimal_log_level = ERROR;
  if (argc < 2) {
    print_usage(argc, argv);
    return 1;
  }

  nob_log(INFO, "Parsing arguments");
  *parsed_argv = CliArgs_parse(argc, argv);

  if (parsed_argv->cct_flags & CliComptimeFlag_NoLogs) {
    nob_minimal_log_level = NO_LOGS;
  } else if (parsed_argv->cct_flags & CliComptimeFlag_Debug) {
    nob_minimal_log_level = NOB_VERBOSE;
  }

  return 0;
}
