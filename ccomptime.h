#include <stdlib.h>
#ifndef _COMPTIME_RUNTIME_H
#define _COMPTIME_RUNTIME_H

#ifndef _Comptime
#define _Comptime(...) /* comptime block has not been computed yet */
#endif

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
typedef struct _Comptime_NextNode_Vtable _Comptime_NextNode_Vtable;

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

struct _Comptime_NextNode_Vtable {
  void (*get_type)(_Comptime_Type *out);
  void (*get_function)(_Comptime_Function *out);
  int _stmt_index;
};

typedef struct {
  _Comptime_Buffer_Vtable Inline;
  _Comptime_Buffer_Vtable TopLevel;
  _Comptime_NextNode_Vtable NextNode;
  int _StatementIndex;
  int _PlaceholderIndex;
} _ComptimeCtx;
#endif
