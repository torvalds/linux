#ifndef __ASM_GENERIC_HASH_H
#define __ASM_GENERIC_HASH_H

#include <linux/jhash.h>

/**
 *	arch_fast_hash - Caclulates a hash over a given buffer that can have
 *			 arbitrary size. This function will eventually use an
 *			 architecture-optimized hashing implementation if
 *			 available, and trades off distribution for speed.
 *
 *	@data: buffer to hash
 *	@len: length of buffer in bytes
 *	@seed: start seed
 *
 *	Returns 32bit hash.
 */
static inline u32 arch_fast_hash(const void *data, u32 len, u32 seed)
{
	return jhash(data, len, seed);
}

/**
 *	arch_fast_hash2 - Caclulates a hash over a given buffer that has a
 *			  size that is of a multiple of 32bit words. This
 *			  function will eventually use an architecture-
 *			  optimized hashing implementation if available,
 *			  and trades off distribution for speed.
 *
 *	@data: buffer to hash (must be 32bit padded)
 *	@len: number of 32bit words
 *	@seed: start seed
 *
 *	Returns 32bit hash.
 */
static inline u32 arch_fast_hash2(const u32 *data, u32 len, u32 seed)
{
	return jhash2(data, len, seed);
}

#endif /* __ASM_GENERIC_HASH_H */
