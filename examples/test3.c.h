/*// @generated - ccomptime™ v0.0.1 - 1762080459 \*/
#pragma once
#define _CONCAT_(x, y) x##y
#define CONCAT(x, y) _CONCAT_(x, y)
#define _Comptime(x) _COMPTIME_X(__COUNTER__, x)
#define _ComptimeType(x) _COMPTIME_X(__COUNTER__, x)
#define _COMPTIME_X(n, x) CONCAT(_PLACEHOLDER_COMPTIME_X, n)(x)
/* ---- */// the end. ///* ---- */

#undef _COMPTIME_X
#define _COMPTIME_X(n,x) CONCAT(_COMPTIME_X,n)(x)
#define _COMPTIME_X0(x) 42
#define _COMPTIME_X1(x) 10
#define _COMPTIME_X2(x) int
#define _COMPTIMETYPE_0 int
#define _COMPTIME_X3(x) 
#define _COMPTIME_X4(x) 5
