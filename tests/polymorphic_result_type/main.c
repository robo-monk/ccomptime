#include "../../ccomptime.h"
#define HASHMAP_IMPLEMENTATION
#include "../../deps/hashmap/hashmap.h"
#define NOB_IMPLEMENTATION
#include "../../nob.h"
#include "./main.c.h"
#include <stdbool.h>
#include <stdio.h>

HashMap polymorphisms = {0};
void _Result(_ComptimeCtx ctx, char *success_type, char *error_type) {
  char *type_key = nob_temp_sprintf("Result_%s_%s", success_type, error_type);
  uint64_t hash = hashmap_fnv_hash((char *)type_key, strlen(type_key));

  if (!hashmap_get(&polymorphisms, type_key)) {
    hashmap_put(&polymorphisms, type_key, (void *)1);
    ctx.TopLevel.appendf("typedef struct { int ok; union { %s value; %s error; "
                         "};} Result_%zu;\n",
                         success_type, error_type, hash);
  }

  ctx.Inline.appendf("Result_%zu", hash);
}

#define Result(success_type, error_type)                                       \
  _ComptimeType({ _Result(_ComptimeCtx, success_type, error_type); })

Result("int", "char*") foo() {
  return (Result("int", "char*")){.ok = true, .value = 42};
}

int main() {
  Result("char*", "char*") x =
      (Result("char*", "char*")){.ok = true, .value = "yep"};

  _Comptime({ printf("Hello from comptime\n"); });
  printf("%s : %d\n", x.value, foo().value);
  return 0;
}
