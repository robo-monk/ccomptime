// ccomptime-clang.c — drop-in clang driver with compile-time execution pass
// bootstrap: cc -O2 -o ccomptime-clang ccomptime-clang.c
// OR: use nob.c below.
//
// Runtime dependency: none (self-contained). Uses nob.h internally.

#define NOB_STRIP_PREFIX
#define NOB_IMPLEMENTATION
#include "nob.h"

#define CCT_MAIN "cct_main"

static const char *TMP_DIR = "build/cctmp";

static const char *MARK_CTX_BEG = "/*CCT_CTX_BEGIN*/";
static const char *MARK_CTX_END = "/*CCT_CTX_END*/";
static const char *MARK_RUNI_BEG = "/*CCT_RUNI_BEGIN";
static const char *MARK_RUNI_END = "/*CCT_RUNI_END*/";
static const char *MARK_RUNS_BEG = "/*CCT_RUNS_BEGIN*/";
static const char *MARK_RUNS_END = "/*CCT_RUNS_END*/";
static const char *MARK_DEFINE_BEG = "/*CCT_DEFINE_BEGIN*/";
static const char *MARK_DEFINE_END = "/*CCT_DEFINE_END*/";
static const char *MARK_DO_BEG = "/*CCT_DO_BEGIN*/";
static const char *MARK_DO_END = "/*CCT_DO_END*/";

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Line Line;

typedef struct {
  char **items;
  size_t count, capacity;
} VecStr;
static void vpush(VecStr *v, char *s) { nob_da_append(v, s); }

typedef struct {
  size_t start, end;
  Line *line;
} Block;

typedef struct {
  size_t count, capacity;
  Block *items;
} Blocks;

static char *dup_range(const char *a, const char *b) {
  size_t n = (size_t)(b - a);
  char *s = NOB_REALLOC(NULL, n + 1);
  NOB_ASSERT(s);
  memcpy(s, a, n);
  s[n] = 0;
  return s;
}

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

void lines_debug(Lines *lines) {
  nob_log(INFO, "-- DEBUG LINES (length: %zu) --", lines->count);
  for (Line *line = lines->head; line != NULL; line = line->next) {
    printf("%s\n", line->text);
  }
  printf("----\n");
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

  for (int i = 0; i < source->count; i++) {
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

  lines_debug(&lines);

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

static char *replace_blocks(const char *text, const char *beg, const char *end,
                            VecStr *repls) {
  Nob_String_Builder sb = {0};
  const char *p = text;
  size_t idx = 0;
  while (1) {
    const char *s = strstr(p, beg);
    if (!s) {
      nob_sb_append_cstr(&sb, p);
      break;
    }
    const char *e = strstr(s + strlen(beg), end);
    if (!e) {
      nob_sb_append_cstr(&sb, p);
      break;
    }
    nob_sb_append_buf(&sb, p, (size_t)(s - p));
    const char *r = (idx < repls->count) ? repls->items[idx] : "";
    nob_sb_append_cstr(&sb, r);
    idx++;
    p = e + strlen(end);
  }
  nob_sb_append_null(&sb);
  return sb.items;
}

static char *remove_blocks(const char *text, const char *beg, const char *end) {
  VecStr none = {0};
  return replace_blocks(text, beg, end, &none);
}

int parse_cct_statement(const char *line, Nob_String_Builder *out) {
  nob_log(INFO, "parsing line %s", line);
  int ii = 0;
  // parses "__CCT_STMT_$n = ""
  while (line[ii] != '$') {
    ii++;
  }

  const char *dollar = &line[ii];
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
  VecStr I, S;
} Values;

static bool parse_vals(const char *path, Values *vals) {
  Nob_String_Builder sb = {0};
  if (!nob_read_entire_file(path, &sb))
    return false;
  nob_sb_append_null(&sb);
  char *p = sb.items, *end = sb.items + sb.count;
  while (p < end) {
    char *nl = memchr(p, '\n', (size_t)(end - p));
    char *le = nl ? nl : end;
    if (le > p && (p[0] == 'I' || p[0] == 'S')) {
      int isS = (p[0] == 'S');
      char *eq = memchr(p, '=', (size_t)(le - p));
      if (eq) {
        size_t idx = (size_t)strtoul(p + 1, NULL, 10);
        char *val = dup_range(eq + 1, le);
        if (isS) {
          while (vals->S.count <= idx)
            vpush(&vals->S, nob_temp_strdup(""));
          vals->S.items[idx] = val;
        } else {
          while (vals->I.count <= idx)
            vpush(&vals->I, nob_temp_strdup("0"));
          vals->I.items[idx] = val;
        }
      }
    }
    p = nl ? nl + 1 : end;
  }
  nob_da_free(sb);
  return true;
}

// --- arg handling ------------------------------------------------------------

static int is_flag(const char *s) { return s && s[0] == '-'; }
static int looks_like_source(const char *s) {
  if (!s || s[0] == '-')
    return 0;
  size_t n = strlen(s);
  return (n > 2 && (nob_sv_end_with(nob_sv_from_cstr(s), ".c") ||
                    nob_sv_end_with(nob_sv_from_cstr(s), ".C")));
}

// Whitelist flags to forward to the preprocess/helper compile steps
static int is_pp_relevant_flag(const char *arg) {
  if (!arg || arg[0] != '-')
    return 0;
  // prefixes
  const char *pfx[] = {"-I",        "-isystem",    "-include", "-imacros", "-D",
                       "-U",        "-std",        "-target",  "-f",       "-m",
                       "-nostdinc", "-nostdinc++", "-x"};
  for (size_t i = 0; i < NOB_ARRAY_LEN(pfx); ++i) {
    size_t k = strlen(pfx[i]);
    if (strncmp(arg, pfx[i], k) == 0)
      return 1;
  }
  // two-arg forms handled by scanning the next token too; we return true here
  // for the flag, caller is responsible for also pushing the following value.
  const char *eq[] = {"-isystem", "-include", "-imacros", "-x"};
  for (size_t i = 0; i < NOB_ARRAY_LEN(eq); ++i) {
    if (strcmp(arg, eq[i]) == 0)
      return 1;
  }
  return 0;
}

typedef struct {
  const char *real_cc; // underlying clang (env CC_REAL or "clang")
  int have_E;          // global -E present
} Driver;

static const char *real_cc_path(void) {
  // const char *cc = nob_getenv("CC_REAL");
  return "clang";
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
static const char *process_tu(const char *src, int argc, char **argv,
                              Driver drv, size_t tu_index) {
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
        &cmd, drv.real_cc ? drv.real_cc : real_cc_path(), "-E", "-P", "-CC",
        /* rename a potenial "main" func to avoid entry point conflicts */
        "-Dmain=__user_main");
    // forward relevant flags (+ their args)
    for (int i = 1; i < argc; i++) {
      const char *a = argv[i];
      if (strcmp(a, "-o") == 0) {
        i++;
        continue;
      }
      if (is_pp_relevant_flag(a)) {
        nob_cmd_append(&cmd, a);
        // two-arg flags: add the next token if present and not another flag
        if ((strcmp(a, "-isystem") == 0 || strcmp(a, "-include") == 0 ||
             strcmp(a, "-imacros") == 0 || strcmp(a, "-x") == 0) &&
            i + 1 < argc && !is_flag(argv[i + 1])) {
          nob_cmd_append(&cmd, argv[++i]);
        }
      }
    }
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
  //
  SourceCode pp_source = {0};
  pp_source.source = pp_text;
  pp_source.lines = lines_from_source(&pp_text);
  printf("source code has %zu lines\n", pp_source.lines.count);

  // VecStr ctx = {0}, runi = {0}, runs = {0}, dos = {0}, defines = {0};
  Blocks ctx = {0}, defines = {0}, dos = {0};
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
    for (int i = 1; i < argc; i++) {
      const char *a = argv[i];
      if (is_pp_relevant_flag(a)) {
        nob_cmd_append(&cmd, a);
        if ((strcmp(a, "-isystem") == 0 || strcmp(a, "-include") == 0 ||
             strcmp(a, "-imacros") == 0 || strcmp(a, "-x") == 0) &&
            i + 1 < argc && !is_flag(argv[i + 1])) {
          nob_cmd_append(&cmd, argv[++i]);
        }
      }
    }

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
    nob_log(INFO, "performing %d replacements", final_repls.count);

    for (size_t i = final_repls.count; i--; i > 0) {
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

int main(int argc, char **argv) {
  if (argc < 2) {
    nob_log(NOB_ERROR,
            "usage: %s "
            "[clang-like "
            "args] file.c ...",
            argv[0]);
    return 1;
  }

  Driver drv = {0};
  drv.real_cc = real_cc_path();

  // collect inputs; also
  // detect global -E
  VecStr sources = {0};
  int dashdash = 0;
  for (int i = 1; i < argc; i++) {
    if (!dashdash && strcmp(argv[i], "--") == 0) {
      dashdash = 1;
      continue;
    }
    if (strcmp(argv[i], "-E") == 0)
      drv.have_E = 1;
    if (!dashdash && looks_like_source(argv[i]))
      vpush(&sources, argv[i]);
  }

  // map old source ->
  // rewritten path
  typedef struct {
    const char *oldp;
    const char *newp;
  } Map;
  Map *maps = NULL;
  size_t maps_n = 0, maps_cap = 0;
#define MPUSH(o, n)                                                            \
  do {                                                                         \
    if (maps_n == maps_cap) {                                                  \
      maps_cap = maps_cap ? maps_cap * 2 : 8;                                  \
      maps = NOB_REALLOC(maps, maps_cap * sizeof(Map));                        \
    }                                                                          \
    maps[maps_n++] = (Map){(o), (n)};                                          \
  } while (0)

  // pre-process each TU
  for (size_t i = 0; i < sources.count; i++) {
    const char *rew = process_tu(sources.items[i], argc, argv, drv, i);
    if (!rew)
      return 1;
    MPUSH(sources.items[i], rew);
  }

  // Build final argv by
  // replacing inputs with
  // rewritten paths
  Nob_Cmd final = {0};
  nob_cmd_append(&final, drv.real_cc);
  for (int i = 1; i < argc; i++) {
    const char *tok = argv[i];
    if (looks_like_source(tok)) {
      // replace
      const char *rep = tok;
      for (size_t j = 0; j < maps_n; j++) {
        if (strcmp(maps[j].oldp, tok) == 0) {
          rep = maps[j].newp;
          break;
        }
      }
      nob_cmd_append(&final, rep);
      continue;
    }
    // pass-thru everything
    // else as-is
    nob_cmd_append(&final, tok);
    // NOTE: keep two-arg flags
    // paired; clang expects
    // the next token. We
    // forward unchanged.
    if ((strcmp(tok, "-o") == 0 || strcmp(tok, "-isystem") == 0 ||
         strcmp(tok, "-include") == 0 || strcmp(tok, "-imacros") == 0 ||
         strcmp(tok, "-x") == 0) &&
        i + 1 < argc) {
      nob_cmd_append(&final, argv[++i]);
    }
  }

  nob_cmd_append(&final, "-D__user_main=main");

  // Execute underlying clang
  // with swapped sources
  if (!nob_cmd_run(&final))
    return 1;
  return 0;
}
