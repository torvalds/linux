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
 * Typedefs and defines for working with Octeon physical addresses.
 *
 * <hr>$Revision: 38306 $<hr>
*/
#ifndef __CVMX_ADDRESS_H__
#define __CVMX_ADDRESS_H__

#ifndef CVMX_BUILD_FOR_LINUX_KERNEL
#include "cvmx-abi.h"
#endif

#ifdef	__cplusplus
extern "C" {
#endif

typedef enum {
   CVMX_MIPS_SPACE_XKSEG = 3LL,
   CVMX_MIPS_SPACE_XKPHYS = 2LL,
   CVMX_MIPS_SPACE_XSSEG = 1LL,
   CVMX_MIPS_SPACE_XUSEG = 0LL
} cvmx_mips_space_t;

typedef enum {
   CVMX_MIPS_XKSEG_SPACE_KSEG0 = 0LL,
   CVMX_MIPS_XKSEG_SPACE_KSEG1 = 1LL,
   CVMX_MIPS_XKSEG_SPACE_SSEG = 2LL,
   CVMX_MIPS_XKSEG_SPACE_KSEG3 = 3LL
} cvmx_mips_xkseg_space_t;

  /* decodes <14:13> of a kseg3 window address */
typedef enum {
   CVMX_ADD_WIN_SCR = 0L,
   CVMX_ADD_WIN_DMA = 1L,   /* see cvmx_add_win_dma_dec_t for further decode */
   CVMX_ADD_WIN_UNUSED = 2L,
   CVMX_ADD_WIN_UNUSED2 = 3L
} cvmx_add_win_dec_t;

  /* decode within DMA space */
typedef enum {
   CVMX_ADD_WIN_DMA_ADD = 0L,     /* add store data to the write buffer entry, allocating it if necessary */
   CVMX_ADD_WIN_DMA_SENDMEM = 1L, /* send out the write buffer entry to DRAM */
   /* store data must be normal DRAM memory space address in this case */
   CVMX_ADD_WIN_DMA_SENDDMA = 2L, /* send out the write buffer entry as an IOBDMA command */
   /* see CVMX_ADD_WIN_DMA_SEND_DEC for data contents */
   CVMX_ADD_WIN_DMA_SENDIO = 3L,  /* send out the write buffer entry as an IO write */
   /* store data must be normal IO space address in this case */
   CVMX_ADD_WIN_DMA_SENDSINGLE = 4L, /* send out a single-tick command on the NCB bus */
   /* no write buffer data needed/used */
} cvmx_add_win_dma_dec_t;

/**
 *   Physical Address Decode
 *
 * Octeon-I HW never interprets this X (<39:36> reserved
 * for future expansion), software should set to 0.
 *
 *  - 0x0 XXX0 0000 0000 to      DRAM         Cached
 *  - 0x0 XXX0 0FFF FFFF
 *
 *  - 0x0 XXX0 1000 0000 to      Boot Bus     Uncached  (Converted to 0x1 00X0 1000 0000
 *  - 0x0 XXX0 1FFF FFFF         + EJTAG                           to 0x1 00X0 1FFF FFFF)
 *
 *  - 0x0 XXX0 2000 0000 to      DRAM         Cached
 *  - 0x0 XXXF FFFF FFFF
 *
 *  - 0x1 00X0 0000 0000 to      Boot Bus     Uncached
 *  - 0x1 00XF FFFF FFFF
 *
 *  - 0x1 01X0 0000 0000 to      Other NCB    Uncached
 *  - 0x1 FFXF FFFF FFFF         devices
 *
 * Decode of all Octeon addresses
 */
typedef union {

   uint64_t         u64;

   struct {
      cvmx_mips_space_t          R   : 2;
      uint64_t               offset :62;
   } sva; /* mapped or unmapped virtual address */

   struct {
      uint64_t               zeroes :33;
      uint64_t               offset :31;
   } suseg; /* mapped USEG virtual addresses (typically) */

   struct {
      uint64_t                ones  :33;
      cvmx_mips_xkseg_space_t   sp   : 2;
      uint64_t               offset :29;
   } sxkseg; /* mapped or unmapped virtual address */

   struct {
      cvmx_mips_space_t          R   : 2; /* CVMX_MIPS_SPACE_XKPHYS in this case */
      uint64_t                 cca  : 3; /* ignored by octeon */
      uint64_t                 mbz  :10;
      uint64_t                  pa  :49; /* physical address */
   } sxkphys; /* physical address accessed through xkphys unmapped virtual address */

   struct {
      uint64_t                 mbz  :15;
      uint64_t                is_io : 1; /* if set, the address is uncached and resides on MCB bus */
      uint64_t                 did  : 8; /* the hardware ignores this field when is_io==0, else device ID */
      uint64_t                unaddr: 4; /* the hardware ignores <39:36> in Octeon I */
      uint64_t               offset :36;
   } sphys; /* physical address */

   struct {
      uint64_t               zeroes :24; /* techically, <47:40> are dont-cares */
      uint64_t                unaddr: 4; /* the hardware ignores <39:36> in Octeon I */
      uint64_t               offset :36;
   } smem; /* physical mem address */

   struct {
      uint64_t                 mem_region  :2;
      uint64_t                 mbz  :13;
      uint64_t                is_io : 1; /* 1 in this case */
      uint64_t                 did  : 8; /* the hardware ignores this field when is_io==0, else device ID */
      uint64_t                unaddr: 4; /* the hardware ignores <39:36> in Octeon I */
      uint64_t               offset :36;
   } sio; /* physical IO address */

   struct {
      uint64_t                ones   : 49;
      cvmx_add_win_dec_t   csrdec : 2;    /* CVMX_ADD_WIN_SCR (0) in this case */
      uint64_t                addr   : 13;
   } sscr; /* scratchpad virtual address - accessed through a window at the end of kseg3 */

   /* there should only be stores to IOBDMA space, no loads */
   struct {
      uint64_t                ones   : 49;
      cvmx_add_win_dec_t   csrdec : 2;    /* CVMX_ADD_WIN_DMA (1) in this case */
      uint64_t                unused2: 3;
      cvmx_add_win_dma_dec_t type : 3;
      uint64_t                addr   : 7;
   } sdma; /* IOBDMA virtual address - accessed through a window at the end of kseg3 */

   struct {
      uint64_t                didspace : 24;
      uint64_t                unused   : 40;
   } sfilldidspace;

} cvmx_addr_t;

/* These macros for used by 32 bit applications */

#define CVMX_MIPS32_SPACE_KSEG0 1l
#define CVMX_ADD_SEG32(segment, add)          (((int32_t)segment << 31) | (int32_t)(add))

/* Currently all IOs are performed using XKPHYS addressing. Linux uses the
    CvmMemCtl register to enable XKPHYS addressing to IO space from user mode.
    Future OSes may need to change the upper bits of IO addresses. The
    following define controls the upper two bits for all IO addresses generated
    by the simple executive library */
#define CVMX_IO_SEG CVMX_MIPS_SPACE_XKPHYS

/* These macros simplify the process of creating common IO addresses */
#define CVMX_ADD_SEG(segment, add)          ((((uint64_t)segment) << 62) | (add))
#ifndef CVMX_ADD_IO_SEG
#define CVMX_ADD_IO_SEG(add)                CVMX_ADD_SEG(CVMX_IO_SEG, (add))
#endif
#define CVMX_ADDR_DIDSPACE(did)             (((CVMX_IO_SEG) << 22) | ((1ULL) << 8) | (did))
#define CVMX_ADDR_DID(did)                  (CVMX_ADDR_DIDSPACE(did) << 40)
#define CVMX_FULL_DID(did,subdid)           (((did) << 3) | (subdid))


   /* from include/ncb_rsl_id.v */
#define CVMX_OCT_DID_MIS 0ULL   /* misc stuff */
#define CVMX_OCT_DID_GMX0 1ULL
#define CVMX_OCT_DID_GMX1 2ULL
#define CVMX_OCT_DID_PCI 3ULL
#define CVMX_OCT_DID_KEY 4ULL
#define CVMX_OCT_DID_FPA 5ULL
#define CVMX_OCT_DID_DFA 6ULL
#define CVMX_OCT_DID_ZIP 7ULL
#define CVMX_OCT_DID_RNG 8ULL
#define CVMX_OCT_DID_IPD 9ULL
#define CVMX_OCT_DID_PKT 10ULL
#define CVMX_OCT_DID_TIM 11ULL
#define CVMX_OCT_DID_TAG 12ULL
   /* the rest are not on the IO bus */
#define CVMX_OCT_DID_L2C 16ULL
#define CVMX_OCT_DID_LMC 17ULL
#define CVMX_OCT_DID_SPX0 18ULL
#define CVMX_OCT_DID_SPX1 19ULL
#define CVMX_OCT_DID_PIP 20ULL
#define CVMX_OCT_DID_ASX0 22ULL
#define CVMX_OCT_DID_ASX1 23ULL
#define CVMX_OCT_DID_IOB 30ULL

#define CVMX_OCT_DID_PKT_SEND       CVMX_FULL_DID(CVMX_OCT_DID_PKT,2ULL)
#define CVMX_OCT_DID_TAG_SWTAG      CVMX_FULL_DID(CVMX_OCT_DID_TAG,0ULL)
#define CVMX_OCT_DID_TAG_TAG1       CVMX_FULL_DID(CVMX_OCT_DID_TAG,1ULL)
#define CVMX_OCT_DID_TAG_TAG2       CVMX_FULL_DID(CVMX_OCT_DID_TAG,2ULL)
#define CVMX_OCT_DID_TAG_TAG3       CVMX_FULL_DID(CVMX_OCT_DID_TAG,3ULL)
#define CVMX_OCT_DID_TAG_NULL_RD    CVMX_FULL_DID(CVMX_OCT_DID_TAG,4ULL)
#define CVMX_OCT_DID_TAG_TAG5       CVMX_FULL_DID(CVMX_OCT_DID_TAG,5ULL)
#define CVMX_OCT_DID_TAG_CSR        CVMX_FULL_DID(CVMX_OCT_DID_TAG,7ULL)
#define CVMX_OCT_DID_FAU_FAI        CVMX_FULL_DID(CVMX_OCT_DID_IOB,0ULL)
#define CVMX_OCT_DID_TIM_CSR        CVMX_FULL_DID(CVMX_OCT_DID_TIM,0ULL)
#define CVMX_OCT_DID_KEY_RW         CVMX_FULL_DID(CVMX_OCT_DID_KEY,0ULL)
#define CVMX_OCT_DID_PCI_6          CVMX_FULL_DID(CVMX_OCT_DID_PCI,6ULL)
#define CVMX_OCT_DID_MIS_BOO        CVMX_FULL_DID(CVMX_OCT_DID_MIS,0ULL)
#define CVMX_OCT_DID_PCI_RML        CVMX_FULL_DID(CVMX_OCT_DID_PCI,0ULL)
#define CVMX_OCT_DID_IPD_CSR        CVMX_FULL_DID(CVMX_OCT_DID_IPD,7ULL)
#define CVMX_OCT_DID_DFA_CSR        CVMX_FULL_DID(CVMX_OCT_DID_DFA,7ULL)
#define CVMX_OCT_DID_MIS_CSR        CVMX_FULL_DID(CVMX_OCT_DID_MIS,7ULL)
#define CVMX_OCT_DID_ZIP_CSR        CVMX_FULL_DID(CVMX_OCT_DID_ZIP,0ULL)

#ifndef CVMX_BUILD_FOR_LINUX_KERNEL
#ifdef CVMX_ABI_N32
#define UNMAPPED_PTR(x) ( (1U << 31) | x )
#else
#define UNMAPPED_PTR(x) ( (1ULL << 63) | x )
#endif
#endif

#ifdef	__cplusplus
}
#endif

#endif /* __CVMX_ADDRESS_H__ */

