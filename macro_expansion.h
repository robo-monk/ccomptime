#ifndef CCOMPTIME_MACRO_EXPANSION_H
#define CCOMPTIME_MACRO_EXPANSION_H

#include "comptime_common.h"

TSTree *cct_expand_macros(TSParser *parser, TSTree *tree,
                          MacroDefinitionHashMap *macros, const char *tree_src,
                          size_t tree_len, String_Builder *out_source);

#endif // CCOMPTIME_MACRO_EXPANSION_H
