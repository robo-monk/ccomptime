#if defined(_INPUT_PROGRAM_PATH) && defined(_OUTPUT_HEADERS_PATH)
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *_Comptime_FP;

void _Register_Comptime_exec(char *exe_result, int index) {
  fprintf(_Comptime_FP, "#define _COMPTIME_X%d(x) %s\n", index,
          exe_result ? exe_result : "/*void block*/");
}

#define main _User_main // overwrite the entrypoint of the user program
#include _INPUT_PROGRAM_PATH
#undef main

int main(void) {
  _Comptime_FP = fopen(_INPUT_PROGRAM_PATH, "w");
  if (!_Comptime_FP) {
    fprintf(stderr, "Failed to open %s for writing\n", _INPUT_PROGRAM_PATH);
    exit(EXIT_FAILURE);
  }

  fprintf(_Comptime_FP, "\n#undef _COMPTIME_X\n#define _COMPTIME_X(n,x) "
                        "CONCAT(_COMPTIME_X,n)(x)\n");

  fflush(_Comptime_FP);
  fclose(_Comptime_FP);
}

#else
#error "please define _INPUT_PROGRAM_PATH and _OUTPUT_HEADERS_PATH"
#endif
