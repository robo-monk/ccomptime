#include "macro_expansion.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  TSNode node;
  String_View with;
} Macro_Replacement;

typedef struct {
  Macro_Replacement *items;
  size_t count, capacity;
} Macro_Replacements;

typedef struct {
  TSNode node;
  Nob_String_View with;
} NodeReplacement;

typedef struct {
  NodeReplacement *items;
  size_t count, capacity;
} NodeReplacements;

typedef struct {
  NodeReplacements replacements;
} MacroExpansionCtx;

static void expand_macro_node(TSNode node, const char *src, Strings *arg_names,
                              Strings *arg_values,
                              Macro_Replacements *replacements);

void macros_put(HashMap *macros, const char *key, int key_len,
                MacroDefinition *def) {
  hashmap_put2(macros, (char *)key, key_len, def);
}

MacroDefinition *macros_get(HashMap *macros, const char *key, int key_len) {
  return (MacroDefinition *)hashmap_get2(macros, (char *)key, key_len);
}

static void expand_macro_tree(MacroDefinition *macro_def, Strings arg_values,
                              String_Builder *out_sb) {
  assert(macro_def != NULL);
  assert(macro_def->arg_names.count == arg_values.count);
  nob_log(VERBOSE, "------- Expand macro tree with %zu args ---- ",
          arg_values.count);

  debug_tree(macro_def->body_tree, macro_def->body_src, 4);
  nob_log(VERBOSE, "^^^^^^^ Expand macro tree with %zu args ^^^^^^^",
          arg_values.count);

  TSNode root = ts_tree_root_node(macro_def->body_tree);
  Slice root_range = ts_node_range(root, macro_def->body_src);

  Macro_Replacements replacements = {0};

  expand_macro_node(root, macro_def->body_src, &macro_def->arg_names,
                    &arg_values, &replacements);

  nob_log(VERBOSE, "========= Macro expansion replacements: %zu",
          replacements.count);

  assert(root_range.start == macro_def->body_src);

  char *cursor = (char *)macro_def->body_src;
  if (replacements.count != arg_values.count) {
    nob_log(ERROR, "Macro expansion failed: %zu replacements != %zu args",
            replacements.count, arg_values.count);
  }
  assert(replacements.count == arg_values.count);

  nob_da_foreach(Macro_Replacement, it, &replacements) {
    Slice r = ts_node_range(it->node, macro_def->body_src);
    assert(r.start >= cursor);

    nob_sb_append_buf(out_sb, cursor, r.start - cursor);
    nob_sb_append_buf(out_sb, it->with.data, it->with.count);
    cursor = (char *)r.start + r.len;
  }

  nob_sb_append_buf(out_sb, cursor, root_range.start + root_range.len - cursor);
  nob_log(VERBOSE, ">> Expanded as %.*s", (int)out_sb->count, out_sb->items);
}

static void expand_macro_node(TSNode node, const char *src, Strings *arg_names,
                              Strings *arg_values,
                              Macro_Replacements *replacements) {

  assert(arg_names->count == arg_values->count);
  switch (ts_node_symbol(node)) {
  case sym_preproc_directive: {
    for (size_t i = 0; i < arg_names->count; i++) {
      String_View arg_name = arg_names->items[i];

      Slice node_range = {
          .start = ts_node_range(node, src).start + 1,
          .len = ts_node_range(node, src).len - 1,
      };

      nob_log(VERBOSE, "~> %.*s", node_range.len, node_range.start);

      if ((size_t)node_range.len != arg_name.count)
        continue;

      assert(src && "src should be non null");

      if (memcmp(node_range.start, arg_name.data, arg_name.count) == 0) {

        String_View arg_value = arg_values->items[i];
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
  case sym_identifier: {

    for (size_t i = 0; i < arg_names->count; i++) {
      String_View arg_name = arg_names->items[i];

      if ((size_t)ts_node_range(node, src).len != arg_name.count)
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
  default:
    break;
  }

  uint32_t n = ts_node_child_count(node);
  for (uint32_t i = 0; i < n; i++) {
    expand_macro_node(ts_node_child(node, i), src, arg_names, arg_values,
                      replacements);
  }
}

static bool node_has_comptime_identifier(TSNode node, const char *src) {
  if (ts_node_symbol(node) == sym_identifier) {
    return ts_node_is_comptime_kw(node, src) ||
           ts_node_is_comptimetype_kw(node, src);
  }

  uint32_t n = ts_node_child_count(node);
  for (uint32_t i = 0; i < n; i++) {
    if (node_has_comptime_identifier(ts_node_child(node, i), src)) {
      return true;
    }
  }
  return false;
}

static bool has_comptime_identifier(TSTree *tree, const char *src) {
  assert(tree != NULL);
  return node_has_comptime_identifier(ts_tree_root_node(tree), src);
}

static int try_expand_macro_call_expression(TSNode node,
                                            MacroDefinitionHashMap *macros,
                                            const char *src,
                                            String_Builder *expanded) {
  assert(ts_node_symbol(node) == sym_call_expression);

  TSNode identifier = ts_node_child(node, 0);
  if (ts_node_symbol(identifier) != sym_identifier)
    return 0;

  Slice id_range = ts_node_range(identifier, src);

  MacroDefinition *macro_def = macros_get(macros, id_range.start, id_range.len);
  if (!macro_def)
    return 0;

  TSNode argument_list = ts_node_child(node, 1);
  assert(ts_node_symbol(argument_list) == sym_argument_list);

  nob_log(VERBOSE, ORANGE("=> expanding macro def :: %.*s\n"),
          ts_node_range(macro_def->identifier, src).len,
          ts_node_range(macro_def->identifier, src).start);

  if (macro_def->arg_names.count > 0) {
    Strings arg_values = strings_new(ts_node_child_count(argument_list));

    for (uint32_t i = 0; i < ts_node_child_count(argument_list); i++) {
      TSNode arg = ts_node_child(argument_list, i);
      TSSymbol arg_sym = ts_node_symbol(arg);

      if (arg_sym == anon_sym_LPAREN || arg_sym == anon_sym_COMMA ||
          arg_sym == anon_sym_RPAREN) {
        continue;
      }

      strings_append(&arg_values, ts_node_to_str_view(arg, src));
    }
    expand_macro_tree(macro_def, arg_values, expanded);
  } else {
    nob_log(VERBOSE, "  [MACRO CALL: %.*s]", id_range.len, id_range.start);
    expand_macro_tree(macro_def, (Strings){0}, expanded);
  }

  nob_log(VERBOSE, "Macro expansion result: %.*s", (int)expanded->count,
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

static bool put_macro_def_if_comptime_relevant(
    TSParser *parser, MacroDefinitionHashMap *const macros,
    TSNode macro_identifier, TSNode macro_body, MacroDefinition *macro_def,
    const char *src) {
  Slice macro_body_range = ts_node_range(macro_body, src);
  TSTree *tree = ts_parser_parse_string(parser, NULL, macro_body_range.start,
                                        (uint32_t)macro_body_range.len);

  Slice macro_identifier_range = ts_node_range(macro_identifier, src);
  if (!has_comptime_identifier(tree, ts_node_range(macro_body, src).start) &&
      !hashmap_get2(macros, (char *)ts_node_range(macro_identifier, src).start,
                    ts_node_range(macro_identifier, src).len)) {
    nob_log(VERBOSE,
            RED("'%.*s' macro has been proven irrelevant because it does not "
                "contain the `_Comptime` keyword"),
            macro_identifier_range.len, macro_identifier_range.start);
    ts_tree_delete(tree);
    return false;
  } else {
    nob_log(VERBOSE,
            GREEN("'%.*s' macro is a *comptime* macro or contains another "
                  "*comptime* macro"),
            macro_identifier_range.len, macro_identifier_range.start);
  }
  if (!macro_def)
    macro_def = calloc(1, sizeof(MacroDefinition));

  macro_def->identifier = macro_identifier;
  macro_def->body_tree = tree;
  macro_def->body_src = ts_node_range(macro_body, src).start;

  macros_put(macros, ts_node_range(macro_def->identifier, src).start,
             ts_node_range(macro_def->identifier, src).len, macro_def);
  return true;
}

static bool parse_preproc_def(TSParser *parser,
                              MacroDefinitionHashMap *const macros, TSNode node,
                              const char *src) {
  assert(ts_node_symbol(node) == sym_preproc_def);

  assert(ts_node_symbol(ts_node_child(node, 0)) == aux_sym_preproc_def_token1);

  TSNode macro_identifier = ts_node_child(node, 1);
  assert(ts_node_symbol(macro_identifier) == sym_identifier);

  TSNode body = ts_node_child(node, 2);
  if (ts_node_is_null(body))
    return false;

  assert(ts_node_symbol(body) == sym_preproc_arg);
  return put_macro_def_if_comptime_relevant(parser, macros, macro_identifier,
                                            body, NULL, src);
}

static bool parse_preproc_function_def(TSParser *parser,
                                       MacroDefinitionHashMap *const macros,
                                       TSNode node, const char *src) {
  assert(ts_node_symbol(node) == sym_preproc_function_def);

  assert(ts_node_symbol(ts_node_child(node, 0)) == aux_sym_preproc_def_token1);

  TSNode macro_identifier = ts_node_child(node, 1);
  assert(ts_node_symbol(macro_identifier) == sym_identifier);

  TSNode preproc_params = ts_node_child(node, 2);
  if (ts_node_is_null(preproc_params))
    return false;

  assert(ts_node_symbol(preproc_params) == sym_preproc_params);

  MacroDefinition *macro = calloc(1, sizeof(MacroDefinition));
  uint32_t param_count = ts_node_child_count(preproc_params);
  for (uint32_t i = 0; i < param_count; i++) {
    TSNode param = ts_node_child(preproc_params, i);
    if (ts_node_symbol(param) != sym_identifier)
      continue;

    uint32_t start = ts_node_start_byte(param);
    uint32_t end = ts_node_end_byte(param);
    nob_log(VERBOSE, "Param %u: %.*s", i, (int)(end - start), src + start);

    nob_da_append(&macro->arg_names, ts_node_to_str_view(param, src));
  }
  nob_log(VERBOSE, "got in total of %zu args\n", macro->arg_names.count);

  TSNode body = ts_node_child(node, 3);
  assert(ts_node_symbol(body) == sym_preproc_arg);

  return put_macro_def_if_comptime_relevant(parser, macros, macro_identifier,
                                            body, macro, src);
}

static TSTree *apply_node_replacements_to_tree(
    TSParser *parser, NodeReplacements *replacements, TSTree *tree,
    const char *tree_src, size_t tree_src_len, String_Builder *out_source) {
  char *cursor = (char *)tree_src;

  if (replacements->count == 0) {
    nob_log(WARNING, YELLOW("No replacements performed for tree"));
    out_source->items = (char *)tree_src;
    out_source->count = tree_src_len;
    return tree;
  }

  nob_da_foreach(NodeReplacement, repl, replacements) {
    TSNode node = repl->node;
    Nob_String_View with = repl->with;

    Slice r = ts_node_range(node, tree_src);

    assert(cursor <= r.start && "cursor is ahead of slice");

    ssize_t offset = r.start - cursor;

    if (offset) {
      nob_sb_append_buf(out_source, cursor, offset);
    }
    if (with.count) {
      nob_sb_append_buf(out_source, with.data, with.count);
    }

    cursor = (char *)r.start + r.len;
  }
  ssize_t _offset = tree_src + tree_src_len - cursor;
  assert(_offset >= 0);
  nob_sb_append_buf(out_source, cursor, _offset);

  ts_tree_delete(tree);

  const char *tree_source = out_source->items;
  TSTree *clean_tree =
      ts_parser_parse_string(parser, NULL, tree_source, out_source->count);

  return clean_tree;
}

static void on_macro_expansion_cb(TSNode node, String_Builder *expanded,
                                  void *ctx) {
  MacroExpansionCtx *macro_ctx = ctx;

  nob_da_append(&macro_ctx->replacements,
                ((NodeReplacement){.node = node,
                                   .with = nob_sv_from_parts(
                                       expanded->items, expanded->count)}));

  nob_log(VERBOSE, "Macro was expanded to %.*s", (int)expanded->count,
          expanded->items);
}

static void expand_macros_tree_node(
    TSParser *parser, TSNode node, MacroDefinitionHashMap *macros,
    const char *src,
    void (*on_macro_expansion)(TSNode node, String_Builder *expanded,
                               void *ctx),
    void *on_macro_expansion_ctx) {

  TSSymbol sym = ts_node_symbol(node);

  if (sym == sym_preproc_function_def) {
    bool success = parse_preproc_function_def(parser, macros, node, src);
    nob_log(VERBOSE, "Parsed preproc function def: %d", success);
    return;
  }

  if (sym == sym_preproc_def) {
    bool success = parse_preproc_def(parser, macros, node, src);
    nob_log(VERBOSE, "Parsed preproc def: %d", success);
    return;
  }

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
    nob_log(VERBOSE, MAGENTA("%.*s -> %.*s"), (int)ts_node_range(node, src).len,
            ts_node_range(node, src).start, (int)expanded.count,
            expanded.items);
  }

  uint32_t n = ts_node_child_count(node);
  for (uint32_t i = 0; i < n; i++) {
    expand_macros_tree_node(parser, ts_node_child(node, i), macros, src,
                            on_macro_expansion, on_macro_expansion_ctx);
  }
}

TSTree *cct_expand_macros(TSParser *parser, TSTree *tree,
                          MacroDefinitionHashMap *macros, const char *tree_src,
                          size_t tree_len, String_Builder *out_source) {

  MacroExpansionCtx ctx = {.replacements = {0}};
  expand_macros_tree_node(parser, ts_tree_root_node(tree), macros, tree_src,
                          on_macro_expansion_cb, &ctx);
  return apply_node_replacements_to_tree(parser, &ctx.replacements, tree,
                                         tree_src, tree_len, out_source);
}
