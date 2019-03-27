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
 * Interface to the hardware Free Pool Allocator.
 *
 * <hr>$Revision: 70030 $<hr>
 *
 */

#ifndef __CVMX_FPA_H__
#define __CVMX_FPA_H__

#include "cvmx-scratch.h"

#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
#include "cvmx-fpa-defs.h"
#endif

#ifdef	__cplusplus
extern "C" {
#endif

#define CVMX_FPA_NUM_POOLS      8
#define CVMX_FPA_MIN_BLOCK_SIZE 128
#define CVMX_FPA_ALIGNMENT      128

/**
 * Structure describing the data format used for stores to the FPA.
 */
typedef union
{
    uint64_t        u64;
    struct {
        uint64_t    scraddr : 8;    /**< the (64-bit word) location in scratchpad to write to (if len != 0) */
        uint64_t    len     : 8;    /**< the number of words in the response (0 => no response) */
        uint64_t    did     : 8;    /**< the ID of the device on the non-coherent bus */
        uint64_t    addr    :40;    /**< the address that will appear in the first tick on the NCB bus */
    } s;
} cvmx_fpa_iobdma_data_t;

/**
 * Structure describing the current state of a FPA pool.
 */
typedef struct
{
    const char *name;                   /**< Name it was created under */
    uint64_t    size;                   /**< Size of each block */
    void *      base;                   /**< The base memory address of whole block */
    uint64_t    starting_element_count; /**< The number of elements in the pool at creation */
} cvmx_fpa_pool_info_t;

/**
 * Current state of all the pools. Use access functions
 * instead of using it directly.
 */
extern cvmx_fpa_pool_info_t cvmx_fpa_pool_info[CVMX_FPA_NUM_POOLS];

/* CSR typedefs have been moved to cvmx-fpa-defs.h */

/**
 * Return the name of the pool
 *
 * @param pool   Pool to get the name of
 * @return The name
 */
static inline const char *cvmx_fpa_get_name(uint64_t pool)
{
    return cvmx_fpa_pool_info[pool].name;
}

/**
 * Return the base of the pool
 *
 * @param pool   Pool to get the base of
 * @return The base
 */
static inline void *cvmx_fpa_get_base(uint64_t pool)
{
    return cvmx_fpa_pool_info[pool].base;
}

/**
 * Check if a pointer belongs to an FPA pool. Return non-zero
 * if the supplied pointer is inside the memory controlled by
 * an FPA pool.
 *
 * @param pool   Pool to check
 * @param ptr    Pointer to check
 * @return Non-zero if pointer is in the pool. Zero if not
 */
static inline int cvmx_fpa_is_member(uint64_t pool, void *ptr)
{
    return ((ptr >= cvmx_fpa_pool_info[pool].base) &&
            ((char*)ptr < ((char*)(cvmx_fpa_pool_info[pool].base)) + cvmx_fpa_pool_info[pool].size * cvmx_fpa_pool_info[pool].starting_element_count));
}

/**
 * Enable the FPA for use. Must be performed after any CSR
 * configuration but before any other FPA functions.
 */
static inline void cvmx_fpa_enable(void)
{
    cvmx_fpa_ctl_status_t status;

    status.u64 = cvmx_read_csr(CVMX_FPA_CTL_STATUS);
    if (status.s.enb)
    {
	/*
	 * CN68XXP1 should not reset the FPA (doing so may break the
	 * SSO, so we may end up enabling it more than once.  Just
	 * return and don't spew messages.
	 */
	return;
    }

    status.u64 = 0;
    status.s.enb = 1;
    cvmx_write_csr(CVMX_FPA_CTL_STATUS, status.u64);
}

/**
 * Reset FPA to disable. Make sure buffers from all FPA pools are freed 
 * before disabling FPA.
 */
static inline void cvmx_fpa_disable(void)
{
    cvmx_fpa_ctl_status_t status;

    status.u64 = cvmx_read_csr(CVMX_FPA_CTL_STATUS);
    status.s.reset = 1;
    cvmx_write_csr(CVMX_FPA_CTL_STATUS, status.u64);
}

/**
 * Get a new block from the FPA
 *
 * @param pool   Pool to get the block from
 * @return Pointer to the block or NULL on failure
 */
static inline void *cvmx_fpa_alloc(uint64_t pool)
{
    uint64_t address;

    for (;;) {
        address = cvmx_read_csr(CVMX_ADDR_DID(CVMX_FULL_DID(CVMX_OCT_DID_FPA,pool)));
        if (cvmx_likely(address)) {
            return cvmx_phys_to_ptr(address);
        } else {
	   /* If pointers are available, continuously retry.  */
           if (cvmx_read_csr(CVMX_FPA_QUEX_AVAILABLE(pool)) > 0)
               cvmx_wait(50);
           else
               return NULL;
	}
    }
}

/**
 * Asynchronously get a new block from the FPA
 *
 * The result of cvmx_fpa_async_alloc() may be retrieved using
 * cvmx_fpa_async_alloc_finish().
 *
 * @param scr_addr Local scratch address to put response in.  This is a byte address,
 *                  but must be 8 byte aligned.
 * @param pool      Pool to get the block from
 */
static inline void cvmx_fpa_async_alloc(uint64_t scr_addr, uint64_t pool)
{
   cvmx_fpa_iobdma_data_t data;

   /* Hardware only uses 64 bit aligned locations, so convert from byte address
   ** to 64-bit index
   */
   data.s.scraddr = scr_addr >> 3;
   data.s.len = 1;
   data.s.did = CVMX_FULL_DID(CVMX_OCT_DID_FPA,pool);
   data.s.addr = 0;
   cvmx_send_single(data.u64);
}

/**
 * Retrieve the result of cvmx_fpa_async_alloc
 *
 * @param scr_addr The Local scratch address.  Must be the same value
 * passed to cvmx_fpa_async_alloc().
 *
 * @param pool Pool the block came from.  Must be the same value
 * passed to cvmx_fpa_async_alloc.
 *
 * @return Pointer to the block or NULL on failure
 */
static inline void *cvmx_fpa_async_alloc_finish(uint64_t scr_addr, uint64_t pool)
{
    uint64_t address;

    CVMX_SYNCIOBDMA;

    address = cvmx_scratch_read64(scr_addr);
    if (cvmx_likely(address))
        return cvmx_phys_to_ptr(address);
    else
        return cvmx_fpa_alloc(pool);
}

/**
 * Free a block allocated with a FPA pool.
 * Does NOT provide memory ordering in cases where the memory block was modified by the core.
 *
 * @param ptr    Block to free
 * @param pool   Pool to put it in
 * @param num_cache_lines
 *               Cache lines to invalidate
 */
static inline void cvmx_fpa_free_nosync(void *ptr, uint64_t pool, uint64_t num_cache_lines)
{
    cvmx_addr_t newptr;
    newptr.u64 = cvmx_ptr_to_phys(ptr);
    newptr.sfilldidspace.didspace = CVMX_ADDR_DIDSPACE(CVMX_FULL_DID(CVMX_OCT_DID_FPA,pool));
    asm volatile ("" : : : "memory");  /* Prevent GCC from reordering around free */
    /* value written is number of cache lines not written back */
    cvmx_write_io(newptr.u64, num_cache_lines);
}

/**
 * Free a block allocated with a FPA pool.  Provides required memory
 * ordering in cases where memory block was modified by core.
 *
 * @param ptr    Block to free
 * @param pool   Pool to put it in
 * @param num_cache_lines
 *               Cache lines to invalidate
 */
static inline void cvmx_fpa_free(void *ptr, uint64_t pool, uint64_t num_cache_lines)
{
    cvmx_addr_t newptr;
    newptr.u64 = cvmx_ptr_to_phys(ptr);
    newptr.sfilldidspace.didspace = CVMX_ADDR_DIDSPACE(CVMX_FULL_DID(CVMX_OCT_DID_FPA,pool));
    /* Make sure that any previous writes to memory go out before we free this buffer.
    ** This also serves as a barrier to prevent GCC from reordering operations to after
    ** the free. */
    CVMX_SYNCWS;
    /* value written is number of cache lines not written back */
    cvmx_write_io(newptr.u64, num_cache_lines);
}

/**
 * Setup a FPA pool to control a new block of memory.
 * This can only be called once per pool. Make sure proper
 * locking enforces this.
 *
 * @param pool       Pool to initialize
 *                   0 <= pool < 8
 * @param name       Constant character string to name this pool.
 *                   String is not copied.
 * @param buffer     Pointer to the block of memory to use. This must be
 *                   accessable by all processors and external hardware.
 * @param block_size Size for each block controlled by the FPA
 * @param num_blocks Number of blocks
 *
 * @return 0 on Success,
 *         -1 on failure
 */
extern int cvmx_fpa_setup_pool(uint64_t pool, const char *name, void *buffer,
                                uint64_t block_size, uint64_t num_blocks);

/**
 * Shutdown a Memory pool and validate that it had all of
 * the buffers originally placed in it. This should only be
 * called by one processor after all hardware has finished
 * using the pool. Most like you will want to have called
 * cvmx_helper_shutdown_packet_io_global() before this
 * function to make sure all FPA buffers are out of the packet
 * IO hardware.
 *
 * @param pool   Pool to shutdown
 *
 * @return Zero on success
 *         - Positive is count of missing buffers
 *         - Negative is too many buffers or corrupted pointers
 */
extern uint64_t cvmx_fpa_shutdown_pool(uint64_t pool);

/**
 * Get the size of blocks controlled by the pool
 * This is resolved to a constant at compile time.
 *
 * @param pool   Pool to access
 * @return Size of the block in bytes
 */
uint64_t cvmx_fpa_get_block_size(uint64_t pool);

#ifdef	__cplusplus
}
#endif

#endif /*  __CVM_FPA_H__ */
