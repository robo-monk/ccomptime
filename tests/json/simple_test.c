#include <stdio.h>
#include "../../ccomptime.h"

#define _Test _Comptime({ printf("Test comptime block\n"); })

typedef _Test struct {
  char *name;
  int age;
} User;

int main() {
  printf("Hello\n");
  return 0;
}
