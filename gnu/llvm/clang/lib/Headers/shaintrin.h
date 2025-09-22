/*===---- shaintrin.h - SHA intrinsics -------------------------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __IMMINTRIN_H
#error "Never use <shaintrin.h> directly; include <immintrin.h> instead."
#endif

#ifndef __SHAINTRIN_H
#define __SHAINTRIN_H

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS __attribute__((__always_inline__, __nodebug__, __target__("sha"), __min_vector_width__(128)))

/// Performs four iterations of the inner loop of the SHA-1 message digest
///    algorithm using the starting SHA-1 state (A, B, C, D) from the 128-bit
///    vector of [4 x i32] in \a V1 and the next four 32-bit elements of the
///    message from the 128-bit vector of [4 x i32] in \a V2. Note that the
///    SHA-1 state variable E must have already been added to \a V2
///    (\c _mm_sha1nexte_epu32() can perform this step). Returns the updated
///    SHA-1 state (A, B, C, D) as a 128-bit vector of [4 x i32].
///
///    The SHA-1 algorithm has an inner loop of 80 iterations, twenty each
///    with a different combining function and rounding constant. This
///    intrinsic performs four iterations using a combining function and
///    rounding constant selected by \a M[1:0].
///
/// \headerfile <immintrin.h>
///
/// \code
/// __m128i _mm_sha1rnds4_epu32(__m128i V1, __m128i V2, const int M);
/// \endcode
///
/// This intrinsic corresponds to the \c SHA1RNDS4 instruction.
///
/// \param V1
///    A 128-bit vector of [4 x i32] containing the initial SHA-1 state.
/// \param V2
///    A 128-bit vector of [4 x i32] containing the next four elements of
///    the message, plus SHA-1 state variable E.
/// \param M
///    An immediate value where bits [1:0] select among four possible
///    combining functions and rounding constants (not specified here).
/// \returns A 128-bit vector of [4 x i32] containing the updated SHA-1 state.
#define _mm_sha1rnds4_epu32(V1, V2, M) \
  __builtin_ia32_sha1rnds4((__v4si)(__m128i)(V1), (__v4si)(__m128i)(V2), (M))

/// Calculates the SHA-1 state variable E from the SHA-1 state variables in
///    the 128-bit vector of [4 x i32] in \a __X, adds that to the next set of
///    four message elements in the 128-bit vector of [4 x i32] in \a __Y, and
///    returns the result.
///
/// \headerfile <immintrin.h>
///
/// This intrinsic corresponds to the \c SHA1NEXTE instruction.
///
/// \param __X
///    A 128-bit vector of [4 x i32] containing the current SHA-1 state.
/// \param __Y
///    A 128-bit vector of [4 x i32] containing the next four elements of the
///    message.
/// \returns A 128-bit vector of [4 x i32] containing the updated SHA-1
///    values.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_sha1nexte_epu32(__m128i __X, __m128i __Y)
{
  return (__m128i)__builtin_ia32_sha1nexte((__v4si)__X, (__v4si)__Y);
}

/// Performs an intermediate calculation for deriving the next four SHA-1
///    message elements using previous message elements from the 128-bit
///    vectors of [4 x i32] in \a __X and \a __Y, and returns the result.
///
/// \headerfile <immintrin.h>
///
/// This intrinsic corresponds to the \c SHA1MSG1 instruction.
///
/// \param __X
///    A 128-bit vector of [4 x i32] containing previous message elements.
/// \param __Y
///    A 128-bit vector of [4 x i32] containing previous message elements.
/// \returns A 128-bit vector of [4 x i32] containing the derived SHA-1
///    elements.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_sha1msg1_epu32(__m128i __X, __m128i __Y)
{
  return (__m128i)__builtin_ia32_sha1msg1((__v4si)__X, (__v4si)__Y);
}

/// Performs the final calculation for deriving the next four SHA-1 message
///    elements using previous message elements from the 128-bit vectors of
///    [4 x i32] in \a __X and \a __Y, and returns the result.
///
/// \headerfile <immintrin.h>
///
/// This intrinsic corresponds to the \c SHA1MSG2 instruction.
///
/// \param __X
///    A 128-bit vector of [4 x i32] containing an intermediate result.
/// \param __Y
///    A 128-bit vector of [4 x i32] containing previous message values.
/// \returns A 128-bit vector of [4 x i32] containing the updated SHA-1
///    values.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_sha1msg2_epu32(__m128i __X, __m128i __Y)
{
  return (__m128i)__builtin_ia32_sha1msg2((__v4si)__X, (__v4si)__Y);
}

/// Performs two rounds of SHA-256 operation using the following inputs: a
///    starting SHA-256 state (C, D, G, H) from the 128-bit vector of
///    [4 x i32] in \a __X; a starting SHA-256 state (A, B, E, F) from the
///    128-bit vector of [4 x i32] in \a __Y; and a pre-computed sum of the
///    next two message elements (unsigned 32-bit integers) and corresponding
///    rounding constants from the 128-bit vector of [4 x i32] in \a __Z.
///    Returns the updated SHA-256 state (A, B, E, F) as a 128-bit vector of
///    [4 x i32].
///
///    The SHA-256 algorithm has a core loop of 64 iterations. This intrinsic
///    performs two of those iterations.
///
/// \headerfile <immintrin.h>
///
/// This intrinsic corresponds to the \c SHA256RNDS2 instruction.
///
/// \param __X
///    A 128-bit vector of [4 x i32] containing part of the initial SHA-256
///    state.
/// \param __Y
///    A 128-bit vector of [4 x i32] containing part of the initial SHA-256
///    state.
/// \param __Z
///    A 128-bit vector of [4 x i32] containing additional input to the
///    SHA-256 operation.
/// \returns A 128-bit vector of [4 x i32] containing the updated SHA-1 state.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_sha256rnds2_epu32(__m128i __X, __m128i __Y, __m128i __Z)
{
  return (__m128i)__builtin_ia32_sha256rnds2((__v4si)__X, (__v4si)__Y, (__v4si)__Z);
}

/// Performs an intermediate calculation for deriving the next four SHA-256
///    message elements using previous message elements from the 128-bit
///    vectors of [4 x i32] in \a __X and \a __Y, and returns the result.
///
/// \headerfile <immintrin.h>
///
/// This intrinsic corresponds to the \c SHA256MSG1 instruction.
///
/// \param __X
///    A 128-bit vector of [4 x i32] containing previous message elements.
/// \param __Y
///    A 128-bit vector of [4 x i32] containing previous message elements.
/// \returns A 128-bit vector of [4 x i32] containing the updated SHA-256
///    values.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_sha256msg1_epu32(__m128i __X, __m128i __Y)
{
  return (__m128i)__builtin_ia32_sha256msg1((__v4si)__X, (__v4si)__Y);
}

/// Performs the final calculation for deriving the next four SHA-256 message
///    elements using previous message elements from the 128-bit vectors of
///    [4 x i32] in \a __X and \a __Y, and returns the result.
///
/// \headerfile <immintrin.h>
///
/// This intrinsic corresponds to the \c SHA256MSG2 instruction.
///
/// \param __X
///    A 128-bit vector of [4 x i32] containing an intermediate result.
/// \param __Y
///    A 128-bit vector of [4 x i32] containing previous message values.
/// \returns A 128-bit vector of [4 x i32] containing the updated SHA-256
///    values.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_sha256msg2_epu32(__m128i __X, __m128i __Y)
{
  return (__m128i)__builtin_ia32_sha256msg2((__v4si)__X, (__v4si)__Y);
}

#undef __DEFAULT_FN_ATTRS

#endif /* __SHAINTRIN_H */
