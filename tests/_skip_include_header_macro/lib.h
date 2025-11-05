#pragma once

static inline int get_header_value(void) {
  return _Comptime(_ComptimeCtx.Inline.appendf("5"));
}
