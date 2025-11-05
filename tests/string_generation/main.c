#include <stdio.h>
#include <string.h>

#include "../../ccomptime.h"
#include "main.c.h"

int main(void) {
  // Generate string literals at compile time
  const char *greeting = _Comptime({
    _ComptimeCtx.Inline.appendf("\"Hello from comptime!\"");
  });

  // Generate char array with computed content
  char buffer[] = _Comptime({
    char temp[64];
    snprintf(temp, sizeof(temp), "\"%s\"", "Generated at compile time");
    _ComptimeCtx.Inline.appendf("%s", temp);
  });

  // Verify the strings
  printf("GREETING=%s\n", greeting);
  printf("BUFFER=%s\n", buffer);

  int result = (strcmp(greeting, "Hello from comptime!") == 0 &&
                strcmp(buffer, "Generated at compile time") == 0) ? 0 : 1;

  return result;
}
