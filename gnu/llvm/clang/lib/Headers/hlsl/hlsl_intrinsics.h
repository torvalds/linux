//===----- hlsl_intrinsics.h - HLSL definitions for intrinsics ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _HLSL_HLSL_INTRINSICS_H_
#define _HLSL_HLSL_INTRINSICS_H_

namespace hlsl {

// Note: Functions in this file are sorted alphabetically, then grouped by base
// element type, and the element types are sorted by size, then singed integer,
// unsigned integer and floating point. Keeping this ordering consistent will
// help keep this file manageable as it grows.

#define _HLSL_BUILTIN_ALIAS(builtin)                                           \
  __attribute__((clang_builtin_alias(builtin)))
#define _HLSL_AVAILABILITY(platform, version)                                  \
  __attribute__((availability(platform, introduced = version)))
#define _HLSL_AVAILABILITY_STAGE(platform, version, stage)                     \
  __attribute__((                                                              \
      availability(platform, introduced = version, environment = stage)))

#ifdef __HLSL_ENABLE_16_BIT
#define _HLSL_16BIT_AVAILABILITY(platform, version)                            \
  __attribute__((availability(platform, introduced = version)))
#define _HLSL_16BIT_AVAILABILITY_STAGE(platform, version, stage)               \
  __attribute__((                                                              \
      availability(platform, introduced = version, environment = stage)))
#else
#define _HLSL_16BIT_AVAILABILITY(environment, version)
#define _HLSL_16BIT_AVAILABILITY_STAGE(environment, version, stage)
#endif

//===----------------------------------------------------------------------===//
// abs builtins
//===----------------------------------------------------------------------===//

/// \fn T abs(T Val)
/// \brief Returns the absolute value of the input value, \a Val.
/// \param Val The input value.

#ifdef __HLSL_ENABLE_16_BIT
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_abs)
int16_t abs(int16_t);
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_abs)
int16_t2 abs(int16_t2);
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_abs)
int16_t3 abs(int16_t3);
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_abs)
int16_t4 abs(int16_t4);
#endif

_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_abs)
half abs(half);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_abs)
half2 abs(half2);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_abs)
half3 abs(half3);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_abs)
half4 abs(half4);

_HLSL_BUILTIN_ALIAS(__builtin_elementwise_abs)
int abs(int);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_abs)
int2 abs(int2);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_abs)
int3 abs(int3);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_abs)
int4 abs(int4);

_HLSL_BUILTIN_ALIAS(__builtin_elementwise_abs)
float abs(float);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_abs)
float2 abs(float2);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_abs)
float3 abs(float3);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_abs)
float4 abs(float4);

_HLSL_BUILTIN_ALIAS(__builtin_elementwise_abs)
int64_t abs(int64_t);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_abs)
int64_t2 abs(int64_t2);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_abs)
int64_t3 abs(int64_t3);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_abs)
int64_t4 abs(int64_t4);

_HLSL_BUILTIN_ALIAS(__builtin_elementwise_abs)
double abs(double);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_abs)
double2 abs(double2);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_abs)
double3 abs(double3);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_abs)
double4 abs(double4);

//===----------------------------------------------------------------------===//
// acos builtins
//===----------------------------------------------------------------------===//

/// \fn T acos(T Val)
/// \brief Returns the arccosine of the input value, \a Val.
/// \param Val The input value.

#ifdef __HLSL_ENABLE_16_BIT
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_acos)
half acos(half);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_acos)
half2 acos(half2);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_acos)
half3 acos(half3);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_acos)
half4 acos(half4);
#endif

_HLSL_BUILTIN_ALIAS(__builtin_elementwise_acos)
float acos(float);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_acos)
float2 acos(float2);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_acos)
float3 acos(float3);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_acos)
float4 acos(float4);

//===----------------------------------------------------------------------===//
// all builtins
//===----------------------------------------------------------------------===//

/// \fn bool all(T x)
/// \brief Returns True if all components of the \a x parameter are non-zero;
/// otherwise, false. \param x The input value.

#ifdef __HLSL_ENABLE_16_BIT
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_all)
bool all(int16_t);
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_all)
bool all(int16_t2);
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_all)
bool all(int16_t3);
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_all)
bool all(int16_t4);
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_all)
bool all(uint16_t);
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_all)
bool all(uint16_t2);
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_all)
bool all(uint16_t3);
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_all)
bool all(uint16_t4);
#endif

_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_all)
bool all(half);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_all)
bool all(half2);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_all)
bool all(half3);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_all)
bool all(half4);

_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_all)
bool all(bool);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_all)
bool all(bool2);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_all)
bool all(bool3);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_all)
bool all(bool4);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_all)

_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_all)
bool all(int);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_all)
bool all(int2);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_all)
bool all(int3);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_all)
bool all(int4);

_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_all)
bool all(uint);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_all)
bool all(uint2);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_all)
bool all(uint3);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_all)
bool all(uint4);

_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_all)
bool all(float);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_all)
bool all(float2);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_all)
bool all(float3);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_all)
bool all(float4);

_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_all)
bool all(int64_t);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_all)
bool all(int64_t2);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_all)
bool all(int64_t3);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_all)
bool all(int64_t4);

_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_all)
bool all(uint64_t);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_all)
bool all(uint64_t2);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_all)
bool all(uint64_t3);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_all)
bool all(uint64_t4);

_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_all)
bool all(double);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_all)
bool all(double2);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_all)
bool all(double3);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_all)
bool all(double4);

//===----------------------------------------------------------------------===//
// any builtins
//===----------------------------------------------------------------------===//

/// \fn bool any(T x)
/// \brief Returns True if any components of the \a x parameter are non-zero;
/// otherwise, false. \param x The input value.

#ifdef __HLSL_ENABLE_16_BIT
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_any)
bool any(int16_t);
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_any)
bool any(int16_t2);
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_any)
bool any(int16_t3);
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_any)
bool any(int16_t4);
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_any)
bool any(uint16_t);
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_any)
bool any(uint16_t2);
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_any)
bool any(uint16_t3);
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_any)
bool any(uint16_t4);
#endif

_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_any)
bool any(half);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_any)
bool any(half2);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_any)
bool any(half3);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_any)
bool any(half4);

_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_any)
bool any(bool);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_any)
bool any(bool2);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_any)
bool any(bool3);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_any)
bool any(bool4);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_any)

_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_any)
bool any(int);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_any)
bool any(int2);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_any)
bool any(int3);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_any)
bool any(int4);

_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_any)
bool any(uint);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_any)
bool any(uint2);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_any)
bool any(uint3);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_any)
bool any(uint4);

_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_any)
bool any(float);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_any)
bool any(float2);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_any)
bool any(float3);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_any)
bool any(float4);

_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_any)
bool any(int64_t);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_any)
bool any(int64_t2);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_any)
bool any(int64_t3);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_any)
bool any(int64_t4);

_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_any)
bool any(uint64_t);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_any)
bool any(uint64_t2);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_any)
bool any(uint64_t3);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_any)
bool any(uint64_t4);

_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_any)
bool any(double);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_any)
bool any(double2);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_any)
bool any(double3);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_any)
bool any(double4);

//===----------------------------------------------------------------------===//
// asin builtins
//===----------------------------------------------------------------------===//

/// \fn T asin(T Val)
/// \brief Returns the arcsine of the input value, \a Val.
/// \param Val The input value.

#ifdef __HLSL_ENABLE_16_BIT
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_asin)
half asin(half);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_asin)
half2 asin(half2);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_asin)
half3 asin(half3);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_asin)
half4 asin(half4);
#endif

_HLSL_BUILTIN_ALIAS(__builtin_elementwise_asin)
float asin(float);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_asin)
float2 asin(float2);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_asin)
float3 asin(float3);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_asin)
float4 asin(float4);

//===----------------------------------------------------------------------===//
// atan builtins
//===----------------------------------------------------------------------===//

/// \fn T atan(T Val)
/// \brief Returns the arctangent of the input value, \a Val.
/// \param Val The input value.

#ifdef __HLSL_ENABLE_16_BIT
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_atan)
half atan(half);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_atan)
half2 atan(half2);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_atan)
half3 atan(half3);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_atan)
half4 atan(half4);
#endif

_HLSL_BUILTIN_ALIAS(__builtin_elementwise_atan)
float atan(float);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_atan)
float2 atan(float2);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_atan)
float3 atan(float3);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_atan)
float4 atan(float4);

//===----------------------------------------------------------------------===//
// ceil builtins
//===----------------------------------------------------------------------===//

/// \fn T ceil(T Val)
/// \brief Returns the smallest integer value that is greater than or equal to
/// the input value, \a Val.
/// \param Val The input value.

_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_ceil)
half ceil(half);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_ceil)
half2 ceil(half2);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_ceil)
half3 ceil(half3);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_ceil)
half4 ceil(half4);

_HLSL_BUILTIN_ALIAS(__builtin_elementwise_ceil)
float ceil(float);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_ceil)
float2 ceil(float2);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_ceil)
float3 ceil(float3);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_ceil)
float4 ceil(float4);

//===----------------------------------------------------------------------===//
// clamp builtins
//===----------------------------------------------------------------------===//

/// \fn T clamp(T X, T Min, T Max)
/// \brief Clamps the specified value \a X to the specified
/// minimum ( \a Min) and maximum ( \a Max) range.
/// \param X A value to clamp.
/// \param Min The specified minimum range.
/// \param Max The specified maximum range.
///
/// Returns The clamped value for the \a X parameter.
/// For values of -INF or INF, clamp will behave as expected.
/// However for values of NaN, the results are undefined.

_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_clamp)
half clamp(half, half, half);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_clamp)
half2 clamp(half2, half2, half2);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_clamp)
half3 clamp(half3, half3, half3);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_clamp)
half4 clamp(half4, half4, half4);

#ifdef __HLSL_ENABLE_16_BIT
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_clamp)
int16_t clamp(int16_t, int16_t, int16_t);
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_clamp)
int16_t2 clamp(int16_t2, int16_t2, int16_t2);
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_clamp)
int16_t3 clamp(int16_t3, int16_t3, int16_t3);
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_clamp)
int16_t4 clamp(int16_t4, int16_t4, int16_t4);

_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_clamp)
uint16_t clamp(uint16_t, uint16_t, uint16_t);
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_clamp)
uint16_t2 clamp(uint16_t2, uint16_t2, uint16_t2);
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_clamp)
uint16_t3 clamp(uint16_t3, uint16_t3, uint16_t3);
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_clamp)
uint16_t4 clamp(uint16_t4, uint16_t4, uint16_t4);
#endif

_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_clamp)
int clamp(int, int, int);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_clamp)
int2 clamp(int2, int2, int2);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_clamp)
int3 clamp(int3, int3, int3);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_clamp)
int4 clamp(int4, int4, int4);

_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_clamp)
uint clamp(uint, uint, uint);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_clamp)
uint2 clamp(uint2, uint2, uint2);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_clamp)
uint3 clamp(uint3, uint3, uint3);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_clamp)
uint4 clamp(uint4, uint4, uint4);

_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_clamp)
int64_t clamp(int64_t, int64_t, int64_t);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_clamp)
int64_t2 clamp(int64_t2, int64_t2, int64_t2);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_clamp)
int64_t3 clamp(int64_t3, int64_t3, int64_t3);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_clamp)
int64_t4 clamp(int64_t4, int64_t4, int64_t4);

_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_clamp)
uint64_t clamp(uint64_t, uint64_t, uint64_t);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_clamp)
uint64_t2 clamp(uint64_t2, uint64_t2, uint64_t2);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_clamp)
uint64_t3 clamp(uint64_t3, uint64_t3, uint64_t3);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_clamp)
uint64_t4 clamp(uint64_t4, uint64_t4, uint64_t4);

_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_clamp)
float clamp(float, float, float);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_clamp)
float2 clamp(float2, float2, float2);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_clamp)
float3 clamp(float3, float3, float3);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_clamp)
float4 clamp(float4, float4, float4);

_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_clamp)
double clamp(double, double, double);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_clamp)
double2 clamp(double2, double2, double2);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_clamp)
double3 clamp(double3, double3, double3);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_clamp)
double4 clamp(double4, double4, double4);

//===----------------------------------------------------------------------===//
// cos builtins
//===----------------------------------------------------------------------===//

/// \fn T cos(T Val)
/// \brief Returns the cosine of the input value, \a Val.
/// \param Val The input value.

_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_cos)
half cos(half);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_cos)
half2 cos(half2);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_cos)
half3 cos(half3);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_cos)
half4 cos(half4);

_HLSL_BUILTIN_ALIAS(__builtin_elementwise_cos)
float cos(float);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_cos)
float2 cos(float2);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_cos)
float3 cos(float3);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_cos)
float4 cos(float4);

//===----------------------------------------------------------------------===//
// cosh builtins
//===----------------------------------------------------------------------===//

/// \fn T cosh(T Val)
/// \brief Returns the hyperbolic cosine of the input value, \a Val.
/// \param Val The input value.

#ifdef __HLSL_ENABLE_16_BIT
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_cosh)
half cosh(half);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_cosh)
half2 cosh(half2);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_cosh)
half3 cosh(half3);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_cosh)
half4 cosh(half4);
#endif

_HLSL_BUILTIN_ALIAS(__builtin_elementwise_cosh)
float cosh(float);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_cosh)
float2 cosh(float2);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_cosh)
float3 cosh(float3);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_cosh)
float4 cosh(float4);

//===----------------------------------------------------------------------===//
// dot product builtins
//===----------------------------------------------------------------------===//

/// \fn K dot(T X, T Y)
/// \brief Return the dot product (a scalar value) of \a X and \a Y.
/// \param X The X input value.
/// \param Y The Y input value.

_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_dot)
half dot(half, half);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_dot)
half dot(half2, half2);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_dot)
half dot(half3, half3);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_dot)
half dot(half4, half4);

#ifdef __HLSL_ENABLE_16_BIT
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_dot)
int16_t dot(int16_t, int16_t);
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_dot)
int16_t dot(int16_t2, int16_t2);
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_dot)
int16_t dot(int16_t3, int16_t3);
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_dot)
int16_t dot(int16_t4, int16_t4);

_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_dot)
uint16_t dot(uint16_t, uint16_t);
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_dot)
uint16_t dot(uint16_t2, uint16_t2);
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_dot)
uint16_t dot(uint16_t3, uint16_t3);
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_dot)
uint16_t dot(uint16_t4, uint16_t4);
#endif

_HLSL_BUILTIN_ALIAS(__builtin_hlsl_dot)
float dot(float, float);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_dot)
float dot(float2, float2);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_dot)
float dot(float3, float3);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_dot)
float dot(float4, float4);

_HLSL_BUILTIN_ALIAS(__builtin_hlsl_dot)
double dot(double, double);

_HLSL_BUILTIN_ALIAS(__builtin_hlsl_dot)
int dot(int, int);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_dot)
int dot(int2, int2);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_dot)
int dot(int3, int3);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_dot)
int dot(int4, int4);

_HLSL_BUILTIN_ALIAS(__builtin_hlsl_dot)
uint dot(uint, uint);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_dot)
uint dot(uint2, uint2);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_dot)
uint dot(uint3, uint3);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_dot)
uint dot(uint4, uint4);

_HLSL_BUILTIN_ALIAS(__builtin_hlsl_dot)
int64_t dot(int64_t, int64_t);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_dot)
int64_t dot(int64_t2, int64_t2);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_dot)
int64_t dot(int64_t3, int64_t3);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_dot)
int64_t dot(int64_t4, int64_t4);

_HLSL_BUILTIN_ALIAS(__builtin_hlsl_dot)
uint64_t dot(uint64_t, uint64_t);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_dot)
uint64_t dot(uint64_t2, uint64_t2);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_dot)
uint64_t dot(uint64_t3, uint64_t3);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_dot)
uint64_t dot(uint64_t4, uint64_t4);

//===----------------------------------------------------------------------===//
// exp builtins
//===----------------------------------------------------------------------===//

/// \fn T exp(T x)
/// \brief Returns the base-e exponential, or \a e**x, of the specified value.
/// \param x The specified input value.
///
/// The return value is the base-e exponential of the \a x parameter.

_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_exp)
half exp(half);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_exp)
half2 exp(half2);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_exp)
half3 exp(half3);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_exp)
half4 exp(half4);

_HLSL_BUILTIN_ALIAS(__builtin_elementwise_exp)
float exp(float);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_exp)
float2 exp(float2);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_exp)
float3 exp(float3);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_exp)
float4 exp(float4);

//===----------------------------------------------------------------------===//
// exp2 builtins
//===----------------------------------------------------------------------===//

/// \fn T exp2(T x)
/// \brief Returns the base 2 exponential, or \a 2**x, of the specified value.
/// \param x The specified input value.
///
/// The base 2 exponential of the \a x parameter.

_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_exp2)
half exp2(half);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_exp2)
half2 exp2(half2);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_exp2)
half3 exp2(half3);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_exp2)
half4 exp2(half4);

_HLSL_BUILTIN_ALIAS(__builtin_elementwise_exp2)
float exp2(float);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_exp2)
float2 exp2(float2);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_exp2)
float3 exp2(float3);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_exp2)
float4 exp2(float4);

//===----------------------------------------------------------------------===//
// floor builtins
//===----------------------------------------------------------------------===//

/// \fn T floor(T Val)
/// \brief Returns the largest integer that is less than or equal to the input
/// value, \a Val.
/// \param Val The input value.

_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_floor)
half floor(half);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_floor)
half2 floor(half2);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_floor)
half3 floor(half3);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_floor)
half4 floor(half4);

_HLSL_BUILTIN_ALIAS(__builtin_elementwise_floor)
float floor(float);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_floor)
float2 floor(float2);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_floor)
float3 floor(float3);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_floor)
float4 floor(float4);

//===----------------------------------------------------------------------===//
// frac builtins
//===----------------------------------------------------------------------===//

/// \fn T frac(T x)
/// \brief Returns the fractional (or decimal) part of x. \a x parameter.
/// \param x The specified input value.
///
/// If \a the return value is greater than or equal to 0 and less than 1.

_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_frac)
half frac(half);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_frac)
half2 frac(half2);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_frac)
half3 frac(half3);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_frac)
half4 frac(half4);

_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_frac)
float frac(float);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_frac)
float2 frac(float2);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_frac)
float3 frac(float3);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_frac)
float4 frac(float4);

//===----------------------------------------------------------------------===//
// isinf builtins
//===----------------------------------------------------------------------===//

/// \fn T isinf(T x)
/// \brief Determines if the specified value \a x  is infinite.
/// \param x The specified input value.
///
/// Returns a value of the same size as the input, with a value set
/// to True if the x parameter is +INF or -INF. Otherwise, False.

_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_isinf)
bool isinf(half);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_isinf)
bool2 isinf(half2);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_isinf)
bool3 isinf(half3);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_isinf)
bool4 isinf(half4);

_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_isinf)
bool isinf(float);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_isinf)
bool2 isinf(float2);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_isinf)
bool3 isinf(float3);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_isinf)
bool4 isinf(float4);

//===----------------------------------------------------------------------===//
// lerp builtins
//===----------------------------------------------------------------------===//

/// \fn T lerp(T x, T y, T s)
/// \brief Returns the linear interpolation of x to y by s.
/// \param x [in] The first-floating point value.
/// \param y [in] The second-floating point value.
/// \param s [in] A value that linearly interpolates between the x parameter and
/// the y parameter.
///
/// Linear interpolation is based on the following formula: x*(1-s) + y*s which
/// can equivalently be written as x + s(y-x).

_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_lerp)
half lerp(half, half, half);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_lerp)
half2 lerp(half2, half2, half2);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_lerp)
half3 lerp(half3, half3, half3);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_lerp)
half4 lerp(half4, half4, half4);

_HLSL_BUILTIN_ALIAS(__builtin_hlsl_lerp)
float lerp(float, float, float);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_lerp)
float2 lerp(float2, float2, float2);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_lerp)
float3 lerp(float3, float3, float3);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_lerp)
float4 lerp(float4, float4, float4);

//===----------------------------------------------------------------------===//
// log builtins
//===----------------------------------------------------------------------===//

/// \fn T log(T Val)
/// \brief The base-e logarithm of the input value, \a Val parameter.
/// \param Val The input value.
///
/// If \a Val is negative, this result is undefined. If \a Val is 0, this
/// function returns negative infinity.

_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_log)
half log(half);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_log)
half2 log(half2);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_log)
half3 log(half3);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_log)
half4 log(half4);

_HLSL_BUILTIN_ALIAS(__builtin_elementwise_log)
float log(float);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_log)
float2 log(float2);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_log)
float3 log(float3);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_log)
float4 log(float4);

//===----------------------------------------------------------------------===//
// log10 builtins
//===----------------------------------------------------------------------===//

/// \fn T log10(T Val)
/// \brief The base-10 logarithm of the input value, \a Val parameter.
/// \param Val The input value.
///
/// If \a Val is negative, this result is undefined. If \a Val is 0, this
/// function returns negative infinity.

_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_log10)
half log10(half);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_log10)
half2 log10(half2);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_log10)
half3 log10(half3);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_log10)
half4 log10(half4);

_HLSL_BUILTIN_ALIAS(__builtin_elementwise_log10)
float log10(float);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_log10)
float2 log10(float2);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_log10)
float3 log10(float3);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_log10)
float4 log10(float4);

//===----------------------------------------------------------------------===//
// log2 builtins
//===----------------------------------------------------------------------===//

/// \fn T log2(T Val)
/// \brief The base-2 logarithm of the input value, \a Val parameter.
/// \param Val The input value.
///
/// If \a Val is negative, this result is undefined. If \a Val is 0, this
/// function returns negative infinity.

_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_log2)
half log2(half);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_log2)
half2 log2(half2);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_log2)
half3 log2(half3);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_log2)
half4 log2(half4);

_HLSL_BUILTIN_ALIAS(__builtin_elementwise_log2)
float log2(float);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_log2)
float2 log2(float2);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_log2)
float3 log2(float3);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_log2)
float4 log2(float4);

//===----------------------------------------------------------------------===//
// mad builtins
//===----------------------------------------------------------------------===//

/// \fn T mad(T M, T A, T B)
/// \brief The result of \a M * \a A + \a B.
/// \param M The multiplication value.
/// \param A The first addition value.
/// \param B The second addition value.

_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_mad)
half mad(half, half, half);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_mad)
half2 mad(half2, half2, half2);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_mad)
half3 mad(half3, half3, half3);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_mad)
half4 mad(half4, half4, half4);

#ifdef __HLSL_ENABLE_16_BIT
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_mad)
int16_t mad(int16_t, int16_t, int16_t);
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_mad)
int16_t2 mad(int16_t2, int16_t2, int16_t2);
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_mad)
int16_t3 mad(int16_t3, int16_t3, int16_t3);
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_mad)
int16_t4 mad(int16_t4, int16_t4, int16_t4);

_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_mad)
uint16_t mad(uint16_t, uint16_t, uint16_t);
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_mad)
uint16_t2 mad(uint16_t2, uint16_t2, uint16_t2);
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_mad)
uint16_t3 mad(uint16_t3, uint16_t3, uint16_t3);
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_mad)
uint16_t4 mad(uint16_t4, uint16_t4, uint16_t4);
#endif

_HLSL_BUILTIN_ALIAS(__builtin_hlsl_mad)
int mad(int, int, int);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_mad)
int2 mad(int2, int2, int2);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_mad)
int3 mad(int3, int3, int3);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_mad)
int4 mad(int4, int4, int4);

_HLSL_BUILTIN_ALIAS(__builtin_hlsl_mad)
uint mad(uint, uint, uint);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_mad)
uint2 mad(uint2, uint2, uint2);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_mad)
uint3 mad(uint3, uint3, uint3);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_mad)
uint4 mad(uint4, uint4, uint4);

_HLSL_BUILTIN_ALIAS(__builtin_hlsl_mad)
int64_t mad(int64_t, int64_t, int64_t);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_mad)
int64_t2 mad(int64_t2, int64_t2, int64_t2);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_mad)
int64_t3 mad(int64_t3, int64_t3, int64_t3);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_mad)
int64_t4 mad(int64_t4, int64_t4, int64_t4);

_HLSL_BUILTIN_ALIAS(__builtin_hlsl_mad)
uint64_t mad(uint64_t, uint64_t, uint64_t);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_mad)
uint64_t2 mad(uint64_t2, uint64_t2, uint64_t2);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_mad)
uint64_t3 mad(uint64_t3, uint64_t3, uint64_t3);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_mad)
uint64_t4 mad(uint64_t4, uint64_t4, uint64_t4);

_HLSL_BUILTIN_ALIAS(__builtin_hlsl_mad)
float mad(float, float, float);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_mad)
float2 mad(float2, float2, float2);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_mad)
float3 mad(float3, float3, float3);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_mad)
float4 mad(float4, float4, float4);

_HLSL_BUILTIN_ALIAS(__builtin_hlsl_mad)
double mad(double, double, double);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_mad)
double2 mad(double2, double2, double2);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_mad)
double3 mad(double3, double3, double3);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_mad)
double4 mad(double4, double4, double4);

//===----------------------------------------------------------------------===//
// max builtins
//===----------------------------------------------------------------------===//

/// \fn T max(T X, T Y)
/// \brief Return the greater of \a X and \a Y.
/// \param X The X input value.
/// \param Y The Y input value.

_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_max)
half max(half, half);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_max)
half2 max(half2, half2);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_max)
half3 max(half3, half3);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_max)
half4 max(half4, half4);

#ifdef __HLSL_ENABLE_16_BIT
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_max)
int16_t max(int16_t, int16_t);
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_max)
int16_t2 max(int16_t2, int16_t2);
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_max)
int16_t3 max(int16_t3, int16_t3);
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_max)
int16_t4 max(int16_t4, int16_t4);

_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_max)
uint16_t max(uint16_t, uint16_t);
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_max)
uint16_t2 max(uint16_t2, uint16_t2);
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_max)
uint16_t3 max(uint16_t3, uint16_t3);
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_max)
uint16_t4 max(uint16_t4, uint16_t4);
#endif

_HLSL_BUILTIN_ALIAS(__builtin_elementwise_max)
int max(int, int);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_max)
int2 max(int2, int2);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_max)
int3 max(int3, int3);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_max)
int4 max(int4, int4);

_HLSL_BUILTIN_ALIAS(__builtin_elementwise_max)
uint max(uint, uint);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_max)
uint2 max(uint2, uint2);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_max)
uint3 max(uint3, uint3);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_max)
uint4 max(uint4, uint4);

_HLSL_BUILTIN_ALIAS(__builtin_elementwise_max)
int64_t max(int64_t, int64_t);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_max)
int64_t2 max(int64_t2, int64_t2);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_max)
int64_t3 max(int64_t3, int64_t3);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_max)
int64_t4 max(int64_t4, int64_t4);

_HLSL_BUILTIN_ALIAS(__builtin_elementwise_max)
uint64_t max(uint64_t, uint64_t);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_max)
uint64_t2 max(uint64_t2, uint64_t2);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_max)
uint64_t3 max(uint64_t3, uint64_t3);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_max)
uint64_t4 max(uint64_t4, uint64_t4);

_HLSL_BUILTIN_ALIAS(__builtin_elementwise_max)
float max(float, float);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_max)
float2 max(float2, float2);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_max)
float3 max(float3, float3);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_max)
float4 max(float4, float4);

_HLSL_BUILTIN_ALIAS(__builtin_elementwise_max)
double max(double, double);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_max)
double2 max(double2, double2);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_max)
double3 max(double3, double3);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_max)
double4 max(double4, double4);

//===----------------------------------------------------------------------===//
// min builtins
//===----------------------------------------------------------------------===//

/// \fn T min(T X, T Y)
/// \brief Return the lesser of \a X and \a Y.
/// \param X The X input value.
/// \param Y The Y input value.

_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_min)
half min(half, half);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_min)
half2 min(half2, half2);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_min)
half3 min(half3, half3);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_min)
half4 min(half4, half4);

#ifdef __HLSL_ENABLE_16_BIT
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_min)
int16_t min(int16_t, int16_t);
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_min)
int16_t2 min(int16_t2, int16_t2);
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_min)
int16_t3 min(int16_t3, int16_t3);
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_min)
int16_t4 min(int16_t4, int16_t4);

_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_min)
uint16_t min(uint16_t, uint16_t);
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_min)
uint16_t2 min(uint16_t2, uint16_t2);
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_min)
uint16_t3 min(uint16_t3, uint16_t3);
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_min)
uint16_t4 min(uint16_t4, uint16_t4);
#endif

_HLSL_BUILTIN_ALIAS(__builtin_elementwise_min)
int min(int, int);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_min)
int2 min(int2, int2);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_min)
int3 min(int3, int3);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_min)
int4 min(int4, int4);

_HLSL_BUILTIN_ALIAS(__builtin_elementwise_min)
uint min(uint, uint);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_min)
uint2 min(uint2, uint2);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_min)
uint3 min(uint3, uint3);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_min)
uint4 min(uint4, uint4);

_HLSL_BUILTIN_ALIAS(__builtin_elementwise_min)
float min(float, float);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_min)
float2 min(float2, float2);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_min)
float3 min(float3, float3);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_min)
float4 min(float4, float4);

_HLSL_BUILTIN_ALIAS(__builtin_elementwise_min)
int64_t min(int64_t, int64_t);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_min)
int64_t2 min(int64_t2, int64_t2);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_min)
int64_t3 min(int64_t3, int64_t3);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_min)
int64_t4 min(int64_t4, int64_t4);

_HLSL_BUILTIN_ALIAS(__builtin_elementwise_min)
uint64_t min(uint64_t, uint64_t);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_min)
uint64_t2 min(uint64_t2, uint64_t2);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_min)
uint64_t3 min(uint64_t3, uint64_t3);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_min)
uint64_t4 min(uint64_t4, uint64_t4);

_HLSL_BUILTIN_ALIAS(__builtin_elementwise_min)
double min(double, double);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_min)
double2 min(double2, double2);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_min)
double3 min(double3, double3);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_min)
double4 min(double4, double4);

//===----------------------------------------------------------------------===//
// pow builtins
//===----------------------------------------------------------------------===//

/// \fn T pow(T Val, T Pow)
/// \brief Return the value \a Val, raised to the power \a Pow.
/// \param Val The input value.
/// \param Pow The specified power.

_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_pow)
half pow(half, half);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_pow)
half2 pow(half2, half2);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_pow)
half3 pow(half3, half3);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_pow)
half4 pow(half4, half4);

_HLSL_BUILTIN_ALIAS(__builtin_elementwise_pow)
float pow(float, float);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_pow)
float2 pow(float2, float2);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_pow)
float3 pow(float3, float3);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_pow)
float4 pow(float4, float4);

//===----------------------------------------------------------------------===//
// reversebits builtins
//===----------------------------------------------------------------------===//

/// \fn T reversebits(T Val)
/// \brief Return the value \a Val with the bit order reversed.
/// \param Val The input value.

#ifdef __HLSL_ENABLE_16_BIT
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_bitreverse)
uint16_t reversebits(uint16_t);
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_bitreverse)
uint16_t2 reversebits(uint16_t2);
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_bitreverse)
uint16_t3 reversebits(uint16_t3);
_HLSL_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_bitreverse)
uint16_t4 reversebits(uint16_t4);
#endif

_HLSL_BUILTIN_ALIAS(__builtin_elementwise_bitreverse)
uint reversebits(uint);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_bitreverse)
uint2 reversebits(uint2);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_bitreverse)
uint3 reversebits(uint3);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_bitreverse)
uint4 reversebits(uint4);

_HLSL_BUILTIN_ALIAS(__builtin_elementwise_bitreverse)
uint64_t reversebits(uint64_t);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_bitreverse)
uint64_t2 reversebits(uint64_t2);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_bitreverse)
uint64_t3 reversebits(uint64_t3);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_bitreverse)
uint64_t4 reversebits(uint64_t4);

//===----------------------------------------------------------------------===//
// rcp builtins
//===----------------------------------------------------------------------===//

/// \fn T rcp(T x)
/// \brief Calculates a fast, approximate, per-component reciprocal ie 1 / \a x.
/// \param x The specified input value.
///
/// The return value is the reciprocal of the \a x parameter.

_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_rcp)
half rcp(half);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_rcp)
half2 rcp(half2);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_rcp)
half3 rcp(half3);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_rcp)
half4 rcp(half4);

_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_rcp)
float rcp(float);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_rcp)
float2 rcp(float2);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_rcp)
float3 rcp(float3);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_rcp)
float4 rcp(float4);

_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_rcp)
double rcp(double);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_rcp)
double2 rcp(double2);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_rcp)
double3 rcp(double3);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_rcp)
double4 rcp(double4);

//===----------------------------------------------------------------------===//
// rsqrt builtins
//===----------------------------------------------------------------------===//

/// \fn T rsqrt(T x)
/// \brief Returns the reciprocal of the square root of the specified value.
/// ie 1 / sqrt( \a x).
/// \param x The specified input value.
///
/// This function uses the following formula: 1 / sqrt(x).

_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_rsqrt)
half rsqrt(half);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_rsqrt)
half2 rsqrt(half2);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_rsqrt)
half3 rsqrt(half3);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_rsqrt)
half4 rsqrt(half4);

_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_rsqrt)
float rsqrt(float);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_rsqrt)
float2 rsqrt(float2);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_rsqrt)
float3 rsqrt(float3);
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_elementwise_rsqrt)
float4 rsqrt(float4);

//===----------------------------------------------------------------------===//
// round builtins
//===----------------------------------------------------------------------===//

/// \fn T round(T x)
/// \brief Rounds the specified value \a x to the nearest integer.
/// \param x The specified input value.
///
/// The return value is the \a x parameter, rounded to the nearest integer
/// within a floating-point type. Halfway cases are
/// rounded to the nearest even value.

_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_roundeven)
half round(half);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_roundeven)
half2 round(half2);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_roundeven)
half3 round(half3);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_roundeven)
half4 round(half4);

_HLSL_BUILTIN_ALIAS(__builtin_elementwise_roundeven)
float round(float);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_roundeven)
float2 round(float2);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_roundeven)
float3 round(float3);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_roundeven)
float4 round(float4);

//===----------------------------------------------------------------------===//
// sin builtins
//===----------------------------------------------------------------------===//

/// \fn T sin(T Val)
/// \brief Returns the sine of the input value, \a Val.
/// \param Val The input value.

_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_sin)
half sin(half);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_sin)
half2 sin(half2);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_sin)
half3 sin(half3);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_sin)
half4 sin(half4);

_HLSL_BUILTIN_ALIAS(__builtin_elementwise_sin)
float sin(float);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_sin)
float2 sin(float2);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_sin)
float3 sin(float3);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_sin)
float4 sin(float4);

//===----------------------------------------------------------------------===//
// sinh builtins
//===----------------------------------------------------------------------===//

/// \fn T sinh(T Val)
/// \brief Returns the hyperbolic sine of the input value, \a Val.
/// \param Val The input value.

#ifdef __HLSL_ENABLE_16_BIT
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_sinh)
half sinh(half);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_sinh)
half2 sinh(half2);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_sinh)
half3 sinh(half3);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_sinh)
half4 sinh(half4);
#endif

_HLSL_BUILTIN_ALIAS(__builtin_elementwise_sinh)
float sinh(float);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_sinh)
float2 sinh(float2);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_sinh)
float3 sinh(float3);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_sinh)
float4 sinh(float4);

//===----------------------------------------------------------------------===//
// sqrt builtins
//===----------------------------------------------------------------------===//

/// \fn T sqrt(T Val)
/// \brief Returns the square root of the input value, \a Val.
/// \param Val The input value.

_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_sqrt)
half sqrt(half);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_sqrt)
half2 sqrt(half2);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_sqrt)
half3 sqrt(half3);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_sqrt)
half4 sqrt(half4);

_HLSL_BUILTIN_ALIAS(__builtin_elementwise_sqrt)
float sqrt(float);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_sqrt)
float2 sqrt(float2);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_sqrt)
float3 sqrt(float3);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_sqrt)
float4 sqrt(float4);

//===----------------------------------------------------------------------===//
// tan builtins
//===----------------------------------------------------------------------===//

/// \fn T tan(T Val)
/// \brief Returns the tangent of the input value, \a Val.
/// \param Val The input value.

#ifdef __HLSL_ENABLE_16_BIT
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_tan)
half tan(half);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_tan)
half2 tan(half2);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_tan)
half3 tan(half3);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_tan)
half4 tan(half4);
#endif

_HLSL_BUILTIN_ALIAS(__builtin_elementwise_tan)
float tan(float);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_tan)
float2 tan(float2);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_tan)
float3 tan(float3);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_tan)
float4 tan(float4);

//===----------------------------------------------------------------------===//
// tanh builtins
//===----------------------------------------------------------------------===//

/// \fn T tanh(T Val)
/// \brief Returns the hyperbolic tangent of the input value, \a Val.
/// \param Val The input value.

#ifdef __HLSL_ENABLE_16_BIT
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_tanh)
half tanh(half);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_tanh)
half2 tanh(half2);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_tanh)
half3 tanh(half3);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_tanh)
half4 tanh(half4);
#endif

_HLSL_BUILTIN_ALIAS(__builtin_elementwise_tanh)
float tanh(float);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_tanh)
float2 tanh(float2);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_tanh)
float3 tanh(float3);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_tanh)
float4 tanh(float4);

//===----------------------------------------------------------------------===//
// trunc builtins
//===----------------------------------------------------------------------===//

/// \fn T trunc(T Val)
/// \brief Returns the truncated integer value of the input value, \a Val.
/// \param Val The input value.

_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_trunc)
half trunc(half);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_trunc)
half2 trunc(half2);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_trunc)
half3 trunc(half3);
_HLSL_16BIT_AVAILABILITY(shadermodel, 6.2)
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_trunc)
half4 trunc(half4);

_HLSL_BUILTIN_ALIAS(__builtin_elementwise_trunc)
float trunc(float);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_trunc)
float2 trunc(float2);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_trunc)
float3 trunc(float3);
_HLSL_BUILTIN_ALIAS(__builtin_elementwise_trunc)
float4 trunc(float4);

//===----------------------------------------------------------------------===//
// Wave* builtins
//===----------------------------------------------------------------------===//

/// \brief Counts the number of boolean variables which evaluate to true across
/// all active lanes in the current wave.
///
/// \param Val The input boolean value.
/// \return The number of lanes for which the boolean variable evaluates to
/// true, across all active lanes in the current wave.
_HLSL_AVAILABILITY(shadermodel, 6.0)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_wave_active_count_bits)
__attribute__((convergent)) uint WaveActiveCountBits(bool Val);

/// \brief Returns the index of the current lane within the current wave.
_HLSL_AVAILABILITY(shadermodel, 6.0)
_HLSL_BUILTIN_ALIAS(__builtin_hlsl_wave_get_lane_index)
__attribute__((convergent)) uint WaveGetLaneIndex();

} // namespace hlsl
#endif //_HLSL_HLSL_INTRINSICS_H_
