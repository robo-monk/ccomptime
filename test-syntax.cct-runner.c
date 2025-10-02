#include <string.h>
#define _InlineC strdup
#include <stdlib.h>
#include <stdio.h>
static FILE *_Comptime_FP;
void _Register_Comptime_exec(char *exe_result, int index) {
  fprintf(_Comptime_FP, "#define _COMPTIME_X%d(x) %s\n", index, exe_result ? exe_result : "/*void block*/"); // appends text at the end
}
#define main _User_main // overwriting the entry point of the program
#include "test-syntax.c"
#undef main

char* _Comptime__exec0(void){
fprintf(_Comptime_FP, "\n#include <stdio.h>\n");
return NULL;}

char* _Comptime__exec1(void){
printf("%s\n", "~> Hello, World!");
return NULL;}

char* _Comptime__exec2(void){
printf("Hello, World!\n");
return NULL;}

char* _Comptime__exec3(void){

    printf("wooo$$$");
    fprintf(_Comptime_FP,
            "\nvoid hello_world() { printf(\"what in the fuck\"); }\n");
    return _InlineC("42");
;
return NULL;}

int main(void) {
_Comptime_FP = fopen("test-syntax.c.h", "a");if(!_Comptime_FP){perror("fopen");return 1;}

_Register_Comptime_exec(_Comptime__exec0(), 0); // execute comptime statement #0
_Register_Comptime_exec(_Comptime__exec1(), 1); // execute comptime statement #1
_Register_Comptime_exec(_Comptime__exec2(), 2); // execute comptime statement #2
_Register_Comptime_exec(_Comptime__exec3(), 3); // execute comptime statement #3

fprintf(_Comptime_FP, "#undef _COMPTIME_X\n#define _COMPTIME_X(n, x) CONCAT(_COMPTIME_X, n)(x)\n");
fclose(_Comptime_FP);
}