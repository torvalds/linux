/* Copyright (C) 2016 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 *
 * This file is provided under a dual BSD/GPLv2 license.
 *
 * SipHash: a fast short-input PRF
 * https://131002.net/siphash/
 *
 * This implementation is specifically for SipHash2-4 for a secure PRF
 * and HalfSipHash1-3/SipHash1-3 for an insecure PRF only suitable for
 * hashtables.
 */

#ifndef _LINUX_SIPHASH_H
#define _LINUX_SIPHASH_H

#include <linux/types.h>
#include <linux/kernel.h>

#define SIPHASH_ALIGNMENT __alignof__(u64)
typedef struct {
	u64 key[2];
} siphash_key_t;

static inline bool siphash_key_is_zero(const siphash_key_t *key)
{
	return !(key->key[0] | key->key[1]);
}

u64 __siphash_aligned(const void *data, size_t len, const siphash_key_t *key);
u64 __siphash_unaligned(const void *data, size_t len, const siphash_key_t *key);

u64 siphash_1u64(const u64 a, const siphash_key_t *key);
u64 siphash_2u64(const u64 a, const u64 b, const siphash_key_t *key);
u64 siphash_3u64(const u64 a, const u64 b, const u64 c,
		 const siphash_key_t *key);
u64 siphash_4u64(const u64 a, const u64 b, const u64 c, const u64 d,
		 const siphash_key_t *key);
u64 siphash_1u32(const u32 a, const siphash_key_t *key);
u64 siphash_3u32(const u32 a, const u32 b, const u32 c,
		 const siphash_key_t *key);

static inline u64 siphash_2u32(const u32 a, const u32 b,
			       const siphash_key_t *key)
{
	return siphash_1u64((u64)b << 32 | a, key);
}
static inline u64 siphash_4u32(const u32 a, const u32 b, const u32 c,
			       const u32 d, const siphash_key_t *key)
{
	return siphash_2u64((u64)b << 32 | a, (u64)d << 32 | c, key);
}


static inline u64 ___siphash_aligned(const __le64 *data, size_t len,
				     const siphash_key_t *key)
{
	if (__builtin_constant_p(len) && len == 4)
		return siphash_1u32(le32_to_cpup((const __le32 *)data), key);
	if (__builtin_constant_p(len) && len == 8)
		return siphash_1u64(le64_to_cpu(data[0]), key);
	if (__builtin_constant_p(len) && len == 16)
		return siphash_2u64(le64_to_cpu(data[0]), le64_to_cpu(data[1]),
				    key);
	if (__builtin_constant_p(len) && len == 24)
		return siphash_3u64(le64_to_cpu(data[0]), le64_to_cpu(data[1]),
				    le64_to_cpu(data[2]), key);
	if (__builtin_constant_p(len) && len == 32)
		return siphash_4u64(le64_to_cpu(data[0]), le64_to_cpu(data[1]),
				    le64_to_cpu(data[2]), le64_to_cpu(data[3]),
				    key);
	return __siphash_aligned(data, len, key);
}

/**
 * siphash - compute 64-bit siphash PRF value
 * @data: buffer to hash
 * @size: size of @data
 * @key: the siphash key
 */
static inline u64 siphash(const void *data, size_t len,
			  const siphash_key_t *key)
{
	if (IS_ENABLED(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS) ||
	    !IS_ALIGNED((unsigned long)data, SIPHASH_ALIGNMENT))
		return __siphash_unaligned(data, len, key);
	return ___siphash_aligned(data, len, key);
}

#define HSIPHASH_ALIGNMENT __alignof__(unsigned long)
typedef struct {
	unsigned long key[2];
} hsiphash_key_t;

u32 __hsiphash_aligned(const void *data, size_t len,
		       const hsiphash_key_t *key);
u32 __hsiphash_unaligned(const void *data, size_t len,
			 const hsiphash_key_t *key);

u32 hsiphash_1u32(const u32 a, const hsiphash_key_t *key);
u32 hsiphash_2u32(const u32 a, const u32 b, const hsiphash_key_t *key);
u32 hsiphash_3u32(const u32 a, const u32 b, const u32 c,
		  const hsiphash_key_t *key);
u32 hsiphash_4u32(const u32 a, const u32 b, const u32 c, const u32 d,
		  const hsiphash_key_t *key);

static inline u32 ___hsiphash_aligned(const __le32 *data, size_t len,
				      const hsiphash_key_t *key)
{
	if (__builtin_constant_p(len) && len == 4)
		return hsiphash_1u32(le32_to_cpu(data[0]), key);
	if (__builtin_constant_p(len) && len == 8)
		return hsiphash_2u32(le32_to_cpu(data[0]), le32_to_cpu(data[1]),
				     key);
	if (__builtin_constant_p(len) && len == 12)
		return hsiphash_3u32(le32_to_cpu(data[0]), le32_to_cpu(data[1]),
				     le32_to_cpu(data[2]), key);
	if (__builtin_constant_p(len) && len == 16)
		return hsiphash_4u32(le32_to_cpu(data[0]), le32_to_cpu(data[1]),
				     le32_to_cpu(data[2]), le32_to_cpu(data[3]),
				     key);
	return __hsiphash_aligned(data, len, key);
}

/**
 * hsiphash - compute 32-bit hsiphash PRF value
 * @data: buffer to hash
 * @size: size of @data
 * @key: the hsiphash key
 */
static inline u32 hsiphash(const void *data, size_t len,
			   const hsiphash_key_t *key)
{
	if (IS_ENABLED(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS) ||
	    !IS_ALIGNED((unsigned long)data, HSIPHASH_ALIGNMENT))
		return __hsiphash_unaligned(data, len, key);
	return ___hsiphash_aligned(data, len, key);
}

/*
 * These macros expose the raw SipHash and HalfSipHash permutations.
 * Do not use them directly! If you think you have a use for them,
 * be sure to CC the maintainer of this file explaining why.
 */

#define SIPHASH_PERMUTATION(a, b, c, d) ( \
	(a) += (b), (b) = rol64((b), 13), (b) ^= (a), (a) = rol64((a), 32), \
	(c) += (d), (d) = rol64((d), 16), (d) ^= (c), \
	(a) += (d), (d) = rol64((d), 21), (d) ^= (a), \
	(c) += (b), (b) = rol64((b), 17), (b) ^= (c), (c) = rol64((c), 32))

#define SIPHASH_CONST_0 0x736f6d6570736575ULL
#define SIPHASH_CONST_1 0x646f72616e646f6dULL
#define SIPHASH_CONST_2 0x6c7967656e657261ULL
#define SIPHASH_CONST_3 0x7465646279746573ULL

#define HSIPHASH_PERMUTATION(a, b, c, d) ( \
	(a) += (b), (b) = rol32((b), 5), (b) ^= (a), (a) = rol32((a), 16), \
	(c) += (d), (d) = rol32((d), 8), (d) ^= (c), \
	(a) += (d), (d) = rol32((d), 7), (d) ^= (a), \
	(c) += (b), (b) = rol32((b), 13), (b) ^= (c), (c) = rol32((c), 16))

#define HSIPHASH_CONST_0 0U
#define HSIPHASH_CONST_1 0U
#define HSIPHASH_CONST_2 0x6c796765U
#define HSIPHASH_CONST_3 0x74656462U

#endif /* _LINUX_SIPHASH_H */
