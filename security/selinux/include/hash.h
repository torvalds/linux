/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _SELINUX_HASH_H_
#define _SELINUX_HASH_H_

/*
 * Based on MurmurHash3, written by Austin Appleby and placed in the
 * public domain.
 */
static inline u32 av_hash(u32 key1, u32 key2, u32 key3, u32 mask)
{
	static const u32 c1 = 0xcc9e2d51;
	static const u32 c2 = 0x1b873593;
	static const u32 r1 = 15;
	static const u32 r2 = 13;
	static const u32 m = 5;
	static const u32 n = 0xe6546b64;

	u32 hash = 0;

#define mix(input)                                         \
	do {                                               \
		u32 v = input;                             \
		v *= c1;                                   \
		v = (v << r1) | (v >> (32 - r1));          \
		v *= c2;                                   \
		hash ^= v;                                 \
		hash = (hash << r2) | (hash >> (32 - r2)); \
		hash = hash * m + n;                       \
	} while (0)

	mix(key1);
	mix(key2);
	mix(key3);

#undef mix

	hash ^= hash >> 16;
	hash *= 0x85ebca6b;
	hash ^= hash >> 13;
	hash *= 0xc2b2ae35;
	hash ^= hash >> 16;

	return hash & mask;
}

#endif /* _SELINUX_HASH_H_ */
