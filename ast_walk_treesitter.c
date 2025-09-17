#define NOB_IMPLEMENTATION
#include "nob.h"
#include "tree_sitter_c_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// From the C grammar package:
extern const TSLanguage *tree_sitter_c(void);
typedef struct {
  String_Builder out_h;
  int comptime_count;
} WalkContext;

typedef struct {
  bool is_inside_function_body;
  bool is_assigning_to_var;
  bool is_within_preproc_def;
  bool is_within_compound;
} VisitFlags;

static void walk(WalkContext *const ctx, VisitFlags visit_flags, TSNode node,
                 const char *src, unsigned depth) {
  for (unsigned i = 0; i < depth; i++)
    putchar(' ');
  printf("- [%s] (%d)", ts_node_type(node), ts_node_symbol(node));

  TSSymbol sym = ts_node_symbol(node);
  switch (sym) {
  case 1: // identifier
    break;
  case 196: // function_definition
    visit_flags.is_inside_function_body = true;
    break;
  case 240: // init_declarator
    visit_flags.is_assigning_to_var = true;
    break;
  case 165: // preproc_def
    visit_flags.is_within_preproc_def = true;
    break;
    // return;
  case 241: // compound_statement
    visit_flags.is_within_compound = true;
    break;
  }

  // switch (ts_node_symbol(node)) {
  // case
  // }
  if (visit_flags.is_assigning_to_var) {
    printf(" [VAR] ");
  }

  if (visit_flags.is_inside_function_body) {
    printf(" [FN]");
  }

  if (visit_flags.is_within_compound) {
    printf("  [{}]");
  }

  if (visit_flags.is_within_preproc_def) {
    printf("  [#define]");
  }

  // Print token text for identifiers (best-effort)
  if (sym == 1 /* identifier */) {
    uint32_t start = ts_node_start_byte(node);
    uint32_t end = ts_node_end_byte(node);
    printf("  : %.*s", (int)(end - start), src + start);
    if (memmem(src + start, end - start, "comptime", strlen("comptime"))) {
      printf("  [contains 'comptime'] (%d)", ctx->comptime_count);
      if (visit_flags.is_within_preproc_def) {
        printf("\n  [SKIP - within #define]\n");
        // TODO: now we should preprocess this identifier and replace future
        printf("  [FOUND COMPTIME POWERED MACRO]");
        return;
      }
      if (visit_flags.is_assigning_to_var) {
        nob_sb_appendf(&ctx->out_h, "#define _cct_%d 1 ? 0 :\n",
                       ctx->comptime_count);
      } else if (visit_flags.is_inside_function_body) {
        nob_sb_appendf(&ctx->out_h, "#define _cct_%d while (0)\n",
                       ctx->comptime_count);
      } else {
        nob_sb_appendf(&ctx->out_h, "#define _cct_%d void _cct_fn_%d(void)\n",
                       ctx->comptime_count, ctx->comptime_count);
      }
      ctx->comptime_count++;
    }
  }
  putchar('\n');

  uint32_t n = ts_node_child_count(node);
  for (uint32_t i = 0; i < n; i++) {
    walk(ctx, visit_flags, ts_node_child(node, i), src, depth + 2);
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
  WalkContext walk_ctx = (WalkContext){0};
  sb_appendf(&walk_ctx.out_h,
             "/*// @generated - ccomptime™ v0.0.1 - %lu \\*/\n",
             // "/* ---- */// the end. ///* ---- */"
             // "/*      */// @generated ///*       */\n"
             // "/*       - ccomptime™ v0.0.1       */\n"
             // "/*                1758074492       */\n"
             // "/*                %lu       */\n",
             time(NULL));
  sb_appendf(&walk_ctx.out_h, "#define _CONCAT_(x, y) x##y\n"
                              "#define CONCAT(x, y) _CONCAT_(x, y)\n"
                              "#define comptime CONCAT(_cct_, __COUNTER__)\n");

  walk(&walk_ctx, (VisitFlags){0}, root, src.items, 0);
  sb_appendf(&walk_ctx.out_h, "/* ---- */// the end. ///* ---- */\n");
  printf("comptime found : %d", walk_ctx.comptime_count);

  printf("\n\nGenerated header:\n%s\n", walk_ctx.out_h.items);

  ts_tree_delete(tree);
  ts_parser_delete(parser);
  sb_free(src);
  sb_free(walk_ctx.out_h);
  return 0;
}
