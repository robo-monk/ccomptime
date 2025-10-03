#include <stdint.h>
#include <string.h>
#define NOB_STRIP_PREFIX
#define NOB_IMPLEMENTATION
#include "nob.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#define BLOCK_ID_PREFIX "CCT_STMT_$"
#define BLOCK_FUNC_PREFIX "void " BLOCK_ID_PREFIX

#define GREEN(str) "\033[0;32m" str "\033[0m"
#define RED(str) "\033[0;31m" str "\033[0m"
#define YELLOW(str) "\033[0;33m" str "\033[0m"
#define GRAY(str) "\033[0;90m" str "\033[0m"

#define log_info(x, ...) nob_log(INFO, GRAY(x), ##__VA_ARGS__)
#define log_success(x, ...) nob_log(INFO, GREEN(x), ##__VA_ARGS__)
#define log_error(x, ...) nob_log(ERROR, RED(x), ##__VA_ARGS__)
#define log_warning(x, ...) nob_log(WARNING, YELLOW(x), ##__VA_ARGS__)

static bool has_suffix(const char *s, const char *suf) {
  size_t ns = strlen(s), nf = strlen(suf);
  return ns >= nf && memcmp(s + ns - nf, suf, nf) == 0;
}
static bool has_prefix(const char *s, const char *pre) {
  size_t np = strlen(pre);
  return strncmp(s, pre, np) == 0;
}

typedef enum {
  CtBlockKind_Inline,
  CtBlockKind_Function,

} CtBlockKind;

typedef struct {
  const char *beg, *end;
  size_t beg_index, end_index;
  const char *block_id;
  size_t block_index;
  CtBlockKind kind;
} CtBlock;

typedef struct {
  size_t count, capacity;
  CtBlock *items;
} CtBlocks;

typedef struct {
  String_Builder source;
} FileBuffer;

typedef struct {
  int *items;
  size_t count, capacity;
} ArgIndexList;

typedef enum {
  Compiler_Invalid = -1,
  Compiler_CLANG,
  Compiler_GCC,
  _Compiler_Len
} Compiler;

static const char *CCompiler_Map[] = {"clang", "gcc"};

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

typedef enum {
  CliComptimeFlag_Debug = 1u << 0,
  CliComptimeFlag_KeepInter = 1u << 1,
  CliComptimeFlag_NoLogs = 1u << 2,
} CliComptimeFlag;

typedef struct {
  int argc;
  char **argv;
  Compiler compiler;
  ArgIndexList input_files;
  ArgIndexList flags;
  u_int32_t cct_flags;
} CliArgs;

#define TOOL_FLAG_PREFIX "-comptime"

CliArgs CliArgs_parse(int argc, char **argv) {
  CliArgs parsed_argv = {
      .argv = argv,
      .argc = argc,
      .input_files = {0},
      .cct_flags = 0,
      .flags = {0},
  };

  parsed_argv.compiler = parse_compiler_name(argv[1]);
  if (parsed_argv.compiler == Compiler_Invalid) {
    fprintf(stderr, "[ERROR] First arg must be a supported compiler (one of: ");
    for (size_t i = 0; i < _Compiler_Len; i++) {
      fprintf(stderr, "%s", CCompiler_Map[i]);
      if (i < _Compiler_Len - 1) {
        fprintf(stderr, ", ");
      }
    }
    fprintf(stderr, ")\n");
    exit(EXIT_FAILURE);
  }

  for (int i = 2; i < argc; i++) {
    if (argv[i][0] == '-') {

      if (has_prefix(argv[i], TOOL_FLAG_PREFIX)) {
        const char *flag = argv[i] + (NOB_ARRAY_LEN(TOOL_FLAG_PREFIX) - 1);

        log_info("Found -cct flag (%s) (%d)", flag,
                 strcmp(flag, "-keep-inter"));

        if (strcmp(flag, "-debug") == 0) {
          parsed_argv.cct_flags |= CliComptimeFlag_Debug;
        } else if (strcmp(flag, "-no-logs") == 0) {
          parsed_argv.cct_flags |= CliComptimeFlag_NoLogs;
        } else if (strcmp(flag, "-keep-inter") == 0) {
          parsed_argv.cct_flags |= CliComptimeFlag_KeepInter;
        } else {
          // nob_log(ERROR, "Unknown -comptime flag: %s", flag);
          log_error("Unknown -comptime flag: %s", flag);
          exit(1);
        }
      } else {
        da_append(&parsed_argv.flags, i);
      }

      continue;
    }

    if (has_suffix(argv[i], ".c") || has_suffix(argv[i], ".C")) {
      log_info("Detected input file: %s", argv[i]);
      da_append(&parsed_argv.input_files, i);
    } else {
      log_info("Detected other flag: %s", argv[i]);
      da_append(&parsed_argv.flags, i);
    }
  }

  return parsed_argv;
}

static const char *Parsed_Argv_compiler_name(CliArgs *pa) {
  return CCompiler_Map[pa->compiler];
}

// Reads in C code until the matching closing brace is found.
// Escapes braces in strings, character literals, and comments.
#define PEEK2(ptr, c1, c2) ((ptr)[0] == (c1) && (ptr)[1] == (c2))

typedef enum {
  NORMAL,
  IN_STRING,
  IN_CHAR,
  IN_LINE_COMMENT,
  IN_BLOCK_COMMENT
} ParseState;

const char *scan_balanced_brace(const char *ccode) {
  assert(ccode[0] == '{');

  const char *ptr = ccode + 1; // Start after the opening brace
  int depth = 1;               // We've seen one opening brace
  ParseState state = NORMAL;

  while (*ptr != '\0' && depth > 0) {
    switch (state) {
    case IN_STRING:
    case IN_CHAR:
      if (*ptr == '\\' && *(ptr + 1) != '\0') {
        ptr++; // Skip escaped character
      } else if (*ptr == (state == IN_STRING ? '"' : '\'')) {
        state = NORMAL;
      }
      ptr++;
      break;

    case IN_LINE_COMMENT:
      if (*ptr == '\n') {
        state = NORMAL;
      }
      ptr++;
      break;

    case IN_BLOCK_COMMENT:
      if (PEEK2(ptr, '*', '/')) {
        state = NORMAL;
        ptr += 2;
      } else {
        ptr++;
      }
      break;

    case NORMAL:
      if (PEEK2(ptr, '/', '/')) {
        state = IN_LINE_COMMENT;
        ptr += 2;
      } else if (PEEK2(ptr, '/', '*')) {
        state = IN_BLOCK_COMMENT;
        ptr += 2;
      } else if (*ptr == '"') {
        state = IN_STRING;
        ptr++;
      } else if (*ptr == '\'') {
        state = IN_CHAR;
        ptr++;
      } else if (*ptr == '{') {
        depth++;
        ptr++;
      } else if (*ptr == '}') {
        depth--;
        if (depth == 0) {
          return ptr; // Return pointer to matching brace
        }
        ptr++;
      } else {
        ptr++;
      }
      break;
    }
  }

  assert(0 && "Unmatched braces in input C code");
  return NULL; // No matching brace found
}

const char *parse_stringified_c_code(const char *line,
                                     Nob_String_Builder *out) {
  assert(line[0] == '"');
  log_info("parsing line %s", line);

  const char *ptr = line + 1; // Start after the opening brace
  while (*ptr != '\0') {
    if (*ptr == '\\' && *(ptr + 1) != '\0') {
      ptr++; // Skip escaped character
    } else if (*ptr == '"') {
      break;
    }

    nob_da_append(out, *ptr);
    ptr++;
  }
  assert(*ptr == '"');
  return ptr;
}

void cmd_append_inputs_except(CliArgs *pa, const char *skip_input, Cmd *cmd) {
  nob_da_foreach(int, index, &pa->input_files) {
    const char *f = pa->argv[*index];
    if (strcmp(f, skip_input) == 0) {
      log_info("~~ skipping %s", f);
      continue;
    }
    log_info("~~ append input %s", f);
    nob_cmd_append(cmd, f);
  }
}

void cmd_append_arg_indeces(CliArgs *pa, ArgIndexList *pointers, Cmd *cmd) {
  nob_da_foreach(int, index, pointers) {
    const char *f = pa->argv[*index];
    nob_cmd_append(cmd, f);
  }
}

typedef struct {
  FileBuffer *raw_source;
  FileBuffer *preprocessed_source;

  const char *input_path;
  const char *preprocessed_path;
  const char *runner_templ_path;

  const char *runner_defs_path;
  const char *runner_main_path;

  const char *runner_exepath;
  const char *vals_path;
  const char *final_out_path;
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
  ctx->preprocessed_path =
      leaky_sprintf("%sct-preprocessed.i", original_source);
  ctx->runner_templ_path = leaky_sprintf("%sct-runner.c", original_source);

  ctx->runner_defs_path = leaky_sprintf("%sc-runner-defs.c", original_source);
  ctx->runner_main_path = leaky_sprintf("%sc-runner-main.c", original_source);

  ctx->runner_templ_path = leaky_sprintf("%sct-runner.c", original_source);
#ifdef _WIN32
  ctx->runner_exepath = leaky_sprintf("%sct-runner.exe", original_source);
#else
  ctx->runner_exepath = leaky_sprintf("%sct-runner", original_source);
#endif
  ctx->vals_path = leaky_sprintf("%sct-vals.cct_vals", original_source);
  ctx->final_out_path = leaky_sprintf("%sct-final.i", original_source);
}

void compile_and_run_runner(Context *ctx) {
  assert(ctx->runner_templ_path);
  static Nob_Cmd cmd = {0};

  nob_cmd_append(&cmd, Parsed_Argv_compiler_name(ctx->parsed_argv));
  cmd_append_arg_indeces(ctx->parsed_argv, &ctx->parsed_argv->flags,
                         &cmd);
  nob_cmd_append(&cmd, "-O0");

  cmd_append_inputs_except(ctx->parsed_argv, ctx->input_path, &cmd);
  nob_cmd_append(&cmd, ctx->runner_templ_path);
  nob_cmd_append(&cmd, "-Dmain=__cct_user_main", "-Dcct_runner_main=main");

  if (!(ctx->parsed_argv->cct_flags & CliComptimeFlag_Debug)) {
    nob_cmd_append(&cmd, "-w");
  } else {
    nob_cmd_append(&cmd, "-g", "-fsanitize=address", "-fno-omit-frame-pointer");
  }

  nob_cmd_append(&cmd, "-o", ctx->runner_exepath);

  if (!nob_cmd_run(&cmd)) {
    nob_log(ERROR, "failed to compile runner %s", ctx->runner_templ_path);
    exit(1);
  }

  nob_cmd_append(&cmd, ctx->runner_exepath);

  log_info("Running runner %s", ctx->runner_exepath);
  if (!nob_cmd_run(&cmd)) {
    nob_log(ERROR, "failed to run runner %s", ctx->runner_templ_path);
    exit(1);
  }
}

void emit_runner_tu(Context *ctx, CtBlocks *comptime_blocks) {

  Nob_String_Builder sb = {0};

  sb_appendf(&sb, "\n#define COMPTIME\n");
  sb_appendf(&sb, "\n// -- SOURCE FILE START -- \n");
  sb_appendf(&sb, "\n#define main __cct_user_main\n");
  sb_append_buf(&sb, ctx->raw_source->source.items,
                ctx->raw_source->source.count - 1);
  sb_appendf(&sb, "\n#undef main\n");
  sb_appendf(&sb, "\n// -- SOURCE FILE END -- \n\n");
  // Function to write escaped strings
  sb_append_cstr(
      &sb,
      "static void cct_write_escaped(FILE* f, const char* s){\n"
      "    if(!s) return;\n"
      "    for (const unsigned char* p=(const unsigned char*)s; *p; ++p){\n"
      "        unsigned char c=*p;\n"
      "        if(c=='\\\\' || c=='\"'){ fputc('\\\\',f); fputc(c,f); }\n"
      "        else if(c=='\\n') fputs(\"\\\\n\",f);\n"
      "        else if(c=='\\r') fputs(\"\\\\r\",f);\n"
      "        else if(c=='\\t') fputs(\"\\\\t\",f);\n"
      "        else fputc(c,f);\n"
      "    }\n"
      "}\n");

  sb_append_cstr(&sb, "\nFILE* __cct_file;\n");
  sb_appendf(&sb, "\nchar* _cct_pp_filename = \"%s\";\n",
             ctx->preprocessed_path);
  sb_appendf(&sb, "\nNob_String_Builder _cct_pp_file = {0};\n");
  // sb_appendfkx

  // Fixed macro that uses dynamic allocation and escaping
  sb_append_cstr(&sb, "#define DEFINE_LABELED_WRITER(name, label_str) \\\n"
                      "int name(const char *fmt, ...) { \\\n"
                      "    va_list args; \\\n"
                      "    int size; \\\n"
                      "    char* buffer; \\\n"
                      "    \\\n"
                      "    /* Get required size */ \\\n"
                      "    va_start(args, fmt); \\\n"
                      "    size = vsnprintf(NULL, 0, fmt, args); \\\n"
                      "    va_end(args); \\\n"
                      "    \\\n"
                      "    if (size < 0) return -1; \\\n"
                      "    \\\n"
                      "    /* Allocate buffer (+1 for null terminator) */ \\\n"
                      "    buffer = (char*)malloc(size + 1); \\\n"
                      "    if (!buffer) return -1; \\\n"
                      "    \\\n"
                      "    /* Format the string */ \\\n"
                      "    va_start(args, fmt); \\\n"
                      "    vsnprintf(buffer, size + 1, fmt, args); \\\n"
                      "    va_end(args); \\\n"
                      "    \\\n"
                      "    /* Write label and escaped content */ \\\n"
                      "    fprintf(__cct_file, \"%s:\", label_str); \\\n"
                      "    cct_write_escaped(__cct_file, buffer); \\\n"
                      "    fprintf(__cct_file, \"\\n\"); \\\n"
                      "    \\\n"
                      "    /* Clean up */ \\\n"
                      "    free(buffer); \\\n"
                      "    return 0; \\\n"
                      "}\n");

  String_Builder main_body = {0};

  const CtBlock b = comptime_blocks->items[0];
  assert(strcmp(b.block_id, "__top_level") == 0);
  // DEFINE TOP LEVEL WRITER
  sb_appendf(&sb, "DEFINE_LABELED_WRITER(__cct_write_%s, \"%zu\")\n",
             b.block_id, b.block_index);

  const CtBlock bottom_block =
      comptime_blocks->items[comptime_blocks->count - 1];

  assert(strcmp(bottom_block.block_id, "__bottom_level") == 0);
  // DEFINE BOT LEVEL WRITER
  sb_appendf(&sb, "DEFINE_LABELED_WRITER(__cct_write_%s, \"%zu\")\n",
             bottom_block.block_id, bottom_block.block_index);

  for (size_t i = 1; i < comptime_blocks->count - 1; i++) {
    CtBlock b = comptime_blocks->items[i];

    sb_appendf(&sb, "DEFINE_LABELED_WRITER(__cct_write_%s, \"%zu\")\n",
               b.block_id, b.block_index);

    if (b.kind == CtBlockKind_Inline) {
      // parse the inline block, extract the content between
      // /*INLINE_COMPTIME*/ and /*__cct_end*/, and wrap it in a function
      // move cursor to first '"'
      const char *cursor = strchr(b.beg, '"');
      String_Builder inline_body = {0};
      const char *stmt_end = parse_stringified_c_code(cursor, &inline_body);
      log_info("Parsed [INLINE] block: %.*s", (int)inline_body.count,
               inline_body.items);

      const char *end_marker = "/*__cct_end*/";
      const char *end_marker_pos = strstr(stmt_end, end_marker);

      sb_appendf(
          &sb,
          "void " BLOCK_ID_PREFIX
          "%s(CCT_write_in_place_fn $$, CCT_write_in_place_fn "
          "$$_top_level, CCT_write_in_place_fn $$_bottom_level, cct_parser "
          "parser) { %.*s }",
          b.block_id, (int)inline_body.count, inline_body.items);
    }

    sb_appendf(&main_body,
               BLOCK_ID_PREFIX "%s(__cct_write_%s, __cct_write___top_level, "
                               "__cct_write___bottom_level, (cct_parser) "
                               "{.current=_cct_pp_file.items+1+%zu}); // "
                               "execute statement '%s' '%zu-%zu' \n",
               b.block_id, b.block_id, b.end_index, b.block_id, (b.beg_index),
               (b.end_index));
  }

  // String_Builder escaped_source = {0};
  // sb_escape(&ctx->preprocessed_source->source, &escaped_source);
  // sb_appendf(&sb, "\nstatic char raw_file[%zu] = \"%.*s\";\n",
  //            escaped_source.count, (int)escaped_source.count,
  //            escaped_source.items);

  nob_sb_append_cstr(&sb, "int cct_runner_main(void){\n"
                          "  __cct_file = fopen(\"");

  nob_sb_append_cstr(&sb, ctx->vals_path);
  nob_sb_append_cstr(
      &sb, "\", \"wb\"); if(!__cct_file){ perror(\"runner\"); return 1; }\n");

  sb_appendf(&sb, "nob_read_entire_file("
                  "_cct_pp_filename, &_cct_pp_file);\n");

  sb_append_buf(&sb, main_body.items, main_body.count);

  nob_sb_append_cstr(&sb, "  fclose(__cct_file);\n"
                          "  return 0;\n"
                          "}\n");
  write_entire_file(ctx->runner_templ_path, sb.items, sb.count);
}

bool ValsFile_parse_next_replacement(String_View *contents, String_View *label,
                                     String_View *replacement) {

  String_View line = sv_chop_by_delim(contents, '\n');
  if (line.count == 0) {
    return false;
  }

  String_View index_str = sv_chop_by_delim(&line, ':');

  *label = index_str;
  *replacement = line;

  return true;
}

typedef struct {
  String_Builder *builders;
  size_t count;
} StringBuilderArray;

#define MAX_BLOCKS 5000

StringBuilderArray ValsFile_read_file(const char *path) {

  String_Builder out = {0};
  nob_read_entire_file(path, &out);

  String_View sv = sv_from_parts(out.items, out.count);

  static String_Builder sba[MAX_BLOCKS] = {0};
  for (size_t i = 0; i < MAX_BLOCKS; i++) {
    sba[i].count = 0;
  }

  int max_index = 0;
  while (1) {
    String_View label, replacement = {0};
    bool has_next = ValsFile_parse_next_replacement(&sv, &label, &replacement);
    if (!has_next) {
      break;
    }
    log_info("vals line [%.*s] : '%.*s'", (int)label.count, label.data,
             (int)replacement.count, replacement.data);

    const char *label_str = nob_temp_sv_to_cstr(label);

    int block_index = atoi(label_str);
    assert(block_index < MAX_BLOCKS);

    String_Builder parsed_replacement = {0};
    // unescape the replacement string
    size_t i = 0;
    while (i < replacement.count) {
      if (replacement.data[i] == '\\') {
        i++; // skip the backslash
        log_info("FOUND escape %c", replacement.data[i]);
        // da_append(&parsed_replacement, replacement.data[i++]);
        switch (replacement.data[i++]) {
        case 'n':
          da_append(&parsed_replacement, '\n');
          break;
        case 'r':
          da_append(&parsed_replacement, '\r');
          break;
        case 't':
          da_append(&parsed_replacement, '\t');
          break;
        case '\\':
          da_append(&parsed_replacement, '\\');
          break;
        default:
          // da_append(&parsed_replacement, 'replacement.data[i - 1]);');
          nob_log(
              ERROR,
              "unknown escape sequence \\%c in replacement string \n: '%.*s' ",
              replacement.data[i - 1], (int)replacement.count,
              replacement.data);
          assert(0 && "unknown esc seq");
          break;
        }
      } else {
        da_append(&parsed_replacement, replacement.data[i++]);
      }
    }

    sb_append_buf(&sba[block_index], parsed_replacement.items,
                  parsed_replacement.count);

    if (block_index > max_index) {
      max_index = block_index;
    }
  }
  return (StringBuilderArray){.builders = sba, .count = max_index + 1};
}

void substitute_block_values(Context *ctx, CtBlocks *blocks,
                             String_Builder *out) {

  StringBuilderArray sba = ValsFile_read_file(ctx->vals_path);

  char *flushed_until_ptr = ctx->preprocessed_source->source.items;

  // for each block
  da_foreach(CtBlock, b, blocks) {
    // assert(b->block_index < sba.count);
    String_Builder replacement = sba.builders[b->block_index];

    log_info("Processing block '%s' (index %zu) [%zu bytes replacement]",
             b->block_id, b->block_index, replacement.count);
    log_info("Replacement is -> %.*s", (int)replacement.count,
             replacement.items);

    assert(flushed_until_ptr <= b->beg);

    // copy everything from flushed_until_ptr to b.beg
    size_t n = (size_t)(b->beg - flushed_until_ptr);
    log_info("appending %zu bytes of original source", n);
    sb_append_buf(out, flushed_until_ptr, n);

    if (replacement.count > 0) {
      sb_append_buf(out, replacement.items, replacement.count);
    }

    flushed_until_ptr = (char *)b->end + 1;
  }

  // copy the rest of the file
  if (flushed_until_ptr < ctx->preprocessed_source->source.items +
                              ctx->preprocessed_source->source.count) {
    log_info("We have remaining source to flush (from %p to %p)",
             flushed_until_ptr,
             ctx->preprocessed_source->source.items +
                 ctx->preprocessed_source->source.count);
    size_t n =
        (size_t)(ctx->preprocessed_source->source.items +
                 ctx->preprocessed_source->source.count - flushed_until_ptr);
    sb_append_buf(out, flushed_until_ptr, n - 1);
  }
}

void parse_source_file(const char *filename, FileBuffer *sc) {
  // String_Builder text_buf = {0};
  sc->source = (String_Builder){0};

  if (!read_entire_file(filename, &sc->source)) {
    nob_log(ERROR, "could not read source file %s", filename);
    exit(1);
  }
}

static void run_preprocess_cmd_for_source(CliArgs *parsed_argv,
                                          const char *input_filename,
                                          const char *output_filename) {

  static Nob_Cmd pp_cmd = {0};

  log_info("Preprocessing %s", input_filename);
  nob_cmd_append(&pp_cmd, Parsed_Argv_compiler_name(parsed_argv));
  nob_cmd_append(&pp_cmd, input_filename);
  cmd_append_arg_indeces(parsed_argv, &parsed_argv->flags, &pp_cmd);

  nob_cmd_append(&pp_cmd, "-E", "-CC");
  nob_cmd_append(&pp_cmd, "-o", output_filename);

  if (!nob_cmd_run(&pp_cmd)) {
    nob_log(ERROR, "failed to process TU %s", input_filename);
    exit(1);
  }
}

void scan_ct_blocks(FileBuffer *sc, CtBlocks *blocks) {
  sc->source.items[sc->source.count] = '\0';
  const char *source = sc->source.items;

  CtBlock top_level_block = {.beg = source,
                             .end = source - 1,
                             .beg_index = 0,
                             .end_index = 0,
                             .block_id = "__top_level",
                             .block_index = 0};

  da_append(blocks, top_level_block);

  while (true) {
    const char *beg = strstr(source, BLOCK_FUNC_PREFIX);
    if (!beg) {
      break;
    }
    const char *cursor = beg + strlen(BLOCK_FUNC_PREFIX);
    log_info("Found beg at %p %c", beg, *beg);
    // skip whitespace
    const char *stmt_name_beg = cursor;
    while (*cursor && *cursor != '(') {
      cursor++;
    }

    // extract name of the block
    const char *block_id = strndup(stmt_name_beg, cursor - stmt_name_beg);
    log_info("Block id is '%s'", block_id);

    if (strcmp(block_id, "INLINE") == 0) {
      const char *end_mark = "/*__cct_end*/";
      const char *end = strstr(cursor, end_mark) + strlen(end_mark) - 1;

      log_error(
          "--> Found Inline block, (%zu) [cursor relative index: %zu-%zu] ",
          end - beg, beg - source, end - source);

      CtBlock b =
          (CtBlock){.beg = beg - strlen("/*"), // we start from the /*
                    .end = end,
                    .beg_index = beg - strlen("/*") - sc->source.items,
                    .end_index = end - sc->source.items,
                    .kind = CtBlockKind_Inline,
                    .block_id = leaky_sprintf("inline_%zu", blocks->count),
                    .block_index = blocks->count};

      da_append(blocks, b);
      source = (char *)end + 1;
    } else {

      // skip over the number and whitespace
      while (*cursor && *cursor != '{') {
        cursor++;
      }

      const char *end = scan_balanced_brace(cursor);
      log_info("Block length is %zu", end - cursor);

      log_error("--> Found block, (%zu) [cursor relative index: %zu-%zu] ",
                end - beg, beg - source, end - source);

      CtBlock b = (CtBlock){.beg = beg,
                            .end = end,
                            .beg_index = beg - sc->source.items,
                            .end_index = end - sc->source.items,
                            .kind = CtBlockKind_Function,
                            .block_id = block_id,
                            .block_index = blocks->count};
      da_append(blocks, b);

      source = (char *)end + 1;
    }
  }

  CtBlock bottom_level_block =
      (CtBlock){.beg = sc->source.items + sc->source.count - 1,
                .end = sc->source.items + sc->source.count - 2,
                .beg_index = sc->source.count - 1,
                .end_index = sc->source.count - 1,
                .block_id = "__bottom_level",
                .block_index = blocks->count};

  da_append(blocks, bottom_level_block);

  log_info("found %zu blocks", blocks->count);
}

int main(int argc, char **argv) {
  nob_minimal_log_level = ERROR;
  if (argc < 2) {
    nob_log(NOB_ERROR,
            "usage: %s "
            "[clang-like "
            "args] file.c ...",
            argv[0]);
    return 1;
  }

  log_info("Parsing arguments");
  CliArgs parsed_argv = CliArgs_parse(argc, argv);

  if (parsed_argv.cct_flags & CliComptimeFlag_NoLogs) {
    nob_minimal_log_level = NO_LOGS;
  } else if (parsed_argv.cct_flags & CliComptimeFlag_Debug) {
    nob_minimal_log_level = INFO;
  }

  log_info("Received %d arguments", argc);
  log_info("Using compiler %s", Parsed_Argv_compiler_name(&parsed_argv));

  Nob_Cmd final = {0};
  nob_cmd_append(&final, Parsed_Argv_compiler_name(&parsed_argv));
  cmd_append_arg_indeces(&parsed_argv, &parsed_argv.flags, &final);

  struct {
    const char **items;
    size_t count, capacity;
  } files_to_remove = {0};

  nob_da_foreach(int, index, &parsed_argv.input_files) {
    // -- SETUP CONTEXT --
    const char *input_filename = argv[*index];
    FileBuffer raw_source = {0}, preprocessed_source = {0};
    Context ctx = {0};
    ctx.input_path = input_filename;
    ctx.parsed_argv = &parsed_argv;
    ctx.raw_source = &raw_source;
    ctx.preprocessed_source = &preprocessed_source;

    Context_fill_paths(&ctx, input_filename);

    // -- PREPROCESS INPUT FILE (expand macros) --
    log_info("Processing %s source as SourceCode", input_filename);
    parse_source_file(input_filename, ctx.raw_source);

    log_info("Preprocessing source");
    run_preprocess_cmd_for_source(&parsed_argv, input_filename,
                                  ctx.preprocessed_path);

    log_info("Rewrote argv %s -> %s", input_filename, ctx.preprocessed_path);

    parse_source_file(ctx.preprocessed_path, ctx.preprocessed_source);

    // -- EXTRACT COMPILE TIME BLOCKS --
    CtBlocks comptime_blocks = {0};

    scan_ct_blocks(ctx.preprocessed_source, &comptime_blocks);
    log_info("Found %zu comptime blocks", comptime_blocks.count);

    if (comptime_blocks.count == 0) {
      nob_log(WARNING, "No comptime blocks found in %s, skipping file.",
              input_filename);
      da_append(&files_to_remove, ctx.preprocessed_path);

      // -- APPEND ORIGINAL UNCHANGED FILE TO ARGV --
      nob_cmd_append(&final, ctx.input_path);
      log_info("Appended initial input file %s to final argv", ctx.input_path);
      continue;
    }

    // -- GENERATE AND RUN THE RUNNER --
    log_info("Generating runner c");
    emit_runner_tu(&ctx, &comptime_blocks);
    compile_and_run_runner(&ctx);

    String_Builder repls2 = {0};
    substitute_block_values(&ctx, &comptime_blocks, &repls2);

    log_info("WRTING FINAL OUTPUT FILE %s (%zu)bytes ", ctx.final_out_path,
             repls2.count);
    write_entire_file(ctx.final_out_path, repls2.items, repls2.count);

    // -- APPEND FINAL OUTPUT FILE TO ARGV --
    nob_cmd_append(&final, ctx.final_out_path);

    log_info("Appended final output file %s to final argv", ctx.final_out_path);

    log_info("Scheduling intermediate files for deletion");
    da_append(&files_to_remove, ctx.preprocessed_path);
    da_append(&files_to_remove, ctx.runner_templ_path);
    da_append(&files_to_remove, ctx.runner_defs_path);
    da_append(&files_to_remove, ctx.runner_main_path);
    da_append(&files_to_remove, ctx.runner_exepath);
    da_append(&files_to_remove, ctx.vals_path);
    da_append(&files_to_remove, ctx.final_out_path);
  }

  if (!cmd_run(&final)) {
    nob_log(ERROR, "failed to compile final output");
    return 1;
  } else {
    log_success("Successfuly compiled final output");
  }

  // -- CLEANUP --
  log_info("Cleaning up %zu intermediate files", files_to_remove.count);
  nob_da_foreach(const char *, f, &files_to_remove) {
    if (!(parsed_argv.cct_flags & CliComptimeFlag_KeepInter)) {
      log_warning("Deleting intermediate file %s", *f);
      delete_file(*f);
    } else {
      log_info("Keeping intermediate file %s", *f);
    }
  }

  return 0;
}
