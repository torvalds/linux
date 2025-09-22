/*===------------ vpclmulqdqintrin.h - VPCLMULQDQ intrinsics ---------------===
 *
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */
#ifndef __IMMINTRIN_H
#error "Never use <vpclmulqdqintrin.h> directly; include <immintrin.h> instead."
#endif

#ifndef __VPCLMULQDQINTRIN_H
#define __VPCLMULQDQINTRIN_H

#define _mm256_clmulepi64_epi128(A, B, I) \
  ((__m256i)__builtin_ia32_pclmulqdq256((__v4di)(__m256i)(A),  \
                                        (__v4di)(__m256i)(B),  \
                                        (char)(I)))

#ifdef __AVX512FINTRIN_H
#define _mm512_clmulepi64_epi128(A, B, I) \
  ((__m512i)__builtin_ia32_pclmulqdq512((__v8di)(__m512i)(A),  \
                                        (__v8di)(__m512i)(B),  \
                                        (char)(I)))
#endif // __AVX512FINTRIN_H

#endif /* __VPCLMULQDQINTRIN_H */

