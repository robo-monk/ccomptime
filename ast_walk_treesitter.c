#include "tree_sitter_c_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// From the C grammar package:
extern const TSLanguage *tree_sitter_c(void);

static char *read_file(const char *path, size_t *out_len) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *buf = (char *)malloc(n + 1);
  if (!buf) {
    fclose(f);
    return NULL;
  }
  fread(buf, 1, n, f);
  fclose(f);
  buf[n] = '\0';
  if (out_len)
    *out_len = (size_t)n;
  return buf;
}

static void walk(TSNode node, const char *src, unsigned depth) {
  for (unsigned i = 0; i < depth; i++)
    putchar(' ');
  printf("- %s", ts_node_type(node));

  // Print token text for identifiers (best-effort)
  const char *type = ts_node_type(node);
  if (strcmp(type, "identifier") == 0) {
    uint32_t start = ts_node_start_byte(node);
    uint32_t end = ts_node_end_byte(node);
    printf("  : %.*s", (int)(end - start), src + start);
  }
  putchar('\n');

  uint32_t n = ts_node_child_count(node);
  for (uint32_t i = 0; i < n; i++) {
    walk(ts_node_child(node, i), src, depth + 2);
  }
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s <file.c>\n", argv[0]);
    return 1;
  }

  size_t len = 0;
  char *src = read_file(argv[1], &len);
  if (!src) {
    fprintf(stderr, "failed to read %s\n", argv[1]);
    return 2;
  }

  TSParser *parser = ts_parser_new();
  ts_parser_set_language(parser, tree_sitter_c());
  TSTree *tree = ts_parser_parse_string(parser, NULL, src, (uint32_t)len);

  TSNode root = ts_tree_root_node(tree);
  walk(root, src, 0);

  ts_tree_delete(tree);
  ts_parser_delete(parser);
  free(src);
  return 0;
}
