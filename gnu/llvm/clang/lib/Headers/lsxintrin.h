/*===------------- lsxintrin.h - LoongArch LSX intrinsics ------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef _LOONGSON_SXINTRIN_H
#define _LOONGSON_SXINTRIN_H 1

#if defined(__loongarch_sx)
typedef signed char v16i8 __attribute__((vector_size(16), aligned(16)));
typedef signed char v16i8_b __attribute__((vector_size(16), aligned(1)));
typedef unsigned char v16u8 __attribute__((vector_size(16), aligned(16)));
typedef unsigned char v16u8_b __attribute__((vector_size(16), aligned(1)));
typedef short v8i16 __attribute__((vector_size(16), aligned(16)));
typedef short v8i16_h __attribute__((vector_size(16), aligned(2)));
typedef unsigned short v8u16 __attribute__((vector_size(16), aligned(16)));
typedef unsigned short v8u16_h __attribute__((vector_size(16), aligned(2)));
typedef int v4i32 __attribute__((vector_size(16), aligned(16)));
typedef int v4i32_w __attribute__((vector_size(16), aligned(4)));
typedef unsigned int v4u32 __attribute__((vector_size(16), aligned(16)));
typedef unsigned int v4u32_w __attribute__((vector_size(16), aligned(4)));
typedef long long v2i64 __attribute__((vector_size(16), aligned(16)));
typedef long long v2i64_d __attribute__((vector_size(16), aligned(8)));
typedef unsigned long long v2u64 __attribute__((vector_size(16), aligned(16)));
typedef unsigned long long v2u64_d __attribute__((vector_size(16), aligned(8)));
typedef float v4f32 __attribute__((vector_size(16), aligned(16)));
typedef float v4f32_w __attribute__((vector_size(16), aligned(4)));
typedef double v2f64 __attribute__((vector_size(16), aligned(16)));
typedef double v2f64_d __attribute__((vector_size(16), aligned(8)));

typedef long long __m128i __attribute__((__vector_size__(16), __may_alias__));
typedef float __m128 __attribute__((__vector_size__(16), __may_alias__));
typedef double __m128d __attribute__((__vector_size__(16), __may_alias__));

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsll_b(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsll_b((v16i8)_1, (v16i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsll_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsll_h((v8i16)_1, (v8i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsll_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsll_w((v4i32)_1, (v4i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsll_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsll_d((v2i64)_1, (v2i64)_2);
}

#define __lsx_vslli_b(/*__m128i*/ _1, /*ui3*/ _2)                              \
  ((__m128i)__builtin_lsx_vslli_b((v16i8)(_1), (_2)))

#define __lsx_vslli_h(/*__m128i*/ _1, /*ui4*/ _2)                              \
  ((__m128i)__builtin_lsx_vslli_h((v8i16)(_1), (_2)))

#define __lsx_vslli_w(/*__m128i*/ _1, /*ui5*/ _2)                              \
  ((__m128i)__builtin_lsx_vslli_w((v4i32)(_1), (_2)))

#define __lsx_vslli_d(/*__m128i*/ _1, /*ui6*/ _2)                              \
  ((__m128i)__builtin_lsx_vslli_d((v2i64)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsra_b(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsra_b((v16i8)_1, (v16i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsra_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsra_h((v8i16)_1, (v8i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsra_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsra_w((v4i32)_1, (v4i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsra_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsra_d((v2i64)_1, (v2i64)_2);
}

#define __lsx_vsrai_b(/*__m128i*/ _1, /*ui3*/ _2)                              \
  ((__m128i)__builtin_lsx_vsrai_b((v16i8)(_1), (_2)))

#define __lsx_vsrai_h(/*__m128i*/ _1, /*ui4*/ _2)                              \
  ((__m128i)__builtin_lsx_vsrai_h((v8i16)(_1), (_2)))

#define __lsx_vsrai_w(/*__m128i*/ _1, /*ui5*/ _2)                              \
  ((__m128i)__builtin_lsx_vsrai_w((v4i32)(_1), (_2)))

#define __lsx_vsrai_d(/*__m128i*/ _1, /*ui6*/ _2)                              \
  ((__m128i)__builtin_lsx_vsrai_d((v2i64)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsrar_b(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsrar_b((v16i8)_1, (v16i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsrar_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsrar_h((v8i16)_1, (v8i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsrar_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsrar_w((v4i32)_1, (v4i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsrar_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsrar_d((v2i64)_1, (v2i64)_2);
}

#define __lsx_vsrari_b(/*__m128i*/ _1, /*ui3*/ _2)                             \
  ((__m128i)__builtin_lsx_vsrari_b((v16i8)(_1), (_2)))

#define __lsx_vsrari_h(/*__m128i*/ _1, /*ui4*/ _2)                             \
  ((__m128i)__builtin_lsx_vsrari_h((v8i16)(_1), (_2)))

#define __lsx_vsrari_w(/*__m128i*/ _1, /*ui5*/ _2)                             \
  ((__m128i)__builtin_lsx_vsrari_w((v4i32)(_1), (_2)))

#define __lsx_vsrari_d(/*__m128i*/ _1, /*ui6*/ _2)                             \
  ((__m128i)__builtin_lsx_vsrari_d((v2i64)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsrl_b(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsrl_b((v16i8)_1, (v16i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsrl_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsrl_h((v8i16)_1, (v8i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsrl_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsrl_w((v4i32)_1, (v4i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsrl_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsrl_d((v2i64)_1, (v2i64)_2);
}

#define __lsx_vsrli_b(/*__m128i*/ _1, /*ui3*/ _2)                              \
  ((__m128i)__builtin_lsx_vsrli_b((v16i8)(_1), (_2)))

#define __lsx_vsrli_h(/*__m128i*/ _1, /*ui4*/ _2)                              \
  ((__m128i)__builtin_lsx_vsrli_h((v8i16)(_1), (_2)))

#define __lsx_vsrli_w(/*__m128i*/ _1, /*ui5*/ _2)                              \
  ((__m128i)__builtin_lsx_vsrli_w((v4i32)(_1), (_2)))

#define __lsx_vsrli_d(/*__m128i*/ _1, /*ui6*/ _2)                              \
  ((__m128i)__builtin_lsx_vsrli_d((v2i64)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsrlr_b(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsrlr_b((v16i8)_1, (v16i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsrlr_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsrlr_h((v8i16)_1, (v8i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsrlr_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsrlr_w((v4i32)_1, (v4i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsrlr_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsrlr_d((v2i64)_1, (v2i64)_2);
}

#define __lsx_vsrlri_b(/*__m128i*/ _1, /*ui3*/ _2)                             \
  ((__m128i)__builtin_lsx_vsrlri_b((v16i8)(_1), (_2)))

#define __lsx_vsrlri_h(/*__m128i*/ _1, /*ui4*/ _2)                             \
  ((__m128i)__builtin_lsx_vsrlri_h((v8i16)(_1), (_2)))

#define __lsx_vsrlri_w(/*__m128i*/ _1, /*ui5*/ _2)                             \
  ((__m128i)__builtin_lsx_vsrlri_w((v4i32)(_1), (_2)))

#define __lsx_vsrlri_d(/*__m128i*/ _1, /*ui6*/ _2)                             \
  ((__m128i)__builtin_lsx_vsrlri_d((v2i64)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vbitclr_b(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vbitclr_b((v16u8)_1, (v16u8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vbitclr_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vbitclr_h((v8u16)_1, (v8u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vbitclr_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vbitclr_w((v4u32)_1, (v4u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vbitclr_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vbitclr_d((v2u64)_1, (v2u64)_2);
}

#define __lsx_vbitclri_b(/*__m128i*/ _1, /*ui3*/ _2)                           \
  ((__m128i)__builtin_lsx_vbitclri_b((v16u8)(_1), (_2)))

#define __lsx_vbitclri_h(/*__m128i*/ _1, /*ui4*/ _2)                           \
  ((__m128i)__builtin_lsx_vbitclri_h((v8u16)(_1), (_2)))

#define __lsx_vbitclri_w(/*__m128i*/ _1, /*ui5*/ _2)                           \
  ((__m128i)__builtin_lsx_vbitclri_w((v4u32)(_1), (_2)))

#define __lsx_vbitclri_d(/*__m128i*/ _1, /*ui6*/ _2)                           \
  ((__m128i)__builtin_lsx_vbitclri_d((v2u64)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vbitset_b(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vbitset_b((v16u8)_1, (v16u8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vbitset_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vbitset_h((v8u16)_1, (v8u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vbitset_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vbitset_w((v4u32)_1, (v4u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vbitset_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vbitset_d((v2u64)_1, (v2u64)_2);
}

#define __lsx_vbitseti_b(/*__m128i*/ _1, /*ui3*/ _2)                           \
  ((__m128i)__builtin_lsx_vbitseti_b((v16u8)(_1), (_2)))

#define __lsx_vbitseti_h(/*__m128i*/ _1, /*ui4*/ _2)                           \
  ((__m128i)__builtin_lsx_vbitseti_h((v8u16)(_1), (_2)))

#define __lsx_vbitseti_w(/*__m128i*/ _1, /*ui5*/ _2)                           \
  ((__m128i)__builtin_lsx_vbitseti_w((v4u32)(_1), (_2)))

#define __lsx_vbitseti_d(/*__m128i*/ _1, /*ui6*/ _2)                           \
  ((__m128i)__builtin_lsx_vbitseti_d((v2u64)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vbitrev_b(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vbitrev_b((v16u8)_1, (v16u8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vbitrev_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vbitrev_h((v8u16)_1, (v8u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vbitrev_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vbitrev_w((v4u32)_1, (v4u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vbitrev_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vbitrev_d((v2u64)_1, (v2u64)_2);
}

#define __lsx_vbitrevi_b(/*__m128i*/ _1, /*ui3*/ _2)                           \
  ((__m128i)__builtin_lsx_vbitrevi_b((v16u8)(_1), (_2)))

#define __lsx_vbitrevi_h(/*__m128i*/ _1, /*ui4*/ _2)                           \
  ((__m128i)__builtin_lsx_vbitrevi_h((v8u16)(_1), (_2)))

#define __lsx_vbitrevi_w(/*__m128i*/ _1, /*ui5*/ _2)                           \
  ((__m128i)__builtin_lsx_vbitrevi_w((v4u32)(_1), (_2)))

#define __lsx_vbitrevi_d(/*__m128i*/ _1, /*ui6*/ _2)                           \
  ((__m128i)__builtin_lsx_vbitrevi_d((v2u64)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vadd_b(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vadd_b((v16i8)_1, (v16i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vadd_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vadd_h((v8i16)_1, (v8i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vadd_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vadd_w((v4i32)_1, (v4i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vadd_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vadd_d((v2i64)_1, (v2i64)_2);
}

#define __lsx_vaddi_bu(/*__m128i*/ _1, /*ui5*/ _2)                             \
  ((__m128i)__builtin_lsx_vaddi_bu((v16i8)(_1), (_2)))

#define __lsx_vaddi_hu(/*__m128i*/ _1, /*ui5*/ _2)                             \
  ((__m128i)__builtin_lsx_vaddi_hu((v8i16)(_1), (_2)))

#define __lsx_vaddi_wu(/*__m128i*/ _1, /*ui5*/ _2)                             \
  ((__m128i)__builtin_lsx_vaddi_wu((v4i32)(_1), (_2)))

#define __lsx_vaddi_du(/*__m128i*/ _1, /*ui5*/ _2)                             \
  ((__m128i)__builtin_lsx_vaddi_du((v2i64)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsub_b(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsub_b((v16i8)_1, (v16i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsub_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsub_h((v8i16)_1, (v8i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsub_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsub_w((v4i32)_1, (v4i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsub_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsub_d((v2i64)_1, (v2i64)_2);
}

#define __lsx_vsubi_bu(/*__m128i*/ _1, /*ui5*/ _2)                             \
  ((__m128i)__builtin_lsx_vsubi_bu((v16i8)(_1), (_2)))

#define __lsx_vsubi_hu(/*__m128i*/ _1, /*ui5*/ _2)                             \
  ((__m128i)__builtin_lsx_vsubi_hu((v8i16)(_1), (_2)))

#define __lsx_vsubi_wu(/*__m128i*/ _1, /*ui5*/ _2)                             \
  ((__m128i)__builtin_lsx_vsubi_wu((v4i32)(_1), (_2)))

#define __lsx_vsubi_du(/*__m128i*/ _1, /*ui5*/ _2)                             \
  ((__m128i)__builtin_lsx_vsubi_du((v2i64)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmax_b(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmax_b((v16i8)_1, (v16i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmax_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmax_h((v8i16)_1, (v8i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmax_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmax_w((v4i32)_1, (v4i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmax_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmax_d((v2i64)_1, (v2i64)_2);
}

#define __lsx_vmaxi_b(/*__m128i*/ _1, /*si5*/ _2)                              \
  ((__m128i)__builtin_lsx_vmaxi_b((v16i8)(_1), (_2)))

#define __lsx_vmaxi_h(/*__m128i*/ _1, /*si5*/ _2)                              \
  ((__m128i)__builtin_lsx_vmaxi_h((v8i16)(_1), (_2)))

#define __lsx_vmaxi_w(/*__m128i*/ _1, /*si5*/ _2)                              \
  ((__m128i)__builtin_lsx_vmaxi_w((v4i32)(_1), (_2)))

#define __lsx_vmaxi_d(/*__m128i*/ _1, /*si5*/ _2)                              \
  ((__m128i)__builtin_lsx_vmaxi_d((v2i64)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmax_bu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmax_bu((v16u8)_1, (v16u8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmax_hu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmax_hu((v8u16)_1, (v8u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmax_wu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmax_wu((v4u32)_1, (v4u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmax_du(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmax_du((v2u64)_1, (v2u64)_2);
}

#define __lsx_vmaxi_bu(/*__m128i*/ _1, /*ui5*/ _2)                             \
  ((__m128i)__builtin_lsx_vmaxi_bu((v16u8)(_1), (_2)))

#define __lsx_vmaxi_hu(/*__m128i*/ _1, /*ui5*/ _2)                             \
  ((__m128i)__builtin_lsx_vmaxi_hu((v8u16)(_1), (_2)))

#define __lsx_vmaxi_wu(/*__m128i*/ _1, /*ui5*/ _2)                             \
  ((__m128i)__builtin_lsx_vmaxi_wu((v4u32)(_1), (_2)))

#define __lsx_vmaxi_du(/*__m128i*/ _1, /*ui5*/ _2)                             \
  ((__m128i)__builtin_lsx_vmaxi_du((v2u64)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmin_b(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmin_b((v16i8)_1, (v16i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmin_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmin_h((v8i16)_1, (v8i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmin_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmin_w((v4i32)_1, (v4i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmin_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmin_d((v2i64)_1, (v2i64)_2);
}

#define __lsx_vmini_b(/*__m128i*/ _1, /*si5*/ _2)                              \
  ((__m128i)__builtin_lsx_vmini_b((v16i8)(_1), (_2)))

#define __lsx_vmini_h(/*__m128i*/ _1, /*si5*/ _2)                              \
  ((__m128i)__builtin_lsx_vmini_h((v8i16)(_1), (_2)))

#define __lsx_vmini_w(/*__m128i*/ _1, /*si5*/ _2)                              \
  ((__m128i)__builtin_lsx_vmini_w((v4i32)(_1), (_2)))

#define __lsx_vmini_d(/*__m128i*/ _1, /*si5*/ _2)                              \
  ((__m128i)__builtin_lsx_vmini_d((v2i64)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmin_bu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmin_bu((v16u8)_1, (v16u8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmin_hu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmin_hu((v8u16)_1, (v8u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmin_wu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmin_wu((v4u32)_1, (v4u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmin_du(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmin_du((v2u64)_1, (v2u64)_2);
}

#define __lsx_vmini_bu(/*__m128i*/ _1, /*ui5*/ _2)                             \
  ((__m128i)__builtin_lsx_vmini_bu((v16u8)(_1), (_2)))

#define __lsx_vmini_hu(/*__m128i*/ _1, /*ui5*/ _2)                             \
  ((__m128i)__builtin_lsx_vmini_hu((v8u16)(_1), (_2)))

#define __lsx_vmini_wu(/*__m128i*/ _1, /*ui5*/ _2)                             \
  ((__m128i)__builtin_lsx_vmini_wu((v4u32)(_1), (_2)))

#define __lsx_vmini_du(/*__m128i*/ _1, /*ui5*/ _2)                             \
  ((__m128i)__builtin_lsx_vmini_du((v2u64)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vseq_b(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vseq_b((v16i8)_1, (v16i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vseq_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vseq_h((v8i16)_1, (v8i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vseq_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vseq_w((v4i32)_1, (v4i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vseq_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vseq_d((v2i64)_1, (v2i64)_2);
}

#define __lsx_vseqi_b(/*__m128i*/ _1, /*si5*/ _2)                              \
  ((__m128i)__builtin_lsx_vseqi_b((v16i8)(_1), (_2)))

#define __lsx_vseqi_h(/*__m128i*/ _1, /*si5*/ _2)                              \
  ((__m128i)__builtin_lsx_vseqi_h((v8i16)(_1), (_2)))

#define __lsx_vseqi_w(/*__m128i*/ _1, /*si5*/ _2)                              \
  ((__m128i)__builtin_lsx_vseqi_w((v4i32)(_1), (_2)))

#define __lsx_vseqi_d(/*__m128i*/ _1, /*si5*/ _2)                              \
  ((__m128i)__builtin_lsx_vseqi_d((v2i64)(_1), (_2)))

#define __lsx_vslti_b(/*__m128i*/ _1, /*si5*/ _2)                              \
  ((__m128i)__builtin_lsx_vslti_b((v16i8)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vslt_b(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vslt_b((v16i8)_1, (v16i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vslt_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vslt_h((v8i16)_1, (v8i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vslt_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vslt_w((v4i32)_1, (v4i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vslt_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vslt_d((v2i64)_1, (v2i64)_2);
}

#define __lsx_vslti_h(/*__m128i*/ _1, /*si5*/ _2)                              \
  ((__m128i)__builtin_lsx_vslti_h((v8i16)(_1), (_2)))

#define __lsx_vslti_w(/*__m128i*/ _1, /*si5*/ _2)                              \
  ((__m128i)__builtin_lsx_vslti_w((v4i32)(_1), (_2)))

#define __lsx_vslti_d(/*__m128i*/ _1, /*si5*/ _2)                              \
  ((__m128i)__builtin_lsx_vslti_d((v2i64)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vslt_bu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vslt_bu((v16u8)_1, (v16u8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vslt_hu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vslt_hu((v8u16)_1, (v8u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vslt_wu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vslt_wu((v4u32)_1, (v4u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vslt_du(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vslt_du((v2u64)_1, (v2u64)_2);
}

#define __lsx_vslti_bu(/*__m128i*/ _1, /*ui5*/ _2)                             \
  ((__m128i)__builtin_lsx_vslti_bu((v16u8)(_1), (_2)))

#define __lsx_vslti_hu(/*__m128i*/ _1, /*ui5*/ _2)                             \
  ((__m128i)__builtin_lsx_vslti_hu((v8u16)(_1), (_2)))

#define __lsx_vslti_wu(/*__m128i*/ _1, /*ui5*/ _2)                             \
  ((__m128i)__builtin_lsx_vslti_wu((v4u32)(_1), (_2)))

#define __lsx_vslti_du(/*__m128i*/ _1, /*ui5*/ _2)                             \
  ((__m128i)__builtin_lsx_vslti_du((v2u64)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsle_b(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsle_b((v16i8)_1, (v16i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsle_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsle_h((v8i16)_1, (v8i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsle_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsle_w((v4i32)_1, (v4i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsle_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsle_d((v2i64)_1, (v2i64)_2);
}

#define __lsx_vslei_b(/*__m128i*/ _1, /*si5*/ _2)                              \
  ((__m128i)__builtin_lsx_vslei_b((v16i8)(_1), (_2)))

#define __lsx_vslei_h(/*__m128i*/ _1, /*si5*/ _2)                              \
  ((__m128i)__builtin_lsx_vslei_h((v8i16)(_1), (_2)))

#define __lsx_vslei_w(/*__m128i*/ _1, /*si5*/ _2)                              \
  ((__m128i)__builtin_lsx_vslei_w((v4i32)(_1), (_2)))

#define __lsx_vslei_d(/*__m128i*/ _1, /*si5*/ _2)                              \
  ((__m128i)__builtin_lsx_vslei_d((v2i64)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsle_bu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsle_bu((v16u8)_1, (v16u8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsle_hu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsle_hu((v8u16)_1, (v8u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsle_wu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsle_wu((v4u32)_1, (v4u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsle_du(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsle_du((v2u64)_1, (v2u64)_2);
}

#define __lsx_vslei_bu(/*__m128i*/ _1, /*ui5*/ _2)                             \
  ((__m128i)__builtin_lsx_vslei_bu((v16u8)(_1), (_2)))

#define __lsx_vslei_hu(/*__m128i*/ _1, /*ui5*/ _2)                             \
  ((__m128i)__builtin_lsx_vslei_hu((v8u16)(_1), (_2)))

#define __lsx_vslei_wu(/*__m128i*/ _1, /*ui5*/ _2)                             \
  ((__m128i)__builtin_lsx_vslei_wu((v4u32)(_1), (_2)))

#define __lsx_vslei_du(/*__m128i*/ _1, /*ui5*/ _2)                             \
  ((__m128i)__builtin_lsx_vslei_du((v2u64)(_1), (_2)))

#define __lsx_vsat_b(/*__m128i*/ _1, /*ui3*/ _2)                               \
  ((__m128i)__builtin_lsx_vsat_b((v16i8)(_1), (_2)))

#define __lsx_vsat_h(/*__m128i*/ _1, /*ui4*/ _2)                               \
  ((__m128i)__builtin_lsx_vsat_h((v8i16)(_1), (_2)))

#define __lsx_vsat_w(/*__m128i*/ _1, /*ui5*/ _2)                               \
  ((__m128i)__builtin_lsx_vsat_w((v4i32)(_1), (_2)))

#define __lsx_vsat_d(/*__m128i*/ _1, /*ui6*/ _2)                               \
  ((__m128i)__builtin_lsx_vsat_d((v2i64)(_1), (_2)))

#define __lsx_vsat_bu(/*__m128i*/ _1, /*ui3*/ _2)                              \
  ((__m128i)__builtin_lsx_vsat_bu((v16u8)(_1), (_2)))

#define __lsx_vsat_hu(/*__m128i*/ _1, /*ui4*/ _2)                              \
  ((__m128i)__builtin_lsx_vsat_hu((v8u16)(_1), (_2)))

#define __lsx_vsat_wu(/*__m128i*/ _1, /*ui5*/ _2)                              \
  ((__m128i)__builtin_lsx_vsat_wu((v4u32)(_1), (_2)))

#define __lsx_vsat_du(/*__m128i*/ _1, /*ui6*/ _2)                              \
  ((__m128i)__builtin_lsx_vsat_du((v2u64)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vadda_b(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vadda_b((v16i8)_1, (v16i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vadda_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vadda_h((v8i16)_1, (v8i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vadda_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vadda_w((v4i32)_1, (v4i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vadda_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vadda_d((v2i64)_1, (v2i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsadd_b(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsadd_b((v16i8)_1, (v16i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsadd_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsadd_h((v8i16)_1, (v8i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsadd_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsadd_w((v4i32)_1, (v4i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsadd_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsadd_d((v2i64)_1, (v2i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsadd_bu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsadd_bu((v16u8)_1, (v16u8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsadd_hu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsadd_hu((v8u16)_1, (v8u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsadd_wu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsadd_wu((v4u32)_1, (v4u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsadd_du(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsadd_du((v2u64)_1, (v2u64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vavg_b(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vavg_b((v16i8)_1, (v16i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vavg_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vavg_h((v8i16)_1, (v8i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vavg_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vavg_w((v4i32)_1, (v4i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vavg_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vavg_d((v2i64)_1, (v2i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vavg_bu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vavg_bu((v16u8)_1, (v16u8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vavg_hu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vavg_hu((v8u16)_1, (v8u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vavg_wu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vavg_wu((v4u32)_1, (v4u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vavg_du(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vavg_du((v2u64)_1, (v2u64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vavgr_b(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vavgr_b((v16i8)_1, (v16i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vavgr_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vavgr_h((v8i16)_1, (v8i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vavgr_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vavgr_w((v4i32)_1, (v4i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vavgr_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vavgr_d((v2i64)_1, (v2i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vavgr_bu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vavgr_bu((v16u8)_1, (v16u8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vavgr_hu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vavgr_hu((v8u16)_1, (v8u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vavgr_wu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vavgr_wu((v4u32)_1, (v4u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vavgr_du(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vavgr_du((v2u64)_1, (v2u64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vssub_b(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vssub_b((v16i8)_1, (v16i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vssub_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vssub_h((v8i16)_1, (v8i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vssub_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vssub_w((v4i32)_1, (v4i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vssub_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vssub_d((v2i64)_1, (v2i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vssub_bu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vssub_bu((v16u8)_1, (v16u8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vssub_hu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vssub_hu((v8u16)_1, (v8u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vssub_wu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vssub_wu((v4u32)_1, (v4u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vssub_du(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vssub_du((v2u64)_1, (v2u64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vabsd_b(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vabsd_b((v16i8)_1, (v16i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vabsd_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vabsd_h((v8i16)_1, (v8i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vabsd_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vabsd_w((v4i32)_1, (v4i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vabsd_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vabsd_d((v2i64)_1, (v2i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vabsd_bu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vabsd_bu((v16u8)_1, (v16u8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vabsd_hu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vabsd_hu((v8u16)_1, (v8u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vabsd_wu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vabsd_wu((v4u32)_1, (v4u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vabsd_du(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vabsd_du((v2u64)_1, (v2u64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmul_b(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmul_b((v16i8)_1, (v16i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmul_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmul_h((v8i16)_1, (v8i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmul_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmul_w((v4i32)_1, (v4i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmul_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmul_d((v2i64)_1, (v2i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmadd_b(__m128i _1, __m128i _2, __m128i _3) {
  return (__m128i)__builtin_lsx_vmadd_b((v16i8)_1, (v16i8)_2, (v16i8)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmadd_h(__m128i _1, __m128i _2, __m128i _3) {
  return (__m128i)__builtin_lsx_vmadd_h((v8i16)_1, (v8i16)_2, (v8i16)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmadd_w(__m128i _1, __m128i _2, __m128i _3) {
  return (__m128i)__builtin_lsx_vmadd_w((v4i32)_1, (v4i32)_2, (v4i32)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmadd_d(__m128i _1, __m128i _2, __m128i _3) {
  return (__m128i)__builtin_lsx_vmadd_d((v2i64)_1, (v2i64)_2, (v2i64)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmsub_b(__m128i _1, __m128i _2, __m128i _3) {
  return (__m128i)__builtin_lsx_vmsub_b((v16i8)_1, (v16i8)_2, (v16i8)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmsub_h(__m128i _1, __m128i _2, __m128i _3) {
  return (__m128i)__builtin_lsx_vmsub_h((v8i16)_1, (v8i16)_2, (v8i16)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmsub_w(__m128i _1, __m128i _2, __m128i _3) {
  return (__m128i)__builtin_lsx_vmsub_w((v4i32)_1, (v4i32)_2, (v4i32)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmsub_d(__m128i _1, __m128i _2, __m128i _3) {
  return (__m128i)__builtin_lsx_vmsub_d((v2i64)_1, (v2i64)_2, (v2i64)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vdiv_b(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vdiv_b((v16i8)_1, (v16i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vdiv_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vdiv_h((v8i16)_1, (v8i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vdiv_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vdiv_w((v4i32)_1, (v4i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vdiv_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vdiv_d((v2i64)_1, (v2i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vdiv_bu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vdiv_bu((v16u8)_1, (v16u8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vdiv_hu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vdiv_hu((v8u16)_1, (v8u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vdiv_wu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vdiv_wu((v4u32)_1, (v4u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vdiv_du(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vdiv_du((v2u64)_1, (v2u64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vhaddw_h_b(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vhaddw_h_b((v16i8)_1, (v16i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vhaddw_w_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vhaddw_w_h((v8i16)_1, (v8i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vhaddw_d_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vhaddw_d_w((v4i32)_1, (v4i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vhaddw_hu_bu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vhaddw_hu_bu((v16u8)_1, (v16u8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vhaddw_wu_hu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vhaddw_wu_hu((v8u16)_1, (v8u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vhaddw_du_wu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vhaddw_du_wu((v4u32)_1, (v4u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vhsubw_h_b(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vhsubw_h_b((v16i8)_1, (v16i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vhsubw_w_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vhsubw_w_h((v8i16)_1, (v8i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vhsubw_d_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vhsubw_d_w((v4i32)_1, (v4i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vhsubw_hu_bu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vhsubw_hu_bu((v16u8)_1, (v16u8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vhsubw_wu_hu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vhsubw_wu_hu((v8u16)_1, (v8u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vhsubw_du_wu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vhsubw_du_wu((v4u32)_1, (v4u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmod_b(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmod_b((v16i8)_1, (v16i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmod_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmod_h((v8i16)_1, (v8i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmod_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmod_w((v4i32)_1, (v4i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmod_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmod_d((v2i64)_1, (v2i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmod_bu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmod_bu((v16u8)_1, (v16u8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmod_hu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmod_hu((v8u16)_1, (v8u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmod_wu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmod_wu((v4u32)_1, (v4u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmod_du(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmod_du((v2u64)_1, (v2u64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vreplve_b(__m128i _1, int _2) {
  return (__m128i)__builtin_lsx_vreplve_b((v16i8)_1, (int)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vreplve_h(__m128i _1, int _2) {
  return (__m128i)__builtin_lsx_vreplve_h((v8i16)_1, (int)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vreplve_w(__m128i _1, int _2) {
  return (__m128i)__builtin_lsx_vreplve_w((v4i32)_1, (int)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vreplve_d(__m128i _1, int _2) {
  return (__m128i)__builtin_lsx_vreplve_d((v2i64)_1, (int)_2);
}

#define __lsx_vreplvei_b(/*__m128i*/ _1, /*ui4*/ _2)                           \
  ((__m128i)__builtin_lsx_vreplvei_b((v16i8)(_1), (_2)))

#define __lsx_vreplvei_h(/*__m128i*/ _1, /*ui3*/ _2)                           \
  ((__m128i)__builtin_lsx_vreplvei_h((v8i16)(_1), (_2)))

#define __lsx_vreplvei_w(/*__m128i*/ _1, /*ui2*/ _2)                           \
  ((__m128i)__builtin_lsx_vreplvei_w((v4i32)(_1), (_2)))

#define __lsx_vreplvei_d(/*__m128i*/ _1, /*ui1*/ _2)                           \
  ((__m128i)__builtin_lsx_vreplvei_d((v2i64)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vpickev_b(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vpickev_b((v16i8)_1, (v16i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vpickev_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vpickev_h((v8i16)_1, (v8i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vpickev_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vpickev_w((v4i32)_1, (v4i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vpickev_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vpickev_d((v2i64)_1, (v2i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vpickod_b(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vpickod_b((v16i8)_1, (v16i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vpickod_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vpickod_h((v8i16)_1, (v8i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vpickod_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vpickod_w((v4i32)_1, (v4i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vpickod_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vpickod_d((v2i64)_1, (v2i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vilvh_b(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vilvh_b((v16i8)_1, (v16i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vilvh_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vilvh_h((v8i16)_1, (v8i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vilvh_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vilvh_w((v4i32)_1, (v4i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vilvh_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vilvh_d((v2i64)_1, (v2i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vilvl_b(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vilvl_b((v16i8)_1, (v16i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vilvl_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vilvl_h((v8i16)_1, (v8i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vilvl_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vilvl_w((v4i32)_1, (v4i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vilvl_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vilvl_d((v2i64)_1, (v2i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vpackev_b(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vpackev_b((v16i8)_1, (v16i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vpackev_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vpackev_h((v8i16)_1, (v8i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vpackev_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vpackev_w((v4i32)_1, (v4i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vpackev_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vpackev_d((v2i64)_1, (v2i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vpackod_b(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vpackod_b((v16i8)_1, (v16i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vpackod_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vpackod_h((v8i16)_1, (v8i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vpackod_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vpackod_w((v4i32)_1, (v4i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vpackod_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vpackod_d((v2i64)_1, (v2i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vshuf_h(__m128i _1, __m128i _2, __m128i _3) {
  return (__m128i)__builtin_lsx_vshuf_h((v8i16)_1, (v8i16)_2, (v8i16)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vshuf_w(__m128i _1, __m128i _2, __m128i _3) {
  return (__m128i)__builtin_lsx_vshuf_w((v4i32)_1, (v4i32)_2, (v4i32)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vshuf_d(__m128i _1, __m128i _2, __m128i _3) {
  return (__m128i)__builtin_lsx_vshuf_d((v2i64)_1, (v2i64)_2, (v2i64)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vand_v(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vand_v((v16u8)_1, (v16u8)_2);
}

#define __lsx_vandi_b(/*__m128i*/ _1, /*ui8*/ _2)                              \
  ((__m128i)__builtin_lsx_vandi_b((v16u8)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vor_v(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vor_v((v16u8)_1, (v16u8)_2);
}

#define __lsx_vori_b(/*__m128i*/ _1, /*ui8*/ _2)                               \
  ((__m128i)__builtin_lsx_vori_b((v16u8)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vnor_v(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vnor_v((v16u8)_1, (v16u8)_2);
}

#define __lsx_vnori_b(/*__m128i*/ _1, /*ui8*/ _2)                              \
  ((__m128i)__builtin_lsx_vnori_b((v16u8)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vxor_v(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vxor_v((v16u8)_1, (v16u8)_2);
}

#define __lsx_vxori_b(/*__m128i*/ _1, /*ui8*/ _2)                              \
  ((__m128i)__builtin_lsx_vxori_b((v16u8)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vbitsel_v(__m128i _1, __m128i _2, __m128i _3) {
  return (__m128i)__builtin_lsx_vbitsel_v((v16u8)_1, (v16u8)_2, (v16u8)_3);
}

#define __lsx_vbitseli_b(/*__m128i*/ _1, /*__m128i*/ _2, /*ui8*/ _3)           \
  ((__m128i)__builtin_lsx_vbitseli_b((v16u8)(_1), (v16u8)(_2), (_3)))

#define __lsx_vshuf4i_b(/*__m128i*/ _1, /*ui8*/ _2)                            \
  ((__m128i)__builtin_lsx_vshuf4i_b((v16i8)(_1), (_2)))

#define __lsx_vshuf4i_h(/*__m128i*/ _1, /*ui8*/ _2)                            \
  ((__m128i)__builtin_lsx_vshuf4i_h((v8i16)(_1), (_2)))

#define __lsx_vshuf4i_w(/*__m128i*/ _1, /*ui8*/ _2)                            \
  ((__m128i)__builtin_lsx_vshuf4i_w((v4i32)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vreplgr2vr_b(int _1) {
  return (__m128i)__builtin_lsx_vreplgr2vr_b((int)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vreplgr2vr_h(int _1) {
  return (__m128i)__builtin_lsx_vreplgr2vr_h((int)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vreplgr2vr_w(int _1) {
  return (__m128i)__builtin_lsx_vreplgr2vr_w((int)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vreplgr2vr_d(long int _1) {
  return (__m128i)__builtin_lsx_vreplgr2vr_d((long int)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vpcnt_b(__m128i _1) {
  return (__m128i)__builtin_lsx_vpcnt_b((v16i8)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vpcnt_h(__m128i _1) {
  return (__m128i)__builtin_lsx_vpcnt_h((v8i16)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vpcnt_w(__m128i _1) {
  return (__m128i)__builtin_lsx_vpcnt_w((v4i32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vpcnt_d(__m128i _1) {
  return (__m128i)__builtin_lsx_vpcnt_d((v2i64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vclo_b(__m128i _1) {
  return (__m128i)__builtin_lsx_vclo_b((v16i8)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vclo_h(__m128i _1) {
  return (__m128i)__builtin_lsx_vclo_h((v8i16)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vclo_w(__m128i _1) {
  return (__m128i)__builtin_lsx_vclo_w((v4i32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vclo_d(__m128i _1) {
  return (__m128i)__builtin_lsx_vclo_d((v2i64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vclz_b(__m128i _1) {
  return (__m128i)__builtin_lsx_vclz_b((v16i8)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vclz_h(__m128i _1) {
  return (__m128i)__builtin_lsx_vclz_h((v8i16)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vclz_w(__m128i _1) {
  return (__m128i)__builtin_lsx_vclz_w((v4i32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vclz_d(__m128i _1) {
  return (__m128i)__builtin_lsx_vclz_d((v2i64)_1);
}

#define __lsx_vpickve2gr_b(/*__m128i*/ _1, /*ui4*/ _2)                         \
  ((int)__builtin_lsx_vpickve2gr_b((v16i8)(_1), (_2)))

#define __lsx_vpickve2gr_h(/*__m128i*/ _1, /*ui3*/ _2)                         \
  ((int)__builtin_lsx_vpickve2gr_h((v8i16)(_1), (_2)))

#define __lsx_vpickve2gr_w(/*__m128i*/ _1, /*ui2*/ _2)                         \
  ((int)__builtin_lsx_vpickve2gr_w((v4i32)(_1), (_2)))

#define __lsx_vpickve2gr_d(/*__m128i*/ _1, /*ui1*/ _2)                         \
  ((long int)__builtin_lsx_vpickve2gr_d((v2i64)(_1), (_2)))

#define __lsx_vpickve2gr_bu(/*__m128i*/ _1, /*ui4*/ _2)                        \
  ((unsigned int)__builtin_lsx_vpickve2gr_bu((v16i8)(_1), (_2)))

#define __lsx_vpickve2gr_hu(/*__m128i*/ _1, /*ui3*/ _2)                        \
  ((unsigned int)__builtin_lsx_vpickve2gr_hu((v8i16)(_1), (_2)))

#define __lsx_vpickve2gr_wu(/*__m128i*/ _1, /*ui2*/ _2)                        \
  ((unsigned int)__builtin_lsx_vpickve2gr_wu((v4i32)(_1), (_2)))

#define __lsx_vpickve2gr_du(/*__m128i*/ _1, /*ui1*/ _2)                        \
  ((unsigned long int)__builtin_lsx_vpickve2gr_du((v2i64)(_1), (_2)))

#define __lsx_vinsgr2vr_b(/*__m128i*/ _1, /*int*/ _2, /*ui4*/ _3)              \
  ((__m128i)__builtin_lsx_vinsgr2vr_b((v16i8)(_1), (int)(_2), (_3)))

#define __lsx_vinsgr2vr_h(/*__m128i*/ _1, /*int*/ _2, /*ui3*/ _3)              \
  ((__m128i)__builtin_lsx_vinsgr2vr_h((v8i16)(_1), (int)(_2), (_3)))

#define __lsx_vinsgr2vr_w(/*__m128i*/ _1, /*int*/ _2, /*ui2*/ _3)              \
  ((__m128i)__builtin_lsx_vinsgr2vr_w((v4i32)(_1), (int)(_2), (_3)))

#define __lsx_vinsgr2vr_d(/*__m128i*/ _1, /*long int*/ _2, /*ui1*/ _3)         \
  ((__m128i)__builtin_lsx_vinsgr2vr_d((v2i64)(_1), (long int)(_2), (_3)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128
    __lsx_vfadd_s(__m128 _1, __m128 _2) {
  return (__m128)__builtin_lsx_vfadd_s((v4f32)_1, (v4f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128d
    __lsx_vfadd_d(__m128d _1, __m128d _2) {
  return (__m128d)__builtin_lsx_vfadd_d((v2f64)_1, (v2f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128
    __lsx_vfsub_s(__m128 _1, __m128 _2) {
  return (__m128)__builtin_lsx_vfsub_s((v4f32)_1, (v4f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128d
    __lsx_vfsub_d(__m128d _1, __m128d _2) {
  return (__m128d)__builtin_lsx_vfsub_d((v2f64)_1, (v2f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128
    __lsx_vfmul_s(__m128 _1, __m128 _2) {
  return (__m128)__builtin_lsx_vfmul_s((v4f32)_1, (v4f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128d
    __lsx_vfmul_d(__m128d _1, __m128d _2) {
  return (__m128d)__builtin_lsx_vfmul_d((v2f64)_1, (v2f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128
    __lsx_vfdiv_s(__m128 _1, __m128 _2) {
  return (__m128)__builtin_lsx_vfdiv_s((v4f32)_1, (v4f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128d
    __lsx_vfdiv_d(__m128d _1, __m128d _2) {
  return (__m128d)__builtin_lsx_vfdiv_d((v2f64)_1, (v2f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vfcvt_h_s(__m128 _1, __m128 _2) {
  return (__m128i)__builtin_lsx_vfcvt_h_s((v4f32)_1, (v4f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128
    __lsx_vfcvt_s_d(__m128d _1, __m128d _2) {
  return (__m128)__builtin_lsx_vfcvt_s_d((v2f64)_1, (v2f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128
    __lsx_vfmin_s(__m128 _1, __m128 _2) {
  return (__m128)__builtin_lsx_vfmin_s((v4f32)_1, (v4f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128d
    __lsx_vfmin_d(__m128d _1, __m128d _2) {
  return (__m128d)__builtin_lsx_vfmin_d((v2f64)_1, (v2f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128
    __lsx_vfmina_s(__m128 _1, __m128 _2) {
  return (__m128)__builtin_lsx_vfmina_s((v4f32)_1, (v4f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128d
    __lsx_vfmina_d(__m128d _1, __m128d _2) {
  return (__m128d)__builtin_lsx_vfmina_d((v2f64)_1, (v2f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128
    __lsx_vfmax_s(__m128 _1, __m128 _2) {
  return (__m128)__builtin_lsx_vfmax_s((v4f32)_1, (v4f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128d
    __lsx_vfmax_d(__m128d _1, __m128d _2) {
  return (__m128d)__builtin_lsx_vfmax_d((v2f64)_1, (v2f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128
    __lsx_vfmaxa_s(__m128 _1, __m128 _2) {
  return (__m128)__builtin_lsx_vfmaxa_s((v4f32)_1, (v4f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128d
    __lsx_vfmaxa_d(__m128d _1, __m128d _2) {
  return (__m128d)__builtin_lsx_vfmaxa_d((v2f64)_1, (v2f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vfclass_s(__m128 _1) {
  return (__m128i)__builtin_lsx_vfclass_s((v4f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vfclass_d(__m128d _1) {
  return (__m128i)__builtin_lsx_vfclass_d((v2f64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128
    __lsx_vfsqrt_s(__m128 _1) {
  return (__m128)__builtin_lsx_vfsqrt_s((v4f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128d
    __lsx_vfsqrt_d(__m128d _1) {
  return (__m128d)__builtin_lsx_vfsqrt_d((v2f64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128
    __lsx_vfrecip_s(__m128 _1) {
  return (__m128)__builtin_lsx_vfrecip_s((v4f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128d
    __lsx_vfrecip_d(__m128d _1) {
  return (__m128d)__builtin_lsx_vfrecip_d((v2f64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128
    __lsx_vfrecipe_s(__m128 _1) {
  return (__m128)__builtin_lsx_vfrecipe_s((v4f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128d
    __lsx_vfrecipe_d(__m128d _1) {
  return (__m128d)__builtin_lsx_vfrecipe_d((v2f64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128
    __lsx_vfrint_s(__m128 _1) {
  return (__m128)__builtin_lsx_vfrint_s((v4f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128d
    __lsx_vfrint_d(__m128d _1) {
  return (__m128d)__builtin_lsx_vfrint_d((v2f64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128
    __lsx_vfrsqrt_s(__m128 _1) {
  return (__m128)__builtin_lsx_vfrsqrt_s((v4f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128d
    __lsx_vfrsqrt_d(__m128d _1) {
  return (__m128d)__builtin_lsx_vfrsqrt_d((v2f64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128
    __lsx_vfrsqrte_s(__m128 _1) {
  return (__m128)__builtin_lsx_vfrsqrte_s((v4f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128d
    __lsx_vfrsqrte_d(__m128d _1) {
  return (__m128d)__builtin_lsx_vfrsqrte_d((v2f64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128
    __lsx_vflogb_s(__m128 _1) {
  return (__m128)__builtin_lsx_vflogb_s((v4f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128d
    __lsx_vflogb_d(__m128d _1) {
  return (__m128d)__builtin_lsx_vflogb_d((v2f64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128
    __lsx_vfcvth_s_h(__m128i _1) {
  return (__m128)__builtin_lsx_vfcvth_s_h((v8i16)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128d
    __lsx_vfcvth_d_s(__m128 _1) {
  return (__m128d)__builtin_lsx_vfcvth_d_s((v4f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128
    __lsx_vfcvtl_s_h(__m128i _1) {
  return (__m128)__builtin_lsx_vfcvtl_s_h((v8i16)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128d
    __lsx_vfcvtl_d_s(__m128 _1) {
  return (__m128d)__builtin_lsx_vfcvtl_d_s((v4f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vftint_w_s(__m128 _1) {
  return (__m128i)__builtin_lsx_vftint_w_s((v4f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vftint_l_d(__m128d _1) {
  return (__m128i)__builtin_lsx_vftint_l_d((v2f64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vftint_wu_s(__m128 _1) {
  return (__m128i)__builtin_lsx_vftint_wu_s((v4f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vftint_lu_d(__m128d _1) {
  return (__m128i)__builtin_lsx_vftint_lu_d((v2f64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vftintrz_w_s(__m128 _1) {
  return (__m128i)__builtin_lsx_vftintrz_w_s((v4f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vftintrz_l_d(__m128d _1) {
  return (__m128i)__builtin_lsx_vftintrz_l_d((v2f64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vftintrz_wu_s(__m128 _1) {
  return (__m128i)__builtin_lsx_vftintrz_wu_s((v4f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vftintrz_lu_d(__m128d _1) {
  return (__m128i)__builtin_lsx_vftintrz_lu_d((v2f64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128
    __lsx_vffint_s_w(__m128i _1) {
  return (__m128)__builtin_lsx_vffint_s_w((v4i32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128d
    __lsx_vffint_d_l(__m128i _1) {
  return (__m128d)__builtin_lsx_vffint_d_l((v2i64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128
    __lsx_vffint_s_wu(__m128i _1) {
  return (__m128)__builtin_lsx_vffint_s_wu((v4u32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128d
    __lsx_vffint_d_lu(__m128i _1) {
  return (__m128d)__builtin_lsx_vffint_d_lu((v2u64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vandn_v(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vandn_v((v16u8)_1, (v16u8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vneg_b(__m128i _1) {
  return (__m128i)__builtin_lsx_vneg_b((v16i8)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vneg_h(__m128i _1) {
  return (__m128i)__builtin_lsx_vneg_h((v8i16)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vneg_w(__m128i _1) {
  return (__m128i)__builtin_lsx_vneg_w((v4i32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vneg_d(__m128i _1) {
  return (__m128i)__builtin_lsx_vneg_d((v2i64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmuh_b(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmuh_b((v16i8)_1, (v16i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmuh_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmuh_h((v8i16)_1, (v8i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmuh_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmuh_w((v4i32)_1, (v4i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmuh_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmuh_d((v2i64)_1, (v2i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmuh_bu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmuh_bu((v16u8)_1, (v16u8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmuh_hu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmuh_hu((v8u16)_1, (v8u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmuh_wu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmuh_wu((v4u32)_1, (v4u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmuh_du(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmuh_du((v2u64)_1, (v2u64)_2);
}

#define __lsx_vsllwil_h_b(/*__m128i*/ _1, /*ui3*/ _2)                          \
  ((__m128i)__builtin_lsx_vsllwil_h_b((v16i8)(_1), (_2)))

#define __lsx_vsllwil_w_h(/*__m128i*/ _1, /*ui4*/ _2)                          \
  ((__m128i)__builtin_lsx_vsllwil_w_h((v8i16)(_1), (_2)))

#define __lsx_vsllwil_d_w(/*__m128i*/ _1, /*ui5*/ _2)                          \
  ((__m128i)__builtin_lsx_vsllwil_d_w((v4i32)(_1), (_2)))

#define __lsx_vsllwil_hu_bu(/*__m128i*/ _1, /*ui3*/ _2)                        \
  ((__m128i)__builtin_lsx_vsllwil_hu_bu((v16u8)(_1), (_2)))

#define __lsx_vsllwil_wu_hu(/*__m128i*/ _1, /*ui4*/ _2)                        \
  ((__m128i)__builtin_lsx_vsllwil_wu_hu((v8u16)(_1), (_2)))

#define __lsx_vsllwil_du_wu(/*__m128i*/ _1, /*ui5*/ _2)                        \
  ((__m128i)__builtin_lsx_vsllwil_du_wu((v4u32)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsran_b_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsran_b_h((v8i16)_1, (v8i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsran_h_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsran_h_w((v4i32)_1, (v4i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsran_w_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsran_w_d((v2i64)_1, (v2i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vssran_b_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vssran_b_h((v8i16)_1, (v8i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vssran_h_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vssran_h_w((v4i32)_1, (v4i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vssran_w_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vssran_w_d((v2i64)_1, (v2i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vssran_bu_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vssran_bu_h((v8u16)_1, (v8u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vssran_hu_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vssran_hu_w((v4u32)_1, (v4u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vssran_wu_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vssran_wu_d((v2u64)_1, (v2u64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsrarn_b_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsrarn_b_h((v8i16)_1, (v8i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsrarn_h_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsrarn_h_w((v4i32)_1, (v4i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsrarn_w_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsrarn_w_d((v2i64)_1, (v2i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vssrarn_b_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vssrarn_b_h((v8i16)_1, (v8i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vssrarn_h_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vssrarn_h_w((v4i32)_1, (v4i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vssrarn_w_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vssrarn_w_d((v2i64)_1, (v2i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vssrarn_bu_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vssrarn_bu_h((v8u16)_1, (v8u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vssrarn_hu_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vssrarn_hu_w((v4u32)_1, (v4u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vssrarn_wu_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vssrarn_wu_d((v2u64)_1, (v2u64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsrln_b_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsrln_b_h((v8i16)_1, (v8i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsrln_h_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsrln_h_w((v4i32)_1, (v4i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsrln_w_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsrln_w_d((v2i64)_1, (v2i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vssrln_bu_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vssrln_bu_h((v8u16)_1, (v8u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vssrln_hu_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vssrln_hu_w((v4u32)_1, (v4u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vssrln_wu_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vssrln_wu_d((v2u64)_1, (v2u64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsrlrn_b_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsrlrn_b_h((v8i16)_1, (v8i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsrlrn_h_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsrlrn_h_w((v4i32)_1, (v4i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsrlrn_w_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsrlrn_w_d((v2i64)_1, (v2i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vssrlrn_bu_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vssrlrn_bu_h((v8u16)_1, (v8u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vssrlrn_hu_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vssrlrn_hu_w((v4u32)_1, (v4u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vssrlrn_wu_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vssrlrn_wu_d((v2u64)_1, (v2u64)_2);
}

#define __lsx_vfrstpi_b(/*__m128i*/ _1, /*__m128i*/ _2, /*ui5*/ _3)            \
  ((__m128i)__builtin_lsx_vfrstpi_b((v16i8)(_1), (v16i8)(_2), (_3)))

#define __lsx_vfrstpi_h(/*__m128i*/ _1, /*__m128i*/ _2, /*ui5*/ _3)            \
  ((__m128i)__builtin_lsx_vfrstpi_h((v8i16)(_1), (v8i16)(_2), (_3)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vfrstp_b(__m128i _1, __m128i _2, __m128i _3) {
  return (__m128i)__builtin_lsx_vfrstp_b((v16i8)_1, (v16i8)_2, (v16i8)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vfrstp_h(__m128i _1, __m128i _2, __m128i _3) {
  return (__m128i)__builtin_lsx_vfrstp_h((v8i16)_1, (v8i16)_2, (v8i16)_3);
}

#define __lsx_vshuf4i_d(/*__m128i*/ _1, /*__m128i*/ _2, /*ui8*/ _3)            \
  ((__m128i)__builtin_lsx_vshuf4i_d((v2i64)(_1), (v2i64)(_2), (_3)))

#define __lsx_vbsrl_v(/*__m128i*/ _1, /*ui5*/ _2)                              \
  ((__m128i)__builtin_lsx_vbsrl_v((v16i8)(_1), (_2)))

#define __lsx_vbsll_v(/*__m128i*/ _1, /*ui5*/ _2)                              \
  ((__m128i)__builtin_lsx_vbsll_v((v16i8)(_1), (_2)))

#define __lsx_vextrins_b(/*__m128i*/ _1, /*__m128i*/ _2, /*ui8*/ _3)           \
  ((__m128i)__builtin_lsx_vextrins_b((v16i8)(_1), (v16i8)(_2), (_3)))

#define __lsx_vextrins_h(/*__m128i*/ _1, /*__m128i*/ _2, /*ui8*/ _3)           \
  ((__m128i)__builtin_lsx_vextrins_h((v8i16)(_1), (v8i16)(_2), (_3)))

#define __lsx_vextrins_w(/*__m128i*/ _1, /*__m128i*/ _2, /*ui8*/ _3)           \
  ((__m128i)__builtin_lsx_vextrins_w((v4i32)(_1), (v4i32)(_2), (_3)))

#define __lsx_vextrins_d(/*__m128i*/ _1, /*__m128i*/ _2, /*ui8*/ _3)           \
  ((__m128i)__builtin_lsx_vextrins_d((v2i64)(_1), (v2i64)(_2), (_3)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmskltz_b(__m128i _1) {
  return (__m128i)__builtin_lsx_vmskltz_b((v16i8)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmskltz_h(__m128i _1) {
  return (__m128i)__builtin_lsx_vmskltz_h((v8i16)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmskltz_w(__m128i _1) {
  return (__m128i)__builtin_lsx_vmskltz_w((v4i32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmskltz_d(__m128i _1) {
  return (__m128i)__builtin_lsx_vmskltz_d((v2i64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsigncov_b(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsigncov_b((v16i8)_1, (v16i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsigncov_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsigncov_h((v8i16)_1, (v8i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsigncov_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsigncov_w((v4i32)_1, (v4i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsigncov_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsigncov_d((v2i64)_1, (v2i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128
    __lsx_vfmadd_s(__m128 _1, __m128 _2, __m128 _3) {
  return (__m128)__builtin_lsx_vfmadd_s((v4f32)_1, (v4f32)_2, (v4f32)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128d
    __lsx_vfmadd_d(__m128d _1, __m128d _2, __m128d _3) {
  return (__m128d)__builtin_lsx_vfmadd_d((v2f64)_1, (v2f64)_2, (v2f64)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128
    __lsx_vfmsub_s(__m128 _1, __m128 _2, __m128 _3) {
  return (__m128)__builtin_lsx_vfmsub_s((v4f32)_1, (v4f32)_2, (v4f32)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128d
    __lsx_vfmsub_d(__m128d _1, __m128d _2, __m128d _3) {
  return (__m128d)__builtin_lsx_vfmsub_d((v2f64)_1, (v2f64)_2, (v2f64)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128
    __lsx_vfnmadd_s(__m128 _1, __m128 _2, __m128 _3) {
  return (__m128)__builtin_lsx_vfnmadd_s((v4f32)_1, (v4f32)_2, (v4f32)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128d
    __lsx_vfnmadd_d(__m128d _1, __m128d _2, __m128d _3) {
  return (__m128d)__builtin_lsx_vfnmadd_d((v2f64)_1, (v2f64)_2, (v2f64)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128
    __lsx_vfnmsub_s(__m128 _1, __m128 _2, __m128 _3) {
  return (__m128)__builtin_lsx_vfnmsub_s((v4f32)_1, (v4f32)_2, (v4f32)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128d
    __lsx_vfnmsub_d(__m128d _1, __m128d _2, __m128d _3) {
  return (__m128d)__builtin_lsx_vfnmsub_d((v2f64)_1, (v2f64)_2, (v2f64)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vftintrne_w_s(__m128 _1) {
  return (__m128i)__builtin_lsx_vftintrne_w_s((v4f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vftintrne_l_d(__m128d _1) {
  return (__m128i)__builtin_lsx_vftintrne_l_d((v2f64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vftintrp_w_s(__m128 _1) {
  return (__m128i)__builtin_lsx_vftintrp_w_s((v4f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vftintrp_l_d(__m128d _1) {
  return (__m128i)__builtin_lsx_vftintrp_l_d((v2f64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vftintrm_w_s(__m128 _1) {
  return (__m128i)__builtin_lsx_vftintrm_w_s((v4f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vftintrm_l_d(__m128d _1) {
  return (__m128i)__builtin_lsx_vftintrm_l_d((v2f64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vftint_w_d(__m128d _1, __m128d _2) {
  return (__m128i)__builtin_lsx_vftint_w_d((v2f64)_1, (v2f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128
    __lsx_vffint_s_l(__m128i _1, __m128i _2) {
  return (__m128)__builtin_lsx_vffint_s_l((v2i64)_1, (v2i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vftintrz_w_d(__m128d _1, __m128d _2) {
  return (__m128i)__builtin_lsx_vftintrz_w_d((v2f64)_1, (v2f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vftintrp_w_d(__m128d _1, __m128d _2) {
  return (__m128i)__builtin_lsx_vftintrp_w_d((v2f64)_1, (v2f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vftintrm_w_d(__m128d _1, __m128d _2) {
  return (__m128i)__builtin_lsx_vftintrm_w_d((v2f64)_1, (v2f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vftintrne_w_d(__m128d _1, __m128d _2) {
  return (__m128i)__builtin_lsx_vftintrne_w_d((v2f64)_1, (v2f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vftintl_l_s(__m128 _1) {
  return (__m128i)__builtin_lsx_vftintl_l_s((v4f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vftinth_l_s(__m128 _1) {
  return (__m128i)__builtin_lsx_vftinth_l_s((v4f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128d
    __lsx_vffinth_d_w(__m128i _1) {
  return (__m128d)__builtin_lsx_vffinth_d_w((v4i32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128d
    __lsx_vffintl_d_w(__m128i _1) {
  return (__m128d)__builtin_lsx_vffintl_d_w((v4i32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vftintrzl_l_s(__m128 _1) {
  return (__m128i)__builtin_lsx_vftintrzl_l_s((v4f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vftintrzh_l_s(__m128 _1) {
  return (__m128i)__builtin_lsx_vftintrzh_l_s((v4f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vftintrpl_l_s(__m128 _1) {
  return (__m128i)__builtin_lsx_vftintrpl_l_s((v4f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vftintrph_l_s(__m128 _1) {
  return (__m128i)__builtin_lsx_vftintrph_l_s((v4f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vftintrml_l_s(__m128 _1) {
  return (__m128i)__builtin_lsx_vftintrml_l_s((v4f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vftintrmh_l_s(__m128 _1) {
  return (__m128i)__builtin_lsx_vftintrmh_l_s((v4f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vftintrnel_l_s(__m128 _1) {
  return (__m128i)__builtin_lsx_vftintrnel_l_s((v4f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vftintrneh_l_s(__m128 _1) {
  return (__m128i)__builtin_lsx_vftintrneh_l_s((v4f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128
    __lsx_vfrintrne_s(__m128 _1) {
  return (__m128)__builtin_lsx_vfrintrne_s((v4f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128d
    __lsx_vfrintrne_d(__m128d _1) {
  return (__m128d)__builtin_lsx_vfrintrne_d((v2f64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128
    __lsx_vfrintrz_s(__m128 _1) {
  return (__m128)__builtin_lsx_vfrintrz_s((v4f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128d
    __lsx_vfrintrz_d(__m128d _1) {
  return (__m128d)__builtin_lsx_vfrintrz_d((v2f64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128
    __lsx_vfrintrp_s(__m128 _1) {
  return (__m128)__builtin_lsx_vfrintrp_s((v4f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128d
    __lsx_vfrintrp_d(__m128d _1) {
  return (__m128d)__builtin_lsx_vfrintrp_d((v2f64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128
    __lsx_vfrintrm_s(__m128 _1) {
  return (__m128)__builtin_lsx_vfrintrm_s((v4f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128d
    __lsx_vfrintrm_d(__m128d _1) {
  return (__m128d)__builtin_lsx_vfrintrm_d((v2f64)_1);
}

#define __lsx_vstelm_b(/*__m128i*/ _1, /*void **/ _2, /*si8*/ _3, /*idx*/ _4)  \
  ((void)__builtin_lsx_vstelm_b((v16i8)(_1), (void *)(_2), (_3), (_4)))

#define __lsx_vstelm_h(/*__m128i*/ _1, /*void **/ _2, /*si8*/ _3, /*idx*/ _4)  \
  ((void)__builtin_lsx_vstelm_h((v8i16)(_1), (void *)(_2), (_3), (_4)))

#define __lsx_vstelm_w(/*__m128i*/ _1, /*void **/ _2, /*si8*/ _3, /*idx*/ _4)  \
  ((void)__builtin_lsx_vstelm_w((v4i32)(_1), (void *)(_2), (_3), (_4)))

#define __lsx_vstelm_d(/*__m128i*/ _1, /*void **/ _2, /*si8*/ _3, /*idx*/ _4)  \
  ((void)__builtin_lsx_vstelm_d((v2i64)(_1), (void *)(_2), (_3), (_4)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vaddwev_d_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vaddwev_d_w((v4i32)_1, (v4i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vaddwev_w_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vaddwev_w_h((v8i16)_1, (v8i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vaddwev_h_b(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vaddwev_h_b((v16i8)_1, (v16i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vaddwod_d_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vaddwod_d_w((v4i32)_1, (v4i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vaddwod_w_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vaddwod_w_h((v8i16)_1, (v8i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vaddwod_h_b(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vaddwod_h_b((v16i8)_1, (v16i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vaddwev_d_wu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vaddwev_d_wu((v4u32)_1, (v4u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vaddwev_w_hu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vaddwev_w_hu((v8u16)_1, (v8u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vaddwev_h_bu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vaddwev_h_bu((v16u8)_1, (v16u8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vaddwod_d_wu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vaddwod_d_wu((v4u32)_1, (v4u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vaddwod_w_hu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vaddwod_w_hu((v8u16)_1, (v8u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vaddwod_h_bu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vaddwod_h_bu((v16u8)_1, (v16u8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vaddwev_d_wu_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vaddwev_d_wu_w((v4u32)_1, (v4i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vaddwev_w_hu_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vaddwev_w_hu_h((v8u16)_1, (v8i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vaddwev_h_bu_b(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vaddwev_h_bu_b((v16u8)_1, (v16i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vaddwod_d_wu_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vaddwod_d_wu_w((v4u32)_1, (v4i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vaddwod_w_hu_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vaddwod_w_hu_h((v8u16)_1, (v8i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vaddwod_h_bu_b(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vaddwod_h_bu_b((v16u8)_1, (v16i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsubwev_d_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsubwev_d_w((v4i32)_1, (v4i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsubwev_w_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsubwev_w_h((v8i16)_1, (v8i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsubwev_h_b(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsubwev_h_b((v16i8)_1, (v16i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsubwod_d_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsubwod_d_w((v4i32)_1, (v4i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsubwod_w_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsubwod_w_h((v8i16)_1, (v8i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsubwod_h_b(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsubwod_h_b((v16i8)_1, (v16i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsubwev_d_wu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsubwev_d_wu((v4u32)_1, (v4u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsubwev_w_hu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsubwev_w_hu((v8u16)_1, (v8u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsubwev_h_bu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsubwev_h_bu((v16u8)_1, (v16u8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsubwod_d_wu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsubwod_d_wu((v4u32)_1, (v4u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsubwod_w_hu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsubwod_w_hu((v8u16)_1, (v8u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsubwod_h_bu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsubwod_h_bu((v16u8)_1, (v16u8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vaddwev_q_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vaddwev_q_d((v2i64)_1, (v2i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vaddwod_q_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vaddwod_q_d((v2i64)_1, (v2i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vaddwev_q_du(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vaddwev_q_du((v2u64)_1, (v2u64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vaddwod_q_du(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vaddwod_q_du((v2u64)_1, (v2u64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsubwev_q_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsubwev_q_d((v2i64)_1, (v2i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsubwod_q_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsubwod_q_d((v2i64)_1, (v2i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsubwev_q_du(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsubwev_q_du((v2u64)_1, (v2u64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsubwod_q_du(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsubwod_q_du((v2u64)_1, (v2u64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vaddwev_q_du_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vaddwev_q_du_d((v2u64)_1, (v2i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vaddwod_q_du_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vaddwod_q_du_d((v2u64)_1, (v2i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmulwev_d_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmulwev_d_w((v4i32)_1, (v4i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmulwev_w_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmulwev_w_h((v8i16)_1, (v8i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmulwev_h_b(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmulwev_h_b((v16i8)_1, (v16i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmulwod_d_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmulwod_d_w((v4i32)_1, (v4i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmulwod_w_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmulwod_w_h((v8i16)_1, (v8i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmulwod_h_b(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmulwod_h_b((v16i8)_1, (v16i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmulwev_d_wu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmulwev_d_wu((v4u32)_1, (v4u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmulwev_w_hu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmulwev_w_hu((v8u16)_1, (v8u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmulwev_h_bu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmulwev_h_bu((v16u8)_1, (v16u8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmulwod_d_wu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmulwod_d_wu((v4u32)_1, (v4u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmulwod_w_hu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmulwod_w_hu((v8u16)_1, (v8u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmulwod_h_bu(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmulwod_h_bu((v16u8)_1, (v16u8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmulwev_d_wu_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmulwev_d_wu_w((v4u32)_1, (v4i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmulwev_w_hu_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmulwev_w_hu_h((v8u16)_1, (v8i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmulwev_h_bu_b(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmulwev_h_bu_b((v16u8)_1, (v16i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmulwod_d_wu_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmulwod_d_wu_w((v4u32)_1, (v4i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmulwod_w_hu_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmulwod_w_hu_h((v8u16)_1, (v8i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmulwod_h_bu_b(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmulwod_h_bu_b((v16u8)_1, (v16i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmulwev_q_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmulwev_q_d((v2i64)_1, (v2i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmulwod_q_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmulwod_q_d((v2i64)_1, (v2i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmulwev_q_du(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmulwev_q_du((v2u64)_1, (v2u64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmulwod_q_du(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmulwod_q_du((v2u64)_1, (v2u64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmulwev_q_du_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmulwev_q_du_d((v2u64)_1, (v2i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmulwod_q_du_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vmulwod_q_du_d((v2u64)_1, (v2i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vhaddw_q_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vhaddw_q_d((v2i64)_1, (v2i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vhaddw_qu_du(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vhaddw_qu_du((v2u64)_1, (v2u64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vhsubw_q_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vhsubw_q_d((v2i64)_1, (v2i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vhsubw_qu_du(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vhsubw_qu_du((v2u64)_1, (v2u64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmaddwev_d_w(__m128i _1, __m128i _2, __m128i _3) {
  return (__m128i)__builtin_lsx_vmaddwev_d_w((v2i64)_1, (v4i32)_2, (v4i32)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmaddwev_w_h(__m128i _1, __m128i _2, __m128i _3) {
  return (__m128i)__builtin_lsx_vmaddwev_w_h((v4i32)_1, (v8i16)_2, (v8i16)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmaddwev_h_b(__m128i _1, __m128i _2, __m128i _3) {
  return (__m128i)__builtin_lsx_vmaddwev_h_b((v8i16)_1, (v16i8)_2, (v16i8)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmaddwev_d_wu(__m128i _1, __m128i _2, __m128i _3) {
  return (__m128i)__builtin_lsx_vmaddwev_d_wu((v2u64)_1, (v4u32)_2, (v4u32)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmaddwev_w_hu(__m128i _1, __m128i _2, __m128i _3) {
  return (__m128i)__builtin_lsx_vmaddwev_w_hu((v4u32)_1, (v8u16)_2, (v8u16)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmaddwev_h_bu(__m128i _1, __m128i _2, __m128i _3) {
  return (__m128i)__builtin_lsx_vmaddwev_h_bu((v8u16)_1, (v16u8)_2, (v16u8)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmaddwod_d_w(__m128i _1, __m128i _2, __m128i _3) {
  return (__m128i)__builtin_lsx_vmaddwod_d_w((v2i64)_1, (v4i32)_2, (v4i32)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmaddwod_w_h(__m128i _1, __m128i _2, __m128i _3) {
  return (__m128i)__builtin_lsx_vmaddwod_w_h((v4i32)_1, (v8i16)_2, (v8i16)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmaddwod_h_b(__m128i _1, __m128i _2, __m128i _3) {
  return (__m128i)__builtin_lsx_vmaddwod_h_b((v8i16)_1, (v16i8)_2, (v16i8)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmaddwod_d_wu(__m128i _1, __m128i _2, __m128i _3) {
  return (__m128i)__builtin_lsx_vmaddwod_d_wu((v2u64)_1, (v4u32)_2, (v4u32)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmaddwod_w_hu(__m128i _1, __m128i _2, __m128i _3) {
  return (__m128i)__builtin_lsx_vmaddwod_w_hu((v4u32)_1, (v8u16)_2, (v8u16)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmaddwod_h_bu(__m128i _1, __m128i _2, __m128i _3) {
  return (__m128i)__builtin_lsx_vmaddwod_h_bu((v8u16)_1, (v16u8)_2, (v16u8)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmaddwev_d_wu_w(__m128i _1, __m128i _2, __m128i _3) {
  return (__m128i)__builtin_lsx_vmaddwev_d_wu_w((v2i64)_1, (v4u32)_2,
                                                (v4i32)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmaddwev_w_hu_h(__m128i _1, __m128i _2, __m128i _3) {
  return (__m128i)__builtin_lsx_vmaddwev_w_hu_h((v4i32)_1, (v8u16)_2,
                                                (v8i16)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmaddwev_h_bu_b(__m128i _1, __m128i _2, __m128i _3) {
  return (__m128i)__builtin_lsx_vmaddwev_h_bu_b((v8i16)_1, (v16u8)_2,
                                                (v16i8)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmaddwod_d_wu_w(__m128i _1, __m128i _2, __m128i _3) {
  return (__m128i)__builtin_lsx_vmaddwod_d_wu_w((v2i64)_1, (v4u32)_2,
                                                (v4i32)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmaddwod_w_hu_h(__m128i _1, __m128i _2, __m128i _3) {
  return (__m128i)__builtin_lsx_vmaddwod_w_hu_h((v4i32)_1, (v8u16)_2,
                                                (v8i16)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmaddwod_h_bu_b(__m128i _1, __m128i _2, __m128i _3) {
  return (__m128i)__builtin_lsx_vmaddwod_h_bu_b((v8i16)_1, (v16u8)_2,
                                                (v16i8)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmaddwev_q_d(__m128i _1, __m128i _2, __m128i _3) {
  return (__m128i)__builtin_lsx_vmaddwev_q_d((v2i64)_1, (v2i64)_2, (v2i64)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmaddwod_q_d(__m128i _1, __m128i _2, __m128i _3) {
  return (__m128i)__builtin_lsx_vmaddwod_q_d((v2i64)_1, (v2i64)_2, (v2i64)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmaddwev_q_du(__m128i _1, __m128i _2, __m128i _3) {
  return (__m128i)__builtin_lsx_vmaddwev_q_du((v2u64)_1, (v2u64)_2, (v2u64)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmaddwod_q_du(__m128i _1, __m128i _2, __m128i _3) {
  return (__m128i)__builtin_lsx_vmaddwod_q_du((v2u64)_1, (v2u64)_2, (v2u64)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmaddwev_q_du_d(__m128i _1, __m128i _2, __m128i _3) {
  return (__m128i)__builtin_lsx_vmaddwev_q_du_d((v2i64)_1, (v2u64)_2,
                                                (v2i64)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmaddwod_q_du_d(__m128i _1, __m128i _2, __m128i _3) {
  return (__m128i)__builtin_lsx_vmaddwod_q_du_d((v2i64)_1, (v2u64)_2,
                                                (v2i64)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vrotr_b(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vrotr_b((v16i8)_1, (v16i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vrotr_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vrotr_h((v8i16)_1, (v8i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vrotr_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vrotr_w((v4i32)_1, (v4i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vrotr_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vrotr_d((v2i64)_1, (v2i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vadd_q(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vadd_q((v2i64)_1, (v2i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vsub_q(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vsub_q((v2i64)_1, (v2i64)_2);
}

#define __lsx_vldrepl_b(/*void **/ _1, /*si12*/ _2)                            \
  ((__m128i)__builtin_lsx_vldrepl_b((void const *)(_1), (_2)))

#define __lsx_vldrepl_h(/*void **/ _1, /*si11*/ _2)                            \
  ((__m128i)__builtin_lsx_vldrepl_h((void const *)(_1), (_2)))

#define __lsx_vldrepl_w(/*void **/ _1, /*si10*/ _2)                            \
  ((__m128i)__builtin_lsx_vldrepl_w((void const *)(_1), (_2)))

#define __lsx_vldrepl_d(/*void **/ _1, /*si9*/ _2)                             \
  ((__m128i)__builtin_lsx_vldrepl_d((void const *)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmskgez_b(__m128i _1) {
  return (__m128i)__builtin_lsx_vmskgez_b((v16i8)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vmsknz_b(__m128i _1) {
  return (__m128i)__builtin_lsx_vmsknz_b((v16i8)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vexth_h_b(__m128i _1) {
  return (__m128i)__builtin_lsx_vexth_h_b((v16i8)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vexth_w_h(__m128i _1) {
  return (__m128i)__builtin_lsx_vexth_w_h((v8i16)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vexth_d_w(__m128i _1) {
  return (__m128i)__builtin_lsx_vexth_d_w((v4i32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vexth_q_d(__m128i _1) {
  return (__m128i)__builtin_lsx_vexth_q_d((v2i64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vexth_hu_bu(__m128i _1) {
  return (__m128i)__builtin_lsx_vexth_hu_bu((v16u8)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vexth_wu_hu(__m128i _1) {
  return (__m128i)__builtin_lsx_vexth_wu_hu((v8u16)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vexth_du_wu(__m128i _1) {
  return (__m128i)__builtin_lsx_vexth_du_wu((v4u32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vexth_qu_du(__m128i _1) {
  return (__m128i)__builtin_lsx_vexth_qu_du((v2u64)_1);
}

#define __lsx_vrotri_b(/*__m128i*/ _1, /*ui3*/ _2)                             \
  ((__m128i)__builtin_lsx_vrotri_b((v16i8)(_1), (_2)))

#define __lsx_vrotri_h(/*__m128i*/ _1, /*ui4*/ _2)                             \
  ((__m128i)__builtin_lsx_vrotri_h((v8i16)(_1), (_2)))

#define __lsx_vrotri_w(/*__m128i*/ _1, /*ui5*/ _2)                             \
  ((__m128i)__builtin_lsx_vrotri_w((v4i32)(_1), (_2)))

#define __lsx_vrotri_d(/*__m128i*/ _1, /*ui6*/ _2)                             \
  ((__m128i)__builtin_lsx_vrotri_d((v2i64)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vextl_q_d(__m128i _1) {
  return (__m128i)__builtin_lsx_vextl_q_d((v2i64)_1);
}

#define __lsx_vsrlni_b_h(/*__m128i*/ _1, /*__m128i*/ _2, /*ui4*/ _3)           \
  ((__m128i)__builtin_lsx_vsrlni_b_h((v16i8)(_1), (v16i8)(_2), (_3)))

#define __lsx_vsrlni_h_w(/*__m128i*/ _1, /*__m128i*/ _2, /*ui5*/ _3)           \
  ((__m128i)__builtin_lsx_vsrlni_h_w((v8i16)(_1), (v8i16)(_2), (_3)))

#define __lsx_vsrlni_w_d(/*__m128i*/ _1, /*__m128i*/ _2, /*ui6*/ _3)           \
  ((__m128i)__builtin_lsx_vsrlni_w_d((v4i32)(_1), (v4i32)(_2), (_3)))

#define __lsx_vsrlni_d_q(/*__m128i*/ _1, /*__m128i*/ _2, /*ui7*/ _3)           \
  ((__m128i)__builtin_lsx_vsrlni_d_q((v2i64)(_1), (v2i64)(_2), (_3)))

#define __lsx_vsrlrni_b_h(/*__m128i*/ _1, /*__m128i*/ _2, /*ui4*/ _3)          \
  ((__m128i)__builtin_lsx_vsrlrni_b_h((v16i8)(_1), (v16i8)(_2), (_3)))

#define __lsx_vsrlrni_h_w(/*__m128i*/ _1, /*__m128i*/ _2, /*ui5*/ _3)          \
  ((__m128i)__builtin_lsx_vsrlrni_h_w((v8i16)(_1), (v8i16)(_2), (_3)))

#define __lsx_vsrlrni_w_d(/*__m128i*/ _1, /*__m128i*/ _2, /*ui6*/ _3)          \
  ((__m128i)__builtin_lsx_vsrlrni_w_d((v4i32)(_1), (v4i32)(_2), (_3)))

#define __lsx_vsrlrni_d_q(/*__m128i*/ _1, /*__m128i*/ _2, /*ui7*/ _3)          \
  ((__m128i)__builtin_lsx_vsrlrni_d_q((v2i64)(_1), (v2i64)(_2), (_3)))

#define __lsx_vssrlni_b_h(/*__m128i*/ _1, /*__m128i*/ _2, /*ui4*/ _3)          \
  ((__m128i)__builtin_lsx_vssrlni_b_h((v16i8)(_1), (v16i8)(_2), (_3)))

#define __lsx_vssrlni_h_w(/*__m128i*/ _1, /*__m128i*/ _2, /*ui5*/ _3)          \
  ((__m128i)__builtin_lsx_vssrlni_h_w((v8i16)(_1), (v8i16)(_2), (_3)))

#define __lsx_vssrlni_w_d(/*__m128i*/ _1, /*__m128i*/ _2, /*ui6*/ _3)          \
  ((__m128i)__builtin_lsx_vssrlni_w_d((v4i32)(_1), (v4i32)(_2), (_3)))

#define __lsx_vssrlni_d_q(/*__m128i*/ _1, /*__m128i*/ _2, /*ui7*/ _3)          \
  ((__m128i)__builtin_lsx_vssrlni_d_q((v2i64)(_1), (v2i64)(_2), (_3)))

#define __lsx_vssrlni_bu_h(/*__m128i*/ _1, /*__m128i*/ _2, /*ui4*/ _3)         \
  ((__m128i)__builtin_lsx_vssrlni_bu_h((v16u8)(_1), (v16i8)(_2), (_3)))

#define __lsx_vssrlni_hu_w(/*__m128i*/ _1, /*__m128i*/ _2, /*ui5*/ _3)         \
  ((__m128i)__builtin_lsx_vssrlni_hu_w((v8u16)(_1), (v8i16)(_2), (_3)))

#define __lsx_vssrlni_wu_d(/*__m128i*/ _1, /*__m128i*/ _2, /*ui6*/ _3)         \
  ((__m128i)__builtin_lsx_vssrlni_wu_d((v4u32)(_1), (v4i32)(_2), (_3)))

#define __lsx_vssrlni_du_q(/*__m128i*/ _1, /*__m128i*/ _2, /*ui7*/ _3)         \
  ((__m128i)__builtin_lsx_vssrlni_du_q((v2u64)(_1), (v2i64)(_2), (_3)))

#define __lsx_vssrlrni_b_h(/*__m128i*/ _1, /*__m128i*/ _2, /*ui4*/ _3)         \
  ((__m128i)__builtin_lsx_vssrlrni_b_h((v16i8)(_1), (v16i8)(_2), (_3)))

#define __lsx_vssrlrni_h_w(/*__m128i*/ _1, /*__m128i*/ _2, /*ui5*/ _3)         \
  ((__m128i)__builtin_lsx_vssrlrni_h_w((v8i16)(_1), (v8i16)(_2), (_3)))

#define __lsx_vssrlrni_w_d(/*__m128i*/ _1, /*__m128i*/ _2, /*ui6*/ _3)         \
  ((__m128i)__builtin_lsx_vssrlrni_w_d((v4i32)(_1), (v4i32)(_2), (_3)))

#define __lsx_vssrlrni_d_q(/*__m128i*/ _1, /*__m128i*/ _2, /*ui7*/ _3)         \
  ((__m128i)__builtin_lsx_vssrlrni_d_q((v2i64)(_1), (v2i64)(_2), (_3)))

#define __lsx_vssrlrni_bu_h(/*__m128i*/ _1, /*__m128i*/ _2, /*ui4*/ _3)        \
  ((__m128i)__builtin_lsx_vssrlrni_bu_h((v16u8)(_1), (v16i8)(_2), (_3)))

#define __lsx_vssrlrni_hu_w(/*__m128i*/ _1, /*__m128i*/ _2, /*ui5*/ _3)        \
  ((__m128i)__builtin_lsx_vssrlrni_hu_w((v8u16)(_1), (v8i16)(_2), (_3)))

#define __lsx_vssrlrni_wu_d(/*__m128i*/ _1, /*__m128i*/ _2, /*ui6*/ _3)        \
  ((__m128i)__builtin_lsx_vssrlrni_wu_d((v4u32)(_1), (v4i32)(_2), (_3)))

#define __lsx_vssrlrni_du_q(/*__m128i*/ _1, /*__m128i*/ _2, /*ui7*/ _3)        \
  ((__m128i)__builtin_lsx_vssrlrni_du_q((v2u64)(_1), (v2i64)(_2), (_3)))

#define __lsx_vsrani_b_h(/*__m128i*/ _1, /*__m128i*/ _2, /*ui4*/ _3)           \
  ((__m128i)__builtin_lsx_vsrani_b_h((v16i8)(_1), (v16i8)(_2), (_3)))

#define __lsx_vsrani_h_w(/*__m128i*/ _1, /*__m128i*/ _2, /*ui5*/ _3)           \
  ((__m128i)__builtin_lsx_vsrani_h_w((v8i16)(_1), (v8i16)(_2), (_3)))

#define __lsx_vsrani_w_d(/*__m128i*/ _1, /*__m128i*/ _2, /*ui6*/ _3)           \
  ((__m128i)__builtin_lsx_vsrani_w_d((v4i32)(_1), (v4i32)(_2), (_3)))

#define __lsx_vsrani_d_q(/*__m128i*/ _1, /*__m128i*/ _2, /*ui7*/ _3)           \
  ((__m128i)__builtin_lsx_vsrani_d_q((v2i64)(_1), (v2i64)(_2), (_3)))

#define __lsx_vsrarni_b_h(/*__m128i*/ _1, /*__m128i*/ _2, /*ui4*/ _3)          \
  ((__m128i)__builtin_lsx_vsrarni_b_h((v16i8)(_1), (v16i8)(_2), (_3)))

#define __lsx_vsrarni_h_w(/*__m128i*/ _1, /*__m128i*/ _2, /*ui5*/ _3)          \
  ((__m128i)__builtin_lsx_vsrarni_h_w((v8i16)(_1), (v8i16)(_2), (_3)))

#define __lsx_vsrarni_w_d(/*__m128i*/ _1, /*__m128i*/ _2, /*ui6*/ _3)          \
  ((__m128i)__builtin_lsx_vsrarni_w_d((v4i32)(_1), (v4i32)(_2), (_3)))

#define __lsx_vsrarni_d_q(/*__m128i*/ _1, /*__m128i*/ _2, /*ui7*/ _3)          \
  ((__m128i)__builtin_lsx_vsrarni_d_q((v2i64)(_1), (v2i64)(_2), (_3)))

#define __lsx_vssrani_b_h(/*__m128i*/ _1, /*__m128i*/ _2, /*ui4*/ _3)          \
  ((__m128i)__builtin_lsx_vssrani_b_h((v16i8)(_1), (v16i8)(_2), (_3)))

#define __lsx_vssrani_h_w(/*__m128i*/ _1, /*__m128i*/ _2, /*ui5*/ _3)          \
  ((__m128i)__builtin_lsx_vssrani_h_w((v8i16)(_1), (v8i16)(_2), (_3)))

#define __lsx_vssrani_w_d(/*__m128i*/ _1, /*__m128i*/ _2, /*ui6*/ _3)          \
  ((__m128i)__builtin_lsx_vssrani_w_d((v4i32)(_1), (v4i32)(_2), (_3)))

#define __lsx_vssrani_d_q(/*__m128i*/ _1, /*__m128i*/ _2, /*ui7*/ _3)          \
  ((__m128i)__builtin_lsx_vssrani_d_q((v2i64)(_1), (v2i64)(_2), (_3)))

#define __lsx_vssrani_bu_h(/*__m128i*/ _1, /*__m128i*/ _2, /*ui4*/ _3)         \
  ((__m128i)__builtin_lsx_vssrani_bu_h((v16u8)(_1), (v16i8)(_2), (_3)))

#define __lsx_vssrani_hu_w(/*__m128i*/ _1, /*__m128i*/ _2, /*ui5*/ _3)         \
  ((__m128i)__builtin_lsx_vssrani_hu_w((v8u16)(_1), (v8i16)(_2), (_3)))

#define __lsx_vssrani_wu_d(/*__m128i*/ _1, /*__m128i*/ _2, /*ui6*/ _3)         \
  ((__m128i)__builtin_lsx_vssrani_wu_d((v4u32)(_1), (v4i32)(_2), (_3)))

#define __lsx_vssrani_du_q(/*__m128i*/ _1, /*__m128i*/ _2, /*ui7*/ _3)         \
  ((__m128i)__builtin_lsx_vssrani_du_q((v2u64)(_1), (v2i64)(_2), (_3)))

#define __lsx_vssrarni_b_h(/*__m128i*/ _1, /*__m128i*/ _2, /*ui4*/ _3)         \
  ((__m128i)__builtin_lsx_vssrarni_b_h((v16i8)(_1), (v16i8)(_2), (_3)))

#define __lsx_vssrarni_h_w(/*__m128i*/ _1, /*__m128i*/ _2, /*ui5*/ _3)         \
  ((__m128i)__builtin_lsx_vssrarni_h_w((v8i16)(_1), (v8i16)(_2), (_3)))

#define __lsx_vssrarni_w_d(/*__m128i*/ _1, /*__m128i*/ _2, /*ui6*/ _3)         \
  ((__m128i)__builtin_lsx_vssrarni_w_d((v4i32)(_1), (v4i32)(_2), (_3)))

#define __lsx_vssrarni_d_q(/*__m128i*/ _1, /*__m128i*/ _2, /*ui7*/ _3)         \
  ((__m128i)__builtin_lsx_vssrarni_d_q((v2i64)(_1), (v2i64)(_2), (_3)))

#define __lsx_vssrarni_bu_h(/*__m128i*/ _1, /*__m128i*/ _2, /*ui4*/ _3)        \
  ((__m128i)__builtin_lsx_vssrarni_bu_h((v16u8)(_1), (v16i8)(_2), (_3)))

#define __lsx_vssrarni_hu_w(/*__m128i*/ _1, /*__m128i*/ _2, /*ui5*/ _3)        \
  ((__m128i)__builtin_lsx_vssrarni_hu_w((v8u16)(_1), (v8i16)(_2), (_3)))

#define __lsx_vssrarni_wu_d(/*__m128i*/ _1, /*__m128i*/ _2, /*ui6*/ _3)        \
  ((__m128i)__builtin_lsx_vssrarni_wu_d((v4u32)(_1), (v4i32)(_2), (_3)))

#define __lsx_vssrarni_du_q(/*__m128i*/ _1, /*__m128i*/ _2, /*ui7*/ _3)        \
  ((__m128i)__builtin_lsx_vssrarni_du_q((v2u64)(_1), (v2i64)(_2), (_3)))

#define __lsx_vpermi_w(/*__m128i*/ _1, /*__m128i*/ _2, /*ui8*/ _3)             \
  ((__m128i)__builtin_lsx_vpermi_w((v4i32)(_1), (v4i32)(_2), (_3)))

#define __lsx_vld(/*void **/ _1, /*si12*/ _2)                                  \
  ((__m128i)__builtin_lsx_vld((void const *)(_1), (_2)))

#define __lsx_vst(/*__m128i*/ _1, /*void **/ _2, /*si12*/ _3)                  \
  ((void)__builtin_lsx_vst((v16i8)(_1), (void *)(_2), (_3)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vssrlrn_b_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vssrlrn_b_h((v8i16)_1, (v8i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vssrlrn_h_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vssrlrn_h_w((v4i32)_1, (v4i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vssrlrn_w_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vssrlrn_w_d((v2i64)_1, (v2i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vssrln_b_h(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vssrln_b_h((v8i16)_1, (v8i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vssrln_h_w(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vssrln_h_w((v4i32)_1, (v4i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vssrln_w_d(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vssrln_w_d((v2i64)_1, (v2i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vorn_v(__m128i _1, __m128i _2) {
  return (__m128i)__builtin_lsx_vorn_v((v16i8)_1, (v16i8)_2);
}

#define __lsx_vldi(/*i13*/ _1) ((__m128i)__builtin_lsx_vldi((_1)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vshuf_b(__m128i _1, __m128i _2, __m128i _3) {
  return (__m128i)__builtin_lsx_vshuf_b((v16i8)_1, (v16i8)_2, (v16i8)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vldx(void const *_1, long int _2) {
  return (__m128i)__builtin_lsx_vldx((void const *)_1, (long int)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) void
    __lsx_vstx(__m128i _1, void *_2, long int _3) {
  return (void)__builtin_lsx_vstx((v16i8)_1, (void *)_2, (long int)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vextl_qu_du(__m128i _1) {
  return (__m128i)__builtin_lsx_vextl_qu_du((v2u64)_1);
}

#define __lsx_bnz_b(/*__m128i*/ _1) ((int)__builtin_lsx_bnz_b((v16u8)(_1)))

#define __lsx_bnz_d(/*__m128i*/ _1) ((int)__builtin_lsx_bnz_d((v2u64)(_1)))

#define __lsx_bnz_h(/*__m128i*/ _1) ((int)__builtin_lsx_bnz_h((v8u16)(_1)))

#define __lsx_bnz_v(/*__m128i*/ _1) ((int)__builtin_lsx_bnz_v((v16u8)(_1)))

#define __lsx_bnz_w(/*__m128i*/ _1) ((int)__builtin_lsx_bnz_w((v4u32)(_1)))

#define __lsx_bz_b(/*__m128i*/ _1) ((int)__builtin_lsx_bz_b((v16u8)(_1)))

#define __lsx_bz_d(/*__m128i*/ _1) ((int)__builtin_lsx_bz_d((v2u64)(_1)))

#define __lsx_bz_h(/*__m128i*/ _1) ((int)__builtin_lsx_bz_h((v8u16)(_1)))

#define __lsx_bz_v(/*__m128i*/ _1) ((int)__builtin_lsx_bz_v((v16u8)(_1)))

#define __lsx_bz_w(/*__m128i*/ _1) ((int)__builtin_lsx_bz_w((v4u32)(_1)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vfcmp_caf_d(__m128d _1, __m128d _2) {
  return (__m128i)__builtin_lsx_vfcmp_caf_d((v2f64)_1, (v2f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vfcmp_caf_s(__m128 _1, __m128 _2) {
  return (__m128i)__builtin_lsx_vfcmp_caf_s((v4f32)_1, (v4f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vfcmp_ceq_d(__m128d _1, __m128d _2) {
  return (__m128i)__builtin_lsx_vfcmp_ceq_d((v2f64)_1, (v2f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vfcmp_ceq_s(__m128 _1, __m128 _2) {
  return (__m128i)__builtin_lsx_vfcmp_ceq_s((v4f32)_1, (v4f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vfcmp_cle_d(__m128d _1, __m128d _2) {
  return (__m128i)__builtin_lsx_vfcmp_cle_d((v2f64)_1, (v2f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vfcmp_cle_s(__m128 _1, __m128 _2) {
  return (__m128i)__builtin_lsx_vfcmp_cle_s((v4f32)_1, (v4f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vfcmp_clt_d(__m128d _1, __m128d _2) {
  return (__m128i)__builtin_lsx_vfcmp_clt_d((v2f64)_1, (v2f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vfcmp_clt_s(__m128 _1, __m128 _2) {
  return (__m128i)__builtin_lsx_vfcmp_clt_s((v4f32)_1, (v4f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vfcmp_cne_d(__m128d _1, __m128d _2) {
  return (__m128i)__builtin_lsx_vfcmp_cne_d((v2f64)_1, (v2f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vfcmp_cne_s(__m128 _1, __m128 _2) {
  return (__m128i)__builtin_lsx_vfcmp_cne_s((v4f32)_1, (v4f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vfcmp_cor_d(__m128d _1, __m128d _2) {
  return (__m128i)__builtin_lsx_vfcmp_cor_d((v2f64)_1, (v2f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vfcmp_cor_s(__m128 _1, __m128 _2) {
  return (__m128i)__builtin_lsx_vfcmp_cor_s((v4f32)_1, (v4f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vfcmp_cueq_d(__m128d _1, __m128d _2) {
  return (__m128i)__builtin_lsx_vfcmp_cueq_d((v2f64)_1, (v2f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vfcmp_cueq_s(__m128 _1, __m128 _2) {
  return (__m128i)__builtin_lsx_vfcmp_cueq_s((v4f32)_1, (v4f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vfcmp_cule_d(__m128d _1, __m128d _2) {
  return (__m128i)__builtin_lsx_vfcmp_cule_d((v2f64)_1, (v2f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vfcmp_cule_s(__m128 _1, __m128 _2) {
  return (__m128i)__builtin_lsx_vfcmp_cule_s((v4f32)_1, (v4f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vfcmp_cult_d(__m128d _1, __m128d _2) {
  return (__m128i)__builtin_lsx_vfcmp_cult_d((v2f64)_1, (v2f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vfcmp_cult_s(__m128 _1, __m128 _2) {
  return (__m128i)__builtin_lsx_vfcmp_cult_s((v4f32)_1, (v4f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vfcmp_cun_d(__m128d _1, __m128d _2) {
  return (__m128i)__builtin_lsx_vfcmp_cun_d((v2f64)_1, (v2f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vfcmp_cune_d(__m128d _1, __m128d _2) {
  return (__m128i)__builtin_lsx_vfcmp_cune_d((v2f64)_1, (v2f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vfcmp_cune_s(__m128 _1, __m128 _2) {
  return (__m128i)__builtin_lsx_vfcmp_cune_s((v4f32)_1, (v4f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vfcmp_cun_s(__m128 _1, __m128 _2) {
  return (__m128i)__builtin_lsx_vfcmp_cun_s((v4f32)_1, (v4f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vfcmp_saf_d(__m128d _1, __m128d _2) {
  return (__m128i)__builtin_lsx_vfcmp_saf_d((v2f64)_1, (v2f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vfcmp_saf_s(__m128 _1, __m128 _2) {
  return (__m128i)__builtin_lsx_vfcmp_saf_s((v4f32)_1, (v4f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vfcmp_seq_d(__m128d _1, __m128d _2) {
  return (__m128i)__builtin_lsx_vfcmp_seq_d((v2f64)_1, (v2f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vfcmp_seq_s(__m128 _1, __m128 _2) {
  return (__m128i)__builtin_lsx_vfcmp_seq_s((v4f32)_1, (v4f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vfcmp_sle_d(__m128d _1, __m128d _2) {
  return (__m128i)__builtin_lsx_vfcmp_sle_d((v2f64)_1, (v2f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vfcmp_sle_s(__m128 _1, __m128 _2) {
  return (__m128i)__builtin_lsx_vfcmp_sle_s((v4f32)_1, (v4f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vfcmp_slt_d(__m128d _1, __m128d _2) {
  return (__m128i)__builtin_lsx_vfcmp_slt_d((v2f64)_1, (v2f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vfcmp_slt_s(__m128 _1, __m128 _2) {
  return (__m128i)__builtin_lsx_vfcmp_slt_s((v4f32)_1, (v4f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vfcmp_sne_d(__m128d _1, __m128d _2) {
  return (__m128i)__builtin_lsx_vfcmp_sne_d((v2f64)_1, (v2f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vfcmp_sne_s(__m128 _1, __m128 _2) {
  return (__m128i)__builtin_lsx_vfcmp_sne_s((v4f32)_1, (v4f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vfcmp_sor_d(__m128d _1, __m128d _2) {
  return (__m128i)__builtin_lsx_vfcmp_sor_d((v2f64)_1, (v2f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vfcmp_sor_s(__m128 _1, __m128 _2) {
  return (__m128i)__builtin_lsx_vfcmp_sor_s((v4f32)_1, (v4f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vfcmp_sueq_d(__m128d _1, __m128d _2) {
  return (__m128i)__builtin_lsx_vfcmp_sueq_d((v2f64)_1, (v2f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vfcmp_sueq_s(__m128 _1, __m128 _2) {
  return (__m128i)__builtin_lsx_vfcmp_sueq_s((v4f32)_1, (v4f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vfcmp_sule_d(__m128d _1, __m128d _2) {
  return (__m128i)__builtin_lsx_vfcmp_sule_d((v2f64)_1, (v2f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vfcmp_sule_s(__m128 _1, __m128 _2) {
  return (__m128i)__builtin_lsx_vfcmp_sule_s((v4f32)_1, (v4f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vfcmp_sult_d(__m128d _1, __m128d _2) {
  return (__m128i)__builtin_lsx_vfcmp_sult_d((v2f64)_1, (v2f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vfcmp_sult_s(__m128 _1, __m128 _2) {
  return (__m128i)__builtin_lsx_vfcmp_sult_s((v4f32)_1, (v4f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vfcmp_sun_d(__m128d _1, __m128d _2) {
  return (__m128i)__builtin_lsx_vfcmp_sun_d((v2f64)_1, (v2f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vfcmp_sune_d(__m128d _1, __m128d _2) {
  return (__m128i)__builtin_lsx_vfcmp_sune_d((v2f64)_1, (v2f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vfcmp_sune_s(__m128 _1, __m128 _2) {
  return (__m128i)__builtin_lsx_vfcmp_sune_s((v4f32)_1, (v4f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m128i
    __lsx_vfcmp_sun_s(__m128 _1, __m128 _2) {
  return (__m128i)__builtin_lsx_vfcmp_sun_s((v4f32)_1, (v4f32)_2);
}

#define __lsx_vrepli_b(/*si10*/ _1) ((__m128i)__builtin_lsx_vrepli_b((_1)))

#define __lsx_vrepli_d(/*si10*/ _1) ((__m128i)__builtin_lsx_vrepli_d((_1)))

#define __lsx_vrepli_h(/*si10*/ _1) ((__m128i)__builtin_lsx_vrepli_h((_1)))

#define __lsx_vrepli_w(/*si10*/ _1) ((__m128i)__builtin_lsx_vrepli_w((_1)))

#endif /* defined(__loongarch_sx) */
#endif /* _LOONGSON_SXINTRIN_H */
