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
 * cvmx-tlb supplies per core TLB access functions for simple executive
 * applications.
 *
 * <hr>$Revision: 41586 $<hr>
 */
#include "cvmx.h"
#include "cvmx-tlb.h"
#include "cvmx-core.h"
#include <math.h>

extern __uint32_t  __log2(__uint32_t);
//#define DEBUG

/**
 * @INTERNAL
 * issue the tlb read instruction
 */
static inline void __tlb_read(void){
    CVMX_EHB;
    CVMX_TLBR;
    CVMX_EHB;
}

/**
 * @INTERNAL
 * issue the tlb write instruction
 */
static inline void __tlb_write(void){

    CVMX_EHB;
    CVMX_TLBWI;
    CVMX_EHB;
}

/**
 * @INTERNAL
 * issue the tlb read instruction
 */
static inline int __tlb_probe(uint64_t hi){
    int index;
    CVMX_EHB;
    CVMX_MT_ENTRY_HIGH(hi);
    CVMX_TLBP;
    CVMX_EHB;

    CVMX_MF_TLB_INDEX(index);

    if (index < 0) index = -1;

    return index;
}

/**
 * @INTERNAL
 * read a single tlb entry
 *
 * return 0: tlb entry is read
 *    -1: index is invalid
 */
static inline int __tlb_read_index(uint32_t tlbi){

    if (tlbi >= (uint32_t)cvmx_core_get_tlb_entries()) {
        return -1;
    }

    CVMX_MT_TLB_INDEX(tlbi);
    __tlb_read();

    return 0;
}

/**
 * @INTERNAL
 * write a single tlb entry
 *
 * return 0: tlb entry is read
 *    -1: index is invalid
 */
static inline int __tlb_write_index(uint32_t tlbi,
        			    uint64_t hi, uint64_t lo0,
				    uint64_t lo1, uint64_t pagemask)
{

    if (tlbi >= (uint32_t)cvmx_core_get_tlb_entries()) {
        return -1;
    }

#ifdef DEBUG
    cvmx_dprintf("cvmx-tlb-dbg: "
	    "write TLB %d: hi %lx, lo0 %lx, lo1 %lx, pagemask %lx \n",
		tlbi, hi, lo0, lo1, pagemask);
#endif

    CVMX_MT_TLB_INDEX(tlbi);
    CVMX_MT_ENTRY_HIGH(hi);
    CVMX_MT_ENTRY_LO_0(lo0);
    CVMX_MT_ENTRY_LO_1(lo1);
    CVMX_MT_PAGEMASK(pagemask);
    __tlb_write();

    return 0;
}

/**
 * @INTERNAL
 * Determine if a TLB entry is free to use
 */
static inline int __tlb_entry_is_free(uint32_t tlbi) {
    int ret = 0;
    uint64_t lo0 = 0, lo1 = 0;

    if (tlbi < (uint32_t)cvmx_core_get_tlb_entries()) {

        __tlb_read_index(tlbi);

        /* Unused entries have neither even nor odd page mapped */
    	CVMX_MF_ENTRY_LO_0(lo0);
    	CVMX_MF_ENTRY_LO_1(lo1);

        if ( !(lo0 & TLB_VALID) && !(lo1 & TLB_VALID)) {
            ret = 1;
        }
    }

    return ret;
}


/**
 * @INTERNAL
 * dump a single tlb entry
 */
static inline void __tlb_dump_index(uint32_t tlbi)
{
    if (tlbi < (uint32_t)cvmx_core_get_tlb_entries()) {

        if (__tlb_entry_is_free(tlbi)) {
#ifdef DEBUG
            cvmx_dprintf("Index: %3d Free \n", tlbi);
#endif
        } else {
            uint64_t lo0, lo1, pgmask;
            uint32_t hi;
#ifdef DEBUG
            uint32_t c0, c1;
            int width = 13;
#endif

            __tlb_read_index(tlbi);

            CVMX_MF_ENTRY_HIGH(hi);
            CVMX_MF_ENTRY_LO_0(lo0);
            CVMX_MF_ENTRY_LO_1(lo1);
            CVMX_MF_PAGEMASK(pgmask);

#ifdef DEBUG
            c0 = ( lo0 >> 3 ) & 7;
            c1 = ( lo1 >> 3 ) & 7;

            cvmx_dprintf("va=%0*lx asid=%02x\n",
                               width, (hi & ~0x1fffUL), hi & 0xff);

            cvmx_dprintf("\t[pa=%0*lx c=%d d=%d v=%d g=%d] ",
                               width,
                               (lo0 << 6) & PAGE_MASK, c0,
                               (lo0 & 4) ? 1 : 0,
                               (lo0 & 2) ? 1 : 0,
                               (lo0 & 1) ? 1 : 0);
            cvmx_dprintf("[pa=%0*lx c=%d d=%d v=%d g=%d]\n",
                               width,
                               (lo1 << 6) & PAGE_MASK, c1,
                               (lo1 & 4) ? 1 : 0,
                               (lo1 & 2) ? 1 : 0,
                               (lo1 & 1) ? 1 : 0);

#endif
        }
    }
}

/**
 * @INTERNAL
 * dump a single tlb entry
 */
static inline uint32_t __tlb_wired_index() {
    uint32_t  tlbi;

    CVMX_MF_TLB_WIRED(tlbi);
    return tlbi;
}

/**
 *  Find a free entry that can be used for share memory mapping.
 *
 *  @return -1: no free entry found
 *  @return :  a free entry
 */
int cvmx_tlb_allocate_runtime_entry(void)
{
    uint32_t i, ret = -1;

    for (i = __tlb_wired_index(); i< (uint32_t)cvmx_core_get_tlb_entries(); i++) {

    	/* Check to make sure the index is free to use */
        if (__tlb_entry_is_free(i)) {
		/* Found and return */
        	ret = i;
        	break;
	}
    }

    return ret;
}

/**
 *  Invalidate the TLB entry. Remove previous mapping if one was set up
 */
void cvmx_tlb_free_runtime_entry(uint32_t tlbi)
{
    /* Invalidate an unwired TLB entry */
    if ((tlbi < (uint32_t)cvmx_core_get_tlb_entries()) && (tlbi >= __tlb_wired_index())) {
        __tlb_write_index(tlbi, 0xffffffff80000000ULL, 0, 0, 0);
    }
}


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
			uint64_t size, uint64_t tlb_flags) {
	uint64_t lo0, lo1, hi, pagemask;

	if ( __is_power_of_two(size) ) {
		if ( (__log2(size) & 1 ) == 0) {
			/* size is not power of 4,  we only need to map
  			   one page, figure out even or odd page to map */
			if ((vaddr >> __log2(size) & 1))  {
				lo0 =  0;
				lo1 =  ((paddr >> 12) << 6) | tlb_flags;
				hi =   ((vaddr - size) >> 12) << 12;
			}else {
				lo0 =  ((paddr >> 12) << 6) | tlb_flags;
				lo1 =  0;
				hi =   ((vaddr) >> 12) << 12;
			}
			pagemask = (size - 1) & (~1<<11);
		}else {
			lo0 =  ((paddr >> 12)<< 6) | tlb_flags;
			lo1 =  (((paddr + size /2) >> 12) << 6) | tlb_flags;
			hi =   ((vaddr) >> 12) << 12;
			pagemask = ((size/2) -1) & (~1<<11);
		}


        	__tlb_write_index(index, hi, lo0, lo1, pagemask);

	}
}


/**
 *  Program a single TLB entry to enable the provided vaddr to paddr mapping.
 *  This version adds a wired entry that should not be changed at run time
 *
 *  @param vaddr  The virtual address for this mapping
 *  @param paddr  The physical address for this mapping
 *  @param size   Size of the mapping
 *  @param tlb_flags  Entry mapping flags
 *  @return -1: TLB out of entries
 * 	     0:  fixed entry added
 */
int cvmx_tlb_add_fixed_entry( uint64_t vaddr, uint64_t paddr, uint64_t size, uint64_t tlb_flags) {

    uint64_t index;
    int ret = 0;

    CVMX_MF_TLB_WIRED(index);

    /* Check to make sure if the index is free to use */
    if (index < (uint32_t)cvmx_core_get_tlb_entries() && __tlb_entry_is_free(index) ) {
	cvmx_tlb_write_entry(index, vaddr, paddr, size, tlb_flags);

	if (!__tlb_entry_is_free(index)) {
        	/* Bump up the wired register*/
        	CVMX_MT_TLB_WIRED(index + 1);
		ret  = 1;
	}
    }
    return ret;
}


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
                          uint64_t size, uint64_t tlb_flags)
{

    int wired_index;
    CVMX_MF_TLB_WIRED(wired_index);

    if (index >= wired_index) {
	cvmx_tlb_write_entry(index, vaddr, paddr, size, tlb_flags);
    }

}



/**
 * Find the TLB index of a given virtual address
 *
 *  @param vaddr  The virtual address to look up
 *  @return  -1  not TLB mapped
 *           >=0 TLB TLB index
 */
int cvmx_tlb_lookup(uint64_t vaddr) {
	uint64_t hi= (vaddr >> 13 ) << 13; /* We always use ASID 0 */

	return  __tlb_probe(hi);
}

/**
 *  Debug routine to show all shared memory mapping
 */
void cvmx_tlb_dump_shared_mapping(void) {
    uint32_t tlbi;

    for ( tlbi = __tlb_wired_index(); tlbi<(uint32_t)cvmx_core_get_tlb_entries(); tlbi++ ) {
        __tlb_dump_index(tlbi);
    }
}

/**
 *  Debug routine to show all TLB entries of this core
 *
 */
void cvmx_tlb_dump_all(void) {

    uint32_t tlbi;

    for (tlbi = 0; tlbi<= (uint32_t)cvmx_core_get_tlb_entries(); tlbi++ ) {
        __tlb_dump_index(tlbi);
    }
}

