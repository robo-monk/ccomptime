#include "ccomptime.h"
#include "test2.c.h"
#define NOB_IMPLEMENTATION
#include "nob.h"

#define HASHMAP_IMPLEMENTATION
#include "deps/hashmap/hashmap.h"

HashMap _func_defs = {0};
void _comptime_polymorph_type(_ComptimeCtx _ComptimeCtx) {
  _ComptimeCtx.Inline.appendf("int");
}

_ComptimeType(_comptime_polymorph_type(_ComptimeCtx)) test() { return 1; }

void _polymorphic_result(_ComptimeCtx _ComptimeCtx, const char *t,
                         const char *e) {
  const char *struct_sign =
      nob_temp_sprintf("typedef struct { union {%s value; %s error;} as; bool"
                       "is_ok; } Result_%s_%s;",
                       t, e, t, e);

  if (!hashmap_get(&_func_defs, (char *)struct_sign)) {
    hashmap_put(&_func_defs, (char *)struct_sign, (void *)1);
    printf("\n ~~ Generating struct sign %s\n", struct_sign);
    fprintf(_Comptime_FP, "\n%s\n", struct_sign);
  }

  _ComptimeCtx.Inline.appendf("Result_%s_%s", t, e);
}

// #define Result(T, E) _ComptimeType(_polymorphic_result(_ComptimeCtx, #T, #E))
// #define ResultIntInt() Result(int, int)

// _ComptimeType(_comptime_polymorph_type(_ComptimeCtx)) test5() {
_ComptimeType(_polymorphic_result(_ComptimeCtx, "char*", "char*")) test5() {
  if (true) {
    return "hi";
  }
  // return {.is_ok = true, .value = 5};
}
