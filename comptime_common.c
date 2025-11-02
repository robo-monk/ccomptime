#define HASHMAP_IMPLEMENTATION
#include "comptime_common.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int min_int(int a, int b) { return a < b ? a : b; }

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

const char *get_parent_dir(const char *filepath) {
  static char buffer[1024];
  const char *last_slash = strrchr(filepath, '/');

#ifdef _WIN32
  const char *last_bslash = strrchr(filepath, '\\');
  if (last_bslash && (!last_slash || last_bslash > last_slash))
    last_slash = last_bslash;
#endif

  if (!last_slash) {
    return ".";
  }

  size_t len = (size_t)(last_slash - filepath);
  if (len >= sizeof(buffer))
    len = sizeof(buffer) - 1;

  memcpy(buffer, filepath, len);
  buffer[len] = '\0';
  return buffer;
}

const char *path_basename(const char *filepath) {
  const char *last_slash = strrchr(filepath, '/');
#ifdef _WIN32
  const char *last_bslash = strrchr(filepath, '\\');
  if (last_bslash && (!last_slash || last_bslash > last_slash))
    last_slash = last_bslash;
#endif
  if (!last_slash)
    return filepath;
  return last_slash + 1;
}

const char *resolve(const char *FILE_NAME, const char *path) {
  return nob_temp_sprintf("%s/%s", get_parent_dir(FILE_NAME), path);
}

Slice ts_node_range(TSNode node, const char *src) {
  assert(src);
  assert(!ts_node_is_null(node));
  const char *start = src + ts_node_start_byte(node);
  int len = ts_node_end_byte(node) - ts_node_start_byte(node);
  return (Slice){start, len};
}

String_View ts_node_to_str_view(TSNode node, const char *src) {
  const char *start = src + ts_node_start_byte(node);
  int len = ts_node_end_byte(node) - ts_node_start_byte(node);
  return (String_View){.data = start, .count = len};
}

void debug_tree_node(TSNode node, const char *src, int depth) {
  if (nob_minimal_log_level > NOB_VERBOSE)
    return;

  for (unsigned i = 0; i < (unsigned)depth; i++)
    fprintf(stderr, ".");

  if (ts_node_has_error(node)) {
    fprintf(stderr, RED("[%s] (%d)"), ts_node_type(node), ts_node_symbol(node));
  } else if (ts_node_symbol(node) == sym_identifier &&
             ts_node_is_comptime_kw(node, src)) {
    fprintf(stderr, MAGENTA(" [%s] (%d) [_Comptime]"), ts_node_type(node),
            ts_node_symbol(node));
  } else if (ts_node_symbol(node) == sym_identifier &&
             ts_node_is_comptimetype_kw(node, src)) {
    fprintf(stderr, ORANGE(" [%s] (%d) [_ComptimeType]"), ts_node_type(node),
            ts_node_symbol(node));
  } else {
    fprintf(stderr, BOLD("%s") " " GRAY("[%d]"), ts_node_type(node),
            ts_node_symbol(node));
  }

  Slice range = ts_node_range(node, src);
  fprintf(stderr, GRAY(" %.*s"), min_int(range.len, 35), range.start);

  fprintf(stderr, GRAY(" [%p]\n"), node.id);

  uint32_t n = ts_node_child_count(node);
  for (uint32_t i = 0; i < n; i++) {
    debug_tree_node(ts_node_child(node, i), src, depth + 2);
  }
}

void debug_tree(TSTree *tree, const char *src, int depth) {
  if (nob_minimal_log_level > NOB_VERBOSE)
    return;
  TSNode root = ts_tree_root_node(tree);
  debug_tree_node(root, src, depth);
}

bool ts_node_is_comptimetype_kw(TSNode node, const char *src) {
  return (ts_node_symbol(node) == sym_identifier ||
          ts_node_symbol(node) == alias_sym_type_identifier) &&
         strlen("_ComptimeType") == (size_t)ts_node_range(node, src).len &&
         memcmp(ts_node_range(node, src).start, "_ComptimeType",
                ts_node_range(node, src).len) == 0;
}

bool ts_node_is_comptime_kw(TSNode node, const char *src) {
  assert(ts_node_symbol(node) == sym_identifier);
  return strlen("_Comptime") == (size_t)ts_node_range(node, src).len &&
         memcmp(ts_node_range(node, src).start, "_Comptime",
                ts_node_range(node, src).len) == 0;
}
