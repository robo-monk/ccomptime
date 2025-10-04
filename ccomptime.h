#include <stdlib.h>
#ifndef _COMPTIME_RUNTIME_H
#define _COMPTIME_RUNTIME_H
typedef struct {
  char *items;
  size_t count;
  size_t capacity;
} _Comptime__String_Builder;

typedef struct {
  _Comptime__String_Builder *_sb;
  void (*appendf)(const char *fmt, ...);
} _Comptime_Buffer_Vtable;

typedef struct {
  _Comptime_Buffer_Vtable Inline;
  _Comptime_Buffer_Vtable TopLevel;
  int _StatementIndex;
} _ComptimeCtx;
#endif
