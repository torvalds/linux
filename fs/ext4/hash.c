// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/ext4/hash.c
 *
 * Copyright (C) 2002 by Theodore Ts'o
 */

#include <linux/fs.h>
#include <linux/unicode.h>
#include <linux/compiler.h>
#include <linux/bitops.h>
#include <linux/unaligned.h>
#include "ext4.h"

#define DELTA 0x9E3779B9

static void TEA_transform(__u32 buf[4], __u32 const in[])
{
	__u32	sum = 0;
	__u32	b0 = buf[0], b1 = buf[1];
	__u32	a = in[0], b = in[1], c = in[2], d = in[3];
	int	n = 16;

	do {
		sum += DELTA;
		b0 += ((b1 << 4)+a) ^ (b1+sum) ^ ((b1 >> 5)+b);
		b1 += ((b0 << 4)+c) ^ (b0+sum) ^ ((b0 >> 5)+d);
	} while (--n);

	buf[0] += b0;
	buf[1] += b1;
}

/* F, G and H are basic MD4 functions: selection, majority, parity */
#define F(x, y, z) ((z) ^ ((x) & ((y) ^ (z))))
#define G(x, y, z) (((x) & (y)) + (((x) ^ (y)) & (z)))
#define H(x, y, z) ((x) ^ (y) ^ (z))

/*
 * The generic round function.  The application is so specific that
 * we don't bother protecting all the arguments with parens, as is generally
 * good macro practice, in favor of extra legibility.
 * Rotation is separate from addition to prevent recomputation
 */
#define ROUND(f, a, b, c, d, x, s)	\
	(a += f(b, c, d) + x, a = rol32(a, s))
#define K1 0
#define K2 013240474631UL
#define K3 015666365641UL

/*
 * Basic cut-down MD4 transform.  Returns only 32 bits of result.
 */
static __u32 half_md4_transform(__u32 buf[4], __u32 const in[8])
{
	__u32 a = buf[0], b = buf[1], c = buf[2], d = buf[3];

	/* Round 1 */
	ROUND(F, a, b, c, d, in[0] + K1,  3);
	ROUND(F, d, a, b, c, in[1] + K1,  7);
	ROUND(F, c, d, a, b, in[2] + K1, 11);
	ROUND(F, b, c, d, a, in[3] + K1, 19);
	ROUND(F, a, b, c, d, in[4] + K1,  3);
	ROUND(F, d, a, b, c, in[5] + K1,  7);
	ROUND(F, c, d, a, b, in[6] + K1, 11);
	ROUND(F, b, c, d, a, in[7] + K1, 19);

	/* Round 2 */
	ROUND(G, a, b, c, d, in[1] + K2,  3);
	ROUND(G, d, a, b, c, in[3] + K2,  5);
	ROUND(G, c, d, a, b, in[5] + K2,  9);
	ROUND(G, b, c, d, a, in[7] + K2, 13);
	ROUND(G, a, b, c, d, in[0] + K2,  3);
	ROUND(G, d, a, b, c, in[2] + K2,  5);
	ROUND(G, c, d, a, b, in[4] + K2,  9);
	ROUND(G, b, c, d, a, in[6] + K2, 13);

	/* Round 3 */
	ROUND(H, a, b, c, d, in[3] + K3,  3);
	ROUND(H, d, a, b, c, in[7] + K3,  9);
	ROUND(H, c, d, a, b, in[2] + K3, 11);
	ROUND(H, b, c, d, a, in[6] + K3, 15);
	ROUND(H, a, b, c, d, in[1] + K3,  3);
	ROUND(H, d, a, b, c, in[5] + K3,  9);
	ROUND(H, c, d, a, b, in[0] + K3, 11);
	ROUND(H, b, c, d, a, in[4] + K3, 15);

	buf[0] += a;
	buf[1] += b;
	buf[2] += c;
	buf[3] += d;

	return buf[1]; /* "most hashed" word */
}
#undef ROUND
#undef K1
#undef K2
#undef K3
#undef F
#undef G
#undef H

/* The old legacy hash */
static __u32 dx_hack_hash_unsigned(const char *name, int len)
{
	__u32 hash, hash0 = 0x12a3fe2d, hash1 = 0x37abe8f9;
	const unsigned char *ucp = (const unsigned char *) name;

	while (len--) {
		hash = hash1 + (hash0 ^ (((int) *ucp++) * 7152373));

		if (hash & 0x80000000)
			hash -= 0x7fffffff;
		hash1 = hash0;
		hash0 = hash;
	}
	return hash0 << 1;
}

static __u32 dx_hack_hash_signed(const char *name, int len)
{
	__u32 hash, hash0 = 0x12a3fe2d, hash1 = 0x37abe8f9;
	const signed char *scp = (const signed char *) name;

	while (len--) {
		hash = hash1 + (hash0 ^ (((int) *scp++) * 7152373));

		if (hash & 0x80000000)
			hash -= 0x7fffffff;
		hash1 = hash0;
		hash0 = hash;
	}
	return hash0 << 1;
}

static void str2hashbuf_signed(const char *msg, int len, __u32 *buf, int num)
{
	__u32	pad, val;
	int	i;
	const signed char *scp = (const signed char *) msg;

	pad = (__u32)len | ((__u32)len << 8);
	pad |= pad << 16;

	if (len > num*4)
		len = num * 4;

	while (len >= 4) {
		val = ((__u32)scp[0] << 24) + ((__u32)scp[1] << 16) + ((__u32)scp[2] << 8) + scp[3];
		*buf++ = val;
		scp += 4;
		len -= 4;
		num--;
	}

	val = pad;

	for (i = 0; i < len; i++)
		val = scp[i] + (val << 8);

	if (--num >= 0)
		*buf++ = val;

	while (--num >= 0)
		*buf++ = pad;

}

static void str2hashbuf_unsigned(const char *msg, int len, __u32 *buf, int num)
{
	__u32	pad, val;
	int	i;
	const unsigned char *ucp = (const unsigned char *) msg;

	pad = (__u32)len | ((__u32)len << 8);
	pad |= pad << 16;

	if (len > num*4)
		len = num * 4;

	while (len >= 4) {
		val = get_unaligned_be32(ucp);
		*buf++ = val;
		ucp += 4;
		len -= 4;
		num--;
	}

	val = pad;

	for (i = 0; i < len; i++)
		val = ucp[i] + (val << 8);

	if (--num >= 0)
		*buf++ = val;

	while (--num >= 0)
		*buf++ = pad;

}

/*
 * Returns the hash of a filename.  If len is 0 and name is NULL, then
 * this function can be used to test whether or not a hash version is
 * supported.
 *
 * The seed is an 4 longword (32 bits) "secret" which can be used to
 * uniquify a hash.  If the seed is all zero's, then some default seed
 * may be used.
 *
 * A particular hash version specifies whether or not the seed is
 * represented, and whether or not the returned hash is 32 bits or 64
 * bits.  32 bit hashes will return 0 for the minor hash.
 */
static int __ext4fs_dirhash(const struct inode *dir, const char *name, int len,
			    struct dx_hash_info *hinfo)
{
	__u32	hash;
	__u32	minor_hash = 0;
	const char	*p;
	int		i;
	__u32		in[8], buf[4];
	bool use_unsigned = false;

	/* Initialize the default seed for the hash checksum functions */
	buf[0] = 0x67452301;
	buf[1] = 0xefcdab89;
	buf[2] = 0x98badcfe;
	buf[3] = 0x10325476;

	/* Check to see if the seed is all zero's */
	if (hinfo->seed) {
		for (i = 0; i < 4; i++) {
			if (hinfo->seed[i]) {
				memcpy(buf, hinfo->seed, sizeof(buf));
				break;
			}
		}
	}

	switch (hinfo->hash_version) {
	case DX_HASH_LEGACY_UNSIGNED:
		hash = dx_hack_hash_unsigned(name, len);
		break;
	case DX_HASH_LEGACY:
		hash = dx_hack_hash_signed(name, len);
		break;
	case DX_HASH_HALF_MD4_UNSIGNED:
		use_unsigned = true;
		fallthrough;
	case DX_HASH_HALF_MD4:
		p = name;
		while (len > 0) {
			if (use_unsigned)
				str2hashbuf_unsigned(p, len, in, 8);
			else
				str2hashbuf_signed(p, len, in, 8);
			half_md4_transform(buf, in);
			len -= 32;
			p += 32;
		}
		minor_hash = buf[2];
		hash = buf[1];
		break;
	case DX_HASH_TEA_UNSIGNED:
		use_unsigned = true;
		fallthrough;
	case DX_HASH_TEA:
		p = name;
		while (len > 0) {
			if (use_unsigned)
				str2hashbuf_unsigned(p, len, in, 4);
			else
				str2hashbuf_signed(p, len, in, 4);
			TEA_transform(buf, in);
			len -= 16;
			p += 16;
		}
		hash = buf[0];
		minor_hash = buf[1];
		break;
	case DX_HASH_SIPHASH:
	{
		struct qstr qname = QSTR_INIT(name, len);
		__u64	combined_hash;

		if (fscrypt_has_encryption_key(dir)) {
			combined_hash = fscrypt_fname_siphash(dir, &qname);
		} else {
			ext4_warning_inode(dir, "Siphash requires key");
			return -EINVAL;
		}

		hash = (__u32)(combined_hash >> 32);
		minor_hash = (__u32)combined_hash;
		break;
	}
	default:
		hinfo->hash = 0;
		hinfo->minor_hash = 0;
		ext4_warning(dir->i_sb,
			     "invalid/unsupported hash tree version %u",
			     hinfo->hash_version);
		return -EINVAL;
	}
	hash = hash & ~1;
	if (hash == (EXT4_HTREE_EOF_32BIT << 1))
		hash = (EXT4_HTREE_EOF_32BIT - 1) << 1;
	hinfo->hash = hash;
	hinfo->minor_hash = minor_hash;
	return 0;
}

int ext4fs_dirhash(const struct inode *dir, const char *name, int len,
		   struct dx_hash_info *hinfo)
{
#if IS_ENABLED(CONFIG_UNICODE)
	const struct unicode_map *um = dir->i_sb->s_encoding;
	int r, dlen;
	unsigned char *buff;
	struct qstr qstr = {.name = name, .len = len };

	if (len && IS_CASEFOLDED(dir) &&
	   (!IS_ENCRYPTED(dir) || fscrypt_has_encryption_key(dir))) {
		buff = kzalloc(PATH_MAX, GFP_KERNEL);
		if (!buff)
			return -ENOMEM;

		dlen = utf8_casefold(um, &qstr, buff, PATH_MAX);
		if (dlen < 0) {
			kfree(buff);
			goto opaque_seq;
		}

		r = __ext4fs_dirhash(dir, buff, dlen, hinfo);

		kfree(buff);
		return r;
	}
opaque_seq:
#endif
	return __ext4fs_dirhash(dir, name, len, hinfo);
}

#if IS_ENABLED(CONFIG_EXT4_KUNIT_TESTS)
EXPORT_SYMBOL_FOR_EXT4_TEST(ext4fs_dirhash);
#endif
