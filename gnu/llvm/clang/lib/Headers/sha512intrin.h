/*===--------------- sha512intrin.h - SHA512 intrinsics -----------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __IMMINTRIN_H
#error "Never use <sha512intrin.h> directly; include <immintrin.h> instead."
#endif // __IMMINTRIN_H

#ifndef __SHA512INTRIN_H
#define __SHA512INTRIN_H

#define __DEFAULT_FN_ATTRS256                                                  \
  __attribute__((__always_inline__, __nodebug__, __target__("sha512"),         \
                 __min_vector_width__(256)))

/// This intrinisc is one of the two SHA512 message scheduling instructions.
///    The intrinsic performs an intermediate calculation for the next four
///    SHA512 message qwords. The calculated results are stored in \a dst.
///
/// \headerfile <immintrin.h>
///
/// \code
/// __m256i _mm256_sha512msg1_epi64(__m256i __A, __m128i __B)
/// \endcode
///
/// This intrinsic corresponds to the \c VSHA512MSG1 instruction.
///
/// \param __A
///    A 256-bit vector of [4 x long long].
/// \param __B
///    A 128-bit vector of [2 x long long].
/// \returns
///    A 256-bit vector of [4 x long long].
///
/// \code{.operation}
/// DEFINE ROR64(qword, n) {
/// 	count := n % 64
/// 	dest := (qword >> count) | (qword << (64 - count))
/// 	RETURN dest
/// }
/// DEFINE SHR64(qword, n) {
/// 	RETURN qword >> n
/// }
/// DEFINE s0(qword):
/// 	RETURN ROR64(qword,1) ^ ROR64(qword, 8) ^ SHR64(qword, 7)
/// }
/// W[4] := __B.qword[0]
/// W[3] := __A.qword[3]
/// W[2] := __A.qword[2]
/// W[1] := __A.qword[1]
/// W[0] := __A.qword[0]
/// dst.qword[3] := W[3] + s0(W[4])
/// dst.qword[2] := W[2] + s0(W[3])
/// dst.qword[1] := W[1] + s0(W[2])
/// dst.qword[0] := W[0] + s0(W[1])
/// dst[MAX:256] := 0
/// \endcode
static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_sha512msg1_epi64(__m256i __A, __m128i __B) {
  return (__m256i)__builtin_ia32_vsha512msg1((__v4du)__A, (__v2du)__B);
}

/// This intrinisc is one of the two SHA512 message scheduling instructions.
///    The intrinsic performs the final calculation for the next four SHA512
///    message qwords. The calculated results are stored in \a dst.
///
/// \headerfile <immintrin.h>
///
/// \code
/// __m256i _mm256_sha512msg2_epi64(__m256i __A, __m256i __B)
/// \endcode
///
/// This intrinsic corresponds to the \c VSHA512MSG2 instruction.
///
/// \param __A
///    A 256-bit vector of [4 x long long].
/// \param __B
///    A 256-bit vector of [4 x long long].
/// \returns
///    A 256-bit vector of [4 x long long].
///
/// \code{.operation}
/// DEFINE ROR64(qword, n) {
/// 	count := n % 64
/// 	dest := (qword >> count) | (qword << (64 - count))
/// 	RETURN dest
/// }
/// DEFINE SHR64(qword, n) {
/// 	RETURN qword >> n
/// }
/// DEFINE s1(qword) {
/// 	RETURN ROR64(qword,19) ^ ROR64(qword, 61) ^ SHR64(qword, 6)
/// }
/// W[14] := __B.qword[2]
/// W[15] := __B.qword[3]
/// W[16] := __A.qword[0] + s1(W[14])
/// W[17] := __A.qword[1] + s1(W[15])
/// W[18] := __A.qword[2] + s1(W[16])
/// W[19] := __A.qword[3] + s1(W[17])
/// dst.qword[3] := W[19]
/// dst.qword[2] := W[18]
/// dst.qword[1] := W[17]
/// dst.qword[0] := W[16]
/// dst[MAX:256] := 0
/// \endcode
static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_sha512msg2_epi64(__m256i __A, __m256i __B) {
  return (__m256i)__builtin_ia32_vsha512msg2((__v4du)__A, (__v4du)__B);
}

/// This intrinisc performs two rounds of SHA512 operation using initial SHA512
///    state (C,D,G,H) from \a __A, an initial SHA512 state (A,B,E,F) from
///    \a __A, and a pre-computed sum of the next two round message qwords and
///    the corresponding round constants from \a __C (only the two lower qwords
///    of the third operand). The updated SHA512 state (A,B,E,F) is written to
///    \a __A, and \a __A can be used as the updated state (C,D,G,H) in later
///    rounds.
///
/// \headerfile <immintrin.h>
///
/// \code
/// __m256i _mm256_sha512rnds2_epi64(__m256i __A, __m256i __B, __m128i __C)
/// \endcode
///
/// This intrinsic corresponds to the \c VSHA512RNDS2 instruction.
///
/// \param __A
///    A 256-bit vector of [4 x long long].
/// \param __B
///    A 256-bit vector of [4 x long long].
/// \param __C
///    A 128-bit vector of [2 x long long].
/// \returns
///    A 256-bit vector of [4 x long long].
///
/// \code{.operation}
/// DEFINE ROR64(qword, n) {
/// 	count := n % 64
/// 	dest := (qword >> count) | (qword << (64 - count))
/// 	RETURN dest
/// }
/// DEFINE SHR64(qword, n) {
/// 	RETURN qword >> n
/// }
/// DEFINE cap_sigma0(qword) {
/// 	RETURN ROR64(qword,28) ^ ROR64(qword, 34) ^ ROR64(qword, 39)
/// }
/// DEFINE cap_sigma1(qword) {
/// 	RETURN ROR64(qword,14) ^ ROR64(qword, 18) ^ ROR64(qword, 41)
/// }
/// DEFINE MAJ(a,b,c) {
/// 	RETURN (a & b) ^ (a & c) ^ (b & c)
/// }
/// DEFINE CH(e,f,g) {
/// 	RETURN (e & f) ^ (g & ~e)
/// }
/// A[0] := __B.qword[3]
/// B[0] := __B.qword[2]
/// C[0] := __C.qword[3]
/// D[0] := __C.qword[2]
/// E[0] := __B.qword[1]
/// F[0] := __B.qword[0]
/// G[0] := __C.qword[1]
/// H[0] := __C.qword[0]
/// WK[0]:= __A.qword[0]
/// WK[1]:= __A.qword[1]
/// FOR i := 0 to 1:
/// 	A[i+1] := CH(E[i], F[i], G[i]) +
/// 	cap_sigma1(E[i]) + WK[i] + H[i] +
/// 	MAJ(A[i], B[i], C[i]) +
/// 	cap_sigma0(A[i])
/// 	B[i+1] := A[i]
/// 	C[i+1] := B[i]
/// 	D[i+1] := C[i]
/// 	E[i+1] := CH(E[i], F[i], G[i]) +
/// 	cap_sigma1(E[i]) + WK[i] + H[i] + D[i]
/// 	F[i+1] := E[i]
/// 	G[i+1] := F[i]
/// 	H[i+1] := G[i]
/// ENDFOR
/// dst.qword[3] := A[2]
/// dst.qword[2] := B[2]
/// dst.qword[1] := E[2]
/// dst.qword[0] := F[2]
/// dst[MAX:256] := 0
/// \endcode
static __inline__ __m256i __DEFAULT_FN_ATTRS256
_mm256_sha512rnds2_epi64(__m256i __A, __m256i __B, __m128i __C) {
  return (__m256i)__builtin_ia32_vsha512rnds2((__v4du)__A, (__v4du)__B,
                                              (__v2du)__C);
}

#undef __DEFAULT_FN_ATTRS256

#endif // __SHA512INTRIN_H
