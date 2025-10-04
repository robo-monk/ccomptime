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

typedef HashMap MacroDefinitionHashMap;

typedef struct {
  String_Builder definitions;
  String_Builder main;
} C_FileBuilder;

typedef struct {
  const char *start;
  int len;
} Slice;

bool slice_begins_with(Slice s, const char *prefix) {
  int prefix_len = (int)strlen(prefix);
  if (prefix_len > s.len)
    return false;
  return strncmp(s.start, prefix, prefix_len) == 0;
}
Slice slice_strip_prefix(Slice s, const char *prefix) {
  int prefix_len = (int)strlen(prefix);
  assert(prefix_len <= s.len);
  assert(strncmp(s.start, prefix, prefix_len) == 0);

  Slice out;
  out.start = s.start + prefix_len;
  out.len = s.len - prefix_len;
  return out;
}

typedef struct {
  String_Builder out_h;
  int comptime_count;
  HashMap macros;
  HashMap comptime_dependencies;

  C_FileBuilder out_c;

  struct {
    Slice *items;
    size_t count, capacity;
  } to_be_removed;

  struct {
    Slice *items;
    size_t count, capacity;
  } comptimetype_stmts;

  struct {
    Slice *items;
    size_t count, capacity;
  } comptime_stmts;
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

Slice parse_comptimetype_macro_type_specifier(TSNode macro_type_specifier_root,
                                              const char *src) {
  // LOOK AHEAD, WHERE ARE WE?
  assert(ts_node_symbol(ts_node_child(macro_type_specifier_root, 0)) ==
         sym_identifier);

  Slice _ComptimeTypeIdentifierRange =
      ts_node_range(ts_node_child(macro_type_specifier_root, 0), src);

  Slice range = ts_node_range(macro_type_specifier_root, src);

  // parsed range
  return (Slice){
      .start = _ComptimeTypeIdentifierRange.start +
               _ComptimeTypeIdentifierRange.len +
               1, // +1 to skip the 1st parenthesis
      .len = range.len - _ComptimeTypeIdentifierRange.len -
             2, // -2 to compensate for 1st and last paren
  };
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

static int try_expand_macro_call_expression(TSNode node,
                                            MacroDefinitionHashMap *macros,
                                            const char *src,
                                            String_Builder *expanded) {

  assert(ts_node_symbol(node) == sym_call_expression);

  TSNode identifier = ts_node_child(node, 0);
  TSSymbol sym = ts_node_symbol(identifier);
  if (ts_node_symbol(identifier) !=
      sym_identifier) // could be a field_expression
    return 0;

  assert(ts_node_symbol(identifier) == sym_identifier);

  Slice id_range = ts_node_range(identifier, src);

  MacroDefinition *macro_def = macros_get(macros, id_range.start, id_range.len);
  if (!macro_def)
    return 0;

  TSNode argument_list = ts_node_child(node, 1);
  assert(ts_node_symbol(argument_list) ==
         sym_argument_list /* argument_list */);

  printf(ORANGE("=> expanding macro def :: %.*s\n"),
         ts_node_range(macro_def->identifier, src).len,
         ts_node_range(macro_def->identifier, src).start);

  if (macro_def->arg_names.count > 0) {
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
    expand_macro_tree(macro_def, arg_values, expanded);
  } else {
    printf("  [MACRO CALL: %.*s]", id_range.len, id_range.start);
    expand_macro_tree(macro_def, (Strings){0}, expanded);
  }

  nob_log(INFO, "Macro expansion result: %.*s", (int)expanded->count,
          expanded->items);

  return 1;
}
static int try_expand_type_identifier_expression(TSNode node,
                                                 MacroDefinitionHashMap *macros,
                                                 const char *src,
                                                 String_Builder *expanded) {

  assert(ts_node_symbol(node) == alias_sym_type_identifier);
  Slice id_range = ts_node_range(node, src);
  MacroDefinition *macro_def = macros_get(macros, id_range.start, id_range.len);
  if (!macro_def)
    return 0;

  expand_macro_tree(macro_def, (Strings){0}, expanded);

  return 1;
}

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

    if (!local.call_expression_root) {
      debug_tree(macro_def->body_tree, macro_def->body_src, 0);
    }
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

static int put_macro_def_if_comptime_relevant(
    MacroDefinitionHashMap *const macros, TSNode macro_identifier,
    TSNode macro_body, MacroDefinition *macro_def, const char *src) {
  // we only care if it includes a `comptime`, otherwise skip expansion
  TSTree *tree = ts_parser_parse_string(
      cparser, NULL, ts_node_start_len_tuple(macro_body, src));

  if (!has_comptime_identifier(tree, ts_node_range(macro_body, src).start) &&
      !hashmap_get2(macros, (char *)ts_node_range(macro_identifier, src).start,
                    ts_node_range(macro_identifier, src).len)) {
    nob_log(INFO,
            RED("'%.*s' macro has been proven irrelevant because it does not "
                "contain the `_Comptime` keyword"),
            ts_node_len_start_tuple(macro_identifier, src));
    return 0;
  } else {
    nob_log(INFO,
            GREEN("'%.*s' macro is a *comptime* macro or contains another "
                  "*comptime* macro"),
            ts_node_len_start_tuple(macro_identifier, src));
  }
  if (!macro_def)
    macro_def = calloc(1, sizeof(MacroDefinition));

  macro_def->identifier = macro_identifier;
  macro_def->body_tree = tree;
  macro_def->body_src = ts_node_range(macro_body, src).start;

  macros_put(macros, ts_node_range(macro_def->identifier, src).start,
             ts_node_range(macro_def->identifier, src).len, macro_def);
  return 1;
}
static int parse_preproc_def(MacroDefinitionHashMap *const macros, TSNode node,
                             const char *src) {
  assert(ts_node_symbol(node) == sym_preproc_def);

  uint32_t n = ts_node_child_count(node);
  assert(ts_node_symbol(ts_node_child(node, 0)) ==
         aux_sym_preproc_def_token1 /* #define */);

  TSNode macro_identifier = ts_node_child(node, 1); // should be identifier
  assert(ts_node_symbol(macro_identifier) == sym_identifier);

  TSNode body = ts_node_child(node, 2); // should be the body
  if (ts_node_is_null(body))
    return 0;

  assert(ts_node_symbol(body) == sym_preproc_arg);
  return put_macro_def_if_comptime_relevant(macros, macro_identifier, body,
                                            NULL, src);
}
static int parse_preproc_function_def(MacroDefinitionHashMap *const macros,
                                      TSNode node, const char *src) {
  assert(ts_node_symbol(node) == sym_preproc_function_def);

  uint32_t n = ts_node_child_count(node);
  assert(ts_node_symbol(ts_node_child(node, 0)) ==
         aux_sym_preproc_def_token1 /* #define */);

  TSNode macro_identifier = ts_node_child(node, 1); // should be identifier
  assert(ts_node_symbol(macro_identifier) == sym_identifier);

  TSNode preproc_params = ts_node_child(node, 2); // should be preproc_params
  if (ts_node_is_null(preproc_params))
    return 0;

  assert(!ts_node_is_null(preproc_params));
  assert(ts_node_symbol(preproc_params) == sym_preproc_params);

  MacroDefinition *macro = calloc(1, sizeof(MacroDefinition));
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

  return put_macro_def_if_comptime_relevant(macros, macro_identifier, body,
                                            macro, src);
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
  case sym_declaration:
    assert(!local.decleration_root);
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
            "`macro_type_specifier`");
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

// Sometimes due to C syntactic rules, _ComptimeType stuff might break the
// treeparser For example _ComptimeType(test930(_ComptimeCtx, 0)) test5() {...}
// does not get parsed into a function decleration with a macro type specifier
// instead it gets parsed as an expression, expression and compound statement,
// completely bypassing the the function decleration
//
// To solve this, we do a first pass where we parse All _ComptimeType
// statements into identifiers and reparse the tree
typedef struct {
  TSNode node;
  Nob_String_View with;
} NodeReplacement;

typedef struct {
  NodeReplacement *items;
  size_t count, capacity;
} NodeReplacements;

typedef struct {
  TSNode *items;
  size_t count, capacity;
} OutReplacements;
static void correct_tree(OutReplacements *out_replacements, TSNode node,
                         const char *src) {
  if (ts_node_symbol(node) == sym_call_expression) {
    if (ts_node_symbol(ts_node_child(node, 0)) == sym_identifier &&
        ts_node_is_comptimetype_kw(ts_node_child(node, 0), src)) {
      nob_da_append(out_replacements, node);
    }
  } else if (ts_node_symbol(node) == sym_macro_type_specifier) {
    if (ts_node_symbol(ts_node_child(node, 0)) == sym_identifier &&
        ts_node_is_comptimetype_kw(ts_node_child(node, 0), src)) {
      nob_da_append(out_replacements, node);
    }
  }

  uint32_t n = ts_node_child_count(node);
  for (uint32_t i = 0; i < n; i++) {
    correct_tree(out_replacements, ts_node_child(node, i), src);
  }
}

static void clean_include_directives(OutReplacements *out_replacements,
                                     TSNode node, const char *src) {
  if (ts_node_symbol(node) == sym_preproc_include) {
    Slice r = ts_node_range(node, src);
    nob_log(INFO, BOLD("#include ") RED("%.*s"), r.len, r.start);
    nob_da_append(out_replacements, node);
  }

  uint32_t n = ts_node_child_count(node);
  for (uint32_t i = 0; i < n; i++) {
    clean_include_directives(out_replacements, ts_node_child(node, i), src);
  }
}

TSTree *apply_replacements_to_tree(
    OutReplacements *replacements, TSTree *tree, const char *tree_src,
    int tree_src_len, String_Builder *out_source,
    void (*mapper_fn)(String_Builder *out_source, TSNode node,
                      const char *tree_src, void *user_args),
    void *user_args) {

  // correct_tree(&corrections, ts_tree_root_node(tree), tree_src);
  char *cursor = (char *)tree_src;

  nob_da_foreach(TSNode, node, replacements) {
    Slice r = ts_node_range(*node, tree_src);

    if (cursor >= r.start)
      continue;

    assert(cursor < r.start && "cursor is ahead of slice");

    ssize_t offset = r.start - cursor;
    assert(offset >= 0);
    if (mapper_fn) {
      mapper_fn(out_source, *node, tree_src, user_args);
    }
    cursor = (char *)r.start + r.len; // jump over the comptime slice
  }
  ssize_t _offset = tree_src + tree_src_len - cursor;
  assert(_offset >= 0);
  nob_sb_append_buf(out_source, cursor, _offset);

  ts_tree_delete(tree);

  const char *tree_source = out_source->items;
  TSTree *clean_tree = ts_parser_parse_string(cparser, NULL, out_source->items,
                                              out_source->count);

  return clean_tree;
}

TSTree *apply_node_replacements_to_tree(NodeReplacements *replacements,
                                        TSTree *tree, const char *tree_src,
                                        int tree_src_len,
                                        String_Builder *out_source) {

  // correct_tree(&corrections, ts_tree_root_node(tree), tree_src);
  char *cursor = (char *)tree_src;

  if (replacements->count == 0) {
    nob_log(WARNING, YELLOW("No replacements performed for tree"));
    return tree;
  }
  // nob_da_foreach(NodeReplacement, r, replacements) {
  nob_da_foreach(NodeReplacement, repl, replacements) {
    TSNode node = repl->node;
    Nob_String_View with = repl->with;

    Slice r = ts_node_range(node, tree_src);
    // if (cursor > r.start)
    //   continue;

    assert(cursor < r.start && "cursor is ahead of slice");

    ssize_t offset = r.start - cursor;

    if (offset) {
      nob_sb_append_buf(out_source, cursor, offset);
    }
    assert(offset >= 0);
    if (with.count) {
      nob_sb_append_buf(out_source, with.data, with.count);
    }

    // if (mapper_fn) {
    //   mapper_fn(out_source, node, tree_src, user_args);
    // }
    cursor = (char *)r.start + r.len; // jump over the comptime slice
  }
  ssize_t _offset = tree_src + tree_src_len - cursor;
  assert(_offset >= 0);
  nob_sb_append_buf(out_source, cursor, _offset);

  ts_tree_delete(tree);

  const char *tree_source = out_source->items;
  TSTree *clean_tree = ts_parser_parse_string(cparser, NULL, out_source->items,
                                              out_source->count);

  return clean_tree;
}
// input tree is going to get deleted
// usage: TSTree t = ...
// t = clean_include_directives_tree(t);
// void _aux_clean(String_Builder *out_source, TSNode node, const char *src, )
// {}

TSTree *clean_include_directives_tree(TSTree *tree, const char *tree_src,
                                      int tree_src_len,
                                      String_Builder *out_source) {
  OutReplacements corrections = {0};
  clean_include_directives(&corrections, ts_tree_root_node(tree), tree_src);
  return apply_replacements_to_tree(&corrections, tree, tree_src, tree_src_len,
                                    out_source, NULL, NULL);
}

TSTree *correct_comptimetype_tree(TSTree *tree, const char *tree_src,
                                  int tree_src_len,
                                  String_Builder *out_source) {
  OutReplacements corrections = {0};
  correct_tree(&corrections, ts_tree_root_node(tree), tree_src);
  return apply_replacements_to_tree(&corrections, tree, tree_src, tree_src_len,
                                    out_source, NULL, NULL);
}

static void _expand_macros_tree_node(
    TSNode node, MacroDefinitionHashMap *macros, const char *src,
    void (*on_macro_expansion)(TSNode node, String_Builder *expanded,
                               void *ctx),
    void *on_macro_expansion_ctx) {

  TSSymbol sym = ts_node_symbol(node);

  // HERE WE PARSE MACRO DEFINITIONS
  if (sym == sym_preproc_function_def) {
    int success = parse_preproc_function_def(macros, node, src);
    nob_log(INFO, "Parsed preproc function def: %d", success);
    return;
  }

  if (sym == sym_preproc_def) {
    int success = parse_preproc_def(macros, node, src);
    nob_log(INFO, "Parsed preproc def: %d", success);
    return;
  }

  // HERE WE PUT EVERY SYMBOL THAT MIGHT BE HIDING
  // A MACRO BEHIND IT!!!
  String_Builder expanded = {0};
  int result = 0;
  if (sym == sym_call_expression) {
    result = try_expand_macro_call_expression(node, macros, src, &expanded);

  } else if (sym == alias_sym_type_identifier) {
    result =
        try_expand_type_identifier_expression(node, macros, src, &expanded);
  }

  if (result) {
    on_macro_expansion(node, &expanded, on_macro_expansion_ctx);
    nob_log(INFO, MAGENTA("%.*s -> %.*s"), (int)ts_node_range(node, src).len,
            ts_node_range(node, src).start, (int)expanded.count,
            expanded.items);
    // nob_sb_free(expanded);
  }

  uint32_t n = ts_node_child_count(node);
  for (uint32_t i = 0; i < n; i++) {
    _expand_macros_tree_node(ts_node_child(node, i), macros, src,
                             on_macro_expansion, on_macro_expansion_ctx);
  }
}

typedef struct {
  NodeReplacements replacements;
} _on_macro_expansion_cb_ctx;

static void _on_macro_expansion_cb(TSNode node, String_Builder *expanded,
                                   void *_ctx) {
  _on_macro_expansion_cb_ctx *ctx = _ctx;

  nob_da_append(&ctx->replacements,
                ((NodeReplacement){.node = node,
                                   .with = nob_sv_from_parts(
                                       expanded->items, expanded->count)}));

  nob_log(INFO, "Macro was expanded to %.*s", (int)expanded->count,
          expanded->items);
}

TSTree *expand_macros_tree(TSTree *tree, MacroDefinitionHashMap *macros,
                           const char *tree_src, int tree_src_len,
                           String_Builder *out_source) {

  _on_macro_expansion_cb_ctx cb_ctx = {.replacements = {0}};
  _expand_macros_tree_node(ts_tree_root_node(tree), macros, tree_src,
                           _on_macro_expansion_cb, &cb_ctx);
  TSTree *mapped_tree = apply_node_replacements_to_tree(
      &cb_ctx.replacements, tree, tree_src, tree_src_len, out_source);
  return mapped_tree;
}

static void register_comptime_dependencies(WalkContext *const ctx,
                                           LocalWalkContext local, TSNode node,
                                           const char *src, unsigned depth) {
  TSSymbol sym = ts_node_symbol(node);
  switch (sym) {
  // case sym_identifier: { // identifier
  //   try_expand_macro(node, ctx, local, src, depth,
  //                    register_comptime_dependencies);
  //   break;
  // }
  case sym_function_definition: { // function_definition
    local.function_definition_root = &node;
    break;
  }
  case sym_declaration:
    local.decleration_root = &node;
    break;
  case sym_preproc_def: // preproc_def
    local.preproc_def_root = &node;
    break;
  case sym_macro_type_specifier:
    local.macro_type_specifier_root = &node;
    break;
  case sym_call_expression: /* call exresssion */
    nob_log(INFO, MAGENTA("=> WITHIN CALL EXPRRR"));
    local.call_expression_root = &node;
    break;
    // case sym_preproc_function_def: {
    //   parse_preproc_function_def(ctx, node, src);
    //   return;
    // }
  }

  Slice r = {0};
  if (sym == sym_identifier && ts_node_is_comptime_kw(node, src)) {
    if (!local.call_expression_root && local.preproc_def_root &&
        local.child_idx == 1)
      fatal("Redefining `_Comptime` macro is not supported");
    if (!local.call_expression_root)
      fatal("Invalid use of _Comptime");

    r = parse_comptime_call_expr2(*local.call_expression_root, src);
    nob_log(INFO, BOLD("Parsed _Comptime call : ") "%.*s", r.len, r.start);
  } else if (sym == sym_identifier && ts_node_is_comptimetype_kw(node, src)) {
    debug_tree_node(node, src, depth);
    // TODO: remove this branch
    assert(0 && "unreachable");
    if (!local.macro_type_specifier_root && local.preproc_def_root &&
        local.child_idx == 1)
      fatal("Redefining `_ComptimeType` macro is not supported");
    if (!local.macro_type_specifier_root)
      fatal("Invalid use of _ComptimeType");

    r = parse_comptimetype_macro_type_specifier(
        *local.macro_type_specifier_root, src);

    nob_log(INFO, BOLD("Parsed _ComptimeType call : ") "%.*s", r.len, r.start);
  } else if (sym == alias_sym_type_identifier) {
    Slice type_id = ts_node_range(node, src);
    if (slice_begins_with(type_id, "_COMPTIMETYPE_")) {
      Slice out = slice_strip_prefix(type_id, "_COMPTIMETYPE_");
      int index = atoi(out.start);
      nob_log(INFO, "Found comptimetype placeholder %d", index);
      assert(index < ctx->comptimetype_stmts.count);
      r = ctx->comptimetype_stmts.items[index];
    }
  }

  if (r.start) {

    // if we are within a function body, then this function cannot be used
    // during the comptime calculation
    if (local.function_definition_root) {
      assert(ts_node_symbol(ts_node_child(*local.function_definition_root,
                                          1)) == sym_function_declarator);
      assert(ts_node_symbol(ts_node_child(
                 ts_node_child(*local.function_definition_root, 1), 0)) ==
             sym_identifier);
      TSNode func_identifier =
          ts_node_child(ts_node_child(*local.function_definition_root, 1), 0);
      Slice func_name = ts_node_range(func_identifier, src);

      nob_log(INFO, ORANGE("function %.*s is comptime dependent"),
              ts_node_range(func_identifier, src).len,
              ts_node_range(func_identifier, src).start);

      hashmap_put2(&ctx->comptime_dependencies, (char *)func_name.start,
                   func_name.len, (void *)1);

      // Comptime_Statement stmt = {
      //     .root_sym = ts_node_symbol(*local.decleration_root),
      //     // .root_slice = ts_node_range(*local.decleration_root, src),
      //     .root_slice = tobe_replaced,
      //     .root_replacement = "; /* decleration removed */",
      //     .snippet = r};
      // da_append(&ctx->comptime_stmts, stmt);
      // ts_node_range(*local.function_definition_root, src));
    } else if (local.decleration_root) {
      nob_log(INFO, ORANGE("Stripping top level decleration with comptime"));
      // remove the = and after
      assert(ts_node_symbol(ts_node_child(*local.decleration_root, 1)) ==
             sym_init_declarator);
      assert(ts_node_symbol(
                 ts_node_child(ts_node_child(*local.decleration_root, 1), 0)) ==
             sym_identifier);

      TSNode var_identifier =
          ts_node_child(ts_node_child(*local.decleration_root, 1), 0);
      Slice var_name = ts_node_range(var_identifier, src);

      nob_log(INFO, ORANGE("decleration %.*s is comptime dependent"),
              ts_node_range(var_identifier, src).len,
              ts_node_range(var_identifier, src).start);

      hashmap_put2(&ctx->comptime_dependencies, (char *)var_name.start,
                   var_name.len, (void *)1);

      Slice tobe_replaced = {.start = src + ts_node_end_byte(var_identifier),
                             .len = ts_node_end_byte(*local.decleration_root) -
                                    ts_node_end_byte(var_identifier)};

      // Comptime_Statement stmt = {
      //     .root_sym = ts_node_symbol(*local.decleration_root),
      //     // .root_slice = ts_node_range(*local.decleration_root, src),
      //     .root_slice = tobe_replaced,
      //     .root_replacement = "; /* decleration removed */",
      //     .snippet = r};
      // da_append(&ctx->comptime_stmts, stmt);
    } else {
      nob_log(INFO, ORANGE("Stripping top level comptime block"));
      // Comptime_Statement stmt = {
      //     .root_sym = ts_node_symbol(*local.call_expression_root),
      //     .root_slice = ts_node_range(*local.call_expression_root, src),
      //     .root_replacement = "/* top level statement removed */",
      //     .snippet = r};
      // da_append(&ctx->comptime_stmts, stmt);
    }

    da_append(&ctx->comptime_stmts, r);
  }

  uint32_t n = ts_node_child_count(node);
  for (uint32_t i = 0; i < n; i++) {
    local.child_idx = i;
    register_comptime_dependencies(ctx, local, ts_node_child(node, i), src,
                                   depth + 1);
  }
}

static void strip_comptime_dependencies(WalkContext *const ctx,
                                        LocalWalkContext local, TSNode node,
                                        const char *src, unsigned depth) {
  TSSymbol sym = ts_node_symbol(node);
  switch (sym) {
  // case sym_identifier: { // identifier
  //   try_expand_macro(node, ctx, local, src, depth,
  //   strip_comptime_dependencies); break;
  // }
  case sym_function_definition: { // function_definition
    local.function_definition_root = &node;
    break;
  }
  case sym_declaration:
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
    // case sym_preproc_function_def: {
    //   parse_preproc_function_def(ctx, node, src);
    //   return;
    // }
  }

  Slice node_slice = ts_node_range(node, src);
  if (sym == sym_identifier &&
      hashmap_get2(&ctx->comptime_dependencies, (char *)node_slice.start,
                   node_slice.len)) {
    nob_log(INFO, MAGENTA("Within a comptime dependency :: !"));

    // if we are within a function body, then this function cannot be used
    // during the comptime calculation
    //
    // thus we should strip it
    if (local.function_definition_root) {

      assert(ts_node_symbol(ts_node_child(*local.function_definition_root,
                                          1)) == sym_function_declarator);
      assert(ts_node_symbol(ts_node_child(
                 ts_node_child(*local.function_definition_root, 1), 0)) ==
             sym_identifier);
      TSNode func_identifier =
          ts_node_child(ts_node_child(*local.function_definition_root, 1), 0);

      Slice r = ts_node_range(*local.function_definition_root, src);
      fwrite(r.start, 1, r.len, stderr);
      fputc('\n', stderr);
      nob_log(INFO, ORANGE("Stripping comptime dependent function (%d) '%.*s'"),
              ts_node_range(*local.function_definition_root, src).len,
              ts_node_range(*local.function_definition_root, src).len,
              ts_node_range(*local.function_definition_root, src).start);
      // ts_node_range(func_identifier, src).len,
      // ts_node_range(func_identifier, src).start

      nob_da_append(&ctx->to_be_removed,
                    ts_node_range(*local.function_definition_root, src));

    } else if (local.decleration_root) {
      nob_log(INFO, ORANGE("Stripping top level decleration with comptime"));

      nob_da_append(&ctx->to_be_removed,
                    ts_node_range(*local.decleration_root, src));
    } else {
      nob_log(INFO, ORANGE("Stripping top level comptime block"));

      nob_da_append(&ctx->to_be_removed,
                    ts_node_range(*local.call_expression_root, src));
    }
  };

  Slice r = {0};

  uint32_t n = ts_node_child_count(node);
  for (uint32_t i = 0; i < n; i++) {
    local.child_idx = i;
    strip_comptime_dependencies(ctx, local, ts_node_child(node, i), src,
                                depth + 1);
  }
}

void build_compile_base_command(Nob_Cmd *out, CliArgs *parsed_argv) {
  nob_cmd_append(out, Parsed_Argv_compiler_name(parsed_argv));
  cmd_append_arg_indeces(parsed_argv, &parsed_argv->flags, out);

  if (!(parsed_argv->cct_flags & CliComptimeFlag_Debug)) {
    nob_cmd_append(out, "-w");
  } else {
    nob_cmd_append(out, "-g", "-fsanitize=address,undefined",
                   "-fno-omit-frame-pointer");
  }
}

int run_file(const char *filename, Context *ctx) {
  size_t _mark = nob_temp_save();

  cparser = ts_parser_new();
  ts_parser_set_language(cparser, tree_sitter_c());

  TSTree *raw_tree = ts_parser_parse_string(
      cparser, NULL, ctx->raw_source->items, ctx->raw_source->count);

  debug_tree(raw_tree, ctx->raw_source->items, 0);

  String_Builder pp_source = {0};
  // expand_macros_tree(raw_tree, ctx->raw_source->items,
  // ctx->raw_source->count,
  //                    &pp_source);

  HashMap macros = {0};
  TSTree *pp_tree =
      expand_macros_tree(raw_tree, &macros, ctx->raw_source->items,
                         ctx->raw_source->count, &pp_source);

  nob_log(INFO, GREEN("After macro expansion: \n%.*s\n"), (int)pp_source.count,
          pp_source.items);
  // debug_tree(pp_tree, pp_source.items, 0);
  OutReplacements corrections = {0};
  correct_tree(&corrections, ts_tree_root_node(pp_tree), pp_source.items);

  nob_log(INFO, "Gathered %d corrections", corrections.count);

  String_Builder processed_source = {0};
  char *_cursor = pp_source.items;
  int comptimetype_counter = 0;
  WalkContext walk_ctx = (WalkContext){0};

  nob_da_foreach(TSNode, node, &corrections) {

    Slice r = ts_node_range(*node, pp_source.items);

    // if (_cursor > r.start)
    //   continue;

    // assert(_cursor <= r.start && "cursor is ahead of comptime slice");

    size_t offset = r.start - _cursor;
    assert(offset >= 0);

    nob_log(INFO, BOLD("[%zu] Correcting _ComptimeType: ") "%.*s", offset,
            r.len, r.start);
    // save comptimetype definition:
    assert(walk_ctx.comptimetype_stmts.count == comptimetype_counter);
    da_append(&walk_ctx.comptimetype_stmts, r);

    nob_log(
        INFO,
        BOLD("Replacing _ComptimeType with placeholder: ") "%.*s -> "
                                                           "_COMPTIMETYPE_%d",
        r.len, r.start, comptimetype_counter);

    nob_log(INFO, "Appending %zu bytes until comptimetype", offset);
    // nob_log(INFO, "%.*s", _cursor, offset);
    if (offset > 0) {
      nob_sb_append_buf(&processed_source, _cursor, offset);
    }

    nob_sb_appendf(&processed_source, "_COMPTIMETYPE_%d",
                   comptimetype_counter++);
    _cursor = r.start + r.len; // jump over the comptime slice
  }

  ssize_t _offset = pp_source.items + pp_source.count - _cursor;
  assert(_offset >= 0);
  nob_sb_append_buf(&processed_source, _cursor, _offset);

  // nob_log(INFO, GREEN("CLEAN TREE:: "));
  // nob_log(INFO, GREEN("%.*s"), processed_source.count,
  // processed_source.items);

  nob_log(INFO, PURPLE("After corrections :: \n%.*s"),
          (int)processed_source.count, processed_source.items);

  // const char *tree_source = processed_source.items;
  TSTree *clean_tree = ts_parser_parse_string(
      cparser, NULL, processed_source.items, processed_source.count);

  // debug_tree(clean_tree, processed_source.items, 0);
  nob_log(INFO, GREEN("^^^^^^^^^^^^^^"));

  TSNode root = ts_tree_root_node(clean_tree);

  register_comptime_dependencies(&walk_ctx, (LocalWalkContext){0}, root,
                                 processed_source.items, 0);
  // const char *nul =
  //     memchr(processed_source.items, '\0', processed_source.count);
  // if (nul) {
  //   ptrdiff_t pos = nul - processed_source.items;
  //   fprintf(stderr, "Early NUL at byte %td of %d\n", pos,
  //           processed_source.count);
  // }

  strip_comptime_dependencies(&walk_ctx, (LocalWalkContext){0}, root,
                              processed_source.items, 0);

  // C_FileBuilder runner = {0};
  String_Builder runner_definitions = {0};
  String_Builder runner_main = {0};
  int comptime_count = 0;

  nob_da_foreach(Slice, it, &walk_ctx.comptime_stmts) {
    nob_sb_appendf(&runner_definitions, "\n__Comptime_Statement_Fn(%d, %.*s)\n",
                   comptime_count, it->len, it->start);

    nob_sb_appendf(&runner_main,
                   "__Comptime_Register_Main_Exec(%d); // "
                   "execute comptime statement #%d\n",
                   comptime_count, comptime_count);

    nob_log(INFO, BOLD("COMPTIME SNIPPET ::") PURPLE("%.*s"), (int)it->len,
            it->start);
    comptime_count++;
  }

  String_Builder stripped_input_source = {0};
  char *cursor = (char *)processed_source.items;
  nob_da_foreach(Slice, it, &walk_ctx.to_be_removed) {
    nob_log(INFO, BLUE("TOBEREMOVED: \n'''\n%.*s\n'''\n"), it->len, it->start);
  }
  nob_da_foreach(Slice, it, &walk_ctx.to_be_removed) {
    // append everything until here to the source
    // TODO: it should be ok for the same root_slice to contain multiple
    // comptime blocks (currenlty itsnot)
    // we already removed that slice

    nob_log(INFO, RED("Removing: \n'''\n%.*s\n'''\n"), it->len, it->start);
    if (cursor >= it->start)
      continue;
    assert(cursor < it->start && "cursor is ahead of comptime slice");

    ssize_t offset = it->start - cursor;
    assert(offset >= 0);

    nob_log(INFO, GREEN("Appending: \n'''\n %.*s \n'''\n"), (int)offset,
            cursor);
    nob_sb_append_buf(&stripped_input_source, cursor, offset);
    cursor = it->start + it->len; // jump over the comptime slice
  }

  ssize_t offset = processed_source.items + processed_source.count - cursor;
  assert(offset >= 0);
  nob_sb_append_buf(&stripped_input_source, cursor, offset);
  nob_log(INFO, BOLD("\n\nAmalgamated stripped file: \n") "%.*s",
          (int)stripped_input_source.count, stripped_input_source.items);

  // save stripped source
  nob_write_entire_file(ctx->stripped_source_path, stripped_input_source.items,
                        stripped_input_source.count);

  nob_write_entire_file(ctx->runner_defs_path, runner_definitions.items,
                        runner_definitions.count);

  // const char *runner_main_filename =
  //     nob_temp_sprintf("%somptime.runner_main", filename);

  nob_write_entire_file(ctx->runner_main_path, runner_main.items,
                        runner_main.count);

  Nob_Cmd build_cmd = {0};
  // build_compile_base_command(&build_cmd, ctx->parsed_argv);

  // String_Builder static_obj_filename = {0};
  // nob_sb_appendf(&static_obj_filename, "_%s.o", filename);
  // static_obj_filename.items[static_obj_filename.count] = '\0';

  // nob_cmd_append(&build_cmd, stripped_source_filename);
  // nob_cmd_append(&build_cmd, "-c");
  // nob_cmd_append(&build_cmd, "-o", static_obj_filename.items);
  // if (!nob_cmd_run(&build_cmd))
  //   fatal("Failed to compile stripped source");

  build_compile_base_command(&build_cmd, ctx->parsed_argv);
  nob_cmd_append(&build_cmd, "runner.templ.c");
  // nob_cmd_append(&build_cmd, static_obj_filename.items);
  nob_cmd_append(&build_cmd, "-o", ctx->runner_exepath);
  // nob_cmd_append(&build_cmd, "-E");
  nob_cmd_append(
      &build_cmd,
      temp_sprintf("-D_INPUT_PROGRAM_PATH=\"%s\"", ctx->stripped_source_path),
      temp_sprintf("-D_INPUT_COMPTIME_DEFS_PATH=\"%s\"", ctx->runner_defs_path),
      temp_sprintf("-D_INPUT_COMPTIME_MAIN_PATH=\"%s\"", ctx->runner_main_path),
      temp_sprintf("-D_OUTPUT_HEADERS_PATH=\"%s\"", ctx->gen_header_path));

  if (!nob_cmd_run(&build_cmd))
    fatal("Failed to compile comptime runner");

  sb_appendf(&walk_ctx.out_h,
             "/*// @generated - ccomptime™ v0.0.1 - %lu \\*/\n", time(NULL));
  sb_appendf(
      &walk_ctx.out_h,
      "#define _CONCAT_(x, y) x##y\n"
      "#define CONCAT(x, y) _CONCAT_(x, y)\n"
      "#define _Comptime(x) _COMPTIME_X(__COUNTER__, x)\n"
      "#define _ComptimeType(x) _COMPTIME_X(__COUNTER__, x)\n"
      "#define _COMPTIME_X(n, x) CONCAT(_PLACEHOLDER_COMPTIME_X, n)(x)\n");

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

  ts_tree_delete(clean_tree);

  ts_parser_delete(cparser);
  sb_free(walk_ctx.out_h);
  nob_temp_rewind(_mark);
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

    run_file(input_filename, &ctx);

    // assert(ctx.runner_templ_path);
    // Nob_Cmd cmd = {0};

    // nob_cmd_append(&cmd, Parsed_Argv_compiler_name(ctx.parsed_argv));
    // cmd_append_arg_indeces(ctx.parsed_argv, &ctx.parsed_argv->flags, &cmd);
    // nob_cmd_append(&cmd, "-O0");

    // cmd_append_inputs_except(ctx.parsed_argv, ctx.input_path, &cmd);

    // // nob_cmd_append(&cmd, ctx.runner_cpath);
    // nob_cmd_append(&cmd, "runner.templ.c");

    // if (!(ctx.parsed_argv->cct_flags & CliComptimeFlag_Debug)) {
    //   nob_cmd_append(&cmd, "-w");
    // } else {
    //   nob_cmd_append(&cmd, "-g", "-fsanitize=address,undefined",
    //                  "-fno-omit-frame-pointer");
    // }

    // nob_cmd_append(
    //     &cmd, "-o", ctx.runner_exepath,
    //     temp_sprintf("-D_INPUT_PROGRAM_PATH=\"%s\"", ctx.input_path),
    //     temp_sprintf("-D_INPUT_COMPTIME_DEFS_PATH=\"%s\"",
    //                  ctx.runner_defs_path),
    //     temp_sprintf("-D_INPUT_COMPTIME_MAIN_PATH=\"%s\"",
    //                  ctx.runner_main_path),
    //     temp_sprintf("-D_OUTPUT_HEADERS_PATH=\"%s\"", ctx.gen_header_path));

    // if (!nob_cmd_run(&cmd)) {
    //   nob_log(ERROR, "failed to compile runner %s", ctx.runner_templ_path);
    //   exit(1);
    // }

    Nob_Cmd cmd = {0};
    nob_log(INFO, "Running runner %s", ctx.runner_exepath);
    nob_cmd_append(&cmd, nob_temp_sprintf("./%s", ctx.runner_exepath));
    if (!nob_cmd_run(&cmd)) {
      nob_log(ERROR, "failed to run runner %s", ctx.runner_templ_path);
      exit(1);
    }

    // build_compile_base_command(cmd, )
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

    // log_info("WRTING FINAL OUTPUT FILE %s (%zu)bytes ",
    // ctx.final_out_path,
    //          repls2.count);
    // write_entire_file(ctx.final_out_path, repls2.items, repls2.count);

    // // -- APPEND FINAL OUTPUT FILE TO ARGV --
    // nob_cmd_append(&final, ctx.final_out_path);

    // log_info("Appended final output file %s to final argv",
    // ctx.final_out_path);

    // log_info("Scheduling intermediate files for deletion");
    da_append(&files_to_remove, ctx.preprocessed_path);
    da_append(&files_to_remove, ctx.runner_main_path);
    da_append(&files_to_remove, ctx.runner_defs_path);
    da_append(&files_to_remove, ctx.stripped_source_path);
    da_append(&files_to_remove, ctx.runner_templ_path);
    da_append(&files_to_remove, ctx.runner_exepath);
    da_append(&files_to_remove, ctx.vals_path);
    da_append(&files_to_remove, ctx.final_out_path);
  }

  Nob_Cmd final = {0};
  nob_cmd_append(&final, Parsed_Argv_compiler_name(&parsed_argv));
  cmd_append_arg_indeces(&parsed_argv, &parsed_argv.input_files, &final);
  cmd_append_arg_indeces(&parsed_argv, &parsed_argv.output_files, &final);
  cmd_append_arg_indeces(&parsed_argv, &parsed_argv.flags, &final);

  if (!cmd_run(&final)) {
    nob_log(ERROR, "failed to compile final output");
    return 1;
  } else {
    nob_log(INFO, "Successfuly compiled final output");
  }

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
