#include <stdio.h>
#include <stdlib.h>

#include "../../ccomptime.h"
#include "main.c.h"

int main(void) {
  // Read file at compile time and embed the value
  int magic_number = _Comptime({
    FILE *f = fopen("tests/comptime_file_io/data.txt", "r");
    if (!f) {
      fprintf(stderr, "Failed to open data.txt at comptime\n");
      exit(1);
    }
    int value = 0;
    fscanf(f, "%d", &value);
    fclose(f);

    printf("Read value %d from file at compile time\n", value);
    _ComptimeCtx.Inline.appendf("%d", value);
  });

  printf("MAGIC_NUMBER=%d\n", magic_number);
  return magic_number == 42 ? 0 : 1;
}
