#ifndef CCOMPTIME_TREE_PASSES_H
#define CCOMPTIME_TREE_PASSES_H

#include "comptime_common.h"

TSTree *cct_correct_comptimetype_nodes(TSParser *parser, TSTree *tree,
                                       const char *src, size_t len,
                                       WalkContext *ctx,
                                       String_Builder *out_source);

void cct_collect_comptime_statements(WalkContext *ctx, TSTree *tree,
                                     const char *src);

#endif // CCOMPTIME_TREE_PASSES_H
