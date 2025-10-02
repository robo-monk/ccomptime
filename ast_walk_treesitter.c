#include "ansi.h"
#include "utils.h"

#include <stddef.h>
#define HASHMAP_IMPLEMENTATION
#include "deps/hashmap/hashmap.h"

#include "cli.c"
#define NOB_IMPLEMENTATION
#include "nob.h"

#define TS_INCLUDE_SYMBOLS
#include "tree_sitter_c_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern const TSLanguage *tree_sitter_c(void);

TSParser *cparser;

typedef struct {
  TSNode identifier;
  Strings arg_names;
  TSTree *body_tree;
  const char *body_src;
} MacroDefinition;

typedef struct {
  String_Builder definitions;
  String_Builder main;
} C_FileBuilder;

typedef struct {
  const char *start;
  int len;
} Slice;

typedef struct {
  String_Builder out_h;
  int comptime_count;
  HashMap macros;
  C_FileBuilder out_c;

  struct {
    Slice *items;
    size_t count, capacity;
  } comptime_slices;
} WalkContext;

// #define node_len(n) (int)(ts_node_end_byte(n) - ts_node_start_byte(n))
// #define node_start(n) src + ts_node_start_byte(n)
Slice ts_node_range(TSNode node, const char *src) {
  const char *start = src + ts_node_start_byte(node);
  int len = ts_node_end_byte(node) - ts_node_start_byte(node);
  return (Slice){start, len};
}

static bool ts_node_is_comptime_kw(TSNode node, const char *src);
static bool ts_node_is_comptimetype_kw(TSNode node, const char *src);

// static void debug_tree
static void debug_tree_node(TSNode node, const char *src, int depth) {
#define MIN(x, y) x > y ? y : x
  for (unsigned i = 0; i < depth; i++)
    putchar('.');

  if (ts_node_has_error(node)) {
    // printf(RED("- (%d)"), ts_node_symbol(node));
    printf(RED("[%s] (%d)"), ts_node_type(node), ts_node_symbol(node));
  } else if (ts_node_symbol(node) == sym_identifier &&
             ts_node_is_comptime_kw(node, src)) {
    printf(MAGENTA(" [%s] (%d) [_Comptime]"), ts_node_type(node),
           ts_node_symbol(node));
  } else if (ts_node_symbol(node) == sym_identifier &&
             ts_node_is_comptimetype_kw(node, src)) {
    printf(ORANGE(" [%s] (%d) [_ComptimeType]"), ts_node_type(node),
           ts_node_symbol(node));
  } else {
    printf(BOLD("%s") " " GRAY("[%d]"), ts_node_type(node),
           ts_node_symbol(node));
  }

  printf(GRAY(" %.*s"), MIN(ts_node_range(node, src).len, 35),
         ts_node_range(node, src).start);

  printf(GRAY(" [%p]\n"), node.id);

  uint32_t n = ts_node_child_count(node);
  for (uint32_t i = 0; i < n; i++) {
    debug_tree_node(ts_node_child(node, i), src, depth + 2);
  }
}

static void debug_tree(TSTree *tree, const char *src, int depth) {
  TSNode root = ts_tree_root_node(tree);
  debug_tree_node(root, src, depth);
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

/* INFERENCE */
Slice infer(TSNode node, const char *src);

TSNode *dig_until_return(TSNode node) {
  if (ts_node_symbol(node) == sym_return_statement) {
    static TSNode found_node;
    found_node = node;
    return &found_node;
  }
  uint32_t n = ts_node_child_count(node);
  for (uint32_t i = 0; i < n; i++) {
    TSNode child = ts_node_child(node, i);
    TSNode *result = dig_until_return(child);
    if (result)
      return result;
  }
  return NULL;
}

#define _STATIC_TYPE_RANGE(type)                                               \
  { .start = type, .len = strlen(type) }

static Slice _VOID_RANGE = _STATIC_TYPE_RANGE("void");
static Slice _INT_RANGE = _STATIC_TYPE_RANGE("int");
static Slice _CHARPTR_RANGE = _STATIC_TYPE_RANGE("char*");
static Slice _CHAR_RANGE = _STATIC_TYPE_RANGE("char");
static Slice _VOIDPTR_RANGE = _STATIC_TYPE_RANGE("void*");

Slice infer_func_type_from_ret(TSNode node, const char *src) {
  assert(ts_node_symbol(node) == sym_function_definition);
  TSNode *ret_node = dig_until_return(node);
  if (!ret_node) {
    return _VOID_RANGE;
  }
  assert(ts_node_symbol(*ret_node) == sym_return_statement);
  assert(ts_node_symbol(ts_node_child(*ret_node, 0)) == anon_sym_return);
  return infer(ts_node_child(*ret_node, 1), src);
}

Slice infer(TSNode node, const char *src) {
  printf("\n--- Inference on node: ");
  debug_tree_node(node, src, 0);
  printf("\n---\n");
  switch (ts_node_symbol(node)) {
  case sym_primitive_type:
    return ts_node_range(node, src);
  case sym_function_definition: {
    assert(ts_node_child_count(node) > 0);
    TSNode type_node = ts_node_child(node, 0);
    if (ts_node_symbol(type_node) == sym_macro_type_specifier &&
        ts_node_is_comptimetype_kw(ts_node_child(type_node, 0), src)) {
      // we need to dig
      return infer_func_type_from_ret(node, src);
    } else {
      return infer(type_node, src);
    }
  case alias_sym_type_identifier:
    return _VOIDPTR_RANGE;
    // return ts_node_range(node, src);
  case sym_number_literal:
    return _INT_RANGE;
  case sym_char_literal:
    return _CHAR_RANGE;
  case sym_concatenated_string:
  case sym_string_literal:
    return _CHARPTR_RANGE;

  default: {
    printf("\nCannot infer: Unknown symbol: %d ('%s')\n", ts_node_symbol(node),
           ts_node_type(node));
    exit(1);
  }
  }
  }
}
/* --------- */

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

  assert(arg_names->count == arg_values->count);
  switch (ts_node_symbol(node)) {
  case sym_preproc_directive: {
    for (int i = 0; i < arg_names->count; i++) {
      String_View arg_name = arg_names->items[i];
      // String_View arg_name = {
      //     .count = preproc_name.count - 1,
      //     .data = preproc_name.data + 1 // skip the #
      // };
      Slice node_range = {
          .start = ts_node_range(node, src).start + 1,
          .len = ts_node_range(node, src).len - 1, // skip the #
      };

      nob_log(INFO, "~> %.*s", node_range.len, node_range.start);

      if (node_range.len != arg_name.count)
        continue;

      assert(src && "src should be non null");

      if (memcmp(node_range.start, arg_name.data, arg_name.count) == 0) {

        String_View arg_value = arg_values->items[i];
        // naive stringification:
        String_Builder stringified_value = {0};
        nob_sb_appendf(&stringified_value, "\"%.*s\"", (int)arg_value.count,
                       arg_value.data);

        Macro_Replacement repl = {.node = node,
                                  .with =
                                      sv_from_parts(stringified_value.items,
                                                    stringified_value.count)};
        nob_da_append(replacements, repl);
      }
    }
    break;
  }
  case sym_identifier: { // identifier

    for (int i = 0; i < arg_names->count; i++) {
      String_View arg_name = arg_names->items[i];

      if (ts_node_range(node, src).len != arg_name.count)
        continue;

      assert(src && "src should be non null");

      if (memcmp(ts_node_range(node, src).start, arg_name.data,
                 arg_name.count) == 0) {

        String_View arg_value = arg_values->items[i];
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
  nob_log(INFO, "------- Expand macro tree with %zu args ---- ",
          arg_values.count);

  debug_tree(macro_def->body_tree, macro_def->body_src, 4);
  nob_log(INFO, "^^^^^^^ Expand macro tree with %zu args ^^^^^^^",
          arg_values.count);

  TSNode root = ts_tree_root_node(macro_def->body_tree);
  Slice root_range = ts_node_range(root, macro_def->body_src);

  Macro_Replacements replacements = {0};
  _expand_macro_node(root, macro_def->body_src, &macro_def->arg_names,
                     &arg_values, &replacements);

  nob_log(INFO, "========= Macro expansion replacements: %zu",
          replacements.count);

  assert(root_range.start == macro_def->body_src);

  char *cursor = (char *)macro_def->body_src;
  if (replacements.count != arg_values.count) {
    nob_log(ERROR, "Macro expansion failed: %zu replacements != %zu args",
            replacements.count, arg_values.count);
  }
  assert(replacements.count == arg_values.count);

  // if (replacements.count > 0) {
  nob_da_foreach(Macro_Replacement, it, &replacements) {
    Slice r = ts_node_range(it->node, macro_def->body_src);
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
  nob_log(INFO, ">> Expanded as %.*s", (int)out_sb->count, out_sb->items);
  // } else {
  // nob_log(NOB_WARNING, "macro does not contain any replacements. sus");
  // }
}

static bool ts_node_is_comptimetype_kw(TSNode node, const char *src) {
  assert(ts_node_symbol(node) == sym_identifier);
  return strlen("_ComptimeType") == ts_node_range(node, src).len &&
         memcmp(ts_node_range(node, src).start, "_ComptimeType",
                ts_node_range(node, src).len) == 0;
}

static bool ts_node_is_comptime_kw(TSNode node, const char *src) {
  assert(ts_node_symbol(node) == sym_identifier);
  return strlen("_Comptime") == ts_node_range(node, src).len &&
         memcmp(ts_node_range(node, src).start, "_Comptime",
                ts_node_range(node, src).len) == 0;
}

static bool _has_comptime_identifier(TSNode node, const char *src) {
  if (ts_node_symbol(node) == sym_identifier) {
    return ts_node_is_comptime_kw(node, src) ||
           ts_node_is_comptimetype_kw(node, src);
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

Slice parse_comptime_call_expr2(TSNode call_expression_root, const char *src) {
  assert(ts_node_symbol(call_expression_root) == sym_call_expression);
  TSNode argument_list = ts_node_child(call_expression_root, 1);

  printf("\n---->\n");
  debug_tree_node(argument_list, src, 4);
  printf("\n<----\n");
  assert(ts_node_symbol(argument_list) == sym_argument_list);
  // the code to execute is within the argument_list:
  String_View _comptime_code = ts_node_to_str_view(argument_list, src);
  assert(_comptime_code.count > 2 && "Empty _Comptime are not allowed");
  nob_log(INFO, "@@@@(%zu)@@@\n %.*s \n@@@ @@@", _comptime_code.count,
          (int)_comptime_code.count, _comptime_code.data);
  assert(_comptime_code.data[0] == '(');
  printf("\n\n\n\n----> %c\n\n\n\n",
         _comptime_code.data[_comptime_code.count - 1]);
  assert(_comptime_code.data[_comptime_code.count - 1] == ')');
  // assert(true);

  char *start = (char *)_comptime_code.data + 1; // skip the initial paren;
  char *end = (char *)_comptime_code.data + _comptime_code.count - 2;

  // trim initial white space
  while (*start == ' ' || *start == '\t' || *start == '{') {
    start++;
  }
  while (*end == ' ' || *end == '\t' || *end == '}' || *end == ';') {
    end--;
  }

  return (Slice){.start = start, .len = (int)(end - start + 1)};

  nob_log(INFO, "\n=== FOUND _Comptime CODE ===\n%.*s\n=============",
          (int)(end - start + 1), start);
}

void parse_comptime_call_expr(WalkContext *const ctx,
                              TSNode call_expression_root, const char *src) {
  assert(ts_node_symbol(call_expression_root) == sym_call_expression);
  TSNode argument_list = ts_node_child(call_expression_root, 1);

  printf("\n---->\n");
  debug_tree_node(argument_list, src, 4);
  printf("\n<----\n");
  assert(ts_node_symbol(argument_list) == sym_argument_list);
  // the code to execute is within the argument_list:
  String_View _comptime_code = ts_node_to_str_view(argument_list, src);
  assert(_comptime_code.count > 2 && "Empty _Comptime are not allowed");
  nob_log(INFO, "@@@@(%zu)@@@\n %.*s \n@@@ @@@", _comptime_code.count,
          (int)_comptime_code.count, _comptime_code.data);
  assert(_comptime_code.data[0] == '(');
  printf("\n\n\n\n----> %c\n\n\n\n",
         _comptime_code.data[_comptime_code.count - 1]);
  assert(_comptime_code.data[_comptime_code.count - 1] == ')');
  // assert(true);

  char *start = (char *)_comptime_code.data + 1; // skip the initial paren;
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

  nob_sb_appendf(&ctx->out_c.definitions,
                 "\n__Comptime_Statement_Fn(%d, %.*s)\n", ctx->comptime_count,
                 (int)(end - start + 1), start);

  nob_sb_appendf(&ctx->out_c.main,
                 "__Comptime_Register_Main_Exec(%d); // "
                 "execute comptime statement #%d\n",
                 ctx->comptime_count, ctx->comptime_count);

  ctx->comptime_count++;
}

// WALKING THE AST
typedef struct {
  TSNode *decleration_root;
  TSNode *preproc_def_root;
  TSNode *call_expression_root;
  TSNode *macro_type_specifier_root;
  TSNode *function_definition_root;
  int child_idx;
} LocalWalkContext;

typedef void (*WalkerFunction)(WalkContext *const ctx, LocalWalkContext local,
                               TSNode node, const char *src, unsigned depth);

static void try_expand_macro(
    TSNode node, WalkContext *const ctx, LocalWalkContext local,
    const char *src, unsigned depth,
    WalkerFunction walker /* will be called if we find a macro */) {

  const char *key = ts_node_range(node, src).start;
  int key_len = ts_node_range(node, src).len;

  MacroDefinition *macro_def = macros_get(&ctx->macros, key, key_len);

  Nob_String_Builder out_sb = {0};
  if (!macro_def)
    return;

  printf(ORANGE("=> expanding macro def :: %.*s\n"),
         ts_node_range(macro_def->identifier, src).len,
         ts_node_range(macro_def->identifier, src).start);

  if (macro_def->arg_names.count > 0) {
    // macro call with arguments, we must be inside a call expression
    assert(local.call_expression_root &&
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
  printf("\n------------[GOING IN THE MACRO TREE]--->\n");
  debug_tree(tree, out_sb.items, depth);
  walker(ctx, local, root, out_sb.items, depth + 4);
  printf("\n^ ^ ^ ^ ^ ^ ^ ^ DONE ^ ^ ^ ^ ^ ^\n");
  return;
}

static void parse_preproc_function_def(WalkContext *const ctx, TSNode node,
                                       const char *src) {
  MacroDefinition *macro = calloc(1, sizeof(MacroDefinition));

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
  TSTree *tree =
      ts_parser_parse_string(cparser, NULL, ts_node_start_len_tuple(body, src));

  if (!has_comptime_identifier(tree, ts_node_range(body, src).start) &&
      !hashmap_get2(&ctx->macros,
                    (char *)ts_node_range(macro->identifier, src).start,
                    ts_node_range(macro->identifier, src).len)) {
    nob_log(INFO,
            "'%.*s' macro has been proven irrelevant because it does not "
            "contain the `_Comptime` keyword",
            ts_node_len_start_tuple(macro->identifier, src));
  } else {
    nob_log(INFO,
            "'%.*s' macro is a *comptime* macro or contains another "
            "*comptime* macro",
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
}

static void walk(WalkContext *const ctx, LocalWalkContext local, TSNode node,
                 const char *src, unsigned depth) {

  for (unsigned i = 0; i < depth; i++)
    putchar(' ');
  printf("- [%s] (%d)", ts_node_type(node), ts_node_symbol(node));

  TSSymbol sym = ts_node_symbol(node);
  switch (sym) {
  case sym_identifier: { // identifier
    try_expand_macro(node, ctx, local, src, depth, walk);
    break;
  }
  case sym_function_definition: { // function_definition

    local.function_definition_root = &node;
    break;
  }
  case sym_init_declarator: // init_declarator
    local.decleration_root = &node;
    break;
  case sym_preproc_def: // preproc_def
    local.preproc_def_root = &node;
    break;
  case sym_macro_type_specifier:
    local.macro_type_specifier_root = &node;
    break;
  case sym_call_expression: /* call exresssion */
    local.call_expression_root = &node;
    break;
  case sym_preproc_function_def:
    parse_preproc_function_def(ctx, node, src);
    return;
  }

  if (sym == sym_identifier) {
    printf(" ~~");
    if (ts_node_is_comptime_kw(node, src) && !local.call_expression_root) {
      if (local.preproc_def_root) {
        if (local.child_idx == 1) {
          fatal("Redefining `_Comptime` macro is not supported");
        }
      }
      fatal("Invalid use of _Comptime");
    }

    if (ts_node_is_comptimetype_kw(node, src) &&
        !local.macro_type_specifier_root) {
      if (local.preproc_def_root) {
        if (local.child_idx == 1) {
          fatal("Redefining `_ComptimeType` macro is not supported");
        }
      }

      // sometimes tree sitter misparses
      fatal("Invalid use of _ComptimeType (found _ComptimeType outside of a "
            "call expression)");
    }

    if (ts_node_is_comptimetype_kw(node, src) &&
        local.macro_type_specifier_root) {

      assert(local.function_definition_root);
      printf(MAGENTA("!INFERING!\n"));
      printf(MAGENTA(":: %.*s\n"),
             ts_node_range(*local.function_definition_root, src).len,
             ts_node_range(*local.function_definition_root, src).start);
      Slice inferred_type = infer(*local.function_definition_root, src);
      printf(MAGENTA("!INFERED! :: %.*s"), inferred_type.len,
             inferred_type.start);
      printf(ORANGE("  [contains '_ComptimeType'] (%d)"), ctx->comptime_count);

      // FIRST PASS REPLACE THE SEMANTIC VALUE WITH A PLACEHOLDER
      nob_sb_appendf(
          &ctx->out_h, "\n#define _PLACEHOLDER_COMPTIME_X%d(x) %.*s\n",
          ctx->comptime_count, inferred_type.len, inferred_type.start);

      // LOOK AHEAD, WHERE ARE WE?
      assert(ts_node_symbol(ts_node_child(*local.macro_type_specifier_root,
                                          0)) == sym_identifier);

      Slice _ComptimeTypeIdentifierRange = ts_node_range(
          ts_node_child(*local.macro_type_specifier_root, 0), src);

      Slice range = ts_node_range(*local.macro_type_specifier_root, src);

      // parsed range
      Slice parsed_range = (Slice){
          .start = _ComptimeTypeIdentifierRange.start +
                   _ComptimeTypeIdentifierRange.len +
                   1, // +1 to skip the 1st parenthesis
          .len = range.len - _ComptimeTypeIdentifierRange.len -
                 2, // -2 to compensate for 1st and last paren
      };

      printf("~> parsed range is %.*s\n", parsed_range.len, parsed_range.start);

      nob_sb_appendf(&ctx->out_c.definitions,
                     "\n__Comptime_Statement_Fn(%d, %.*s)\n",
                     ctx->comptime_count, parsed_range.len, parsed_range.start);

      nob_sb_appendf(&ctx->out_c.main,
                     "__Comptime_Register_Main_Exec(%d); // "
                     "execute comptime statement #%d\n",
                     ctx->comptime_count, ctx->comptime_count);
      ctx->comptime_count++;

      // range.start = _ComptimeTypeIdentifierRange.start;

      // assert(ts_node_symbol(ts_node_child(*local.macro_type_specifier_root,
      //                                     1)) == anon_sym_LPAREN);

      // assert(ts_node_symbol(ts_node_child(*local.macro_type_specifier_root,
      //                                     1)) == anon_sym_RPAREN);
      // TSNode next =
      // ts_node_next_sibling(ts_node_parent(*local.call_expression_root));
      // nob_log(INFO, "Next node after ComptimeType is");
      // debug_tree_node(next, src, depth + 1);
      // printf("\n---\n");
      // parse_comptime_call_expr(ctx, *local.call_expression_root, src);
    }

    // expand
    if (ts_node_is_comptime_kw(node, src) && local.call_expression_root) {
      printf(BLUE("  [contains '_Comptime'] (%d)"), ctx->comptime_count);

      // FIRST PASS REPLACE THE SEMANTIC VALUE WITH A PLACEHOLDER
      if (local.decleration_root) {
        nob_sb_appendf(&ctx->out_h, "#define _PLACEHOLDER_COMPTIME_X%d(x) 0\n",
                       ctx->comptime_count);
      } else if (local.function_definition_root) {
        nob_sb_appendf(&ctx->out_h,
                       "#define _PLACEHOLDER_COMPTIME_X%d(x) /* comptime block "
                       "statement %d */\n",
                       ctx->comptime_count, ctx->comptime_count);
      } else {
        nob_sb_appendf(
            &ctx->out_h,
            "#define _PLACEHOLDER_COMPTIME_X%d(x)/* comptime block %d */\n",
            ctx->comptime_count, ctx->comptime_count);
      }

      // surface until the expression
      parse_comptime_call_expr(ctx, *local.call_expression_root, src);
    }
  }
  putchar('\n');

  uint32_t n = ts_node_child_count(node);
  for (uint32_t i = 0; i < n; i++) {
    local.child_idx = i;
    walk(ctx, local, ts_node_child(node, i), src, depth + 2);
  }
}

static void strip_comptime_dependencies(WalkContext *const ctx,
                                        LocalWalkContext local, TSNode node,
                                        const char *src, unsigned depth) {

  TSSymbol sym = ts_node_symbol(node);
  switch (sym) {
  case sym_identifier: { // identifier
    try_expand_macro(node, ctx, local, src, depth, strip_comptime_dependencies);
    break;
  }
  case sym_function_definition: { // function_definition
    local.function_definition_root = &node;
    break;
  }
  case sym_init_declarator: // init_declarator
    local.decleration_root = &node;
    break;
  case sym_preproc_def: // preproc_def
    local.preproc_def_root = &node;
    break;
  case sym_macro_type_specifier:
    local.macro_type_specifier_root = &node;
    break;
  case sym_call_expression: /* call exresssion */
    local.call_expression_root = &node;
    break;
  case sym_preproc_function_def: {
    parse_preproc_function_def(ctx, node, src);
    return;
  }
  }

  if (sym == sym_identifier && ts_node_is_comptime_kw(node, src)) {
    if (!local.call_expression_root && local.preproc_def_root &&
        local.child_idx == 1)
      fatal("Redefining `_Comptime` macro is not supported");
    if (!local.call_expression_root)
      fatal("Invalid use of _Comptime");

    Slice r = parse_comptime_call_expr2(*local.call_expression_root, src);
    nob_log(INFO, BOLD("Parsed _Comptime call : ") "%.*s", r.len, r.start);

    // if we are within a function body, then this function cannot be used
    // during the comptime calculation
    if (local.function_definition_root) {
      nob_log(INFO, ORANGE("Stripping comptime block within a function"));
      da_append(&ctx->comptime_slices,
                ts_node_range(*local.function_definition_root, src));
    } else if (local.decleration_root) {
      nob_log(INFO, ORANGE("Stripping top level decleration with comptime"));
    } else {
      nob_log(INFO, ORANGE("Stripping top level comptime block"));
      da_append(&ctx->comptime_slices,
                ts_node_range(*local.call_expression_root, src));
    }
  }

  uint32_t n = ts_node_child_count(node);

  if (n == 0) {
    nob_log(INFO, GREEN("%.*s"), ts_node_range(node, src).len,
            ts_node_range(node, src).start);
    // nob_sb_append_buf(&ctx->out_h, ts_node_range(node, src).start,
    //                   ts_node_range(node, src).len);
  }
  for (uint32_t i = 0; i < n; i++) {
    local.child_idx = i;
    strip_comptime_dependencies(ctx, local, ts_node_child(node, i), src,
                                depth + 1);
  }
}

int run_file(Context *ctx) {

  cparser = ts_parser_new();
  ts_parser_set_language(cparser, tree_sitter_c());

  TSTree *tree = ts_parser_parse_string(cparser, NULL, ctx->raw_source->items,
                                        ctx->raw_source->count);

  debug_tree(tree, ctx->raw_source->items, 0);
  TSNode root = ts_tree_root_node(tree);
  WalkContext walk_ctx = (WalkContext){0};
  strip_comptime_dependencies(&walk_ctx, (LocalWalkContext){0}, root,
                              ctx->raw_source->items, 0);

  nob_da_foreach(Slice, it, &walk_ctx.comptime_slices) {
    nob_log(INFO, "Stripping comptime slice %.*s", (int)it->len, it->start);
  }
  exit(1);

  sb_appendf(&walk_ctx.out_h,
             "/*// @generated - ccomptime™ v0.0.1 - %lu \\*/\n", time(NULL));
  sb_appendf(
      &walk_ctx.out_h,
      "#define _CONCAT_(x, y) x##y\n"
      "#define CONCAT(x, y) _CONCAT_(x, y)\n"
      "#define _Comptime(x) _COMPTIME_X(__COUNTER__, x)\n"
      "#define _ComptimeType(x) _COMPTIME_X(__COUNTER__, x)\n"
      "#define _COMPTIME_X(n, x) CONCAT(_PLACEHOLDER_COMPTIME_X, n)(x)\n");

  // walk(&walk_ctx, (LocalWalkContext){0}, root, ctx->raw_source->items, 0);
  strip_comptime_dependencies(&walk_ctx, (LocalWalkContext){0}, root,
                              ctx->raw_source->items, 0);

  sb_appendf(&walk_ctx.out_h, "/* ---- */// the end. ///* ---- */\n");

  printf("comptime found : %d", walk_ctx.comptime_count);
  // printf("\n\nGenerated header:\n%s\n", walk_ctx.out_h.items);

  printf("\n\nGenerated header:\n%s\n", walk_ctx.out_h.items);

  write_entire_file(ctx->runner_main_path, walk_ctx.out_c.main.items,
                    walk_ctx.out_c.main.count);

  write_entire_file(ctx->runner_defs_path, walk_ctx.out_c.definitions.items,
                    walk_ctx.out_c.definitions.count);

  write_entire_file(ctx->gen_header_path, walk_ctx.out_h.items,
                    walk_ctx.out_h.count);

  ts_tree_delete(tree);
  ts_parser_delete(cparser);
  sb_free(walk_ctx.out_h);
  return 0;
}

int main(int argc, char **argv) {
  // if (argc < 2) {
  //   fprintf(stderr, "usage: %s <file.c>\n", argv[0]);
  //   return 1;
  // }

  CliArgs parsed_argv = {0};
  if (cli(argc, argv, &parsed_argv) != 0) {
    return 1;
  }

  nob_log(INFO, "Received %d arguments", argc);
  nob_log(INFO, "Using compiler %s", Parsed_Argv_compiler_name(&parsed_argv));

  Nob_Cmd final = {0};
  nob_cmd_append(&final, Parsed_Argv_compiler_name(&parsed_argv));
  cmd_append_arg_indeces(&parsed_argv, &parsed_argv.not_input_files, &final);

  struct {
    const char **items;
    size_t count, capacity;
  } files_to_remove = {0};

  nob_da_foreach(int, index, &parsed_argv.input_files) {
    // -- SETUP CONTEXT --
    const char *input_filename = argv[*index];
    String_Builder raw_source = {0}, preprocessed_source = {0};
    Context ctx = {0};
    ctx.input_path = input_filename;
    ctx.parsed_argv = &parsed_argv;
    ctx.raw_source = &raw_source;
    ctx.preprocessed_source = &preprocessed_source;

    Context_fill_paths(&ctx, input_filename);

    // -- PREPROCESS INPUT FILE (expand macros) --
    nob_log(INFO, "Processing %s source as SourceCode", input_filename);
    // parse_source_file(input_filename, ctx.raw_source);
    nob_read_entire_file(input_filename, ctx.raw_source);

    run_file(&ctx);

    assert(ctx.runner_templ_path);
    Nob_Cmd cmd = {0};

    nob_cmd_append(&cmd, Parsed_Argv_compiler_name(ctx.parsed_argv));
    cmd_append_arg_indeces(ctx.parsed_argv, &ctx.parsed_argv->not_input_files,
                           &cmd);
    nob_cmd_append(&cmd, "-O0");

    cmd_append_inputs_except(ctx.parsed_argv, ctx.input_path, &cmd);

    // nob_cmd_append(&cmd, ctx.runner_cpath);
    nob_cmd_append(&cmd, "runner.templ.c");

    if (!(ctx.parsed_argv->cct_flags & CliComptimeFlag_Debug)) {
      nob_cmd_append(&cmd, "-w");
    } else {
      nob_cmd_append(&cmd, "-g", "-fsanitize=address,undefined",
                     "-fno-omit-frame-pointer");
    }

    nob_cmd_append(
        &cmd, "-o", ctx.runner_exepath,
        temp_sprintf("-D_INPUT_PROGRAM_PATH=\"%s\"", ctx.input_path),
        temp_sprintf("-D_INPUT_COMPTIME_DEFS_PATH=\"%s\"",
                     ctx.runner_defs_path),
        temp_sprintf("-D_INPUT_COMPTIME_MAIN_PATH=\"%s\"",
                     ctx.runner_main_path),
        temp_sprintf("-D_OUTPUT_HEADERS_PATH=\"%s\"", ctx.gen_header_path));

    if (!nob_cmd_run(&cmd)) {
      nob_log(ERROR, "failed to compile runner %s", ctx.runner_templ_path);
      exit(1);
    }

    nob_log(INFO, "Running runner %s", ctx.runner_exepath);
    nob_cmd_append(&cmd, nob_temp_sprintf("./%s", ctx.runner_exepath));
    if (!nob_cmd_run(&cmd)) {
      nob_log(ERROR, "failed to run runner %s", ctx.runner_templ_path);
      exit(1);
    }

    // nob_log(INFO, "Preprocessing source");
    // run_preprocess_cmd_for_source(&parsed_argv, input_filename,
    //                               ctx.preprocessed_path);

    // nob_log(INFO, "Rewrote argv %s -> %s", input_filename,
    //         ctx.preprocessed_path);

    // nob_read_entire_file(ctx.preprocessed_path, ctx.preprocessed_source);

    // -- GENERATE AND RUN THE RUNNER --
    // log_info("Generating runner c");
    // emit_runner_tu(&ctx, &comptime_blocks);
    // compile_and_run_runner(&ctx);

    // String_Builder repls2 = {0};
    // substitute_block_values(&ctx, &comptime_blocks, &repls2);

    // log_info("WRTING FINAL OUTPUT FILE %s (%zu)bytes ", ctx.final_out_path,
    //          repls2.count);
    // write_entire_file(ctx.final_out_path, repls2.items, repls2.count);

    // // -- APPEND FINAL OUTPUT FILE TO ARGV --
    // nob_cmd_append(&final, ctx.final_out_path);

    // log_info("Appended final output file %s to final argv",
    // ctx.final_out_path);

    // log_info("Scheduling intermediate files for deletion");
    da_append(&files_to_remove, ctx.preprocessed_path);
    da_append(&files_to_remove, ctx.runner_templ_path);
    da_append(&files_to_remove, ctx.runner_exepath);
    da_append(&files_to_remove, ctx.vals_path);
    da_append(&files_to_remove, ctx.final_out_path);
  }

  // if (!cmd_run(&final)) {
  //   nob_log(ERROR, "failed to compile final output");
  //   return 1;
  // } else {
  //   nob_log(INFO, "Successfuly compiled final output");
  // }

  // -- CLEANUP --
  nob_log(INFO, "Cleaning up %zu intermediate files", files_to_remove.count);
  nob_da_foreach(const char *, f, &files_to_remove) {
    if (!(parsed_argv.cct_flags & CliComptimeFlag_KeepInter)) {
      nob_log(NOB_WARNING, "Deleting intermediate file %s", *f);
      delete_file(*f);
    } else {
      nob_log(INFO, "Keeping intermediate file %s", *f);
    }
  }

  // const char *filename = argv[1];
  // return run_file(filename);
}
