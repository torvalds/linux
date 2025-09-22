/* Public domain. */

#ifndef _LINUX_HASH_H
#define _LINUX_HASH_H

#include <sys/types.h>

/* 2^32 * ((sqrt(5) - 1) / 2) from Knuth */
#define GOLDEN_RATIO_32 0x9e3779b9

static inline uint32_t
hash_32(uint32_t val, unsigned int bits)
{
	return (val * GOLDEN_RATIO_32) >> (32 - bits);
}

/* 2^64 * ((sqrt(5) - 1) / 2) from Knuth */
#define GOLDEN_RATIO_64 0x9e3779b97f4a7c16ULL

static inline uint32_t
hash_64(uint64_t val, unsigned int bits)
{
	return (val * GOLDEN_RATIO_64) >> (64 - bits);
}

#ifdef __LP64__
#define hash_long(val, bits) hash_64(val, bits)
#else
#define hash_long(val, bits) hash_32(val, bits)
#endif

#endif
