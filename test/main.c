const static int first = __LINE__;
#include "../ccomptime.h"
#include <stdatomic.h>
#define NOB_IMPLEMENTATION
#include "../nob.h"
// #include "another.c"
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

#define GREEN(str) "\033[0;32m" str "\033[0m"
#define RED(str) "\033[0;31m" str "\033[0m"
#define GRAY(str) "\033[0;90m" str "\033[0m"

void test_assert(int condition, const char *message) {
  if (!condition) {
    fprintf(stderr, RED("[FAILED]") GRAY(" %s\n"), message);
  } else {
    printf(GREEN("[PASSED]") GRAY(" %s\n"), message);
  }
}

comptime { test_assert(compute() == 420, "compute() should be 420"); }

int fibo(int n) {
  if (n <= 1)
    return n;
  return fibo(n - 1) + fibo(n - 2);
}

comptime {
  test_assert(fibo(10) == 55, "fibo(10) should be 55");
  test_assert(fibo(20) == 6765, "fibo(20) should be 6765");
  test_assert(fibo(30) == 832040, "fibo(30) should be 832040");
  // test_assert(fibo(42) == 2, "fibo(42) should be 267914296");
  test_assert(fibo(0) == 0, "fibo(0) should be 0");
  test_assert(fibo(1) == 1, "fibo(1) should be 1");
  test_assert(false, "..");
}

comptime {
  printf("\n\nHello from the comptime!\n\n");
  $$("int result = %d;", compute());
}

// $DEFINE_OP("hello");
int main() {

  // const char *fmt = CCT__auto_fmt(result);
  // $comptime(printf("hello from: comptime from main(%d) \n\n", result));
  // printf("hello from runtime space %d\n", result);
  return 42;
}

// $comptime(on_exit());

const static int last = __LINE__;
