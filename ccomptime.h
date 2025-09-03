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
// Works at file scope and inside blocks; requires a trailing semicolon.
#define CCT_DO(stmt) /*CCT_DO_BEGIN*/ VAR_LINE(#stmt) /*CCT_DO_END*/

#define $comptime_ctx CCT_CTX
#define $comptime CCT_DO

#define CCT__type_of(x)                                                        \
  _Generic((x), \
    int: int, \
    double: double, \
    char*: const char*, \
    default: void*)

#define CCT__auto_fmt(x)                                                       \
  _Generic((x), int : "%d", double : "%ld", const char * : "%s", default : "?")

#define CCT__ASSIGNMENT_TO_VAR(varname, expr)                                  \
  $comptime({                                                                  \
    CCT__type_of(expr) _result = /*CCT_CTX_BEGIN*/ expr; /*CCT_CTX_END*/       \
    $$(nob_temp_sprintf("%s = " CCT__auto_fmt(_result) ";", name, _result));   \
  });

#define $comptime_var(name, v)                                                 \
  CCT__type_of(v) name = v;                                                    \
  CCT__ASSIGNMENT_TO_VAR(v, v);

// int name = 0;
// $comptime({
//   int _result = /*CCT_CTX_BEGIN*/ int_expr; /*CCT_CTX_END*/
//   $$(nob_temp_sprintf("%s = %%;", name, _result));
// })

#define $COMPTIME_INT(name, int_expr)                                          \
  int name = 0;                                                                \
  $comptime({                                                                  \
    int __result = /*CCT_CTX_BEGIN*/ int_expr; /*CCT_CTX_END*/                 \
    $$(nob_temp_sprintf("int %s = %d;", #name, __result));                     \
  })

#endif /* CCOMPTIME_H */
