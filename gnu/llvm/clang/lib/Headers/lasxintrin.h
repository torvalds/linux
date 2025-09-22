/*===------------ lasxintrin.h - LoongArch LASX intrinsics -----------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef _LOONGSON_ASXINTRIN_H
#define _LOONGSON_ASXINTRIN_H 1

#if defined(__loongarch_asx)

typedef signed char v32i8 __attribute__((vector_size(32), aligned(32)));
typedef signed char v32i8_b __attribute__((vector_size(32), aligned(1)));
typedef unsigned char v32u8 __attribute__((vector_size(32), aligned(32)));
typedef unsigned char v32u8_b __attribute__((vector_size(32), aligned(1)));
typedef short v16i16 __attribute__((vector_size(32), aligned(32)));
typedef short v16i16_h __attribute__((vector_size(32), aligned(2)));
typedef unsigned short v16u16 __attribute__((vector_size(32), aligned(32)));
typedef unsigned short v16u16_h __attribute__((vector_size(32), aligned(2)));
typedef int v8i32 __attribute__((vector_size(32), aligned(32)));
typedef int v8i32_w __attribute__((vector_size(32), aligned(4)));
typedef unsigned int v8u32 __attribute__((vector_size(32), aligned(32)));
typedef unsigned int v8u32_w __attribute__((vector_size(32), aligned(4)));
typedef long long v4i64 __attribute__((vector_size(32), aligned(32)));
typedef long long v4i64_d __attribute__((vector_size(32), aligned(8)));
typedef unsigned long long v4u64 __attribute__((vector_size(32), aligned(32)));
typedef unsigned long long v4u64_d __attribute__((vector_size(32), aligned(8)));
typedef float v8f32 __attribute__((vector_size(32), aligned(32)));
typedef float v8f32_w __attribute__((vector_size(32), aligned(4)));
typedef double v4f64 __attribute__((vector_size(32), aligned(32)));
typedef double v4f64_d __attribute__((vector_size(32), aligned(8)));

typedef double v4f64 __attribute__((vector_size(32), aligned(32)));
typedef double v4f64_d __attribute__((vector_size(32), aligned(8)));

typedef float __m256 __attribute__((__vector_size__(32), __may_alias__));
typedef long long __m256i __attribute__((__vector_size__(32), __may_alias__));
typedef double __m256d __attribute__((__vector_size__(32), __may_alias__));

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsll_b(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsll_b((v32i8)_1, (v32i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsll_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsll_h((v16i16)_1, (v16i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsll_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsll_w((v8i32)_1, (v8i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsll_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsll_d((v4i64)_1, (v4i64)_2);
}

#define __lasx_xvslli_b(/*__m256i*/ _1, /*ui3*/ _2)                            \
  ((__m256i)__builtin_lasx_xvslli_b((v32i8)(_1), (_2)))

#define __lasx_xvslli_h(/*__m256i*/ _1, /*ui4*/ _2)                            \
  ((__m256i)__builtin_lasx_xvslli_h((v16i16)(_1), (_2)))

#define __lasx_xvslli_w(/*__m256i*/ _1, /*ui5*/ _2)                            \
  ((__m256i)__builtin_lasx_xvslli_w((v8i32)(_1), (_2)))

#define __lasx_xvslli_d(/*__m256i*/ _1, /*ui6*/ _2)                            \
  ((__m256i)__builtin_lasx_xvslli_d((v4i64)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsra_b(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsra_b((v32i8)_1, (v32i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsra_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsra_h((v16i16)_1, (v16i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsra_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsra_w((v8i32)_1, (v8i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsra_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsra_d((v4i64)_1, (v4i64)_2);
}

#define __lasx_xvsrai_b(/*__m256i*/ _1, /*ui3*/ _2)                            \
  ((__m256i)__builtin_lasx_xvsrai_b((v32i8)(_1), (_2)))

#define __lasx_xvsrai_h(/*__m256i*/ _1, /*ui4*/ _2)                            \
  ((__m256i)__builtin_lasx_xvsrai_h((v16i16)(_1), (_2)))

#define __lasx_xvsrai_w(/*__m256i*/ _1, /*ui5*/ _2)                            \
  ((__m256i)__builtin_lasx_xvsrai_w((v8i32)(_1), (_2)))

#define __lasx_xvsrai_d(/*__m256i*/ _1, /*ui6*/ _2)                            \
  ((__m256i)__builtin_lasx_xvsrai_d((v4i64)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsrar_b(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsrar_b((v32i8)_1, (v32i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsrar_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsrar_h((v16i16)_1, (v16i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsrar_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsrar_w((v8i32)_1, (v8i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsrar_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsrar_d((v4i64)_1, (v4i64)_2);
}

#define __lasx_xvsrari_b(/*__m256i*/ _1, /*ui3*/ _2)                           \
  ((__m256i)__builtin_lasx_xvsrari_b((v32i8)(_1), (_2)))

#define __lasx_xvsrari_h(/*__m256i*/ _1, /*ui4*/ _2)                           \
  ((__m256i)__builtin_lasx_xvsrari_h((v16i16)(_1), (_2)))

#define __lasx_xvsrari_w(/*__m256i*/ _1, /*ui5*/ _2)                           \
  ((__m256i)__builtin_lasx_xvsrari_w((v8i32)(_1), (_2)))

#define __lasx_xvsrari_d(/*__m256i*/ _1, /*ui6*/ _2)                           \
  ((__m256i)__builtin_lasx_xvsrari_d((v4i64)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsrl_b(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsrl_b((v32i8)_1, (v32i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsrl_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsrl_h((v16i16)_1, (v16i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsrl_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsrl_w((v8i32)_1, (v8i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsrl_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsrl_d((v4i64)_1, (v4i64)_2);
}

#define __lasx_xvsrli_b(/*__m256i*/ _1, /*ui3*/ _2)                            \
  ((__m256i)__builtin_lasx_xvsrli_b((v32i8)(_1), (_2)))

#define __lasx_xvsrli_h(/*__m256i*/ _1, /*ui4*/ _2)                            \
  ((__m256i)__builtin_lasx_xvsrli_h((v16i16)(_1), (_2)))

#define __lasx_xvsrli_w(/*__m256i*/ _1, /*ui5*/ _2)                            \
  ((__m256i)__builtin_lasx_xvsrli_w((v8i32)(_1), (_2)))

#define __lasx_xvsrli_d(/*__m256i*/ _1, /*ui6*/ _2)                            \
  ((__m256i)__builtin_lasx_xvsrli_d((v4i64)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsrlr_b(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsrlr_b((v32i8)_1, (v32i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsrlr_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsrlr_h((v16i16)_1, (v16i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsrlr_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsrlr_w((v8i32)_1, (v8i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsrlr_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsrlr_d((v4i64)_1, (v4i64)_2);
}

#define __lasx_xvsrlri_b(/*__m256i*/ _1, /*ui3*/ _2)                           \
  ((__m256i)__builtin_lasx_xvsrlri_b((v32i8)(_1), (_2)))

#define __lasx_xvsrlri_h(/*__m256i*/ _1, /*ui4*/ _2)                           \
  ((__m256i)__builtin_lasx_xvsrlri_h((v16i16)(_1), (_2)))

#define __lasx_xvsrlri_w(/*__m256i*/ _1, /*ui5*/ _2)                           \
  ((__m256i)__builtin_lasx_xvsrlri_w((v8i32)(_1), (_2)))

#define __lasx_xvsrlri_d(/*__m256i*/ _1, /*ui6*/ _2)                           \
  ((__m256i)__builtin_lasx_xvsrlri_d((v4i64)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvbitclr_b(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvbitclr_b((v32u8)_1, (v32u8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvbitclr_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvbitclr_h((v16u16)_1, (v16u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvbitclr_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvbitclr_w((v8u32)_1, (v8u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvbitclr_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvbitclr_d((v4u64)_1, (v4u64)_2);
}

#define __lasx_xvbitclri_b(/*__m256i*/ _1, /*ui3*/ _2)                         \
  ((__m256i)__builtin_lasx_xvbitclri_b((v32u8)(_1), (_2)))

#define __lasx_xvbitclri_h(/*__m256i*/ _1, /*ui4*/ _2)                         \
  ((__m256i)__builtin_lasx_xvbitclri_h((v16u16)(_1), (_2)))

#define __lasx_xvbitclri_w(/*__m256i*/ _1, /*ui5*/ _2)                         \
  ((__m256i)__builtin_lasx_xvbitclri_w((v8u32)(_1), (_2)))

#define __lasx_xvbitclri_d(/*__m256i*/ _1, /*ui6*/ _2)                         \
  ((__m256i)__builtin_lasx_xvbitclri_d((v4u64)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvbitset_b(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvbitset_b((v32u8)_1, (v32u8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvbitset_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvbitset_h((v16u16)_1, (v16u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvbitset_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvbitset_w((v8u32)_1, (v8u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvbitset_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvbitset_d((v4u64)_1, (v4u64)_2);
}

#define __lasx_xvbitseti_b(/*__m256i*/ _1, /*ui3*/ _2)                         \
  ((__m256i)__builtin_lasx_xvbitseti_b((v32u8)(_1), (_2)))

#define __lasx_xvbitseti_h(/*__m256i*/ _1, /*ui4*/ _2)                         \
  ((__m256i)__builtin_lasx_xvbitseti_h((v16u16)(_1), (_2)))

#define __lasx_xvbitseti_w(/*__m256i*/ _1, /*ui5*/ _2)                         \
  ((__m256i)__builtin_lasx_xvbitseti_w((v8u32)(_1), (_2)))

#define __lasx_xvbitseti_d(/*__m256i*/ _1, /*ui6*/ _2)                         \
  ((__m256i)__builtin_lasx_xvbitseti_d((v4u64)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvbitrev_b(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvbitrev_b((v32u8)_1, (v32u8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvbitrev_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvbitrev_h((v16u16)_1, (v16u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvbitrev_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvbitrev_w((v8u32)_1, (v8u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvbitrev_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvbitrev_d((v4u64)_1, (v4u64)_2);
}

#define __lasx_xvbitrevi_b(/*__m256i*/ _1, /*ui3*/ _2)                         \
  ((__m256i)__builtin_lasx_xvbitrevi_b((v32u8)(_1), (_2)))

#define __lasx_xvbitrevi_h(/*__m256i*/ _1, /*ui4*/ _2)                         \
  ((__m256i)__builtin_lasx_xvbitrevi_h((v16u16)(_1), (_2)))

#define __lasx_xvbitrevi_w(/*__m256i*/ _1, /*ui5*/ _2)                         \
  ((__m256i)__builtin_lasx_xvbitrevi_w((v8u32)(_1), (_2)))

#define __lasx_xvbitrevi_d(/*__m256i*/ _1, /*ui6*/ _2)                         \
  ((__m256i)__builtin_lasx_xvbitrevi_d((v4u64)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvadd_b(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvadd_b((v32i8)_1, (v32i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvadd_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvadd_h((v16i16)_1, (v16i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvadd_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvadd_w((v8i32)_1, (v8i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvadd_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvadd_d((v4i64)_1, (v4i64)_2);
}

#define __lasx_xvaddi_bu(/*__m256i*/ _1, /*ui5*/ _2)                           \
  ((__m256i)__builtin_lasx_xvaddi_bu((v32i8)(_1), (_2)))

#define __lasx_xvaddi_hu(/*__m256i*/ _1, /*ui5*/ _2)                           \
  ((__m256i)__builtin_lasx_xvaddi_hu((v16i16)(_1), (_2)))

#define __lasx_xvaddi_wu(/*__m256i*/ _1, /*ui5*/ _2)                           \
  ((__m256i)__builtin_lasx_xvaddi_wu((v8i32)(_1), (_2)))

#define __lasx_xvaddi_du(/*__m256i*/ _1, /*ui5*/ _2)                           \
  ((__m256i)__builtin_lasx_xvaddi_du((v4i64)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsub_b(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsub_b((v32i8)_1, (v32i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsub_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsub_h((v16i16)_1, (v16i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsub_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsub_w((v8i32)_1, (v8i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsub_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsub_d((v4i64)_1, (v4i64)_2);
}

#define __lasx_xvsubi_bu(/*__m256i*/ _1, /*ui5*/ _2)                           \
  ((__m256i)__builtin_lasx_xvsubi_bu((v32i8)(_1), (_2)))

#define __lasx_xvsubi_hu(/*__m256i*/ _1, /*ui5*/ _2)                           \
  ((__m256i)__builtin_lasx_xvsubi_hu((v16i16)(_1), (_2)))

#define __lasx_xvsubi_wu(/*__m256i*/ _1, /*ui5*/ _2)                           \
  ((__m256i)__builtin_lasx_xvsubi_wu((v8i32)(_1), (_2)))

#define __lasx_xvsubi_du(/*__m256i*/ _1, /*ui5*/ _2)                           \
  ((__m256i)__builtin_lasx_xvsubi_du((v4i64)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmax_b(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmax_b((v32i8)_1, (v32i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmax_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmax_h((v16i16)_1, (v16i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmax_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmax_w((v8i32)_1, (v8i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmax_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmax_d((v4i64)_1, (v4i64)_2);
}

#define __lasx_xvmaxi_b(/*__m256i*/ _1, /*si5*/ _2)                            \
  ((__m256i)__builtin_lasx_xvmaxi_b((v32i8)(_1), (_2)))

#define __lasx_xvmaxi_h(/*__m256i*/ _1, /*si5*/ _2)                            \
  ((__m256i)__builtin_lasx_xvmaxi_h((v16i16)(_1), (_2)))

#define __lasx_xvmaxi_w(/*__m256i*/ _1, /*si5*/ _2)                            \
  ((__m256i)__builtin_lasx_xvmaxi_w((v8i32)(_1), (_2)))

#define __lasx_xvmaxi_d(/*__m256i*/ _1, /*si5*/ _2)                            \
  ((__m256i)__builtin_lasx_xvmaxi_d((v4i64)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmax_bu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmax_bu((v32u8)_1, (v32u8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmax_hu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmax_hu((v16u16)_1, (v16u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmax_wu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmax_wu((v8u32)_1, (v8u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmax_du(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmax_du((v4u64)_1, (v4u64)_2);
}

#define __lasx_xvmaxi_bu(/*__m256i*/ _1, /*ui5*/ _2)                           \
  ((__m256i)__builtin_lasx_xvmaxi_bu((v32u8)(_1), (_2)))

#define __lasx_xvmaxi_hu(/*__m256i*/ _1, /*ui5*/ _2)                           \
  ((__m256i)__builtin_lasx_xvmaxi_hu((v16u16)(_1), (_2)))

#define __lasx_xvmaxi_wu(/*__m256i*/ _1, /*ui5*/ _2)                           \
  ((__m256i)__builtin_lasx_xvmaxi_wu((v8u32)(_1), (_2)))

#define __lasx_xvmaxi_du(/*__m256i*/ _1, /*ui5*/ _2)                           \
  ((__m256i)__builtin_lasx_xvmaxi_du((v4u64)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmin_b(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmin_b((v32i8)_1, (v32i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmin_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmin_h((v16i16)_1, (v16i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmin_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmin_w((v8i32)_1, (v8i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmin_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmin_d((v4i64)_1, (v4i64)_2);
}

#define __lasx_xvmini_b(/*__m256i*/ _1, /*si5*/ _2)                            \
  ((__m256i)__builtin_lasx_xvmini_b((v32i8)(_1), (_2)))

#define __lasx_xvmini_h(/*__m256i*/ _1, /*si5*/ _2)                            \
  ((__m256i)__builtin_lasx_xvmini_h((v16i16)(_1), (_2)))

#define __lasx_xvmini_w(/*__m256i*/ _1, /*si5*/ _2)                            \
  ((__m256i)__builtin_lasx_xvmini_w((v8i32)(_1), (_2)))

#define __lasx_xvmini_d(/*__m256i*/ _1, /*si5*/ _2)                            \
  ((__m256i)__builtin_lasx_xvmini_d((v4i64)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmin_bu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmin_bu((v32u8)_1, (v32u8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmin_hu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmin_hu((v16u16)_1, (v16u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmin_wu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmin_wu((v8u32)_1, (v8u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmin_du(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmin_du((v4u64)_1, (v4u64)_2);
}

#define __lasx_xvmini_bu(/*__m256i*/ _1, /*ui5*/ _2)                           \
  ((__m256i)__builtin_lasx_xvmini_bu((v32u8)(_1), (_2)))

#define __lasx_xvmini_hu(/*__m256i*/ _1, /*ui5*/ _2)                           \
  ((__m256i)__builtin_lasx_xvmini_hu((v16u16)(_1), (_2)))

#define __lasx_xvmini_wu(/*__m256i*/ _1, /*ui5*/ _2)                           \
  ((__m256i)__builtin_lasx_xvmini_wu((v8u32)(_1), (_2)))

#define __lasx_xvmini_du(/*__m256i*/ _1, /*ui5*/ _2)                           \
  ((__m256i)__builtin_lasx_xvmini_du((v4u64)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvseq_b(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvseq_b((v32i8)_1, (v32i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvseq_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvseq_h((v16i16)_1, (v16i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvseq_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvseq_w((v8i32)_1, (v8i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvseq_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvseq_d((v4i64)_1, (v4i64)_2);
}

#define __lasx_xvseqi_b(/*__m256i*/ _1, /*si5*/ _2)                            \
  ((__m256i)__builtin_lasx_xvseqi_b((v32i8)(_1), (_2)))

#define __lasx_xvseqi_h(/*__m256i*/ _1, /*si5*/ _2)                            \
  ((__m256i)__builtin_lasx_xvseqi_h((v16i16)(_1), (_2)))

#define __lasx_xvseqi_w(/*__m256i*/ _1, /*si5*/ _2)                            \
  ((__m256i)__builtin_lasx_xvseqi_w((v8i32)(_1), (_2)))

#define __lasx_xvseqi_d(/*__m256i*/ _1, /*si5*/ _2)                            \
  ((__m256i)__builtin_lasx_xvseqi_d((v4i64)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvslt_b(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvslt_b((v32i8)_1, (v32i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvslt_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvslt_h((v16i16)_1, (v16i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvslt_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvslt_w((v8i32)_1, (v8i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvslt_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvslt_d((v4i64)_1, (v4i64)_2);
}

#define __lasx_xvslti_b(/*__m256i*/ _1, /*si5*/ _2)                            \
  ((__m256i)__builtin_lasx_xvslti_b((v32i8)(_1), (_2)))

#define __lasx_xvslti_h(/*__m256i*/ _1, /*si5*/ _2)                            \
  ((__m256i)__builtin_lasx_xvslti_h((v16i16)(_1), (_2)))

#define __lasx_xvslti_w(/*__m256i*/ _1, /*si5*/ _2)                            \
  ((__m256i)__builtin_lasx_xvslti_w((v8i32)(_1), (_2)))

#define __lasx_xvslti_d(/*__m256i*/ _1, /*si5*/ _2)                            \
  ((__m256i)__builtin_lasx_xvslti_d((v4i64)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvslt_bu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvslt_bu((v32u8)_1, (v32u8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvslt_hu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvslt_hu((v16u16)_1, (v16u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvslt_wu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvslt_wu((v8u32)_1, (v8u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvslt_du(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvslt_du((v4u64)_1, (v4u64)_2);
}

#define __lasx_xvslti_bu(/*__m256i*/ _1, /*ui5*/ _2)                           \
  ((__m256i)__builtin_lasx_xvslti_bu((v32u8)(_1), (_2)))

#define __lasx_xvslti_hu(/*__m256i*/ _1, /*ui5*/ _2)                           \
  ((__m256i)__builtin_lasx_xvslti_hu((v16u16)(_1), (_2)))

#define __lasx_xvslti_wu(/*__m256i*/ _1, /*ui5*/ _2)                           \
  ((__m256i)__builtin_lasx_xvslti_wu((v8u32)(_1), (_2)))

#define __lasx_xvslti_du(/*__m256i*/ _1, /*ui5*/ _2)                           \
  ((__m256i)__builtin_lasx_xvslti_du((v4u64)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsle_b(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsle_b((v32i8)_1, (v32i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsle_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsle_h((v16i16)_1, (v16i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsle_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsle_w((v8i32)_1, (v8i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsle_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsle_d((v4i64)_1, (v4i64)_2);
}

#define __lasx_xvslei_b(/*__m256i*/ _1, /*si5*/ _2)                            \
  ((__m256i)__builtin_lasx_xvslei_b((v32i8)(_1), (_2)))

#define __lasx_xvslei_h(/*__m256i*/ _1, /*si5*/ _2)                            \
  ((__m256i)__builtin_lasx_xvslei_h((v16i16)(_1), (_2)))

#define __lasx_xvslei_w(/*__m256i*/ _1, /*si5*/ _2)                            \
  ((__m256i)__builtin_lasx_xvslei_w((v8i32)(_1), (_2)))

#define __lasx_xvslei_d(/*__m256i*/ _1, /*si5*/ _2)                            \
  ((__m256i)__builtin_lasx_xvslei_d((v4i64)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsle_bu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsle_bu((v32u8)_1, (v32u8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsle_hu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsle_hu((v16u16)_1, (v16u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsle_wu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsle_wu((v8u32)_1, (v8u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsle_du(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsle_du((v4u64)_1, (v4u64)_2);
}

#define __lasx_xvslei_bu(/*__m256i*/ _1, /*ui5*/ _2)                           \
  ((__m256i)__builtin_lasx_xvslei_bu((v32u8)(_1), (_2)))

#define __lasx_xvslei_hu(/*__m256i*/ _1, /*ui5*/ _2)                           \
  ((__m256i)__builtin_lasx_xvslei_hu((v16u16)(_1), (_2)))

#define __lasx_xvslei_wu(/*__m256i*/ _1, /*ui5*/ _2)                           \
  ((__m256i)__builtin_lasx_xvslei_wu((v8u32)(_1), (_2)))

#define __lasx_xvslei_du(/*__m256i*/ _1, /*ui5*/ _2)                           \
  ((__m256i)__builtin_lasx_xvslei_du((v4u64)(_1), (_2)))

#define __lasx_xvsat_b(/*__m256i*/ _1, /*ui3*/ _2)                             \
  ((__m256i)__builtin_lasx_xvsat_b((v32i8)(_1), (_2)))

#define __lasx_xvsat_h(/*__m256i*/ _1, /*ui4*/ _2)                             \
  ((__m256i)__builtin_lasx_xvsat_h((v16i16)(_1), (_2)))

#define __lasx_xvsat_w(/*__m256i*/ _1, /*ui5*/ _2)                             \
  ((__m256i)__builtin_lasx_xvsat_w((v8i32)(_1), (_2)))

#define __lasx_xvsat_d(/*__m256i*/ _1, /*ui6*/ _2)                             \
  ((__m256i)__builtin_lasx_xvsat_d((v4i64)(_1), (_2)))

#define __lasx_xvsat_bu(/*__m256i*/ _1, /*ui3*/ _2)                            \
  ((__m256i)__builtin_lasx_xvsat_bu((v32u8)(_1), (_2)))

#define __lasx_xvsat_hu(/*__m256i*/ _1, /*ui4*/ _2)                            \
  ((__m256i)__builtin_lasx_xvsat_hu((v16u16)(_1), (_2)))

#define __lasx_xvsat_wu(/*__m256i*/ _1, /*ui5*/ _2)                            \
  ((__m256i)__builtin_lasx_xvsat_wu((v8u32)(_1), (_2)))

#define __lasx_xvsat_du(/*__m256i*/ _1, /*ui6*/ _2)                            \
  ((__m256i)__builtin_lasx_xvsat_du((v4u64)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvadda_b(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvadda_b((v32i8)_1, (v32i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvadda_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvadda_h((v16i16)_1, (v16i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvadda_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvadda_w((v8i32)_1, (v8i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvadda_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvadda_d((v4i64)_1, (v4i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsadd_b(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsadd_b((v32i8)_1, (v32i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsadd_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsadd_h((v16i16)_1, (v16i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsadd_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsadd_w((v8i32)_1, (v8i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsadd_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsadd_d((v4i64)_1, (v4i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsadd_bu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsadd_bu((v32u8)_1, (v32u8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsadd_hu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsadd_hu((v16u16)_1, (v16u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsadd_wu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsadd_wu((v8u32)_1, (v8u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsadd_du(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsadd_du((v4u64)_1, (v4u64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvavg_b(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvavg_b((v32i8)_1, (v32i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvavg_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvavg_h((v16i16)_1, (v16i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvavg_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvavg_w((v8i32)_1, (v8i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvavg_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvavg_d((v4i64)_1, (v4i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvavg_bu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvavg_bu((v32u8)_1, (v32u8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvavg_hu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvavg_hu((v16u16)_1, (v16u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvavg_wu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvavg_wu((v8u32)_1, (v8u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvavg_du(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvavg_du((v4u64)_1, (v4u64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvavgr_b(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvavgr_b((v32i8)_1, (v32i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvavgr_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvavgr_h((v16i16)_1, (v16i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvavgr_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvavgr_w((v8i32)_1, (v8i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvavgr_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvavgr_d((v4i64)_1, (v4i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvavgr_bu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvavgr_bu((v32u8)_1, (v32u8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvavgr_hu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvavgr_hu((v16u16)_1, (v16u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvavgr_wu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvavgr_wu((v8u32)_1, (v8u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvavgr_du(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvavgr_du((v4u64)_1, (v4u64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvssub_b(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvssub_b((v32i8)_1, (v32i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvssub_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvssub_h((v16i16)_1, (v16i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvssub_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvssub_w((v8i32)_1, (v8i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvssub_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvssub_d((v4i64)_1, (v4i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvssub_bu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvssub_bu((v32u8)_1, (v32u8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvssub_hu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvssub_hu((v16u16)_1, (v16u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvssub_wu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvssub_wu((v8u32)_1, (v8u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvssub_du(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvssub_du((v4u64)_1, (v4u64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvabsd_b(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvabsd_b((v32i8)_1, (v32i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvabsd_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvabsd_h((v16i16)_1, (v16i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvabsd_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvabsd_w((v8i32)_1, (v8i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvabsd_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvabsd_d((v4i64)_1, (v4i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvabsd_bu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvabsd_bu((v32u8)_1, (v32u8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvabsd_hu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvabsd_hu((v16u16)_1, (v16u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvabsd_wu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvabsd_wu((v8u32)_1, (v8u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvabsd_du(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvabsd_du((v4u64)_1, (v4u64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmul_b(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmul_b((v32i8)_1, (v32i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmul_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmul_h((v16i16)_1, (v16i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmul_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmul_w((v8i32)_1, (v8i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmul_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmul_d((v4i64)_1, (v4i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmadd_b(__m256i _1, __m256i _2, __m256i _3) {
  return (__m256i)__builtin_lasx_xvmadd_b((v32i8)_1, (v32i8)_2, (v32i8)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmadd_h(__m256i _1, __m256i _2, __m256i _3) {
  return (__m256i)__builtin_lasx_xvmadd_h((v16i16)_1, (v16i16)_2, (v16i16)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmadd_w(__m256i _1, __m256i _2, __m256i _3) {
  return (__m256i)__builtin_lasx_xvmadd_w((v8i32)_1, (v8i32)_2, (v8i32)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmadd_d(__m256i _1, __m256i _2, __m256i _3) {
  return (__m256i)__builtin_lasx_xvmadd_d((v4i64)_1, (v4i64)_2, (v4i64)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmsub_b(__m256i _1, __m256i _2, __m256i _3) {
  return (__m256i)__builtin_lasx_xvmsub_b((v32i8)_1, (v32i8)_2, (v32i8)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmsub_h(__m256i _1, __m256i _2, __m256i _3) {
  return (__m256i)__builtin_lasx_xvmsub_h((v16i16)_1, (v16i16)_2, (v16i16)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmsub_w(__m256i _1, __m256i _2, __m256i _3) {
  return (__m256i)__builtin_lasx_xvmsub_w((v8i32)_1, (v8i32)_2, (v8i32)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmsub_d(__m256i _1, __m256i _2, __m256i _3) {
  return (__m256i)__builtin_lasx_xvmsub_d((v4i64)_1, (v4i64)_2, (v4i64)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvdiv_b(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvdiv_b((v32i8)_1, (v32i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvdiv_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvdiv_h((v16i16)_1, (v16i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvdiv_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvdiv_w((v8i32)_1, (v8i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvdiv_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvdiv_d((v4i64)_1, (v4i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvdiv_bu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvdiv_bu((v32u8)_1, (v32u8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvdiv_hu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvdiv_hu((v16u16)_1, (v16u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvdiv_wu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvdiv_wu((v8u32)_1, (v8u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvdiv_du(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvdiv_du((v4u64)_1, (v4u64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvhaddw_h_b(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvhaddw_h_b((v32i8)_1, (v32i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvhaddw_w_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvhaddw_w_h((v16i16)_1, (v16i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvhaddw_d_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvhaddw_d_w((v8i32)_1, (v8i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvhaddw_hu_bu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvhaddw_hu_bu((v32u8)_1, (v32u8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvhaddw_wu_hu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvhaddw_wu_hu((v16u16)_1, (v16u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvhaddw_du_wu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvhaddw_du_wu((v8u32)_1, (v8u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvhsubw_h_b(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvhsubw_h_b((v32i8)_1, (v32i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvhsubw_w_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvhsubw_w_h((v16i16)_1, (v16i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvhsubw_d_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvhsubw_d_w((v8i32)_1, (v8i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvhsubw_hu_bu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvhsubw_hu_bu((v32u8)_1, (v32u8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvhsubw_wu_hu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvhsubw_wu_hu((v16u16)_1, (v16u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvhsubw_du_wu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvhsubw_du_wu((v8u32)_1, (v8u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmod_b(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmod_b((v32i8)_1, (v32i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmod_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmod_h((v16i16)_1, (v16i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmod_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmod_w((v8i32)_1, (v8i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmod_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmod_d((v4i64)_1, (v4i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmod_bu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmod_bu((v32u8)_1, (v32u8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmod_hu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmod_hu((v16u16)_1, (v16u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmod_wu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmod_wu((v8u32)_1, (v8u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmod_du(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmod_du((v4u64)_1, (v4u64)_2);
}

#define __lasx_xvrepl128vei_b(/*__m256i*/ _1, /*ui4*/ _2)                      \
  ((__m256i)__builtin_lasx_xvrepl128vei_b((v32i8)(_1), (_2)))

#define __lasx_xvrepl128vei_h(/*__m256i*/ _1, /*ui3*/ _2)                      \
  ((__m256i)__builtin_lasx_xvrepl128vei_h((v16i16)(_1), (_2)))

#define __lasx_xvrepl128vei_w(/*__m256i*/ _1, /*ui2*/ _2)                      \
  ((__m256i)__builtin_lasx_xvrepl128vei_w((v8i32)(_1), (_2)))

#define __lasx_xvrepl128vei_d(/*__m256i*/ _1, /*ui1*/ _2)                      \
  ((__m256i)__builtin_lasx_xvrepl128vei_d((v4i64)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvpickev_b(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvpickev_b((v32i8)_1, (v32i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvpickev_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvpickev_h((v16i16)_1, (v16i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvpickev_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvpickev_w((v8i32)_1, (v8i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvpickev_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvpickev_d((v4i64)_1, (v4i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvpickod_b(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvpickod_b((v32i8)_1, (v32i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvpickod_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvpickod_h((v16i16)_1, (v16i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvpickod_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvpickod_w((v8i32)_1, (v8i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvpickod_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvpickod_d((v4i64)_1, (v4i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvilvh_b(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvilvh_b((v32i8)_1, (v32i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvilvh_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvilvh_h((v16i16)_1, (v16i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvilvh_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvilvh_w((v8i32)_1, (v8i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvilvh_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvilvh_d((v4i64)_1, (v4i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvilvl_b(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvilvl_b((v32i8)_1, (v32i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvilvl_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvilvl_h((v16i16)_1, (v16i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvilvl_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvilvl_w((v8i32)_1, (v8i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvilvl_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvilvl_d((v4i64)_1, (v4i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvpackev_b(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvpackev_b((v32i8)_1, (v32i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvpackev_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvpackev_h((v16i16)_1, (v16i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvpackev_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvpackev_w((v8i32)_1, (v8i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvpackev_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvpackev_d((v4i64)_1, (v4i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvpackod_b(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvpackod_b((v32i8)_1, (v32i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvpackod_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvpackod_h((v16i16)_1, (v16i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvpackod_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvpackod_w((v8i32)_1, (v8i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvpackod_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvpackod_d((v4i64)_1, (v4i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvshuf_b(__m256i _1, __m256i _2, __m256i _3) {
  return (__m256i)__builtin_lasx_xvshuf_b((v32i8)_1, (v32i8)_2, (v32i8)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvshuf_h(__m256i _1, __m256i _2, __m256i _3) {
  return (__m256i)__builtin_lasx_xvshuf_h((v16i16)_1, (v16i16)_2, (v16i16)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvshuf_w(__m256i _1, __m256i _2, __m256i _3) {
  return (__m256i)__builtin_lasx_xvshuf_w((v8i32)_1, (v8i32)_2, (v8i32)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvshuf_d(__m256i _1, __m256i _2, __m256i _3) {
  return (__m256i)__builtin_lasx_xvshuf_d((v4i64)_1, (v4i64)_2, (v4i64)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvand_v(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvand_v((v32u8)_1, (v32u8)_2);
}

#define __lasx_xvandi_b(/*__m256i*/ _1, /*ui8*/ _2)                            \
  ((__m256i)__builtin_lasx_xvandi_b((v32u8)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvor_v(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvor_v((v32u8)_1, (v32u8)_2);
}

#define __lasx_xvori_b(/*__m256i*/ _1, /*ui8*/ _2)                             \
  ((__m256i)__builtin_lasx_xvori_b((v32u8)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvnor_v(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvnor_v((v32u8)_1, (v32u8)_2);
}

#define __lasx_xvnori_b(/*__m256i*/ _1, /*ui8*/ _2)                            \
  ((__m256i)__builtin_lasx_xvnori_b((v32u8)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvxor_v(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvxor_v((v32u8)_1, (v32u8)_2);
}

#define __lasx_xvxori_b(/*__m256i*/ _1, /*ui8*/ _2)                            \
  ((__m256i)__builtin_lasx_xvxori_b((v32u8)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvbitsel_v(__m256i _1, __m256i _2, __m256i _3) {
  return (__m256i)__builtin_lasx_xvbitsel_v((v32u8)_1, (v32u8)_2, (v32u8)_3);
}

#define __lasx_xvbitseli_b(/*__m256i*/ _1, /*__m256i*/ _2, /*ui8*/ _3)         \
  ((__m256i)__builtin_lasx_xvbitseli_b((v32u8)(_1), (v32u8)(_2), (_3)))

#define __lasx_xvshuf4i_b(/*__m256i*/ _1, /*ui8*/ _2)                          \
  ((__m256i)__builtin_lasx_xvshuf4i_b((v32i8)(_1), (_2)))

#define __lasx_xvshuf4i_h(/*__m256i*/ _1, /*ui8*/ _2)                          \
  ((__m256i)__builtin_lasx_xvshuf4i_h((v16i16)(_1), (_2)))

#define __lasx_xvshuf4i_w(/*__m256i*/ _1, /*ui8*/ _2)                          \
  ((__m256i)__builtin_lasx_xvshuf4i_w((v8i32)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvreplgr2vr_b(int _1) {
  return (__m256i)__builtin_lasx_xvreplgr2vr_b((int)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvreplgr2vr_h(int _1) {
  return (__m256i)__builtin_lasx_xvreplgr2vr_h((int)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvreplgr2vr_w(int _1) {
  return (__m256i)__builtin_lasx_xvreplgr2vr_w((int)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvreplgr2vr_d(long int _1) {
  return (__m256i)__builtin_lasx_xvreplgr2vr_d((long int)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvpcnt_b(__m256i _1) {
  return (__m256i)__builtin_lasx_xvpcnt_b((v32i8)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvpcnt_h(__m256i _1) {
  return (__m256i)__builtin_lasx_xvpcnt_h((v16i16)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvpcnt_w(__m256i _1) {
  return (__m256i)__builtin_lasx_xvpcnt_w((v8i32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvpcnt_d(__m256i _1) {
  return (__m256i)__builtin_lasx_xvpcnt_d((v4i64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvclo_b(__m256i _1) {
  return (__m256i)__builtin_lasx_xvclo_b((v32i8)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvclo_h(__m256i _1) {
  return (__m256i)__builtin_lasx_xvclo_h((v16i16)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvclo_w(__m256i _1) {
  return (__m256i)__builtin_lasx_xvclo_w((v8i32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvclo_d(__m256i _1) {
  return (__m256i)__builtin_lasx_xvclo_d((v4i64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvclz_b(__m256i _1) {
  return (__m256i)__builtin_lasx_xvclz_b((v32i8)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvclz_h(__m256i _1) {
  return (__m256i)__builtin_lasx_xvclz_h((v16i16)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvclz_w(__m256i _1) {
  return (__m256i)__builtin_lasx_xvclz_w((v8i32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvclz_d(__m256i _1) {
  return (__m256i)__builtin_lasx_xvclz_d((v4i64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256
    __lasx_xvfadd_s(__m256 _1, __m256 _2) {
  return (__m256)__builtin_lasx_xvfadd_s((v8f32)_1, (v8f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256d
    __lasx_xvfadd_d(__m256d _1, __m256d _2) {
  return (__m256d)__builtin_lasx_xvfadd_d((v4f64)_1, (v4f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256
    __lasx_xvfsub_s(__m256 _1, __m256 _2) {
  return (__m256)__builtin_lasx_xvfsub_s((v8f32)_1, (v8f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256d
    __lasx_xvfsub_d(__m256d _1, __m256d _2) {
  return (__m256d)__builtin_lasx_xvfsub_d((v4f64)_1, (v4f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256
    __lasx_xvfmul_s(__m256 _1, __m256 _2) {
  return (__m256)__builtin_lasx_xvfmul_s((v8f32)_1, (v8f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256d
    __lasx_xvfmul_d(__m256d _1, __m256d _2) {
  return (__m256d)__builtin_lasx_xvfmul_d((v4f64)_1, (v4f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256
    __lasx_xvfdiv_s(__m256 _1, __m256 _2) {
  return (__m256)__builtin_lasx_xvfdiv_s((v8f32)_1, (v8f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256d
    __lasx_xvfdiv_d(__m256d _1, __m256d _2) {
  return (__m256d)__builtin_lasx_xvfdiv_d((v4f64)_1, (v4f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvfcvt_h_s(__m256 _1, __m256 _2) {
  return (__m256i)__builtin_lasx_xvfcvt_h_s((v8f32)_1, (v8f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256
    __lasx_xvfcvt_s_d(__m256d _1, __m256d _2) {
  return (__m256)__builtin_lasx_xvfcvt_s_d((v4f64)_1, (v4f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256
    __lasx_xvfmin_s(__m256 _1, __m256 _2) {
  return (__m256)__builtin_lasx_xvfmin_s((v8f32)_1, (v8f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256d
    __lasx_xvfmin_d(__m256d _1, __m256d _2) {
  return (__m256d)__builtin_lasx_xvfmin_d((v4f64)_1, (v4f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256
    __lasx_xvfmina_s(__m256 _1, __m256 _2) {
  return (__m256)__builtin_lasx_xvfmina_s((v8f32)_1, (v8f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256d
    __lasx_xvfmina_d(__m256d _1, __m256d _2) {
  return (__m256d)__builtin_lasx_xvfmina_d((v4f64)_1, (v4f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256
    __lasx_xvfmax_s(__m256 _1, __m256 _2) {
  return (__m256)__builtin_lasx_xvfmax_s((v8f32)_1, (v8f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256d
    __lasx_xvfmax_d(__m256d _1, __m256d _2) {
  return (__m256d)__builtin_lasx_xvfmax_d((v4f64)_1, (v4f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256
    __lasx_xvfmaxa_s(__m256 _1, __m256 _2) {
  return (__m256)__builtin_lasx_xvfmaxa_s((v8f32)_1, (v8f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256d
    __lasx_xvfmaxa_d(__m256d _1, __m256d _2) {
  return (__m256d)__builtin_lasx_xvfmaxa_d((v4f64)_1, (v4f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvfclass_s(__m256 _1) {
  return (__m256i)__builtin_lasx_xvfclass_s((v8f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvfclass_d(__m256d _1) {
  return (__m256i)__builtin_lasx_xvfclass_d((v4f64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256
    __lasx_xvfsqrt_s(__m256 _1) {
  return (__m256)__builtin_lasx_xvfsqrt_s((v8f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256d
    __lasx_xvfsqrt_d(__m256d _1) {
  return (__m256d)__builtin_lasx_xvfsqrt_d((v4f64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256
    __lasx_xvfrecip_s(__m256 _1) {
  return (__m256)__builtin_lasx_xvfrecip_s((v8f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256d
    __lasx_xvfrecip_d(__m256d _1) {
  return (__m256d)__builtin_lasx_xvfrecip_d((v4f64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256
    __lasx_xvfrecipe_s(__m256 _1) {
  return (__m256)__builtin_lasx_xvfrecipe_s((v8f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256d
    __lasx_xvfrecipe_d(__m256d _1) {
  return (__m256d)__builtin_lasx_xvfrecipe_d((v4f64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256
    __lasx_xvfrint_s(__m256 _1) {
  return (__m256)__builtin_lasx_xvfrint_s((v8f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256d
    __lasx_xvfrint_d(__m256d _1) {
  return (__m256d)__builtin_lasx_xvfrint_d((v4f64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256
    __lasx_xvfrsqrt_s(__m256 _1) {
  return (__m256)__builtin_lasx_xvfrsqrt_s((v8f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256d
    __lasx_xvfrsqrt_d(__m256d _1) {
  return (__m256d)__builtin_lasx_xvfrsqrt_d((v4f64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256
    __lasx_xvfrsqrte_s(__m256 _1) {
  return (__m256)__builtin_lasx_xvfrsqrte_s((v8f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256d
    __lasx_xvfrsqrte_d(__m256d _1) {
  return (__m256d)__builtin_lasx_xvfrsqrte_d((v4f64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256
    __lasx_xvflogb_s(__m256 _1) {
  return (__m256)__builtin_lasx_xvflogb_s((v8f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256d
    __lasx_xvflogb_d(__m256d _1) {
  return (__m256d)__builtin_lasx_xvflogb_d((v4f64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256
    __lasx_xvfcvth_s_h(__m256i _1) {
  return (__m256)__builtin_lasx_xvfcvth_s_h((v16i16)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256d
    __lasx_xvfcvth_d_s(__m256 _1) {
  return (__m256d)__builtin_lasx_xvfcvth_d_s((v8f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256
    __lasx_xvfcvtl_s_h(__m256i _1) {
  return (__m256)__builtin_lasx_xvfcvtl_s_h((v16i16)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256d
    __lasx_xvfcvtl_d_s(__m256 _1) {
  return (__m256d)__builtin_lasx_xvfcvtl_d_s((v8f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvftint_w_s(__m256 _1) {
  return (__m256i)__builtin_lasx_xvftint_w_s((v8f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvftint_l_d(__m256d _1) {
  return (__m256i)__builtin_lasx_xvftint_l_d((v4f64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvftint_wu_s(__m256 _1) {
  return (__m256i)__builtin_lasx_xvftint_wu_s((v8f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvftint_lu_d(__m256d _1) {
  return (__m256i)__builtin_lasx_xvftint_lu_d((v4f64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvftintrz_w_s(__m256 _1) {
  return (__m256i)__builtin_lasx_xvftintrz_w_s((v8f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvftintrz_l_d(__m256d _1) {
  return (__m256i)__builtin_lasx_xvftintrz_l_d((v4f64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvftintrz_wu_s(__m256 _1) {
  return (__m256i)__builtin_lasx_xvftintrz_wu_s((v8f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvftintrz_lu_d(__m256d _1) {
  return (__m256i)__builtin_lasx_xvftintrz_lu_d((v4f64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256
    __lasx_xvffint_s_w(__m256i _1) {
  return (__m256)__builtin_lasx_xvffint_s_w((v8i32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256d
    __lasx_xvffint_d_l(__m256i _1) {
  return (__m256d)__builtin_lasx_xvffint_d_l((v4i64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256
    __lasx_xvffint_s_wu(__m256i _1) {
  return (__m256)__builtin_lasx_xvffint_s_wu((v8u32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256d
    __lasx_xvffint_d_lu(__m256i _1) {
  return (__m256d)__builtin_lasx_xvffint_d_lu((v4u64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvreplve_b(__m256i _1, int _2) {
  return (__m256i)__builtin_lasx_xvreplve_b((v32i8)_1, (int)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvreplve_h(__m256i _1, int _2) {
  return (__m256i)__builtin_lasx_xvreplve_h((v16i16)_1, (int)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvreplve_w(__m256i _1, int _2) {
  return (__m256i)__builtin_lasx_xvreplve_w((v8i32)_1, (int)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvreplve_d(__m256i _1, int _2) {
  return (__m256i)__builtin_lasx_xvreplve_d((v4i64)_1, (int)_2);
}

#define __lasx_xvpermi_w(/*__m256i*/ _1, /*__m256i*/ _2, /*ui8*/ _3)           \
  ((__m256i)__builtin_lasx_xvpermi_w((v8i32)(_1), (v8i32)(_2), (_3)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvandn_v(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvandn_v((v32u8)_1, (v32u8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvneg_b(__m256i _1) {
  return (__m256i)__builtin_lasx_xvneg_b((v32i8)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvneg_h(__m256i _1) {
  return (__m256i)__builtin_lasx_xvneg_h((v16i16)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvneg_w(__m256i _1) {
  return (__m256i)__builtin_lasx_xvneg_w((v8i32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvneg_d(__m256i _1) {
  return (__m256i)__builtin_lasx_xvneg_d((v4i64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmuh_b(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmuh_b((v32i8)_1, (v32i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmuh_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmuh_h((v16i16)_1, (v16i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmuh_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmuh_w((v8i32)_1, (v8i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmuh_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmuh_d((v4i64)_1, (v4i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmuh_bu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmuh_bu((v32u8)_1, (v32u8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmuh_hu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmuh_hu((v16u16)_1, (v16u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmuh_wu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmuh_wu((v8u32)_1, (v8u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmuh_du(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmuh_du((v4u64)_1, (v4u64)_2);
}

#define __lasx_xvsllwil_h_b(/*__m256i*/ _1, /*ui3*/ _2)                        \
  ((__m256i)__builtin_lasx_xvsllwil_h_b((v32i8)(_1), (_2)))

#define __lasx_xvsllwil_w_h(/*__m256i*/ _1, /*ui4*/ _2)                        \
  ((__m256i)__builtin_lasx_xvsllwil_w_h((v16i16)(_1), (_2)))

#define __lasx_xvsllwil_d_w(/*__m256i*/ _1, /*ui5*/ _2)                        \
  ((__m256i)__builtin_lasx_xvsllwil_d_w((v8i32)(_1), (_2)))

#define __lasx_xvsllwil_hu_bu(/*__m256i*/ _1, /*ui3*/ _2)                      \
  ((__m256i)__builtin_lasx_xvsllwil_hu_bu((v32u8)(_1), (_2)))

#define __lasx_xvsllwil_wu_hu(/*__m256i*/ _1, /*ui4*/ _2)                      \
  ((__m256i)__builtin_lasx_xvsllwil_wu_hu((v16u16)(_1), (_2)))

#define __lasx_xvsllwil_du_wu(/*__m256i*/ _1, /*ui5*/ _2)                      \
  ((__m256i)__builtin_lasx_xvsllwil_du_wu((v8u32)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsran_b_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsran_b_h((v16i16)_1, (v16i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsran_h_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsran_h_w((v8i32)_1, (v8i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsran_w_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsran_w_d((v4i64)_1, (v4i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvssran_b_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvssran_b_h((v16i16)_1, (v16i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvssran_h_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvssran_h_w((v8i32)_1, (v8i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvssran_w_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvssran_w_d((v4i64)_1, (v4i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvssran_bu_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvssran_bu_h((v16u16)_1, (v16u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvssran_hu_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvssran_hu_w((v8u32)_1, (v8u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvssran_wu_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvssran_wu_d((v4u64)_1, (v4u64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsrarn_b_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsrarn_b_h((v16i16)_1, (v16i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsrarn_h_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsrarn_h_w((v8i32)_1, (v8i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsrarn_w_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsrarn_w_d((v4i64)_1, (v4i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvssrarn_b_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvssrarn_b_h((v16i16)_1, (v16i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvssrarn_h_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvssrarn_h_w((v8i32)_1, (v8i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvssrarn_w_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvssrarn_w_d((v4i64)_1, (v4i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvssrarn_bu_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvssrarn_bu_h((v16u16)_1, (v16u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvssrarn_hu_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvssrarn_hu_w((v8u32)_1, (v8u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvssrarn_wu_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvssrarn_wu_d((v4u64)_1, (v4u64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsrln_b_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsrln_b_h((v16i16)_1, (v16i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsrln_h_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsrln_h_w((v8i32)_1, (v8i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsrln_w_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsrln_w_d((v4i64)_1, (v4i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvssrln_bu_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvssrln_bu_h((v16u16)_1, (v16u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvssrln_hu_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvssrln_hu_w((v8u32)_1, (v8u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvssrln_wu_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvssrln_wu_d((v4u64)_1, (v4u64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsrlrn_b_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsrlrn_b_h((v16i16)_1, (v16i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsrlrn_h_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsrlrn_h_w((v8i32)_1, (v8i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsrlrn_w_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsrlrn_w_d((v4i64)_1, (v4i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvssrlrn_bu_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvssrlrn_bu_h((v16u16)_1, (v16u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvssrlrn_hu_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvssrlrn_hu_w((v8u32)_1, (v8u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvssrlrn_wu_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvssrlrn_wu_d((v4u64)_1, (v4u64)_2);
}

#define __lasx_xvfrstpi_b(/*__m256i*/ _1, /*__m256i*/ _2, /*ui5*/ _3)          \
  ((__m256i)__builtin_lasx_xvfrstpi_b((v32i8)(_1), (v32i8)(_2), (_3)))

#define __lasx_xvfrstpi_h(/*__m256i*/ _1, /*__m256i*/ _2, /*ui5*/ _3)          \
  ((__m256i)__builtin_lasx_xvfrstpi_h((v16i16)(_1), (v16i16)(_2), (_3)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvfrstp_b(__m256i _1, __m256i _2, __m256i _3) {
  return (__m256i)__builtin_lasx_xvfrstp_b((v32i8)_1, (v32i8)_2, (v32i8)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvfrstp_h(__m256i _1, __m256i _2, __m256i _3) {
  return (__m256i)__builtin_lasx_xvfrstp_h((v16i16)_1, (v16i16)_2, (v16i16)_3);
}

#define __lasx_xvshuf4i_d(/*__m256i*/ _1, /*__m256i*/ _2, /*ui8*/ _3)          \
  ((__m256i)__builtin_lasx_xvshuf4i_d((v4i64)(_1), (v4i64)(_2), (_3)))

#define __lasx_xvbsrl_v(/*__m256i*/ _1, /*ui5*/ _2)                            \
  ((__m256i)__builtin_lasx_xvbsrl_v((v32i8)(_1), (_2)))

#define __lasx_xvbsll_v(/*__m256i*/ _1, /*ui5*/ _2)                            \
  ((__m256i)__builtin_lasx_xvbsll_v((v32i8)(_1), (_2)))

#define __lasx_xvextrins_b(/*__m256i*/ _1, /*__m256i*/ _2, /*ui8*/ _3)         \
  ((__m256i)__builtin_lasx_xvextrins_b((v32i8)(_1), (v32i8)(_2), (_3)))

#define __lasx_xvextrins_h(/*__m256i*/ _1, /*__m256i*/ _2, /*ui8*/ _3)         \
  ((__m256i)__builtin_lasx_xvextrins_h((v16i16)(_1), (v16i16)(_2), (_3)))

#define __lasx_xvextrins_w(/*__m256i*/ _1, /*__m256i*/ _2, /*ui8*/ _3)         \
  ((__m256i)__builtin_lasx_xvextrins_w((v8i32)(_1), (v8i32)(_2), (_3)))

#define __lasx_xvextrins_d(/*__m256i*/ _1, /*__m256i*/ _2, /*ui8*/ _3)         \
  ((__m256i)__builtin_lasx_xvextrins_d((v4i64)(_1), (v4i64)(_2), (_3)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmskltz_b(__m256i _1) {
  return (__m256i)__builtin_lasx_xvmskltz_b((v32i8)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmskltz_h(__m256i _1) {
  return (__m256i)__builtin_lasx_xvmskltz_h((v16i16)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmskltz_w(__m256i _1) {
  return (__m256i)__builtin_lasx_xvmskltz_w((v8i32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmskltz_d(__m256i _1) {
  return (__m256i)__builtin_lasx_xvmskltz_d((v4i64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsigncov_b(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsigncov_b((v32i8)_1, (v32i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsigncov_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsigncov_h((v16i16)_1, (v16i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsigncov_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsigncov_w((v8i32)_1, (v8i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsigncov_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsigncov_d((v4i64)_1, (v4i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256
    __lasx_xvfmadd_s(__m256 _1, __m256 _2, __m256 _3) {
  return (__m256)__builtin_lasx_xvfmadd_s((v8f32)_1, (v8f32)_2, (v8f32)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256d
    __lasx_xvfmadd_d(__m256d _1, __m256d _2, __m256d _3) {
  return (__m256d)__builtin_lasx_xvfmadd_d((v4f64)_1, (v4f64)_2, (v4f64)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256
    __lasx_xvfmsub_s(__m256 _1, __m256 _2, __m256 _3) {
  return (__m256)__builtin_lasx_xvfmsub_s((v8f32)_1, (v8f32)_2, (v8f32)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256d
    __lasx_xvfmsub_d(__m256d _1, __m256d _2, __m256d _3) {
  return (__m256d)__builtin_lasx_xvfmsub_d((v4f64)_1, (v4f64)_2, (v4f64)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256
    __lasx_xvfnmadd_s(__m256 _1, __m256 _2, __m256 _3) {
  return (__m256)__builtin_lasx_xvfnmadd_s((v8f32)_1, (v8f32)_2, (v8f32)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256d
    __lasx_xvfnmadd_d(__m256d _1, __m256d _2, __m256d _3) {
  return (__m256d)__builtin_lasx_xvfnmadd_d((v4f64)_1, (v4f64)_2, (v4f64)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256
    __lasx_xvfnmsub_s(__m256 _1, __m256 _2, __m256 _3) {
  return (__m256)__builtin_lasx_xvfnmsub_s((v8f32)_1, (v8f32)_2, (v8f32)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256d
    __lasx_xvfnmsub_d(__m256d _1, __m256d _2, __m256d _3) {
  return (__m256d)__builtin_lasx_xvfnmsub_d((v4f64)_1, (v4f64)_2, (v4f64)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvftintrne_w_s(__m256 _1) {
  return (__m256i)__builtin_lasx_xvftintrne_w_s((v8f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvftintrne_l_d(__m256d _1) {
  return (__m256i)__builtin_lasx_xvftintrne_l_d((v4f64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvftintrp_w_s(__m256 _1) {
  return (__m256i)__builtin_lasx_xvftintrp_w_s((v8f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvftintrp_l_d(__m256d _1) {
  return (__m256i)__builtin_lasx_xvftintrp_l_d((v4f64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvftintrm_w_s(__m256 _1) {
  return (__m256i)__builtin_lasx_xvftintrm_w_s((v8f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvftintrm_l_d(__m256d _1) {
  return (__m256i)__builtin_lasx_xvftintrm_l_d((v4f64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvftint_w_d(__m256d _1, __m256d _2) {
  return (__m256i)__builtin_lasx_xvftint_w_d((v4f64)_1, (v4f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256
    __lasx_xvffint_s_l(__m256i _1, __m256i _2) {
  return (__m256)__builtin_lasx_xvffint_s_l((v4i64)_1, (v4i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvftintrz_w_d(__m256d _1, __m256d _2) {
  return (__m256i)__builtin_lasx_xvftintrz_w_d((v4f64)_1, (v4f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvftintrp_w_d(__m256d _1, __m256d _2) {
  return (__m256i)__builtin_lasx_xvftintrp_w_d((v4f64)_1, (v4f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvftintrm_w_d(__m256d _1, __m256d _2) {
  return (__m256i)__builtin_lasx_xvftintrm_w_d((v4f64)_1, (v4f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvftintrne_w_d(__m256d _1, __m256d _2) {
  return (__m256i)__builtin_lasx_xvftintrne_w_d((v4f64)_1, (v4f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvftinth_l_s(__m256 _1) {
  return (__m256i)__builtin_lasx_xvftinth_l_s((v8f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvftintl_l_s(__m256 _1) {
  return (__m256i)__builtin_lasx_xvftintl_l_s((v8f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256d
    __lasx_xvffinth_d_w(__m256i _1) {
  return (__m256d)__builtin_lasx_xvffinth_d_w((v8i32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256d
    __lasx_xvffintl_d_w(__m256i _1) {
  return (__m256d)__builtin_lasx_xvffintl_d_w((v8i32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvftintrzh_l_s(__m256 _1) {
  return (__m256i)__builtin_lasx_xvftintrzh_l_s((v8f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvftintrzl_l_s(__m256 _1) {
  return (__m256i)__builtin_lasx_xvftintrzl_l_s((v8f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvftintrph_l_s(__m256 _1) {
  return (__m256i)__builtin_lasx_xvftintrph_l_s((v8f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvftintrpl_l_s(__m256 _1) {
  return (__m256i)__builtin_lasx_xvftintrpl_l_s((v8f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvftintrmh_l_s(__m256 _1) {
  return (__m256i)__builtin_lasx_xvftintrmh_l_s((v8f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvftintrml_l_s(__m256 _1) {
  return (__m256i)__builtin_lasx_xvftintrml_l_s((v8f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvftintrneh_l_s(__m256 _1) {
  return (__m256i)__builtin_lasx_xvftintrneh_l_s((v8f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvftintrnel_l_s(__m256 _1) {
  return (__m256i)__builtin_lasx_xvftintrnel_l_s((v8f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256
    __lasx_xvfrintrne_s(__m256 _1) {
  return (__m256)__builtin_lasx_xvfrintrne_s((v8f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256d
    __lasx_xvfrintrne_d(__m256d _1) {
  return (__m256d)__builtin_lasx_xvfrintrne_d((v4f64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256
    __lasx_xvfrintrz_s(__m256 _1) {
  return (__m256)__builtin_lasx_xvfrintrz_s((v8f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256d
    __lasx_xvfrintrz_d(__m256d _1) {
  return (__m256d)__builtin_lasx_xvfrintrz_d((v4f64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256
    __lasx_xvfrintrp_s(__m256 _1) {
  return (__m256)__builtin_lasx_xvfrintrp_s((v8f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256d
    __lasx_xvfrintrp_d(__m256d _1) {
  return (__m256d)__builtin_lasx_xvfrintrp_d((v4f64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256
    __lasx_xvfrintrm_s(__m256 _1) {
  return (__m256)__builtin_lasx_xvfrintrm_s((v8f32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256d
    __lasx_xvfrintrm_d(__m256d _1) {
  return (__m256d)__builtin_lasx_xvfrintrm_d((v4f64)_1);
}

#define __lasx_xvld(/*void **/ _1, /*si12*/ _2)                                \
  ((__m256i)__builtin_lasx_xvld((void const *)(_1), (_2)))

#define __lasx_xvst(/*__m256i*/ _1, /*void **/ _2, /*si12*/ _3)                \
  ((void)__builtin_lasx_xvst((v32i8)(_1), (void *)(_2), (_3)))

#define __lasx_xvstelm_b(/*__m256i*/ _1, /*void **/ _2, /*si8*/ _3,            \
                         /*idx*/ _4)                                           \
  ((void)__builtin_lasx_xvstelm_b((v32i8)(_1), (void *)(_2), (_3), (_4)))

#define __lasx_xvstelm_h(/*__m256i*/ _1, /*void **/ _2, /*si8*/ _3,            \
                         /*idx*/ _4)                                           \
  ((void)__builtin_lasx_xvstelm_h((v16i16)(_1), (void *)(_2), (_3), (_4)))

#define __lasx_xvstelm_w(/*__m256i*/ _1, /*void **/ _2, /*si8*/ _3,            \
                         /*idx*/ _4)                                           \
  ((void)__builtin_lasx_xvstelm_w((v8i32)(_1), (void *)(_2), (_3), (_4)))

#define __lasx_xvstelm_d(/*__m256i*/ _1, /*void **/ _2, /*si8*/ _3,            \
                         /*idx*/ _4)                                           \
  ((void)__builtin_lasx_xvstelm_d((v4i64)(_1), (void *)(_2), (_3), (_4)))

#define __lasx_xvinsve0_w(/*__m256i*/ _1, /*__m256i*/ _2, /*ui3*/ _3)          \
  ((__m256i)__builtin_lasx_xvinsve0_w((v8i32)(_1), (v8i32)(_2), (_3)))

#define __lasx_xvinsve0_d(/*__m256i*/ _1, /*__m256i*/ _2, /*ui2*/ _3)          \
  ((__m256i)__builtin_lasx_xvinsve0_d((v4i64)(_1), (v4i64)(_2), (_3)))

#define __lasx_xvpickve_w(/*__m256i*/ _1, /*ui3*/ _2)                          \
  ((__m256i)__builtin_lasx_xvpickve_w((v8i32)(_1), (_2)))

#define __lasx_xvpickve_d(/*__m256i*/ _1, /*ui2*/ _2)                          \
  ((__m256i)__builtin_lasx_xvpickve_d((v4i64)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvssrlrn_b_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvssrlrn_b_h((v16i16)_1, (v16i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvssrlrn_h_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvssrlrn_h_w((v8i32)_1, (v8i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvssrlrn_w_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvssrlrn_w_d((v4i64)_1, (v4i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvssrln_b_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvssrln_b_h((v16i16)_1, (v16i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvssrln_h_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvssrln_h_w((v8i32)_1, (v8i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvssrln_w_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvssrln_w_d((v4i64)_1, (v4i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvorn_v(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvorn_v((v32i8)_1, (v32i8)_2);
}

#define __lasx_xvldi(/*i13*/ _1) ((__m256i)__builtin_lasx_xvldi((_1)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvldx(void const *_1, long int _2) {
  return (__m256i)__builtin_lasx_xvldx((void const *)_1, (long int)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) void
    __lasx_xvstx(__m256i _1, void *_2, long int _3) {
  return (void)__builtin_lasx_xvstx((v32i8)_1, (void *)_2, (long int)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvextl_qu_du(__m256i _1) {
  return (__m256i)__builtin_lasx_xvextl_qu_du((v4u64)_1);
}

#define __lasx_xvinsgr2vr_w(/*__m256i*/ _1, /*int*/ _2, /*ui3*/ _3)            \
  ((__m256i)__builtin_lasx_xvinsgr2vr_w((v8i32)(_1), (int)(_2), (_3)))

#define __lasx_xvinsgr2vr_d(/*__m256i*/ _1, /*long int*/ _2, /*ui2*/ _3)       \
  ((__m256i)__builtin_lasx_xvinsgr2vr_d((v4i64)(_1), (long int)(_2), (_3)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvreplve0_b(__m256i _1) {
  return (__m256i)__builtin_lasx_xvreplve0_b((v32i8)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvreplve0_h(__m256i _1) {
  return (__m256i)__builtin_lasx_xvreplve0_h((v16i16)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvreplve0_w(__m256i _1) {
  return (__m256i)__builtin_lasx_xvreplve0_w((v8i32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvreplve0_d(__m256i _1) {
  return (__m256i)__builtin_lasx_xvreplve0_d((v4i64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvreplve0_q(__m256i _1) {
  return (__m256i)__builtin_lasx_xvreplve0_q((v32i8)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_vext2xv_h_b(__m256i _1) {
  return (__m256i)__builtin_lasx_vext2xv_h_b((v32i8)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_vext2xv_w_h(__m256i _1) {
  return (__m256i)__builtin_lasx_vext2xv_w_h((v16i16)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_vext2xv_d_w(__m256i _1) {
  return (__m256i)__builtin_lasx_vext2xv_d_w((v8i32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_vext2xv_w_b(__m256i _1) {
  return (__m256i)__builtin_lasx_vext2xv_w_b((v32i8)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_vext2xv_d_h(__m256i _1) {
  return (__m256i)__builtin_lasx_vext2xv_d_h((v16i16)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_vext2xv_d_b(__m256i _1) {
  return (__m256i)__builtin_lasx_vext2xv_d_b((v32i8)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_vext2xv_hu_bu(__m256i _1) {
  return (__m256i)__builtin_lasx_vext2xv_hu_bu((v32i8)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_vext2xv_wu_hu(__m256i _1) {
  return (__m256i)__builtin_lasx_vext2xv_wu_hu((v16i16)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_vext2xv_du_wu(__m256i _1) {
  return (__m256i)__builtin_lasx_vext2xv_du_wu((v8i32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_vext2xv_wu_bu(__m256i _1) {
  return (__m256i)__builtin_lasx_vext2xv_wu_bu((v32i8)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_vext2xv_du_hu(__m256i _1) {
  return (__m256i)__builtin_lasx_vext2xv_du_hu((v16i16)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_vext2xv_du_bu(__m256i _1) {
  return (__m256i)__builtin_lasx_vext2xv_du_bu((v32i8)_1);
}

#define __lasx_xvpermi_q(/*__m256i*/ _1, /*__m256i*/ _2, /*ui8*/ _3)           \
  ((__m256i)__builtin_lasx_xvpermi_q((v32i8)(_1), (v32i8)(_2), (_3)))

#define __lasx_xvpermi_d(/*__m256i*/ _1, /*ui8*/ _2)                           \
  ((__m256i)__builtin_lasx_xvpermi_d((v4i64)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvperm_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvperm_w((v8i32)_1, (v8i32)_2);
}

#define __lasx_xvldrepl_b(/*void **/ _1, /*si12*/ _2)                          \
  ((__m256i)__builtin_lasx_xvldrepl_b((void const *)(_1), (_2)))

#define __lasx_xvldrepl_h(/*void **/ _1, /*si11*/ _2)                          \
  ((__m256i)__builtin_lasx_xvldrepl_h((void const *)(_1), (_2)))

#define __lasx_xvldrepl_w(/*void **/ _1, /*si10*/ _2)                          \
  ((__m256i)__builtin_lasx_xvldrepl_w((void const *)(_1), (_2)))

#define __lasx_xvldrepl_d(/*void **/ _1, /*si9*/ _2)                           \
  ((__m256i)__builtin_lasx_xvldrepl_d((void const *)(_1), (_2)))

#define __lasx_xvpickve2gr_w(/*__m256i*/ _1, /*ui3*/ _2)                       \
  ((int)__builtin_lasx_xvpickve2gr_w((v8i32)(_1), (_2)))

#define __lasx_xvpickve2gr_wu(/*__m256i*/ _1, /*ui3*/ _2)                      \
  ((unsigned int)__builtin_lasx_xvpickve2gr_wu((v8i32)(_1), (_2)))

#define __lasx_xvpickve2gr_d(/*__m256i*/ _1, /*ui2*/ _2)                       \
  ((long int)__builtin_lasx_xvpickve2gr_d((v4i64)(_1), (_2)))

#define __lasx_xvpickve2gr_du(/*__m256i*/ _1, /*ui2*/ _2)                      \
  ((unsigned long int)__builtin_lasx_xvpickve2gr_du((v4i64)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvaddwev_q_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvaddwev_q_d((v4i64)_1, (v4i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvaddwev_d_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvaddwev_d_w((v8i32)_1, (v8i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvaddwev_w_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvaddwev_w_h((v16i16)_1, (v16i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvaddwev_h_b(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvaddwev_h_b((v32i8)_1, (v32i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvaddwev_q_du(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvaddwev_q_du((v4u64)_1, (v4u64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvaddwev_d_wu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvaddwev_d_wu((v8u32)_1, (v8u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvaddwev_w_hu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvaddwev_w_hu((v16u16)_1, (v16u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvaddwev_h_bu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvaddwev_h_bu((v32u8)_1, (v32u8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsubwev_q_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsubwev_q_d((v4i64)_1, (v4i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsubwev_d_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsubwev_d_w((v8i32)_1, (v8i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsubwev_w_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsubwev_w_h((v16i16)_1, (v16i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsubwev_h_b(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsubwev_h_b((v32i8)_1, (v32i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsubwev_q_du(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsubwev_q_du((v4u64)_1, (v4u64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsubwev_d_wu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsubwev_d_wu((v8u32)_1, (v8u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsubwev_w_hu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsubwev_w_hu((v16u16)_1, (v16u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsubwev_h_bu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsubwev_h_bu((v32u8)_1, (v32u8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmulwev_q_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmulwev_q_d((v4i64)_1, (v4i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmulwev_d_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmulwev_d_w((v8i32)_1, (v8i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmulwev_w_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmulwev_w_h((v16i16)_1, (v16i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmulwev_h_b(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmulwev_h_b((v32i8)_1, (v32i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmulwev_q_du(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmulwev_q_du((v4u64)_1, (v4u64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmulwev_d_wu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmulwev_d_wu((v8u32)_1, (v8u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmulwev_w_hu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmulwev_w_hu((v16u16)_1, (v16u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmulwev_h_bu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmulwev_h_bu((v32u8)_1, (v32u8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvaddwod_q_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvaddwod_q_d((v4i64)_1, (v4i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvaddwod_d_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvaddwod_d_w((v8i32)_1, (v8i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvaddwod_w_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvaddwod_w_h((v16i16)_1, (v16i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvaddwod_h_b(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvaddwod_h_b((v32i8)_1, (v32i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvaddwod_q_du(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvaddwod_q_du((v4u64)_1, (v4u64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvaddwod_d_wu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvaddwod_d_wu((v8u32)_1, (v8u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvaddwod_w_hu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvaddwod_w_hu((v16u16)_1, (v16u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvaddwod_h_bu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvaddwod_h_bu((v32u8)_1, (v32u8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsubwod_q_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsubwod_q_d((v4i64)_1, (v4i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsubwod_d_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsubwod_d_w((v8i32)_1, (v8i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsubwod_w_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsubwod_w_h((v16i16)_1, (v16i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsubwod_h_b(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsubwod_h_b((v32i8)_1, (v32i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsubwod_q_du(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsubwod_q_du((v4u64)_1, (v4u64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsubwod_d_wu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsubwod_d_wu((v8u32)_1, (v8u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsubwod_w_hu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsubwod_w_hu((v16u16)_1, (v16u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsubwod_h_bu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsubwod_h_bu((v32u8)_1, (v32u8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmulwod_q_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmulwod_q_d((v4i64)_1, (v4i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmulwod_d_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmulwod_d_w((v8i32)_1, (v8i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmulwod_w_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmulwod_w_h((v16i16)_1, (v16i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmulwod_h_b(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmulwod_h_b((v32i8)_1, (v32i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmulwod_q_du(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmulwod_q_du((v4u64)_1, (v4u64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmulwod_d_wu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmulwod_d_wu((v8u32)_1, (v8u32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmulwod_w_hu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmulwod_w_hu((v16u16)_1, (v16u16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmulwod_h_bu(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmulwod_h_bu((v32u8)_1, (v32u8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvaddwev_d_wu_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvaddwev_d_wu_w((v8u32)_1, (v8i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvaddwev_w_hu_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvaddwev_w_hu_h((v16u16)_1, (v16i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvaddwev_h_bu_b(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvaddwev_h_bu_b((v32u8)_1, (v32i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmulwev_d_wu_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmulwev_d_wu_w((v8u32)_1, (v8i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmulwev_w_hu_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmulwev_w_hu_h((v16u16)_1, (v16i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmulwev_h_bu_b(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmulwev_h_bu_b((v32u8)_1, (v32i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvaddwod_d_wu_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvaddwod_d_wu_w((v8u32)_1, (v8i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvaddwod_w_hu_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvaddwod_w_hu_h((v16u16)_1, (v16i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvaddwod_h_bu_b(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvaddwod_h_bu_b((v32u8)_1, (v32i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmulwod_d_wu_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmulwod_d_wu_w((v8u32)_1, (v8i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmulwod_w_hu_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmulwod_w_hu_h((v16u16)_1, (v16i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmulwod_h_bu_b(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmulwod_h_bu_b((v32u8)_1, (v32i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvhaddw_q_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvhaddw_q_d((v4i64)_1, (v4i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvhaddw_qu_du(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvhaddw_qu_du((v4u64)_1, (v4u64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvhsubw_q_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvhsubw_q_d((v4i64)_1, (v4i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvhsubw_qu_du(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvhsubw_qu_du((v4u64)_1, (v4u64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmaddwev_q_d(__m256i _1, __m256i _2, __m256i _3) {
  return (__m256i)__builtin_lasx_xvmaddwev_q_d((v4i64)_1, (v4i64)_2, (v4i64)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmaddwev_d_w(__m256i _1, __m256i _2, __m256i _3) {
  return (__m256i)__builtin_lasx_xvmaddwev_d_w((v4i64)_1, (v8i32)_2, (v8i32)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmaddwev_w_h(__m256i _1, __m256i _2, __m256i _3) {
  return (__m256i)__builtin_lasx_xvmaddwev_w_h((v8i32)_1, (v16i16)_2,
                                               (v16i16)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmaddwev_h_b(__m256i _1, __m256i _2, __m256i _3) {
  return (__m256i)__builtin_lasx_xvmaddwev_h_b((v16i16)_1, (v32i8)_2,
                                               (v32i8)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmaddwev_q_du(__m256i _1, __m256i _2, __m256i _3) {
  return (__m256i)__builtin_lasx_xvmaddwev_q_du((v4u64)_1, (v4u64)_2,
                                                (v4u64)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmaddwev_d_wu(__m256i _1, __m256i _2, __m256i _3) {
  return (__m256i)__builtin_lasx_xvmaddwev_d_wu((v4u64)_1, (v8u32)_2,
                                                (v8u32)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmaddwev_w_hu(__m256i _1, __m256i _2, __m256i _3) {
  return (__m256i)__builtin_lasx_xvmaddwev_w_hu((v8u32)_1, (v16u16)_2,
                                                (v16u16)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmaddwev_h_bu(__m256i _1, __m256i _2, __m256i _3) {
  return (__m256i)__builtin_lasx_xvmaddwev_h_bu((v16u16)_1, (v32u8)_2,
                                                (v32u8)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmaddwod_q_d(__m256i _1, __m256i _2, __m256i _3) {
  return (__m256i)__builtin_lasx_xvmaddwod_q_d((v4i64)_1, (v4i64)_2, (v4i64)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmaddwod_d_w(__m256i _1, __m256i _2, __m256i _3) {
  return (__m256i)__builtin_lasx_xvmaddwod_d_w((v4i64)_1, (v8i32)_2, (v8i32)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmaddwod_w_h(__m256i _1, __m256i _2, __m256i _3) {
  return (__m256i)__builtin_lasx_xvmaddwod_w_h((v8i32)_1, (v16i16)_2,
                                               (v16i16)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmaddwod_h_b(__m256i _1, __m256i _2, __m256i _3) {
  return (__m256i)__builtin_lasx_xvmaddwod_h_b((v16i16)_1, (v32i8)_2,
                                               (v32i8)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmaddwod_q_du(__m256i _1, __m256i _2, __m256i _3) {
  return (__m256i)__builtin_lasx_xvmaddwod_q_du((v4u64)_1, (v4u64)_2,
                                                (v4u64)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmaddwod_d_wu(__m256i _1, __m256i _2, __m256i _3) {
  return (__m256i)__builtin_lasx_xvmaddwod_d_wu((v4u64)_1, (v8u32)_2,
                                                (v8u32)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmaddwod_w_hu(__m256i _1, __m256i _2, __m256i _3) {
  return (__m256i)__builtin_lasx_xvmaddwod_w_hu((v8u32)_1, (v16u16)_2,
                                                (v16u16)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmaddwod_h_bu(__m256i _1, __m256i _2, __m256i _3) {
  return (__m256i)__builtin_lasx_xvmaddwod_h_bu((v16u16)_1, (v32u8)_2,
                                                (v32u8)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmaddwev_q_du_d(__m256i _1, __m256i _2, __m256i _3) {
  return (__m256i)__builtin_lasx_xvmaddwev_q_du_d((v4i64)_1, (v4u64)_2,
                                                  (v4i64)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmaddwev_d_wu_w(__m256i _1, __m256i _2, __m256i _3) {
  return (__m256i)__builtin_lasx_xvmaddwev_d_wu_w((v4i64)_1, (v8u32)_2,
                                                  (v8i32)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmaddwev_w_hu_h(__m256i _1, __m256i _2, __m256i _3) {
  return (__m256i)__builtin_lasx_xvmaddwev_w_hu_h((v8i32)_1, (v16u16)_2,
                                                  (v16i16)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmaddwev_h_bu_b(__m256i _1, __m256i _2, __m256i _3) {
  return (__m256i)__builtin_lasx_xvmaddwev_h_bu_b((v16i16)_1, (v32u8)_2,
                                                  (v32i8)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmaddwod_q_du_d(__m256i _1, __m256i _2, __m256i _3) {
  return (__m256i)__builtin_lasx_xvmaddwod_q_du_d((v4i64)_1, (v4u64)_2,
                                                  (v4i64)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmaddwod_d_wu_w(__m256i _1, __m256i _2, __m256i _3) {
  return (__m256i)__builtin_lasx_xvmaddwod_d_wu_w((v4i64)_1, (v8u32)_2,
                                                  (v8i32)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmaddwod_w_hu_h(__m256i _1, __m256i _2, __m256i _3) {
  return (__m256i)__builtin_lasx_xvmaddwod_w_hu_h((v8i32)_1, (v16u16)_2,
                                                  (v16i16)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmaddwod_h_bu_b(__m256i _1, __m256i _2, __m256i _3) {
  return (__m256i)__builtin_lasx_xvmaddwod_h_bu_b((v16i16)_1, (v32u8)_2,
                                                  (v32i8)_3);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvrotr_b(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvrotr_b((v32i8)_1, (v32i8)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvrotr_h(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvrotr_h((v16i16)_1, (v16i16)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvrotr_w(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvrotr_w((v8i32)_1, (v8i32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvrotr_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvrotr_d((v4i64)_1, (v4i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvadd_q(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvadd_q((v4i64)_1, (v4i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvsub_q(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvsub_q((v4i64)_1, (v4i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvaddwev_q_du_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvaddwev_q_du_d((v4u64)_1, (v4i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvaddwod_q_du_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvaddwod_q_du_d((v4u64)_1, (v4i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmulwev_q_du_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmulwev_q_du_d((v4u64)_1, (v4i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmulwod_q_du_d(__m256i _1, __m256i _2) {
  return (__m256i)__builtin_lasx_xvmulwod_q_du_d((v4u64)_1, (v4i64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmskgez_b(__m256i _1) {
  return (__m256i)__builtin_lasx_xvmskgez_b((v32i8)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvmsknz_b(__m256i _1) {
  return (__m256i)__builtin_lasx_xvmsknz_b((v32i8)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvexth_h_b(__m256i _1) {
  return (__m256i)__builtin_lasx_xvexth_h_b((v32i8)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvexth_w_h(__m256i _1) {
  return (__m256i)__builtin_lasx_xvexth_w_h((v16i16)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvexth_d_w(__m256i _1) {
  return (__m256i)__builtin_lasx_xvexth_d_w((v8i32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvexth_q_d(__m256i _1) {
  return (__m256i)__builtin_lasx_xvexth_q_d((v4i64)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvexth_hu_bu(__m256i _1) {
  return (__m256i)__builtin_lasx_xvexth_hu_bu((v32u8)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvexth_wu_hu(__m256i _1) {
  return (__m256i)__builtin_lasx_xvexth_wu_hu((v16u16)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvexth_du_wu(__m256i _1) {
  return (__m256i)__builtin_lasx_xvexth_du_wu((v8u32)_1);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvexth_qu_du(__m256i _1) {
  return (__m256i)__builtin_lasx_xvexth_qu_du((v4u64)_1);
}

#define __lasx_xvrotri_b(/*__m256i*/ _1, /*ui3*/ _2)                           \
  ((__m256i)__builtin_lasx_xvrotri_b((v32i8)(_1), (_2)))

#define __lasx_xvrotri_h(/*__m256i*/ _1, /*ui4*/ _2)                           \
  ((__m256i)__builtin_lasx_xvrotri_h((v16i16)(_1), (_2)))

#define __lasx_xvrotri_w(/*__m256i*/ _1, /*ui5*/ _2)                           \
  ((__m256i)__builtin_lasx_xvrotri_w((v8i32)(_1), (_2)))

#define __lasx_xvrotri_d(/*__m256i*/ _1, /*ui6*/ _2)                           \
  ((__m256i)__builtin_lasx_xvrotri_d((v4i64)(_1), (_2)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvextl_q_d(__m256i _1) {
  return (__m256i)__builtin_lasx_xvextl_q_d((v4i64)_1);
}

#define __lasx_xvsrlni_b_h(/*__m256i*/ _1, /*__m256i*/ _2, /*ui4*/ _3)         \
  ((__m256i)__builtin_lasx_xvsrlni_b_h((v32i8)(_1), (v32i8)(_2), (_3)))

#define __lasx_xvsrlni_h_w(/*__m256i*/ _1, /*__m256i*/ _2, /*ui5*/ _3)         \
  ((__m256i)__builtin_lasx_xvsrlni_h_w((v16i16)(_1), (v16i16)(_2), (_3)))

#define __lasx_xvsrlni_w_d(/*__m256i*/ _1, /*__m256i*/ _2, /*ui6*/ _3)         \
  ((__m256i)__builtin_lasx_xvsrlni_w_d((v8i32)(_1), (v8i32)(_2), (_3)))

#define __lasx_xvsrlni_d_q(/*__m256i*/ _1, /*__m256i*/ _2, /*ui7*/ _3)         \
  ((__m256i)__builtin_lasx_xvsrlni_d_q((v4i64)(_1), (v4i64)(_2), (_3)))

#define __lasx_xvsrlrni_b_h(/*__m256i*/ _1, /*__m256i*/ _2, /*ui4*/ _3)        \
  ((__m256i)__builtin_lasx_xvsrlrni_b_h((v32i8)(_1), (v32i8)(_2), (_3)))

#define __lasx_xvsrlrni_h_w(/*__m256i*/ _1, /*__m256i*/ _2, /*ui5*/ _3)        \
  ((__m256i)__builtin_lasx_xvsrlrni_h_w((v16i16)(_1), (v16i16)(_2), (_3)))

#define __lasx_xvsrlrni_w_d(/*__m256i*/ _1, /*__m256i*/ _2, /*ui6*/ _3)        \
  ((__m256i)__builtin_lasx_xvsrlrni_w_d((v8i32)(_1), (v8i32)(_2), (_3)))

#define __lasx_xvsrlrni_d_q(/*__m256i*/ _1, /*__m256i*/ _2, /*ui7*/ _3)        \
  ((__m256i)__builtin_lasx_xvsrlrni_d_q((v4i64)(_1), (v4i64)(_2), (_3)))

#define __lasx_xvssrlni_b_h(/*__m256i*/ _1, /*__m256i*/ _2, /*ui4*/ _3)        \
  ((__m256i)__builtin_lasx_xvssrlni_b_h((v32i8)(_1), (v32i8)(_2), (_3)))

#define __lasx_xvssrlni_h_w(/*__m256i*/ _1, /*__m256i*/ _2, /*ui5*/ _3)        \
  ((__m256i)__builtin_lasx_xvssrlni_h_w((v16i16)(_1), (v16i16)(_2), (_3)))

#define __lasx_xvssrlni_w_d(/*__m256i*/ _1, /*__m256i*/ _2, /*ui6*/ _3)        \
  ((__m256i)__builtin_lasx_xvssrlni_w_d((v8i32)(_1), (v8i32)(_2), (_3)))

#define __lasx_xvssrlni_d_q(/*__m256i*/ _1, /*__m256i*/ _2, /*ui7*/ _3)        \
  ((__m256i)__builtin_lasx_xvssrlni_d_q((v4i64)(_1), (v4i64)(_2), (_3)))

#define __lasx_xvssrlni_bu_h(/*__m256i*/ _1, /*__m256i*/ _2, /*ui4*/ _3)       \
  ((__m256i)__builtin_lasx_xvssrlni_bu_h((v32u8)(_1), (v32i8)(_2), (_3)))

#define __lasx_xvssrlni_hu_w(/*__m256i*/ _1, /*__m256i*/ _2, /*ui5*/ _3)       \
  ((__m256i)__builtin_lasx_xvssrlni_hu_w((v16u16)(_1), (v16i16)(_2), (_3)))

#define __lasx_xvssrlni_wu_d(/*__m256i*/ _1, /*__m256i*/ _2, /*ui6*/ _3)       \
  ((__m256i)__builtin_lasx_xvssrlni_wu_d((v8u32)(_1), (v8i32)(_2), (_3)))

#define __lasx_xvssrlni_du_q(/*__m256i*/ _1, /*__m256i*/ _2, /*ui7*/ _3)       \
  ((__m256i)__builtin_lasx_xvssrlni_du_q((v4u64)(_1), (v4i64)(_2), (_3)))

#define __lasx_xvssrlrni_b_h(/*__m256i*/ _1, /*__m256i*/ _2, /*ui4*/ _3)       \
  ((__m256i)__builtin_lasx_xvssrlrni_b_h((v32i8)(_1), (v32i8)(_2), (_3)))

#define __lasx_xvssrlrni_h_w(/*__m256i*/ _1, /*__m256i*/ _2, /*ui5*/ _3)       \
  ((__m256i)__builtin_lasx_xvssrlrni_h_w((v16i16)(_1), (v16i16)(_2), (_3)))

#define __lasx_xvssrlrni_w_d(/*__m256i*/ _1, /*__m256i*/ _2, /*ui6*/ _3)       \
  ((__m256i)__builtin_lasx_xvssrlrni_w_d((v8i32)(_1), (v8i32)(_2), (_3)))

#define __lasx_xvssrlrni_d_q(/*__m256i*/ _1, /*__m256i*/ _2, /*ui7*/ _3)       \
  ((__m256i)__builtin_lasx_xvssrlrni_d_q((v4i64)(_1), (v4i64)(_2), (_3)))

#define __lasx_xvssrlrni_bu_h(/*__m256i*/ _1, /*__m256i*/ _2, /*ui4*/ _3)      \
  ((__m256i)__builtin_lasx_xvssrlrni_bu_h((v32u8)(_1), (v32i8)(_2), (_3)))

#define __lasx_xvssrlrni_hu_w(/*__m256i*/ _1, /*__m256i*/ _2, /*ui5*/ _3)      \
  ((__m256i)__builtin_lasx_xvssrlrni_hu_w((v16u16)(_1), (v16i16)(_2), (_3)))

#define __lasx_xvssrlrni_wu_d(/*__m256i*/ _1, /*__m256i*/ _2, /*ui6*/ _3)      \
  ((__m256i)__builtin_lasx_xvssrlrni_wu_d((v8u32)(_1), (v8i32)(_2), (_3)))

#define __lasx_xvssrlrni_du_q(/*__m256i*/ _1, /*__m256i*/ _2, /*ui7*/ _3)      \
  ((__m256i)__builtin_lasx_xvssrlrni_du_q((v4u64)(_1), (v4i64)(_2), (_3)))

#define __lasx_xvsrani_b_h(/*__m256i*/ _1, /*__m256i*/ _2, /*ui4*/ _3)         \
  ((__m256i)__builtin_lasx_xvsrani_b_h((v32i8)(_1), (v32i8)(_2), (_3)))

#define __lasx_xvsrani_h_w(/*__m256i*/ _1, /*__m256i*/ _2, /*ui5*/ _3)         \
  ((__m256i)__builtin_lasx_xvsrani_h_w((v16i16)(_1), (v16i16)(_2), (_3)))

#define __lasx_xvsrani_w_d(/*__m256i*/ _1, /*__m256i*/ _2, /*ui6*/ _3)         \
  ((__m256i)__builtin_lasx_xvsrani_w_d((v8i32)(_1), (v8i32)(_2), (_3)))

#define __lasx_xvsrani_d_q(/*__m256i*/ _1, /*__m256i*/ _2, /*ui7*/ _3)         \
  ((__m256i)__builtin_lasx_xvsrani_d_q((v4i64)(_1), (v4i64)(_2), (_3)))

#define __lasx_xvsrarni_b_h(/*__m256i*/ _1, /*__m256i*/ _2, /*ui4*/ _3)        \
  ((__m256i)__builtin_lasx_xvsrarni_b_h((v32i8)(_1), (v32i8)(_2), (_3)))

#define __lasx_xvsrarni_h_w(/*__m256i*/ _1, /*__m256i*/ _2, /*ui5*/ _3)        \
  ((__m256i)__builtin_lasx_xvsrarni_h_w((v16i16)(_1), (v16i16)(_2), (_3)))

#define __lasx_xvsrarni_w_d(/*__m256i*/ _1, /*__m256i*/ _2, /*ui6*/ _3)        \
  ((__m256i)__builtin_lasx_xvsrarni_w_d((v8i32)(_1), (v8i32)(_2), (_3)))

#define __lasx_xvsrarni_d_q(/*__m256i*/ _1, /*__m256i*/ _2, /*ui7*/ _3)        \
  ((__m256i)__builtin_lasx_xvsrarni_d_q((v4i64)(_1), (v4i64)(_2), (_3)))

#define __lasx_xvssrani_b_h(/*__m256i*/ _1, /*__m256i*/ _2, /*ui4*/ _3)        \
  ((__m256i)__builtin_lasx_xvssrani_b_h((v32i8)(_1), (v32i8)(_2), (_3)))

#define __lasx_xvssrani_h_w(/*__m256i*/ _1, /*__m256i*/ _2, /*ui5*/ _3)        \
  ((__m256i)__builtin_lasx_xvssrani_h_w((v16i16)(_1), (v16i16)(_2), (_3)))

#define __lasx_xvssrani_w_d(/*__m256i*/ _1, /*__m256i*/ _2, /*ui6*/ _3)        \
  ((__m256i)__builtin_lasx_xvssrani_w_d((v8i32)(_1), (v8i32)(_2), (_3)))

#define __lasx_xvssrani_d_q(/*__m256i*/ _1, /*__m256i*/ _2, /*ui7*/ _3)        \
  ((__m256i)__builtin_lasx_xvssrani_d_q((v4i64)(_1), (v4i64)(_2), (_3)))

#define __lasx_xvssrani_bu_h(/*__m256i*/ _1, /*__m256i*/ _2, /*ui4*/ _3)       \
  ((__m256i)__builtin_lasx_xvssrani_bu_h((v32u8)(_1), (v32i8)(_2), (_3)))

#define __lasx_xvssrani_hu_w(/*__m256i*/ _1, /*__m256i*/ _2, /*ui5*/ _3)       \
  ((__m256i)__builtin_lasx_xvssrani_hu_w((v16u16)(_1), (v16i16)(_2), (_3)))

#define __lasx_xvssrani_wu_d(/*__m256i*/ _1, /*__m256i*/ _2, /*ui6*/ _3)       \
  ((__m256i)__builtin_lasx_xvssrani_wu_d((v8u32)(_1), (v8i32)(_2), (_3)))

#define __lasx_xvssrani_du_q(/*__m256i*/ _1, /*__m256i*/ _2, /*ui7*/ _3)       \
  ((__m256i)__builtin_lasx_xvssrani_du_q((v4u64)(_1), (v4i64)(_2), (_3)))

#define __lasx_xvssrarni_b_h(/*__m256i*/ _1, /*__m256i*/ _2, /*ui4*/ _3)       \
  ((__m256i)__builtin_lasx_xvssrarni_b_h((v32i8)(_1), (v32i8)(_2), (_3)))

#define __lasx_xvssrarni_h_w(/*__m256i*/ _1, /*__m256i*/ _2, /*ui5*/ _3)       \
  ((__m256i)__builtin_lasx_xvssrarni_h_w((v16i16)(_1), (v16i16)(_2), (_3)))

#define __lasx_xvssrarni_w_d(/*__m256i*/ _1, /*__m256i*/ _2, /*ui6*/ _3)       \
  ((__m256i)__builtin_lasx_xvssrarni_w_d((v8i32)(_1), (v8i32)(_2), (_3)))

#define __lasx_xvssrarni_d_q(/*__m256i*/ _1, /*__m256i*/ _2, /*ui7*/ _3)       \
  ((__m256i)__builtin_lasx_xvssrarni_d_q((v4i64)(_1), (v4i64)(_2), (_3)))

#define __lasx_xvssrarni_bu_h(/*__m256i*/ _1, /*__m256i*/ _2, /*ui4*/ _3)      \
  ((__m256i)__builtin_lasx_xvssrarni_bu_h((v32u8)(_1), (v32i8)(_2), (_3)))

#define __lasx_xvssrarni_hu_w(/*__m256i*/ _1, /*__m256i*/ _2, /*ui5*/ _3)      \
  ((__m256i)__builtin_lasx_xvssrarni_hu_w((v16u16)(_1), (v16i16)(_2), (_3)))

#define __lasx_xvssrarni_wu_d(/*__m256i*/ _1, /*__m256i*/ _2, /*ui6*/ _3)      \
  ((__m256i)__builtin_lasx_xvssrarni_wu_d((v8u32)(_1), (v8i32)(_2), (_3)))

#define __lasx_xvssrarni_du_q(/*__m256i*/ _1, /*__m256i*/ _2, /*ui7*/ _3)      \
  ((__m256i)__builtin_lasx_xvssrarni_du_q((v4u64)(_1), (v4i64)(_2), (_3)))

#define __lasx_xbnz_b(/*__m256i*/ _1) ((int)__builtin_lasx_xbnz_b((v32u8)(_1)))

#define __lasx_xbnz_d(/*__m256i*/ _1) ((int)__builtin_lasx_xbnz_d((v4u64)(_1)))

#define __lasx_xbnz_h(/*__m256i*/ _1) ((int)__builtin_lasx_xbnz_h((v16u16)(_1)))

#define __lasx_xbnz_v(/*__m256i*/ _1) ((int)__builtin_lasx_xbnz_v((v32u8)(_1)))

#define __lasx_xbnz_w(/*__m256i*/ _1) ((int)__builtin_lasx_xbnz_w((v8u32)(_1)))

#define __lasx_xbz_b(/*__m256i*/ _1) ((int)__builtin_lasx_xbz_b((v32u8)(_1)))

#define __lasx_xbz_d(/*__m256i*/ _1) ((int)__builtin_lasx_xbz_d((v4u64)(_1)))

#define __lasx_xbz_h(/*__m256i*/ _1) ((int)__builtin_lasx_xbz_h((v16u16)(_1)))

#define __lasx_xbz_v(/*__m256i*/ _1) ((int)__builtin_lasx_xbz_v((v32u8)(_1)))

#define __lasx_xbz_w(/*__m256i*/ _1) ((int)__builtin_lasx_xbz_w((v8u32)(_1)))

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvfcmp_caf_d(__m256d _1, __m256d _2) {
  return (__m256i)__builtin_lasx_xvfcmp_caf_d((v4f64)_1, (v4f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvfcmp_caf_s(__m256 _1, __m256 _2) {
  return (__m256i)__builtin_lasx_xvfcmp_caf_s((v8f32)_1, (v8f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvfcmp_ceq_d(__m256d _1, __m256d _2) {
  return (__m256i)__builtin_lasx_xvfcmp_ceq_d((v4f64)_1, (v4f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvfcmp_ceq_s(__m256 _1, __m256 _2) {
  return (__m256i)__builtin_lasx_xvfcmp_ceq_s((v8f32)_1, (v8f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvfcmp_cle_d(__m256d _1, __m256d _2) {
  return (__m256i)__builtin_lasx_xvfcmp_cle_d((v4f64)_1, (v4f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvfcmp_cle_s(__m256 _1, __m256 _2) {
  return (__m256i)__builtin_lasx_xvfcmp_cle_s((v8f32)_1, (v8f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvfcmp_clt_d(__m256d _1, __m256d _2) {
  return (__m256i)__builtin_lasx_xvfcmp_clt_d((v4f64)_1, (v4f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvfcmp_clt_s(__m256 _1, __m256 _2) {
  return (__m256i)__builtin_lasx_xvfcmp_clt_s((v8f32)_1, (v8f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvfcmp_cne_d(__m256d _1, __m256d _2) {
  return (__m256i)__builtin_lasx_xvfcmp_cne_d((v4f64)_1, (v4f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvfcmp_cne_s(__m256 _1, __m256 _2) {
  return (__m256i)__builtin_lasx_xvfcmp_cne_s((v8f32)_1, (v8f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvfcmp_cor_d(__m256d _1, __m256d _2) {
  return (__m256i)__builtin_lasx_xvfcmp_cor_d((v4f64)_1, (v4f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvfcmp_cor_s(__m256 _1, __m256 _2) {
  return (__m256i)__builtin_lasx_xvfcmp_cor_s((v8f32)_1, (v8f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvfcmp_cueq_d(__m256d _1, __m256d _2) {
  return (__m256i)__builtin_lasx_xvfcmp_cueq_d((v4f64)_1, (v4f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvfcmp_cueq_s(__m256 _1, __m256 _2) {
  return (__m256i)__builtin_lasx_xvfcmp_cueq_s((v8f32)_1, (v8f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvfcmp_cule_d(__m256d _1, __m256d _2) {
  return (__m256i)__builtin_lasx_xvfcmp_cule_d((v4f64)_1, (v4f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvfcmp_cule_s(__m256 _1, __m256 _2) {
  return (__m256i)__builtin_lasx_xvfcmp_cule_s((v8f32)_1, (v8f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvfcmp_cult_d(__m256d _1, __m256d _2) {
  return (__m256i)__builtin_lasx_xvfcmp_cult_d((v4f64)_1, (v4f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvfcmp_cult_s(__m256 _1, __m256 _2) {
  return (__m256i)__builtin_lasx_xvfcmp_cult_s((v8f32)_1, (v8f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvfcmp_cun_d(__m256d _1, __m256d _2) {
  return (__m256i)__builtin_lasx_xvfcmp_cun_d((v4f64)_1, (v4f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvfcmp_cune_d(__m256d _1, __m256d _2) {
  return (__m256i)__builtin_lasx_xvfcmp_cune_d((v4f64)_1, (v4f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvfcmp_cune_s(__m256 _1, __m256 _2) {
  return (__m256i)__builtin_lasx_xvfcmp_cune_s((v8f32)_1, (v8f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvfcmp_cun_s(__m256 _1, __m256 _2) {
  return (__m256i)__builtin_lasx_xvfcmp_cun_s((v8f32)_1, (v8f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvfcmp_saf_d(__m256d _1, __m256d _2) {
  return (__m256i)__builtin_lasx_xvfcmp_saf_d((v4f64)_1, (v4f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvfcmp_saf_s(__m256 _1, __m256 _2) {
  return (__m256i)__builtin_lasx_xvfcmp_saf_s((v8f32)_1, (v8f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvfcmp_seq_d(__m256d _1, __m256d _2) {
  return (__m256i)__builtin_lasx_xvfcmp_seq_d((v4f64)_1, (v4f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvfcmp_seq_s(__m256 _1, __m256 _2) {
  return (__m256i)__builtin_lasx_xvfcmp_seq_s((v8f32)_1, (v8f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvfcmp_sle_d(__m256d _1, __m256d _2) {
  return (__m256i)__builtin_lasx_xvfcmp_sle_d((v4f64)_1, (v4f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvfcmp_sle_s(__m256 _1, __m256 _2) {
  return (__m256i)__builtin_lasx_xvfcmp_sle_s((v8f32)_1, (v8f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvfcmp_slt_d(__m256d _1, __m256d _2) {
  return (__m256i)__builtin_lasx_xvfcmp_slt_d((v4f64)_1, (v4f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvfcmp_slt_s(__m256 _1, __m256 _2) {
  return (__m256i)__builtin_lasx_xvfcmp_slt_s((v8f32)_1, (v8f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvfcmp_sne_d(__m256d _1, __m256d _2) {
  return (__m256i)__builtin_lasx_xvfcmp_sne_d((v4f64)_1, (v4f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvfcmp_sne_s(__m256 _1, __m256 _2) {
  return (__m256i)__builtin_lasx_xvfcmp_sne_s((v8f32)_1, (v8f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvfcmp_sor_d(__m256d _1, __m256d _2) {
  return (__m256i)__builtin_lasx_xvfcmp_sor_d((v4f64)_1, (v4f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvfcmp_sor_s(__m256 _1, __m256 _2) {
  return (__m256i)__builtin_lasx_xvfcmp_sor_s((v8f32)_1, (v8f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvfcmp_sueq_d(__m256d _1, __m256d _2) {
  return (__m256i)__builtin_lasx_xvfcmp_sueq_d((v4f64)_1, (v4f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvfcmp_sueq_s(__m256 _1, __m256 _2) {
  return (__m256i)__builtin_lasx_xvfcmp_sueq_s((v8f32)_1, (v8f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvfcmp_sule_d(__m256d _1, __m256d _2) {
  return (__m256i)__builtin_lasx_xvfcmp_sule_d((v4f64)_1, (v4f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvfcmp_sule_s(__m256 _1, __m256 _2) {
  return (__m256i)__builtin_lasx_xvfcmp_sule_s((v8f32)_1, (v8f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvfcmp_sult_d(__m256d _1, __m256d _2) {
  return (__m256i)__builtin_lasx_xvfcmp_sult_d((v4f64)_1, (v4f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvfcmp_sult_s(__m256 _1, __m256 _2) {
  return (__m256i)__builtin_lasx_xvfcmp_sult_s((v8f32)_1, (v8f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvfcmp_sun_d(__m256d _1, __m256d _2) {
  return (__m256i)__builtin_lasx_xvfcmp_sun_d((v4f64)_1, (v4f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvfcmp_sune_d(__m256d _1, __m256d _2) {
  return (__m256i)__builtin_lasx_xvfcmp_sune_d((v4f64)_1, (v4f64)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvfcmp_sune_s(__m256 _1, __m256 _2) {
  return (__m256i)__builtin_lasx_xvfcmp_sune_s((v8f32)_1, (v8f32)_2);
}

extern __inline
    __attribute__((__gnu_inline__, __always_inline__, __artificial__)) __m256i
    __lasx_xvfcmp_sun_s(__m256 _1, __m256 _2) {
  return (__m256i)__builtin_lasx_xvfcmp_sun_s((v8f32)_1, (v8f32)_2);
}

#define __lasx_xvpickve_d_f(/*__m256d*/ _1, /*ui2*/ _2)                        \
  ((__m256d)__builtin_lasx_xvpickve_d_f((v4f64)(_1), (_2)))

#define __lasx_xvpickve_w_f(/*__m256*/ _1, /*ui3*/ _2)                         \
  ((__m256)__builtin_lasx_xvpickve_w_f((v8f32)(_1), (_2)))

#define __lasx_xvrepli_b(/*si10*/ _1) ((__m256i)__builtin_lasx_xvrepli_b((_1)))

#define __lasx_xvrepli_d(/*si10*/ _1) ((__m256i)__builtin_lasx_xvrepli_d((_1)))

#define __lasx_xvrepli_h(/*si10*/ _1) ((__m256i)__builtin_lasx_xvrepli_h((_1)))

#define __lasx_xvrepli_w(/*si10*/ _1) ((__m256i)__builtin_lasx_xvrepli_w((_1)))

#endif /* defined(__loongarch_asx).  */
#endif /* _LOONGSON_ASXINTRIN_H.  */
