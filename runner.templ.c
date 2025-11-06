// #define _INPUT_PROGRAM_PATH "test2.c"
// #define _OUTPUT_HEADERS_PATH ""
// #define _INPUT_COMPTIME_DEFS_PATH "test2.cc-runner-defs.c"
// #define _INPUT_COMPTIME_MAIN_PATH "test2.cc-runner-main.c"

#if defined(_INPUT_PROGRAM_PATH) && defined(_OUTPUT_HEADERS_PATH) &&           \
    defined(_INPUT_COMPTIME_DEFS_PATH) && defined(_INPUT_COMPTIME_MAIN_PATH)

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *_Comptime_FP;

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
  int _PlaceholderIndex;
} _ComptimeCtx;
#endif

// this is all nobs shit but we took it
#define _Comptime__da_reserve(da, expected_capacity)                           \
  do {                                                                         \
    if ((expected_capacity) > (da)->capacity) {                                \
      if ((da)->capacity == 0) {                                               \
        (da)->capacity = 256;                                                  \
      }                                                                        \
      while ((expected_capacity) > (da)->capacity) {                           \
        (da)->capacity *= 2;                                                   \
      };                                                                       \
      (da)->items =                                                            \
          realloc((da)->items, (da)->capacity * sizeof(*(da)->items));         \
      assert((da)->items != NULL && "Buy more RAM lol");                       \
    }                                                                          \
  } while (0)

int _Comptime__sb_appendf(_Comptime__String_Builder *sb, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

#define _Comptime__da_append(da, item)                                         \
  do {                                                                         \
    _Comptime__da_reserve((da), (da)->count + 1);                              \
    (da)->items[(da)->count++] = (item);                                       \
      } while (0

int _Comptime__sb_appendf(_Comptime__String_Builder *sb, const char *fmt, ...) {
  va_list args;

  va_start(args, fmt);
  int n = vsnprintf(NULL, 0, fmt, args);
  va_end(args);

  _Comptime__da_reserve(sb, sb->count + n + 1);
  char *dest = sb->items + sb->count;
  va_start(args, fmt);
  vsnprintf(dest, n + 1, fmt, args);
  va_end(args);

  sb->count += n;
  return n;
}

#define __Define_Comptime_Buffer(suffix)                                       \
  _Comptime__String_Builder _Comptime_Buffer_##suffix = {0};                   \
  void _Comptime_Buffer_appendf_##suffix(const char *fmt, ...) {               \
    _Comptime__String_Builder *sb = &_Comptime_Buffer_##suffix;                \
    va_list args;                                                              \
    va_start(args, fmt);                                                       \
    int n = vsnprintf(NULL, 0, fmt, args);                                     \
    va_end(args);                                                              \
    _Comptime__da_reserve(sb, sb->count + n + 1);                              \
    char *dest = sb->items + sb->count;                                        \
    va_start(args, fmt);                                                       \
    vsnprintf(dest, n + 1, fmt, args);                                         \
    va_end(args);                                                              \
    sb->count += n;                                                            \
  };

__Define_Comptime_Buffer(TopLevel);

#define __Comptime_Statement_Fn(index, ...)                                    \
  __Define_Comptime_Buffer(Inline_##index);                                    \
  void _Comptime_exec##index(_ComptimeCtx _ComptimeCtx) { __VA_ARGS__; }

// __Comptime_Statement_Fn(0, int a = 1)

// this will get called for every comptime block within the main
#define __Comptime_Register(index, placeholder_index)                          \
  __Comptime_wrap_exec(                                                        \
      _Comptime_exec##index,                                                   \
      (_ComptimeCtx){._StatementIndex = index,                                 \
                     ._PlaceholderIndex = placeholder_index,                   \
                     .TopLevel =                                               \
                         (_Comptime_Buffer_Vtable){                            \
                             ._sb = &_Comptime_Buffer_TopLevel,                \
                             .appendf = _Comptime_Buffer_appendf_TopLevel},    \
                     .Inline = (_Comptime_Buffer_Vtable){                      \
                         ._sb = &_Comptime_Buffer_Inline_##index,              \
                         .appendf = _Comptime_Buffer_appendf_Inline_##index}})

#define __Comptime_Register_Main_Exec(index) __Comptime_Register(index, -1)

#define __Comptime_Register_Type_Exec(index, placeholder_index)                \
  __Comptime_Register(index, placeholder_index)

void __Comptime_wrap_exec(void (*fn)(_ComptimeCtx), _ComptimeCtx ctx) {
  fn(ctx);
  fprintf(_Comptime_FP, "#define _COMPTIME_X%d(...) %.*s\n",
          ctx._StatementIndex, (int)ctx.Inline._sb->count,
          ctx.Inline._sb->items);
  if (ctx._PlaceholderIndex >= 0) {
    fprintf(_Comptime_FP, "#define _COMPTIMETYPE_%d %.*s\n",
            ctx._PlaceholderIndex, (int)ctx.Inline._sb->count,
            ctx.Inline._sb->items);
  }
}

#define main _User_main // overwrite the entrypoint of the user program
#include _INPUT_PROGRAM_PATH
#undef main

#undef _ComptimeType
#define _ComptimeType(x) x

#include _INPUT_COMPTIME_DEFS_PATH

int main(void) {
  _Comptime_FP = fopen(_OUTPUT_HEADERS_PATH, "a");
  if (!_Comptime_FP) {
    fprintf(stderr, "Failed to open %s for writing\n", _OUTPUT_HEADERS_PATH);
    exit(EXIT_FAILURE);
  }

  fprintf(_Comptime_FP, "\n#undef _COMPTIME_X\n#define _COMPTIME_X(n,...) "
                        "CONCAT(_COMPTIME_X,n)(__VA_ARGS__)\n");

#include _INPUT_COMPTIME_MAIN_PATH

  if (_Comptime_Buffer_TopLevel.count > 0) {
    fprintf(_Comptime_FP, "\n/* top level definitions */\n%.*s\n",
            (int)_Comptime_Buffer_TopLevel.count,
            _Comptime_Buffer_TopLevel.items);
  }
  fflush(_Comptime_FP);
  fclose(_Comptime_FP);
}

#else
#error                                                                         \
    "please define _INPUT_PROGRAM_PATH and _OUTPUT_HEADERS_PATH and _INPUT_COMPTIME_DEFS_PATH and _INPUT_COMPTIME_MAIN_PATH "
#endif
