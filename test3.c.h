/*// @generated - ccomptime™ v0.0.1 - 1759413235 \*/
#define _CONCAT_(x, y) x##y
#define CONCAT(x, y) _CONCAT_(x, y)
#define _Comptime(x) _COMPTIME_X(__COUNTER__, x)
#define _ComptimeType(x) _COMPTIME_X(__COUNTER__, x)
#define _COMPTIME_X(n, x) CONCAT(_PLACEHOLDER_COMPTIME_X, n)(x)
#include<stdio.h>#defineHASHMAP_IMPLEMENTATION#include"deps/hashmap/hashmap.h"_Comptime(printf("hi"));HashMap_func_defs={0};void_comptime_polymorph_type(_ComptimeCtx_ComptimeCtx){_ComptimeCtx.Inline.appendf("int");}_ComptimeType(_comptime_polymorph_type(_ComptimeCtx))test(){return1;}/* ---- */// the end. ///* ---- */
