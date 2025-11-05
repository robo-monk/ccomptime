#include <stdio.h>

#include "../../ccomptime.h"
#include "main.c.h"

int main(void) {
  // Generate nested struct types at compile time
  _Comptime({
    // Generate inner struct
    _ComptimeCtx.TopLevel.appendf(
      "typedef struct {\n"
      "  int x;\n"
      "  int y;\n"
      "} Point2D;\n"
    );

    // Generate outer struct with nested type
    _ComptimeCtx.TopLevel.appendf(
      "typedef struct {\n"
      "  Point2D position;\n"
      "  Point2D velocity;\n"
      "  int mass;\n"
      "} Particle;\n"
    );

    printf("Generated nested struct types at comptime\n");
  });

  // Generate a vector type with _ComptimeType
  typedef _ComptimeType({
    int dimensions = 3;
    _ComptimeCtx.Inline.appendf("struct { ");
    for (int i = 0; i < dimensions; i++) {
      char axis = 'x' + i;
      _ComptimeCtx.Inline.appendf("float %c; ", axis);
    }
    _ComptimeCtx.Inline.appendf("}");
    printf("Generated Vector3D type at comptime\n");
  }) Vector3D;

  // Use the generated types
  Particle p = {
    .position = {.x = 10, .y = 20},
    .velocity = {.x = 1, .y = 2},
    .mass = 100
  };

  Vector3D v = {.x = 1.0f, .y = 2.0f, .z = 3.0f};

  printf("PARTICLE_POS_X=%d\n", p.position.x);
  printf("PARTICLE_POS_Y=%d\n", p.position.y);
  printf("PARTICLE_VEL_X=%d\n", p.velocity.x);
  printf("PARTICLE_VEL_Y=%d\n", p.velocity.y);
  printf("PARTICLE_MASS=%d\n", p.mass);
  printf("VECTOR_X=%.1f\n", v.x);
  printf("VECTOR_Y=%.1f\n", v.y);
  printf("VECTOR_Z=%.1f\n", v.z);

  return (p.position.x == 10 && p.mass == 100 &&
          v.x == 1.0f && v.y == 2.0f && v.z == 3.0f) ? 0 : 1;
}
