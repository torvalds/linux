/*-
 * SPDX-License-Identifier: (BSD-2-Clause-FreeBSD AND RSA-MD)
 *
 * Copyright (c) 2010, 2013 Zheng Liu <lz@freebsd.org>
 * Copyright (c) 2012, Vyacheslav Matyushin
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * The following notice applies to the code in ext2_half_md4():
 *
 * Copyright (C) 1990-2, RSA Data Security, Inc. All rights reserved.
 *
 * License to copy and use this software is granted provided that it
 * is identified as the "RSA Data Security, Inc. MD4 Message-Digest
 * Algorithm" in all material mentioning or referencing this software
 * or this function.
 *
 * License is also granted to make and use derivative works provided
 * that such works are identified as "derived from the RSA Data
 * Security, Inc. MD4 Message-Digest Algorithm" in all material
 * mentioning or referencing the derived work.
 *
 * RSA Data Security, Inc. makes no representations concerning either
 * the merchantability of this software or the suitability of this
 * software for any particular purpose. It is provided "as is"
 * without express or implied warranty of any kind.
 *
 * These notices must be retained in any copies of any part of this
 * documentation and/or software.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/vnode.h>
#include <sys/stat.h>
#include <sys/mount.h>

#include <fs/ext2fs/ext2fs.h>
#include <fs/ext2fs/fs.h>
#include <fs/ext2fs/htree.h>
#include <fs/ext2fs/inode.h>
#include <fs/ext2fs/ext2_mount.h>
#include <fs/ext2fs/ext2_extern.h>

/* F, G, and H are MD4 functions */
#define F(x, y, z) (((x) & (y)) | ((~x) & (z)))
#define G(x, y, z) (((x) & (y)) | ((x) & (z)) | ((y) & (z)))
#define H(x, y, z) ((x) ^ (y) ^ (z))

/* ROTATE_LEFT rotates x left n bits */
#define ROTATE_LEFT(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

/*
 * FF, GG, and HH are transformations for rounds 1, 2, and 3.
 * Rotation is separated from addition to prevent recomputation.
 */
#define FF(a, b, c, d, x, s) { \
	(a) += F ((b), (c), (d)) + (x); \
	(a) = ROTATE_LEFT ((a), (s)); \
}

#define GG(a, b, c, d, x, s) { \
	(a) += G ((b), (c), (d)) + (x) + (uint32_t)0x5A827999; \
	(a) = ROTATE_LEFT ((a), (s)); \
}

#define HH(a, b, c, d, x, s) { \
	(a) += H ((b), (c), (d)) + (x) + (uint32_t)0x6ED9EBA1; \
	(a) = ROTATE_LEFT ((a), (s)); \
}

/*
 * MD4 basic transformation.  It transforms state based on block.
 *
 * This is a half md4 algorithm since Linux uses this algorithm for dir
 * index.  This function is derived from the RSA Data Security, Inc. MD4
 * Message-Digest Algorithm and was modified as necessary.
 *
 * The return value of this function is uint32_t in Linux, but actually we don't
 * need to check this value, so in our version this function doesn't return any
 * value.
 */
static void
ext2_half_md4(uint32_t hash[4], uint32_t data[8])
{
	uint32_t a = hash[0], b = hash[1], c = hash[2], d = hash[3];

	/* Round 1 */
	FF(a, b, c, d, data[0],  3);
	FF(d, a, b, c, data[1],  7);
	FF(c, d, a, b, data[2], 11);
	FF(b, c, d, a, data[3], 19);
	FF(a, b, c, d, data[4],  3);
	FF(d, a, b, c, data[5],  7);
	FF(c, d, a, b, data[6], 11);
	FF(b, c, d, a, data[7], 19);

	/* Round 2 */
	GG(a, b, c, d, data[1],  3);
	GG(d, a, b, c, data[3],  5);
	GG(c, d, a, b, data[5],  9);
	GG(b, c, d, a, data[7], 13);
	GG(a, b, c, d, data[0],  3);
	GG(d, a, b, c, data[2],  5);
	GG(c, d, a, b, data[4],  9);
	GG(b, c, d, a, data[6], 13);

	/* Round 3 */
	HH(a, b, c, d, data[3],  3);
	HH(d, a, b, c, data[7],  9);
	HH(c, d, a, b, data[2], 11);
	HH(b, c, d, a, data[6], 15);
	HH(a, b, c, d, data[1],  3);
	HH(d, a, b, c, data[5],  9);
	HH(c, d, a, b, data[0], 11);
	HH(b, c, d, a, data[4], 15);

	hash[0] += a;
	hash[1] += b;
	hash[2] += c;
	hash[3] += d;
}

/*
 * Tiny Encryption Algorithm.
 */
static void
ext2_tea(uint32_t hash[4], uint32_t data[8])
{
	uint32_t tea_delta = 0x9E3779B9;
	uint32_t sum;
	uint32_t x = hash[0], y = hash[1];
	int n = 16;
	int i = 1;

	while (n-- > 0) {
		sum = i * tea_delta;
		x += ((y << 4) + data[0]) ^ (y + sum) ^ ((y >> 5) + data[1]);
		y += ((x << 4) + data[2]) ^ (x + sum) ^ ((x >> 5) + data[3]);
		i++;
	}

	hash[0] += x;
	hash[1] += y;
}

static uint32_t
ext2_legacy_hash(const char *name, int len, int unsigned_char)
{
	uint32_t h0, h1 = 0x12A3FE2D, h2 = 0x37ABE8F9;
	uint32_t multi = 0x6D22F5;
	const unsigned char *uname = (const unsigned char *)name;
	const signed char *sname = (const signed char *)name;
	int val, i;

	for (i = 0; i < len; i++) {
		if (unsigned_char)
			val = (u_int)*uname++;
		else
			val = (int)*sname++;

		h0 = h2 + (h1 ^ (val * multi));
		if (h0 & 0x80000000)
			h0 -= 0x7FFFFFFF;
		h2 = h1;
		h1 = h0;
	}

	return (h1 << 1);
}

static void
ext2_prep_hashbuf(const char *src, int slen, uint32_t *dst, int dlen,
    int unsigned_char)
{
	uint32_t padding = slen | (slen << 8) | (slen << 16) | (slen << 24);
	uint32_t buf_val;
	const unsigned char *ubuf = (const unsigned char *)src;
	const signed char *sbuf = (const signed char *)src;
	int len, i;
	int buf_byte;

	if (slen > dlen)
		len = dlen;
	else
		len = slen;

	buf_val = padding;

	for (i = 0; i < len; i++) {
		if (unsigned_char)
			buf_byte = (u_int)ubuf[i];
		else
			buf_byte = (int)sbuf[i];

		if ((i % 4) == 0)
			buf_val = padding;

		buf_val <<= 8;
		buf_val += buf_byte;

		if ((i % 4) == 3) {
			*dst++ = buf_val;
			dlen -= sizeof(uint32_t);
			buf_val = padding;
		}
	}

	dlen -= sizeof(uint32_t);
	if (dlen >= 0)
		*dst++ = buf_val;

	dlen -= sizeof(uint32_t);
	while (dlen >= 0) {
		*dst++ = padding;
		dlen -= sizeof(uint32_t);
	}
}

int
ext2_htree_hash(const char *name, int len,
    uint32_t *hash_seed, int hash_version,
    uint32_t *hash_major, uint32_t *hash_minor)
{
	uint32_t hash[4];
	uint32_t data[8];
	uint32_t major = 0, minor = 0;
	int unsigned_char = 0;

	if (!name || !hash_major)
		return (-1);

	if (len < 1 || len > 255)
		goto error;

	hash[0] = 0x67452301;
	hash[1] = 0xEFCDAB89;
	hash[2] = 0x98BADCFE;
	hash[3] = 0x10325476;

	if (hash_seed)
		memcpy(hash, hash_seed, sizeof(hash));

	switch (hash_version) {
	case EXT2_HTREE_TEA_UNSIGNED:
		unsigned_char = 1;
		/* FALLTHROUGH */
	case EXT2_HTREE_TEA:
		while (len > 0) {
			ext2_prep_hashbuf(name, len, data, 16, unsigned_char);
			ext2_tea(hash, data);
			len -= 16;
			name += 16;
		}
		major = hash[0];
		minor = hash[1];
		break;
	case EXT2_HTREE_LEGACY_UNSIGNED:
		unsigned_char = 1;
		/* FALLTHROUGH */
	case EXT2_HTREE_LEGACY:
		major = ext2_legacy_hash(name, len, unsigned_char);
		break;
	case EXT2_HTREE_HALF_MD4_UNSIGNED:
		unsigned_char = 1;
		/* FALLTHROUGH */
	case EXT2_HTREE_HALF_MD4:
		while (len > 0) {
			ext2_prep_hashbuf(name, len, data, 32, unsigned_char);
			ext2_half_md4(hash, data);
			len -= 32;
			name += 32;
		}
		major = hash[1];
		minor = hash[2];
		break;
	default:
		goto error;
	}

	major &= ~1;
	if (major == (EXT2_HTREE_EOF << 1))
		major = (EXT2_HTREE_EOF - 1) << 1;
	*hash_major = major;
	if (hash_minor)
		*hash_minor = minor;

	return (0);

error:
	*hash_major = 0;
	if (hash_minor)
		*hash_minor = 0;
	return (-1);
}
