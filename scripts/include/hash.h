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

#endif /* HASH_H */
