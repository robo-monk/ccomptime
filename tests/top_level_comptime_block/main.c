#include <stdio.h>

#include "../../ccomptime.h"
#include "main.c.h"

int main(void) {
  _Comptime({ printf("This gets executed during comptime!"); });
  return 1;
}
