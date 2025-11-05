#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include "../../ccomptime.h"
#include "main.c.h"

int main(void) {
  // Use math functions at compile time
  double pi_approx = _Comptime({
    double pi = 4.0 * atan(1.0);
    char buf[64];
    snprintf(buf, sizeof(buf), "%.5f", pi);
    _ComptimeCtx.Inline.appendf("%s", buf);
    printf("Computed PI approximation: %s\n", buf);
  });

  // Use string functions at compile time
  _Comptime({
    const char *names[] = {"Alice", "Bob", "Charlie"};
    _ComptimeCtx.TopLevel.appendf("enum PersonID {\n");
    for (int i = 0; i < 3; i++) {
      char upper_name[64] = {0};
      for (int j = 0; names[i][j]; j++) {
        upper_name[j] = (names[i][j] >= 'a' && names[i][j] <= 'z')
                        ? names[i][j] - 32
                        : names[i][j];
      }
      _ComptimeCtx.TopLevel.appendf("  PERSON_%s = %d,\n", upper_name, i);
    }
    _ComptimeCtx.TopLevel.appendf("};\n");
    printf("Generated enum with 3 person IDs\n");
  });

  // Use time functions at compile time
  long compile_timestamp = _Comptime({
    time_t now = time(NULL);
    printf("Compilation timestamp: %ld\n", (long)now);
    _ComptimeCtx.Inline.appendf("%ldL", (long)now);
  });

  printf("PI=%.5f\n", pi_approx);
  printf("PERSON_ALICE=%d\n", PERSON_ALICE);
  printf("PERSON_BOB=%d\n", PERSON_BOB);
  printf("PERSON_CHARLIE=%d\n", PERSON_CHARLIE);
  printf("COMPILE_TIME=%ld\n", compile_timestamp);

  return (pi_approx > 3.14 && pi_approx < 3.15 &&
          PERSON_ALICE == 0 && PERSON_BOB == 1 &&
          PERSON_CHARLIE == 2 && compile_timestamp > 0) ? 0 : 1;
}
