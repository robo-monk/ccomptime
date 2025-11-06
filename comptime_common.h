#ifndef CCOMPTIME_COMMON_H
#define CCOMPTIME_COMMON_H

#include "ansi.h"
#include "utils.h"

#include "deps/hashmap/hashmap.h"
#include "nob.h"
#ifndef TS_INCLUDE_SYMBOLS
#define TS_INCLUDE_SYMBOLS
#endif
#include "tree_sitter_c_api.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct {
  const char *start;
  int len;
} Slice;

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
    int *items;
    size_t count, capacity;
  } comptimetype_stmt_indices;

  struct {
    Slice *items;
    size_t count, capacity;
  } comptime_stmts;

  // Stores the source of the node following each comptime statement
  // Used for introspection (e.g., getting type info of struct after decorator)
  struct {
    Slice *items;
    size_t count, capacity;
  } comptime_next_nodes;
} WalkContext;

int min_int(int a, int b);
bool slice_begins_with(Slice s, const char *prefix);
Slice slice_strip_prefix(Slice s, const char *prefix);
const char *get_parent_dir(const char *filepath);
const char *resolve(const char *FILE_NAME, const char *path);
const char *path_basename(const char *filepath);

Slice ts_node_range(TSNode node, const char *src);
String_View ts_node_to_str_view(TSNode node, const char *src);
void debug_tree(TSTree *tree, const char *src, int depth);
void debug_tree_node(TSNode node, const char *src, int depth);

bool ts_node_is_comptime_kw(TSNode node, const char *src);
bool ts_node_is_comptimetype_kw(TSNode node, const char *src);

#endif // CCOMPTIME_COMMON_H
