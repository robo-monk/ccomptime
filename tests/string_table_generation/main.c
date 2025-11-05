#include <stdio.h>
#include <string.h>

#include "../../ccomptime.h"
#include "main.c.h"

int main(void) {
  _Comptime({
    FILE *f = fopen("tests/string_table_generation/error_codes.txt", "r");
    if (!f) {
      fprintf(stderr, "Failed to open error_codes.txt\n");
      return;
    }

    // Generate enum
    _ComptimeCtx.TopLevel.appendf("typedef enum {\n");

    char line[256];
    int index = 0;
    char names[100][256];
    int count = 0;

    while (fgets(line, sizeof(line), f)) {
      // Remove newline
      line[strcspn(line, "\n")] = 0;
      if (strlen(line) > 0) {
        strcpy(names[count], line);
        _ComptimeCtx.TopLevel.appendf("  %s = %d,\n", line, index++);
        count++;
      }
    }

    _ComptimeCtx.TopLevel.appendf("} ErrorCode;\n\n");

    // Generate string lookup function
    _ComptimeCtx.TopLevel.appendf("static const char* error_code_to_string(ErrorCode code) {\n");
    _ComptimeCtx.TopLevel.appendf("  switch(code) {\n");

    for (int i = 0; i < count; i++) {
      _ComptimeCtx.TopLevel.appendf("    case %s: return \"%s\";\n", names[i], names[i]);
    }

    _ComptimeCtx.TopLevel.appendf("    default: return \"UNKNOWN\";\n");
    _ComptimeCtx.TopLevel.appendf("  }\n");
    _ComptimeCtx.TopLevel.appendf("}\n");

    fclose(f);
  });
  ErrorCode codes[] = {ERR_SUCCESS, ERR_INVALID_INPUT, ERR_TIMEOUT};

  for (int i = 0; i < 3; i++) {
    printf("Code %d: %s\n", codes[i], error_code_to_string(codes[i]));
  }

  // Verify lookup works
  if (strcmp(error_code_to_string(ERR_SUCCESS), "ERR_SUCCESS") == 0 &&
      strcmp(error_code_to_string(ERR_FILE_NOT_FOUND), "ERR_FILE_NOT_FOUND") == 0) {
    printf("STRING_TABLE_VALID=1\n");
    return 0;
  }

  printf("STRING_TABLE_VALID=0\n");
  return 1;
}
