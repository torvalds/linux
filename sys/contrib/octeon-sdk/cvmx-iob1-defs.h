/***********************license start***************
 * Copyright (c) 2003-2012  Cavium Inc. (support@cavium.com). All rights
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
 * cvmx-iob1-defs.h
 *
 * Configuration and status register (CSR) type definitions for
 * Octeon iob1.
 *
 * This file is auto generated. Do not edit.
 *
 * <hr>$Revision$<hr>
 *
 */
#ifndef __CVMX_IOB1_DEFS_H__
#define __CVMX_IOB1_DEFS_H__

#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_IOB1_BIST_STATUS CVMX_IOB1_BIST_STATUS_FUNC()
static inline uint64_t CVMX_IOB1_BIST_STATUS_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_IOB1_BIST_STATUS not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800F00107F8ull);
}
#else
#define CVMX_IOB1_BIST_STATUS (CVMX_ADD_IO_SEG(0x00011800F00107F8ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_IOB1_CTL_STATUS CVMX_IOB1_CTL_STATUS_FUNC()
static inline uint64_t CVMX_IOB1_CTL_STATUS_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_IOB1_CTL_STATUS not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800F0010050ull);
}
#else
#define CVMX_IOB1_CTL_STATUS (CVMX_ADD_IO_SEG(0x00011800F0010050ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_IOB1_TO_CMB_CREDITS CVMX_IOB1_TO_CMB_CREDITS_FUNC()
static inline uint64_t CVMX_IOB1_TO_CMB_CREDITS_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_IOB1_TO_CMB_CREDITS not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800F00100B0ull);
}
#else
#define CVMX_IOB1_TO_CMB_CREDITS (CVMX_ADD_IO_SEG(0x00011800F00100B0ull))
#endif

/**
 * cvmx_iob1_bist_status
 *
 * IOB_BIST_STATUS = BIST Status of IOB Memories
 *
 * The result of the BIST run on the IOB memories.
 */
union cvmx_iob1_bist_status {
	uint64_t u64;
	struct cvmx_iob1_bist_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_9_63                : 55;
	uint64_t xmdfif                       : 1;  /**< xmdfif_bist_status */
	uint64_t xmcfif                       : 1;  /**< xmcfif_bist_status */
	uint64_t iorfif                       : 1;  /**< iorfif_bist_status */
	uint64_t rsdfif                       : 1;  /**< rsdfif_bist_status */
	uint64_t iocfif                       : 1;  /**< iocfif_bist_status */
	uint64_t reserved_2_3                 : 2;
	uint64_t icrp0                        : 1;  /**< icr_pko_bist_mem0_status */
	uint64_t icrp1                        : 1;  /**< icr_pko_bist_mem1_status */
#else
	uint64_t icrp1                        : 1;
	uint64_t icrp0                        : 1;
	uint64_t reserved_2_3                 : 2;
	uint64_t iocfif                       : 1;
	uint64_t rsdfif                       : 1;
	uint64_t iorfif                       : 1;
	uint64_t xmcfif                       : 1;
	uint64_t xmdfif                       : 1;
	uint64_t reserved_9_63                : 55;
#endif
	} s;
	struct cvmx_iob1_bist_status_s        cn68xx;
	struct cvmx_iob1_bist_status_s        cn68xxp1;
};
typedef union cvmx_iob1_bist_status cvmx_iob1_bist_status_t;

/**
 * cvmx_iob1_ctl_status
 *
 * IOB Control Status = IOB Control and Status Register
 *
 * Provides control for IOB functions.
 */
union cvmx_iob1_ctl_status {
	uint64_t u64;
	struct cvmx_iob1_ctl_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_11_63               : 53;
	uint64_t fif_dly                      : 1;  /**< Delay async FIFO counts to be used when clock ratio
                                                         is greater then 3:1. Writes should be followed by an
                                                         immediate read. */
	uint64_t xmc_per                      : 4;  /**< IBC XMC PUSH EARLY */
	uint64_t reserved_0_5                 : 6;
#else
	uint64_t reserved_0_5                 : 6;
	uint64_t xmc_per                      : 4;
	uint64_t fif_dly                      : 1;
	uint64_t reserved_11_63               : 53;
#endif
	} s;
	struct cvmx_iob1_ctl_status_s         cn68xx;
	struct cvmx_iob1_ctl_status_s         cn68xxp1;
};
typedef union cvmx_iob1_ctl_status cvmx_iob1_ctl_status_t;

/**
 * cvmx_iob1_to_cmb_credits
 *
 * IOB_TO_CMB_CREDITS = IOB To CMB Credits
 *
 * Controls the number of reads and writes that may be outstanding to the L2C (via the CMB).
 */
union cvmx_iob1_to_cmb_credits {
	uint64_t u64;
	struct cvmx_iob1_to_cmb_credits_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_10_63               : 54;
	uint64_t pko_rd                       : 4;  /**< Number of PKO reads that can be out to L2C where
                                                         0 == 16-credits. */
	uint64_t reserved_3_5                 : 3;
	uint64_t ncb_wr                       : 3;  /**< Number of NCB/PKI writes that can be out to L2C
                                                         where 0 == 8-credits. */
#else
	uint64_t ncb_wr                       : 3;
	uint64_t reserved_3_5                 : 3;
	uint64_t pko_rd                       : 4;
	uint64_t reserved_10_63               : 54;
#endif
	} s;
	struct cvmx_iob1_to_cmb_credits_s     cn68xx;
	struct cvmx_iob1_to_cmb_credits_s     cn68xxp1;
};
typedef union cvmx_iob1_to_cmb_credits cvmx_iob1_to_cmb_credits_t;

#endif
