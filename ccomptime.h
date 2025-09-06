#ifndef CCOMPTIME_H
#define CCOMPTIME_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define _CONCAT_(x, y) x##y
#define CONCAT(x, y) _CONCAT_(x, y)
#define VAR_LINE(x) const char *CONCAT(CCT_STMT_$, __LINE__) = x

typedef int (*CCT_write_in_place_fn)(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));

#define FN_LINE                                                                \
  void CONCAT(CCT_STMT_$, __COUNTER__)(CCT_write_in_place_fn $$,               \
                                       CCT_write_in_place_fn $$_top_level,     \
                                       CCT_write_in_place_fn $$_bottom_level)

#define comptime FN_LINE

#define comptime_inline(code)                                                  \
  /* CCT_INLINE_STMT */ while (__COUNTER__ + 1) {                              \
    extern CCT_write_in_place_fn $$;                                           \
    extern CCT_write_in_place_fn $$_top_level;                                 \
    code                                                                       \
  }

#endif
