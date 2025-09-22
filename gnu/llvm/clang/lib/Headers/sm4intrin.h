/*===--------------- sm4intrin.h - SM4 intrinsics -----------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __IMMINTRIN_H
#error "Never use <sm4intrin.h> directly; include <immintrin.h> instead."
#endif // __IMMINTRIN_H

#ifndef __SM4INTRIN_H
#define __SM4INTRIN_H

/// This intrinsic performs four rounds of SM4 key expansion. The intrinsic
///    operates on independent 128-bit lanes. The calculated results are
///    stored in \a dst.
/// \headerfile <immintrin.h>
///
/// \code
/// __m128i _mm_sm4key4_epi32(__m128i __A, __m128i __B)
/// \endcode
///
/// This intrinsic corresponds to the \c VSM4KEY4 instruction.
///
/// \param __A
///    A 128-bit vector of [4 x int].
/// \param __B
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
/// DEFINE SBOX_BYTE(dword, i) {
/// 	RETURN sbox[dword.byte[i]]
/// }
/// DEFINE lower_t(dword) {
/// 	tmp.byte[0] := SBOX_BYTE(dword, 0)
/// 	tmp.byte[1] := SBOX_BYTE(dword, 1)
/// 	tmp.byte[2] := SBOX_BYTE(dword, 2)
/// 	tmp.byte[3] := SBOX_BYTE(dword, 3)
/// 	RETURN tmp
/// }
/// DEFINE L_KEY(dword) {
/// 	RETURN dword ^ ROL32(dword, 13) ^ ROL32(dword, 23)
/// }
/// DEFINE T_KEY(dword) {
/// 	RETURN L_KEY(lower_t(dword))
/// }
/// DEFINE F_KEY(X0, X1, X2, X3, round_key) {
/// 	RETURN X0 ^ T_KEY(X1 ^ X2 ^ X3 ^ round_key)
/// }
/// FOR i:= 0 to 0
/// 	P[0] := __B.xmm[i].dword[0]
/// 	P[1] := __B.xmm[i].dword[1]
/// 	P[2] := __B.xmm[i].dword[2]
/// 	P[3] := __B.xmm[i].dword[3]
/// 	C[0] := F_KEY(P[0], P[1], P[2], P[3], __A.xmm[i].dword[0])
/// 	C[1] := F_KEY(P[1], P[2], P[3], C[0], __A.xmm[i].dword[1])
/// 	C[2] := F_KEY(P[2], P[3], C[0], C[1], __A.xmm[i].dword[2])
/// 	C[3] := F_KEY(P[3], C[0], C[1], C[2], __A.xmm[i].dword[3])
/// 	DEST.xmm[i].dword[0] := C[0]
/// 	DEST.xmm[i].dword[1] := C[1]
/// 	DEST.xmm[i].dword[2] := C[2]
/// 	DEST.xmm[i].dword[3] := C[3]
/// ENDFOR
/// DEST[MAX:128] := 0
/// \endcode
#define _mm_sm4key4_epi32(A, B)                                                \
  (__m128i) __builtin_ia32_vsm4key4128((__v4su)A, (__v4su)B)

/// This intrinsic performs four rounds of SM4 key expansion. The intrinsic
///    operates on independent 128-bit lanes. The calculated results are
///    stored in \a dst.
/// \headerfile <immintrin.h>
///
/// \code
/// __m256i _mm256_sm4key4_epi32(__m256i __A, __m256i __B)
/// \endcode
///
/// This intrinsic corresponds to the \c VSM4KEY4 instruction.
///
/// \param __A
///    A 256-bit vector of [8 x int].
/// \param __B
///    A 256-bit vector of [8 x int].
/// \returns
///    A 256-bit vector of [8 x int].
///
/// \code{.operation}
/// DEFINE ROL32(dword, n) {
/// 	count := n % 32
/// 	dest := (dword << count) | (dword >> (32-count))
/// 	RETURN dest
/// }
/// DEFINE SBOX_BYTE(dword, i) {
/// 	RETURN sbox[dword.byte[i]]
/// }
/// DEFINE lower_t(dword) {
/// 	tmp.byte[0] := SBOX_BYTE(dword, 0)
/// 	tmp.byte[1] := SBOX_BYTE(dword, 1)
/// 	tmp.byte[2] := SBOX_BYTE(dword, 2)
/// 	tmp.byte[3] := SBOX_BYTE(dword, 3)
/// 	RETURN tmp
/// }
/// DEFINE L_KEY(dword) {
/// 	RETURN dword ^ ROL32(dword, 13) ^ ROL32(dword, 23)
/// }
/// DEFINE T_KEY(dword) {
/// 	RETURN L_KEY(lower_t(dword))
/// }
/// DEFINE F_KEY(X0, X1, X2, X3, round_key) {
/// 	RETURN X0 ^ T_KEY(X1 ^ X2 ^ X3 ^ round_key)
/// }
/// FOR i:= 0 to 1
/// 	P[0] := __B.xmm[i].dword[0]
/// 	P[1] := __B.xmm[i].dword[1]
/// 	P[2] := __B.xmm[i].dword[2]
/// 	P[3] := __B.xmm[i].dword[3]
/// 	C[0] := F_KEY(P[0], P[1], P[2], P[3], __A.xmm[i].dword[0])
/// 	C[1] := F_KEY(P[1], P[2], P[3], C[0], __A.xmm[i].dword[1])
/// 	C[2] := F_KEY(P[2], P[3], C[0], C[1], __A.xmm[i].dword[2])
/// 	C[3] := F_KEY(P[3], C[0], C[1], C[2], __A.xmm[i].dword[3])
/// 	DEST.xmm[i].dword[0] := C[0]
/// 	DEST.xmm[i].dword[1] := C[1]
/// 	DEST.xmm[i].dword[2] := C[2]
/// 	DEST.xmm[i].dword[3] := C[3]
/// ENDFOR
/// DEST[MAX:256] := 0
/// \endcode
#define _mm256_sm4key4_epi32(A, B)                                             \
  (__m256i) __builtin_ia32_vsm4key4256((__v8su)A, (__v8su)B)

/// This intrinisc performs four rounds of SM4 encryption. The intrinisc
///    operates on independent 128-bit lanes. The calculated results are
///    stored in \a dst.
/// \headerfile <immintrin.h>
///
/// \code
/// __m128i _mm_sm4rnds4_epi32(__m128i __A, __m128i __B)
/// \endcode
///
/// This intrinsic corresponds to the \c VSM4RNDS4 instruction.
///
/// \param __A
///    A 128-bit vector of [4 x int].
/// \param __B
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
/// DEFINE lower_t(dword) {
/// 	tmp.byte[0] := SBOX_BYTE(dword, 0)
/// 	tmp.byte[1] := SBOX_BYTE(dword, 1)
/// 	tmp.byte[2] := SBOX_BYTE(dword, 2)
/// 	tmp.byte[3] := SBOX_BYTE(dword, 3)
/// 	RETURN tmp
/// }
/// DEFINE L_RND(dword) {
/// 	tmp := dword
/// 	tmp := tmp ^ ROL32(dword, 2)
/// 	tmp := tmp ^ ROL32(dword, 10)
/// 	tmp := tmp ^ ROL32(dword, 18)
/// 	tmp := tmp ^ ROL32(dword, 24)
///   RETURN tmp
/// }
/// DEFINE T_RND(dword) {
/// 	RETURN L_RND(lower_t(dword))
/// }
/// DEFINE F_RND(X0, X1, X2, X3, round_key) {
/// 	RETURN X0 ^ T_RND(X1 ^ X2 ^ X3 ^ round_key)
/// }
/// FOR i:= 0 to 0
/// 	P[0] := __B.xmm[i].dword[0]
/// 	P[1] := __B.xmm[i].dword[1]
/// 	P[2] := __B.xmm[i].dword[2]
/// 	P[3] := __B.xmm[i].dword[3]
/// 	C[0] := F_RND(P[0], P[1], P[2], P[3], __A.xmm[i].dword[0])
/// 	C[1] := F_RND(P[1], P[2], P[3], C[0], __A.xmm[i].dword[1])
/// 	C[2] := F_RND(P[2], P[3], C[0], C[1], __A.xmm[i].dword[2])
/// 	C[3] := F_RND(P[3], C[0], C[1], C[2], __A.xmm[i].dword[3])
/// 	DEST.xmm[i].dword[0] := C[0]
/// 	DEST.xmm[i].dword[1] := C[1]
/// 	DEST.xmm[i].dword[2] := C[2]
/// 	DEST.xmm[i].dword[3] := C[3]
/// ENDFOR
/// DEST[MAX:128] := 0
/// \endcode
#define _mm_sm4rnds4_epi32(A, B)                                               \
  (__m128i) __builtin_ia32_vsm4rnds4128((__v4su)A, (__v4su)B)

/// This intrinisc performs four rounds of SM4 encryption. The intrinisc
///    operates on independent 128-bit lanes. The calculated results are
///    stored in \a dst.
/// \headerfile <immintrin.h>
///
/// \code
/// __m256i _mm256_sm4rnds4_epi32(__m256i __A, __m256i __B)
/// \endcode
///
/// This intrinsic corresponds to the \c VSM4RNDS4 instruction.
///
/// \param __A
///    A 256-bit vector of [8 x int].
/// \param __B
///    A 256-bit vector of [8 x int].
/// \returns
///    A 256-bit vector of [8 x int].
///
/// \code{.operation}
/// DEFINE ROL32(dword, n) {
/// 	count := n % 32
/// 	dest := (dword << count) | (dword >> (32-count))
/// 	RETURN dest
/// }
/// DEFINE lower_t(dword) {
/// 	tmp.byte[0] := SBOX_BYTE(dword, 0)
/// 	tmp.byte[1] := SBOX_BYTE(dword, 1)
/// 	tmp.byte[2] := SBOX_BYTE(dword, 2)
/// 	tmp.byte[3] := SBOX_BYTE(dword, 3)
/// 	RETURN tmp
/// }
/// DEFINE L_RND(dword) {
/// 	tmp := dword
/// 	tmp := tmp ^ ROL32(dword, 2)
/// 	tmp := tmp ^ ROL32(dword, 10)
/// 	tmp := tmp ^ ROL32(dword, 18)
/// 	tmp := tmp ^ ROL32(dword, 24)
///   RETURN tmp
/// }
/// DEFINE T_RND(dword) {
/// 	RETURN L_RND(lower_t(dword))
/// }
/// DEFINE F_RND(X0, X1, X2, X3, round_key) {
/// 	RETURN X0 ^ T_RND(X1 ^ X2 ^ X3 ^ round_key)
/// }
/// FOR i:= 0 to 0
/// 	P[0] := __B.xmm[i].dword[0]
/// 	P[1] := __B.xmm[i].dword[1]
/// 	P[2] := __B.xmm[i].dword[2]
/// 	P[3] := __B.xmm[i].dword[3]
/// 	C[0] := F_RND(P[0], P[1], P[2], P[3], __A.xmm[i].dword[0])
/// 	C[1] := F_RND(P[1], P[2], P[3], C[0], __A.xmm[i].dword[1])
/// 	C[2] := F_RND(P[2], P[3], C[0], C[1], __A.xmm[i].dword[2])
/// 	C[3] := F_RND(P[3], C[0], C[1], C[2], __A.xmm[i].dword[3])
/// 	DEST.xmm[i].dword[0] := C[0]
/// 	DEST.xmm[i].dword[1] := C[1]
/// 	DEST.xmm[i].dword[2] := C[2]
/// 	DEST.xmm[i].dword[3] := C[3]
/// ENDFOR
/// DEST[MAX:256] := 0
/// \endcode
#define _mm256_sm4rnds4_epi32(A, B)                                            \
  (__m256i) __builtin_ia32_vsm4rnds4256((__v8su)A, (__v8su)B)

#endif // __SM4INTRIN_H
