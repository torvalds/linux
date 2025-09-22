/*===---- fma4intrin.h - FMA4 intrinsics -----------------------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __X86INTRIN_H
#error "Never use <fma4intrin.h> directly; include <x86intrin.h> instead."
#endif

#ifndef __FMA4INTRIN_H
#define __FMA4INTRIN_H

#include <pmmintrin.h>

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS128 __attribute__((__always_inline__, __nodebug__, __target__("fma4"), __min_vector_width__(128)))
#define __DEFAULT_FN_ATTRS256 __attribute__((__always_inline__, __nodebug__, __target__("fma4"), __min_vector_width__(256)))

static __inline__ __m128 __DEFAULT_FN_ATTRS128
_mm_macc_ps(__m128 __A, __m128 __B, __m128 __C)
{
  return (__m128)__builtin_ia32_vfmaddps((__v4sf)__A, (__v4sf)__B, (__v4sf)__C);
}

static __inline__ __m128d __DEFAULT_FN_ATTRS128
_mm_macc_pd(__m128d __A, __m128d __B, __m128d __C)
{
  return (__m128d)__builtin_ia32_vfmaddpd((__v2df)__A, (__v2df)__B, (__v2df)__C);
}

static __inline__ __m128 __DEFAULT_FN_ATTRS128
_mm_macc_ss(__m128 __A, __m128 __B, __m128 __C)
{
  return (__m128)__builtin_ia32_vfmaddss((__v4sf)__A, (__v4sf)__B, (__v4sf)__C);
}

static __inline__ __m128d __DEFAULT_FN_ATTRS128
_mm_macc_sd(__m128d __A, __m128d __B, __m128d __C)
{
  return (__m128d)__builtin_ia32_vfmaddsd((__v2df)__A, (__v2df)__B, (__v2df)__C);
}

static __inline__ __m128 __DEFAULT_FN_ATTRS128
_mm_msub_ps(__m128 __A, __m128 __B, __m128 __C)
{
  return (__m128)__builtin_ia32_vfmaddps((__v4sf)__A, (__v4sf)__B, -(__v4sf)__C);
}

static __inline__ __m128d __DEFAULT_FN_ATTRS128
_mm_msub_pd(__m128d __A, __m128d __B, __m128d __C)
{
  return (__m128d)__builtin_ia32_vfmaddpd((__v2df)__A, (__v2df)__B, -(__v2df)__C);
}

static __inline__ __m128 __DEFAULT_FN_ATTRS128
_mm_msub_ss(__m128 __A, __m128 __B, __m128 __C)
{
  return (__m128)__builtin_ia32_vfmaddss((__v4sf)__A, (__v4sf)__B, -(__v4sf)__C);
}

static __inline__ __m128d __DEFAULT_FN_ATTRS128
_mm_msub_sd(__m128d __A, __m128d __B, __m128d __C)
{
  return (__m128d)__builtin_ia32_vfmaddsd((__v2df)__A, (__v2df)__B, -(__v2df)__C);
}

static __inline__ __m128 __DEFAULT_FN_ATTRS128
_mm_nmacc_ps(__m128 __A, __m128 __B, __m128 __C)
{
  return (__m128)__builtin_ia32_vfmaddps(-(__v4sf)__A, (__v4sf)__B, (__v4sf)__C);
}

static __inline__ __m128d __DEFAULT_FN_ATTRS128
_mm_nmacc_pd(__m128d __A, __m128d __B, __m128d __C)
{
  return (__m128d)__builtin_ia32_vfmaddpd(-(__v2df)__A, (__v2df)__B, (__v2df)__C);
}

static __inline__ __m128 __DEFAULT_FN_ATTRS128
_mm_nmacc_ss(__m128 __A, __m128 __B, __m128 __C)
{
  return (__m128)__builtin_ia32_vfmaddss(-(__v4sf)__A, (__v4sf)__B, (__v4sf)__C);
}

static __inline__ __m128d __DEFAULT_FN_ATTRS128
_mm_nmacc_sd(__m128d __A, __m128d __B, __m128d __C)
{
  return (__m128d)__builtin_ia32_vfmaddsd(-(__v2df)__A, (__v2df)__B, (__v2df)__C);
}

static __inline__ __m128 __DEFAULT_FN_ATTRS128
_mm_nmsub_ps(__m128 __A, __m128 __B, __m128 __C)
{
  return (__m128)__builtin_ia32_vfmaddps(-(__v4sf)__A, (__v4sf)__B, -(__v4sf)__C);
}

static __inline__ __m128d __DEFAULT_FN_ATTRS128
_mm_nmsub_pd(__m128d __A, __m128d __B, __m128d __C)
{
  return (__m128d)__builtin_ia32_vfmaddpd(-(__v2df)__A, (__v2df)__B, -(__v2df)__C);
}

static __inline__ __m128 __DEFAULT_FN_ATTRS128
_mm_nmsub_ss(__m128 __A, __m128 __B, __m128 __C)
{
  return (__m128)__builtin_ia32_vfmaddss(-(__v4sf)__A, (__v4sf)__B, -(__v4sf)__C);
}

static __inline__ __m128d __DEFAULT_FN_ATTRS128
_mm_nmsub_sd(__m128d __A, __m128d __B, __m128d __C)
{
  return (__m128d)__builtin_ia32_vfmaddsd(-(__v2df)__A, (__v2df)__B, -(__v2df)__C);
}

static __inline__ __m128 __DEFAULT_FN_ATTRS128
_mm_maddsub_ps(__m128 __A, __m128 __B, __m128 __C)
{
  return (__m128)__builtin_ia32_vfmaddsubps((__v4sf)__A, (__v4sf)__B, (__v4sf)__C);
}

static __inline__ __m128d __DEFAULT_FN_ATTRS128
_mm_maddsub_pd(__m128d __A, __m128d __B, __m128d __C)
{
  return (__m128d)__builtin_ia32_vfmaddsubpd((__v2df)__A, (__v2df)__B, (__v2df)__C);
}

static __inline__ __m128 __DEFAULT_FN_ATTRS128
_mm_msubadd_ps(__m128 __A, __m128 __B, __m128 __C)
{
  return (__m128)__builtin_ia32_vfmaddsubps((__v4sf)__A, (__v4sf)__B, -(__v4sf)__C);
}

static __inline__ __m128d __DEFAULT_FN_ATTRS128
_mm_msubadd_pd(__m128d __A, __m128d __B, __m128d __C)
{
  return (__m128d)__builtin_ia32_vfmaddsubpd((__v2df)__A, (__v2df)__B, -(__v2df)__C);
}

static __inline__ __m256 __DEFAULT_FN_ATTRS256
_mm256_macc_ps(__m256 __A, __m256 __B, __m256 __C)
{
  return (__m256)__builtin_ia32_vfmaddps256((__v8sf)__A, (__v8sf)__B, (__v8sf)__C);
}

static __inline__ __m256d __DEFAULT_FN_ATTRS256
_mm256_macc_pd(__m256d __A, __m256d __B, __m256d __C)
{
  return (__m256d)__builtin_ia32_vfmaddpd256((__v4df)__A, (__v4df)__B, (__v4df)__C);
}

static __inline__ __m256 __DEFAULT_FN_ATTRS256
_mm256_msub_ps(__m256 __A, __m256 __B, __m256 __C)
{
  return (__m256)__builtin_ia32_vfmaddps256((__v8sf)__A, (__v8sf)__B, -(__v8sf)__C);
}

static __inline__ __m256d __DEFAULT_FN_ATTRS256
_mm256_msub_pd(__m256d __A, __m256d __B, __m256d __C)
{
  return (__m256d)__builtin_ia32_vfmaddpd256((__v4df)__A, (__v4df)__B, -(__v4df)__C);
}

static __inline__ __m256 __DEFAULT_FN_ATTRS256
_mm256_nmacc_ps(__m256 __A, __m256 __B, __m256 __C)
{
  return (__m256)__builtin_ia32_vfmaddps256(-(__v8sf)__A, (__v8sf)__B, (__v8sf)__C);
}

static __inline__ __m256d __DEFAULT_FN_ATTRS256
_mm256_nmacc_pd(__m256d __A, __m256d __B, __m256d __C)
{
  return (__m256d)__builtin_ia32_vfmaddpd256(-(__v4df)__A, (__v4df)__B, (__v4df)__C);
}

static __inline__ __m256 __DEFAULT_FN_ATTRS256
_mm256_nmsub_ps(__m256 __A, __m256 __B, __m256 __C)
{
  return (__m256)__builtin_ia32_vfmaddps256(-(__v8sf)__A, (__v8sf)__B, -(__v8sf)__C);
}

static __inline__ __m256d __DEFAULT_FN_ATTRS256
_mm256_nmsub_pd(__m256d __A, __m256d __B, __m256d __C)
{
  return (__m256d)__builtin_ia32_vfmaddpd256(-(__v4df)__A, (__v4df)__B, -(__v4df)__C);
}

static __inline__ __m256 __DEFAULT_FN_ATTRS256
_mm256_maddsub_ps(__m256 __A, __m256 __B, __m256 __C)
{
  return (__m256)__builtin_ia32_vfmaddsubps256((__v8sf)__A, (__v8sf)__B, (__v8sf)__C);
}

static __inline__ __m256d __DEFAULT_FN_ATTRS256
_mm256_maddsub_pd(__m256d __A, __m256d __B, __m256d __C)
{
  return (__m256d)__builtin_ia32_vfmaddsubpd256((__v4df)__A, (__v4df)__B, (__v4df)__C);
}

static __inline__ __m256 __DEFAULT_FN_ATTRS256
_mm256_msubadd_ps(__m256 __A, __m256 __B, __m256 __C)
{
  return (__m256)__builtin_ia32_vfmaddsubps256((__v8sf)__A, (__v8sf)__B, -(__v8sf)__C);
}

static __inline__ __m256d __DEFAULT_FN_ATTRS256
_mm256_msubadd_pd(__m256d __A, __m256d __B, __m256d __C)
{
  return (__m256d)__builtin_ia32_vfmaddsubpd256((__v4df)__A, (__v4df)__B, -(__v4df)__C);
}

#undef __DEFAULT_FN_ATTRS128
#undef __DEFAULT_FN_ATTRS256

#endif /* __FMA4INTRIN_H */
