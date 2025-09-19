#include "nob.h"
#include <stdlib.h>

#define fatal(fmt, ...)                                                        \
  do {                                                                         \
    fflush(stdout);                                                            \
    fprintf(stderr, "\n" RED("[FATAL] "));                                     \
    fprintf(stderr, fmt, ##__VA_ARGS__);                                       \
    fputc('\n', stderr);                                                       \
    exit(1);                                                                   \
  } while (0)

typedef struct {
  String_View *items;
  size_t count, capacity;
} Strings;

void strings_append(Strings *strings, String_View item) {
  nob_da_append(strings, item);
}

Strings strings_new(size_t capacity) {
  Strings strings = {
      .count = 0,
      .capacity = capacity,
      .items = (Nob_String_View *)malloc(sizeof(Nob_String_View) * capacity)};
  return strings;
}
