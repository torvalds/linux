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
 * Module to support operations on core such as TLB config, etc.
 *
 * <hr>$Revision: 70030 $<hr>
 *
 */

#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
#include <linux/module.h>
#include <asm/octeon/cvmx.h>
#include <asm/octeon/cvmx-core.h>
#else
#include "cvmx-config.h"
#include "cvmx.h"
#include "cvmx-core.h"
#endif


/**
 * Adds a wired TLB entry, and returns the index of the entry added.
 * Parameters are written to TLB registers without further processing.
 *
 * @param hi     HI register value
 * @param lo0    lo0 register value
 * @param lo1    lo1 register value
 * @param page_mask   pagemask register value
 *
 * @return Success: TLB index used (0-31 Octeon, 0-63 Octeon+, or 0-127
 *         Octeon2). Failure: -1
 */
int cvmx_core_add_wired_tlb_entry(uint64_t hi, uint64_t lo0, uint64_t lo1, cvmx_tlb_pagemask_t page_mask)
{
    uint32_t index;

    CVMX_MF_TLB_WIRED(index);
    if (index >= (unsigned int)cvmx_core_get_tlb_entries())
    {
        return(-1);
    }
    CVMX_MT_ENTRY_HIGH(hi);
    CVMX_MT_ENTRY_LO_0(lo0);
    CVMX_MT_ENTRY_LO_1(lo1);
    CVMX_MT_PAGEMASK(page_mask);
    CVMX_MT_TLB_INDEX(index);
    CVMX_MT_TLB_WIRED(index + 1);
    CVMX_EHB;
    CVMX_TLBWI;
    CVMX_EHB;
    return(index);
}



/**
 * Adds a fixed (wired) TLB mapping.  Returns TLB index used or -1 on error.
 * This is a wrapper around cvmx_core_add_wired_tlb_entry()
 *
 * @param vaddr      Virtual address to map
 * @param page0_addr page 0 physical address, with low 3 bits representing the DIRTY, VALID, and GLOBAL bits
 * @param page1_addr page1 physical address, with low 3 bits representing the DIRTY, VALID, and GLOBAL bits
 * @param page_mask  page mask.
 *
 * @return Success: TLB index used (0-31)
 *         Failure: -1
 */
int cvmx_core_add_fixed_tlb_mapping_bits(uint64_t vaddr, uint64_t page0_addr, uint64_t page1_addr, cvmx_tlb_pagemask_t page_mask)
{

    if ((vaddr & (page_mask | 0x7ff))
        || ((page0_addr & ~0x7ULL) & ((page_mask | 0x7ff) >> 1))
        || ((page1_addr & ~0x7ULL) & ((page_mask | 0x7ff) >> 1)))
    {
        cvmx_dprintf("Error adding tlb mapping: invalid address alignment at vaddr: 0x%llx\n", (unsigned long long)vaddr);
        return(-1);
    }


    return(cvmx_core_add_wired_tlb_entry(vaddr,
                                         (page0_addr >> 6) | (page0_addr & 0x7),
                                         (page1_addr >> 6) | (page1_addr & 0x7),
                                         page_mask));

}
/**
 * Adds a fixed (wired) TLB mapping.  Returns TLB index used or -1 on error.
 * Assumes both pages are valid.  Use cvmx_core_add_fixed_tlb_mapping_bits for more control.
 * This is a wrapper around cvmx_core_add_wired_tlb_entry()
 *
 * @param vaddr      Virtual address to map
 * @param page0_addr page 0 physical address
 * @param page1_addr page1 physical address
 * @param page_mask  page mask.
 *
 * @return Success: TLB index used (0-31)
 *         Failure: -1
 */
int cvmx_core_add_fixed_tlb_mapping(uint64_t vaddr, uint64_t page0_addr, uint64_t page1_addr, cvmx_tlb_pagemask_t page_mask)
{

    return(cvmx_core_add_fixed_tlb_mapping_bits(vaddr, page0_addr | TLB_DIRTY | TLB_VALID | TLB_GLOBAL, page1_addr | TLB_DIRTY | TLB_VALID | TLB_GLOBAL, page_mask));

}

/**
 * Return number of TLB entries.
 */
int cvmx_core_get_tlb_entries(void)
{
    if (OCTEON_IS_MODEL(OCTEON_CN3XXX))
        return 32;
    else if (OCTEON_IS_MODEL(OCTEON_CN5XXX))
        return 64;
    else
        return 128;
}
