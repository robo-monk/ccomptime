#include "ccomptime.h"
#include "test2.c.h"
#define NOB_IMPLEMENTATION
#include "nob.h"
#include "test_lib.h"

#define HASHMAP_IMPLEMENTATION
#include "deps/hashmap/hashmap.h"

HashMap _func_defs = {0};
// void _comptime_polymorph_type(_ComptimeCtx _ComptimeCtx) {
//   _ComptimeCtx.Inline.appendf("int");
// }

// _ComptimeType(_comptime_polymorph_type(_ComptimeCtx)) test() { return 1; }

void _polymorphic_result(_ComptimeCtx _ComptimeCtx, const char *t) {
  const char *e = "char*";
  const uint16_t mangle_hash = fnv_hash((char *)t, (int)strlen(t));
  printf("%s => got mangled hash :: %d\n", t, mangle_hash);
  const char *struct_sign =
      nob_temp_sprintf("typedef struct { union {%s value; %s error;} as; int "
                       "is_ok; } Result_%d;",
                       t, e, mangle_hash);

  if (!hashmap_get(&_func_defs, (char *)struct_sign)) {
    hashmap_put(&_func_defs, (char *)struct_sign, (void *)1);
    printf("=> Generating struct sign %s\n", struct_sign);
    // fprintf(_Comptime_FP, "\n%s\n", struct_sign);
    _ComptimeCtx.TopLevel.appendf("\n%s\n", struct_sign);
  }

  _ComptimeCtx.Inline.appendf("Result_%d", mangle_hash);
}

// #define String char *
// #define Result(T, E) _ComptimeType(_polymorphic_result(_ComptimeCtx, #T, #E))
// #define Result(T, E) _ComptimeType(_polymorphic_result(_ComptimeCtx, T))
// #define ResultIntInt() Result(int, int)

// _ComptimeType(_comptime_polymorph_type(_ComptimeCtx)) test5() {
// Result(int, int) test5() {
// _Comptime(_polymorphic_result(_ComptimeCtx, "char*")) test5() {

void test930(_ComptimeCtx _ComptimeCtx, int i) {}

// #define xx _polymorphic_result(_ComptimeCtx, "int")

// _ComptimeType(xx) test5() {
//   if (true) {
//   }
// }

#define Result(t) _ComptimeType(_polymorphic_result(_ComptimeCtx, t))

Result("int") test5() { return (Result("int")){.as.value = 5, .is_ok = true}; }

Result("char") test2() {
  if (true) {
    return (Result("char")){.is_ok = false};
  }
  return (Result("char")){.as.value = 'a', .is_ok = true};
}

Result("char*") test3() {
  if (true) {
    return (Result("char*")){.is_ok = false};
  }
  return (Result("char*")){.as.value = "ok", .is_ok = true};
}

// #define _                                                                      \
//   _Comptime(_ComptimeCtx.Inline.appendf("%s", _ComptimeCtx.ExpectedType))

// Result("char") test3() {
//   if (true) {
//     // TODO: use the inferd value here
//     return (_){.is_ok = false};
//   }

//   let yes = return (xx){.as.value = 'a', .is_ok = true};
// }

int main() {
  int x = 10 + _Comptime(_ComptimeCtx.Inline.appendf("5")) + get_42();
  printf("hello! %d\n", x);
  return x;
}

// STRAT 1: replace all _ComptimeType(...) with _COMPTIME_TYPE so tree-sitter
// doesnt get confused (reverse macros)
// k STRAT 2: remove ALL instances of
//
// _ComptimeType(...) from the included user file so the runner compiles
// properly;
