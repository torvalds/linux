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


#ifndef __CVMX_TLB_H__
#define __CVMX_TLB_H__

/**
 * @file
 *
 * cvmx-tlb provides access functions for setting up TLB entries for simple
 * executive applications.
 *
 * <hr>$Revision: 41586 $<hr>
 */

#ifdef	__cplusplus
extern "C" {
#endif

/**
 *  Find a free entry that can be used for share memory mapping.
 *
 *  @return -1: no free entry found
 *  @return :  a free entry
 */
int cvmx_tlb_allocate_runtime_entry(void);

/**
 *  Invalidate the TLB entry. Remove previous mapping if one was set up
 *  @param tlbi
 */
void cvmx_tlb_free_runtime_entry(uint32_t tlbi);

/**
 *  Debug routine to show all shared memory mapping
 */
void cvmx_tlb_dump_shared_mapping(void);

/**
 *  Program a single TLB entry to enable the provided vaddr to paddr mapping.
 *
 *  @param index  Index of the TLB entry
 *  @param vaddr  The virtual address for this mapping
 *  @param paddr  The physical address for this mapping
 *  @param size   Size of the mapping
 *  @param tlb_flags  Entry mapping flags
 */
void cvmx_tlb_write_entry(int index, uint64_t vaddr, uint64_t paddr,
                          uint64_t size, uint64_t tlb_flags);


/**
 *  Program a single TLB entry to enable the provided vaddr to paddr mapping.
 *  This version adds a wired entry that should not be changed at run time
 *
 *  @param index  Index of the TLB entry
 *  @param vaddr  The virtual address for this mapping
 *  @param paddr  The physical address for this mapping
 *  @param size   Size of the mapping
 *  @param tlb_flags  Entry mapping flags
 *  @return -1: TLB out of entries
 *           0:  fixed entry added
 *
 */
int cvmx_tlb_add_fixed_entry(uint64_t vaddr, uint64_t paddr,
                          uint64_t size, uint64_t tlb_flags);

/**
 *  Program a single TLB entry to enable the provided vaddr to paddr mapping.
 *  This version writes a runtime entry. It will check the index to make sure
 *  not to overwrite any fixed entries.
 *
 *  @param index  Index of the TLB entry
 *  @param vaddr  The virtual address for this mapping
 *  @param paddr  The physical address for this mapping
 *  @param size   Size of the mapping
 *  @param tlb_flags  Entry mapping flags
 */
void cvmx_tlb_write_runtime_entry(int index, uint64_t vaddr, uint64_t paddr,
                          uint64_t size, uint64_t tlb_flags);


/**
 *  Find the TLB index of a given virtual address
 *
 *  @param vaddr  The virtual address to look up
 *  @return  -1  not TLB mapped
 *  	     >=0 TLB TLB index
 */
int cvmx_tlb_lookup(uint64_t vaddr);

/**
 *  Debug routine to show all TLB entries of this core
 *
 */
void cvmx_tlb_dump_all(void);

/*
 * @INTERNAL
 * return the next power of two value for the given input <v>
 *
 * @param v input value
 * @return next power of two value for v
 */
static inline uint64_t __upper_power_of_two(uint64_t v)
{
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v |= v >> 32;
    v++;
    return v;
}

/**
 * @INTERNAL
 * Check if the given value 'v' is power of two.
 *
 * @param v input value
 * @return 1  yes
 * 	   0  no
 */
static inline int __is_power_of_two(uint64_t v)
{
	int num_of_1s = 0;

	CVMX_DPOP(num_of_1s, v);
	return (num_of_1s  ==  1 );
}

#ifdef	__cplusplus
}
#endif

#endif
