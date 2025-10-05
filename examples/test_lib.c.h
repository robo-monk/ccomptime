/*// @generated - ccomptime™ v0.0.1 - 1759685792 \*/
#define _CONCAT_(x, y) x##y
#define CONCAT(x, y) _CONCAT_(x, y)
#define _Comptime(x) _COMPTIME_X(__COUNTER__, x)
#define _ComptimeType(x) _COMPTIME_X(__COUNTER__, x)
#define _COMPTIME_X(n, x) CONCAT(_PLACEHOLDER_COMPTIME_X, n)(x)
/* ---- */// the end. ///* ---- */

#undef _COMPTIME_X
#define _COMPTIME_X(n,x) CONCAT(_COMPTIME_X,n)(x)
