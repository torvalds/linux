/*	$OpenBSD: if_icevar.h,v 1.9 2025/09/17 12:54:19 jan Exp $	*/

/*  Copyright (c) 2024, Intel Corporation
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *   1. Redistributions of source code must retain the above copyright notice,
 *      this list of conditions and the following disclaimer.
 *
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *
 *   3. Neither the name of the Intel Corporation nor the names of its
 *      contributors may be used to endorse or promote products derived from
 *      this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Ported from FreeBSD ice(4) by Stefan Sperling in 2024.
 *
 * Copyright (c) 2024 Stefan Sperling <stsp@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* Code derived from FreeBSD sys/bitstring.h:
 *
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Paul Vixie.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Copyright (c) 2014 Spectra Logic Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 */

#ifndef _ICE_BITOPS_H_
#define _ICE_BITOPS_H_

/* Define the size of the bitmap chunk */
typedef uint32_t ice_bitmap_t;

/* NOTE!
 * Do not use any of the functions declared in this file
 * on memory that was not declared with ice_declare_bitmap.
 * Not following this rule might cause issues like split
 * locks.
 */

/* Number of bits per bitmap chunk */
#define BITS_PER_CHUNK		(8 * sizeof(ice_bitmap_t))
/* Determine which chunk a bit belongs in */
#define BIT_CHUNK(nr)		((nr) / BITS_PER_CHUNK)
/* How many chunks are required to store this many bits */
#define BITS_TO_CHUNKS(sz)	(((sz) + BITS_PER_CHUNK - 1) / BITS_PER_CHUNK)
/* Which bit inside a chunk this bit corresponds to */
#define BIT_IN_CHUNK(nr)	((nr) % BITS_PER_CHUNK)
/* How many bits are valid in the last chunk, assumes nr > 0 */
#define LAST_CHUNK_BITS(nr)	((((nr) - 1) % BITS_PER_CHUNK) + 1)
/* Generate a bitmask of valid bits in the last chunk, assumes nr > 0 */
#define LAST_CHUNK_MASK(nr)	(((ice_bitmap_t)~0) >> \
				 (BITS_PER_CHUNK - LAST_CHUNK_BITS(nr)))

#define ice_declare_bitmap(A, sz) \
	ice_bitmap_t A[BITS_TO_CHUNKS(sz)]

static inline bool ice_is_bit_set_internal(uint16_t nr, const ice_bitmap_t *bitmap)
{
	return !!(*bitmap & BIT(nr));
}

/*
 * If atomic version of the bitops are required, each specific OS
 * implementation will need to implement OS/platform specific atomic
 * version of the functions below:
 *
 * ice_clear_bit_internal
 * ice_set_bit_internal
 * ice_test_and_clear_bit_internal
 * ice_test_and_set_bit_internal
 *
 * and define macro ICE_ATOMIC_BITOPS to overwrite the default non-atomic
 * implementation.
 */
static inline void ice_clear_bit_internal(uint16_t nr, ice_bitmap_t *bitmap)
{
	*bitmap &= ~BIT(nr);
}

static inline void ice_set_bit_internal(uint16_t nr, ice_bitmap_t *bitmap)
{
	*bitmap |= BIT(nr);
}

static inline bool ice_test_and_clear_bit_internal(uint16_t nr,
						   ice_bitmap_t *bitmap)
{
	if (ice_is_bit_set_internal(nr, bitmap)) {
		ice_clear_bit_internal(nr, bitmap);
		return true;
	}
	return false;
}

static inline bool ice_test_and_set_bit_internal(uint16_t nr, ice_bitmap_t *bitmap)
{
	if (ice_is_bit_set_internal(nr, bitmap))
		return true;

	ice_set_bit_internal(nr, bitmap);
	return false;
}

/**
 * ice_is_bit_set - Check state of a bit in a bitmap
 * @bitmap: the bitmap to check
 * @nr: the bit to check
 *
 * Returns true if bit nr of bitmap is set. False otherwise. Assumes that nr
 * is less than the size of the bitmap.
 */
static inline bool ice_is_bit_set(const ice_bitmap_t *bitmap, uint16_t nr)
{
	return ice_is_bit_set_internal(BIT_IN_CHUNK(nr),
				       &bitmap[BIT_CHUNK(nr)]);
}

/**
 * ice_clear_bit - Clear a bit in a bitmap
 * @bitmap: the bitmap to change
 * @nr: the bit to change
 *
 * Clears the bit nr in bitmap. Assumes that nr is less than the size of the
 * bitmap.
 */
static inline void ice_clear_bit(uint16_t nr, ice_bitmap_t *bitmap)
{
	ice_clear_bit_internal(BIT_IN_CHUNK(nr), &bitmap[BIT_CHUNK(nr)]);
}

/**
 * ice_set_bit - Set a bit in a bitmap
 * @bitmap: the bitmap to change
 * @nr: the bit to change
 *
 * Sets the bit nr in bitmap. Assumes that nr is less than the size of the
 * bitmap.
 */
static inline void ice_set_bit(uint16_t nr, ice_bitmap_t *bitmap)
{
	ice_set_bit_internal(BIT_IN_CHUNK(nr), &bitmap[BIT_CHUNK(nr)]);
}

/**
 * ice_test_and_clear_bit - Atomically clear a bit and return the old bit value
 * @nr: the bit to change
 * @bitmap: the bitmap to change
 *
 * Check and clear the bit nr in bitmap. Assumes that nr is less than the size
 * of the bitmap.
 */
static inline bool
ice_test_and_clear_bit(uint16_t nr, ice_bitmap_t *bitmap)
{
	return ice_test_and_clear_bit_internal(BIT_IN_CHUNK(nr),
					       &bitmap[BIT_CHUNK(nr)]);
}

/**
 * ice_test_and_set_bit - Atomically set a bit and return the old bit value
 * @nr: the bit to change
 * @bitmap: the bitmap to change
 *
 * Check and set the bit nr in bitmap. Assumes that nr is less than the size of
 * the bitmap.
 */
static inline bool
ice_test_and_set_bit(uint16_t nr, ice_bitmap_t *bitmap)
{
	return ice_test_and_set_bit_internal(BIT_IN_CHUNK(nr),
					     &bitmap[BIT_CHUNK(nr)]);
}

/* ice_zero_bitmap - set bits of bitmap to zero.
 * @bmp: bitmap to set zeros
 * @size: Size of the bitmaps in bits
 *
 * Set all of the bits in a bitmap to zero. Note that this function assumes it
 * operates on an ice_bitmap_t which was declared using ice_declare_bitmap. It
 * will zero every bit in the last chunk, even if those bits are beyond the
 * size.
 */
static inline void ice_zero_bitmap(ice_bitmap_t *bmp, uint16_t size)
{
	memset(bmp, 0, BITS_TO_CHUNKS(size) * sizeof(ice_bitmap_t));
}

/**
 * ice_and_bitmap - bitwise AND 2 bitmaps and store result in dst bitmap
 * @dst: Destination bitmap that receive the result of the operation
 * @bmp1: The first bitmap to intersect
 * @bmp2: The second bitmap to intersect with the first
 * @size: Size of the bitmaps in bits
 *
 * This function performs a bitwise AND on two "source" bitmaps of the same size
 * and stores the result to "dst" bitmap. The "dst" bitmap must be of the same
 * size as the "source" bitmaps to avoid buffer overflows. This function returns
 * a non-zero value if at least one bit location from both "source" bitmaps is
 * non-zero.
 */
static inline int
ice_and_bitmap(ice_bitmap_t *dst, const ice_bitmap_t *bmp1,
	       const ice_bitmap_t *bmp2, uint16_t size)
{
	ice_bitmap_t res = 0, mask;
	uint16_t i;

	/* Handle all but the last chunk */
	for (i = 0; i < BITS_TO_CHUNKS(size) - 1; i++) {
		dst[i] = bmp1[i] & bmp2[i];
		res |= dst[i];
	}

	/* We want to take care not to modify any bits outside of the bitmap
	 * size, even in the destination bitmap. Thus, we won't directly
	 * assign the last bitmap, but instead use a bitmask to ensure we only
	 * modify bits which are within the size, and leave any bits above the
	 * size value alone.
	 */
	mask = LAST_CHUNK_MASK(size);
	dst[i] = (dst[i] & ~mask) | ((bmp1[i] & bmp2[i]) & mask);
	res |= dst[i] & mask;

	return res != 0;
}

/**
 * ice_or_bitmap - bitwise OR 2 bitmaps and store result in dst bitmap
 * @dst: Destination bitmap that receive the result of the operation
 * @bmp1: The first bitmap to intersect
 * @bmp2: The second bitmap to intersect with the first
 * @size: Size of the bitmaps in bits
 *
 * This function performs a bitwise OR on two "source" bitmaps of the same size
 * and stores the result to "dst" bitmap. The "dst" bitmap must be of the same
 * size as the "source" bitmaps to avoid buffer overflows.
 */
static inline void
ice_or_bitmap(ice_bitmap_t *dst, const ice_bitmap_t *bmp1,
	      const ice_bitmap_t *bmp2, uint16_t size)
{
	ice_bitmap_t mask;
	uint16_t i;

	/* Handle all but last chunk */
	for (i = 0; i < BITS_TO_CHUNKS(size) - 1; i++)
		dst[i] = bmp1[i] | bmp2[i];

	/* We want to only OR bits within the size. Furthermore, we also do
	 * not want to modify destination bits which are beyond the specified
	 * size. Use a bitmask to ensure that we only modify the bits that are
	 * within the specified size.
	 */
	mask = LAST_CHUNK_MASK(size);
	dst[i] = (dst[i] & ~mask) | ((bmp1[i] | bmp2[i]) & mask);
}

/**
 * ice_xor_bitmap - bitwise XOR 2 bitmaps and store result in dst bitmap
 * @dst: Destination bitmap that receive the result of the operation
 * @bmp1: The first bitmap of XOR operation
 * @bmp2: The second bitmap to XOR with the first
 * @size: Size of the bitmaps in bits
 *
 * This function performs a bitwise XOR on two "source" bitmaps of the same size
 * and stores the result to "dst" bitmap. The "dst" bitmap must be of the same
 * size as the "source" bitmaps to avoid buffer overflows.
 */
static inline void
ice_xor_bitmap(ice_bitmap_t *dst, const ice_bitmap_t *bmp1,
	       const ice_bitmap_t *bmp2, uint16_t size)
{
	ice_bitmap_t mask;
	uint16_t i;

	/* Handle all but last chunk */
	for (i = 0; i < BITS_TO_CHUNKS(size) - 1; i++)
		dst[i] = bmp1[i] ^ bmp2[i];

	/* We want to only XOR bits within the size. Furthermore, we also do
	 * not want to modify destination bits which are beyond the specified
	 * size. Use a bitmask to ensure that we only modify the bits that are
	 * within the specified size.
	 */
	mask = LAST_CHUNK_MASK(size);
	dst[i] = (dst[i] & ~mask) | ((bmp1[i] ^ bmp2[i]) & mask);
}

/**
 * ice_andnot_bitmap - bitwise ANDNOT 2 bitmaps and result in dst bitmap
 * @dst: Destination bitmap that receive the result of the operation
 * @bmp1: The first bitmap of ANDNOT operation
 * @bmp2: The second bitmap to ANDNOT operation
 * @size: Size of the bitmaps in bits
 *
 * This function performs a bitwise ANDNOT on two "source" bitmaps of the same
 * size, and stores the result to "dst" bitmap. The "dst" bitmap must be of the
 * same size as the "source" bitmaps to avoid buffer overflows.
 */
static inline void
ice_andnot_bitmap(ice_bitmap_t *dst, const ice_bitmap_t *bmp1,
		  const ice_bitmap_t *bmp2, uint16_t size)
{
	ice_bitmap_t mask;
	uint16_t i;

	/* Handle all but last chunk */
	for (i = 0; i < BITS_TO_CHUNKS(size) - 1; i++)
		dst[i] = bmp1[i] & ~bmp2[i];

	/* We want to only clear bits within the size. Furthermore, we also do
	 * not want to modify destination bits which are beyond the specified
	 * size. Use a bitmask to ensure that we only modify the bits that are
	 * within the specified size.
	 */
	mask = LAST_CHUNK_MASK(size);
	dst[i] = (dst[i] & ~mask) | ((bmp1[i] & ~bmp2[i]) & mask);
}

/**
 * ice_find_next_bit - Find the index of the next set bit of a bitmap
 * @bitmap: the bitmap to scan
 * @size: the size in bits of the bitmap
 * @offset: the offset to start at
 *
 * Scans the bitmap and returns the index of the first set bit which is equal
 * to or after the specified offset. Will return size if no bits are set.
 */
static inline uint16_t
ice_find_next_bit(const ice_bitmap_t *bitmap, uint16_t size, uint16_t offset)
{
	uint16_t i, j;

	if (offset >= size)
		return size;

	/* Since the starting position may not be directly on a chunk
	 * boundary, we need to be careful to handle the first chunk specially
	 */
	i = BIT_CHUNK(offset);
	if (bitmap[i] != 0) {
		uint16_t off = i * BITS_PER_CHUNK;

		for (j = offset % BITS_PER_CHUNK; j < BITS_PER_CHUNK; j++) {
			if (ice_is_bit_set(bitmap, off + j))
				return min(size, (uint16_t)(off + j));
		}
	}

	/* Now we handle the remaining chunks, if any */
	for (i++; i < BITS_TO_CHUNKS(size); i++) {
		if (bitmap[i] != 0) {
			uint16_t off = i * BITS_PER_CHUNK;

			for (j = 0; j < BITS_PER_CHUNK; j++) {
				if (ice_is_bit_set(bitmap, off + j))
					return min(size, (uint16_t)(off + j));
			}
		}
	}
	return size;
}

/**
 * ice_find_first_bit - Find the index of the first set bit of a bitmap
 * @bitmap: the bitmap to scan
 * @size: the size in bits of the bitmap
 *
 * Scans the bitmap and returns the index of the first set bit. Will return
 * size if no bits are set.
 */
static inline uint16_t ice_find_first_bit(const ice_bitmap_t *bitmap, uint16_t size)
{
	return ice_find_next_bit(bitmap, size, 0);
}

#define ice_for_each_set_bit(_bitpos, _addr, _maxlen)	\
	for ((_bitpos) = ice_find_first_bit((_addr), (_maxlen)); \
	     (_bitpos) < (_maxlen); \
	     (_bitpos) = ice_find_next_bit((_addr), (_maxlen), (_bitpos) + 1))

/**
 * ice_is_any_bit_set - Return true of any bit in the bitmap is set
 * @bitmap: the bitmap to check
 * @size: the size of the bitmap
 *
 * Equivalent to checking if ice_find_first_bit returns a value less than the
 * bitmap size.
 */
static inline bool ice_is_any_bit_set(ice_bitmap_t *bitmap, uint16_t size)
{
	return ice_find_first_bit(bitmap, size) < size;
}

/**
 * ice_cp_bitmap - copy bitmaps
 * @dst: bitmap destination
 * @src: bitmap to copy from
 * @size: Size of the bitmaps in bits
 *
 * This function copy bitmap from src to dst. Note that this function assumes
 * it is operating on a bitmap declared using ice_declare_bitmap. It will copy
 * the entire last chunk even if this contains bits beyond the size.
 */
static inline void ice_cp_bitmap(ice_bitmap_t *dst, ice_bitmap_t *src, uint16_t size)
{
	memcpy(dst, src, BITS_TO_CHUNKS(size) * sizeof(ice_bitmap_t));
}

/**
 * ice_bitmap_set - set a number of bits in bitmap from a starting position
 * @dst: bitmap destination
 * @pos: first bit position to set
 * @num_bits: number of bits to set
 *
 * This function sets bits in a bitmap from pos to (pos + num_bits) - 1.
 * Note that this function assumes it is operating on a bitmap declared using
 * ice_declare_bitmap.
 */
static inline void
ice_bitmap_set(ice_bitmap_t *dst, uint16_t pos, uint16_t num_bits)
{
	uint16_t i;

	for (i = pos; i < pos + num_bits; i++)
		ice_set_bit(i, dst);
}

/**
 * ice_bitmap_hweight - hamming weight of bitmap
 * @bm: bitmap pointer
 * @size: size of bitmap (in bits)
 *
 * This function determines the number of set bits in a bitmap.
 * Note that this function assumes it is operating on a bitmap declared using
 * ice_declare_bitmap.
 */
static inline int
ice_bitmap_hweight(ice_bitmap_t *bm, uint16_t size)
{
	int count = 0;
	uint16_t bit = 0;

	while (size > (bit = ice_find_next_bit(bm, size, bit))) {
		count++;
		bit++;
	}

	return count;
}

/**
 * ice_cmp_bitmap - compares two bitmaps
 * @bmp1: the bitmap to compare
 * @bmp2: the bitmap to compare with bmp1
 * @size: Size of the bitmaps in bits
 *
 * This function compares two bitmaps, and returns result as true or false.
 */
static inline bool
ice_cmp_bitmap(ice_bitmap_t *bmp1, ice_bitmap_t *bmp2, uint16_t size)
{
	ice_bitmap_t mask;
	uint16_t i;

	/* Handle all but last chunk */
	for (i = 0; i < BITS_TO_CHUNKS(size) - 1; i++)
		if (bmp1[i] != bmp2[i])
			return false;

	/* We want to only compare bits within the size */
	mask = LAST_CHUNK_MASK(size);
	if ((bmp1[i] & mask) != (bmp2[i] & mask))
		return false;

	return true;
}

/**
 * ice_bitmap_from_array32 - copies u32 array source into bitmap destination
 * @dst: the destination bitmap
 * @src: the source u32 array
 * @size: size of the bitmap (in bits)
 *
 * This function copies the src bitmap stored in an u32 array into the dst
 * bitmap stored as an ice_bitmap_t.
 */
static inline void
ice_bitmap_from_array32(ice_bitmap_t *dst, uint32_t *src, uint16_t size)
{
	uint32_t remaining_bits, i;

#define BITS_PER_U32	(sizeof(uint32_t) * 8)
	/* clear bitmap so we only have to set when iterating */
	ice_zero_bitmap(dst, size);

	for (i = 0; i < (uint32_t)(size / BITS_PER_U32); i++) {
		uint32_t bit_offset = i * BITS_PER_U32;
		uint32_t entry = src[i];
		uint32_t j;

		for (j = 0; j < BITS_PER_U32; j++) {
			if (entry & BIT(j))
				ice_set_bit((uint16_t)(j + bit_offset), dst);
		}
	}

	/* still need to check the leftover bits (i.e. if size isn't evenly
	 * divisible by BITS_PER_U32
	 **/
	remaining_bits = size % BITS_PER_U32;
	if (remaining_bits) {
		uint32_t bit_offset = i * BITS_PER_U32;
		uint32_t entry = src[i];
		uint32_t j;

		for (j = 0; j < remaining_bits; j++) {
			if (entry & BIT(j))
				ice_set_bit((uint16_t)(j + bit_offset), dst);
		}
	}
}

#undef BIT_CHUNK
#undef BIT_IN_CHUNK
#undef LAST_CHUNK_BITS
#undef LAST_CHUNK_MASK

#endif /* _ICE_BITOPS_H_ */

/*
 * @struct ice_dma_mem
 * @brief DMA memory allocation
 *
 * Contains DMA allocation bits, used to simplify DMA allocations.
 */
struct ice_dma_mem {
	void *va;
	uint64_t pa;
	bus_size_t size;

	bus_dma_tag_t		tag;
	bus_dmamap_t		map;
	bus_dma_segment_t	seg;
};
#define ICE_DMA_MAP(_m)	((_m)->map)
#define ICE_DMA_DVA(_m)	((_m)->map->dm_segs[0].ds_addr)
#define ICE_DMA_KVA(_m)	((void *)(_m)->va)
#define ICE_DMA_LEN(_m)	((_m)->size)

#define ICE_STR_BUF_LEN 32

/**
 * @struct ice_lock
 * @brief simplified lock API
 *
 * Contains a simple lock implementation used to lock various resources.
 */
struct ice_lock {
	struct mutex mutex;
	char name[ICE_STR_BUF_LEN];
};

extern uint16_t ice_lock_count;

/*
 * ice_init_lock - Initialize a lock for use
 * @lock: the lock memory to initialize
 *
 * OS compatibility layer to provide a simple locking mechanism. We use
 * a mutex for this purpose.
 */
static inline void
ice_init_lock(struct ice_lock *lock)
{
	/*
	 * Make each lock unique by incrementing a counter each time this
	 * function is called. Use of a uint16_t allows 65535 possible locks before
	 * we'd hit a duplicate.
	 */
	memset(lock->name, 0, sizeof(lock->name));
	snprintf(lock->name, ICE_STR_BUF_LEN, "ice_lock_%u", ice_lock_count++);
	mtx_init_flags(&lock->mutex, IPL_NET, lock->name, 0);
}

/* FW update timeout definitions are in milliseconds */
#define ICE_NVM_TIMEOUT			180000
#define ICE_CHANGE_LOCK_TIMEOUT		1000
#define ICE_GLOBAL_CFG_LOCK_TIMEOUT	3000

#define ICE_PF_RESET_WAIT_COUNT	500

/* Error Codes */
enum ice_status {
	ICE_SUCCESS				= 0,

	/* Generic codes : Range -1..-49 */
	ICE_ERR_PARAM				= -1,
	ICE_ERR_NOT_IMPL			= -2,
	ICE_ERR_NOT_READY			= -3,
	ICE_ERR_NOT_SUPPORTED			= -4,
	ICE_ERR_BAD_PTR				= -5,
	ICE_ERR_INVAL_SIZE			= -6,
	ICE_ERR_DEVICE_NOT_SUPPORTED		= -8,
	ICE_ERR_RESET_FAILED			= -9,
	ICE_ERR_FW_API_VER			= -10,
	ICE_ERR_NO_MEMORY			= -11,
	ICE_ERR_CFG				= -12,
	ICE_ERR_OUT_OF_RANGE			= -13,
	ICE_ERR_ALREADY_EXISTS			= -14,
	ICE_ERR_DOES_NOT_EXIST			= -15,
	ICE_ERR_IN_USE				= -16,
	ICE_ERR_MAX_LIMIT			= -17,
	ICE_ERR_RESET_ONGOING			= -18,
	ICE_ERR_HW_TABLE			= -19,
	ICE_ERR_FW_DDP_MISMATCH			= -20,

	/* NVM specific error codes: Range -50..-59 */
	ICE_ERR_NVM				= -50,
	ICE_ERR_NVM_CHECKSUM			= -51,
	ICE_ERR_BUF_TOO_SHORT			= -52,
	ICE_ERR_NVM_BLANK_MODE			= -53,

	/* ARQ/ASQ specific error codes. Range -100..-109 */
	ICE_ERR_AQ_ERROR			= -100,
	ICE_ERR_AQ_TIMEOUT			= -101,
	ICE_ERR_AQ_FULL				= -102,
	ICE_ERR_AQ_NO_WORK			= -103,
	ICE_ERR_AQ_EMPTY			= -104,
	ICE_ERR_AQ_FW_CRITICAL			= -105,
};

#define ICE_SQ_SEND_DELAY_TIME_MS	10
#define ICE_SQ_SEND_MAX_EXECUTE		3

enum ice_fw_modes {
	ICE_FW_MODE_NORMAL,
	ICE_FW_MODE_DBG,
	ICE_FW_MODE_REC,
	ICE_FW_MODE_ROLLBACK
};

#define ICE_AQ_LEN		1023
#define ICE_MBXQ_LEN		512
#define ICE_SBQ_LEN		512

#define ICE_CTRLQ_WORK_LIMIT 256

#define ICE_DFLT_TRAFFIC_CLASS BIT(0)

/* wait up to 50 microseconds for queue state change */
#define ICE_Q_WAIT_RETRY_LIMIT	5

/* Maximum buffer lengths for all control queue types */
#define ICE_AQ_MAX_BUF_LEN 4096
#define ICE_MBXQ_MAX_BUF_LEN 4096

#define ICE_CTL_Q_DESC(R, i) \
	(&(((struct ice_aq_desc *)((R).desc_buf.va))[i]))

#define ICE_CTL_Q_DESC_UNUSED(R) \
	((uint16_t)((((R)->next_to_clean > (R)->next_to_use) ? 0 : (R)->count) + \
	       (R)->next_to_clean - (R)->next_to_use - 1))

/* Defines that help manage the driver vs FW API checks.
 * Take a look at ice_aq_ver_check in ice_controlq.c for actual usage.
 */
#define EXP_FW_API_VER_BRANCH		0x00
#define EXP_FW_API_VER_MAJOR		0x01
#define EXP_FW_API_VER_MINOR		0x05

/* Alignment for queues */
#define DBA_ALIGN		128

/* Maximum TSO size is (256K)-1 */
#define ICE_TSO_SIZE		((256*1024) - 1)

/* Minimum size for TSO MSS */
#define ICE_MIN_TSO_MSS		64

#define ICE_MAX_TX_SEGS		8
#define ICE_MAX_TSO_SEGS	128

#define ICE_MAX_DMA_SEG_SIZE	((16*1024) - 1)

#define ICE_MAX_RX_SEGS		5

#define ICE_MAX_TSO_HDR_SEGS	3

#define ICE_MSIX_BAR		3

#define ICE_DEFAULT_DESC_COUNT	1024
#define ICE_MAX_DESC_COUNT	8160
#define ICE_MIN_DESC_COUNT	64
#define ICE_DESC_COUNT_INCR	32

/* Maximum size of a single frame (for Tx and Rx) */
#define ICE_MAX_FRAME_SIZE ICE_AQ_SET_MAC_FRAME_SIZE_MAX

/* Maximum MTU size */
#define ICE_MAX_MTU (ICE_MAX_FRAME_SIZE - \
		     ETHER_HDR_LEN - ETHER_CRC_LEN - ETHER_VLAN_ENCAP_LEN)

#define ICE_QIDX_INVALID 0xffff

/*
 * Hardware requires that TSO packets have an segment size of at least 64
 * bytes. To avoid sending bad frames to the hardware, the driver forces the
 * MSS for all TSO packets to have a segment size of at least 64 bytes.
 *
 * However, if the MTU is reduced below a certain size, then the resulting
 * larger MSS can result in transmitting segmented frames with a packet size
 * larger than the MTU.
 *
 * Avoid this by preventing the MTU from being lowered below this limit.
 * Alternative solutions require changing the TCP stack to disable offloading
 * the segmentation when the requested segment size goes below 64 bytes.
 */
#define ICE_MIN_MTU 112

/*
 * The default number of queues reserved for a VF is 4, according to the
 * AVF Base Mode specification.
 */
#define ICE_DEFAULT_VF_QUEUES	4

/*
 * An invalid VSI number to indicate that mirroring should be disabled.
 */
#define ICE_INVALID_MIRROR_VSI	((u16)-1)
/*
 * The maximum number of RX queues allowed per TC in a VSI.
 */
#define ICE_MAX_RXQS_PER_TC	256

/*
 * There are three settings that can be updated independently or
 * altogether: Link speed, FEC, and Flow Control.  These macros allow
 * the caller to specify which setting(s) to update.
 */
#define ICE_APPLY_LS        BIT(0)
#define ICE_APPLY_FEC       BIT(1)
#define ICE_APPLY_FC        BIT(2)
#define ICE_APPLY_LS_FEC    (ICE_APPLY_LS | ICE_APPLY_FEC)
#define ICE_APPLY_LS_FC     (ICE_APPLY_LS | ICE_APPLY_FC)
#define ICE_APPLY_FEC_FC    (ICE_APPLY_FEC | ICE_APPLY_FC)
#define ICE_APPLY_LS_FEC_FC (ICE_APPLY_LS_FEC | ICE_APPLY_FC)

/**
 * @enum ice_dyn_idx_t
 * @brief Dynamic Control ITR indexes
 *
 * This enum matches hardware bits and is meant to be used by DYN_CTLN
 * registers and QINT registers or more generally anywhere in the manual
 * mentioning ITR_INDX, ITR_NONE cannot be used as an index 'n' into any
 * register but instead is a special value meaning "don't update" ITR0/1/2.
 */
enum ice_dyn_idx_t {
	ICE_IDX_ITR0 = 0,
	ICE_IDX_ITR1 = 1,
	ICE_IDX_ITR2 = 2,
	ICE_ITR_NONE = 3	/* ITR_NONE must not be used as an index */
};

/* By convention ITR0 is used for RX, and ITR1 is used for TX */
#define ICE_RX_ITR ICE_IDX_ITR0
#define ICE_TX_ITR ICE_IDX_ITR1

#define ICE_ITR_MAX		8160

/* Define the default Tx and Rx ITR as 50us (translates to ~20k int/sec max) */
#define ICE_DFLT_TX_ITR		50
#define ICE_DFLT_RX_ITR		50

/**
 * @enum ice_rx_dtype
 * @brief DTYPE header split options
 *
 * This enum matches the Rx context bits to define whether header split is
 * enabled or not.
 */
enum ice_rx_dtype {
	ICE_RX_DTYPE_NO_SPLIT		= 0,
	ICE_RX_DTYPE_HEADER_SPLIT	= 1,
	ICE_RX_DTYPE_SPLIT_ALWAYS	= 2,
};

#if 0
/* List of hardware offloads we support */
#define ICE_CSUM_OFFLOAD (CSUM_IP | CSUM_IP_TCP | CSUM_IP_UDP | CSUM_IP_SCTP |	\
			  CSUM_IP6_TCP| CSUM_IP6_UDP | CSUM_IP6_SCTP |		\
			  CSUM_IP_TSO | CSUM_IP6_TSO)

/* Macros to decide what kind of hardware offload to enable */
#define ICE_CSUM_TCP (CSUM_IP_TCP|CSUM_IP_TSO|CSUM_IP6_TSO|CSUM_IP6_TCP)
#define ICE_CSUM_UDP (CSUM_IP_UDP|CSUM_IP6_UDP)
#define ICE_CSUM_SCTP (CSUM_IP_SCTP|CSUM_IP6_SCTP)
#define ICE_CSUM_IP (CSUM_IP|CSUM_IP_TSO)

/* List of known RX CSUM offload flags */
#define ICE_RX_CSUM_FLAGS (CSUM_L3_CALC | CSUM_L3_VALID | CSUM_L4_CALC | \
			   CSUM_L4_VALID | CSUM_L5_CALC | CSUM_L5_VALID | \
			   CSUM_COALESCED)
#endif

/* List of interface capabilities supported by ice hardware */
#define ICE_FULL_CAPS \
	(IFCAP_TSOv4 | IFCAP_TSOv6 | \
	 IFCAP_CSUM_TCPv4 | IFCAP_CSUM_TCPv6| \
	 IFCAP_VLAN_HWTAGGING | IFCAP_VLAN_HWOFFLOAD | \
	 IFCAP_VLAN_MTU | IFCAP_LRO)

/* Safe mode disables support for hardware checksums and TSO */
#define ICE_SAFE_CAPS \
	(ICE_FULL_CAPS & ~(IFCAP_CSUM_TCPv4 | IFCAP_CSUM_TCPv6 | \
	IFCAP_TSOv4 | IFCAP_TSOv6 | IFCAP_VLAN_HWOFFLOAD))

#define ICE_CAPS(sc) \
	(ice_is_bit_set(sc->feat_en, ICE_FEATURE_SAFE_MODE) ? ICE_SAFE_CAPS : ICE_FULL_CAPS)


/* Different control queue types: These are mainly for SW consumption. */
enum ice_ctl_q {
	ICE_CTL_Q_UNKNOWN = 0,
	ICE_CTL_Q_ADMIN,
	ICE_CTL_Q_MAILBOX,
};

/* Control Queue timeout settings - max delay 1s */
#define ICE_CTL_Q_SQ_CMD_TIMEOUT	100000	/* Count 100000 times */
#define ICE_CTL_Q_ADMIN_INIT_TIMEOUT	10    /* Count 10 times */
#define ICE_CTL_Q_ADMIN_INIT_MSEC	100   /* Check every 100msec */

struct ice_ctl_q_ring {
	void *dma_head;			/* Virtual address to DMA head */
	struct ice_dma_mem desc_buf;	/* descriptor ring memory */

	union {
		struct ice_dma_mem *sq_bi;
		struct ice_dma_mem *rq_bi;
	} r;

	uint16_t count;		/* Number of descriptors */

	/* used for interrupt processing */
	uint16_t next_to_use;
	uint16_t next_to_clean;

	/* used for queue tracking */
	uint32_t head;
	uint32_t tail;
	uint32_t len;
	uint32_t bah;
	uint32_t bal;
	uint32_t len_mask;
	uint32_t len_ena_mask;
	uint32_t len_crit_mask;
	uint32_t head_mask;
};

/* sq transaction details */
struct ice_sq_cd {
	struct ice_aq_desc *wb_desc;
};

/* rq event information */
struct ice_rq_event_info {
	struct ice_aq_desc desc;
	uint16_t msg_len;
	uint16_t buf_len;
	uint8_t *msg_buf;
};

/* Control Queue information */
struct ice_ctl_q_info {
	enum ice_ctl_q qtype;
	struct ice_ctl_q_ring rq;	/* receive queue */
	struct ice_ctl_q_ring sq;	/* send queue */
	uint32_t sq_cmd_timeout;		/* send queue cmd write back timeout */

	uint16_t num_rq_entries;		/* receive queue depth */
	uint16_t num_sq_entries;		/* send queue depth */
	uint16_t rq_buf_size;		/* receive queue buffer size */
	uint16_t sq_buf_size;		/* send queue buffer size */
	enum ice_aq_err sq_last_status;	/* last status on send queue */
	struct ice_lock sq_lock;		/* Send queue lock */
	struct ice_lock rq_lock;		/* Receive queue lock */
};

enum ice_mac_type {
	ICE_MAC_UNKNOWN = 0,
	ICE_MAC_VF,
	ICE_MAC_E810,
	ICE_MAC_GENERIC,
	ICE_MAC_GENERIC_3K,
	ICE_MAC_GENERIC_3K_E825,
};

/*
 * Reset types used to determine which kind of reset was requested. These
 * defines match what the RESET_TYPE field of the GLGEN_RSTAT register.
 * ICE_RESET_PFR does not match any RESET_TYPE field in the GLGEN_RSTAT register
 * because its reset source is different than the other types listed.
 */
enum ice_reset_req {
	ICE_RESET_POR	= 0,
	ICE_RESET_INVAL	= 0,
	ICE_RESET_CORER	= 1,
	ICE_RESET_GLOBR	= 2,
	ICE_RESET_EMPR	= 3,
	ICE_RESET_PFR	= 4,
};

/* Common HW capabilities for SW use */
struct ice_hw_common_caps {
	/* Write CSR protection */
	uint64_t wr_csr_prot;
	uint32_t switching_mode;
	/* switching mode supported - EVB switching (including cloud) */
#define ICE_NVM_IMAGE_TYPE_EVB		0x0

	/* Manageablity mode & supported protocols over MCTP */
	uint32_t mgmt_mode;
#define ICE_MGMT_MODE_PASS_THRU_MODE_M		0xF
#define ICE_MGMT_MODE_CTL_INTERFACE_M		0xF0
#define ICE_MGMT_MODE_REDIR_SB_INTERFACE_M	0xF00

	uint32_t mgmt_protocols_mctp;
#define ICE_MGMT_MODE_PROTO_RSVD	BIT(0)
#define ICE_MGMT_MODE_PROTO_PLDM	BIT(1)
#define ICE_MGMT_MODE_PROTO_OEM		BIT(2)
#define ICE_MGMT_MODE_PROTO_NC_SI	BIT(3)

	uint32_t os2bmc;
	uint32_t valid_functions;
	/* DCB capabilities */
	uint32_t active_tc_bitmap;
	uint32_t maxtc;

	/* RSS related capabilities */
	uint32_t rss_table_size;		/* 512 for PFs and 64 for VFs */
	uint32_t rss_table_entry_width;	/* RSS Entry width in bits */

	/* Tx/Rx queues */
	uint32_t num_rxq;			/* Number/Total Rx queues */
	uint32_t rxq_first_id;		/* First queue ID for Rx queues */
	uint32_t num_txq;			/* Number/Total Tx queues */
	uint32_t txq_first_id;		/* First queue ID for Tx queues */

	/* MSI-X vectors */
	uint32_t num_msix_vectors;
	uint32_t msix_vector_first_id;

	/* Max MTU for function or device */
	uint32_t max_mtu;

	/* WOL related */
	uint32_t num_wol_proxy_fltr;
	uint32_t wol_proxy_vsi_seid;

	/* LED/SDP pin count */
	uint32_t led_pin_num;
	uint32_t sdp_pin_num;

	/* LED/SDP - Supports up to 12 LED pins and 8 SDP signals */
#define ICE_MAX_SUPPORTED_GPIO_LED	12
#define ICE_MAX_SUPPORTED_GPIO_SDP	8
	uint8_t led[ICE_MAX_SUPPORTED_GPIO_LED];
	uint8_t sdp[ICE_MAX_SUPPORTED_GPIO_SDP];

	/* SR-IOV virtualization */
	uint8_t sr_iov_1_1;			/* SR-IOV enabled */

	/* VMDQ */
	uint8_t vmdq;			/* VMDQ supported */

	/* EVB capabilities */
	uint8_t evb_802_1_qbg;		/* Edge Virtual Bridging */
	uint8_t evb_802_1_qbh;		/* Bridge Port Extension */

	uint8_t dcb;
	uint8_t iscsi;
	uint8_t mgmt_cem;
	uint8_t iwarp;
	uint8_t roce_lag;

	/* WoL and APM support */
#define ICE_WOL_SUPPORT_M		BIT(0)
#define ICE_ACPI_PROG_MTHD_M		BIT(1)
#define ICE_PROXY_SUPPORT_M		BIT(2)
	uint8_t apm_wol_support;
	uint8_t acpi_prog_mthd;
	uint8_t proxy_support;
	bool sec_rev_disabled;
	bool update_disabled;
	bool nvm_unified_update;
	bool netlist_auth;
#define ICE_NVM_MGMT_SEC_REV_DISABLED		BIT(0)
#define ICE_NVM_MGMT_UPDATE_DISABLED		BIT(1)
#define ICE_NVM_MGMT_UNIFIED_UPD_SUPPORT	BIT(3)
#define ICE_NVM_MGMT_NETLIST_AUTH_SUPPORT	BIT(5)
	/* PCIe reset avoidance */
	bool pcie_reset_avoidance; /* false: not supported, true: supported */
	/* Post update reset restriction */
	bool reset_restrict_support; /* false: not supported, true: supported */

	/* External topology device images within the NVM */
#define ICE_EXT_TOPO_DEV_IMG_COUNT	4
	uint32_t ext_topo_dev_img_ver_high[ICE_EXT_TOPO_DEV_IMG_COUNT];
	uint32_t ext_topo_dev_img_ver_low[ICE_EXT_TOPO_DEV_IMG_COUNT];
	uint8_t ext_topo_dev_img_part_num[ICE_EXT_TOPO_DEV_IMG_COUNT];
#define ICE_EXT_TOPO_DEV_IMG_PART_NUM_S	8
#define ICE_EXT_TOPO_DEV_IMG_PART_NUM_M	\
		MAKEMASK(0xFF, ICE_EXT_TOPO_DEV_IMG_PART_NUM_S)
	bool ext_topo_dev_img_load_en[ICE_EXT_TOPO_DEV_IMG_COUNT];
#define ICE_EXT_TOPO_DEV_IMG_LOAD_EN	BIT(0)
	bool ext_topo_dev_img_prog_en[ICE_EXT_TOPO_DEV_IMG_COUNT];
#define ICE_EXT_TOPO_DEV_IMG_PROG_EN	BIT(1)
	bool ext_topo_dev_img_ver_schema[ICE_EXT_TOPO_DEV_IMG_COUNT];
#define ICE_EXT_TOPO_DEV_IMG_VER_SCHEMA	BIT(2)
	bool tx_sched_topo_comp_mode_en;
	bool dyn_flattening_en;
	/* Support for OROM update in Recovery Mode */
	bool orom_recovery_update;
};

#define ICE_NAC_TOPO_PRIMARY_M	BIT(0)
#define ICE_NAC_TOPO_DUAL_M	BIT(1)
#define ICE_NAC_TOPO_ID_M	MAKEMASK(0xf, 0)

enum ice_aq_res_ids {
	ICE_NVM_RES_ID = 1,
	ICE_SPD_RES_ID,
	ICE_CHANGE_LOCK_RES_ID,
	ICE_GLOBAL_CFG_LOCK_RES_ID
};

/* FW update timeout definitions are in milliseconds */
#define ICE_NVM_TIMEOUT			180000
#define ICE_CHANGE_LOCK_TIMEOUT		1000
#define ICE_GLOBAL_CFG_LOCK_TIMEOUT	3000

struct ice_link_default_override_tlv {
	uint8_t options;
#define ICE_LINK_OVERRIDE_OPT_M		0x3F
#define ICE_LINK_OVERRIDE_STRICT_MODE	BIT(0)
#define ICE_LINK_OVERRIDE_EPCT_DIS	BIT(1)
#define ICE_LINK_OVERRIDE_PORT_DIS	BIT(2)
#define ICE_LINK_OVERRIDE_EN		BIT(3)
#define ICE_LINK_OVERRIDE_AUTO_LINK_DIS	BIT(4)
#define ICE_LINK_OVERRIDE_EEE_EN	BIT(5)
	uint8_t phy_config;
#define ICE_LINK_OVERRIDE_PHY_CFG_S	8
#define ICE_LINK_OVERRIDE_PHY_CFG_M	(0xC3 << ICE_LINK_OVERRIDE_PHY_CFG_S)
#define ICE_LINK_OVERRIDE_PAUSE_M	0x3
#define ICE_LINK_OVERRIDE_LESM_EN	BIT(6)
#define ICE_LINK_OVERRIDE_AUTO_FEC_EN	BIT(7)
	uint8_t fec_options;
#define ICE_LINK_OVERRIDE_FEC_OPT_M	0xFF
	uint8_t rsvd1;
	uint64_t phy_type_low;
	uint64_t phy_type_high;
};

#define ICE_NVM_VER_LEN	32

#define ICE_NVM_VER_LEN	32

#define ICE_MAX_TRAFFIC_CLASS	8

/* Max number of port to queue branches w.r.t topology */
#define ICE_TXSCHED_MAX_BRANCHES ICE_MAX_TRAFFIC_CLASS

#define ice_for_each_traffic_class(_i)	\
	for ((_i) = 0; (_i) < ICE_MAX_TRAFFIC_CLASS; (_i)++)

#define ICE_INVAL_TEID 0xFFFFFFFF
#define ICE_DFLT_AGG_ID 0

struct ice_sched_node {
	struct ice_sched_node *parent;
	struct ice_sched_node *sibling; /* next sibling in the same layer */
	struct ice_sched_node **children;
	struct ice_aqc_txsched_elem_data info;
	uint32_t agg_id;			/* aggregator group ID */
	uint16_t vsi_handle;
	uint8_t in_use;			/* suspended or in use */
	uint8_t tx_sched_layer;		/* Logical Layer (1-9) */
	uint8_t num_children;
	uint8_t tc_num;
	uint8_t owner;
#define ICE_SCHED_NODE_OWNER_LAN	0
#define ICE_SCHED_NODE_OWNER_AE		1
#define ICE_SCHED_NODE_OWNER_RDMA	2
};

/* Access Macros for Tx Sched Elements data */
#define ICE_TXSCHED_GET_NODE_TEID(x) le32toh((x)->info.node_teid)
#define ICE_TXSCHED_GET_PARENT_TEID(x) le32toh((x)->info.parent_teid)
#define ICE_TXSCHED_GET_CIR_RL_ID(x)	\
	le16toh((x)->info.cir_bw.bw_profile_idx)
#define ICE_TXSCHED_GET_EIR_RL_ID(x)	\
	le16toh((x)->info.eir_bw.bw_profile_idx)
#define ICE_TXSCHED_GET_SRL_ID(x) le16toh((x)->info.srl_id)
#define ICE_TXSCHED_GET_CIR_BWALLOC(x)	\
	le16toh((x)->info.cir_bw.bw_alloc)
#define ICE_TXSCHED_GET_EIR_BWALLOC(x)	\
	le16toh((x)->info.eir_bw.bw_alloc)

/* Rate limit types */
enum ice_rl_type {
	ICE_UNKNOWN_BW = 0,
	ICE_MIN_BW,		/* for CIR profile */
	ICE_MAX_BW,		/* for EIR profile */
	ICE_SHARED_BW		/* for shared profile */
};

#define ICE_SCHED_MIN_BW		500		/* in Kbps */
#define ICE_SCHED_MAX_BW		100000000	/* in Kbps */
#define ICE_SCHED_DFLT_BW		0xFFFFFFFF	/* unlimited */
#define ICE_SCHED_NO_PRIORITY		0
#define ICE_SCHED_NO_BW_WT		0
#define ICE_SCHED_DFLT_RL_PROF_ID	0
#define ICE_SCHED_NO_SHARED_RL_PROF_ID	0xFFFF
#define ICE_SCHED_DFLT_BW_WT		4
#define ICE_SCHED_INVAL_PROF_ID		0xFFFF
#define ICE_SCHED_DFLT_BURST_SIZE	(15 * 1024)	/* in bytes (15k) */

struct ice_driver_ver {
	uint8_t major_ver;
	uint8_t minor_ver;
	uint8_t build_ver;
	uint8_t subbuild_ver;
	uint8_t driver_string[32];
};

enum ice_fc_mode {
	ICE_FC_NONE = 0,
	ICE_FC_RX_PAUSE,
	ICE_FC_TX_PAUSE,
	ICE_FC_FULL,
	ICE_FC_AUTO,
	ICE_FC_PFC,
	ICE_FC_DFLT
};

enum ice_fec_mode {
	ICE_FEC_NONE = 0,
	ICE_FEC_RS,
	ICE_FEC_BASER,
	ICE_FEC_AUTO,
	ICE_FEC_DIS_AUTO
};

/* Flow control (FC) parameters */
struct ice_fc_info {
	enum ice_fc_mode current_mode;	/* FC mode in effect */
	enum ice_fc_mode req_mode;	/* FC mode requested by caller */
};

/* Option ROM version information */
struct ice_orom_info {
	uint8_t major;			/* Major version of OROM */
	uint8_t patch;			/* Patch version of OROM */
	uint16_t build;			/* Build version of OROM */
	uint32_t srev;			/* Security revision */
};

/* NVM version information */
struct ice_nvm_info {
	uint32_t eetrack;
	uint32_t srev;
	uint8_t major;
	uint8_t minor;
};

/* Minimum Security Revision information */
struct ice_minsrev_info {
	uint32_t nvm;
	uint32_t orom;
	uint8_t nvm_valid : 1;
	uint8_t orom_valid : 1;
};

/* netlist version information */
struct ice_netlist_info {
	uint32_t major;			/* major high/low */
	uint32_t minor;			/* minor high/low */
	uint32_t type;			/* type high/low */
	uint32_t rev;			/* revision high/low */
	uint32_t hash;			/* SHA-1 hash word */
	uint16_t cust_ver;			/* customer version */
};

/* Enumeration of possible flash banks for the NVM, OROM, and Netlist modules
 * of the flash image.
 */
enum ice_flash_bank {
	ICE_INVALID_FLASH_BANK,
	ICE_1ST_FLASH_BANK,
	ICE_2ND_FLASH_BANK,
};

/* Enumeration of which flash bank is desired to read from, either the active
 * bank or the inactive bank. Used to abstract 1st and 2nd bank notion from
 * code which just wants to read the active or inactive flash bank.
 */
enum ice_bank_select {
	ICE_ACTIVE_FLASH_BANK,
	ICE_INACTIVE_FLASH_BANK,
};

/* information for accessing NVM, OROM, and Netlist flash banks */
struct ice_bank_info {
	uint32_t nvm_ptr;				/* Pointer to 1st NVM bank */
	uint32_t nvm_size;				/* Size of NVM bank */
	uint32_t orom_ptr;				/* Pointer to 1st OROM bank */
	uint32_t orom_size;				/* Size of OROM bank */
	uint32_t netlist_ptr;			/* Pointer to 1st Netlist bank */
	uint32_t netlist_size;			/* Size of Netlist bank */
	enum ice_flash_bank nvm_bank;		/* Active NVM bank */
	enum ice_flash_bank orom_bank;		/* Active OROM bank */
	enum ice_flash_bank netlist_bank;	/* Active Netlist bank */
};

/* Flash Chip Information */
struct ice_flash_info {
	struct ice_orom_info orom;	/* Option ROM version info */
	struct ice_nvm_info nvm;	/* NVM version information */
	struct ice_netlist_info netlist;/* Netlist version info */
	struct ice_bank_info banks;	/* Flash Bank information */
	uint16_t sr_words;			/* Shadow RAM size in words */
	uint32_t flash_size;			/* Size of available flash in bytes */
	uint8_t blank_nvm_mode;		/* is NVM empty (no FW present) */
};

/* Checksum and Shadow RAM pointers */
#define ICE_SR_NVM_CTRL_WORD			0x00
#define ICE_SR_PHY_ANALOG_PTR			0x04
#define ICE_SR_OPTION_ROM_PTR			0x05
#define ICE_SR_RO_PCIR_REGS_AUTO_LOAD_PTR	0x06
#define ICE_SR_AUTO_GENERATED_POINTERS_PTR	0x07
#define ICE_SR_PCIR_REGS_AUTO_LOAD_PTR		0x08
#define ICE_SR_EMP_GLOBAL_MODULE_PTR		0x09
#define ICE_SR_EMP_IMAGE_PTR			0x0B
#define ICE_SR_PE_IMAGE_PTR			0x0C
#define ICE_SR_CSR_PROTECTED_LIST_PTR		0x0D
#define ICE_SR_MNG_CFG_PTR			0x0E
#define ICE_SR_EMP_MODULE_PTR			0x0F
#define ICE_SR_PBA_BLOCK_PTR			0x16
#define ICE_SR_BOOT_CFG_PTR			0x132
#define ICE_SR_NVM_WOL_CFG			0x19
#define ICE_NVM_OROM_VER_OFF			0x02
#define ICE_SR_NVM_DEV_STARTER_VER		0x18
#define ICE_SR_ALTERNATE_SAN_MAC_ADDR_PTR	0x27
#define ICE_SR_PERMANENT_SAN_MAC_ADDR_PTR	0x28
#define ICE_SR_NVM_MAP_VER			0x29
#define ICE_SR_NVM_IMAGE_VER			0x2A
#define ICE_SR_NVM_STRUCTURE_VER		0x2B
#define ICE_SR_NVM_EETRACK_LO			0x2D
#define ICE_SR_NVM_EETRACK_HI			0x2E
#define ICE_NVM_VER_LO_SHIFT			0
#define ICE_NVM_VER_LO_MASK			(0xff << ICE_NVM_VER_LO_SHIFT)
#define ICE_NVM_VER_HI_SHIFT			12
#define ICE_NVM_VER_HI_MASK			(0xf << ICE_NVM_VER_HI_SHIFT)
#define ICE_OEM_EETRACK_ID			0xffffffff
#define ICE_OROM_VER_PATCH_SHIFT		0
#define ICE_OROM_VER_PATCH_MASK		(0xff << ICE_OROM_VER_PATCH_SHIFT)
#define ICE_OROM_VER_BUILD_SHIFT		8
#define ICE_OROM_VER_BUILD_MASK		(0xffff << ICE_OROM_VER_BUILD_SHIFT)
#define ICE_OROM_VER_SHIFT			24
#define ICE_OROM_VER_MASK			(0xff << ICE_OROM_VER_SHIFT)
#define ICE_SR_VPD_PTR				0x2F
#define ICE_SR_PXE_SETUP_PTR			0x30
#define ICE_SR_PXE_CFG_CUST_OPTIONS_PTR		0x31
#define ICE_SR_NVM_ORIGINAL_EETRACK_LO		0x34
#define ICE_SR_NVM_ORIGINAL_EETRACK_HI		0x35
#define ICE_SR_VLAN_CFG_PTR			0x37
#define ICE_SR_POR_REGS_AUTO_LOAD_PTR		0x38
#define ICE_SR_EMPR_REGS_AUTO_LOAD_PTR		0x3A
#define ICE_SR_GLOBR_REGS_AUTO_LOAD_PTR		0x3B
#define ICE_SR_CORER_REGS_AUTO_LOAD_PTR		0x3C
#define ICE_SR_PHY_CFG_SCRIPT_PTR		0x3D
#define ICE_SR_PCIE_ALT_AUTO_LOAD_PTR		0x3E
#define ICE_SR_SW_CHECKSUM_WORD			0x3F
#define ICE_SR_PFA_PTR				0x40
#define ICE_SR_1ST_SCRATCH_PAD_PTR		0x41
#define ICE_SR_1ST_NVM_BANK_PTR			0x42
#define ICE_SR_NVM_BANK_SIZE			0x43
#define ICE_SR_1ST_OROM_BANK_PTR		0x44
#define ICE_SR_OROM_BANK_SIZE			0x45
#define ICE_SR_NETLIST_BANK_PTR			0x46
#define ICE_SR_NETLIST_BANK_SIZE		0x47
#define ICE_SR_EMP_SR_SETTINGS_PTR		0x48
#define ICE_SR_CONFIGURATION_METADATA_PTR	0x4D
#define ICE_SR_IMMEDIATE_VALUES_PTR		0x4E
#define ICE_SR_LINK_DEFAULT_OVERRIDE_PTR	0x134
#define ICE_SR_POR_REGISTERS_AUTOLOAD_PTR	0x118

/* CSS Header words */
#define ICE_NVM_CSS_HDR_LEN_L			0x02
#define ICE_NVM_CSS_HDR_LEN_H			0x03
#define ICE_NVM_CSS_SREV_L			0x14
#define ICE_NVM_CSS_SREV_H			0x15

/* Length of Authentication header section in words */
#define ICE_NVM_AUTH_HEADER_LEN			0x08

/* The Link Topology Netlist section is stored as a series of words. It is
 * stored in the NVM as a TLV, with the first two words containing the type
 * and length.
 */
#define ICE_NETLIST_LINK_TOPO_MOD_ID		0x011B
#define ICE_NETLIST_TYPE_OFFSET			0x0000
#define ICE_NETLIST_LEN_OFFSET			0x0001

/* The Link Topology section follows the TLV header. When reading the netlist
 * using ice_read_netlist_module, we need to account for the 2-word TLV
 * header.
 */
#define ICE_NETLIST_LINK_TOPO_OFFSET(n)		((n) + 2)

#define ICE_LINK_TOPO_MODULE_LEN		ICE_NETLIST_LINK_TOPO_OFFSET(0x0000)
#define ICE_LINK_TOPO_NODE_COUNT		ICE_NETLIST_LINK_TOPO_OFFSET(0x0001)

#define ICE_LINK_TOPO_NODE_COUNT_M		MAKEMASK(0x3FF, 0)

/* The Netlist ID Block is located after all of the Link Topology nodes. */
#define ICE_NETLIST_ID_BLK_SIZE			0x30
#define ICE_NETLIST_ID_BLK_OFFSET(n)		ICE_NETLIST_LINK_TOPO_OFFSET(0x0004 + 2 * (n))

/* netlist ID block field offsets (word offsets) */
#define ICE_NETLIST_ID_BLK_MAJOR_VER_LOW	0x02
#define ICE_NETLIST_ID_BLK_MAJOR_VER_HIGH	0x03
#define ICE_NETLIST_ID_BLK_MINOR_VER_LOW	0x04
#define ICE_NETLIST_ID_BLK_MINOR_VER_HIGH	0x05
#define ICE_NETLIST_ID_BLK_TYPE_LOW		0x06
#define ICE_NETLIST_ID_BLK_TYPE_HIGH		0x07
#define ICE_NETLIST_ID_BLK_REV_LOW		0x08
#define ICE_NETLIST_ID_BLK_REV_HIGH		0x09
#define ICE_NETLIST_ID_BLK_SHA_HASH_WORD(n)	(0x0A + (n))
#define ICE_NETLIST_ID_BLK_CUST_VER		0x2F

/* Auxiliary field, mask and shift definition for Shadow RAM and NVM Flash */
#define ICE_SR_VPD_SIZE_WORDS		512
#define ICE_SR_PCIE_ALT_SIZE_WORDS	512
#define ICE_SR_CTRL_WORD_1_S		0x06
#define ICE_SR_CTRL_WORD_1_M		(0x03 << ICE_SR_CTRL_WORD_1_S)
#define ICE_SR_CTRL_WORD_VALID		0x1
#define ICE_SR_CTRL_WORD_OROM_BANK	BIT(3)
#define ICE_SR_CTRL_WORD_NETLIST_BANK	BIT(4)
#define ICE_SR_CTRL_WORD_NVM_BANK	BIT(5)

#define ICE_SR_NVM_PTR_4KB_UNITS	BIT(15)

/* Shadow RAM related */
#define ICE_SR_SECTOR_SIZE_IN_WORDS	0x800
#define ICE_SR_BUF_ALIGNMENT		4096
#define ICE_SR_WORDS_IN_1KB		512
/* Checksum should be calculated such that after adding all the words,
 * including the checksum word itself, the sum should be 0xBABA.
 */
#define ICE_SR_SW_CHECKSUM_BASE		0xBABA

/* Link override related */
#define ICE_SR_PFA_LINK_OVERRIDE_WORDS		10
#define ICE_SR_PFA_LINK_OVERRIDE_PHY_WORDS	4
#define ICE_SR_PFA_LINK_OVERRIDE_OFFSET		2
#define ICE_SR_PFA_LINK_OVERRIDE_FEC_OFFSET	1
#define ICE_SR_PFA_LINK_OVERRIDE_PHY_OFFSET	2
#define ICE_FW_API_LINK_OVERRIDE_MAJ		1
#define ICE_FW_API_LINK_OVERRIDE_MIN		5
#define ICE_FW_API_LINK_OVERRIDE_PATCH		2

#define ICE_PBA_FLAG_DFLT		0xFAFA
/* Hash redirection LUT for VSI - maximum array size */
#define ICE_VSIQF_HLUT_ARRAY_SIZE	((VSIQF_HLUT_MAX_INDEX + 1) * 4)

/*
 * Defines for values in the VF_PE_DB_SIZE bits in the GLPCI_LBARCTRL register.
 * This is needed to determine the BAR0 space for the VFs
 */
#define GLPCI_LBARCTRL_VF_PE_DB_SIZE_0KB 0x0
#define GLPCI_LBARCTRL_VF_PE_DB_SIZE_8KB 0x1
#define GLPCI_LBARCTRL_VF_PE_DB_SIZE_64KB 0x2

/* AQ API version for LLDP_FILTER_CONTROL */
#define ICE_FW_API_LLDP_FLTR_MAJ	1
#define ICE_FW_API_LLDP_FLTR_MIN	7
#define ICE_FW_API_LLDP_FLTR_PATCH	1

/* AQ API version for report default configuration */
#define ICE_FW_API_REPORT_DFLT_CFG_MAJ		1
#define ICE_FW_API_REPORT_DFLT_CFG_MIN		7
#define ICE_FW_API_REPORT_DFLT_CFG_PATCH	3

/* FW branch number for hardware families */
#define ICE_FW_VER_BRANCH_E82X			0
#define ICE_FW_VER_BRANCH_E810			1

/* FW version for FEC disable in Auto FEC mode */
#define ICE_FW_FEC_DIS_AUTO_MAJ			7
#define ICE_FW_FEC_DIS_AUTO_MIN			0
#define ICE_FW_FEC_DIS_AUTO_PATCH		5
#define ICE_FW_FEC_DIS_AUTO_MAJ_E82X		7
#define ICE_FW_FEC_DIS_AUTO_MIN_E82X		1
#define ICE_FW_FEC_DIS_AUTO_PATCH_E82X		2

/* AQ API version for FW health reports */
#define ICE_FW_API_HEALTH_REPORT_MAJ		1
#define ICE_FW_API_HEALTH_REPORT_MIN		7
#define ICE_FW_API_HEALTH_REPORT_PATCH		6

/* AQ API version for FW auto drop reports */
#define ICE_FW_API_AUTO_DROP_MAJ		1
#define ICE_FW_API_AUTO_DROP_MIN		4

/* Function specific capabilities */
struct ice_hw_func_caps {
	struct ice_hw_common_caps common_cap;
	uint32_t num_allocd_vfs;		/* Number of allocated VFs */
	uint32_t vf_base_id;			/* Logical ID of the first VF */
	uint32_t guar_num_vsi;
};

struct ice_nac_topology {
	uint32_t mode;
	uint8_t id;
};

/* Device wide capabilities */
struct ice_hw_dev_caps {
	struct ice_hw_common_caps common_cap;
	uint32_t num_vfs_exposed;		/* Total number of VFs exposed */
	uint32_t num_vsi_allocd_to_host;	/* Excluding EMP VSI */
	uint32_t num_funcs;
	struct ice_nac_topology nac_topo;
	/* bitmap of supported sensors */
	uint32_t supported_sensors;
#define ICE_SENSOR_SUPPORT_E810_INT_TEMP	BIT(0)
};

#define SCHED_NODE_NAME_MAX_LEN 32

#define ICE_SCHED_5_LAYERS	5
#define ICE_SCHED_9_LAYERS	9

#define ICE_QGRP_LAYER_OFFSET	2
#define ICE_VSI_LAYER_OFFSET	4
#define ICE_AGG_LAYER_OFFSET	6
#define ICE_SCHED_INVAL_LAYER_NUM	0xFF
/* Burst size is a 12 bits register that is configured while creating the RL
 * profile(s). MSB is a granularity bit and tells the granularity type
 * 0 - LSB bits are in 64 bytes granularity
 * 1 - LSB bits are in 1K bytes granularity
 */
#define ICE_64_BYTE_GRANULARITY			0
#define ICE_KBYTE_GRANULARITY			BIT(11)
#define ICE_MIN_BURST_SIZE_ALLOWED		64 /* In Bytes */
#define ICE_MAX_BURST_SIZE_ALLOWED \
	((BIT(11) - 1) * 1024) /* In Bytes */
#define ICE_MAX_BURST_SIZE_64_BYTE_GRANULARITY \
	((BIT(11) - 1) * 64) /* In Bytes */
#define ICE_MAX_BURST_SIZE_KBYTE_GRANULARITY	ICE_MAX_BURST_SIZE_ALLOWED

#define ICE_RL_PROF_ACCURACY_BYTES 128
#define ICE_RL_PROF_MULTIPLIER 10000
#define ICE_RL_PROF_TS_MULTIPLIER 32
#define ICE_RL_PROF_FRACTION 512

#define ICE_PSM_CLK_367MHZ_IN_HZ 367647059
#define ICE_PSM_CLK_416MHZ_IN_HZ 416666667
#define ICE_PSM_CLK_446MHZ_IN_HZ 446428571
#define ICE_PSM_CLK_390MHZ_IN_HZ 390625000

#define PSM_CLK_SRC_367_MHZ 0x0
#define PSM_CLK_SRC_416_MHZ 0x1
#define PSM_CLK_SRC_446_MHZ 0x2
#define PSM_CLK_SRC_390_MHZ 0x3

#define ICE_SCHED_MIN_BW		500		/* in Kbps */
#define ICE_SCHED_MAX_BW		100000000	/* in Kbps */
#define ICE_SCHED_DFLT_BW		0xFFFFFFFF	/* unlimited */
#define ICE_SCHED_NO_PRIORITY		0
#define ICE_SCHED_NO_BW_WT		0
#define ICE_SCHED_DFLT_RL_PROF_ID	0
#define ICE_SCHED_NO_SHARED_RL_PROF_ID	0xFFFF
#define ICE_SCHED_DFLT_BW_WT		4
#define ICE_SCHED_INVAL_PROF_ID		0xFFFF
#define ICE_SCHED_DFLT_BURST_SIZE	(15 * 1024)	/* in bytes (15k) */

/* Access Macros for Tx Sched RL Profile data */
#define ICE_TXSCHED_GET_RL_PROF_ID(p) le16toh((p)->info.profile_id)
#define ICE_TXSCHED_GET_RL_MBS(p) le16toh((p)->info.max_burst_size)
#define ICE_TXSCHED_GET_RL_MULTIPLIER(p) le16toh((p)->info.rl_multiply)
#define ICE_TXSCHED_GET_RL_WAKEUP_MV(p) le16toh((p)->info.wake_up_calc)
#define ICE_TXSCHED_GET_RL_ENCODE(p) le16toh((p)->info.rl_encode)

#define ICE_MAX_PORT_PER_PCI_DEV	8

/* The following tree example shows the naming conventions followed under
 * ice_port_info struct for default scheduler tree topology.
 *
 *                 A tree on a port
 *                       *                ---> root node
 *        (TC0)/  /  /  / \  \  \  \(TC7) ---> num_branches (range:1- 8)
 *            *  *  *  *   *  *  *  *     |
 *           /                            |
 *          *                             |
 *         /                              |-> num_elements (range:1 - 9)
 *        *                               |   implies num_of_layers
 *       /                                |
 *   (a)*                                 |
 *
 *  (a) is the last_node_teid(not of type Leaf). A leaf node is created under
 *  (a) as child node where queues get added, add Tx/Rx queue admin commands;
 *  need TEID of (a) to add queues.
 *
 *  This tree
 *       -> has 8 branches (one for each TC)
 *       -> First branch (TC0) has 4 elements
 *       -> has 4 layers
 *       -> (a) is the topmost layer node created by firmware on branch 0
 *
 *  Note: Above asterisk tree covers only basic terminology and scenario.
 *  Refer to the documentation for more info.
 */

 /* Data structure for saving BW information */
enum ice_bw_type {
	ICE_BW_TYPE_PRIO,
	ICE_BW_TYPE_CIR,
	ICE_BW_TYPE_CIR_WT,
	ICE_BW_TYPE_EIR,
	ICE_BW_TYPE_EIR_WT,
	ICE_BW_TYPE_SHARED,
	ICE_BW_TYPE_CNT		/* This must be last */
};

struct ice_bw {
	uint32_t bw;
	uint16_t bw_alloc;
};

struct ice_bw_type_info {
	ice_declare_bitmap(bw_t_bitmap, ICE_BW_TYPE_CNT);
	uint8_t generic;
	struct ice_bw cir_bw;
	struct ice_bw eir_bw;
	uint32_t shared_bw;
};

/* VSI queue context structure for given TC */
struct ice_q_ctx {
	uint16_t  q_handle;
	uint32_t  q_teid;
	/* bw_t_info saves queue BW information */
	struct ice_bw_type_info bw_t_info;
};

struct ice_sched_agg_vsi_info {
	TAILQ_ENTRY(ice_sched_agg_vsi_info) list_entry;
	ice_declare_bitmap(tc_bitmap, ICE_MAX_TRAFFIC_CLASS);
	uint16_t vsi_handle;
	/* save aggregator VSI TC bitmap */
	ice_declare_bitmap(replay_tc_bitmap, ICE_MAX_TRAFFIC_CLASS);
};

/* VSI type list entry to locate corresponding VSI/aggregator nodes */
struct ice_sched_vsi_info {
	struct ice_sched_node *vsi_node[ICE_MAX_TRAFFIC_CLASS];
	struct ice_sched_node *ag_node[ICE_MAX_TRAFFIC_CLASS];
	uint16_t max_lanq[ICE_MAX_TRAFFIC_CLASS];
	uint16_t max_rdmaq[ICE_MAX_TRAFFIC_CLASS];
	/* bw_t_info saves VSI BW information */
	struct ice_bw_type_info bw_t_info[ICE_MAX_TRAFFIC_CLASS];
};

/* The aggregator type determines if identifier is for a VSI group,
 * aggregator group, aggregator of queues, or queue group.
 */
enum ice_agg_type {
	ICE_AGG_TYPE_UNKNOWN = 0,
	ICE_AGG_TYPE_TC,
	ICE_AGG_TYPE_AGG, /* aggregator */
	ICE_AGG_TYPE_VSI,
	ICE_AGG_TYPE_QG,
	ICE_AGG_TYPE_Q
};

TAILQ_HEAD(ice_vsi_list_head, ice_sched_agg_vsi_info);

/*
 * For now, set this to the hardware maximum. Each function gets a smaller
 * number assigned to it in hw->func_caps.guar_num_vsi, though there
 * appears to be no guarantee that is the maximum number that a function
 * can use.
 */
#define ICE_MAX_VSI_AVAILABLE	768

struct ice_sched_agg_info {
	struct ice_vsi_list_head agg_vsi_list;
	TAILQ_ENTRY(ice_sched_agg_info) list_entry;
	ice_declare_bitmap(tc_bitmap, ICE_MAX_TRAFFIC_CLASS);
	uint32_t agg_id;
	enum ice_agg_type agg_type;
	/* bw_t_info saves aggregator BW information */
	struct ice_bw_type_info bw_t_info[ICE_MAX_TRAFFIC_CLASS];
	/* save aggregator TC bitmap */
	ice_declare_bitmap(replay_tc_bitmap, ICE_MAX_TRAFFIC_CLASS);
};

#define ICE_DCBX_OFFLOAD_DIS		0
#define ICE_DCBX_OFFLOAD_ENABLED	1

#define ICE_DCBX_STATUS_NOT_STARTED	0
#define ICE_DCBX_STATUS_IN_PROGRESS	1
#define ICE_DCBX_STATUS_DONE		2
#define ICE_DCBX_STATUS_MULTIPLE_PEERS	3
#define ICE_DCBX_STATUS_DIS		7

#define ICE_TLV_TYPE_END		0
#define ICE_TLV_TYPE_ORG		127

#define ICE_IEEE_8021QAZ_OUI		0x0080C2
#define ICE_IEEE_SUBTYPE_ETS_CFG	9
#define ICE_IEEE_SUBTYPE_ETS_REC	10
#define ICE_IEEE_SUBTYPE_PFC_CFG	11
#define ICE_IEEE_SUBTYPE_APP_PRI	12

#define ICE_CEE_DCBX_OUI		0x001B21
#define ICE_CEE_DCBX_TYPE		2

#define ICE_DSCP_OUI			0xFFFFFF
#define ICE_DSCP_SUBTYPE_DSCP2UP	0x41
#define ICE_DSCP_SUBTYPE_ENFORCE	0x42
#define ICE_DSCP_SUBTYPE_TCBW		0x43
#define ICE_DSCP_SUBTYPE_PFC		0x44
#define ICE_DSCP_IPV6_OFFSET		80

#define ICE_CEE_SUBTYPE_CTRL		1
#define ICE_CEE_SUBTYPE_PG_CFG		2
#define ICE_CEE_SUBTYPE_PFC_CFG		3
#define ICE_CEE_SUBTYPE_APP_PRI		4

#define ICE_CEE_MAX_FEAT_TYPE		3
#define ICE_LLDP_ADMINSTATUS_DIS	0
#define ICE_LLDP_ADMINSTATUS_ENA_RX	1
#define ICE_LLDP_ADMINSTATUS_ENA_TX	2
#define ICE_LLDP_ADMINSTATUS_ENA_RXTX	3

/* Defines for LLDP TLV header */
#define ICE_LLDP_TLV_LEN_S		0
#define ICE_LLDP_TLV_LEN_M		(0x01FF << ICE_LLDP_TLV_LEN_S)
#define ICE_LLDP_TLV_TYPE_S		9
#define ICE_LLDP_TLV_TYPE_M		(0x7F << ICE_LLDP_TLV_TYPE_S)
#define ICE_LLDP_TLV_SUBTYPE_S		0
#define ICE_LLDP_TLV_SUBTYPE_M		(0xFF << ICE_LLDP_TLV_SUBTYPE_S)
#define ICE_LLDP_TLV_OUI_S		8
#define ICE_LLDP_TLV_OUI_M		(0xFFFFFFUL << ICE_LLDP_TLV_OUI_S)

/* Defines for IEEE ETS TLV */
#define ICE_IEEE_ETS_MAXTC_S	0
#define ICE_IEEE_ETS_MAXTC_M		(0x7 << ICE_IEEE_ETS_MAXTC_S)
#define ICE_IEEE_ETS_CBS_S		6
#define ICE_IEEE_ETS_CBS_M		BIT(ICE_IEEE_ETS_CBS_S)
#define ICE_IEEE_ETS_WILLING_S		7
#define ICE_IEEE_ETS_WILLING_M		BIT(ICE_IEEE_ETS_WILLING_S)
#define ICE_IEEE_ETS_PRIO_0_S		0
#define ICE_IEEE_ETS_PRIO_0_M		(0x7 << ICE_IEEE_ETS_PRIO_0_S)
#define ICE_IEEE_ETS_PRIO_1_S		4
#define ICE_IEEE_ETS_PRIO_1_M		(0x7 << ICE_IEEE_ETS_PRIO_1_S)
#define ICE_CEE_PGID_PRIO_0_S		0
#define ICE_CEE_PGID_PRIO_0_M		(0xF << ICE_CEE_PGID_PRIO_0_S)
#define ICE_CEE_PGID_PRIO_1_S		4
#define ICE_CEE_PGID_PRIO_1_M		(0xF << ICE_CEE_PGID_PRIO_1_S)
#define ICE_CEE_PGID_STRICT		15

/* Defines for IEEE TSA types */
#define ICE_IEEE_TSA_STRICT		0
#define ICE_IEEE_TSA_CBS		1
#define ICE_IEEE_TSA_ETS		2
#define ICE_IEEE_TSA_VENDOR		255

/* Defines for IEEE PFC TLV */
#define ICE_IEEE_PFC_CAP_S		0
#define ICE_IEEE_PFC_CAP_M		(0xF << ICE_IEEE_PFC_CAP_S)
#define ICE_IEEE_PFC_MBC_S		6
#define ICE_IEEE_PFC_MBC_M		BIT(ICE_IEEE_PFC_MBC_S)
#define ICE_IEEE_PFC_WILLING_S		7
#define ICE_IEEE_PFC_WILLING_M		BIT(ICE_IEEE_PFC_WILLING_S)

/* Defines for IEEE APP TLV */
#define ICE_IEEE_APP_SEL_S		0
#define ICE_IEEE_APP_SEL_M		(0x7 << ICE_IEEE_APP_SEL_S)
#define ICE_IEEE_APP_PRIO_S		5
#define ICE_IEEE_APP_PRIO_M		(0x7 << ICE_IEEE_APP_PRIO_S)

/* TLV definitions for preparing MIB */
#define ICE_TLV_ID_CHASSIS_ID		0
#define ICE_TLV_ID_PORT_ID		1
#define ICE_TLV_ID_TIME_TO_LIVE		2
#define ICE_IEEE_TLV_ID_ETS_CFG		3
#define ICE_IEEE_TLV_ID_ETS_REC		4
#define ICE_IEEE_TLV_ID_PFC_CFG		5
#define ICE_IEEE_TLV_ID_APP_PRI		6
#define ICE_TLV_ID_END_OF_LLDPPDU	7
#define ICE_TLV_ID_START		ICE_IEEE_TLV_ID_ETS_CFG
#define ICE_TLV_ID_DSCP_UP		3
#define ICE_TLV_ID_DSCP_ENF		4
#define ICE_TLV_ID_DSCP_TC_BW		5
#define ICE_TLV_ID_DSCP_TO_PFC		6

#define ICE_IEEE_ETS_TLV_LEN		25
#define ICE_IEEE_PFC_TLV_LEN		6
#define ICE_IEEE_APP_TLV_LEN		11

#define ICE_DSCP_UP_TLV_LEN		148
#define ICE_DSCP_ENF_TLV_LEN		132
#define ICE_DSCP_TC_BW_TLV_LEN		25
#define ICE_DSCP_PFC_TLV_LEN		6

/* IEEE 802.1AB LLDP Organization specific TLV */
struct ice_lldp_org_tlv {
	uint16_t typelen;
	uint32_t ouisubtype;
	uint8_t tlvinfo[STRUCT_HACK_VAR_LEN];
} __packed;

struct ice_cee_tlv_hdr {
	uint16_t typelen;
	uint8_t operver;
	uint8_t maxver;
};

struct ice_cee_ctrl_tlv {
	struct ice_cee_tlv_hdr hdr;
	uint32_t seqno;
	uint32_t ackno;
};

struct ice_cee_feat_tlv {
	struct ice_cee_tlv_hdr hdr;
	uint8_t en_will_err; /* Bits: |En|Will|Err|Reserved(5)| */
#define ICE_CEE_FEAT_TLV_ENA_M		0x80
#define ICE_CEE_FEAT_TLV_WILLING_M	0x40
#define ICE_CEE_FEAT_TLV_ERR_M		0x20
	uint8_t subtype;
	uint8_t tlvinfo[STRUCT_HACK_VAR_LEN];
};

struct ice_cee_app_prio {
	uint16_t protocol;
	uint8_t upper_oui_sel; /* Bits: |Upper OUI(6)|Selector(2)| */
#define ICE_CEE_APP_SELECTOR_M	0x03
	uint16_t lower_oui;
	uint8_t prio_map;
} __packed;

/* CEE or IEEE 802.1Qaz ETS Configuration data */
struct ice_dcb_ets_cfg {
	uint8_t willing;
	uint8_t cbs;
	uint8_t maxtcs;
	uint8_t prio_table[ICE_MAX_TRAFFIC_CLASS];
	uint8_t tcbwtable[ICE_MAX_TRAFFIC_CLASS];
	uint8_t tsatable[ICE_MAX_TRAFFIC_CLASS];
};

/* CEE or IEEE 802.1Qaz PFC Configuration data */
struct ice_dcb_pfc_cfg {
	uint8_t willing;
	uint8_t mbc;
	uint8_t pfccap;
	uint8_t pfcena;
};

/* CEE or IEEE 802.1Qaz Application Priority data */
struct ice_dcb_app_priority_table {
	uint16_t prot_id;
	uint8_t priority;
	uint8_t selector;
};

#define ICE_MAX_USER_PRIORITY		8
#define ICE_DCBX_MAX_APPS		64
#define ICE_DSCP_NUM_VAL		64
#define ICE_LLDPDU_SIZE			1500
#define ICE_TLV_STATUS_OPER		0x1
#define ICE_TLV_STATUS_SYNC		0x2
#define ICE_TLV_STATUS_ERR		0x4
#define ICE_APP_PROT_ID_FCOE		0x8906
#define ICE_APP_PROT_ID_ISCSI		0x0cbc
#define ICE_APP_PROT_ID_ISCSI_860	0x035c
#define ICE_APP_PROT_ID_FIP		0x8914
#define ICE_APP_SEL_ETHTYPE		0x1
#define ICE_APP_SEL_TCPIP		0x2
#define ICE_CEE_APP_SEL_ETHTYPE		0x0
#define ICE_CEE_APP_SEL_TCPIP		0x1

struct ice_dcbx_cfg {
	uint32_t numapps;
	uint32_t tlv_status; /* CEE mode TLV status */
	struct ice_dcb_ets_cfg etscfg;
	struct ice_dcb_ets_cfg etsrec;
	struct ice_dcb_pfc_cfg pfc;
#define ICE_QOS_MODE_VLAN	0x0
#define ICE_QOS_MODE_DSCP	0x1
	uint8_t pfc_mode;
	struct ice_dcb_app_priority_table app[ICE_DCBX_MAX_APPS];
	/* when DSCP mapping defined by user set its bit to 1 */
	ice_declare_bitmap(dscp_mapped, ICE_DSCP_NUM_VAL);
	/* array holding DSCP -> UP/TC values for DSCP L3 QoS mode */
	uint8_t dscp_map[ICE_DSCP_NUM_VAL];
	uint8_t dcbx_mode;
#define ICE_DCBX_MODE_CEE	0x1
#define ICE_DCBX_MODE_IEEE	0x2
	uint8_t app_mode;
#define ICE_DCBX_APPS_NON_WILLING	0x1
};

struct ice_qos_cfg {
	struct ice_dcbx_cfg local_dcbx_cfg;	/* Oper/Local Cfg */
	struct ice_dcbx_cfg desired_dcbx_cfg;	/* CEE Desired Cfg */
	struct ice_dcbx_cfg remote_dcbx_cfg;	/* Peer Cfg */
	uint8_t dcbx_status : 3;			/* see ICE_DCBX_STATUS_DIS */
	uint8_t is_sw_lldp : 1;
};

/* Information about MAC such as address, etc... */
struct ice_mac_info {
	uint8_t lan_addr[ETHER_ADDR_LEN];
	uint8_t perm_addr[ETHER_ADDR_LEN];
	uint8_t port_addr[ETHER_ADDR_LEN];
	uint8_t wol_addr[ETHER_ADDR_LEN];
};

/* Media Types */
enum ice_media_type {
	ICE_MEDIA_NONE = 0,
	ICE_MEDIA_UNKNOWN,
	ICE_MEDIA_FIBER,
	ICE_MEDIA_BASET,
	ICE_MEDIA_BACKPLANE,
	ICE_MEDIA_DA,
	ICE_MEDIA_AUI,
};

#define ICE_MEDIA_BASET_PHY_TYPE_LOW_M	(ICE_PHY_TYPE_LOW_100BASE_TX | \
					 ICE_PHY_TYPE_LOW_1000BASE_T | \
					 ICE_PHY_TYPE_LOW_2500BASE_T | \
					 ICE_PHY_TYPE_LOW_5GBASE_T | \
					 ICE_PHY_TYPE_LOW_10GBASE_T | \
					 ICE_PHY_TYPE_LOW_25GBASE_T)

#define ICE_MEDIA_C2M_PHY_TYPE_LOW_M	(ICE_PHY_TYPE_LOW_10G_SFI_AOC_ACC | \
					 ICE_PHY_TYPE_LOW_25G_AUI_AOC_ACC | \
					 ICE_PHY_TYPE_LOW_40G_XLAUI_AOC_ACC | \
					 ICE_PHY_TYPE_LOW_50G_LAUI2_AOC_ACC | \
					 ICE_PHY_TYPE_LOW_50G_AUI2_AOC_ACC | \
					 ICE_PHY_TYPE_LOW_50G_AUI1_AOC_ACC | \
					 ICE_PHY_TYPE_LOW_100G_CAUI4_AOC_ACC | \
					 ICE_PHY_TYPE_LOW_100G_AUI4_AOC_ACC)

#define ICE_MEDIA_C2M_PHY_TYPE_HIGH_M (ICE_PHY_TYPE_HIGH_100G_CAUI2_AOC_ACC | \
				       ICE_PHY_TYPE_HIGH_100G_AUI2_AOC_ACC)

#define ICE_MEDIA_OPT_PHY_TYPE_LOW_M	(ICE_PHY_TYPE_LOW_1000BASE_SX | \
					 ICE_PHY_TYPE_LOW_1000BASE_LX | \
					 ICE_PHY_TYPE_LOW_10GBASE_SR | \
					 ICE_PHY_TYPE_LOW_10GBASE_LR | \
					 ICE_PHY_TYPE_LOW_25GBASE_SR | \
					 ICE_PHY_TYPE_LOW_25GBASE_LR | \
					 ICE_PHY_TYPE_LOW_40GBASE_SR4 | \
					 ICE_PHY_TYPE_LOW_40GBASE_LR4 | \
					 ICE_PHY_TYPE_LOW_50GBASE_SR2 | \
					 ICE_PHY_TYPE_LOW_50GBASE_LR2 | \
					 ICE_PHY_TYPE_LOW_50GBASE_SR | \
					 ICE_PHY_TYPE_LOW_50GBASE_LR | \
					 ICE_PHY_TYPE_LOW_100GBASE_SR4 | \
					 ICE_PHY_TYPE_LOW_100GBASE_LR4 | \
					 ICE_PHY_TYPE_LOW_100GBASE_SR2 | \
					 ICE_PHY_TYPE_LOW_50GBASE_FR | \
					 ICE_PHY_TYPE_LOW_100GBASE_DR)

#define ICE_MEDIA_BP_PHY_TYPE_LOW_M	(ICE_PHY_TYPE_LOW_1000BASE_KX | \
					 ICE_PHY_TYPE_LOW_2500BASE_KX | \
					 ICE_PHY_TYPE_LOW_5GBASE_KR | \
					 ICE_PHY_TYPE_LOW_10GBASE_KR_CR1 | \
					 ICE_PHY_TYPE_LOW_25GBASE_KR | \
					 ICE_PHY_TYPE_LOW_25GBASE_KR_S | \
					 ICE_PHY_TYPE_LOW_25GBASE_KR1 | \
					 ICE_PHY_TYPE_LOW_40GBASE_KR4 | \
					 ICE_PHY_TYPE_LOW_50GBASE_KR2 | \
					 ICE_PHY_TYPE_LOW_50GBASE_KR_PAM4 | \
					 ICE_PHY_TYPE_LOW_100GBASE_KR4 | \
					 ICE_PHY_TYPE_LOW_100GBASE_KR_PAM4)

#define ICE_MEDIA_BP_PHY_TYPE_HIGH_M    ICE_PHY_TYPE_HIGH_100GBASE_KR2_PAM4

#define ICE_MEDIA_DAC_PHY_TYPE_LOW_M	(ICE_PHY_TYPE_LOW_10G_SFI_DA | \
					 ICE_PHY_TYPE_LOW_25GBASE_CR | \
					 ICE_PHY_TYPE_LOW_25GBASE_CR_S | \
					 ICE_PHY_TYPE_LOW_25GBASE_CR1 | \
					 ICE_PHY_TYPE_LOW_40GBASE_CR4 | \
					 ICE_PHY_TYPE_LOW_50GBASE_CR2 | \
					 ICE_PHY_TYPE_LOW_100GBASE_CR4 | \
					 ICE_PHY_TYPE_LOW_100GBASE_CR_PAM4 | \
					 ICE_PHY_TYPE_LOW_50GBASE_CP | \
					 ICE_PHY_TYPE_LOW_100GBASE_CP2)

#define ICE_MEDIA_C2C_PHY_TYPE_LOW_M	(ICE_PHY_TYPE_LOW_100M_SGMII | \
					 ICE_PHY_TYPE_LOW_1G_SGMII | \
					 ICE_PHY_TYPE_LOW_2500BASE_X | \
					 ICE_PHY_TYPE_LOW_10G_SFI_C2C | \
					 ICE_PHY_TYPE_LOW_25G_AUI_C2C | \
					 ICE_PHY_TYPE_LOW_40G_XLAUI | \
					 ICE_PHY_TYPE_LOW_50G_LAUI2 | \
					 ICE_PHY_TYPE_LOW_50G_AUI2 | \
					 ICE_PHY_TYPE_LOW_50G_AUI1 | \
					 ICE_PHY_TYPE_LOW_100G_CAUI4 | \
					 ICE_PHY_TYPE_LOW_100G_AUI4)

#define ICE_MEDIA_C2C_PHY_TYPE_HIGH_M	(ICE_PHY_TYPE_HIGH_100G_CAUI2 | \
					 ICE_PHY_TYPE_HIGH_100G_AUI2)

#define ICE_IPV6_ADDR_LENGTH 16

/* Each recipe can match up to 5 different fields. Fields to match can be meta-
 * data, values extracted from packet headers, or results from other recipes.
 * One of the 5 fields is reserved for matching the switch ID. So, up to 4
 * recipes can provide intermediate results to another one through chaining,
 * e.g. recipes 0, 1, 2, and 3 can provide intermediate results to recipe 4.
 */
#define ICE_NUM_WORDS_RECIPE 4

/* Max recipes that can be chained */
#define ICE_MAX_CHAIN_RECIPE 5

/* 1 word reserved for switch ID from allowed 5 words.
 * So a recipe can have max 4 words. And you can chain 5 such recipes
 * together. So maximum words that can be programmed for look up is 5 * 4.
 */
#define ICE_MAX_CHAIN_WORDS (ICE_NUM_WORDS_RECIPE * ICE_MAX_CHAIN_RECIPE)

/* Field vector index corresponding to chaining */
#define ICE_CHAIN_FV_INDEX_START 47

enum ice_protocol_type {
	ICE_MAC_OFOS = 0,
	ICE_MAC_IL,
	ICE_ETYPE_OL,
	ICE_ETYPE_IL,
	ICE_VLAN_OFOS,
	ICE_IPV4_OFOS,
	ICE_IPV4_IL,
	ICE_IPV6_OFOS,
	ICE_IPV6_IL,
	ICE_TCP_IL,
	ICE_UDP_OF,
	ICE_UDP_ILOS,
	ICE_SCTP_IL,
	ICE_VXLAN,
	ICE_GENEVE,
	ICE_VXLAN_GPE,
	ICE_NVGRE,
	ICE_GTP,
	ICE_GTP_NO_PAY,
	ICE_PPPOE,
	ICE_L2TPV3,
	ICE_PROTOCOL_LAST
};

enum ice_sw_tunnel_type {
	ICE_NON_TUN = 0,
	ICE_SW_TUN_AND_NON_TUN,
	ICE_SW_TUN_VXLAN_GPE,
	ICE_SW_TUN_GENEVE,      /* GENEVE matches only non-VLAN pkts */
	ICE_SW_TUN_GENEVE_VLAN, /* GENEVE matches both VLAN and non-VLAN pkts */
	ICE_SW_TUN_VXLAN,	/* VXLAN matches only non-VLAN pkts */
	ICE_SW_TUN_VXLAN_VLAN,  /* VXLAN matches both VLAN and non-VLAN pkts */
	ICE_SW_TUN_NVGRE,
	ICE_SW_TUN_UDP, /* This means all "UDP" tunnel types: VXLAN-GPE, VXLAN
			 * and GENEVE
			 */
	ICE_SW_TUN_GTPU,
	ICE_SW_TUN_GTPC,
	ICE_ALL_TUNNELS /* All tunnel types including NVGRE */
};

/* Decoders for ice_prot_id:
 * - F: First
 * - I: Inner
 * - L: Last
 * - O: Outer
 * - S: Single
 */
enum ice_prot_id {
	ICE_PROT_ID_INVAL	= 0,
	ICE_PROT_MAC_OF_OR_S	= 1,
	ICE_PROT_MAC_O2		= 2,
	ICE_PROT_MAC_IL		= 4,
	ICE_PROT_MAC_IN_MAC	= 7,
	ICE_PROT_ETYPE_OL	= 9,
	ICE_PROT_ETYPE_IL	= 10,
	ICE_PROT_PAY		= 15,
	ICE_PROT_EVLAN_O	= 16,
	ICE_PROT_VLAN_O		= 17,
	ICE_PROT_VLAN_IF	= 18,
	ICE_PROT_MPLS_OL_MINUS_1 = 27,
	ICE_PROT_MPLS_OL_OR_OS	= 28,
	ICE_PROT_MPLS_IL	= 29,
	ICE_PROT_IPV4_OF_OR_S	= 32,
	ICE_PROT_IPV4_IL	= 33,
	ICE_PROT_IPV4_IL_IL	= 34,
	ICE_PROT_IPV6_OF_OR_S	= 40,
	ICE_PROT_IPV6_IL	= 41,
	ICE_PROT_IPV6_IL_IL	= 42,
	ICE_PROT_IPV6_NEXT_PROTO = 43,
	ICE_PROT_IPV6_FRAG	= 47,
	ICE_PROT_TCP_IL		= 49,
	ICE_PROT_UDP_OF		= 52,
	ICE_PROT_UDP_IL_OR_S	= 53,
	ICE_PROT_GRE_OF		= 64,
	ICE_PROT_NSH_F		= 84,
	ICE_PROT_ESP_F		= 88,
	ICE_PROT_ESP_2		= 89,
	ICE_PROT_SCTP_IL	= 96,
	ICE_PROT_ICMP_IL	= 98,
	ICE_PROT_ICMPV6_IL	= 100,
	ICE_PROT_VRRP_F		= 101,
	ICE_PROT_OSPF		= 102,
	ICE_PROT_ATAOE_OF	= 114,
	ICE_PROT_CTRL_OF	= 116,
	ICE_PROT_LLDP_OF	= 117,
	ICE_PROT_ARP_OF		= 118,
	ICE_PROT_EAPOL_OF	= 120,
	ICE_PROT_META_ID	= 255, /* when offset == metadata */
	ICE_PROT_INVALID	= 255  /* when offset == ICE_FV_OFFSET_INVAL */
};

#define ICE_VNI_OFFSET		12 /* offset of VNI from ICE_PROT_UDP_OF */

#define ICE_NAN_OFFSET		511
#define ICE_MAC_OFOS_HW		1
#define ICE_MAC_IL_HW		4
#define ICE_ETYPE_OL_HW		9
#define ICE_ETYPE_IL_HW		10
#define ICE_VLAN_OF_HW		16
#define ICE_VLAN_OL_HW		17
#define ICE_IPV4_OFOS_HW	32
#define ICE_IPV4_IL_HW		33
#define ICE_IPV6_OFOS_HW	40
#define ICE_IPV6_IL_HW		41
#define ICE_TCP_IL_HW		49
#define ICE_UDP_ILOS_HW		53
#define ICE_SCTP_IL_HW		96
#define ICE_PPPOE_HW		103
#define ICE_L2TPV3_HW		104

/* ICE_UDP_OF is used to identify all 3 tunnel types
 * VXLAN, GENEVE and VXLAN_GPE. To differentiate further
 * need to use flags from the field vector
 */
#define ICE_UDP_OF_HW	52 /* UDP Tunnels */
#define ICE_GRE_OF_HW	64 /* NVGRE */
#define ICE_META_DATA_ID_HW 255 /* this is used for tunnel and VLAN type */

#define ICE_MDID_SIZE 2
#define ICE_TUN_FLAG_MDID 20
#define ICE_TUN_FLAG_MDID_OFF(word) \
	(ICE_MDID_SIZE * (ICE_TUN_FLAG_MDID + (word)))
#define ICE_TUN_FLAG_MASK 0xFF
#define ICE_FROM_NETWORK_FLAG_MASK 0x8
#define ICE_DIR_FLAG_MASK 0x10
#define ICE_TUN_FLAG_IN_VLAN_MASK 0x80 /* VLAN inside tunneled header */
#define ICE_TUN_FLAG_VLAN_MASK 0x01
#define ICE_TUN_FLAG_FV_IND 2

#define ICE_VLAN_FLAG_MDID 20
#define ICE_VLAN_FLAG_MDID_OFF (ICE_MDID_SIZE * ICE_VLAN_FLAG_MDID)
#define ICE_PKT_FLAGS_0_TO_15_VLAN_FLAGS_MASK 0xD000

#define ICE_PROTOCOL_MAX_ENTRIES 16

/* Mapping of software defined protocol ID to hardware defined protocol ID */
struct ice_protocol_entry {
	enum ice_protocol_type type;
	uint8_t protocol_id;
};

struct ice_ether_hdr {
	uint8_t dst_addr[ETHER_ADDR_LEN];
	uint8_t src_addr[ETHER_ADDR_LEN];
};

struct ice_ethtype_hdr {
	uint16_t ethtype_id;
};

struct ice_ether_vlan_hdr {
	uint8_t dst_addr[ETHER_ADDR_LEN];
	uint8_t src_addr[ETHER_ADDR_LEN];
	uint32_t vlan_id;
};

struct ice_vlan_hdr {
	uint16_t type;
	uint16_t vlan;
};

struct ice_ipv4_hdr {
	uint8_t version;
	uint8_t tos;
	uint16_t total_length;
	uint16_t id;
	uint16_t frag_off;
	uint8_t time_to_live;
	uint8_t protocol;
	uint16_t check;
	uint32_t src_addr;
	uint32_t dst_addr;
};

struct ice_le_ver_tc_flow {
	union {
		struct {
			uint32_t flow_label : 20;
			uint32_t tc : 8;
			uint32_t version : 4;
		} fld;
		uint32_t val;
	} u;
};

struct ice_ipv6_hdr {
	uint32_t be_ver_tc_flow;
	uint16_t payload_len;
	uint8_t next_hdr;
	uint8_t hop_limit;
	uint8_t src_addr[ICE_IPV6_ADDR_LENGTH];
	uint8_t dst_addr[ICE_IPV6_ADDR_LENGTH];
};

struct ice_sctp_hdr {
	uint16_t src_port;
	uint16_t dst_port;
	uint32_t verification_tag;
	uint32_t check;
};

struct ice_l4_hdr {
	uint16_t src_port;
	uint16_t dst_port;
	uint16_t len;
	uint16_t check;
};

struct ice_udp_tnl_hdr {
	uint16_t field;
	uint16_t proto_type;
	uint32_t vni;	/* only use lower 24-bits */
};

struct ice_udp_gtp_hdr {
	uint8_t flags;
	uint8_t msg_type;
	uint16_t rsrvd_len;
	uint32_t teid;
	uint16_t rsrvd_seq_nbr;
	uint8_t rsrvd_n_pdu_nbr;
	uint8_t rsrvd_next_ext;
	uint8_t rsvrd_ext_len;
	uint8_t pdu_type;
	uint8_t qfi;
	uint8_t rsvrd;
};
struct ice_pppoe_hdr {
	uint8_t rsrvd_ver_type;
	uint8_t rsrvd_code;
	uint16_t session_id;
	uint16_t length;
	uint16_t ppp_prot_id; /* control and data only */
};

struct ice_l2tpv3_sess_hdr {
	uint32_t session_id;
	uint64_t cookie;
};

struct ice_nvgre {
	uint16_t flags;
	uint16_t protocol;
	uint32_t tni_flow;
};

union ice_prot_hdr {
	struct ice_ether_hdr eth_hdr;
	struct ice_ethtype_hdr ethertype;
	struct ice_vlan_hdr vlan_hdr;
	struct ice_ipv4_hdr ipv4_hdr;
	struct ice_ipv6_hdr ipv6_hdr;
	struct ice_l4_hdr l4_hdr;
	struct ice_sctp_hdr sctp_hdr;
	struct ice_udp_tnl_hdr tnl_hdr;
	struct ice_nvgre nvgre_hdr;
	struct ice_udp_gtp_hdr gtp_hdr;
	struct ice_pppoe_hdr pppoe_hdr;
	struct ice_l2tpv3_sess_hdr l2tpv3_sess_hdr;
};

/* This is mapping table entry that maps every word within a given protocol
 * structure to the real byte offset as per the specification of that
 * protocol header.
 * for e.g. dst address is 3 words in ethertype header and corresponding bytes
 * are 0, 2, 3 in the actual packet header and src address is at 4, 6, 8
 */
struct ice_prot_ext_tbl_entry {
	enum ice_protocol_type prot_type;
	/* Byte offset into header of given protocol type */
	uint8_t offs[sizeof(union ice_prot_hdr)];
};

#define ICE_FV_OFFSET_INVAL	0x1FF

/* Extraction Sequence (Field Vector) Table */
struct ice_fv_word {
	uint8_t prot_id;
	uint16_t off;		/* Offset within the protocol header */
	uint8_t nresvrd;
} __packed;

#define ICE_MAX_FV_WORDS 48

struct ice_fv {
	struct ice_fv_word ew[ICE_MAX_FV_WORDS];
};

/* Packet Type (PTYPE) values */
#define ICE_PTYPE_MAC_PAY		1
#define ICE_PTYPE_IPV4FRAG_PAY		22
#define ICE_PTYPE_IPV4_PAY		23
#define ICE_PTYPE_IPV4_UDP_PAY		24
#define ICE_PTYPE_IPV4_TCP_PAY		26
#define ICE_PTYPE_IPV4_SCTP_PAY		27
#define ICE_PTYPE_IPV4_ICMP_PAY		28
#define ICE_PTYPE_IPV6FRAG_PAY		88
#define ICE_PTYPE_IPV6_PAY		89
#define ICE_PTYPE_IPV6_UDP_PAY		90
#define ICE_PTYPE_IPV6_TCP_PAY		92
#define ICE_PTYPE_IPV6_SCTP_PAY		93
#define ICE_PTYPE_IPV6_ICMP_PAY		94

struct ice_meta_sect {
	struct ice_pkg_ver ver;
#define ICE_META_SECT_NAME_SIZE	28
	char name[ICE_META_SECT_NAME_SIZE];
	uint32_t track_id;
};

/* Packet Type Groups (PTG) - Inner Most fields (IM) */
#define ICE_PTG_IM_IPV4_TCP		16
#define ICE_PTG_IM_IPV4_UDP		17
#define ICE_PTG_IM_IPV4_SCTP		18
#define ICE_PTG_IM_IPV4_PAY		20
#define ICE_PTG_IM_IPV4_OTHER		21
#define ICE_PTG_IM_IPV6_TCP		32
#define ICE_PTG_IM_IPV6_UDP		33
#define ICE_PTG_IM_IPV6_SCTP		34
#define ICE_PTG_IM_IPV6_OTHER		37
#define ICE_PTG_IM_L2_OTHER		67

/* Extractions to be looked up for a given recipe */
struct ice_prot_lkup_ext {
	uint16_t prot_type;
	uint8_t n_val_words;
	/* create a buffer to hold max words per recipe */
	uint16_t field_off[ICE_MAX_CHAIN_WORDS];
	uint16_t field_mask[ICE_MAX_CHAIN_WORDS];

	struct ice_fv_word fv_words[ICE_MAX_CHAIN_WORDS];

	/* Indicate field offsets that have field vector indices assigned */
	ice_declare_bitmap(done, ICE_MAX_CHAIN_WORDS);
};

struct ice_pref_recipe_group {
	uint8_t n_val_pairs;		/* Number of valid pairs */
	struct ice_fv_word pairs[ICE_NUM_WORDS_RECIPE];
	uint16_t mask[ICE_NUM_WORDS_RECIPE];
};

struct ice_recp_grp_entry {
	TAILQ_ENTRY(ice_recp_grp_entry) l_entry;
#define ICE_INVAL_CHAIN_IND 0xFF
	uint16_t rid;
	uint8_t chain_idx;
	uint16_t fv_idx[ICE_NUM_WORDS_RECIPE];
	uint16_t fv_mask[ICE_NUM_WORDS_RECIPE];
	struct ice_pref_recipe_group r_group;
};

/* Software VSI types. */
enum ice_vsi_type {
	ICE_VSI_PF = 0,
	ICE_VSI_VF = 1,
	ICE_VSI_VMDQ2 = 2,
	ICE_VSI_LB = 6,
};


struct ice_link_status {
	/* Refer to ice_aq_phy_type for bits definition */
	uint64_t phy_type_low;
	uint64_t phy_type_high;
	uint8_t topo_media_conflict;
	uint16_t max_frame_size;
	uint16_t link_speed;
	uint16_t req_speeds;
	uint8_t link_cfg_err;
	uint8_t lse_ena;	/* Link Status Event notification */
	uint8_t link_info;
	uint8_t an_info;
	uint8_t ext_info;
	uint8_t fec_info;
	uint8_t pacing;
	/* Refer to #define from module_type[ICE_MODULE_TYPE_TOTAL_BYTE] of
	 * ice_aqc_get_phy_caps structure
	 */
	uint8_t module_type[ICE_MODULE_TYPE_TOTAL_BYTE];
};


/* PHY info such as phy_type, etc... */
struct ice_phy_info {
	struct ice_link_status link_info;
	struct ice_link_status link_info_old;
	uint64_t phy_type_low;
	uint64_t phy_type_high;
	enum ice_media_type media_type;
	uint8_t get_link_info;
	/* Please refer to struct ice_aqc_get_link_status_data to get
	 * detail of enable bit in curr_user_speed_req
	 */
	uint16_t curr_user_speed_req;
	enum ice_fec_mode curr_user_fec_req;
	enum ice_fc_mode curr_user_fc_req;
	struct ice_aqc_set_phy_cfg_data curr_user_phy_cfg;
};

struct ice_port_info {
	struct ice_sched_node *root;	/* Root Node per Port */
	struct ice_hw *hw;		/* back pointer to HW instance */
	uint32_t last_node_teid;		/* scheduler last node info */
	uint16_t sw_id;			/* Initial switch ID belongs to port */
	uint16_t pf_vf_num;
	uint8_t port_state;
#define ICE_SCHED_PORT_STATE_INIT	0x0
#define ICE_SCHED_PORT_STATE_READY	0x1
	uint8_t lport;
#define ICE_LPORT_MASK			0xff
	struct ice_fc_info fc;
	struct ice_mac_info mac;
	struct ice_phy_info phy;
	struct ice_lock sched_lock;	/* protect access to TXSched tree */
	struct ice_sched_node *
		sib_head[ICE_MAX_TRAFFIC_CLASS][ICE_AQC_TOPO_MAX_LEVEL_NUM];
	struct ice_bw_type_info root_node_bw_t_info;
	struct ice_bw_type_info tc_node_bw_t_info[ICE_MAX_TRAFFIC_CLASS];
	struct ice_qos_cfg qos_cfg;
	uint8_t is_vf:1;
	uint8_t is_custom_tx_enabled:1;
};

TAILQ_HEAD(ice_vsi_list_map_head, ice_vsi_list_map_info);

#define ICE_MAX_NUM_PROFILES 256

#define ICE_SW_CFG_MAX_BUF_LEN 2048
#define ICE_MAX_SW 256
#define ICE_DFLT_VSI_INVAL 0xff

#define ICE_VSI_INVAL_ID 0xFFFF
#define ICE_INVAL_Q_HANDLE 0xFFFF

#define ICE_FLTR_RX	BIT(0)
#define ICE_FLTR_TX	BIT(1)
#define ICE_FLTR_RX_LB	BIT(2)
#define ICE_FLTR_TX_RX	(ICE_FLTR_RX | ICE_FLTR_TX)

#define ICE_DUMMY_ETH_HDR_LEN		16

/* VSI context structure for add/get/update/free operations */
struct ice_vsi_ctx {
	uint16_t vsi_num;
	uint16_t vsis_allocd;
	uint16_t vsis_unallocated;
	uint16_t flags;
	struct ice_aqc_vsi_props info;
	struct ice_sched_vsi_info sched;
	uint8_t alloc_from_pool;
	uint8_t vf_num;
	uint16_t num_lan_q_entries[ICE_MAX_TRAFFIC_CLASS];
	struct ice_q_ctx *lan_q_ctx[ICE_MAX_TRAFFIC_CLASS];
	uint16_t num_rdma_q_entries[ICE_MAX_TRAFFIC_CLASS];
	struct ice_q_ctx *rdma_q_ctx[ICE_MAX_TRAFFIC_CLASS];
};


struct ice_switch_info {
	struct ice_vsi_list_map_head vsi_list_map_head;
	struct ice_sw_recipe *recp_list;
	uint16_t prof_res_bm_init;
	uint16_t max_used_prof_index;

	ice_declare_bitmap(prof_res_bm[ICE_MAX_NUM_PROFILES], ICE_MAX_FV_WORDS);
};

TAILQ_HEAD(ice_rl_prof_list_head, ice_aqc_rl_profile_info);
TAILQ_HEAD(ice_agg_list_head, ice_sched_agg_info);

/* BW rate limit profile parameters list entry along
 * with bandwidth maintained per layer in port info
 */
struct ice_aqc_rl_profile_info {
	struct ice_aqc_rl_profile_elem profile;
	TAILQ_ENTRY(ice_aqc_rl_profile_info) list_entry;
	uint32_t bw;			/* requested */
	uint16_t prof_id_ref;	/* profile ID to node association ref count */
};

/* Bookkeeping structure to hold bitmap of VSIs corresponding to VSI list ID */
struct ice_vsi_list_map_info {
	TAILQ_ENTRY(ice_vsi_list_map_info) list_entry;
	ice_declare_bitmap(vsi_map, ICE_MAX_VSI);
	uint16_t vsi_list_id;
	/* counter to track how many rules are reusing this VSI list */
	uint16_t ref_cnt;
};

struct ice_adv_lkup_elem {
	enum ice_protocol_type type;
	union ice_prot_hdr h_u;	/* Header values */
	union ice_prot_hdr m_u;	/* Mask of header values to match */
};

/*
 * This structure allows to pass info about lb_en and lan_en
 * flags to ice_add_adv_rule. Values in act would be used
 * only if act_valid was set to true, otherwise dflt
 * values would be used.
 */
struct ice_adv_rule_flags_info {
	uint32_t act;
	uint8_t act_valid;		/* indicate if flags in act are valid */
};

enum ice_sw_fwd_act_type {
	ICE_FWD_TO_VSI = 0,
	ICE_FWD_TO_VSI_LIST, /* Do not use this when adding filter */
	ICE_FWD_TO_Q,
	ICE_FWD_TO_QGRP,
	ICE_DROP_PACKET,
	ICE_LG_ACTION,
	ICE_INVAL_ACT
};

struct ice_sw_act_ctrl {
	/* Source VSI for LOOKUP_TX or source port for LOOKUP_RX */
	uint16_t src;
	uint16_t flag;
	enum ice_sw_fwd_act_type fltr_act;
	/* Depending on filter action */
	union {
		/* This is a queue ID in case of ICE_FWD_TO_Q and starting
		 * queue ID in case of ICE_FWD_TO_QGRP.
		 */
		uint16_t q_id:11;
		uint16_t vsi_id:10;
		uint16_t hw_vsi_id:10;
		uint16_t vsi_list_id:10;
	} fwd_id;
	/* software VSI handle */
	uint16_t vsi_handle;
	uint8_t qgrp_size;
};

struct ice_adv_rule_info {
	enum ice_sw_tunnel_type tun_type;
	struct ice_sw_act_ctrl sw_act;
	uint32_t priority;
	uint8_t rx; /* true means LOOKUP_RX otherwise LOOKUP_TX */
	uint8_t add_dir_lkup;
	uint16_t fltr_rule_id;
	uint16_t lg_id;
	uint16_t vlan_type;
	struct ice_adv_rule_flags_info flags_info;
};

struct ice_adv_fltr_mgmt_list_entry {
	TAILQ_ENTRY(ice_adv_fltr_mgmt_list_entry) list_entry;

	struct ice_adv_lkup_elem *lkups;
	struct ice_adv_rule_info rule_info;
	uint16_t lkups_cnt;
	struct ice_vsi_list_map_info *vsi_list_info;
	uint16_t vsi_count;
};

enum ice_promisc_flags {
	ICE_PROMISC_UCAST_RX = 0,
	ICE_PROMISC_UCAST_TX,
	ICE_PROMISC_MCAST_RX,
	ICE_PROMISC_MCAST_TX,
	ICE_PROMISC_BCAST_RX,
	ICE_PROMISC_BCAST_TX,
	ICE_PROMISC_VLAN_RX,
	ICE_PROMISC_VLAN_TX,
	ICE_PROMISC_UCAST_RX_LB,
	/* Max value */
	ICE_PROMISC_MAX,
};

/* type of filter src ID */
enum ice_src_id {
	ICE_SRC_ID_UNKNOWN = 0,
	ICE_SRC_ID_VSI,
	ICE_SRC_ID_QUEUE,
	ICE_SRC_ID_LPORT,
};

struct ice_fltr_info {
	/* Look up information: how to look up packet */
	enum ice_sw_lkup_type lkup_type;
	/* Forward action: filter action to do after lookup */
	enum ice_sw_fwd_act_type fltr_act;
	/* rule ID returned by firmware once filter rule is created */
	uint16_t fltr_rule_id;
	uint16_t flag;

	/* Source VSI for LOOKUP_TX or source port for LOOKUP_RX */
	uint16_t src;
	enum ice_src_id src_id;

	union {
		struct {
			uint8_t mac_addr[ETHER_ADDR_LEN];
		} mac;
		struct {
			uint8_t mac_addr[ETHER_ADDR_LEN];
			uint16_t vlan_id;
		} mac_vlan;
		struct {
			uint16_t vlan_id;
			uint16_t tpid;
			uint8_t tpid_valid;
		} vlan;
		/* Set lkup_type as ICE_SW_LKUP_ETHERTYPE
		 * if just using ethertype as filter. Set lkup_type as
		 * ICE_SW_LKUP_ETHERTYPE_MAC if MAC also needs to be
		 * passed in as filter.
		 */
		struct {
			uint16_t ethertype;
			uint8_t mac_addr[ETHER_ADDR_LEN]; /* optional */
		} ethertype_mac;
	} l_data; /* Make sure to zero out the memory of l_data before using
		   * it or only set the data associated with lookup match
		   * rest everything should be zero
		   */

	/* Depending on filter action */
	union {
		/* queue ID in case of ICE_FWD_TO_Q and starting
		 * queue ID in case of ICE_FWD_TO_QGRP.
		 */
		uint16_t q_id:11;
		uint16_t hw_vsi_id:10;
		uint16_t vsi_list_id:10;
	} fwd_id;

	/* Sw VSI handle */
	uint16_t vsi_handle;

	/* Set to num_queues if action is ICE_FWD_TO_QGRP. This field
	 * determines the range of queues the packet needs to be forwarded to.
	 * Note that qgrp_size must be set to a power of 2.
	 */
	uint8_t qgrp_size;

	/* Rule creations populate these indicators basing on the switch type */
	uint8_t lb_en;	/* Indicate if packet can be looped back */
	uint8_t lan_en;	/* Indicate if packet can be forwarded to the uplink */
};

/**
 * enum ice_fltr_marker - Marker for syncing OS and driver filter lists
 * @ICE_FLTR_NOT_FOUND: initial state, indicates filter has not been found
 * @ICE_FLTR_FOUND: set when a filter has been found in both lists
 *
 * This enumeration is used to help sync an operating system provided filter
 * list with the filters previously added.
 *
 * This is required for FreeBSD because the operating system does not provide
 * individual indications of whether a filter has been added or deleted, but
 * instead just notifies the driver with the entire new list.
 *
 * To use this marker state, the driver shall initially reset all filters to
 * the ICE_FLTR_NOT_FOUND state. Then, for each filter in the OS list, it
 * shall search the driver list for the filter. If found, the filter state
 * will be set to ICE_FLTR_FOUND. If not found, that filter will be added.
 * Finally, the driver shall search the internal filter list for all filters
 * still marked as ICE_FLTR_NOT_FOUND and remove them.
 */
enum ice_fltr_marker {
	ICE_FLTR_NOT_FOUND,
	ICE_FLTR_FOUND,
};

struct ice_fltr_list_entry {
	TAILQ_ENTRY(ice_fltr_list_entry) list_entry;
	enum ice_status status;
	struct ice_fltr_info fltr_info;
};

/* This defines an entry in the list that maintains MAC or VLAN membership
 * to HW list mapping, since multiple VSIs can subscribe to the same MAC or
 * VLAN. As an optimization the VSI list should be created only when a
 * second VSI becomes a subscriber to the same MAC address. VSI lists are always
 * used for VLAN membership.
 */
struct ice_fltr_mgmt_list_entry {
	/* back pointer to VSI list ID to VSI list mapping */
	struct ice_vsi_list_map_info *vsi_list_info;
	uint16_t vsi_count;
#define ICE_INVAL_LG_ACT_INDEX 0xffff
	uint16_t lg_act_idx;
#define ICE_INVAL_SW_MARKER_ID 0xffff
	uint16_t sw_marker_id;
	TAILQ_ENTRY(ice_fltr_mgmt_list_entry) list_entry;
	struct ice_fltr_info fltr_info;
#define ICE_INVAL_COUNTER_ID 0xff
	uint8_t counter_index;
	enum ice_fltr_marker marker;
};


#define ICE_IPV4_MAKE_PREFIX_MASK(prefix) ((uint32_t)((~0ULL) << (32 - (prefix))))
#define ICE_FLOW_PROF_ID_INVAL		0xfffffffffffffffful
#define ICE_FLOW_PROF_ID_BYPASS		0
#define ICE_FLOW_PROF_ID_DEFAULT	1
#define ICE_FLOW_ENTRY_HANDLE_INVAL	0
#define ICE_FLOW_VSI_INVAL		0xffff
#define ICE_FLOW_FLD_OFF_INVAL		0xffff

/* Generate flow hash field from flow field type(s) */
#define ICE_FLOW_HASH_IPV4	\
	(BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_SA) | \
	 BIT_ULL(ICE_FLOW_FIELD_IDX_IPV4_DA))
#define ICE_FLOW_HASH_IPV6	\
	(BIT_ULL(ICE_FLOW_FIELD_IDX_IPV6_SA) | \
	 BIT_ULL(ICE_FLOW_FIELD_IDX_IPV6_DA))
#define ICE_FLOW_HASH_TCP_PORT	\
	(BIT_ULL(ICE_FLOW_FIELD_IDX_TCP_SRC_PORT) | \
	 BIT_ULL(ICE_FLOW_FIELD_IDX_TCP_DST_PORT))
#define ICE_FLOW_HASH_UDP_PORT	\
	(BIT_ULL(ICE_FLOW_FIELD_IDX_UDP_SRC_PORT) | \
	 BIT_ULL(ICE_FLOW_FIELD_IDX_UDP_DST_PORT))
#define ICE_FLOW_HASH_SCTP_PORT	\
	(BIT_ULL(ICE_FLOW_FIELD_IDX_SCTP_SRC_PORT) | \
	 BIT_ULL(ICE_FLOW_FIELD_IDX_SCTP_DST_PORT))

#define ICE_HASH_INVALID	0
#define ICE_HASH_TCP_IPV4	(ICE_FLOW_HASH_IPV4 | ICE_FLOW_HASH_TCP_PORT)
#define ICE_HASH_TCP_IPV6	(ICE_FLOW_HASH_IPV6 | ICE_FLOW_HASH_TCP_PORT)
#define ICE_HASH_UDP_IPV4	(ICE_FLOW_HASH_IPV4 | ICE_FLOW_HASH_UDP_PORT)
#define ICE_HASH_UDP_IPV6	(ICE_FLOW_HASH_IPV6 | ICE_FLOW_HASH_UDP_PORT)
#define ICE_HASH_SCTP_IPV4	(ICE_FLOW_HASH_IPV4 | ICE_FLOW_HASH_SCTP_PORT)
#define ICE_HASH_SCTP_IPV6	(ICE_FLOW_HASH_IPV6 | ICE_FLOW_HASH_SCTP_PORT)

/* Protocol header fields within a packet segment. A segment consists of one or
 * more protocol headers that make up a logical group of protocol headers. Each
 * logical group of protocol headers encapsulates or is encapsulated using/by
 * tunneling or encapsulation protocols for network virtualization such as GRE,
 * VxLAN, etc.
 */
enum ice_flow_seg_hdr {
	ICE_FLOW_SEG_HDR_NONE		= 0x00000000,
	ICE_FLOW_SEG_HDR_ETH		= 0x00000001,
	ICE_FLOW_SEG_HDR_VLAN		= 0x00000002,
	ICE_FLOW_SEG_HDR_IPV4		= 0x00000004,
	ICE_FLOW_SEG_HDR_IPV6		= 0x00000008,
	ICE_FLOW_SEG_HDR_ARP		= 0x00000010,
	ICE_FLOW_SEG_HDR_ICMP		= 0x00000020,
	ICE_FLOW_SEG_HDR_TCP		= 0x00000040,
	ICE_FLOW_SEG_HDR_UDP		= 0x00000080,
	ICE_FLOW_SEG_HDR_SCTP		= 0x00000100,
	ICE_FLOW_SEG_HDR_GRE		= 0x00000200,
	/* The following is an additive bit for ICE_FLOW_SEG_HDR_IPV4 and
	 * ICE_FLOW_SEG_HDR_IPV6.
	 */
	ICE_FLOW_SEG_HDR_IPV_FRAG	= 0x40000000,
	ICE_FLOW_SEG_HDR_IPV_OTHER	= 0x80000000,
};

enum ice_flow_field {
	/* L2 */
	ICE_FLOW_FIELD_IDX_ETH_DA,
	ICE_FLOW_FIELD_IDX_ETH_SA,
	ICE_FLOW_FIELD_IDX_S_VLAN,
	ICE_FLOW_FIELD_IDX_C_VLAN,
	ICE_FLOW_FIELD_IDX_ETH_TYPE,
	/* L3 */
	ICE_FLOW_FIELD_IDX_IPV4_DSCP,
	ICE_FLOW_FIELD_IDX_IPV6_DSCP,
	ICE_FLOW_FIELD_IDX_IPV4_TTL,
	ICE_FLOW_FIELD_IDX_IPV4_PROT,
	ICE_FLOW_FIELD_IDX_IPV6_TTL,
	ICE_FLOW_FIELD_IDX_IPV6_PROT,
	ICE_FLOW_FIELD_IDX_IPV4_SA,
	ICE_FLOW_FIELD_IDX_IPV4_DA,
	ICE_FLOW_FIELD_IDX_IPV6_SA,
	ICE_FLOW_FIELD_IDX_IPV6_DA,
	/* L4 */
	ICE_FLOW_FIELD_IDX_TCP_SRC_PORT,
	ICE_FLOW_FIELD_IDX_TCP_DST_PORT,
	ICE_FLOW_FIELD_IDX_UDP_SRC_PORT,
	ICE_FLOW_FIELD_IDX_UDP_DST_PORT,
	ICE_FLOW_FIELD_IDX_SCTP_SRC_PORT,
	ICE_FLOW_FIELD_IDX_SCTP_DST_PORT,
	ICE_FLOW_FIELD_IDX_TCP_FLAGS,
	/* ARP */
	ICE_FLOW_FIELD_IDX_ARP_SIP,
	ICE_FLOW_FIELD_IDX_ARP_DIP,
	ICE_FLOW_FIELD_IDX_ARP_SHA,
	ICE_FLOW_FIELD_IDX_ARP_DHA,
	ICE_FLOW_FIELD_IDX_ARP_OP,
	/* ICMP */
	ICE_FLOW_FIELD_IDX_ICMP_TYPE,
	ICE_FLOW_FIELD_IDX_ICMP_CODE,
	/* GRE */
	ICE_FLOW_FIELD_IDX_GRE_KEYID,
	 /* The total number of enums must not exceed 64 */
	ICE_FLOW_FIELD_IDX_MAX
};

/* Flow headers and fields for AVF support */
enum ice_flow_avf_hdr_field {
	/* Values 0 - 28 are reserved for future use */
	ICE_AVF_FLOW_FIELD_INVALID		= 0,
	ICE_AVF_FLOW_FIELD_UNICAST_IPV4_UDP	= 29,
	ICE_AVF_FLOW_FIELD_MULTICAST_IPV4_UDP,
	ICE_AVF_FLOW_FIELD_IPV4_UDP,
	ICE_AVF_FLOW_FIELD_IPV4_TCP_SYN_NO_ACK,
	ICE_AVF_FLOW_FIELD_IPV4_TCP,
	ICE_AVF_FLOW_FIELD_IPV4_SCTP,
	ICE_AVF_FLOW_FIELD_IPV4_OTHER,
	ICE_AVF_FLOW_FIELD_FRAG_IPV4,
	/* Values 37-38 are reserved */
	ICE_AVF_FLOW_FIELD_UNICAST_IPV6_UDP	= 39,
	ICE_AVF_FLOW_FIELD_MULTICAST_IPV6_UDP,
	ICE_AVF_FLOW_FIELD_IPV6_UDP,
	ICE_AVF_FLOW_FIELD_IPV6_TCP_SYN_NO_ACK,
	ICE_AVF_FLOW_FIELD_IPV6_TCP,
	ICE_AVF_FLOW_FIELD_IPV6_SCTP,
	ICE_AVF_FLOW_FIELD_IPV6_OTHER,
	ICE_AVF_FLOW_FIELD_FRAG_IPV6,
	ICE_AVF_FLOW_FIELD_RSVD47,
	ICE_AVF_FLOW_FIELD_FCOE_OX,
	ICE_AVF_FLOW_FIELD_FCOE_RX,
	ICE_AVF_FLOW_FIELD_FCOE_OTHER,
	/* Values 51-62 are reserved */
	ICE_AVF_FLOW_FIELD_L2_PAYLOAD		= 63,
	ICE_AVF_FLOW_FIELD_MAX
};

/* Supported RSS offloads  This macro is defined to support
 * VIRTCHNL_OP_GET_RSS_HENA_CAPS ops. PF driver sends the RSS hardware
 * capabilities to the caller of this ops.
 */
#define ICE_DEFAULT_RSS_HENA ( \
	BIT_ULL(ICE_AVF_FLOW_FIELD_IPV4_UDP) | \
	BIT_ULL(ICE_AVF_FLOW_FIELD_IPV4_SCTP) | \
	BIT_ULL(ICE_AVF_FLOW_FIELD_IPV4_TCP) | \
	BIT_ULL(ICE_AVF_FLOW_FIELD_IPV4_OTHER) | \
	BIT_ULL(ICE_AVF_FLOW_FIELD_FRAG_IPV4) | \
	BIT_ULL(ICE_AVF_FLOW_FIELD_IPV6_UDP) | \
	BIT_ULL(ICE_AVF_FLOW_FIELD_IPV6_TCP) | \
	BIT_ULL(ICE_AVF_FLOW_FIELD_IPV6_SCTP) | \
	BIT_ULL(ICE_AVF_FLOW_FIELD_IPV6_OTHER) | \
	BIT_ULL(ICE_AVF_FLOW_FIELD_FRAG_IPV6) | \
	BIT_ULL(ICE_AVF_FLOW_FIELD_IPV4_TCP_SYN_NO_ACK) | \
	BIT_ULL(ICE_AVF_FLOW_FIELD_UNICAST_IPV4_UDP) | \
	BIT_ULL(ICE_AVF_FLOW_FIELD_MULTICAST_IPV4_UDP) | \
	BIT_ULL(ICE_AVF_FLOW_FIELD_IPV6_TCP_SYN_NO_ACK) | \
	BIT_ULL(ICE_AVF_FLOW_FIELD_UNICAST_IPV6_UDP) | \
	BIT_ULL(ICE_AVF_FLOW_FIELD_MULTICAST_IPV6_UDP))

enum ice_rss_cfg_hdr_type {
	ICE_RSS_OUTER_HEADERS, /* take outer headers as inputset. */
	ICE_RSS_INNER_HEADERS, /* take inner headers as inputset. */
	/* take inner headers as inputset for packet with outer IPv4. */
	ICE_RSS_INNER_HEADERS_W_OUTER_IPV4,
	/* take inner headers as inputset for packet with outer IPv6. */
	ICE_RSS_INNER_HEADERS_W_OUTER_IPV6,
	/* take outer headers first then inner headers as inputset */
	/* take inner as inputset for GTPoGRE with outer IPv4 + GRE. */
	ICE_RSS_INNER_HEADERS_W_OUTER_IPV4_GRE,
	/* take inner as inputset for GTPoGRE with outer IPv6 + GRE. */
	ICE_RSS_INNER_HEADERS_W_OUTER_IPV6_GRE,
	ICE_RSS_ANY_HEADERS
};

struct ice_rss_hash_cfg {
	uint32_t addl_hdrs; /* protocol header fields */
	uint64_t hash_flds; /* hash bit field (ICE_FLOW_HASH_*) to configure */
	enum ice_rss_cfg_hdr_type hdr_type; /* to specify inner or outer */
	bool symm; /* symmetric or asymmetric hash */
};

enum ice_flow_dir {
	ICE_FLOW_DIR_UNDEFINED	= 0,
	ICE_FLOW_TX		= 0x01,
	ICE_FLOW_RX		= 0x02,
	ICE_FLOW_TX_RX		= ICE_FLOW_RX | ICE_FLOW_TX
};

enum ice_flow_priority {
	ICE_FLOW_PRIO_LOW,
	ICE_FLOW_PRIO_NORMAL,
	ICE_FLOW_PRIO_HIGH
};

#define ICE_FLOW_SEG_SINGLE		1
#define ICE_FLOW_SEG_MAX		2
#define ICE_FLOW_PROFILE_MAX		1024
#define ICE_FLOW_ACL_FIELD_VECTOR_MAX	32
#define ICE_FLOW_FV_EXTRACT_SZ		2

#define ICE_FLOW_SET_HDRS(seg, val)	((seg)->hdrs |= (uint32_t)(val))

struct ice_flow_seg_xtrct {
	uint8_t prot_id;	/* Protocol ID of extracted header field */
	uint16_t off;	/* Starting offset of the field in header in bytes */
	uint8_t idx;		/* Index of FV entry used */
	uint8_t disp;	/* Displacement of field in bits fr. FV entry's start */
};

enum ice_flow_fld_match_type {
	ICE_FLOW_FLD_TYPE_REG,		/* Value, mask */
	ICE_FLOW_FLD_TYPE_RANGE,	/* Value, mask, last (upper bound) */
	ICE_FLOW_FLD_TYPE_PREFIX,	/* IP address, prefix, size of prefix */
	ICE_FLOW_FLD_TYPE_SIZE,		/* Value, mask, size of match */
};

struct ice_flow_fld_loc {
	/* Describe offsets of field information relative to the beginning of
	 * input buffer provided when adding flow entries.
	 */
	uint16_t val;	/* Offset where the value is located */
	uint16_t mask;	/* Offset where the mask/prefix value is located */
	uint16_t last;	/* Length or offset where the upper value is located */
};

struct ice_flow_fld_info {
	enum ice_flow_fld_match_type type;
	/* Location where to retrieve data from an input buffer */
	struct ice_flow_fld_loc src;
	/* Location where to put the data into the final entry buffer */
	struct ice_flow_fld_loc entry;
	struct ice_flow_seg_xtrct xtrct;
};

struct ice_flow_seg_info {
	uint32_t hdrs;	/* Bitmask indicating protocol headers present */
	/* Bitmask indicating header fields to be matched */
	ice_declare_bitmap(match, ICE_FLOW_FIELD_IDX_MAX);
	/* Bitmask indicating header fields matched as ranges */
	ice_declare_bitmap(range, ICE_FLOW_FIELD_IDX_MAX);

	struct ice_flow_fld_info fields[ICE_FLOW_FIELD_IDX_MAX];
};

#define ICE_FLOW_ENTRY_HNDL(e)	((uint64_t)e)

struct ice_flow_prof {
	TAILQ_ENTRY(ice_flow_prof) l_entry;

	uint64_t id;
	enum ice_flow_dir dir;
	uint8_t segs_cnt;

	struct ice_flow_seg_info segs[ICE_FLOW_SEG_MAX];

	/* software VSI handles referenced by this flow profile */
	ice_declare_bitmap(vsis, ICE_MAX_VSI);

	union {
		/* struct sw_recipe */
		bool symm; /* Symmetric Hash for RSS */
	} cfg;
};

struct ice_rss_cfg {
	TAILQ_ENTRY(ice_rss_cfg) l_entry;
	/* bitmap of VSIs added to the RSS entry */
	ice_declare_bitmap(vsis, ICE_MAX_VSI);
	struct ice_rss_hash_cfg hash;
};

TAILQ_HEAD(ice_rss_cfg_head, ice_rss_cfg);

enum ice_flow_action_type {
	ICE_FLOW_ACT_NOP,
	ICE_FLOW_ACT_ALLOW,
	ICE_FLOW_ACT_DROP,
	ICE_FLOW_ACT_CNTR_PKT,
	ICE_FLOW_ACT_FWD_VSI,
	ICE_FLOW_ACT_FWD_VSI_LIST,	/* Should be abstracted away */
	ICE_FLOW_ACT_FWD_QUEUE,		/* Can Queues be abstracted away? */
	ICE_FLOW_ACT_FWD_QUEUE_GROUP,	/* Can Queues be abstracted away? */
	ICE_FLOW_ACT_PUSH,
	ICE_FLOW_ACT_POP,
	ICE_FLOW_ACT_MODIFY,
	ICE_FLOW_ACT_CNTR_BYTES,
	ICE_FLOW_ACT_CNTR_PKT_BYTES,
	ICE_FLOW_ACT_GENERIC_0,
	ICE_FLOW_ACT_GENERIC_1,
	ICE_FLOW_ACT_GENERIC_2,
	ICE_FLOW_ACT_GENERIC_3,
	ICE_FLOW_ACT_GENERIC_4,
	ICE_FLOW_ACT_RPT_FLOW_ID,
	ICE_FLOW_ACT_BUILD_PROF_IDX,
};

struct ice_flow_action {
	enum ice_flow_action_type type;
	union {
		uint32_t dummy;
	} data;
};

TAILQ_HEAD(ice_recp_grp_entry_head, ice_recp_grp_entry);
TAILQ_HEAD(ice_fltr_list_head, ice_fltr_list_entry);
TAILQ_HEAD(ice_fltr_mgmt_list_head, ice_fltr_mgmt_list_entry);
TAILQ_HEAD(ice_adv_fltr_mgmt_list_head, ice_adv_fltr_mgmt_list_entry);

/* Package minimal version supported */
#define ICE_PKG_SUPP_VER_MAJ	1
#define ICE_PKG_SUPP_VER_MNR	3

/* Package format version */
#define ICE_PKG_FMT_VER_MAJ	1
#define ICE_PKG_FMT_VER_MNR	0
#define ICE_PKG_FMT_VER_UPD	0
#define ICE_PKG_FMT_VER_DFT	0

#define ICE_PKG_CNT 4

enum ice_ddp_state {
	/* Indicates that this call to ice_init_pkg
	 * successfully loaded the requested DDP package
	 */
	ICE_DDP_PKG_SUCCESS				= 0,

	/* Generic error for already loaded errors, it is mapped later to
	 * the more specific one (one of the next 3)
	 */
	ICE_DDP_PKG_ALREADY_LOADED			= -1,

	/* Indicates that a DDP package of the same version has already been
	 * loaded onto the device by a previous call or by another PF
	 */
	ICE_DDP_PKG_SAME_VERSION_ALREADY_LOADED		= -2,

	/* The device has a DDP package that is not supported by the driver */
	ICE_DDP_PKG_ALREADY_LOADED_NOT_SUPPORTED	= -3,

	/* The device has a compatible package
	 * (but different from the request) already loaded
	 */
	ICE_DDP_PKG_COMPATIBLE_ALREADY_LOADED		= -4,

	/* The firmware loaded on the device is not compatible with
	 * the DDP package loaded
	 */
	ICE_DDP_PKG_FW_MISMATCH				= -5,

	/* The DDP package file is invalid */
	ICE_DDP_PKG_INVALID_FILE			= -6,

	/* The version of the DDP package provided is higher than
	 * the driver supports
	 */
	ICE_DDP_PKG_FILE_VERSION_TOO_HIGH		= -7,

	/* The version of the DDP package provided is lower than the
	 * driver supports
	 */
	ICE_DDP_PKG_FILE_VERSION_TOO_LOW		= -8,

	/* Missing security manifest in DDP pkg */
	ICE_DDP_PKG_NO_SEC_MANIFEST			= -9,

	/* The RSA signature of the DDP package file provided is invalid */
	ICE_DDP_PKG_FILE_SIGNATURE_INVALID		= -10,

	/* The DDP package file security revision is too low and not
	 * supported by firmware
	 */
	ICE_DDP_PKG_SECURE_VERSION_NBR_TOO_LOW		= -11,

	/* Manifest hash mismatch */
	ICE_DDP_PKG_MANIFEST_INVALID			= -12,

	/* Buffer hash mismatches manifest */
	ICE_DDP_PKG_BUFFER_INVALID			= -13,

	/* Other errors */
	ICE_DDP_PKG_ERR					= -14,
};

/* Package and segment headers and tables */
struct ice_pkg_hdr {
	struct ice_pkg_ver pkg_format_ver;
	uint32_t seg_count;
	uint32_t seg_offset[STRUCT_HACK_VAR_LEN];
};

/* Package signing algorithm types */
#define SEGMENT_SIGN_TYPE_INVALID	0x00000000
#define SEGMENT_SIGN_TYPE_RSA2K		0x00000001
#define SEGMENT_SIGN_TYPE_RSA3K		0x00000002
#define SEGMENT_SIGN_TYPE_RSA3K_SBB	0x00000003 /* Secure Boot Block */
#define SEGMENT_SIGN_TYPE_RSA3K_E825	0x00000005

/* generic segment */
struct ice_generic_seg_hdr {
#define	SEGMENT_TYPE_INVALID	0x00000000
#define SEGMENT_TYPE_METADATA	0x00000001
#define SEGMENT_TYPE_ICE_E810	0x00000010
#define SEGMENT_TYPE_SIGNING	0x00001001
#define SEGMENT_TYPE_ICE_RUN_TIME_CFG 0x00000020
	uint32_t seg_type;
	struct ice_pkg_ver seg_format_ver;
	uint32_t seg_size;
	char seg_id[ICE_PKG_NAME_SIZE];
};

/* ice specific segment */

union ice_device_id {
	struct {
		uint16_t device_id;
		uint16_t vendor_id;
	} dev_vend_id;
	uint32_t id;
};

struct ice_device_id_entry {
	union ice_device_id device;
	union ice_device_id sub_device;
};

struct ice_seg {
	struct ice_generic_seg_hdr hdr;
	uint32_t device_table_count;
	struct ice_device_id_entry device_table[STRUCT_HACK_VAR_LEN];
};

struct ice_nvm_table {
	uint32_t table_count;
	uint32_t vers[STRUCT_HACK_VAR_LEN];
};

struct ice_buf {
#define ICE_PKG_BUF_SIZE	4096
	uint8_t buf[ICE_PKG_BUF_SIZE];
};

struct ice_buf_table {
	uint32_t buf_count;
	struct ice_buf buf_array[STRUCT_HACK_VAR_LEN];
};

struct ice_run_time_cfg_seg {
	struct ice_generic_seg_hdr hdr;
	uint8_t rsvd[8];
	struct ice_buf_table buf_table;
};

/* global metadata specific segment */
struct ice_global_metadata_seg {
	struct ice_generic_seg_hdr hdr;
	struct ice_pkg_ver pkg_ver;
	uint32_t rsvd;
	char pkg_name[ICE_PKG_NAME_SIZE];
};

#define ICE_MIN_S_OFF		12
#define ICE_MAX_S_OFF		4095
#define ICE_MIN_S_SZ		1
#define ICE_MAX_S_SZ		4084

struct ice_sign_seg {
	struct ice_generic_seg_hdr hdr;
	uint32_t seg_id;
	uint32_t sign_type;
	uint32_t signed_seg_idx;
	uint32_t signed_buf_start;
	uint32_t signed_buf_count;
#define ICE_SIGN_SEG_RESERVED_COUNT	44
	uint8_t reserved[ICE_SIGN_SEG_RESERVED_COUNT];
	struct ice_buf_table buf_tbl;
};

/* section information */
struct ice_section_entry {
	uint32_t type;
	uint16_t offset;
	uint16_t size;
};

#define ICE_MIN_S_COUNT		1
#define ICE_MAX_S_COUNT		511
#define ICE_MIN_S_DATA_END	12
#define ICE_MAX_S_DATA_END	4096

#define ICE_METADATA_BUF	0x80000000

struct ice_buf_hdr {
	uint16_t section_count;
	uint16_t data_end;
	struct ice_section_entry section_entry[STRUCT_HACK_VAR_LEN];
};

#define ICE_MAX_ENTRIES_IN_BUF(hd_sz, ent_sz) ((ICE_PKG_BUF_SIZE - \
	ice_struct_size((struct ice_buf_hdr *)0, section_entry, 1) - (hd_sz)) /\
	(ent_sz))

/* ice package section IDs */
#define ICE_SID_METADATA		1
#define ICE_SID_XLT0_SW			10
#define ICE_SID_XLT_KEY_BUILDER_SW	11
#define ICE_SID_XLT1_SW			12
#define ICE_SID_XLT2_SW			13
#define ICE_SID_PROFID_TCAM_SW		14
#define ICE_SID_PROFID_REDIR_SW		15
#define ICE_SID_FLD_VEC_SW		16
#define ICE_SID_CDID_KEY_BUILDER_SW	17
#define ICE_SID_CDID_REDIR_SW		18

#define ICE_SID_XLT0_ACL		20
#define ICE_SID_XLT_KEY_BUILDER_ACL	21
#define ICE_SID_XLT1_ACL		22
#define ICE_SID_XLT2_ACL		23
#define ICE_SID_PROFID_TCAM_ACL		24
#define ICE_SID_PROFID_REDIR_ACL	25
#define ICE_SID_FLD_VEC_ACL		26
#define ICE_SID_CDID_KEY_BUILDER_ACL	27
#define ICE_SID_CDID_REDIR_ACL		28

#define ICE_SID_XLT0_FD			30
#define ICE_SID_XLT_KEY_BUILDER_FD	31
#define ICE_SID_XLT1_FD			32
#define ICE_SID_XLT2_FD			33
#define ICE_SID_PROFID_TCAM_FD		34
#define ICE_SID_PROFID_REDIR_FD		35
#define ICE_SID_FLD_VEC_FD		36
#define ICE_SID_CDID_KEY_BUILDER_FD	37
#define ICE_SID_CDID_REDIR_FD		38

#define ICE_SID_XLT0_RSS		40
#define ICE_SID_XLT_KEY_BUILDER_RSS	41
#define ICE_SID_XLT1_RSS		42
#define ICE_SID_XLT2_RSS		43
#define ICE_SID_PROFID_TCAM_RSS		44
#define ICE_SID_PROFID_REDIR_RSS	45
#define ICE_SID_FLD_VEC_RSS		46
#define ICE_SID_CDID_KEY_BUILDER_RSS	47
#define ICE_SID_CDID_REDIR_RSS		48

#define ICE_SID_RXPARSER_CAM		50
#define ICE_SID_RXPARSER_NOMATCH_CAM	51
#define ICE_SID_RXPARSER_IMEM		52
#define ICE_SID_RXPARSER_XLT0_BUILDER	53
#define ICE_SID_RXPARSER_NODE_PTYPE	54
#define ICE_SID_RXPARSER_MARKER_PTYPE	55
#define ICE_SID_RXPARSER_BOOST_TCAM	56
#define ICE_SID_RXPARSER_PROTO_GRP	57
#define ICE_SID_RXPARSER_METADATA_INIT	58
#define ICE_SID_RXPARSER_XLT0		59

#define ICE_SID_TXPARSER_CAM		60
#define ICE_SID_TXPARSER_NOMATCH_CAM	61
#define ICE_SID_TXPARSER_IMEM		62
#define ICE_SID_TXPARSER_XLT0_BUILDER	63
#define ICE_SID_TXPARSER_NODE_PTYPE	64
#define ICE_SID_TXPARSER_MARKER_PTYPE	65
#define ICE_SID_TXPARSER_BOOST_TCAM	66
#define ICE_SID_TXPARSER_PROTO_GRP	67
#define ICE_SID_TXPARSER_METADATA_INIT	68
#define ICE_SID_TXPARSER_XLT0		69

#define ICE_SID_RXPARSER_INIT_REDIR	70
#define ICE_SID_TXPARSER_INIT_REDIR	71
#define ICE_SID_RXPARSER_MARKER_GRP	72
#define ICE_SID_TXPARSER_MARKER_GRP	73
#define ICE_SID_RXPARSER_LAST_PROTO	74
#define ICE_SID_TXPARSER_LAST_PROTO	75
#define ICE_SID_RXPARSER_PG_SPILL	76
#define ICE_SID_TXPARSER_PG_SPILL	77
#define ICE_SID_RXPARSER_NOMATCH_SPILL	78
#define ICE_SID_TXPARSER_NOMATCH_SPILL	79

#define ICE_SID_XLT0_PE			80
#define ICE_SID_XLT_KEY_BUILDER_PE	81
#define ICE_SID_XLT1_PE			82
#define ICE_SID_XLT2_PE			83
#define ICE_SID_PROFID_TCAM_PE		84
#define ICE_SID_PROFID_REDIR_PE		85
#define ICE_SID_FLD_VEC_PE		86
#define ICE_SID_CDID_KEY_BUILDER_PE	87
#define ICE_SID_CDID_REDIR_PE		88

#define ICE_SID_RXPARSER_FLAG_REDIR	97

/* Label Metadata section IDs */
#define ICE_SID_LBL_FIRST		0x80000010
#define ICE_SID_LBL_RXPARSER_IMEM	0x80000010
#define ICE_SID_LBL_TXPARSER_IMEM	0x80000011
#define ICE_SID_LBL_RESERVED_12		0x80000012
#define ICE_SID_LBL_RESERVED_13		0x80000013
#define ICE_SID_LBL_RXPARSER_MARKER	0x80000014
#define ICE_SID_LBL_TXPARSER_MARKER	0x80000015
#define ICE_SID_LBL_PTYPE		0x80000016
#define ICE_SID_LBL_PROTOCOL_ID		0x80000017
#define ICE_SID_LBL_RXPARSER_TMEM	0x80000018
#define ICE_SID_LBL_TXPARSER_TMEM	0x80000019
#define ICE_SID_LBL_RXPARSER_PG		0x8000001A
#define ICE_SID_LBL_TXPARSER_PG		0x8000001B
#define ICE_SID_LBL_RXPARSER_M_TCAM	0x8000001C
#define ICE_SID_LBL_TXPARSER_M_TCAM	0x8000001D
#define ICE_SID_LBL_SW_PROFID_TCAM	0x8000001E
#define ICE_SID_LBL_ACL_PROFID_TCAM	0x8000001F
#define ICE_SID_LBL_PE_PROFID_TCAM	0x80000020
#define ICE_SID_LBL_RSS_PROFID_TCAM	0x80000021
#define ICE_SID_LBL_FD_PROFID_TCAM	0x80000022
#define ICE_SID_LBL_FLAG		0x80000023
#define ICE_SID_LBL_REG			0x80000024
#define ICE_SID_LBL_SW_PTG		0x80000025
#define ICE_SID_LBL_ACL_PTG		0x80000026
#define ICE_SID_LBL_PE_PTG		0x80000027
#define ICE_SID_LBL_RSS_PTG		0x80000028
#define ICE_SID_LBL_FD_PTG		0x80000029
#define ICE_SID_LBL_SW_VSIG		0x8000002A
#define ICE_SID_LBL_ACL_VSIG		0x8000002B
#define ICE_SID_LBL_PE_VSIG		0x8000002C
#define ICE_SID_LBL_RSS_VSIG		0x8000002D
#define ICE_SID_LBL_FD_VSIG		0x8000002E
#define ICE_SID_LBL_PTYPE_META		0x8000002F
#define ICE_SID_LBL_SW_PROFID		0x80000030
#define ICE_SID_LBL_ACL_PROFID		0x80000031
#define ICE_SID_LBL_PE_PROFID		0x80000032
#define ICE_SID_LBL_RSS_PROFID		0x80000033
#define ICE_SID_LBL_FD_PROFID		0x80000034
#define ICE_SID_LBL_RXPARSER_MARKER_GRP	0x80000035
#define ICE_SID_LBL_TXPARSER_MARKER_GRP	0x80000036
#define ICE_SID_LBL_RXPARSER_PROTO	0x80000037
#define ICE_SID_LBL_TXPARSER_PROTO	0x80000038
/* The following define MUST be updated to reflect the last label section ID */
#define ICE_SID_LBL_LAST		0x80000038

/* Label ICE runtime configuration section IDs */
#define ICE_SID_TX_5_LAYER_TOPO		0x10

enum ice_block {
	ICE_BLK_SW = 0,
	ICE_BLK_ACL,
	ICE_BLK_FD,
	ICE_BLK_RSS,
	ICE_BLK_PE,
	ICE_BLK_COUNT
};

enum ice_sect {
	ICE_XLT0 = 0,
	ICE_XLT_KB,
	ICE_XLT1,
	ICE_XLT2,
	ICE_PROF_TCAM,
	ICE_PROF_REDIR,
	ICE_VEC_TBL,
	ICE_CDID_KB,
	ICE_CDID_REDIR,
	ICE_SECT_COUNT
};

/* package buffer building */

struct ice_buf_build {
	struct ice_buf buf;
	uint16_t reserved_section_table_entries;
};

struct ice_pkg_enum {
	struct ice_buf_table *buf_table;
	uint32_t buf_idx;

	uint32_t type;
	struct ice_buf_hdr *buf;
	uint32_t sect_idx;
	void *sect;
	uint32_t sect_type;

	uint32_t entry_idx;
	void *(*handler)(uint32_t sect_type, void *section, uint32_t index,
	    uint32_t *offset);
};

struct ice_flex_fields {
	union {
		struct {
			uint8_t src_ip;
			uint8_t dst_ip;
			uint8_t flow_label;	/* valid for IPv6 only */
		} ip_fields;

		struct {
			uint8_t src_prt;
			uint8_t dst_prt;
		} tcp_udp_fields;

		struct {
			uint8_t src_ip;
			uint8_t dst_ip;
			uint8_t src_prt;
			uint8_t dst_prt;
		} ip_tcp_udp_fields;

		struct {
			uint8_t src_prt;
			uint8_t dst_prt;
			uint8_t flow_label;	/* valid for IPv6 only */
			uint8_t spi;
		} ip_esp_fields;

		struct {
			uint32_t offset;
			uint32_t length;
		} off_len;
	} fields;
};

#define ICE_XLT1_DFLT_GRP	0
#define ICE_XLT1_TABLE_SIZE	1024

/* package labels */
struct ice_label {
	uint16_t value;
#define ICE_PKG_LABEL_SIZE	64
	char name[ICE_PKG_LABEL_SIZE];
};

struct ice_label_section {
	uint16_t count;
	struct ice_label label[STRUCT_HACK_VAR_LEN];
};

#define ICE_MAX_LABELS_IN_BUF ICE_MAX_ENTRIES_IN_BUF( \
	ice_struct_size((struct ice_label_section *)0, label, 1) - \
	sizeof(struct ice_label), sizeof(struct ice_label))

struct ice_sw_fv_section {
	uint16_t count;
	uint16_t base_offset;
	struct ice_fv fv[STRUCT_HACK_VAR_LEN];
};


#pragma pack(1)
/* The BOOST TCAM stores the match packet header in reverse order, meaning
 * the fields are reversed; in addition, this means that the normally big endian
 * fields of the packet are now little endian.
 */
struct ice_boost_key_value {
#define ICE_BOOST_REMAINING_HV_KEY     15
	uint8_t remaining_hv_key[ICE_BOOST_REMAINING_HV_KEY];
	union {
		struct {
			uint16_t hv_dst_port_key;
			uint16_t hv_src_port_key;
		} /* udp_tunnel */;
		struct {
			uint16_t hv_vlan_id_key;
			uint16_t hv_etype_key;
		} vlan;
	};
	uint8_t tcam_search_key;
};
#pragma pack()

struct ice_boost_key {
	struct ice_boost_key_value key;
	struct ice_boost_key_value key2;
};

/* package Boost TCAM entry */
struct ice_boost_tcam_entry {
	uint16_t addr;
	uint16_t reserved;
	/* break up the 40 bytes of key into different fields */
	struct ice_boost_key key;
	uint8_t boost_hit_index_group;
	/* The following contains bitfields which are not on byte boundaries.
	 * These fields are currently unused by driver software.
	 */
#define ICE_BOOST_BIT_FIELDS		43
	uint8_t bit_fields[ICE_BOOST_BIT_FIELDS];
};

struct ice_boost_tcam_section {
	uint16_t count;
	uint16_t reserved;
	struct ice_boost_tcam_entry tcam[STRUCT_HACK_VAR_LEN];
};

#define ICE_MAX_BST_TCAMS_IN_BUF ICE_MAX_ENTRIES_IN_BUF( \
	ice_struct_size((struct ice_boost_tcam_section *)0, tcam, 1) - \
	sizeof(struct ice_boost_tcam_entry), \
	sizeof(struct ice_boost_tcam_entry))

struct ice_xlt1_section {
	uint16_t count;
	uint16_t offset;
	uint8_t value[STRUCT_HACK_VAR_LEN];
};

struct ice_xlt2_section {
	uint16_t count;
	uint16_t offset;
	uint16_t value[STRUCT_HACK_VAR_LEN];
};

struct ice_prof_redir_section {
	uint16_t count;
	uint16_t offset;
	uint8_t redir_value[STRUCT_HACK_VAR_LEN];
};

/* Tunnel enabling */

enum ice_tunnel_type {
	TNL_VXLAN = 0,
	TNL_GENEVE,
	TNL_GRETAP,
	TNL_GTP,
	TNL_GTPC,
	TNL_GTPU,
	TNL_LAST = 0xFF,
	TNL_ALL = 0xFF,
};

struct ice_tunnel_type_scan {
	enum ice_tunnel_type type;
	const char *label_prefix;
};

struct ice_tunnel_entry {
	enum ice_tunnel_type type;
	uint16_t boost_addr;
	uint16_t port;
	uint16_t ref;
	struct ice_boost_tcam_entry *boost_entry;
	uint8_t valid;
	uint8_t in_use;
	uint8_t marked;
};

#define ICE_TUNNEL_MAX_ENTRIES	16

struct ice_tunnel_table {
	struct ice_tunnel_entry tbl[ICE_TUNNEL_MAX_ENTRIES];
	uint16_t count;
};


/* To support tunneling entries by PF, the package will append the PF number to
 * the label; for example TNL_VXLAN_PF0, TNL_VXLAN_PF1, TNL_VXLAN_PF2, etc.
 */
#define ICE_TNL_PRE	"TNL_"

struct ice_pkg_es {
	uint16_t count;
	uint16_t offset;
	struct ice_fv_word es[STRUCT_HACK_VAR_LEN];
};

TAILQ_HEAD(ice_prof_map_head, ice_prof_map);

struct ice_es {
	uint32_t sid;
	uint16_t count;
	uint16_t fvw;
	uint16_t *ref_count;
	struct ice_prof_map_head prof_map;
	struct ice_fv_word *t;
	struct ice_lock prof_map_lock;	/* protect access to profiles list */
	uint8_t *written;
	uint8_t reverse; /* set to true to reverse FV order */
};

/* PTYPE Group management */

/* Note: XLT1 table takes 13-bit as input, and results in an 8-bit packet type
 * group (PTG) ID as output.
 *
 * Note: PTG 0 is the default packet type group and it is assumed that all PTYPE
 * are a part of this group until moved to a new PTG.
 */
#define ICE_DEFAULT_PTG	0

struct ice_ptg_entry {
	struct ice_ptg_ptype *first_ptype;
	uint8_t in_use;
};

struct ice_ptg_ptype {
	struct ice_ptg_ptype *next_ptype;
	uint8_t ptg;
};

#define ICE_MAX_TCAM_PER_PROFILE	32
#define ICE_MAX_PTG_PER_PROFILE		32

struct ice_prof_map {
	TAILQ_ENTRY(ice_prof_map) list;
	uint64_t profile_cookie;
	uint64_t context;
	uint8_t prof_id;
	uint8_t ptg_cnt;
	uint8_t ptg[ICE_MAX_PTG_PER_PROFILE];
};

#define ICE_INVALID_TCAM	0xFFFF

struct ice_tcam_inf {
	uint16_t tcam_idx;
	uint8_t ptg;
	uint8_t prof_id;
	uint8_t in_use;
};

struct ice_vsig_prof {
	TAILQ_ENTRY(ice_vsig_prof) list;
	uint64_t profile_cookie;
	uint8_t prof_id;
	uint8_t tcam_count;
	struct ice_tcam_inf tcam[ICE_MAX_TCAM_PER_PROFILE];
};

TAILQ_HEAD(ice_vsig_prof_head, ice_vsig_prof);

struct ice_vsig_entry {
	struct ice_vsig_prof_head prop_lst;
	struct ice_vsig_vsi *first_vsi;
	uint8_t in_use;
};

struct ice_vsig_vsi {
	struct ice_vsig_vsi *next_vsi;
	uint32_t prop_mask;
	uint16_t changed;
	uint16_t vsig;
};

#define ICE_XLT1_CNT	1024
#define ICE_MAX_PTGS	256

/* XLT1 Table */
struct ice_xlt1 {
	struct ice_ptg_entry *ptg_tbl;
	struct ice_ptg_ptype *ptypes;
	uint8_t *t;
	uint32_t sid;
	uint16_t count;
};


#define ICE_XLT2_CNT	768
#define ICE_MAX_VSIGS	768

/* VSIG bit layout:
 * [0:12]: incremental VSIG index 1 to ICE_MAX_VSIGS
 * [13:15]: PF number of device
 */
#define ICE_VSIG_IDX_M	(0x1FFF)
#define ICE_PF_NUM_S	13
#define ICE_PF_NUM_M	(0x07 << ICE_PF_NUM_S)
#define ICE_VSIG_VALUE(vsig, pf_id) \
	((uint16_t)((((uint16_t)(vsig)) & ICE_VSIG_IDX_M) | \
	       (((uint16_t)(pf_id) << ICE_PF_NUM_S) & ICE_PF_NUM_M)))
#define ICE_DEFAULT_VSIG	0

/* XLT2 Table */
struct ice_xlt2 {
	struct ice_vsig_entry *vsig_tbl;
	struct ice_vsig_vsi *vsis;
	uint16_t *t;
	uint32_t sid;
	uint16_t count;
};

/* Extraction sequence - list of match fields:
 * protocol ID, offset, profile length
 */
union ice_match_fld {
	struct {
		uint8_t prot_id;
		uint8_t offset;
		uint8_t length;
		uint8_t reserved; /* must be zero */
	} fld;
	uint32_t val;
};

#define ICE_MATCH_LIST_SZ	20
#pragma pack(1)
struct ice_match {
	uint8_t count;
	union ice_match_fld list[ICE_MATCH_LIST_SZ];
};

/* Profile ID Management */
struct ice_prof_id_key {
	uint16_t flags;
	uint8_t xlt1;
	uint16_t xlt2_cdid;
};

/* Keys are made up of two values, each one-half the size of the key.
 * For TCAM, the entire key is 80 bits wide (or 2, 40-bit wide values)
 */
#define ICE_TCAM_KEY_VAL_SZ	5
#define ICE_TCAM_KEY_SZ		(2 * ICE_TCAM_KEY_VAL_SZ)

struct ice_prof_tcam_entry {
	uint16_t addr;
	uint8_t key[ICE_TCAM_KEY_SZ];
	uint8_t prof_id;
};
#pragma pack()

struct ice_prof_id_section {
	uint16_t count;
	struct ice_prof_tcam_entry entry[STRUCT_HACK_VAR_LEN];
};

struct ice_prof_tcam {
	uint32_t sid;
	uint16_t count;
	uint16_t max_prof_id;
	struct ice_prof_tcam_entry *t;
	uint8_t cdid_bits; /* # CDID bits to use in key, 0, 2, 4, or 8 */
};

enum ice_chg_type {
	ICE_TCAM_NONE = 0,
	ICE_PTG_ES_ADD,
	ICE_TCAM_ADD,
	ICE_VSIG_ADD,
	ICE_VSIG_REM,
	ICE_VSI_MOVE,
};

TAILQ_HEAD(ice_chs_chg_head, ice_chs_chg);

struct ice_chs_chg {
	TAILQ_ENTRY(ice_chs_chg) list_entry;
	enum ice_chg_type type;

	uint8_t add_ptg;
	uint8_t add_vsig;
	uint8_t add_tcam_idx;
	uint8_t add_prof;
	uint16_t ptype;
	uint8_t ptg;
	uint8_t prof_id;
	uint16_t vsi;
	uint16_t vsig;
	uint16_t orig_vsig;
	uint16_t tcam_idx;
};

#define ICE_FLOW_PTYPE_MAX		ICE_XLT1_CNT

struct ice_prof_redir {
	uint8_t *t;
	uint32_t sid;
	uint16_t count;
};

/* Tables per block */
struct ice_blk_info {
	struct ice_xlt1 xlt1;
	struct ice_xlt2 xlt2;
	struct ice_prof_tcam prof;
	struct ice_prof_redir prof_redir;
	struct ice_es es;
	uint8_t overwrite; /* set to true to allow overwrite of table entries */
	uint8_t is_list_init;
};


struct ice_sw_recipe {
	/* For a chained recipe the root recipe is what should be used for
	 * programming rules
	 */
	uint8_t is_root;
	uint8_t root_rid;
	uint8_t recp_created;

	/* Number of extraction words */
	uint8_t n_ext_words;
	/* Protocol ID and Offset pair (extraction word) to describe the
	 * recipe
	 */
	struct ice_fv_word ext_words[ICE_MAX_CHAIN_WORDS];
	uint16_t word_masks[ICE_MAX_CHAIN_WORDS];

	/* if this recipe is a collection of other recipe */
	uint8_t big_recp;

	/* if this recipe is part of another bigger recipe then chain index
	 * corresponding to this recipe
	 */
	uint8_t chain_idx;

	/* if this recipe is a collection of other recipe then count of other
	 * recipes and recipe IDs of those recipes
	 */
	uint8_t n_grp_count;

	/* Bit map specifying the IDs associated with this group of recipe */
	ice_declare_bitmap(r_bitmap, ICE_MAX_NUM_RECIPES);
#if 0
	enum ice_sw_tunnel_type tun_type;
#endif
	/* List of type ice_fltr_mgmt_list_entry or adv_rule */
	uint8_t adv_rule;
	struct ice_fltr_mgmt_list_head filt_rules;
	struct ice_adv_fltr_mgmt_list_head adv_filt_rules;
	struct ice_fltr_mgmt_list_head filt_replay_rules;
	struct ice_lock filt_rule_lock;	/* protect filter rule structure */
#if 0
	/* Profiles this recipe should be associated with */
	struct LIST_HEAD_TYPE fv_list;
#endif
	/* Profiles this recipe is associated with */
	uint8_t num_profs, *prof_ids;

	/* Bit map for possible result indexes */
	ice_declare_bitmap(res_idxs, ICE_MAX_FV_WORDS);

	/* This allows user to specify the recipe priority.
	 * For now, this becomes 'fwd_priority' when recipe
	 * is created, usually recipes can have 'fwd' and 'join'
	 * priority.
	 */
	uint8_t priority;

	struct ice_recp_grp_entry_head rg_list;

	/* AQ buffer associated with this recipe */
	struct ice_aqc_recipe_data_elem *root_buf;
#if 0
	/* This struct saves the fv_words for a given lookup */
	struct ice_prot_lkup_ext lkup_exts;
#endif
};

TAILQ_HEAD(ice_flow_prof_head, ice_flow_prof);

/* Port hardware description */
struct ice_hw {
	struct ice_softc *hw_sc;
#if 0
	uint8_t *hw_addr;
	void *back;
#endif
	struct ice_aqc_layer_props *layer_info;
	struct ice_port_info *port_info;
#if 0
	/* 2D Array for each Tx Sched RL Profile type */
	struct ice_sched_rl_profile **cir_profiles;
	struct ice_sched_rl_profile **eir_profiles;
	struct ice_sched_rl_profile **srl_profiles;
#endif
	/* PSM clock frequency for calculating RL profile params */
	uint32_t psm_clk_freq;
	enum ice_mac_type mac_type;
#if 0
	/* pci info */
	uint16_t device_id;
	uint16_t vendor_id;
	uint16_t subsystem_device_id;
	uint16_t subsystem_vendor_id;
	uint8_t revision_id;
#endif
	uint8_t pf_id;		/* device profile info */
#if 0
	enum ice_phy_model phy_model;
	uint8_t phy_ports;
	uint8_t max_phy_port;

#endif
	uint16_t max_burst_size;	/* driver sets this value */

	/* Tx Scheduler values */
	uint8_t num_tx_sched_layers;
	uint8_t num_tx_sched_phys_layers;
	uint8_t flattened_layers;
	uint8_t max_cgds;
	uint8_t sw_entry_point_layer;
	uint16_t max_children[ICE_AQC_TOPO_MAX_LEVEL_NUM];
	struct ice_agg_list_head agg_list;	/* lists all aggregator */
	/* List contain profile ID(s) and other params per layer */
	struct ice_rl_prof_list_head rl_prof_list[ICE_AQC_TOPO_MAX_LEVEL_NUM];
	struct ice_vsi_ctx *vsi_ctx[ICE_MAX_VSI];
	uint8_t evb_veb;	/* true for VEB, false for VEPA */
	uint8_t reset_ongoing;	/* true if HW is in reset, false otherwise */
#if 0
	struct ice_bus_info bus;
#endif
	struct ice_flash_info flash;
	struct ice_hw_dev_caps dev_caps;	/* device capabilities */
	struct ice_hw_func_caps func_caps;	/* function capabilities */
	struct ice_switch_info *switch_info;	/* switch filter lists */

	/* Control Queue info */
	struct ice_ctl_q_info adminq;
	struct ice_ctl_q_info mailboxq;
	uint8_t api_branch;		/* API branch version */
	uint8_t api_maj_ver;		/* API major version */
	uint8_t api_min_ver;		/* API minor version */
	uint8_t api_patch;		/* API patch version */
	uint8_t fw_branch;		/* firmware branch version */
	uint8_t fw_maj_ver;		/* firmware major version */
	uint8_t fw_min_ver;		/* firmware minor version */
	uint8_t fw_patch;		/* firmware patch version */
	uint32_t fw_build;		/* firmware build number */
	struct ice_fwlog_cfg fwlog_cfg;
	bool fwlog_support_ena; /* does hardware support FW logging? */

/* Device max aggregate bandwidths corresponding to the GL_PWR_MODE_CTL
 * register. Used for determining the ITR/INTRL granularity during
 * initialization.
 */
#define ICE_MAX_AGG_BW_200G	0x0
#define ICE_MAX_AGG_BW_100G	0X1
#define ICE_MAX_AGG_BW_50G	0x2
#define ICE_MAX_AGG_BW_25G	0x3
	/* ITR granularity for different speeds */
#define ICE_ITR_GRAN_ABOVE_25	2
#define ICE_ITR_GRAN_MAX_25	4
	/* ITR granularity in 1 us */
	uint8_t itr_gran;
	/* INTRL granularity for different speeds */
#define ICE_INTRL_GRAN_ABOVE_25	4
#define ICE_INTRL_GRAN_MAX_25	8
	/* INTRL granularity in 1 us */
	uint8_t intrl_gran;

	/* true if VSIs can share unicast MAC addr */
	uint8_t umac_shared;
#if 0

#define ICE_PHY_PER_NAC_E822		1
#define ICE_MAX_QUAD			2
#define ICE_QUADS_PER_PHY_E822		2
#define ICE_PORTS_PER_PHY_E822		8
#define ICE_PORTS_PER_QUAD		4
#define ICE_PORTS_PER_PHY_E810		4
#define ICE_NUM_EXTERNAL_PORTS		(ICE_MAX_QUAD * ICE_PORTS_PER_QUAD)
#endif
	/* Active package version (currently active) */
	struct ice_pkg_ver active_pkg_ver;
	uint32_t pkg_seg_id;
	uint32_t pkg_sign_type;
	uint32_t active_track_id;
	uint8_t pkg_has_signing_seg:1;
	uint8_t active_pkg_name[ICE_PKG_NAME_SIZE];
	uint8_t active_pkg_in_nvm;

	/* Driver's package ver - (from the Ice Metadata section) */
	struct ice_pkg_ver pkg_ver;
	uint8_t pkg_name[ICE_PKG_NAME_SIZE];

	/* Driver's Ice segment format version and id (from the Ice seg) */
	struct ice_pkg_ver ice_seg_fmt_ver;
	uint8_t ice_seg_id[ICE_SEG_ID_SIZE];

	/* Pointer to the ice segment */
	struct ice_seg *seg;

	/* Pointer to allocated copy of pkg memory */
	uint8_t *pkg_copy;
	uint32_t pkg_size;

	/* tunneling info */
	struct ice_lock tnl_lock;
	struct ice_tunnel_table tnl;

	/* HW block tables */
	struct ice_blk_info blk[ICE_BLK_COUNT];
#if 0
	struct ice_lock fl_profs_locks[ICE_BLK_COUNT];	/* lock fltr profiles */
#endif
	struct ice_flow_prof_head fl_profs[ICE_BLK_COUNT];
#if 0
	struct ice_lock rss_locks;	/* protect RSS configuration */
#endif
	struct ice_rss_cfg_head rss_list_head;
#if 0
	uint16_t vsi_owning_pf_lut; /* SW IDX of VSI that acquired PF RSS LUT */
	struct ice_mbx_snapshot mbx_snapshot;
#endif
	uint8_t dvm_ena;
#if 0
	bool subscribable_recipes_supported;
#endif
};

/**
 * @enum ice_state
 * @brief Driver state flags
 *
 * Used to indicate the status of various driver events. Intended to be
 * modified only using atomic operations, so that we can use it even in places
 * which aren't locked.
 */
enum ice_state {
	ICE_STATE_CONTROLQ_EVENT_PENDING,
	ICE_STATE_VFLR_PENDING,
	ICE_STATE_MDD_PENDING,
	ICE_STATE_RESET_OICR_RECV,
	ICE_STATE_RESET_PFR_REQ,
	ICE_STATE_PREPARED_FOR_RESET,
	ICE_STATE_SUBIF_NEEDS_REINIT,
	ICE_STATE_RESET_FAILED,
	ICE_STATE_DRIVER_INITIALIZED,
	ICE_STATE_NO_MEDIA,
	ICE_STATE_RECOVERY_MODE,
	ICE_STATE_ROLLBACK_MODE,
	ICE_STATE_LINK_STATUS_REPORTED,
	ICE_STATE_ATTACHING,
	ICE_STATE_DETACHING,
	ICE_STATE_LINK_DEFAULT_OVERRIDE_PENDING,
	ICE_STATE_LLDP_RX_FLTR_FROM_DRIVER,
	ICE_STATE_MULTIPLE_TCS,
	ICE_STATE_DO_FW_DEBUG_DUMP,
	ICE_STATE_LINK_ACTIVE_ON_DOWN,
	ICE_STATE_FIRST_INIT_LINK,
	ICE_STATE_DO_CREATE_MIRR_INTFC,
	ICE_STATE_DO_DESTROY_MIRR_INTFC,
	/* This entry must be last */
	ICE_STATE_LAST,
};

/**
 * ice_set_state - Set the specified state
 * @s: the state bitmap
 * @bit: the state to set
 *
 * Atomically update the state bitmap with the specified bit set.
 */
static inline void
ice_set_state(volatile uint32_t *s, enum ice_state bit)
{
	atomic_setbits_int(s, (1UL << bit));
}

/**
 * ice_clear_state - Clear the specified state
 * @s: the state bitmap
 * @bit: the state to clear
 *
 * Atomically update the state bitmap with the specified bit cleared.
 */
static inline void
ice_clear_state(volatile uint32_t *s, enum ice_state bit)
{
	atomic_clearbits_int(s, (1UL << bit));
}

/**
 * ice_testandset_state - Test and set the specified state
 * @s: the state bitmap
 * @bit: the bit to test
 *
 * Atomically update the state bitmap, setting the specified bit. Returns the
 * previous value of the bit.
 */
static inline uint32_t
ice_testandset_state(volatile uint32_t *s, enum ice_state bit)
{
	uint32_t expected = *s;
	uint32_t previous;

	previous = atomic_cas_uint(s, expected, expected | (1UL << bit));
	return (previous & (1UL << bit)) ? 1 : 0;
}

/**
 * ice_testandclear_state - Test and clear the specified state
 * @s: the state bitmap
 * @bit: the bit to test
 *
 * Atomically update the state bitmap, clearing the specified bit. Returns the
 * previous value of the bit.
 */
static inline uint32_t
ice_testandclear_state(volatile uint32_t *s, enum ice_state bit)
{
	uint32_t expected = *s;
	uint32_t previous;

	previous = atomic_cas_uint(s, expected, expected & ~(1UL << bit));
	return (previous & (1UL << bit)) ? 1 : 0;
}

/**
 * ice_test_state - Test the specified state
 * @s: the state bitmap
 * @bit: the bit to test
 *
 * Return true if the state is set, false otherwise. Use this only if the flow
 * does not need to update the state. If you must update the state as well,
 * prefer ice_testandset_state or ice_testandclear_state.
 */
static inline uint32_t
ice_test_state(volatile uint32_t *s, enum ice_state bit)
{
	return (*s & (1UL << bit)) ? 1 : 0;
}

static inline uint32_t ice_round_to_num(uint32_t N, uint32_t R)
{
	return ((((N) % (R)) < ((R) / 2)) ? (((N) / (R)) * (R)) :
		((((N) + (R) - 1) / (R)) * (R)));
}

/* based on parity() in sys/net/toepliz.c */
static inline uint16_t
ice_popcount16(uint16_t n16)
{
	n16 = ((n16 & 0xaaaa) >> 1) + (n16 & 0x5555);
	n16 = ((n16 & 0xcccc) >> 2) + (n16 & 0x3333);
	n16 = ((n16 & 0xf0f0) >> 4) + (n16 & 0x0f0f);
	n16 = ((n16 & 0xff00) >> 8) + (n16 & 0x00ff);

	return (n16);
}

/* based on parity() in sys/net/toepliz.c */
static inline uint32_t
ice_popcount32(uint32_t n32)
{
	n32 = ((n32 & 0xaaaaaaaa) >> 1) + (n32 & 0x55555555);
	n32 = ((n32 & 0xcccccccc) >> 2) + (n32 & 0x33333333);
	n32 = ((n32 & 0xf0f0f0f0) >> 4) + (n32 & 0x0f0f0f0f);
	n32 = ((n32 & 0xff00ff00) >> 8) + (n32 & 0x00ff00ff);
	n32 = ((n32 & 0xffff0000) >> 16) + (n32 & 0x0000ffff);

	return (n32);
}

#define ice_ilog2(x) ((sizeof(x) <= 4) ? (fls(x) - 1) : (flsl(x) - 1))

/*
 * ice_bit_* functions derived from FreeBSD sys/bitstring.h
 */

typedef	uint32_t ice_bitstr_t;

#define	ICE_BITSTR_MASK (~0UL)
#define	ICE_BITSTR_BITS (sizeof(ice_bitstr_t) * 8)

/* round up x to the next multiple of y if y is a power of two */
#define ice_bit_roundup(x, y)						\
	(((size_t)(x) + (y) - 1) & ~((size_t)(y) - 1))

/* Number of bytes allocated for a bit string of nbits bits */
#define	ice_bitstr_size(nbits) (ice_bit_roundup((nbits), ICE_BITSTR_BITS) / 8)

static inline ice_bitstr_t *
ice_bit_alloc(size_t nbits)
{
	return malloc(ice_bitstr_size(nbits), M_DEVBUF, M_NOWAIT | M_ZERO);
}

/* Allocate a bit string on the stack */
#define	ice_bit_decl(name, nbits) \
	((name)[bitstr_size(nbits) / sizeof(ice_bitstr_t)])

/* ice_bitstr_t in bit string containing the bit. */
static inline size_t
ice_bit_idx(size_t bit)
{
	return (bit / ICE_BITSTR_BITS);
}

/* bit number within ice_bitstr_t at ice_bit_idx(_bit). */
static inline size_t
ice_bit_offset(size_t bit)
{
	return (bit % ICE_BITSTR_BITS);
}

/* Mask for the bit within its long. */
static inline ice_bitstr_t
ice_bit_mask(size_t bit)
{
	return (1UL << ice_bit_offset(bit));
}

static inline ice_bitstr_t
ice_bit_make_mask(size_t start, size_t stop)
{
	return ((ICE_BITSTR_MASK << ice_bit_offset(start)) &
	    (ICE_BITSTR_MASK >> (ICE_BITSTR_BITS - ice_bit_offset(stop) - 1)));
}

/* Is bit N of bit string set? */
static inline int
ice_bit_test(const ice_bitstr_t *bitstr, size_t bit)
{
	return ((bitstr[ice_bit_idx(bit)] & ice_bit_mask(bit)) != 0);
}

/* Set bit N of bit string. */
static inline void
ice_bit_set(ice_bitstr_t *bitstr, size_t bit)
{
	bitstr[ice_bit_idx(bit)] |= ice_bit_mask(bit);
}

/* clear bit N of bit string name */
static inline void
ice_bit_clear(ice_bitstr_t *bitstr, size_t bit)
{
	bitstr[ice_bit_idx(bit)] &= ~ice_bit_mask(bit);
}

/* Count the number of bits set in a bitstr of size nbits at or after start */
static inline ssize_t
ice_bit_count(ice_bitstr_t *bitstr, size_t start, size_t nbits)
{
	ice_bitstr_t *curbitstr, mask;
	size_t curbitstr_len;
	ssize_t value = 0;

	if (start >= nbits)
		return (0);

	curbitstr = bitstr + ice_bit_idx(start);
	nbits -= ICE_BITSTR_BITS * ice_bit_idx(start);
	start -= ICE_BITSTR_BITS * ice_bit_idx(start);

	if (start > 0) {
		curbitstr_len = (int)ICE_BITSTR_BITS < nbits ?
				(int)ICE_BITSTR_BITS : nbits;
		mask = ice_bit_make_mask(start,
		    ice_bit_offset(curbitstr_len - 1));
		value += ice_popcount32(*curbitstr & mask);
		curbitstr++;
		if (nbits < ICE_BITSTR_BITS)
			return (value);
		nbits -= ICE_BITSTR_BITS;
	}
	while (nbits >= (int)ICE_BITSTR_BITS) {
		value += ice_popcount32(*curbitstr);
		curbitstr++;
		nbits -= ICE_BITSTR_BITS;
	}
	if (nbits > 0) {
		mask = ice_bit_make_mask(0, ice_bit_offset(nbits - 1));
		value += ice_popcount32(*curbitstr & mask);
	}

	return (value);
}

/* Find the first 'match'-bit in bit string at or after bit start. */
static inline ssize_t
ice_bit_ff_at(ice_bitstr_t *bitstr, size_t start, size_t nbits, int match)
{
	ice_bitstr_t *curbitstr;
	ice_bitstr_t *stopbitstr;
	ice_bitstr_t mask;
	ice_bitstr_t test;
	ssize_t value;

	if (start >= nbits || nbits <= 0)
		return (-1);

	curbitstr = bitstr + ice_bit_idx(start);
	stopbitstr = bitstr + ice_bit_idx(nbits - 1);
	mask = match ? 0 : ICE_BITSTR_MASK;

	test = mask ^ *curbitstr;
	if (ice_bit_offset(start) != 0)
		test &= ice_bit_make_mask(start, ICE_BITSTR_BITS - 1);
	while (test == 0 && curbitstr < stopbitstr)
		test = mask ^ *(++curbitstr);

	value = ((curbitstr - bitstr) * ICE_BITSTR_BITS) + ffs(test) - 1;
	if (test == 0 ||
	    (ice_bit_offset(nbits) != 0 && (size_t)value >= nbits))
		value = -1;
	return (value);
}

/* Find contiguous sequence of at least size 'match'-bits at or after start */
static inline ssize_t
ice_bit_ff_area_at(ice_bitstr_t *bitstr, size_t start, size_t nbits,
    size_t size, int match)
{
	ice_bitstr_t *curbitstr, mask, test;
	size_t last, shft, maxshft;
	ssize_t value;

	if (start + size > nbits || nbits <= 0)
		return (-1);

	mask = match ? ICE_BITSTR_MASK : 0;
	maxshft = ice_bit_idx(size - 1) == 0 ? size : (int)ICE_BITSTR_BITS;
	value = start;
	curbitstr = bitstr + ice_bit_idx(start);
	test = ~(ICE_BITSTR_MASK << ice_bit_offset(start));
	for (last = size - 1, test |= mask ^ *curbitstr;
	    !(ice_bit_idx(last) == 0 &&
	    (test & ice_bit_make_mask(0, last)) == 0);
	    last -= ICE_BITSTR_BITS, test = mask ^ *++curbitstr) {
		if (test == 0)
			continue;
		/* Shrink-left every 0-area in _test by maxshft-1 bits. */
		for (shft = maxshft; shft > 1 && (test & (test + 1)) != 0;
		     shft = (shft + 1) / 2)
			test |= test >> shft / 2;
		/* Find the start of the first 0-area in 'test'. */
		last = ffs(~(test >> 1));
		value = (curbitstr - bitstr) * ICE_BITSTR_BITS + last;
		/* If there's insufficient space left, give up. */
		if (value + size > nbits) {
			value = -1;
			break;
		}
		last += size - 1;
		/* If a solution is contained in 'test', success! */
		if (ice_bit_idx(last) == 0)
			break;
		/* A solution here needs bits from the next word. */
	}

	return (value);
}

/* Find contiguous sequence of at least size set bits in bit string */
#define ice_bit_ffs_area(_bitstr, _nbits, _size, _resultp)		\
	*(_resultp) = ice_bit_ff_area_at((_bitstr), 0, (_nbits), (_size), 1)

/* Find contiguous sequence of at least size cleared bits in bit string */
#define ice_bit_ffc_area(_bitstr, _nbits, _size, _resultp)		\
	*(_resultp) = ice_bit_ff_area_at((_bitstr), 0, (_nbits), (_size), 0)


/**
 * @file ice_resmgr.h
 * @brief Resource manager interface
 *
 * Defines an interface for managing PF hardware queues and interrupts for assigning them to
 * hardware VSIs and VFs.
 *
 * For queue management:
 * The total number of available Tx and Rx queues is not equal, so it is
 * expected that each PF will allocate two ice_resmgr structures, one for Tx
 * and one for Rx. These should be allocated in attach() prior to initializing
 * VSIs, and destroyed in detach().
 *
 * For interrupt management:
 * The PF allocates an ice_resmgr structure that does not allow scattered
 * allocations since interrupt allocations must be contiguous.
 */

/*
 * For managing VSI queue allocations
 */
/* Hardware only supports a limited number of resources in scattered mode */
#define ICE_MAX_SCATTERED_QUEUES	16
/* Use highest value to indicate invalid resource mapping */
#define ICE_INVALID_RES_IDX		0xFFFF

/**
 * @struct ice_resmgr
 * @brief Resource manager
 *
 * Represent resource allocations using a bitstring, where bit zero represents
 * the first resource. If a particular bit is set this indicates that the
 * resource has been allocated and is not free.
 */
struct ice_resmgr {
	ice_bitstr_t	*resources;
	uint16_t	num_res;
	bool		contig_only;
};

/**
 * @enum ice_resmgr_alloc_type
 * @brief resource manager allocation types
 *
 * Enumeration of possible allocation types that can be used when
 * assigning resources. For now, SCATTERED is only used with
 * managing queue allocations.
 */
enum ice_resmgr_alloc_type {
	ICE_RESMGR_ALLOC_INVALID = 0,
	ICE_RESMGR_ALLOC_CONTIGUOUS,
	ICE_RESMGR_ALLOC_SCATTERED
};

/**
 * @struct ice_tc_info
 * @brief Traffic class information for a VSI
 *
 * Stores traffic class information used in configuring
 * a VSI.
 */
struct ice_tc_info {
	uint16_t qoffset;	/* Offset in VSI queue space */
	uint16_t qcount_tx;	/* TX queues for this Traffic Class */
	uint16_t qcount_rx;	/* RX queues */
};

/* Statistics collected by each port, VSI, VEB, and S-channel */
struct ice_eth_stats {
	uint64_t rx_bytes;			/* gorc */
	uint64_t rx_unicast;			/* uprc */
	uint64_t rx_multicast;		/* mprc */
	uint64_t rx_broadcast;		/* bprc */
	uint64_t rx_discards;		/* rdpc */
	uint64_t rx_unknown_protocol;	/* rupp */
	uint64_t tx_bytes;			/* gotc */
	uint64_t tx_unicast;			/* uptc */
	uint64_t tx_multicast;		/* mptc */
	uint64_t tx_broadcast;		/* bptc */
	uint64_t tx_discards;		/* tdpc */
	uint64_t tx_errors;			/* tepc */
	uint64_t rx_no_desc;			/* repc */
	uint64_t rx_errors;			/* repc */
};

/**
 * @struct ice_vsi_hw_stats
 * @brief hardware statistics for a VSI
 *
 * Stores statistics that are generated by hardware for a VSI.
 */
struct ice_vsi_hw_stats {
	struct ice_eth_stats prev;
	struct ice_eth_stats cur;
	bool offsets_loaded;
};

/* Statistics collected by the MAC */
struct ice_hw_port_stats {
	/* eth stats collected by the port */
	struct ice_eth_stats eth;
	/* additional port specific stats */
	uint64_t tx_dropped_link_down;	/* tdold */
	uint64_t crc_errors;		/* crcerrs */
	uint64_t illegal_bytes;		/* illerrc */
	uint64_t error_bytes;		/* errbc */
	uint64_t mac_local_faults;	/* mlfc */
	uint64_t mac_remote_faults;	/* mrfc */
	uint64_t rx_len_errors;		/* rlec */
	uint64_t link_xon_rx;		/* lxonrxc */
	uint64_t link_xoff_rx;		/* lxoffrxc */
	uint64_t link_xon_tx;		/* lxontxc */
	uint64_t link_xoff_tx;		/* lxofftxc */
	uint64_t priority_xon_rx[8];	/* pxonrxc[8] */
	uint64_t priority_xoff_rx[8];	/* pxoffrxc[8] */
	uint64_t priority_xon_tx[8];	/* pxontxc[8] */
	uint64_t priority_xoff_tx[8];	/* pxofftxc[8] */
	uint64_t priority_xon_2_xoff[8];/* pxon2offc[8] */
	uint64_t rx_size_64;		/* prc64 */
	uint64_t rx_size_127;		/* prc127 */
	uint64_t rx_size_255;		/* prc255 */
	uint64_t rx_size_511;		/* prc511 */
	uint64_t rx_size_1023;		/* prc1023 */
	uint64_t rx_size_1522;		/* prc1522 */
	uint64_t rx_size_big;		/* prc9522 */
	uint64_t rx_undersize;		/* ruc */
	uint64_t rx_fragments;		/* rfc */
	uint64_t rx_oversize;		/* roc */
	uint64_t rx_jabber;		/* rjc */
	uint64_t tx_size_64;		/* ptc64 */
	uint64_t tx_size_127;		/* ptc127 */
	uint64_t tx_size_255;		/* ptc255 */
	uint64_t tx_size_511;		/* ptc511 */
	uint64_t tx_size_1023;		/* ptc1023 */
	uint64_t tx_size_1522;		/* ptc1522 */
	uint64_t tx_size_big;		/* ptc9522 */
	uint64_t mac_short_pkt_dropped;	/* mspdc */
	/* EEE LPI */
	uint32_t tx_lpi_status;
	uint32_t rx_lpi_status;
	uint64_t tx_lpi_count;		/* etlpic */
	uint64_t rx_lpi_count;		/* erlpic */
};

/**
 * @struct ice_pf_hw_stats
 * @brief hardware statistics for a PF
 *
 * Stores statistics that are generated by hardware for each PF.
 */
struct ice_pf_hw_stats {
	struct ice_hw_port_stats prev;
	struct ice_hw_port_stats cur;
	bool offsets_loaded;
};

/**
 * @struct ice_pf_sw_stats
 * @brief software statistics for a PF
 *
 * Contains software generated statistics relevant to a PF.
 */
struct ice_pf_sw_stats {
	/* # of reset events handled, by type */
	uint32_t corer_count;
	uint32_t globr_count;
	uint32_t empr_count;
	uint32_t pfr_count;

	/* # of detected MDD events for Tx and Rx */
	uint32_t tx_mdd_count;
	uint32_t rx_mdd_count;
};

struct ice_tx_map {
	struct mbuf		*txm_m;
	bus_dmamap_t		 txm_map;
	bus_dmamap_t		 txm_map_tso;
	unsigned int		 txm_eop;
};

/**
 * @struct ice_tx_queue
 * @brief Driver Tx queue structure
 *
 * @vsi: backpointer the VSI structure
 * @me: this queue's index into the queue array
 * @irqv: always NULL for iflib
 * @desc_count: the number of descriptors
 * @tx_paddr: the physical address for this queue
 * @q_teid: the Tx queue TEID returned from firmware
 * @stats: queue statistics
 * @tc: traffic class queue belongs to
 * @q_handle: qidx in tc; used in TXQ enable functions
 */
struct ice_tx_queue {
	struct ice_vsi		*vsi;
	struct ice_tx_desc	*tx_base;
	struct ice_dma_mem	 tx_desc_mem;
	bus_addr_t		 tx_paddr;
	struct ice_tx_map	*tx_map;
#if 0
	struct tx_stats		stats;
#endif
	uint64_t		tso;
	uint16_t		desc_count;
	uint32_t		tail;
	struct ice_intr_vector	*irqv;
	uint32_t		q_teid;
	uint32_t		me;
	uint16_t		q_handle;
	uint8_t			tc;

	/* descriptor writeback status */
	uint16_t		*tx_rsq;
	uint16_t		tx_rs_cidx;
	uint16_t		tx_rs_pidx;
	uint16_t		tx_cidx_processed;

	struct ifqueue		*txq_ifq;

	unsigned int		txq_prod;
	unsigned int		txq_cons;
};

struct ice_rx_map {
	struct mbuf		*rxm_m;
	bus_dmamap_t		 rxm_map;
};

/**
 * @struct ice_rx_queue
 * @brief Driver Rx queue structure
 *
 * @vsi: backpointer the VSI structure
 * @me: this queue's index into the queue array
 * @irqv: pointer to vector structure associated with this queue
 * @desc_count: the number of descriptors
 * @rx_paddr: the physical address for this queue
 * @tail: the tail register address for this queue
 * @stats: queue statistics
 * @tc: traffic class queue belongs to
 */
struct ice_rx_queue {
	struct ice_vsi			*vsi;
	union ice_32b_rx_flex_desc	*rx_base;
	struct ice_dma_mem		 rx_desc_mem;
	bus_addr_t			 rx_paddr;
	struct ice_rx_map		*rx_map;
#if 0
	struct rx_stats			stats;
#endif
	uint16_t			desc_count;
	uint32_t			tail;
	struct ice_intr_vector		*irqv;
	uint32_t			me;
	uint8_t				tc;

	struct if_rxring		rxq_acct;
	struct timeout			rxq_refill;
	unsigned int			rxq_prod;
	unsigned int			rxq_cons;
	struct ifiqueue			*rxq_ifiq;
	struct mbuf			*rxq_m_head;
	struct mbuf			**rxq_m_tail;
};

/**
 * @struct ice_vsi
 * @brief VSI structure
 *
 * Contains data relevant to a single VSI
 */
struct ice_vsi {
	/* back pointer to the softc */
	struct ice_softc	*sc;

	bool dynamic;		/* if true, dynamically allocated */

	enum ice_vsi_type type;	/* type of this VSI */
	uint16_t idx;		/* software index to sc->all_vsi[] */

	uint16_t *tx_qmap; /* Tx VSI to PF queue mapping */
	uint16_t *rx_qmap; /* Rx VSI to PF queue mapping */

	enum ice_resmgr_alloc_type qmap_type;

	struct ice_tx_queue *tx_queues;	/* Tx queue array */
	struct ice_rx_queue *rx_queues;	/* Rx queue array */

	int num_tx_queues;
	int num_rx_queues;
	int num_vectors;

	int16_t rx_itr;
	int16_t tx_itr;

	/* RSS configuration */
	uint16_t rss_table_size; /* HW RSS table size */
	uint8_t rss_lut_type; /* Used to configure Get/Set RSS LUT AQ call */

	int max_frame_size;
	uint16_t mbuf_sz;

	struct ice_aqc_vsi_props info;

	/* DCB configuration */
	uint8_t num_tcs;	/* Total number of enabled TCs */
	uint16_t tc_map;	/* bitmap of enabled Traffic Classes */
	/* Information for each traffic class */
	struct ice_tc_info tc_info[ICE_MAX_TRAFFIC_CLASS];
#if 0
	/* context for per-VSI sysctls */
	struct sysctl_ctx_list ctx;
	struct sysctl_oid *vsi_node;

	/* context for per-txq sysctls */
	struct sysctl_ctx_list txqs_ctx;
	struct sysctl_oid *txqs_node;

	/* context for per-rxq sysctls */
	struct sysctl_ctx_list rxqs_ctx;
	struct sysctl_oid *rxqs_node;
#endif
	/* VSI-level stats */
	struct ice_vsi_hw_stats hw_stats;

	/* VSI mirroring details */
	uint16_t mirror_src_vsi;
	uint16_t rule_mir_ingress;
	uint16_t rule_mir_egress;
};

/* Driver always calls main vsi_handle first */
#define ICE_MAIN_VSI_HANDLE		0

#define ICE_I2C_MAX_RETRIES		10
