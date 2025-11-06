#include <stdio.h>
#include <string.h>

#include "../../ccomptime.h"
#include "main.c.h"

int main(void) {
  _Comptime({
    FILE *f = fopen("tests/binary_asset_embed/icon.dat", "rb");
    if (!f) {
      fprintf(stderr, "Failed to open icon.dat\n");
      return;
    }

    // Get file size
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    // Generate array with size
    _ComptimeCtx.TopLevel.appendf("static const unsigned char icon_data[] = {");

    unsigned char byte;
    int first = 1;
    while (fread(&byte, 1, 1, f) == 1) {
      if (!first) {
        _ComptimeCtx.TopLevel.appendf(", ");
      }
      _ComptimeCtx.TopLevel.appendf("0x%02x", byte);
      first = 0;
    }

    _ComptimeCtx.TopLevel.appendf("};\n");
    _ComptimeCtx.TopLevel.appendf("static const long icon_data_size = %ld;\n", size);

    fclose(f);
  });
  printf("Icon data size: %ld bytes\n", icon_data_size);
  printf("First bytes: 0x%02x 0x%02x 0x%02x 0x%02x\n",
         icon_data[0], icon_data[1], icon_data[2], icon_data[3]);

  // Check PNG magic number
  if (icon_data_size == 12 &&
      icon_data[0] == 0x89 && icon_data[1] == 0x50 &&
      icon_data[2] == 0x4e && icon_data[3] == 0x47) {
    printf("BINARY_EMBED_VALID=1\n");
    return 0;
  }

  printf("BINARY_EMBED_VALID=0\n");
  return 1;
}
