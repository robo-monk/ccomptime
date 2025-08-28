#include "../ccomptime.h"
#include "stdio.h"

CCT_CTX(
    CCT_DEFINE(ON_EXIT on_exit);

    void on_exit() { printf("\nhellooo on exit dump called\n"); }

    void helper() { printf("helper called\n"); });

CCT_DO({
  int i = 0;
  while (i++ < 10) {
    helper();
    printf("hmm %d \" and this \"\n", i);
  }
});

int main() {
  CCT_DO({
    printf("er?\n");
    printf("yes?\n");
  });

  printf("hello!");
  return 0;
}
