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


#ifndef __CVMX_CORE_H__
#define __CVMX_CORE_H__

#ifdef	__cplusplus
extern "C" {
#endif

/**
 * The types of performance counters supported per cpu
 */
typedef enum
{
    CVMX_CORE_PERF_NONE      = 0,    /**< Turn off the performance counter */
    CVMX_CORE_PERF_CLK       = 1,    /**< Conditionally clocked cycles (as opposed to count/cvm_count which count even with no clocks) */
    CVMX_CORE_PERF_ISSUE     = 2,    /**< Instructions issued but not retired */
    CVMX_CORE_PERF_RET       = 3,    /**< Instructions retired */
    CVMX_CORE_PERF_NISSUE    = 4,    /**< Cycles no issue */
    CVMX_CORE_PERF_SISSUE    = 5,    /**< Cycles single issue */
    CVMX_CORE_PERF_DISSUE    = 6,    /**< Cycles dual issue */
    CVMX_CORE_PERF_IFI       = 7,    /**< Cycle ifetch issued (but not necessarily commit to pp_mem) */
    CVMX_CORE_PERF_BR        = 8,    /**< Branches retired */
    CVMX_CORE_PERF_BRMIS     = 9,    /**< Branch mispredicts */
    CVMX_CORE_PERF_J         = 10,   /**< Jumps retired */
    CVMX_CORE_PERF_JMIS      = 11,   /**< Jumps mispredicted */
    CVMX_CORE_PERF_REPLAY    = 12,   /**< Mem Replays */
    CVMX_CORE_PERF_IUNA      = 13,   /**< Cycles idle due to unaligned_replays */
    CVMX_CORE_PERF_TRAP      = 14,   /**< trap_6a signal */
    CVMX_CORE_PERF_UULOAD    = 16,   /**< Unexpected unaligned loads (REPUN=1) */
    CVMX_CORE_PERF_UUSTORE   = 17,   /**< Unexpected unaligned store (REPUN=1) */
    CVMX_CORE_PERF_ULOAD     = 18,   /**< Unaligned loads (REPUN=1 or USEUN=1) */
    CVMX_CORE_PERF_USTORE    = 19,   /**< Unaligned store (REPUN=1 or USEUN=1) */
    CVMX_CORE_PERF_EC        = 20,   /**< Exec clocks(must set CvmCtl[DISCE] for accurate timing) */
    CVMX_CORE_PERF_MC        = 21,   /**< Mul clocks(must set CvmCtl[DISCE] for accurate timing) */
    CVMX_CORE_PERF_CC        = 22,   /**< Crypto clocks(must set CvmCtl[DISCE] for accurate timing) */
    CVMX_CORE_PERF_CSRC      = 23,   /**< Issue_csr clocks(must set CvmCtl[DISCE] for accurate timing) */
    CVMX_CORE_PERF_CFETCH    = 24,   /**< Icache committed fetches (demand+prefetch) */
    CVMX_CORE_PERF_CPREF     = 25,   /**< Icache committed prefetches */
    CVMX_CORE_PERF_ICA       = 26,   /**< Icache aliases */
    CVMX_CORE_PERF_II        = 27,   /**< Icache invalidates */
    CVMX_CORE_PERF_IP        = 28,   /**< Icache parity error */
    CVMX_CORE_PERF_CIMISS    = 29,   /**< Cycles idle due to imiss (must set CvmCtl[DISCE] for accurate timing) */
    CVMX_CORE_PERF_WBUF      = 32,   /**< Number of write buffer entries created */
    CVMX_CORE_PERF_WDAT      = 33,   /**< Number of write buffer data cycles used (may need to set CvmCtl[DISCE] for accurate counts) */
    CVMX_CORE_PERF_WBUFLD    = 34,   /**< Number of write buffer entries forced out by loads */
    CVMX_CORE_PERF_WBUFFL    = 35,   /**< Number of cycles that there was no available write buffer entry (may need to set CvmCtl[DISCE] and CvmMemCtl[MCLK] for accurate counts) */
    CVMX_CORE_PERF_WBUFTR    = 36,   /**< Number of stores that found no available write buffer entries */
    CVMX_CORE_PERF_BADD      = 37,   /**< Number of address bus cycles used (may need to set CvmCtl[DISCE] for accurate counts) */
    CVMX_CORE_PERF_BADDL2    = 38,   /**< Number of address bus cycles not reflected (i.e. destined for L2) (may need to set CvmCtl[DISCE] for accurate counts) */
    CVMX_CORE_PERF_BFILL     = 39,   /**< Number of fill bus cycles used (may need to set CvmCtl[DISCE] for accurate counts) */
    CVMX_CORE_PERF_DDIDS     = 40,   /**< Number of Dstream DIDs created */
    CVMX_CORE_PERF_IDIDS     = 41,   /**< Number of Istream DIDs created */
    CVMX_CORE_PERF_DIDNA     = 42,   /**< Number of cycles that no DIDs were available (may need to set CvmCtl[DISCE] and CvmMemCtl[MCLK] for accurate counts) */
    CVMX_CORE_PERF_LDS       = 43,   /**< Number of load issues */
    CVMX_CORE_PERF_LMLDS     = 44,   /**< Number of local memory load */
    CVMX_CORE_PERF_IOLDS     = 45,   /**< Number of I/O load issues */
    CVMX_CORE_PERF_DMLDS     = 46,   /**< Number of loads that were not prefetches and missed in the cache */
    CVMX_CORE_PERF_STS       = 48,   /**< Number of store issues */
    CVMX_CORE_PERF_LMSTS     = 49,   /**< Number of local memory store issues */
    CVMX_CORE_PERF_IOSTS     = 50,   /**< Number of I/O store issues */
    CVMX_CORE_PERF_IOBDMA    = 51,   /**< Number of IOBDMAs */
    CVMX_CORE_PERF_DTLB      = 53,   /**< Number of dstream TLB refill, invalid, or modified exceptions */
    CVMX_CORE_PERF_DTLBAD    = 54,   /**< Number of dstream TLB address errors */
    CVMX_CORE_PERF_ITLB      = 55,   /**< Number of istream TLB refill, invalid, or address error exceptions */
    CVMX_CORE_PERF_SYNC      = 56,   /**< Number of SYNC stall cycles (may need to set CvmCtl[DISCE] for accurate counts) */
    CVMX_CORE_PERF_SYNCIOB   = 57,   /**< Number of SYNCIOBDMA stall cycles (may need to set CvmCtl[DISCE] for accurate counts) */
    CVMX_CORE_PERF_SYNCW     = 58,   /**< Number of SYNCWs */
    /* Added in CN63XX */
    CVMX_CORE_PERF_ERETMIS   = 64,   /**< D/eret mispredicts */
    CVMX_CORE_PERF_LIKMIS    = 65,   /**< Branch likely mispredicts */
    CVMX_CORE_PERF_HAZTR     = 66,   /**< Hazard traps due to *MTC0 to CvmCtl, Perf counter control, EntryHi, or CvmMemCtl registers */
    CVMX_CORE_PERF_MAX               /**< This not a counter, just a marker for the highest number */
} cvmx_core_perf_t;

/**
 * Bit description of the COP0 counter control register
 */
typedef union
{
    uint32_t u32;
    struct
    {
#ifdef __BIG_ENDIAN_BITFIELD
        uint32_t m              : 1; /**< Set to 1 for sel 0 and 0 for sel 2, indicating there are two performance counters */
        uint32_t w              : 1; /**< Set to 1 indicating counters are 64 bit */
        uint32_t reserved_11_29 :15;
        cvmx_core_perf_t event  :10; /**< Selects the event to be counted by the corresponding Counter Register */
        uint32_t ie             : 1; /**< Interrupt Enable */
        uint32_t u              : 1; /**< Count in user mode */
        uint32_t s              : 1; /**< Count in supervisor mode */
        uint32_t k              : 1; /**< Count in kernel mode */
        uint32_t ex             : 1; /**< Count in exception context */
#else
	uint32_t ex             : 1;
        uint32_t k              : 1;
        uint32_t s              : 1;
        uint32_t u              : 1;
        uint32_t ie             : 1;
        uint32_t event          :10;
        uint32_t reserved_11_29 :15;
        uint32_t w              : 1;
        uint32_t m              : 1;
#endif
    } s;
} cvmx_core_perf_control_t;

typedef enum {
    CVMX_TLB_PAGEMASK_4K   = 0x3     << 11,
    CVMX_TLB_PAGEMASK_16K  = 0xF     << 11,
    CVMX_TLB_PAGEMASK_64K  = 0x3F    << 11,
    CVMX_TLB_PAGEMASK_256K = 0xFF    << 11,
    CVMX_TLB_PAGEMASK_1M   = 0x3FF   << 11,
    CVMX_TLB_PAGEMASK_4M   = 0xFFF   << 11,
    CVMX_TLB_PAGEMASK_16M  = 0x3FFF  << 11,
    CVMX_TLB_PAGEMASK_64M  = 0xFFFF  << 11,
    CVMX_TLB_PAGEMASK_256M = 0x3FFFF << 11,
} cvmx_tlb_pagemask_t;


int cvmx_core_add_wired_tlb_entry(uint64_t hi, uint64_t lo0, uint64_t lo1, cvmx_tlb_pagemask_t page_mask);


int cvmx_core_add_fixed_tlb_mapping(uint64_t vaddr, uint64_t page0_addr, uint64_t page1_addr, cvmx_tlb_pagemask_t page_mask);
int cvmx_core_add_fixed_tlb_mapping_bits(uint64_t vaddr, uint64_t page0_addr, uint64_t page1_addr, cvmx_tlb_pagemask_t page_mask);

/**
 * Return number of TLB entries.
 */
int cvmx_core_get_tlb_entries(void);
#ifdef	__cplusplus
}
#endif

#endif /* __CVMX_CORE_H__ */
