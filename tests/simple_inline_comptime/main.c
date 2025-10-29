#include <stdio.h>

#include "../../ccomptime.h"
#include "main.c.h"

int main(void) {
  int value = _Comptime(_ComptimeCtx.Inline.appendf("21 + 21"));
  printf("COMPTIME_VALUE=%d\n", value);
  return value == 42 ? 0 : 1;
}
