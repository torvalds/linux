/*===------- avx512vpintersectintrin.h - VP2INTERSECT intrinsics ------------===
 *
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 *===-----------------------------------------------------------------------===
 */
#ifndef __IMMINTRIN_H
#error "Never use <avx512vp2intersect.h> directly; include <immintrin.h> instead."
#endif

#ifndef _AVX512VP2INTERSECT_H
#define _AVX512VP2INTERSECT_H

#define __DEFAULT_FN_ATTRS                                                     \
  __attribute__((__always_inline__, __nodebug__,                               \
                 __target__("avx512vp2intersect,evex512"),                     \
                 __min_vector_width__(512)))

/// Store, in an even/odd pair of mask registers, the indicators of the
/// locations of value matches between dwords in operands __a and __b.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VP2INTERSECTD </c> instruction.
///
/// \param __a
///    A 512-bit vector of [16 x i32].
/// \param __b
///    A 512-bit vector of [16 x i32]
/// \param __m0
///    A pointer point to 16-bit mask
/// \param __m1
///    A pointer point to 16-bit mask
static __inline__ void __DEFAULT_FN_ATTRS
_mm512_2intersect_epi32(__m512i __a, __m512i __b, __mmask16 *__m0, __mmask16 *__m1) {
  __builtin_ia32_vp2intersect_d_512((__v16si)__a, (__v16si)__b, __m0, __m1);
}

/// Store, in an even/odd pair of mask registers, the indicators of the
/// locations of value matches between quadwords in operands __a and __b.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VP2INTERSECTQ </c> instruction.
///
/// \param __a
///    A 512-bit vector of [8 x i64].
/// \param __b
///    A 512-bit vector of [8 x i64]
/// \param __m0
///    A pointer point to 8-bit mask
/// \param __m1
///    A pointer point to 8-bit mask
static __inline__ void __DEFAULT_FN_ATTRS
_mm512_2intersect_epi64(__m512i __a, __m512i __b, __mmask8 *__m0, __mmask8 *__m1) {
  __builtin_ia32_vp2intersect_q_512((__v8di)__a, (__v8di)__b, __m0, __m1);
}

#undef __DEFAULT_FN_ATTRS

#endif
