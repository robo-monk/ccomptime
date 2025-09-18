#include "ansi.h"
#include <stddef.h>
#define HASHMAP_IMPLEMENTATION
#include "deps/hashmap/hashmap.h"

#define NOB_IMPLEMENTATION
#include "nob.h"

#define TS_INCLUDE_SYMBOLS
#include "tree_sitter_c_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define fatal(s)                                                               \
  fflush(stdout);                                                              \
  printf("\n" RED("[FATAL] "s                                                  \
                  "\n"));                                                      \
  exit(1);

extern const TSLanguage *tree_sitter_c(void);

TSParser *cparser;

typedef struct {
  String_View *items;
  size_t count, capacity;
} Strings;

void strings_append(Strings *strings, String_View item) {
  nob_da_append(strings, item);
}

Strings strings_new(size_t capacity) {
  Strings strings = {.count = 0,
                     .capacity = capacity,
                     .items = malloc(sizeof(String_View) * capacity)};
  return strings;
}

typedef struct {
  TSNode identifier;
  Strings arg_names;
  // TSNode body;
  //
  // const char *src;

  TSTree *body_tree;
  const char *body_src;

} MacroDefinition;

typedef struct {
  String_Builder definitions;
  String_Builder main;
} RunnerBuilder;

typedef struct {
  String_Builder out_h;
  int comptime_count;
  HashMap macros;

  RunnerBuilder runner;
} WalkContext;

// #define node_len(n) (int)(ts_node_end_byte(n) - ts_node_start_byte(n))
// #define node_start(n) src + ts_node_start_byte(n)

typedef struct {
  const char *start;
  int len;
} NodeRange;

NodeRange ts_node_range(TSNode node, const char *src) {
  const char *start = src + ts_node_start_byte(node);
  int len = ts_node_end_byte(node) - ts_node_start_byte(node);
  return (NodeRange){start, len};
}

#define ts_node_len_start_tuple(node, src)                                     \
  ts_node_range(node, src).len, ts_node_range(node, src).start

#define ts_node_start_len_tuple(node, src)                                     \
  ts_node_range(node, src).start, ts_node_range(node, src).len

void macros_put(HashMap *macros, const char *key, int key_len,
                MacroDefinition *def) {
  hashmap_put2(macros, (char *)key, key_len, def);
}

MacroDefinition *macros_get(HashMap *macros, const char *key, int key_len) {
  return (MacroDefinition *)hashmap_get2(macros, (char *)key, key_len);
}

String_View ts_node_to_str_view(TSNode node, const char *src) {
  const char *start = src + ts_node_start_byte(node);
  int len = ts_node_end_byte(node) - ts_node_start_byte(node);
  return (String_View){.data = start, .count = len};
}

typedef struct {
  TSNode node;
  String_View with;
} Macro_Replacement;

typedef struct {
  Macro_Replacement *items;
  size_t count, capacity;
} Macro_Replacements;

static void _expand_macro_node(TSNode node, const char *src, Strings *arg_names,
                               Strings *arg_values,
                               Macro_Replacements *replacements) {

  switch (ts_node_symbol(node)) {
  case sym_identifier: { // identifier
    assert(arg_names->count == arg_values->count);

    for (int i = 0; i < arg_names->count; i++) {
      String_View arg_name = arg_names->items[i];

      if (ts_node_range(node, src).len != arg_name.count)
        continue;

      assert(src && "src should be non null");
      // printf("node_len(node): %d, node_len(arg_node): %d [ASSESSING:
      // %.*s]\n",
      //        node_len(node), node_len(arg_node), node_len(arg_node),
      //        node_start(arg_node));

      if (memcmp(ts_node_range(node, src).start, arg_name.data,
                 arg_name.count) == 0) {

        String_View arg_value = arg_values->items[i];
        // printf("~> Replacing (arg:%d) '%.*s' with: %.*s\n", i,
        //        (int)arg_name.count, arg_name.data, (int)arg_value.count,
        //        arg_value.data);

        Macro_Replacement repl = {.node = node, .with = arg_value};
        nob_da_append(replacements, repl);
      }
    }
    break;
  }
  }

  uint32_t n = ts_node_child_count(node);
  for (uint32_t i = 0; i < n; i++) {
    _expand_macro_node(ts_node_child(node, i), src, arg_names, arg_values,
                       replacements);
  }
}

static void expand_macro_tree(MacroDefinition *macro_def, Strings arg_values,
                              String_Builder *out_sb) {

  assert(macro_def != NULL);
  assert(macro_def->arg_names.count == arg_values.count);
  nob_log(INFO, "Expand macro tree with %zu args", arg_values.count);
  TSNode root = ts_tree_root_node(macro_def->body_tree);
  NodeRange root_range = ts_node_range(root, macro_def->body_src);

  Macro_Replacements replacements = {0};
  _expand_macro_node(root, macro_def->body_src, &macro_def->arg_names,
                     &arg_values, &replacements);

  nob_log(INFO, "Macro expansion replacements: %zu", replacements.count);

  assert(root_range.start == macro_def->body_src);

  char *cursor = (char *)macro_def->body_src;
  nob_da_foreach(Macro_Replacement, it, &replacements) {
    NodeRange r = ts_node_range(it->node, macro_def->body_src);
    assert(r.start >= cursor);

    // copy everything from cursor to r.start to out
    nob_sb_append_buf(out_sb, cursor, r.start - cursor);
    // copy the replacement
    nob_sb_append_buf(out_sb, it->with.data, (it->with.count));
    // set cursor to end of the range
    cursor = (char *)r.start + r.len;
  }
  // flush remaining
  nob_sb_append_buf(out_sb, cursor, root_range.start + root_range.len - cursor);
}

static bool ts_node_is_comptime_kw(TSNode node, const char *src) {
  assert(ts_node_symbol(node) == sym_identifier);
  return strlen("_Comptime") == ts_node_range(node, src).len &&
         memcmp(ts_node_range(node, src).start, "_Comptime",
                ts_node_range(node, src).len) == 0;
}

static bool _has_comptime_identifier(TSNode node, const char *src) {
  if (ts_node_symbol(node) == sym_identifier) {
    return ts_node_is_comptime_kw(node, src);
  }

  uint32_t n = ts_node_child_count(node);
  for (uint32_t i = 0; i < n; i++) {
    if (_has_comptime_identifier(ts_node_child(node, i), src)) {
      return true;
    }
  }
  return false;
}

static bool has_comptime_identifier(TSTree *tree, const char *src) {
  assert(tree != NULL);
  return _has_comptime_identifier(ts_tree_root_node(tree), src);
}

// static void debug_tree
static void debug_tree_node(TSNode node, const char *src, int depth) {

  for (unsigned i = 0; i < depth; i++)
    putchar(' ');

  printf("- [%s] (%d)", ts_node_type(node), ts_node_symbol(node));

  printf("  // %.*s", ts_node_range(node, src).len,
         ts_node_range(node, src).start);

  putchar('\n');

  uint32_t n = ts_node_child_count(node);
  for (uint32_t i = 0; i < n; i++) {
    debug_tree_node(ts_node_child(node, i), src, depth + 2);
  }
}

static void debug_tree(TSTree *tree, const char *src, int depth) {
  TSNode root = ts_tree_root_node(tree);
  debug_tree_node(root, src, depth);
}

typedef struct {
  bool is_inside_function_body;
  bool is_assigning_to_var;
  TSNode *preproc_def_root;
  TSNode *call_expression_root;
  // TSNode *preproc_func_def_root;
  bool is_within_compound;
  int child_idx;
} LocalContext;
static void walk(WalkContext *const ctx, LocalContext local, TSNode node,
                 const char *src, unsigned depth) {

  for (unsigned i = 0; i < depth; i++)
    putchar(' ');
  printf("- [%s] (%d)", ts_node_type(node), ts_node_symbol(node));

  TSSymbol sym = ts_node_symbol(node);
  switch (sym) {
  case sym_identifier: { // identifier

    const char *key = ts_node_range(node, src).start;
    int key_len = ts_node_range(node, src).len;

    MacroDefinition *macro_def = macros_get(&ctx->macros, key, key_len);

    Nob_String_Builder out_sb = {0};
    if (macro_def) {
      if (macro_def->arg_names.count > 0) {
        // macro call with arguments, we must be inside a call expression
        assert(
            local.call_expression_root &&
            "macro call with args must be inside a `call expression`"
            "(you might be calling the macro correctly but we are not "
            "supporting stuff like this: macro(bing bong bing) <- "
            "technically correct but the parser gets confused. the arguments "
            "should look like a real function call for now. sorry.)");

        TSNode argument_list = ts_node_child(*local.call_expression_root, 1);
        assert(ts_node_symbol(argument_list) ==
               sym_argument_list /* argument_list */);

        Strings arg_values = strings_new(ts_node_child_count(argument_list));

        for (int i = 0; i < ts_node_child_count(argument_list); i++) {
          TSNode arg = ts_node_child(argument_list, i);
          TSSymbol arg_sym = ts_node_symbol(arg);

          if (arg_sym == anon_sym_LPAREN    /* ( */
              || arg_sym == anon_sym_COMMA  /* , */
              || arg_sym == anon_sym_RPAREN /* ) */
          ) {
            continue;
          }

          strings_append(&arg_values, ts_node_to_str_view(arg, src));
        }

        expand_macro_tree(macro_def, arg_values, &out_sb);
      } else {
        printf("  [MACRO CALL: %.*s]", key_len, key);
        expand_macro_tree(macro_def, (Strings){0}, &out_sb);
      }

      nob_log(INFO, "Macro expansion result: %.*s", (int)out_sb.count,
              out_sb.items);

      // now reparse the output and dive in
      TSTree *tree =
          ts_parser_parse_string(cparser, NULL, out_sb.items, out_sb.count);
      TSNode root = ts_tree_root_node(tree);
      printf("\n-----------------------------------------------\n");
      walk(ctx, local, root, out_sb.items, depth + 4);
      printf("\n-----------------------------------------------\n");
      return;
    }
    break;
  }

  case sym_function_definition: // function_definition
    local.is_inside_function_body = true;
    break;
  case sym_init_declarator: // init_declarator
    local.is_assigning_to_var = true;
    break;
  case 165: // preproc_def
    local.preproc_def_root = &node;
    break;
  case sym_call_expression: /* call exresssion */
    local.call_expression_root = &node;
    break;
  case sym_preproc_function_def: {

    // MacroDefinition macro = {0};
    MacroDefinition *macro = calloc(1, sizeof(MacroDefinition));
    // macro->src = src;

    uint32_t n = ts_node_child_count(node);
    assert(ts_node_symbol(ts_node_child(node, 0)) ==
           aux_sym_preproc_def_token1 /* #define */);

    macro->identifier = ts_node_child(node, 1); // should be identifier
    assert(ts_node_symbol(macro->identifier) == sym_identifier);

    TSNode preproc_params = ts_node_child(node, 2); // should be preproc_params
    assert(ts_node_symbol(preproc_params) == sym_preproc_params);

    uint32_t param_count = ts_node_child_count(preproc_params);
    for (int i = 0; i < param_count; i++) {
      TSNode param = ts_node_child(preproc_params, i);
      if (ts_node_symbol(param) != sym_identifier)
        continue; // commas and stuff

      assert(ts_node_symbol(param) == sym_identifier);
      uint32_t start = ts_node_start_byte(param);
      uint32_t end = ts_node_end_byte(param);
      printf("Param %d: %.*s\n", i, (int)(end - start), src + start);

      nob_da_append(&macro->arg_names, ts_node_to_str_view(param, src));
    }
    printf("got in total of %zu args\n", macro->arg_names.count);

    TSNode body = ts_node_child(node, 3); // should be the body
    assert(ts_node_symbol(body) == sym_preproc_arg /* preproc_arg */);

    // we only care if it includes a `comptime`, otherwise skip expansion
    TSTree *tree = ts_parser_parse_string(cparser, NULL,
                                          ts_node_start_len_tuple(body, src));

    if (!has_comptime_identifier(tree, ts_node_range(body, src).start)) {
      nob_log(INFO,
              "'%.*s' macro has been proven irrelevant because it does not "
              "contain the `comptime` keyword",
              ts_node_len_start_tuple(macro->identifier, src));
    } else {
      nob_log(INFO, "'%.*s' macro is a *comptime* macro ",
              ts_node_len_start_tuple(macro->identifier, src));
    }

    macro->body_tree = tree;
    macro->body_src = ts_node_range(body, src).start;

    /*
        printf("\n---- MACRO DEF SUB TREE ----\n");
        debug_tree(tree, ts_node_range(body, src).start, 4);
        printf("\n^^^^^^^ MACRO DEF SUB TREE ----\n");
    */

    macros_put(&ctx->macros, ts_node_range(macro->identifier, src).start,
               ts_node_range(macro->identifier, src).len, macro);
    return;
  }

    // return;
  case 241: // compound_statement
    local.is_within_compound = true;
    break;
  }

  // Print token text for identifiers (best-effort)
  if (sym == sym_identifier) {
    printf(" ~~");
    if (ts_node_is_comptime_kw(node, src)) {
      printf(BLUE("  [contains 'comptime'] (%d)"), ctx->comptime_count);
      if (local.preproc_def_root) {
        if (local.child_idx == 1) {
          fatal("Redefining `comptime` macro is not supported");
        }
      }

      if (local.is_assigning_to_var) {
        nob_sb_appendf(&ctx->out_h,
                       "#define _cct_%d _CCOMPTIME_OUTPUT_%d ? 0 :\n",
                       ctx->comptime_count, ctx->comptime_count);
      } else if (local.is_inside_function_body) {
        nob_sb_appendf(&ctx->out_h,
                       "#define _cct_%d _CCOMPTIME_OUTPUT_%d;if(0)\n",
                       ctx->comptime_count, ctx->comptime_count);
      } else {
        nob_sb_appendf(&ctx->out_h, "#define _cct_%d void _cct_fn_%d(void)\n",
                       ctx->comptime_count, ctx->comptime_count);
      }

      if (ts_node_is_comptime_kw(node, src) && local.call_expression_root) {
        TSNode argument_list = ts_node_child(*local.call_expression_root, 1);
        assert(ts_node_symbol(argument_list) == sym_argument_list);
        // the code to execute is within the argument_list:
        String_View _comptime_code = ts_node_to_str_view(argument_list, src);
        assert(_comptime_code.data[0] == '(');
        printf("\n\n\n\n----> %c\n\n\n\n",
               _comptime_code.data[_comptime_code.count - 1]);
        assert(_comptime_code.data[_comptime_code.count - 1] == ')');
        // assert(true);

        char *start =
            (char *)_comptime_code.data + 1; // skip the initial paren;
        char *end = (char *)_comptime_code.data + _comptime_code.count - 2;

        // trim initial white space
        while (*start == ' ' || *start == '\t' || *start == '{') {
          start++;
        }
        while (*end == ' ' || *end == '\t' || *end == '}' || *end == ';') {
          end--;
        }

        nob_log(INFO, "\n=== FOUND _Comptime CODE ===\n%.*s\n=============",
                (int)(end - start + 1), start);

        // call the function in the main;

        nob_sb_appendf(&ctx->runner.definitions,
                       "\nvoid _Comptime__exec%d(void){\n%.*s;\n}\n",
                       ctx->comptime_count, (int)(end - start + 1), start);

        nob_sb_appendf(
            &ctx->runner.main,
            "_Comptime__exec%d(); // execute comptime statement #%d\n",
            ctx->comptime_count, ctx->comptime_count);
      }

      ctx->comptime_count++;
    }
  }
  putchar('\n');

  uint32_t n = ts_node_child_count(node);
  for (uint32_t i = 0; i < n; i++) {
    local.child_idx = i;
    walk(ctx, local, ts_node_child(node, i), src, depth + 2);
  }
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s <file.c>\n", argv[0]);
    return 1;
  }

  String_Builder src = {0};
  const char *filename = argv[1];
  read_entire_file(filename, &src);

  nob_sb_append_null(&src);
  // char *src = read_file(argv[1], &len);
  if (!src.items) {
    fprintf(stderr, "failed to read %s\n", argv[1]);
    return 2;
  }

  cparser = ts_parser_new();
  ts_parser_set_language(cparser, tree_sitter_c());

  TSTree *tree = ts_parser_parse_string(cparser, NULL, src.items, src.count);

  TSNode root = ts_tree_root_node(tree);
  WalkContext walk_ctx = (WalkContext){0};
  sb_appendf(&walk_ctx.out_h,
             "/*// @generated - ccomptime™ v0.0.1 - %lu \\*/\n", time(NULL));
  sb_appendf(&walk_ctx.out_h, "#define _CONCAT_(x, y) x##y\n"
                              "#define CONCAT(x, y) _CONCAT_(x, y)\n"
                              "#define comptime CONCAT(_cct_, __COUNTER__)\n");

  walk(&walk_ctx, (LocalContext){0}, root, src.items, 0);
  sb_appendf(&walk_ctx.out_h, "/* ---- */// the end. ///* ---- */\n");
  printf("comptime found : %d", walk_ctx.comptime_count);

  printf("\n\nGenerated header:\n%s\n", walk_ctx.out_h.items);

  String_Builder runner_file = {0};
  sb_appendf(&runner_file, "#define main _User_main // overwriting the entry "
                           "point of the program\n");
  sb_appendf(&runner_file, "#include \"%s\"\n", filename);
  sb_appendf(&runner_file, "#undef main\n");

  sb_append_buf(&runner_file, walk_ctx.runner.definitions.items,
                walk_ctx.runner.definitions.count);

  sb_appendf(&runner_file, "\nint main(void) {\n%.*s}",
             (int)walk_ctx.runner.main.count, walk_ctx.runner.main.items);

  write_entire_file("./test-runner.c", runner_file.items, runner_file.count);

  write_entire_file("./test-syntax.h", walk_ctx.out_h.items,
                    walk_ctx.out_h.count);

  ts_tree_delete(tree);
  ts_parser_delete(cparser);
  sb_free(src);
  sb_free(walk_ctx.out_h);
  return 0;
}
