#include "ccomptime.h"
#include "test2.c.h"



void _comptime_polymorph_type(_ComptimeCtx _ComptimeCtx) {
  _ComptimeCtx.Inline.appendf("int");
}

_ComptimeType(_comptime_polymorph_type(_ComptimeCtx)) test() { return 1; }
// int test() { return 1; }
// _ComptimeType(x) v = 0;
