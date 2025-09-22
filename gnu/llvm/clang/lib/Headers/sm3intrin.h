/*===-------------------- sm3intrin.h - SM3 intrinsics ---------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __IMMINTRIN_H
#error "Never use <sm3intrin.h> directly; include <immintrin.h> instead."
#endif // __IMMINTRIN_H

#ifndef __SM3INTRIN_H
#define __SM3INTRIN_H

#define __DEFAULT_FN_ATTRS128                                                  \
  __attribute__((__always_inline__, __nodebug__, __target__("sm3"),            \
                 __min_vector_width__(128)))

/// This intrinisc is one of the two SM3 message scheduling intrinsics. The
///    intrinsic performs an initial calculation for the next four SM3 message
///    words. The calculated results are stored in \a dst.
///
/// \headerfile <immintrin.h>
///
/// \code
/// __m128i _mm_sm3msg1_epi32(__m128i __A, __m128i __B, __m128i __C)
/// \endcode
///
/// This intrinsic corresponds to the \c VSM3MSG1 instruction.
///
/// \param __A
///    A 128-bit vector of [4 x int].
/// \param __B
///    A 128-bit vector of [4 x int].
/// \param __C
///    A 128-bit vector of [4 x int].
/// \returns
///    A 128-bit vector of [4 x int].
///
/// \code{.operation}
/// DEFINE ROL32(dword, n) {
/// 	count := n % 32
/// 	dest := (dword << count) | (dword >> (32 - count))
/// 	RETURN dest
/// }
/// DEFINE P1(x) {
/// 	RETURN x ^ ROL32(x, 15) ^ ROL32(x, 23)
/// }
/// W[0] := __C.dword[0]
/// W[1] := __C.dword[1]
/// W[2] := __C.dword[2]
/// W[3] := __C.dword[3]
/// W[7] := __A.dword[0]
/// W[8] := __A.dword[1]
/// W[9] := __A.dword[2]
/// W[10] := __A.dword[3]
/// W[13] := __B.dword[0]
/// W[14] := __B.dword[1]
/// W[15] := __B.dword[2]
/// TMP0 := W[7] ^ W[0] ^ ROL32(W[13], 15)
/// TMP1 := W[8] ^ W[1] ^ ROL32(W[14], 15)
/// TMP2 := W[9] ^ W[2] ^ ROL32(W[15], 15)
/// TMP3 := W[10] ^ W[3]
/// dst.dword[0] := P1(TMP0)
/// dst.dword[1] := P1(TMP1)
/// dst.dword[2] := P1(TMP2)
/// dst.dword[3] := P1(TMP3)
/// dst[MAX:128] := 0
/// \endcode
static __inline__ __m128i __DEFAULT_FN_ATTRS128 _mm_sm3msg1_epi32(__m128i __A,
                                                                  __m128i __B,
                                                                  __m128i __C) {
  return (__m128i)__builtin_ia32_vsm3msg1((__v4su)__A, (__v4su)__B,
                                          (__v4su)__C);
}

/// This intrinisc is one of the two SM3 message scheduling intrinsics. The
///    intrinsic performs the final calculation for the next four SM3 message
///    words. The calculated results are stored in \a dst.
///
/// \headerfile <immintrin.h>
///
/// \code
/// __m128i _mm_sm3msg2_epi32(__m128i __A, __m128i __B, __m128i __C)
/// \endcode
///
/// This intrinsic corresponds to the \c VSM3MSG2 instruction.
///
/// \param __A
///    A 128-bit vector of [4 x int].
/// \param __B
///    A 128-bit vector of [4 x int].
/// \param __C
///    A 128-bit vector of [4 x int].
/// \returns
///    A 128-bit vector of [4 x int].
///
/// \code{.operation}
/// DEFINE ROL32(dword, n) {
/// 	count := n % 32
/// 	dest := (dword << count) | (dword >> (32-count))
/// 	RETURN dest
/// }
/// WTMP[0] := __A.dword[0]
/// WTMP[1] := __A.dword[1]
/// WTMP[2] := __A.dword[2]
/// WTMP[3] := __A.dword[3]
/// W[3] := __B.dword[0]
/// W[4] := __B.dword[1]
/// W[5] := __B.dword[2]
/// W[6] := __B.dword[3]
/// W[10] := __C.dword[0]
/// W[11] := __C.dword[1]
/// W[12] := __C.dword[2]
/// W[13] := __C.dword[3]
/// W[16] := ROL32(W[3], 7) ^ W[10] ^ WTMP[0]
/// W[17] := ROL32(W[4], 7) ^ W[11] ^ WTMP[1]
/// W[18] := ROL32(W[5], 7) ^ W[12] ^ WTMP[2]
/// W[19] := ROL32(W[6], 7) ^ W[13] ^ WTMP[3]
/// W[19] := W[19] ^ ROL32(W[16], 6) ^ ROL32(W[16], 15) ^ ROL32(W[16], 30)
/// dst.dword[0] := W[16]
/// dst.dword[1] := W[17]
/// dst.dword[2] := W[18]
/// dst.dword[3] := W[19]
/// dst[MAX:128] := 0
/// \endcode
static __inline__ __m128i __DEFAULT_FN_ATTRS128 _mm_sm3msg2_epi32(__m128i __A,
                                                                  __m128i __B,
                                                                  __m128i __C) {
  return (__m128i)__builtin_ia32_vsm3msg2((__v4su)__A, (__v4su)__B,
                                          (__v4su)__C);
}

/// This intrinsic performs two rounds of SM3 operation using initial SM3 state
///    (C, D, G, H) from \a __A, an initial SM3 states (A, B, E, F)
///    from \a __B and a pre-computed words from the \a __C. \a __A with
///    initial SM3 state of (C, D, G, H) assumes input of non-rotated left
///    variables from previous state. The updated SM3 state (A, B, E, F) is
///    written to \a __A. The \a imm8 should contain the even round number
///    for the first of the two rounds computed by this instruction. The
///    computation masks the \a imm8 value by AND’ing it with 0x3E so that only
///    even round numbers from 0 through 62 are used for this operation. The
///    calculated results are stored in \a dst.
///
/// \headerfile <immintrin.h>
///
/// \code
/// __m128i _mm_sm3rnds2_epi32(__m128i __A, __m128i __B, __m128i __C, const int
/// imm8) \endcode
///
/// This intrinsic corresponds to the \c VSM3RNDS2 instruction.
///
/// \param __A
///    A 128-bit vector of [4 x int].
/// \param __B
///    A 128-bit vector of [4 x int].
/// \param __C
///    A 128-bit vector of [4 x int].
/// \param imm8
///    A 8-bit constant integer.
/// \returns
///    A 128-bit vector of [4 x int].
///
/// \code{.operation}
/// DEFINE ROL32(dword, n) {
/// 	count := n % 32
/// 	dest := (dword << count) | (dword >> (32-count))
/// 	RETURN dest
/// }
/// DEFINE P0(dword) {
/// 	RETURN dword ^ ROL32(dword, 9) ^ ROL32(dword, 17)
/// }
/// DEFINE FF(x,y,z, round){
/// 	IF round < 16
/// 		RETURN (x ^ y ^ z)
/// 	ELSE
/// 		RETURN (x & y) | (x & z) | (y & z)
/// 	FI
/// }
/// DEFINE GG(x, y, z, round){
///   IF round < 16
///   	RETURN (x ^ y ^ z)
///   ELSE
///   	RETURN (x & y) | (~x & z)
///   FI
/// }
/// A[0] := __B.dword[3]
/// B[0] := __B.dword[2]
/// C[0] := __A.dword[3]
/// D[0] := __A.dword[2]
/// E[0] := __B.dword[1]
/// F[0] := __B.dword[0]
/// G[0] := __A.dword[1]
/// H[0] := __A.dword[0]
/// W[0] := __C.dword[0]
/// W[1] := __C.dword[1]
/// W[4] := __C.dword[2]
/// W[5] := __C.dword[3]
/// C[0] := ROL32(C[0], 9)
/// D[0] := ROL32(D[0], 9)
/// G[0] := ROL32(G[0], 19)
/// H[0] := ROL32(H[0], 19)
/// ROUND := __D & 0x3E
/// IF ROUND < 16
/// 	CONST := 0x79CC4519
/// ELSE
/// 	CONST := 0x7A879D8A
/// FI
/// CONST := ROL32(CONST,ROUND)
/// FOR i:= 0 to 1
/// 	S1 := ROL32((ROL32(A[i], 12) + E[i] + CONST), 7)
/// 	S2 := S1 ^ ROL32(A[i], 12)
/// 	T1 := FF(A[i], B[i], C[i], ROUND) + D[i] + S2 + (W[i] ^ W[i+4])
/// 	T2 := GG(E[i], F[i], G[i], ROUND) + H[i] + S1 + W[i]
/// 	D[i+1] := C[i]
/// 	C[i+1] := ROL32(B[i],9)
/// 	B[i+1] := A[i]
/// 	A[i+1] := T1
/// 	H[i+1] := G[i]
/// 	G[i+1] := ROL32(F[i], 19)
/// 	F[i+1] := E[i]
/// 	E[i+1] := P0(T2)
/// 	CONST := ROL32(CONST, 1)
/// ENDFOR
/// dst.dword[3] := A[2]
/// dst.dword[2] := B[2]
/// dst.dword[1] := E[2]
/// dst.dword[0] := F[2]
/// dst[MAX:128] := 0
/// \endcode
#define _mm_sm3rnds2_epi32(A, B, C, D)                                         \
  (__m128i) __builtin_ia32_vsm3rnds2((__v4su)A, (__v4su)B, (__v4su)C, (int)D)

#undef __DEFAULT_FN_ATTRS128

#endif // __SM3INTRIN_H
