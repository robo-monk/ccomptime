#include <stdio.h>

#include "../../ccomptime.h"

typedef _ComptimeType({
  _ComptimeCtx.Inline.appendf("struct { int value; }");
}) Generated;

int main(void) {
  Generated g = {.value = 7};
  printf("VALUE=%d\n", g.value);
  return g.value == 7 ? 0 : 1;
}
