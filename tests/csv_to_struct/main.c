#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../../ccomptime.h"
#include "main.c.h"

int main(void) {
  _Comptime({
    FILE *f = fopen("tests/csv_to_struct/users.csv", "r");
    if (!f) {
      fprintf(stderr, "Failed to open users.csv\n");
      return;
    }

    // Skip header line
    char line[256];
    fgets(line, sizeof(line), f);

    // Generate struct array
    _ComptimeCtx.TopLevel.appendf("typedef struct { int id; const char *name; int age; } User;\n");
    _ComptimeCtx.TopLevel.appendf("static const User users[] = {\n");

    int count = 0;
    while (fgets(line, sizeof(line), f)) {
      int id, age;
      char name[64];

      // Simple CSV parsing (assumes no commas in fields)
      if (sscanf(line, "%d,%63[^,],%d", &id, name, &age) == 3) {
        _ComptimeCtx.TopLevel.appendf("  {%d, \"%s\", %d},\n", id, name, age);
        count++;
      }
    }

    _ComptimeCtx.TopLevel.appendf("};\n");
    _ComptimeCtx.TopLevel.appendf("static const int users_count = %d;\n", count);

    fclose(f);
  });
  printf("Loaded %d users from CSV\n", users_count);

  for (int i = 0; i < users_count; i++) {
    printf("User %d: id=%d name=%s age=%d\n",
           i, users[i].id, users[i].name, users[i].age);
  }

  // Verify data
  if (users_count == 3 &&
      strcmp(users[0].name, "Alice") == 0 && users[0].age == 25 &&
      strcmp(users[1].name, "Bob") == 0 && users[1].age == 30 &&
      strcmp(users[2].name, "Charlie") == 0 && users[2].age == 35) {
    printf("CSV_PARSE_VALID=1\n");
    return 0;
  }

  printf("CSV_PARSE_VALID=0\n");
  return 1;
}
