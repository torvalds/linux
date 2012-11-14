/**
 * fs/f2fs/hash.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 *
 * Portions of this code from linux/fs/ext3/hash.c
 *
 * Copyright (C) 2002 by Theodore Ts'o
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/f2fs_fs.h>
#include <linux/cryptohash.h>
#include <linux/pagemap.h>

#include "f2fs.h"

/*
 * Hashing code copied from ext3
 */
#define DELTA 0x9E3779B9

static void TEA_transform(unsigned int buf[4], unsigned int const in[])
{
	__u32 sum = 0;
	__u32 b0 = buf[0], b1 = buf[1];
	__u32 a = in[0], b = in[1], c = in[2], d = in[3];
	int n = 16;

	do {
		sum += DELTA;
		b0 += ((b1 << 4)+a) ^ (b1+sum) ^ ((b1 >> 5)+b);
		b1 += ((b0 << 4)+c) ^ (b0+sum) ^ ((b0 >> 5)+d);
	} while (--n);

	buf[0] += b0;
	buf[1] += b1;
}

static void str2hashbuf(const char *msg, int len, unsigned int *buf, int num)
{
	unsigned pad, val;
	int i;

	pad = (__u32)len | ((__u32)len << 8);
	pad |= pad << 16;

	val = pad;
	if (len > num * 4)
		len = num * 4;
	for (i = 0; i < len; i++) {
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

f2fs_hash_t f2fs_dentry_hash(const char *name, int len)
{
	__u32 hash, minor_hash;
	f2fs_hash_t f2fs_hash;
	const char *p;
	__u32 in[8], buf[4];

	/* Initialize the default seed for the hash checksum functions */
	buf[0] = 0x67452301;
	buf[1] = 0xefcdab89;
	buf[2] = 0x98badcfe;
	buf[3] = 0x10325476;

	p = name;
	while (len > 0) {
		str2hashbuf(p, len, in, 4);
		TEA_transform(buf, in);
		len -= 16;
		p += 16;
	}
	hash = buf[0];
	minor_hash = buf[1];

	f2fs_hash = hash;
	f2fs_hash &= ~F2FS_HASH_COL_BIT;
	return f2fs_hash;
}
