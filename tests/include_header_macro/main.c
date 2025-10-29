#include <stdio.h>

#include "../../ccomptime.h"
#include "main.c.h"
#include "lib.h"

int main(void) {
  int v = get_header_value();
  printf("ANSWER=%d\n", v);
  return v == 5 ? 0 : 1;
}
