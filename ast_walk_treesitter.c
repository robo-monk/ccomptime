#define NOB_IMPLEMENTATION
#include "nob.h"
#include "tree_sitter_c_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// From the C grammar package:
extern const TSLanguage *tree_sitter_c(void);

typedef struct {
  bool is_inside_function_body;
  bool is_assigning_to_var;
  bool is_within_compound;
} ASTWalkerContext;

static void walk(TSNode node, const char *src, unsigned depth,
                 ASTWalkerContext ctx) {
  for (unsigned i = 0; i < depth; i++)
    putchar(' ');
  printf("- [%s] (%d)", ts_node_type(node), ts_node_symbol(node));

  switch (ts_node_symbol(node)) {
  case 1: // identifier
    break;
  case 196: // function_definition
    ctx.is_inside_function_body = true;
    break;
  case 240: // init_declarator
    ctx.is_assigning_to_var = true;
    break;
  case 241: // compound_statement
    ctx.is_within_compound = true;
    break;
  }

  // switch (ts_node_symbol(node)) {
  // case
  // }
  if (ctx.is_assigning_to_var) {
    printf("  [is assigning to var]");
  }

  if (ctx.is_inside_function_body) {
    printf("  [inside function body]");
  }

  if (ctx.is_within_compound) {
    printf("  [within compound statement]");
  }

  // Print token text for identifiers (best-effort)
  const char *type = ts_node_type(node);
  if (strcmp(type, "identifier") == 0) {
    uint32_t start = ts_node_start_byte(node);
    uint32_t end = ts_node_end_byte(node);
    printf("  : %.*s", (int)(end - start), src + start);
    if (memmem(src + start, end - start, "comptime", strlen("comptime"))) {
      printf("  [contains 'comptime']");
    }
  }
  putchar('\n');

  uint32_t n = ts_node_child_count(node);
  for (uint32_t i = 0; i < n; i++) {
    walk(ts_node_child(node, i), src, depth + 2, ctx);
  }
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s <file.c>\n", argv[0]);
    return 1;
  }

  String_Builder src = {0};
  read_entire_file(argv[1], &src);
  nob_sb_append_null(&src);
  // char *src = read_file(argv[1], &len);
  if (!src.items) {
    fprintf(stderr, "failed to read %s\n", argv[1]);
    return 2;
  }

  TSParser *parser = ts_parser_new();
  ts_parser_set_language(parser, tree_sitter_c());
  TSTree *tree = ts_parser_parse_string(parser, NULL, src.items, src.count);

  TSNode root = ts_tree_root_node(tree);
  walk(root, src.items, 0, (ASTWalkerContext){0});

  ts_tree_delete(tree);
  ts_parser_delete(parser);
  sb_free(src);
  return 0;
}
