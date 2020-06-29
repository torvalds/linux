/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _KERNEL_KCSAN_ENCODING_H
#define _KERNEL_KCSAN_ENCODING_H

#include <linux/bits.h>
#include <linux/log2.h>
#include <linux/mm.h>

#include "kcsan.h"

#define SLOT_RANGE PAGE_SIZE

#define INVALID_WATCHPOINT  0
#define CONSUMED_WATCHPOINT 1

/*
 * The maximum useful size of accesses for which we set up watchpoints is the
 * max range of slots we check on an access.
 */
#define MAX_ENCODABLE_SIZE (SLOT_RANGE * (1 + KCSAN_CHECK_ADJACENT))

/*
 * Number of bits we use to store size info.
 */
#define WATCHPOINT_SIZE_BITS bits_per(MAX_ENCODABLE_SIZE)
/*
 * This encoding for addresses discards the upper (1 for is-write + SIZE_BITS);
 * however, most 64-bit architectures do not use the full 64-bit address space.
 * Also, in order for a false positive to be observable 2 things need to happen:
 *
 *	1. different addresses but with the same encoded address race;
 *	2. and both map onto the same watchpoint slots;
 *
 * Both these are assumed to be very unlikely. However, in case it still happens
 * happens, the report logic will filter out the false positive (see report.c).
 */
#define WATCHPOINT_ADDR_BITS (BITS_PER_LONG-1 - WATCHPOINT_SIZE_BITS)

/*
 * Masks to set/retrieve the encoded data.
 */
#define WATCHPOINT_WRITE_MASK BIT(BITS_PER_LONG-1)
#define WATCHPOINT_SIZE_MASK                                                   \
	GENMASK(BITS_PER_LONG-2, BITS_PER_LONG-2 - WATCHPOINT_SIZE_BITS)
#define WATCHPOINT_ADDR_MASK                                                   \
	GENMASK(BITS_PER_LONG-3 - WATCHPOINT_SIZE_BITS, 0)

static inline bool check_encodable(unsigned long addr, size_t size)
{
	return size <= MAX_ENCODABLE_SIZE;
}

static inline long
encode_watchpoint(unsigned long addr, size_t size, bool is_write)
{
	return (long)((is_write ? WATCHPOINT_WRITE_MASK : 0) |
		      (size << WATCHPOINT_ADDR_BITS) |
		      (addr & WATCHPOINT_ADDR_MASK));
}

static __always_inline bool decode_watchpoint(long watchpoint,
					      unsigned long *addr_masked,
					      size_t *size,
					      bool *is_write)
{
	if (watchpoint == INVALID_WATCHPOINT ||
	    watchpoint == CONSUMED_WATCHPOINT)
		return false;

	*addr_masked =    (unsigned long)watchpoint & WATCHPOINT_ADDR_MASK;
	*size	     =   ((unsigned long)watchpoint & WATCHPOINT_SIZE_MASK) >> WATCHPOINT_ADDR_BITS;
	*is_write    = !!((unsigned long)watchpoint & WATCHPOINT_WRITE_MASK);

	return true;
}

/*
 * Return watchpoint slot for an address.
 */
static __always_inline int watchpoint_slot(unsigned long addr)
{
	return (addr / PAGE_SIZE) % CONFIG_KCSAN_NUM_WATCHPOINTS;
}

static __always_inline bool matching_access(unsigned long addr1, size_t size1,
					    unsigned long addr2, size_t size2)
{
	unsigned long end_range1 = addr1 + size1 - 1;
	unsigned long end_range2 = addr2 + size2 - 1;

	return addr1 <= end_range2 && addr2 <= end_range1;
}

#endif /* _KERNEL_KCSAN_ENCODING_H */
