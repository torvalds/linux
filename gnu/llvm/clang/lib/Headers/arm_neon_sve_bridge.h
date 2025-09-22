/*===---- arm_neon_sve_bridge.h - ARM NEON SVE Bridge intrinsics -----------===
 *
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __ARM_NEON_SVE_BRIDGE_H
#define __ARM_NEON_SVE_BRIDGE_H

#include <arm_neon.h>
#include <arm_sve.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Function attributes */
#define __ai static __inline__ __attribute__((__always_inline__, __nodebug__))
#define __aio                                                                  \
  static __inline__                                                            \
      __attribute__((__always_inline__, __nodebug__, __overloadable__))

__aio __attribute__((__clang_arm_builtin_alias(__builtin_sve_svset_neonq_s8)))
svint8_t svset_neonq(svint8_t, int8x16_t);
__aio __attribute__((__clang_arm_builtin_alias(__builtin_sve_svset_neonq_s16)))
svint16_t svset_neonq(svint16_t, int16x8_t);
__aio __attribute__((__clang_arm_builtin_alias(__builtin_sve_svset_neonq_s32)))
svint32_t svset_neonq(svint32_t, int32x4_t);
__aio __attribute__((__clang_arm_builtin_alias(__builtin_sve_svset_neonq_s64)))
svint64_t svset_neonq(svint64_t, int64x2_t);
__aio __attribute__((__clang_arm_builtin_alias(__builtin_sve_svset_neonq_u8)))
svuint8_t svset_neonq(svuint8_t, uint8x16_t);
__aio __attribute__((__clang_arm_builtin_alias(__builtin_sve_svset_neonq_u16)))
svuint16_t svset_neonq(svuint16_t, uint16x8_t);
__aio __attribute__((__clang_arm_builtin_alias(__builtin_sve_svset_neonq_u32)))
svuint32_t svset_neonq(svuint32_t, uint32x4_t);
__aio __attribute__((__clang_arm_builtin_alias(__builtin_sve_svset_neonq_u64)))
svuint64_t svset_neonq(svuint64_t, uint64x2_t);
__aio __attribute__((__clang_arm_builtin_alias(__builtin_sve_svset_neonq_f16)))
svfloat16_t svset_neonq(svfloat16_t, float16x8_t);
__aio __attribute__((__clang_arm_builtin_alias(__builtin_sve_svset_neonq_f32)))
svfloat32_t svset_neonq(svfloat32_t, float32x4_t);
__aio __attribute__((__clang_arm_builtin_alias(__builtin_sve_svset_neonq_f64)))
svfloat64_t svset_neonq(svfloat64_t, float64x2_t);
__ai __attribute__((__clang_arm_builtin_alias(__builtin_sve_svset_neonq_s8)))
svint8_t svset_neonq_s8(svint8_t, int8x16_t);
__ai __attribute__((__clang_arm_builtin_alias(__builtin_sve_svset_neonq_s16)))
svint16_t svset_neonq_s16(svint16_t, int16x8_t);
__ai __attribute__((__clang_arm_builtin_alias(__builtin_sve_svset_neonq_s32)))
svint32_t svset_neonq_s32(svint32_t, int32x4_t);
__ai __attribute__((__clang_arm_builtin_alias(__builtin_sve_svset_neonq_s64)))
svint64_t svset_neonq_s64(svint64_t, int64x2_t);
__ai __attribute__((__clang_arm_builtin_alias(__builtin_sve_svset_neonq_u8)))
svuint8_t svset_neonq_u8(svuint8_t, uint8x16_t);
__ai __attribute__((__clang_arm_builtin_alias(__builtin_sve_svset_neonq_u16)))
svuint16_t svset_neonq_u16(svuint16_t, uint16x8_t);
__ai __attribute__((__clang_arm_builtin_alias(__builtin_sve_svset_neonq_u32)))
svuint32_t svset_neonq_u32(svuint32_t, uint32x4_t);
__ai __attribute__((__clang_arm_builtin_alias(__builtin_sve_svset_neonq_u64)))
svuint64_t svset_neonq_u64(svuint64_t, uint64x2_t);
__ai __attribute__((__clang_arm_builtin_alias(__builtin_sve_svset_neonq_f16)))
svfloat16_t svset_neonq_f16(svfloat16_t, float16x8_t);
__ai __attribute__((__clang_arm_builtin_alias(__builtin_sve_svset_neonq_f32)))
svfloat32_t svset_neonq_f32(svfloat32_t, float32x4_t);
__ai __attribute__((__clang_arm_builtin_alias(__builtin_sve_svset_neonq_f64)))
svfloat64_t svset_neonq_f64(svfloat64_t, float64x2_t);

__aio __attribute__((__clang_arm_builtin_alias(__builtin_sve_svget_neonq_s8)))
int8x16_t svget_neonq(svint8_t);
__aio __attribute__((__clang_arm_builtin_alias(__builtin_sve_svget_neonq_s16)))
int16x8_t svget_neonq(svint16_t);
__aio __attribute__((__clang_arm_builtin_alias(__builtin_sve_svget_neonq_s32)))
int32x4_t svget_neonq(svint32_t);
__aio __attribute__((__clang_arm_builtin_alias(__builtin_sve_svget_neonq_s64)))
int64x2_t svget_neonq(svint64_t);
__aio __attribute__((__clang_arm_builtin_alias(__builtin_sve_svget_neonq_u8)))
uint8x16_t svget_neonq(svuint8_t);
__aio __attribute__((__clang_arm_builtin_alias(__builtin_sve_svget_neonq_u16)))
uint16x8_t svget_neonq(svuint16_t);
__aio __attribute__((__clang_arm_builtin_alias(__builtin_sve_svget_neonq_u32)))
uint32x4_t svget_neonq(svuint32_t);
__aio __attribute__((__clang_arm_builtin_alias(__builtin_sve_svget_neonq_u64)))
uint64x2_t svget_neonq(svuint64_t);
__aio __attribute__((__clang_arm_builtin_alias(__builtin_sve_svget_neonq_f16)))
float16x8_t svget_neonq(svfloat16_t);
__aio __attribute__((__clang_arm_builtin_alias(__builtin_sve_svget_neonq_f32)))
float32x4_t svget_neonq(svfloat32_t);
__aio __attribute__((__clang_arm_builtin_alias(__builtin_sve_svget_neonq_f64)))
float64x2_t svget_neonq(svfloat64_t);
__aio __attribute__((__clang_arm_builtin_alias(__builtin_sve_svget_neonq_s8)))
int8x16_t svget_neonq_s8(svint8_t);
__ai __attribute__((__clang_arm_builtin_alias(__builtin_sve_svget_neonq_s16)))
int16x8_t svget_neonq_s16(svint16_t);
__ai __attribute__((__clang_arm_builtin_alias(__builtin_sve_svget_neonq_s32)))
int32x4_t svget_neonq_s32(svint32_t);
__ai __attribute__((__clang_arm_builtin_alias(__builtin_sve_svget_neonq_s64)))
int64x2_t svget_neonq_s64(svint64_t);
__ai __attribute__((__clang_arm_builtin_alias(__builtin_sve_svget_neonq_u8)))
uint8x16_t svget_neonq_u8(svuint8_t);
__ai __attribute__((__clang_arm_builtin_alias(__builtin_sve_svget_neonq_u16)))
uint16x8_t svget_neonq_u16(svuint16_t);
__ai __attribute__((__clang_arm_builtin_alias(__builtin_sve_svget_neonq_u32)))
uint32x4_t svget_neonq_u32(svuint32_t);
__ai __attribute__((__clang_arm_builtin_alias(__builtin_sve_svget_neonq_u64)))
uint64x2_t svget_neonq_u64(svuint64_t);
__ai __attribute__((__clang_arm_builtin_alias(__builtin_sve_svget_neonq_f16)))
float16x8_t svget_neonq_f16(svfloat16_t);
__ai __attribute__((__clang_arm_builtin_alias(__builtin_sve_svget_neonq_f32)))
float32x4_t svget_neonq_f32(svfloat32_t);
__ai __attribute__((__clang_arm_builtin_alias(__builtin_sve_svget_neonq_f64)))
float64x2_t svget_neonq_f64(svfloat64_t);

__aio __attribute__((__clang_arm_builtin_alias(__builtin_sve_svdup_neonq_s8)))
svint8_t svdup_neonq(int8x16_t);
__aio __attribute__((__clang_arm_builtin_alias(__builtin_sve_svdup_neonq_s16)))
svint16_t svdup_neonq(int16x8_t);
__aio __attribute__((__clang_arm_builtin_alias(__builtin_sve_svdup_neonq_s32)))
svint32_t svdup_neonq(int32x4_t);
__aio __attribute__((__clang_arm_builtin_alias(__builtin_sve_svdup_neonq_s64)))
svint64_t svdup_neonq(int64x2_t);
__aio __attribute__((__clang_arm_builtin_alias(__builtin_sve_svdup_neonq_u8)))
svuint8_t svdup_neonq(uint8x16_t);
__aio __attribute__((__clang_arm_builtin_alias(__builtin_sve_svdup_neonq_u16)))
svuint16_t svdup_neonq(uint16x8_t);
__aio __attribute__((__clang_arm_builtin_alias(__builtin_sve_svdup_neonq_u32)))
svuint32_t svdup_neonq(uint32x4_t);
__aio __attribute__((__clang_arm_builtin_alias(__builtin_sve_svdup_neonq_u64)))
svuint64_t svdup_neonq(uint64x2_t);
__aio __attribute__((__clang_arm_builtin_alias(__builtin_sve_svdup_neonq_f16)))
svfloat16_t svdup_neonq(float16x8_t);
__aio __attribute__((__clang_arm_builtin_alias(__builtin_sve_svdup_neonq_f32)))
svfloat32_t svdup_neonq(float32x4_t);
__aio __attribute__((__clang_arm_builtin_alias(__builtin_sve_svdup_neonq_f64)))
svfloat64_t svdup_neonq(float64x2_t);
__ai __attribute__((__clang_arm_builtin_alias(__builtin_sve_svdup_neonq_s8)))
svint8_t svdup_neonq_s8(int8x16_t);
__ai __attribute__((__clang_arm_builtin_alias(__builtin_sve_svdup_neonq_s16)))
svint16_t svdup_neonq_s16(int16x8_t);
__ai __attribute__((__clang_arm_builtin_alias(__builtin_sve_svdup_neonq_s32)))
svint32_t svdup_neonq_s32(int32x4_t);
__ai __attribute__((__clang_arm_builtin_alias(__builtin_sve_svdup_neonq_s64)))
svint64_t svdup_neonq_s64(int64x2_t);
__ai __attribute__((__clang_arm_builtin_alias(__builtin_sve_svdup_neonq_u8)))
svuint8_t svdup_neonq_u8(uint8x16_t);
__ai __attribute__((__clang_arm_builtin_alias(__builtin_sve_svdup_neonq_u16)))
svuint16_t svdup_neonq_u16(uint16x8_t);
__ai __attribute__((__clang_arm_builtin_alias(__builtin_sve_svdup_neonq_u32)))
svuint32_t svdup_neonq_u32(uint32x4_t);
__ai __attribute__((__clang_arm_builtin_alias(__builtin_sve_svdup_neonq_u64)))
svuint64_t svdup_neonq_u64(uint64x2_t);
__ai __attribute__((__clang_arm_builtin_alias(__builtin_sve_svdup_neonq_f16)))
svfloat16_t svdup_neonq_f16(float16x8_t);
__ai __attribute__((__clang_arm_builtin_alias(__builtin_sve_svdup_neonq_f32)))
svfloat32_t svdup_neonq_f32(float32x4_t);
__ai __attribute__((__clang_arm_builtin_alias(__builtin_sve_svdup_neonq_f64)))
svfloat64_t svdup_neonq_f64(float64x2_t);

__aio __attribute__((__clang_arm_builtin_alias(__builtin_sve_svset_neonq_bf16)))
svbfloat16_t svset_neonq(svbfloat16_t, bfloat16x8_t);
__ai __attribute__((__clang_arm_builtin_alias(__builtin_sve_svset_neonq_bf16)))
svbfloat16_t svset_neonq_bf16(svbfloat16_t, bfloat16x8_t);
__aio __attribute__((__clang_arm_builtin_alias(__builtin_sve_svget_neonq_bf16)))
bfloat16x8_t svget_neonq(svbfloat16_t);
__ai __attribute__((__clang_arm_builtin_alias(__builtin_sve_svget_neonq_bf16)))
bfloat16x8_t svget_neonq_bf16(svbfloat16_t);
__aio __attribute__((__clang_arm_builtin_alias(__builtin_sve_svdup_neonq_bf16)))
svbfloat16_t svdup_neonq(bfloat16x8_t);
__ai __attribute__((__clang_arm_builtin_alias(__builtin_sve_svdup_neonq_bf16)))
svbfloat16_t svdup_neonq_bf16(bfloat16x8_t);

#undef __ai
#undef __aio

#ifdef __cplusplus
} // extern "C"
#endif

#endif //__ARM_NEON_SVE_BRIDGE_H
