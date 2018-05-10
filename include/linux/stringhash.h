/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_STRINGHASH_H
#define __LINUX_STRINGHASH_H

#include <linux/compiler.h>	/* For __pure */
#include <linux/types.h>	/* For u32, u64 */
#include <linux/hash.h>

/*
 * Routines for hashing strings of bytes to a 32-bit hash value.
 *
 * These hash functions are NOT GUARANTEED STABLE between kernel
 * versions, architectures, or even repeated boots of the same kernel.
 * (E.g. they may depend on boot-time hardware detection or be
 * deliberately randomized.)
 *
 * They are also not intended to be secure against collisions caused by
 * malicious inputs; much slower hash functions are required for that.
 *
 * They are optimized for pathname components, meaning short strings.
 * Even if a majority of files have longer names, the dynamic profile of
 * pathname components skews short due to short directory names.
 * (E.g. /usr/lib/libsesquipedalianism.so.3.141.)
 */

/*
 * Version 1: one byte at a time.  Example of use:
 *
 * unsigned long hash = init_name_hash;
 * while (*p)
 *	hash = partial_name_hash(tolower(*p++), hash);
 * hash = end_name_hash(hash);
 *
 * Although this is designed for bytes, fs/hfsplus/unicode.c
 * abuses it to hash 16-bit values.
 */

/* Hash courtesy of the R5 hash in reiserfs modulo sign bits */
#define init_name_hash(salt)		(unsigned long)(salt)

/* partial hash update function. Assume roughly 4 bits per character */
static inline unsigned long
partial_name_hash(unsigned long c, unsigned long prevhash)
{
	return (prevhash + (c << 4) + (c >> 4)) * 11;
}

/*
 * Finally: cut down the number of bits to a int value (and try to avoid
 * losing bits).  This also has the property (wanted by the dcache)
 * that the msbits make a good hash table index.
 */
static inline unsigned int end_name_hash(unsigned long hash)
{
	return hash_long(hash, 32);
}

/*
 * Version 2: One word (32 or 64 bits) at a time.
 * If CONFIG_DCACHE_WORD_ACCESS is defined (meaning <asm/word-at-a-time.h>
 * exists, which describes major Linux platforms like x86 and ARM), then
 * this computes a different hash function much faster.
 *
 * If not set, this falls back to a wrapper around the preceding.
 */
extern unsigned int __pure full_name_hash(const void *salt, const char *, unsigned int);

/*
 * A hash_len is a u64 with the hash of a string in the low
 * half and the length in the high half.
 */
#define hashlen_hash(hashlen) ((u32)(hashlen))
#define hashlen_len(hashlen)  ((u32)((hashlen) >> 32))
#define hashlen_create(hash, len) ((u64)(len)<<32 | (u32)(hash))

/* Return the "hash_len" (hash and length) of a null-terminated string */
extern u64 __pure hashlen_string(const void *salt, const char *name);

#endif	/* __LINUX_STRINGHASH_H */
