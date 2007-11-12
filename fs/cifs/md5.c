/*
 * This code implements the MD5 message-digest algorithm.
 * The algorithm is due to Ron Rivest.  This code was
 * written by Colin Plumb in 1993, no copyright is claimed.
 * This code is in the public domain; do with it what you wish.
 *
 * Equivalent code is available from RSA Data Security, Inc.
 * This code has been tested against that, and is equivalent,
 * except that you don't need to include two pages of legalese
 * with every copy.
 *
 * To compute the message digest of a chunk of bytes, declare an
 * MD5Context structure, pass it to MD5Init, call MD5Update as
 * needed on buffers full of bytes, and then call MD5Final, which
 * will fill a supplied 16-byte array with the digest.
 */

/* This code slightly modified to fit into Samba by
   abartlet@samba.org Jun 2001
   and to fit the cifs vfs by
   Steve French sfrench@us.ibm.com */

#include <linux/string.h>
#include "md5.h"

static void MD5Transform(__u32 buf[4], __u32 const in[16]);

/*
 * Note: this code is harmless on little-endian machines.
 */
static void
byteReverse(unsigned char *buf, unsigned longs)
{
	__u32 t;
	do {
		t = (__u32) ((unsigned) buf[3] << 8 | buf[2]) << 16 |
		    ((unsigned) buf[1] << 8 | buf[0]);
		*(__u32 *) buf = t;
		buf += 4;
	} while (--longs);
}

/*
 * Start MD5 accumulation.  Set bit count to 0 and buffer to mysterious
 * initialization constants.
 */
void
MD5Init(struct MD5Context *ctx)
{
	ctx->buf[0] = 0x67452301;
	ctx->buf[1] = 0xefcdab89;
	ctx->buf[2] = 0x98badcfe;
	ctx->buf[3] = 0x10325476;

	ctx->bits[0] = 0;
	ctx->bits[1] = 0;
}

/*
 * Update context to reflect the concatenation of another buffer full
 * of bytes.
 */
void
MD5Update(struct MD5Context *ctx, unsigned char const *buf, unsigned len)
{
	register __u32 t;

	/* Update bitcount */

	t = ctx->bits[0];
	if ((ctx->bits[0] = t + ((__u32) len << 3)) < t)
		ctx->bits[1]++;	/* Carry from low to high */
	ctx->bits[1] += len >> 29;

	t = (t >> 3) & 0x3f;	/* Bytes already in shsInfo->data */

	/* Handle any leading odd-sized chunks */

	if (t) {
		unsigned char *p = (unsigned char *) ctx->in + t;

		t = 64 - t;
		if (len < t) {
			memmove(p, buf, len);
			return;
		}
		memmove(p, buf, t);
		byteReverse(ctx->in, 16);
		MD5Transform(ctx->buf, (__u32 *) ctx->in);
		buf += t;
		len -= t;
	}
	/* Process data in 64-byte chunks */

	while (len >= 64) {
		memmove(ctx->in, buf, 64);
		byteReverse(ctx->in, 16);
		MD5Transform(ctx->buf, (__u32 *) ctx->in);
		buf += 64;
		len -= 64;
	}

	/* Handle any remaining bytes of data. */

	memmove(ctx->in, buf, len);
}

/*
 * Final wrapup - pad to 64-byte boundary with the bit pattern
 * 1 0* (64-bit count of bits processed, MSB-first)
 */
void
MD5Final(unsigned char digest[16], struct MD5Context *ctx)
{
	unsigned int count;
	unsigned char *p;

	/* Compute number of bytes mod 64 */
	count = (ctx->bits[0] >> 3) & 0x3F;

	/* Set the first char of padding to 0x80.  This is safe since there is
	   always at least one byte free */
	p = ctx->in + count;
	*p++ = 0x80;

	/* Bytes of padding needed to make 64 bytes */
	count = 64 - 1 - count;

	/* Pad out to 56 mod 64 */
	if (count < 8) {
		/* Two lots of padding:  Pad the first block to 64 bytes */
		memset(p, 0, count);
		byteReverse(ctx->in, 16);
		MD5Transform(ctx->buf, (__u32 *) ctx->in);

		/* Now fill the next block with 56 bytes */
		memset(ctx->in, 0, 56);
	} else {
		/* Pad block to 56 bytes */
		memset(p, 0, count - 8);
	}
	byteReverse(ctx->in, 14);

	/* Append length in bits and transform */
	((__u32 *) ctx->in)[14] = ctx->bits[0];
	((__u32 *) ctx->in)[15] = ctx->bits[1];

	MD5Transform(ctx->buf, (__u32 *) ctx->in);
	byteReverse((unsigned char *) ctx->buf, 4);
	memmove(digest, ctx->buf, 16);
	memset(ctx, 0, sizeof(*ctx));	/* In case it's sensitive */
}

/* The four core functions - F1 is optimized somewhat */

/* #define F1(x, y, z) (x & y | ~x & z) */
#define F1(x, y, z) (z ^ (x & (y ^ z)))
#define F2(x, y, z) F1(z, x, y)
#define F3(x, y, z) (x ^ y ^ z)
#define F4(x, y, z) (y ^ (x | ~z))

/* This is the central step in the MD5 algorithm. */
#define MD5STEP(f, w, x, y, z, data, s) \
	( w += f(x, y, z) + data,  w = w<<s | w>>(32-s),  w += x )

/*
 * The core of the MD5 algorithm, this alters an existing MD5 hash to
 * reflect the addition of 16 longwords of new data.  MD5Update blocks
 * the data and converts bytes into longwords for this routine.
 */
static void
MD5Transform(__u32 buf[4], __u32 const in[16])
{
	register __u32 a, b, c, d;

	a = buf[0];
	b = buf[1];
	c = buf[2];
	d = buf[3];

	MD5STEP(F1, a, b, c, d, in[0] + 0xd76aa478, 7);
	MD5STEP(F1, d, a, b, c, in[1] + 0xe8c7b756, 12);
	MD5STEP(F1, c, d, a, b, in[2] + 0x242070db, 17);
	MD5STEP(F1, b, c, d, a, in[3] + 0xc1bdceee, 22);
	MD5STEP(F1, a, b, c, d, in[4] + 0xf57c0faf, 7);
	MD5STEP(F1, d, a, b, c, in[5] + 0x4787c62a, 12);
	MD5STEP(F1, c, d, a, b, in[6] + 0xa8304613, 17);
	MD5STEP(F1, b, c, d, a, in[7] + 0xfd469501, 22);
	MD5STEP(F1, a, b, c, d, in[8] + 0x698098d8, 7);
	MD5STEP(F1, d, a, b, c, in[9] + 0x8b44f7af, 12);
	MD5STEP(F1, c, d, a, b, in[10] + 0xffff5bb1, 17);
	MD5STEP(F1, b, c, d, a, in[11] + 0x895cd7be, 22);
	MD5STEP(F1, a, b, c, d, in[12] + 0x6b901122, 7);
	MD5STEP(F1, d, a, b, c, in[13] + 0xfd987193, 12);
	MD5STEP(F1, c, d, a, b, in[14] + 0xa679438e, 17);
	MD5STEP(F1, b, c, d, a, in[15] + 0x49b40821, 22);

	MD5STEP(F2, a, b, c, d, in[1] + 0xf61e2562, 5);
	MD5STEP(F2, d, a, b, c, in[6] + 0xc040b340, 9);
	MD5STEP(F2, c, d, a, b, in[11] + 0x265e5a51, 14);
	MD5STEP(F2, b, c, d, a, in[0] + 0xe9b6c7aa, 20);
	MD5STEP(F2, a, b, c, d, in[5] + 0xd62f105d, 5);
	MD5STEP(F2, d, a, b, c, in[10] + 0x02441453, 9);
	MD5STEP(F2, c, d, a, b, in[15] + 0xd8a1e681, 14);
	MD5STEP(F2, b, c, d, a, in[4] + 0xe7d3fbc8, 20);
	MD5STEP(F2, a, b, c, d, in[9] + 0x21e1cde6, 5);
	MD5STEP(F2, d, a, b, c, in[14] + 0xc33707d6, 9);
	MD5STEP(F2, c, d, a, b, in[3] + 0xf4d50d87, 14);
	MD5STEP(F2, b, c, d, a, in[8] + 0x455a14ed, 20);
	MD5STEP(F2, a, b, c, d, in[13] + 0xa9e3e905, 5);
	MD5STEP(F2, d, a, b, c, in[2] + 0xfcefa3f8, 9);
	MD5STEP(F2, c, d, a, b, in[7] + 0x676f02d9, 14);
	MD5STEP(F2, b, c, d, a, in[12] + 0x8d2a4c8a, 20);

	MD5STEP(F3, a, b, c, d, in[5] + 0xfffa3942, 4);
	MD5STEP(F3, d, a, b, c, in[8] + 0x8771f681, 11);
	MD5STEP(F3, c, d, a, b, in[11] + 0x6d9d6122, 16);
	MD5STEP(F3, b, c, d, a, in[14] + 0xfde5380c, 23);
	MD5STEP(F3, a, b, c, d, in[1] + 0xa4beea44, 4);
	MD5STEP(F3, d, a, b, c, in[4] + 0x4bdecfa9, 11);
	MD5STEP(F3, c, d, a, b, in[7] + 0xf6bb4b60, 16);
	MD5STEP(F3, b, c, d, a, in[10] + 0xbebfbc70, 23);
	MD5STEP(F3, a, b, c, d, in[13] + 0x289b7ec6, 4);
	MD5STEP(F3, d, a, b, c, in[0] + 0xeaa127fa, 11);
	MD5STEP(F3, c, d, a, b, in[3] + 0xd4ef3085, 16);
	MD5STEP(F3, b, c, d, a, in[6] + 0x04881d05, 23);
	MD5STEP(F3, a, b, c, d, in[9] + 0xd9d4d039, 4);
	MD5STEP(F3, d, a, b, c, in[12] + 0xe6db99e5, 11);
	MD5STEP(F3, c, d, a, b, in[15] + 0x1fa27cf8, 16);
	MD5STEP(F3, b, c, d, a, in[2] + 0xc4ac5665, 23);

	MD5STEP(F4, a, b, c, d, in[0] + 0xf4292244, 6);
	MD5STEP(F4, d, a, b, c, in[7] + 0x432aff97, 10);
	MD5STEP(F4, c, d, a, b, in[14] + 0xab9423a7, 15);
	MD5STEP(F4, b, c, d, a, in[5] + 0xfc93a039, 21);
	MD5STEP(F4, a, b, c, d, in[12] + 0x655b59c3, 6);
	MD5STEP(F4, d, a, b, c, in[3] + 0x8f0ccc92, 10);
	MD5STEP(F4, c, d, a, b, in[10] + 0xffeff47d, 15);
	MD5STEP(F4, b, c, d, a, in[1] + 0x85845dd1, 21);
	MD5STEP(F4, a, b, c, d, in[8] + 0x6fa87e4f, 6);
	MD5STEP(F4, d, a, b, c, in[15] + 0xfe2ce6e0, 10);
	MD5STEP(F4, c, d, a, b, in[6] + 0xa3014314, 15);
	MD5STEP(F4, b, c, d, a, in[13] + 0x4e0811a1, 21);
	MD5STEP(F4, a, b, c, d, in[4] + 0xf7537e82, 6);
	MD5STEP(F4, d, a, b, c, in[11] + 0xbd3af235, 10);
	MD5STEP(F4, c, d, a, b, in[2] + 0x2ad7d2bb, 15);
	MD5STEP(F4, b, c, d, a, in[9] + 0xeb86d391, 21);

	buf[0] += a;
	buf[1] += b;
	buf[2] += c;
	buf[3] += d;
}

#if 0   /* currently unused */
/***********************************************************************
 the rfc 2104 version of hmac_md5 initialisation.
***********************************************************************/
static void
hmac_md5_init_rfc2104(unsigned char *key, int key_len,
		      struct HMACMD5Context *ctx)
{
	int i;

	/* if key is longer than 64 bytes reset it to key=MD5(key) */
	if (key_len > 64) {
		unsigned char tk[16];
		struct MD5Context tctx;

		MD5Init(&tctx);
		MD5Update(&tctx, key, key_len);
		MD5Final(tk, &tctx);

		key = tk;
		key_len = 16;
	}

	/* start out by storing key in pads */
	memset(ctx->k_ipad, 0, sizeof(ctx->k_ipad));
	memset(ctx->k_opad, 0, sizeof(ctx->k_opad));
	memcpy(ctx->k_ipad, key, key_len);
	memcpy(ctx->k_opad, key, key_len);

	/* XOR key with ipad and opad values */
	for (i = 0; i < 64; i++) {
		ctx->k_ipad[i] ^= 0x36;
		ctx->k_opad[i] ^= 0x5c;
	}

	MD5Init(&ctx->ctx);
	MD5Update(&ctx->ctx, ctx->k_ipad, 64);
}
#endif

/***********************************************************************
 the microsoft version of hmac_md5 initialisation.
***********************************************************************/
void
hmac_md5_init_limK_to_64(const unsigned char *key, int key_len,
			 struct HMACMD5Context *ctx)
{
	int i;

	/* if key is longer than 64 bytes truncate it */
	if (key_len > 64) {
		key_len = 64;
	}

	/* start out by storing key in pads */
	memset(ctx->k_ipad, 0, sizeof(ctx->k_ipad));
	memset(ctx->k_opad, 0, sizeof(ctx->k_opad));
	memcpy(ctx->k_ipad, key, key_len);
	memcpy(ctx->k_opad, key, key_len);

	/* XOR key with ipad and opad values */
	for (i = 0; i < 64; i++) {
		ctx->k_ipad[i] ^= 0x36;
		ctx->k_opad[i] ^= 0x5c;
	}

	MD5Init(&ctx->ctx);
	MD5Update(&ctx->ctx, ctx->k_ipad, 64);
}

/***********************************************************************
 update hmac_md5 "inner" buffer
***********************************************************************/
void
hmac_md5_update(const unsigned char *text, int text_len,
		struct HMACMD5Context *ctx)
{
	MD5Update(&ctx->ctx, text, text_len);	/* then text of datagram */
}

/***********************************************************************
 finish off hmac_md5 "inner" buffer and generate outer one.
***********************************************************************/
void
hmac_md5_final(unsigned char *digest, struct HMACMD5Context *ctx)
{
	struct MD5Context ctx_o;

	MD5Final(digest, &ctx->ctx);

	MD5Init(&ctx_o);
	MD5Update(&ctx_o, ctx->k_opad, 64);
	MD5Update(&ctx_o, digest, 16);
	MD5Final(digest, &ctx_o);
}

/***********************************************************
 single function to calculate an HMAC MD5 digest from data.
 use the microsoft hmacmd5 init method because the key is 16 bytes.
************************************************************/
#if 0 /* currently unused */
static void
hmac_md5(unsigned char key[16], unsigned char *data, int data_len,
	 unsigned char *digest)
{
	struct HMACMD5Context ctx;
	hmac_md5_init_limK_to_64(key, 16, &ctx);
	if (data_len != 0) {
		hmac_md5_update(data, data_len, &ctx);
	}
	hmac_md5_final(digest, &ctx);
}
#endif
