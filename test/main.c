const static int first = __LINE__;
#include "../ccomptime.h"
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

#define typename(x)                                                            \
  _Generic((x), \
      int: "int", \
      double: "double", \
      char*: "char*", \
      default: "unknown" \
  )

const char *func_definitions[500] = {};
size_t func_def_count = 0;
bool _has_func_def(const char *def) {
  for (size_t i = 0; i < func_def_count; i++) {
    if (strcmp(func_definitions[i], def) == 0) {
      return true;
    }
  }
  return false;
}
void _append_function_definition(const char *def) {
  func_definitions[func_def_count++] = def;
}

// #define add(type, a, b)                                                        \
//   (/* compile-time side effects */                                             \
//    comptime_inline({                                                           \
//      assert(strcmp(typename(a), typename(b)) == 0 &&                           \
//             "types should be the same");                                       \
//      printf("Got types (%s) a and (%s) b\n", typename(a), typename(b));        \
//      const char *t = #type;                                                    \
//      const char *func_sign =                                                   \
//          nob_temp_sprintf("%s add_%s(%s a, %s b)", t, t, t, t);                \
//      if (!_has_func_def(func_sign)) {                                          \
//        printf("[POLYMORPHISM] Generating... %s\n", func_sign);                 \
//        _append_function_definition(func_sign);                                 \
//        $$_top_level("\n%s;\n", func_sign);                                     \
//        $$_bottom_level("\n%s { return a + b; }\n", func_sign);                 \
//      }                                                                         \
//      $$("add_%s(%s,%s);", t, #a, #b);                                          \
//    }),                                                                         \
//    (type)0)

#define add(type, a, b)                                                        \
  comptime_inline_typed((type)0, {                                             \
    assert(strcmp(typename(a), typename(b)) == 0 &&                            \
           "types should be the same");                                        \
    printf("Got types (%s) a and (%s) b\n", typename(a), typename(b));         \
    const char *t = #type;                                                     \
    const char *func_sign =                                                    \
        nob_temp_sprintf("%s add_%s(%s a, %s b)", t, t, t, t);                 \
    if (!_has_func_def(func_sign)) {                                           \
      printf("[POLYMORPHISM] Generating... %s\n", func_sign);                  \
      _append_function_definition(func_sign);                                  \
      $$_top_level("\n%s;\n", func_sign);                                      \
      $$_bottom_level("\n%s { return a + b; }\n", func_sign);                  \
    }                                                                          \
    $$("add_%s(%s,%s);", t, #a, #b);                                           \
  })

int result2;
// add(int, 1, 2, &result2);

comptime {
  printf("\n\n\n\n\nComptime block executed at runtime!\n\n\n\n\n\n");

  // THIS will not work since we generate the add function during comptime,
  // thus during comptime its not available
  //
  // maybe we can have a two-pass system where we first generate all the
  // functions then in the second pass we can use them?

  // int query2 = add(int, 50, 5);
  // int query3 = add(int, 100, 20);
  // float query4 = add(float, 1.5, 2.5);
  // double query5 = add(double, 2.5, 3.5);

  // test_assert(query2 == 55, "query2 should be 55");
  // test_assert(query3 == 120, "query3 should be 120");
  // test_assert(query4 == 4.0f, "query4 should be 4.0f");
  // test_assert(query5 == 6.0, "query5 should be 6.0");
}

int main() {

  int query2 = add(int, 50, 5);
  int query3 = add(int, 100, 20);
  float query4 = add(float, 1.5, 2.5);
  double query5 = add(double, 2.5, 3.5);

  // char sql = add(char, 'a', 'b');
  // char *vv = add((char *), "hello", "b");

  assert(query2 == 55 && "query2 should be 55");
  assert(query3 == 120 && "query3 should be 120");
  assert(query4 == 4.0f && "query4 should be 4.0f");
  assert(query5 == 6.0 && "query5 should be 6.0");

  char sum = add(char, '0', '1');
  printf("sum is %c\n", sum);
  assert(sum == 'a' && "adding characters??");

  printf("Hello! -> (result2 is %d and query2 is %d) ", result2, query2);

  // add(int, 1, 2);
  return 42;
}

const static int last = __LINE__;
