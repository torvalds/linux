/*
 *  linux/fs/ext3/hash.c
 *
 * Copyright (C) 2002 by Theodore Ts'o
 *
 * This file is released under the GPL v2.
 *
 * This file may be redistributed under the terms of the GNU Public
 * License.
 */

#include <linux/fs.h>
#include <linux/jbd.h>
#include <linux/ext3_fs.h>
#include <linux/cryptohash.h>

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
	} while(--n);

	buf[0] += b0;
	buf[1] += b1;
}


/* The old legacy hash */
static __u32 dx_hack_hash (const char *name, int len)
{
	__u32 hash0 = 0x12a3fe2d, hash1 = 0x37abe8f9;
	while (len--) {
		__u32 hash = hash1 + (hash0 ^ (*name++ * 7152373));

		if (hash & 0x80000000) hash -= 0x7fffffff;
		hash1 = hash0;
		hash0 = hash;
	}
	return (hash0 << 1);
}

static void str2hashbuf(const char *msg, int len, __u32 *buf, int num)
{
	__u32	pad, val;
	int	i;

	pad = (__u32)len | ((__u32)len << 8);
	pad |= pad << 16;

	val = pad;
	if (len > num*4)
		len = num * 4;
	for (i=0; i < len; i++) {
		if ((i % 4) == 0)
			val = pad;
		val = msg[i] + (val << 8);
		if ((i % 4) == 3) {
			*buf++ = val;
			val = pad;
			num--;
		}
	}
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
int ext3fs_dirhash(const char *name, int len, struct dx_hash_info *hinfo)
{
	__u32	hash;
	__u32	minor_hash = 0;
	const char	*p;
	int		i;
	__u32		in[8], buf[4];

	/* Initialize the default seed for the hash checksum functions */
	buf[0] = 0x67452301;
	buf[1] = 0xefcdab89;
	buf[2] = 0x98badcfe;
	buf[3] = 0x10325476;

	/* Check to see if the seed is all zero's */
	if (hinfo->seed) {
		for (i=0; i < 4; i++) {
			if (hinfo->seed[i])
				break;
		}
		if (i < 4)
			memcpy(buf, hinfo->seed, sizeof(buf));
	}

	switch (hinfo->hash_version) {
	case DX_HASH_LEGACY:
		hash = dx_hack_hash(name, len);
		break;
	case DX_HASH_HALF_MD4:
		p = name;
		while (len > 0) {
			str2hashbuf(p, len, in, 8);
			half_md4_transform(buf, in);
			len -= 32;
			p += 32;
		}
		minor_hash = buf[2];
		hash = buf[1];
		break;
	case DX_HASH_TEA:
		p = name;
		while (len > 0) {
			str2hashbuf(p, len, in, 4);
			TEA_transform(buf, in);
			len -= 16;
			p += 16;
		}
		hash = buf[0];
		minor_hash = buf[1];
		break;
	default:
		hinfo->hash = 0;
		return -1;
	}
	hash = hash & ~1;
	if (hash == (EXT3_HTREE_EOF << 1))
		hash = (EXT3_HTREE_EOF-1) << 1;
	hinfo->hash = hash;
	hinfo->minor_hash = minor_hash;
	return 0;
}
