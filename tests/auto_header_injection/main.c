#include <stdio.h>

#include "../../ccomptime.h"

int main(void) {
  int value = _Comptime(_ComptimeCtx.Inline.appendf("7 * 6"));
  printf("COMPTIME_VALUE=%d\n", value);
  return value == 42 ? 0 : 1;
}
