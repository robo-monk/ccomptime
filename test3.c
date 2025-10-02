#include <stdio.h>
#define HASHMAP_IMPLEMENTATION
#include "ccomptime.h"
#include "deps/hashmap/hashmap.h"
#include "test3.c.h"

int a = _Comptime(_ComptimeCtx.Inline.appendf("42"));
// b = a;

void _comptime_polymorph_type(_ComptimeCtx _ComptimeCtx) {
  _ComptimeCtx.Inline.appendf("int");
}

_ComptimeType(_comptime_polymorph_type(_ComptimeCtx)) test() { return 1; }

int main2() {
  int x = 10 + a + test();
  printf("hello!\n");
  return x;
}
