#include <stdio.h>
#define HASHMAP_IMPLEMENTATION
#include "deps/hashmap/hashmap.h"

_Comptime(printf("hi"));

void test() { _Comptime(printf("inside test")); }

HashMap _func_defs = {0};
void _comptime_polymorph_type(_ComptimeCtx _ComptimeCtx) {
  _ComptimeCtx.Inline.appendf("int");
}

_ComptimeType(_comptime_polymorph_type(_ComptimeCtx)) test() { return 1; }
