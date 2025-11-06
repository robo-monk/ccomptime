#include "../../ccomptime.h"
#define NOB_IMPLEMENTATION
#include "../../nob.h"
#include "./main.c.h"

// _COMPTIME_BUFFER(Exports); // this will inline all elements appended in the
// "Export" buffer

const char *module_name = "ModuleA";

Nob_String_Builder ExportsType = {0};
Nob_String_Builder Exports = {0};

/*
_Comptime({
  Function fn;
  _ComptimeCtx.NextNode.get_function(&fn);

  if (!fn.success)
    return _ComptimeCtx.ThrowError("expected function after export decorator");

  _ComptimeCtx.GetBuffer("ExportsType")
      .appendf("%s (*%s)(%s);\n", fn.return_type, fn.name, fn.args_type);

  _ComptimeCtx.GetBuffer("Exports").appendf(".%s = %s", fn.name, fn.name);
});
*/

int add(int a, int b) { return a + b; }

int main() {
  printf("whatzpop\n");
  _Comptime({
    // ffwd declare
    _ComptimeCtx.TopLevel.appendf("%s %s(%s);\n", "int", "add", "int a, int b");

    nob_sb_appendf(&ExportsType, "%s (*%s)(%s);\n", "int", "add",
                   "int a, int b");
    nob_sb_appendf(&Exports, ".%s = %s", "add", "add");
  });
  _Comptime({
    nob_sb_append_null(&ExportsType);
    nob_sb_append_null(&Exports);
    // _ComptimeCtx.Inline.appendf("struct %s_vtable { %s }",
    // ExportsType.items);
    _ComptimeCtx.TopLevel.appendf(
        "static const struct %s_vtable { %s } %s = (struct %s_vtable) { %s };",
        module_name, ExportsType.items, module_name, module_name,
        Exports.items);
  });

  const int result = ModuleA.add(1, 5);
}
