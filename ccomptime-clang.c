#include <stdint.h>
#define NOB_STRIP_PREFIX
#define NOB_IMPLEMENTATION
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
} Lines;

typedef struct {
  Lines lines;
  String_Builder source;
} SourceCode;

typedef struct {
  int *items;
  size_t count, capacity;
} ArgvPointers;

typedef struct {
  int arc;
  char **argv;
  ArgvPointers input_files;
  ArgvPointers not_input_files;
} Parsed_Argv;

Parsed_Argv parse_argv(int argc, char **argv) {
  Parsed_Argv parsed_argv = {
      .argv = argv,
      .arc = argc,
      .input_files = {0},
      .not_input_files = {0},
  };

  for (int i = 1; i < argc; i++) {
    if (argv[i][0] == '-') {
      da_append(&parsed_argv.not_input_files, i);
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

void append_argv_pointers_to_cmd(Parsed_Argv *pa, ArgvPointers *pointers,
                                 Cmd *cmd) {
  nob_da_foreach(int, index, pointers) {
    const char *f = pa->argv[*index];
    nob_cmd_append(cmd, f);
  }
}

Line *lines_find_line_index(Lines *line, size_t index) {
  for (Line *l = line->head; l != NULL; l = l->next) {
    if (l->index == index) {
      return l;
    }
  }
  return NULL;
}

void lines_append_new(Lines *lines, char *text) {
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
}

void lines_to_sb(Lines *lines, String_Builder *sb) {
  for (Line *line = lines->head; line != NULL; line = line->next) {
    sb_appendf(sb, "%s\n", line->text);
  }
  sb_append_null(sb);
}

Lines lines_from_source(String_Builder *source) {
  Lines lines = {0};

  String_Builder buffer = {0};
#define append_buffer_to_lines()                                               \
  do {                                                                         \
    sb_append_null(&buffer);                                                   \
    lines_append_new(&lines, strdup(buffer.items));                            \
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
  // parses "__CCT_STMT_$n = ""
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

static char *gen_runner_c(SourceCode *sc, Blocks *dos, const char *vals_path) {

  Nob_String_Builder sb = {0};

  // nob_sb_appendf(
  //     &sb, "#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n\n"
  //          "// --- Context ---\n");

  sb_appendf(&sb, "\n#define COMPTIME\n");
  sb_appendf(&sb, "\n#define CCTRUNNER\n");

  sb_appendf(&sb, "\n// -- SOURCE FILE START -- \n");
  sb_append_buf(&sb, sc->source.items, sc->source.count - 1);
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
  // nob_sb_append_cstr(&sb, source);

  // nob_sb_appendf(
  //     &sb, "#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n\n"
  //          "// --- Context ---\n");

  String_Builder main_body = {0};

  for (size_t i = 0; i < dos->count; i++) {
    Block b = dos->items[i];
    printf("\n>block starts at %zu ends at %zu\n", b.start, b.end);
    Nob_String_Builder out = {0};

    parse_cct_statement(b.line->text, &out);
    // b.line->index;
    // const int n = parse_cct_statement(b.line->text, &out);
    //

    if (out.items[out.count] != ';') {
      nob_da_append(&out, ';');
    }

    nob_sb_append_null(&out);
    nob_sb_appendf(&sb,
                   "static void do_%zu(void){"
                   "\n#define $$(str) write_labeled_output(\"%zu\", str);\n"
                   "%s"
                   "\n#undef $$\n"
                   "}\n",
                   b.line->index, b.line->index, out.items);

    sb_appendf(&main_body, "  do_%zu(); // execute statement %zu \n",
               b.line->index, b.line->index);

    free(b.line->text);
    b.line->text = temp_sprintf("// CCT_MARK $%zu", b.line->index);
    // const char *mark_comment = temp_sprintf("//CCT_MARK$%d", n);
    // strcpy(source->items + b.start, mark_comment);
    // for (char *ptr = source->items + b.start + strlen(mark_comment);
    //      ptr < source->items + b.end; ptr++) {
    //   *ptr = ' ';
    // }
  }

  // ---- on_exit via macro-selected function pointer ----
  // user may define in CCT_CTX:
  //   #define on_exit my_fn
  //   // or
  //   #define on_exit exit_ptr
  // If not defined, default is NULL (no-op).
  nob_sb_append_cstr(&sb, "int main(void){\n"
                          "  __cct_file = fopen(\"");

  nob_sb_append_cstr(&sb, vals_path);
  nob_sb_append_cstr(
      &sb, "\", \"wb\"); if(!__cct_file){ perror(\"runner\"); return 1; }\n");

  sb_append_buf(&sb, main_body.items, main_body.count);

  // for (size_t i = 0; i < dos->count; i++)
  //   nob_sb_appendf(&sb, "  do_%zu();\n", i);
  // for (size_t i = 0; i < runi->count; i++)
  //   nob_sb_appendf(&sb, "  fprintf(f, \"I%zu=%%lld\\n\", eval_I%zu());\n", i,
  //                  i);
  // for (size_t i = 0; i < runs->count; i++)
  //   nob_sb_appendf(&sb,
  //                  "  fputs(\"S%zu=\", f); cct_write_escaped(f, eval_S%zu());
  //                  " "fputc('\\n', f);\n", i, i);

  nob_sb_append_cstr(&sb, "  fclose(__cct_file);\n"
                          "  #ifdef ON_EXIT\n ON_EXIT();\n#endif\n"
                          "  return 0;\n"
                          "}\n");
  nob_sb_append_null(&sb);
  return sb.items;
}

typedef struct {
  const char *real_cc; // underlying clang (env CC_REAL or "clang")
} Driver;

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
    String_View index_str = sv_chop_by_delim(&line, ':');
    int i = atoi(nob_temp_sv_to_cstr(index_str));
    printf("i is %d\n - value is %s\n", i, nob_temp_sv_to_cstr(line));

    LineReplacement repl = (LineReplacement){
        .line_index = (size_t)i,
        .replacement = nob_temp_strdup(nob_temp_sv_to_cstr(line)),
    };

    da_append(repls, repl);
    if (line.count == 0)
      break;
  }
}

// Build one TU: returns path to rewritten .c (owned by temp arena)
static const char *process_tu(const char *src, Parsed_Argv *pa, Driver drv,
                              size_t tu_index) {
  // paths
  if (!mkdir_if_not_exists("build")) { /*noop*/
  }
  if (!mkdir_if_not_exists(TMP_DIR)) { /*noop*/
  }

  nob_log(INFO, "processing %s\n", src);

  const char *base = path_name(src);
  const char *stem = base; // naive; could strip .c
  Nob_String_Builder pp_path = {0}, vals_path = {0}, runner_c = {0},
                     runner_exe = {0}, final_c = {0};
#ifdef _WIN32
  const char *exe_ext = ".exe";
#else
  const char *exe_ext = "";
#endif
  sb_appendf(&pp_path, "%s/%s.%zu.pp.c", TMP_DIR, stem, tu_index);
  sb_appendf(&vals_path, "%s/%s.%zu.vals", TMP_DIR, stem, tu_index);
  sb_appendf(&runner_c, "%s/%s.%zu.runner.c", TMP_DIR, stem, tu_index);
  sb_appendf(&runner_exe, "%s/%s.%zu.runner%s", TMP_DIR, stem, tu_index,
             exe_ext);
  sb_appendf(&final_c, "%s/%s.%zu.final.c", TMP_DIR, stem, tu_index);

  sb_append_null(&pp_path);
  sb_append_null(&vals_path);
  sb_append_null(&runner_c);
  sb_append_null(&runner_exe);

  // 1) preprocess with comments preserved
  {
    Nob_Cmd cmd = {0};
    nob_cmd_append(
        &cmd, "clang", "-E", "-P", "-CC",
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
  String_Builder pp_text = {0};
  if (!read_entire_file(pp_path.items, &pp_text))
    return NULL;

  nob_sb_append_null(&pp_text);

  // 3) extract blocks
  SourceCode pp_source = {0};
  pp_source.source = pp_text;
  pp_source.lines = lines_from_source(&pp_text);
  printf("source code has %zu lines\n", pp_source.lines.count);

  Blocks ctx = {0}, dos = {0};
  find_blocks(&pp_source, MARK_CTX_BEG, MARK_CTX_END, &ctx);
  find_blocks(&pp_source, MARK_DO_BEG, MARK_DO_END, &dos);

  // 4) generate runner.c
  char *runner_src = gen_runner_c(&pp_source, &dos, vals_path.items);

  if (!nob_write_entire_file(runner_c.items, runner_src, strlen(runner_src)))
    return NULL;
  NOB_FREE(runner_src);

  // 5) compile runner with
  // same includes/defines/lang
  // flags
  {
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "clang");

    // forward relevant flags
    append_argv_pointers_to_cmd(pa, &pa->not_input_files, &cmd);

    // nob_cmd_append(&cmd, "-include", "stdio.h");
    // nob_cmd_append(&cmd, "-include", "string.h");
    // nob_cmd_append(&cmd, "-include", "stdlib.h");

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

    for (size_t i = final_repls.count; i > 0; i--) {
      LineReplacement *repl = &final_repls.items[i];
      Line *line = lines_find_line_index(&pp_source.lines, repl->line_index);

      if (!line) {
        nob_log(ERROR, "could not find line %zu for replacement",
                repl->line_index);
      }
      // inject the replacement
      Line *line_repl = malloc(sizeof(Line));
      line_repl->text = repl->replacement;

      line_repl->next = line->next;
      line->next = line_repl;
    }
  }

  String_Builder final_c_source = {0};
  lines_to_sb(&pp_source.lines, &final_c_source);

  if (!nob_write_entire_file(final_c.items, final_c_source.items,
                             final_c_source.count))
    return NULL;
  return nob_temp_strdup(final_c.items);
}

typedef struct {
  char *filename;
  int argv_index;
} SourceFile;

typedef struct {
  char **items;
  size_t count, capacity;
} Flags;

typedef struct {
  int argc;
  char **argv;
  // Flags flags;
  // char **items;
  SourceFile *items;
  size_t count, capacity;
} Sources;

void detect_input_files(int argc, char **argv, Sources *sources) {
  for (int i = 0; i < argc; i++) {
    if (argv[i][0] == '-') {
      continue;
    }

    if (strstr(argv[i], ".c") || strstr(argv[i], ".C")) {
      nob_log(INFO, "Detected input file: %s\n", argv[i]);
      SourceFile sf = {
          .filename = argv[i],
          .argv_index = (int)i,
      };

      da_append(sources, sf);
    }
  }
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

  Driver drv = {0};
  drv.real_cc = "clang";

  Parsed_Argv parsed_argv = parse_argv(argc, argv);
  char **new_argv = clone_argv(argc, argv);

  nob_da_foreach(int, index, &parsed_argv.input_files) {
    const char *f = argv[*index];
    const char *rew = process_tu(f, &parsed_argv, drv, *index);

    argv[*index] = (char *)rew;
    nob_log(INFO, "Rewrote argv[%d] %s -> %s", *index, f, rew);
  }

  // // pre-process each TU
  // for (size_t i = 0; i < sources.count; i++) {
  //   const char *rew = process_tu(sources.items[i], argc, argv, drv, i);
  //   if (!rew)
  //     return 1;
  //   MPUSH(sources.items[i], rew);
  // }

  // Build final argv by
  // replacing inputs with
  // rewritten paths
  Nob_Cmd final = {0};
  nob_cmd_append(&final, drv.real_cc);
  for (int i = 1; i < argc; i++) {
    const char *tok = new_argv[i];
    nob_cmd_append(&final, tok);
  }

  nob_cmd_append(&final, "-D__user_main=main");

  // Execute underlying clang
  // with swapped sources
  if (!nob_cmd_run(&final))
    return 1;
  return 0;
}
