// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2008-2015 Fuzhou Rockchip Electronics Co., Ltd
 */

#include <errno.h>
#include <memory.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <time.h>

/**
 * \brief	   SHA-1 context structure
 */
typedef struct
{
    unsigned long total[2];	/*!< number of bytes processed	*/
    unsigned long state[5];	/*!< intermediate digest state	*/
    unsigned char buffer[64];	/*!< data block being processed */
}
sha1_context;

/*
 * 32-bit integer manipulation macros (big endian)
 */
#ifndef GET_UINT32_BE
#define GET_UINT32_BE(n,b,i) {				\
	(n) = ( (unsigned long) (b)[(i)    ] << 24 )	\
	    | ( (unsigned long) (b)[(i) + 1] << 16 )	\
	    | ( (unsigned long) (b)[(i) + 2] <<  8 )	\
	    | ( (unsigned long) (b)[(i) + 3]       );	\
}
#endif
#ifndef PUT_UINT32_BE
#define PUT_UINT32_BE(n,b,i) {				\
	(b)[(i)    ] = (unsigned char) ( (n) >> 24 );	\
	(b)[(i) + 1] = (unsigned char) ( (n) >> 16 );	\
	(b)[(i) + 2] = (unsigned char) ( (n) >>  8 );	\
	(b)[(i) + 3] = (unsigned char) ( (n)       );	\
}
#endif

/*
 * SHA-1 context setup
 */
static
void sha1_starts (sha1_context * ctx)
{
	ctx->total[0] = 0;
	ctx->total[1] = 0;

	ctx->state[0] = 0x67452301;
	ctx->state[1] = 0xEFCDAB89;
	ctx->state[2] = 0x98BADCFE;
	ctx->state[3] = 0x10325476;
	ctx->state[4] = 0xC3D2E1F0;
}

static void sha1_process(sha1_context *ctx, const unsigned char data[64])
{
	unsigned long temp, W[16], A, B, C, D, E;

	GET_UINT32_BE (W[0], data, 0);
	GET_UINT32_BE (W[1], data, 4);
	GET_UINT32_BE (W[2], data, 8);
	GET_UINT32_BE (W[3], data, 12);
	GET_UINT32_BE (W[4], data, 16);
	GET_UINT32_BE (W[5], data, 20);
	GET_UINT32_BE (W[6], data, 24);
	GET_UINT32_BE (W[7], data, 28);
	GET_UINT32_BE (W[8], data, 32);
	GET_UINT32_BE (W[9], data, 36);
	GET_UINT32_BE (W[10], data, 40);
	GET_UINT32_BE (W[11], data, 44);
	GET_UINT32_BE (W[12], data, 48);
	GET_UINT32_BE (W[13], data, 52);
	GET_UINT32_BE (W[14], data, 56);
	GET_UINT32_BE (W[15], data, 60);

#define S(x,n)	((x << n) | ((x & 0xFFFFFFFF) >> (32 - n)))

#define R(t) (						\
	temp = W[(t -  3) & 0x0F] ^ W[(t - 8) & 0x0F] ^	\
	       W[(t - 14) & 0x0F] ^ W[ t      & 0x0F],	\
	( W[t & 0x0F] = S(temp,1) )			\
)

#define P(a,b,c,d,e,x)	{				\
	e += S(a,5) + F(b,c,d) + K + x; b = S(b,30);	\
}

	A = ctx->state[0];
	B = ctx->state[1];
	C = ctx->state[2];
	D = ctx->state[3];
	E = ctx->state[4];

#define F(x,y,z) (z ^ (x & (y ^ z)))
#define K 0x5A827999

	P (A, B, C, D, E, W[0]);
	P (E, A, B, C, D, W[1]);
	P (D, E, A, B, C, W[2]);
	P (C, D, E, A, B, W[3]);
	P (B, C, D, E, A, W[4]);
	P (A, B, C, D, E, W[5]);
	P (E, A, B, C, D, W[6]);
	P (D, E, A, B, C, W[7]);
	P (C, D, E, A, B, W[8]);
	P (B, C, D, E, A, W[9]);
	P (A, B, C, D, E, W[10]);
	P (E, A, B, C, D, W[11]);
	P (D, E, A, B, C, W[12]);
	P (C, D, E, A, B, W[13]);
	P (B, C, D, E, A, W[14]);
	P (A, B, C, D, E, W[15]);
	P (E, A, B, C, D, R (16));
	P (D, E, A, B, C, R (17));
	P (C, D, E, A, B, R (18));
	P (B, C, D, E, A, R (19));

#undef K
#undef F

#define F(x,y,z) (x ^ y ^ z)
#define K 0x6ED9EBA1

	P (A, B, C, D, E, R (20));
	P (E, A, B, C, D, R (21));
	P (D, E, A, B, C, R (22));
	P (C, D, E, A, B, R (23));
	P (B, C, D, E, A, R (24));
	P (A, B, C, D, E, R (25));
	P (E, A, B, C, D, R (26));
	P (D, E, A, B, C, R (27));
	P (C, D, E, A, B, R (28));
	P (B, C, D, E, A, R (29));
	P (A, B, C, D, E, R (30));
	P (E, A, B, C, D, R (31));
	P (D, E, A, B, C, R (32));
	P (C, D, E, A, B, R (33));
	P (B, C, D, E, A, R (34));
	P (A, B, C, D, E, R (35));
	P (E, A, B, C, D, R (36));
	P (D, E, A, B, C, R (37));
	P (C, D, E, A, B, R (38));
	P (B, C, D, E, A, R (39));

#undef K
#undef F

#define F(x,y,z) ((x & y) | (z & (x | y)))
#define K 0x8F1BBCDC

	P (A, B, C, D, E, R (40));
	P (E, A, B, C, D, R (41));
	P (D, E, A, B, C, R (42));
	P (C, D, E, A, B, R (43));
	P (B, C, D, E, A, R (44));
	P (A, B, C, D, E, R (45));
	P (E, A, B, C, D, R (46));
	P (D, E, A, B, C, R (47));
	P (C, D, E, A, B, R (48));
	P (B, C, D, E, A, R (49));
	P (A, B, C, D, E, R (50));
	P (E, A, B, C, D, R (51));
	P (D, E, A, B, C, R (52));
	P (C, D, E, A, B, R (53));
	P (B, C, D, E, A, R (54));
	P (A, B, C, D, E, R (55));
	P (E, A, B, C, D, R (56));
	P (D, E, A, B, C, R (57));
	P (C, D, E, A, B, R (58));
	P (B, C, D, E, A, R (59));

#undef K
#undef F

#define F(x,y,z) (x ^ y ^ z)
#define K 0xCA62C1D6

	P (A, B, C, D, E, R (60));
	P (E, A, B, C, D, R (61));
	P (D, E, A, B, C, R (62));
	P (C, D, E, A, B, R (63));
	P (B, C, D, E, A, R (64));
	P (A, B, C, D, E, R (65));
	P (E, A, B, C, D, R (66));
	P (D, E, A, B, C, R (67));
	P (C, D, E, A, B, R (68));
	P (B, C, D, E, A, R (69));
	P (A, B, C, D, E, R (70));
	P (E, A, B, C, D, R (71));
	P (D, E, A, B, C, R (72));
	P (C, D, E, A, B, R (73));
	P (B, C, D, E, A, R (74));
	P (A, B, C, D, E, R (75));
	P (E, A, B, C, D, R (76));
	P (D, E, A, B, C, R (77));
	P (C, D, E, A, B, R (78));
	P (B, C, D, E, A, R (79));

#undef K
#undef F

	ctx->state[0] += A;
	ctx->state[1] += B;
	ctx->state[2] += C;
	ctx->state[3] += D;
	ctx->state[4] += E;
}

#undef P
#undef R
#undef S

/*
 * SHA-1 process buffer
 */
static
void sha1_update(sha1_context *ctx, const unsigned char *input,
		 unsigned int ilen)
{
	int fill;
	unsigned long left;

	if (ilen <= 0)
		return;

	left = ctx->total[0] & 0x3F;
	fill = 64 - left;

	ctx->total[0] += ilen;
	ctx->total[0] &= 0xFFFFFFFF;

	if (ctx->total[0] < (unsigned long) ilen)
		ctx->total[1]++;

	if (left && ilen >= fill) {
		memcpy ((void *) (ctx->buffer + left), (void *) input, fill);
		sha1_process (ctx, ctx->buffer);
		input += fill;
		ilen -= fill;
		left = 0;
	}

	while (ilen >= 64) {
		sha1_process (ctx, input);
		input += 64;
		ilen -= 64;
	}

	if (ilen > 0) {
		memcpy ((void *) (ctx->buffer + left), (void *) input, ilen);
	}
}

static const unsigned char sha1_padding[64] = {
	0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/*
 * SHA-1 final digest
 */
static
void sha1_finish (sha1_context * ctx, unsigned char output[20])
{
	unsigned long last, padn;
	unsigned long high, low;
	unsigned char msglen[8];

	high = (ctx->total[0] >> 29)
		| (ctx->total[1] << 3);
	low = (ctx->total[0] << 3);

	PUT_UINT32_BE (high, msglen, 0);
	PUT_UINT32_BE (low, msglen, 4);

	last = ctx->total[0] & 0x3F;
	padn = (last < 56) ? (56 - last) : (120 - last);

	sha1_update (ctx, (unsigned char *) sha1_padding, padn);
	sha1_update (ctx, msglen, 8);

	PUT_UINT32_BE (ctx->state[0], output, 0);
	PUT_UINT32_BE (ctx->state[1], output, 4);
	PUT_UINT32_BE (ctx->state[2], output, 8);
	PUT_UINT32_BE (ctx->state[3], output, 12);
	PUT_UINT32_BE (ctx->state[4], output, 16);
}

/*
 * Output = SHA-1( input buffer )
 */
static
void sha1_csum(const unsigned char *input, unsigned int ilen,
	       unsigned char *output)
{
	sha1_context ctx;

	sha1_starts (&ctx);
	sha1_update (&ctx, input, ilen);
	sha1_finish (&ctx, output);
}

typedef struct {
	uint32_t total[2];
	uint32_t state[8];
	uint8_t buffer[64];
} sha256_context;

static
void sha256_starts(sha256_context * ctx)
{
	ctx->total[0] = 0;
	ctx->total[1] = 0;

	ctx->state[0] = 0x6A09E667;
	ctx->state[1] = 0xBB67AE85;
	ctx->state[2] = 0x3C6EF372;
	ctx->state[3] = 0xA54FF53A;
	ctx->state[4] = 0x510E527F;
	ctx->state[5] = 0x9B05688C;
	ctx->state[6] = 0x1F83D9AB;
	ctx->state[7] = 0x5BE0CD19;
}

static void sha256_process(sha256_context *ctx, const uint8_t data[64])
{
	uint32_t temp1, temp2;
	uint32_t W[64];
	uint32_t A, B, C, D, E, F, G, H;

	GET_UINT32_BE(W[0], data, 0);
	GET_UINT32_BE(W[1], data, 4);
	GET_UINT32_BE(W[2], data, 8);
	GET_UINT32_BE(W[3], data, 12);
	GET_UINT32_BE(W[4], data, 16);
	GET_UINT32_BE(W[5], data, 20);
	GET_UINT32_BE(W[6], data, 24);
	GET_UINT32_BE(W[7], data, 28);
	GET_UINT32_BE(W[8], data, 32);
	GET_UINT32_BE(W[9], data, 36);
	GET_UINT32_BE(W[10], data, 40);
	GET_UINT32_BE(W[11], data, 44);
	GET_UINT32_BE(W[12], data, 48);
	GET_UINT32_BE(W[13], data, 52);
	GET_UINT32_BE(W[14], data, 56);
	GET_UINT32_BE(W[15], data, 60);

#define SHR(x,n) ((x & 0xFFFFFFFF) >> n)
#define ROTR(x,n) (SHR(x,n) | (x << (32 - n)))

#define S0(x) (ROTR(x, 7) ^ ROTR(x,18) ^ SHR(x, 3))
#define S1(x) (ROTR(x,17) ^ ROTR(x,19) ^ SHR(x,10))

#define S2(x) (ROTR(x, 2) ^ ROTR(x,13) ^ ROTR(x,22))
#define S3(x) (ROTR(x, 6) ^ ROTR(x,11) ^ ROTR(x,25))

#define F0(x,y,z) ((x & y) | (z & (x | y)))
#define F1(x,y,z) (z ^ (x & (y ^ z)))

#define R(t)					\
(						\
	W[t] = S1(W[t - 2]) + W[t - 7] +	\
		S0(W[t - 15]) + W[t - 16]	\
)

#define P(a,b,c,d,e,f,g,h,x,K) {		\
	temp1 = h + S3(e) + F1(e,f,g) + K + x;	\
	temp2 = S2(a) + F0(a,b,c);		\
	d += temp1; h = temp1 + temp2;		\
}

	A = ctx->state[0];
	B = ctx->state[1];
	C = ctx->state[2];
	D = ctx->state[3];
	E = ctx->state[4];
	F = ctx->state[5];
	G = ctx->state[6];
	H = ctx->state[7];

	P(A, B, C, D, E, F, G, H, W[0], 0x428A2F98);
	P(H, A, B, C, D, E, F, G, W[1], 0x71374491);
	P(G, H, A, B, C, D, E, F, W[2], 0xB5C0FBCF);
	P(F, G, H, A, B, C, D, E, W[3], 0xE9B5DBA5);
	P(E, F, G, H, A, B, C, D, W[4], 0x3956C25B);
	P(D, E, F, G, H, A, B, C, W[5], 0x59F111F1);
	P(C, D, E, F, G, H, A, B, W[6], 0x923F82A4);
	P(B, C, D, E, F, G, H, A, W[7], 0xAB1C5ED5);
	P(A, B, C, D, E, F, G, H, W[8], 0xD807AA98);
	P(H, A, B, C, D, E, F, G, W[9], 0x12835B01);
	P(G, H, A, B, C, D, E, F, W[10], 0x243185BE);
	P(F, G, H, A, B, C, D, E, W[11], 0x550C7DC3);
	P(E, F, G, H, A, B, C, D, W[12], 0x72BE5D74);
	P(D, E, F, G, H, A, B, C, W[13], 0x80DEB1FE);
	P(C, D, E, F, G, H, A, B, W[14], 0x9BDC06A7);
	P(B, C, D, E, F, G, H, A, W[15], 0xC19BF174);
	P(A, B, C, D, E, F, G, H, R(16), 0xE49B69C1);
	P(H, A, B, C, D, E, F, G, R(17), 0xEFBE4786);
	P(G, H, A, B, C, D, E, F, R(18), 0x0FC19DC6);
	P(F, G, H, A, B, C, D, E, R(19), 0x240CA1CC);
	P(E, F, G, H, A, B, C, D, R(20), 0x2DE92C6F);
	P(D, E, F, G, H, A, B, C, R(21), 0x4A7484AA);
	P(C, D, E, F, G, H, A, B, R(22), 0x5CB0A9DC);
	P(B, C, D, E, F, G, H, A, R(23), 0x76F988DA);
	P(A, B, C, D, E, F, G, H, R(24), 0x983E5152);
	P(H, A, B, C, D, E, F, G, R(25), 0xA831C66D);
	P(G, H, A, B, C, D, E, F, R(26), 0xB00327C8);
	P(F, G, H, A, B, C, D, E, R(27), 0xBF597FC7);
	P(E, F, G, H, A, B, C, D, R(28), 0xC6E00BF3);
	P(D, E, F, G, H, A, B, C, R(29), 0xD5A79147);
	P(C, D, E, F, G, H, A, B, R(30), 0x06CA6351);
	P(B, C, D, E, F, G, H, A, R(31), 0x14292967);
	P(A, B, C, D, E, F, G, H, R(32), 0x27B70A85);
	P(H, A, B, C, D, E, F, G, R(33), 0x2E1B2138);
	P(G, H, A, B, C, D, E, F, R(34), 0x4D2C6DFC);
	P(F, G, H, A, B, C, D, E, R(35), 0x53380D13);
	P(E, F, G, H, A, B, C, D, R(36), 0x650A7354);
	P(D, E, F, G, H, A, B, C, R(37), 0x766A0ABB);
	P(C, D, E, F, G, H, A, B, R(38), 0x81C2C92E);
	P(B, C, D, E, F, G, H, A, R(39), 0x92722C85);
	P(A, B, C, D, E, F, G, H, R(40), 0xA2BFE8A1);
	P(H, A, B, C, D, E, F, G, R(41), 0xA81A664B);
	P(G, H, A, B, C, D, E, F, R(42), 0xC24B8B70);
	P(F, G, H, A, B, C, D, E, R(43), 0xC76C51A3);
	P(E, F, G, H, A, B, C, D, R(44), 0xD192E819);
	P(D, E, F, G, H, A, B, C, R(45), 0xD6990624);
	P(C, D, E, F, G, H, A, B, R(46), 0xF40E3585);
	P(B, C, D, E, F, G, H, A, R(47), 0x106AA070);
	P(A, B, C, D, E, F, G, H, R(48), 0x19A4C116);
	P(H, A, B, C, D, E, F, G, R(49), 0x1E376C08);
	P(G, H, A, B, C, D, E, F, R(50), 0x2748774C);
	P(F, G, H, A, B, C, D, E, R(51), 0x34B0BCB5);
	P(E, F, G, H, A, B, C, D, R(52), 0x391C0CB3);
	P(D, E, F, G, H, A, B, C, R(53), 0x4ED8AA4A);
	P(C, D, E, F, G, H, A, B, R(54), 0x5B9CCA4F);
	P(B, C, D, E, F, G, H, A, R(55), 0x682E6FF3);
	P(A, B, C, D, E, F, G, H, R(56), 0x748F82EE);
	P(H, A, B, C, D, E, F, G, R(57), 0x78A5636F);
	P(G, H, A, B, C, D, E, F, R(58), 0x84C87814);
	P(F, G, H, A, B, C, D, E, R(59), 0x8CC70208);
	P(E, F, G, H, A, B, C, D, R(60), 0x90BEFFFA);
	P(D, E, F, G, H, A, B, C, R(61), 0xA4506CEB);
	P(C, D, E, F, G, H, A, B, R(62), 0xBEF9A3F7);
	P(B, C, D, E, F, G, H, A, R(63), 0xC67178F2);

	ctx->state[0] += A;
	ctx->state[1] += B;
	ctx->state[2] += C;
	ctx->state[3] += D;
	ctx->state[4] += E;
	ctx->state[5] += F;
	ctx->state[6] += G;
	ctx->state[7] += H;
}

#undef P
#undef R
#undef F1
#undef F0
#undef S3
#undef S2
#undef S1
#undef S0
#undef ROTR
#undef SHR

static
void sha256_update(sha256_context *ctx, const uint8_t *input, uint32_t length)
{
	uint32_t left, fill;

	if (!length)
		return;

	left = ctx->total[0] & 0x3F;
	fill = 64 - left;

	ctx->total[0] += length;
	ctx->total[0] &= 0xFFFFFFFF;

	if (ctx->total[0] < length)
		ctx->total[1]++;

	if (left && length >= fill) {
		memcpy((void *) (ctx->buffer + left), (void *) input, fill);
		sha256_process(ctx, ctx->buffer);
		length -= fill;
		input += fill;
		left = 0;
	}

	while (length >= 64) {
		sha256_process(ctx, input);
		length -= 64;
		input += 64;
	}

	if (length)
		memcpy((void *) (ctx->buffer + left), (void *) input, length);
}

static uint8_t sha256_padding[64] = {
	0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static
void sha256_finish(sha256_context * ctx, uint8_t digest[32])
{
	uint32_t last, padn;
	uint32_t high, low;
	uint8_t msglen[8];

	high = ((ctx->total[0] >> 29)
		| (ctx->total[1] << 3));
	low = (ctx->total[0] << 3);

	PUT_UINT32_BE(high, msglen, 0);
	PUT_UINT32_BE(low, msglen, 4);

	last = ctx->total[0] & 0x3F;
	padn = (last < 56) ? (56 - last) : (120 - last);

	sha256_update(ctx, sha256_padding, padn);
	sha256_update(ctx, msglen, 8);

	PUT_UINT32_BE(ctx->state[0], digest, 0);
	PUT_UINT32_BE(ctx->state[1], digest, 4);
	PUT_UINT32_BE(ctx->state[2], digest, 8);
	PUT_UINT32_BE(ctx->state[3], digest, 12);
	PUT_UINT32_BE(ctx->state[4], digest, 16);
	PUT_UINT32_BE(ctx->state[5], digest, 20);
	PUT_UINT32_BE(ctx->state[6], digest, 24);
	PUT_UINT32_BE(ctx->state[7], digest, 28);
}

/*
 * Output = SHA-256( input buffer ).
 */
static
void sha256_csum(const unsigned char *input, unsigned int ilen,
		 unsigned char *output)
{
	sha256_context ctx;

	sha256_starts(&ctx);
	sha256_update(&ctx, input, ilen);
	sha256_finish(&ctx, output);
}

/* #define DEBUG */

static bool g_debug =
#ifdef DEBUG
        true;
#else
        false;
#endif /* DEBUG */

#define LOGE(fmt, args...)                                                     \
  fprintf(stderr, "E/%s(%d): " fmt "\n", __func__, __LINE__, ##args)
#define LOGD(fmt, args...)                                                     \
  do {                                                                         \
    if (g_debug)                                                               \
      fprintf(stderr, "D/%s(%d): " fmt "\n", __func__, __LINE__, ##args);      \
  } while (0)

/* sync with ./board/rockchip/rk30xx/rkloader.c #define FDT_PATH */
#define FDT_PATH "rk-kernel.dtb"
#define DTD_SUBFIX ".dtb"

#define DEFAULT_IMAGE_PATH "resource.img"
#define DEFAULT_UNPACK_DIR "out"
#define BLOCK_SIZE 512

#define RESOURCE_PTN_HDR_SIZE 1
#define INDEX_TBL_ENTR_SIZE 1

#define RESOURCE_PTN_VERSION 0
#define INDEX_TBL_VERSION 0

#define RESOURCE_PTN_HDR_MAGIC "RSCE"
typedef struct {
	char magic[4]; /* tag, "RSCE" */
	uint16_t resource_ptn_version;
	uint16_t index_tbl_version;
	uint8_t header_size;    /* blocks, size of ptn header. */
	uint8_t tbl_offset;     /* blocks, offset of index table. */
	uint8_t tbl_entry_size; /* blocks, size of index table's entry. */
	uint32_t tbl_entry_num; /* numbers of index table's entry. */
} resource_ptn_header;

#define INDEX_TBL_ENTR_TAG "ENTR"
#define MAX_INDEX_ENTRY_PATH_LEN	220
#define MAX_HASH_LEN			32

typedef struct {
	char tag[4]; /* tag, "ENTR" */
	char path[MAX_INDEX_ENTRY_PATH_LEN];
	char hash[MAX_HASH_LEN]; /* hash data */
	uint32_t hash_size;	 /* 20 or 32 */
	uint32_t content_offset; /* blocks, offset of resource content. */
	uint32_t content_size;   /* bytes, size of resource content. */
} index_tbl_entry;

#define OPT_VERBOSE "--verbose"
#define OPT_HELP "--help"
#define OPT_VERSION "--version"
#define OPT_PRINT "--print"
#define OPT_PACK "--pack"
#define OPT_UNPACK "--unpack"
#define OPT_TEST_LOAD "--test_load"
#define OPT_TEST_CHARGE "--test_charge"
#define OPT_IMAGE "--image="
#define OPT_ROOT "--root="

#define VERSION "2014-5-31 14:43:42"

typedef struct {
	char path[MAX_INDEX_ENTRY_PATH_LEN];
	uint32_t content_offset; /* blocks, offset of resource content. */
	uint32_t content_size;   /* bytes, size of resource content. */
	void *load_addr;
} resource_content;

typedef struct {
	int max_level;
	int num;
	int delay;
	char prefix[MAX_INDEX_ENTRY_PATH_LEN];
} anim_level_conf;

#define DEF_CHARGE_DESC_PATH "charge_anim_desc.txt"

#define OPT_CHARGE_ANIM_DELAY "delay="
#define OPT_CHARGE_ANIM_LOOP_CUR "only_current_level="
#define OPT_CHARGE_ANIM_LEVELS "levels="
#define OPT_CHARGE_ANIM_LEVEL_CONF "max_level="
#define OPT_CHARGE_ANIM_LEVEL_NUM "num="
#define OPT_CHARGE_ANIM_LEVEL_PFX "prefix="

static char image_path[MAX_INDEX_ENTRY_PATH_LEN] = "\0";

static int fix_blocks(size_t size)
{
	return (size + BLOCK_SIZE - 1) / BLOCK_SIZE;
}

static const char *fix_path(const char *path)
{
	if (!memcmp(path, "./", 2)) {
		return path + 2;
	}
	return path;
}

static uint16_t switch_short(uint16_t x)
{
	uint16_t val;
	uint8_t *p = (uint8_t *)(&x);

	val = (*p++ & 0xff) << 0;
	val |= (*p & 0xff) << 8;

	return val;
}

static uint32_t switch_int(uint32_t x)
{
	uint32_t val;
	uint8_t *p = (uint8_t *)(&x);

	val = (*p++ & 0xff) << 0;
	val |= (*p++ & 0xff) << 8;
	val |= (*p++ & 0xff) << 16;
	val |= (*p & 0xff) << 24;

	return val;
}

static void fix_header(resource_ptn_header *header)
{
	/* switch for be. */
	header->resource_ptn_version = switch_short(header->resource_ptn_version);
	header->index_tbl_version = switch_short(header->index_tbl_version);
	header->tbl_entry_num = switch_int(header->tbl_entry_num);
}

static void fix_entry(index_tbl_entry *entry)
{
	/* switch for be. */
	entry->content_offset = switch_int(entry->content_offset);
	entry->content_size = switch_int(entry->content_size);
}

static int inline get_ptn_offset(void)
{
	return 0;
}

static bool StorageWriteLba(int offset_block, void *data, int blocks)
{
	bool ret = false;
	FILE *file = fopen(image_path, "rb+");
	if (!file)
		goto end;
	int offset = offset_block * BLOCK_SIZE;
	fseek(file, offset, SEEK_SET);
	if (offset != ftell(file)) {
		LOGE("Failed to seek %s to %d!", image_path, offset);
		goto end;
	}
	if (!fwrite(data, blocks * BLOCK_SIZE, 1, file)) {
		LOGE("Failed to write %s!", image_path);
		goto end;
	}
	ret = true;
end:
	if (file)
		fclose(file);
	return ret;
}

static bool StorageReadLba(int offset_block, void *data, int blocks)
{
	bool ret = false;
	FILE *file = fopen(image_path, "rb");
	if (!file)
		goto end;
	int offset = offset_block * BLOCK_SIZE;
	fseek(file, offset, SEEK_SET);
	if (offset != ftell(file)) {
		goto end;
	}
	if (!fread(data, blocks * BLOCK_SIZE, 1, file)) {
		goto end;
	}
	ret = true;
end:
	if (file)
		fclose(file);
	return ret;
}

static bool write_data(int offset_block, void *data, size_t len)
{
	bool ret = false;
	if (!data)
		goto end;
	int blocks = len / BLOCK_SIZE;
	if (blocks && !StorageWriteLba(offset_block, data, blocks)) {
		goto end;
	}
	int left = len % BLOCK_SIZE;
	if (left) {
		char buf[BLOCK_SIZE] = "\0";
		memcpy(buf, data + blocks * BLOCK_SIZE, left);
		if (!StorageWriteLba(offset_block + blocks, buf, 1))
			goto end;
	}
	ret = true;
end:
	return ret;
}

/**********************load test************************/
static int load_file(const char *file_path, int offset_block, int blocks);

static int test_load(int argc, char **argv)
{
	if (argc < 1) {
		LOGE("Nothing to load!");
		return -1;
	}
	const char *file_path;
	int offset_block = 0;
	int blocks = 0;
	if (argc > 0) {
		file_path = (const char *)fix_path(argv[0]);
		argc--, argv++;
	}
	if (argc > 0) {
		offset_block = atoi(argv[0]);
		argc--, argv++;
	}
	if (argc > 0) {
		blocks = atoi(argv[0]);
	}
	return load_file(file_path, offset_block, blocks);
}

static void free_content(resource_content *content)
{
	if (content->load_addr) {
		free(content->load_addr);
		content->load_addr = 0;
	}
}

static void tests_dump_file(const char *path, void *data, int len)
{
	FILE *file = fopen(path, "wb");
	if (!file)
		return;
	fwrite(data, len, 1, file);
	fclose(file);
}

static bool load_content(resource_content *content)
{
	if (content->load_addr)
		return true;
	int blocks = fix_blocks(content->content_size);
	content->load_addr = malloc(blocks * BLOCK_SIZE);
	if (!content->load_addr)
		return false;
	if (!StorageReadLba(get_ptn_offset() + content->content_offset,
	                    content->load_addr, blocks)) {
		free_content(content);
		return false;
	}

	tests_dump_file(content->path, content->load_addr, content->content_size);
	return true;
}

static bool load_content_data(resource_content *content, int offset_block,
                              void *data, int blocks)
{
	if (!StorageReadLba(get_ptn_offset() + content->content_offset + offset_block,
	                    data, blocks)) {
		return false;
	}
	tests_dump_file(content->path, data, blocks * BLOCK_SIZE);
	return true;
}

static bool get_entry(const char *file_path, index_tbl_entry *entry)
{
	bool ret = false;
	char buf[BLOCK_SIZE];
	resource_ptn_header header;
	if (!StorageReadLba(get_ptn_offset(), buf, 1)) {
		LOGE("Failed to read header!");
		goto end;
	}
	memcpy(&header, buf, sizeof(header));

	if (memcmp(header.magic, RESOURCE_PTN_HDR_MAGIC, sizeof(header.magic))) {
		LOGE("Not a resource image(%s)!", image_path);
		goto end;
	}
	/* test on pc, switch for be. */
	fix_header(&header);

	/* TODO: support header_size & tbl_entry_size */
	if (header.resource_ptn_version != RESOURCE_PTN_VERSION ||
	    header.header_size != RESOURCE_PTN_HDR_SIZE ||
	    header.index_tbl_version != INDEX_TBL_VERSION ||
	    header.tbl_entry_size != INDEX_TBL_ENTR_SIZE) {
		LOGE("Not supported in this version!");
		goto end;
	}

	int i;
	for (i = 0; i < header.tbl_entry_num; i++) {
		/* TODO: support tbl_entry_size */
		if (!StorageReadLba(
		            get_ptn_offset() + header.header_size + i * header.tbl_entry_size,
		            buf, 1)) {
			LOGE("Failed to read index entry:%d!", i);
			goto end;
		}
		memcpy(entry, buf, sizeof(*entry));

		if (memcmp(entry->tag, INDEX_TBL_ENTR_TAG, sizeof(entry->tag))) {
			LOGE("Something wrong with index entry:%d!", i);
			goto end;
		}

		if (!strncmp(entry->path, file_path, sizeof(entry->path)))
			break;
	}
	if (i == header.tbl_entry_num) {
		LOGE("Cannot find %s!", file_path);
		goto end;
	}
	/* test on pc, switch for be. */
	fix_entry(entry);

	printf("Found entry:\n\tpath:%s\n\toffset:%d\tsize:%d\n", entry->path,
	       entry->content_offset, entry->content_size);

	ret = true;
end:
	return ret;
}

static bool get_content(resource_content *content)
{
	bool ret = false;
	index_tbl_entry entry;
	if (!get_entry(content->path, &entry))
		goto end;
	content->content_offset = entry.content_offset;
	content->content_size = entry.content_size;
	ret = true;
end:
	return ret;
}

static int load_file(const char *file_path, int offset_block, int blocks)
{
	printf("Try to load:%s", file_path);
	if (blocks) {
		printf(", offset block:%d, blocks:%d\n", offset_block, blocks);
	} else {
		printf("\n");
	}
	bool ret = false;
	resource_content content;
	snprintf(content.path, sizeof(content.path), "%s", file_path);
	content.load_addr = 0;
	if (!get_content(&content)) {
		goto end;
	}
	if (!blocks) {
		if (!load_content(&content)) {
			goto end;
		}
	} else {
		void *data = malloc(blocks * BLOCK_SIZE);
		if (!data)
			goto end;
		if (!load_content_data(&content, offset_block, data, blocks)) {
			goto end;
		}
	}
	ret = true;
end:
	free_content(&content);
	return ret;
}

/**********************load test end************************/
/**********************anim test************************/

static bool parse_level_conf(const char *arg, anim_level_conf *level_conf)
{
	memset(level_conf, 0, sizeof(anim_level_conf));
	char *buf = NULL;
	buf = strstr(arg, OPT_CHARGE_ANIM_LEVEL_CONF);
	if (buf) {
		level_conf->max_level = atoi(buf + strlen(OPT_CHARGE_ANIM_LEVEL_CONF));
	} else {
		LOGE("Not found:%s", OPT_CHARGE_ANIM_LEVEL_CONF);
		return false;
	}
	buf = strstr(arg, OPT_CHARGE_ANIM_LEVEL_NUM);
	if (buf) {
		level_conf->num = atoi(buf + strlen(OPT_CHARGE_ANIM_LEVEL_NUM));
		if (level_conf->num <= 0) {
			return false;
		}
	} else {
		LOGE("Not found:%s", OPT_CHARGE_ANIM_LEVEL_NUM);
		return false;
	}
	buf = strstr(arg, OPT_CHARGE_ANIM_DELAY);
	if (buf) {
		level_conf->delay = atoi(buf + strlen(OPT_CHARGE_ANIM_DELAY));
	}
	buf = strstr(arg, OPT_CHARGE_ANIM_LEVEL_PFX);
	if (buf) {
		snprintf(level_conf->prefix, sizeof(level_conf->prefix), "%s",
		         buf + strlen(OPT_CHARGE_ANIM_LEVEL_PFX));
	} else {
		LOGE("Not found:%s", OPT_CHARGE_ANIM_LEVEL_PFX);
		return false;
	}

	LOGD("Found conf:\nmax_level:%d, num:%d, delay:%d, prefix:%s",
	     level_conf->max_level, level_conf->num, level_conf->delay,
	     level_conf->prefix);
	return true;
}

static int test_charge(int argc, char **argv)
{
	const char *desc;
	if (argc > 0) {
		desc = argv[0];
	} else {
		desc = DEF_CHARGE_DESC_PATH;
	}

	resource_content content;
	snprintf(content.path, sizeof(content.path), "%s", desc);
	content.load_addr = 0;
	if (!get_content(&content)) {
		goto end;
	}
	if (!load_content(&content)) {
		goto end;
	}

	char *buf = (char *)content.load_addr;
	char *end = buf + content.content_size - 1;
	*end = '\0';
	LOGD("desc:\n%s", buf);

	int pos = 0;
	while (1) {
		char *line = (char *)memchr(buf + pos, '\n', strlen(buf + pos));
		if (!line)
			break;
		*line = '\0';
		LOGD("splite:%s", buf + pos);
		pos += (strlen(buf + pos) + 1);
	}

	int delay = 900;
	int only_current_level = false;
	anim_level_conf *level_confs = NULL;
	int level_conf_pos = 0;
	int level_conf_num = 0;

	while (true) {
		if (buf >= end)
			break;
		const char *arg = buf;
		buf += (strlen(buf) + 1);

		LOGD("parse arg:%s", arg);
		if (!memcmp(arg, OPT_CHARGE_ANIM_LEVEL_CONF,
		            strlen(OPT_CHARGE_ANIM_LEVEL_CONF))) {
			if (!level_confs) {
				LOGE("Found level conf before levels!");
				goto end;
			}
			if (level_conf_pos >= level_conf_num) {
				LOGE("Too many level confs!(%d >= %d)", level_conf_pos, level_conf_num);
				goto end;
			}
			if (!parse_level_conf(arg, level_confs + level_conf_pos)) {
				LOGE("Failed to parse level conf:%s", arg);
				goto end;
			}
			level_conf_pos++;
		} else if (!memcmp(arg, OPT_CHARGE_ANIM_DELAY,
		                   strlen(OPT_CHARGE_ANIM_DELAY))) {
			delay = atoi(arg + strlen(OPT_CHARGE_ANIM_DELAY));
			LOGD("Found delay:%d", delay);
		} else if (!memcmp(arg, OPT_CHARGE_ANIM_LOOP_CUR,
		                   strlen(OPT_CHARGE_ANIM_LOOP_CUR))) {
			only_current_level =
			        !memcmp(arg + strlen(OPT_CHARGE_ANIM_LOOP_CUR), "true", 4);
			LOGD("Found only_current_level:%d", only_current_level);
		} else if (!memcmp(arg, OPT_CHARGE_ANIM_LEVELS,
		                   strlen(OPT_CHARGE_ANIM_LEVELS))) {
			if (level_conf_num) {
				goto end;
			}
			level_conf_num = atoi(arg + strlen(OPT_CHARGE_ANIM_LEVELS));
			if (!level_conf_num) {
				goto end;
			}
			level_confs =
			        (anim_level_conf *)malloc(level_conf_num * sizeof(anim_level_conf));
			LOGD("Found levels:%d", level_conf_num);
		} else {
			LOGE("Unknown arg:%s", arg);
			goto end;
		}
	}

	if (level_conf_pos != level_conf_num || !level_conf_num) {
		LOGE("Something wrong with level confs!");
		goto end;
	}

	int i = 0, j = 0;
	for (i = 0; i < level_conf_num; i++) {
		if (!level_confs[i].delay) {
			level_confs[i].delay = delay;
		}
		if (!level_confs[i].delay) {
			LOGE("Missing delay in level conf:%d", i);
			goto end;
		}
		for (j = 0; j < i; j++) {
			if (level_confs[j].max_level == level_confs[i].max_level) {
				LOGE("Dup level conf:%d", i);
				goto end;
			}
			if (level_confs[j].max_level > level_confs[i].max_level) {
				anim_level_conf conf = level_confs[i];
				memmove(level_confs + j + 1, level_confs + j,
				        (i - j) * sizeof(anim_level_conf));
				level_confs[j] = conf;
			}
		}
	}

	printf("Parse anim desc(%s):\n", desc);
	printf("only_current_level=%d\n", only_current_level);
	printf("level conf:\n");
	for (i = 0; i < level_conf_num; i++) {
		printf("\tmax=%d, delay=%d, num=%d, prefix=%s\n", level_confs[i].max_level,
		       level_confs[i].delay, level_confs[i].num, level_confs[i].prefix);
	}

end:
	free_content(&content);
	return 0;
}

/**********************anim test end************************/
/**********************append file************************/

static const char *PROG = NULL;
static resource_ptn_header header;
static bool just_print = false;
static char root_path[MAX_INDEX_ENTRY_PATH_LEN] = "\0";

static void version(void)
{
	printf("%s (cjf@rock-chips.com)\t" VERSION "\n", PROG);
}

static void usage(void)
{
	printf("Usage: %s [options] [FILES]\n", PROG);
	printf("Tools for Rockchip's resource image.\n");
	version();
	printf("Options:\n");
	printf("\t" OPT_PACK "\t\t\tPack image from given files.\n");
	printf("\t" OPT_UNPACK "\t\tUnpack given image to current dir.\n");
	printf("\t" OPT_IMAGE "path"
	       "\t\tSpecify input/output image path.\n");
	printf("\t" OPT_PRINT "\t\t\tJust print informations.\n");
	printf("\t" OPT_VERBOSE "\t\tDisplay more runtime informations.\n");
	printf("\t" OPT_HELP "\t\t\tDisplay this information.\n");
	printf("\t" OPT_VERSION "\t\tDisplay version information.\n");
	printf("\t" OPT_ROOT "path"
	       "\t\tSpecify resources' root dir.\n");
}

static int pack_image(int file_num, const char **files);
static int unpack_image(const char *unpack_dir);

enum ACTION {
	ACTION_PACK,
	ACTION_UNPACK,
	ACTION_TEST_LOAD,
	ACTION_TEST_CHARGE,
};

int main(int argc, char **argv)
{
	PROG = fix_path(argv[0]);

	enum ACTION action = ACTION_PACK;

	argc--, argv++;
	while (argc > 0 && argv[0][0] == '-') {
		/* it's a opt arg. */
		const char *arg = argv[0];
		argc--, argv++;
		if (!strcmp(OPT_VERBOSE, arg)) {
			g_debug = true;
		} else if (!strcmp(OPT_HELP, arg)) {
			usage();
			return 0;
		} else if (!strcmp(OPT_VERSION, arg)) {
			version();
			return 0;
		} else if (!strcmp(OPT_PRINT, arg)) {
			just_print = true;
		} else if (!strcmp(OPT_PACK, arg)) {
			action = ACTION_PACK;
		} else if (!strcmp(OPT_UNPACK, arg)) {
			action = ACTION_UNPACK;
		} else if (!strcmp(OPT_TEST_LOAD, arg)) {
			action = ACTION_TEST_LOAD;
		} else if (!strcmp(OPT_TEST_CHARGE, arg)) {
			action = ACTION_TEST_CHARGE;
		} else if (!memcmp(OPT_IMAGE, arg, strlen(OPT_IMAGE))) {
			snprintf(image_path, sizeof(image_path), "%s", arg + strlen(OPT_IMAGE));
		} else if (!memcmp(OPT_ROOT, arg, strlen(OPT_ROOT))) {
			snprintf(root_path, sizeof(root_path), "%s", arg + strlen(OPT_ROOT));
		} else {
			LOGE("Unknown opt:%s", arg);
			usage();
			return -1;
		}
	}

	if (!image_path[0]) {
		snprintf(image_path, sizeof(image_path), "%s", DEFAULT_IMAGE_PATH);
	}

	switch (action) {
	case ACTION_PACK: {
		int file_num = argc;
		const char **files = (const char **)argv;
		if (!file_num) {
			LOGE("No file to pack!");
			return 0;
		}
		LOGD("try to pack %d files.", file_num);
		return pack_image(file_num, files);
	}
	case ACTION_UNPACK: {
		return unpack_image(argc > 0 ? argv[0] : DEFAULT_UNPACK_DIR);
	}
	case ACTION_TEST_LOAD: {
		return test_load(argc, argv);
	}
	case ACTION_TEST_CHARGE: {
		return test_charge(argc, argv);
	}
	}
	/* not reach here. */
	return -1;
}

/************unpack code****************/
static bool mkdirs(char *path)
{
	char *tmp = path;
	char *pos = NULL;
	char buf[MAX_INDEX_ENTRY_PATH_LEN];
	bool ret = true;
	while ((pos = memchr(tmp, '/', strlen(tmp)))) {
		strcpy(buf, path);
		buf[pos - path] = '\0';
		tmp = pos + 1;
		LOGD("mkdir:%s", buf);
		if (!mkdir(buf, 0755)) {
			ret = false;
		}
	}
	if (!ret)
		LOGD("Failed to mkdir(%s)!", path);
	return ret;
}

static bool dump_file(FILE *file, const char *unpack_dir,
                      index_tbl_entry entry)
{
	LOGD("try to dump entry:%s", entry.path);
	bool ret = false;
	FILE *out_file = NULL;
	long int pos = 0;
	char path[MAX_INDEX_ENTRY_PATH_LEN * 2 + 1];
	if (just_print) {
		ret = true;
		goto done;
	}

	pos = ftell(file);
	snprintf(path, sizeof(path), "%s/%s", unpack_dir, entry.path);
	mkdirs(path);
	out_file = fopen(path, "wb");
	if (!out_file) {
		LOGE("Failed to create:%s", path);
		goto end;
	}
	long int offset = entry.content_offset * BLOCK_SIZE;
	fseek(file, offset, SEEK_SET);
	if (offset != ftell(file)) {
		LOGE("Failed to read content:%s", entry.path);
		goto end;
	}
	char buf[BLOCK_SIZE];
	int n;
	int len = entry.content_size;
	while (len > 0) {
		n = len > BLOCK_SIZE ? BLOCK_SIZE : len;
		if (!fread(buf, n, 1, file)) {
			LOGE("Failed to read content:%s", entry.path);
			goto end;
		}
		if (!fwrite(buf, n, 1, out_file)) {
			LOGE("Failed to write:%s", entry.path);
			goto end;
		}
		len -= n;
	}
done:
	ret = true;
end:
	if (out_file)
		fclose(out_file);
	if (pos)
		fseek(file, pos, SEEK_SET);
	return ret;
}

static int unpack_image(const char *dir)
{
	FILE *image_file = NULL;
	bool ret = false;
	char unpack_dir[MAX_INDEX_ENTRY_PATH_LEN];
	if (just_print)
		dir = ".";
	snprintf(unpack_dir, sizeof(unpack_dir), "%s", dir);
	if (!strlen(unpack_dir)) {
		goto end;
	} else if (unpack_dir[strlen(unpack_dir) - 1] == '/') {
		unpack_dir[strlen(unpack_dir) - 1] = '\0';
	}

	mkdir(unpack_dir, 0755);
	image_file = fopen(image_path, "rb");
	char buf[BLOCK_SIZE];
	if (!image_file) {
		LOGE("Failed to open:%s", image_path);
		goto end;
	}
	if (!fread(buf, BLOCK_SIZE, 1, image_file)) {
		LOGE("Failed to read header!");
		goto end;
	}
	memcpy(&header, buf, sizeof(header));

	if (memcmp(header.magic, RESOURCE_PTN_HDR_MAGIC, sizeof(header.magic))) {
		LOGE("Not a resource image(%s)!", image_path);
		goto end;
	}
	/* switch for be. */
	fix_header(&header);

	printf("Dump header:\n");
	printf("partition version:%d.%d\n", header.resource_ptn_version,
	       header.index_tbl_version);
	printf("header size:%d\n", header.header_size);
	printf("index tbl:\n\toffset:%d\tentry size:%d\tentry num:%d\n",
	       header.tbl_offset, header.tbl_entry_size, header.tbl_entry_num);

	/* TODO: support header_size & tbl_entry_size */
	if (header.resource_ptn_version != RESOURCE_PTN_VERSION ||
	    header.header_size != RESOURCE_PTN_HDR_SIZE ||
	    header.index_tbl_version != INDEX_TBL_VERSION ||
	    header.tbl_entry_size != INDEX_TBL_ENTR_SIZE) {
		LOGE("Not supported in this version!");
		goto end;
	}

	printf("Dump Index table:\n");
	index_tbl_entry entry;
	int i;
	for (i = 0; i < header.tbl_entry_num; i++) {
		/* TODO: support tbl_entry_size */
		if (!fread(buf, BLOCK_SIZE, 1, image_file)) {
			LOGE("Failed to read index entry:%d!", i);
			goto end;
		}
		memcpy(&entry, buf, sizeof(entry));

		if (memcmp(entry.tag, INDEX_TBL_ENTR_TAG, sizeof(entry.tag))) {
			LOGE("Something wrong with index entry:%d!", i);
			goto end;
		}
		/* switch for be. */
		fix_entry(&entry);

		printf("entry(%d):\n\tpath:%s\n\toffset:%d\tsize:%d\n", i, entry.path,
		       entry.content_offset, entry.content_size);
		if (!dump_file(image_file, unpack_dir, entry)) {
			goto end;
		}
	}
	printf("Unack %s to %s successed!\n", image_path, unpack_dir);
	ret = true;
end:
	if (image_file)
		fclose(image_file);
	return ret ? 0 : -1;
}

/************unpack code end****************/
/************pack code****************/

static inline size_t get_file_size(const char *path)
{
	LOGD("try to get size(%s)...", path);
	struct stat st;
	if (stat(path, &st) < 0) {
		LOGE("Failed to get size:%s", path);
		return -1;
	}
	LOGD("path:%s, size:%ld", path, st.st_size);
	return st.st_size;
}

static int write_file(int offset_block, const char *src_path,
		      char hash[], int hash_size)
{
	LOGD("try to write file(%s) to offset:%d...", src_path, offset_block);
	char *buf = NULL;
	int ret = -1;
	size_t file_size;
	FILE *src_file = fopen(src_path, "rb");
	if (!src_file) {
		LOGE("Failed to open:%s", src_path);
		goto end;
	}

	file_size = get_file_size(src_path);
	if (file_size < 0) {
		goto end;
	}

	buf = calloc(file_size, 1);
	if (!buf)
		goto end;

	if (!fread(buf, file_size, 1, src_file))
		goto end;

	if (!write_data(offset_block, buf, file_size))
		goto end;

	if (hash_size == 20)
		sha1_csum((const unsigned char *)buf, file_size,
			  (unsigned char *)hash);
	else if (hash_size == 32)
		sha256_csum((const unsigned char *)buf, file_size,
			    (unsigned char *)hash);
	else
		goto end;

	ret = file_size;
end:
	if (src_file)
		fclose(src_file);
	if (buf)
		free(buf);

	return ret;
}

static bool write_header(const int file_num)
{
	LOGD("try to write header...");
	memcpy(header.magic, RESOURCE_PTN_HDR_MAGIC, sizeof(header.magic));
	header.resource_ptn_version = RESOURCE_PTN_VERSION;
	header.index_tbl_version = INDEX_TBL_VERSION;
	header.header_size = RESOURCE_PTN_HDR_SIZE;
	header.tbl_offset = header.header_size;
	header.tbl_entry_size = INDEX_TBL_ENTR_SIZE;
	header.tbl_entry_num = file_num;

	/* switch for le. */
	resource_ptn_header hdr = header;
	fix_header(&hdr);
	return write_data(0, &hdr, sizeof(hdr));
}

static bool write_index_tbl(const int file_num, const char **files)
{
	LOGD("try to write index table...");
	bool ret = false;
	bool foundFdt = false;
	int offset =
	        header.header_size + header.tbl_entry_size * header.tbl_entry_num;
	index_tbl_entry entry;
	char hash[20];	/* sha1 */
	int i;

	memcpy(entry.tag, INDEX_TBL_ENTR_TAG, sizeof(entry.tag));
	for (i = 0; i < file_num; i++) {
		size_t file_size = get_file_size(files[i]);
		if (file_size < 0)
			goto end;
		entry.content_size = file_size;
		entry.content_offset = offset;

		if (write_file(offset, files[i], hash, sizeof(hash)) < 0)
			goto end;

		memcpy(entry.hash, hash, sizeof(hash));
		entry.hash_size = sizeof(hash);

		LOGD("try to write index entry(%s)...", files[i]);

		/* switch for le. */
		fix_entry(&entry);
		memset(entry.path, 0, sizeof(entry.path));
		const char *path = files[i];
		if (root_path[0]) {
			if (!strncmp(path, root_path, strlen(root_path))) {
				path += strlen(root_path);
				if (path[0] == '/')
					path++;
			}
		}
		path = fix_path(path);
		if (!strcmp(files[i] + strlen(files[i]) - strlen(DTD_SUBFIX), DTD_SUBFIX)) {
			if (!foundFdt) {
				/* use default path. */
				LOGD("mod fdt path:%s -> %s...", files[i], FDT_PATH);
				path = FDT_PATH;
				foundFdt = true;
			}
		}
		snprintf(entry.path, sizeof(entry.path), "%s", path);
		offset += fix_blocks(file_size);
		if (!write_data(header.header_size + i * header.tbl_entry_size, &entry,
		                sizeof(entry)))
			goto end;
	}
	ret = true;
end:
	return ret;
}

static int pack_image(int file_num, const char **files)
{
	bool ret = false;
	FILE *image_file = fopen(image_path, "wb");
	if (!image_file) {
		LOGE("Failed to create:%s", image_path);
		goto end;
	}
	fclose(image_file);

	/* prepare files */
	int i = 0;
	int pos = 0;
	const char *tmp;
	for (i = 0; i < file_num; i++) {
		if (!strcmp(files[i] + strlen(files[i]) - strlen(DTD_SUBFIX), DTD_SUBFIX)) {
			/* dtb files for kernel. */
			tmp = files[pos];
			files[pos] = files[i];
			files[i] = tmp;
			pos++;
		} else if (!strcmp(fix_path(image_path), fix_path(files[i]))) {
			/* not to pack image itself! */
			tmp = files[file_num - 1];
			files[file_num - 1] = files[i];
			files[i] = tmp;
			file_num--;
		}
	}

	if (!write_header(file_num)) {
		LOGE("Failed to write header!");
		goto end;
	}
	if (!write_index_tbl(file_num, files)) {
		LOGE("Failed to write index table!");
		goto end;
	}
	printf("Pack to %s successed!\n", image_path);
	ret = true;
end:
	return ret ? 0 : -1;
}

/************pack code end****************/
