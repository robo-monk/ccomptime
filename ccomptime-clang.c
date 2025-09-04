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

static const char *TMP_DIR = "build/cctmp";

static const char *MARK_CTX_BEG = "/*CCT_CTX_BEGIN*/";
static const char *MARK_CTX_END = "/*CCT_CTX_END*/";
static const char *MARK_DO_BEG = "/*CCT_DO_BEGIN*/";
static const char *MARK_DO_END = "/*CCT_DO_END*/";

typedef struct Line Line;

typedef struct {
  size_t start, end;
  Line *line;
} Block;

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

        if (strcmp(flag, "-keep-inter") == 0) {
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

void append_argv_pointers_to_cmd(Parsed_Argv *pa, ArgvPointers *pointers,
                                 Cmd *cmd) {
  nob_da_foreach(int, index, pointers) {
    const char *f = pa->argv[*index];
    nob_log(INFO, "~~ append %s", f);
    nob_cmd_append(cmd, f);
  }
}

// TODO: this is O(n) and likely very slow
Line *lines_find_line_index(Lines *lines, size_t index) {
  return (Line *)HashMap_get(&lines->map, index);

  // for (Line *l = line->head; l != NULL; l = l->next) {
  //   if (l->index == index) {
  //     return l;
  //   }
  // }
  // return NULL;
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

    printf("#expected \\\n");
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
} Context;

// static char *gen_runner_c(SourceCode *sc, Blocks *dos, const char *vals_path)
// {
static char *gen_runner_c(Context *ctx, Blocks *dos, const char *vals_path) {

  Nob_String_Builder sb = {0};

  sb_appendf(&sb, "\n#define COMPTIME\n");
  sb_appendf(&sb, "\n#define CCTRUNNER\n");

  sb_appendf(&sb, "\n// -- SOURCE FILE START -- \n");
  sb_append_buf(&sb, ctx->pp_sc->source.items, ctx->pp_sc->source.count - 1);
  sb_appendf(&sb, "\n// -- SOURCE FILE END -- \n\n");

  sb_append_cstr(&sb, "\nFILE* __cct_file;\n");
  sb_append_cstr(
      &sb, "static void cct_write_escaped(FILE* f, const char* s){\n"
           "  if(!s) return;\n"
           "  for (const unsigned char* p=(const unsigned char*)s; *p; ++p){\n"
           "    unsigned char c=*p;\n"
           "    if(c=='\\\\' || c=='\"'){ fputc('\\\\',f); fputc(c,f); }\n"
           "    else if(c=='\\n') fputs(\"\\\\n\",f);\n"
           "    else if(c=='\\r') fputs(\"\\\\r\",f);\n"
           "    else if(c=='\\t') fputs(\"\\\\t\",f);\n"
           "    else fputc(c,f);\n"
           "  }\n"
           "}\n"
           "\n"
           "\n"
           "static void write_labeled_output(const char* label, char* v){\n"
           "  fputs(label, __cct_file); fputc(':', __cct_file); "
           "cct_write_escaped(__cct_file, v); "
           "fputc('\\n', __cct_file);\n"
           "}\n");

  String_Builder main_body = {0};

  for (size_t i = 0; i < dos->count; i++) {
    Block b = dos->items[i];
    printf("\n>block starts at %zu ends at %zu\n", b.start, b.end);
    Nob_String_Builder parsed_cct_statement = {0};

    int line_index = parse_cct_statement(b.line->text, &parsed_cct_statement);

    if (parsed_cct_statement.items[parsed_cct_statement.count] != ';') {
      nob_da_append(&parsed_cct_statement, ';');
    }

    const int n = line_index;

    nob_sb_append_null(&parsed_cct_statement);
    nob_sb_appendf(&sb,
                   "static void do_%d(void){"
                   "\n#define $$(str) write_labeled_output(\"%d\", str);\n"
                   "%s"
                   "\n#undef $$\n"
                   "}\n",
                   // b.line->index, b.line->index, parsed_cct_statement.items);
                   n, n, parsed_cct_statement.items);

    sb_appendf(&main_body, "  do_%d(); // execute statement %d \n", n, n);

    free(b.line->text);
    b.line->text = temp_sprintf("// CCT_MARK $%d", n);
  }

  nob_sb_append_cstr(&sb, "int main(void){\n"
                          "  __cct_file = fopen(\"");

  nob_sb_append_cstr(&sb, vals_path);
  nob_sb_append_cstr(
      &sb, "\", \"wb\"); if(!__cct_file){ perror(\"runner\"); return 1; }\n");

  sb_append_buf(&sb, main_body.items, main_body.count);

  nob_sb_append_cstr(&sb, "  fclose(__cct_file);\n"
                          "  return 0;\n"
                          "}\n");
  nob_sb_append_null(&sb);
  return sb.items;
}

typedef struct {
  size_t line_index;
  char *replacement;
} LineReplacement;

typedef struct {
  LineReplacement *items;
  size_t count, capacity;
} LineReplacements;

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
    int i = atoi(nob_temp_sv_to_cstr(index_str));
    printf("LINE:: i is %zu\n - value is %s\n", (size_t)i,
           strdup(nob_temp_sv_to_cstr(line)));

    LineReplacement repl = (LineReplacement){
        .line_index = (size_t)i,
        .replacement = nob_temp_strdup(nob_temp_sv_to_cstr(line)),
    };

    da_append(repls, repl);
  }
}

void parse_source_file(const char *filename, SourceCode *sc) {
  String_Builder text_buf = {0};

  if (!read_entire_file(filename, &text_buf)) {
    nob_log(ERROR, "could not read source file %s", filename);
    exit(1);
  }

  sc->source = text_buf;
  sc->lines = lines_from_source(&text_buf);
  printf("source code has %zu lines\n", sc->lines.count);
}

// Build one TU: returns path to rewritten .c (owned by temp arena)
static const char *process_tu(const char *src, Parsed_Argv *pa,
                              size_t tu_index) {
  // paths
  // if (!mkdir_if_not_exists("build")) { /*noop*/
  // }
  // if (!mkdir_if_not_exists(TMP_DIR)) { /*noop*/
  // }

  nob_log(INFO, "processing %s\n", src);

  // const char *base = path_name(src);
  // const char *stem = base; // naive; could strip .c
  Nob_String_Builder pp_path = {0}, vals_path = {0}, runner_c = {0},
                     runner_exe = {0}, final_c = {0};
#ifdef _WIN32
  const char *exe_ext = ".exe";
#else
  const char *exe_ext = "";
#endif

  // sb_appendf(&pp_path, "%s/%s.%zu.pp.c", TMP_DIR, stem, tu_index);
  // sb_appendf(&vals_path, "%s/%s.%zu.vals", TMP_DIR, stem, tu_index);
  // sb_appendf(&runner_c, "%s/%s.%zu.runner.c", TMP_DIR, stem, tu_index);
  // sb_appendf(&runner_exe, "%s/%s.%zu.runner%s", TMP_DIR, stem, tu_index,
  //            exe_ext);
  // sb_appendf(&final_c, "%s/%s.%zu.final.c", TMP_DIR, stem, tu_index);

  sb_appendf(&pp_path, "%s.%zu.pp.c", src, tu_index);
  sb_appendf(&vals_path, "%s.%zu.vals", src, tu_index);
  sb_appendf(&runner_c, "%s.%zu.runner.c", src, tu_index);
  sb_appendf(&runner_exe, "%s.%zu.runner%s", src, tu_index, exe_ext);
  sb_appendf(&final_c, "%s.%zu.final.c", src, tu_index);

  sb_append_null(&pp_path);
  sb_append_null(&vals_path);
  sb_append_null(&runner_c);
  sb_append_null(&runner_exe);

  nob_log(INFO, "Processing input source as SourceCode");
  SourceCode input_sc = {0};
  parse_source_file(src, &input_sc);

  nob_log(INFO, "Preprocessing with comments preserved");
  // 1) preprocess with comments preserved
  {
    Nob_Cmd cmd = {0};
    nob_cmd_append(
        &cmd, Parsed_Argv_compiler_name(pa), "-E", "-P", "-CC",
        /* rename a potenial "main" func to avoid entry point conflicts */
        "-Dmain=__user_main");

    append_argv_pointers_to_cmd(pa, &pa->not_input_files, &cmd);

    // yes we are appending -o again to overwrite any user -o
    // is this sustainable idk
    // it saves us from having to parse -o <file> out of argv
    nob_cmd_append(&cmd, "-o", pp_path.items, src);
    if (!nob_cmd_run(&cmd))
      return NULL;
  }

  // 2) read preprocessed text
  nob_log(INFO, "Read preprocessed text");
  String_Builder pp_text = {0};
  if (!read_entire_file(pp_path.items, &pp_text))
    return NULL;

  // 3) extract blocks
  SourceCode pp_source = {0};
  pp_source.source = pp_text;
  pp_source.lines = lines_from_source(&pp_text);
  printf("source code has %zu lines\n", pp_source.lines.count);

  Blocks ctx_blocks = {0}, do_blocks = {0};
  Context context = {.og_sc = &input_sc, .pp_sc = &pp_source};

  // MARK_CTX_END, &ctx_blocks);
  find_blocks(&pp_source, MARK_DO_BEG, MARK_DO_END, &do_blocks);

  // 4) generate runner.c
  char *runner_src = gen_runner_c(&context, &do_blocks, vals_path.items);

  if (!nob_write_entire_file(runner_c.items, runner_src, strlen(runner_src)))
    return NULL;
  NOB_FREE(runner_src);

  // 5) compile runner with
  // same includes/defines/lang
  // flags
  {
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, Parsed_Argv_compiler_name(pa));

    // forward relevant flags
    append_argv_pointers_to_cmd(pa, &pa->not_input_files, &cmd);
    // overwrite optimisation to avoid long compile times
    //
    nob_cmd_append(&cmd, "-O0");

    nob_cc_output(&cmd, runner_exe.items);
    nob_cc_inputs(&cmd, runner_c.items);
    if (!nob_cmd_run(&cmd))
      return NULL;
  }

  // 6) run runner -> vals
  {
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, runner_exe.items);
    if (!nob_cmd_run(&cmd))
      return NULL;
    // runner writes vals_path
    // itself
  }

  // 7) parse vals, rewrite TU
  {
    LineReplacements final_repls = {0};

    parse_vals_file(vals_path.items, &final_repls);
    nob_log(INFO, "performing %zu replacements", final_repls.count);
    int i = final_repls.count - 1;
    while (i >= 0) {
      LineReplacement repl = final_repls.items[i];
      Line *sc_line = lines_find_line_index(&input_sc.lines, repl.line_index);

      nob_log(INFO, "line index : %zu\n", repl.line_index);
      assert(sc_line != NULL);

      nob_log(INFO,
              "(%d) applying repl at line %zu: \n -> REPLACE WITH %s\n -> SC "
              "LINE IS %s \n",
              i, repl.line_index, repl.replacement, sc_line->text);

      // Line *line = lines_find_line_index(&pp_source.lines, repl.line_index);
      Line *line = lines_find_line_index(&input_sc.lines, repl.line_index);

      if (!line) {
        nob_log(ERROR, "could not find line %zu for replacement",
                repl.line_index);
        break;
      }
      // inject the replacement
      Line *line_repl = malloc(sizeof(Line));
      line_repl->text = repl.replacement;

      line_repl->next = line->next;
      line->next = line_repl;
      line->text = "/* REPLACED */";

      i -= 1;

      // if (i <= 0) {
      //   break;
      // }
    };
  }

  String_Builder final_c_source = {0};
  // lines_to_sb(&pp_source.lines, &final_c_source);
  lines_to_sb(&input_sc.lines, &final_c_source);

  if (!nob_write_entire_file(final_c.items, final_c_source.items,
                             final_c_source.count))
    return NULL;

  // if (pa->cct_flags.)
  bool remove_inter = true;
  da_foreach(CCT_Flag, flag, &pa->cct_flags) {
    switch (flag->kind) {
    case CCT_Flag_Kind_Debug:
      remove_inter = false;
      break;
    }
  }

  if (remove_inter) {
    nob_log(INFO, "removing intermediate files");

  } else {
    nob_log(INFO,
            "keeping intermediate files as requested by -cct--keep-inter");
  }
  return nob_temp_strdup(final_c.items);
}

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
  nob_log(NOB_INFO, "Received %d arguments", argc);
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
  Nob_Cmd final = {0};
  nob_cmd_append(&final, Parsed_Argv_compiler_name(&parsed_argv));
  // char **new_argv = clone_argv(argc, argv);

  nob_da_foreach(int, index, &parsed_argv.input_files) {

    const char *f = argv[*index];
    const char *rew = process_tu(f, &parsed_argv, *index);

    nob_log(INFO, "Rewrote argv[%d] %s -> %s", *index, f, rew);
    if (!rew) {
      nob_log(ERROR, "failed to process TU %s", f);
      exit(1);
    }
    nob_cmd_append(&final, rew);
    // new_argv[*index] = (char *)rew;
  }

  nob_da_foreach(int, index, &parsed_argv.not_input_files) {
    nob_cmd_append(&final, argv[*index]);
  }

  // Build final argv by
  // replacing inputs with
  // rewritten paths
  // Nob_Cmd final = {0};
  // nob_cmd_append(&final, Parsed_Argv_compiler_name(&parsed_argv));

  // for (int i = 2; i < argc; i++) {
  //   const char *tok = new_argv[i];
  //   nob_cmd_append(&final, tok);
  // }

  nob_cmd_append(&final, "-D__user_main=main");
  if (!nob_cmd_run(&final))
    return 1;
  return 0;
}
