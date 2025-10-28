#include <stdio.h>
#define HASHMAP_IMPLEMENTATION
#include "../ccomptime.h"
#include "../deps/hashmap/hashmap.h"
#include "test3.c.h"

int fibonnaci(int n) {
  if (n <= 1)
    return n;
  return n + fibonnaci(n - 1);
}

int a = _Comptime(_ComptimeCtx.Inline.appendf("42"));
int b = _Comptime(_ComptimeCtx.Inline.appendf("%d", fibonnaci(4)));

void _comptime_polymorph_type(_ComptimeCtx _ComptimeCtx) {
  printf("generating polymorph type\n");
  _ComptimeCtx.Inline.appendf("int");
}

_ComptimeType(_comptime_polymorph_type(_ComptimeCtx)) test() { return 1; }

int main() {
  int x = 10 + a + test() + _Comptime(_ComptimeCtx.Inline.appendf("5"));
  printf("hello! %d\n", x);
  return x;
}
