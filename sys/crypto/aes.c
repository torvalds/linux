/*	$OpenBSD: aes.c,v 1.2 2020/07/22 13:54:30 tobhe Exp $	*/
/*
 * Copyright (c) 2016 Thomas Pornin <pornin@bolet.org>
 *
 * Modified for OpenBSD by Thomas Pornin and Mike Belopuhov.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/stdint.h>

#include "aes.h"

static inline void
enc32le(void *dst, uint32_t x)
{
	unsigned char *buf = dst;

	buf[0] = (unsigned char)x;
	buf[1] = (unsigned char)(x >> 8);
	buf[2] = (unsigned char)(x >> 16);
	buf[3] = (unsigned char)(x >> 24);
}

static inline uint32_t
dec32le(const void *src)
{
	const unsigned char *buf = src;

	return (uint32_t)buf[0]
		| ((uint32_t)buf[1] << 8)
		| ((uint32_t)buf[2] << 16)
		| ((uint32_t)buf[3] << 24);
}

/*
 * This constant-time implementation is "bitsliced": the 128-bit state is
 * split over eight 32-bit words q* in the following way:
 *
 * -- Input block consists in 16 bytes:
 *    a00 a10 a20 a30 a01 a11 a21 a31 a02 a12 a22 a32 a03 a13 a23 a33
 * In the terminology of FIPS 197, this is a 4x4 matrix which is read
 * column by column.
 *
 * -- Each byte is split into eight bits which are distributed over the
 * eight words, at the same rank. Thus, for a byte x at rank k, bit 0
 * (least significant) of x will be at rank k in q0 (if that bit is b,
 * then it contributes "b << k" to the value of q0), bit 1 of x will be
 * at rank k in q1, and so on.
 *
 * -- Ranks given to bits are in "row order" and are either all even, or
 * all odd. Two independent AES states are thus interleaved, one using
 * the even ranks, the other the odd ranks. Row order means:
 *    a00 a01 a02 a03 a10 a11 a12 a13 a20 a21 a22 a23 a30 a31 a32 a33
 *
 * Converting input bytes from two AES blocks to bitslice representation
 * is done in the following way:
 * -- Decode first block into the four words q0 q2 q4 q6, in that order,
 * using little-endian convention.
 * -- Decode second block into the four words q1 q3 q5 q7, in that order,
 * using little-endian convention.
 * -- Call aes_ct_ortho().
 *
 * Converting back to bytes is done by using the reverse operations. Note
 * that aes_ct_ortho() is its own inverse.
 */

/*
 * The AES S-box, as a bitsliced constant-time version. The input array
 * consists in eight 32-bit words; 32 S-box instances are computed in
 * parallel. Bits 0 to 7 of each S-box input (bit 0 is least significant)
 * are spread over the words 0 to 7, at the same rank.
 */
static void
aes_ct_bitslice_Sbox(uint32_t *q)
{
	/*
	 * This S-box implementation is a straightforward translation of
	 * the circuit described by Boyar and Peralta in "A new
	 * combinational logic minimization technique with applications
	 * to cryptology" (https://eprint.iacr.org/2009/191.pdf).
	 *
	 * Note that variables x* (input) and s* (output) are numbered
	 * in "reverse" order (x0 is the high bit, x7 is the low bit).
	 */

	uint32_t x0, x1, x2, x3, x4, x5, x6, x7;
	uint32_t y1, y2, y3, y4, y5, y6, y7, y8, y9;
	uint32_t y10, y11, y12, y13, y14, y15, y16, y17, y18, y19;
	uint32_t y20, y21;
	uint32_t z0, z1, z2, z3, z4, z5, z6, z7, z8, z9;
	uint32_t z10, z11, z12, z13, z14, z15, z16, z17;
	uint32_t t0, t1, t2, t3, t4, t5, t6, t7, t8, t9;
	uint32_t t10, t11, t12, t13, t14, t15, t16, t17, t18, t19;
	uint32_t t20, t21, t22, t23, t24, t25, t26, t27, t28, t29;
	uint32_t t30, t31, t32, t33, t34, t35, t36, t37, t38, t39;
	uint32_t t40, t41, t42, t43, t44, t45, t46, t47, t48, t49;
	uint32_t t50, t51, t52, t53, t54, t55, t56, t57, t58, t59;
	uint32_t t60, t61, t62, t63, t64, t65, t66, t67;
	uint32_t s0, s1, s2, s3, s4, s5, s6, s7;

	x0 = q[7];
	x1 = q[6];
	x2 = q[5];
	x3 = q[4];
	x4 = q[3];
	x5 = q[2];
	x6 = q[1];
	x7 = q[0];

	/*
	 * Top linear transformation.
	 */
	y14 = x3 ^ x5;
	y13 = x0 ^ x6;
	y9 = x0 ^ x3;
	y8 = x0 ^ x5;
	t0 = x1 ^ x2;
	y1 = t0 ^ x7;
	y4 = y1 ^ x3;
	y12 = y13 ^ y14;
	y2 = y1 ^ x0;
	y5 = y1 ^ x6;
	y3 = y5 ^ y8;
	t1 = x4 ^ y12;
	y15 = t1 ^ x5;
	y20 = t1 ^ x1;
	y6 = y15 ^ x7;
	y10 = y15 ^ t0;
	y11 = y20 ^ y9;
	y7 = x7 ^ y11;
	y17 = y10 ^ y11;
	y19 = y10 ^ y8;
	y16 = t0 ^ y11;
	y21 = y13 ^ y16;
	y18 = x0 ^ y16;

	/*
	 * Non-linear section.
	 */
	t2 = y12 & y15;
	t3 = y3 & y6;
	t4 = t3 ^ t2;
	t5 = y4 & x7;
	t6 = t5 ^ t2;
	t7 = y13 & y16;
	t8 = y5 & y1;
	t9 = t8 ^ t7;
	t10 = y2 & y7;
	t11 = t10 ^ t7;
	t12 = y9 & y11;
	t13 = y14 & y17;
	t14 = t13 ^ t12;
	t15 = y8 & y10;
	t16 = t15 ^ t12;
	t17 = t4 ^ t14;
	t18 = t6 ^ t16;
	t19 = t9 ^ t14;
	t20 = t11 ^ t16;
	t21 = t17 ^ y20;
	t22 = t18 ^ y19;
	t23 = t19 ^ y21;
	t24 = t20 ^ y18;

	t25 = t21 ^ t22;
	t26 = t21 & t23;
	t27 = t24 ^ t26;
	t28 = t25 & t27;
	t29 = t28 ^ t22;
	t30 = t23 ^ t24;
	t31 = t22 ^ t26;
	t32 = t31 & t30;
	t33 = t32 ^ t24;
	t34 = t23 ^ t33;
	t35 = t27 ^ t33;
	t36 = t24 & t35;
	t37 = t36 ^ t34;
	t38 = t27 ^ t36;
	t39 = t29 & t38;
	t40 = t25 ^ t39;

	t41 = t40 ^ t37;
	t42 = t29 ^ t33;
	t43 = t29 ^ t40;
	t44 = t33 ^ t37;
	t45 = t42 ^ t41;
	z0 = t44 & y15;
	z1 = t37 & y6;
	z2 = t33 & x7;
	z3 = t43 & y16;
	z4 = t40 & y1;
	z5 = t29 & y7;
	z6 = t42 & y11;
	z7 = t45 & y17;
	z8 = t41 & y10;
	z9 = t44 & y12;
	z10 = t37 & y3;
	z11 = t33 & y4;
	z12 = t43 & y13;
	z13 = t40 & y5;
	z14 = t29 & y2;
	z15 = t42 & y9;
	z16 = t45 & y14;
	z17 = t41 & y8;

	/*
	 * Bottom linear transformation.
	 */
	t46 = z15 ^ z16;
	t47 = z10 ^ z11;
	t48 = z5 ^ z13;
	t49 = z9 ^ z10;
	t50 = z2 ^ z12;
	t51 = z2 ^ z5;
	t52 = z7 ^ z8;
	t53 = z0 ^ z3;
	t54 = z6 ^ z7;
	t55 = z16 ^ z17;
	t56 = z12 ^ t48;
	t57 = t50 ^ t53;
	t58 = z4 ^ t46;
	t59 = z3 ^ t54;
	t60 = t46 ^ t57;
	t61 = z14 ^ t57;
	t62 = t52 ^ t58;
	t63 = t49 ^ t58;
	t64 = z4 ^ t59;
	t65 = t61 ^ t62;
	t66 = z1 ^ t63;
	s0 = t59 ^ t63;
	s6 = t56 ^ ~t62;
	s7 = t48 ^ ~t60;
	t67 = t64 ^ t65;
	s3 = t53 ^ t66;
	s4 = t51 ^ t66;
	s5 = t47 ^ t65;
	s1 = t64 ^ ~s3;
	s2 = t55 ^ ~t67;

	q[7] = s0;
	q[6] = s1;
	q[5] = s2;
	q[4] = s3;
	q[3] = s4;
	q[2] = s5;
	q[1] = s6;
	q[0] = s7;
}

/*
 * Perform bytewise orthogonalization of eight 32-bit words. Bytes
 * of q0..q7 are spread over all words: for a byte x that occurs
 * at rank i in q[j] (byte x uses bits 8*i to 8*i+7 in q[j]), the bit
 * of rank k in x (0 <= k <= 7) goes to q[k] at rank 8*i+j.
 *
 * This operation is an involution.
 */
static void
aes_ct_ortho(uint32_t *q)
{
#define SWAPN(cl, ch, s, x, y)   do { \
		uint32_t a, b; \
		a = (x); \
		b = (y); \
		(x) = (a & (uint32_t)cl) | ((b & (uint32_t)cl) << (s)); \
		(y) = ((a & (uint32_t)ch) >> (s)) | (b & (uint32_t)ch); \
	} while (0)

#define SWAP2(x, y)   SWAPN(0x55555555, 0xAAAAAAAA, 1, x, y)
#define SWAP4(x, y)   SWAPN(0x33333333, 0xCCCCCCCC, 2, x, y)
#define SWAP8(x, y)   SWAPN(0x0F0F0F0F, 0xF0F0F0F0, 4, x, y)

	SWAP2(q[0], q[1]);
	SWAP2(q[2], q[3]);
	SWAP2(q[4], q[5]);
	SWAP2(q[6], q[7]);

	SWAP4(q[0], q[2]);
	SWAP4(q[1], q[3]);
	SWAP4(q[4], q[6]);
	SWAP4(q[5], q[7]);

	SWAP8(q[0], q[4]);
	SWAP8(q[1], q[5]);
	SWAP8(q[2], q[6]);
	SWAP8(q[3], q[7]);
}

static inline uint32_t
sub_word(uint32_t x)
{
	uint32_t q[8];
	int i;

	for (i = 0; i < 8; i ++) {
		q[i] = x;
	}
	aes_ct_ortho(q);
	aes_ct_bitslice_Sbox(q);
	aes_ct_ortho(q);
	return q[0];
}

static const unsigned char Rcon[] = {
	0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1B, 0x36
};

/*
 * Base key schedule code. The function sub_word() must be defined
 * below. Subkeys are produced in little-endian convention (but not
 * bitsliced). Key length is expressed in bytes.
 */
static unsigned
aes_keysched_base(uint32_t *skey, const void *key, size_t key_len)
{
	unsigned num_rounds;
	int i, j, k, nk, nkf;
	uint32_t tmp;

	switch (key_len) {
	case 16:
		num_rounds = 10;
		break;
	case 24:
		num_rounds = 12;
		break;
	case 32:
		num_rounds = 14;
		break;
	default:
		return 0;
	}
	nk = (int)(key_len >> 2);
	nkf = (int)((num_rounds + 1) << 2);
	for (i = 0; i < nk; i ++) {
		tmp = dec32le((const unsigned char *)key + (i << 2));
		skey[i] = tmp;
	}
	tmp = skey[(key_len >> 2) - 1];
	for (i = nk, j = 0, k = 0; i < nkf; i ++) {
		if (j == 0) {
			tmp = (tmp << 24) | (tmp >> 8);
			tmp = sub_word(tmp) ^ Rcon[k];
		} else if (nk > 6 && j == 4) {
			tmp = sub_word(tmp);
		}
		tmp ^= skey[i - nk];
		skey[i] = tmp;
		if (++ j == nk) {
			j = 0;
			k ++;
		}
	}
	return num_rounds;
}

/*
 * AES key schedule, constant-time version. skey[] is filled with n+1
 * 128-bit subkeys, where n is the number of rounds (10 to 14, depending
 * on key size). The number of rounds is returned. If the key size is
 * invalid (not 16, 24 or 32), then 0 is returned.
 */
unsigned
aes_ct_keysched(uint32_t *comp_skey, const void *key, size_t key_len)
{
	uint32_t skey[60];
	unsigned u, num_rounds;

	num_rounds = aes_keysched_base(skey, key, key_len);
	for (u = 0; u <= num_rounds; u ++) {
		uint32_t q[8];

		q[0] = q[1] = skey[(u << 2) + 0];
		q[2] = q[3] = skey[(u << 2) + 1];
		q[4] = q[5] = skey[(u << 2) + 2];
		q[6] = q[7] = skey[(u << 2) + 3];
		aes_ct_ortho(q);
		comp_skey[(u << 2) + 0] =
			(q[0] & 0x55555555) | (q[1] & 0xAAAAAAAA);
		comp_skey[(u << 2) + 1] =
			(q[2] & 0x55555555) | (q[3] & 0xAAAAAAAA);
		comp_skey[(u << 2) + 2] =
			(q[4] & 0x55555555) | (q[5] & 0xAAAAAAAA);
		comp_skey[(u << 2) + 3] =
			(q[6] & 0x55555555) | (q[7] & 0xAAAAAAAA);
	}
	return num_rounds;
}

/*
 * Expand AES subkeys as produced by aes_ct_keysched(), into
 * a larger array suitable for aes_ct_bitslice_encrypt() and
 * aes_ct_bitslice_decrypt().
 */
void
aes_ct_skey_expand(uint32_t *skey,
	unsigned num_rounds, const uint32_t *comp_skey)
{
	unsigned u, v, n;

	n = (num_rounds + 1) << 2;
	for (u = 0, v = 0; u < n; u ++, v += 2) {
		uint32_t x, y;

		x = y = comp_skey[u];
		x &= 0x55555555;
		skey[v + 0] = x | (x << 1);
		y &= 0xAAAAAAAA;
		skey[v + 1] = y | (y >> 1);
	}
}

static inline void
add_round_key(uint32_t *q, const uint32_t *sk)
{
	q[0] ^= sk[0];
	q[1] ^= sk[1];
	q[2] ^= sk[2];
	q[3] ^= sk[3];
	q[4] ^= sk[4];
	q[5] ^= sk[5];
	q[6] ^= sk[6];
	q[7] ^= sk[7];
}

static inline void
shift_rows(uint32_t *q)
{
	int i;

	for (i = 0; i < 8; i ++) {
		uint32_t x;

		x = q[i];
		q[i] = (x & 0x000000FF)
			| ((x & 0x0000FC00) >> 2) | ((x & 0x00000300) << 6)
			| ((x & 0x00F00000) >> 4) | ((x & 0x000F0000) << 4)
			| ((x & 0xC0000000) >> 6) | ((x & 0x3F000000) << 2);
	}
}

static inline uint32_t
rotr16(uint32_t x)
{
	return (x << 16) | (x >> 16);
}

static inline void
mix_columns(uint32_t *q)
{
	uint32_t q0, q1, q2, q3, q4, q5, q6, q7;
	uint32_t r0, r1, r2, r3, r4, r5, r6, r7;

	q0 = q[0];
	q1 = q[1];
	q2 = q[2];
	q3 = q[3];
	q4 = q[4];
	q5 = q[5];
	q6 = q[6];
	q7 = q[7];
	r0 = (q0 >> 8) | (q0 << 24);
	r1 = (q1 >> 8) | (q1 << 24);
	r2 = (q2 >> 8) | (q2 << 24);
	r3 = (q3 >> 8) | (q3 << 24);
	r4 = (q4 >> 8) | (q4 << 24);
	r5 = (q5 >> 8) | (q5 << 24);
	r6 = (q6 >> 8) | (q6 << 24);
	r7 = (q7 >> 8) | (q7 << 24);

	q[0] = q7 ^ r7 ^ r0 ^ rotr16(q0 ^ r0);
	q[1] = q0 ^ r0 ^ q7 ^ r7 ^ r1 ^ rotr16(q1 ^ r1);
	q[2] = q1 ^ r1 ^ r2 ^ rotr16(q2 ^ r2);
	q[3] = q2 ^ r2 ^ q7 ^ r7 ^ r3 ^ rotr16(q3 ^ r3);
	q[4] = q3 ^ r3 ^ q7 ^ r7 ^ r4 ^ rotr16(q4 ^ r4);
	q[5] = q4 ^ r4 ^ r5 ^ rotr16(q5 ^ r5);
	q[6] = q5 ^ r5 ^ r6 ^ rotr16(q6 ^ r6);
	q[7] = q6 ^ r6 ^ r7 ^ rotr16(q7 ^ r7);
}

/*
 * Compute AES encryption on bitsliced data. Since input is stored on
 * eight 32-bit words, two block encryptions are actually performed
 * in parallel.
 */
void
aes_ct_bitslice_encrypt(unsigned num_rounds,
	const uint32_t *skey, uint32_t *q)
{
	unsigned u;

	add_round_key(q, skey);
	for (u = 1; u < num_rounds; u ++) {
		aes_ct_bitslice_Sbox(q);
		shift_rows(q);
		mix_columns(q);
		add_round_key(q, skey + (u << 3));
	}
	aes_ct_bitslice_Sbox(q);
	shift_rows(q);
	add_round_key(q, skey + (num_rounds << 3));
}

/*
 * Like aes_ct_bitslice_Sbox(), but for the inverse S-box.
 */
void
aes_ct_bitslice_invSbox(uint32_t *q)
{
	/*
	 * AES S-box is:
	 *   S(x) = A(I(x)) ^ 0x63
	 * where I() is inversion in GF(256), and A() is a linear
	 * transform (0 is formally defined to be its own inverse).
	 * Since inversion is an involution, the inverse S-box can be
	 * computed from the S-box as:
	 *   iS(x) = B(S(B(x ^ 0x63)) ^ 0x63)
	 * where B() is the inverse of A(). Indeed, for any y in GF(256):
	 *   iS(S(y)) = B(A(I(B(A(I(y)) ^ 0x63 ^ 0x63))) ^ 0x63 ^ 0x63) = y
	 *
	 * Note: we reuse the implementation of the forward S-box,
	 * instead of duplicating it here, so that total code size is
	 * lower. By merging the B() transforms into the S-box circuit
	 * we could make faster CBC decryption, but CBC decryption is
	 * already quite faster than CBC encryption because we can
	 * process two blocks in parallel.
	 */
	uint32_t q0, q1, q2, q3, q4, q5, q6, q7;

	q0 = ~q[0];
	q1 = ~q[1];
	q2 = q[2];
	q3 = q[3];
	q4 = q[4];
	q5 = ~q[5];
	q6 = ~q[6];
	q7 = q[7];
	q[7] = q1 ^ q4 ^ q6;
	q[6] = q0 ^ q3 ^ q5;
	q[5] = q7 ^ q2 ^ q4;
	q[4] = q6 ^ q1 ^ q3;
	q[3] = q5 ^ q0 ^ q2;
	q[2] = q4 ^ q7 ^ q1;
	q[1] = q3 ^ q6 ^ q0;
	q[0] = q2 ^ q5 ^ q7;

	aes_ct_bitslice_Sbox(q);

	q0 = ~q[0];
	q1 = ~q[1];
	q2 = q[2];
	q3 = q[3];
	q4 = q[4];
	q5 = ~q[5];
	q6 = ~q[6];
	q7 = q[7];
	q[7] = q1 ^ q4 ^ q6;
	q[6] = q0 ^ q3 ^ q5;
	q[5] = q7 ^ q2 ^ q4;
	q[4] = q6 ^ q1 ^ q3;
	q[3] = q5 ^ q0 ^ q2;
	q[2] = q4 ^ q7 ^ q1;
	q[1] = q3 ^ q6 ^ q0;
	q[0] = q2 ^ q5 ^ q7;
}

static inline void
inv_shift_rows(uint32_t *q)
{
	int i;

	for (i = 0; i < 8; i ++) {
		uint32_t x;

		x = q[i];
		q[i] = (x & 0x000000FF)
			| ((x & 0x00003F00) << 2) | ((x & 0x0000C000) >> 6)
			| ((x & 0x000F0000) << 4) | ((x & 0x00F00000) >> 4)
			| ((x & 0x03000000) << 6) | ((x & 0xFC000000) >> 2);
	}
}

static void
inv_mix_columns(uint32_t *q)
{
	uint32_t q0, q1, q2, q3, q4, q5, q6, q7;
	uint32_t r0, r1, r2, r3, r4, r5, r6, r7;

	q0 = q[0];
	q1 = q[1];
	q2 = q[2];
	q3 = q[3];
	q4 = q[4];
	q5 = q[5];
	q6 = q[6];
	q7 = q[7];
	r0 = (q0 >> 8) | (q0 << 24);
	r1 = (q1 >> 8) | (q1 << 24);
	r2 = (q2 >> 8) | (q2 << 24);
	r3 = (q3 >> 8) | (q3 << 24);
	r4 = (q4 >> 8) | (q4 << 24);
	r5 = (q5 >> 8) | (q5 << 24);
	r6 = (q6 >> 8) | (q6 << 24);
	r7 = (q7 >> 8) | (q7 << 24);

	q[0] = q5 ^ q6 ^ q7 ^ r0 ^ r5 ^ r7 ^ rotr16(q0 ^ q5 ^ q6 ^ r0 ^ r5);
	q[1] = q0 ^ q5 ^ r0 ^ r1 ^ r5 ^ r6 ^ r7 ^ rotr16(q1 ^ q5 ^ q7 ^ r1 ^ r5 ^ r6);
	q[2] = q0 ^ q1 ^ q6 ^ r1 ^ r2 ^ r6 ^ r7 ^ rotr16(q0 ^ q2 ^ q6 ^ r2 ^ r6 ^ r7);
	q[3] = q0 ^ q1 ^ q2 ^ q5 ^ q6 ^ r0 ^ r2 ^ r3 ^ r5 ^ rotr16(q0 ^ q1 ^ q3 ^ q5 ^ q6 ^ q7 ^ r0 ^ r3 ^ r5 ^ r7);
	q[4] = q1 ^ q2 ^ q3 ^ q5 ^ r1 ^ r3 ^ r4 ^ r5 ^ r6 ^ r7 ^ rotr16(q1 ^ q2 ^ q4 ^ q5 ^ q7 ^ r1 ^ r4 ^ r5 ^ r6);
	q[5] = q2 ^ q3 ^ q4 ^ q6 ^ r2 ^ r4 ^ r5 ^ r6 ^ r7 ^ rotr16(q2 ^ q3 ^ q5 ^ q6 ^ r2 ^ r5 ^ r6 ^ r7);
	q[6] = q3 ^ q4 ^ q5 ^ q7 ^ r3 ^ r5 ^ r6 ^ r7 ^ rotr16(q3 ^ q4 ^ q6 ^ q7 ^ r3 ^ r6 ^ r7);
	q[7] = q4 ^ q5 ^ q6 ^ r4 ^ r6 ^ r7 ^ rotr16(q4 ^ q5 ^ q7 ^ r4 ^ r7);
}

/*
 * Compute AES decryption on bitsliced data. Since input is stored on
 * eight 32-bit words, two block decryptions are actually performed
 * in parallel.
 */
void
aes_ct_bitslice_decrypt(unsigned num_rounds,
	const uint32_t *skey, uint32_t *q)
{
	unsigned u;

	add_round_key(q, skey + (num_rounds << 3));
	for (u = num_rounds - 1; u > 0; u --) {
		inv_shift_rows(q);
		aes_ct_bitslice_invSbox(q);
		add_round_key(q, skey + (u << 3));
		inv_mix_columns(q);
	}
	inv_shift_rows(q);
	aes_ct_bitslice_invSbox(q);
	add_round_key(q, skey);
}


int
AES_Setkey(AES_CTX *ctx, const uint8_t *key, int len)
{
	ctx->num_rounds = aes_ct_keysched(ctx->sk, key, len);
	if (ctx->num_rounds == 0)
		return -1;
	aes_ct_skey_expand(ctx->sk_exp, ctx->num_rounds, ctx->sk);
	return 0;
}

void
AES_Encrypt_ECB(AES_CTX *ctx, const uint8_t *src,
	uint8_t *dst, size_t num_blocks)
{
	while (num_blocks > 0) {
		uint32_t q[8];

		q[0] = dec32le(src);
		q[2] = dec32le(src + 4);
		q[4] = dec32le(src + 8);
		q[6] = dec32le(src + 12);
		if (num_blocks > 1) {
			q[1] = dec32le(src + 16);
			q[3] = dec32le(src + 20);
			q[5] = dec32le(src + 24);
			q[7] = dec32le(src + 28);
		} else {
			q[1] = 0;
			q[3] = 0;
			q[5] = 0;
			q[7] = 0;
		}
		aes_ct_ortho(q);
		aes_ct_bitslice_encrypt(ctx->num_rounds, ctx->sk_exp, q);
		aes_ct_ortho(q);
		enc32le(dst, q[0]);
		enc32le(dst + 4, q[2]);
		enc32le(dst + 8, q[4]);
		enc32le(dst + 12, q[6]);
		if (num_blocks > 1) {
			enc32le(dst + 16, q[1]);
			enc32le(dst + 20, q[3]);
			enc32le(dst + 24, q[5]);
			enc32le(dst + 28, q[7]);
			src += 32;
			dst += 32;
			num_blocks -= 2;
		} else {
			break;
		}
	}
}

void
AES_Decrypt_ECB(AES_CTX *ctx, const uint8_t *src,
	uint8_t *dst, size_t num_blocks)
{
	while (num_blocks > 0) {
		uint32_t q[8];

		q[0] = dec32le(src);
		q[2] = dec32le(src + 4);
		q[4] = dec32le(src + 8);
		q[6] = dec32le(src + 12);
		if (num_blocks > 1) {
			q[1] = dec32le(src + 16);
			q[3] = dec32le(src + 20);
			q[5] = dec32le(src + 24);
			q[7] = dec32le(src + 28);
		} else {
			q[1] = 0;
			q[3] = 0;
			q[5] = 0;
			q[7] = 0;
		}
		aes_ct_ortho(q);
		aes_ct_bitslice_decrypt(ctx->num_rounds, ctx->sk_exp, q);
		aes_ct_ortho(q);
		enc32le(dst, q[0]);
		enc32le(dst + 4, q[2]);
		enc32le(dst + 8, q[4]);
		enc32le(dst + 12, q[6]);
		if (num_blocks > 1) {
			enc32le(dst + 16, q[1]);
			enc32le(dst + 20, q[3]);
			enc32le(dst + 24, q[5]);
			enc32le(dst + 28, q[7]);
			src += 32;
			dst += 32;
			num_blocks -= 2;
		} else {
			break;
		}
	}
}

void
AES_Encrypt(AES_CTX *ctx, const uint8_t *src, uint8_t *dst)
{
	AES_Encrypt_ECB(ctx, src, dst, 1);
}

void
AES_Decrypt(AES_CTX *ctx, const uint8_t *src, uint8_t *dst)
{
	AES_Decrypt_ECB(ctx, src, dst, 1);
}

int
AES_KeySetup_Encrypt(uint32_t *skey, const uint8_t *key, int len)
{
	unsigned r, u;
	uint32_t tkey[60];

	r = aes_keysched_base(tkey, key, len);
	if (r == 0) {
		return 0;
	}
	for (u = 0; u < ((r + 1) << 2); u ++) {
		uint32_t w;

		w = tkey[u];
		skey[u] = (w << 24)
			| ((w & 0x0000FF00) << 8)
			| ((w & 0x00FF0000) >> 8)
			| (w >> 24);
	}
	return r;
}

/*
 * Reduce value x modulo polynomial x^8+x^4+x^3+x+1. This works as
 * long as x fits on 12 bits at most.
 */
static inline uint32_t
redgf256(uint32_t x)
{
	uint32_t h;

	h = x >> 8;
	return (x ^ h ^ (h << 1) ^ (h << 3) ^ (h << 4)) & 0xFF;
}

/*
 * Multiplication by 0x09 in GF(256).
 */
static inline uint32_t
mul9(uint32_t x)
{
	return redgf256(x ^ (x << 3));
}

/*
 * Multiplication by 0x0B in GF(256).
 */
static inline uint32_t
mulb(uint32_t x)
{
	return redgf256(x ^ (x << 1) ^ (x << 3));
}

/*
 * Multiplication by 0x0D in GF(256).
 */
static inline uint32_t
muld(uint32_t x)
{
	return redgf256(x ^ (x << 2) ^ (x << 3));
}

/*
 * Multiplication by 0x0E in GF(256).
 */
static inline uint32_t
mule(uint32_t x)
{
	return redgf256((x << 1) ^ (x << 2) ^ (x << 3));
}

int
AES_KeySetup_Decrypt(uint32_t *skey, const uint8_t *key, int len)
{
	unsigned r, u;
	uint32_t tkey[60];

	/*
	 * Compute encryption subkeys. We get them in big-endian
	 * notation.
	 */
	r = AES_KeySetup_Encrypt(tkey, key, len);
	if (r == 0) {
		return 0;
	}

	/*
	 * Copy the subkeys in reverse order. Also, apply InvMixColumns()
	 * on the subkeys (except first and last).
	 */
	memcpy(skey + (r << 2), tkey, 4 * sizeof(uint32_t));
	memcpy(skey, tkey + (r << 2), 4 * sizeof(uint32_t));
	for (u = 4; u < (r << 2); u ++) {
		uint32_t sk, sk0, sk1, sk2, sk3;
		uint32_t tk, tk0, tk1, tk2, tk3;

		sk = tkey[u];
		sk0 = sk >> 24;
		sk1 = (sk >> 16) & 0xFF;
		sk2 = (sk >> 8) & 0xFF;
		sk3 = sk & 0xFF;
		tk0 = mule(sk0) ^ mulb(sk1) ^ muld(sk2) ^ mul9(sk3);
		tk1 = mul9(sk0) ^ mule(sk1) ^ mulb(sk2) ^ muld(sk3);
		tk2 = muld(sk0) ^ mul9(sk1) ^ mule(sk2) ^ mulb(sk3);
		tk3 = mulb(sk0) ^ muld(sk1) ^ mul9(sk2) ^ mule(sk3);
		tk = (tk0 << 24) ^ (tk1 << 16) ^ (tk2 << 8) ^ tk3;
		skey[((r - (u >> 2)) << 2) + (u & 3)] = tk;
	}

	return r;
}
