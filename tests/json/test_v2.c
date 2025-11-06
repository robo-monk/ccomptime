#include <stdio.h>
#include <string.h>
#include "../../ccomptime.h"

void derive_json_serializer(_ComptimeCtx ctx) {
  _Comptime_Type typ;
  ctx.NextNode.get_type(&typ);

  if (!typ.success) {
    printf("Error: expected struct after decorator\n");
    return;
  }

  printf("Generating JSON serializer for struct with %zu fields:\n", typ.field_count);

  // Generate to_json function
  ctx.TopLevel.appendf("\nvoid User_to_json(User *obj, char *output, size_t size) {\n");
  ctx.TopLevel.appendf("  size_t pos = 0;\n");
  ctx.TopLevel.appendf("  pos += snprintf(output + pos, size - pos, \"{\");\n");

  for (size_t i = 0; i < typ.field_count; i++) {
    _Comptime_Field field = typ.fields[i];
    printf("  Field %zu: %s %s\n", i, field.type, field.name);

    if (i > 0) {
      ctx.TopLevel.appendf("  pos += snprintf(output + pos, size - pos, \",\");\n");
    }

    ctx.TopLevel.appendf("  pos += snprintf(output + pos, size - pos, \"\\\"%s\\\":\");\n", field.name);

    // Handle different types
    if (strstr(field.type, "int")) {
      ctx.TopLevel.appendf("  pos += snprintf(output + pos, size - pos, \"%%d\", obj->%s);\n", field.name);
    } else if (strstr(field.type, "char")) {
      ctx.TopLevel.appendf("  pos += snprintf(output + pos, size - pos, \"\\\"\");\n");
      ctx.TopLevel.appendf("  if (obj->%s) pos += snprintf(output + pos, size - pos, \"%%s\", obj->%s);\n", field.name, field.name);
      ctx.TopLevel.appendf("  pos += snprintf(output + pos, size - pos, \"\\\"\");\n");
    }
  }

  ctx.TopLevel.appendf("  pos += snprintf(output + pos, size - pos, \"}\");\n");
  ctx.TopLevel.appendf("}\n");
}

// Forward declaration for generated function
typedef struct User User;
void User_to_json(User *obj, char *output, size_t size);

// Comptime decorator - placed BEFORE the typedef it decorates
_Comptime({
  derive_json_serializer(_ComptimeCtx);
});

struct User {
  char *name;
  int age;
};

#ifndef _COMPILING
int main() {
  User user = {.name = "Alice", .age = 30};
  char json[256];
  User_to_json(&user, json, sizeof(json));
  printf("JSON: %s\n", json);
  return 0;
}
#endif
