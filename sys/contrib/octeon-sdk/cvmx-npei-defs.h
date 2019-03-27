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
 * cvmx-npei-defs.h
 *
 * Configuration and status register (CSR) type definitions for
 * Octeon npei.
 *
 * This file is auto generated. Do not edit.
 *
 * <hr>$Revision$<hr>
 *
 */
#ifndef __CVMX_NPEI_DEFS_H__
#define __CVMX_NPEI_DEFS_H__

#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_NPEI_BAR1_INDEXX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 31))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 31)))))
		cvmx_warn("CVMX_NPEI_BAR1_INDEXX(%lu) is invalid on this chip\n", offset);
	return 0x0000000000000000ull + ((offset) & 31) * 16;
}
#else
#define CVMX_NPEI_BAR1_INDEXX(offset) (0x0000000000000000ull + ((offset) & 31) * 16)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_BIST_STATUS CVMX_NPEI_BIST_STATUS_FUNC()
static inline uint64_t CVMX_NPEI_BIST_STATUS_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_BIST_STATUS not supported on this chip\n");
	return 0x0000000000000580ull;
}
#else
#define CVMX_NPEI_BIST_STATUS (0x0000000000000580ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_BIST_STATUS2 CVMX_NPEI_BIST_STATUS2_FUNC()
static inline uint64_t CVMX_NPEI_BIST_STATUS2_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_BIST_STATUS2 not supported on this chip\n");
	return 0x0000000000000680ull;
}
#else
#define CVMX_NPEI_BIST_STATUS2 (0x0000000000000680ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_CTL_PORT0 CVMX_NPEI_CTL_PORT0_FUNC()
static inline uint64_t CVMX_NPEI_CTL_PORT0_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_CTL_PORT0 not supported on this chip\n");
	return 0x0000000000000250ull;
}
#else
#define CVMX_NPEI_CTL_PORT0 (0x0000000000000250ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_CTL_PORT1 CVMX_NPEI_CTL_PORT1_FUNC()
static inline uint64_t CVMX_NPEI_CTL_PORT1_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_CTL_PORT1 not supported on this chip\n");
	return 0x0000000000000260ull;
}
#else
#define CVMX_NPEI_CTL_PORT1 (0x0000000000000260ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_CTL_STATUS CVMX_NPEI_CTL_STATUS_FUNC()
static inline uint64_t CVMX_NPEI_CTL_STATUS_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_CTL_STATUS not supported on this chip\n");
	return 0x0000000000000570ull;
}
#else
#define CVMX_NPEI_CTL_STATUS (0x0000000000000570ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_CTL_STATUS2 CVMX_NPEI_CTL_STATUS2_FUNC()
static inline uint64_t CVMX_NPEI_CTL_STATUS2_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_CTL_STATUS2 not supported on this chip\n");
	return 0x0000000000003C00ull;
}
#else
#define CVMX_NPEI_CTL_STATUS2 (0x0000000000003C00ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_DATA_OUT_CNT CVMX_NPEI_DATA_OUT_CNT_FUNC()
static inline uint64_t CVMX_NPEI_DATA_OUT_CNT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_DATA_OUT_CNT not supported on this chip\n");
	return 0x00000000000005F0ull;
}
#else
#define CVMX_NPEI_DATA_OUT_CNT (0x00000000000005F0ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_DBG_DATA CVMX_NPEI_DBG_DATA_FUNC()
static inline uint64_t CVMX_NPEI_DBG_DATA_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_DBG_DATA not supported on this chip\n");
	return 0x0000000000000510ull;
}
#else
#define CVMX_NPEI_DBG_DATA (0x0000000000000510ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_DBG_SELECT CVMX_NPEI_DBG_SELECT_FUNC()
static inline uint64_t CVMX_NPEI_DBG_SELECT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_DBG_SELECT not supported on this chip\n");
	return 0x0000000000000500ull;
}
#else
#define CVMX_NPEI_DBG_SELECT (0x0000000000000500ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_DMA0_INT_LEVEL CVMX_NPEI_DMA0_INT_LEVEL_FUNC()
static inline uint64_t CVMX_NPEI_DMA0_INT_LEVEL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_DMA0_INT_LEVEL not supported on this chip\n");
	return 0x00000000000005C0ull;
}
#else
#define CVMX_NPEI_DMA0_INT_LEVEL (0x00000000000005C0ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_DMA1_INT_LEVEL CVMX_NPEI_DMA1_INT_LEVEL_FUNC()
static inline uint64_t CVMX_NPEI_DMA1_INT_LEVEL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_DMA1_INT_LEVEL not supported on this chip\n");
	return 0x00000000000005D0ull;
}
#else
#define CVMX_NPEI_DMA1_INT_LEVEL (0x00000000000005D0ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_NPEI_DMAX_COUNTS(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 4))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 4)))))
		cvmx_warn("CVMX_NPEI_DMAX_COUNTS(%lu) is invalid on this chip\n", offset);
	return 0x0000000000000450ull + ((offset) & 7) * 16;
}
#else
#define CVMX_NPEI_DMAX_COUNTS(offset) (0x0000000000000450ull + ((offset) & 7) * 16)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_NPEI_DMAX_DBELL(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 4))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 4)))))
		cvmx_warn("CVMX_NPEI_DMAX_DBELL(%lu) is invalid on this chip\n", offset);
	return 0x00000000000003B0ull + ((offset) & 7) * 16;
}
#else
#define CVMX_NPEI_DMAX_DBELL(offset) (0x00000000000003B0ull + ((offset) & 7) * 16)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_NPEI_DMAX_IBUFF_SADDR(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 4))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 4)))))
		cvmx_warn("CVMX_NPEI_DMAX_IBUFF_SADDR(%lu) is invalid on this chip\n", offset);
	return 0x0000000000000400ull + ((offset) & 7) * 16;
}
#else
#define CVMX_NPEI_DMAX_IBUFF_SADDR(offset) (0x0000000000000400ull + ((offset) & 7) * 16)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_NPEI_DMAX_NADDR(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 4))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 4)))))
		cvmx_warn("CVMX_NPEI_DMAX_NADDR(%lu) is invalid on this chip\n", offset);
	return 0x00000000000004A0ull + ((offset) & 7) * 16;
}
#else
#define CVMX_NPEI_DMAX_NADDR(offset) (0x00000000000004A0ull + ((offset) & 7) * 16)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_DMA_CNTS CVMX_NPEI_DMA_CNTS_FUNC()
static inline uint64_t CVMX_NPEI_DMA_CNTS_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_DMA_CNTS not supported on this chip\n");
	return 0x00000000000005E0ull;
}
#else
#define CVMX_NPEI_DMA_CNTS (0x00000000000005E0ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_DMA_CONTROL CVMX_NPEI_DMA_CONTROL_FUNC()
static inline uint64_t CVMX_NPEI_DMA_CONTROL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_DMA_CONTROL not supported on this chip\n");
	return 0x00000000000003A0ull;
}
#else
#define CVMX_NPEI_DMA_CONTROL (0x00000000000003A0ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_DMA_PCIE_REQ_NUM CVMX_NPEI_DMA_PCIE_REQ_NUM_FUNC()
static inline uint64_t CVMX_NPEI_DMA_PCIE_REQ_NUM_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_DMA_PCIE_REQ_NUM not supported on this chip\n");
	return 0x00000000000005B0ull;
}
#else
#define CVMX_NPEI_DMA_PCIE_REQ_NUM (0x00000000000005B0ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_DMA_STATE1 CVMX_NPEI_DMA_STATE1_FUNC()
static inline uint64_t CVMX_NPEI_DMA_STATE1_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX)))
		cvmx_warn("CVMX_NPEI_DMA_STATE1 not supported on this chip\n");
	return 0x00000000000006C0ull;
}
#else
#define CVMX_NPEI_DMA_STATE1 (0x00000000000006C0ull)
#endif
#define CVMX_NPEI_DMA_STATE1_P1 (0x0000000000000680ull)
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_DMA_STATE2 CVMX_NPEI_DMA_STATE2_FUNC()
static inline uint64_t CVMX_NPEI_DMA_STATE2_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX)))
		cvmx_warn("CVMX_NPEI_DMA_STATE2 not supported on this chip\n");
	return 0x00000000000006D0ull;
}
#else
#define CVMX_NPEI_DMA_STATE2 (0x00000000000006D0ull)
#endif
#define CVMX_NPEI_DMA_STATE2_P1 (0x0000000000000690ull)
#define CVMX_NPEI_DMA_STATE3_P1 (0x00000000000006A0ull)
#define CVMX_NPEI_DMA_STATE4_P1 (0x00000000000006B0ull)
#define CVMX_NPEI_DMA_STATE5_P1 (0x00000000000006C0ull)
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_INT_A_ENB CVMX_NPEI_INT_A_ENB_FUNC()
static inline uint64_t CVMX_NPEI_INT_A_ENB_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_INT_A_ENB not supported on this chip\n");
	return 0x0000000000000560ull;
}
#else
#define CVMX_NPEI_INT_A_ENB (0x0000000000000560ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_INT_A_ENB2 CVMX_NPEI_INT_A_ENB2_FUNC()
static inline uint64_t CVMX_NPEI_INT_A_ENB2_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_INT_A_ENB2 not supported on this chip\n");
	return 0x0000000000003CE0ull;
}
#else
#define CVMX_NPEI_INT_A_ENB2 (0x0000000000003CE0ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_INT_A_SUM CVMX_NPEI_INT_A_SUM_FUNC()
static inline uint64_t CVMX_NPEI_INT_A_SUM_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_INT_A_SUM not supported on this chip\n");
	return 0x0000000000000550ull;
}
#else
#define CVMX_NPEI_INT_A_SUM (0x0000000000000550ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_INT_ENB CVMX_NPEI_INT_ENB_FUNC()
static inline uint64_t CVMX_NPEI_INT_ENB_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_INT_ENB not supported on this chip\n");
	return 0x0000000000000540ull;
}
#else
#define CVMX_NPEI_INT_ENB (0x0000000000000540ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_INT_ENB2 CVMX_NPEI_INT_ENB2_FUNC()
static inline uint64_t CVMX_NPEI_INT_ENB2_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_INT_ENB2 not supported on this chip\n");
	return 0x0000000000003CD0ull;
}
#else
#define CVMX_NPEI_INT_ENB2 (0x0000000000003CD0ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_INT_INFO CVMX_NPEI_INT_INFO_FUNC()
static inline uint64_t CVMX_NPEI_INT_INFO_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_INT_INFO not supported on this chip\n");
	return 0x0000000000000590ull;
}
#else
#define CVMX_NPEI_INT_INFO (0x0000000000000590ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_INT_SUM CVMX_NPEI_INT_SUM_FUNC()
static inline uint64_t CVMX_NPEI_INT_SUM_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_INT_SUM not supported on this chip\n");
	return 0x0000000000000530ull;
}
#else
#define CVMX_NPEI_INT_SUM (0x0000000000000530ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_INT_SUM2 CVMX_NPEI_INT_SUM2_FUNC()
static inline uint64_t CVMX_NPEI_INT_SUM2_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_INT_SUM2 not supported on this chip\n");
	return 0x0000000000003CC0ull;
}
#else
#define CVMX_NPEI_INT_SUM2 (0x0000000000003CC0ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_LAST_WIN_RDATA0 CVMX_NPEI_LAST_WIN_RDATA0_FUNC()
static inline uint64_t CVMX_NPEI_LAST_WIN_RDATA0_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_LAST_WIN_RDATA0 not supported on this chip\n");
	return 0x0000000000000600ull;
}
#else
#define CVMX_NPEI_LAST_WIN_RDATA0 (0x0000000000000600ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_LAST_WIN_RDATA1 CVMX_NPEI_LAST_WIN_RDATA1_FUNC()
static inline uint64_t CVMX_NPEI_LAST_WIN_RDATA1_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_LAST_WIN_RDATA1 not supported on this chip\n");
	return 0x0000000000000610ull;
}
#else
#define CVMX_NPEI_LAST_WIN_RDATA1 (0x0000000000000610ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_MEM_ACCESS_CTL CVMX_NPEI_MEM_ACCESS_CTL_FUNC()
static inline uint64_t CVMX_NPEI_MEM_ACCESS_CTL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_MEM_ACCESS_CTL not supported on this chip\n");
	return 0x00000000000004F0ull;
}
#else
#define CVMX_NPEI_MEM_ACCESS_CTL (0x00000000000004F0ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_NPEI_MEM_ACCESS_SUBIDX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && (((offset >= 12) && (offset <= 27)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && (((offset >= 12) && (offset <= 27))))))
		cvmx_warn("CVMX_NPEI_MEM_ACCESS_SUBIDX(%lu) is invalid on this chip\n", offset);
	return 0x0000000000000280ull + ((offset) & 31) * 16 - 16*12;
}
#else
#define CVMX_NPEI_MEM_ACCESS_SUBIDX(offset) (0x0000000000000280ull + ((offset) & 31) * 16 - 16*12)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_MSI_ENB0 CVMX_NPEI_MSI_ENB0_FUNC()
static inline uint64_t CVMX_NPEI_MSI_ENB0_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_MSI_ENB0 not supported on this chip\n");
	return 0x0000000000003C50ull;
}
#else
#define CVMX_NPEI_MSI_ENB0 (0x0000000000003C50ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_MSI_ENB1 CVMX_NPEI_MSI_ENB1_FUNC()
static inline uint64_t CVMX_NPEI_MSI_ENB1_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_MSI_ENB1 not supported on this chip\n");
	return 0x0000000000003C60ull;
}
#else
#define CVMX_NPEI_MSI_ENB1 (0x0000000000003C60ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_MSI_ENB2 CVMX_NPEI_MSI_ENB2_FUNC()
static inline uint64_t CVMX_NPEI_MSI_ENB2_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_MSI_ENB2 not supported on this chip\n");
	return 0x0000000000003C70ull;
}
#else
#define CVMX_NPEI_MSI_ENB2 (0x0000000000003C70ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_MSI_ENB3 CVMX_NPEI_MSI_ENB3_FUNC()
static inline uint64_t CVMX_NPEI_MSI_ENB3_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_MSI_ENB3 not supported on this chip\n");
	return 0x0000000000003C80ull;
}
#else
#define CVMX_NPEI_MSI_ENB3 (0x0000000000003C80ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_MSI_RCV0 CVMX_NPEI_MSI_RCV0_FUNC()
static inline uint64_t CVMX_NPEI_MSI_RCV0_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_MSI_RCV0 not supported on this chip\n");
	return 0x0000000000003C10ull;
}
#else
#define CVMX_NPEI_MSI_RCV0 (0x0000000000003C10ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_MSI_RCV1 CVMX_NPEI_MSI_RCV1_FUNC()
static inline uint64_t CVMX_NPEI_MSI_RCV1_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_MSI_RCV1 not supported on this chip\n");
	return 0x0000000000003C20ull;
}
#else
#define CVMX_NPEI_MSI_RCV1 (0x0000000000003C20ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_MSI_RCV2 CVMX_NPEI_MSI_RCV2_FUNC()
static inline uint64_t CVMX_NPEI_MSI_RCV2_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_MSI_RCV2 not supported on this chip\n");
	return 0x0000000000003C30ull;
}
#else
#define CVMX_NPEI_MSI_RCV2 (0x0000000000003C30ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_MSI_RCV3 CVMX_NPEI_MSI_RCV3_FUNC()
static inline uint64_t CVMX_NPEI_MSI_RCV3_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_MSI_RCV3 not supported on this chip\n");
	return 0x0000000000003C40ull;
}
#else
#define CVMX_NPEI_MSI_RCV3 (0x0000000000003C40ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_MSI_RD_MAP CVMX_NPEI_MSI_RD_MAP_FUNC()
static inline uint64_t CVMX_NPEI_MSI_RD_MAP_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_MSI_RD_MAP not supported on this chip\n");
	return 0x0000000000003CA0ull;
}
#else
#define CVMX_NPEI_MSI_RD_MAP (0x0000000000003CA0ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_MSI_W1C_ENB0 CVMX_NPEI_MSI_W1C_ENB0_FUNC()
static inline uint64_t CVMX_NPEI_MSI_W1C_ENB0_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_MSI_W1C_ENB0 not supported on this chip\n");
	return 0x0000000000003CF0ull;
}
#else
#define CVMX_NPEI_MSI_W1C_ENB0 (0x0000000000003CF0ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_MSI_W1C_ENB1 CVMX_NPEI_MSI_W1C_ENB1_FUNC()
static inline uint64_t CVMX_NPEI_MSI_W1C_ENB1_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_MSI_W1C_ENB1 not supported on this chip\n");
	return 0x0000000000003D00ull;
}
#else
#define CVMX_NPEI_MSI_W1C_ENB1 (0x0000000000003D00ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_MSI_W1C_ENB2 CVMX_NPEI_MSI_W1C_ENB2_FUNC()
static inline uint64_t CVMX_NPEI_MSI_W1C_ENB2_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_MSI_W1C_ENB2 not supported on this chip\n");
	return 0x0000000000003D10ull;
}
#else
#define CVMX_NPEI_MSI_W1C_ENB2 (0x0000000000003D10ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_MSI_W1C_ENB3 CVMX_NPEI_MSI_W1C_ENB3_FUNC()
static inline uint64_t CVMX_NPEI_MSI_W1C_ENB3_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_MSI_W1C_ENB3 not supported on this chip\n");
	return 0x0000000000003D20ull;
}
#else
#define CVMX_NPEI_MSI_W1C_ENB3 (0x0000000000003D20ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_MSI_W1S_ENB0 CVMX_NPEI_MSI_W1S_ENB0_FUNC()
static inline uint64_t CVMX_NPEI_MSI_W1S_ENB0_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_MSI_W1S_ENB0 not supported on this chip\n");
	return 0x0000000000003D30ull;
}
#else
#define CVMX_NPEI_MSI_W1S_ENB0 (0x0000000000003D30ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_MSI_W1S_ENB1 CVMX_NPEI_MSI_W1S_ENB1_FUNC()
static inline uint64_t CVMX_NPEI_MSI_W1S_ENB1_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_MSI_W1S_ENB1 not supported on this chip\n");
	return 0x0000000000003D40ull;
}
#else
#define CVMX_NPEI_MSI_W1S_ENB1 (0x0000000000003D40ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_MSI_W1S_ENB2 CVMX_NPEI_MSI_W1S_ENB2_FUNC()
static inline uint64_t CVMX_NPEI_MSI_W1S_ENB2_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_MSI_W1S_ENB2 not supported on this chip\n");
	return 0x0000000000003D50ull;
}
#else
#define CVMX_NPEI_MSI_W1S_ENB2 (0x0000000000003D50ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_MSI_W1S_ENB3 CVMX_NPEI_MSI_W1S_ENB3_FUNC()
static inline uint64_t CVMX_NPEI_MSI_W1S_ENB3_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_MSI_W1S_ENB3 not supported on this chip\n");
	return 0x0000000000003D60ull;
}
#else
#define CVMX_NPEI_MSI_W1S_ENB3 (0x0000000000003D60ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_MSI_WR_MAP CVMX_NPEI_MSI_WR_MAP_FUNC()
static inline uint64_t CVMX_NPEI_MSI_WR_MAP_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_MSI_WR_MAP not supported on this chip\n");
	return 0x0000000000003C90ull;
}
#else
#define CVMX_NPEI_MSI_WR_MAP (0x0000000000003C90ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_PCIE_CREDIT_CNT CVMX_NPEI_PCIE_CREDIT_CNT_FUNC()
static inline uint64_t CVMX_NPEI_PCIE_CREDIT_CNT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_PCIE_CREDIT_CNT not supported on this chip\n");
	return 0x0000000000003D70ull;
}
#else
#define CVMX_NPEI_PCIE_CREDIT_CNT (0x0000000000003D70ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_PCIE_MSI_RCV CVMX_NPEI_PCIE_MSI_RCV_FUNC()
static inline uint64_t CVMX_NPEI_PCIE_MSI_RCV_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_PCIE_MSI_RCV not supported on this chip\n");
	return 0x0000000000003CB0ull;
}
#else
#define CVMX_NPEI_PCIE_MSI_RCV (0x0000000000003CB0ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_PCIE_MSI_RCV_B1 CVMX_NPEI_PCIE_MSI_RCV_B1_FUNC()
static inline uint64_t CVMX_NPEI_PCIE_MSI_RCV_B1_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_PCIE_MSI_RCV_B1 not supported on this chip\n");
	return 0x0000000000000650ull;
}
#else
#define CVMX_NPEI_PCIE_MSI_RCV_B1 (0x0000000000000650ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_PCIE_MSI_RCV_B2 CVMX_NPEI_PCIE_MSI_RCV_B2_FUNC()
static inline uint64_t CVMX_NPEI_PCIE_MSI_RCV_B2_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_PCIE_MSI_RCV_B2 not supported on this chip\n");
	return 0x0000000000000660ull;
}
#else
#define CVMX_NPEI_PCIE_MSI_RCV_B2 (0x0000000000000660ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_PCIE_MSI_RCV_B3 CVMX_NPEI_PCIE_MSI_RCV_B3_FUNC()
static inline uint64_t CVMX_NPEI_PCIE_MSI_RCV_B3_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_PCIE_MSI_RCV_B3 not supported on this chip\n");
	return 0x0000000000000670ull;
}
#else
#define CVMX_NPEI_PCIE_MSI_RCV_B3 (0x0000000000000670ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_NPEI_PKTX_CNTS(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 31))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 31)))))
		cvmx_warn("CVMX_NPEI_PKTX_CNTS(%lu) is invalid on this chip\n", offset);
	return 0x0000000000002400ull + ((offset) & 31) * 16;
}
#else
#define CVMX_NPEI_PKTX_CNTS(offset) (0x0000000000002400ull + ((offset) & 31) * 16)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_NPEI_PKTX_INSTR_BADDR(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 31))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 31)))))
		cvmx_warn("CVMX_NPEI_PKTX_INSTR_BADDR(%lu) is invalid on this chip\n", offset);
	return 0x0000000000002800ull + ((offset) & 31) * 16;
}
#else
#define CVMX_NPEI_PKTX_INSTR_BADDR(offset) (0x0000000000002800ull + ((offset) & 31) * 16)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_NPEI_PKTX_INSTR_BAOFF_DBELL(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 31))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 31)))))
		cvmx_warn("CVMX_NPEI_PKTX_INSTR_BAOFF_DBELL(%lu) is invalid on this chip\n", offset);
	return 0x0000000000002C00ull + ((offset) & 31) * 16;
}
#else
#define CVMX_NPEI_PKTX_INSTR_BAOFF_DBELL(offset) (0x0000000000002C00ull + ((offset) & 31) * 16)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_NPEI_PKTX_INSTR_FIFO_RSIZE(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 31))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 31)))))
		cvmx_warn("CVMX_NPEI_PKTX_INSTR_FIFO_RSIZE(%lu) is invalid on this chip\n", offset);
	return 0x0000000000003000ull + ((offset) & 31) * 16;
}
#else
#define CVMX_NPEI_PKTX_INSTR_FIFO_RSIZE(offset) (0x0000000000003000ull + ((offset) & 31) * 16)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_NPEI_PKTX_INSTR_HEADER(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 31))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 31)))))
		cvmx_warn("CVMX_NPEI_PKTX_INSTR_HEADER(%lu) is invalid on this chip\n", offset);
	return 0x0000000000003400ull + ((offset) & 31) * 16;
}
#else
#define CVMX_NPEI_PKTX_INSTR_HEADER(offset) (0x0000000000003400ull + ((offset) & 31) * 16)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_NPEI_PKTX_IN_BP(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 31))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 31)))))
		cvmx_warn("CVMX_NPEI_PKTX_IN_BP(%lu) is invalid on this chip\n", offset);
	return 0x0000000000003800ull + ((offset) & 31) * 16;
}
#else
#define CVMX_NPEI_PKTX_IN_BP(offset) (0x0000000000003800ull + ((offset) & 31) * 16)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_NPEI_PKTX_SLIST_BADDR(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 31))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 31)))))
		cvmx_warn("CVMX_NPEI_PKTX_SLIST_BADDR(%lu) is invalid on this chip\n", offset);
	return 0x0000000000001400ull + ((offset) & 31) * 16;
}
#else
#define CVMX_NPEI_PKTX_SLIST_BADDR(offset) (0x0000000000001400ull + ((offset) & 31) * 16)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_NPEI_PKTX_SLIST_BAOFF_DBELL(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 31))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 31)))))
		cvmx_warn("CVMX_NPEI_PKTX_SLIST_BAOFF_DBELL(%lu) is invalid on this chip\n", offset);
	return 0x0000000000001800ull + ((offset) & 31) * 16;
}
#else
#define CVMX_NPEI_PKTX_SLIST_BAOFF_DBELL(offset) (0x0000000000001800ull + ((offset) & 31) * 16)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_NPEI_PKTX_SLIST_FIFO_RSIZE(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 31))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 31)))))
		cvmx_warn("CVMX_NPEI_PKTX_SLIST_FIFO_RSIZE(%lu) is invalid on this chip\n", offset);
	return 0x0000000000001C00ull + ((offset) & 31) * 16;
}
#else
#define CVMX_NPEI_PKTX_SLIST_FIFO_RSIZE(offset) (0x0000000000001C00ull + ((offset) & 31) * 16)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_PKT_CNT_INT CVMX_NPEI_PKT_CNT_INT_FUNC()
static inline uint64_t CVMX_NPEI_PKT_CNT_INT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_PKT_CNT_INT not supported on this chip\n");
	return 0x0000000000001110ull;
}
#else
#define CVMX_NPEI_PKT_CNT_INT (0x0000000000001110ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_PKT_CNT_INT_ENB CVMX_NPEI_PKT_CNT_INT_ENB_FUNC()
static inline uint64_t CVMX_NPEI_PKT_CNT_INT_ENB_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_PKT_CNT_INT_ENB not supported on this chip\n");
	return 0x0000000000001130ull;
}
#else
#define CVMX_NPEI_PKT_CNT_INT_ENB (0x0000000000001130ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_PKT_DATA_OUT_ES CVMX_NPEI_PKT_DATA_OUT_ES_FUNC()
static inline uint64_t CVMX_NPEI_PKT_DATA_OUT_ES_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_PKT_DATA_OUT_ES not supported on this chip\n");
	return 0x00000000000010B0ull;
}
#else
#define CVMX_NPEI_PKT_DATA_OUT_ES (0x00000000000010B0ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_PKT_DATA_OUT_NS CVMX_NPEI_PKT_DATA_OUT_NS_FUNC()
static inline uint64_t CVMX_NPEI_PKT_DATA_OUT_NS_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_PKT_DATA_OUT_NS not supported on this chip\n");
	return 0x00000000000010A0ull;
}
#else
#define CVMX_NPEI_PKT_DATA_OUT_NS (0x00000000000010A0ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_PKT_DATA_OUT_ROR CVMX_NPEI_PKT_DATA_OUT_ROR_FUNC()
static inline uint64_t CVMX_NPEI_PKT_DATA_OUT_ROR_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_PKT_DATA_OUT_ROR not supported on this chip\n");
	return 0x0000000000001090ull;
}
#else
#define CVMX_NPEI_PKT_DATA_OUT_ROR (0x0000000000001090ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_PKT_DPADDR CVMX_NPEI_PKT_DPADDR_FUNC()
static inline uint64_t CVMX_NPEI_PKT_DPADDR_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_PKT_DPADDR not supported on this chip\n");
	return 0x0000000000001080ull;
}
#else
#define CVMX_NPEI_PKT_DPADDR (0x0000000000001080ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_PKT_INPUT_CONTROL CVMX_NPEI_PKT_INPUT_CONTROL_FUNC()
static inline uint64_t CVMX_NPEI_PKT_INPUT_CONTROL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_PKT_INPUT_CONTROL not supported on this chip\n");
	return 0x0000000000001150ull;
}
#else
#define CVMX_NPEI_PKT_INPUT_CONTROL (0x0000000000001150ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_PKT_INSTR_ENB CVMX_NPEI_PKT_INSTR_ENB_FUNC()
static inline uint64_t CVMX_NPEI_PKT_INSTR_ENB_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_PKT_INSTR_ENB not supported on this chip\n");
	return 0x0000000000001000ull;
}
#else
#define CVMX_NPEI_PKT_INSTR_ENB (0x0000000000001000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_PKT_INSTR_RD_SIZE CVMX_NPEI_PKT_INSTR_RD_SIZE_FUNC()
static inline uint64_t CVMX_NPEI_PKT_INSTR_RD_SIZE_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_PKT_INSTR_RD_SIZE not supported on this chip\n");
	return 0x0000000000001190ull;
}
#else
#define CVMX_NPEI_PKT_INSTR_RD_SIZE (0x0000000000001190ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_PKT_INSTR_SIZE CVMX_NPEI_PKT_INSTR_SIZE_FUNC()
static inline uint64_t CVMX_NPEI_PKT_INSTR_SIZE_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_PKT_INSTR_SIZE not supported on this chip\n");
	return 0x0000000000001020ull;
}
#else
#define CVMX_NPEI_PKT_INSTR_SIZE (0x0000000000001020ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_PKT_INT_LEVELS CVMX_NPEI_PKT_INT_LEVELS_FUNC()
static inline uint64_t CVMX_NPEI_PKT_INT_LEVELS_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_PKT_INT_LEVELS not supported on this chip\n");
	return 0x0000000000001100ull;
}
#else
#define CVMX_NPEI_PKT_INT_LEVELS (0x0000000000001100ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_PKT_IN_BP CVMX_NPEI_PKT_IN_BP_FUNC()
static inline uint64_t CVMX_NPEI_PKT_IN_BP_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_PKT_IN_BP not supported on this chip\n");
	return 0x00000000000006B0ull;
}
#else
#define CVMX_NPEI_PKT_IN_BP (0x00000000000006B0ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_NPEI_PKT_IN_DONEX_CNTS(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 31))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 31)))))
		cvmx_warn("CVMX_NPEI_PKT_IN_DONEX_CNTS(%lu) is invalid on this chip\n", offset);
	return 0x0000000000002000ull + ((offset) & 31) * 16;
}
#else
#define CVMX_NPEI_PKT_IN_DONEX_CNTS(offset) (0x0000000000002000ull + ((offset) & 31) * 16)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_PKT_IN_INSTR_COUNTS CVMX_NPEI_PKT_IN_INSTR_COUNTS_FUNC()
static inline uint64_t CVMX_NPEI_PKT_IN_INSTR_COUNTS_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_PKT_IN_INSTR_COUNTS not supported on this chip\n");
	return 0x00000000000006A0ull;
}
#else
#define CVMX_NPEI_PKT_IN_INSTR_COUNTS (0x00000000000006A0ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_PKT_IN_PCIE_PORT CVMX_NPEI_PKT_IN_PCIE_PORT_FUNC()
static inline uint64_t CVMX_NPEI_PKT_IN_PCIE_PORT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_PKT_IN_PCIE_PORT not supported on this chip\n");
	return 0x00000000000011A0ull;
}
#else
#define CVMX_NPEI_PKT_IN_PCIE_PORT (0x00000000000011A0ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_PKT_IPTR CVMX_NPEI_PKT_IPTR_FUNC()
static inline uint64_t CVMX_NPEI_PKT_IPTR_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_PKT_IPTR not supported on this chip\n");
	return 0x0000000000001070ull;
}
#else
#define CVMX_NPEI_PKT_IPTR (0x0000000000001070ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_PKT_OUTPUT_WMARK CVMX_NPEI_PKT_OUTPUT_WMARK_FUNC()
static inline uint64_t CVMX_NPEI_PKT_OUTPUT_WMARK_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_PKT_OUTPUT_WMARK not supported on this chip\n");
	return 0x0000000000001160ull;
}
#else
#define CVMX_NPEI_PKT_OUTPUT_WMARK (0x0000000000001160ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_PKT_OUT_BMODE CVMX_NPEI_PKT_OUT_BMODE_FUNC()
static inline uint64_t CVMX_NPEI_PKT_OUT_BMODE_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_PKT_OUT_BMODE not supported on this chip\n");
	return 0x00000000000010D0ull;
}
#else
#define CVMX_NPEI_PKT_OUT_BMODE (0x00000000000010D0ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_PKT_OUT_ENB CVMX_NPEI_PKT_OUT_ENB_FUNC()
static inline uint64_t CVMX_NPEI_PKT_OUT_ENB_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_PKT_OUT_ENB not supported on this chip\n");
	return 0x0000000000001010ull;
}
#else
#define CVMX_NPEI_PKT_OUT_ENB (0x0000000000001010ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_PKT_PCIE_PORT CVMX_NPEI_PKT_PCIE_PORT_FUNC()
static inline uint64_t CVMX_NPEI_PKT_PCIE_PORT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_PKT_PCIE_PORT not supported on this chip\n");
	return 0x00000000000010E0ull;
}
#else
#define CVMX_NPEI_PKT_PCIE_PORT (0x00000000000010E0ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_PKT_PORT_IN_RST CVMX_NPEI_PKT_PORT_IN_RST_FUNC()
static inline uint64_t CVMX_NPEI_PKT_PORT_IN_RST_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_PKT_PORT_IN_RST not supported on this chip\n");
	return 0x0000000000000690ull;
}
#else
#define CVMX_NPEI_PKT_PORT_IN_RST (0x0000000000000690ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_PKT_SLIST_ES CVMX_NPEI_PKT_SLIST_ES_FUNC()
static inline uint64_t CVMX_NPEI_PKT_SLIST_ES_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_PKT_SLIST_ES not supported on this chip\n");
	return 0x0000000000001050ull;
}
#else
#define CVMX_NPEI_PKT_SLIST_ES (0x0000000000001050ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_PKT_SLIST_ID_SIZE CVMX_NPEI_PKT_SLIST_ID_SIZE_FUNC()
static inline uint64_t CVMX_NPEI_PKT_SLIST_ID_SIZE_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_PKT_SLIST_ID_SIZE not supported on this chip\n");
	return 0x0000000000001180ull;
}
#else
#define CVMX_NPEI_PKT_SLIST_ID_SIZE (0x0000000000001180ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_PKT_SLIST_NS CVMX_NPEI_PKT_SLIST_NS_FUNC()
static inline uint64_t CVMX_NPEI_PKT_SLIST_NS_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_PKT_SLIST_NS not supported on this chip\n");
	return 0x0000000000001040ull;
}
#else
#define CVMX_NPEI_PKT_SLIST_NS (0x0000000000001040ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_PKT_SLIST_ROR CVMX_NPEI_PKT_SLIST_ROR_FUNC()
static inline uint64_t CVMX_NPEI_PKT_SLIST_ROR_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_PKT_SLIST_ROR not supported on this chip\n");
	return 0x0000000000001030ull;
}
#else
#define CVMX_NPEI_PKT_SLIST_ROR (0x0000000000001030ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_PKT_TIME_INT CVMX_NPEI_PKT_TIME_INT_FUNC()
static inline uint64_t CVMX_NPEI_PKT_TIME_INT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_PKT_TIME_INT not supported on this chip\n");
	return 0x0000000000001120ull;
}
#else
#define CVMX_NPEI_PKT_TIME_INT (0x0000000000001120ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_PKT_TIME_INT_ENB CVMX_NPEI_PKT_TIME_INT_ENB_FUNC()
static inline uint64_t CVMX_NPEI_PKT_TIME_INT_ENB_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_PKT_TIME_INT_ENB not supported on this chip\n");
	return 0x0000000000001140ull;
}
#else
#define CVMX_NPEI_PKT_TIME_INT_ENB (0x0000000000001140ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_RSL_INT_BLOCKS CVMX_NPEI_RSL_INT_BLOCKS_FUNC()
static inline uint64_t CVMX_NPEI_RSL_INT_BLOCKS_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_RSL_INT_BLOCKS not supported on this chip\n");
	return 0x0000000000000520ull;
}
#else
#define CVMX_NPEI_RSL_INT_BLOCKS (0x0000000000000520ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_SCRATCH_1 CVMX_NPEI_SCRATCH_1_FUNC()
static inline uint64_t CVMX_NPEI_SCRATCH_1_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_SCRATCH_1 not supported on this chip\n");
	return 0x0000000000000270ull;
}
#else
#define CVMX_NPEI_SCRATCH_1 (0x0000000000000270ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_STATE1 CVMX_NPEI_STATE1_FUNC()
static inline uint64_t CVMX_NPEI_STATE1_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_STATE1 not supported on this chip\n");
	return 0x0000000000000620ull;
}
#else
#define CVMX_NPEI_STATE1 (0x0000000000000620ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_STATE2 CVMX_NPEI_STATE2_FUNC()
static inline uint64_t CVMX_NPEI_STATE2_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_STATE2 not supported on this chip\n");
	return 0x0000000000000630ull;
}
#else
#define CVMX_NPEI_STATE2 (0x0000000000000630ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_STATE3 CVMX_NPEI_STATE3_FUNC()
static inline uint64_t CVMX_NPEI_STATE3_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_STATE3 not supported on this chip\n");
	return 0x0000000000000640ull;
}
#else
#define CVMX_NPEI_STATE3 (0x0000000000000640ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_WINDOW_CTL CVMX_NPEI_WINDOW_CTL_FUNC()
static inline uint64_t CVMX_NPEI_WINDOW_CTL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_WINDOW_CTL not supported on this chip\n");
	return 0x0000000000000380ull;
}
#else
#define CVMX_NPEI_WINDOW_CTL (0x0000000000000380ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_WIN_RD_ADDR CVMX_NPEI_WIN_RD_ADDR_FUNC()
static inline uint64_t CVMX_NPEI_WIN_RD_ADDR_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_WIN_RD_ADDR not supported on this chip\n");
	return 0x0000000000000210ull;
}
#else
#define CVMX_NPEI_WIN_RD_ADDR (0x0000000000000210ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_WIN_RD_DATA CVMX_NPEI_WIN_RD_DATA_FUNC()
static inline uint64_t CVMX_NPEI_WIN_RD_DATA_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_WIN_RD_DATA not supported on this chip\n");
	return 0x0000000000000240ull;
}
#else
#define CVMX_NPEI_WIN_RD_DATA (0x0000000000000240ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_WIN_WR_ADDR CVMX_NPEI_WIN_WR_ADDR_FUNC()
static inline uint64_t CVMX_NPEI_WIN_WR_ADDR_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_WIN_WR_ADDR not supported on this chip\n");
	return 0x0000000000000200ull;
}
#else
#define CVMX_NPEI_WIN_WR_ADDR (0x0000000000000200ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_WIN_WR_DATA CVMX_NPEI_WIN_WR_DATA_FUNC()
static inline uint64_t CVMX_NPEI_WIN_WR_DATA_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_WIN_WR_DATA not supported on this chip\n");
	return 0x0000000000000220ull;
}
#else
#define CVMX_NPEI_WIN_WR_DATA (0x0000000000000220ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NPEI_WIN_WR_MASK CVMX_NPEI_WIN_WR_MASK_FUNC()
static inline uint64_t CVMX_NPEI_WIN_WR_MASK_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX)))
		cvmx_warn("CVMX_NPEI_WIN_WR_MASK not supported on this chip\n");
	return 0x0000000000000230ull;
}
#else
#define CVMX_NPEI_WIN_WR_MASK (0x0000000000000230ull)
#endif

/**
 * cvmx_npei_bar1_index#
 *
 * Total Address is 16Kb; 0x0000 - 0x3fff, 0x000 - 0x7fe(Reg, every other 8B)
 *
 * General  5kb; 0x0000 - 0x13ff, 0x000 - 0x27e(Reg-General)
 * PktMem  10Kb; 0x1400 - 0x3bff, 0x280 - 0x77e(Reg-General-Packet)
 * Rsvd     1Kb; 0x3c00 - 0x3fff, 0x780 - 0x7fe(Reg-NCB Only Mode)
 *                                   == NPEI_PKT_CNT_INT_ENB[PORT]
 *                                   == NPEI_PKT_TIME_INT_ENB[PORT]
 *                                   == NPEI_PKT_CNT_INT[PORT]
 *                                   == NPEI_PKT_TIME_INT[PORT]
 *                                   == NPEI_PKT_PCIE_PORT[PP]
 *                                   == NPEI_PKT_SLIST_ROR[ROR]
 *                                   == NPEI_PKT_SLIST_ROR[NSR] ?
 *                                   == NPEI_PKT_SLIST_ES[ES]
 *                                   == NPEI_PKTn_SLIST_BAOFF_DBELL[AOFF]
 *                                   == NPEI_PKTn_SLIST_BAOFF_DBELL[DBELL]
 *                                   == NPEI_PKTn_CNTS[CNT]
 * NPEI_CTL_STATUS[OUTn_ENB]         == NPEI_PKT_OUT_ENB[ENB]
 * NPEI_BASE_ADDRESS_OUTPUTn[BADDR]  == NPEI_PKTn_SLIST_BADDR[ADDR]
 * NPEI_DESC_OUTPUTn[SIZE]           == NPEI_PKTn_SLIST_FIFO_RSIZE[RSIZE]
 * NPEI_Pn_DBPAIR_ADDR[NADDR]        == NPEI_PKTn_SLIST_BADDR[ADDR] + NPEI_PKTn_SLIST_BAOFF_DBELL[AOFF]
 * NPEI_PKT_CREDITSn[PTR_CNT]        == NPEI_PKTn_SLIST_BAOFF_DBELL[DBELL]
 * NPEI_P0_PAIR_CNTS[AVAIL]          == NPEI_PKTn_SLIST_BAOFF_DBELL[DBELL]
 * NPEI_P0_PAIR_CNTS[FCNT]           ==
 * NPEI_PKTS_SENTn[PKT_CNT]          == NPEI_PKTn_CNTS[CNT]
 * NPEI_OUTPUT_CONTROL[Pn_BMODE]     == NPEI_PKT_OUT_BMODE[BMODE]
 * NPEI_PKT_CREDITSn[PKT_CNT]        == NPEI_PKTn_CNTS[CNT]
 * NPEI_BUFF_SIZE_OUTPUTn[BSIZE]     == NPEI_PKT_SLIST_ID_SIZE[BSIZE]
 * NPEI_BUFF_SIZE_OUTPUTn[ISIZE]     == NPEI_PKT_SLIST_ID_SIZE[ISIZE]
 * NPEI_OUTPUT_CONTROL[On_CSRM]      == NPEI_PKT_DPADDR[DPTR] & NPEI_PKT_OUT_USE_IPTR[PORT]
 * NPEI_OUTPUT_CONTROL[On_ES]        == NPEI_PKT_DATA_OUT_ES[ES]
 * NPEI_OUTPUT_CONTROL[On_NS]        == NPEI_PKT_DATA_OUT_NS[NSR] ?
 * NPEI_OUTPUT_CONTROL[On_RO]        == NPEI_PKT_DATA_OUT_ROR[ROR]
 * NPEI_PKTS_SENT_INT_LEVn[PKT_CNT]  == NPEI_PKT_INT_LEVELS[CNT]
 * NPEI_PKTS_SENT_TIMEn[PKT_TIME]    == NPEI_PKT_INT_LEVELS[TIME]
 * NPEI_OUTPUT_CONTROL[IPTR_On]      == NPEI_PKT_IPTR[IPTR]
 * NPEI_PCIE_PORT_OUTPUT[]           == NPEI_PKT_PCIE_PORT[PP]
 *
 *                  NPEI_BAR1_INDEXX = NPEI BAR1 IndexX Register
 *
 * Contains address index and control bits for access to memory ranges of BAR-1. Index is build from supplied address [25:22].
 * NPEI_BAR1_INDEX0 through NPEI_BAR1_INDEX15 is used for transactions orginating with PCIE-PORT0 and NPEI_BAR1_INDEX16
 * through NPEI_BAR1_INDEX31 is used for transactions originating with PCIE-PORT1.
 */
union cvmx_npei_bar1_indexx {
	uint32_t u32;
	struct cvmx_npei_bar1_indexx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_18_31               : 14;
	uint32_t addr_idx                     : 14; /**< Address bits [35:22] sent to L2C */
	uint32_t ca                           : 1;  /**< Set '1' when access is not to be cached in L2. */
	uint32_t end_swp                      : 2;  /**< Endian Swap Mode */
	uint32_t addr_v                       : 1;  /**< Set '1' when the selected address range is valid. */
#else
	uint32_t addr_v                       : 1;
	uint32_t end_swp                      : 2;
	uint32_t ca                           : 1;
	uint32_t addr_idx                     : 14;
	uint32_t reserved_18_31               : 14;
#endif
	} s;
	struct cvmx_npei_bar1_indexx_s        cn52xx;
	struct cvmx_npei_bar1_indexx_s        cn52xxp1;
	struct cvmx_npei_bar1_indexx_s        cn56xx;
	struct cvmx_npei_bar1_indexx_s        cn56xxp1;
};
typedef union cvmx_npei_bar1_indexx cvmx_npei_bar1_indexx_t;

/**
 * cvmx_npei_bist_status
 *
 * NPEI_BIST_STATUS = NPI's BIST Status Register
 *
 * Results from BIST runs of NPEI's memories.
 */
union cvmx_npei_bist_status {
	uint64_t u64;
	struct cvmx_npei_bist_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t pkt_rdf                      : 1;  /**< BIST Status for PKT Read FIFO */
	uint64_t reserved_60_62               : 3;
	uint64_t pcr_gim                      : 1;  /**< BIST Status for PKT Gather Instr MEM */
	uint64_t pkt_pif                      : 1;  /**< BIST Status for PKT INB FIFO */
	uint64_t pcsr_int                     : 1;  /**< BIST Status for PKT pout_int_bstatus */
	uint64_t pcsr_im                      : 1;  /**< BIST Status for PKT pcsr_instr_mem_bstatus */
	uint64_t pcsr_cnt                     : 1;  /**< BIST Status for PKT pin_cnt_bstatus */
	uint64_t pcsr_id                      : 1;  /**< BIST Status for PKT pcsr_in_done_bstatus */
	uint64_t pcsr_sl                      : 1;  /**< BIST Status for PKT pcsr_slist_bstatus */
	uint64_t reserved_50_52               : 3;
	uint64_t pkt_ind                      : 1;  /**< BIST Status for PKT Instruction Done MEM */
	uint64_t pkt_slm                      : 1;  /**< BIST Status for PKT SList MEM */
	uint64_t reserved_36_47               : 12;
	uint64_t d0_pst                       : 1;  /**< BIST Status for DMA0 Pcie Store */
	uint64_t d1_pst                       : 1;  /**< BIST Status for DMA1 Pcie Store */
	uint64_t d2_pst                       : 1;  /**< BIST Status for DMA2 Pcie Store */
	uint64_t d3_pst                       : 1;  /**< BIST Status for DMA3 Pcie Store */
	uint64_t reserved_31_31               : 1;
	uint64_t n2p0_c                       : 1;  /**< BIST Status for N2P Port0 Cmd */
	uint64_t n2p0_o                       : 1;  /**< BIST Status for N2P Port0 Data */
	uint64_t n2p1_c                       : 1;  /**< BIST Status for N2P Port1 Cmd */
	uint64_t n2p1_o                       : 1;  /**< BIST Status for N2P Port1 Data */
	uint64_t cpl_p0                       : 1;  /**< BIST Status for CPL Port 0 */
	uint64_t cpl_p1                       : 1;  /**< BIST Status for CPL Port 1 */
	uint64_t p2n1_po                      : 1;  /**< BIST Status for P2N Port1 P Order */
	uint64_t p2n1_no                      : 1;  /**< BIST Status for P2N Port1 N Order */
	uint64_t p2n1_co                      : 1;  /**< BIST Status for P2N Port1 C Order */
	uint64_t p2n0_po                      : 1;  /**< BIST Status for P2N Port0 P Order */
	uint64_t p2n0_no                      : 1;  /**< BIST Status for P2N Port0 N Order */
	uint64_t p2n0_co                      : 1;  /**< BIST Status for P2N Port0 C Order */
	uint64_t p2n0_c0                      : 1;  /**< BIST Status for P2N Port0 C0 */
	uint64_t p2n0_c1                      : 1;  /**< BIST Status for P2N Port0 C1 */
	uint64_t p2n0_n                       : 1;  /**< BIST Status for P2N Port0 N */
	uint64_t p2n0_p0                      : 1;  /**< BIST Status for P2N Port0 P0 */
	uint64_t p2n0_p1                      : 1;  /**< BIST Status for P2N Port0 P1 */
	uint64_t p2n1_c0                      : 1;  /**< BIST Status for P2N Port1 C0 */
	uint64_t p2n1_c1                      : 1;  /**< BIST Status for P2N Port1 C1 */
	uint64_t p2n1_n                       : 1;  /**< BIST Status for P2N Port1 N */
	uint64_t p2n1_p0                      : 1;  /**< BIST Status for P2N Port1 P0 */
	uint64_t p2n1_p1                      : 1;  /**< BIST Status for P2N Port1 P1 */
	uint64_t csm0                         : 1;  /**< BIST Status for CSM0 */
	uint64_t csm1                         : 1;  /**< BIST Status for CSM1 */
	uint64_t dif0                         : 1;  /**< BIST Status for DMA Instr0 */
	uint64_t dif1                         : 1;  /**< BIST Status for DMA Instr0 */
	uint64_t dif2                         : 1;  /**< BIST Status for DMA Instr0 */
	uint64_t dif3                         : 1;  /**< BIST Status for DMA Instr0 */
	uint64_t reserved_2_2                 : 1;
	uint64_t msi                          : 1;  /**< BIST Status for MSI Memory Map */
	uint64_t ncb_cmd                      : 1;  /**< BIST Status for NCB Outbound Commands */
#else
	uint64_t ncb_cmd                      : 1;
	uint64_t msi                          : 1;
	uint64_t reserved_2_2                 : 1;
	uint64_t dif3                         : 1;
	uint64_t dif2                         : 1;
	uint64_t dif1                         : 1;
	uint64_t dif0                         : 1;
	uint64_t csm1                         : 1;
	uint64_t csm0                         : 1;
	uint64_t p2n1_p1                      : 1;
	uint64_t p2n1_p0                      : 1;
	uint64_t p2n1_n                       : 1;
	uint64_t p2n1_c1                      : 1;
	uint64_t p2n1_c0                      : 1;
	uint64_t p2n0_p1                      : 1;
	uint64_t p2n0_p0                      : 1;
	uint64_t p2n0_n                       : 1;
	uint64_t p2n0_c1                      : 1;
	uint64_t p2n0_c0                      : 1;
	uint64_t p2n0_co                      : 1;
	uint64_t p2n0_no                      : 1;
	uint64_t p2n0_po                      : 1;
	uint64_t p2n1_co                      : 1;
	uint64_t p2n1_no                      : 1;
	uint64_t p2n1_po                      : 1;
	uint64_t cpl_p1                       : 1;
	uint64_t cpl_p0                       : 1;
	uint64_t n2p1_o                       : 1;
	uint64_t n2p1_c                       : 1;
	uint64_t n2p0_o                       : 1;
	uint64_t n2p0_c                       : 1;
	uint64_t reserved_31_31               : 1;
	uint64_t d3_pst                       : 1;
	uint64_t d2_pst                       : 1;
	uint64_t d1_pst                       : 1;
	uint64_t d0_pst                       : 1;
	uint64_t reserved_36_47               : 12;
	uint64_t pkt_slm                      : 1;
	uint64_t pkt_ind                      : 1;
	uint64_t reserved_50_52               : 3;
	uint64_t pcsr_sl                      : 1;
	uint64_t pcsr_id                      : 1;
	uint64_t pcsr_cnt                     : 1;
	uint64_t pcsr_im                      : 1;
	uint64_t pcsr_int                     : 1;
	uint64_t pkt_pif                      : 1;
	uint64_t pcr_gim                      : 1;
	uint64_t reserved_60_62               : 3;
	uint64_t pkt_rdf                      : 1;
#endif
	} s;
	struct cvmx_npei_bist_status_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t pkt_rdf                      : 1;  /**< BIST Status for PKT Read FIFO */
	uint64_t reserved_60_62               : 3;
	uint64_t pcr_gim                      : 1;  /**< BIST Status for PKT Gather Instr MEM */
	uint64_t pkt_pif                      : 1;  /**< BIST Status for PKT INB FIFO */
	uint64_t pcsr_int                     : 1;  /**< BIST Status for PKT OUTB Interrupt MEM */
	uint64_t pcsr_im                      : 1;  /**< BIST Status for PKT CSR Instr MEM */
	uint64_t pcsr_cnt                     : 1;  /**< BIST Status for PKT INB Count MEM */
	uint64_t pcsr_id                      : 1;  /**< BIST Status for PKT INB Instr Done MEM */
	uint64_t pcsr_sl                      : 1;  /**< BIST Status for PKT OUTB SLIST MEM */
	uint64_t pkt_imem                     : 1;  /**< BIST Status for PKT OUTB IFIFO */
	uint64_t pkt_pfm                      : 1;  /**< BIST Status for PKT Front MEM */
	uint64_t pkt_pof                      : 1;  /**< BIST Status for PKT OUTB FIFO */
	uint64_t reserved_48_49               : 2;
	uint64_t pkt_pop0                     : 1;  /**< BIST Status for PKT OUTB Slist0 */
	uint64_t pkt_pop1                     : 1;  /**< BIST Status for PKT OUTB Slist1 */
	uint64_t d0_mem                       : 1;  /**< BIST Status for DMA MEM 0 */
	uint64_t d1_mem                       : 1;  /**< BIST Status for DMA MEM 1 */
	uint64_t d2_mem                       : 1;  /**< BIST Status for DMA MEM 2 */
	uint64_t d3_mem                       : 1;  /**< BIST Status for DMA MEM 3 */
	uint64_t d4_mem                       : 1;  /**< BIST Status for DMA MEM 4 */
	uint64_t ds_mem                       : 1;  /**< BIST Status for DMA  Memory */
	uint64_t reserved_36_39               : 4;
	uint64_t d0_pst                       : 1;  /**< BIST Status for DMA0 Pcie Store */
	uint64_t d1_pst                       : 1;  /**< BIST Status for DMA1 Pcie Store */
	uint64_t d2_pst                       : 1;  /**< BIST Status for DMA2 Pcie Store */
	uint64_t d3_pst                       : 1;  /**< BIST Status for DMA3 Pcie Store */
	uint64_t d4_pst                       : 1;  /**< BIST Status for DMA4 Pcie Store */
	uint64_t n2p0_c                       : 1;  /**< BIST Status for N2P Port0 Cmd */
	uint64_t n2p0_o                       : 1;  /**< BIST Status for N2P Port0 Data */
	uint64_t n2p1_c                       : 1;  /**< BIST Status for N2P Port1 Cmd */
	uint64_t n2p1_o                       : 1;  /**< BIST Status for N2P Port1 Data */
	uint64_t cpl_p0                       : 1;  /**< BIST Status for CPL Port 0 */
	uint64_t cpl_p1                       : 1;  /**< BIST Status for CPL Port 1 */
	uint64_t p2n1_po                      : 1;  /**< BIST Status for P2N Port1 P Order */
	uint64_t p2n1_no                      : 1;  /**< BIST Status for P2N Port1 N Order */
	uint64_t p2n1_co                      : 1;  /**< BIST Status for P2N Port1 C Order */
	uint64_t p2n0_po                      : 1;  /**< BIST Status for P2N Port0 P Order */
	uint64_t p2n0_no                      : 1;  /**< BIST Status for P2N Port0 N Order */
	uint64_t p2n0_co                      : 1;  /**< BIST Status for P2N Port0 C Order */
	uint64_t p2n0_c0                      : 1;  /**< BIST Status for P2N Port0 C0 */
	uint64_t p2n0_c1                      : 1;  /**< BIST Status for P2N Port0 C1 */
	uint64_t p2n0_n                       : 1;  /**< BIST Status for P2N Port0 N */
	uint64_t p2n0_p0                      : 1;  /**< BIST Status for P2N Port0 P0 */
	uint64_t p2n0_p1                      : 1;  /**< BIST Status for P2N Port0 P1 */
	uint64_t p2n1_c0                      : 1;  /**< BIST Status for P2N Port1 C0 */
	uint64_t p2n1_c1                      : 1;  /**< BIST Status for P2N Port1 C1 */
	uint64_t p2n1_n                       : 1;  /**< BIST Status for P2N Port1 N */
	uint64_t p2n1_p0                      : 1;  /**< BIST Status for P2N Port1 P0 */
	uint64_t p2n1_p1                      : 1;  /**< BIST Status for P2N Port1 P1 */
	uint64_t csm0                         : 1;  /**< BIST Status for CSM0 */
	uint64_t csm1                         : 1;  /**< BIST Status for CSM1 */
	uint64_t dif0                         : 1;  /**< BIST Status for DMA Instr0 */
	uint64_t dif1                         : 1;  /**< BIST Status for DMA Instr0 */
	uint64_t dif2                         : 1;  /**< BIST Status for DMA Instr0 */
	uint64_t dif3                         : 1;  /**< BIST Status for DMA Instr0 */
	uint64_t dif4                         : 1;  /**< BIST Status for DMA Instr0 */
	uint64_t msi                          : 1;  /**< BIST Status for MSI Memory Map */
	uint64_t ncb_cmd                      : 1;  /**< BIST Status for NCB Outbound Commands */
#else
	uint64_t ncb_cmd                      : 1;
	uint64_t msi                          : 1;
	uint64_t dif4                         : 1;
	uint64_t dif3                         : 1;
	uint64_t dif2                         : 1;
	uint64_t dif1                         : 1;
	uint64_t dif0                         : 1;
	uint64_t csm1                         : 1;
	uint64_t csm0                         : 1;
	uint64_t p2n1_p1                      : 1;
	uint64_t p2n1_p0                      : 1;
	uint64_t p2n1_n                       : 1;
	uint64_t p2n1_c1                      : 1;
	uint64_t p2n1_c0                      : 1;
	uint64_t p2n0_p1                      : 1;
	uint64_t p2n0_p0                      : 1;
	uint64_t p2n0_n                       : 1;
	uint64_t p2n0_c1                      : 1;
	uint64_t p2n0_c0                      : 1;
	uint64_t p2n0_co                      : 1;
	uint64_t p2n0_no                      : 1;
	uint64_t p2n0_po                      : 1;
	uint64_t p2n1_co                      : 1;
	uint64_t p2n1_no                      : 1;
	uint64_t p2n1_po                      : 1;
	uint64_t cpl_p1                       : 1;
	uint64_t cpl_p0                       : 1;
	uint64_t n2p1_o                       : 1;
	uint64_t n2p1_c                       : 1;
	uint64_t n2p0_o                       : 1;
	uint64_t n2p0_c                       : 1;
	uint64_t d4_pst                       : 1;
	uint64_t d3_pst                       : 1;
	uint64_t d2_pst                       : 1;
	uint64_t d1_pst                       : 1;
	uint64_t d0_pst                       : 1;
	uint64_t reserved_36_39               : 4;
	uint64_t ds_mem                       : 1;
	uint64_t d4_mem                       : 1;
	uint64_t d3_mem                       : 1;
	uint64_t d2_mem                       : 1;
	uint64_t d1_mem                       : 1;
	uint64_t d0_mem                       : 1;
	uint64_t pkt_pop1                     : 1;
	uint64_t pkt_pop0                     : 1;
	uint64_t reserved_48_49               : 2;
	uint64_t pkt_pof                      : 1;
	uint64_t pkt_pfm                      : 1;
	uint64_t pkt_imem                     : 1;
	uint64_t pcsr_sl                      : 1;
	uint64_t pcsr_id                      : 1;
	uint64_t pcsr_cnt                     : 1;
	uint64_t pcsr_im                      : 1;
	uint64_t pcsr_int                     : 1;
	uint64_t pkt_pif                      : 1;
	uint64_t pcr_gim                      : 1;
	uint64_t reserved_60_62               : 3;
	uint64_t pkt_rdf                      : 1;
#endif
	} cn52xx;
	struct cvmx_npei_bist_status_cn52xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_46_63               : 18;
	uint64_t d0_mem0                      : 1;  /**< BIST Status for DMA0 Memory */
	uint64_t d1_mem1                      : 1;  /**< BIST Status for DMA1 Memory */
	uint64_t d2_mem2                      : 1;  /**< BIST Status for DMA2 Memory */
	uint64_t d3_mem3                      : 1;  /**< BIST Status for DMA3 Memory */
	uint64_t dr0_mem                      : 1;  /**< BIST Status for DMA0 Store */
	uint64_t d0_mem                       : 1;  /**< BIST Status for DMA0 Memory */
	uint64_t d1_mem                       : 1;  /**< BIST Status for DMA1 Memory */
	uint64_t d2_mem                       : 1;  /**< BIST Status for DMA2 Memory */
	uint64_t d3_mem                       : 1;  /**< BIST Status for DMA3 Memory */
	uint64_t dr1_mem                      : 1;  /**< BIST Status for DMA1 Store */
	uint64_t d0_pst                       : 1;  /**< BIST Status for DMA0 Pcie Store */
	uint64_t d1_pst                       : 1;  /**< BIST Status for DMA1 Pcie Store */
	uint64_t d2_pst                       : 1;  /**< BIST Status for DMA2 Pcie Store */
	uint64_t d3_pst                       : 1;  /**< BIST Status for DMA3 Pcie Store */
	uint64_t dr2_mem                      : 1;  /**< BIST Status for DMA2 Store */
	uint64_t n2p0_c                       : 1;  /**< BIST Status for N2P Port0 Cmd */
	uint64_t n2p0_o                       : 1;  /**< BIST Status for N2P Port0 Data */
	uint64_t n2p1_c                       : 1;  /**< BIST Status for N2P Port1 Cmd */
	uint64_t n2p1_o                       : 1;  /**< BIST Status for N2P Port1 Data */
	uint64_t cpl_p0                       : 1;  /**< BIST Status for CPL Port 0 */
	uint64_t cpl_p1                       : 1;  /**< BIST Status for CPL Port 1 */
	uint64_t p2n1_po                      : 1;  /**< BIST Status for P2N Port1 P Order */
	uint64_t p2n1_no                      : 1;  /**< BIST Status for P2N Port1 N Order */
	uint64_t p2n1_co                      : 1;  /**< BIST Status for P2N Port1 C Order */
	uint64_t p2n0_po                      : 1;  /**< BIST Status for P2N Port0 P Order */
	uint64_t p2n0_no                      : 1;  /**< BIST Status for P2N Port0 N Order */
	uint64_t p2n0_co                      : 1;  /**< BIST Status for P2N Port0 C Order */
	uint64_t p2n0_c0                      : 1;  /**< BIST Status for P2N Port0 C0 */
	uint64_t p2n0_c1                      : 1;  /**< BIST Status for P2N Port0 C1 */
	uint64_t p2n0_n                       : 1;  /**< BIST Status for P2N Port0 N */
	uint64_t p2n0_p0                      : 1;  /**< BIST Status for P2N Port0 P0 */
	uint64_t p2n0_p1                      : 1;  /**< BIST Status for P2N Port0 P1 */
	uint64_t p2n1_c0                      : 1;  /**< BIST Status for P2N Port1 C0 */
	uint64_t p2n1_c1                      : 1;  /**< BIST Status for P2N Port1 C1 */
	uint64_t p2n1_n                       : 1;  /**< BIST Status for P2N Port1 N */
	uint64_t p2n1_p0                      : 1;  /**< BIST Status for P2N Port1 P0 */
	uint64_t p2n1_p1                      : 1;  /**< BIST Status for P2N Port1 P1 */
	uint64_t csm0                         : 1;  /**< BIST Status for CSM0 */
	uint64_t csm1                         : 1;  /**< BIST Status for CSM1 */
	uint64_t dif0                         : 1;  /**< BIST Status for DMA Instr0 */
	uint64_t dif1                         : 1;  /**< BIST Status for DMA Instr0 */
	uint64_t dif2                         : 1;  /**< BIST Status for DMA Instr0 */
	uint64_t dif3                         : 1;  /**< BIST Status for DMA Instr0 */
	uint64_t dr3_mem                      : 1;  /**< BIST Status for DMA3 Store */
	uint64_t msi                          : 1;  /**< BIST Status for MSI Memory Map */
	uint64_t ncb_cmd                      : 1;  /**< BIST Status for NCB Outbound Commands */
#else
	uint64_t ncb_cmd                      : 1;
	uint64_t msi                          : 1;
	uint64_t dr3_mem                      : 1;
	uint64_t dif3                         : 1;
	uint64_t dif2                         : 1;
	uint64_t dif1                         : 1;
	uint64_t dif0                         : 1;
	uint64_t csm1                         : 1;
	uint64_t csm0                         : 1;
	uint64_t p2n1_p1                      : 1;
	uint64_t p2n1_p0                      : 1;
	uint64_t p2n1_n                       : 1;
	uint64_t p2n1_c1                      : 1;
	uint64_t p2n1_c0                      : 1;
	uint64_t p2n0_p1                      : 1;
	uint64_t p2n0_p0                      : 1;
	uint64_t p2n0_n                       : 1;
	uint64_t p2n0_c1                      : 1;
	uint64_t p2n0_c0                      : 1;
	uint64_t p2n0_co                      : 1;
	uint64_t p2n0_no                      : 1;
	uint64_t p2n0_po                      : 1;
	uint64_t p2n1_co                      : 1;
	uint64_t p2n1_no                      : 1;
	uint64_t p2n1_po                      : 1;
	uint64_t cpl_p1                       : 1;
	uint64_t cpl_p0                       : 1;
	uint64_t n2p1_o                       : 1;
	uint64_t n2p1_c                       : 1;
	uint64_t n2p0_o                       : 1;
	uint64_t n2p0_c                       : 1;
	uint64_t dr2_mem                      : 1;
	uint64_t d3_pst                       : 1;
	uint64_t d2_pst                       : 1;
	uint64_t d1_pst                       : 1;
	uint64_t d0_pst                       : 1;
	uint64_t dr1_mem                      : 1;
	uint64_t d3_mem                       : 1;
	uint64_t d2_mem                       : 1;
	uint64_t d1_mem                       : 1;
	uint64_t d0_mem                       : 1;
	uint64_t dr0_mem                      : 1;
	uint64_t d3_mem3                      : 1;
	uint64_t d2_mem2                      : 1;
	uint64_t d1_mem1                      : 1;
	uint64_t d0_mem0                      : 1;
	uint64_t reserved_46_63               : 18;
#endif
	} cn52xxp1;
	struct cvmx_npei_bist_status_cn52xx   cn56xx;
	struct cvmx_npei_bist_status_cn56xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_58_63               : 6;
	uint64_t pcsr_int                     : 1;  /**< BIST Status for PKT pout_int_bstatus */
	uint64_t pcsr_im                      : 1;  /**< BIST Status for PKT pcsr_instr_mem_bstatus */
	uint64_t pcsr_cnt                     : 1;  /**< BIST Status for PKT pin_cnt_bstatus */
	uint64_t pcsr_id                      : 1;  /**< BIST Status for PKT pcsr_in_done_bstatus */
	uint64_t pcsr_sl                      : 1;  /**< BIST Status for PKT pcsr_slist_bstatus */
	uint64_t pkt_pout                     : 1;  /**< BIST Status for PKT OUT Count MEM */
	uint64_t pkt_imem                     : 1;  /**< BIST Status for PKT Instruction MEM */
	uint64_t pkt_cntm                     : 1;  /**< BIST Status for PKT Count MEM */
	uint64_t pkt_ind                      : 1;  /**< BIST Status for PKT Instruction Done MEM */
	uint64_t pkt_slm                      : 1;  /**< BIST Status for PKT SList MEM */
	uint64_t pkt_odf                      : 1;  /**< BIST Status for PKT Output Data FIFO */
	uint64_t pkt_oif                      : 1;  /**< BIST Status for PKT Output INFO FIFO */
	uint64_t pkt_out                      : 1;  /**< BIST Status for PKT Output FIFO */
	uint64_t pkt_i0                       : 1;  /**< BIST Status for PKT Instr0 */
	uint64_t pkt_i1                       : 1;  /**< BIST Status for PKT Instr1 */
	uint64_t pkt_s0                       : 1;  /**< BIST Status for PKT Slist0 */
	uint64_t pkt_s1                       : 1;  /**< BIST Status for PKT Slist1 */
	uint64_t d0_mem                       : 1;  /**< BIST Status for DMA0 Memory */
	uint64_t d1_mem                       : 1;  /**< BIST Status for DMA1 Memory */
	uint64_t d2_mem                       : 1;  /**< BIST Status for DMA2 Memory */
	uint64_t d3_mem                       : 1;  /**< BIST Status for DMA3 Memory */
	uint64_t d4_mem                       : 1;  /**< BIST Status for DMA4 Memory */
	uint64_t d0_pst                       : 1;  /**< BIST Status for DMA0 Pcie Store */
	uint64_t d1_pst                       : 1;  /**< BIST Status for DMA1 Pcie Store */
	uint64_t d2_pst                       : 1;  /**< BIST Status for DMA2 Pcie Store */
	uint64_t d3_pst                       : 1;  /**< BIST Status for DMA3 Pcie Store */
	uint64_t d4_pst                       : 1;  /**< BIST Status for DMA4 Pcie Store */
	uint64_t n2p0_c                       : 1;  /**< BIST Status for N2P Port0 Cmd */
	uint64_t n2p0_o                       : 1;  /**< BIST Status for N2P Port0 Data */
	uint64_t n2p1_c                       : 1;  /**< BIST Status for N2P Port1 Cmd */
	uint64_t n2p1_o                       : 1;  /**< BIST Status for N2P Port1 Data */
	uint64_t cpl_p0                       : 1;  /**< BIST Status for CPL Port 0 */
	uint64_t cpl_p1                       : 1;  /**< BIST Status for CPL Port 1 */
	uint64_t p2n1_po                      : 1;  /**< BIST Status for P2N Port1 P Order */
	uint64_t p2n1_no                      : 1;  /**< BIST Status for P2N Port1 N Order */
	uint64_t p2n1_co                      : 1;  /**< BIST Status for P2N Port1 C Order */
	uint64_t p2n0_po                      : 1;  /**< BIST Status for P2N Port0 P Order */
	uint64_t p2n0_no                      : 1;  /**< BIST Status for P2N Port0 N Order */
	uint64_t p2n0_co                      : 1;  /**< BIST Status for P2N Port0 C Order */
	uint64_t p2n0_c0                      : 1;  /**< BIST Status for P2N Port0 C0 */
	uint64_t p2n0_c1                      : 1;  /**< BIST Status for P2N Port0 C1 */
	uint64_t p2n0_n                       : 1;  /**< BIST Status for P2N Port0 N */
	uint64_t p2n0_p0                      : 1;  /**< BIST Status for P2N Port0 P0 */
	uint64_t p2n0_p1                      : 1;  /**< BIST Status for P2N Port0 P1 */
	uint64_t p2n1_c0                      : 1;  /**< BIST Status for P2N Port1 C0 */
	uint64_t p2n1_c1                      : 1;  /**< BIST Status for P2N Port1 C1 */
	uint64_t p2n1_n                       : 1;  /**< BIST Status for P2N Port1 N */
	uint64_t p2n1_p0                      : 1;  /**< BIST Status for P2N Port1 P0 */
	uint64_t p2n1_p1                      : 1;  /**< BIST Status for P2N Port1 P1 */
	uint64_t csm0                         : 1;  /**< BIST Status for CSM0 */
	uint64_t csm1                         : 1;  /**< BIST Status for CSM1 */
	uint64_t dif0                         : 1;  /**< BIST Status for DMA Instr0 */
	uint64_t dif1                         : 1;  /**< BIST Status for DMA Instr0 */
	uint64_t dif2                         : 1;  /**< BIST Status for DMA Instr0 */
	uint64_t dif3                         : 1;  /**< BIST Status for DMA Instr0 */
	uint64_t dif4                         : 1;  /**< BIST Status for DMA Instr0 */
	uint64_t msi                          : 1;  /**< BIST Status for MSI Memory Map */
	uint64_t ncb_cmd                      : 1;  /**< BIST Status for NCB Outbound Commands */
#else
	uint64_t ncb_cmd                      : 1;
	uint64_t msi                          : 1;
	uint64_t dif4                         : 1;
	uint64_t dif3                         : 1;
	uint64_t dif2                         : 1;
	uint64_t dif1                         : 1;
	uint64_t dif0                         : 1;
	uint64_t csm1                         : 1;
	uint64_t csm0                         : 1;
	uint64_t p2n1_p1                      : 1;
	uint64_t p2n1_p0                      : 1;
	uint64_t p2n1_n                       : 1;
	uint64_t p2n1_c1                      : 1;
	uint64_t p2n1_c0                      : 1;
	uint64_t p2n0_p1                      : 1;
	uint64_t p2n0_p0                      : 1;
	uint64_t p2n0_n                       : 1;
	uint64_t p2n0_c1                      : 1;
	uint64_t p2n0_c0                      : 1;
	uint64_t p2n0_co                      : 1;
	uint64_t p2n0_no                      : 1;
	uint64_t p2n0_po                      : 1;
	uint64_t p2n1_co                      : 1;
	uint64_t p2n1_no                      : 1;
	uint64_t p2n1_po                      : 1;
	uint64_t cpl_p1                       : 1;
	uint64_t cpl_p0                       : 1;
	uint64_t n2p1_o                       : 1;
	uint64_t n2p1_c                       : 1;
	uint64_t n2p0_o                       : 1;
	uint64_t n2p0_c                       : 1;
	uint64_t d4_pst                       : 1;
	uint64_t d3_pst                       : 1;
	uint64_t d2_pst                       : 1;
	uint64_t d1_pst                       : 1;
	uint64_t d0_pst                       : 1;
	uint64_t d4_mem                       : 1;
	uint64_t d3_mem                       : 1;
	uint64_t d2_mem                       : 1;
	uint64_t d1_mem                       : 1;
	uint64_t d0_mem                       : 1;
	uint64_t pkt_s1                       : 1;
	uint64_t pkt_s0                       : 1;
	uint64_t pkt_i1                       : 1;
	uint64_t pkt_i0                       : 1;
	uint64_t pkt_out                      : 1;
	uint64_t pkt_oif                      : 1;
	uint64_t pkt_odf                      : 1;
	uint64_t pkt_slm                      : 1;
	uint64_t pkt_ind                      : 1;
	uint64_t pkt_cntm                     : 1;
	uint64_t pkt_imem                     : 1;
	uint64_t pkt_pout                     : 1;
	uint64_t pcsr_sl                      : 1;
	uint64_t pcsr_id                      : 1;
	uint64_t pcsr_cnt                     : 1;
	uint64_t pcsr_im                      : 1;
	uint64_t pcsr_int                     : 1;
	uint64_t reserved_58_63               : 6;
#endif
	} cn56xxp1;
};
typedef union cvmx_npei_bist_status cvmx_npei_bist_status_t;

/**
 * cvmx_npei_bist_status2
 *
 * NPEI_BIST_STATUS2 = NPI's BIST Status Register2
 *
 * Results from BIST runs of NPEI's memories.
 */
union cvmx_npei_bist_status2 {
	uint64_t u64;
	struct cvmx_npei_bist_status2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_14_63               : 50;
	uint64_t prd_tag                      : 1;  /**< BIST Status for DMA PCIE RD Tag MEM */
	uint64_t prd_st0                      : 1;  /**< BIST Status for DMA PCIE RD state MEM 0 */
	uint64_t prd_st1                      : 1;  /**< BIST Status for DMA PCIE RD state MEM 1 */
	uint64_t prd_err                      : 1;  /**< BIST Status for DMA PCIE RD ERR state MEM */
	uint64_t nrd_st                       : 1;  /**< BIST Status for DMA L2C RD state MEM */
	uint64_t nwe_st                       : 1;  /**< BIST Status for DMA L2C WR state MEM */
	uint64_t nwe_wr0                      : 1;  /**< BIST Status for DMA L2C WR MEM 0 */
	uint64_t nwe_wr1                      : 1;  /**< BIST Status for DMA L2C WR MEM 1 */
	uint64_t pkt_rd                       : 1;  /**< BIST Status for Inbound PKT MEM */
	uint64_t psc_p0                       : 1;  /**< BIST Status for PSC TLP 0 MEM */
	uint64_t psc_p1                       : 1;  /**< BIST Status for PSC TLP 1 MEM */
	uint64_t pkt_gd                       : 1;  /**< BIST Status for PKT OUTB Gather Data FIFO */
	uint64_t pkt_gl                       : 1;  /**< BIST Status for PKT_OUTB Gather List FIFO */
	uint64_t pkt_blk                      : 1;  /**< BIST Status for PKT OUTB Blocked FIFO */
#else
	uint64_t pkt_blk                      : 1;
	uint64_t pkt_gl                       : 1;
	uint64_t pkt_gd                       : 1;
	uint64_t psc_p1                       : 1;
	uint64_t psc_p0                       : 1;
	uint64_t pkt_rd                       : 1;
	uint64_t nwe_wr1                      : 1;
	uint64_t nwe_wr0                      : 1;
	uint64_t nwe_st                       : 1;
	uint64_t nrd_st                       : 1;
	uint64_t prd_err                      : 1;
	uint64_t prd_st1                      : 1;
	uint64_t prd_st0                      : 1;
	uint64_t prd_tag                      : 1;
	uint64_t reserved_14_63               : 50;
#endif
	} s;
	struct cvmx_npei_bist_status2_s       cn52xx;
	struct cvmx_npei_bist_status2_s       cn56xx;
};
typedef union cvmx_npei_bist_status2 cvmx_npei_bist_status2_t;

/**
 * cvmx_npei_ctl_port0
 *
 * NPEI_CTL_PORT0 = NPEI's Control Port 0
 *
 * Contains control for access for Port0
 */
union cvmx_npei_ctl_port0 {
	uint64_t u64;
	struct cvmx_npei_ctl_port0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_21_63               : 43;
	uint64_t waitl_com                    : 1;  /**< When set '1' casues the NPI to wait for a commit
                                                         from the L2C before sending additional completions
                                                         to the L2C from the PCIe.
                                                         Set this for more conservative behavior. Clear
                                                         this for more aggressive, higher-performance
                                                         behavior */
	uint64_t intd                         : 1;  /**< When '0' Intd wire asserted. Before mapping. */
	uint64_t intc                         : 1;  /**< When '0' Intc wire asserted. Before mapping. */
	uint64_t intb                         : 1;  /**< When '0' Intb wire asserted. Before mapping. */
	uint64_t inta                         : 1;  /**< When '0' Inta wire asserted. Before mapping. */
	uint64_t intd_map                     : 2;  /**< Maps INTD to INTA(00), INTB(01), INTC(10) or
                                                         INTD (11). */
	uint64_t intc_map                     : 2;  /**< Maps INTC to INTA(00), INTB(01), INTC(10) or
                                                         INTD (11). */
	uint64_t intb_map                     : 2;  /**< Maps INTB to INTA(00), INTB(01), INTC(10) or
                                                         INTD (11). */
	uint64_t inta_map                     : 2;  /**< Maps INTA to INTA(00), INTB(01), INTC(10) or
                                                         INTD (11). */
	uint64_t ctlp_ro                      : 1;  /**< Relaxed ordering enable for Completion TLPS. */
	uint64_t reserved_6_6                 : 1;
	uint64_t ptlp_ro                      : 1;  /**< Relaxed ordering enable for Posted TLPS. */
	uint64_t bar2_enb                     : 1;  /**< When set '1' BAR2 is enable and will respond when
                                                         clear '0' BAR2 access will cause UR responses. */
	uint64_t bar2_esx                     : 2;  /**< Value will be XORed with pci-address[37:36] to
                                                         determine the endian swap mode. */
	uint64_t bar2_cax                     : 1;  /**< Value will be XORed with pcie-address[38] to
                                                         determine the L2 cache attribute.
                                                         Not cached in L2 if XOR result is 1 */
	uint64_t wait_com                     : 1;  /**< When set '1' casues the NPI to wait for a commit
                                                         from the L2C before sending additional stores to
                                                         the L2C from the PCIe.
                                                         Most applications will not notice a difference, so
                                                         should not set this bit. Setting the bit is more
                                                         conservative on ordering, lower performance */
#else
	uint64_t wait_com                     : 1;
	uint64_t bar2_cax                     : 1;
	uint64_t bar2_esx                     : 2;
	uint64_t bar2_enb                     : 1;
	uint64_t ptlp_ro                      : 1;
	uint64_t reserved_6_6                 : 1;
	uint64_t ctlp_ro                      : 1;
	uint64_t inta_map                     : 2;
	uint64_t intb_map                     : 2;
	uint64_t intc_map                     : 2;
	uint64_t intd_map                     : 2;
	uint64_t inta                         : 1;
	uint64_t intb                         : 1;
	uint64_t intc                         : 1;
	uint64_t intd                         : 1;
	uint64_t waitl_com                    : 1;
	uint64_t reserved_21_63               : 43;
#endif
	} s;
	struct cvmx_npei_ctl_port0_s          cn52xx;
	struct cvmx_npei_ctl_port0_s          cn52xxp1;
	struct cvmx_npei_ctl_port0_s          cn56xx;
	struct cvmx_npei_ctl_port0_s          cn56xxp1;
};
typedef union cvmx_npei_ctl_port0 cvmx_npei_ctl_port0_t;

/**
 * cvmx_npei_ctl_port1
 *
 * NPEI_CTL_PORT1 = NPEI's Control Port1
 *
 * Contains control for access for Port1
 */
union cvmx_npei_ctl_port1 {
	uint64_t u64;
	struct cvmx_npei_ctl_port1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_21_63               : 43;
	uint64_t waitl_com                    : 1;  /**< When set '1' casues the NPI to wait for a commit
                                                         from the L2C before sending additional completions
                                                         to the L2C from the PCIe.
                                                         Set this for more conservative behavior. Clear
                                                         this for more aggressive, higher-performance */
	uint64_t intd                         : 1;  /**< When '0' Intd wire asserted. Before mapping. */
	uint64_t intc                         : 1;  /**< When '0' Intc wire asserted. Before mapping. */
	uint64_t intb                         : 1;  /**< When '0' Intv wire asserted. Before mapping. */
	uint64_t inta                         : 1;  /**< When '0' Inta wire asserted. Before mapping. */
	uint64_t intd_map                     : 2;  /**< Maps INTD to INTA(00), INTB(01), INTC(10) or
                                                         INTD (11). */
	uint64_t intc_map                     : 2;  /**< Maps INTC to INTA(00), INTB(01), INTC(10) or
                                                         INTD (11). */
	uint64_t intb_map                     : 2;  /**< Maps INTB to INTA(00), INTB(01), INTC(10) or
                                                         INTD (11). */
	uint64_t inta_map                     : 2;  /**< Maps INTA to INTA(00), INTB(01), INTC(10) or
                                                         INTD (11). */
	uint64_t ctlp_ro                      : 1;  /**< Relaxed ordering enable for Completion TLPS. */
	uint64_t reserved_6_6                 : 1;
	uint64_t ptlp_ro                      : 1;  /**< Relaxed ordering enable for Posted TLPS. */
	uint64_t bar2_enb                     : 1;  /**< When set '1' BAR2 is enable and will respond when
                                                         clear '0' BAR2 access will cause UR responses. */
	uint64_t bar2_esx                     : 2;  /**< Value will be XORed with pci-address[37:36] to
                                                         determine the endian swap mode. */
	uint64_t bar2_cax                     : 1;  /**< Value will be XORed with pcie-address[38] to
                                                         determine the L2 cache attribute.
                                                         Not cached in L2 if XOR result is 1 */
	uint64_t wait_com                     : 1;  /**< When set '1' casues the NPI to wait for a commit
                                                         from the L2C before sending additional stores to
                                                         the L2C from the PCIe.
                                                         Most applications will not notice a difference, so
                                                         should not set this bit. Setting the bit is more
                                                         conservative on ordering, lower performance */
#else
	uint64_t wait_com                     : 1;
	uint64_t bar2_cax                     : 1;
	uint64_t bar2_esx                     : 2;
	uint64_t bar2_enb                     : 1;
	uint64_t ptlp_ro                      : 1;
	uint64_t reserved_6_6                 : 1;
	uint64_t ctlp_ro                      : 1;
	uint64_t inta_map                     : 2;
	uint64_t intb_map                     : 2;
	uint64_t intc_map                     : 2;
	uint64_t intd_map                     : 2;
	uint64_t inta                         : 1;
	uint64_t intb                         : 1;
	uint64_t intc                         : 1;
	uint64_t intd                         : 1;
	uint64_t waitl_com                    : 1;
	uint64_t reserved_21_63               : 43;
#endif
	} s;
	struct cvmx_npei_ctl_port1_s          cn52xx;
	struct cvmx_npei_ctl_port1_s          cn52xxp1;
	struct cvmx_npei_ctl_port1_s          cn56xx;
	struct cvmx_npei_ctl_port1_s          cn56xxp1;
};
typedef union cvmx_npei_ctl_port1 cvmx_npei_ctl_port1_t;

/**
 * cvmx_npei_ctl_status
 *
 * NPEI_CTL_STATUS = NPEI Control Status Register
 *
 * Contains control and status for NPEI. Writes to this register are not oSrdered with writes/reads to the PCIe Memory space.
 * To ensure that a write has completed the user must read the register before making an access(i.e. PCIe memory space)
 * that requires the value of this register to be updated.
 */
union cvmx_npei_ctl_status {
	uint64_t u64;
	struct cvmx_npei_ctl_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_44_63               : 20;
	uint64_t p1_ntags                     : 6;  /**< Number of tags avaiable for PCIe Port1.
                                                         In RC mode 1 tag is needed for each outbound TLP
                                                         that requires a CPL TLP. In Endpoint mode the
                                                         number of tags required for a TLP request is
                                                         1 per 64-bytes of CPL data + 1.
                                                         This field should only be written as part of
                                                         reset sequence, before issuing any reads, CFGs, or
                                                         IO transactions from the core(s). */
	uint64_t p0_ntags                     : 6;  /**< Number of tags avaiable for PCIe Port0.
                                                         In RC mode 1 tag is needed for each outbound TLP
                                                         that requires a CPL TLP. In Endpoint mode the
                                                         number of tags required for a TLP request is
                                                         1 per 64-bytes of CPL data + 1.
                                                         This field should only be written as part of
                                                         reset sequence, before issuing any reads, CFGs, or
                                                         IO transactions from the core(s). */
	uint64_t cfg_rtry                     : 16; /**< The time x 0x10000 in core clocks to wait for a
                                                         CPL to a CFG RD that does not carry a Retry Status.
                                                         Until such time that the timeout occurs and Retry
                                                         Status is received for a CFG RD, the Read CFG Read
                                                         will be resent. A value of 0 disables retries and
                                                         treats a CPL Retry as a CPL UR. */
	uint64_t ring_en                      : 1;  /**< When '0' forces "relative Q position" received
                                                         from PKO to be zero, and replicates the back-
                                                         pressure indication for the first ring attached
                                                         to a PKO port across all the rings attached to a
                                                         PKO port.  When '1' backpressure is on a per
                                                         port/ring. */
	uint64_t lnk_rst                      : 1;  /**< Set when PCIe Core 0 request a link reset due to
                                                         link down state. This bit is only reset on raw
                                                         reset so it can be read for state to determine if
                                                         a reset occured. Bit is cleared when a '1' is
                                                         written to this field. */
	uint64_t arb                          : 1;  /**< PCIe switch arbitration mode. '0' == fixed priority
                                                         NPEI, PCIe0, then PCIe1. '1' == round robin. */
	uint64_t pkt_bp                       : 4;  /**< Unused */
	uint64_t host_mode                    : 1;  /**< Host mode */
	uint64_t chip_rev                     : 8;  /**< The chip revision. */
#else
	uint64_t chip_rev                     : 8;
	uint64_t host_mode                    : 1;
	uint64_t pkt_bp                       : 4;
	uint64_t arb                          : 1;
	uint64_t lnk_rst                      : 1;
	uint64_t ring_en                      : 1;
	uint64_t cfg_rtry                     : 16;
	uint64_t p0_ntags                     : 6;
	uint64_t p1_ntags                     : 6;
	uint64_t reserved_44_63               : 20;
#endif
	} s;
	struct cvmx_npei_ctl_status_s         cn52xx;
	struct cvmx_npei_ctl_status_cn52xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_44_63               : 20;
	uint64_t p1_ntags                     : 6;  /**< Number of tags avaiable for PCIe Port1.
                                                         In RC mode 1 tag is needed for each outbound TLP
                                                         that requires a CPL TLP. In Endpoint mode the
                                                         number of tags required for a TLP request is
                                                         1 per 64-bytes of CPL data + 1.
                                                         This field should only be written as part of
                                                         reset sequence, before issuing any reads, CFGs, or
                                                         IO transactions from the core(s). */
	uint64_t p0_ntags                     : 6;  /**< Number of tags avaiable for PCIe Port0.
                                                         In RC mode 1 tag is needed for each outbound TLP
                                                         that requires a CPL TLP. In Endpoint mode the
                                                         number of tags required for a TLP request is
                                                         1 per 64-bytes of CPL data + 1.
                                                         This field should only be written as part of
                                                         reset sequence, before issuing any reads, CFGs, or
                                                         IO transactions from the core(s). */
	uint64_t cfg_rtry                     : 16; /**< The time x 0x10000 in core clocks to wait for a
                                                         CPL to a CFG RD that does not carry a Retry Status.
                                                         Until such time that the timeout occurs and Retry
                                                         Status is received for a CFG RD, the Read CFG Read
                                                         will be resent. A value of 0 disables retries and
                                                         treats a CPL Retry as a CPL UR. */
	uint64_t reserved_15_15               : 1;
	uint64_t lnk_rst                      : 1;  /**< Set when PCIe Core 0 request a link reset due to
                                                         link down state. This bit is only reset on raw
                                                         reset so it can be read for state to determine if
                                                         a reset occured. Bit is cleared when a '1' is
                                                         written to this field. */
	uint64_t arb                          : 1;  /**< PCIe switch arbitration mode. '0' == fixed priority
                                                         NPEI, PCIe0, then PCIe1. '1' == round robin. */
	uint64_t reserved_9_12                : 4;
	uint64_t host_mode                    : 1;  /**< Host mode */
	uint64_t chip_rev                     : 8;  /**< The chip revision. */
#else
	uint64_t chip_rev                     : 8;
	uint64_t host_mode                    : 1;
	uint64_t reserved_9_12                : 4;
	uint64_t arb                          : 1;
	uint64_t lnk_rst                      : 1;
	uint64_t reserved_15_15               : 1;
	uint64_t cfg_rtry                     : 16;
	uint64_t p0_ntags                     : 6;
	uint64_t p1_ntags                     : 6;
	uint64_t reserved_44_63               : 20;
#endif
	} cn52xxp1;
	struct cvmx_npei_ctl_status_s         cn56xx;
	struct cvmx_npei_ctl_status_cn56xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_15_63               : 49;
	uint64_t lnk_rst                      : 1;  /**< Set when PCIe Core 0 request a link reset due to
                                                         link down state. This bit is only reset on raw
                                                         reset so it can be read for state to determine if
                                                         a reset occured. Bit is cleared when a '1' is
                                                         written to this field. */
	uint64_t arb                          : 1;  /**< PCIe switch arbitration mode. '0' == fixed priority
                                                         NPEI, PCIe0, then PCIe1. '1' == round robin. */
	uint64_t pkt_bp                       : 4;  /**< Unused */
	uint64_t host_mode                    : 1;  /**< Host mode */
	uint64_t chip_rev                     : 8;  /**< The chip revision. */
#else
	uint64_t chip_rev                     : 8;
	uint64_t host_mode                    : 1;
	uint64_t pkt_bp                       : 4;
	uint64_t arb                          : 1;
	uint64_t lnk_rst                      : 1;
	uint64_t reserved_15_63               : 49;
#endif
	} cn56xxp1;
};
typedef union cvmx_npei_ctl_status cvmx_npei_ctl_status_t;

/**
 * cvmx_npei_ctl_status2
 *
 * NPEI_CTL_STATUS2 = NPEI's Control Status2 Register
 *
 * Contains control and status for NPEI.
 * Writes to this register are not ordered with writes/reads to the PCI Memory space.
 * To ensure that a write has completed the user must read the register before
 * making an access(i.e. PCI memory space) that requires the value of this register to be updated.
 */
union cvmx_npei_ctl_status2 {
	uint64_t u64;
	struct cvmx_npei_ctl_status2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t mps                          : 1;  /**< Max Payload Size
                                                                  0  = 128B
                                                                  1  = 256B
                                                         Note: PCIE*_CFG030[MPS] must be set to the same
                                                               value for proper function. */
	uint64_t mrrs                         : 3;  /**< Max Read Request Size
                                                                 0 = 128B
                                                                 1 = 256B
                                                                 2 = 512B
                                                                 3 = 1024B
                                                                 4 = 2048B
                                                                 5 = 4096B
                                                         Note: This field must not exceed the desired
                                                               max read request size. This means this field
                                                               should not exceed PCIE*_CFG030[MRRS]. */
	uint64_t c1_w_flt                     : 1;  /**< When '1' enables the window filter for reads and
                                                         writes using the window registers.
                                                         PCIE-Port1.
                                                         Unfilter writes are:
                                                         MIO,   SubId0
                                                         MIO,   SubId7
                                                         NPEI,  SubId0
                                                         NPEI,  SubId7
                                                         POW,   SubId7
                                                         IPD,   SubId7
                                                         USBN0, SubId7
                                                         Unfiltered Reads are:
                                                         MIO,   SubId0
                                                         MIO,   SubId7
                                                         NPEI,  SubId0
                                                         NPEI,  SubId7
                                                         POW,   SubId1
                                                         POW,   SubId2
                                                         POW,   SubId3
                                                         POW,   SubId7
                                                         IPD,   SubId7
                                                         USBN0, SubId7 */
	uint64_t c0_w_flt                     : 1;  /**< When '1' enables the window filter for reads and
                                                         writes using the window registers.
                                                         PCIE-Port0.
                                                         Unfilter writes are:
                                                         MIO,   SubId0
                                                         MIO,   SubId7
                                                         NPEI,  SubId0
                                                         NPEI,  SubId7
                                                         POW,   SubId7
                                                         IPD,   SubId7
                                                         USBN0, SubId7
                                                         Unfiltered Reads are:
                                                         MIO,   SubId0
                                                         MIO,   SubId7
                                                         NPEI,  SubId0
                                                         NPEI,  SubId7
                                                         POW,   SubId1
                                                         POW,   SubId2
                                                         POW,   SubId3
                                                         POW,   SubId7
                                                         IPD,   SubId7
                                                         USBN0, SubId7 */
	uint64_t c1_b1_s                      : 3;  /**< Pcie-Port1, Bar1 Size. 1 == 64MB, 2 == 128MB,
                                                         3 == 256MB, 4 == 512MB, 5 == 1024MB, 6 == 2048MB,
                                                         0 and 7 are reserved. */
	uint64_t c0_b1_s                      : 3;  /**< Pcie-Port0, Bar1 Size. 1 == 64MB, 2 == 128MB,
                                                         3 == 256MB, 4 == 512MB, 5 == 1024MB, 6 == 2048MB,
                                                         0 and 7 are reserved. */
	uint64_t c1_wi_d                      : 1;  /**< When set '1' disables access to the Window
                                                         Registers from the PCIe-Port1. */
	uint64_t c1_b0_d                      : 1;  /**< When set '1' disables access from PCIe-Port1 to
                                                         BAR-0 address offsets: Less Than 0x270,
                                                         Greater than 0x270 AND less than 0x0520, 0x3BC0,
                                                         0x3CD0. */
	uint64_t c0_wi_d                      : 1;  /**< When set '1' disables access to the Window
                                                         Registers from the PCIe-Port0. */
	uint64_t c0_b0_d                      : 1;  /**< When set '1' disables access from PCIe-Port0 to
                                                         BAR-0 address offsets: Less Than 0x270,
                                                         Greater than 0x270 AND less than 0x0520, 0x3BC0,
                                                         0x3CD0. */
#else
	uint64_t c0_b0_d                      : 1;
	uint64_t c0_wi_d                      : 1;
	uint64_t c1_b0_d                      : 1;
	uint64_t c1_wi_d                      : 1;
	uint64_t c0_b1_s                      : 3;
	uint64_t c1_b1_s                      : 3;
	uint64_t c0_w_flt                     : 1;
	uint64_t c1_w_flt                     : 1;
	uint64_t mrrs                         : 3;
	uint64_t mps                          : 1;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_npei_ctl_status2_s        cn52xx;
	struct cvmx_npei_ctl_status2_s        cn52xxp1;
	struct cvmx_npei_ctl_status2_s        cn56xx;
	struct cvmx_npei_ctl_status2_s        cn56xxp1;
};
typedef union cvmx_npei_ctl_status2 cvmx_npei_ctl_status2_t;

/**
 * cvmx_npei_data_out_cnt
 *
 * NPEI_DATA_OUT_CNT = NPEI DATA OUT COUNT
 *
 * The EXEC data out fifo-count and the data unload counter.
 */
union cvmx_npei_data_out_cnt {
	uint64_t u64;
	struct cvmx_npei_data_out_cnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_44_63               : 20;
	uint64_t p1_ucnt                      : 16; /**< PCIE-Port1 Fifo Unload Count. This counter is
                                                         incremented by '1' every time a word is removed
                                                         from the Data Out FIFO, whose count is shown in
                                                         P0_FCNT. */
	uint64_t p1_fcnt                      : 6;  /**< PCIE-Port1 Data Out Fifo Count. Number of address
                                                         data words to be sent out the PCIe port presently
                                                         buffered in the FIFO. */
	uint64_t p0_ucnt                      : 16; /**< PCIE-Port0 Fifo Unload Count. This counter is
                                                         incremented by '1' every time a word is removed
                                                         from the Data Out FIFO, whose count is shown in
                                                         P0_FCNT. */
	uint64_t p0_fcnt                      : 6;  /**< PCIE-Port0 Data Out Fifo Count. Number of address
                                                         data words to be sent out the PCIe port presently
                                                         buffered in the FIFO. */
#else
	uint64_t p0_fcnt                      : 6;
	uint64_t p0_ucnt                      : 16;
	uint64_t p1_fcnt                      : 6;
	uint64_t p1_ucnt                      : 16;
	uint64_t reserved_44_63               : 20;
#endif
	} s;
	struct cvmx_npei_data_out_cnt_s       cn52xx;
	struct cvmx_npei_data_out_cnt_s       cn52xxp1;
	struct cvmx_npei_data_out_cnt_s       cn56xx;
	struct cvmx_npei_data_out_cnt_s       cn56xxp1;
};
typedef union cvmx_npei_data_out_cnt cvmx_npei_data_out_cnt_t;

/**
 * cvmx_npei_dbg_data
 *
 * NPEI_DBG_DATA = NPEI Debug Data Register
 *
 * Value returned on the debug-data lines from the RSLs
 */
union cvmx_npei_dbg_data {
	uint64_t u64;
	struct cvmx_npei_dbg_data_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_28_63               : 36;
	uint64_t qlm0_rev_lanes               : 1;  /**< Lane reversal for PCIe port 0 */
	uint64_t reserved_25_26               : 2;
	uint64_t qlm1_spd                     : 2;  /**< Sets the QLM1 frequency
                                                         0=1.25 Gbaud
                                                         1=2.5 Gbaud
                                                         2=3.125 Gbaud
                                                         3=3.75 Gbaud */
	uint64_t c_mul                        : 5;  /**< PLL_MUL pins sampled at DCOK assertion
                                                         Core frequency = 50MHz*C_MUL */
	uint64_t dsel_ext                     : 1;  /**< Allows changes in the external pins to set the
                                                         debug select value. */
	uint64_t data                         : 17; /**< Value on the debug data lines. */
#else
	uint64_t data                         : 17;
	uint64_t dsel_ext                     : 1;
	uint64_t c_mul                        : 5;
	uint64_t qlm1_spd                     : 2;
	uint64_t reserved_25_26               : 2;
	uint64_t qlm0_rev_lanes               : 1;
	uint64_t reserved_28_63               : 36;
#endif
	} s;
	struct cvmx_npei_dbg_data_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_29_63               : 35;
	uint64_t qlm0_link_width              : 1;  /**< Link width of PCIe port 0
                                                         0 = PCIe port 0 is 2 lanes,
                                                             2 lane PCIe port 1 exists
                                                         1 = PCIe port 0 is 4 lanes,
                                                             PCIe port 1 does not exist */
	uint64_t qlm0_rev_lanes               : 1;  /**< Lane reversal for PCIe port 0 */
	uint64_t qlm1_mode                    : 2;  /**< Sets the QLM1 Mode
                                                         0=Reserved
                                                         1=XAUI
                                                         2=SGMII
                                                         3=PICMG */
	uint64_t qlm1_spd                     : 2;  /**< Sets the QLM1 frequency
                                                         0=1.25 Gbaud
                                                         1=2.5 Gbaud
                                                         2=3.125 Gbaud
                                                         3=3.75 Gbaud */
	uint64_t c_mul                        : 5;  /**< PLL_MUL pins sampled at DCOK assertion
                                                         Core frequency = 50MHz*C_MUL */
	uint64_t dsel_ext                     : 1;  /**< Allows changes in the external pins to set the
                                                         debug select value. */
	uint64_t data                         : 17; /**< Value on the debug data lines. */
#else
	uint64_t data                         : 17;
	uint64_t dsel_ext                     : 1;
	uint64_t c_mul                        : 5;
	uint64_t qlm1_spd                     : 2;
	uint64_t qlm1_mode                    : 2;
	uint64_t qlm0_rev_lanes               : 1;
	uint64_t qlm0_link_width              : 1;
	uint64_t reserved_29_63               : 35;
#endif
	} cn52xx;
	struct cvmx_npei_dbg_data_cn52xx      cn52xxp1;
	struct cvmx_npei_dbg_data_cn56xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_29_63               : 35;
	uint64_t qlm2_rev_lanes               : 1;  /**< Lane reversal for PCIe port 1 */
	uint64_t qlm0_rev_lanes               : 1;  /**< Lane reversal for PCIe port 0 */
	uint64_t qlm3_spd                     : 2;  /**< Sets the QLM3 frequency
                                                         0=1.25 Gbaud
                                                         1=2.5 Gbaud
                                                         2=3.125 Gbaud
                                                         3=3.75 Gbaud */
	uint64_t qlm1_spd                     : 2;  /**< Sets the QLM1 frequency
                                                         0=1.25 Gbaud
                                                         1=2.5 Gbaud
                                                         2=3.125 Gbaud
                                                         3=3.75 Gbaud */
	uint64_t c_mul                        : 5;  /**< PLL_MUL pins sampled at DCOK assertion
                                                         Core frequency = 50MHz*C_MUL */
	uint64_t dsel_ext                     : 1;  /**< Allows changes in the external pins to set the
                                                         debug select value. */
	uint64_t data                         : 17; /**< Value on the debug data lines. */
#else
	uint64_t data                         : 17;
	uint64_t dsel_ext                     : 1;
	uint64_t c_mul                        : 5;
	uint64_t qlm1_spd                     : 2;
	uint64_t qlm3_spd                     : 2;
	uint64_t qlm0_rev_lanes               : 1;
	uint64_t qlm2_rev_lanes               : 1;
	uint64_t reserved_29_63               : 35;
#endif
	} cn56xx;
	struct cvmx_npei_dbg_data_cn56xx      cn56xxp1;
};
typedef union cvmx_npei_dbg_data cvmx_npei_dbg_data_t;

/**
 * cvmx_npei_dbg_select
 *
 * NPEI_DBG_SELECT = Debug Select Register
 *
 * Contains the debug select value last written to the RSLs.
 */
union cvmx_npei_dbg_select {
	uint64_t u64;
	struct cvmx_npei_dbg_select_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t dbg_sel                      : 16; /**< When this register is written its value is sent to
                                                         all RSLs. */
#else
	uint64_t dbg_sel                      : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_npei_dbg_select_s         cn52xx;
	struct cvmx_npei_dbg_select_s         cn52xxp1;
	struct cvmx_npei_dbg_select_s         cn56xx;
	struct cvmx_npei_dbg_select_s         cn56xxp1;
};
typedef union cvmx_npei_dbg_select cvmx_npei_dbg_select_t;

/**
 * cvmx_npei_dma#_counts
 *
 * NPEI_DMA[0..4]_COUNTS = DMA Instruction Counts
 *
 * Values for determing the number of instructions for DMA[0..4] in the NPEI.
 */
union cvmx_npei_dmax_counts {
	uint64_t u64;
	struct cvmx_npei_dmax_counts_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_39_63               : 25;
	uint64_t fcnt                         : 7;  /**< Number of words in the Instruction FIFO. */
	uint64_t dbell                        : 32; /**< Number of available words of Instructions to read. */
#else
	uint64_t dbell                        : 32;
	uint64_t fcnt                         : 7;
	uint64_t reserved_39_63               : 25;
#endif
	} s;
	struct cvmx_npei_dmax_counts_s        cn52xx;
	struct cvmx_npei_dmax_counts_s        cn52xxp1;
	struct cvmx_npei_dmax_counts_s        cn56xx;
	struct cvmx_npei_dmax_counts_s        cn56xxp1;
};
typedef union cvmx_npei_dmax_counts cvmx_npei_dmax_counts_t;

/**
 * cvmx_npei_dma#_dbell
 *
 * NPEI_DMA_DBELL[0..4] = DMA Door Bell
 *
 * The door bell register for DMA[0..4] queue.
 */
union cvmx_npei_dmax_dbell {
	uint32_t u32;
	struct cvmx_npei_dmax_dbell_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t reserved_16_31               : 16;
	uint32_t dbell                        : 16; /**< The value written to this register is added to the
                                                         number of 8byte words to be read and processes for
                                                         the low priority dma queue. */
#else
	uint32_t dbell                        : 16;
	uint32_t reserved_16_31               : 16;
#endif
	} s;
	struct cvmx_npei_dmax_dbell_s         cn52xx;
	struct cvmx_npei_dmax_dbell_s         cn52xxp1;
	struct cvmx_npei_dmax_dbell_s         cn56xx;
	struct cvmx_npei_dmax_dbell_s         cn56xxp1;
};
typedef union cvmx_npei_dmax_dbell cvmx_npei_dmax_dbell_t;

/**
 * cvmx_npei_dma#_ibuff_saddr
 *
 * NPEI_DMA[0..4]_IBUFF_SADDR = DMA Instruction Buffer Starting Address
 *
 * The address to start reading Instructions from for DMA[0..4].
 */
union cvmx_npei_dmax_ibuff_saddr {
	uint64_t u64;
	struct cvmx_npei_dmax_ibuff_saddr_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_37_63               : 27;
	uint64_t idle                         : 1;  /**< DMA Engine IDLE state */
	uint64_t saddr                        : 29; /**< The 128 byte aligned starting address to read the
                                                         first instruction. SADDR is address bit 35:7 of the
                                                         first instructions address. */
	uint64_t reserved_0_6                 : 7;
#else
	uint64_t reserved_0_6                 : 7;
	uint64_t saddr                        : 29;
	uint64_t idle                         : 1;
	uint64_t reserved_37_63               : 27;
#endif
	} s;
	struct cvmx_npei_dmax_ibuff_saddr_s   cn52xx;
	struct cvmx_npei_dmax_ibuff_saddr_cn52xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_36_63               : 28;
	uint64_t saddr                        : 29; /**< The 128 byte aligned starting address to read the
                                                         first instruction. SADDR is address bit 35:7 of the
                                                         first instructions address. */
	uint64_t reserved_0_6                 : 7;
#else
	uint64_t reserved_0_6                 : 7;
	uint64_t saddr                        : 29;
	uint64_t reserved_36_63               : 28;
#endif
	} cn52xxp1;
	struct cvmx_npei_dmax_ibuff_saddr_s   cn56xx;
	struct cvmx_npei_dmax_ibuff_saddr_cn52xxp1 cn56xxp1;
};
typedef union cvmx_npei_dmax_ibuff_saddr cvmx_npei_dmax_ibuff_saddr_t;

/**
 * cvmx_npei_dma#_naddr
 *
 * NPEI_DMA[0..4]_NADDR = DMA Next Ichunk Address
 *
 * Place NPEI will read the next Ichunk data from. This is valid when state is 0
 */
union cvmx_npei_dmax_naddr {
	uint64_t u64;
	struct cvmx_npei_dmax_naddr_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_36_63               : 28;
	uint64_t addr                         : 36; /**< The next L2C address to read DMA# instructions
                                                         from. */
#else
	uint64_t addr                         : 36;
	uint64_t reserved_36_63               : 28;
#endif
	} s;
	struct cvmx_npei_dmax_naddr_s         cn52xx;
	struct cvmx_npei_dmax_naddr_s         cn52xxp1;
	struct cvmx_npei_dmax_naddr_s         cn56xx;
	struct cvmx_npei_dmax_naddr_s         cn56xxp1;
};
typedef union cvmx_npei_dmax_naddr cvmx_npei_dmax_naddr_t;

/**
 * cvmx_npei_dma0_int_level
 *
 * NPEI_DMA0_INT_LEVEL = NPEI DMA0 Interrupt Level
 *
 * Thresholds for DMA count and timer interrupts for DMA0.
 */
union cvmx_npei_dma0_int_level {
	uint64_t u64;
	struct cvmx_npei_dma0_int_level_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t time                         : 32; /**< Whenever the DMA_CNT0 timer exceeds
                                                         this value, NPEI_INT_SUM[DTIME0] is set.
                                                         The DMA_CNT0 timer increments every core clock
                                                         whenever NPEI_DMA_CNTS[DMA0]!=0, and is cleared
                                                         when NPEI_INT_SUM[DTIME0] is written with one. */
	uint64_t cnt                          : 32; /**< Whenever NPEI_DMA_CNTS[DMA0] exceeds this value,
                                                         NPEI_INT_SUM[DCNT0] is set. */
#else
	uint64_t cnt                          : 32;
	uint64_t time                         : 32;
#endif
	} s;
	struct cvmx_npei_dma0_int_level_s     cn52xx;
	struct cvmx_npei_dma0_int_level_s     cn52xxp1;
	struct cvmx_npei_dma0_int_level_s     cn56xx;
	struct cvmx_npei_dma0_int_level_s     cn56xxp1;
};
typedef union cvmx_npei_dma0_int_level cvmx_npei_dma0_int_level_t;

/**
 * cvmx_npei_dma1_int_level
 *
 * NPEI_DMA1_INT_LEVEL = NPEI DMA1 Interrupt Level
 *
 * Thresholds for DMA count and timer interrupts for DMA1.
 */
union cvmx_npei_dma1_int_level {
	uint64_t u64;
	struct cvmx_npei_dma1_int_level_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t time                         : 32; /**< Whenever the DMA_CNT1 timer exceeds
                                                         this value, NPEI_INT_SUM[DTIME1] is set.
                                                         The DMA_CNT1 timer increments every core clock
                                                         whenever NPEI_DMA_CNTS[DMA1]!=0, and is cleared
                                                         when NPEI_INT_SUM[DTIME1] is written with one. */
	uint64_t cnt                          : 32; /**< Whenever NPEI_DMA_CNTS[DMA1] exceeds this value,
                                                         NPEI_INT_SUM[DCNT1] is set. */
#else
	uint64_t cnt                          : 32;
	uint64_t time                         : 32;
#endif
	} s;
	struct cvmx_npei_dma1_int_level_s     cn52xx;
	struct cvmx_npei_dma1_int_level_s     cn52xxp1;
	struct cvmx_npei_dma1_int_level_s     cn56xx;
	struct cvmx_npei_dma1_int_level_s     cn56xxp1;
};
typedef union cvmx_npei_dma1_int_level cvmx_npei_dma1_int_level_t;

/**
 * cvmx_npei_dma_cnts
 *
 * NPEI_DMA_CNTS = NPEI DMA Count
 *
 * The DMA Count values for DMA0 and DMA1.
 */
union cvmx_npei_dma_cnts {
	uint64_t u64;
	struct cvmx_npei_dma_cnts_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t dma1                         : 32; /**< The DMA counter 1.
                                                         Writing this field will cause the written value to
                                                         be subtracted from DMA1. SW should use a 4-byte
                                                         write to access this field so as not to change the
                                                         value of other fields in this register.
                                                         HW will optionally increment this field after
                                                         it completes an OUTBOUND or EXTERNAL-ONLY DMA
                                                         instruction. These increments may cause interrupts.
                                                         Refer to NPEI_DMA1_INT_LEVEL and
                                                         NPEI_INT_SUM[DCNT1,DTIME1]. */
	uint64_t dma0                         : 32; /**< The DMA counter 0.
                                                         Writing this field will cause the written value to
                                                         be subtracted from DMA0. SW should use a 4-byte
                                                         write to access this field so as not to change the
                                                         value of other fields in this register.
                                                         HW will optionally increment this field after
                                                         it completes an OUTBOUND or EXTERNAL-ONLY DMA
                                                         instruction. These increments may cause interrupts.
                                                         Refer to NPEI_DMA0_INT_LEVEL and
                                                         NPEI_INT_SUM[DCNT0,DTIME0]. */
#else
	uint64_t dma0                         : 32;
	uint64_t dma1                         : 32;
#endif
	} s;
	struct cvmx_npei_dma_cnts_s           cn52xx;
	struct cvmx_npei_dma_cnts_s           cn52xxp1;
	struct cvmx_npei_dma_cnts_s           cn56xx;
	struct cvmx_npei_dma_cnts_s           cn56xxp1;
};
typedef union cvmx_npei_dma_cnts cvmx_npei_dma_cnts_t;

/**
 * cvmx_npei_dma_control
 *
 * NPEI_DMA_CONTROL = DMA Control Register
 *
 * Controls operation of the DMA IN/OUT.
 */
union cvmx_npei_dma_control {
	uint64_t u64;
	struct cvmx_npei_dma_control_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_40_63               : 24;
	uint64_t p_32b_m                      : 1;  /**< DMA PCIE 32-bit word read disable bit
                                                         When 0, enable the feature */
	uint64_t dma4_enb                     : 1;  /**< DMA# enable. Enables the operation of the DMA
                                                         engine. After being enabled a DMA engine should not
                                                         be dis-abled while processing instructions. */
	uint64_t dma3_enb                     : 1;  /**< DMA# enable. Enables the operation of the DMA
                                                         engine. After being enabled a DMA engine should not
                                                         be dis-abled while processing instructions. */
	uint64_t dma2_enb                     : 1;  /**< DMA# enable. Enables the operation of the DMA
                                                         engine. After being enabled a DMA engine should not
                                                         be dis-abled while processing instructions. */
	uint64_t dma1_enb                     : 1;  /**< DMA# enable. Enables the operation of the DMA
                                                         engine. After being enabled a DMA engine should not
                                                         be dis-abled while processing instructions. */
	uint64_t dma0_enb                     : 1;  /**< DMA# enable. Enables the operation of the DMA
                                                         engine. After being enabled a DMA engine should not
                                                         be dis-abled while processing instructions. */
	uint64_t b0_lend                      : 1;  /**< When set '1' and the NPEI is in the mode to write
                                                         0 to L2C memory when a DMA is done, the address
                                                         to be written to will be treated as a Little
                                                         Endian address. */
	uint64_t dwb_denb                     : 1;  /**< When set '1' the NPEI will send a value in the DWB
                                                         field for a free page operation for the memory
                                                         that contained the data. */
	uint64_t dwb_ichk                     : 9;  /**< When Instruction Chunks for DMA operations are freed
                                                         this value is used for the DWB field of the
                                                         operation. */
	uint64_t fpa_que                      : 3;  /**< The FPA queue that the instruction-chunk page will
                                                         be returned to when used. */
	uint64_t o_add1                       : 1;  /**< When set '1' 1 will be added to the DMA counters,
                                                         if '0' then the number of bytes in the dma transfer
                                                         will be added to the count register. */
	uint64_t o_ro                         : 1;  /**< Relaxed Ordering Mode for DMA. */
	uint64_t o_ns                         : 1;  /**< Nosnoop For DMA. */
	uint64_t o_es                         : 2;  /**< Endian Swap Mode for DMA. */
	uint64_t o_mode                       : 1;  /**< Select PCI_POINTER MODE to be used.
                                                         '1' use pointer values for address and register
                                                         values for RO, ES, and NS, '0' use register
                                                         values for address and pointer values for
                                                         RO, ES, and NS. */
	uint64_t csize                        : 14; /**< The size in words of the DMA Instruction Chunk.
                                                         This value should only be written once. After
                                                         writing this value a new value will not be
                                                         recognized until the end of the DMA I-Chunk is
                                                         reached. */
#else
	uint64_t csize                        : 14;
	uint64_t o_mode                       : 1;
	uint64_t o_es                         : 2;
	uint64_t o_ns                         : 1;
	uint64_t o_ro                         : 1;
	uint64_t o_add1                       : 1;
	uint64_t fpa_que                      : 3;
	uint64_t dwb_ichk                     : 9;
	uint64_t dwb_denb                     : 1;
	uint64_t b0_lend                      : 1;
	uint64_t dma0_enb                     : 1;
	uint64_t dma1_enb                     : 1;
	uint64_t dma2_enb                     : 1;
	uint64_t dma3_enb                     : 1;
	uint64_t dma4_enb                     : 1;
	uint64_t p_32b_m                      : 1;
	uint64_t reserved_40_63               : 24;
#endif
	} s;
	struct cvmx_npei_dma_control_s        cn52xx;
	struct cvmx_npei_dma_control_cn52xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_38_63               : 26;
	uint64_t dma3_enb                     : 1;  /**< DMA# enable. Enables the operation of the DMA
                                                         engine. After being enabled a DMA engine should not
                                                         be dis-abled while processing instructions. */
	uint64_t dma2_enb                     : 1;  /**< DMA# enable. Enables the operation of the DMA
                                                         engine. After being enabled a DMA engine should not
                                                         be dis-abled while processing instructions. */
	uint64_t dma1_enb                     : 1;  /**< DMA# enable. Enables the operation of the DMA
                                                         engine. After being enabled a DMA engine should not
                                                         be dis-abled while processing instructions. */
	uint64_t dma0_enb                     : 1;  /**< DMA# enable. Enables the operation of the DMA
                                                         engine. After being enabled a DMA engine should not
                                                         be dis-abled while processing instructions. */
	uint64_t b0_lend                      : 1;  /**< When set '1' and the NPEI is in the mode to write
                                                         0 to L2C memory when a DMA is done, the address
                                                         to be written to will be treated as a Little
                                                         Endian address. */
	uint64_t dwb_denb                     : 1;  /**< When set '1' the NPEI will send a value in the DWB
                                                         field for a free page operation for the memory
                                                         that contained the data. */
	uint64_t dwb_ichk                     : 9;  /**< When Instruction Chunks for DMA operations are freed
                                                         this value is used for the DWB field of the
                                                         operation. */
	uint64_t fpa_que                      : 3;  /**< The FPA queue that the instruction-chunk page will
                                                         be returned to when used. */
	uint64_t o_add1                       : 1;  /**< When set '1' 1 will be added to the DMA counters,
                                                         if '0' then the number of bytes in the dma transfer
                                                         will be added to the count register. */
	uint64_t o_ro                         : 1;  /**< Relaxed Ordering Mode for DMA. */
	uint64_t o_ns                         : 1;  /**< Nosnoop For DMA. */
	uint64_t o_es                         : 2;  /**< Endian Swap Mode for DMA. */
	uint64_t o_mode                       : 1;  /**< Select PCI_POINTER MODE to be used.
                                                         '1' use pointer values for address and register
                                                         values for RO, ES, and NS, '0' use register
                                                         values for address and pointer values for
                                                         RO, ES, and NS. */
	uint64_t csize                        : 14; /**< The size in words of the DMA Instruction Chunk.
                                                         This value should only be written once. After
                                                         writing this value a new value will not be
                                                         recognized until the end of the DMA I-Chunk is
                                                         reached. */
#else
	uint64_t csize                        : 14;
	uint64_t o_mode                       : 1;
	uint64_t o_es                         : 2;
	uint64_t o_ns                         : 1;
	uint64_t o_ro                         : 1;
	uint64_t o_add1                       : 1;
	uint64_t fpa_que                      : 3;
	uint64_t dwb_ichk                     : 9;
	uint64_t dwb_denb                     : 1;
	uint64_t b0_lend                      : 1;
	uint64_t dma0_enb                     : 1;
	uint64_t dma1_enb                     : 1;
	uint64_t dma2_enb                     : 1;
	uint64_t dma3_enb                     : 1;
	uint64_t reserved_38_63               : 26;
#endif
	} cn52xxp1;
	struct cvmx_npei_dma_control_s        cn56xx;
	struct cvmx_npei_dma_control_cn56xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_39_63               : 25;
	uint64_t dma4_enb                     : 1;  /**< DMA# enable. Enables the operation of the DMA
                                                         engine. After being enabled a DMA engine should not
                                                         be dis-abled while processing instructions. */
	uint64_t dma3_enb                     : 1;  /**< DMA# enable. Enables the operation of the DMA
                                                         engine. After being enabled a DMA engine should not
                                                         be dis-abled while processing instructions. */
	uint64_t dma2_enb                     : 1;  /**< DMA# enable. Enables the operation of the DMA
                                                         engine. After being enabled a DMA engine should not
                                                         be dis-abled while processing instructions. */
	uint64_t dma1_enb                     : 1;  /**< DMA# enable. Enables the operation of the DMA
                                                         engine. After being enabled a DMA engine should not
                                                         be dis-abled while processing instructions. */
	uint64_t dma0_enb                     : 1;  /**< DMA# enable. Enables the operation of the DMA
                                                         engine. After being enabled a DMA engine should not
                                                         be dis-abled while processing instructions. */
	uint64_t b0_lend                      : 1;  /**< When set '1' and the NPEI is in the mode to write
                                                         0 to L2C memory when a DMA is done, the address
                                                         to be written to will be treated as a Little
                                                         Endian address. */
	uint64_t dwb_denb                     : 1;  /**< When set '1' the NPEI will send a value in the DWB
                                                         field for a free page operation for the memory
                                                         that contained the data. */
	uint64_t dwb_ichk                     : 9;  /**< When Instruction Chunks for DMA operations are freed
                                                         this value is used for the DWB field of the
                                                         operation. */
	uint64_t fpa_que                      : 3;  /**< The FPA queue that the instruction-chunk page will
                                                         be returned to when used. */
	uint64_t o_add1                       : 1;  /**< When set '1' 1 will be added to the DMA counters,
                                                         if '0' then the number of bytes in the dma transfer
                                                         will be added to the count register. */
	uint64_t o_ro                         : 1;  /**< Relaxed Ordering Mode for DMA. */
	uint64_t o_ns                         : 1;  /**< Nosnoop For DMA. */
	uint64_t o_es                         : 2;  /**< Endian Swap Mode for DMA. */
	uint64_t o_mode                       : 1;  /**< Select PCI_POINTER MODE to be used.
                                                         '1' use pointer values for address and register
                                                         values for RO, ES, and NS, '0' use register
                                                         values for address and pointer values for
                                                         RO, ES, and NS. */
	uint64_t csize                        : 14; /**< The size in words of the DMA Instruction Chunk.
                                                         This value should only be written once. After
                                                         writing this value a new value will not be
                                                         recognized until the end of the DMA I-Chunk is
                                                         reached. */
#else
	uint64_t csize                        : 14;
	uint64_t o_mode                       : 1;
	uint64_t o_es                         : 2;
	uint64_t o_ns                         : 1;
	uint64_t o_ro                         : 1;
	uint64_t o_add1                       : 1;
	uint64_t fpa_que                      : 3;
	uint64_t dwb_ichk                     : 9;
	uint64_t dwb_denb                     : 1;
	uint64_t b0_lend                      : 1;
	uint64_t dma0_enb                     : 1;
	uint64_t dma1_enb                     : 1;
	uint64_t dma2_enb                     : 1;
	uint64_t dma3_enb                     : 1;
	uint64_t dma4_enb                     : 1;
	uint64_t reserved_39_63               : 25;
#endif
	} cn56xxp1;
};
typedef union cvmx_npei_dma_control cvmx_npei_dma_control_t;

/**
 * cvmx_npei_dma_pcie_req_num
 *
 * NPEI_DMA_PCIE_REQ_NUM = NPEI DMA PCIE Outstanding Read Request Number
 *
 * Outstanding PCIE read request number for DMAs and Packet, maximum number is 16
 */
union cvmx_npei_dma_pcie_req_num {
	uint64_t u64;
	struct cvmx_npei_dma_pcie_req_num_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t dma_arb                      : 1;  /**< DMA_PKT Read Request Arbitration
                                                         - 1: DMA0-4 and PKT are round robin. i.e.
                                                             DMA0-DMA1-DMA2-DMA3-DMA4-PKT...
                                                         - 0: DMA0-4 are round robin, pkt gets selected
                                                             half the time. i.e.
                                                             DMA0-PKT-DMA1-PKT-DMA2-PKT-DMA3-PKT-DMA4-PKT... */
	uint64_t reserved_53_62               : 10;
	uint64_t pkt_cnt                      : 5;  /**< PKT outstanding PCIE Read Request Number for each
                                                         PCIe port
                                                         When PKT_CNT=x, for each PCIe port, the number
                                                         of outstanding PCIe memory space reads by the PCIe
                                                         packet input/output will not exceed x.
                                                         Valid Number is between 1 and 16 */
	uint64_t reserved_45_47               : 3;
	uint64_t dma4_cnt                     : 5;  /**< DMA4 outstanding PCIE Read Request Number
                                                         When DMA4_CNT=x, the number of outstanding PCIe
                                                         memory space reads by the PCIe DMA engine 4
                                                         will not exceed x.
                                                         Valid Number is between 1 and 16 */
	uint64_t reserved_37_39               : 3;
	uint64_t dma3_cnt                     : 5;  /**< DMA3 outstanding PCIE Read Request Number
                                                         When DMA3_CNT=x, the number of outstanding PCIe
                                                         memory space reads by the PCIe DMA engine 3
                                                         will not exceed x.
                                                         Valid Number is between 1 and 16 */
	uint64_t reserved_29_31               : 3;
	uint64_t dma2_cnt                     : 5;  /**< DMA2 outstanding PCIE Read Request Number
                                                         When DMA2_CNT=x, the number of outstanding PCIe
                                                         memory space reads by the PCIe DMA engine 2
                                                         will not exceed x.
                                                         Valid Number is between 1 and 16 */
	uint64_t reserved_21_23               : 3;
	uint64_t dma1_cnt                     : 5;  /**< DMA1 outstanding PCIE Read Request Number
                                                         When DMA1_CNT=x, the number of outstanding PCIe
                                                         memory space reads by the PCIe DMA engine 1
                                                         will not exceed x.
                                                         Valid Number is between 1 and 16 */
	uint64_t reserved_13_15               : 3;
	uint64_t dma0_cnt                     : 5;  /**< DMA0 outstanding PCIE Read Request Number
                                                         When DMA0_CNT=x, the number of outstanding PCIe
                                                         memory space reads by the PCIe DMA engine 0
                                                         will not exceed x.
                                                         Valid Number is between 1 and 16 */
	uint64_t reserved_5_7                 : 3;
	uint64_t dma_cnt                      : 5;  /**< Total outstanding PCIE Read Request Number for each
                                                         PCIe port
                                                         When DMA_CNT=x, for each PCIe port, the total
                                                         number of outstanding PCIe memory space reads
                                                         by the PCIe DMA engines and packet input/output
                                                         will not exceed x.
                                                         Valid Number is between 1 and 16 */
#else
	uint64_t dma_cnt                      : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t dma0_cnt                     : 5;
	uint64_t reserved_13_15               : 3;
	uint64_t dma1_cnt                     : 5;
	uint64_t reserved_21_23               : 3;
	uint64_t dma2_cnt                     : 5;
	uint64_t reserved_29_31               : 3;
	uint64_t dma3_cnt                     : 5;
	uint64_t reserved_37_39               : 3;
	uint64_t dma4_cnt                     : 5;
	uint64_t reserved_45_47               : 3;
	uint64_t pkt_cnt                      : 5;
	uint64_t reserved_53_62               : 10;
	uint64_t dma_arb                      : 1;
#endif
	} s;
	struct cvmx_npei_dma_pcie_req_num_s   cn52xx;
	struct cvmx_npei_dma_pcie_req_num_s   cn56xx;
};
typedef union cvmx_npei_dma_pcie_req_num cvmx_npei_dma_pcie_req_num_t;

/**
 * cvmx_npei_dma_state1
 *
 * NPEI_DMA_STATE1 = NPI's DMA State 1
 *
 * Results from DMA state register 1
 */
union cvmx_npei_dma_state1 {
	uint64_t u64;
	struct cvmx_npei_dma_state1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_40_63               : 24;
	uint64_t d4_dwe                       : 8;  /**< DMA4 PICe Write State */
	uint64_t d3_dwe                       : 8;  /**< DMA3 PICe Write State */
	uint64_t d2_dwe                       : 8;  /**< DMA2 PICe Write State */
	uint64_t d1_dwe                       : 8;  /**< DMA1 PICe Write State */
	uint64_t d0_dwe                       : 8;  /**< DMA0 PICe Write State */
#else
	uint64_t d0_dwe                       : 8;
	uint64_t d1_dwe                       : 8;
	uint64_t d2_dwe                       : 8;
	uint64_t d3_dwe                       : 8;
	uint64_t d4_dwe                       : 8;
	uint64_t reserved_40_63               : 24;
#endif
	} s;
	struct cvmx_npei_dma_state1_s         cn52xx;
};
typedef union cvmx_npei_dma_state1 cvmx_npei_dma_state1_t;

/**
 * cvmx_npei_dma_state1_p1
 *
 * NPEI_DMA_STATE1_P1 = NPEI DMA Request and Instruction State
 *
 * DMA engine Debug information.
 */
union cvmx_npei_dma_state1_p1 {
	uint64_t u64;
	struct cvmx_npei_dma_state1_p1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_60_63               : 4;
	uint64_t d0_difst                     : 7;  /**< DMA engine 0 dif instruction read state */
	uint64_t d1_difst                     : 7;  /**< DMA engine 1 dif instruction read state */
	uint64_t d2_difst                     : 7;  /**< DMA engine 2 dif instruction read state */
	uint64_t d3_difst                     : 7;  /**< DMA engine 3 dif instruction read state */
	uint64_t d4_difst                     : 7;  /**< DMA engine 4 dif instruction read state */
	uint64_t d0_reqst                     : 5;  /**< DMA engine 0 request data state */
	uint64_t d1_reqst                     : 5;  /**< DMA engine 1 request data state */
	uint64_t d2_reqst                     : 5;  /**< DMA engine 2 request data state */
	uint64_t d3_reqst                     : 5;  /**< DMA engine 3 request data state */
	uint64_t d4_reqst                     : 5;  /**< DMA engine 4 request data state */
#else
	uint64_t d4_reqst                     : 5;
	uint64_t d3_reqst                     : 5;
	uint64_t d2_reqst                     : 5;
	uint64_t d1_reqst                     : 5;
	uint64_t d0_reqst                     : 5;
	uint64_t d4_difst                     : 7;
	uint64_t d3_difst                     : 7;
	uint64_t d2_difst                     : 7;
	uint64_t d1_difst                     : 7;
	uint64_t d0_difst                     : 7;
	uint64_t reserved_60_63               : 4;
#endif
	} s;
	struct cvmx_npei_dma_state1_p1_cn52xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_60_63               : 4;
	uint64_t d0_difst                     : 7;  /**< DMA engine 0 dif instruction read state */
	uint64_t d1_difst                     : 7;  /**< DMA engine 1 dif instruction read state */
	uint64_t d2_difst                     : 7;  /**< DMA engine 2 dif instruction read state */
	uint64_t d3_difst                     : 7;  /**< DMA engine 3 dif instruction read state */
	uint64_t reserved_25_31               : 7;
	uint64_t d0_reqst                     : 5;  /**< DMA engine 0 request data state */
	uint64_t d1_reqst                     : 5;  /**< DMA engine 1 request data state */
	uint64_t d2_reqst                     : 5;  /**< DMA engine 2 request data state */
	uint64_t d3_reqst                     : 5;  /**< DMA engine 3 request data state */
	uint64_t reserved_0_4                 : 5;
#else
	uint64_t reserved_0_4                 : 5;
	uint64_t d3_reqst                     : 5;
	uint64_t d2_reqst                     : 5;
	uint64_t d1_reqst                     : 5;
	uint64_t d0_reqst                     : 5;
	uint64_t reserved_25_31               : 7;
	uint64_t d3_difst                     : 7;
	uint64_t d2_difst                     : 7;
	uint64_t d1_difst                     : 7;
	uint64_t d0_difst                     : 7;
	uint64_t reserved_60_63               : 4;
#endif
	} cn52xxp1;
	struct cvmx_npei_dma_state1_p1_s      cn56xxp1;
};
typedef union cvmx_npei_dma_state1_p1 cvmx_npei_dma_state1_p1_t;

/**
 * cvmx_npei_dma_state2
 *
 * NPEI_DMA_STATE2 = NPI's DMA State 2
 *
 * Results from DMA state register 2
 */
union cvmx_npei_dma_state2 {
	uint64_t u64;
	struct cvmx_npei_dma_state2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_28_63               : 36;
	uint64_t ndwe                         : 4;  /**< DMA L2C Write State */
	uint64_t reserved_21_23               : 3;
	uint64_t ndre                         : 5;  /**< DMA L2C Read State */
	uint64_t reserved_10_15               : 6;
	uint64_t prd                          : 10; /**< DMA PICe Read State */
#else
	uint64_t prd                          : 10;
	uint64_t reserved_10_15               : 6;
	uint64_t ndre                         : 5;
	uint64_t reserved_21_23               : 3;
	uint64_t ndwe                         : 4;
	uint64_t reserved_28_63               : 36;
#endif
	} s;
	struct cvmx_npei_dma_state2_s         cn52xx;
};
typedef union cvmx_npei_dma_state2 cvmx_npei_dma_state2_t;

/**
 * cvmx_npei_dma_state2_p1
 *
 * NPEI_DMA_STATE2_P1 = NPEI DMA Instruction Fetch State
 *
 * DMA engine Debug information.
 */
union cvmx_npei_dma_state2_p1 {
	uint64_t u64;
	struct cvmx_npei_dma_state2_p1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_45_63               : 19;
	uint64_t d0_dffst                     : 9;  /**< DMA engine 0 dif instruction fetch state */
	uint64_t d1_dffst                     : 9;  /**< DMA engine 1 dif instruction fetch state */
	uint64_t d2_dffst                     : 9;  /**< DMA engine 2 dif instruction fetch state */
	uint64_t d3_dffst                     : 9;  /**< DMA engine 3 dif instruction fetch state */
	uint64_t d4_dffst                     : 9;  /**< DMA engine 4 dif instruction fetch state */
#else
	uint64_t d4_dffst                     : 9;
	uint64_t d3_dffst                     : 9;
	uint64_t d2_dffst                     : 9;
	uint64_t d1_dffst                     : 9;
	uint64_t d0_dffst                     : 9;
	uint64_t reserved_45_63               : 19;
#endif
	} s;
	struct cvmx_npei_dma_state2_p1_cn52xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_45_63               : 19;
	uint64_t d0_dffst                     : 9;  /**< DMA engine 0 dif instruction fetch state */
	uint64_t d1_dffst                     : 9;  /**< DMA engine 1 dif instruction fetch state */
	uint64_t d2_dffst                     : 9;  /**< DMA engine 2 dif instruction fetch state */
	uint64_t d3_dffst                     : 9;  /**< DMA engine 3 dif instruction fetch state */
	uint64_t reserved_0_8                 : 9;
#else
	uint64_t reserved_0_8                 : 9;
	uint64_t d3_dffst                     : 9;
	uint64_t d2_dffst                     : 9;
	uint64_t d1_dffst                     : 9;
	uint64_t d0_dffst                     : 9;
	uint64_t reserved_45_63               : 19;
#endif
	} cn52xxp1;
	struct cvmx_npei_dma_state2_p1_s      cn56xxp1;
};
typedef union cvmx_npei_dma_state2_p1 cvmx_npei_dma_state2_p1_t;

/**
 * cvmx_npei_dma_state3_p1
 *
 * NPEI_DMA_STATE3_P1 = NPEI DMA DRE State
 *
 * DMA engine Debug information.
 */
union cvmx_npei_dma_state3_p1 {
	uint64_t u64;
	struct cvmx_npei_dma_state3_p1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_60_63               : 4;
	uint64_t d0_drest                     : 15; /**< DMA engine 0 dre state */
	uint64_t d1_drest                     : 15; /**< DMA engine 1 dre state */
	uint64_t d2_drest                     : 15; /**< DMA engine 2 dre state */
	uint64_t d3_drest                     : 15; /**< DMA engine 3 dre state */
#else
	uint64_t d3_drest                     : 15;
	uint64_t d2_drest                     : 15;
	uint64_t d1_drest                     : 15;
	uint64_t d0_drest                     : 15;
	uint64_t reserved_60_63               : 4;
#endif
	} s;
	struct cvmx_npei_dma_state3_p1_s      cn52xxp1;
	struct cvmx_npei_dma_state3_p1_s      cn56xxp1;
};
typedef union cvmx_npei_dma_state3_p1 cvmx_npei_dma_state3_p1_t;

/**
 * cvmx_npei_dma_state4_p1
 *
 * NPEI_DMA_STATE4_P1 = NPEI DMA DWE State
 *
 * DMA engine Debug information.
 */
union cvmx_npei_dma_state4_p1 {
	uint64_t u64;
	struct cvmx_npei_dma_state4_p1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_52_63               : 12;
	uint64_t d0_dwest                     : 13; /**< DMA engine 0 dwe state */
	uint64_t d1_dwest                     : 13; /**< DMA engine 1 dwe state */
	uint64_t d2_dwest                     : 13; /**< DMA engine 2 dwe state */
	uint64_t d3_dwest                     : 13; /**< DMA engine 3 dwe state */
#else
	uint64_t d3_dwest                     : 13;
	uint64_t d2_dwest                     : 13;
	uint64_t d1_dwest                     : 13;
	uint64_t d0_dwest                     : 13;
	uint64_t reserved_52_63               : 12;
#endif
	} s;
	struct cvmx_npei_dma_state4_p1_s      cn52xxp1;
	struct cvmx_npei_dma_state4_p1_s      cn56xxp1;
};
typedef union cvmx_npei_dma_state4_p1 cvmx_npei_dma_state4_p1_t;

/**
 * cvmx_npei_dma_state5_p1
 *
 * NPEI_DMA_STATE5_P1 = NPEI DMA DWE and DRE State
 *
 * DMA engine Debug information.
 */
union cvmx_npei_dma_state5_p1 {
	uint64_t u64;
	struct cvmx_npei_dma_state5_p1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_28_63               : 36;
	uint64_t d4_drest                     : 15; /**< DMA engine 4 dre state */
	uint64_t d4_dwest                     : 13; /**< DMA engine 4 dwe state */
#else
	uint64_t d4_dwest                     : 13;
	uint64_t d4_drest                     : 15;
	uint64_t reserved_28_63               : 36;
#endif
	} s;
	struct cvmx_npei_dma_state5_p1_s      cn56xxp1;
};
typedef union cvmx_npei_dma_state5_p1 cvmx_npei_dma_state5_p1_t;

/**
 * cvmx_npei_int_a_enb
 *
 * NPEI_INTERRUPT_A_ENB = NPI's Interrupt A Enable Register
 *
 * Used to allow the generation of interrupts (MSI/INTA) to the PCIe CoresUsed to enable the various interrupting conditions of NPEI
 */
union cvmx_npei_int_a_enb {
	uint64_t u64;
	struct cvmx_npei_int_a_enb_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_10_63               : 54;
	uint64_t pout_err                     : 1;  /**< Enables NPEI_INT_A_SUM[9] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t pin_bp                       : 1;  /**< Enables NPEI_INT_A_SUM[8] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t p1_rdlk                      : 1;  /**< Enables NPEI_INT_A_SUM[7] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t p0_rdlk                      : 1;  /**< Enables NPEI_INT_A_SUM[6] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t pgl_err                      : 1;  /**< Enables NPEI_INT_A_SUM[5] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t pdi_err                      : 1;  /**< Enables NPEI_INT_A_SUM[4] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t pop_err                      : 1;  /**< Enables NPEI_INT_A_SUM[3] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t pins_err                     : 1;  /**< Enables NPEI_INT_A_SUM[2] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t dma1_cpl                     : 1;  /**< Enables NPEI_INT_A_SUM[1] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t dma0_cpl                     : 1;  /**< Enables NPEI_INT_A_SUM[0] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
#else
	uint64_t dma0_cpl                     : 1;
	uint64_t dma1_cpl                     : 1;
	uint64_t pins_err                     : 1;
	uint64_t pop_err                      : 1;
	uint64_t pdi_err                      : 1;
	uint64_t pgl_err                      : 1;
	uint64_t p0_rdlk                      : 1;
	uint64_t p1_rdlk                      : 1;
	uint64_t pin_bp                       : 1;
	uint64_t pout_err                     : 1;
	uint64_t reserved_10_63               : 54;
#endif
	} s;
	struct cvmx_npei_int_a_enb_s          cn52xx;
	struct cvmx_npei_int_a_enb_cn52xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_2_63                : 62;
	uint64_t dma1_cpl                     : 1;  /**< Enables NPEI_INT_A_SUM[1] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t dma0_cpl                     : 1;  /**< Enables NPEI_INT_A_SUM[0] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
#else
	uint64_t dma0_cpl                     : 1;
	uint64_t dma1_cpl                     : 1;
	uint64_t reserved_2_63                : 62;
#endif
	} cn52xxp1;
	struct cvmx_npei_int_a_enb_s          cn56xx;
};
typedef union cvmx_npei_int_a_enb cvmx_npei_int_a_enb_t;

/**
 * cvmx_npei_int_a_enb2
 *
 * NPEI_INTERRUPT_A_ENB2 = NPEI's Interrupt A Enable2 Register
 *
 * Used to enable the various interrupting conditions of NPEI
 */
union cvmx_npei_int_a_enb2 {
	uint64_t u64;
	struct cvmx_npei_int_a_enb2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_10_63               : 54;
	uint64_t pout_err                     : 1;  /**< Enables NPEI_INT_A_SUM[9] to generate an
                                                         interrupt on the RSL. */
	uint64_t pin_bp                       : 1;  /**< Enables NPEI_INT_A_SUM[8] to generate an
                                                         interrupt on the RSL. */
	uint64_t p1_rdlk                      : 1;  /**< Enables NPEI_INT_A_SUM[7] to generate an
                                                         interrupt on the RSL. */
	uint64_t p0_rdlk                      : 1;  /**< Enables NPEI_INT_A_SUM[6] to generate an
                                                         interrupt on the RSL. */
	uint64_t pgl_err                      : 1;  /**< Enables NPEI_INT_A_SUM[5] to generate an
                                                         interrupt on the RSL. */
	uint64_t pdi_err                      : 1;  /**< Enables NPEI_INT_A_SUM[4] to generate an
                                                         interrupt on the RSL. */
	uint64_t pop_err                      : 1;  /**< Enables NPEI_INT_A_SUM[3] to generate an
                                                         interrupt on the RSL. */
	uint64_t pins_err                     : 1;  /**< Enables NPEI_INT_A_SUM[2] to generate an
                                                         interrupt on the RSL. */
	uint64_t dma1_cpl                     : 1;  /**< Enables NPEI_INT_A_SUM[1] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t dma0_cpl                     : 1;  /**< Enables NPEI_INT_A_SUM[0] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
#else
	uint64_t dma0_cpl                     : 1;
	uint64_t dma1_cpl                     : 1;
	uint64_t pins_err                     : 1;
	uint64_t pop_err                      : 1;
	uint64_t pdi_err                      : 1;
	uint64_t pgl_err                      : 1;
	uint64_t p0_rdlk                      : 1;
	uint64_t p1_rdlk                      : 1;
	uint64_t pin_bp                       : 1;
	uint64_t pout_err                     : 1;
	uint64_t reserved_10_63               : 54;
#endif
	} s;
	struct cvmx_npei_int_a_enb2_s         cn52xx;
	struct cvmx_npei_int_a_enb2_cn52xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_2_63                : 62;
	uint64_t dma1_cpl                     : 1;  /**< Enables NPEI_INT_A_SUM[1] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t dma0_cpl                     : 1;  /**< Enables NPEI_INT_A_SUM[0] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
#else
	uint64_t dma0_cpl                     : 1;
	uint64_t dma1_cpl                     : 1;
	uint64_t reserved_2_63                : 62;
#endif
	} cn52xxp1;
	struct cvmx_npei_int_a_enb2_s         cn56xx;
};
typedef union cvmx_npei_int_a_enb2 cvmx_npei_int_a_enb2_t;

/**
 * cvmx_npei_int_a_sum
 *
 * NPEI_INTERRUPT_A_SUM = NPI Interrupt A Summary Register
 *
 * Set when an interrupt condition occurs, write '1' to clear. When an interrupt bitin this register is set and
 * the cooresponding bit in the NPEI_INT_A_ENB register is set, then NPEI_INT_SUM[61] will be set.
 */
union cvmx_npei_int_a_sum {
	uint64_t u64;
	struct cvmx_npei_int_a_sum_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_10_63               : 54;
	uint64_t pout_err                     : 1;  /**< Set when PKO sends packet data with the error bit
                                                         set. */
	uint64_t pin_bp                       : 1;  /**< Packet input count has exceeded the WMARK.
                                                         See NPEI_PKT_IN_BP */
	uint64_t p1_rdlk                      : 1;  /**< PCIe port 1 received a read lock. */
	uint64_t p0_rdlk                      : 1;  /**< PCIe port 0 received a read lock. */
	uint64_t pgl_err                      : 1;  /**< When a read error occurs on a packet gather list
                                                         read this bit is set. */
	uint64_t pdi_err                      : 1;  /**< When a read error occurs on a packet data read
                                                         this bit is set. */
	uint64_t pop_err                      : 1;  /**< When a read error occurs on a packet scatter
                                                         pointer pair this bit is set. */
	uint64_t pins_err                     : 1;  /**< When a read error occurs on a packet instruction
                                                         this bit is set. */
	uint64_t dma1_cpl                     : 1;  /**< Set each time any PCIe DMA engine recieves a UR/CA
                                                         response from PCIe Port 1 */
	uint64_t dma0_cpl                     : 1;  /**< Set each time any PCIe DMA engine recieves a UR/CA
                                                         response from PCIe Port 0 */
#else
	uint64_t dma0_cpl                     : 1;
	uint64_t dma1_cpl                     : 1;
	uint64_t pins_err                     : 1;
	uint64_t pop_err                      : 1;
	uint64_t pdi_err                      : 1;
	uint64_t pgl_err                      : 1;
	uint64_t p0_rdlk                      : 1;
	uint64_t p1_rdlk                      : 1;
	uint64_t pin_bp                       : 1;
	uint64_t pout_err                     : 1;
	uint64_t reserved_10_63               : 54;
#endif
	} s;
	struct cvmx_npei_int_a_sum_s          cn52xx;
	struct cvmx_npei_int_a_sum_cn52xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_2_63                : 62;
	uint64_t dma1_cpl                     : 1;  /**< Set each time any PCIe DMA engine recieves a UR/CA
                                                         response from PCIe Port 1 */
	uint64_t dma0_cpl                     : 1;  /**< Set each time any PCIe DMA engine recieves a UR/CA
                                                         response from PCIe Port 0 */
#else
	uint64_t dma0_cpl                     : 1;
	uint64_t dma1_cpl                     : 1;
	uint64_t reserved_2_63                : 62;
#endif
	} cn52xxp1;
	struct cvmx_npei_int_a_sum_s          cn56xx;
};
typedef union cvmx_npei_int_a_sum cvmx_npei_int_a_sum_t;

/**
 * cvmx_npei_int_enb
 *
 * NPEI_INTERRUPT_ENB = NPI's Interrupt Enable Register
 *
 * Used to allow the generation of interrupts (MSI/INTA) to the PCIe CoresUsed to enable the various interrupting conditions of NPI
 */
union cvmx_npei_int_enb {
	uint64_t u64;
	struct cvmx_npei_int_enb_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t mio_inta                     : 1;  /**< Enables NPEI_INT_SUM[63] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t reserved_62_62               : 1;
	uint64_t int_a                        : 1;  /**< Enables NPEI_INT_SUM[61] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_ldwn                      : 1;  /**< Enables NPEI_INT_SUM[60] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_ldwn                      : 1;  /**< Enables NPEI_INT_SUM[59] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_exc                       : 1;  /**< Enables NPEI_INT_SUM[58] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_exc                       : 1;  /**< Enables NPEI_INT_SUM[57] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_up_wf                     : 1;  /**< Enables NPEI_INT_SUM[56] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_up_wf                     : 1;  /**< Enables NPEI_INT_SUM[55] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_un_wf                     : 1;  /**< Enables NPEI_INT_SUM[54] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_un_wf                     : 1;  /**< Enables NPEI_INT_SUM[53] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_un_bx                     : 1;  /**< Enables NPEI_INT_SUM[52] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_un_wi                     : 1;  /**< Enables NPEI_INT_SUM[51] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_un_b2                     : 1;  /**< Enables NPEI_INT_SUM[50] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_un_b1                     : 1;  /**< Enables NPEI_INT_SUM[49] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_un_b0                     : 1;  /**< Enables NPEI_INT_SUM[48] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_up_bx                     : 1;  /**< Enables NPEI_INT_SUM[47] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_up_wi                     : 1;  /**< Enables NPEI_INT_SUM[46] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_up_b2                     : 1;  /**< Enables NPEI_INT_SUM[45] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_up_b1                     : 1;  /**< Enables NPEI_INT_SUM[44] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_up_b0                     : 1;  /**< Enables NPEI_INT_SUM[43] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_un_bx                     : 1;  /**< Enables NPEI_INT_SUM[42] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_un_wi                     : 1;  /**< Enables NPEI_INT_SUM[41] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_un_b2                     : 1;  /**< Enables NPEI_INT_SUM[40] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_un_b1                     : 1;  /**< Enables NPEI_INT_SUM[39] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_un_b0                     : 1;  /**< Enables NPEI_INT_SUM[38] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_up_bx                     : 1;  /**< Enables NPEI_INT_SUM[37] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_up_wi                     : 1;  /**< Enables NPEI_INT_SUM[36] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_up_b2                     : 1;  /**< Enables NPEI_INT_SUM[35] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_up_b1                     : 1;  /**< Enables NPEI_INT_SUM[34] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_up_b0                     : 1;  /**< Enables NPEI_INT_SUM[33] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_hpint                     : 1;  /**< Enables NPEI_INT_SUM[32] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_pmei                      : 1;  /**< Enables NPEI_INT_SUM[31] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_wake                      : 1;  /**< Enables NPEI_INT_SUM[30] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t crs1_dr                      : 1;  /**< Enables NPEI_INT_SUM[29] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_se                        : 1;  /**< Enables NPEI_INT_SUM[28] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t crs1_er                      : 1;  /**< Enables NPEI_INT_SUM[27] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_aeri                      : 1;  /**< Enables NPEI_INT_SUM[26] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_hpint                     : 1;  /**< Enables NPEI_INT_SUM[25] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_pmei                      : 1;  /**< Enables NPEI_INT_SUM[24] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_wake                      : 1;  /**< Enables NPEI_INT_SUM[23] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t crs0_dr                      : 1;  /**< Enables NPEI_INT_SUM[22] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_se                        : 1;  /**< Enables NPEI_INT_SUM[21] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t crs0_er                      : 1;  /**< Enables NPEI_INT_SUM[20] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_aeri                      : 1;  /**< Enables NPEI_INT_SUM[19] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t ptime                        : 1;  /**< Enables NPEI_INT_SUM[18] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t pcnt                         : 1;  /**< Enables NPEI_INT_SUM[17] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t pidbof                       : 1;  /**< Enables NPEI_INT_SUM[16] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t psldbof                      : 1;  /**< Enables NPEI_INT_SUM[15] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t dtime1                       : 1;  /**< Enables NPEI_INT_SUM[14] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t dtime0                       : 1;  /**< Enables NPEI_INT_SUM[13] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t dcnt1                        : 1;  /**< Enables NPEI_INT_SUM[12] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t dcnt0                        : 1;  /**< Enables NPEI_INT_SUM[11] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t dma1fi                       : 1;  /**< Enables NPEI_INT_SUM[10] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t dma0fi                       : 1;  /**< Enables NPEI_INT_SUM[9] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t dma4dbo                      : 1;  /**< Enables NPEI_INT_SUM[8] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t dma3dbo                      : 1;  /**< Enables NPEI_INT_SUM[7] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t dma2dbo                      : 1;  /**< Enables NPEI_INT_SUM[6] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t dma1dbo                      : 1;  /**< Enables NPEI_INT_SUM[5] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t dma0dbo                      : 1;  /**< Enables NPEI_INT_SUM[4] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t iob2big                      : 1;  /**< Enables NPEI_INT_SUM[3] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t bar0_to                      : 1;  /**< Enables NPEI_INT_SUM[2] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t rml_wto                      : 1;  /**< Enables NPEI_INT_SUM[1] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t rml_rto                      : 1;  /**< Enables NPEI_INT_SUM[0] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
#else
	uint64_t rml_rto                      : 1;
	uint64_t rml_wto                      : 1;
	uint64_t bar0_to                      : 1;
	uint64_t iob2big                      : 1;
	uint64_t dma0dbo                      : 1;
	uint64_t dma1dbo                      : 1;
	uint64_t dma2dbo                      : 1;
	uint64_t dma3dbo                      : 1;
	uint64_t dma4dbo                      : 1;
	uint64_t dma0fi                       : 1;
	uint64_t dma1fi                       : 1;
	uint64_t dcnt0                        : 1;
	uint64_t dcnt1                        : 1;
	uint64_t dtime0                       : 1;
	uint64_t dtime1                       : 1;
	uint64_t psldbof                      : 1;
	uint64_t pidbof                       : 1;
	uint64_t pcnt                         : 1;
	uint64_t ptime                        : 1;
	uint64_t c0_aeri                      : 1;
	uint64_t crs0_er                      : 1;
	uint64_t c0_se                        : 1;
	uint64_t crs0_dr                      : 1;
	uint64_t c0_wake                      : 1;
	uint64_t c0_pmei                      : 1;
	uint64_t c0_hpint                     : 1;
	uint64_t c1_aeri                      : 1;
	uint64_t crs1_er                      : 1;
	uint64_t c1_se                        : 1;
	uint64_t crs1_dr                      : 1;
	uint64_t c1_wake                      : 1;
	uint64_t c1_pmei                      : 1;
	uint64_t c1_hpint                     : 1;
	uint64_t c0_up_b0                     : 1;
	uint64_t c0_up_b1                     : 1;
	uint64_t c0_up_b2                     : 1;
	uint64_t c0_up_wi                     : 1;
	uint64_t c0_up_bx                     : 1;
	uint64_t c0_un_b0                     : 1;
	uint64_t c0_un_b1                     : 1;
	uint64_t c0_un_b2                     : 1;
	uint64_t c0_un_wi                     : 1;
	uint64_t c0_un_bx                     : 1;
	uint64_t c1_up_b0                     : 1;
	uint64_t c1_up_b1                     : 1;
	uint64_t c1_up_b2                     : 1;
	uint64_t c1_up_wi                     : 1;
	uint64_t c1_up_bx                     : 1;
	uint64_t c1_un_b0                     : 1;
	uint64_t c1_un_b1                     : 1;
	uint64_t c1_un_b2                     : 1;
	uint64_t c1_un_wi                     : 1;
	uint64_t c1_un_bx                     : 1;
	uint64_t c0_un_wf                     : 1;
	uint64_t c1_un_wf                     : 1;
	uint64_t c0_up_wf                     : 1;
	uint64_t c1_up_wf                     : 1;
	uint64_t c0_exc                       : 1;
	uint64_t c1_exc                       : 1;
	uint64_t c0_ldwn                      : 1;
	uint64_t c1_ldwn                      : 1;
	uint64_t int_a                        : 1;
	uint64_t reserved_62_62               : 1;
	uint64_t mio_inta                     : 1;
#endif
	} s;
	struct cvmx_npei_int_enb_s            cn52xx;
	struct cvmx_npei_int_enb_cn52xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t mio_inta                     : 1;  /**< Enables NPEI_INT_SUM[63] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t reserved_62_62               : 1;
	uint64_t int_a                        : 1;  /**< Enables NPEI_INT_SUM[61] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_ldwn                      : 1;  /**< Enables NPEI_INT_SUM[60] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_ldwn                      : 1;  /**< Enables NPEI_INT_SUM[59] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_exc                       : 1;  /**< Enables NPEI_INT_SUM[58] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_exc                       : 1;  /**< Enables NPEI_INT_SUM[57] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_up_wf                     : 1;  /**< Enables NPEI_INT_SUM[56] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_up_wf                     : 1;  /**< Enables NPEI_INT_SUM[55] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_un_wf                     : 1;  /**< Enables NPEI_INT_SUM[54] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_un_wf                     : 1;  /**< Enables NPEI_INT_SUM[53] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_un_bx                     : 1;  /**< Enables NPEI_INT_SUM[52] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_un_wi                     : 1;  /**< Enables NPEI_INT_SUM[51] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_un_b2                     : 1;  /**< Enables NPEI_INT_SUM[50] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_un_b1                     : 1;  /**< Enables NPEI_INT_SUM[49] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_un_b0                     : 1;  /**< Enables NPEI_INT_SUM[48] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_up_bx                     : 1;  /**< Enables NPEI_INT_SUM[47] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_up_wi                     : 1;  /**< Enables NPEI_INT_SUM[46] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_up_b2                     : 1;  /**< Enables NPEI_INT_SUM[45] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_up_b1                     : 1;  /**< Enables NPEI_INT_SUM[44] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_up_b0                     : 1;  /**< Enables NPEI_INT_SUM[43] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_un_bx                     : 1;  /**< Enables NPEI_INT_SUM[42] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_un_wi                     : 1;  /**< Enables NPEI_INT_SUM[41] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_un_b2                     : 1;  /**< Enables NPEI_INT_SUM[40] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_un_b1                     : 1;  /**< Enables NPEI_INT_SUM[39] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_un_b0                     : 1;  /**< Enables NPEI_INT_SUM[38] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_up_bx                     : 1;  /**< Enables NPEI_INT_SUM[37] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_up_wi                     : 1;  /**< Enables NPEI_INT_SUM[36] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_up_b2                     : 1;  /**< Enables NPEI_INT_SUM[35] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_up_b1                     : 1;  /**< Enables NPEI_INT_SUM[34] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_up_b0                     : 1;  /**< Enables NPEI_INT_SUM[33] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_hpint                     : 1;  /**< Enables NPEI_INT_SUM[32] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_pmei                      : 1;  /**< Enables NPEI_INT_SUM[31] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_wake                      : 1;  /**< Enables NPEI_INT_SUM[30] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t crs1_dr                      : 1;  /**< Enables NPEI_INT_SUM[29] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_se                        : 1;  /**< Enables NPEI_INT_SUM[28] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t crs1_er                      : 1;  /**< Enables NPEI_INT_SUM[27] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_aeri                      : 1;  /**< Enables NPEI_INT_SUM[26] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_hpint                     : 1;  /**< Enables NPEI_INT_SUM[25] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_pmei                      : 1;  /**< Enables NPEI_INT_SUM[24] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_wake                      : 1;  /**< Enables NPEI_INT_SUM[23] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t crs0_dr                      : 1;  /**< Enables NPEI_INT_SUM[22] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_se                        : 1;  /**< Enables NPEI_INT_SUM[21] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t crs0_er                      : 1;  /**< Enables NPEI_INT_SUM[20] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_aeri                      : 1;  /**< Enables NPEI_INT_SUM[19] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t ptime                        : 1;  /**< Enables NPEI_INT_SUM[18] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t pcnt                         : 1;  /**< Enables NPEI_INT_SUM[17] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t pidbof                       : 1;  /**< Enables NPEI_INT_SUM[16] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t psldbof                      : 1;  /**< Enables NPEI_INT_SUM[15] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t dtime1                       : 1;  /**< Enables NPEI_INT_SUM[14] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t dtime0                       : 1;  /**< Enables NPEI_INT_SUM[13] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t dcnt1                        : 1;  /**< Enables NPEI_INT_SUM[12] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t dcnt0                        : 1;  /**< Enables NPEI_INT_SUM[11] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t dma1fi                       : 1;  /**< Enables NPEI_INT_SUM[10] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t dma0fi                       : 1;  /**< Enables NPEI_INT_SUM[9] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t reserved_8_8                 : 1;
	uint64_t dma3dbo                      : 1;  /**< Enables NPEI_INT_SUM[7] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t dma2dbo                      : 1;  /**< Enables NPEI_INT_SUM[6] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t dma1dbo                      : 1;  /**< Enables NPEI_INT_SUM[5] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t dma0dbo                      : 1;  /**< Enables NPEI_INT_SUM[4] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t iob2big                      : 1;  /**< Enables NPEI_INT_SUM[3] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t bar0_to                      : 1;  /**< Enables NPEI_INT_SUM[2] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t rml_wto                      : 1;  /**< Enables NPEI_INT_SUM[1] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t rml_rto                      : 1;  /**< Enables NPEI_INT_SUM[0] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
#else
	uint64_t rml_rto                      : 1;
	uint64_t rml_wto                      : 1;
	uint64_t bar0_to                      : 1;
	uint64_t iob2big                      : 1;
	uint64_t dma0dbo                      : 1;
	uint64_t dma1dbo                      : 1;
	uint64_t dma2dbo                      : 1;
	uint64_t dma3dbo                      : 1;
	uint64_t reserved_8_8                 : 1;
	uint64_t dma0fi                       : 1;
	uint64_t dma1fi                       : 1;
	uint64_t dcnt0                        : 1;
	uint64_t dcnt1                        : 1;
	uint64_t dtime0                       : 1;
	uint64_t dtime1                       : 1;
	uint64_t psldbof                      : 1;
	uint64_t pidbof                       : 1;
	uint64_t pcnt                         : 1;
	uint64_t ptime                        : 1;
	uint64_t c0_aeri                      : 1;
	uint64_t crs0_er                      : 1;
	uint64_t c0_se                        : 1;
	uint64_t crs0_dr                      : 1;
	uint64_t c0_wake                      : 1;
	uint64_t c0_pmei                      : 1;
	uint64_t c0_hpint                     : 1;
	uint64_t c1_aeri                      : 1;
	uint64_t crs1_er                      : 1;
	uint64_t c1_se                        : 1;
	uint64_t crs1_dr                      : 1;
	uint64_t c1_wake                      : 1;
	uint64_t c1_pmei                      : 1;
	uint64_t c1_hpint                     : 1;
	uint64_t c0_up_b0                     : 1;
	uint64_t c0_up_b1                     : 1;
	uint64_t c0_up_b2                     : 1;
	uint64_t c0_up_wi                     : 1;
	uint64_t c0_up_bx                     : 1;
	uint64_t c0_un_b0                     : 1;
	uint64_t c0_un_b1                     : 1;
	uint64_t c0_un_b2                     : 1;
	uint64_t c0_un_wi                     : 1;
	uint64_t c0_un_bx                     : 1;
	uint64_t c1_up_b0                     : 1;
	uint64_t c1_up_b1                     : 1;
	uint64_t c1_up_b2                     : 1;
	uint64_t c1_up_wi                     : 1;
	uint64_t c1_up_bx                     : 1;
	uint64_t c1_un_b0                     : 1;
	uint64_t c1_un_b1                     : 1;
	uint64_t c1_un_b2                     : 1;
	uint64_t c1_un_wi                     : 1;
	uint64_t c1_un_bx                     : 1;
	uint64_t c0_un_wf                     : 1;
	uint64_t c1_un_wf                     : 1;
	uint64_t c0_up_wf                     : 1;
	uint64_t c1_up_wf                     : 1;
	uint64_t c0_exc                       : 1;
	uint64_t c1_exc                       : 1;
	uint64_t c0_ldwn                      : 1;
	uint64_t c1_ldwn                      : 1;
	uint64_t int_a                        : 1;
	uint64_t reserved_62_62               : 1;
	uint64_t mio_inta                     : 1;
#endif
	} cn52xxp1;
	struct cvmx_npei_int_enb_s            cn56xx;
	struct cvmx_npei_int_enb_cn56xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t mio_inta                     : 1;  /**< Enables NPEI_INT_SUM[63] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t reserved_61_62               : 2;
	uint64_t c1_ldwn                      : 1;  /**< Enables NPEI_INT_SUM[60] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_ldwn                      : 1;  /**< Enables NPEI_INT_SUM[59] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_exc                       : 1;  /**< Enables NPEI_INT_SUM[58] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_exc                       : 1;  /**< Enables NPEI_INT_SUM[57] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_up_wf                     : 1;  /**< Enables NPEI_INT_SUM[56] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_up_wf                     : 1;  /**< Enables NPEI_INT_SUM[55] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_un_wf                     : 1;  /**< Enables NPEI_INT_SUM[54] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_un_wf                     : 1;  /**< Enables NPEI_INT_SUM[53] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_un_bx                     : 1;  /**< Enables NPEI_INT_SUM[52] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_un_wi                     : 1;  /**< Enables NPEI_INT_SUM[51] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_un_b2                     : 1;  /**< Enables NPEI_INT_SUM[50] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_un_b1                     : 1;  /**< Enables NPEI_INT_SUM[49] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_un_b0                     : 1;  /**< Enables NPEI_INT_SUM[48] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_up_bx                     : 1;  /**< Enables NPEI_INT_SUM[47] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_up_wi                     : 1;  /**< Enables NPEI_INT_SUM[46] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_up_b2                     : 1;  /**< Enables NPEI_INT_SUM[45] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_up_b1                     : 1;  /**< Enables NPEI_INT_SUM[44] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_up_b0                     : 1;  /**< Enables NPEI_INT_SUM[43] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_un_bx                     : 1;  /**< Enables NPEI_INT_SUM[42] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_un_wi                     : 1;  /**< Enables NPEI_INT_SUM[41] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_un_b2                     : 1;  /**< Enables NPEI_INT_SUM[40] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_un_b1                     : 1;  /**< Enables NPEI_INT_SUM[39] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_un_b0                     : 1;  /**< Enables NPEI_INT_SUM[38] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_up_bx                     : 1;  /**< Enables NPEI_INT_SUM[37] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_up_wi                     : 1;  /**< Enables NPEI_INT_SUM[36] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_up_b2                     : 1;  /**< Enables NPEI_INT_SUM[35] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_up_b1                     : 1;  /**< Enables NPEI_INT_SUM[34] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_up_b0                     : 1;  /**< Enables NPEI_INT_SUM[33] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_hpint                     : 1;  /**< Enables NPEI_INT_SUM[32] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_pmei                      : 1;  /**< Enables NPEI_INT_SUM[31] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c1_wake                      : 1;  /**< Enables NPEI_INT_SUM[30] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t reserved_29_29               : 1;
	uint64_t c1_se                        : 1;  /**< Enables NPEI_INT_SUM[28] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t reserved_27_27               : 1;
	uint64_t c1_aeri                      : 1;  /**< Enables NPEI_INT_SUM[26] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_hpint                     : 1;  /**< Enables NPEI_INT_SUM[25] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_pmei                      : 1;  /**< Enables NPEI_INT_SUM[24] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t c0_wake                      : 1;  /**< Enables NPEI_INT_SUM[23] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t reserved_22_22               : 1;
	uint64_t c0_se                        : 1;  /**< Enables NPEI_INT_SUM[21] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t reserved_20_20               : 1;
	uint64_t c0_aeri                      : 1;  /**< Enables NPEI_INT_SUM[19] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t ptime                        : 1;  /**< Enables NPEI_INT_SUM[18] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t pcnt                         : 1;  /**< Enables NPEI_INT_SUM[17] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t pidbof                       : 1;  /**< Enables NPEI_INT_SUM[16] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t psldbof                      : 1;  /**< Enables NPEI_INT_SUM[15] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t dtime1                       : 1;  /**< Enables NPEI_INT_SUM[14] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t dtime0                       : 1;  /**< Enables NPEI_INT_SUM[13] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t dcnt1                        : 1;  /**< Enables NPEI_INT_SUM[12] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t dcnt0                        : 1;  /**< Enables NPEI_INT_SUM[11] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t dma1fi                       : 1;  /**< Enables NPEI_INT_SUM[10] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t dma0fi                       : 1;  /**< Enables NPEI_INT_SUM[9] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t dma4dbo                      : 1;  /**< Enables NPEI_INT_SUM[8] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t dma3dbo                      : 1;  /**< Enables NPEI_INT_SUM[7] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t dma2dbo                      : 1;  /**< Enables NPEI_INT_SUM[6] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t dma1dbo                      : 1;  /**< Enables NPEI_INT_SUM[5] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t dma0dbo                      : 1;  /**< Enables NPEI_INT_SUM[4] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t iob2big                      : 1;  /**< Enables NPEI_INT_SUM[3] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t bar0_to                      : 1;  /**< Enables NPEI_INT_SUM[2] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t rml_wto                      : 1;  /**< Enables NPEI_INT_SUM[1] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
	uint64_t rml_rto                      : 1;  /**< Enables NPEI_INT_SUM[0] to generate an
                                                         interrupt to the PCIE core for MSI/inta. */
#else
	uint64_t rml_rto                      : 1;
	uint64_t rml_wto                      : 1;
	uint64_t bar0_to                      : 1;
	uint64_t iob2big                      : 1;
	uint64_t dma0dbo                      : 1;
	uint64_t dma1dbo                      : 1;
	uint64_t dma2dbo                      : 1;
	uint64_t dma3dbo                      : 1;
	uint64_t dma4dbo                      : 1;
	uint64_t dma0fi                       : 1;
	uint64_t dma1fi                       : 1;
	uint64_t dcnt0                        : 1;
	uint64_t dcnt1                        : 1;
	uint64_t dtime0                       : 1;
	uint64_t dtime1                       : 1;
	uint64_t psldbof                      : 1;
	uint64_t pidbof                       : 1;
	uint64_t pcnt                         : 1;
	uint64_t ptime                        : 1;
	uint64_t c0_aeri                      : 1;
	uint64_t reserved_20_20               : 1;
	uint64_t c0_se                        : 1;
	uint64_t reserved_22_22               : 1;
	uint64_t c0_wake                      : 1;
	uint64_t c0_pmei                      : 1;
	uint64_t c0_hpint                     : 1;
	uint64_t c1_aeri                      : 1;
	uint64_t reserved_27_27               : 1;
	uint64_t c1_se                        : 1;
	uint64_t reserved_29_29               : 1;
	uint64_t c1_wake                      : 1;
	uint64_t c1_pmei                      : 1;
	uint64_t c1_hpint                     : 1;
	uint64_t c0_up_b0                     : 1;
	uint64_t c0_up_b1                     : 1;
	uint64_t c0_up_b2                     : 1;
	uint64_t c0_up_wi                     : 1;
	uint64_t c0_up_bx                     : 1;
	uint64_t c0_un_b0                     : 1;
	uint64_t c0_un_b1                     : 1;
	uint64_t c0_un_b2                     : 1;
	uint64_t c0_un_wi                     : 1;
	uint64_t c0_un_bx                     : 1;
	uint64_t c1_up_b0                     : 1;
	uint64_t c1_up_b1                     : 1;
	uint64_t c1_up_b2                     : 1;
	uint64_t c1_up_wi                     : 1;
	uint64_t c1_up_bx                     : 1;
	uint64_t c1_un_b0                     : 1;
	uint64_t c1_un_b1                     : 1;
	uint64_t c1_un_b2                     : 1;
	uint64_t c1_un_wi                     : 1;
	uint64_t c1_un_bx                     : 1;
	uint64_t c0_un_wf                     : 1;
	uint64_t c1_un_wf                     : 1;
	uint64_t c0_up_wf                     : 1;
	uint64_t c1_up_wf                     : 1;
	uint64_t c0_exc                       : 1;
	uint64_t c1_exc                       : 1;
	uint64_t c0_ldwn                      : 1;
	uint64_t c1_ldwn                      : 1;
	uint64_t reserved_61_62               : 2;
	uint64_t mio_inta                     : 1;
#endif
	} cn56xxp1;
};
typedef union cvmx_npei_int_enb cvmx_npei_int_enb_t;

/**
 * cvmx_npei_int_enb2
 *
 * NPEI_INTERRUPT_ENB2 = NPI's Interrupt Enable2 Register
 *
 * Used to enable the various interrupting conditions of NPI
 */
union cvmx_npei_int_enb2 {
	uint64_t u64;
	struct cvmx_npei_int_enb2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_62_63               : 2;
	uint64_t int_a                        : 1;  /**< Enables NPEI_INT_SUM2[61] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_ldwn                      : 1;  /**< Enables NPEI_INT_SUM[60] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_ldwn                      : 1;  /**< Enables NPEI_INT_SUM[59] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_exc                       : 1;  /**< Enables NPEI_INT_SUM[58] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_exc                       : 1;  /**< Enables NPEI_INT_SUM[57] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_up_wf                     : 1;  /**< Enables NPEI_INT_SUM[56] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_up_wf                     : 1;  /**< Enables NPEI_INT_SUM[55] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_un_wf                     : 1;  /**< Enables NPEI_INT_SUM[54] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_un_wf                     : 1;  /**< Enables NPEI_INT_SUM[53] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_un_bx                     : 1;  /**< Enables NPEI_INT_SUM[52] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_un_wi                     : 1;  /**< Enables NPEI_INT_SUM[51] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_un_b2                     : 1;  /**< Enables NPEI_INT_SUM[50] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_un_b1                     : 1;  /**< Enables NPEI_INT_SUM[49] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_un_b0                     : 1;  /**< Enables NPEI_INT_SUM[48] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_up_bx                     : 1;  /**< Enables NPEI_INT_SUM[47] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_up_wi                     : 1;  /**< Enables NPEI_INT_SUM[46] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_up_b2                     : 1;  /**< Enables NPEI_INT_SUM[45] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_up_b1                     : 1;  /**< Enables NPEI_INT_SUM[44] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_up_b0                     : 1;  /**< Enables NPEI_INT_SUM[43] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_un_bx                     : 1;  /**< Enables NPEI_INT_SUM[42] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_un_wi                     : 1;  /**< Enables NPEI_INT_SUM[41] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_un_b2                     : 1;  /**< Enables NPEI_INT_SUM[40] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_un_b1                     : 1;  /**< Enables NPEI_INT_SUM[39] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_un_b0                     : 1;  /**< Enables NPEI_INT_SUM[38] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_up_bx                     : 1;  /**< Enables NPEI_INT_SUM[37] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_up_wi                     : 1;  /**< Enables NPEI_INT_SUM[36] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_up_b2                     : 1;  /**< Enables NPEI_INT_SUM[35] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_up_b1                     : 1;  /**< Enables NPEI_INT_SUM[34] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_up_b0                     : 1;  /**< Enables NPEI_INT_SUM[33] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_hpint                     : 1;  /**< Enables NPEI_INT_SUM[32] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_pmei                      : 1;  /**< Enables NPEI_INT_SUM[31] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_wake                      : 1;  /**< Enables NPEI_INT_SUM[30] to generate an
                                                         interrupt on the RSL. */
	uint64_t crs1_dr                      : 1;  /**< Enables NPEI_INT_SUM2[29] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_se                        : 1;  /**< Enables NPEI_INT_SUM[28] to generate an
                                                         interrupt on the RSL. */
	uint64_t crs1_er                      : 1;  /**< Enables NPEI_INT_SUM2[27] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_aeri                      : 1;  /**< Enables NPEI_INT_SUM[26] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_hpint                     : 1;  /**< Enables NPEI_INT_SUM[25] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_pmei                      : 1;  /**< Enables NPEI_INT_SUM[24] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_wake                      : 1;  /**< Enables NPEI_INT_SUM[23] to generate an
                                                         interrupt on the RSL. */
	uint64_t crs0_dr                      : 1;  /**< Enables NPEI_INT_SUM2[22] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_se                        : 1;  /**< Enables NPEI_INT_SUM[21] to generate an
                                                         interrupt on the RSL. */
	uint64_t crs0_er                      : 1;  /**< Enables NPEI_INT_SUM2[20] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_aeri                      : 1;  /**< Enables NPEI_INT_SUM[19] to generate an
                                                         interrupt on the RSL. */
	uint64_t ptime                        : 1;  /**< Enables NPEI_INT_SUM[18] to generate an
                                                         interrupt on the RSL. */
	uint64_t pcnt                         : 1;  /**< Enables NPEI_INT_SUM[17] to generate an
                                                         interrupt on the RSL. */
	uint64_t pidbof                       : 1;  /**< Enables NPEI_INT_SUM[16] to generate an
                                                         interrupt on the RSL. */
	uint64_t psldbof                      : 1;  /**< Enables NPEI_INT_SUM[15] to generate an
                                                         interrupt on the RSL. */
	uint64_t dtime1                       : 1;  /**< Enables NPEI_INT_SUM[14] to generate an
                                                         interrupt on the RSL. */
	uint64_t dtime0                       : 1;  /**< Enables NPEI_INT_SUM[13] to generate an
                                                         interrupt on the RSL. */
	uint64_t dcnt1                        : 1;  /**< Enables NPEI_INT_SUM[12] to generate an
                                                         interrupt on the RSL. */
	uint64_t dcnt0                        : 1;  /**< Enables NPEI_INT_SUM[11] to generate an
                                                         interrupt on the RSL. */
	uint64_t dma1fi                       : 1;  /**< Enables NPEI_INT_SUM[10] to generate an
                                                         interrupt on the RSL. */
	uint64_t dma0fi                       : 1;  /**< Enables NPEI_INT_SUM[9] to generate an
                                                         interrupt on the RSL. */
	uint64_t dma4dbo                      : 1;  /**< Enables NPEI_INT_SUM[8] to generate an
                                                         interrupt on the RSL. */
	uint64_t dma3dbo                      : 1;  /**< Enables NPEI_INT_SUM[7] to generate an
                                                         interrupt on the RSL. */
	uint64_t dma2dbo                      : 1;  /**< Enables NPEI_INT_SUM[6] to generate an
                                                         interrupt on the RSL. */
	uint64_t dma1dbo                      : 1;  /**< Enables NPEI_INT_SUM[5] to generate an
                                                         interrupt on the RSL. */
	uint64_t dma0dbo                      : 1;  /**< Enables NPEI_INT_SUM[4] to generate an
                                                         interrupt on the RSL. */
	uint64_t iob2big                      : 1;  /**< Enables NPEI_INT_SUM[3] to generate an
                                                         interrupt on the RSL. */
	uint64_t bar0_to                      : 1;  /**< Enables NPEI_INT_SUM[2] to generate an
                                                         interrupt on the RSL. */
	uint64_t rml_wto                      : 1;  /**< Enables NPEI_INT_SUM[1] to generate an
                                                         interrupt on the RSL. */
	uint64_t rml_rto                      : 1;  /**< Enables NPEI_INT_UM[0] to generate an
                                                         interrupt on the RSL. */
#else
	uint64_t rml_rto                      : 1;
	uint64_t rml_wto                      : 1;
	uint64_t bar0_to                      : 1;
	uint64_t iob2big                      : 1;
	uint64_t dma0dbo                      : 1;
	uint64_t dma1dbo                      : 1;
	uint64_t dma2dbo                      : 1;
	uint64_t dma3dbo                      : 1;
	uint64_t dma4dbo                      : 1;
	uint64_t dma0fi                       : 1;
	uint64_t dma1fi                       : 1;
	uint64_t dcnt0                        : 1;
	uint64_t dcnt1                        : 1;
	uint64_t dtime0                       : 1;
	uint64_t dtime1                       : 1;
	uint64_t psldbof                      : 1;
	uint64_t pidbof                       : 1;
	uint64_t pcnt                         : 1;
	uint64_t ptime                        : 1;
	uint64_t c0_aeri                      : 1;
	uint64_t crs0_er                      : 1;
	uint64_t c0_se                        : 1;
	uint64_t crs0_dr                      : 1;
	uint64_t c0_wake                      : 1;
	uint64_t c0_pmei                      : 1;
	uint64_t c0_hpint                     : 1;
	uint64_t c1_aeri                      : 1;
	uint64_t crs1_er                      : 1;
	uint64_t c1_se                        : 1;
	uint64_t crs1_dr                      : 1;
	uint64_t c1_wake                      : 1;
	uint64_t c1_pmei                      : 1;
	uint64_t c1_hpint                     : 1;
	uint64_t c0_up_b0                     : 1;
	uint64_t c0_up_b1                     : 1;
	uint64_t c0_up_b2                     : 1;
	uint64_t c0_up_wi                     : 1;
	uint64_t c0_up_bx                     : 1;
	uint64_t c0_un_b0                     : 1;
	uint64_t c0_un_b1                     : 1;
	uint64_t c0_un_b2                     : 1;
	uint64_t c0_un_wi                     : 1;
	uint64_t c0_un_bx                     : 1;
	uint64_t c1_up_b0                     : 1;
	uint64_t c1_up_b1                     : 1;
	uint64_t c1_up_b2                     : 1;
	uint64_t c1_up_wi                     : 1;
	uint64_t c1_up_bx                     : 1;
	uint64_t c1_un_b0                     : 1;
	uint64_t c1_un_b1                     : 1;
	uint64_t c1_un_b2                     : 1;
	uint64_t c1_un_wi                     : 1;
	uint64_t c1_un_bx                     : 1;
	uint64_t c0_un_wf                     : 1;
	uint64_t c1_un_wf                     : 1;
	uint64_t c0_up_wf                     : 1;
	uint64_t c1_up_wf                     : 1;
	uint64_t c0_exc                       : 1;
	uint64_t c1_exc                       : 1;
	uint64_t c0_ldwn                      : 1;
	uint64_t c1_ldwn                      : 1;
	uint64_t int_a                        : 1;
	uint64_t reserved_62_63               : 2;
#endif
	} s;
	struct cvmx_npei_int_enb2_s           cn52xx;
	struct cvmx_npei_int_enb2_cn52xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_62_63               : 2;
	uint64_t int_a                        : 1;  /**< Enables NPEI_INT_SUM2[61] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_ldwn                      : 1;  /**< Enables NPEI_INT_SUM2[60] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_ldwn                      : 1;  /**< Enables NPEI_INT_SUM2[59] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_exc                       : 1;  /**< Enables NPEI_INT_SUM2[58] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_exc                       : 1;  /**< Enables NPEI_INT_SUM2[57] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_up_wf                     : 1;  /**< Enables NPEI_INT_SUM2[56] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_up_wf                     : 1;  /**< Enables NPEI_INT_SUM2[55] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_un_wf                     : 1;  /**< Enables NPEI_INT_SUM2[54] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_un_wf                     : 1;  /**< Enables NPEI_INT_SUM2[53] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_un_bx                     : 1;  /**< Enables NPEI_INT_SUM2[52] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_un_wi                     : 1;  /**< Enables NPEI_INT_SUM2[51] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_un_b2                     : 1;  /**< Enables NPEI_INT_SUM2[50] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_un_b1                     : 1;  /**< Enables NPEI_INT_SUM2[49] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_un_b0                     : 1;  /**< Enables NPEI_INT_SUM2[48] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_up_bx                     : 1;  /**< Enables NPEI_INT_SUM2[47] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_up_wi                     : 1;  /**< Enables NPEI_INT_SUM2[46] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_up_b2                     : 1;  /**< Enables NPEI_INT_SUM2[45] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_up_b1                     : 1;  /**< Enables NPEI_INT_SUM2[44] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_up_b0                     : 1;  /**< Enables NPEI_INT_SUM2[43] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_un_bx                     : 1;  /**< Enables NPEI_INT_SUM2[42] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_un_wi                     : 1;  /**< Enables NPEI_INT_SUM2[41] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_un_b2                     : 1;  /**< Enables NPEI_INT_SUM2[40] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_un_b1                     : 1;  /**< Enables NPEI_INT_SUM2[39] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_un_b0                     : 1;  /**< Enables NPEI_INT_SUM2[38] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_up_bx                     : 1;  /**< Enables NPEI_INT_SUM2[37] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_up_wi                     : 1;  /**< Enables NPEI_INT_SUM2[36] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_up_b2                     : 1;  /**< Enables NPEI_INT_SUM2[35] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_up_b1                     : 1;  /**< Enables NPEI_INT_SUM2[34] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_up_b0                     : 1;  /**< Enables NPEI_INT_SUM2[33] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_hpint                     : 1;  /**< Enables NPEI_INT_SUM2[32] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_pmei                      : 1;  /**< Enables NPEI_INT_SUM2[31] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_wake                      : 1;  /**< Enables NPEI_INT_SUM2[30] to generate an
                                                         interrupt on the RSL. */
	uint64_t crs1_dr                      : 1;  /**< Enables NPEI_INT_SUM2[29] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_se                        : 1;  /**< Enables NPEI_INT_SUM2[28] to generate an
                                                         interrupt on the RSL. */
	uint64_t crs1_er                      : 1;  /**< Enables NPEI_INT_SUM2[27] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_aeri                      : 1;  /**< Enables NPEI_INT_SUM2[26] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_hpint                     : 1;  /**< Enables NPEI_INT_SUM2[25] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_pmei                      : 1;  /**< Enables NPEI_INT_SUM2[24] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_wake                      : 1;  /**< Enables NPEI_INT_SUM2[23] to generate an
                                                         interrupt on the RSL. */
	uint64_t crs0_dr                      : 1;  /**< Enables NPEI_INT_SUM2[22] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_se                        : 1;  /**< Enables NPEI_INT_SUM2[21] to generate an
                                                         interrupt on the RSL. */
	uint64_t crs0_er                      : 1;  /**< Enables NPEI_INT_SUM2[20] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_aeri                      : 1;  /**< Enables NPEI_INT_SUM2[19] to generate an
                                                         interrupt on the RSL. */
	uint64_t ptime                        : 1;  /**< Enables NPEI_INT_SUM2[18] to generate an
                                                         interrupt on the RSL. */
	uint64_t pcnt                         : 1;  /**< Enables NPEI_INT_SUM2[17] to generate an
                                                         interrupt on the RSL. */
	uint64_t pidbof                       : 1;  /**< Enables NPEI_INT_SUM2[16] to generate an
                                                         interrupt on the RSL. */
	uint64_t psldbof                      : 1;  /**< Enables NPEI_INT_SUM2[15] to generate an
                                                         interrupt on the RSL. */
	uint64_t dtime1                       : 1;  /**< Enables NPEI_INT_SUM2[14] to generate an
                                                         interrupt on the RSL. */
	uint64_t dtime0                       : 1;  /**< Enables NPEI_INT_SUM2[13] to generate an
                                                         interrupt on the RSL. */
	uint64_t dcnt1                        : 1;  /**< Enables NPEI_INT_SUM2[12] to generate an
                                                         interrupt on the RSL. */
	uint64_t dcnt0                        : 1;  /**< Enables NPEI_INT_SUM2[11] to generate an
                                                         interrupt on the RSL. */
	uint64_t dma1fi                       : 1;  /**< Enables NPEI_INT_SUM2[10] to generate an
                                                         interrupt on the RSL. */
	uint64_t dma0fi                       : 1;  /**< Enables NPEI_INT_SUM2[9] to generate an
                                                         interrupt on the RSL. */
	uint64_t reserved_8_8                 : 1;
	uint64_t dma3dbo                      : 1;  /**< Enables NPEI_INT_SUM2[7] to generate an
                                                         interrupt on the RSL. */
	uint64_t dma2dbo                      : 1;  /**< Enables NPEI_INT_SUM2[6] to generate an
                                                         interrupt on the RSL. */
	uint64_t dma1dbo                      : 1;  /**< Enables NPEI_INT_SUM2[5] to generate an
                                                         interrupt on the RSL. */
	uint64_t dma0dbo                      : 1;  /**< Enables NPEI_INT_SUM2[4] to generate an
                                                         interrupt on the RSL. */
	uint64_t iob2big                      : 1;  /**< Enables NPEI_INT_SUM2[3] to generate an
                                                         interrupt on the RSL. */
	uint64_t bar0_to                      : 1;  /**< Enables NPEI_INT_SUM2[2] to generate an
                                                         interrupt on the RSL. */
	uint64_t rml_wto                      : 1;  /**< Enables NPEI_INT_SUM2[1] to generate an
                                                         interrupt on the RSL. */
	uint64_t rml_rto                      : 1;  /**< Enables NPEI_INT_SUM2[0] to generate an
                                                         interrupt on the RSL. */
#else
	uint64_t rml_rto                      : 1;
	uint64_t rml_wto                      : 1;
	uint64_t bar0_to                      : 1;
	uint64_t iob2big                      : 1;
	uint64_t dma0dbo                      : 1;
	uint64_t dma1dbo                      : 1;
	uint64_t dma2dbo                      : 1;
	uint64_t dma3dbo                      : 1;
	uint64_t reserved_8_8                 : 1;
	uint64_t dma0fi                       : 1;
	uint64_t dma1fi                       : 1;
	uint64_t dcnt0                        : 1;
	uint64_t dcnt1                        : 1;
	uint64_t dtime0                       : 1;
	uint64_t dtime1                       : 1;
	uint64_t psldbof                      : 1;
	uint64_t pidbof                       : 1;
	uint64_t pcnt                         : 1;
	uint64_t ptime                        : 1;
	uint64_t c0_aeri                      : 1;
	uint64_t crs0_er                      : 1;
	uint64_t c0_se                        : 1;
	uint64_t crs0_dr                      : 1;
	uint64_t c0_wake                      : 1;
	uint64_t c0_pmei                      : 1;
	uint64_t c0_hpint                     : 1;
	uint64_t c1_aeri                      : 1;
	uint64_t crs1_er                      : 1;
	uint64_t c1_se                        : 1;
	uint64_t crs1_dr                      : 1;
	uint64_t c1_wake                      : 1;
	uint64_t c1_pmei                      : 1;
	uint64_t c1_hpint                     : 1;
	uint64_t c0_up_b0                     : 1;
	uint64_t c0_up_b1                     : 1;
	uint64_t c0_up_b2                     : 1;
	uint64_t c0_up_wi                     : 1;
	uint64_t c0_up_bx                     : 1;
	uint64_t c0_un_b0                     : 1;
	uint64_t c0_un_b1                     : 1;
	uint64_t c0_un_b2                     : 1;
	uint64_t c0_un_wi                     : 1;
	uint64_t c0_un_bx                     : 1;
	uint64_t c1_up_b0                     : 1;
	uint64_t c1_up_b1                     : 1;
	uint64_t c1_up_b2                     : 1;
	uint64_t c1_up_wi                     : 1;
	uint64_t c1_up_bx                     : 1;
	uint64_t c1_un_b0                     : 1;
	uint64_t c1_un_b1                     : 1;
	uint64_t c1_un_b2                     : 1;
	uint64_t c1_un_wi                     : 1;
	uint64_t c1_un_bx                     : 1;
	uint64_t c0_un_wf                     : 1;
	uint64_t c1_un_wf                     : 1;
	uint64_t c0_up_wf                     : 1;
	uint64_t c1_up_wf                     : 1;
	uint64_t c0_exc                       : 1;
	uint64_t c1_exc                       : 1;
	uint64_t c0_ldwn                      : 1;
	uint64_t c1_ldwn                      : 1;
	uint64_t int_a                        : 1;
	uint64_t reserved_62_63               : 2;
#endif
	} cn52xxp1;
	struct cvmx_npei_int_enb2_s           cn56xx;
	struct cvmx_npei_int_enb2_cn56xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_61_63               : 3;
	uint64_t c1_ldwn                      : 1;  /**< Enables NPEI_INT_SUM[60] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_ldwn                      : 1;  /**< Enables NPEI_INT_SUM[59] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_exc                       : 1;  /**< Enables NPEI_INT_SUM[58] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_exc                       : 1;  /**< Enables NPEI_INT_SUM[57] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_up_wf                     : 1;  /**< Enables NPEI_INT_SUM[56] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_up_wf                     : 1;  /**< Enables NPEI_INT_SUM[55] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_un_wf                     : 1;  /**< Enables NPEI_INT_SUM[54] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_un_wf                     : 1;  /**< Enables NPEI_INT_SUM[53] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_un_bx                     : 1;  /**< Enables NPEI_INT_SUM[52] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_un_wi                     : 1;  /**< Enables NPEI_INT_SUM[51] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_un_b2                     : 1;  /**< Enables NPEI_INT_SUM[50] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_un_b1                     : 1;  /**< Enables NPEI_INT_SUM[49] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_un_b0                     : 1;  /**< Enables NPEI_INT_SUM[48] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_up_bx                     : 1;  /**< Enables NPEI_INT_SUM[47] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_up_wi                     : 1;  /**< Enables NPEI_INT_SUM[46] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_up_b2                     : 1;  /**< Enables NPEI_INT_SUM[45] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_up_b1                     : 1;  /**< Enables NPEI_INT_SUM[44] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_up_b0                     : 1;  /**< Enables NPEI_INT_SUM[43] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_un_bx                     : 1;  /**< Enables NPEI_INT_SUM[42] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_un_wi                     : 1;  /**< Enables NPEI_INT_SUM[41] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_un_b2                     : 1;  /**< Enables NPEI_INT_SUM[40] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_un_b1                     : 1;  /**< Enables NPEI_INT_SUM[39] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_un_b0                     : 1;  /**< Enables NPEI_INT_SUM[38] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_up_bx                     : 1;  /**< Enables NPEI_INT_SUM[37] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_up_wi                     : 1;  /**< Enables NPEI_INT_SUM[36] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_up_b2                     : 1;  /**< Enables NPEI_INT_SUM[35] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_up_b1                     : 1;  /**< Enables NPEI_INT_SUM[34] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_up_b0                     : 1;  /**< Enables NPEI_INT_SUM[33] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_hpint                     : 1;  /**< Enables NPEI_INT_SUM[32] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_pmei                      : 1;  /**< Enables NPEI_INT_SUM[31] to generate an
                                                         interrupt on the RSL. */
	uint64_t c1_wake                      : 1;  /**< Enables NPEI_INT_SUM[30] to generate an
                                                         interrupt on the RSL. */
	uint64_t reserved_29_29               : 1;
	uint64_t c1_se                        : 1;  /**< Enables NPEI_INT_SUM[28] to generate an
                                                         interrupt on the RSL. */
	uint64_t reserved_27_27               : 1;
	uint64_t c1_aeri                      : 1;  /**< Enables NPEI_INT_SUM[26] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_hpint                     : 1;  /**< Enables NPEI_INT_SUM[25] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_pmei                      : 1;  /**< Enables NPEI_INT_SUM[24] to generate an
                                                         interrupt on the RSL. */
	uint64_t c0_wake                      : 1;  /**< Enables NPEI_INT_SUM[23] to generate an
                                                         interrupt on the RSL. */
	uint64_t reserved_22_22               : 1;
	uint64_t c0_se                        : 1;  /**< Enables NPEI_INT_SUM[21] to generate an
                                                         interrupt on the RSL. */
	uint64_t reserved_20_20               : 1;
	uint64_t c0_aeri                      : 1;  /**< Enables NPEI_INT_SUM[19] to generate an
                                                         interrupt on the RSL. */
	uint64_t ptime                        : 1;  /**< Enables NPEI_INT_SUM[18] to generate an
                                                         interrupt on the RSL. */
	uint64_t pcnt                         : 1;  /**< Enables NPEI_INT_SUM[17] to generate an
                                                         interrupt on the RSL. */
	uint64_t pidbof                       : 1;  /**< Enables NPEI_INT_SUM[16] to generate an
                                                         interrupt on the RSL. */
	uint64_t psldbof                      : 1;  /**< Enables NPEI_INT_SUM[15] to generate an
                                                         interrupt on the RSL. */
	uint64_t dtime1                       : 1;  /**< Enables NPEI_INT_SUM[14] to generate an
                                                         interrupt on the RSL. */
	uint64_t dtime0                       : 1;  /**< Enables NPEI_INT_SUM[13] to generate an
                                                         interrupt on the RSL. */
	uint64_t dcnt1                        : 1;  /**< Enables NPEI_INT_SUM[12] to generate an
                                                         interrupt on the RSL. */
	uint64_t dcnt0                        : 1;  /**< Enables NPEI_INT_SUM[11] to generate an
                                                         interrupt on the RSL. */
	uint64_t dma1fi                       : 1;  /**< Enables NPEI_INT_SUM[10] to generate an
                                                         interrupt on the RSL. */
	uint64_t dma0fi                       : 1;  /**< Enables NPEI_INT_SUM[9] to generate an
                                                         interrupt on the RSL. */
	uint64_t dma4dbo                      : 1;  /**< Enables NPEI_INT_SUM[8] to generate an
                                                         interrupt on the RSL. */
	uint64_t dma3dbo                      : 1;  /**< Enables NPEI_INT_SUM[7] to generate an
                                                         interrupt on the RSL. */
	uint64_t dma2dbo                      : 1;  /**< Enables NPEI_INT_SUM[6] to generate an
                                                         interrupt on the RSL. */
	uint64_t dma1dbo                      : 1;  /**< Enables NPEI_INT_SUM[5] to generate an
                                                         interrupt on the RSL. */
	uint64_t dma0dbo                      : 1;  /**< Enables NPEI_INT_SUM[4] to generate an
                                                         interrupt on the RSL. */
	uint64_t iob2big                      : 1;  /**< Enables NPEI_INT_SUM[3] to generate an
                                                         interrupt on the RSL. */
	uint64_t bar0_to                      : 1;  /**< Enables NPEI_INT_SUM[2] to generate an
                                                         interrupt on the RSL. */
	uint64_t rml_wto                      : 1;  /**< Enables NPEI_INT_SUM[1] to generate an
                                                         interrupt on the RSL. */
	uint64_t rml_rto                      : 1;  /**< Enables NPEI_INT_UM[0] to generate an
                                                         interrupt on the RSL. */
#else
	uint64_t rml_rto                      : 1;
	uint64_t rml_wto                      : 1;
	uint64_t bar0_to                      : 1;
	uint64_t iob2big                      : 1;
	uint64_t dma0dbo                      : 1;
	uint64_t dma1dbo                      : 1;
	uint64_t dma2dbo                      : 1;
	uint64_t dma3dbo                      : 1;
	uint64_t dma4dbo                      : 1;
	uint64_t dma0fi                       : 1;
	uint64_t dma1fi                       : 1;
	uint64_t dcnt0                        : 1;
	uint64_t dcnt1                        : 1;
	uint64_t dtime0                       : 1;
	uint64_t dtime1                       : 1;
	uint64_t psldbof                      : 1;
	uint64_t pidbof                       : 1;
	uint64_t pcnt                         : 1;
	uint64_t ptime                        : 1;
	uint64_t c0_aeri                      : 1;
	uint64_t reserved_20_20               : 1;
	uint64_t c0_se                        : 1;
	uint64_t reserved_22_22               : 1;
	uint64_t c0_wake                      : 1;
	uint64_t c0_pmei                      : 1;
	uint64_t c0_hpint                     : 1;
	uint64_t c1_aeri                      : 1;
	uint64_t reserved_27_27               : 1;
	uint64_t c1_se                        : 1;
	uint64_t reserved_29_29               : 1;
	uint64_t c1_wake                      : 1;
	uint64_t c1_pmei                      : 1;
	uint64_t c1_hpint                     : 1;
	uint64_t c0_up_b0                     : 1;
	uint64_t c0_up_b1                     : 1;
	uint64_t c0_up_b2                     : 1;
	uint64_t c0_up_wi                     : 1;
	uint64_t c0_up_bx                     : 1;
	uint64_t c0_un_b0                     : 1;
	uint64_t c0_un_b1                     : 1;
	uint64_t c0_un_b2                     : 1;
	uint64_t c0_un_wi                     : 1;
	uint64_t c0_un_bx                     : 1;
	uint64_t c1_up_b0                     : 1;
	uint64_t c1_up_b1                     : 1;
	uint64_t c1_up_b2                     : 1;
	uint64_t c1_up_wi                     : 1;
	uint64_t c1_up_bx                     : 1;
	uint64_t c1_un_b0                     : 1;
	uint64_t c1_un_b1                     : 1;
	uint64_t c1_un_b2                     : 1;
	uint64_t c1_un_wi                     : 1;
	uint64_t c1_un_bx                     : 1;
	uint64_t c0_un_wf                     : 1;
	uint64_t c1_un_wf                     : 1;
	uint64_t c0_up_wf                     : 1;
	uint64_t c1_up_wf                     : 1;
	uint64_t c0_exc                       : 1;
	uint64_t c1_exc                       : 1;
	uint64_t c0_ldwn                      : 1;
	uint64_t c1_ldwn                      : 1;
	uint64_t reserved_61_63               : 3;
#endif
	} cn56xxp1;
};
typedef union cvmx_npei_int_enb2 cvmx_npei_int_enb2_t;

/**
 * cvmx_npei_int_info
 *
 * NPEI_INT_INFO = NPI Interrupt Information
 *
 * Contains information about some of the interrupt condition that can occur in the NPEI_INTERRUPT_SUM register.
 */
union cvmx_npei_int_info {
	uint64_t u64;
	struct cvmx_npei_int_info_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_12_63               : 52;
	uint64_t pidbof                       : 6;  /**< Field set when the NPEI_INTERRUPT_SUM[PIDBOF] bit
                                                         is set. This field when set will not change again
                                                         unitl NPEI_INTERRUPT_SUM[PIDBOF] is cleared. */
	uint64_t psldbof                      : 6;  /**< Field set when the NPEI_INTERRUPT_SUM[PSLDBOF] bit
                                                         is set. This field when set will not change again
                                                         unitl NPEI_INTERRUPT_SUM[PSLDBOF] is cleared. */
#else
	uint64_t psldbof                      : 6;
	uint64_t pidbof                       : 6;
	uint64_t reserved_12_63               : 52;
#endif
	} s;
	struct cvmx_npei_int_info_s           cn52xx;
	struct cvmx_npei_int_info_s           cn56xx;
	struct cvmx_npei_int_info_s           cn56xxp1;
};
typedef union cvmx_npei_int_info cvmx_npei_int_info_t;

/**
 * cvmx_npei_int_sum
 *
 * NPEI_INTERRUPT_SUM = NPI Interrupt Summary Register
 *
 * Set when an interrupt condition occurs, write '1' to clear.
 *
 * HACK: These used to exist, how are TO handled?
 *  <3>     PO0_2SML R/W1C    0x0         0         The packet being sent out on Port0 is smaller          $R     NS
 *                                                            than the NPI_BUFF_SIZE_OUTPUT0[ISIZE] field.
 * <7>     I0_RTOUT R/W1C    0x0         0         Port-0 had a read timeout while attempting to          $R     NS
 *                                                 read instructions.
 * <15>    P0_RTOUT R/W1C    0x0         0         Port-0 had a read timeout while attempting to          $R     NS
 *                                                 read packet data.
 * <23>    G0_RTOUT R/W1C    0x0         0         Port-0 had a read timeout while attempting to          $R     NS
 *                                                 read a gather list.
 * <31>    P0_PTOUT R/W1C    0x0         0         Port-0 output had a read timeout on a DATA/INFO         $R     NS
 *                                                 pair.
 */
union cvmx_npei_int_sum {
	uint64_t u64;
	struct cvmx_npei_int_sum_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t mio_inta                     : 1;  /**< Interrupt from MIO. */
	uint64_t reserved_62_62               : 1;
	uint64_t int_a                        : 1;  /**< Set when a bit in the NPEI_INT_A_SUM register and
                                                         the cooresponding bit in the NPEI_INT_A_ENB
                                                         register is set. */
	uint64_t c1_ldwn                      : 1;  /**< Reset request due to link1 down status. */
	uint64_t c0_ldwn                      : 1;  /**< Reset request due to link0 down status. */
	uint64_t c1_exc                       : 1;  /**< Set when the PESC1_DBG_INFO register has a bit
                                                         set and its cooresponding PESC1_DBG_INFO_EN bit
                                                         is set. */
	uint64_t c0_exc                       : 1;  /**< Set when the PESC0_DBG_INFO register has a bit
                                                         set and its cooresponding PESC0_DBG_INFO_EN bit
                                                         is set. */
	uint64_t c1_up_wf                     : 1;  /**< Received Unsupported P-TLP for filtered window
                                                         register. Core1. */
	uint64_t c0_up_wf                     : 1;  /**< Received Unsupported P-TLP for filtered window
                                                         register. Core0. */
	uint64_t c1_un_wf                     : 1;  /**< Received Unsupported N-TLP for filtered window
                                                         register. Core1. */
	uint64_t c0_un_wf                     : 1;  /**< Received Unsupported N-TLP for filtered window
                                                         register. Core0. */
	uint64_t c1_un_bx                     : 1;  /**< Received Unsupported N-TLP for unknown Bar.
                                                         Core 1. */
	uint64_t c1_un_wi                     : 1;  /**< Received Unsupported N-TLP for Window Register.
                                                         Core 1. */
	uint64_t c1_un_b2                     : 1;  /**< Received Unsupported N-TLP for Bar2.
                                                         Core 1. */
	uint64_t c1_un_b1                     : 1;  /**< Received Unsupported N-TLP for Bar1.
                                                         Core 1. */
	uint64_t c1_un_b0                     : 1;  /**< Received Unsupported N-TLP for Bar0.
                                                         Core 1. */
	uint64_t c1_up_bx                     : 1;  /**< Received Unsupported P-TLP for unknown Bar.
                                                         Core 1. */
	uint64_t c1_up_wi                     : 1;  /**< Received Unsupported P-TLP for Window Register.
                                                         Core 1. */
	uint64_t c1_up_b2                     : 1;  /**< Received Unsupported P-TLP for Bar2.
                                                         Core 1. */
	uint64_t c1_up_b1                     : 1;  /**< Received Unsupported P-TLP for Bar1.
                                                         Core 1. */
	uint64_t c1_up_b0                     : 1;  /**< Received Unsupported P-TLP for Bar0.
                                                         Core 1. */
	uint64_t c0_un_bx                     : 1;  /**< Received Unsupported N-TLP for unknown Bar.
                                                         Core 0. */
	uint64_t c0_un_wi                     : 1;  /**< Received Unsupported N-TLP for Window Register.
                                                         Core 0. */
	uint64_t c0_un_b2                     : 1;  /**< Received Unsupported N-TLP for Bar2.
                                                         Core 0. */
	uint64_t c0_un_b1                     : 1;  /**< Received Unsupported N-TLP for Bar1.
                                                         Core 0. */
	uint64_t c0_un_b0                     : 1;  /**< Received Unsupported N-TLP for Bar0.
                                                         Core 0. */
	uint64_t c0_up_bx                     : 1;  /**< Received Unsupported P-TLP for unknown Bar.
                                                         Core 0. */
	uint64_t c0_up_wi                     : 1;  /**< Received Unsupported P-TLP for Window Register.
                                                         Core 0. */
	uint64_t c0_up_b2                     : 1;  /**< Received Unsupported P-TLP for Bar2.
                                                         Core 0. */
	uint64_t c0_up_b1                     : 1;  /**< Received Unsupported P-TLP for Bar1.
                                                         Core 0. */
	uint64_t c0_up_b0                     : 1;  /**< Received Unsupported P-TLP for Bar0.
                                                         Core 0. */
	uint64_t c1_hpint                     : 1;  /**< Hot-Plug Interrupt.
                                                         Pcie Core 1 (hp_int).
                                                         This interrupt will only be generated when
                                                         PCIERC1_CFG034[DLLS_C] is generated. Hot plug is
                                                         not supported. */
	uint64_t c1_pmei                      : 1;  /**< PME Interrupt.
                                                         Pcie Core 1. (cfg_pme_int) */
	uint64_t c1_wake                      : 1;  /**< Wake up from Power Management Unit.
                                                         Pcie Core 1. (wake_n)
                                                         Octeon will never generate this interrupt. */
	uint64_t crs1_dr                      : 1;  /**< Had a CRS when Retries were disabled. */
	uint64_t c1_se                        : 1;  /**< System Error, RC Mode Only.
                                                         Pcie Core 1. (cfg_sys_err_rc) */
	uint64_t crs1_er                      : 1;  /**< Had a CRS Timeout when Retries were enabled. */
	uint64_t c1_aeri                      : 1;  /**< Advanced Error Reporting Interrupt, RC Mode Only.
                                                         Pcie Core 1. */
	uint64_t c0_hpint                     : 1;  /**< Hot-Plug Interrupt.
                                                         Pcie Core 0 (hp_int).
                                                         This interrupt will only be generated when
                                                         PCIERC0_CFG034[DLLS_C] is generated. Hot plug is
                                                         not supported. */
	uint64_t c0_pmei                      : 1;  /**< PME Interrupt.
                                                         Pcie Core 0. (cfg_pme_int) */
	uint64_t c0_wake                      : 1;  /**< Wake up from Power Management Unit.
                                                         Pcie Core 0. (wake_n)
                                                         Octeon will never generate this interrupt. */
	uint64_t crs0_dr                      : 1;  /**< Had a CRS when Retries were disabled. */
	uint64_t c0_se                        : 1;  /**< System Error, RC Mode Only.
                                                         Pcie Core 0. (cfg_sys_err_rc) */
	uint64_t crs0_er                      : 1;  /**< Had a CRS Timeout when Retries were enabled. */
	uint64_t c0_aeri                      : 1;  /**< Advanced Error Reporting Interrupt, RC Mode Only.
                                                         Pcie Core 0 (cfg_aer_rc_err_int). */
	uint64_t ptime                        : 1;  /**< Packet Timer has an interrupt. Which rings can
                                                         be found in NPEI_PKT_TIME_INT. */
	uint64_t pcnt                         : 1;  /**< Packet Counter has an interrupt. Which rings can
                                                         be found in NPEI_PKT_CNT_INT. */
	uint64_t pidbof                       : 1;  /**< Packet Instruction Doorbell count overflowed. Which
                                                         doorbell can be found in NPEI_INT_INFO[PIDBOF] */
	uint64_t psldbof                      : 1;  /**< Packet Scatterlist Doorbell count overflowed. Which
                                                         doorbell can be found in NPEI_INT_INFO[PSLDBOF] */
	uint64_t dtime1                       : 1;  /**< Whenever NPEI_DMA_CNTS[DMA1] is not 0, the
                                                         DMA_CNT1 timer increments every core clock. When
                                                         DMA_CNT1 timer exceeds NPEI_DMA1_INT_LEVEL[TIME],
                                                         this bit is set. Writing a '1' to this bit also
                                                         clears the DMA_CNT1 timer. */
	uint64_t dtime0                       : 1;  /**< Whenever NPEI_DMA_CNTS[DMA0] is not 0, the
                                                         DMA_CNT0 timer increments every core clock. When
                                                         DMA_CNT0 timer exceeds NPEI_DMA0_INT_LEVEL[TIME],
                                                         this bit is set. Writing a '1' to this bit also
                                                         clears the DMA_CNT0 timer. */
	uint64_t dcnt1                        : 1;  /**< This bit indicates that NPEI_DMA_CNTS[DMA1] was/is
                                                         greater than NPEI_DMA1_INT_LEVEL[CNT]. */
	uint64_t dcnt0                        : 1;  /**< This bit indicates that NPEI_DMA_CNTS[DMA0] was/is
                                                         greater than NPEI_DMA0_INT_LEVEL[CNT]. */
	uint64_t dma1fi                       : 1;  /**< DMA0 set Forced Interrupt. */
	uint64_t dma0fi                       : 1;  /**< DMA0 set Forced Interrupt. */
	uint64_t dma4dbo                      : 1;  /**< DMA4 doorbell overflow.
                                                         Bit[32] of the doorbell count was set. */
	uint64_t dma3dbo                      : 1;  /**< DMA3 doorbell overflow.
                                                         Bit[32] of the doorbell count was set. */
	uint64_t dma2dbo                      : 1;  /**< DMA2 doorbell overflow.
                                                         Bit[32] of the doorbell count was set. */
	uint64_t dma1dbo                      : 1;  /**< DMA1 doorbell overflow.
                                                         Bit[32] of the doorbell count was set. */
	uint64_t dma0dbo                      : 1;  /**< DMA0 doorbell overflow.
                                                         Bit[32] of the doorbell count was set. */
	uint64_t iob2big                      : 1;  /**< A requested IOBDMA is to large. */
	uint64_t bar0_to                      : 1;  /**< BAR0 R/W to a NCB device did not receive
                                                         read-data/commit in 0xffff core clocks. */
	uint64_t rml_wto                      : 1;  /**< RML write did not get commit in 0xffff core clocks. */
	uint64_t rml_rto                      : 1;  /**< RML read did not return data in 0xffff core clocks. */
#else
	uint64_t rml_rto                      : 1;
	uint64_t rml_wto                      : 1;
	uint64_t bar0_to                      : 1;
	uint64_t iob2big                      : 1;
	uint64_t dma0dbo                      : 1;
	uint64_t dma1dbo                      : 1;
	uint64_t dma2dbo                      : 1;
	uint64_t dma3dbo                      : 1;
	uint64_t dma4dbo                      : 1;
	uint64_t dma0fi                       : 1;
	uint64_t dma1fi                       : 1;
	uint64_t dcnt0                        : 1;
	uint64_t dcnt1                        : 1;
	uint64_t dtime0                       : 1;
	uint64_t dtime1                       : 1;
	uint64_t psldbof                      : 1;
	uint64_t pidbof                       : 1;
	uint64_t pcnt                         : 1;
	uint64_t ptime                        : 1;
	uint64_t c0_aeri                      : 1;
	uint64_t crs0_er                      : 1;
	uint64_t c0_se                        : 1;
	uint64_t crs0_dr                      : 1;
	uint64_t c0_wake                      : 1;
	uint64_t c0_pmei                      : 1;
	uint64_t c0_hpint                     : 1;
	uint64_t c1_aeri                      : 1;
	uint64_t crs1_er                      : 1;
	uint64_t c1_se                        : 1;
	uint64_t crs1_dr                      : 1;
	uint64_t c1_wake                      : 1;
	uint64_t c1_pmei                      : 1;
	uint64_t c1_hpint                     : 1;
	uint64_t c0_up_b0                     : 1;
	uint64_t c0_up_b1                     : 1;
	uint64_t c0_up_b2                     : 1;
	uint64_t c0_up_wi                     : 1;
	uint64_t c0_up_bx                     : 1;
	uint64_t c0_un_b0                     : 1;
	uint64_t c0_un_b1                     : 1;
	uint64_t c0_un_b2                     : 1;
	uint64_t c0_un_wi                     : 1;
	uint64_t c0_un_bx                     : 1;
	uint64_t c1_up_b0                     : 1;
	uint64_t c1_up_b1                     : 1;
	uint64_t c1_up_b2                     : 1;
	uint64_t c1_up_wi                     : 1;
	uint64_t c1_up_bx                     : 1;
	uint64_t c1_un_b0                     : 1;
	uint64_t c1_un_b1                     : 1;
	uint64_t c1_un_b2                     : 1;
	uint64_t c1_un_wi                     : 1;
	uint64_t c1_un_bx                     : 1;
	uint64_t c0_un_wf                     : 1;
	uint64_t c1_un_wf                     : 1;
	uint64_t c0_up_wf                     : 1;
	uint64_t c1_up_wf                     : 1;
	uint64_t c0_exc                       : 1;
	uint64_t c1_exc                       : 1;
	uint64_t c0_ldwn                      : 1;
	uint64_t c1_ldwn                      : 1;
	uint64_t int_a                        : 1;
	uint64_t reserved_62_62               : 1;
	uint64_t mio_inta                     : 1;
#endif
	} s;
	struct cvmx_npei_int_sum_s            cn52xx;
	struct cvmx_npei_int_sum_cn52xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t mio_inta                     : 1;  /**< Interrupt from MIO. */
	uint64_t reserved_62_62               : 1;
	uint64_t int_a                        : 1;  /**< Set when a bit in the NPEI_INT_A_SUM register and
                                                         the cooresponding bit in the NPEI_INT_A_ENB
                                                         register is set. */
	uint64_t c1_ldwn                      : 1;  /**< Reset request due to link1 down status. */
	uint64_t c0_ldwn                      : 1;  /**< Reset request due to link0 down status. */
	uint64_t c1_exc                       : 1;  /**< Set when the PESC1_DBG_INFO register has a bit
                                                         set and its cooresponding PESC1_DBG_INFO_EN bit
                                                         is set. */
	uint64_t c0_exc                       : 1;  /**< Set when the PESC0_DBG_INFO register has a bit
                                                         set and its cooresponding PESC0_DBG_INFO_EN bit
                                                         is set. */
	uint64_t c1_up_wf                     : 1;  /**< Received Unsupported P-TLP for filtered window
                                                         register. Core1. */
	uint64_t c0_up_wf                     : 1;  /**< Received Unsupported P-TLP for filtered window
                                                         register. Core0. */
	uint64_t c1_un_wf                     : 1;  /**< Received Unsupported N-TLP for filtered window
                                                         register. Core1. */
	uint64_t c0_un_wf                     : 1;  /**< Received Unsupported N-TLP for filtered window
                                                         register. Core0. */
	uint64_t c1_un_bx                     : 1;  /**< Received Unsupported N-TLP for unknown Bar.
                                                         Core 1. */
	uint64_t c1_un_wi                     : 1;  /**< Received Unsupported N-TLP for Window Register.
                                                         Core 1. */
	uint64_t c1_un_b2                     : 1;  /**< Received Unsupported N-TLP for Bar2.
                                                         Core 1. */
	uint64_t c1_un_b1                     : 1;  /**< Received Unsupported N-TLP for Bar1.
                                                         Core 1. */
	uint64_t c1_un_b0                     : 1;  /**< Received Unsupported N-TLP for Bar0.
                                                         Core 1. */
	uint64_t c1_up_bx                     : 1;  /**< Received Unsupported P-TLP for unknown Bar.
                                                         Core 1. */
	uint64_t c1_up_wi                     : 1;  /**< Received Unsupported P-TLP for Window Register.
                                                         Core 1. */
	uint64_t c1_up_b2                     : 1;  /**< Received Unsupported P-TLP for Bar2.
                                                         Core 1. */
	uint64_t c1_up_b1                     : 1;  /**< Received Unsupported P-TLP for Bar1.
                                                         Core 1. */
	uint64_t c1_up_b0                     : 1;  /**< Received Unsupported P-TLP for Bar0.
                                                         Core 1. */
	uint64_t c0_un_bx                     : 1;  /**< Received Unsupported N-TLP for unknown Bar.
                                                         Core 0. */
	uint64_t c0_un_wi                     : 1;  /**< Received Unsupported N-TLP for Window Register.
                                                         Core 0. */
	uint64_t c0_un_b2                     : 1;  /**< Received Unsupported N-TLP for Bar2.
                                                         Core 0. */
	uint64_t c0_un_b1                     : 1;  /**< Received Unsupported N-TLP for Bar1.
                                                         Core 0. */
	uint64_t c0_un_b0                     : 1;  /**< Received Unsupported N-TLP for Bar0.
                                                         Core 0. */
	uint64_t c0_up_bx                     : 1;  /**< Received Unsupported P-TLP for unknown Bar.
                                                         Core 0. */
	uint64_t c0_up_wi                     : 1;  /**< Received Unsupported P-TLP for Window Register.
                                                         Core 0. */
	uint64_t c0_up_b2                     : 1;  /**< Received Unsupported P-TLP for Bar2.
                                                         Core 0. */
	uint64_t c0_up_b1                     : 1;  /**< Received Unsupported P-TLP for Bar1.
                                                         Core 0. */
	uint64_t c0_up_b0                     : 1;  /**< Received Unsupported P-TLP for Bar0.
                                                         Core 0. */
	uint64_t c1_hpint                     : 1;  /**< Hot-Plug Interrupt.
                                                         Pcie Core 1 (hp_int).
                                                         This interrupt will only be generated when
                                                         PCIERC1_CFG034[DLLS_C] is generated. Hot plug is
                                                         not supported. */
	uint64_t c1_pmei                      : 1;  /**< PME Interrupt.
                                                         Pcie Core 1. (cfg_pme_int) */
	uint64_t c1_wake                      : 1;  /**< Wake up from Power Management Unit.
                                                         Pcie Core 1. (wake_n)
                                                         Octeon will never generate this interrupt. */
	uint64_t crs1_dr                      : 1;  /**< Had a CRS when Retries were disabled. */
	uint64_t c1_se                        : 1;  /**< System Error, RC Mode Only.
                                                         Pcie Core 1. (cfg_sys_err_rc) */
	uint64_t crs1_er                      : 1;  /**< Had a CRS Timeout when Retries were enabled. */
	uint64_t c1_aeri                      : 1;  /**< Advanced Error Reporting Interrupt, RC Mode Only.
                                                         Pcie Core 1. */
	uint64_t c0_hpint                     : 1;  /**< Hot-Plug Interrupt.
                                                         Pcie Core 0 (hp_int).
                                                         This interrupt will only be generated when
                                                         PCIERC0_CFG034[DLLS_C] is generated. Hot plug is
                                                         not supported. */
	uint64_t c0_pmei                      : 1;  /**< PME Interrupt.
                                                         Pcie Core 0. (cfg_pme_int) */
	uint64_t c0_wake                      : 1;  /**< Wake up from Power Management Unit.
                                                         Pcie Core 0. (wake_n)
                                                         Octeon will never generate this interrupt. */
	uint64_t crs0_dr                      : 1;  /**< Had a CRS when Retries were disabled. */
	uint64_t c0_se                        : 1;  /**< System Error, RC Mode Only.
                                                         Pcie Core 0. (cfg_sys_err_rc) */
	uint64_t crs0_er                      : 1;  /**< Had a CRS Timeout when Retries were enabled. */
	uint64_t c0_aeri                      : 1;  /**< Advanced Error Reporting Interrupt, RC Mode Only.
                                                         Pcie Core 0 (cfg_aer_rc_err_int). */
	uint64_t reserved_15_18               : 4;
	uint64_t dtime1                       : 1;  /**< Whenever NPEI_DMA_CNTS[DMA1] is not 0, the
                                                         DMA_CNT1 timer increments every core clock. When
                                                         DMA_CNT1 timer exceeds NPEI_DMA1_INT_LEVEL[TIME],
                                                         this bit is set. Writing a '1' to this bit also
                                                         clears the DMA_CNT1 timer. */
	uint64_t dtime0                       : 1;  /**< Whenever NPEI_DMA_CNTS[DMA0] is not 0, the
                                                         DMA_CNT0 timer increments every core clock. When
                                                         DMA_CNT0 timer exceeds NPEI_DMA0_INT_LEVEL[TIME],
                                                         this bit is set. Writing a '1' to this bit also
                                                         clears the DMA_CNT0 timer. */
	uint64_t dcnt1                        : 1;  /**< This bit indicates that NPEI_DMA_CNTS[DMA1] was/is
                                                         greater than NPEI_DMA1_INT_LEVEL[CNT]. */
	uint64_t dcnt0                        : 1;  /**< This bit indicates that NPEI_DMA_CNTS[DMA0] was/is
                                                         greater than NPEI_DMA0_INT_LEVEL[CNT]. */
	uint64_t dma1fi                       : 1;  /**< DMA0 set Forced Interrupt. */
	uint64_t dma0fi                       : 1;  /**< DMA0 set Forced Interrupt. */
	uint64_t reserved_8_8                 : 1;
	uint64_t dma3dbo                      : 1;  /**< DMA3 doorbell count overflow.
                                                         Bit[32] of the doorbell count was set. */
	uint64_t dma2dbo                      : 1;  /**< DMA2 doorbell count overflow.
                                                         Bit[32] of the doorbell count was set. */
	uint64_t dma1dbo                      : 1;  /**< DMA1 doorbell count overflow.
                                                         Bit[32] of the doorbell count was set. */
	uint64_t dma0dbo                      : 1;  /**< DMA0 doorbell count overflow.
                                                         Bit[32] of the doorbell count was set. */
	uint64_t iob2big                      : 1;  /**< A requested IOBDMA is to large. */
	uint64_t bar0_to                      : 1;  /**< BAR0 R/W to a NCB device did not receive
                                                         read-data/commit in 0xffff core clocks. */
	uint64_t rml_wto                      : 1;  /**< RML write did not get commit in 0xffff core clocks. */
	uint64_t rml_rto                      : 1;  /**< RML read did not return data in 0xffff core clocks. */
#else
	uint64_t rml_rto                      : 1;
	uint64_t rml_wto                      : 1;
	uint64_t bar0_to                      : 1;
	uint64_t iob2big                      : 1;
	uint64_t dma0dbo                      : 1;
	uint64_t dma1dbo                      : 1;
	uint64_t dma2dbo                      : 1;
	uint64_t dma3dbo                      : 1;
	uint64_t reserved_8_8                 : 1;
	uint64_t dma0fi                       : 1;
	uint64_t dma1fi                       : 1;
	uint64_t dcnt0                        : 1;
	uint64_t dcnt1                        : 1;
	uint64_t dtime0                       : 1;
	uint64_t dtime1                       : 1;
	uint64_t reserved_15_18               : 4;
	uint64_t c0_aeri                      : 1;
	uint64_t crs0_er                      : 1;
	uint64_t c0_se                        : 1;
	uint64_t crs0_dr                      : 1;
	uint64_t c0_wake                      : 1;
	uint64_t c0_pmei                      : 1;
	uint64_t c0_hpint                     : 1;
	uint64_t c1_aeri                      : 1;
	uint64_t crs1_er                      : 1;
	uint64_t c1_se                        : 1;
	uint64_t crs1_dr                      : 1;
	uint64_t c1_wake                      : 1;
	uint64_t c1_pmei                      : 1;
	uint64_t c1_hpint                     : 1;
	uint64_t c0_up_b0                     : 1;
	uint64_t c0_up_b1                     : 1;
	uint64_t c0_up_b2                     : 1;
	uint64_t c0_up_wi                     : 1;
	uint64_t c0_up_bx                     : 1;
	uint64_t c0_un_b0                     : 1;
	uint64_t c0_un_b1                     : 1;
	uint64_t c0_un_b2                     : 1;
	uint64_t c0_un_wi                     : 1;
	uint64_t c0_un_bx                     : 1;
	uint64_t c1_up_b0                     : 1;
	uint64_t c1_up_b1                     : 1;
	uint64_t c1_up_b2                     : 1;
	uint64_t c1_up_wi                     : 1;
	uint64_t c1_up_bx                     : 1;
	uint64_t c1_un_b0                     : 1;
	uint64_t c1_un_b1                     : 1;
	uint64_t c1_un_b2                     : 1;
	uint64_t c1_un_wi                     : 1;
	uint64_t c1_un_bx                     : 1;
	uint64_t c0_un_wf                     : 1;
	uint64_t c1_un_wf                     : 1;
	uint64_t c0_up_wf                     : 1;
	uint64_t c1_up_wf                     : 1;
	uint64_t c0_exc                       : 1;
	uint64_t c1_exc                       : 1;
	uint64_t c0_ldwn                      : 1;
	uint64_t c1_ldwn                      : 1;
	uint64_t int_a                        : 1;
	uint64_t reserved_62_62               : 1;
	uint64_t mio_inta                     : 1;
#endif
	} cn52xxp1;
	struct cvmx_npei_int_sum_s            cn56xx;
	struct cvmx_npei_int_sum_cn56xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t mio_inta                     : 1;  /**< Interrupt from MIO. */
	uint64_t reserved_61_62               : 2;
	uint64_t c1_ldwn                      : 1;  /**< Reset request due to link1 down status. */
	uint64_t c0_ldwn                      : 1;  /**< Reset request due to link0 down status. */
	uint64_t c1_exc                       : 1;  /**< Set when the PESC1_DBG_INFO register has a bit
                                                         set and its cooresponding PESC1_DBG_INFO_EN bit
                                                         is set. */
	uint64_t c0_exc                       : 1;  /**< Set when the PESC0_DBG_INFO register has a bit
                                                         set and its cooresponding PESC0_DBG_INFO_EN bit
                                                         is set. */
	uint64_t c1_up_wf                     : 1;  /**< Received Unsupported P-TLP for filtered window
                                                         register. Core1. */
	uint64_t c0_up_wf                     : 1;  /**< Received Unsupported P-TLP for filtered window
                                                         register. Core0. */
	uint64_t c1_un_wf                     : 1;  /**< Received Unsupported N-TLP for filtered window
                                                         register. Core1. */
	uint64_t c0_un_wf                     : 1;  /**< Received Unsupported N-TLP for filtered window
                                                         register. Core0. */
	uint64_t c1_un_bx                     : 1;  /**< Received Unsupported N-TLP for unknown Bar.
                                                         Core 1. */
	uint64_t c1_un_wi                     : 1;  /**< Received Unsupported N-TLP for Window Register.
                                                         Core 1. */
	uint64_t c1_un_b2                     : 1;  /**< Received Unsupported N-TLP for Bar2.
                                                         Core 1. */
	uint64_t c1_un_b1                     : 1;  /**< Received Unsupported N-TLP for Bar1.
                                                         Core 1. */
	uint64_t c1_un_b0                     : 1;  /**< Received Unsupported N-TLP for Bar0.
                                                         Core 1. */
	uint64_t c1_up_bx                     : 1;  /**< Received Unsupported P-TLP for unknown Bar.
                                                         Core 1. */
	uint64_t c1_up_wi                     : 1;  /**< Received Unsupported P-TLP for Window Register.
                                                         Core 1. */
	uint64_t c1_up_b2                     : 1;  /**< Received Unsupported P-TLP for Bar2.
                                                         Core 1. */
	uint64_t c1_up_b1                     : 1;  /**< Received Unsupported P-TLP for Bar1.
                                                         Core 1. */
	uint64_t c1_up_b0                     : 1;  /**< Received Unsupported P-TLP for Bar0.
                                                         Core 1. */
	uint64_t c0_un_bx                     : 1;  /**< Received Unsupported N-TLP for unknown Bar.
                                                         Core 0. */
	uint64_t c0_un_wi                     : 1;  /**< Received Unsupported N-TLP for Window Register.
                                                         Core 0. */
	uint64_t c0_un_b2                     : 1;  /**< Received Unsupported N-TLP for Bar2.
                                                         Core 0. */
	uint64_t c0_un_b1                     : 1;  /**< Received Unsupported N-TLP for Bar1.
                                                         Core 0. */
	uint64_t c0_un_b0                     : 1;  /**< Received Unsupported N-TLP for Bar0.
                                                         Core 0. */
	uint64_t c0_up_bx                     : 1;  /**< Received Unsupported P-TLP for unknown Bar.
                                                         Core 0. */
	uint64_t c0_up_wi                     : 1;  /**< Received Unsupported P-TLP for Window Register.
                                                         Core 0. */
	uint64_t c0_up_b2                     : 1;  /**< Received Unsupported P-TLP for Bar2.
                                                         Core 0. */
	uint64_t c0_up_b1                     : 1;  /**< Received Unsupported P-TLP for Bar1.
                                                         Core 0. */
	uint64_t c0_up_b0                     : 1;  /**< Received Unsupported P-TLP for Bar0.
                                                         Core 0. */
	uint64_t c1_hpint                     : 1;  /**< Hot-Plug Interrupt.
                                                         Pcie Core 1 (hp_int).
                                                         This interrupt will only be generated when
                                                         PCIERC1_CFG034[DLLS_C] is generated. Hot plug is
                                                         not supported. */
	uint64_t c1_pmei                      : 1;  /**< PME Interrupt.
                                                         Pcie Core 1. (cfg_pme_int) */
	uint64_t c1_wake                      : 1;  /**< Wake up from Power Management Unit.
                                                         Pcie Core 1. (wake_n)
                                                         Octeon will never generate this interrupt. */
	uint64_t reserved_29_29               : 1;
	uint64_t c1_se                        : 1;  /**< System Error, RC Mode Only.
                                                         Pcie Core 1. (cfg_sys_err_rc) */
	uint64_t reserved_27_27               : 1;
	uint64_t c1_aeri                      : 1;  /**< Advanced Error Reporting Interrupt, RC Mode Only.
                                                         Pcie Core 1. */
	uint64_t c0_hpint                     : 1;  /**< Hot-Plug Interrupt.
                                                         Pcie Core 0 (hp_int).
                                                         This interrupt will only be generated when
                                                         PCIERC0_CFG034[DLLS_C] is generated. Hot plug is
                                                         not supported. */
	uint64_t c0_pmei                      : 1;  /**< PME Interrupt.
                                                         Pcie Core 0. (cfg_pme_int) */
	uint64_t c0_wake                      : 1;  /**< Wake up from Power Management Unit.
                                                         Pcie Core 0. (wake_n)
                                                         Octeon will never generate this interrupt. */
	uint64_t reserved_22_22               : 1;
	uint64_t c0_se                        : 1;  /**< System Error, RC Mode Only.
                                                         Pcie Core 0. (cfg_sys_err_rc) */
	uint64_t reserved_20_20               : 1;
	uint64_t c0_aeri                      : 1;  /**< Advanced Error Reporting Interrupt, RC Mode Only.
                                                         Pcie Core 0 (cfg_aer_rc_err_int). */
	uint64_t reserved_15_18               : 4;
	uint64_t dtime1                       : 1;  /**< Whenever NPEI_DMA_CNTS[DMA1] is not 0, the
                                                         DMA_CNT1 timer increments every core clock. When
                                                         DMA_CNT1 timer exceeds NPEI_DMA1_INT_LEVEL[TIME],
                                                         this bit is set. Writing a '1' to this bit also
                                                         clears the DMA_CNT1 timer. */
	uint64_t dtime0                       : 1;  /**< Whenever NPEI_DMA_CNTS[DMA0] is not 0, the
                                                         DMA_CNT0 timer increments every core clock. When
                                                         DMA_CNT0 timer exceeds NPEI_DMA0_INT_LEVEL[TIME],
                                                         this bit is set. Writing a '1' to this bit also
                                                         clears the DMA_CNT0 timer. */
	uint64_t dcnt1                        : 1;  /**< This bit indicates that NPEI_DMA_CNTS[DMA1] was/is
                                                         greater than NPEI_DMA1_INT_LEVEL[CNT]. */
	uint64_t dcnt0                        : 1;  /**< This bit indicates that NPEI_DMA_CNTS[DMA0] was/is
                                                         greater than NPEI_DMA0_INT_LEVEL[CNT]. */
	uint64_t dma1fi                       : 1;  /**< DMA0 set Forced Interrupt. */
	uint64_t dma0fi                       : 1;  /**< DMA0 set Forced Interrupt. */
	uint64_t dma4dbo                      : 1;  /**< DMA4 doorbell overflow.
                                                         Bit[32] of the doorbell count was set. */
	uint64_t dma3dbo                      : 1;  /**< DMA3 doorbell overflow.
                                                         Bit[32] of the doorbell count was set. */
	uint64_t dma2dbo                      : 1;  /**< DMA2 doorbell overflow.
                                                         Bit[32] of the doorbell count was set. */
	uint64_t dma1dbo                      : 1;  /**< DMA1 doorbell overflow.
                                                         Bit[32] of the doorbell count was set. */
	uint64_t dma0dbo                      : 1;  /**< DMA0 doorbell overflow.
                                                         Bit[32] of the doorbell count was set. */
	uint64_t iob2big                      : 1;  /**< A requested IOBDMA is to large. */
	uint64_t bar0_to                      : 1;  /**< BAR0 R/W to a NCB device did not receive
                                                         read-data/commit in 0xffff core clocks. */
	uint64_t rml_wto                      : 1;  /**< RML write did not get commit in 0xffff core clocks. */
	uint64_t rml_rto                      : 1;  /**< RML read did not return data in 0xffff core clocks. */
#else
	uint64_t rml_rto                      : 1;
	uint64_t rml_wto                      : 1;
	uint64_t bar0_to                      : 1;
	uint64_t iob2big                      : 1;
	uint64_t dma0dbo                      : 1;
	uint64_t dma1dbo                      : 1;
	uint64_t dma2dbo                      : 1;
	uint64_t dma3dbo                      : 1;
	uint64_t dma4dbo                      : 1;
	uint64_t dma0fi                       : 1;
	uint64_t dma1fi                       : 1;
	uint64_t dcnt0                        : 1;
	uint64_t dcnt1                        : 1;
	uint64_t dtime0                       : 1;
	uint64_t dtime1                       : 1;
	uint64_t reserved_15_18               : 4;
	uint64_t c0_aeri                      : 1;
	uint64_t reserved_20_20               : 1;
	uint64_t c0_se                        : 1;
	uint64_t reserved_22_22               : 1;
	uint64_t c0_wake                      : 1;
	uint64_t c0_pmei                      : 1;
	uint64_t c0_hpint                     : 1;
	uint64_t c1_aeri                      : 1;
	uint64_t reserved_27_27               : 1;
	uint64_t c1_se                        : 1;
	uint64_t reserved_29_29               : 1;
	uint64_t c1_wake                      : 1;
	uint64_t c1_pmei                      : 1;
	uint64_t c1_hpint                     : 1;
	uint64_t c0_up_b0                     : 1;
	uint64_t c0_up_b1                     : 1;
	uint64_t c0_up_b2                     : 1;
	uint64_t c0_up_wi                     : 1;
	uint64_t c0_up_bx                     : 1;
	uint64_t c0_un_b0                     : 1;
	uint64_t c0_un_b1                     : 1;
	uint64_t c0_un_b2                     : 1;
	uint64_t c0_un_wi                     : 1;
	uint64_t c0_un_bx                     : 1;
	uint64_t c1_up_b0                     : 1;
	uint64_t c1_up_b1                     : 1;
	uint64_t c1_up_b2                     : 1;
	uint64_t c1_up_wi                     : 1;
	uint64_t c1_up_bx                     : 1;
	uint64_t c1_un_b0                     : 1;
	uint64_t c1_un_b1                     : 1;
	uint64_t c1_un_b2                     : 1;
	uint64_t c1_un_wi                     : 1;
	uint64_t c1_un_bx                     : 1;
	uint64_t c0_un_wf                     : 1;
	uint64_t c1_un_wf                     : 1;
	uint64_t c0_up_wf                     : 1;
	uint64_t c1_up_wf                     : 1;
	uint64_t c0_exc                       : 1;
	uint64_t c1_exc                       : 1;
	uint64_t c0_ldwn                      : 1;
	uint64_t c1_ldwn                      : 1;
	uint64_t reserved_61_62               : 2;
	uint64_t mio_inta                     : 1;
#endif
	} cn56xxp1;
};
typedef union cvmx_npei_int_sum cvmx_npei_int_sum_t;

/**
 * cvmx_npei_int_sum2
 *
 * NPEI_INTERRUPT_SUM2 = NPI Interrupt Summary2 Register
 *
 * This is a read only copy of the NPEI_INTERRUPT_SUM register with bit variances.
 */
union cvmx_npei_int_sum2 {
	uint64_t u64;
	struct cvmx_npei_int_sum2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t mio_inta                     : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t reserved_62_62               : 1;
	uint64_t int_a                        : 1;  /**< Set when a bit in the NPEI_INT_A_SUM register and
                                                         the cooresponding bit in the NPEI_INT_A_ENB2
                                                         register is set. */
	uint64_t c1_ldwn                      : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t c0_ldwn                      : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t c1_exc                       : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t c0_exc                       : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t c1_up_wf                     : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t c0_up_wf                     : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t c1_un_wf                     : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t c0_un_wf                     : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t c1_un_bx                     : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t c1_un_wi                     : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t c1_un_b2                     : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t c1_un_b1                     : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t c1_un_b0                     : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t c1_up_bx                     : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t c1_up_wi                     : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t c1_up_b2                     : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t c1_up_b1                     : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t c1_up_b0                     : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t c0_un_bx                     : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t c0_un_wi                     : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t c0_un_b2                     : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t c0_un_b1                     : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t c0_un_b0                     : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t c0_up_bx                     : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t c0_up_wi                     : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t c0_up_b2                     : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t c0_up_b1                     : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t c0_up_b0                     : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t c1_hpint                     : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t c1_pmei                      : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t c1_wake                      : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t crs1_dr                      : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t c1_se                        : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t crs1_er                      : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t c1_aeri                      : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t c0_hpint                     : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t c0_pmei                      : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t c0_wake                      : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t crs0_dr                      : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t c0_se                        : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t crs0_er                      : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t c0_aeri                      : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t reserved_15_18               : 4;
	uint64_t dtime1                       : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t dtime0                       : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t dcnt1                        : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t dcnt0                        : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t dma1fi                       : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t dma0fi                       : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t reserved_8_8                 : 1;
	uint64_t dma3dbo                      : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t dma2dbo                      : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t dma1dbo                      : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t dma0dbo                      : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t iob2big                      : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t bar0_to                      : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t rml_wto                      : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
	uint64_t rml_rto                      : 1;  /**< Equal to the cooresponding bit if the
                                                         NPEI_INT_SUM register. */
#else
	uint64_t rml_rto                      : 1;
	uint64_t rml_wto                      : 1;
	uint64_t bar0_to                      : 1;
	uint64_t iob2big                      : 1;
	uint64_t dma0dbo                      : 1;
	uint64_t dma1dbo                      : 1;
	uint64_t dma2dbo                      : 1;
	uint64_t dma3dbo                      : 1;
	uint64_t reserved_8_8                 : 1;
	uint64_t dma0fi                       : 1;
	uint64_t dma1fi                       : 1;
	uint64_t dcnt0                        : 1;
	uint64_t dcnt1                        : 1;
	uint64_t dtime0                       : 1;
	uint64_t dtime1                       : 1;
	uint64_t reserved_15_18               : 4;
	uint64_t c0_aeri                      : 1;
	uint64_t crs0_er                      : 1;
	uint64_t c0_se                        : 1;
	uint64_t crs0_dr                      : 1;
	uint64_t c0_wake                      : 1;
	uint64_t c0_pmei                      : 1;
	uint64_t c0_hpint                     : 1;
	uint64_t c1_aeri                      : 1;
	uint64_t crs1_er                      : 1;
	uint64_t c1_se                        : 1;
	uint64_t crs1_dr                      : 1;
	uint64_t c1_wake                      : 1;
	uint64_t c1_pmei                      : 1;
	uint64_t c1_hpint                     : 1;
	uint64_t c0_up_b0                     : 1;
	uint64_t c0_up_b1                     : 1;
	uint64_t c0_up_b2                     : 1;
	uint64_t c0_up_wi                     : 1;
	uint64_t c0_up_bx                     : 1;
	uint64_t c0_un_b0                     : 1;
	uint64_t c0_un_b1                     : 1;
	uint64_t c0_un_b2                     : 1;
	uint64_t c0_un_wi                     : 1;
	uint64_t c0_un_bx                     : 1;
	uint64_t c1_up_b0                     : 1;
	uint64_t c1_up_b1                     : 1;
	uint64_t c1_up_b2                     : 1;
	uint64_t c1_up_wi                     : 1;
	uint64_t c1_up_bx                     : 1;
	uint64_t c1_un_b0                     : 1;
	uint64_t c1_un_b1                     : 1;
	uint64_t c1_un_b2                     : 1;
	uint64_t c1_un_wi                     : 1;
	uint64_t c1_un_bx                     : 1;
	uint64_t c0_un_wf                     : 1;
	uint64_t c1_un_wf                     : 1;
	uint64_t c0_up_wf                     : 1;
	uint64_t c1_up_wf                     : 1;
	uint64_t c0_exc                       : 1;
	uint64_t c1_exc                       : 1;
	uint64_t c0_ldwn                      : 1;
	uint64_t c1_ldwn                      : 1;
	uint64_t int_a                        : 1;
	uint64_t reserved_62_62               : 1;
	uint64_t mio_inta                     : 1;
#endif
	} s;
	struct cvmx_npei_int_sum2_s           cn52xx;
	struct cvmx_npei_int_sum2_s           cn52xxp1;
	struct cvmx_npei_int_sum2_s           cn56xx;
};
typedef union cvmx_npei_int_sum2 cvmx_npei_int_sum2_t;

/**
 * cvmx_npei_last_win_rdata0
 *
 * NPEI_LAST_WIN_RDATA0 = NPEI Last Window Read Data Port0
 *
 * The data from the last initiated window read.
 */
union cvmx_npei_last_win_rdata0 {
	uint64_t u64;
	struct cvmx_npei_last_win_rdata0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t data                         : 64; /**< Last window read data. */
#else
	uint64_t data                         : 64;
#endif
	} s;
	struct cvmx_npei_last_win_rdata0_s    cn52xx;
	struct cvmx_npei_last_win_rdata0_s    cn52xxp1;
	struct cvmx_npei_last_win_rdata0_s    cn56xx;
	struct cvmx_npei_last_win_rdata0_s    cn56xxp1;
};
typedef union cvmx_npei_last_win_rdata0 cvmx_npei_last_win_rdata0_t;

/**
 * cvmx_npei_last_win_rdata1
 *
 * NPEI_LAST_WIN_RDATA1 = NPEI Last Window Read Data Port1
 *
 * The data from the last initiated window read.
 */
union cvmx_npei_last_win_rdata1 {
	uint64_t u64;
	struct cvmx_npei_last_win_rdata1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t data                         : 64; /**< Last window read data. */
#else
	uint64_t data                         : 64;
#endif
	} s;
	struct cvmx_npei_last_win_rdata1_s    cn52xx;
	struct cvmx_npei_last_win_rdata1_s    cn52xxp1;
	struct cvmx_npei_last_win_rdata1_s    cn56xx;
	struct cvmx_npei_last_win_rdata1_s    cn56xxp1;
};
typedef union cvmx_npei_last_win_rdata1 cvmx_npei_last_win_rdata1_t;

/**
 * cvmx_npei_mem_access_ctl
 *
 * NPEI_MEM_ACCESS_CTL = NPEI's Memory Access Control
 *
 * Contains control for access to the PCIe address space.
 */
union cvmx_npei_mem_access_ctl {
	uint64_t u64;
	struct cvmx_npei_mem_access_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_14_63               : 50;
	uint64_t max_word                     : 4;  /**< The maximum number of words to merge into a single
                                                         write operation from the PPs to the PCIe. Legal
                                                         values are 1 to 16, where a '0' is treated as 16. */
	uint64_t timer                        : 10; /**< When the NPEI starts a PP to PCIe write it waits
                                                         no longer than the value of TIMER in eclks to
                                                         merge additional writes from the PPs into 1
                                                         large write. The values for this field is 1 to
                                                         1024 where a value of '0' is treated as 1024. */
#else
	uint64_t timer                        : 10;
	uint64_t max_word                     : 4;
	uint64_t reserved_14_63               : 50;
#endif
	} s;
	struct cvmx_npei_mem_access_ctl_s     cn52xx;
	struct cvmx_npei_mem_access_ctl_s     cn52xxp1;
	struct cvmx_npei_mem_access_ctl_s     cn56xx;
	struct cvmx_npei_mem_access_ctl_s     cn56xxp1;
};
typedef union cvmx_npei_mem_access_ctl cvmx_npei_mem_access_ctl_t;

/**
 * cvmx_npei_mem_access_subid#
 *
 * NPEI_MEM_ACCESS_SUBIDX = NPEI Memory Access SubidX Register
 *
 * Contains address index and control bits for access to memory from Core PPs.
 */
union cvmx_npei_mem_access_subidx {
	uint64_t u64;
	struct cvmx_npei_mem_access_subidx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_42_63               : 22;
	uint64_t zero                         : 1;  /**< Causes all byte reads to be zero length reads.
                                                         Returns to the EXEC a zero for all read data. */
	uint64_t port                         : 2;  /**< Port the request is sent to. */
	uint64_t nmerge                       : 1;  /**< No merging is allowed in this window. */
	uint64_t esr                          : 2;  /**< Endian-swap for Reads. */
	uint64_t esw                          : 2;  /**< Endian-swap for Writes. */
	uint64_t nsr                          : 1;  /**< No Snoop for Reads. */
	uint64_t nsw                          : 1;  /**< No Snoop for Writes. */
	uint64_t ror                          : 1;  /**< Relaxed Ordering for Reads. */
	uint64_t row                          : 1;  /**< Relaxed Ordering for Writes. */
	uint64_t ba                           : 30; /**< PCIe Adddress Bits <63:34>. */
#else
	uint64_t ba                           : 30;
	uint64_t row                          : 1;
	uint64_t ror                          : 1;
	uint64_t nsw                          : 1;
	uint64_t nsr                          : 1;
	uint64_t esw                          : 2;
	uint64_t esr                          : 2;
	uint64_t nmerge                       : 1;
	uint64_t port                         : 2;
	uint64_t zero                         : 1;
	uint64_t reserved_42_63               : 22;
#endif
	} s;
	struct cvmx_npei_mem_access_subidx_s  cn52xx;
	struct cvmx_npei_mem_access_subidx_s  cn52xxp1;
	struct cvmx_npei_mem_access_subidx_s  cn56xx;
	struct cvmx_npei_mem_access_subidx_s  cn56xxp1;
};
typedef union cvmx_npei_mem_access_subidx cvmx_npei_mem_access_subidx_t;

/**
 * cvmx_npei_msi_enb0
 *
 * NPEI_MSI_ENB0 = NPEI MSI Enable0
 *
 * Used to enable the interrupt generation for the bits in the NPEI_MSI_RCV0.
 */
union cvmx_npei_msi_enb0 {
	uint64_t u64;
	struct cvmx_npei_msi_enb0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t enb                          : 64; /**< Enables bit [63:0] of NPEI_MSI_RCV0. */
#else
	uint64_t enb                          : 64;
#endif
	} s;
	struct cvmx_npei_msi_enb0_s           cn52xx;
	struct cvmx_npei_msi_enb0_s           cn52xxp1;
	struct cvmx_npei_msi_enb0_s           cn56xx;
	struct cvmx_npei_msi_enb0_s           cn56xxp1;
};
typedef union cvmx_npei_msi_enb0 cvmx_npei_msi_enb0_t;

/**
 * cvmx_npei_msi_enb1
 *
 * NPEI_MSI_ENB1 = NPEI MSI Enable1
 *
 * Used to enable the interrupt generation for the bits in the NPEI_MSI_RCV1.
 */
union cvmx_npei_msi_enb1 {
	uint64_t u64;
	struct cvmx_npei_msi_enb1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t enb                          : 64; /**< Enables bit [63:0] of NPEI_MSI_RCV1. */
#else
	uint64_t enb                          : 64;
#endif
	} s;
	struct cvmx_npei_msi_enb1_s           cn52xx;
	struct cvmx_npei_msi_enb1_s           cn52xxp1;
	struct cvmx_npei_msi_enb1_s           cn56xx;
	struct cvmx_npei_msi_enb1_s           cn56xxp1;
};
typedef union cvmx_npei_msi_enb1 cvmx_npei_msi_enb1_t;

/**
 * cvmx_npei_msi_enb2
 *
 * NPEI_MSI_ENB2 = NPEI MSI Enable2
 *
 * Used to enable the interrupt generation for the bits in the NPEI_MSI_RCV2.
 */
union cvmx_npei_msi_enb2 {
	uint64_t u64;
	struct cvmx_npei_msi_enb2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t enb                          : 64; /**< Enables bit [63:0] of NPEI_MSI_RCV2. */
#else
	uint64_t enb                          : 64;
#endif
	} s;
	struct cvmx_npei_msi_enb2_s           cn52xx;
	struct cvmx_npei_msi_enb2_s           cn52xxp1;
	struct cvmx_npei_msi_enb2_s           cn56xx;
	struct cvmx_npei_msi_enb2_s           cn56xxp1;
};
typedef union cvmx_npei_msi_enb2 cvmx_npei_msi_enb2_t;

/**
 * cvmx_npei_msi_enb3
 *
 * NPEI_MSI_ENB3 = NPEI MSI Enable3
 *
 * Used to enable the interrupt generation for the bits in the NPEI_MSI_RCV3.
 */
union cvmx_npei_msi_enb3 {
	uint64_t u64;
	struct cvmx_npei_msi_enb3_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t enb                          : 64; /**< Enables bit [63:0] of NPEI_MSI_RCV3. */
#else
	uint64_t enb                          : 64;
#endif
	} s;
	struct cvmx_npei_msi_enb3_s           cn52xx;
	struct cvmx_npei_msi_enb3_s           cn52xxp1;
	struct cvmx_npei_msi_enb3_s           cn56xx;
	struct cvmx_npei_msi_enb3_s           cn56xxp1;
};
typedef union cvmx_npei_msi_enb3 cvmx_npei_msi_enb3_t;

/**
 * cvmx_npei_msi_rcv0
 *
 * NPEI_MSI_RCV0 = NPEI MSI Receive0
 *
 * Contains bits [63:0] of the 256 bits oof MSI interrupts.
 */
union cvmx_npei_msi_rcv0 {
	uint64_t u64;
	struct cvmx_npei_msi_rcv0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t intr                         : 64; /**< Bits 63-0 of the 256 bits of MSI interrupt. */
#else
	uint64_t intr                         : 64;
#endif
	} s;
	struct cvmx_npei_msi_rcv0_s           cn52xx;
	struct cvmx_npei_msi_rcv0_s           cn52xxp1;
	struct cvmx_npei_msi_rcv0_s           cn56xx;
	struct cvmx_npei_msi_rcv0_s           cn56xxp1;
};
typedef union cvmx_npei_msi_rcv0 cvmx_npei_msi_rcv0_t;

/**
 * cvmx_npei_msi_rcv1
 *
 * NPEI_MSI_RCV1 = NPEI MSI Receive1
 *
 * Contains bits [127:64] of the 256 bits oof MSI interrupts.
 */
union cvmx_npei_msi_rcv1 {
	uint64_t u64;
	struct cvmx_npei_msi_rcv1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t intr                         : 64; /**< Bits 127-64 of the 256 bits of MSI interrupt. */
#else
	uint64_t intr                         : 64;
#endif
	} s;
	struct cvmx_npei_msi_rcv1_s           cn52xx;
	struct cvmx_npei_msi_rcv1_s           cn52xxp1;
	struct cvmx_npei_msi_rcv1_s           cn56xx;
	struct cvmx_npei_msi_rcv1_s           cn56xxp1;
};
typedef union cvmx_npei_msi_rcv1 cvmx_npei_msi_rcv1_t;

/**
 * cvmx_npei_msi_rcv2
 *
 * NPEI_MSI_RCV2 = NPEI MSI Receive2
 *
 * Contains bits [191:128] of the 256 bits oof MSI interrupts.
 */
union cvmx_npei_msi_rcv2 {
	uint64_t u64;
	struct cvmx_npei_msi_rcv2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t intr                         : 64; /**< Bits 191-128 of the 256 bits of MSI interrupt. */
#else
	uint64_t intr                         : 64;
#endif
	} s;
	struct cvmx_npei_msi_rcv2_s           cn52xx;
	struct cvmx_npei_msi_rcv2_s           cn52xxp1;
	struct cvmx_npei_msi_rcv2_s           cn56xx;
	struct cvmx_npei_msi_rcv2_s           cn56xxp1;
};
typedef union cvmx_npei_msi_rcv2 cvmx_npei_msi_rcv2_t;

/**
 * cvmx_npei_msi_rcv3
 *
 * NPEI_MSI_RCV3 = NPEI MSI Receive3
 *
 * Contains bits [255:192] of the 256 bits oof MSI interrupts.
 */
union cvmx_npei_msi_rcv3 {
	uint64_t u64;
	struct cvmx_npei_msi_rcv3_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t intr                         : 64; /**< Bits 255-192 of the 256 bits of MSI interrupt. */
#else
	uint64_t intr                         : 64;
#endif
	} s;
	struct cvmx_npei_msi_rcv3_s           cn52xx;
	struct cvmx_npei_msi_rcv3_s           cn52xxp1;
	struct cvmx_npei_msi_rcv3_s           cn56xx;
	struct cvmx_npei_msi_rcv3_s           cn56xxp1;
};
typedef union cvmx_npei_msi_rcv3 cvmx_npei_msi_rcv3_t;

/**
 * cvmx_npei_msi_rd_map
 *
 * NPEI_MSI_RD_MAP = NPEI MSI Read MAP
 *
 * Used to read the mapping function of the NPEI_PCIE_MSI_RCV to NPEI_MSI_RCV registers.
 */
union cvmx_npei_msi_rd_map {
	uint64_t u64;
	struct cvmx_npei_msi_rd_map_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t rd_int                       : 8;  /**< The value of the map at the location PREVIOUSLY
                                                         written to the MSI_INT field of this register. */
	uint64_t msi_int                      : 8;  /**< Selects the value that would be received when the
                                                         NPEI_PCIE_MSI_RCV register is written. */
#else
	uint64_t msi_int                      : 8;
	uint64_t rd_int                       : 8;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_npei_msi_rd_map_s         cn52xx;
	struct cvmx_npei_msi_rd_map_s         cn52xxp1;
	struct cvmx_npei_msi_rd_map_s         cn56xx;
	struct cvmx_npei_msi_rd_map_s         cn56xxp1;
};
typedef union cvmx_npei_msi_rd_map cvmx_npei_msi_rd_map_t;

/**
 * cvmx_npei_msi_w1c_enb0
 *
 * NPEI_MSI_W1C_ENB0 = NPEI MSI Write 1 To Clear Enable0
 *
 * Used to clear bits in NPEI_MSI_ENB0. This is a PASS2 register.
 */
union cvmx_npei_msi_w1c_enb0 {
	uint64_t u64;
	struct cvmx_npei_msi_w1c_enb0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t clr                          : 64; /**< A write of '1' to a vector will clear the
                                                         cooresponding bit in NPEI_MSI_ENB0.
                                                         A read to this address will return 0. */
#else
	uint64_t clr                          : 64;
#endif
	} s;
	struct cvmx_npei_msi_w1c_enb0_s       cn52xx;
	struct cvmx_npei_msi_w1c_enb0_s       cn56xx;
};
typedef union cvmx_npei_msi_w1c_enb0 cvmx_npei_msi_w1c_enb0_t;

/**
 * cvmx_npei_msi_w1c_enb1
 *
 * NPEI_MSI_W1C_ENB1 = NPEI MSI Write 1 To Clear Enable1
 *
 * Used to clear bits in NPEI_MSI_ENB1. This is a PASS2 register.
 */
union cvmx_npei_msi_w1c_enb1 {
	uint64_t u64;
	struct cvmx_npei_msi_w1c_enb1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t clr                          : 64; /**< A write of '1' to a vector will clear the
                                                         cooresponding bit in NPEI_MSI_ENB1.
                                                         A read to this address will return 0. */
#else
	uint64_t clr                          : 64;
#endif
	} s;
	struct cvmx_npei_msi_w1c_enb1_s       cn52xx;
	struct cvmx_npei_msi_w1c_enb1_s       cn56xx;
};
typedef union cvmx_npei_msi_w1c_enb1 cvmx_npei_msi_w1c_enb1_t;

/**
 * cvmx_npei_msi_w1c_enb2
 *
 * NPEI_MSI_W1C_ENB2 = NPEI MSI Write 1 To Clear Enable2
 *
 * Used to clear bits in NPEI_MSI_ENB2. This is a PASS2 register.
 */
union cvmx_npei_msi_w1c_enb2 {
	uint64_t u64;
	struct cvmx_npei_msi_w1c_enb2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t clr                          : 64; /**< A write of '1' to a vector will clear the
                                                         cooresponding bit in NPEI_MSI_ENB2.
                                                         A read to this address will return 0. */
#else
	uint64_t clr                          : 64;
#endif
	} s;
	struct cvmx_npei_msi_w1c_enb2_s       cn52xx;
	struct cvmx_npei_msi_w1c_enb2_s       cn56xx;
};
typedef union cvmx_npei_msi_w1c_enb2 cvmx_npei_msi_w1c_enb2_t;

/**
 * cvmx_npei_msi_w1c_enb3
 *
 * NPEI_MSI_W1C_ENB3 = NPEI MSI Write 1 To Clear Enable3
 *
 * Used to clear bits in NPEI_MSI_ENB3. This is a PASS2 register.
 */
union cvmx_npei_msi_w1c_enb3 {
	uint64_t u64;
	struct cvmx_npei_msi_w1c_enb3_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t clr                          : 64; /**< A write of '1' to a vector will clear the
                                                         cooresponding bit in NPEI_MSI_ENB3.
                                                         A read to this address will return 0. */
#else
	uint64_t clr                          : 64;
#endif
	} s;
	struct cvmx_npei_msi_w1c_enb3_s       cn52xx;
	struct cvmx_npei_msi_w1c_enb3_s       cn56xx;
};
typedef union cvmx_npei_msi_w1c_enb3 cvmx_npei_msi_w1c_enb3_t;

/**
 * cvmx_npei_msi_w1s_enb0
 *
 * NPEI_MSI_W1S_ENB0 = NPEI MSI Write 1 To Set Enable0
 *
 * Used to set bits in NPEI_MSI_ENB0. This is a PASS2 register.
 */
union cvmx_npei_msi_w1s_enb0 {
	uint64_t u64;
	struct cvmx_npei_msi_w1s_enb0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t set                          : 64; /**< A write of '1' to a vector will set the
                                                         cooresponding bit in NPEI_MSI_ENB0.
                                                         A read to this address will return 0. */
#else
	uint64_t set                          : 64;
#endif
	} s;
	struct cvmx_npei_msi_w1s_enb0_s       cn52xx;
	struct cvmx_npei_msi_w1s_enb0_s       cn56xx;
};
typedef union cvmx_npei_msi_w1s_enb0 cvmx_npei_msi_w1s_enb0_t;

/**
 * cvmx_npei_msi_w1s_enb1
 *
 * NPEI_MSI_W1S_ENB0 = NPEI MSI Write 1 To Set Enable1
 *
 * Used to set bits in NPEI_MSI_ENB1. This is a PASS2 register.
 */
union cvmx_npei_msi_w1s_enb1 {
	uint64_t u64;
	struct cvmx_npei_msi_w1s_enb1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t set                          : 64; /**< A write of '1' to a vector will set the
                                                         cooresponding bit in NPEI_MSI_ENB1.
                                                         A read to this address will return 0. */
#else
	uint64_t set                          : 64;
#endif
	} s;
	struct cvmx_npei_msi_w1s_enb1_s       cn52xx;
	struct cvmx_npei_msi_w1s_enb1_s       cn56xx;
};
typedef union cvmx_npei_msi_w1s_enb1 cvmx_npei_msi_w1s_enb1_t;

/**
 * cvmx_npei_msi_w1s_enb2
 *
 * NPEI_MSI_W1S_ENB2 = NPEI MSI Write 1 To Set Enable2
 *
 * Used to set bits in NPEI_MSI_ENB2. This is a PASS2 register.
 */
union cvmx_npei_msi_w1s_enb2 {
	uint64_t u64;
	struct cvmx_npei_msi_w1s_enb2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t set                          : 64; /**< A write of '1' to a vector will set the
                                                         cooresponding bit in NPEI_MSI_ENB2.
                                                         A read to this address will return 0. */
#else
	uint64_t set                          : 64;
#endif
	} s;
	struct cvmx_npei_msi_w1s_enb2_s       cn52xx;
	struct cvmx_npei_msi_w1s_enb2_s       cn56xx;
};
typedef union cvmx_npei_msi_w1s_enb2 cvmx_npei_msi_w1s_enb2_t;

/**
 * cvmx_npei_msi_w1s_enb3
 *
 * NPEI_MSI_W1S_ENB3 = NPEI MSI Write 1 To Set Enable3
 *
 * Used to set bits in NPEI_MSI_ENB3. This is a PASS2 register.
 */
union cvmx_npei_msi_w1s_enb3 {
	uint64_t u64;
	struct cvmx_npei_msi_w1s_enb3_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t set                          : 64; /**< A write of '1' to a vector will set the
                                                         cooresponding bit in NPEI_MSI_ENB3.
                                                         A read to this address will return 0. */
#else
	uint64_t set                          : 64;
#endif
	} s;
	struct cvmx_npei_msi_w1s_enb3_s       cn52xx;
	struct cvmx_npei_msi_w1s_enb3_s       cn56xx;
};
typedef union cvmx_npei_msi_w1s_enb3 cvmx_npei_msi_w1s_enb3_t;

/**
 * cvmx_npei_msi_wr_map
 *
 * NPEI_MSI_WR_MAP = NPEI MSI Write MAP
 *
 * Used to write the mapping function of the NPEI_PCIE_MSI_RCV to NPEI_MSI_RCV registers.
 */
union cvmx_npei_msi_wr_map {
	uint64_t u64;
	struct cvmx_npei_msi_wr_map_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t ciu_int                      : 8;  /**< Selects which bit in the NPEI_MSI_RCV# (0-255)
                                                         will be set when the value specified in the
                                                         MSI_INT of this register is recevied during a
                                                         write to the NPEI_PCIE_MSI_RCV register. */
	uint64_t msi_int                      : 8;  /**< Selects the value that would be received when the
                                                         NPEI_PCIE_MSI_RCV register is written. */
#else
	uint64_t msi_int                      : 8;
	uint64_t ciu_int                      : 8;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_npei_msi_wr_map_s         cn52xx;
	struct cvmx_npei_msi_wr_map_s         cn52xxp1;
	struct cvmx_npei_msi_wr_map_s         cn56xx;
	struct cvmx_npei_msi_wr_map_s         cn56xxp1;
};
typedef union cvmx_npei_msi_wr_map cvmx_npei_msi_wr_map_t;

/**
 * cvmx_npei_pcie_credit_cnt
 *
 * NPEI_PCIE_CREDIT_CNT = NPEI PCIE Credit Count
 *
 * Contains the number of credits for the pcie port FIFOs used by the NPEI. This value needs to be set BEFORE PCIe traffic
 * flow from NPEI to PCIE Ports starts. A write to this register will cause the credit counts in the NPEI for the two
 * PCIE ports to be reset to the value in this register.
 */
union cvmx_npei_pcie_credit_cnt {
	uint64_t u64;
	struct cvmx_npei_pcie_credit_cnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_48_63               : 16;
	uint64_t p1_ccnt                      : 8;  /**< Port1 C-TLP FIFO Credits.
                                                         Legal values are 0x25 to 0x80. */
	uint64_t p1_ncnt                      : 8;  /**< Port1 N-TLP FIFO Credits.
                                                         Legal values are 0x5 to 0x10. */
	uint64_t p1_pcnt                      : 8;  /**< Port1 P-TLP FIFO Credits.
                                                         Legal values are 0x25 to 0x80. */
	uint64_t p0_ccnt                      : 8;  /**< Port0 C-TLP FIFO Credits.
                                                         Legal values are 0x25 to 0x80. */
	uint64_t p0_ncnt                      : 8;  /**< Port0 N-TLP FIFO Credits.
                                                         Legal values are 0x5 to 0x10. */
	uint64_t p0_pcnt                      : 8;  /**< Port0 P-TLP FIFO Credits.
                                                         Legal values are 0x25 to 0x80. */
#else
	uint64_t p0_pcnt                      : 8;
	uint64_t p0_ncnt                      : 8;
	uint64_t p0_ccnt                      : 8;
	uint64_t p1_pcnt                      : 8;
	uint64_t p1_ncnt                      : 8;
	uint64_t p1_ccnt                      : 8;
	uint64_t reserved_48_63               : 16;
#endif
	} s;
	struct cvmx_npei_pcie_credit_cnt_s    cn52xx;
	struct cvmx_npei_pcie_credit_cnt_s    cn56xx;
};
typedef union cvmx_npei_pcie_credit_cnt cvmx_npei_pcie_credit_cnt_t;

/**
 * cvmx_npei_pcie_msi_rcv
 *
 * NPEI_PCIE_MSI_RCV = NPEI PCIe MSI Receive
 *
 * Register where MSI writes are directed from the PCIe.
 */
union cvmx_npei_pcie_msi_rcv {
	uint64_t u64;
	struct cvmx_npei_pcie_msi_rcv_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_8_63                : 56;
	uint64_t intr                         : 8;  /**< A write to this register will result in a bit in
                                                         one of the NPEI_MSI_RCV# registers being set.
                                                         Which bit is set is dependent on the previously
                                                         written using the NPEI_MSI_WR_MAP register or if
                                                         not previously written the reset value of the MAP. */
#else
	uint64_t intr                         : 8;
	uint64_t reserved_8_63                : 56;
#endif
	} s;
	struct cvmx_npei_pcie_msi_rcv_s       cn52xx;
	struct cvmx_npei_pcie_msi_rcv_s       cn52xxp1;
	struct cvmx_npei_pcie_msi_rcv_s       cn56xx;
	struct cvmx_npei_pcie_msi_rcv_s       cn56xxp1;
};
typedef union cvmx_npei_pcie_msi_rcv cvmx_npei_pcie_msi_rcv_t;

/**
 * cvmx_npei_pcie_msi_rcv_b1
 *
 * NPEI_PCIE_MSI_RCV_B1 = NPEI PCIe MSI Receive Byte 1
 *
 * Register where MSI writes are directed from the PCIe.
 */
union cvmx_npei_pcie_msi_rcv_b1 {
	uint64_t u64;
	struct cvmx_npei_pcie_msi_rcv_b1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t intr                         : 8;  /**< A write to this register will result in a bit in
                                                         one of the NPEI_MSI_RCV# registers being set.
                                                         Which bit is set is dependent on the previously
                                                         written using the NPEI_MSI_WR_MAP register or if
                                                         not previously written the reset value of the MAP. */
	uint64_t reserved_0_7                 : 8;
#else
	uint64_t reserved_0_7                 : 8;
	uint64_t intr                         : 8;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_npei_pcie_msi_rcv_b1_s    cn52xx;
	struct cvmx_npei_pcie_msi_rcv_b1_s    cn52xxp1;
	struct cvmx_npei_pcie_msi_rcv_b1_s    cn56xx;
	struct cvmx_npei_pcie_msi_rcv_b1_s    cn56xxp1;
};
typedef union cvmx_npei_pcie_msi_rcv_b1 cvmx_npei_pcie_msi_rcv_b1_t;

/**
 * cvmx_npei_pcie_msi_rcv_b2
 *
 * NPEI_PCIE_MSI_RCV_B2 = NPEI PCIe MSI Receive Byte 2
 *
 * Register where MSI writes are directed from the PCIe.
 */
union cvmx_npei_pcie_msi_rcv_b2 {
	uint64_t u64;
	struct cvmx_npei_pcie_msi_rcv_b2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_24_63               : 40;
	uint64_t intr                         : 8;  /**< A write to this register will result in a bit in
                                                         one of the NPEI_MSI_RCV# registers being set.
                                                         Which bit is set is dependent on the previously
                                                         written using the NPEI_MSI_WR_MAP register or if
                                                         not previously written the reset value of the MAP. */
	uint64_t reserved_0_15                : 16;
#else
	uint64_t reserved_0_15                : 16;
	uint64_t intr                         : 8;
	uint64_t reserved_24_63               : 40;
#endif
	} s;
	struct cvmx_npei_pcie_msi_rcv_b2_s    cn52xx;
	struct cvmx_npei_pcie_msi_rcv_b2_s    cn52xxp1;
	struct cvmx_npei_pcie_msi_rcv_b2_s    cn56xx;
	struct cvmx_npei_pcie_msi_rcv_b2_s    cn56xxp1;
};
typedef union cvmx_npei_pcie_msi_rcv_b2 cvmx_npei_pcie_msi_rcv_b2_t;

/**
 * cvmx_npei_pcie_msi_rcv_b3
 *
 * NPEI_PCIE_MSI_RCV_B3 = NPEI PCIe MSI Receive Byte 3
 *
 * Register where MSI writes are directed from the PCIe.
 */
union cvmx_npei_pcie_msi_rcv_b3 {
	uint64_t u64;
	struct cvmx_npei_pcie_msi_rcv_b3_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t intr                         : 8;  /**< A write to this register will result in a bit in
                                                         one of the NPEI_MSI_RCV# registers being set.
                                                         Which bit is set is dependent on the previously
                                                         written using the NPEI_MSI_WR_MAP register or if
                                                         not previously written the reset value of the MAP. */
	uint64_t reserved_0_23                : 24;
#else
	uint64_t reserved_0_23                : 24;
	uint64_t intr                         : 8;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_npei_pcie_msi_rcv_b3_s    cn52xx;
	struct cvmx_npei_pcie_msi_rcv_b3_s    cn52xxp1;
	struct cvmx_npei_pcie_msi_rcv_b3_s    cn56xx;
	struct cvmx_npei_pcie_msi_rcv_b3_s    cn56xxp1;
};
typedef union cvmx_npei_pcie_msi_rcv_b3 cvmx_npei_pcie_msi_rcv_b3_t;

/**
 * cvmx_npei_pkt#_cnts
 *
 * NPEI_PKT[0..31]_CNTS = NPEI Packet ring# Counts
 *
 * The counters for output rings.
 */
union cvmx_npei_pktx_cnts {
	uint64_t u64;
	struct cvmx_npei_pktx_cnts_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_54_63               : 10;
	uint64_t timer                        : 22; /**< Timer incremented every 1024 core clocks
                                                         when NPEI_PKTS#_CNTS[CNT] is non zero. Field
                                                         cleared when NPEI_PKTS#_CNTS[CNT] goes to 0.
                                                         Field is also cleared when NPEI_PKT_TIME_INT is
                                                         cleared.
                                                         The first increment of this count can occur
                                                         between 0 to 1023 core clocks. */
	uint64_t cnt                          : 32; /**< ring counter. This field is incremented as
                                                         packets are sent out and decremented in response to
                                                         writes to this field.
                                                         When NPEI_PKT_OUT_BMODE is '0' a value of 1 is
                                                         added to the register for each packet, when '1'
                                                         and the info-pointer is NOT used the length of the
                                                         packet plus 8 is added, when '1' and info-pointer
                                                         mode IS used the packet length is added to this
                                                         field. */
#else
	uint64_t cnt                          : 32;
	uint64_t timer                        : 22;
	uint64_t reserved_54_63               : 10;
#endif
	} s;
	struct cvmx_npei_pktx_cnts_s          cn52xx;
	struct cvmx_npei_pktx_cnts_s          cn56xx;
};
typedef union cvmx_npei_pktx_cnts cvmx_npei_pktx_cnts_t;

/**
 * cvmx_npei_pkt#_in_bp
 *
 * NPEI_PKT[0..31]_IN_BP = NPEI Packet ring# Input Backpressure
 *
 * The counters and thresholds for input packets to apply backpressure to processing of the packets.
 */
union cvmx_npei_pktx_in_bp {
	uint64_t u64;
	struct cvmx_npei_pktx_in_bp_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t wmark                        : 32; /**< When CNT is greater than this threshold no more
                                                         packets will be processed for this ring.
                                                         When writing this field of the NPEI_PKT#_IN_BP
                                                         register, use a 4-byte write so as to not write
                                                         any other field of this register. */
	uint64_t cnt                          : 32; /**< ring counter. This field is incremented by one
                                                         whenever OCTEON receives, buffers, and creates a
                                                         work queue entry for a packet that arrives by the
                                                         cooresponding input ring. A write to this field
                                                         will be subtracted from the field value.
                                                         When writing this field of the NPEI_PKT#_IN_BP
                                                         register, use a 4-byte write so as to not write
                                                         any other field of this register. */
#else
	uint64_t cnt                          : 32;
	uint64_t wmark                        : 32;
#endif
	} s;
	struct cvmx_npei_pktx_in_bp_s         cn52xx;
	struct cvmx_npei_pktx_in_bp_s         cn56xx;
};
typedef union cvmx_npei_pktx_in_bp cvmx_npei_pktx_in_bp_t;

/**
 * cvmx_npei_pkt#_instr_baddr
 *
 * NPEI_PKT[0..31]_INSTR_BADDR = NPEI Packet ring# Instruction Base Address
 *
 * Start of Instruction for input packets.
 */
union cvmx_npei_pktx_instr_baddr {
	uint64_t u64;
	struct cvmx_npei_pktx_instr_baddr_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t addr                         : 61; /**< Base address for Instructions. */
	uint64_t reserved_0_2                 : 3;
#else
	uint64_t reserved_0_2                 : 3;
	uint64_t addr                         : 61;
#endif
	} s;
	struct cvmx_npei_pktx_instr_baddr_s   cn52xx;
	struct cvmx_npei_pktx_instr_baddr_s   cn56xx;
};
typedef union cvmx_npei_pktx_instr_baddr cvmx_npei_pktx_instr_baddr_t;

/**
 * cvmx_npei_pkt#_instr_baoff_dbell
 *
 * NPEI_PKT[0..31]_INSTR_BAOFF_DBELL = NPEI Packet ring# Instruction Base Address Offset and Doorbell
 *
 * The doorbell and base address offset for next read.
 */
union cvmx_npei_pktx_instr_baoff_dbell {
	uint64_t u64;
	struct cvmx_npei_pktx_instr_baoff_dbell_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t aoff                         : 32; /**< The offset from the NPEI_PKT[0..31]_INSTR_BADDR
                                                         where the next instruction will be read. */
	uint64_t dbell                        : 32; /**< Instruction doorbell count. Writes to this field
                                                         will increment the value here. Reads will return
                                                         present value. A write of 0xffffffff will set the
                                                         DBELL and AOFF fields to '0'. */
#else
	uint64_t dbell                        : 32;
	uint64_t aoff                         : 32;
#endif
	} s;
	struct cvmx_npei_pktx_instr_baoff_dbell_s cn52xx;
	struct cvmx_npei_pktx_instr_baoff_dbell_s cn56xx;
};
typedef union cvmx_npei_pktx_instr_baoff_dbell cvmx_npei_pktx_instr_baoff_dbell_t;

/**
 * cvmx_npei_pkt#_instr_fifo_rsize
 *
 * NPEI_PKT[0..31]_INSTR_FIFO_RSIZE = NPEI Packet ring# Instruction FIFO and Ring Size.
 *
 * Fifo field and ring size for Instructions.
 */
union cvmx_npei_pktx_instr_fifo_rsize {
	uint64_t u64;
	struct cvmx_npei_pktx_instr_fifo_rsize_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t max                          : 9;  /**< Max Fifo Size. */
	uint64_t rrp                          : 9;  /**< Fifo read pointer. */
	uint64_t wrp                          : 9;  /**< Fifo write pointer. */
	uint64_t fcnt                         : 5;  /**< Fifo count. */
	uint64_t rsize                        : 32; /**< Instruction ring size. */
#else
	uint64_t rsize                        : 32;
	uint64_t fcnt                         : 5;
	uint64_t wrp                          : 9;
	uint64_t rrp                          : 9;
	uint64_t max                          : 9;
#endif
	} s;
	struct cvmx_npei_pktx_instr_fifo_rsize_s cn52xx;
	struct cvmx_npei_pktx_instr_fifo_rsize_s cn56xx;
};
typedef union cvmx_npei_pktx_instr_fifo_rsize cvmx_npei_pktx_instr_fifo_rsize_t;

/**
 * cvmx_npei_pkt#_instr_header
 *
 * NPEI_PKT[0..31]_INSTR_HEADER = NPEI Packet ring# Instruction Header.
 *
 * VAlues used to build input packet header.
 */
union cvmx_npei_pktx_instr_header {
	uint64_t u64;
	struct cvmx_npei_pktx_instr_header_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_44_63               : 20;
	uint64_t pbp                          : 1;  /**< Enable Packet-by-packet mode. */
	uint64_t reserved_38_42               : 5;
	uint64_t rparmode                     : 2;  /**< Parse Mode. Used when packet is raw and PBP==0. */
	uint64_t reserved_35_35               : 1;
	uint64_t rskp_len                     : 7;  /**< Skip Length. Used when packet is raw and PBP==0. */
	uint64_t reserved_22_27               : 6;
	uint64_t use_ihdr                     : 1;  /**< When set '1' the instruction header will be sent
                                                         as part of the packet data, regardless of the
                                                         value of bit [63] of the instruction header.
                                                         USE_IHDR must be set whenever PBP is set. */
	uint64_t reserved_16_20               : 5;
	uint64_t par_mode                     : 2;  /**< Parse Mode. Used when USE_IHDR is set and packet
                                                         is not raw and PBP is not set. */
	uint64_t reserved_13_13               : 1;
	uint64_t skp_len                      : 7;  /**< Skip Length. Used when USE_IHDR is set and packet
                                                         is not raw and PBP is not set. */
	uint64_t reserved_0_5                 : 6;
#else
	uint64_t reserved_0_5                 : 6;
	uint64_t skp_len                      : 7;
	uint64_t reserved_13_13               : 1;
	uint64_t par_mode                     : 2;
	uint64_t reserved_16_20               : 5;
	uint64_t use_ihdr                     : 1;
	uint64_t reserved_22_27               : 6;
	uint64_t rskp_len                     : 7;
	uint64_t reserved_35_35               : 1;
	uint64_t rparmode                     : 2;
	uint64_t reserved_38_42               : 5;
	uint64_t pbp                          : 1;
	uint64_t reserved_44_63               : 20;
#endif
	} s;
	struct cvmx_npei_pktx_instr_header_s  cn52xx;
	struct cvmx_npei_pktx_instr_header_s  cn56xx;
};
typedef union cvmx_npei_pktx_instr_header cvmx_npei_pktx_instr_header_t;

/**
 * cvmx_npei_pkt#_slist_baddr
 *
 * NPEI_PKT[0..31]_SLIST_BADDR = NPEI Packet ring# Scatter List Base Address
 *
 * Start of Scatter List for output packet pointers - MUST be 16 byte alligned
 */
union cvmx_npei_pktx_slist_baddr {
	uint64_t u64;
	struct cvmx_npei_pktx_slist_baddr_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t addr                         : 60; /**< Base address for scatter list pointers. */
	uint64_t reserved_0_3                 : 4;
#else
	uint64_t reserved_0_3                 : 4;
	uint64_t addr                         : 60;
#endif
	} s;
	struct cvmx_npei_pktx_slist_baddr_s   cn52xx;
	struct cvmx_npei_pktx_slist_baddr_s   cn56xx;
};
typedef union cvmx_npei_pktx_slist_baddr cvmx_npei_pktx_slist_baddr_t;

/**
 * cvmx_npei_pkt#_slist_baoff_dbell
 *
 * NPEI_PKT[0..31]_SLIST_BAOFF_DBELL = NPEI Packet ring# Scatter List Base Address Offset and Doorbell
 *
 * The doorbell and base address offset for next read.
 */
union cvmx_npei_pktx_slist_baoff_dbell {
	uint64_t u64;
	struct cvmx_npei_pktx_slist_baoff_dbell_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t aoff                         : 32; /**< The offset from the NPEI_PKT[0..31]_SLIST_BADDR
                                                         where the next SList pointer will be read.
                                                         A write of 0xFFFFFFFF to the DBELL field will
                                                         clear DBELL and AOFF */
	uint64_t dbell                        : 32; /**< Scatter list doorbell count. Writes to this field
                                                         will increment the value here. Reads will return
                                                         present value. The value of this field is
                                                         decremented as read operations are ISSUED for
                                                         scatter pointers.
                                                         A write of 0xFFFFFFFF will clear DBELL and AOFF */
#else
	uint64_t dbell                        : 32;
	uint64_t aoff                         : 32;
#endif
	} s;
	struct cvmx_npei_pktx_slist_baoff_dbell_s cn52xx;
	struct cvmx_npei_pktx_slist_baoff_dbell_s cn56xx;
};
typedef union cvmx_npei_pktx_slist_baoff_dbell cvmx_npei_pktx_slist_baoff_dbell_t;

/**
 * cvmx_npei_pkt#_slist_fifo_rsize
 *
 * NPEI_PKT[0..31]_SLIST_FIFO_RSIZE = NPEI Packet ring# Scatter List FIFO and Ring Size.
 *
 * The number of scatter pointer pairs in the scatter list.
 */
union cvmx_npei_pktx_slist_fifo_rsize {
	uint64_t u64;
	struct cvmx_npei_pktx_slist_fifo_rsize_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t rsize                        : 32; /**< The number of scatter pointer pairs contained in
                                                         the scatter list ring. */
#else
	uint64_t rsize                        : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_npei_pktx_slist_fifo_rsize_s cn52xx;
	struct cvmx_npei_pktx_slist_fifo_rsize_s cn56xx;
};
typedef union cvmx_npei_pktx_slist_fifo_rsize cvmx_npei_pktx_slist_fifo_rsize_t;

/**
 * cvmx_npei_pkt_cnt_int
 *
 * NPEI_PKT_CNT_INT = NPI Packet Counter Interrupt
 *
 * The packets rings that are interrupting because of Packet Counters.
 */
union cvmx_npei_pkt_cnt_int {
	uint64_t u64;
	struct cvmx_npei_pkt_cnt_int_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t port                         : 32; /**< Bit vector cooresponding to ring number is set when
                                                         NPEI_PKT#_CNTS[CNT] is greater
                                                         than NPEI_PKT_INT_LEVELS[CNT]. */
#else
	uint64_t port                         : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_npei_pkt_cnt_int_s        cn52xx;
	struct cvmx_npei_pkt_cnt_int_s        cn56xx;
};
typedef union cvmx_npei_pkt_cnt_int cvmx_npei_pkt_cnt_int_t;

/**
 * cvmx_npei_pkt_cnt_int_enb
 *
 * NPEI_PKT_CNT_INT_ENB = NPI Packet Counter Interrupt Enable
 *
 * Enable for the packets rings that are interrupting because of Packet Counters.
 */
union cvmx_npei_pkt_cnt_int_enb {
	uint64_t u64;
	struct cvmx_npei_pkt_cnt_int_enb_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t port                         : 32; /**< Bit vector cooresponding to ring number when set
                                                         allows NPEI_PKT_CNT_INT to generate an interrupt. */
#else
	uint64_t port                         : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_npei_pkt_cnt_int_enb_s    cn52xx;
	struct cvmx_npei_pkt_cnt_int_enb_s    cn56xx;
};
typedef union cvmx_npei_pkt_cnt_int_enb cvmx_npei_pkt_cnt_int_enb_t;

/**
 * cvmx_npei_pkt_data_out_es
 *
 * NPEI_PKT_DATA_OUT_ES = NPEI's Packet Data Out Endian Swap
 *
 * The Endian Swap for writing Data Out.
 */
union cvmx_npei_pkt_data_out_es {
	uint64_t u64;
	struct cvmx_npei_pkt_data_out_es_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t es                           : 64; /**< The endian swap mode for Packet rings 0 through 31.
                                                         Two bits are used per ring (i.e. ring 0 [1:0],
                                                         ring 1 [3:2], ....). */
#else
	uint64_t es                           : 64;
#endif
	} s;
	struct cvmx_npei_pkt_data_out_es_s    cn52xx;
	struct cvmx_npei_pkt_data_out_es_s    cn56xx;
};
typedef union cvmx_npei_pkt_data_out_es cvmx_npei_pkt_data_out_es_t;

/**
 * cvmx_npei_pkt_data_out_ns
 *
 * NPEI_PKT_DATA_OUT_NS = NPEI's Packet Data Out No Snoop
 *
 * The NS field for the TLP when writing packet data.
 */
union cvmx_npei_pkt_data_out_ns {
	uint64_t u64;
	struct cvmx_npei_pkt_data_out_ns_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t nsr                          : 32; /**< When asserted '1' the vector bit cooresponding
                                                         to the Packet-ring will enable NS in TLP header. */
#else
	uint64_t nsr                          : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_npei_pkt_data_out_ns_s    cn52xx;
	struct cvmx_npei_pkt_data_out_ns_s    cn56xx;
};
typedef union cvmx_npei_pkt_data_out_ns cvmx_npei_pkt_data_out_ns_t;

/**
 * cvmx_npei_pkt_data_out_ror
 *
 * NPEI_PKT_DATA_OUT_ROR = NPEI's Packet Data Out Relaxed Ordering
 *
 * The ROR field for the TLP when writing Packet Data.
 */
union cvmx_npei_pkt_data_out_ror {
	uint64_t u64;
	struct cvmx_npei_pkt_data_out_ror_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t ror                          : 32; /**< When asserted '1' the vector bit cooresponding
                                                         to the Packet-ring will enable ROR in TLP header. */
#else
	uint64_t ror                          : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_npei_pkt_data_out_ror_s   cn52xx;
	struct cvmx_npei_pkt_data_out_ror_s   cn56xx;
};
typedef union cvmx_npei_pkt_data_out_ror cvmx_npei_pkt_data_out_ror_t;

/**
 * cvmx_npei_pkt_dpaddr
 *
 * NPEI_PKT_DPADDR = NPEI's Packet Data Pointer Addr
 *
 * Used to detemine address and attributes for packet data writes.
 */
union cvmx_npei_pkt_dpaddr {
	uint64_t u64;
	struct cvmx_npei_pkt_dpaddr_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t dptr                         : 32; /**< When asserted '1' the vector bit cooresponding
                                                         to the Packet-ring will use:
                                                         the address[63:60] to write packet data
                                                         comes from the DPTR[63:60] in the scatter-list
                                                         pair and the RO, NS, ES values come from the O0_ES,
                                                         O0_NS, O0_RO. When '0' the RO == DPTR[60],
                                                         NS == DPTR[61], ES == DPTR[63:62], the address the
                                                         packet will be written to is ADDR[63:60] ==
                                                         O0_ES[1:0], O0_NS, O0_RO. */
#else
	uint64_t dptr                         : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_npei_pkt_dpaddr_s         cn52xx;
	struct cvmx_npei_pkt_dpaddr_s         cn56xx;
};
typedef union cvmx_npei_pkt_dpaddr cvmx_npei_pkt_dpaddr_t;

/**
 * cvmx_npei_pkt_in_bp
 *
 * NPEI_PKT_IN_BP = NPEI Packet Input Backpressure
 *
 * Which input rings have backpressure applied.
 */
union cvmx_npei_pkt_in_bp {
	uint64_t u64;
	struct cvmx_npei_pkt_in_bp_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t bp                           : 32; /**< A packet input  ring that has its count greater
                                                         than its WMARK will have backpressure applied.
                                                         Each of the 32 bits coorespond to an input ring.
                                                         When '1' that ring has backpressure applied an
                                                         will fetch no more instructions, but will process
                                                         any previously fetched instructions. */
#else
	uint64_t bp                           : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_npei_pkt_in_bp_s          cn52xx;
	struct cvmx_npei_pkt_in_bp_s          cn56xx;
};
typedef union cvmx_npei_pkt_in_bp cvmx_npei_pkt_in_bp_t;

/**
 * cvmx_npei_pkt_in_done#_cnts
 *
 * NPEI_PKT_IN_DONE[0..31]_CNTS = NPEI Instruction Done ring# Counts
 *
 * Counters for instructions completed on Input rings.
 */
union cvmx_npei_pkt_in_donex_cnts {
	uint64_t u64;
	struct cvmx_npei_pkt_in_donex_cnts_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t cnt                          : 32; /**< This field is incrmented by '1' when an instruction
                                                         is completed. This field is incremented as the
                                                         last of the data is read from the PCIe. */
#else
	uint64_t cnt                          : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_npei_pkt_in_donex_cnts_s  cn52xx;
	struct cvmx_npei_pkt_in_donex_cnts_s  cn56xx;
};
typedef union cvmx_npei_pkt_in_donex_cnts cvmx_npei_pkt_in_donex_cnts_t;

/**
 * cvmx_npei_pkt_in_instr_counts
 *
 * NPEI_PKT_IN_INSTR_COUNTS = NPEI Packet Input Instrutction Counts
 *
 * Keeps track of the number of instructions read into the FIFO and Packets sent to IPD.
 */
union cvmx_npei_pkt_in_instr_counts {
	uint64_t u64;
	struct cvmx_npei_pkt_in_instr_counts_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t wr_cnt                       : 32; /**< Shows the number of packets sent to the IPD. */
	uint64_t rd_cnt                       : 32; /**< Shows the value of instructions that have had reads
                                                         issued for them.
                                                         to the Packet-ring is in reset. */
#else
	uint64_t rd_cnt                       : 32;
	uint64_t wr_cnt                       : 32;
#endif
	} s;
	struct cvmx_npei_pkt_in_instr_counts_s cn52xx;
	struct cvmx_npei_pkt_in_instr_counts_s cn56xx;
};
typedef union cvmx_npei_pkt_in_instr_counts cvmx_npei_pkt_in_instr_counts_t;

/**
 * cvmx_npei_pkt_in_pcie_port
 *
 * NPEI_PKT_IN_PCIE_PORT = NPEI's Packet In To PCIe Port Assignment
 *
 * Assigns Packet Input rings to PCIe ports.
 */
union cvmx_npei_pkt_in_pcie_port {
	uint64_t u64;
	struct cvmx_npei_pkt_in_pcie_port_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t pp                           : 64; /**< The PCIe port that the Packet ring number is
                                                         assigned. Two bits are used per ring (i.e. ring 0
                                                         [1:0], ring 1 [3:2], ....). A value of '0 means
                                                         that the Packetring is assign to PCIe Port 0, a '1'
                                                         PCIe Port 1, '2' and '3' are reserved. */
#else
	uint64_t pp                           : 64;
#endif
	} s;
	struct cvmx_npei_pkt_in_pcie_port_s   cn52xx;
	struct cvmx_npei_pkt_in_pcie_port_s   cn56xx;
};
typedef union cvmx_npei_pkt_in_pcie_port cvmx_npei_pkt_in_pcie_port_t;

/**
 * cvmx_npei_pkt_input_control
 *
 * NPEI_PKT_INPUT_CONTROL = NPEI's Packet Input Control
 *
 * Control for reads for gather list and instructions.
 */
union cvmx_npei_pkt_input_control {
	uint64_t u64;
	struct cvmx_npei_pkt_input_control_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_23_63               : 41;
	uint64_t pkt_rr                       : 1;  /**< When set '1' the input packet selection will be
                                                         made with a Round Robin arbitration. When '0'
                                                         the input packet ring is fixed in priority,
                                                         where the lower ring number has higher priority. */
	uint64_t pbp_dhi                      : 13; /**< Field when in [PBP] is set to be used in
                                                         calculating a DPTR. */
	uint64_t d_nsr                        : 1;  /**< Enables '1' NoSnoop for reading of
                                                         gather data. */
	uint64_t d_esr                        : 2;  /**< The Endian-Swap-Mode for reading of
                                                         gather data. */
	uint64_t d_ror                        : 1;  /**< Enables '1' Relaxed Ordering for reading of
                                                         gather data. */
	uint64_t use_csr                      : 1;  /**< When set '1' the csr value will be used for
                                                         ROR, ESR, and NSR. When clear '0' the value in
                                                         DPTR will be used. In turn the bits not used for
                                                         ROR, ESR, and NSR, will be used for bits [63:60]
                                                         of the address used to fetch packet data. */
	uint64_t nsr                          : 1;  /**< Enables '1' NoSnoop for reading of
                                                         gather list and gather instruction. */
	uint64_t esr                          : 2;  /**< The Endian-Swap-Mode for reading of
                                                         gather list and gather instruction. */
	uint64_t ror                          : 1;  /**< Enables '1' Relaxed Ordering for reading of
                                                         gather list and gather instruction. */
#else
	uint64_t ror                          : 1;
	uint64_t esr                          : 2;
	uint64_t nsr                          : 1;
	uint64_t use_csr                      : 1;
	uint64_t d_ror                        : 1;
	uint64_t d_esr                        : 2;
	uint64_t d_nsr                        : 1;
	uint64_t pbp_dhi                      : 13;
	uint64_t pkt_rr                       : 1;
	uint64_t reserved_23_63               : 41;
#endif
	} s;
	struct cvmx_npei_pkt_input_control_s  cn52xx;
	struct cvmx_npei_pkt_input_control_s  cn56xx;
};
typedef union cvmx_npei_pkt_input_control cvmx_npei_pkt_input_control_t;

/**
 * cvmx_npei_pkt_instr_enb
 *
 * NPEI_PKT_INSTR_ENB = NPEI's Packet Instruction Enable
 *
 * Enables the instruction fetch for a Packet-ring.
 */
union cvmx_npei_pkt_instr_enb {
	uint64_t u64;
	struct cvmx_npei_pkt_instr_enb_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t enb                          : 32; /**< When asserted '1' the vector bit cooresponding
                                                         to the Packet-ring is enabled. */
#else
	uint64_t enb                          : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_npei_pkt_instr_enb_s      cn52xx;
	struct cvmx_npei_pkt_instr_enb_s      cn56xx;
};
typedef union cvmx_npei_pkt_instr_enb cvmx_npei_pkt_instr_enb_t;

/**
 * cvmx_npei_pkt_instr_rd_size
 *
 * NPEI_PKT_INSTR_RD_SIZE = NPEI Instruction Read Size
 *
 * The number of instruction allowed to be read at one time.
 */
union cvmx_npei_pkt_instr_rd_size {
	uint64_t u64;
	struct cvmx_npei_pkt_instr_rd_size_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t rdsize                       : 64; /**< Number of instructions to be read in one PCIe read
                                                         request for the 4 PKOport - 8 rings. Every two bits
                                                         (i.e. 1:0, 3:2, 5:4..) are assign to the port/ring
                                                         combinations.
                                                         - 15:0  PKOPort0,Ring 7..0  31:16 PKOPort1,Ring 7..0
                                                         - 47:32 PKOPort2,Ring 7..0  63:48 PKOPort3,Ring 7..0
                                                         Two bit value are:
                                                         0 - 1 Instruction
                                                         1 - 2 Instructions
                                                         2 - 3 Instructions
                                                         3 - 4 Instructions */
#else
	uint64_t rdsize                       : 64;
#endif
	} s;
	struct cvmx_npei_pkt_instr_rd_size_s  cn52xx;
	struct cvmx_npei_pkt_instr_rd_size_s  cn56xx;
};
typedef union cvmx_npei_pkt_instr_rd_size cvmx_npei_pkt_instr_rd_size_t;

/**
 * cvmx_npei_pkt_instr_size
 *
 * NPEI_PKT_INSTR_SIZE = NPEI's Packet Instruction Size
 *
 * Determines if instructions are 64 or 32 byte in size for a Packet-ring.
 */
union cvmx_npei_pkt_instr_size {
	uint64_t u64;
	struct cvmx_npei_pkt_instr_size_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t is_64b                       : 32; /**< When asserted '1' the vector bit cooresponding
                                                         to the Packet-ring is a 64-byte instruction. */
#else
	uint64_t is_64b                       : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_npei_pkt_instr_size_s     cn52xx;
	struct cvmx_npei_pkt_instr_size_s     cn56xx;
};
typedef union cvmx_npei_pkt_instr_size cvmx_npei_pkt_instr_size_t;

/**
 * cvmx_npei_pkt_int_levels
 *
 * 0x90F0 reserved NPEI_PKT_PCIE_PORT2
 *
 *
 *                  NPEI_PKT_INT_LEVELS = NPEI's Packet Interrupt Levels
 *
 * Output packet interrupt levels.
 */
union cvmx_npei_pkt_int_levels {
	uint64_t u64;
	struct cvmx_npei_pkt_int_levels_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_54_63               : 10;
	uint64_t time                         : 22; /**< When NPEI_PKT#_CNTS[TIMER] is greater than this
                                                         value an interrupt is generated. */
	uint64_t cnt                          : 32; /**< When NPEI_PKT#_CNTS[CNT] becomes
                                                         greater than this value an interrupt is generated. */
#else
	uint64_t cnt                          : 32;
	uint64_t time                         : 22;
	uint64_t reserved_54_63               : 10;
#endif
	} s;
	struct cvmx_npei_pkt_int_levels_s     cn52xx;
	struct cvmx_npei_pkt_int_levels_s     cn56xx;
};
typedef union cvmx_npei_pkt_int_levels cvmx_npei_pkt_int_levels_t;

/**
 * cvmx_npei_pkt_iptr
 *
 * NPEI_PKT_IPTR = NPEI's Packet Info Poitner
 *
 * Controls using the Info-Pointer to store length and data.
 */
union cvmx_npei_pkt_iptr {
	uint64_t u64;
	struct cvmx_npei_pkt_iptr_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t iptr                         : 32; /**< When asserted '1' the vector bit cooresponding
                                                         to the Packet-ring will use the Info-Pointer to
                                                         store length and data. */
#else
	uint64_t iptr                         : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_npei_pkt_iptr_s           cn52xx;
	struct cvmx_npei_pkt_iptr_s           cn56xx;
};
typedef union cvmx_npei_pkt_iptr cvmx_npei_pkt_iptr_t;

/**
 * cvmx_npei_pkt_out_bmode
 *
 * NPEI_PKT_OUT_BMODE = NPEI's Packet Out Byte Mode
 *
 * Control the updating of the NPEI_PKT#_CNT register.
 */
union cvmx_npei_pkt_out_bmode {
	uint64_t u64;
	struct cvmx_npei_pkt_out_bmode_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t bmode                        : 32; /**< When asserted '1' the vector bit cooresponding
                                                         to the Packet-ring will have its NPEI_PKT#_CNT
                                                         register updated with the number of bytes in the
                                                         packet sent, when '0' the register will have a
                                                         value of '1' added. */
#else
	uint64_t bmode                        : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_npei_pkt_out_bmode_s      cn52xx;
	struct cvmx_npei_pkt_out_bmode_s      cn56xx;
};
typedef union cvmx_npei_pkt_out_bmode cvmx_npei_pkt_out_bmode_t;

/**
 * cvmx_npei_pkt_out_enb
 *
 * NPEI_PKT_OUT_ENB = NPEI's Packet Output Enable
 *
 * Enables the output packet engines.
 */
union cvmx_npei_pkt_out_enb {
	uint64_t u64;
	struct cvmx_npei_pkt_out_enb_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t enb                          : 32; /**< When asserted '1' the vector bit cooresponding
                                                         to the Packet-ring is enabled.
                                                         If an error occurs on reading pointers for an
                                                         output ring, the ring will be disabled by clearing
                                                         the bit associated with the ring to '0'. */
#else
	uint64_t enb                          : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_npei_pkt_out_enb_s        cn52xx;
	struct cvmx_npei_pkt_out_enb_s        cn56xx;
};
typedef union cvmx_npei_pkt_out_enb cvmx_npei_pkt_out_enb_t;

/**
 * cvmx_npei_pkt_output_wmark
 *
 * NPEI_PKT_OUTPUT_WMARK = NPEI's Packet Output Water Mark
 *
 * Value that when the NPEI_PKT#_SLIST_BAOFF_DBELL[DBELL] value is less then that backpressure for the rings will be applied.
 */
union cvmx_npei_pkt_output_wmark {
	uint64_t u64;
	struct cvmx_npei_pkt_output_wmark_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t wmark                        : 32; /**< Value when DBELL count drops below backpressure
                                                         for the ring will be applied to the PKO. */
#else
	uint64_t wmark                        : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_npei_pkt_output_wmark_s   cn52xx;
	struct cvmx_npei_pkt_output_wmark_s   cn56xx;
};
typedef union cvmx_npei_pkt_output_wmark cvmx_npei_pkt_output_wmark_t;

/**
 * cvmx_npei_pkt_pcie_port
 *
 * NPEI_PKT_PCIE_PORT = NPEI's Packet To PCIe Port Assignment
 *
 * Assigns Packet Ports to PCIe ports.
 */
union cvmx_npei_pkt_pcie_port {
	uint64_t u64;
	struct cvmx_npei_pkt_pcie_port_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t pp                           : 64; /**< The PCIe port that the Packet ring number is
                                                         assigned. Two bits are used per ring (i.e. ring 0
                                                         [1:0], ring 1 [3:2], ....). A value of '0 means
                                                         that the Packetring is assign to PCIe Port 0, a '1'
                                                         PCIe Port 1, '2' and '3' are reserved. */
#else
	uint64_t pp                           : 64;
#endif
	} s;
	struct cvmx_npei_pkt_pcie_port_s      cn52xx;
	struct cvmx_npei_pkt_pcie_port_s      cn56xx;
};
typedef union cvmx_npei_pkt_pcie_port cvmx_npei_pkt_pcie_port_t;

/**
 * cvmx_npei_pkt_port_in_rst
 *
 * NPEI_PKT_PORT_IN_RST = NPEI Packet Port In Reset
 *
 * Vector bits related to ring-port for ones that are reset.
 */
union cvmx_npei_pkt_port_in_rst {
	uint64_t u64;
	struct cvmx_npei_pkt_port_in_rst_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t in_rst                       : 32; /**< When asserted '1' the vector bit cooresponding
                                                         to the inbound Packet-ring is in reset. */
	uint64_t out_rst                      : 32; /**< When asserted '1' the vector bit cooresponding
                                                         to the outbound Packet-ring is in reset. */
#else
	uint64_t out_rst                      : 32;
	uint64_t in_rst                       : 32;
#endif
	} s;
	struct cvmx_npei_pkt_port_in_rst_s    cn52xx;
	struct cvmx_npei_pkt_port_in_rst_s    cn56xx;
};
typedef union cvmx_npei_pkt_port_in_rst cvmx_npei_pkt_port_in_rst_t;

/**
 * cvmx_npei_pkt_slist_es
 *
 * NPEI_PKT_SLIST_ES = NPEI's Packet Scatter List Endian Swap
 *
 * The Endian Swap for Scatter List Read.
 */
union cvmx_npei_pkt_slist_es {
	uint64_t u64;
	struct cvmx_npei_pkt_slist_es_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t es                           : 64; /**< The endian swap mode for Packet rings 0 through 31.
                                                         Two bits are used per ring (i.e. ring 0 [1:0],
                                                         ring 1 [3:2], ....). */
#else
	uint64_t es                           : 64;
#endif
	} s;
	struct cvmx_npei_pkt_slist_es_s       cn52xx;
	struct cvmx_npei_pkt_slist_es_s       cn56xx;
};
typedef union cvmx_npei_pkt_slist_es cvmx_npei_pkt_slist_es_t;

/**
 * cvmx_npei_pkt_slist_id_size
 *
 * NPEI_PKT_SLIST_ID_SIZE = NPEI Packet Scatter List Info and Data Size
 *
 * The Size of the information and data fields pointed to by Scatter List pointers.
 */
union cvmx_npei_pkt_slist_id_size {
	uint64_t u64;
	struct cvmx_npei_pkt_slist_id_size_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_23_63               : 41;
	uint64_t isize                        : 7;  /**< Information size. Legal sizes are 0 to 120. */
	uint64_t bsize                        : 16; /**< Data size. */
#else
	uint64_t bsize                        : 16;
	uint64_t isize                        : 7;
	uint64_t reserved_23_63               : 41;
#endif
	} s;
	struct cvmx_npei_pkt_slist_id_size_s  cn52xx;
	struct cvmx_npei_pkt_slist_id_size_s  cn56xx;
};
typedef union cvmx_npei_pkt_slist_id_size cvmx_npei_pkt_slist_id_size_t;

/**
 * cvmx_npei_pkt_slist_ns
 *
 * NPEI_PKT_SLIST_NS = NPEI's Packet Scatter List No Snoop
 *
 * The NS field for the TLP when fetching Scatter List.
 */
union cvmx_npei_pkt_slist_ns {
	uint64_t u64;
	struct cvmx_npei_pkt_slist_ns_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t nsr                          : 32; /**< When asserted '1' the vector bit cooresponding
                                                         to the Packet-ring will enable NS in TLP header. */
#else
	uint64_t nsr                          : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_npei_pkt_slist_ns_s       cn52xx;
	struct cvmx_npei_pkt_slist_ns_s       cn56xx;
};
typedef union cvmx_npei_pkt_slist_ns cvmx_npei_pkt_slist_ns_t;

/**
 * cvmx_npei_pkt_slist_ror
 *
 * NPEI_PKT_SLIST_ROR = NPEI's Packet Scatter List Relaxed Ordering
 *
 * The ROR field for the TLP when fetching Scatter List.
 */
union cvmx_npei_pkt_slist_ror {
	uint64_t u64;
	struct cvmx_npei_pkt_slist_ror_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t ror                          : 32; /**< When asserted '1' the vector bit cooresponding
                                                         to the Packet-ring will enable ROR in TLP header. */
#else
	uint64_t ror                          : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_npei_pkt_slist_ror_s      cn52xx;
	struct cvmx_npei_pkt_slist_ror_s      cn56xx;
};
typedef union cvmx_npei_pkt_slist_ror cvmx_npei_pkt_slist_ror_t;

/**
 * cvmx_npei_pkt_time_int
 *
 * NPEI_PKT_TIME_INT = NPEI Packet Timer Interrupt
 *
 * The packets rings that are interrupting because of Packet Timers.
 */
union cvmx_npei_pkt_time_int {
	uint64_t u64;
	struct cvmx_npei_pkt_time_int_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t port                         : 32; /**< Bit vector cooresponding to ring number is set when
                                                         NPEI_PKT#_CNTS[TIMER] is greater than
                                                         NPEI_PKT_INT_LEVELS[TIME]. */
#else
	uint64_t port                         : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_npei_pkt_time_int_s       cn52xx;
	struct cvmx_npei_pkt_time_int_s       cn56xx;
};
typedef union cvmx_npei_pkt_time_int cvmx_npei_pkt_time_int_t;

/**
 * cvmx_npei_pkt_time_int_enb
 *
 * NPEI_PKT_TIME_INT_ENB = NPEI Packet Timer Interrupt Enable
 *
 * The packets rings that are interrupting because of Packet Timers.
 */
union cvmx_npei_pkt_time_int_enb {
	uint64_t u64;
	struct cvmx_npei_pkt_time_int_enb_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t port                         : 32; /**< Bit vector cooresponding to ring number when set
                                                         allows NPEI_PKT_TIME_INT to generate an interrupt. */
#else
	uint64_t port                         : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_npei_pkt_time_int_enb_s   cn52xx;
	struct cvmx_npei_pkt_time_int_enb_s   cn56xx;
};
typedef union cvmx_npei_pkt_time_int_enb cvmx_npei_pkt_time_int_enb_t;

/**
 * cvmx_npei_rsl_int_blocks
 *
 * NPEI_RSL_INT_BLOCKS = NPEI RSL Interrupt Blocks Register
 *
 * Reading this register will return a vector with a bit set '1' for a corresponding RSL block
 * that presently has an interrupt pending. The Field Description below supplies the name of the
 * register that software should read to find out why that intterupt bit is set.
 */
union cvmx_npei_rsl_int_blocks {
	uint64_t u64;
	struct cvmx_npei_rsl_int_blocks_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_31_63               : 33;
	uint64_t iob                          : 1;  /**< IOB_INT_SUM */
	uint64_t lmc1                         : 1;  /**< LMC1_MEM_CFG0 */
	uint64_t agl                          : 1;  /**< AGL_GMX_RX0_INT_REG & AGL_GMX_TX_INT_REG */
	uint64_t reserved_24_27               : 4;
	uint64_t asxpcs1                      : 1;  /**< PCS1_INT*_REG */
	uint64_t asxpcs0                      : 1;  /**< PCS0_INT*_REG */
	uint64_t reserved_21_21               : 1;
	uint64_t pip                          : 1;  /**< PIP_INT_REG. */
	uint64_t spx1                         : 1;  /**< Always reads as zero */
	uint64_t spx0                         : 1;  /**< Always reads as zero */
	uint64_t lmc0                         : 1;  /**< LMC0_MEM_CFG0 */
	uint64_t l2c                          : 1;  /**< L2C_INT_STAT */
	uint64_t usb1                         : 1;  /**< Always reads as zero */
	uint64_t rad                          : 1;  /**< RAD_REG_ERROR */
	uint64_t usb                          : 1;  /**< USBN0_INT_SUM */
	uint64_t pow                          : 1;  /**< POW_ECC_ERR */
	uint64_t tim                          : 1;  /**< TIM_REG_ERROR */
	uint64_t pko                          : 1;  /**< PKO_REG_ERROR */
	uint64_t ipd                          : 1;  /**< IPD_INT_SUM */
	uint64_t reserved_8_8                 : 1;
	uint64_t zip                          : 1;  /**< ZIP_ERROR */
	uint64_t dfa                          : 1;  /**< Always reads as zero */
	uint64_t fpa                          : 1;  /**< FPA_INT_SUM */
	uint64_t key                          : 1;  /**< KEY_INT_SUM */
	uint64_t npei                         : 1;  /**< NPEI_INT_SUM */
	uint64_t gmx1                         : 1;  /**< GMX1_RX*_INT_REG & GMX1_TX_INT_REG */
	uint64_t gmx0                         : 1;  /**< GMX0_RX*_INT_REG & GMX0_TX_INT_REG */
	uint64_t mio                          : 1;  /**< MIO_BOOT_ERR */
#else
	uint64_t mio                          : 1;
	uint64_t gmx0                         : 1;
	uint64_t gmx1                         : 1;
	uint64_t npei                         : 1;
	uint64_t key                          : 1;
	uint64_t fpa                          : 1;
	uint64_t dfa                          : 1;
	uint64_t zip                          : 1;
	uint64_t reserved_8_8                 : 1;
	uint64_t ipd                          : 1;
	uint64_t pko                          : 1;
	uint64_t tim                          : 1;
	uint64_t pow                          : 1;
	uint64_t usb                          : 1;
	uint64_t rad                          : 1;
	uint64_t usb1                         : 1;
	uint64_t l2c                          : 1;
	uint64_t lmc0                         : 1;
	uint64_t spx0                         : 1;
	uint64_t spx1                         : 1;
	uint64_t pip                          : 1;
	uint64_t reserved_21_21               : 1;
	uint64_t asxpcs0                      : 1;
	uint64_t asxpcs1                      : 1;
	uint64_t reserved_24_27               : 4;
	uint64_t agl                          : 1;
	uint64_t lmc1                         : 1;
	uint64_t iob                          : 1;
	uint64_t reserved_31_63               : 33;
#endif
	} s;
	struct cvmx_npei_rsl_int_blocks_s     cn52xx;
	struct cvmx_npei_rsl_int_blocks_s     cn52xxp1;
	struct cvmx_npei_rsl_int_blocks_s     cn56xx;
	struct cvmx_npei_rsl_int_blocks_s     cn56xxp1;
};
typedef union cvmx_npei_rsl_int_blocks cvmx_npei_rsl_int_blocks_t;

/**
 * cvmx_npei_scratch_1
 *
 * NPEI_SCRATCH_1 = NPEI's Scratch 1
 *
 * A general purpose 64 bit register for SW use.
 */
union cvmx_npei_scratch_1 {
	uint64_t u64;
	struct cvmx_npei_scratch_1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t data                         : 64; /**< The value in this register is totaly SW dependent. */
#else
	uint64_t data                         : 64;
#endif
	} s;
	struct cvmx_npei_scratch_1_s          cn52xx;
	struct cvmx_npei_scratch_1_s          cn52xxp1;
	struct cvmx_npei_scratch_1_s          cn56xx;
	struct cvmx_npei_scratch_1_s          cn56xxp1;
};
typedef union cvmx_npei_scratch_1 cvmx_npei_scratch_1_t;

/**
 * cvmx_npei_state1
 *
 * NPEI_STATE1 = NPEI State 1
 *
 * State machines in NPEI. For debug.
 */
union cvmx_npei_state1 {
	uint64_t u64;
	struct cvmx_npei_state1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t cpl1                         : 12; /**< CPL1 State */
	uint64_t cpl0                         : 12; /**< CPL0 State */
	uint64_t arb                          : 1;  /**< ARB State */
	uint64_t csr                          : 39; /**< CSR State */
#else
	uint64_t csr                          : 39;
	uint64_t arb                          : 1;
	uint64_t cpl0                         : 12;
	uint64_t cpl1                         : 12;
#endif
	} s;
	struct cvmx_npei_state1_s             cn52xx;
	struct cvmx_npei_state1_s             cn52xxp1;
	struct cvmx_npei_state1_s             cn56xx;
	struct cvmx_npei_state1_s             cn56xxp1;
};
typedef union cvmx_npei_state1 cvmx_npei_state1_t;

/**
 * cvmx_npei_state2
 *
 * NPEI_STATE2 = NPEI State 2
 *
 * State machines in NPEI. For debug.
 */
union cvmx_npei_state2 {
	uint64_t u64;
	struct cvmx_npei_state2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_48_63               : 16;
	uint64_t npei                         : 1;  /**< NPEI State */
	uint64_t rac                          : 1;  /**< RAC State */
	uint64_t csm1                         : 15; /**< CSM1 State */
	uint64_t csm0                         : 15; /**< CSM0 State */
	uint64_t nnp0                         : 8;  /**< NNP0 State */
	uint64_t nnd                          : 8;  /**< NND State */
#else
	uint64_t nnd                          : 8;
	uint64_t nnp0                         : 8;
	uint64_t csm0                         : 15;
	uint64_t csm1                         : 15;
	uint64_t rac                          : 1;
	uint64_t npei                         : 1;
	uint64_t reserved_48_63               : 16;
#endif
	} s;
	struct cvmx_npei_state2_s             cn52xx;
	struct cvmx_npei_state2_s             cn52xxp1;
	struct cvmx_npei_state2_s             cn56xx;
	struct cvmx_npei_state2_s             cn56xxp1;
};
typedef union cvmx_npei_state2 cvmx_npei_state2_t;

/**
 * cvmx_npei_state3
 *
 * NPEI_STATE3 = NPEI State 3
 *
 * State machines in NPEI. For debug.
 */
union cvmx_npei_state3 {
	uint64_t u64;
	struct cvmx_npei_state3_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_56_63               : 8;
	uint64_t psm1                         : 15; /**< PSM1 State */
	uint64_t psm0                         : 15; /**< PSM0 State */
	uint64_t nsm1                         : 13; /**< NSM1 State */
	uint64_t nsm0                         : 13; /**< NSM0 State */
#else
	uint64_t nsm0                         : 13;
	uint64_t nsm1                         : 13;
	uint64_t psm0                         : 15;
	uint64_t psm1                         : 15;
	uint64_t reserved_56_63               : 8;
#endif
	} s;
	struct cvmx_npei_state3_s             cn52xx;
	struct cvmx_npei_state3_s             cn52xxp1;
	struct cvmx_npei_state3_s             cn56xx;
	struct cvmx_npei_state3_s             cn56xxp1;
};
typedef union cvmx_npei_state3 cvmx_npei_state3_t;

/**
 * cvmx_npei_win_rd_addr
 *
 * NPEI_WIN_RD_ADDR = NPEI Window Read Address Register
 *
 * The address to be read when the NPEI_WIN_RD_DATA register is read.
 */
union cvmx_npei_win_rd_addr {
	uint64_t u64;
	struct cvmx_npei_win_rd_addr_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_51_63               : 13;
	uint64_t ld_cmd                       : 2;  /**< The load command sent wit hthe read.
                                                         0x0 == Load 8-bytes, 0x1 == Load 4-bytes,
                                                         0x2 == Load 2-bytes, 0x3 == Load 1-bytes, */
	uint64_t iobit                        : 1;  /**< A 1 or 0 can be written here but this will always
                                                         read as '0'. */
	uint64_t rd_addr                      : 48; /**< The address to be read from. Whenever the LSB of
                                                         this register is written, the Read Operation will
                                                         take place.
                                                         [47:40] = NCB_ID
                                                         [39:0]  = Address
                                                         When [47:43] == NPI & [42:0] == 0 bits [39:0] are:
                                                              [39:32] == x, Not Used
                                                              [31:27] == RSL_ID
                                                              [12:0]  == RSL Register Offset */
#else
	uint64_t rd_addr                      : 48;
	uint64_t iobit                        : 1;
	uint64_t ld_cmd                       : 2;
	uint64_t reserved_51_63               : 13;
#endif
	} s;
	struct cvmx_npei_win_rd_addr_s        cn52xx;
	struct cvmx_npei_win_rd_addr_s        cn52xxp1;
	struct cvmx_npei_win_rd_addr_s        cn56xx;
	struct cvmx_npei_win_rd_addr_s        cn56xxp1;
};
typedef union cvmx_npei_win_rd_addr cvmx_npei_win_rd_addr_t;

/**
 * cvmx_npei_win_rd_data
 *
 * NPEI_WIN_RD_DATA = NPEI Window Read Data Register
 *
 * Reading this register causes a window read operation to take place. Address read is taht contained in the NPEI_WIN_RD_ADDR
 * register.
 */
union cvmx_npei_win_rd_data {
	uint64_t u64;
	struct cvmx_npei_win_rd_data_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t rd_data                      : 64; /**< The read data. */
#else
	uint64_t rd_data                      : 64;
#endif
	} s;
	struct cvmx_npei_win_rd_data_s        cn52xx;
	struct cvmx_npei_win_rd_data_s        cn52xxp1;
	struct cvmx_npei_win_rd_data_s        cn56xx;
	struct cvmx_npei_win_rd_data_s        cn56xxp1;
};
typedef union cvmx_npei_win_rd_data cvmx_npei_win_rd_data_t;

/**
 * cvmx_npei_win_wr_addr
 *
 * NPEI_WIN_WR_ADDR = NPEI Window Write Address Register
 *
 * Contains the address to be writen to when a write operation is started by writing the
 * NPEI_WIN_WR_DATA register (see below).
 *
 * Notes:
 * Even though address bit [2] can be set, it should always be kept to '0'.
 *
 */
union cvmx_npei_win_wr_addr {
	uint64_t u64;
	struct cvmx_npei_win_wr_addr_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_49_63               : 15;
	uint64_t iobit                        : 1;  /**< A 1 or 0 can be written here but this will always
                                                         read as '0'. */
	uint64_t wr_addr                      : 46; /**< The address that will be written to when the
                                                         NPEI_WIN_WR_DATA register is written.
                                                         [47:40] = NCB_ID
                                                         [39:3]  = Address
                                                         When [47:43] == NPI & [42:0] == 0 bits [39:0] are:
                                                              [39:32] == x, Not Used
                                                              [31:27] == RSL_ID
                                                              [12:2]  == RSL Register Offset
                                                              [1:0]   == x, Not Used */
	uint64_t reserved_0_1                 : 2;
#else
	uint64_t reserved_0_1                 : 2;
	uint64_t wr_addr                      : 46;
	uint64_t iobit                        : 1;
	uint64_t reserved_49_63               : 15;
#endif
	} s;
	struct cvmx_npei_win_wr_addr_s        cn52xx;
	struct cvmx_npei_win_wr_addr_s        cn52xxp1;
	struct cvmx_npei_win_wr_addr_s        cn56xx;
	struct cvmx_npei_win_wr_addr_s        cn56xxp1;
};
typedef union cvmx_npei_win_wr_addr cvmx_npei_win_wr_addr_t;

/**
 * cvmx_npei_win_wr_data
 *
 * NPEI_WIN_WR_DATA = NPEI Window Write Data Register
 *
 * Contains the data to write to the address located in the NPEI_WIN_WR_ADDR Register.
 * Writing the least-significant-byte of this register will cause a write operation to take place.
 */
union cvmx_npei_win_wr_data {
	uint64_t u64;
	struct cvmx_npei_win_wr_data_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t wr_data                      : 64; /**< The data to be written. Whenever the LSB of this
                                                         register is written, the Window Write will take
                                                         place. */
#else
	uint64_t wr_data                      : 64;
#endif
	} s;
	struct cvmx_npei_win_wr_data_s        cn52xx;
	struct cvmx_npei_win_wr_data_s        cn52xxp1;
	struct cvmx_npei_win_wr_data_s        cn56xx;
	struct cvmx_npei_win_wr_data_s        cn56xxp1;
};
typedef union cvmx_npei_win_wr_data cvmx_npei_win_wr_data_t;

/**
 * cvmx_npei_win_wr_mask
 *
 * NPEI_WIN_WR_MASK = NPEI Window Write Mask Register
 *
 * Contains the mask for the data in the NPEI_WIN_WR_DATA Register.
 */
union cvmx_npei_win_wr_mask {
	uint64_t u64;
	struct cvmx_npei_win_wr_mask_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_8_63                : 56;
	uint64_t wr_mask                      : 8;  /**< The data to be written. When a bit is '0'
                                                         the corresponding byte will be written. */
#else
	uint64_t wr_mask                      : 8;
	uint64_t reserved_8_63                : 56;
#endif
	} s;
	struct cvmx_npei_win_wr_mask_s        cn52xx;
	struct cvmx_npei_win_wr_mask_s        cn52xxp1;
	struct cvmx_npei_win_wr_mask_s        cn56xx;
	struct cvmx_npei_win_wr_mask_s        cn56xxp1;
};
typedef union cvmx_npei_win_wr_mask cvmx_npei_win_wr_mask_t;

/**
 * cvmx_npei_window_ctl
 *
 * NPEI_WINDOW_CTL = NPEI's Window Control
 *
 * The name of this register is misleading. The timeout value is used for BAR0 access from PCIE0 and PCIE1.
 * Any access to the regigisters on the RML will timeout as 0xFFFF clock cycle. At time of timeout the next
 * RML access will start, and interrupt will be set, and in the case of reads no data will be returned.
 *
 * The value of this register should be set to a minimum of 0x200000 to ensure that a timeout to an RML register
 * occurs on the RML 0xFFFF timer before the timeout for a BAR0 access from the PCIE#.
 */
union cvmx_npei_window_ctl {
	uint64_t u64;
	struct cvmx_npei_window_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t time                         : 32; /**< Time to wait in core clocks to wait for a
                                                         BAR0 access to completeon the NCB
                                                         before timing out. A value of 0 will cause no
                                                         timeouts. A minimum value of 0x200000 should be
                                                         used when this register is not set to 0x0. */
#else
	uint64_t time                         : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_npei_window_ctl_s         cn52xx;
	struct cvmx_npei_window_ctl_s         cn52xxp1;
	struct cvmx_npei_window_ctl_s         cn56xx;
	struct cvmx_npei_window_ctl_s         cn56xxp1;
};
typedef union cvmx_npei_window_ctl cvmx_npei_window_ctl_t;

#endif
