#include "../ccomptime.h"
#include <stdio.h>

int factorial(int n) {
  if (n < 2) {
    return 1;
  }
  return n * factorial(n - 1);
}

// comptime {
//   printf("\n\nHello from the comptime! Enter a number to bake in the
//   factorial "
//          "calculation: ");

//   int number;
//   scanf("%d", &number);
//   $$("static const int result = %d", number);
// }

// int main() { printf("Runtime says: %d", result); }
