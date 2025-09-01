// ccomptime.h — markers for compile-time execution (survive -E -P -CC)
#ifndef CCOMPTIME_H
#define CCOMPTIME_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define _CONCAT_(x, y) x##y
#define CONCAT(x, y) _CONCAT_(x, y)
#define VAR_LINE(x) const char *CONCAT(CCT_STMT_$, __LINE__) = x

// Context compiled into the helper runner only (stripped from final TU)
#define CCT_CTX(code) /*CCT_CTX_BEGIN*/ code /*CCT_CTX_END*/

#define CCT_RUNI(expr) (/*CCT_RUNI_BEGIN*/ (expr) /*CCT_RUNI_END*/)

// Build-time string expression: same idea as CCT_RUNI.
#define CCT_RUNS(expr) (/*CCT_RUNS_BEGIN*/ (expr) /*CCT_RUNS_END*/)

// Works at file scope and inside blocks; requires a trailing semicolon.
#define CCT_DO(stmt) /*CCT_DO_BEGIN*/ VAR_LINE(#stmt) /*CCT_DO_END*/

#define CCT_DEFINE(macro) /*CCT_DEFINE_BEGIN*/                                 \
  VAR_LINE(#macro)        /*CCT_DEFINE_END*/

#define CCT_ON_EXIT(block)                                                     \
  CCT_DEFINE(ON_EXIT _on_exit);                                                \
  void _on_exit() { block }

#define CCT_PUSH_INLINE_CODE(string)

#define $comptime_ctx CCT_CTX
#define $comptime CCT_DO
#define $comptime_int CCT_RUNI
#define $comptime_str CCT_RUNS

#endif /* CCOMPTIME_H */
