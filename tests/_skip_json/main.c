#import "../../ccomptime.h"

#define _Serializable                                                          \
  _Comptime({                                                                  \
    derive_json_parser(_ComptimeCtx);                                          \
    derive_json_serializer(_ComptimeCtx)                                       \
  })

typedef _Serializable struct {
  char *name;
  int age;
} User;

// _Serializable

// _Comptime({
//   Type typ;
//   _ComptimeCtx.NextNode.get_type(&typ)

//       if (!typ.success) {
//     return _ComptimeCtx.ThrowError(
//         "expected type below JSON serialise decorator");
//   }

//   for_each(char *, field, typ.field) { printf("field name: ", field); }
// })

// void to_json(_ComptimeArg object, char *output) {}
