// #define _INPUT_PROGRAM_PATH "test2.c"
// #define _OUTPUT_HEADERS_PATH ""
// #define _INPUT_COMPTIME_DEFS_PATH "test2.cc-runner-defs.c"
// #define _INPUT_COMPTIME_MAIN_PATH "test2.cc-runner-main.c"

#if defined(_INPUT_PROGRAM_PATH) && defined(_OUTPUT_HEADERS_PATH) &&           \
    defined(_INPUT_COMPTIME_DEFS_PATH) && defined(_INPUT_COMPTIME_MAIN_PATH) && \
    defined(_INPUT_COMPTIME_NEXT_NODES_PATH)

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

// Forward declarations for introspection types
typedef struct _Comptime_Type _Comptime_Type;
typedef struct _Comptime_Function _Comptime_Function;

// Introspection type definitions
typedef struct {
  const char *name;
  const char *type;
} _Comptime_Field;

struct _Comptime_Type {
  int success;
  const char *name;
  const char *kind; // "struct", "union", "enum", etc.
  _Comptime_Field *fields;
  size_t field_count;
  const char *source; // Full source text
};

struct _Comptime_Function {
  int success;
  const char *name;
  const char *return_type;
  const char *args_type;
  const char *source;
};

// NextNode vtable methods
void _Comptime_get_type_impl(int stmt_index, _Comptime_Type *out);
void _Comptime_get_function_impl(int stmt_index, _Comptime_Function *out);

// Complete struct definition before using it
typedef struct {
  void (*get_type)(_Comptime_Type *out);
  void (*get_function)(_Comptime_Function *out);
  int _stmt_index;
} _Comptime_NextNode_Vtable;

typedef struct {
  _Comptime_Buffer_Vtable Inline;
  _Comptime_Buffer_Vtable TopLevel;
  _Comptime_NextNode_Vtable NextNode;
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

// Global wrapper context
static _ComptimeCtx *_current_ctx = NULL;

// Global wrapper functions that use the current context
void _Comptime_NextNode_get_type_wrapper(_Comptime_Type *out) {
  if (_current_ctx) {
    _Comptime_get_type_impl(_current_ctx->_StatementIndex, out);
  } else {
    out->success = 0;
  }
}

void _Comptime_NextNode_get_function_wrapper(_Comptime_Function *out) {
  if (_current_ctx) {
    _Comptime_get_function_impl(_current_ctx->_StatementIndex, out);
  } else {
    out->success = 0;
  }
}

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
                         .appendf = _Comptime_Buffer_appendf_Inline_##index},  \
                     .NextNode = (_Comptime_NextNode_Vtable){                  \
                         .get_type = _Comptime_NextNode_get_type_wrapper,      \
                         .get_function = _Comptime_NextNode_get_function_wrapper, \
                         ._stmt_index = index}})

#define __Comptime_Register_Main_Exec(index) __Comptime_Register(index, -1)

#define __Comptime_Register_Type_Exec(index, placeholder_index)                \
  __Comptime_Register(index, placeholder_index)

void __Comptime_wrap_exec(void (*fn)(_ComptimeCtx), _ComptimeCtx ctx) {
  // Set global context for NextNode wrappers
  _current_ctx = &ctx;

  fn(ctx);

  // Clear context after execution
  _current_ctx = NULL;

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

// Include next node data for introspection
#include _INPUT_COMPTIME_NEXT_NODES_PATH

// Helper function to skip whitespace
static const char *skip_whitespace(const char *p) {
  while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
  return p;
}

// Helper to extract identifier
static const char *extract_identifier(const char *p, char *buf, size_t buf_size) {
  p = skip_whitespace(p);
  size_t i = 0;
  while (*p && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                (*p >= '0' && *p <= '9') || *p == '_' || *p == '*') &&
         i < buf_size - 1) {
    buf[i++] = *p++;
  }
  buf[i] = '\0';
  return p;
}

// Simple struct parser
void _Comptime_parse_struct(const char *source, size_t len, _Comptime_Type *out) {
  if (!source || len == 0) {
    out->success = 0;
    return;
  }

  out->source = source;

  // Find "struct" keyword
  const char *p = strstr(source, "struct");
  if (!p) {
    out->success = 0;
    return;
  }

  out->kind = "struct";
  p += 6; // strlen("struct")

  // Skip whitespace and optional name
  p = skip_whitespace(p);
  static char struct_name[256];
  if (*p != '{') {
    // Has a name
    p = extract_identifier(p, struct_name, sizeof(struct_name));
    out->name = struct_name;
  } else {
    out->name = NULL;
  }

  // Find opening brace
  p = skip_whitespace(p);
  if (*p != '{') {
    out->success = 0;
    return;
  }
  p++; // skip '{'

  // Count and parse fields
  static _Comptime_Field fields[32];
  static char field_types[32][256];
  static char field_names[32][256];
  size_t field_count = 0;

  while (*p && *p != '}' && field_count < 32) {
    p = skip_whitespace(p);
    if (*p == '}') break;

    // Extract type (everything before last identifier before semicolon)
    const char *field_start = p;
    const char *last_ident_start = NULL;

    // Find semicolon
    while (*p && *p != ';' && *p != '}') {
      if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '_') {
        last_ident_start = p;
        while (*p && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                      (*p >= '0' && *p <= '9') || *p == '_')) {
          p++;
        }
      } else {
        p++;
      }
    }

    if (!last_ident_start || *p != ';') {
      break; // malformed
    }

    // Extract field name (last identifier)
    const char *name_start = last_ident_start;
    const char *name_end = last_ident_start;
    while (*name_end && ((*name_end >= 'a' && *name_end <= 'z') ||
                         (*name_end >= 'A' && *name_end <= 'Z') ||
                         (*name_end >= '0' && *name_end <= '9') || *name_end == '_')) {
      name_end++;
    }

    size_t name_len = name_end - name_start;
    if (name_len > 0 && name_len < 256) {
      memcpy(field_names[field_count], name_start, name_len);
      field_names[field_count][name_len] = '\0';

      // Extract type (everything from field_start to name_start, trimmed)
      const char *type_end = name_start;
      while (type_end > field_start && (*type_end == ' ' || *type_end == '\t' || *type_end == '*')) {
        type_end--;
      }
      if (*type_end != ' ' && *type_end != '\t') type_end++;

      size_t type_len = type_end - field_start;
      if (type_len > 0 && type_len < 256) {
        memcpy(field_types[field_count], field_start, type_len);
        field_types[field_count][type_len] = '\0';

        fields[field_count].name = field_names[field_count];
        fields[field_count].type = field_types[field_count];
        field_count++;
      }
    }

    p++; // skip semicolon
  }

  out->fields = fields;
  out->field_count = field_count;
  out->success = 1;
}

// Implement introspection functions
void _Comptime_get_type_impl(int stmt_index, _Comptime_Type *out) {
  // Access the next node source dynamically based on stmt_index
  // For now, we'll use a switch statement - could be improved with a table
  const char *source = NULL;
  size_t len = 0;

  switch (stmt_index) {
    case 0:
      if (_Comptime_NextNode_0) {
        source = _Comptime_NextNode_0;
        len = _Comptime_NextNode_0_len;
      }
      break;
    // Add more cases as needed
    default:
      out->success = 0;
      return;
  }

  if (source && len > 0) {
    _Comptime_parse_struct(source, len, out);
  } else {
    out->success = 0;
  }
}

void _Comptime_get_function_impl(int stmt_index, _Comptime_Function *out) {
  // For now, just mark as not successful
  out->success = 0;
  out->name = NULL;
  out->return_type = NULL;
  out->args_type = NULL;
  out->source = NULL;

  // TODO: Implement actual C function parsing
}

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
    "please define _INPUT_PROGRAM_PATH and _OUTPUT_HEADERS_PATH and _INPUT_COMPTIME_DEFS_PATH and _INPUT_COMPTIME_MAIN_PATH and _INPUT_COMPTIME_NEXT_NODES_PATH"
#endif
