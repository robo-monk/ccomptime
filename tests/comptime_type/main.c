#include <stdio.h>

#include "../../ccomptime.h"
#include "./main.c.h"

typedef _ComptimeType({
  _ComptimeCtx.Inline.appendf("struct { int value; }");
}) Generated;

int main(void) {
  _Comptime({ _ComptimeCtx.TopLevel.appendf("static int foo = 42;"); });
  Generated g = {.value = 7};
  printf("VALUE=%d\n", g.value);
  return g.value == 7 ? 0 : 1;
}
