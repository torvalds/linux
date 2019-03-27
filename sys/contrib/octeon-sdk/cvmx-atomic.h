/***********************license start***************
 * Copyright (c) 2003-2010  Cavium Inc. (support@cavium.com). All rights
 * reserved.
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.

 *   * Neither the name of Cavium Inc. nor the names of
 *     its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written
 *     permission.

 * This Software, including technical data, may be subject to U.S. export  control
 * laws, including the U.S. Export Administration Act and its  associated
 * regulations, and may be subject to export or import  regulations in other
 * countries.

 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED "AS IS"
 * AND WITH ALL FAULTS AND CAVIUM INC. MAKES NO PROMISES, REPRESENTATIONS OR
 * WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, WITH RESPECT TO
 * THE SOFTWARE, INCLUDING ITS CONDITION, ITS CONFORMITY TO ANY REPRESENTATION OR
 * DESCRIPTION, OR THE EXISTENCE OF ANY LATENT OR PATENT DEFECTS, AND CAVIUM
 * SPECIFICALLY DISCLAIMS ALL IMPLIED (IF ANY) WARRANTIES OF TITLE,
 * MERCHANTABILITY, NONINFRINGEMENT, FITNESS FOR A PARTICULAR PURPOSE, LACK OF
 * VIRUSES, ACCURACY OR COMPLETENESS, QUIET ENJOYMENT, QUIET POSSESSION OR
 * CORRESPONDENCE TO DESCRIPTION. THE ENTIRE  RISK ARISING OUT OF USE OR
 * PERFORMANCE OF THE SOFTWARE LIES WITH YOU.
 ***********************license end**************************************/







/**
 * @file
 *
 * This file provides atomic operations
 *
 * <hr>$Revision: 70030 $<hr>
 *
 *
 */


#ifndef __CVMX_ATOMIC_H__
#define __CVMX_ATOMIC_H__

#ifdef	__cplusplus
extern "C" {
#endif


/**
 * Atomically adds a signed value to a 32 bit (aligned) memory location.
 *
 * This version does not perform 'sync' operations to enforce memory
 * operations.  This should only be used when there are no memory operation
 * ordering constraints.  (This should NOT be used for reference counting -
 * use the standard version instead.)
 *
 * @param ptr    address in memory to add incr to
 * @param incr   amount to increment memory location by (signed)
 */
static inline void cvmx_atomic_add32_nosync(int32_t *ptr, int32_t incr)
{
    if (OCTEON_IS_MODEL(OCTEON_CN3XXX))
    {
	uint32_t tmp;

        __asm__ __volatile__(
        ".set noreorder         \n"
        "1: ll   %[tmp], %[val] \n"
        "   addu %[tmp], %[inc] \n"
        "   sc   %[tmp], %[val] \n"
        "   beqz %[tmp], 1b     \n"
        "   nop                 \n"
        ".set reorder           \n"
        : [val] "+m" (*ptr), [tmp] "=&r" (tmp)
        : [inc] "r" (incr)
        : "memory");
    }
    else
    {
        __asm__ __volatile__(
        "   saa %[inc], (%[base]) \n"
        : "+m" (*ptr)
        : [inc] "r" (incr), [base] "r" (ptr)
        : "memory");
    }
}

/**
 * Atomically adds a signed value to a 32 bit (aligned) memory location.
 *
 * Memory access ordering is enforced before/after the atomic operation,
 * so no additional 'sync' instructions are required.
 *
 *
 * @param ptr    address in memory to add incr to
 * @param incr   amount to increment memory location by (signed)
 */
static inline void cvmx_atomic_add32(int32_t *ptr, int32_t incr)
{
    CVMX_SYNCWS;
    cvmx_atomic_add32_nosync(ptr, incr);
    CVMX_SYNCWS;
}

/**
 * Atomically sets a 32 bit (aligned) memory location to a value
 *
 * @param ptr    address of memory to set
 * @param value  value to set memory location to.
 */
static inline void cvmx_atomic_set32(int32_t *ptr, int32_t value)
{
    CVMX_SYNCWS;
    *ptr = value;
    CVMX_SYNCWS;
}

/**
 * Returns the current value of a 32 bit (aligned) memory
 * location.
 *
 * @param ptr    Address of memory to get
 * @return Value of the memory
 */
static inline int32_t cvmx_atomic_get32(int32_t *ptr)
{
    return *(volatile int32_t *)ptr;
}

/**
 * Atomically adds a signed value to a 64 bit (aligned) memory location.
 *
 * This version does not perform 'sync' operations to enforce memory
 * operations.  This should only be used when there are no memory operation
 * ordering constraints.  (This should NOT be used for reference counting -
 * use the standard version instead.)
 *
 * @param ptr    address in memory to add incr to
 * @param incr   amount to increment memory location by (signed)
 */
static inline void cvmx_atomic_add64_nosync(int64_t *ptr, int64_t incr)
{
    if (OCTEON_IS_MODEL(OCTEON_CN3XXX))
    {
	uint64_t tmp;
        __asm__ __volatile__(
        ".set noreorder         \n"
        "1: lld  %[tmp], %[val] \n"
        "   daddu %[tmp], %[inc] \n"
        "   scd  %[tmp], %[val] \n"
        "   beqz %[tmp], 1b     \n"
        "   nop                 \n"
        ".set reorder           \n"
        : [val] "+m" (*ptr), [tmp] "=&r" (tmp)
        : [inc] "r" (incr)
        : "memory");
    }
    else
    {
        __asm__ __volatile__(
        "   saad %[inc], (%[base])  \n"
        : "+m" (*ptr)
        : [inc] "r" (incr), [base] "r" (ptr)
        : "memory");
    }
}

/**
 * Atomically adds a signed value to a 64 bit (aligned) memory location.
 *
 * Memory access ordering is enforced before/after the atomic operation,
 * so no additional 'sync' instructions are required.
 *
 *
 * @param ptr    address in memory to add incr to
 * @param incr   amount to increment memory location by (signed)
 */
static inline void cvmx_atomic_add64(int64_t *ptr, int64_t incr)
{
    CVMX_SYNCWS;
    cvmx_atomic_add64_nosync(ptr, incr);
    CVMX_SYNCWS;
}

/**
 * Atomically sets a 64 bit (aligned) memory location to a value
 *
 * @param ptr    address of memory to set
 * @param value  value to set memory location to.
 */
static inline void cvmx_atomic_set64(int64_t *ptr, int64_t value)
{
    CVMX_SYNCWS;
    *ptr = value;
    CVMX_SYNCWS;
}

/**
 * Returns the current value of a 64 bit (aligned) memory
 * location.
 *
 * @param ptr    Address of memory to get
 * @return Value of the memory
 */
static inline int64_t cvmx_atomic_get64(int64_t *ptr)
{
    return *(volatile int64_t *)ptr;
}

/**
 * Atomically compares the old value with the value at ptr, and if they match,
 * stores new_val to ptr.
 * If *ptr and old don't match, function returns failure immediately.
 * If *ptr and old match, function spins until *ptr updated to new atomically, or
 *  until *ptr and old no longer match
 *
 * Does no memory synchronization.
 *
 * @return 1 on success (match and store)
 *         0 on no match
 */
static inline uint32_t cvmx_atomic_compare_and_store32_nosync(uint32_t *ptr, uint32_t old_val, uint32_t new_val)
{
    uint32_t tmp, ret;

    __asm__ __volatile__(
    ".set noreorder         \n"
    "1: ll   %[tmp], %[val] \n"
    "   li   %[ret], 0     \n"
    "   bne  %[tmp], %[old], 2f \n"
    "   move %[tmp], %[new_val] \n"
    "   sc   %[tmp], %[val] \n"
    "   beqz %[tmp], 1b     \n"
    "   li   %[ret], 1      \n"
    "2: nop               \n"
    ".set reorder           \n"
    : [val] "+m" (*ptr), [tmp] "=&r" (tmp), [ret] "=&r" (ret)
    : [old] "r" (old_val), [new_val] "r" (new_val)
    : "memory");

    return(ret);

}

/**
 * Atomically compares the old value with the value at ptr, and if they match,
 * stores new_val to ptr.
 * If *ptr and old don't match, function returns failure immediately.
 * If *ptr and old match, function spins until *ptr updated to new atomically, or
 *  until *ptr and old no longer match
 *
 * Does memory synchronization that is required to use this as a locking primitive.
 *
 * @return 1 on success (match and store)
 *         0 on no match
 */
static inline uint32_t cvmx_atomic_compare_and_store32(uint32_t *ptr, uint32_t old_val, uint32_t new_val)
{
    uint32_t ret;
    CVMX_SYNCWS;
    ret = cvmx_atomic_compare_and_store32_nosync(ptr, old_val, new_val);
    CVMX_SYNCWS;
    return ret;


}

/**
 * Atomically compares the old value with the value at ptr, and if they match,
 * stores new_val to ptr.
 * If *ptr and old don't match, function returns failure immediately.
 * If *ptr and old match, function spins until *ptr updated to new atomically, or
 *  until *ptr and old no longer match
 *
 * Does no memory synchronization.
 *
 * @return 1 on success (match and store)
 *         0 on no match
 */
static inline uint64_t cvmx_atomic_compare_and_store64_nosync(uint64_t *ptr, uint64_t old_val, uint64_t new_val)
{
    uint64_t tmp, ret;

    __asm__ __volatile__(
    ".set noreorder         \n"
    "1: lld  %[tmp], %[val] \n"
    "   li   %[ret], 0     \n"
    "   bne  %[tmp], %[old], 2f \n"
    "   move %[tmp], %[new_val] \n"
    "   scd  %[tmp], %[val] \n"
    "   beqz %[tmp], 1b     \n"
    "   li   %[ret], 1      \n"
    "2: nop               \n"
    ".set reorder           \n"
    : [val] "+m" (*ptr), [tmp] "=&r" (tmp), [ret] "=&r" (ret)
    : [old] "r" (old_val), [new_val] "r" (new_val)
    : "memory");

    return(ret);

}

/**
 * Atomically compares the old value with the value at ptr, and if they match,
 * stores new_val to ptr.
 * If *ptr and old don't match, function returns failure immediately.
 * If *ptr and old match, function spins until *ptr updated to new atomically, or
 *  until *ptr and old no longer match
 *
 * Does memory synchronization that is required to use this as a locking primitive.
 *
 * @return 1 on success (match and store)
 *         0 on no match
 */
static inline uint64_t cvmx_atomic_compare_and_store64(uint64_t *ptr, uint64_t old_val, uint64_t new_val)
{
    uint64_t ret;
    CVMX_SYNCWS;
    ret = cvmx_atomic_compare_and_store64_nosync(ptr, old_val, new_val);
    CVMX_SYNCWS;
    return ret;
}

/**
 * Atomically adds a signed value to a 64 bit (aligned) memory location,
 * and returns previous value.
 *
 * This version does not perform 'sync' operations to enforce memory
 * operations.  This should only be used when there are no memory operation
 * ordering constraints.  (This should NOT be used for reference counting -
 * use the standard version instead.)
 *
 * @param ptr    address in memory to add incr to
 * @param incr   amount to increment memory location by (signed)
 *
 * @return Value of memory location before increment
 */
static inline int64_t cvmx_atomic_fetch_and_add64_nosync(int64_t *ptr, int64_t incr)
{
    uint64_t tmp, ret;

#if !defined(__FreeBSD__) || !defined(_KERNEL)
    if (OCTEON_IS_MODEL(OCTEON_CN6XXX) || OCTEON_IS_MODEL(OCTEON_CNF7XXX))
    {
	CVMX_PUSH_OCTEON2;
	if (__builtin_constant_p(incr) && incr == 1)
	{
	    __asm__ __volatile__(
		"laid  %0,(%2)"
		: "=r" (ret), "+m" (ptr) : "r" (ptr) : "memory");
	}
	else if (__builtin_constant_p(incr) && incr == -1)
        {
	    __asm__ __volatile__(
		"ladd  %0,(%2)"
		: "=r" (ret), "+m" (ptr) : "r" (ptr) : "memory");
        }
        else
        {
	    __asm__ __volatile__(
		"laad  %0,(%2),%3"
		: "=r" (ret), "+m" (ptr) : "r" (ptr), "r" (incr) : "memory");
        }
	CVMX_POP_OCTEON2;
    }
    else
    {
#endif
        __asm__ __volatile__(
            ".set noreorder          \n"
            "1: lld   %[tmp], %[val] \n"
            "   move  %[ret], %[tmp] \n"
            "   daddu %[tmp], %[inc] \n"
            "   scd   %[tmp], %[val] \n"
            "   beqz  %[tmp], 1b     \n"
            "   nop                  \n"
            ".set reorder            \n"
            : [val] "+m" (*ptr), [tmp] "=&r" (tmp), [ret] "=&r" (ret)
            : [inc] "r" (incr)
            : "memory");
#if !defined(__FreeBSD__) || !defined(_KERNEL)
    }
#endif

    return (ret);
}

/**
 * Atomically adds a signed value to a 64 bit (aligned) memory location,
 * and returns previous value.
 *
 * Memory access ordering is enforced before/after the atomic operation,
 * so no additional 'sync' instructions are required.
 *
 * @param ptr    address in memory to add incr to
 * @param incr   amount to increment memory location by (signed)
 *
 * @return Value of memory location before increment
 */
static inline int64_t cvmx_atomic_fetch_and_add64(int64_t *ptr, int64_t incr)
{
    uint64_t ret;
    CVMX_SYNCWS;
    ret = cvmx_atomic_fetch_and_add64_nosync(ptr, incr);
    CVMX_SYNCWS;
    return ret;
}

/**
 * Atomically adds a signed value to a 32 bit (aligned) memory location,
 * and returns previous value.
 *
 * This version does not perform 'sync' operations to enforce memory
 * operations.  This should only be used when there are no memory operation
 * ordering constraints.  (This should NOT be used for reference counting -
 * use the standard version instead.)
 *
 * @param ptr    address in memory to add incr to
 * @param incr   amount to increment memory location by (signed)
 *
 * @return Value of memory location before increment
 */
static inline int32_t cvmx_atomic_fetch_and_add32_nosync(int32_t *ptr, int32_t incr)
{
    uint32_t tmp, ret;

#if !defined(__FreeBSD__) || !defined(_KERNEL)
    if (OCTEON_IS_MODEL(OCTEON_CN6XXX) || OCTEON_IS_MODEL(OCTEON_CNF7XXX))
    {
	CVMX_PUSH_OCTEON2;
	if (__builtin_constant_p(incr) && incr == 1)
	{
	    __asm__ __volatile__(
		"lai  %0,(%2)"
		: "=r" (ret), "+m" (ptr) : "r" (ptr) : "memory");
	}
	else if (__builtin_constant_p(incr) && incr == -1)
        {
	    __asm__ __volatile__(
		"lad  %0,(%2)"
		: "=r" (ret), "+m" (ptr) : "r" (ptr) : "memory");
        }
        else
        {
	    __asm__ __volatile__(
		"laa  %0,(%2),%3"
		: "=r" (ret), "+m" (ptr) : "r" (ptr), "r" (incr) : "memory");
        }
	CVMX_POP_OCTEON2;
    }
    else
    {
#endif
        __asm__ __volatile__(
            ".set noreorder         \n"
            "1: ll   %[tmp], %[val] \n"
            "   move %[ret], %[tmp] \n"
            "   addu %[tmp], %[inc] \n"
            "   sc   %[tmp], %[val] \n"
            "   beqz %[tmp], 1b     \n"
            "   nop                 \n"
            ".set reorder           \n"
            : [val] "+m" (*ptr), [tmp] "=&r" (tmp), [ret] "=&r" (ret)
            : [inc] "r" (incr)
            : "memory");
#if !defined(__FreeBSD__) || !defined(_KERNEL)
    }
#endif

    return (ret);
}

/**
 * Atomically adds a signed value to a 32 bit (aligned) memory location,
 * and returns previous value.
 *
 * Memory access ordering is enforced before/after the atomic operation,
 * so no additional 'sync' instructions are required.
 *
 * @param ptr    address in memory to add incr to
 * @param incr   amount to increment memory location by (signed)
 *
 * @return Value of memory location before increment
 */
static inline int32_t cvmx_atomic_fetch_and_add32(int32_t *ptr, int32_t incr)
{
    uint32_t ret;
    CVMX_SYNCWS;
    ret = cvmx_atomic_fetch_and_add32_nosync(ptr, incr);
    CVMX_SYNCWS;
    return ret;
}

/**
 * Atomically set bits in a 64 bit (aligned) memory location,
 * and returns previous value.
 *
 * This version does not perform 'sync' operations to enforce memory
 * operations.  This should only be used when there are no memory operation
 * ordering constraints.
 *
 * @param ptr    address in memory
 * @param mask   mask of bits to set
 *
 * @return Value of memory location before setting bits
 */
static inline uint64_t cvmx_atomic_fetch_and_bset64_nosync(uint64_t *ptr, uint64_t mask)
{
    uint64_t tmp, ret;

    __asm__ __volatile__(
    ".set noreorder         \n"
    "1: lld  %[tmp], %[val] \n"
    "   move %[ret], %[tmp] \n"
    "   or   %[tmp], %[msk] \n"
    "   scd  %[tmp], %[val] \n"
    "   beqz %[tmp], 1b     \n"
    "   nop                 \n"
    ".set reorder           \n"
    : [val] "+m" (*ptr), [tmp] "=&r" (tmp), [ret] "=&r" (ret)
    : [msk] "r" (mask)
    : "memory");

    return (ret);
}

/**
 * Atomically set bits in a 32 bit (aligned) memory location,
 * and returns previous value.
 *
 * This version does not perform 'sync' operations to enforce memory
 * operations.  This should only be used when there are no memory operation
 * ordering constraints.
 *
 * @param ptr    address in memory
 * @param mask   mask of bits to set
 *
 * @return Value of memory location before setting bits
 */
static inline uint32_t cvmx_atomic_fetch_and_bset32_nosync(uint32_t *ptr, uint32_t mask)
{
    uint32_t tmp, ret;

    __asm__ __volatile__(
    ".set noreorder         \n"
    "1: ll   %[tmp], %[val] \n"
    "   move %[ret], %[tmp] \n"
    "   or   %[tmp], %[msk] \n"
    "   sc   %[tmp], %[val] \n"
    "   beqz %[tmp], 1b     \n"
    "   nop                 \n"
    ".set reorder           \n"
    : [val] "+m" (*ptr), [tmp] "=&r" (tmp), [ret] "=&r" (ret)
    : [msk] "r" (mask)
    : "memory");

    return (ret);
}

/**
 * Atomically clear bits in a 64 bit (aligned) memory location,
 * and returns previous value.
 *
 * This version does not perform 'sync' operations to enforce memory
 * operations.  This should only be used when there are no memory operation
 * ordering constraints.
 *
 * @param ptr    address in memory
 * @param mask   mask of bits to clear
 *
 * @return Value of memory location before clearing bits
 */
static inline uint64_t cvmx_atomic_fetch_and_bclr64_nosync(uint64_t *ptr, uint64_t mask)
{
    uint64_t tmp, ret;

    __asm__ __volatile__(
    ".set noreorder         \n"
    "   nor  %[msk], 0      \n"
    "1: lld  %[tmp], %[val] \n"
    "   move %[ret], %[tmp] \n"
    "   and  %[tmp], %[msk] \n"
    "   scd  %[tmp], %[val] \n"
    "   beqz %[tmp], 1b     \n"
    "   nop                 \n"
    ".set reorder           \n"
    : [val] "+m" (*ptr), [tmp] "=&r" (tmp), [ret] "=&r" (ret), [msk] "+r" (mask)
    : : "memory");

    return (ret);
}

/**
 * Atomically clear bits in a 32 bit (aligned) memory location,
 * and returns previous value.
 *
 * This version does not perform 'sync' operations to enforce memory
 * operations.  This should only be used when there are no memory operation
 * ordering constraints.
 *
 * @param ptr    address in memory
 * @param mask   mask of bits to clear
 *
 * @return Value of memory location before clearing bits
 */
static inline uint32_t cvmx_atomic_fetch_and_bclr32_nosync(uint32_t *ptr, uint32_t mask)
{
    uint32_t tmp, ret;

    __asm__ __volatile__(
    ".set noreorder         \n"
    "   nor  %[msk], 0      \n"
    "1: ll   %[tmp], %[val] \n"
    "   move %[ret], %[tmp] \n"
    "   and  %[tmp], %[msk] \n"
    "   sc   %[tmp], %[val] \n"
    "   beqz %[tmp], 1b     \n"
    "   nop                 \n"
    ".set reorder           \n"
    : [val] "+m" (*ptr), [tmp] "=&r" (tmp), [ret] "=&r" (ret), [msk] "+r" (mask)
    : : "memory");

    return (ret);
}

/**
 * Atomically swaps value in 64 bit (aligned) memory location,
 * and returns previous value.
 *
 * This version does not perform 'sync' operations to enforce memory
 * operations.  This should only be used when there are no memory operation
 * ordering constraints.
 *
 * @param ptr       address in memory
 * @param new_val   new value to write
 *
 * @return Value of memory location before swap operation
 */
static inline uint64_t cvmx_atomic_swap64_nosync(uint64_t *ptr, uint64_t new_val)
{
    uint64_t tmp, ret;

#if !defined(__FreeBSD__) || !defined(_KERNEL)
    if (OCTEON_IS_MODEL(OCTEON_CN6XXX) || OCTEON_IS_MODEL(OCTEON_CNF7XXX))
    {
	CVMX_PUSH_OCTEON2;
	if (__builtin_constant_p(new_val) && new_val == 0)
	{
	    __asm__ __volatile__(
		"lacd  %0,(%1)"
		: "=r" (ret) : "r" (ptr) : "memory");
	}
	else if (__builtin_constant_p(new_val) && new_val == ~0ull)
        {
	    __asm__ __volatile__(
		"lasd  %0,(%1)"
		: "=r" (ret) : "r" (ptr) : "memory");
        }
        else
        {
	    __asm__ __volatile__(
		"lawd  %0,(%1),%2"
		: "=r" (ret) : "r" (ptr), "r" (new_val) : "memory");
        }
	CVMX_POP_OCTEON2;
    }
    else
    {
#endif
        __asm__ __volatile__(
            ".set noreorder         \n"
            "1: lld  %[ret], %[val] \n"
            "   move %[tmp], %[new_val] \n"
            "   scd  %[tmp], %[val] \n"
            "   beqz %[tmp],  1b    \n"
            "   nop                 \n"
            ".set reorder           \n"
            : [val] "+m" (*ptr), [tmp] "=&r" (tmp), [ret] "=&r" (ret)
            : [new_val] "r"  (new_val)
            : "memory");
#if !defined(__FreeBSD__) || !defined(_KERNEL)
    }
#endif

    return (ret);
}

/**
 * Atomically swaps value in 32 bit (aligned) memory location,
 * and returns previous value.
 *
 * This version does not perform 'sync' operations to enforce memory
 * operations.  This should only be used when there are no memory operation
 * ordering constraints.
 *
 * @param ptr       address in memory
 * @param new_val   new value to write
 *
 * @return Value of memory location before swap operation
 */
static inline uint32_t cvmx_atomic_swap32_nosync(uint32_t *ptr, uint32_t new_val)
{
    uint32_t tmp, ret;

#if !defined(__FreeBSD__) || !defined(_KERNEL)
    if (OCTEON_IS_MODEL(OCTEON_CN6XXX) || OCTEON_IS_MODEL(OCTEON_CNF7XXX))
    {
	CVMX_PUSH_OCTEON2;
	if (__builtin_constant_p(new_val) && new_val == 0)
	{
	    __asm__ __volatile__(
		"lac  %0,(%1)"
		: "=r" (ret) : "r" (ptr) : "memory");
	}
	else if (__builtin_constant_p(new_val) && new_val == ~0u)
        {
	    __asm__ __volatile__(
		"las  %0,(%1)"
		: "=r" (ret) : "r" (ptr) : "memory");
        }
        else
        {
	    __asm__ __volatile__(
		"law  %0,(%1),%2"
		: "=r" (ret) : "r" (ptr), "r" (new_val) : "memory");
        }
	CVMX_POP_OCTEON2;
    }
    else
    {
#endif
        __asm__ __volatile__(
        ".set noreorder         \n"
        "1: ll   %[ret], %[val] \n"
        "   move %[tmp], %[new_val] \n"
        "   sc   %[tmp], %[val] \n"
        "   beqz %[tmp],  1b    \n"
        "   nop                 \n"
        ".set reorder           \n"
        : [val] "+m" (*ptr), [tmp] "=&r" (tmp), [ret] "=&r" (ret)
        : [new_val] "r"  (new_val)
        : "memory");
#if !defined(__FreeBSD__) || !defined(_KERNEL)
    }
#endif

    return (ret);
}

#ifdef	__cplusplus
}
#endif

#endif /* __CVMX_ATOMIC_H__ */
