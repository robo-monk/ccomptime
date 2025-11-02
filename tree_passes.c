#include "tree_passes.h"
#include "tree_sitter_c_api.h"

#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#ifndef ts_builtin_sym_error
#define ts_builtin_sym_error ((TSSymbol)-1)
#endif

typedef struct {
  Slice *items;
  size_t count, capacity;
} OutReplacements;

// Attempt to recover `_ComptimeType(...)` spans that tree-sitter placed inside
// `ERROR` nodes, so we still rewrite them before the dependency walk.
static bool try_append_error_comptime_type(OutReplacements *out_replacements,
                                           TSNode node, const char *src,
                                           size_t len) {
  TSNode parent = ts_node_parent(node);
  if (ts_node_is_null(parent) ||
      ts_node_symbol(parent) != ts_builtin_sym_error) {
    return false;
  }

  Slice id_range = ts_node_range(node, src);
  const char *cursor = id_range.start + id_range.len;
  const char *end = src + len;

  while (cursor < end && isspace((unsigned char)*cursor)) {
    cursor++;
  }
  if (cursor >= end || *cursor != '(') {
    return false;
  }

  int depth = 0;
  const char *p = cursor;
  while (p < end) {
    char c = *p;
    if (c == '"') {
      p++;
      while (p < end) {
        if (*p == '\\' && p + 1 < end) {
          p += 2;
          continue;
        }
        if (*p == '"') {
          p++;
          break;
        }
        p++;
      }
      continue;
    }
    if (c == '\'') {
      p++;
      while (p < end) {
        if (*p == '\\' && p + 1 < end) {
          p += 2;
          continue;
        }
        if (*p == '\'') {
          p++;
          break;
        }
        p++;
      }
      continue;
    }
    if (c == '/' && p + 1 < end) {
      if (p[1] == '/') {
        p += 2;
        while (p < end && *p != '\n') {
          p++;
        }
        continue;
      }
      if (p[1] == '*') {
        p += 2;
        while (p + 1 < end && !(p[0] == '*' && p[1] == '/')) {
          p++;
        }
        if (p + 1 < end) {
          p += 2;
        }
        continue;
      }
    }

    if (c == '(') {
      depth++;
    } else if (c == ')') {
      depth--;
      if (depth == 0) {
        p++;
        Slice replacement = {.start = id_range.start,
                             .len = (int)(p - id_range.start)};
        nob_da_append(out_replacements, replacement);
        return true;
      }
    }
    p++;
  }

  return false;
}

// Walk the syntax tree and collect slices that correspond to
// `_ComptimeType(...)` invocations irrespective of how tree-sitter represented
// them.
static void correct_tree(OutReplacements *out_replacements, TSNode node,
                         const char *src, size_t len) {
  assert(src);
  if (ts_node_symbol(node) == sym_call_expression) {
    if (ts_node_symbol(ts_node_child(node, 0)) == sym_identifier &&
        ts_node_is_comptimetype_kw(ts_node_child(node, 0), src)) {
      nob_da_append(out_replacements, ts_node_range(node, src));
    }
  } else if (ts_node_symbol(node) == sym_macro_type_specifier) {
    if (ts_node_symbol(ts_node_child(node, 0)) == sym_identifier &&
        ts_node_is_comptimetype_kw(ts_node_child(node, 0), src)) {
      nob_da_append(out_replacements, ts_node_range(node, src));
    }
  } else if (ts_node_symbol(node) == sym_type_descriptor &&
             ts_node_is_comptimetype_kw(ts_node_child(node, 0), src)) {
    nob_da_append(out_replacements, ts_node_range(node, src));
  } else if (ts_node_symbol(node) == sym_identifier &&
             ts_node_is_comptimetype_kw(node, src)) {
    if (try_append_error_comptime_type(out_replacements, node, src, len)) {
      nob_log(INFO, RED("Found ComptimeType within error\n!"));
      // Replacement recorded via error recovery path.
    }
  }

  uint32_t n = ts_node_child_count(node);
  for (uint32_t i = 0; i < n; i++) {
    correct_tree(out_replacements, ts_node_child(node, i), src, len);
  }
}

// Rewrite `_ComptimeType` occurrences to placeholders while remembering their
// source slices for later evaluation.
TSTree *cct_correct_comptimetype_nodes(TSParser *parser, TSTree *tree,
                                       const char *src, size_t len,
                                       WalkContext *ctx,
                                       String_Builder *out_source) {
  OutReplacements corrections = {0};
  nob_log(NOB_VERBOSE, "=== Pre correction tree ===");
  debug_tree(tree, src, 0);
  nob_log(NOB_VERBOSE, "=== === ===");
  correct_tree(&corrections, ts_tree_root_node(tree), src, len);

  nob_log(VERBOSE, "Gathered %zu corrections", corrections.count);

  char *cursor = (char *)src;
  int comptimetype_counter = 0;

  if (corrections.count > 0) {
    nob_da_foreach(Slice, replacement, &corrections) {
      Slice r = *replacement;
      assert(cursor <= r.start && "cursor is ahead of comptime slice");

      size_t offset = (size_t)(r.start - cursor);

      nob_log(VERBOSE, BOLD("[%zu] Correcting _ComptimeType: ") "%.*s", offset,
              r.len, r.start);
      assert(ctx->comptimetype_stmts.count == (size_t)comptimetype_counter);
      da_append(&ctx->comptimetype_stmts, r);
      da_append(&ctx->comptimetype_stmt_indices, -1);

      nob_log(
          INFO,
          BOLD("Replacing _ComptimeType with placeholder: ") "%.*s -> "
                                                             "_COMPTIMETYPE_%d",
          r.len, r.start, comptimetype_counter);

      nob_log(VERBOSE, "Appending %zu bytes until comptimetype", offset);
      if (offset > 0) {
        nob_sb_append_buf(out_source, cursor, offset);
      }

      nob_sb_appendf(out_source, "_COMPTIMETYPE_%d", comptimetype_counter++);
      cursor = (char *)r.start + r.len;
    }
  }

  ssize_t tail = (const char *)src + len - cursor;
  assert(tail >= 0);
  nob_sb_append_buf(out_source, cursor, (size_t)tail);

  ts_tree_delete(tree);

  TSTree *clean_tree = ts_parser_parse_string(parser, NULL, out_source->items,
                                              out_source->count);
  return clean_tree;
}

typedef struct {
  TSNode *decleration_root;
  TSNode *preproc_def_root;
  TSNode *call_expression_root;
  TSNode *macro_type_specifier_root;
  TSNode *function_definition_root;
  TSNode *type_definition_root;
  int child_idx;
} LocalWalkContext;

static TSNode find_type_definition_identifier(TSNode node, const char *src) {
  TSNode named = ts_node_child_by_field_name(node, "name", 4);
  if (!ts_node_is_null(named)) {
    return named;
  }
  uint32_t n = ts_node_child_count(node);
  for (uint32_t i = 0; i < n; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol child_sym = ts_node_symbol(child);
    const char *child_type = ts_node_type(child);
    if (child_sym == alias_sym_type_identifier ||
        (child_type && (strcmp(child_type, "type_identifier") == 0 ||
                        strcmp(child_type, "identifier") == 0))) {
      Slice child_slice = ts_node_range(child, src);
      if (slice_begins_with(child_slice, "_COMPTIMETYPE_")) {
        continue;
      }
      return child;
    }
  }
  TSNode null_node = {0};
  return null_node;
}

static Slice parse_comptime_call_expr2(TSNode call_expression_root,
                                       const char *src) {
  assert(ts_node_symbol(call_expression_root) == sym_call_expression);
  TSNode argument_list = ts_node_child(call_expression_root, 1);

  debug_tree_node(argument_list, src, 4);
  assert(ts_node_symbol(argument_list) == sym_argument_list);
  String_View _comptime_code = ts_node_to_str_view(argument_list, src);
  assert(_comptime_code.count > 2 && "Empty _Comptime are not allowed");

  assert(_comptime_code.data[0] == '(');
  assert(_comptime_code.data[_comptime_code.count - 1] == ')');

  char *start = (char *)_comptime_code.data + 1;
  char *end = (char *)_comptime_code.data + _comptime_code.count - 2;

  while (*start == ' ' || *start == '\t' || *start == '{') {
    start++;
  }
  while (*end == ' ' || *end == '\t' || *end == '}' || *end == ';') {
    end--;
  }

  return (Slice){.start = start, .len = (int)(end - start + 1)};
}

// First pass: mark declarations and functions that depend on comptime blocks.
static void register_comptime_dependencies(WalkContext *const ctx,
                                           LocalWalkContext local, TSNode node,
                                           const char *src, unsigned depth) {
  TSSymbol sym = ts_node_symbol(node);
  switch (sym) {
  case sym_function_definition:
    local.function_definition_root = &node;
    break;
  case sym_declaration:
    local.decleration_root = &node;
    break;
  case sym_type_definition:
    local.type_definition_root = &node;
    break;
  case sym_preproc_def:
    local.preproc_def_root = &node;
    break;
  case sym_macro_type_specifier:
    local.macro_type_specifier_root = &node;
    break;
  case sym_call_expression:
    local.call_expression_root = &node;
    break;
  default:
    break;
  }

  Slice r = {0};
  if (sym == sym_identifier && ts_node_is_comptime_kw(node, src)) {
    if (!local.call_expression_root && local.preproc_def_root &&
        local.child_idx == 1)
      fatal("Redefining `_Comptime` macro is not supported");
    if (!local.call_expression_root)
      fatal("Invalid use of _Comptime");

    r = parse_comptime_call_expr2(*local.call_expression_root, src);
    nob_log(VERBOSE, BOLD("Parsed _Comptime call : ") "%.*s", r.len, r.start);
  } else if (sym == sym_identifier && ts_node_is_comptimetype_kw(node, src)) {
    debug_tree_node(node, src, (int)depth);
    assert(0 && "_ComptimeType should be corrected before walking");
  } else if (sym == alias_sym_type_identifier) {
    Slice type_id = ts_node_range(node, src);
    if (slice_begins_with(type_id, "_COMPTIMETYPE_")) {
      Slice out = slice_strip_prefix(type_id, "_COMPTIMETYPE_");
      int index = atoi(out.start);
      nob_log(VERBOSE, "Found comptimetype placeholder %d", index);
      assert((size_t)index < ctx->comptimetype_stmts.count);
      assert((size_t)index < ctx->comptimetype_stmt_indices.count);
      assert(ctx->comptimetype_stmt_indices.items[index] == -1);
      ctx->comptimetype_stmt_indices.items[index] =
          (int)ctx->comptime_stmts.count;
      r = ctx->comptimetype_stmts.items[index];
    }
  }

  if (r.start) {
    if (local.function_definition_root) {
      assert(ts_node_symbol(ts_node_child(*local.function_definition_root,
                                          1)) == sym_function_declarator);
      assert(ts_node_symbol(ts_node_child(
                 ts_node_child(*local.function_definition_root, 1), 0)) ==
             sym_identifier);
      TSNode func_identifier =
          ts_node_child(ts_node_child(*local.function_definition_root, 1), 0);
      Slice func_name = ts_node_range(func_identifier, src);

      nob_log(VERBOSE, ORANGE("function %.*s is comptime dependent"),
              ts_node_range(func_identifier, src).len,
              ts_node_range(func_identifier, src).start);

      hashmap_put2(&ctx->comptime_dependencies, (char *)func_name.start,
                   func_name.len, (void *)1);

    } else if (local.decleration_root) {
      nob_log(VERBOSE, ORANGE("Stripping top level declaration with comptime"));
      assert(ts_node_symbol(ts_node_child(*local.decleration_root, 1)) ==
             sym_init_declarator);
      assert(ts_node_symbol(
                 ts_node_child(ts_node_child(*local.decleration_root, 1), 0)) ==
             sym_identifier);

      TSNode var_identifier =
          ts_node_child(ts_node_child(*local.decleration_root, 1), 0);
      Slice var_name = ts_node_range(var_identifier, src);

      nob_log(VERBOSE, ORANGE("declaration %.*s is comptime dependent"),
              ts_node_range(var_identifier, src).len,
              ts_node_range(var_identifier, src).start);

      hashmap_put2(&ctx->comptime_dependencies, (char *)var_name.start,
                   var_name.len, (void *)1);

    } else if (local.type_definition_root) {
      nob_log(VERBOSE, ORANGE("Stripping type definition with comptime"));
      TSNode type_name_node =
          find_type_definition_identifier(*local.type_definition_root, src);
      if (!ts_node_is_null(type_name_node)) {
        Slice type_name = ts_node_range(type_name_node, src);
        nob_log(VERBOSE, ORANGE("type %.*s is comptime dependent"),
                type_name.len, type_name.start);
        hashmap_put2(&ctx->comptime_dependencies, (char *)type_name.start,
                     type_name.len, (void *)1);
      }

    } else {
      nob_log(VERBOSE, ORANGE("Stripping top level comptime block"));
    }

    da_append(&ctx->comptime_stmts, r);
  }

  uint32_t n = ts_node_child_count(node);
  for (uint32_t i = 0; i < n; i++) {
    local.child_idx = (int)i;
    register_comptime_dependencies(ctx, local, ts_node_child(node, i), src,
                                   depth + 1);
  }
}

// Second pass: remove the statements whose identifiers were marked as
// comptime-dependent in the first traversal.
static void strip_comptime_dependencies(WalkContext *const ctx,
                                        LocalWalkContext local, TSNode node,
                                        const char *src, unsigned depth) {
  TSSymbol sym = ts_node_symbol(node);
  switch (sym) {
  case sym_function_definition:
    local.function_definition_root = &node;
    break;
  case sym_declaration:
    local.decleration_root = &node;
    break;
  case sym_type_definition:
    local.type_definition_root = &node;
    break;
  case sym_preproc_def:
    local.preproc_def_root = &node;
    break;
  case sym_macro_type_specifier:
    local.macro_type_specifier_root = &node;
    break;
  case sym_call_expression:
    local.call_expression_root = &node;
    break;
  default:
    break;
  }

  Slice node_slice = ts_node_range(node, src);
  if ((sym == sym_identifier || sym == alias_sym_type_identifier) &&
      hashmap_get2(&ctx->comptime_dependencies, (char *)node_slice.start,
                   node_slice.len)) {
    nob_log(VERBOSE, MAGENTA("Within a comptime dependency :: !"));

    if (local.function_definition_root) {
      Slice r = ts_node_range(*local.function_definition_root, src);

      nob_log(VERBOSE,
              ORANGE("Stripping comptime dependent function (%d) '%.*s'"),
              ts_node_range(*local.function_definition_root, src).len,
              ts_node_range(*local.function_definition_root, src).len,
              ts_node_range(*local.function_definition_root, src).start);

      nob_da_append(&ctx->to_be_removed,
                    ts_node_range(*local.function_definition_root, src));
    } else if (local.decleration_root) {
      nob_log(VERBOSE, ORANGE("Stripping top level declaration with comptime"));

      nob_da_append(&ctx->to_be_removed,
                    ts_node_range(*local.decleration_root, src));
    } else if (local.type_definition_root) {
      nob_log(VERBOSE,
              ORANGE("Stripping top level type definition with comptime"));

      nob_da_append(&ctx->to_be_removed,
                    ts_node_range(*local.type_definition_root, src));
    } else {
      nob_log(VERBOSE, ORANGE("Stripping top level comptime block"));

      nob_da_append(&ctx->to_be_removed,
                    ts_node_range(*local.call_expression_root, src));
    }
  };

  uint32_t n = ts_node_child_count(node);
  for (uint32_t i = 0; i < n; i++) {
    local.child_idx = (int)i;
    strip_comptime_dependencies(ctx, local, ts_node_child(node, i), src,
                                depth + 1);
  }
}

// Entry point: run the marking pass followed by the stripping pass on the tree.
void cct_collect_comptime_statements(WalkContext *ctx, TSTree *tree,
                                     const char *src) {
  TSNode root = ts_tree_root_node(tree);
  register_comptime_dependencies(ctx, (LocalWalkContext){0}, root, src, 0);
  strip_comptime_dependencies(ctx, (LocalWalkContext){0}, root, src, 0);
}
