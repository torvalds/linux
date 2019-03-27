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
 * Simple allocate only memory allocator.  Used to allocate memory at application
 * start time.
 *
 * <hr>$Revision: 70030 $<hr>
 *
 */
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
#include <linux/module.h>
#include <asm/octeon/cvmx.h>
#include <asm/octeon/cvmx-bootmem.h>
#else
#if !defined(__FreeBSD__) || !defined(_KERNEL)
#include "executive-config.h"
#endif
#include "cvmx.h"
#include "cvmx-bootmem.h"
#endif
typedef uint32_t cvmx_spinlock_t;


//#define DEBUG

#define ULL unsigned long long
#undef	MAX
#define MAX(a, b)  (((a) > (b)) ? (a) : (b))

#undef	MIN
#define MIN(a, b)  (((a) < (b)) ? (a) : (b))

#define ALIGN_ADDR_UP(addr, align)     (((addr) + (~(align))) & (align))

/**
 * This is the physical location of a cvmx_bootmem_desc_t
 * structure in Octeon's memory. Note that dues to addressing
 * limits or runtime environment it might not be possible to
 * create a C pointer to this structure.
 */
static CVMX_SHARED uint64_t cvmx_bootmem_desc_addr = 0;

/**
 * This macro returns the size of a member of a structure.
 * Logically it is the same as "sizeof(s::field)" in C++, but
 * C lacks the "::" operator.
 */
#define SIZEOF_FIELD(s, field) sizeof(((s*)NULL)->field)

/**
 * This macro returns a member of the cvmx_bootmem_desc_t
 * structure. These members can't be directly addressed as
 * they might be in memory not directly reachable. In the case
 * where bootmem is compiled with LINUX_HOST, the structure
 * itself might be located on a remote Octeon. The argument
 * "field" is the member name of the cvmx_bootmem_desc_t to read.
 * Regardless of the type of the field, the return type is always
 * a uint64_t.
 */
#define CVMX_BOOTMEM_DESC_GET_FIELD(field)                          \
    __cvmx_bootmem_desc_get(cvmx_bootmem_desc_addr,                 \
        offsetof(cvmx_bootmem_desc_t, field),                       \
        SIZEOF_FIELD(cvmx_bootmem_desc_t, field))

/**
 * This macro writes a member of the cvmx_bootmem_desc_t
 * structure. These members can't be directly addressed as
 * they might be in memory not directly reachable. In the case
 * where bootmem is compiled with LINUX_HOST, the structure
 * itself might be located on a remote Octeon. The argument
 * "field" is the member name of the cvmx_bootmem_desc_t to write.
 */
#define CVMX_BOOTMEM_DESC_SET_FIELD(field, value)                   \
    __cvmx_bootmem_desc_set(cvmx_bootmem_desc_addr,                 \
        offsetof(cvmx_bootmem_desc_t, field),                       \
        SIZEOF_FIELD(cvmx_bootmem_desc_t, field), value)

/**
 * This macro returns a member of the
 * cvmx_bootmem_named_block_desc_t structure. These members can't
 * be directly addressed as they might be in memory not directly
 * reachable. In the case where bootmem is compiled with
 * LINUX_HOST, the structure itself might be located on a remote
 * Octeon. The argument "field" is the member name of the
 * cvmx_bootmem_named_block_desc_t to read. Regardless of the type
 * of the field, the return type is always a uint64_t. The "addr"
 * parameter is the physical address of the structure.
 */
#define CVMX_BOOTMEM_NAMED_GET_FIELD(addr, field)                   \
    __cvmx_bootmem_desc_get(addr,                                   \
        offsetof(cvmx_bootmem_named_block_desc_t, field),           \
        SIZEOF_FIELD(cvmx_bootmem_named_block_desc_t, field))

/**
 * This macro writes a member of the cvmx_bootmem_named_block_desc_t
 * structure. These members can't be directly addressed as
 * they might be in memory not directly reachable. In the case
 * where bootmem is compiled with LINUX_HOST, the structure
 * itself might be located on a remote Octeon. The argument
 * "field" is the member name of the
 * cvmx_bootmem_named_block_desc_t to write. The "addr" parameter
 * is the physical address of the structure.
 */
#define CVMX_BOOTMEM_NAMED_SET_FIELD(addr, field, value)            \
    __cvmx_bootmem_desc_set(addr,                                   \
        offsetof(cvmx_bootmem_named_block_desc_t, field),           \
        SIZEOF_FIELD(cvmx_bootmem_named_block_desc_t, field), value)

/**
 * This function is the implementation of the get macros defined
 * for individual structure members. The argument are generated
 * by the macros inorder to read only the needed memory.
 *
 * @param base   64bit physical address of the complete structure
 * @param offset Offset from the beginning of the structure to the member being
 *               accessed.
 * @param size   Size of the structure member.
 *
 * @return Value of the structure member promoted into a uint64_t.
 */
static inline uint64_t __cvmx_bootmem_desc_get(uint64_t base, int offset, int size)
{
    base = (1ull << 63) | (base + offset);
    switch (size)
    {
        case 4:
            return cvmx_read64_uint32(base);
        case 8:
            return cvmx_read64_uint64(base);
        default:
            return 0;
    }
}

/**
 * This function is the implementation of the set macros defined
 * for individual structure members. The argument are generated
 * by the macros in order to write only the needed memory.
 *
 * @param base   64bit physical address of the complete structure
 * @param offset Offset from the beginning of the structure to the member being
 *               accessed.
 * @param size   Size of the structure member.
 * @param value  Value to write into the structure
 */
static inline void __cvmx_bootmem_desc_set(uint64_t base, int offset, int size, uint64_t value)
{
    base = (1ull << 63) | (base + offset);
    switch (size)
    {
        case 4:
            cvmx_write64_uint32(base, value);
            break;
        case 8:
            cvmx_write64_uint64(base, value);
            break;
        default:
            break;
    }
}

/**
 * This function retrieves the string name of a named block. It is
 * more complicated than a simple memcpy() since the named block
 * descriptor may not be directly accessable.
 *
 * @param addr   Physical address of the named block descriptor
 * @param str    String to receive the named block string name
 * @param len    Length of the string buffer, which must match the length
 *               stored in the bootmem descriptor.
 */
static void CVMX_BOOTMEM_NAMED_GET_NAME(uint64_t addr, char *str, int len)
{
#ifndef CVMX_BUILD_FOR_LINUX_HOST
    int l = len;
    char *ptr = str;
    addr |= (1ull << 63);
    addr += offsetof(cvmx_bootmem_named_block_desc_t, name);
    while (l--)
        *ptr++ = cvmx_read64_uint8(addr++);
    str[len] = 0;
#else
    extern void octeon_remote_read_mem(void *buffer, uint64_t physical_address, int length);
    addr += offsetof(cvmx_bootmem_named_block_desc_t, name);
    octeon_remote_read_mem(str, addr, len);
    str[len] = 0;
#endif
}

/**
 * This function stores the string name of a named block. It is
 * more complicated than a simple memcpy() since the named block
 * descriptor may not be directly accessable.
 *
 * @param addr   Physical address of the named block descriptor
 * @param str    String to store into the named block string name
 * @param len    Length of the string buffer, which must match the length
 *               stored in the bootmem descriptor.
 */
static void CVMX_BOOTMEM_NAMED_SET_NAME(uint64_t addr, const char *str, int len)
{
#ifndef CVMX_BUILD_FOR_LINUX_HOST
    int l = len;
    addr |= (1ull << 63);
    addr += offsetof(cvmx_bootmem_named_block_desc_t, name);
    while (l--)
    {
        if (l)
            cvmx_write64_uint8(addr++, *str++);
        else
            cvmx_write64_uint8(addr++, 0);
    }
#else
    extern void octeon_remote_write_mem(uint64_t physical_address, const void *buffer, int length);
    char zero = 0;
    addr += offsetof(cvmx_bootmem_named_block_desc_t, name);
    octeon_remote_write_mem(addr, str, len-1);
    octeon_remote_write_mem(addr+len-1, &zero, 1);
#endif
}

/* See header file for descriptions of functions */

/* Wrapper functions are provided for reading/writing the size and next block
** values as these may not be directly addressible (in 32 bit applications, for instance.)
*/
/* Offsets of data elements in bootmem list, must match cvmx_bootmem_block_header_t */
#define NEXT_OFFSET 0
#define SIZE_OFFSET 8
static void cvmx_bootmem_phy_set_size(uint64_t addr, uint64_t size)
{
    cvmx_write64_uint64((addr + SIZE_OFFSET) | (1ull << 63), size);
}
static void cvmx_bootmem_phy_set_next(uint64_t addr, uint64_t next)
{
    cvmx_write64_uint64((addr + NEXT_OFFSET) | (1ull << 63), next);
}
static uint64_t cvmx_bootmem_phy_get_size(uint64_t addr)
{
    return(cvmx_read64_uint64((addr + SIZE_OFFSET) | (1ull << 63)));
}
static uint64_t cvmx_bootmem_phy_get_next(uint64_t addr)
{
    return(cvmx_read64_uint64((addr + NEXT_OFFSET) | (1ull << 63)));
}

/**
 * Check the version information on the bootmem descriptor
 *
 * @param exact_match
 *               Exact major version to check against. A zero means
 *               check that the version supports named blocks.
 *
 * @return Zero if the version is correct. Negative if the version is
 *         incorrect. Failures also cause a message to be displayed.
 */
static int __cvmx_bootmem_check_version(int exact_match)
{
    int major_version;
#ifdef CVMX_BUILD_FOR_LINUX_HOST
    if (!cvmx_bootmem_desc_addr)
        cvmx_bootmem_desc_addr = cvmx_read64_uint64(0x48100);
#endif
    major_version = CVMX_BOOTMEM_DESC_GET_FIELD(major_version);
    if ((major_version > 3) || (exact_match && major_version != exact_match))
    {
        cvmx_dprintf("ERROR: Incompatible bootmem descriptor version: %d.%d at addr: 0x%llx\n",
            major_version, (int)CVMX_BOOTMEM_DESC_GET_FIELD(minor_version),
            (ULL)cvmx_bootmem_desc_addr);
        return -1;
    }
    else
        return 0;
}

/**
 * Get the low level bootmem descriptor lock. If no locking
 * is specified in the flags, then nothing is done.
 *
 * @param flags  CVMX_BOOTMEM_FLAG_NO_LOCKING means this functions should do
 *               nothing. This is used to support nested bootmem calls.
 */
static inline void __cvmx_bootmem_lock(uint32_t flags)
{
    if (!(flags & CVMX_BOOTMEM_FLAG_NO_LOCKING))
    {
#ifndef CVMX_BUILD_FOR_LINUX_HOST
        /* Unfortunately we can't use the normal cvmx-spinlock code as the
            memory for the bootmem descriptor may be not accessable by a C
            pointer. We use a 64bit XKPHYS address to access the memory
            directly */
        uint64_t lock_addr = (1ull << 63) | (cvmx_bootmem_desc_addr + offsetof(cvmx_bootmem_desc_t, lock));
        unsigned int tmp;

        __asm__ __volatile__(
        ".set noreorder         \n"
        "1: ll   %[tmp], 0(%[addr])\n"
        "   bnez %[tmp], 1b     \n"
        "   li   %[tmp], 1      \n"
        "   sc   %[tmp], 0(%[addr])\n"
        "   beqz %[tmp], 1b     \n"
        "   nop                \n"
        ".set reorder           \n"
        : [tmp] "=&r" (tmp)
        : [addr] "r" (lock_addr)
        : "memory");
#endif
    }
}

/**
 * Release the low level bootmem descriptor lock. If no locking
 * is specified in the flags, then nothing is done.
 *
 * @param flags  CVMX_BOOTMEM_FLAG_NO_LOCKING means this functions should do
 *               nothing. This is used to support nested bootmem calls.
 */
static inline void __cvmx_bootmem_unlock(uint32_t flags)
{
    if (!(flags & CVMX_BOOTMEM_FLAG_NO_LOCKING))
    {
#ifndef CVMX_BUILD_FOR_LINUX_HOST
        /* Unfortunately we can't use the normal cvmx-spinlock code as the
            memory for the bootmem descriptor may be not accessable by a C
            pointer. We use a 64bit XKPHYS address to access the memory
            directly */
        uint64_t lock_addr = (1ull << 63) | (cvmx_bootmem_desc_addr + offsetof(cvmx_bootmem_desc_t, lock));

        CVMX_SYNCW;
        __asm__ __volatile__("sw $0, 0(%[addr])\n"
        :: [addr] "r" (lock_addr)
        : "memory");
        CVMX_SYNCW;
#endif
    }
}

/* Some of the cvmx-bootmem functions dealing with C pointers are not supported
    when we are compiling for CVMX_BUILD_FOR_LINUX_HOST. This ifndef removes
    these functions when they aren't needed */
#ifndef CVMX_BUILD_FOR_LINUX_HOST
/* This functions takes an address range and adjusts it as necessary to
** match the ABI that is currently being used.  This is required to ensure
** that bootmem_alloc* functions only return valid pointers for 32 bit ABIs */
static int __cvmx_validate_mem_range(uint64_t *min_addr_ptr, uint64_t *max_addr_ptr)
{

#if defined(__linux__) && defined(CVMX_ABI_N32)
    {
        extern uint64_t linux_mem32_min;
        extern uint64_t linux_mem32_max;
        /* For 32 bit Linux apps, we need to restrict the allocations to the range
        ** of memory configured for access from userspace.  Also, we need to add mappings
        ** for the data structures that we access.*/

        /* Narrow range requests to be bounded by the 32 bit limits.  octeon_phy_mem_block_alloc()
        ** will reject inconsistent req_size/range requests, so we don't repeat those checks here.
        ** If max unspecified, set to 32 bit maximum. */
        *min_addr_ptr = MIN(MAX(*min_addr_ptr, linux_mem32_min), linux_mem32_max);
        if (!*max_addr_ptr)
            *max_addr_ptr = linux_mem32_max;
        else
            *max_addr_ptr = MAX(MIN(*max_addr_ptr, linux_mem32_max), linux_mem32_min);
    }
#elif defined(CVMX_ABI_N32)
    {
        uint32_t max_phys = 0x0FFFFFFF;  /* Max physical address when 1-1 mappings not used */
#if CVMX_USE_1_TO_1_TLB_MAPPINGS
        max_phys = 0x7FFFFFFF;
#endif
        /* We are are running standalone simple executive, so we need to limit the range
        ** that we allocate from */

        /* Narrow range requests to be bounded by the 32 bit limits.  octeon_phy_mem_block_alloc()
        ** will reject inconsistent req_size/range requests, so we don't repeat those checks here.
        ** If max unspecified, set to 32 bit maximum. */
        *min_addr_ptr = MIN(MAX(*min_addr_ptr, 0x0), max_phys);
        if (!*max_addr_ptr)
            *max_addr_ptr = max_phys;
        else
            *max_addr_ptr = MAX(MIN(*max_addr_ptr, max_phys), 0x0);
    }
#endif

    return 0;
}


void *cvmx_bootmem_alloc_range(uint64_t size, uint64_t alignment, uint64_t min_addr, uint64_t max_addr)
{
    int64_t address;

    __cvmx_validate_mem_range(&min_addr, &max_addr);
    address = cvmx_bootmem_phy_alloc(size, min_addr, max_addr, alignment, 0);

    if (address > 0)
        return cvmx_phys_to_ptr(address);
    else
        return NULL;
}
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
EXPORT_SYMBOL(cvmx_bootmem_alloc_range);
#endif

void *cvmx_bootmem_alloc_address(uint64_t size, uint64_t address, uint64_t alignment)
{
    return cvmx_bootmem_alloc_range(size, alignment, address, address + size);
}


void *cvmx_bootmem_alloc(uint64_t size, uint64_t alignment)
{
    return cvmx_bootmem_alloc_range(size, alignment, 0, 0);
}
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
EXPORT_SYMBOL(cvmx_bootmem_alloc);
#endif

void *cvmx_bootmem_alloc_named_range_once(uint64_t size, uint64_t min_addr, uint64_t max_addr, uint64_t align, const char *name, void (*init)(void*))
{
    int64_t addr;
    void *ptr;
    uint64_t named_block_desc_addr;

    __cvmx_bootmem_lock(0);

    __cvmx_validate_mem_range(&min_addr, &max_addr);
    named_block_desc_addr = cvmx_bootmem_phy_named_block_find(name, CVMX_BOOTMEM_FLAG_NO_LOCKING);

    if (named_block_desc_addr)
    {
        addr = CVMX_BOOTMEM_NAMED_GET_FIELD(named_block_desc_addr, base_addr);
        __cvmx_bootmem_unlock(0);
        return cvmx_phys_to_ptr(addr);
    }

    addr = cvmx_bootmem_phy_named_block_alloc(size, min_addr, max_addr, align, name, CVMX_BOOTMEM_FLAG_NO_LOCKING);

    if (addr < 0)
    {
        __cvmx_bootmem_unlock(0);
        return NULL;
    }
    ptr = cvmx_phys_to_ptr(addr);
    init(ptr);
    __cvmx_bootmem_unlock(0);
    return ptr;
}

static void *cvmx_bootmem_alloc_named_range_flags(uint64_t size, uint64_t min_addr, uint64_t max_addr, uint64_t align, const char *name, uint32_t flags)
{
	int64_t addr;

	__cvmx_validate_mem_range(&min_addr, &max_addr);
	addr = cvmx_bootmem_phy_named_block_alloc(size, min_addr, max_addr, align, name, flags);
	if (addr >= 0)
		return cvmx_phys_to_ptr(addr);
	else
		return NULL;

}

void *cvmx_bootmem_alloc_named_range(uint64_t size, uint64_t min_addr, uint64_t max_addr, uint64_t align, const char *name)
{
    return cvmx_bootmem_alloc_named_range_flags(size, min_addr, max_addr, align, name, 0);
}

void *cvmx_bootmem_alloc_named_address(uint64_t size, uint64_t address, const char *name)
{
    return(cvmx_bootmem_alloc_named_range(size, address, address + size, 0, name));
}

void *cvmx_bootmem_alloc_named(uint64_t size, uint64_t alignment, const char *name)
{
    return(cvmx_bootmem_alloc_named_range(size, 0, 0, alignment, name));
}

void *cvmx_bootmem_alloc_named_flags(uint64_t size, uint64_t alignment, const char *name, uint32_t flags)
{
    return cvmx_bootmem_alloc_named_range_flags(size, 0, 0, alignment, name, flags);
}

int cvmx_bootmem_free_named(const char *name)
{
    return(cvmx_bootmem_phy_named_block_free(name, 0));
}
#endif

const cvmx_bootmem_named_block_desc_t *cvmx_bootmem_find_named_block(const char *name)
{
    /* FIXME: Returning a single static object is probably a bad thing */
    static cvmx_bootmem_named_block_desc_t desc;
    uint64_t named_addr = cvmx_bootmem_phy_named_block_find(name, 0);
    if (named_addr)
    {
        desc.base_addr = CVMX_BOOTMEM_NAMED_GET_FIELD(named_addr, base_addr);
        desc.size = CVMX_BOOTMEM_NAMED_GET_FIELD(named_addr, size);
        strncpy(desc.name, name, sizeof(desc.name));
        desc.name[sizeof(desc.name)-1] = 0;
        return &desc;
    }
    else
        return NULL;
}

void cvmx_bootmem_print_named(void)
{
    cvmx_bootmem_phy_named_block_print();
}

int cvmx_bootmem_init(uint64_t mem_desc_addr)
{
    /* Verify that the size of cvmx_spinlock_t meets our assumptions */
    if (sizeof(cvmx_spinlock_t) != 4)
    {
        cvmx_dprintf("ERROR: Unexpected size of cvmx_spinlock_t\n");
        return(-1);
    }
    if (!cvmx_bootmem_desc_addr)
        cvmx_bootmem_desc_addr = mem_desc_addr;
    return(0);
}


uint64_t cvmx_bootmem_available_mem(uint64_t min_block_size)
{
    return(cvmx_bootmem_phy_available_mem(min_block_size));
}





/*********************************************************************
** The cvmx_bootmem_phy* functions below return 64 bit physical addresses,
** and expose more features that the cvmx_bootmem_functions above.  These are
** required for full memory space access in 32 bit applications, as well as for
** using some advance features.
** Most applications should not need to use these.
**
**/


int64_t cvmx_bootmem_phy_alloc(uint64_t req_size, uint64_t address_min, uint64_t address_max, uint64_t alignment, uint32_t flags)
{

    uint64_t head_addr;
    uint64_t ent_addr;
    uint64_t prev_addr = 0;  /* points to previous list entry, NULL current entry is head of list */
    uint64_t new_ent_addr = 0;
    uint64_t desired_min_addr;
    uint64_t alignment_mask = ~(alignment - 1);

#ifdef DEBUG
    cvmx_dprintf("cvmx_bootmem_phy_alloc: req_size: 0x%llx, min_addr: 0x%llx, max_addr: 0x%llx, align: 0x%llx\n",
           (ULL)req_size, (ULL)address_min, (ULL)address_max, (ULL)alignment);
#endif

    if (__cvmx_bootmem_check_version(0))
        goto error_out;

    /* Do a variety of checks to validate the arguments.  The allocator code will later assume
    ** that these checks have been made.  We validate that the requested constraints are not
    ** self-contradictory before we look through the list of available memory
    */

    /* 0 is not a valid req_size for this allocator */
    if (!req_size)
        goto error_out;

    /* Round req_size up to mult of minimum alignment bytes */
    req_size = (req_size + (CVMX_BOOTMEM_ALIGNMENT_SIZE - 1)) & ~(CVMX_BOOTMEM_ALIGNMENT_SIZE - 1);

    
    /* Enforce minimum alignment (this also keeps the minimum free block
    ** req_size the same as the alignment req_size */
    if (alignment < CVMX_BOOTMEM_ALIGNMENT_SIZE)
    {
        alignment = CVMX_BOOTMEM_ALIGNMENT_SIZE;
    }
    alignment_mask = ~(alignment - 1);

    /* Adjust address minimum based on requested alignment (round up to meet alignment).  Do this here so we can
    ** reject impossible requests up front. (NOP for address_min == 0) */
    if (alignment)
        address_min = (address_min + (alignment - 1)) & ~(alignment - 1);

    /* Convert !0 address_min and 0 address_max to special case of range that specifies an exact
     ** memory block to allocate.  Do this before other checks and adjustments so that this tranformation will be validated */
    if (address_min && !address_max)
        address_max = address_min + req_size;
    else if (!address_min && !address_max)
        address_max = ~0ull;   /* If no limits given, use max limits */

    /* Reject inconsistent args.  We have adjusted these, so this may fail due to our internal changes
    ** even if this check would pass for the values the user supplied. */
    if (req_size > address_max - address_min)
        goto error_out;

    /* Walk through the list entries - first fit found is returned */

    __cvmx_bootmem_lock(flags);
    head_addr = CVMX_BOOTMEM_DESC_GET_FIELD(head_addr);
    ent_addr = head_addr;
    while (ent_addr)
    {
        uint64_t usable_base, usable_max;
        uint64_t ent_size = cvmx_bootmem_phy_get_size(ent_addr);

        if (cvmx_bootmem_phy_get_next(ent_addr) && ent_addr > cvmx_bootmem_phy_get_next(ent_addr))
        {
            cvmx_dprintf("Internal bootmem_alloc() error: ent: 0x%llx, next: 0x%llx\n",
                   (ULL)ent_addr, (ULL)cvmx_bootmem_phy_get_next(ent_addr));
            goto error_out;
        }

        /* Determine if this is an entry that can satisify the request */
        /* Check to make sure entry is large enough to satisfy request */
        usable_base = ALIGN_ADDR_UP(MAX(address_min, ent_addr), alignment_mask);
        usable_max = MIN(address_max, ent_addr + ent_size);
        /* We should be able to allocate block at address usable_base */

        desired_min_addr = usable_base;

        /* Determine if request can be satisfied from the current entry */
        if ((((ent_addr + ent_size) > usable_base && ent_addr < address_max))
            && req_size <= usable_max - usable_base)
        {
            /* We have found an entry that has room to satisfy the request, so allocate it from this entry */

            /* If end CVMX_BOOTMEM_FLAG_END_ALLOC set, then allocate from the end of this block
            ** rather than the beginning */
            if (flags & CVMX_BOOTMEM_FLAG_END_ALLOC)
            {
                desired_min_addr = usable_max - req_size;
                /* Align desired address down to required alignment */
                desired_min_addr &= alignment_mask;
            }

            /* Match at start of entry */
            if (desired_min_addr == ent_addr)
            {
                if (req_size < ent_size)
                {
                    /* big enough to create a new block from top portion of block */
                    new_ent_addr = ent_addr + req_size;
                    cvmx_bootmem_phy_set_next(new_ent_addr, cvmx_bootmem_phy_get_next(ent_addr));
                    cvmx_bootmem_phy_set_size(new_ent_addr, ent_size - req_size);

                    /* Adjust next pointer as following code uses this */
                    cvmx_bootmem_phy_set_next(ent_addr, new_ent_addr);
                }

                /* adjust prev ptr or head to remove this entry from list */
                if (prev_addr)
                {
                    cvmx_bootmem_phy_set_next(prev_addr, cvmx_bootmem_phy_get_next(ent_addr));
                }
                else
                {
                    /* head of list being returned, so update head ptr */
                    CVMX_BOOTMEM_DESC_SET_FIELD(head_addr, cvmx_bootmem_phy_get_next(ent_addr));
                }
                __cvmx_bootmem_unlock(flags);
                return(desired_min_addr);
            }


            /* block returned doesn't start at beginning of entry, so we know
            ** that we will be splitting a block off the front of this one.  Create a new block
            ** from the beginning, add to list, and go to top of loop again.
            **
            ** create new block from high portion of block, so that top block
            ** starts at desired addr
            **/
            new_ent_addr = desired_min_addr;
            cvmx_bootmem_phy_set_next(new_ent_addr, cvmx_bootmem_phy_get_next(ent_addr));
            cvmx_bootmem_phy_set_size(new_ent_addr, cvmx_bootmem_phy_get_size(ent_addr) - (desired_min_addr - ent_addr));
            cvmx_bootmem_phy_set_size(ent_addr, desired_min_addr - ent_addr);
            cvmx_bootmem_phy_set_next(ent_addr, new_ent_addr);
            /* Loop again to handle actual alloc from new block */
        }

        prev_addr = ent_addr;
        ent_addr = cvmx_bootmem_phy_get_next(ent_addr);
    }
error_out:
    /* We didn't find anything, so return error */
    __cvmx_bootmem_unlock(flags);
    return(-1);
}



int __cvmx_bootmem_phy_free(uint64_t phy_addr, uint64_t size, uint32_t flags)
{
    uint64_t cur_addr;
    uint64_t prev_addr = 0;  /* zero is invalid */
    int retval = 0;

#ifdef DEBUG
    cvmx_dprintf("__cvmx_bootmem_phy_free addr: 0x%llx, size: 0x%llx\n", (ULL)phy_addr, (ULL)size);
#endif
    if (__cvmx_bootmem_check_version(0))
        return(0);

    /* 0 is not a valid size for this allocator */
    if (!size)
        return(0);


    __cvmx_bootmem_lock(flags);
    cur_addr = CVMX_BOOTMEM_DESC_GET_FIELD(head_addr);
    if (cur_addr == 0 || phy_addr < cur_addr)
    {
        /* add at front of list - special case with changing head ptr */
        if (cur_addr && phy_addr + size > cur_addr)
            goto bootmem_free_done; /* error, overlapping section */
        else if (phy_addr + size == cur_addr)
        {
            /* Add to front of existing first block */
            cvmx_bootmem_phy_set_next(phy_addr, cvmx_bootmem_phy_get_next(cur_addr));
            cvmx_bootmem_phy_set_size(phy_addr, cvmx_bootmem_phy_get_size(cur_addr) + size);
            CVMX_BOOTMEM_DESC_SET_FIELD(head_addr, phy_addr);

        }
        else
        {
            /* New block before first block */
            cvmx_bootmem_phy_set_next(phy_addr, cur_addr);  /* OK if cur_addr is 0 */
            cvmx_bootmem_phy_set_size(phy_addr, size);
            CVMX_BOOTMEM_DESC_SET_FIELD(head_addr, phy_addr);
        }
        retval = 1;
        goto bootmem_free_done;
    }

    /* Find place in list to add block */
    while (cur_addr && phy_addr > cur_addr)
    {
        prev_addr = cur_addr;
        cur_addr = cvmx_bootmem_phy_get_next(cur_addr);
    }

    if (!cur_addr)
    {
        /* We have reached the end of the list, add on to end, checking
        ** to see if we need to combine with last block
        **/
        if (prev_addr +  cvmx_bootmem_phy_get_size(prev_addr) == phy_addr)
        {
            cvmx_bootmem_phy_set_size(prev_addr, cvmx_bootmem_phy_get_size(prev_addr) + size);
        }
        else
        {
            cvmx_bootmem_phy_set_next(prev_addr, phy_addr);
            cvmx_bootmem_phy_set_size(phy_addr, size);
            cvmx_bootmem_phy_set_next(phy_addr, 0);
        }
        retval = 1;
        goto bootmem_free_done;
    }
    else
    {
        /* insert between prev and cur nodes, checking for merge with either/both */

        if (prev_addr +  cvmx_bootmem_phy_get_size(prev_addr) == phy_addr)
        {
            /* Merge with previous */
            cvmx_bootmem_phy_set_size(prev_addr, cvmx_bootmem_phy_get_size(prev_addr) + size);
            if (phy_addr + size == cur_addr)
            {
                /* Also merge with current */
                cvmx_bootmem_phy_set_size(prev_addr, cvmx_bootmem_phy_get_size(cur_addr) + cvmx_bootmem_phy_get_size(prev_addr));
                cvmx_bootmem_phy_set_next(prev_addr, cvmx_bootmem_phy_get_next(cur_addr));
            }
            retval = 1;
            goto bootmem_free_done;
        }
        else if (phy_addr + size == cur_addr)
        {
            /* Merge with current */
            cvmx_bootmem_phy_set_size(phy_addr, cvmx_bootmem_phy_get_size(cur_addr) + size);
            cvmx_bootmem_phy_set_next(phy_addr, cvmx_bootmem_phy_get_next(cur_addr));
            cvmx_bootmem_phy_set_next(prev_addr, phy_addr);
            retval = 1;
            goto bootmem_free_done;
        }

        /* It is a standalone block, add in between prev and cur */
        cvmx_bootmem_phy_set_size(phy_addr, size);
        cvmx_bootmem_phy_set_next(phy_addr, cur_addr);
        cvmx_bootmem_phy_set_next(prev_addr, phy_addr);


    }
    retval = 1;

bootmem_free_done:
    __cvmx_bootmem_unlock(flags);
    return(retval);

}



void cvmx_bootmem_phy_list_print(void)
{
    uint64_t addr;

    addr = CVMX_BOOTMEM_DESC_GET_FIELD(head_addr);
    cvmx_dprintf("\n\n\nPrinting bootmem block list, descriptor: 0x%llx,  head is 0x%llx\n",
           (ULL)cvmx_bootmem_desc_addr, (ULL)addr);
    cvmx_dprintf("Descriptor version: %d.%d\n",
        (int)CVMX_BOOTMEM_DESC_GET_FIELD(major_version),
        (int)CVMX_BOOTMEM_DESC_GET_FIELD(minor_version));
    if (CVMX_BOOTMEM_DESC_GET_FIELD(major_version) > 3)
    {
        cvmx_dprintf("Warning: Bootmem descriptor version is newer than expected\n");
    }
    if (!addr)
    {
        cvmx_dprintf("mem list is empty!\n");
    }
    while (addr)
    {
        cvmx_dprintf("Block address: 0x%08llx, size: 0x%08llx, next: 0x%08llx\n",
               (ULL)addr,
               (ULL)cvmx_bootmem_phy_get_size(addr),
               (ULL)cvmx_bootmem_phy_get_next(addr));
        addr = cvmx_bootmem_phy_get_next(addr);
    }
    cvmx_dprintf("\n\n");

}


uint64_t cvmx_bootmem_phy_available_mem(uint64_t min_block_size)
{
    uint64_t addr;

    uint64_t available_mem = 0;

    __cvmx_bootmem_lock(0);
    addr = CVMX_BOOTMEM_DESC_GET_FIELD(head_addr);
    while (addr)
    {
        if (cvmx_bootmem_phy_get_size(addr) >= min_block_size)
            available_mem += cvmx_bootmem_phy_get_size(addr);
        addr = cvmx_bootmem_phy_get_next(addr);
    }
    __cvmx_bootmem_unlock(0);
    return(available_mem);

}



uint64_t cvmx_bootmem_phy_named_block_find(const char *name, uint32_t flags)
{
    uint64_t result = 0;

#ifdef DEBUG
    cvmx_dprintf("cvmx_bootmem_phy_named_block_find: %s\n", name);
#endif
    __cvmx_bootmem_lock(flags);
    if (!__cvmx_bootmem_check_version(3))
    {
        int i;
        uint64_t named_block_array_addr = CVMX_BOOTMEM_DESC_GET_FIELD(named_block_array_addr);
        int num_blocks = CVMX_BOOTMEM_DESC_GET_FIELD(named_block_num_blocks);
        int name_length = CVMX_BOOTMEM_DESC_GET_FIELD(named_block_name_len);
        uint64_t named_addr = named_block_array_addr;
        for (i = 0; i < num_blocks; i++)
        {
            uint64_t named_size = CVMX_BOOTMEM_NAMED_GET_FIELD(named_addr, size);
            if (name && named_size)
            {
                char name_tmp[name_length];
                CVMX_BOOTMEM_NAMED_GET_NAME(named_addr, name_tmp, name_length);
                if (!strncmp(name, name_tmp, name_length - 1))
                {
                    result = named_addr;
                    break;
                }
            }
            else if (!name && !named_size)
            {
                result = named_addr;
                break;
            }
            named_addr += sizeof(cvmx_bootmem_named_block_desc_t);
        }
    }
    __cvmx_bootmem_unlock(flags);
    return result;
}

int cvmx_bootmem_phy_named_block_free(const char *name, uint32_t flags)
{
    uint64_t named_block_addr;

    if (__cvmx_bootmem_check_version(3))
        return(0);
#ifdef DEBUG
    cvmx_dprintf("cvmx_bootmem_phy_named_block_free: %s\n", name);
#endif

    /* Take lock here, as name lookup/block free/name free need to be atomic */
    __cvmx_bootmem_lock(flags);

    named_block_addr = cvmx_bootmem_phy_named_block_find(name, CVMX_BOOTMEM_FLAG_NO_LOCKING);
    if (named_block_addr)
    {
        uint64_t named_addr = CVMX_BOOTMEM_NAMED_GET_FIELD(named_block_addr, base_addr);
        uint64_t named_size = CVMX_BOOTMEM_NAMED_GET_FIELD(named_block_addr, size);
#ifdef DEBUG
        cvmx_dprintf("cvmx_bootmem_phy_named_block_free: %s, base: 0x%llx, size: 0x%llx\n",
            name, (ULL)named_addr, (ULL)named_size);
#endif
        __cvmx_bootmem_phy_free(named_addr, named_size, CVMX_BOOTMEM_FLAG_NO_LOCKING);
        /* Set size to zero to indicate block not used. */
        CVMX_BOOTMEM_NAMED_SET_FIELD(named_block_addr, size, 0);
    }
    __cvmx_bootmem_unlock(flags);
    return(!!named_block_addr);  /* 0 on failure, 1 on success */
}





int64_t cvmx_bootmem_phy_named_block_alloc(uint64_t size, uint64_t min_addr, uint64_t max_addr, uint64_t alignment, const char *name, uint32_t flags)
{
    int64_t addr_allocated;
    uint64_t named_block_desc_addr;

#ifdef DEBUG
    cvmx_dprintf("cvmx_bootmem_phy_named_block_alloc: size: 0x%llx, min: 0x%llx, max: 0x%llx, align: 0x%llx, name: %s\n",
                 (ULL)size,
                 (ULL)min_addr,
                 (ULL)max_addr,
                 (ULL)alignment,
                 name);
#endif

    if (__cvmx_bootmem_check_version(3))
        return(-1);

    /* Take lock here, as name lookup/block alloc/name add need to be atomic */

    __cvmx_bootmem_lock(flags);

    named_block_desc_addr = cvmx_bootmem_phy_named_block_find(name, flags | CVMX_BOOTMEM_FLAG_NO_LOCKING);
    if (named_block_desc_addr)
    {
        __cvmx_bootmem_unlock(flags);
        return(-1);
    }

    /* Get pointer to first available named block descriptor */
    named_block_desc_addr = cvmx_bootmem_phy_named_block_find(NULL, flags | CVMX_BOOTMEM_FLAG_NO_LOCKING);
    if (!named_block_desc_addr)
    {
        __cvmx_bootmem_unlock(flags);
        return(-1);
    }

    /* Round size up to mult of minimum alignment bytes
    ** We need the actual size allocated to allow for blocks to be coallesced
    ** when they are freed.  The alloc routine does the same rounding up
    ** on all allocations. */
    size = (size + (CVMX_BOOTMEM_ALIGNMENT_SIZE - 1)) & ~(CVMX_BOOTMEM_ALIGNMENT_SIZE - 1);

    addr_allocated = cvmx_bootmem_phy_alloc(size, min_addr, max_addr, alignment, flags | CVMX_BOOTMEM_FLAG_NO_LOCKING);
    if (addr_allocated >= 0)
    {
        CVMX_BOOTMEM_NAMED_SET_FIELD(named_block_desc_addr, base_addr, addr_allocated);
        CVMX_BOOTMEM_NAMED_SET_FIELD(named_block_desc_addr, size, size);
        CVMX_BOOTMEM_NAMED_SET_NAME(named_block_desc_addr, name, CVMX_BOOTMEM_DESC_GET_FIELD(named_block_name_len));
    }

    __cvmx_bootmem_unlock(flags);
    return(addr_allocated);
}




void cvmx_bootmem_phy_named_block_print(void)
{
    int i;
    int printed = 0;

    uint64_t named_block_array_addr = CVMX_BOOTMEM_DESC_GET_FIELD(named_block_array_addr);
    int num_blocks = CVMX_BOOTMEM_DESC_GET_FIELD(named_block_num_blocks);
    int name_length = CVMX_BOOTMEM_DESC_GET_FIELD(named_block_name_len);
    uint64_t named_block_addr = named_block_array_addr;

#ifdef DEBUG
    cvmx_dprintf("cvmx_bootmem_phy_named_block_print, desc addr: 0x%llx\n",
        (ULL)cvmx_bootmem_desc_addr);
#endif
    if (__cvmx_bootmem_check_version(3))
        return;
    cvmx_dprintf("List of currently allocated named bootmem blocks:\n");
    for (i = 0; i < num_blocks; i++)
    {
        uint64_t named_size = CVMX_BOOTMEM_NAMED_GET_FIELD(named_block_addr, size);
        if (named_size)
        {
            char name_tmp[name_length];
            uint64_t named_addr = CVMX_BOOTMEM_NAMED_GET_FIELD(named_block_addr, base_addr);
            CVMX_BOOTMEM_NAMED_GET_NAME(named_block_addr, name_tmp, name_length);
            printed++;
            cvmx_dprintf("Name: %s, address: 0x%08llx, size: 0x%08llx, index: %d\n",
                   name_tmp, (ULL)named_addr, (ULL)named_size, i);
        }
        named_block_addr += sizeof(cvmx_bootmem_named_block_desc_t);
    }
    if (!printed)
    {
        cvmx_dprintf("No named bootmem blocks exist.\n");
    }

}


int64_t cvmx_bootmem_phy_mem_list_init(uint64_t mem_size, uint32_t low_reserved_bytes, cvmx_bootmem_desc_t *desc_buffer)
{
    uint64_t cur_block_addr;
    int64_t addr;
    int i;

#ifdef DEBUG
    cvmx_dprintf("cvmx_bootmem_phy_mem_list_init (arg desc ptr: %p, cvmx_bootmem_desc: 0x%llx)\n",
        desc_buffer, (ULL)cvmx_bootmem_desc_addr);
#endif

    /* Descriptor buffer needs to be in 32 bit addressable space to be compatible with
    ** 32 bit applications */
    if (!desc_buffer)
    {
        cvmx_dprintf("ERROR: no memory for cvmx_bootmem descriptor provided\n");
        return 0;
    }

    if (mem_size > OCTEON_MAX_PHY_MEM_SIZE)
    {
        mem_size = OCTEON_MAX_PHY_MEM_SIZE;
        cvmx_dprintf("ERROR: requested memory size too large, truncating to maximum size\n");
    }

    if (cvmx_bootmem_desc_addr)
        return 1;

    /* Initialize cvmx pointer to descriptor */
#ifndef CVMX_BUILD_FOR_LINUX_HOST
    cvmx_bootmem_init(cvmx_ptr_to_phys(desc_buffer));
#else
    cvmx_bootmem_init((unsigned long)desc_buffer);
#endif

    /* Fill the bootmem descriptor */
    CVMX_BOOTMEM_DESC_SET_FIELD(lock, 0);
    CVMX_BOOTMEM_DESC_SET_FIELD(flags, 0);
    CVMX_BOOTMEM_DESC_SET_FIELD(head_addr, 0);
    CVMX_BOOTMEM_DESC_SET_FIELD(major_version, CVMX_BOOTMEM_DESC_MAJ_VER);
    CVMX_BOOTMEM_DESC_SET_FIELD(minor_version, CVMX_BOOTMEM_DESC_MIN_VER);
    CVMX_BOOTMEM_DESC_SET_FIELD(app_data_addr, 0);
    CVMX_BOOTMEM_DESC_SET_FIELD(app_data_size, 0);

    /* Set up global pointer to start of list, exclude low 64k for exception vectors, space for global descriptor */
    cur_block_addr = (OCTEON_DDR0_BASE + low_reserved_bytes);

    if (mem_size <= OCTEON_DDR0_SIZE)
    {
        __cvmx_bootmem_phy_free(cur_block_addr, mem_size - low_reserved_bytes, 0);
        goto frees_done;
    }

    __cvmx_bootmem_phy_free(cur_block_addr, OCTEON_DDR0_SIZE - low_reserved_bytes, 0);

    mem_size -= OCTEON_DDR0_SIZE;

    /* Add DDR2 block next if present */
    if (mem_size > OCTEON_DDR1_SIZE)
    {
        __cvmx_bootmem_phy_free(OCTEON_DDR1_BASE, OCTEON_DDR1_SIZE, 0);
        __cvmx_bootmem_phy_free(OCTEON_DDR2_BASE, mem_size - OCTEON_DDR1_SIZE, 0);
    }
    else
    {
        __cvmx_bootmem_phy_free(OCTEON_DDR1_BASE, mem_size, 0);

    }
frees_done:

    /* Initialize the named block structure */
    CVMX_BOOTMEM_DESC_SET_FIELD(named_block_name_len, CVMX_BOOTMEM_NAME_LEN);
    CVMX_BOOTMEM_DESC_SET_FIELD(named_block_num_blocks, CVMX_BOOTMEM_NUM_NAMED_BLOCKS);
    CVMX_BOOTMEM_DESC_SET_FIELD(named_block_array_addr, 0);

    /* Allocate this near the top of the low 256 MBytes of memory */
    addr = cvmx_bootmem_phy_alloc(CVMX_BOOTMEM_NUM_NAMED_BLOCKS * sizeof(cvmx_bootmem_named_block_desc_t),0, 0x10000000, 0 ,CVMX_BOOTMEM_FLAG_END_ALLOC);
    if (addr >= 0)
        CVMX_BOOTMEM_DESC_SET_FIELD(named_block_array_addr, addr);

#ifdef DEBUG
    cvmx_dprintf("cvmx_bootmem_phy_mem_list_init: named_block_array_addr: 0x%llx)\n",
        (ULL)addr);
#endif
    if (!addr)
    {
        cvmx_dprintf("FATAL ERROR: unable to allocate memory for bootmem descriptor!\n");
        return(0);
    }
    for (i=0; i<CVMX_BOOTMEM_NUM_NAMED_BLOCKS; i++)
    {
        CVMX_BOOTMEM_NAMED_SET_FIELD(addr, base_addr, 0);
        CVMX_BOOTMEM_NAMED_SET_FIELD(addr, size, 0);
        addr += sizeof(cvmx_bootmem_named_block_desc_t);
    }

    return(1);
}


void cvmx_bootmem_lock(void)
{
    __cvmx_bootmem_lock(0);
}

void cvmx_bootmem_unlock(void)
{
    __cvmx_bootmem_unlock(0);
}

#ifndef CVMX_BUILD_FOR_LINUX_HOST
void *__cvmx_bootmem_internal_get_desc_ptr(void)
{
    return cvmx_phys_to_ptr(cvmx_bootmem_desc_addr);
}
#endif
