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

#define inline_comptime /*INLINE_COMPTIME*/

// type CONCAT(add, type)(type a, type b) { return a + b; }

// add(int, 1, 2);

comptime {

  test_assert(fibo(10) == 55, "fibo(10) should be 55");
  test_assert(fibo(20) == 6765, "fibo(20) should be 6765");
  test_assert(fibo(30) == 832040, "fibo(30) should be 832040");
  // test_assert(fibo(42) == 2, "fibo(42) should be 267914296");
  test_assert(fibo(0) == 0, "fibo(0) should be 0");
  test_assert(fibo(1) == 1, "fibo(1) should be 1");
  test_assert(false, "..");
}

typedef int SQLQuery;
SQLQuery SQL_parse(const char *q) {
  printf("Executing query: %s\n", q);
  return 42;
}

#define add(type, a, b, result)                                                \
  comptime {                                                                   \
    const char *t = #type;                                                     \
    $$_top_level("\n%s _cct_add_%s(%s a, %s b);\n", t, t, t, t);               \
    $$_bottom_level(                                                           \
        "\n%s add_%s(%s a, %s b, %s*result) { *result = a + b; }\n", t, t, t,  \
        t, t);                                                                 \
    $$("add(a,b)");                                                            \
  }

// #define add(type, a, b, result)                                                \
//   comptime { $$("add(a,b)"); }

int result2;
add(int, 1, 2, &result2);

comptime {
  printf("\n\n\n\n\nComptime block executed at runtime!\n\n\n\n\n\n");
}

int main() {
  int query = inline_comptime SQL_parse("SELECT * FROM users WHERE id = 1;");

  printf("Hello! -> (result2 is %d) ", result2);

  // add(int, 1, 2);
  return 42;
}

const static int last = __LINE__;
