//===----- hlsl_basic_types.h - HLSL definitions for basic types ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _HLSL_HLSL_BASIC_TYPES_H_
#define _HLSL_HLSL_BASIC_TYPES_H_

namespace hlsl {
// built-in scalar data types:

/// \typedef template<typename Ty, int Size> using vector = Ty
/// __attribute__((ext_vector_type(Size)))
///
/// \tparam Ty The base type of the vector may be any builtin integral or
/// floating point type.
/// \tparam Size The size of the vector may be any value between 1 and 4.

#ifdef __HLSL_ENABLE_16_BIT
// 16-bit integer.
typedef unsigned short uint16_t;
typedef short int16_t;
#endif

// unsigned 32-bit integer.
typedef unsigned int uint;

// 64-bit integer.
typedef unsigned long uint64_t;
typedef long int64_t;

// built-in vector data types:

#ifdef __HLSL_ENABLE_16_BIT
typedef vector<int16_t, 2> int16_t2;
typedef vector<int16_t, 3> int16_t3;
typedef vector<int16_t, 4> int16_t4;
typedef vector<uint16_t, 2> uint16_t2;
typedef vector<uint16_t, 3> uint16_t3;
typedef vector<uint16_t, 4> uint16_t4;
#endif
typedef vector<bool, 2> bool2;
typedef vector<bool, 3> bool3;
typedef vector<bool, 4> bool4;
typedef vector<int, 2> int2;
typedef vector<int, 3> int3;
typedef vector<int, 4> int4;
typedef vector<uint, 2> uint2;
typedef vector<uint, 3> uint3;
typedef vector<uint, 4> uint4;
typedef vector<int64_t, 2> int64_t2;
typedef vector<int64_t, 3> int64_t3;
typedef vector<int64_t, 4> int64_t4;
typedef vector<uint64_t, 2> uint64_t2;
typedef vector<uint64_t, 3> uint64_t3;
typedef vector<uint64_t, 4> uint64_t4;

typedef vector<half, 2> half2;
typedef vector<half, 3> half3;
typedef vector<half, 4> half4;

typedef vector<float, 2> float2;
typedef vector<float, 3> float3;
typedef vector<float, 4> float4;
typedef vector<double, 2> double2;
typedef vector<double, 3> double3;
typedef vector<double, 4> double4;

} // namespace hlsl

#endif //_HLSL_HLSL_BASIC_TYPES_H_
