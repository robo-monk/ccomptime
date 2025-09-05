#include <assert.h>
#include <stdint.h>
#include <string.h>
#define NOB_STRIP_PREFIX
#define NOB_IMPLEMENTATION
#include "hashmap.c"
#include "nob.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#define CCT_MAIN "cct_main"
#define STRINGIFY(x) #x

static const char *TMP_DIR = "build/cctmp";

static const char *MARK_CTX_BEG = "/*CCT_CTX_BEGIN*/";
static const char *MARK_CTX_END = "/*CCT_CTX_END*/";
static const char *MARK_DO_BEG = "/*CCT_DO_BEGIN*/";
static const char *MARK_DO_END = "/*CCT_DO_END*/";

#define BLOCK_ID_PREFIX "CCT_STMT_$"
#define BLOCK_FUNC_PREFIX "void " BLOCK_ID_PREFIX

typedef struct Line Line;

typedef struct {
  size_t start, end;
  Line *line;
} Block;

typedef struct {
  const char *beg, *end;
  const char *block_id;
  size_t block_index;
} Block2;

typedef struct {
  size_t count, capacity;
  Block2 *items;
} Blocks2;

typedef struct {
  size_t count, capacity;
  Block *items;
} Blocks;

struct Line {
  Line *next;
  Line *prev;

  size_t index;
  char *text;
};

typedef struct {
  Line *head;
  Line *tail;
  size_t count;
  HashMap map;
} Lines;

typedef struct {
  Lines lines;
  String_Builder source;
} SourceCode;

typedef struct {
  int *items;
  size_t count, capacity;
} ArgvPointers;

typedef enum { Compiler_CLANG, Compiler_GCC, _Compiler_Len } Compiler;
static char *CCompiler_Map[] = {"clang", "gcc"};

static_assert(NOB_ARRAY_LEN(CCompiler_Map) == _Compiler_Len,
              "supported_compilers length must match CCompiler enum");

static Compiler parse_compiler_name(const char *name) {
  for (size_t i = 0; i < _Compiler_Len; i++) {
    if (strcmp(name, CCompiler_Map[i]) == 0) {
      return (Compiler)i;
    }
  }

  return -1;
}

typedef enum {
  CCT_Flag_Kind_Debug,
} CCT_Flag_Kind;

typedef struct {
  CCT_Flag_Kind kind;
} CCT_Flag;

typedef struct {
  CCT_Flag *items;
  size_t count, capacity;
} CCT_Flags;

typedef struct {
  int arc;
  char **argv;
  Compiler compiler;
  ArgvPointers input_files;
  ArgvPointers not_input_files;
  CCT_Flags cct_flags;
  char *cct_entry;
} Parsed_Argv;

#define CCT_PREFIX "-cct"

Parsed_Argv parse_argv(int argc, char **argv) {
  Parsed_Argv parsed_argv = {
      .argv = argv,
      .arc = argc,
      .input_files = {0},
      .cct_flags = {0},
      .not_input_files = {0},
  };

  parsed_argv.compiler = parse_compiler_name(argv[1]);
  if (parsed_argv.compiler == -1) {
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

      if (strstr(argv[i], CCT_PREFIX)) {
        const char *flag = argv[i] + (NOB_ARRAY_LEN(CCT_PREFIX) - 1);

        nob_log(INFO, "Found -cct flag (%s) (%d)", flag,
                strcmp(flag, "-keep-inter"));

        if (strcmp(flag, "-entry") == 0) {
          char *entry_filename = argv[++i];
          assert(entry_filename[strlen(entry_filename) - 1] == 'c');
          parsed_argv.cct_entry = entry_filename;
          continue;
        } else if (strcmp(flag, "-debug") == 0) {
          CCT_Flag cct_flag = {.kind = CCT_Flag_Kind_Debug};
          da_append(&parsed_argv.cct_flags, cct_flag);
          continue;
        } else {
          nob_log(ERROR, "Unknown -cct flag: %s", flag);
          exit(1);
        }
      } else {
        da_append(&parsed_argv.not_input_files, i);
      }

      continue;
    }

    if (strstr(argv[i], ".c") || strstr(argv[i], ".C")) {
      nob_log(INFO, "Detected input file: %s", argv[i]);
      da_append(&parsed_argv.input_files, i);
    } else {
      nob_log(INFO, "Detected other flag: %s", argv[i]);
      da_append(&parsed_argv.not_input_files, i);
    }
  }

  return parsed_argv;
}

static char *Parsed_Argv_compiler_name(Parsed_Argv *pa) {
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

const char *find_block_end(const char *ccode) {
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

  return NULL; // No matching brace found
}

void append_inputs_to_cmd_except(Parsed_Argv *pa, const char *skip_input,
                                 Cmd *cmd) {
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

void append_argv_pointers_to_cmd(Parsed_Argv *pa, ArgvPointers *pointers,
                                 Cmd *cmd) {
  nob_da_foreach(int, index, pointers) {
    const char *f = pa->argv[*index];
    nob_log(INFO, "~~ append %s", f);
    nob_cmd_append(cmd, f);
  }
}

Line *lines_find_line_index(Lines *lines, size_t index) {
  return (Line *)HashMap_get(&lines->map, index);
}

void lines_append_new(Lines *lines, size_t index, char *text) {
  Line *line = malloc(sizeof(Line));
  line->text = text;
  line->index = lines->count++;
  if (!lines->head) {
    lines->head = line;
  }

  if (lines->tail) {
    lines->tail->next = line;
  }

  line->prev = lines->tail;
  lines->tail = line;

  HashMap_set(&lines->map, index, line);
}

void lines_to_sb(Lines *lines, String_Builder *sb) {
  for (Line *line = lines->head; line != NULL; line = line->next) {
    sb_appendf(sb, "%s\n", line->text);
  }
}

Lines lines_from_source(String_Builder *source) {
  nob_log(INFO, "lines from source");
  Lines lines = {0};
  lines.map = HashMap_init(256);

  String_Builder buffer = {0};

  size_t line_index = 1;
#define append_buffer_to_lines()                                               \
  do {                                                                         \
    sb_append_null(&buffer);                                                   \
    lines_append_new(&lines, line_index++, strdup(buffer.items));              \
    buffer.count = 0;                                                          \
  } while (0)

  for (size_t i = 0; i < source->count; i++) {
    const char c = source->items[i];
    if (c == '\n') {
      append_buffer_to_lines();
    } else {
      nob_da_append(&buffer, c);
    }
  }

  if (buffer.count > 0) {
    append_buffer_to_lines();
  }

#undef append_buffer_to_lines
  return lines;
}

static void find_blocks(SourceCode *sc, const char *beg, const char *end,
                        Blocks *blocks) {
  Line *line = sc->lines.head;
  while (line) {
    // nob_log(INFO, "ANAL line [%s]", line->text);
    const char *s = strstr(line->text, beg);
    if (s) {
      const char *e = strstr(s + strlen(beg), end);
      if (!e) {
        nob_log(ERROR, "unmatched block marker %s ... %s", beg, end);
        exit(1);
      }

      Block b = (Block){.start = (size_t)(s - sc->source.items),
                        .end = (size_t)(e + strlen(end) - sc->source.items - 1),
                        .line = line};

      da_append(blocks, b);
    }
    line = line->next;
  }
}

int parse_cct_statement(const char *line, Nob_String_Builder *out) {
  nob_log(INFO, "parsing line %s", line);
  int ii = 0;
  while (line[ii] != '$') {
    ii++;
  }

  const int start = ++ii; // skip dollar

  while (line[ii] != '=') {
    ii++;
  }
  const int end = ii; // points to '='

  char n_str[32]; // Adjust size as needed
  strncpy(n_str, &line[start], end - start);
  n_str[end - start] = '\0';
  int n = atoi(n_str);

  nob_log(INFO, "parsed n = %d", n);

  ii += 1; // skip eq
  while (line[ii] == ' ' || line[ii] == '\t')
    ii++; // skip whitespace

  if (line[ii++] != '\"') {

    nob_log(ERROR, "#expected \\\n");
    exit(1);
  }

  while (line[ii] != '\"') {
    char c = line[ii++];
    if (c == '\\') {
      char e = line[ii++];
      nob_da_append(out, e);
    } else {
      nob_da_append(out, c);
    }
  }

  return n;
}

typedef struct {
  SourceCode *og_sc;
  SourceCode *pp_sc;
} Context2;

typedef struct {
  SourceCode *raw_source;
  SourceCode *preprocessed_source;

  const char *input_path;
  const char *preprocessed_path;
  const char *runner_cpath;
  const char *runner_exepath;
  const char *vals_path;
  const char *final_out_path;
  Parsed_Argv *parsed_argv;
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
      leaky_sprintf("%sct-preprocessed.c", original_source);
  ctx->runner_cpath = leaky_sprintf("%sct-runner.c", original_source);
#ifdef _WIN32
  ctx->runner_exepath = leaky_sprintf("%sct-runner.exe", original_source);
#else
  ctx->runner_exepath = leaky_sprintf("%sct-runner", original_source);
#endif
  ctx->vals_path = leaky_sprintf("%sct-vals.txt", original_source);
  ctx->final_out_path = leaky_sprintf("%sct-final.c", original_source);
}

void compile_and_run_runner(Context *ctx) {
  assert(ctx->runner_cpath);
  static Nob_Cmd cmd = {0};

  nob_cmd_append(&cmd, Parsed_Argv_compiler_name(ctx->parsed_argv));
  append_argv_pointers_to_cmd(ctx->parsed_argv,
                              &ctx->parsed_argv->not_input_files, &cmd);
  nob_cmd_append(&cmd, "-O0");

  append_inputs_to_cmd_except(ctx->parsed_argv, ctx->input_path, &cmd);
  nob_cmd_append(&cmd, ctx->runner_cpath);
  nob_cmd_append(&cmd, "-Dmain=__cct_user_main", "-Dcct_runner_main=main");
  nob_cmd_append(&cmd, "-o", ctx->runner_exepath);

  if (!nob_cmd_run(&cmd)) {
    nob_log(ERROR, "failed to compile runner %s", ctx->runner_cpath);
    exit(1);
  }

  nob_cmd_append(&cmd, ctx->runner_exepath);

  nob_log(INFO, "Running runner %s", ctx->runner_exepath);
  if (!nob_cmd_run(&cmd)) {
    nob_log(ERROR, "failed to run runner %s", ctx->runner_cpath);
    exit(1);
  }
}

void generate_runner_c(Context *ctx, Blocks2 *dos) {

  Nob_String_Builder sb = {0};

  sb_appendf(&sb, "\n#define COMPTIME\n");
  sb_appendf(&sb, "\n// -- SOURCE FILE START -- \n");
  sb_appendf(&sb, "\n#define main __cct_user_main\n");
  sb_append_buf(&sb, ctx->raw_source->source.items,
                ctx->raw_source->source.count - 1);
  sb_appendf(&sb, "\n#undef main\n");
  sb_appendf(&sb, "\n// -- SOURCE FILE END -- \n\n");

  sb_append_cstr(&sb, "\nFILE* __cct_file;\n");
  sb_append_cstr(&sb,
                 "#define DEFINE_LABELED_WRITER(name, label_str)          \\\n"
                 "int name(const char *fmt, ...) {                        \\\n"
                 "    va_list args;                                       \\\n"
                 "    va_start(args, fmt);                                \\\n"
                 "    fprintf(__cct_file, \"%s:\", label_str);            \\\n"
                 "    vfprintf(__cct_file, fmt, args);                    \\\n"
                 "    fprintf(__cct_file, \"\\n\");                       \\\n"
                 "    va_end(args);                                       \\\n"
                 "    return 0;                                           \\\n"
                 "}\n");

  String_Builder main_body = {0};

  for (size_t i = 0; i < dos->count; i++) {
    Block2 b = dos->items[i];
    sb_appendf(&sb, "DEFINE_LABELED_WRITER(__cct_write_%s, \"%zu\")\n",
               b.block_id, b.block_index);

    sb_appendf(&main_body,
               BLOCK_ID_PREFIX
               "%s(__cct_write_%s); // execute statement '%s' \n",
               b.block_id, b.block_id, b.block_id);
  }

  nob_sb_append_cstr(&sb, "int cct_runner_main(void){\n"
                          "  __cct_file = fopen(\"");

  nob_sb_append_cstr(&sb, ctx->vals_path);
  nob_sb_append_cstr(
      &sb, "\", \"wb\"); if(!__cct_file){ perror(\"runner\"); return 1; }\n");

  sb_append_buf(&sb, main_body.items, main_body.count);

  nob_sb_append_cstr(&sb, "  fclose(__cct_file);\n"
                          "  return 0;\n"
                          "}\n");
  nob_sb_append_null(&sb);
  write_entire_file(ctx->runner_cpath, sb.items, sb.count);
}

typedef struct {
  // size_t line_index;
  char *label;
  char *replacement;
  // Block2 *block;
} LineReplacement;

typedef struct {
  LineReplacement *items;
  size_t count, capacity;
} LineReplacements;

typedef struct {
  char *replacement; // can be NULL if no replacement
  Block2 *block;
} BlockReplacement;

typedef struct {
  BlockReplacement *items;
  size_t count, capacity;
} BlockReplacements;

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

void map_blocks_to_replacements(Context *ctx, Blocks2 *blocks,
                                BlockReplacements *repls) {
  String_Builder out = {0};

  String_Builder vals = {0};
  nob_read_entire_file(ctx->vals_path, &vals);
  String_View sv = sv_from_parts(vals.items, vals.count);

  // int flushed_until = 0;
  char *flushed_until_ptr = ctx->raw_source->source.items;

  while (1) {
    String_View label, replacement = {0};
    bool has_next = ValsFile_parse_next_replacement(&sv, &label, &replacement);
    if (!has_next) {
      break;
    }
    nob_log(INFO, "vals line [%.*s] : '%.*s'", (int)label.count, label.data,
            (int)replacement.count, replacement.data);

    int block_index = atoi(nob_temp_sv_to_cstr(label));
    assert(block_index < blocks->count);

    Block2 b = blocks->items[block_index];
    nob_log(INFO, "mapping replacement to block %s", b.block_id);
    nob_log(INFO, "%p <= %p", flushed_until_ptr, b.beg);
    assert(flushed_until_ptr <= b.beg);

    // copy everything from flushed_until_ptr to b.beg
    size_t n = (size_t)(b.beg - flushed_until_ptr);
    nob_log(INFO, "appending %zu bytes of original source", n);
    sb_append_buf(&out, flushed_until_ptr, n);

    nob_log(INFO, "appending replacement '%.*s'", (int)replacement.count,
            replacement.data);

    // append replacement
    if (replacement.count > 0) {
      sb_append_buf(&out, replacement.data, replacement.count);
    }

    flushed_until_ptr = (char *)b.end + 1;
  }

  // copy the rest of the file
  if (flushed_until_ptr < vals.items + vals.count) {
    size_t n = (size_t)(vals.items + vals.count - flushed_until_ptr);
    sb_append_buf(&out, flushed_until_ptr, n);
  }

  // by default every block gets a NULL replacement

  // if (vals.count == 0) {
  //   nob_log(WARNING, "vals file %s is empty", path);
  //   return;
  // }

  // String_View line;
  // while (1) {
  //   line = sv_chop_by_delim(&sv, '\n');
  //   if (line.count == 0)
  //     break;

  //   nob_log(INFO, "vals line [%.*s]", (int)line.count, line.data);

  //   String_View index_str = sv_chop_by_delim(&line, ':');
  //   // int i = atoi(nob_temp_sv_to_cstr(index_str));

  //   LineReplacement repl = (LineReplacement){
  //       .label = strdup(nob_temp_sv_to_cstr(index_str)),
  //       .replacement = strdup(nob_temp_sv_to_cstr(line)),
  //   };

  //   da_append(repls, repl);
  // }
}

void parse_vals_file(const char *path, LineReplacements *repls) {
  String_Builder vals = {0};
  nob_read_entire_file(path, &vals);

  if (vals.count == 0) {
    nob_log(WARNING, "vals file %s is empty", path);
    return;
  }

  String_View sv = nob_sv_from_parts(vals.items, vals.count);

  String_View line;
  while (1) {
    line = sv_chop_by_delim(&sv, '\n');
    if (line.count == 0)
      break;

    nob_log(INFO, "vals line [%.*s]", (int)line.count, line.data);

    String_View index_str = sv_chop_by_delim(&line, ':');
    // int i = atoi(nob_temp_sv_to_cstr(index_str));

    LineReplacement repl = (LineReplacement){
        .label = strdup(nob_temp_sv_to_cstr(index_str)),
        .replacement = strdup(nob_temp_sv_to_cstr(line)),
    };

    da_append(repls, repl);
  }
}

void parse_source_file(const char *filename, SourceCode *sc) {
  // String_Builder text_buf = {0};
  sc->source = (String_Builder){0};

  if (!read_entire_file(filename, &sc->source)) {
    nob_log(ERROR, "could not read source file %s", filename);
    exit(1);
  }

  sc->lines = lines_from_source(&sc->source);
}

static void run_preprocess_cmd_for_source(Parsed_Argv *parsed_argv,
                                          const char *input_filename,
                                          const char *output_filename) {

  static Nob_Cmd pp_cmd = {0};

  nob_log(INFO, "Preprocessing %s", input_filename);
  nob_cmd_append(&pp_cmd, Parsed_Argv_compiler_name(parsed_argv));
  nob_cmd_append(&pp_cmd, input_filename);
  append_argv_pointers_to_cmd(parsed_argv, &parsed_argv->not_input_files,
                              &pp_cmd);

  nob_cmd_append(&pp_cmd, "-CC", "-E", "-P", "-Dmain=__user_main");
  nob_cmd_append(&pp_cmd, "-o", output_filename);

  if (!nob_cmd_run(&pp_cmd)) {
    nob_log(ERROR, "failed to process TU %s", input_filename);
    exit(1);
  }
}

typedef char *temp_cstr;
temp_cstr sb_temp_cstr_view(String_Builder *sb) {
  sb->items[sb->count] = '\0';
  return sb->items;
}

void find_blocks2(SourceCode *sc, Blocks2 *blocks) {
  temp_cstr source = sb_temp_cstr_view(&sc->source);
  while (true) {
    const char *beg = strstr(source, BLOCK_FUNC_PREFIX);
    if (!beg) {
      break;
    }
    nob_log(INFO, "found beg at %p %c", beg, *beg);

    beg += strlen(BLOCK_FUNC_PREFIX);

    const char *stmt_name_beg = beg;
    while (*beg && *beg != '(') {
      beg++;
    }

    // extract name of the block
    const char *block_id = strndup(stmt_name_beg, beg - stmt_name_beg);
    nob_log(INFO, "Block id is '%s'", block_id);

    // skip over the number and whitespace
    while (*beg && *beg != '{') {
      beg++;
    }

    const char *end = find_block_end(beg);
    nob_log(INFO, "Block length is %zu", end - beg);

    Block2 b = (Block2){.beg = beg,
                        .end = end,
                        .block_id = block_id,
                        .block_index = blocks->count};
    da_append(blocks, b);

    source = (char *)end + 1;
  }

  nob_log(INFO, "found %zu blocks", blocks->count);
}

// void exec_line_replacements(SourceCode *sc, Blocks2 *blocks, LineReplacements
// *repls) {
//     da_fore
// }

char **clone_argv(int argc, char **argv) {
  char **copy = malloc((argc + 1) * sizeof(char *));
  if (!copy)
    return NULL;

  for (int i = 0; i < argc; i++) {
    copy[i] = strdup(argv[i]); // makes a copy of the string
    if (!copy[i]) {
      // cleanup if strdup fails
      for (int j = 0; j < i; j++)
        free(copy[j]);
      free(copy);
      return NULL;
    }
  }
  copy[argc] = NULL; // NULL-terminate
  return copy;
}

int main(int argc, char **argv) {
  nob_minimal_log_level = ERROR;
  // nob_minimal_log_level = NOB_ERROR;
  if (argc < 2) {
    nob_log(NOB_ERROR,
            "usage: %s "
            "[clang-like "
            "args] file.c ...",
            argv[0]);
    return 1;
  }

  Parsed_Argv parsed_argv = parse_argv(argc, argv);
  bool cleanup_files = true;

  da_foreach(CCT_Flag, flag, &parsed_argv.cct_flags) {
    switch (flag->kind) {
    case CCT_Flag_Kind_Debug:
      cleanup_files = false;
      nob_minimal_log_level = INFO;
      nob_log(INFO, "Debug mode enabled");
      break;
    }
  }

  nob_log(INFO, "Received %d arguments", argc);

  nob_log(INFO, "Using compiler %s", Parsed_Argv_compiler_name(&parsed_argv));
  // const char *pp_input = temp_sprintf("%s.pre_cct.c", "CCT_OUT");
  // Nob_Cmd pp = {0};
  // cmd_append(&pp, Parsed_Argv_compiler_name(&parsed_argv));
  // append_argv_pointers_to_cmd(&parsed_argv, &parsed_argv.input_files, &pp);
  // append_argv_pointers_to_cmd(&parsed_argv, &parsed_argv.not_input_files,
  // &pp); nob_cmd_append(&pp, "-E", "-P", "-CC"); nob_cmd_append(&pp, "-o",
  // pp_input); nob_log(INFO, "Preprocessing TU"); if (!nob_cmd_run(&pp))
  //   return 1;
  // nob_log(INFO, "Done with preprocessing TU");

  Nob_Cmd final = {0};
  nob_cmd_append(&final, Parsed_Argv_compiler_name(&parsed_argv));
  // nob_cmd_append(&final, pp_input);
  append_argv_pointers_to_cmd(&parsed_argv, &parsed_argv.not_input_files,
                              &final);
  // nob_cmd_append(&final, )
  // nob_cmd_append(&final, "-E", "-P", "-CC");

  // char **new_argv = clone_argv(argc, argv);

  // assert(parsed_argv.cct_entry);

  // preprocess each TU
  // Nob_Cmd cmd = {0};
  //
  struct {
    const char **items;
    size_t count, capacity;
  } files_to_remove = {0};

  nob_da_foreach(int, index, &parsed_argv.input_files) {
    // -- SETUP CONTEXT --
    const char *input_filename = argv[*index];
    SourceCode raw_source = {0}, preprocessed_source = {0};
    Context ctx = {0};
    ctx.input_path = input_filename;
    ctx.parsed_argv = &parsed_argv;
    ctx.raw_source = &raw_source;
    ctx.preprocessed_source = &preprocessed_source;

    Context_fill_paths(&ctx, input_filename);

    // -- PREPROCESS INPUT FILE (expand macros) --
    nob_log(INFO, "Processing %s source as SourceCode", input_filename);
    parse_source_file(input_filename, ctx.raw_source);
    nob_log(INFO, "Input source is %zu lines long",
            ctx.raw_source->lines.count);

    nob_log(INFO, "Preprocessing source");
    run_preprocess_cmd_for_source(&parsed_argv, input_filename,
                                  ctx.preprocessed_path);

    nob_log(INFO, "Rewrote argv %s -> %s", input_filename,
            ctx.preprocessed_path);

    parse_source_file(ctx.preprocessed_path, ctx.preprocessed_source);
    nob_log(INFO, "Preprocessed source is %zu lines long",
            ctx.preprocessed_source->lines.count);

    // -- EXTRACT COMPILE TIME BLOCKS --
    Blocks2 do_blocks = {0};
    // find_blocks(ctx.preprocessed_source, MARK_DO_BEG, MARK_DO_END,
    // &do_blocks);
    find_blocks2(ctx.preprocessed_source, &do_blocks);
    nob_log(INFO, "Found %zu comptime blocks", do_blocks.count);

    if (do_blocks.count == 0) {
      nob_log(WARNING, "No comptime blocks found in %s, skipping file.",
              input_filename);
      da_append(&files_to_remove, ctx.preprocessed_path);

      // -- APPEND ORIGINAL UNCHANGED FILE TO ARGV --
      nob_cmd_append(&final, ctx.input_path);
      nob_log(INFO, "Appended initial input file %s to final argv",
              ctx.input_path);
      continue;
    }

    // -- GENERATE AND RUN THE RUNNER --
    nob_log(INFO, "Generating runner c");
    generate_runner_c(&ctx, &do_blocks);
    compile_and_run_runner(&ctx);

    BlockReplacements repls2 = {0};
    map_blocks_to_replacements(&ctx, &do_blocks, &repls2);
    exit(1);
    // -- COMPUTE REPLACEMENT INSTRUCTIONS --
    LineReplacements repls = {0};
    parse_vals_file(ctx.vals_path, &repls);

    if (repls.count > 0) {

      // -- RECONSTRUCT FINAL SOURCE FILE --
      String_Builder reconstructed_source = {0};

      Block2 *last_block = NULL;

      da_foreach(LineReplacement, lr, &repls) {

        size_t block_index = atoi(lr->label);
        assert(block_index < do_blocks.count);
        assert(last_block == NULL || (last_block->block_index <= block_index));

        Block2 *b = &do_blocks.items[block_index];

        // append all source from end of last block to start of this block
        if (last_block != NULL) {
          nob_log(INFO, "Appending replacement for block %s", b->block_id);

          size_t start = (last_block->end - ctx.raw_source->source.items) + 1;
          size_t end = (b->beg - ctx.raw_source->source.items);
          if (end > start) {
            sb_append_buf(&reconstructed_source,
                          ctx.raw_source->source.items + start, end - start);
          }
        } else {
          // first block, append from start of file
          size_t start = 0;
          size_t end = (b->beg - ctx.raw_source->source.items);
          if (end > start) {
            sb_append_buf(&reconstructed_source,
                          ctx.raw_source->source.items + start, end - start);
          }
        }

        last_block = b;
      }

      // da_foreach(Block2 , b, &do_blocks) {
      //   // find which line the block starts on
      //   size_t line_index = 0;
      //   for (Line *line = ctx.raw_source->lines.head; line != NULL;
      //        line = line->next) {
      //     if ((size_t)(b->beg - ctx.raw_source->source.items) >= line->index)
      //     {
      //       line_index = line->index;
      //     } else {
      //       break;
      //     }
      //   }

      //   LineReplacement repl = (LineReplacement){
      //       .line_index = line_index,
      //       .replacement = leaky_sprintf("/* COMPTIME BLOCK %s EXECUTED */",
      //                                    b->block_id),
      //       .block = b,
      //   };

      //   da_append(&repls, repl);

      // exec_line_replacements_in_place(&ctx, &repls);

      // -- WRITE FINAL OUTPUT FILE --
      // String_Builder final_c_source = {0};
      // lines_to_sb(&ctx.raw_source->lines, &final_c_source);
      write_entire_file(ctx.final_out_path, reconstructed_source.items,
                        reconstructed_source.count);

      // -- APPEND FINAL OUTPUT FILE TO ARGV --
      nob_cmd_append(&final, ctx.final_out_path);

    } else {
      // no change to original source
      nob_cmd_append(&final, ctx.input_path);
    }
    nob_log(INFO, "Appended final output file %s to final argv",
            ctx.final_out_path);

    if (cleanup_files) {
      da_append(&files_to_remove, ctx.preprocessed_path);
      da_append(&files_to_remove, ctx.runner_cpath);
      da_append(&files_to_remove, ctx.runner_exepath);
      da_append(&files_to_remove, ctx.vals_path);
      da_append(&files_to_remove, ctx.final_out_path);
    }
  }
  // nob_da_foreach(int, index, &parsed_argv.not_input_files) {
  //   nob_cmd_append(&final, argv[*index]);
  // }

  // Build final argv by
  // replacing inputs with
  // rewritten paths
  // Nob_Cmd final = {0};
  // nob_cmd_append(&final, Parsed_Argv_compiler_name(&parsed_argv));

  // for (int i = 2; i < argc; i++) {
  //   const char *tok = new_argv[i];
  //   nob_cmd_append(&final, tok);
  // }

  // nob_cmd_append(&final, "-D__user_main=main");
  int result = nob_cmd_run(&final);

  // -- CLEANUP --
  nob_da_foreach(const char *, f, &files_to_remove) {
    if (cleanup_files) {
      delete_file(*f);
    } else {
      nob_log(INFO, "Keeping intermediate file %s", *f);
    }
  }

  return 0;
}
