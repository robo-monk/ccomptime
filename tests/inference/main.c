#include <stdio.h>
#include "../../ccomptime.h"
#define var                                                                    \
  _ComptimeType(_ComptimeCtx.Inline.appendf("%s", _ComptimeCtx.InferType()))

#define let                                                                    \
  _ComptimeType(                                                               \
      _ComptimeCtx.Inline.appendf("const %s", _ComptimeCtx.InferType()))

int main() {
  var hi = "hello there";
  let yes = 10;
  printf("hi = %s\n", hi);
  printf("yes = %d\n", yes);
  return 0;
}
