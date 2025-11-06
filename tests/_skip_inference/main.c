#include "../../ccomptime.h"
#define var                                                                    \
  _ComptimeType(_ComptimeCtx.Inline.appendf("%s", _ComptimeCtx.InferType()))

#define let                                                                    \
  _ComptimeType(                                                               \
      _ComptimeCtx.Inline.appendf("const %s", _ComptimeCtx.InferType()))

#define fn var
#define proc let

fn main() {
  var hi = "hello there";
  let yes = 10;
  return 0;
}
