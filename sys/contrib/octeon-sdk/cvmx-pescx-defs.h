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
 * cvmx-pescx-defs.h
 *
 * Configuration and status register (CSR) type definitions for
 * Octeon pescx.
 *
 * This file is auto generated. Do not edit.
 *
 * <hr>$Revision$<hr>
 *
 */
#ifndef __CVMX_PESCX_DEFS_H__
#define __CVMX_PESCX_DEFS_H__

#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PESCX_BIST_STATUS(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PESCX_BIST_STATUS(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000018ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_PESCX_BIST_STATUS(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000018ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PESCX_BIST_STATUS2(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PESCX_BIST_STATUS2(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000418ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_PESCX_BIST_STATUS2(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000418ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PESCX_CFG_RD(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PESCX_CFG_RD(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000030ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_PESCX_CFG_RD(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000030ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PESCX_CFG_WR(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PESCX_CFG_WR(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000028ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_PESCX_CFG_WR(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000028ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PESCX_CPL_LUT_VALID(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PESCX_CPL_LUT_VALID(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000098ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_PESCX_CPL_LUT_VALID(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000098ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PESCX_CTL_STATUS(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PESCX_CTL_STATUS(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000000ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_PESCX_CTL_STATUS(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000000ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PESCX_CTL_STATUS2(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PESCX_CTL_STATUS2(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000400ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_PESCX_CTL_STATUS2(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000400ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PESCX_DBG_INFO(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PESCX_DBG_INFO(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000008ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_PESCX_DBG_INFO(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000008ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PESCX_DBG_INFO_EN(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PESCX_DBG_INFO_EN(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800C80000A0ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_PESCX_DBG_INFO_EN(block_id) (CVMX_ADD_IO_SEG(0x00011800C80000A0ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PESCX_DIAG_STATUS(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PESCX_DIAG_STATUS(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000020ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_PESCX_DIAG_STATUS(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000020ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PESCX_P2N_BAR0_START(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PESCX_P2N_BAR0_START(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000080ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_PESCX_P2N_BAR0_START(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000080ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PESCX_P2N_BAR1_START(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PESCX_P2N_BAR1_START(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000088ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_PESCX_P2N_BAR1_START(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000088ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PESCX_P2N_BAR2_START(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PESCX_P2N_BAR2_START(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000090ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_PESCX_P2N_BAR2_START(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000090ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PESCX_P2P_BARX_END(unsigned long offset, unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && (((offset <= 3)) && ((block_id <= 1)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && (((offset <= 3)) && ((block_id <= 1))))))
		cvmx_warn("CVMX_PESCX_P2P_BARX_END(%lu,%lu) is invalid on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000048ull) + (((offset) & 3) + ((block_id) & 1) * 0x800000ull) * 16;
}
#else
#define CVMX_PESCX_P2P_BARX_END(offset, block_id) (CVMX_ADD_IO_SEG(0x00011800C8000048ull) + (((offset) & 3) + ((block_id) & 1) * 0x800000ull) * 16)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PESCX_P2P_BARX_START(unsigned long offset, unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && (((offset <= 3)) && ((block_id <= 1)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && (((offset <= 3)) && ((block_id <= 1))))))
		cvmx_warn("CVMX_PESCX_P2P_BARX_START(%lu,%lu) is invalid on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000040ull) + (((offset) & 3) + ((block_id) & 1) * 0x800000ull) * 16;
}
#else
#define CVMX_PESCX_P2P_BARX_START(offset, block_id) (CVMX_ADD_IO_SEG(0x00011800C8000040ull) + (((offset) & 3) + ((block_id) & 1) * 0x800000ull) * 16)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PESCX_TLP_CREDITS(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_PESCX_TLP_CREDITS(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800C8000038ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_PESCX_TLP_CREDITS(block_id) (CVMX_ADD_IO_SEG(0x00011800C8000038ull) + ((block_id) & 1) * 0x8000000ull)
#endif

/**
 * cvmx_pesc#_bist_status
 *
 * PESC_BIST_STATUS = PESC Bist Status
 *
 * Contains the diffrent interrupt summary bits of the PESC.
 */
union cvmx_pescx_bist_status {
	uint64_t u64;
	struct cvmx_pescx_bist_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_13_63               : 51;
	uint64_t rqdata5                      : 1;  /**< Rx Queue Data Memory5. */
	uint64_t ctlp_or                      : 1;  /**< C-TLP Order Fifo. */
	uint64_t ntlp_or                      : 1;  /**< N-TLP Order Fifo. */
	uint64_t ptlp_or                      : 1;  /**< P-TLP Order Fifo. */
	uint64_t retry                        : 1;  /**< Retry Buffer. */
	uint64_t rqdata0                      : 1;  /**< Rx Queue Data Memory0. */
	uint64_t rqdata1                      : 1;  /**< Rx Queue Data Memory1. */
	uint64_t rqdata2                      : 1;  /**< Rx Queue Data Memory2. */
	uint64_t rqdata3                      : 1;  /**< Rx Queue Data Memory3. */
	uint64_t rqdata4                      : 1;  /**< Rx Queue Data Memory4. */
	uint64_t rqhdr1                       : 1;  /**< Rx Queue Header1. */
	uint64_t rqhdr0                       : 1;  /**< Rx Queue Header0. */
	uint64_t sot                          : 1;  /**< SOT Buffer. */
#else
	uint64_t sot                          : 1;
	uint64_t rqhdr0                       : 1;
	uint64_t rqhdr1                       : 1;
	uint64_t rqdata4                      : 1;
	uint64_t rqdata3                      : 1;
	uint64_t rqdata2                      : 1;
	uint64_t rqdata1                      : 1;
	uint64_t rqdata0                      : 1;
	uint64_t retry                        : 1;
	uint64_t ptlp_or                      : 1;
	uint64_t ntlp_or                      : 1;
	uint64_t ctlp_or                      : 1;
	uint64_t rqdata5                      : 1;
	uint64_t reserved_13_63               : 51;
#endif
	} s;
	struct cvmx_pescx_bist_status_s       cn52xx;
	struct cvmx_pescx_bist_status_cn52xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_12_63               : 52;
	uint64_t ctlp_or                      : 1;  /**< C-TLP Order Fifo. */
	uint64_t ntlp_or                      : 1;  /**< N-TLP Order Fifo. */
	uint64_t ptlp_or                      : 1;  /**< P-TLP Order Fifo. */
	uint64_t retry                        : 1;  /**< Retry Buffer. */
	uint64_t rqdata0                      : 1;  /**< Rx Queue Data Memory0. */
	uint64_t rqdata1                      : 1;  /**< Rx Queue Data Memory1. */
	uint64_t rqdata2                      : 1;  /**< Rx Queue Data Memory2. */
	uint64_t rqdata3                      : 1;  /**< Rx Queue Data Memory3. */
	uint64_t rqdata4                      : 1;  /**< Rx Queue Data Memory4. */
	uint64_t rqhdr1                       : 1;  /**< Rx Queue Header1. */
	uint64_t rqhdr0                       : 1;  /**< Rx Queue Header0. */
	uint64_t sot                          : 1;  /**< SOT Buffer. */
#else
	uint64_t sot                          : 1;
	uint64_t rqhdr0                       : 1;
	uint64_t rqhdr1                       : 1;
	uint64_t rqdata4                      : 1;
	uint64_t rqdata3                      : 1;
	uint64_t rqdata2                      : 1;
	uint64_t rqdata1                      : 1;
	uint64_t rqdata0                      : 1;
	uint64_t retry                        : 1;
	uint64_t ptlp_or                      : 1;
	uint64_t ntlp_or                      : 1;
	uint64_t ctlp_or                      : 1;
	uint64_t reserved_12_63               : 52;
#endif
	} cn52xxp1;
	struct cvmx_pescx_bist_status_s       cn56xx;
	struct cvmx_pescx_bist_status_cn52xxp1 cn56xxp1;
};
typedef union cvmx_pescx_bist_status cvmx_pescx_bist_status_t;

/**
 * cvmx_pesc#_bist_status2
 *
 * PESC(0..1)_BIST_STATUS2 = PESC BIST Status Register
 *
 * Results from BIST runs of PESC's memories.
 */
union cvmx_pescx_bist_status2 {
	uint64_t u64;
	struct cvmx_pescx_bist_status2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_14_63               : 50;
	uint64_t cto_p2e                      : 1;  /**< BIST Status for the cto_p2e_fifo */
	uint64_t e2p_cpl                      : 1;  /**< BIST Status for the e2p_cpl_fifo */
	uint64_t e2p_n                        : 1;  /**< BIST Status for the e2p_n_fifo */
	uint64_t e2p_p                        : 1;  /**< BIST Status for the e2p_p_fifo */
	uint64_t e2p_rsl                      : 1;  /**< BIST Status for the e2p_rsl__fifo */
	uint64_t dbg_p2e                      : 1;  /**< BIST Status for the dbg_p2e_fifo */
	uint64_t peai_p2e                     : 1;  /**< BIST Status for the peai__pesc_fifo */
	uint64_t rsl_p2e                      : 1;  /**< BIST Status for the rsl_p2e_fifo */
	uint64_t pef_tpf1                     : 1;  /**< BIST Status for the pef_tlp_p_fifo1 */
	uint64_t pef_tpf0                     : 1;  /**< BIST Status for the pef_tlp_p_fifo0 */
	uint64_t pef_tnf                      : 1;  /**< BIST Status for the pef_tlp_n_fifo */
	uint64_t pef_tcf1                     : 1;  /**< BIST Status for the pef_tlp_cpl_fifo1 */
	uint64_t pef_tc0                      : 1;  /**< BIST Status for the pef_tlp_cpl_fifo0 */
	uint64_t ppf                          : 1;  /**< BIST Status for the ppf_fifo */
#else
	uint64_t ppf                          : 1;
	uint64_t pef_tc0                      : 1;
	uint64_t pef_tcf1                     : 1;
	uint64_t pef_tnf                      : 1;
	uint64_t pef_tpf0                     : 1;
	uint64_t pef_tpf1                     : 1;
	uint64_t rsl_p2e                      : 1;
	uint64_t peai_p2e                     : 1;
	uint64_t dbg_p2e                      : 1;
	uint64_t e2p_rsl                      : 1;
	uint64_t e2p_p                        : 1;
	uint64_t e2p_n                        : 1;
	uint64_t e2p_cpl                      : 1;
	uint64_t cto_p2e                      : 1;
	uint64_t reserved_14_63               : 50;
#endif
	} s;
	struct cvmx_pescx_bist_status2_s      cn52xx;
	struct cvmx_pescx_bist_status2_s      cn52xxp1;
	struct cvmx_pescx_bist_status2_s      cn56xx;
	struct cvmx_pescx_bist_status2_s      cn56xxp1;
};
typedef union cvmx_pescx_bist_status2 cvmx_pescx_bist_status2_t;

/**
 * cvmx_pesc#_cfg_rd
 *
 * PESC_CFG_RD = PESC Configuration Read
 *
 * Allows read access to the configuration in the PCIe Core.
 */
union cvmx_pescx_cfg_rd {
	uint64_t u64;
	struct cvmx_pescx_cfg_rd_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t data                         : 32; /**< Data. */
	uint64_t addr                         : 32; /**< Address to read. A write to this register
                                                         starts a read operation. */
#else
	uint64_t addr                         : 32;
	uint64_t data                         : 32;
#endif
	} s;
	struct cvmx_pescx_cfg_rd_s            cn52xx;
	struct cvmx_pescx_cfg_rd_s            cn52xxp1;
	struct cvmx_pescx_cfg_rd_s            cn56xx;
	struct cvmx_pescx_cfg_rd_s            cn56xxp1;
};
typedef union cvmx_pescx_cfg_rd cvmx_pescx_cfg_rd_t;

/**
 * cvmx_pesc#_cfg_wr
 *
 * PESC_CFG_WR = PESC Configuration Write
 *
 * Allows write access to the configuration in the PCIe Core.
 */
union cvmx_pescx_cfg_wr {
	uint64_t u64;
	struct cvmx_pescx_cfg_wr_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t data                         : 32; /**< Data to write. A write to this register starts
                                                         a write operation. */
	uint64_t addr                         : 32; /**< Address to write. A write to this register starts
                                                         a write operation. */
#else
	uint64_t addr                         : 32;
	uint64_t data                         : 32;
#endif
	} s;
	struct cvmx_pescx_cfg_wr_s            cn52xx;
	struct cvmx_pescx_cfg_wr_s            cn52xxp1;
	struct cvmx_pescx_cfg_wr_s            cn56xx;
	struct cvmx_pescx_cfg_wr_s            cn56xxp1;
};
typedef union cvmx_pescx_cfg_wr cvmx_pescx_cfg_wr_t;

/**
 * cvmx_pesc#_cpl_lut_valid
 *
 * PESC_CPL_LUT_VALID = PESC Cmpletion Lookup Table Valid
 *
 * Bit set for outstanding tag read.
 */
union cvmx_pescx_cpl_lut_valid {
	uint64_t u64;
	struct cvmx_pescx_cpl_lut_valid_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t tag                          : 32; /**< Bit vector set cooresponds to an outstanding tag
                                                         expecting a completion. */
#else
	uint64_t tag                          : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_pescx_cpl_lut_valid_s     cn52xx;
	struct cvmx_pescx_cpl_lut_valid_s     cn52xxp1;
	struct cvmx_pescx_cpl_lut_valid_s     cn56xx;
	struct cvmx_pescx_cpl_lut_valid_s     cn56xxp1;
};
typedef union cvmx_pescx_cpl_lut_valid cvmx_pescx_cpl_lut_valid_t;

/**
 * cvmx_pesc#_ctl_status
 *
 * PESC_CTL_STATUS = PESC Control Status
 *
 * General control and status of the PESC.
 */
union cvmx_pescx_ctl_status {
	uint64_t u64;
	struct cvmx_pescx_ctl_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_28_63               : 36;
	uint64_t dnum                         : 5;  /**< Primary bus device number. */
	uint64_t pbus                         : 8;  /**< Primary bus number. */
	uint64_t qlm_cfg                      : 2;  /**< The QLM configuration pad bits. */
	uint64_t lane_swp                     : 1;  /**< Lane Swap. For PEDC1, when 0 NO LANE SWAP when '1'
                                                         enables LANE SWAP. THis bit has no effect on PEDC0.
                                                         This bit should be set before enabling PEDC1. */
	uint64_t pm_xtoff                     : 1;  /**< When WRITTEN with a '1' a single cycle pulse is
                                                         to the PCIe core pm_xmt_turnoff port. RC mode. */
	uint64_t pm_xpme                      : 1;  /**< When WRITTEN with a '1' a single cycle pulse is
                                                         to the PCIe core pm_xmt_pme port. EP mode. */
	uint64_t ob_p_cmd                     : 1;  /**< When WRITTEN with a '1' a single cycle pulse is
                                                         to the PCIe core outband_pwrup_cmd port. EP mode. */
	uint64_t reserved_7_8                 : 2;
	uint64_t nf_ecrc                      : 1;  /**< Do not forward peer-to-peer ECRC TLPs. */
	uint64_t dly_one                      : 1;  /**< When set the output client state machines will
                                                         wait one cycle before starting a new TLP out. */
	uint64_t lnk_enb                      : 1;  /**< When set '1' the link is enabled when '0' the
                                                         link is disabled. This bit only is active when in
                                                         RC mode. */
	uint64_t ro_ctlp                      : 1;  /**< When set '1' C-TLPs that have the RO bit set will
                                                         not wait for P-TLPs that normaly would be sent
                                                         first. */
	uint64_t reserved_2_2                 : 1;
	uint64_t inv_ecrc                     : 1;  /**< When '1' causes the LSB of the ECRC to be inverted. */
	uint64_t inv_lcrc                     : 1;  /**< When '1' causes the LSB of the LCRC to be inverted. */
#else
	uint64_t inv_lcrc                     : 1;
	uint64_t inv_ecrc                     : 1;
	uint64_t reserved_2_2                 : 1;
	uint64_t ro_ctlp                      : 1;
	uint64_t lnk_enb                      : 1;
	uint64_t dly_one                      : 1;
	uint64_t nf_ecrc                      : 1;
	uint64_t reserved_7_8                 : 2;
	uint64_t ob_p_cmd                     : 1;
	uint64_t pm_xpme                      : 1;
	uint64_t pm_xtoff                     : 1;
	uint64_t lane_swp                     : 1;
	uint64_t qlm_cfg                      : 2;
	uint64_t pbus                         : 8;
	uint64_t dnum                         : 5;
	uint64_t reserved_28_63               : 36;
#endif
	} s;
	struct cvmx_pescx_ctl_status_s        cn52xx;
	struct cvmx_pescx_ctl_status_s        cn52xxp1;
	struct cvmx_pescx_ctl_status_cn56xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_28_63               : 36;
	uint64_t dnum                         : 5;  /**< Primary bus device number. */
	uint64_t pbus                         : 8;  /**< Primary bus number. */
	uint64_t qlm_cfg                      : 2;  /**< The QLM configuration pad bits. */
	uint64_t reserved_12_12               : 1;
	uint64_t pm_xtoff                     : 1;  /**< When WRITTEN with a '1' a single cycle pulse is
                                                         to the PCIe core pm_xmt_turnoff port. RC mode. */
	uint64_t pm_xpme                      : 1;  /**< When WRITTEN with a '1' a single cycle pulse is
                                                         to the PCIe core pm_xmt_pme port. EP mode. */
	uint64_t ob_p_cmd                     : 1;  /**< When WRITTEN with a '1' a single cycle pulse is
                                                         to the PCIe core outband_pwrup_cmd port. EP mode. */
	uint64_t reserved_7_8                 : 2;
	uint64_t nf_ecrc                      : 1;  /**< Do not forward peer-to-peer ECRC TLPs. */
	uint64_t dly_one                      : 1;  /**< When set the output client state machines will
                                                         wait one cycle before starting a new TLP out. */
	uint64_t lnk_enb                      : 1;  /**< When set '1' the link is enabled when '0' the
                                                         link is disabled. This bit only is active when in
                                                         RC mode. */
	uint64_t ro_ctlp                      : 1;  /**< When set '1' C-TLPs that have the RO bit set will
                                                         not wait for P-TLPs that normaly would be sent
                                                         first. */
	uint64_t reserved_2_2                 : 1;
	uint64_t inv_ecrc                     : 1;  /**< When '1' causes the LSB of the ECRC to be inverted. */
	uint64_t inv_lcrc                     : 1;  /**< When '1' causes the LSB of the LCRC to be inverted. */
#else
	uint64_t inv_lcrc                     : 1;
	uint64_t inv_ecrc                     : 1;
	uint64_t reserved_2_2                 : 1;
	uint64_t ro_ctlp                      : 1;
	uint64_t lnk_enb                      : 1;
	uint64_t dly_one                      : 1;
	uint64_t nf_ecrc                      : 1;
	uint64_t reserved_7_8                 : 2;
	uint64_t ob_p_cmd                     : 1;
	uint64_t pm_xpme                      : 1;
	uint64_t pm_xtoff                     : 1;
	uint64_t reserved_12_12               : 1;
	uint64_t qlm_cfg                      : 2;
	uint64_t pbus                         : 8;
	uint64_t dnum                         : 5;
	uint64_t reserved_28_63               : 36;
#endif
	} cn56xx;
	struct cvmx_pescx_ctl_status_cn56xx   cn56xxp1;
};
typedef union cvmx_pescx_ctl_status cvmx_pescx_ctl_status_t;

/**
 * cvmx_pesc#_ctl_status2
 *
 * Below are in PESC
 *
 *                  PESC(0..1)_BIST_STATUS2 = PESC BIST Status Register
 *
 * Results from BIST runs of PESC's memories.
 */
union cvmx_pescx_ctl_status2 {
	uint64_t u64;
	struct cvmx_pescx_ctl_status2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_2_63                : 62;
	uint64_t pclk_run                     : 1;  /**< When the pce_clk is running this bit will be '1'.
                                                         Writing a '1' to this location will cause the
                                                         bit to be cleared, but if the pce_clk is running
                                                         this bit will be re-set. */
	uint64_t pcierst                      : 1;  /**< Set to '1' when PCIe is in reset. */
#else
	uint64_t pcierst                      : 1;
	uint64_t pclk_run                     : 1;
	uint64_t reserved_2_63                : 62;
#endif
	} s;
	struct cvmx_pescx_ctl_status2_s       cn52xx;
	struct cvmx_pescx_ctl_status2_cn52xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_1_63                : 63;
	uint64_t pcierst                      : 1;  /**< Set to '1' when PCIe is in reset. */
#else
	uint64_t pcierst                      : 1;
	uint64_t reserved_1_63                : 63;
#endif
	} cn52xxp1;
	struct cvmx_pescx_ctl_status2_s       cn56xx;
	struct cvmx_pescx_ctl_status2_cn52xxp1 cn56xxp1;
};
typedef union cvmx_pescx_ctl_status2 cvmx_pescx_ctl_status2_t;

/**
 * cvmx_pesc#_dbg_info
 *
 * PESC(0..1)_DBG_INFO = PESC Debug Information
 *
 * General debug info.
 */
union cvmx_pescx_dbg_info {
	uint64_t u64;
	struct cvmx_pescx_dbg_info_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_31_63               : 33;
	uint64_t ecrc_e                       : 1;  /**< Received a ECRC error.
                                                         radm_ecrc_err */
	uint64_t rawwpp                       : 1;  /**< Received a write with poisoned payload
                                                         radm_rcvd_wreq_poisoned */
	uint64_t racpp                        : 1;  /**< Received a completion with poisoned payload
                                                         radm_rcvd_cpl_poisoned */
	uint64_t ramtlp                       : 1;  /**< Received a malformed TLP
                                                         radm_mlf_tlp_err */
	uint64_t rarwdns                      : 1;  /**< Recieved a request which device does not support
                                                         radm_rcvd_ur_req */
	uint64_t caar                         : 1;  /**< Completer aborted a request
                                                         radm_rcvd_ca_req
                                                         This bit will never be set because Octeon does
                                                         not generate Completer Aborts. */
	uint64_t racca                        : 1;  /**< Received a completion with CA status
                                                         radm_rcvd_cpl_ca */
	uint64_t racur                        : 1;  /**< Received a completion with UR status
                                                         radm_rcvd_cpl_ur */
	uint64_t rauc                         : 1;  /**< Received an unexpected completion
                                                         radm_unexp_cpl_err */
	uint64_t rqo                          : 1;  /**< Receive queue overflow. Normally happens only when
                                                         flow control advertisements are ignored
                                                         radm_qoverflow */
	uint64_t fcuv                         : 1;  /**< Flow Control Update Violation (opt. checks)
                                                         int_xadm_fc_prot_err */
	uint64_t rpe                          : 1;  /**< When the PHY reports 8B/10B decode error
                                                         (RxStatus = 3b100) or disparity error
                                                         (RxStatus = 3b111), the signal rmlh_rcvd_err will
                                                         be asserted.
                                                         rmlh_rcvd_err */
	uint64_t fcpvwt                       : 1;  /**< Flow Control Protocol Violation (Watchdog Timer)
                                                         rtlh_fc_prot_err */
	uint64_t dpeoosd                      : 1;  /**< DLLP protocol error (out of sequence DLLP)
                                                         rdlh_prot_err */
	uint64_t rtwdle                       : 1;  /**< Received TLP with DataLink Layer Error
                                                         rdlh_bad_tlp_err */
	uint64_t rdwdle                       : 1;  /**< Received DLLP with DataLink Layer Error
                                                         rdlh_bad_dllp_err */
	uint64_t mre                          : 1;  /**< Max Retries Exceeded
                                                         xdlh_replay_num_rlover_err */
	uint64_t rte                          : 1;  /**< Replay Timer Expired
                                                         xdlh_replay_timeout_err
                                                         This bit is set when the REPLAY_TIMER expires in
                                                         the PCIE core. The probability of this bit being
                                                         set will increase with the traffic load. */
	uint64_t acto                         : 1;  /**< A Completion Timeout Occured
                                                         pedc_radm_cpl_timeout */
	uint64_t rvdm                         : 1;  /**< Received Vendor-Defined Message
                                                         pedc_radm_vendor_msg */
	uint64_t rumep                        : 1;  /**< Received Unlock Message (EP Mode Only)
                                                         pedc_radm_msg_unlock */
	uint64_t rptamrc                      : 1;  /**< Received PME Turnoff Acknowledge Message
                                                         (RC Mode only)
                                                         pedc_radm_pm_to_ack */
	uint64_t rpmerc                       : 1;  /**< Received PME Message (RC Mode only)
                                                         pedc_radm_pm_pme */
	uint64_t rfemrc                       : 1;  /**< Received Fatal Error Message (RC Mode only)
                                                         pedc_radm_fatal_err
                                                         Bit set when a message with ERR_FATAL is set. */
	uint64_t rnfemrc                      : 1;  /**< Received Non-Fatal Error Message (RC Mode only)
                                                         pedc_radm_nonfatal_err */
	uint64_t rcemrc                       : 1;  /**< Received Correctable Error Message (RC Mode only)
                                                         pedc_radm_correctable_err */
	uint64_t rpoison                      : 1;  /**< Received Poisoned TLP
                                                         pedc__radm_trgt1_poisoned & pedc__radm_trgt1_hv */
	uint64_t recrce                       : 1;  /**< Received ECRC Error
                                                         pedc_radm_trgt1_ecrc_err & pedc__radm_trgt1_eot */
	uint64_t rtlplle                      : 1;  /**< Received TLP has link layer error
                                                         pedc_radm_trgt1_dllp_abort & pedc__radm_trgt1_eot */
	uint64_t rtlpmal                      : 1;  /**< Received TLP is malformed or a message.
                                                         pedc_radm_trgt1_tlp_abort & pedc__radm_trgt1_eot
                                                         If the core receives a MSG (or Vendor Message)
                                                         this bit will be set. */
	uint64_t spoison                      : 1;  /**< Poisoned TLP sent
                                                         peai__client0_tlp_ep & peai__client0_tlp_hv */
#else
	uint64_t spoison                      : 1;
	uint64_t rtlpmal                      : 1;
	uint64_t rtlplle                      : 1;
	uint64_t recrce                       : 1;
	uint64_t rpoison                      : 1;
	uint64_t rcemrc                       : 1;
	uint64_t rnfemrc                      : 1;
	uint64_t rfemrc                       : 1;
	uint64_t rpmerc                       : 1;
	uint64_t rptamrc                      : 1;
	uint64_t rumep                        : 1;
	uint64_t rvdm                         : 1;
	uint64_t acto                         : 1;
	uint64_t rte                          : 1;
	uint64_t mre                          : 1;
	uint64_t rdwdle                       : 1;
	uint64_t rtwdle                       : 1;
	uint64_t dpeoosd                      : 1;
	uint64_t fcpvwt                       : 1;
	uint64_t rpe                          : 1;
	uint64_t fcuv                         : 1;
	uint64_t rqo                          : 1;
	uint64_t rauc                         : 1;
	uint64_t racur                        : 1;
	uint64_t racca                        : 1;
	uint64_t caar                         : 1;
	uint64_t rarwdns                      : 1;
	uint64_t ramtlp                       : 1;
	uint64_t racpp                        : 1;
	uint64_t rawwpp                       : 1;
	uint64_t ecrc_e                       : 1;
	uint64_t reserved_31_63               : 33;
#endif
	} s;
	struct cvmx_pescx_dbg_info_s          cn52xx;
	struct cvmx_pescx_dbg_info_s          cn52xxp1;
	struct cvmx_pescx_dbg_info_s          cn56xx;
	struct cvmx_pescx_dbg_info_s          cn56xxp1;
};
typedef union cvmx_pescx_dbg_info cvmx_pescx_dbg_info_t;

/**
 * cvmx_pesc#_dbg_info_en
 *
 * PESC(0..1)_DBG_INFO_EN = PESC Debug Information Enable
 *
 * Allows PESC_DBG_INFO to generate interrupts when cooresponding enable bit is set.
 */
union cvmx_pescx_dbg_info_en {
	uint64_t u64;
	struct cvmx_pescx_dbg_info_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_31_63               : 33;
	uint64_t ecrc_e                       : 1;  /**< Allows PESC_DBG_INFO[30] to generate an interrupt. */
	uint64_t rawwpp                       : 1;  /**< Allows PESC_DBG_INFO[29] to generate an interrupt. */
	uint64_t racpp                        : 1;  /**< Allows PESC_DBG_INFO[28] to generate an interrupt. */
	uint64_t ramtlp                       : 1;  /**< Allows PESC_DBG_INFO[27] to generate an interrupt. */
	uint64_t rarwdns                      : 1;  /**< Allows PESC_DBG_INFO[26] to generate an interrupt. */
	uint64_t caar                         : 1;  /**< Allows PESC_DBG_INFO[25] to generate an interrupt. */
	uint64_t racca                        : 1;  /**< Allows PESC_DBG_INFO[24] to generate an interrupt. */
	uint64_t racur                        : 1;  /**< Allows PESC_DBG_INFO[23] to generate an interrupt. */
	uint64_t rauc                         : 1;  /**< Allows PESC_DBG_INFO[22] to generate an interrupt. */
	uint64_t rqo                          : 1;  /**< Allows PESC_DBG_INFO[21] to generate an interrupt. */
	uint64_t fcuv                         : 1;  /**< Allows PESC_DBG_INFO[20] to generate an interrupt. */
	uint64_t rpe                          : 1;  /**< Allows PESC_DBG_INFO[19] to generate an interrupt. */
	uint64_t fcpvwt                       : 1;  /**< Allows PESC_DBG_INFO[18] to generate an interrupt. */
	uint64_t dpeoosd                      : 1;  /**< Allows PESC_DBG_INFO[17] to generate an interrupt. */
	uint64_t rtwdle                       : 1;  /**< Allows PESC_DBG_INFO[16] to generate an interrupt. */
	uint64_t rdwdle                       : 1;  /**< Allows PESC_DBG_INFO[15] to generate an interrupt. */
	uint64_t mre                          : 1;  /**< Allows PESC_DBG_INFO[14] to generate an interrupt. */
	uint64_t rte                          : 1;  /**< Allows PESC_DBG_INFO[13] to generate an interrupt. */
	uint64_t acto                         : 1;  /**< Allows PESC_DBG_INFO[12] to generate an interrupt. */
	uint64_t rvdm                         : 1;  /**< Allows PESC_DBG_INFO[11] to generate an interrupt. */
	uint64_t rumep                        : 1;  /**< Allows PESC_DBG_INFO[10] to generate an interrupt. */
	uint64_t rptamrc                      : 1;  /**< Allows PESC_DBG_INFO[9] to generate an interrupt. */
	uint64_t rpmerc                       : 1;  /**< Allows PESC_DBG_INFO[8] to generate an interrupt. */
	uint64_t rfemrc                       : 1;  /**< Allows PESC_DBG_INFO[7] to generate an interrupt. */
	uint64_t rnfemrc                      : 1;  /**< Allows PESC_DBG_INFO[6] to generate an interrupt. */
	uint64_t rcemrc                       : 1;  /**< Allows PESC_DBG_INFO[5] to generate an interrupt. */
	uint64_t rpoison                      : 1;  /**< Allows PESC_DBG_INFO[4] to generate an interrupt. */
	uint64_t recrce                       : 1;  /**< Allows PESC_DBG_INFO[3] to generate an interrupt. */
	uint64_t rtlplle                      : 1;  /**< Allows PESC_DBG_INFO[2] to generate an interrupt. */
	uint64_t rtlpmal                      : 1;  /**< Allows PESC_DBG_INFO[1] to generate an interrupt. */
	uint64_t spoison                      : 1;  /**< Allows PESC_DBG_INFO[0] to generate an interrupt. */
#else
	uint64_t spoison                      : 1;
	uint64_t rtlpmal                      : 1;
	uint64_t rtlplle                      : 1;
	uint64_t recrce                       : 1;
	uint64_t rpoison                      : 1;
	uint64_t rcemrc                       : 1;
	uint64_t rnfemrc                      : 1;
	uint64_t rfemrc                       : 1;
	uint64_t rpmerc                       : 1;
	uint64_t rptamrc                      : 1;
	uint64_t rumep                        : 1;
	uint64_t rvdm                         : 1;
	uint64_t acto                         : 1;
	uint64_t rte                          : 1;
	uint64_t mre                          : 1;
	uint64_t rdwdle                       : 1;
	uint64_t rtwdle                       : 1;
	uint64_t dpeoosd                      : 1;
	uint64_t fcpvwt                       : 1;
	uint64_t rpe                          : 1;
	uint64_t fcuv                         : 1;
	uint64_t rqo                          : 1;
	uint64_t rauc                         : 1;
	uint64_t racur                        : 1;
	uint64_t racca                        : 1;
	uint64_t caar                         : 1;
	uint64_t rarwdns                      : 1;
	uint64_t ramtlp                       : 1;
	uint64_t racpp                        : 1;
	uint64_t rawwpp                       : 1;
	uint64_t ecrc_e                       : 1;
	uint64_t reserved_31_63               : 33;
#endif
	} s;
	struct cvmx_pescx_dbg_info_en_s       cn52xx;
	struct cvmx_pescx_dbg_info_en_s       cn52xxp1;
	struct cvmx_pescx_dbg_info_en_s       cn56xx;
	struct cvmx_pescx_dbg_info_en_s       cn56xxp1;
};
typedef union cvmx_pescx_dbg_info_en cvmx_pescx_dbg_info_en_t;

/**
 * cvmx_pesc#_diag_status
 *
 * PESC_DIAG_STATUS = PESC Diagnostic Status
 *
 * Selection control for the cores diagnostic bus.
 */
union cvmx_pescx_diag_status {
	uint64_t u64;
	struct cvmx_pescx_diag_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t pm_dst                       : 1;  /**< Current power management DSTATE. */
	uint64_t pm_stat                      : 1;  /**< Power Management Status. */
	uint64_t pm_en                        : 1;  /**< Power Management Event Enable. */
	uint64_t aux_en                       : 1;  /**< Auxilary Power Enable. */
#else
	uint64_t aux_en                       : 1;
	uint64_t pm_en                        : 1;
	uint64_t pm_stat                      : 1;
	uint64_t pm_dst                       : 1;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_pescx_diag_status_s       cn52xx;
	struct cvmx_pescx_diag_status_s       cn52xxp1;
	struct cvmx_pescx_diag_status_s       cn56xx;
	struct cvmx_pescx_diag_status_s       cn56xxp1;
};
typedef union cvmx_pescx_diag_status cvmx_pescx_diag_status_t;

/**
 * cvmx_pesc#_p2n_bar0_start
 *
 * PESC_P2N_BAR0_START = PESC PCIe to Npei BAR0 Start
 *
 * The starting address for addresses to forwarded to the NPEI in RC Mode.
 */
union cvmx_pescx_p2n_bar0_start {
	uint64_t u64;
	struct cvmx_pescx_p2n_bar0_start_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t addr                         : 50; /**< The starting address of the 16KB address space that
                                                         is the BAR0 address space. */
	uint64_t reserved_0_13                : 14;
#else
	uint64_t reserved_0_13                : 14;
	uint64_t addr                         : 50;
#endif
	} s;
	struct cvmx_pescx_p2n_bar0_start_s    cn52xx;
	struct cvmx_pescx_p2n_bar0_start_s    cn52xxp1;
	struct cvmx_pescx_p2n_bar0_start_s    cn56xx;
	struct cvmx_pescx_p2n_bar0_start_s    cn56xxp1;
};
typedef union cvmx_pescx_p2n_bar0_start cvmx_pescx_p2n_bar0_start_t;

/**
 * cvmx_pesc#_p2n_bar1_start
 *
 * PESC_P2N_BAR1_START = PESC PCIe to Npei BAR1 Start
 *
 * The starting address for addresses to forwarded to the NPEI in RC Mode.
 */
union cvmx_pescx_p2n_bar1_start {
	uint64_t u64;
	struct cvmx_pescx_p2n_bar1_start_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t addr                         : 38; /**< The starting address of the 64KB address space
                                                         that is the BAR1 address space. */
	uint64_t reserved_0_25                : 26;
#else
	uint64_t reserved_0_25                : 26;
	uint64_t addr                         : 38;
#endif
	} s;
	struct cvmx_pescx_p2n_bar1_start_s    cn52xx;
	struct cvmx_pescx_p2n_bar1_start_s    cn52xxp1;
	struct cvmx_pescx_p2n_bar1_start_s    cn56xx;
	struct cvmx_pescx_p2n_bar1_start_s    cn56xxp1;
};
typedef union cvmx_pescx_p2n_bar1_start cvmx_pescx_p2n_bar1_start_t;

/**
 * cvmx_pesc#_p2n_bar2_start
 *
 * PESC_P2N_BAR2_START = PESC PCIe to Npei BAR2 Start
 *
 * The starting address for addresses to forwarded to the NPEI in RC Mode.
 */
union cvmx_pescx_p2n_bar2_start {
	uint64_t u64;
	struct cvmx_pescx_p2n_bar2_start_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t addr                         : 25; /**< The starting address of the 2^39 address space
                                                         that is the BAR2 address space. */
	uint64_t reserved_0_38                : 39;
#else
	uint64_t reserved_0_38                : 39;
	uint64_t addr                         : 25;
#endif
	} s;
	struct cvmx_pescx_p2n_bar2_start_s    cn52xx;
	struct cvmx_pescx_p2n_bar2_start_s    cn52xxp1;
	struct cvmx_pescx_p2n_bar2_start_s    cn56xx;
	struct cvmx_pescx_p2n_bar2_start_s    cn56xxp1;
};
typedef union cvmx_pescx_p2n_bar2_start cvmx_pescx_p2n_bar2_start_t;

/**
 * cvmx_pesc#_p2p_bar#_end
 *
 * PESC_P2P_BAR#_END = PESC Peer-To-Peer BAR0 End
 *
 * The ending address for addresses to forwarded to the PCIe peer port.
 */
union cvmx_pescx_p2p_barx_end {
	uint64_t u64;
	struct cvmx_pescx_p2p_barx_end_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t addr                         : 52; /**< The ending address of the address window created
                                                         this field and the PESC_P2P_BAR0_START[63:12]
                                                         field. The full 64-bits of address are created by:
                                                         [ADDR[63:12], 12'b0]. */
	uint64_t reserved_0_11                : 12;
#else
	uint64_t reserved_0_11                : 12;
	uint64_t addr                         : 52;
#endif
	} s;
	struct cvmx_pescx_p2p_barx_end_s      cn52xx;
	struct cvmx_pescx_p2p_barx_end_s      cn52xxp1;
	struct cvmx_pescx_p2p_barx_end_s      cn56xx;
	struct cvmx_pescx_p2p_barx_end_s      cn56xxp1;
};
typedef union cvmx_pescx_p2p_barx_end cvmx_pescx_p2p_barx_end_t;

/**
 * cvmx_pesc#_p2p_bar#_start
 *
 * PESC_P2P_BAR#_START = PESC Peer-To-Peer BAR0 Start
 *
 * The starting address and enable for addresses to forwarded to the PCIe peer port.
 */
union cvmx_pescx_p2p_barx_start {
	uint64_t u64;
	struct cvmx_pescx_p2p_barx_start_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t addr                         : 52; /**< The starting address of the address window created
                                                         this field and the PESC_P2P_BAR0_END[63:12] field.
                                                         The full 64-bits of address are created by:
                                                         [ADDR[63:12], 12'b0]. */
	uint64_t reserved_0_11                : 12;
#else
	uint64_t reserved_0_11                : 12;
	uint64_t addr                         : 52;
#endif
	} s;
	struct cvmx_pescx_p2p_barx_start_s    cn52xx;
	struct cvmx_pescx_p2p_barx_start_s    cn52xxp1;
	struct cvmx_pescx_p2p_barx_start_s    cn56xx;
	struct cvmx_pescx_p2p_barx_start_s    cn56xxp1;
};
typedef union cvmx_pescx_p2p_barx_start cvmx_pescx_p2p_barx_start_t;

/**
 * cvmx_pesc#_tlp_credits
 *
 * PESC_TLP_CREDITS = PESC TLP Credits
 *
 * Specifies the number of credits the PESC for use in moving TLPs. When this register is written the credit values are
 * reset to the register value. A write to this register should take place BEFORE traffic flow starts.
 */
union cvmx_pescx_tlp_credits {
	uint64_t u64;
	struct cvmx_pescx_tlp_credits_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_0_63                : 64;
#else
	uint64_t reserved_0_63                : 64;
#endif
	} s;
	struct cvmx_pescx_tlp_credits_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_56_63               : 8;
	uint64_t peai_ppf                     : 8;  /**< TLP credits for Completion TLPs in the Peer.
                                                         Legal values are 0x24 to 0x80. */
	uint64_t pesc_cpl                     : 8;  /**< TLP credits for Completion TLPs in the Peer.
                                                         Legal values are 0x24 to 0x80. */
	uint64_t pesc_np                      : 8;  /**< TLP credits for Non-Posted TLPs in the Peer.
                                                         Legal values are 0x4 to 0x10. */
	uint64_t pesc_p                       : 8;  /**< TLP credits for Posted TLPs in the Peer.
                                                         Legal values are 0x24 to 0x80. */
	uint64_t npei_cpl                     : 8;  /**< TLP credits for Completion TLPs in the NPEI.
                                                         Legal values are 0x24 to 0x80. */
	uint64_t npei_np                      : 8;  /**< TLP credits for Non-Posted TLPs in the NPEI.
                                                         Legal values are 0x4 to 0x10. */
	uint64_t npei_p                       : 8;  /**< TLP credits for Posted TLPs in the NPEI.
                                                         Legal values are 0x24 to 0x80. */
#else
	uint64_t npei_p                       : 8;
	uint64_t npei_np                      : 8;
	uint64_t npei_cpl                     : 8;
	uint64_t pesc_p                       : 8;
	uint64_t pesc_np                      : 8;
	uint64_t pesc_cpl                     : 8;
	uint64_t peai_ppf                     : 8;
	uint64_t reserved_56_63               : 8;
#endif
	} cn52xx;
	struct cvmx_pescx_tlp_credits_cn52xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_38_63               : 26;
	uint64_t peai_ppf                     : 8;  /**< TLP credits in core clk pre-buffer that holds TLPs
                                                         being sent from PCIe Core to NPEI or PEER. */
	uint64_t pesc_cpl                     : 5;  /**< TLP credits for Completion TLPs in the Peer. */
	uint64_t pesc_np                      : 5;  /**< TLP credits for Non-Posted TLPs in the Peer. */
	uint64_t pesc_p                       : 5;  /**< TLP credits for Posted TLPs in the Peer. */
	uint64_t npei_cpl                     : 5;  /**< TLP credits for Completion TLPs in the NPEI. */
	uint64_t npei_np                      : 5;  /**< TLP credits for Non-Posted TLPs in the NPEI. */
	uint64_t npei_p                       : 5;  /**< TLP credits for Posted TLPs in the NPEI. */
#else
	uint64_t npei_p                       : 5;
	uint64_t npei_np                      : 5;
	uint64_t npei_cpl                     : 5;
	uint64_t pesc_p                       : 5;
	uint64_t pesc_np                      : 5;
	uint64_t pesc_cpl                     : 5;
	uint64_t peai_ppf                     : 8;
	uint64_t reserved_38_63               : 26;
#endif
	} cn52xxp1;
	struct cvmx_pescx_tlp_credits_cn52xx  cn56xx;
	struct cvmx_pescx_tlp_credits_cn52xxp1 cn56xxp1;
};
typedef union cvmx_pescx_tlp_credits cvmx_pescx_tlp_credits_t;

#endif
