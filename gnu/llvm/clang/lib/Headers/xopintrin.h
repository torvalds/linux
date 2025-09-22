/*===---- xopintrin.h - XOP intrinsics -------------------------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __X86INTRIN_H
#error "Never use <xopintrin.h> directly; include <x86intrin.h> instead."
#endif

#ifndef __XOPINTRIN_H
#define __XOPINTRIN_H

#include <fma4intrin.h>

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS __attribute__((__always_inline__, __nodebug__, __target__("xop"), __min_vector_width__(128)))
#define __DEFAULT_FN_ATTRS256 __attribute__((__always_inline__, __nodebug__, __target__("xop"), __min_vector_width__(256)))

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_maccs_epi16(__m128i __A, __m128i __B, __m128i __C)
{
  return (__m128i)__builtin_ia32_vpmacssww((__v8hi)__A, (__v8hi)__B, (__v8hi)__C);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_macc_epi16(__m128i __A, __m128i __B, __m128i __C)
{
  return (__m128i)__builtin_ia32_vpmacsww((__v8hi)__A, (__v8hi)__B, (__v8hi)__C);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_maccsd_epi16(__m128i __A, __m128i __B, __m128i __C)
{
  return (__m128i)__builtin_ia32_vpmacsswd((__v8hi)__A, (__v8hi)__B, (__v4si)__C);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_maccd_epi16(__m128i __A, __m128i __B, __m128i __C)
{
  return (__m128i)__builtin_ia32_vpmacswd((__v8hi)__A, (__v8hi)__B, (__v4si)__C);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_maccs_epi32(__m128i __A, __m128i __B, __m128i __C)
{
  return (__m128i)__builtin_ia32_vpmacssdd((__v4si)__A, (__v4si)__B, (__v4si)__C);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_macc_epi32(__m128i __A, __m128i __B, __m128i __C)
{
  return (__m128i)__builtin_ia32_vpmacsdd((__v4si)__A, (__v4si)__B, (__v4si)__C);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_maccslo_epi32(__m128i __A, __m128i __B, __m128i __C)
{
  return (__m128i)__builtin_ia32_vpmacssdql((__v4si)__A, (__v4si)__B, (__v2di)__C);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_macclo_epi32(__m128i __A, __m128i __B, __m128i __C)
{
  return (__m128i)__builtin_ia32_vpmacsdql((__v4si)__A, (__v4si)__B, (__v2di)__C);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_maccshi_epi32(__m128i __A, __m128i __B, __m128i __C)
{
  return (__m128i)__builtin_ia32_vpmacssdqh((__v4si)__A, (__v4si)__B, (__v2di)__C);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_macchi_epi32(__m128i __A, __m128i __B, __m128i __C)
{
  return (__m128i)__builtin_ia32_vpmacsdqh((__v4si)__A, (__v4si)__B, (__v2di)__C);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_maddsd_epi16(__m128i __A, __m128i __B, __m128i __C)
{
  return (__m128i)__builtin_ia32_vpmadcsswd((__v8hi)__A, (__v8hi)__B, (__v4si)__C);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_maddd_epi16(__m128i __A, __m128i __B, __m128i __C)
{
  return (__m128i)__builtin_ia32_vpmadcswd((__v8hi)__A, (__v8hi)__B, (__v4si)__C);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_haddw_epi8(__m128i __A)
{
  return (__m128i)__builtin_ia32_vphaddbw((__v16qi)__A);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_haddd_epi8(__m128i __A)
{
  return (__m128i)__builtin_ia32_vphaddbd((__v16qi)__A);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_haddq_epi8(__m128i __A)
{
  return (__m128i)__builtin_ia32_vphaddbq((__v16qi)__A);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_haddd_epi16(__m128i __A)
{
  return (__m128i)__builtin_ia32_vphaddwd((__v8hi)__A);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_haddq_epi16(__m128i __A)
{
  return (__m128i)__builtin_ia32_vphaddwq((__v8hi)__A);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_haddq_epi32(__m128i __A)
{
  return (__m128i)__builtin_ia32_vphadddq((__v4si)__A);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_haddw_epu8(__m128i __A)
{
  return (__m128i)__builtin_ia32_vphaddubw((__v16qi)__A);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_haddd_epu8(__m128i __A)
{
  return (__m128i)__builtin_ia32_vphaddubd((__v16qi)__A);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_haddq_epu8(__m128i __A)
{
  return (__m128i)__builtin_ia32_vphaddubq((__v16qi)__A);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_haddd_epu16(__m128i __A)
{
  return (__m128i)__builtin_ia32_vphadduwd((__v8hi)__A);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_haddq_epu16(__m128i __A)
{
  return (__m128i)__builtin_ia32_vphadduwq((__v8hi)__A);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_haddq_epu32(__m128i __A)
{
  return (__m128i)__builtin_ia32_vphaddudq((__v4si)__A);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_hsubw_epi8(__m128i __A)
{
  return (__m128i)__builtin_ia32_vphsubbw((__v16qi)__A);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_hsubd_epi16(__m128i __A)
{
  return (__m128i)__builtin_ia32_vphsubwd((__v8hi)__A);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_hsubq_epi32(__m128i __A)
{
  return (__m128i)__builtin_ia32_vphsubdq((__v4si)__A);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_cmov_si128(__m128i __A, __m128i __B, __m128i __C)
{
  return (__m128i)(((__v2du)__A & (__v2du)__C) | ((__v2du)__B & ~(__v2du)__C));
}

static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_cmov_si256(__m256i __A, __m256i __B, __m256i __C)
{
  return (__m256i)(((__v4du)__A & (__v4du)__C) | ((__v4du)__B & ~(__v4du)__C));
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_perm_epi8(__m128i __A, __m128i __B, __m128i __C)
{
  return (__m128i)__builtin_ia32_vpperm((__v16qi)__A, (__v16qi)__B, (__v16qi)__C);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_rot_epi8(__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_vprotb((__v16qi)__A, (__v16qi)__B);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_rot_epi16(__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_vprotw((__v8hi)__A, (__v8hi)__B);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_rot_epi32(__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_vprotd((__v4si)__A, (__v4si)__B);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_rot_epi64(__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_vprotq((__v2di)__A, (__v2di)__B);
}

#define _mm_roti_epi8(A, N) \
  ((__m128i)__builtin_ia32_vprotbi((__v16qi)(__m128i)(A), (N)))

#define _mm_roti_epi16(A, N) \
  ((__m128i)__builtin_ia32_vprotwi((__v8hi)(__m128i)(A), (N)))

#define _mm_roti_epi32(A, N) \
  ((__m128i)__builtin_ia32_vprotdi((__v4si)(__m128i)(A), (N)))

#define _mm_roti_epi64(A, N) \
  ((__m128i)__builtin_ia32_vprotqi((__v2di)(__m128i)(A), (N)))

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_shl_epi8(__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_vpshlb((__v16qi)__A, (__v16qi)__B);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_shl_epi16(__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_vpshlw((__v8hi)__A, (__v8hi)__B);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_shl_epi32(__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_vpshld((__v4si)__A, (__v4si)__B);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_shl_epi64(__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_vpshlq((__v2di)__A, (__v2di)__B);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_sha_epi8(__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_vpshab((__v16qi)__A, (__v16qi)__B);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_sha_epi16(__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_vpshaw((__v8hi)__A, (__v8hi)__B);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_sha_epi32(__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_vpshad((__v4si)__A, (__v4si)__B);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_sha_epi64(__m128i __A, __m128i __B)
{
  return (__m128i)__builtin_ia32_vpshaq((__v2di)__A, (__v2di)__B);
}

#define _mm_com_epu8(A, B, N) \
  ((__m128i)__builtin_ia32_vpcomub((__v16qi)(__m128i)(A), \
                                   (__v16qi)(__m128i)(B), (N)))

#define _mm_com_epu16(A, B, N) \
  ((__m128i)__builtin_ia32_vpcomuw((__v8hi)(__m128i)(A), \
                                   (__v8hi)(__m128i)(B), (N)))

#define _mm_com_epu32(A, B, N) \
  ((__m128i)__builtin_ia32_vpcomud((__v4si)(__m128i)(A), \
                                   (__v4si)(__m128i)(B), (N)))

#define _mm_com_epu64(A, B, N) \
  ((__m128i)__builtin_ia32_vpcomuq((__v2di)(__m128i)(A), \
                                   (__v2di)(__m128i)(B), (N)))

#define _mm_com_epi8(A, B, N) \
  ((__m128i)__builtin_ia32_vpcomb((__v16qi)(__m128i)(A), \
                                  (__v16qi)(__m128i)(B), (N)))

#define _mm_com_epi16(A, B, N) \
  ((__m128i)__builtin_ia32_vpcomw((__v8hi)(__m128i)(A), \
                                  (__v8hi)(__m128i)(B), (N)))

#define _mm_com_epi32(A, B, N) \
  ((__m128i)__builtin_ia32_vpcomd((__v4si)(__m128i)(A), \
                                  (__v4si)(__m128i)(B), (N)))

#define _mm_com_epi64(A, B, N) \
  ((__m128i)__builtin_ia32_vpcomq((__v2di)(__m128i)(A), \
                                  (__v2di)(__m128i)(B), (N)))

#define _MM_PCOMCTRL_LT    0
#define _MM_PCOMCTRL_LE    1
#define _MM_PCOMCTRL_GT    2
#define _MM_PCOMCTRL_GE    3
#define _MM_PCOMCTRL_EQ    4
#define _MM_PCOMCTRL_NEQ   5
#define _MM_PCOMCTRL_FALSE 6
#define _MM_PCOMCTRL_TRUE  7

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comlt_epu8(__m128i __A, __m128i __B)
{
  return _mm_com_epu8(__A, __B, _MM_PCOMCTRL_LT);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comle_epu8(__m128i __A, __m128i __B)
{
  return _mm_com_epu8(__A, __B, _MM_PCOMCTRL_LE);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comgt_epu8(__m128i __A, __m128i __B)
{
  return _mm_com_epu8(__A, __B, _MM_PCOMCTRL_GT);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comge_epu8(__m128i __A, __m128i __B)
{
  return _mm_com_epu8(__A, __B, _MM_PCOMCTRL_GE);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comeq_epu8(__m128i __A, __m128i __B)
{
  return _mm_com_epu8(__A, __B, _MM_PCOMCTRL_EQ);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comneq_epu8(__m128i __A, __m128i __B)
{
  return _mm_com_epu8(__A, __B, _MM_PCOMCTRL_NEQ);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comfalse_epu8(__m128i __A, __m128i __B)
{
  return _mm_com_epu8(__A, __B, _MM_PCOMCTRL_FALSE);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comtrue_epu8(__m128i __A, __m128i __B)
{
  return _mm_com_epu8(__A, __B, _MM_PCOMCTRL_TRUE);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comlt_epu16(__m128i __A, __m128i __B)
{
  return _mm_com_epu16(__A, __B, _MM_PCOMCTRL_LT);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comle_epu16(__m128i __A, __m128i __B)
{
  return _mm_com_epu16(__A, __B, _MM_PCOMCTRL_LE);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comgt_epu16(__m128i __A, __m128i __B)
{
  return _mm_com_epu16(__A, __B, _MM_PCOMCTRL_GT);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comge_epu16(__m128i __A, __m128i __B)
{
  return _mm_com_epu16(__A, __B, _MM_PCOMCTRL_GE);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comeq_epu16(__m128i __A, __m128i __B)
{
  return _mm_com_epu16(__A, __B, _MM_PCOMCTRL_EQ);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comneq_epu16(__m128i __A, __m128i __B)
{
  return _mm_com_epu16(__A, __B, _MM_PCOMCTRL_NEQ);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comfalse_epu16(__m128i __A, __m128i __B)
{
  return _mm_com_epu16(__A, __B, _MM_PCOMCTRL_FALSE);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comtrue_epu16(__m128i __A, __m128i __B)
{
  return _mm_com_epu16(__A, __B, _MM_PCOMCTRL_TRUE);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comlt_epu32(__m128i __A, __m128i __B)
{
  return _mm_com_epu32(__A, __B, _MM_PCOMCTRL_LT);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comle_epu32(__m128i __A, __m128i __B)
{
  return _mm_com_epu32(__A, __B, _MM_PCOMCTRL_LE);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comgt_epu32(__m128i __A, __m128i __B)
{
  return _mm_com_epu32(__A, __B, _MM_PCOMCTRL_GT);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comge_epu32(__m128i __A, __m128i __B)
{
  return _mm_com_epu32(__A, __B, _MM_PCOMCTRL_GE);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comeq_epu32(__m128i __A, __m128i __B)
{
  return _mm_com_epu32(__A, __B, _MM_PCOMCTRL_EQ);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comneq_epu32(__m128i __A, __m128i __B)
{
  return _mm_com_epu32(__A, __B, _MM_PCOMCTRL_NEQ);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comfalse_epu32(__m128i __A, __m128i __B)
{
  return _mm_com_epu32(__A, __B, _MM_PCOMCTRL_FALSE);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comtrue_epu32(__m128i __A, __m128i __B)
{
  return _mm_com_epu32(__A, __B, _MM_PCOMCTRL_TRUE);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comlt_epu64(__m128i __A, __m128i __B)
{
  return _mm_com_epu64(__A, __B, _MM_PCOMCTRL_LT);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comle_epu64(__m128i __A, __m128i __B)
{
  return _mm_com_epu64(__A, __B, _MM_PCOMCTRL_LE);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comgt_epu64(__m128i __A, __m128i __B)
{
  return _mm_com_epu64(__A, __B, _MM_PCOMCTRL_GT);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comge_epu64(__m128i __A, __m128i __B)
{
  return _mm_com_epu64(__A, __B, _MM_PCOMCTRL_GE);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comeq_epu64(__m128i __A, __m128i __B)
{
  return _mm_com_epu64(__A, __B, _MM_PCOMCTRL_EQ);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comneq_epu64(__m128i __A, __m128i __B)
{
  return _mm_com_epu64(__A, __B, _MM_PCOMCTRL_NEQ);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comfalse_epu64(__m128i __A, __m128i __B)
{
  return _mm_com_epu64(__A, __B, _MM_PCOMCTRL_FALSE);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comtrue_epu64(__m128i __A, __m128i __B)
{
  return _mm_com_epu64(__A, __B, _MM_PCOMCTRL_TRUE);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comlt_epi8(__m128i __A, __m128i __B)
{
  return _mm_com_epi8(__A, __B, _MM_PCOMCTRL_LT);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comle_epi8(__m128i __A, __m128i __B)
{
  return _mm_com_epi8(__A, __B, _MM_PCOMCTRL_LE);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comgt_epi8(__m128i __A, __m128i __B)
{
  return _mm_com_epi8(__A, __B, _MM_PCOMCTRL_GT);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comge_epi8(__m128i __A, __m128i __B)
{
  return _mm_com_epi8(__A, __B, _MM_PCOMCTRL_GE);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comeq_epi8(__m128i __A, __m128i __B)
{
  return _mm_com_epi8(__A, __B, _MM_PCOMCTRL_EQ);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comneq_epi8(__m128i __A, __m128i __B)
{
  return _mm_com_epi8(__A, __B, _MM_PCOMCTRL_NEQ);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comfalse_epi8(__m128i __A, __m128i __B)
{
  return _mm_com_epi8(__A, __B, _MM_PCOMCTRL_FALSE);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comtrue_epi8(__m128i __A, __m128i __B)
{
  return _mm_com_epi8(__A, __B, _MM_PCOMCTRL_TRUE);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comlt_epi16(__m128i __A, __m128i __B)
{
  return _mm_com_epi16(__A, __B, _MM_PCOMCTRL_LT);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comle_epi16(__m128i __A, __m128i __B)
{
  return _mm_com_epi16(__A, __B, _MM_PCOMCTRL_LE);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comgt_epi16(__m128i __A, __m128i __B)
{
  return _mm_com_epi16(__A, __B, _MM_PCOMCTRL_GT);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comge_epi16(__m128i __A, __m128i __B)
{
  return _mm_com_epi16(__A, __B, _MM_PCOMCTRL_GE);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comeq_epi16(__m128i __A, __m128i __B)
{
  return _mm_com_epi16(__A, __B, _MM_PCOMCTRL_EQ);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comneq_epi16(__m128i __A, __m128i __B)
{
  return _mm_com_epi16(__A, __B, _MM_PCOMCTRL_NEQ);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comfalse_epi16(__m128i __A, __m128i __B)
{
  return _mm_com_epi16(__A, __B, _MM_PCOMCTRL_FALSE);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comtrue_epi16(__m128i __A, __m128i __B)
{
  return _mm_com_epi16(__A, __B, _MM_PCOMCTRL_TRUE);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comlt_epi32(__m128i __A, __m128i __B)
{
  return _mm_com_epi32(__A, __B, _MM_PCOMCTRL_LT);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comle_epi32(__m128i __A, __m128i __B)
{
  return _mm_com_epi32(__A, __B, _MM_PCOMCTRL_LE);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comgt_epi32(__m128i __A, __m128i __B)
{
  return _mm_com_epi32(__A, __B, _MM_PCOMCTRL_GT);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comge_epi32(__m128i __A, __m128i __B)
{
  return _mm_com_epi32(__A, __B, _MM_PCOMCTRL_GE);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comeq_epi32(__m128i __A, __m128i __B)
{
  return _mm_com_epi32(__A, __B, _MM_PCOMCTRL_EQ);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comneq_epi32(__m128i __A, __m128i __B)
{
  return _mm_com_epi32(__A, __B, _MM_PCOMCTRL_NEQ);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comfalse_epi32(__m128i __A, __m128i __B)
{
  return _mm_com_epi32(__A, __B, _MM_PCOMCTRL_FALSE);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comtrue_epi32(__m128i __A, __m128i __B)
{
  return _mm_com_epi32(__A, __B, _MM_PCOMCTRL_TRUE);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comlt_epi64(__m128i __A, __m128i __B)
{
  return _mm_com_epi64(__A, __B, _MM_PCOMCTRL_LT);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comle_epi64(__m128i __A, __m128i __B)
{
  return _mm_com_epi64(__A, __B, _MM_PCOMCTRL_LE);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comgt_epi64(__m128i __A, __m128i __B)
{
  return _mm_com_epi64(__A, __B, _MM_PCOMCTRL_GT);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comge_epi64(__m128i __A, __m128i __B)
{
  return _mm_com_epi64(__A, __B, _MM_PCOMCTRL_GE);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comeq_epi64(__m128i __A, __m128i __B)
{
  return _mm_com_epi64(__A, __B, _MM_PCOMCTRL_EQ);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comneq_epi64(__m128i __A, __m128i __B)
{
  return _mm_com_epi64(__A, __B, _MM_PCOMCTRL_NEQ);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comfalse_epi64(__m128i __A, __m128i __B)
{
  return _mm_com_epi64(__A, __B, _MM_PCOMCTRL_FALSE);
}

static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_comtrue_epi64(__m128i __A, __m128i __B)
{
  return _mm_com_epi64(__A, __B, _MM_PCOMCTRL_TRUE);
}

#define _mm_permute2_pd(X, Y, C, I) \
  ((__m128d)__builtin_ia32_vpermil2pd((__v2df)(__m128d)(X), \
                                      (__v2df)(__m128d)(Y), \
                                      (__v2di)(__m128i)(C), (I)))

#define _mm256_permute2_pd(X, Y, C, I) \
  ((__m256d)__builtin_ia32_vpermil2pd256((__v4df)(__m256d)(X), \
                                         (__v4df)(__m256d)(Y), \
                                         (__v4di)(__m256i)(C), (I)))

#define _mm_permute2_ps(X, Y, C, I) \
  ((__m128)__builtin_ia32_vpermil2ps((__v4sf)(__m128)(X), (__v4sf)(__m128)(Y), \
                                     (__v4si)(__m128i)(C), (I)))

#define _mm256_permute2_ps(X, Y, C, I) \
  ((__m256)__builtin_ia32_vpermil2ps256((__v8sf)(__m256)(X), \
                                        (__v8sf)(__m256)(Y), \
                                        (__v8si)(__m256i)(C), (I)))

static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_frcz_ss(__m128 __A)
{
  return (__m128)__builtin_ia32_vfrczss((__v4sf)__A);
}

static __inline__ __m128d __DEFAULT_FN_ATTRS
_mm_frcz_sd(__m128d __A)
{
  return (__m128d)__builtin_ia32_vfrczsd((__v2df)__A);
}

static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_frcz_ps(__m128 __A)
{
  return (__m128)__builtin_ia32_vfrczps((__v4sf)__A);
}

static __inline__ __m128d __DEFAULT_FN_ATTRS
_mm_frcz_pd(__m128d __A)
{
  return (__m128d)__builtin_ia32_vfrczpd((__v2df)__A);
}

static __inline__ __m256 __DEFAULT_FN_ATTRS256
_mm256_frcz_ps(__m256 __A)
{
  return (__m256)__builtin_ia32_vfrczps256((__v8sf)__A);
}

static __inline__ __m256d __DEFAULT_FN_ATTRS256
_mm256_frcz_pd(__m256d __A)
{
  return (__m256d)__builtin_ia32_vfrczpd256((__v4df)__A);
}

#undef __DEFAULT_FN_ATTRS
#undef __DEFAULT_FN_ATTRS256

#endif /* __XOPINTRIN_H */
