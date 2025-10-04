#include "ccomptime.h"
#include "test2.c.h"
#define NOB_IMPLEMENTATION
#include "nob.h"

#define HASHMAP_IMPLEMENTATION
#include "deps/hashmap/hashmap.h"

HashMap _func_defs = {0};
// void _comptime_polymorph_type(_ComptimeCtx _ComptimeCtx) {
//   _ComptimeCtx.Inline.appendf("int");
// }

// _ComptimeType(_comptime_polymorph_type(_ComptimeCtx)) test() { return 1; }

void _polymorphic_result(_ComptimeCtx _ComptimeCtx, const char *t) {
  const char *e = "char*";
  const char *struct_sign =
      nob_temp_sprintf("typedef struct { union {%s value; %s error;} as; int "
                       "is_ok; } Result_%s;",
                       t, e, t);

  if (!hashmap_get(&_func_defs, (char *)struct_sign)) {
    hashmap_put(&_func_defs, (char *)struct_sign, (void *)1);
    printf("\n ~~ Generating struct sign %s\n", struct_sign);
    // fprintf(_Comptime_FP, "\n%s\n", struct_sign);
    _ComptimeCtx.TopLevel.appendf("%s", struct_sign);
  }

  _ComptimeCtx.Inline.appendf("Result_%s", t);
}

// #define String char *
// #define Result(T, E) _ComptimeType(_polymorphic_result(_ComptimeCtx, #T, #E))
// #define Result(T, E) _ComptimeType(_polymorphic_result(_ComptimeCtx, T))
// #define ResultIntInt() Result(int, int)

// _ComptimeType(_comptime_polymorph_type(_ComptimeCtx)) test5() {
// Result(int, int) test5() {
// _Comptime(_polymorphic_result(_ComptimeCtx, "char*")) test5() {

void test930(_ComptimeCtx _ComptimeCtx, int i) {}

_ComptimeType(_polymorphic_result(_ComptimeCtx, "int")) test5() {
  if (true) {
  }
}

int main() {
  int x = 10 + _Comptime(_ComptimeCtx.Inline.appendf("5"));
  printf("hello! %d\n", x);
  return x;
}

// STRAT 1: replace all _ComptimeType(...) with _COMPTIME_TYPE so tree-sitter
// doesnt get confused (reverse macros)
// k STRAT 2: remove ALL instances of
//
// _ComptimeType(...) from the included user file so the runner compiles
// properly;
