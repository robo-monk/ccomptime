/*// @generated - ccomptime™ v0.0.1 - 1759685792 \*/
#define _CONCAT_(x, y) x##y
#define CONCAT(x, y) _CONCAT_(x, y)
#define _Comptime(x) _COMPTIME_X(__COUNTER__, x)
#define _ComptimeType(x) _COMPTIME_X(__COUNTER__, x)
#define _COMPTIME_X(n, x) CONCAT(_PLACEHOLDER_COMPTIME_X, n)(x)
/* ---- */// the end. ///* ---- */

#undef _COMPTIME_X
#define _COMPTIME_X(n,x) CONCAT(_COMPTIME_X,n)(x)
#define _COMPTIME_X0(x) Result_43184
#define _COMPTIME_X1(x) Result_43184
#define _COMPTIME_X2(x) Result_53565
#define _COMPTIME_X3(x) Result_53565
#define _COMPTIME_X4(x) Result_53565
#define _COMPTIME_X5(x) Result_35469
#define _COMPTIME_X6(x) Result_35469
#define _COMPTIME_X7(x) Result_35469
#define _COMPTIME_X8(x) 5

/* top level definitions */

typedef struct { union {int value; char* error;} as; int is_ok; } Result_43184;

typedef struct { union {char value; char* error;} as; int is_ok; } Result_53565;

typedef struct { union {char* value; char* error;} as; int is_ok; } Result_35469;

