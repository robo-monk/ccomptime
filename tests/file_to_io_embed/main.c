#include <stdio.h>
#include <string.h>

#include "../../ccomptime.h"
#include "main.c.h"

int main(void) {

  static const char *config_data = _Comptime({
    FILE *f = fopen("tests/file_to_io_embed/config.txt", "r");
    if (!f) {
      fprintf(stderr, "Failed to open config.txt\n");
      _ComptimeCtx.Inline.appendf(
          "\"\"; static_assert(0 && 'Could not open file');");
      return;
    }

    char line[256];
    _ComptimeCtx.Inline.appendf("\"");
    while (fgets(line, sizeof(line), f)) {
      // Escape newlines for string literal
      for (char *p = line; *p; p++) {
        if (*p == '\n') {
          _ComptimeCtx.Inline.appendf("\\n");
        } else if (*p == '"') {
          _ComptimeCtx.Inline.appendf("\\\"");
        } else if (*p == '\\') {
          _ComptimeCtx.Inline.appendf("\\\\");
        } else {
          _ComptimeCtx.Inline.appendf("%c", *p);
        }
      }
    }
    _ComptimeCtx.Inline.appendf("\"");
    // _ComptimeCtxInline.appendf("\";\n");
    fclose(f);
  });

  printf("Embedded config:\n%s", config_data);

  // Verify the embedded data contains expected content
  if (strstr(config_data, "APP_NAME=MyApplication") &&
      strstr(config_data, "VERSION=1.2.3") &&
      strstr(config_data, "AUTHOR=CompileTime")) {
    printf("CONFIG_VALID=1\n");
    return 0;
  }

  printf("CONFIG_VALID=0\n");
  return 1;
}
