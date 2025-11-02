#include <stdio.h>

#include "../../ccomptime.h"
#include "main.c.h"

#define MAKE_PAIR(name, snippet)                                               \
  int name##_first = _Comptime(snippet);                                       \
  int name##_second = _Comptime(snippet);

int main(void) {
  MAKE_PAIR(val, _ComptimeCtx.Inline.appendf("%d", 3));
  printf("SUM=%d\n", val_first + val_second);
  return (val_first + val_second) == 3 + 3 ? 0 : 1;
}
