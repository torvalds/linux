// SPDX-License-Identifier: GPL-2.0
/* Copyright Amazon.com Inc. or its affiliates. */

#ifndef _TEST_SIPHASH_H
#define _TEST_SIPHASH_H

/* include/linux/bitops.h */
static inline u64 rol64(u64 word, unsigned int shift)
{
	return (word << (shift & 63)) | (word >> ((-shift) & 63));
}

/* include/linux/siphash.h */
#define SIPHASH_PERMUTATION(a, b, c, d) ( \
	(a) += (b), (b) = rol64((b), 13), (b) ^= (a), (a) = rol64((a), 32), \
	(c) += (d), (d) = rol64((d), 16), (d) ^= (c), \
	(a) += (d), (d) = rol64((d), 21), (d) ^= (a), \
	(c) += (b), (b) = rol64((b), 17), (b) ^= (c), (c) = rol64((c), 32))

#define SIPHASH_CONST_0 0x736f6d6570736575ULL
#define SIPHASH_CONST_1 0x646f72616e646f6dULL
#define SIPHASH_CONST_2 0x6c7967656e657261ULL
#define SIPHASH_CONST_3 0x7465646279746573ULL

/* lib/siphash.c */
#define SIPROUND SIPHASH_PERMUTATION(v0, v1, v2, v3)

#define PREAMBLE(len) \
	u64 v0 = SIPHASH_CONST_0; \
	u64 v1 = SIPHASH_CONST_1; \
	u64 v2 = SIPHASH_CONST_2; \
	u64 v3 = SIPHASH_CONST_3; \
	u64 b = ((u64)(len)) << 56; \
	v3 ^= key->key[1]; \
	v2 ^= key->key[0]; \
	v1 ^= key->key[1]; \
	v0 ^= key->key[0];

#define POSTAMBLE \
	v3 ^= b; \
	SIPROUND; \
	SIPROUND; \
	v0 ^= b; \
	v2 ^= 0xff; \
	SIPROUND; \
	SIPROUND; \
	SIPROUND; \
	SIPROUND; \
	return (v0 ^ v1) ^ (v2 ^ v3);

static inline u64 siphash_2u64(const u64 first, const u64 second, const siphash_key_t *key)
{
	PREAMBLE(16)
	v3 ^= first;
	SIPROUND;
	SIPROUND;
	v0 ^= first;
	v3 ^= second;
	SIPROUND;
	SIPROUND;
	v0 ^= second;
	POSTAMBLE
}
#endif
