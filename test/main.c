const static int first = __LINE__;
#include "../ccomptime.h"
#include <stdatomic.h>
#define NOB_IMPLEMENTATION
#include "../nob.h"
#include "stdio.h"

typedef struct {
  char *name;
} Op;

typedef struct {
  Op *items;
  size_t count;
  size_t capacity;
} Ops;

static Ops ops = {0};

void on_exit() {
  // write gencode file
  // open op file
  Nob_String_Builder gencode = {0};
  nob_da_foreach(Op, op, &ops) {
    nob_sb_appendf(&gencode, "function %s(){}\n", op->name);
  }

  nob_write_entire_file("gencode.js", gencode.items, gencode.count);
}

void define_op(char *op) { nob_da_append(&ops, (Op){.name = strdup(op)}); };

#define $DEFINE_OP(op) $comptime(define_op(op));

int compute() {
  int a = 400;
  int b = 20;
  return a + b;
};
$COMPTIME_INT(result, compute());
$DEFINE_OP("hello");
int main() {

  // const char *fmt = CCT__auto_fmt(result);
  // $comptime(printf("hello from: comptime from main(%d) \n\n", result));
  printf("hello from runtime space %d\n", result);
  return 42;
}

$comptime(on_exit());

const static int last = __LINE__;
