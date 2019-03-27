/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Copyright 2013 Saso Kiselkov.  All rights reserved.
 * Copyright 2015 Toomas Soome <tsoome@me.com>
 */

/*
 * SHA-256 and SHA-512/256 hashes, as specified in FIPS 180-4, available at:
 * http://csrc.nist.gov/cryptval
 *
 * This is a very compact implementation of SHA-256 and SHA-512/256.
 * It is designed to be simple and portable, not to be fast.
 */

/*
 * The literal definitions according to FIPS180-4 would be:
 *
 * 	Ch(x, y, z)     (((x) & (y)) ^ ((~(x)) & (z)))
 * 	Maj(x, y, z)    (((x) & (y)) | ((x) & (z)) | ((y) & (z)))
 *
 * We use logical equivalents which require one less op.
 */
#define	Ch(x, y, z)	((z) ^ ((x) & ((y) ^ (z))))
#define	Maj(x, y, z)	(((x) & (y)) ^ ((z) & ((x) ^ (y))))
#define	ROTR(x, n)	(((x) >> (n)) | ((x) << ((sizeof (x) * NBBY)-(n))))

/* SHA-224/256 operations */
#define	BIGSIGMA0_256(x)	(ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define	BIGSIGMA1_256(x)	(ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define	SIGMA0_256(x)		(ROTR(x, 7) ^ ROTR(x, 18) ^ ((x) >> 3))
#define	SIGMA1_256(x)		(ROTR(x, 17) ^ ROTR(x, 19) ^ ((x) >> 10))

/* SHA-384/512 operations */
#define	BIGSIGMA0_512(x)	(ROTR((x), 28) ^ ROTR((x), 34) ^ ROTR((x), 39))
#define	BIGSIGMA1_512(x)	(ROTR((x), 14) ^ ROTR((x), 18) ^ ROTR((x), 41))
#define	SIGMA0_512(x)		(ROTR((x), 1) ^ ROTR((x), 8) ^ ((x) >> 7))
#define	SIGMA1_512(x)		(ROTR((x), 19) ^ ROTR((x), 61) ^ ((x) >> 6))

/* SHA-256 round constants */
static const uint32_t SHA256_K[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
	0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
	0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
	0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
	0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
	0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
	0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
	0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
	0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

/* SHA-512 round constants */
static const uint64_t SHA512_K[80] = {
	0x428A2F98D728AE22ULL, 0x7137449123EF65CDULL,
	0xB5C0FBCFEC4D3B2FULL, 0xE9B5DBA58189DBBCULL,
	0x3956C25BF348B538ULL, 0x59F111F1B605D019ULL,
	0x923F82A4AF194F9BULL, 0xAB1C5ED5DA6D8118ULL,
	0xD807AA98A3030242ULL, 0x12835B0145706FBEULL,
	0x243185BE4EE4B28CULL, 0x550C7DC3D5FFB4E2ULL,
	0x72BE5D74F27B896FULL, 0x80DEB1FE3B1696B1ULL,
	0x9BDC06A725C71235ULL, 0xC19BF174CF692694ULL,
	0xE49B69C19EF14AD2ULL, 0xEFBE4786384F25E3ULL,
	0x0FC19DC68B8CD5B5ULL, 0x240CA1CC77AC9C65ULL,
	0x2DE92C6F592B0275ULL, 0x4A7484AA6EA6E483ULL,
	0x5CB0A9DCBD41FBD4ULL, 0x76F988DA831153B5ULL,
	0x983E5152EE66DFABULL, 0xA831C66D2DB43210ULL,
	0xB00327C898FB213FULL, 0xBF597FC7BEEF0EE4ULL,
	0xC6E00BF33DA88FC2ULL, 0xD5A79147930AA725ULL,
	0x06CA6351E003826FULL, 0x142929670A0E6E70ULL,
	0x27B70A8546D22FFCULL, 0x2E1B21385C26C926ULL,
	0x4D2C6DFC5AC42AEDULL, 0x53380D139D95B3DFULL,
	0x650A73548BAF63DEULL, 0x766A0ABB3C77B2A8ULL,
	0x81C2C92E47EDAEE6ULL, 0x92722C851482353BULL,
	0xA2BFE8A14CF10364ULL, 0xA81A664BBC423001ULL,
	0xC24B8B70D0F89791ULL, 0xC76C51A30654BE30ULL,
	0xD192E819D6EF5218ULL, 0xD69906245565A910ULL,
	0xF40E35855771202AULL, 0x106AA07032BBD1B8ULL,
	0x19A4C116B8D2D0C8ULL, 0x1E376C085141AB53ULL,
	0x2748774CDF8EEB99ULL, 0x34B0BCB5E19B48A8ULL,
	0x391C0CB3C5C95A63ULL, 0x4ED8AA4AE3418ACBULL,
	0x5B9CCA4F7763E373ULL, 0x682E6FF3D6B2B8A3ULL,
	0x748F82EE5DEFB2FCULL, 0x78A5636F43172F60ULL,
	0x84C87814A1F0AB72ULL, 0x8CC702081A6439ECULL,
	0x90BEFFFA23631E28ULL, 0xA4506CEBDE82BDE9ULL,
	0xBEF9A3F7B2C67915ULL, 0xC67178F2E372532BULL,
	0xCA273ECEEA26619CULL, 0xD186B8C721C0C207ULL,
	0xEADA7DD6CDE0EB1EULL, 0xF57D4F7FEE6ED178ULL,
	0x06F067AA72176FBAULL, 0x0A637DC5A2C898A6ULL,
	0x113F9804BEF90DAEULL, 0x1B710B35131C471BULL,
	0x28DB77F523047D84ULL, 0x32CAAB7B40C72493ULL,
	0x3C9EBE0A15C9BEBCULL, 0x431D67C49C100D4CULL,
	0x4CC5D4BECB3E42B6ULL, 0x597F299CFC657E2AULL,
	0x5FCB6FAB3AD6FAECULL, 0x6C44198C4A475817ULL
};

static void
SHA256Transform(uint32_t *H, const uint8_t *cp)
{
	uint32_t a, b, c, d, e, f, g, h, t, T1, T2, W[64];

	/* copy chunk into the first 16 words of the message schedule */
	for (t = 0; t < 16; t++, cp += sizeof (uint32_t))
		W[t] = (cp[0] << 24) | (cp[1] << 16) | (cp[2] << 8) | cp[3];

	/* extend the first 16 words into the remaining 48 words */
	for (t = 16; t < 64; t++)
		W[t] = SIGMA1_256(W[t - 2]) + W[t - 7] +
		    SIGMA0_256(W[t - 15]) + W[t - 16];

	/* init working variables to the current hash value */
	a = H[0]; b = H[1]; c = H[2]; d = H[3];
	e = H[4]; f = H[5]; g = H[6]; h = H[7];

	/* iterate the compression function for all rounds of the hash */
	for (t = 0; t < 64; t++) {
		T1 = h + BIGSIGMA1_256(e) + Ch(e, f, g) + SHA256_K[t] + W[t];
		T2 = BIGSIGMA0_256(a) + Maj(a, b, c);
		h = g; g = f; f = e; e = d + T1;
		d = c; c = b; b = a; a = T1 + T2;
	}

	/* add the compressed chunk to the current hash value */
	H[0] += a; H[1] += b; H[2] += c; H[3] += d;
	H[4] += e; H[5] += f; H[6] += g; H[7] += h;
}

static void
SHA512Transform(uint64_t *H, const uint8_t *cp)
{
	uint64_t a, b, c, d, e, f, g, h, t, T1, T2, W[80];

	/* copy chunk into the first 16 words of the message schedule */
	for (t = 0; t < 16; t++, cp += sizeof (uint64_t))
		W[t] = ((uint64_t)cp[0] << 56) | ((uint64_t)cp[1] << 48) |
		    ((uint64_t)cp[2] << 40) | ((uint64_t)cp[3] << 32) |
		    ((uint64_t)cp[4] << 24) | ((uint64_t)cp[5] << 16) |
		    ((uint64_t)cp[6] << 8) | (uint64_t)cp[7];

	/* extend the first 16 words into the remaining 64 words */
	for (t = 16; t < 80; t++)
		W[t] = SIGMA1_512(W[t - 2]) + W[t - 7] +
		    SIGMA0_512(W[t - 15]) + W[t - 16];

	/* init working variables to the current hash value */
	a = H[0]; b = H[1]; c = H[2]; d = H[3];
	e = H[4]; f = H[5]; g = H[6]; h = H[7];

	/* iterate the compression function for all rounds of the hash */
	for (t = 0; t < 80; t++) {
		T1 = h + BIGSIGMA1_512(e) + Ch(e, f, g) + SHA512_K[t] + W[t];
		T2 = BIGSIGMA0_512(a) + Maj(a, b, c);
		h = g; g = f; f = e; e = d + T1;
		d = c; c = b; b = a; a = T1 + T2;
	}

	/* add the compressed chunk to the current hash value */
	H[0] += a; H[1] += b; H[2] += c; H[3] += d;
	H[4] += e; H[5] += f; H[6] += g; H[7] += h;
}

/*
 * Implements the SHA-224 and SHA-256 hash algos - to select between them
 * pass the appropriate initial values of 'H' and truncate the last 32 bits
 * in case of SHA-224.
 */
static void
SHA256(uint32_t *H, const void *buf, uint64_t size, zio_cksum_t *zcp)
{
	uint8_t pad[128];
	unsigned padsize = size & 63;
	unsigned i, k;

	/* process all blocks up to the last one */
	for (i = 0; i < size - padsize; i += 64)
		SHA256Transform(H, (uint8_t *)buf + i);

	/* process the last block and padding */
	for (k = 0; k < padsize; k++)
		pad[k] = ((uint8_t *)buf)[k+i];

	for (pad[padsize++] = 0x80; (padsize & 63) != 56; padsize++)
		pad[padsize] = 0;

	for (i = 0; i < 8; i++)
		pad[padsize++] = (size << 3) >> (56 - 8 * i);

	for (i = 0; i < padsize; i += 64)
		SHA256Transform(H, pad + i);

	ZIO_SET_CHECKSUM(zcp,
	    (uint64_t)H[0] << 32 | H[1],
	    (uint64_t)H[2] << 32 | H[3],
	    (uint64_t)H[4] << 32 | H[5],
	    (uint64_t)H[6] << 32 | H[7]);
}

/*
 * encode 64bit data in big-endian format.
 */
static void
Encode64(uint8_t *output, uint64_t *input, size_t len)
{
	size_t i, j;
	for (i = 0, j = 0; j < len; i++, j += 8) {
		output[j]	= (input[i] >> 56) & 0xff;
		output[j + 1]	= (input[i] >> 48) & 0xff;
		output[j + 2]	= (input[i] >> 40) & 0xff;
		output[j + 3]	= (input[i] >> 32) & 0xff;
		output[j + 4]	= (input[i] >> 24) & 0xff;
		output[j + 5]	= (input[i] >> 16) & 0xff;
		output[j + 6]	= (input[i] >>  8) & 0xff;
		output[j + 7]	= input[i] & 0xff;
	}
}

/*
 * Implements the SHA-384, SHA-512 and SHA-512/t hash algos - to select
 * between them pass the appropriate initial values for 'H'. The output
 * of this function is truncated to the first 256 bits that fit into 'zcp'.
 */
static void
SHA512(uint64_t *H, const void *buf, uint64_t size, zio_cksum_t *zcp)
{
	uint64_t	c64[2];
	uint8_t		pad[256];
	unsigned	padsize = size & 127;
	unsigned	i, k;

	/* process all blocks up to the last one */
	for (i = 0; i < size - padsize; i += 128)
		SHA512Transform(H, (uint8_t *)buf + i);

	/* process the last block and padding */
	for (k = 0; k < padsize; k++)
		pad[k] = ((uint8_t *)buf)[k+i];

	if (padsize < 112) {
		for (pad[padsize++] = 0x80; padsize < 112; padsize++)
			pad[padsize] = 0;
	} else {
		for (pad[padsize++] = 0x80; padsize < 240; padsize++)
			pad[padsize] = 0;
	}

	c64[0] = 0;
	c64[1] = size << 3;
	Encode64(pad+padsize, c64, sizeof (c64));
	padsize += sizeof (c64);

	for (i = 0; i < padsize; i += 128)
		SHA512Transform(H, pad + i);

	/* truncate the output to the first 256 bits which fit into 'zcp' */
	Encode64((uint8_t *)zcp, H, sizeof (uint64_t) * 4);
}

static void
zio_checksum_SHA256(const void *buf, uint64_t size,
    const void *ctx_template __unused, zio_cksum_t *zcp)
{
	/* SHA-256 as per FIPS 180-4. */
	uint32_t	H[] = {
		0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
		0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
	};
	SHA256(H, buf, size, zcp);
}

static void
zio_checksum_SHA512_native(const void *buf, uint64_t size,
    const void *ctx_template __unused, zio_cksum_t *zcp)
{
	/* SHA-512/256 as per FIPS 180-4. */
	uint64_t	H[] = {
		0x22312194FC2BF72CULL, 0x9F555FA3C84C64C2ULL,
		0x2393B86B6F53B151ULL, 0x963877195940EABDULL,
		0x96283EE2A88EFFE3ULL, 0xBE5E1E2553863992ULL,
		0x2B0199FC2C85B8AAULL, 0x0EB72DDC81C52CA2ULL
	};
	SHA512(H, buf, size, zcp);
}

static void
zio_checksum_SHA512_byteswap(const void *buf, uint64_t size,
    const void *ctx_template, zio_cksum_t *zcp)
{
	zio_cksum_t	tmp;

	zio_checksum_SHA512_native(buf, size, ctx_template, &tmp);
	zcp->zc_word[0] = BSWAP_64(tmp.zc_word[0]);
	zcp->zc_word[1] = BSWAP_64(tmp.zc_word[1]);
	zcp->zc_word[2] = BSWAP_64(tmp.zc_word[2]);
	zcp->zc_word[3] = BSWAP_64(tmp.zc_word[3]);
}
