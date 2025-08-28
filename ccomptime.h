// ccomptime.h — markers for compile-time execution (survive -E -P -CC)
#ifndef CCOMPTIME_H
#define CCOMPTIME_H

#define _CONCAT_(x, y) x##y
#define CONCAT(x, y) _CONCAT_(x, y)
#define VAR_LINE(x) const char *CONCAT(__CCT_DO, __LINE__) = x

// Context compiled into the helper runner only (stripped from final TU)
#define CCT_CTX(code) /*CCT_CTX_BEGIN*/ code /*CCT_CTX_END*/

#define CCT_RUNI(expr) (/*CCT_RUNI_BEGIN*/ (expr) /*CCT_RUNI_END*/)

// Build-time string expression: same idea as CCT_RUNI.
#define CCT_RUNS(expr) (/*CCT_RUNS_BEGIN*/ (expr) /*CCT_RUNS_END*/)

// Works at file scope and inside blocks; requires a trailing semicolon.
#define CCT_DO(stmt) /*CCT_DO_BEGIN*/ VAR_LINE(#stmt) /*CCT_DO_END*/

#define CCT_DEFINE(macro) /*CCT_DEFINE_BEGIN*/                                 \
  VAR_LINE(#macro)        /*CCT_DEFINE_END*/

#endif /* CCOMPTIME_H */
