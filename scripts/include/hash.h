/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef HASH_H
#define HASH_H

static inline unsigned int hash_str(const char *s)
{
	/* fnv32 hash */
	unsigned int hash = 2166136261U;

	for (; *s; s++)
		hash = (hash ^ *s) * 0x01000193;
	return hash;
}

/* simplified version of functions from include/linux/hash.h */
#define GOLDEN_RATIO_32 0x61C88647

static inline unsigned int hash_32(unsigned int val)
{
	return 0x61C88647 * val;
}

static inline unsigned int hash_ptr(const void *ptr)
{
	return hash_32((unsigned int)(unsigned long)ptr);
}

#endif /* HASH_H */
