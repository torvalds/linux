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
 * cvmx-dfm-defs.h
 *
 * Configuration and status register (CSR) type definitions for
 * Octeon dfm.
 *
 * This file is auto generated. Do not edit.
 *
 * <hr>$Revision$<hr>
 *
 */
#ifndef __CVMX_DFM_DEFS_H__
#define __CVMX_DFM_DEFS_H__

#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFM_CHAR_CTL CVMX_DFM_CHAR_CTL_FUNC()
static inline uint64_t CVMX_DFM_CHAR_CTL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX)))
		cvmx_warn("CVMX_DFM_CHAR_CTL not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800D4000220ull);
}
#else
#define CVMX_DFM_CHAR_CTL (CVMX_ADD_IO_SEG(0x00011800D4000220ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFM_CHAR_MASK0 CVMX_DFM_CHAR_MASK0_FUNC()
static inline uint64_t CVMX_DFM_CHAR_MASK0_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX)))
		cvmx_warn("CVMX_DFM_CHAR_MASK0 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800D4000228ull);
}
#else
#define CVMX_DFM_CHAR_MASK0 (CVMX_ADD_IO_SEG(0x00011800D4000228ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFM_CHAR_MASK2 CVMX_DFM_CHAR_MASK2_FUNC()
static inline uint64_t CVMX_DFM_CHAR_MASK2_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX)))
		cvmx_warn("CVMX_DFM_CHAR_MASK2 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800D4000238ull);
}
#else
#define CVMX_DFM_CHAR_MASK2 (CVMX_ADD_IO_SEG(0x00011800D4000238ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFM_CHAR_MASK4 CVMX_DFM_CHAR_MASK4_FUNC()
static inline uint64_t CVMX_DFM_CHAR_MASK4_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX)))
		cvmx_warn("CVMX_DFM_CHAR_MASK4 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800D4000318ull);
}
#else
#define CVMX_DFM_CHAR_MASK4 (CVMX_ADD_IO_SEG(0x00011800D4000318ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFM_COMP_CTL2 CVMX_DFM_COMP_CTL2_FUNC()
static inline uint64_t CVMX_DFM_COMP_CTL2_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX)))
		cvmx_warn("CVMX_DFM_COMP_CTL2 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800D40001B8ull);
}
#else
#define CVMX_DFM_COMP_CTL2 (CVMX_ADD_IO_SEG(0x00011800D40001B8ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFM_CONFIG CVMX_DFM_CONFIG_FUNC()
static inline uint64_t CVMX_DFM_CONFIG_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX)))
		cvmx_warn("CVMX_DFM_CONFIG not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800D4000188ull);
}
#else
#define CVMX_DFM_CONFIG (CVMX_ADD_IO_SEG(0x00011800D4000188ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFM_CONTROL CVMX_DFM_CONTROL_FUNC()
static inline uint64_t CVMX_DFM_CONTROL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX)))
		cvmx_warn("CVMX_DFM_CONTROL not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800D4000190ull);
}
#else
#define CVMX_DFM_CONTROL (CVMX_ADD_IO_SEG(0x00011800D4000190ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFM_DLL_CTL2 CVMX_DFM_DLL_CTL2_FUNC()
static inline uint64_t CVMX_DFM_DLL_CTL2_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX)))
		cvmx_warn("CVMX_DFM_DLL_CTL2 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800D40001C8ull);
}
#else
#define CVMX_DFM_DLL_CTL2 (CVMX_ADD_IO_SEG(0x00011800D40001C8ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFM_DLL_CTL3 CVMX_DFM_DLL_CTL3_FUNC()
static inline uint64_t CVMX_DFM_DLL_CTL3_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX)))
		cvmx_warn("CVMX_DFM_DLL_CTL3 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800D4000218ull);
}
#else
#define CVMX_DFM_DLL_CTL3 (CVMX_ADD_IO_SEG(0x00011800D4000218ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFM_FCLK_CNT CVMX_DFM_FCLK_CNT_FUNC()
static inline uint64_t CVMX_DFM_FCLK_CNT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX)))
		cvmx_warn("CVMX_DFM_FCLK_CNT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800D40001E0ull);
}
#else
#define CVMX_DFM_FCLK_CNT (CVMX_ADD_IO_SEG(0x00011800D40001E0ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFM_FNT_BIST CVMX_DFM_FNT_BIST_FUNC()
static inline uint64_t CVMX_DFM_FNT_BIST_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX)))
		cvmx_warn("CVMX_DFM_FNT_BIST not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800D40007F8ull);
}
#else
#define CVMX_DFM_FNT_BIST (CVMX_ADD_IO_SEG(0x00011800D40007F8ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFM_FNT_CTL CVMX_DFM_FNT_CTL_FUNC()
static inline uint64_t CVMX_DFM_FNT_CTL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX)))
		cvmx_warn("CVMX_DFM_FNT_CTL not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800D4000400ull);
}
#else
#define CVMX_DFM_FNT_CTL (CVMX_ADD_IO_SEG(0x00011800D4000400ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFM_FNT_IENA CVMX_DFM_FNT_IENA_FUNC()
static inline uint64_t CVMX_DFM_FNT_IENA_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX)))
		cvmx_warn("CVMX_DFM_FNT_IENA not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800D4000410ull);
}
#else
#define CVMX_DFM_FNT_IENA (CVMX_ADD_IO_SEG(0x00011800D4000410ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFM_FNT_SCLK CVMX_DFM_FNT_SCLK_FUNC()
static inline uint64_t CVMX_DFM_FNT_SCLK_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX)))
		cvmx_warn("CVMX_DFM_FNT_SCLK not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800D4000418ull);
}
#else
#define CVMX_DFM_FNT_SCLK (CVMX_ADD_IO_SEG(0x00011800D4000418ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFM_FNT_STAT CVMX_DFM_FNT_STAT_FUNC()
static inline uint64_t CVMX_DFM_FNT_STAT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX)))
		cvmx_warn("CVMX_DFM_FNT_STAT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800D4000408ull);
}
#else
#define CVMX_DFM_FNT_STAT (CVMX_ADD_IO_SEG(0x00011800D4000408ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFM_IFB_CNT CVMX_DFM_IFB_CNT_FUNC()
static inline uint64_t CVMX_DFM_IFB_CNT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX)))
		cvmx_warn("CVMX_DFM_IFB_CNT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800D40001D0ull);
}
#else
#define CVMX_DFM_IFB_CNT (CVMX_ADD_IO_SEG(0x00011800D40001D0ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFM_MODEREG_PARAMS0 CVMX_DFM_MODEREG_PARAMS0_FUNC()
static inline uint64_t CVMX_DFM_MODEREG_PARAMS0_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX)))
		cvmx_warn("CVMX_DFM_MODEREG_PARAMS0 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800D40001A8ull);
}
#else
#define CVMX_DFM_MODEREG_PARAMS0 (CVMX_ADD_IO_SEG(0x00011800D40001A8ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFM_MODEREG_PARAMS1 CVMX_DFM_MODEREG_PARAMS1_FUNC()
static inline uint64_t CVMX_DFM_MODEREG_PARAMS1_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX)))
		cvmx_warn("CVMX_DFM_MODEREG_PARAMS1 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800D4000260ull);
}
#else
#define CVMX_DFM_MODEREG_PARAMS1 (CVMX_ADD_IO_SEG(0x00011800D4000260ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFM_OPS_CNT CVMX_DFM_OPS_CNT_FUNC()
static inline uint64_t CVMX_DFM_OPS_CNT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX)))
		cvmx_warn("CVMX_DFM_OPS_CNT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800D40001D8ull);
}
#else
#define CVMX_DFM_OPS_CNT (CVMX_ADD_IO_SEG(0x00011800D40001D8ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFM_PHY_CTL CVMX_DFM_PHY_CTL_FUNC()
static inline uint64_t CVMX_DFM_PHY_CTL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX)))
		cvmx_warn("CVMX_DFM_PHY_CTL not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800D4000210ull);
}
#else
#define CVMX_DFM_PHY_CTL (CVMX_ADD_IO_SEG(0x00011800D4000210ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFM_RESET_CTL CVMX_DFM_RESET_CTL_FUNC()
static inline uint64_t CVMX_DFM_RESET_CTL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX)))
		cvmx_warn("CVMX_DFM_RESET_CTL not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800D4000180ull);
}
#else
#define CVMX_DFM_RESET_CTL (CVMX_ADD_IO_SEG(0x00011800D4000180ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFM_RLEVEL_CTL CVMX_DFM_RLEVEL_CTL_FUNC()
static inline uint64_t CVMX_DFM_RLEVEL_CTL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX)))
		cvmx_warn("CVMX_DFM_RLEVEL_CTL not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800D40002A0ull);
}
#else
#define CVMX_DFM_RLEVEL_CTL (CVMX_ADD_IO_SEG(0x00011800D40002A0ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFM_RLEVEL_DBG CVMX_DFM_RLEVEL_DBG_FUNC()
static inline uint64_t CVMX_DFM_RLEVEL_DBG_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX)))
		cvmx_warn("CVMX_DFM_RLEVEL_DBG not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800D40002A8ull);
}
#else
#define CVMX_DFM_RLEVEL_DBG (CVMX_ADD_IO_SEG(0x00011800D40002A8ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_DFM_RLEVEL_RANKX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_DFM_RLEVEL_RANKX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800D4000280ull) + ((offset) & 1) * 8;
}
#else
#define CVMX_DFM_RLEVEL_RANKX(offset) (CVMX_ADD_IO_SEG(0x00011800D4000280ull) + ((offset) & 1) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFM_RODT_MASK CVMX_DFM_RODT_MASK_FUNC()
static inline uint64_t CVMX_DFM_RODT_MASK_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX)))
		cvmx_warn("CVMX_DFM_RODT_MASK not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800D4000268ull);
}
#else
#define CVMX_DFM_RODT_MASK (CVMX_ADD_IO_SEG(0x00011800D4000268ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFM_SLOT_CTL0 CVMX_DFM_SLOT_CTL0_FUNC()
static inline uint64_t CVMX_DFM_SLOT_CTL0_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX)))
		cvmx_warn("CVMX_DFM_SLOT_CTL0 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800D40001F8ull);
}
#else
#define CVMX_DFM_SLOT_CTL0 (CVMX_ADD_IO_SEG(0x00011800D40001F8ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFM_SLOT_CTL1 CVMX_DFM_SLOT_CTL1_FUNC()
static inline uint64_t CVMX_DFM_SLOT_CTL1_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX)))
		cvmx_warn("CVMX_DFM_SLOT_CTL1 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800D4000200ull);
}
#else
#define CVMX_DFM_SLOT_CTL1 (CVMX_ADD_IO_SEG(0x00011800D4000200ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFM_TIMING_PARAMS0 CVMX_DFM_TIMING_PARAMS0_FUNC()
static inline uint64_t CVMX_DFM_TIMING_PARAMS0_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX)))
		cvmx_warn("CVMX_DFM_TIMING_PARAMS0 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800D4000198ull);
}
#else
#define CVMX_DFM_TIMING_PARAMS0 (CVMX_ADD_IO_SEG(0x00011800D4000198ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFM_TIMING_PARAMS1 CVMX_DFM_TIMING_PARAMS1_FUNC()
static inline uint64_t CVMX_DFM_TIMING_PARAMS1_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX)))
		cvmx_warn("CVMX_DFM_TIMING_PARAMS1 not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800D40001A0ull);
}
#else
#define CVMX_DFM_TIMING_PARAMS1 (CVMX_ADD_IO_SEG(0x00011800D40001A0ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFM_WLEVEL_CTL CVMX_DFM_WLEVEL_CTL_FUNC()
static inline uint64_t CVMX_DFM_WLEVEL_CTL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX)))
		cvmx_warn("CVMX_DFM_WLEVEL_CTL not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800D4000300ull);
}
#else
#define CVMX_DFM_WLEVEL_CTL (CVMX_ADD_IO_SEG(0x00011800D4000300ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFM_WLEVEL_DBG CVMX_DFM_WLEVEL_DBG_FUNC()
static inline uint64_t CVMX_DFM_WLEVEL_DBG_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX)))
		cvmx_warn("CVMX_DFM_WLEVEL_DBG not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800D4000308ull);
}
#else
#define CVMX_DFM_WLEVEL_DBG (CVMX_ADD_IO_SEG(0x00011800D4000308ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_DFM_WLEVEL_RANKX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_DFM_WLEVEL_RANKX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800D40002B0ull) + ((offset) & 1) * 8;
}
#else
#define CVMX_DFM_WLEVEL_RANKX(offset) (CVMX_ADD_IO_SEG(0x00011800D40002B0ull) + ((offset) & 1) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DFM_WODT_MASK CVMX_DFM_WODT_MASK_FUNC()
static inline uint64_t CVMX_DFM_WODT_MASK_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX)))
		cvmx_warn("CVMX_DFM_WODT_MASK not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800D40001B0ull);
}
#else
#define CVMX_DFM_WODT_MASK (CVMX_ADD_IO_SEG(0x00011800D40001B0ull))
#endif

/**
 * cvmx_dfm_char_ctl
 *
 * DFM_CHAR_CTL = DFM Characterization Control
 * This register is an assortment of various control fields needed to charecterize the DDR3 interface
 *
 * Notes:
 * DR bit applies on the DQ port
 *
 */
union cvmx_dfm_char_ctl {
	uint64_t u64;
	struct cvmx_dfm_char_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_44_63               : 20;
	uint64_t dr                           : 1;  /**< Pattern at Data Rate (not Clock Rate) */
	uint64_t skew_on                      : 1;  /**< Skew adjacent bits */
	uint64_t en                           : 1;  /**< Enable characterization */
	uint64_t sel                          : 1;  /**< Pattern select
                                                         0 = PRBS
                                                         1 = Programmable pattern */
	uint64_t prog                         : 8;  /**< Programmable pattern */
	uint64_t prbs                         : 32; /**< PRBS Polynomial */
#else
	uint64_t prbs                         : 32;
	uint64_t prog                         : 8;
	uint64_t sel                          : 1;
	uint64_t en                           : 1;
	uint64_t skew_on                      : 1;
	uint64_t dr                           : 1;
	uint64_t reserved_44_63               : 20;
#endif
	} s;
	struct cvmx_dfm_char_ctl_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_42_63               : 22;
	uint64_t en                           : 1;  /**< Enable characterization */
	uint64_t sel                          : 1;  /**< Pattern select
                                                         0 = PRBS
                                                         1 = Programmable pattern */
	uint64_t prog                         : 8;  /**< Programmable pattern */
	uint64_t prbs                         : 32; /**< PRBS Polynomial */
#else
	uint64_t prbs                         : 32;
	uint64_t prog                         : 8;
	uint64_t sel                          : 1;
	uint64_t en                           : 1;
	uint64_t reserved_42_63               : 22;
#endif
	} cn63xx;
	struct cvmx_dfm_char_ctl_cn63xx       cn63xxp1;
	struct cvmx_dfm_char_ctl_s            cn66xx;
};
typedef union cvmx_dfm_char_ctl cvmx_dfm_char_ctl_t;

/**
 * cvmx_dfm_char_mask0
 *
 * DFM_CHAR_MASK0 = DFM Characterization Control Mask0
 * This register is an assortment of various control fields needed to charecterize the DDR3 interface
 */
union cvmx_dfm_char_mask0 {
	uint64_t u64;
	struct cvmx_dfm_char_mask0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t mask                         : 16; /**< Mask for DQ0[15:0] */
#else
	uint64_t mask                         : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_dfm_char_mask0_s          cn63xx;
	struct cvmx_dfm_char_mask0_s          cn63xxp1;
	struct cvmx_dfm_char_mask0_s          cn66xx;
};
typedef union cvmx_dfm_char_mask0 cvmx_dfm_char_mask0_t;

/**
 * cvmx_dfm_char_mask2
 *
 * DFM_CHAR_MASK2 = DFM Characterization Control Mask2
 * This register is an assortment of various control fields needed to charecterize the DDR3 interface
 */
union cvmx_dfm_char_mask2 {
	uint64_t u64;
	struct cvmx_dfm_char_mask2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t mask                         : 16; /**< Mask for DQ1[15:0] */
#else
	uint64_t mask                         : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_dfm_char_mask2_s          cn63xx;
	struct cvmx_dfm_char_mask2_s          cn63xxp1;
	struct cvmx_dfm_char_mask2_s          cn66xx;
};
typedef union cvmx_dfm_char_mask2 cvmx_dfm_char_mask2_t;

/**
 * cvmx_dfm_char_mask4
 *
 * DFM_CHAR_MASK4 = DFM Characterization Mask4
 * This register is an assortment of various control fields needed to charecterize the DDR3 interface
 */
union cvmx_dfm_char_mask4 {
	uint64_t u64;
	struct cvmx_dfm_char_mask4_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_33_63               : 31;
	uint64_t reset_n_mask                 : 1;  /**< Mask for RESET_N */
	uint64_t a_mask                       : 16; /**< Mask for A[15:0] */
	uint64_t ba_mask                      : 3;  /**< Mask for BA[2:0] */
	uint64_t we_n_mask                    : 1;  /**< Mask for WE_N */
	uint64_t cas_n_mask                   : 1;  /**< Mask for CAS_N */
	uint64_t ras_n_mask                   : 1;  /**< Mask for RAS_N */
	uint64_t odt1_mask                    : 2;  /**< Mask for ODT1
                                                         For DFM, ODT1 is reserved. */
	uint64_t odt0_mask                    : 2;  /**< Mask for ODT0 */
	uint64_t cs1_n_mask                   : 2;  /**< Mask for CS1_N
                                                         For DFM, CS1_N is reserved. */
	uint64_t cs0_n_mask                   : 2;  /**< Mask for CS0_N */
	uint64_t cke_mask                     : 2;  /**< Mask for CKE
                                                         For DFM, CKE_MASK[1] is reserved. */
#else
	uint64_t cke_mask                     : 2;
	uint64_t cs0_n_mask                   : 2;
	uint64_t cs1_n_mask                   : 2;
	uint64_t odt0_mask                    : 2;
	uint64_t odt1_mask                    : 2;
	uint64_t ras_n_mask                   : 1;
	uint64_t cas_n_mask                   : 1;
	uint64_t we_n_mask                    : 1;
	uint64_t ba_mask                      : 3;
	uint64_t a_mask                       : 16;
	uint64_t reset_n_mask                 : 1;
	uint64_t reserved_33_63               : 31;
#endif
	} s;
	struct cvmx_dfm_char_mask4_s          cn63xx;
	struct cvmx_dfm_char_mask4_s          cn66xx;
};
typedef union cvmx_dfm_char_mask4 cvmx_dfm_char_mask4_t;

/**
 * cvmx_dfm_comp_ctl2
 *
 * DFM_COMP_CTL2 = DFM Compensation control2
 *
 */
union cvmx_dfm_comp_ctl2 {
	uint64_t u64;
	struct cvmx_dfm_comp_ctl2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_34_63               : 30;
	uint64_t ddr__ptune                   : 4;  /**< DDR pctl from compensation circuit
                                                         The encoded value provides debug information for the
                                                         compensation impedance on P-pullup */
	uint64_t ddr__ntune                   : 4;  /**< DDR nctl from compensation circuit
                                                         The encoded value provides debug information for the
                                                         compensation impedance on N-pulldown */
	uint64_t m180                         : 1;  /**< Cap impedance at 180 ohm (instead of 240 ohm) */
	uint64_t byp                          : 1;  /**< Bypass mode
                                                         Use compensation setting from PTUNE,NTUNE */
	uint64_t ptune                        : 4;  /**< PCTL impedance control in bypass mode */
	uint64_t ntune                        : 4;  /**< NCTL impedance control in bypass mode */
	uint64_t rodt_ctl                     : 4;  /**< NCTL RODT impedance control bits
                                                         0000 = No ODT
                                                         0001 = 20 ohm
                                                         0010 = 30 ohm
                                                         0011 = 40 ohm
                                                         0100 = 60 ohm
                                                         0101 = 120 ohm
                                                         0110-1111 = Reserved */
	uint64_t cmd_ctl                      : 4;  /**< Drive strength control for CMD/A/RESET_N/CKE drivers
                                                         0001 = 24 ohm
                                                         0010 = 26.67 ohm
                                                         0011 = 30 ohm
                                                         0100 = 34.3 ohm
                                                         0101 = 40 ohm
                                                         0110 = 48 ohm
                                                         0111 = 60 ohm
                                                         0000,1000-1111 = Reserved */
	uint64_t ck_ctl                       : 4;  /**< Drive strength control for CK/CS_N/ODT drivers
                                                         0001 = 24 ohm
                                                         0010 = 26.67 ohm
                                                         0011 = 30 ohm
                                                         0100 = 34.3 ohm
                                                         0101 = 40 ohm
                                                         0110 = 48 ohm
                                                         0111 = 60 ohm
                                                         0000,1000-1111 = Reserved */
	uint64_t dqx_ctl                      : 4;  /**< Drive strength control for DQ/DQS drivers
                                                         0001 = 24 ohm
                                                         0010 = 26.67 ohm
                                                         0011 = 30 ohm
                                                         0100 = 34.3 ohm
                                                         0101 = 40 ohm
                                                         0110 = 48 ohm
                                                         0111 = 60 ohm
                                                         0000,1000-1111 = Reserved */
#else
	uint64_t dqx_ctl                      : 4;
	uint64_t ck_ctl                       : 4;
	uint64_t cmd_ctl                      : 4;
	uint64_t rodt_ctl                     : 4;
	uint64_t ntune                        : 4;
	uint64_t ptune                        : 4;
	uint64_t byp                          : 1;
	uint64_t m180                         : 1;
	uint64_t ddr__ntune                   : 4;
	uint64_t ddr__ptune                   : 4;
	uint64_t reserved_34_63               : 30;
#endif
	} s;
	struct cvmx_dfm_comp_ctl2_s           cn63xx;
	struct cvmx_dfm_comp_ctl2_s           cn63xxp1;
	struct cvmx_dfm_comp_ctl2_s           cn66xx;
};
typedef union cvmx_dfm_comp_ctl2 cvmx_dfm_comp_ctl2_t;

/**
 * cvmx_dfm_config
 *
 * DFM_CONFIG = DFM Memory Configuration Register
 *
 * This register controls certain parameters of  Memory Configuration
 *
 * Notes:
 * a. The self refresh entry sequence(s) power the DLL up/down (depending on DFM_MODEREG_PARAMS[DLL])
 * when DFM_CONFIG[SREF_WITH_DLL] is set
 * b. Prior to the self-refresh exit sequence, DFM_MODEREG_PARAMS should be re-programmed (if needed) to the
 * appropriate values
 *
 * DFM Bringup Sequence:
 * 1. SW must ensure there are no pending DRAM transactions and that the DDR PLL and the DLL have been initialized.
 * 2. Write DFM_COMP_CTL2, DFM_CONTROL, DFM_WODT_MASK, DFM_RODT_MASK, DFM_DUAL_MEMCFG, DFM_TIMING_PARAMS0, DFM_TIMING_PARAMS1,
 *    DFM_MODEREG_PARAMS0, DFM_MODEREG_PARAMS1, DFM_RESET_CTL (with DDR3RST=0), DFM_CONFIG (with INIT_START=0)
 *    with appropriate values, if necessary.
 * 3. Wait 200us, then write DFM_RESET_CTL[DDR3RST] = 1.
 * 4. Initialize all ranks at once by writing DFM_CONFIG[RANKMASK][n] = 1, DFM_CONFIG[INIT_STATUS][n] = 1, and DFM_CONFIG[INIT_START] = 1
 *    where n is a valid rank index for the specific board configuration.
 * 5. for each rank n to be write-leveled [
 *       if auto write-leveling is desired [
 *           write DFM_CONFIG[RANKMASK][n] = 1, DFM_WLEVEL_CTL appropriately and DFM_CONFIG[INIT_START] = 1
 *           wait until DFM_WLEVEL_RANKn[STATUS] = 3
 *       ] else [
 *           write DFM_WLEVEL_RANKn with appropriate values
 *       ]
 *    ]
 * 6. for each rank n to be read-leveled [
 *       if auto read-leveling is desired [
 *           write DFM_CONFIG[RANKMASK][n] = 1, DFM_RLEVEL_CTL appropriately and DFM_CONFIG[INIT_START] = 1
 *           wait until DFM_RLEVEL_RANKn[STATUS] = 3
 *       ] else [
 *           write DFM_RLEVEL_RANKn with appropriate values
 *       ]
 *    ]
 */
union cvmx_dfm_config {
	uint64_t u64;
	struct cvmx_dfm_config_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_59_63               : 5;
	uint64_t early_unload_d1_r1           : 1;  /**< Reserved */
	uint64_t early_unload_d1_r0           : 1;  /**< Reserved */
	uint64_t early_unload_d0_r1           : 1;  /**< When set, unload the PHY silo one cycle early for Rank 1
                                                         reads.
                                                         The recommended EARLY_UNLOAD_D0_R1 value can be calculated
                                                         after the final DFM_RLEVEL_RANK1[BYTE*] values are
                                                         selected (as part of read-leveling initialization).
                                                         Then, determine the largest read-leveling setting
                                                         for rank 1 (i.e. calculate maxset=MAX(DFM_RLEVEL_RANK1[BYTEi])
                                                         across all i), then set EARLY_UNLOAD_D0_R1
                                                         when the low two bits of this largest setting is not
                                                         3 (i.e. EARLY_UNLOAD_D0_R1 = (maxset<1:0>!=3)). */
	uint64_t early_unload_d0_r0           : 1;  /**< When set, unload the PHY silo one cycle early for Rank 0
                                                         reads.
                                                         The recommended EARLY_UNLOAD_D0_R0 value can be calculated
                                                         after the final DFM_RLEVEL_RANK0[BYTE*] values are
                                                         selected (as part of read-leveling initialization).
                                                         Then, determine the largest read-leveling setting
                                                         for rank 0 (i.e. calculate maxset=MAX(DFM_RLEVEL_RANK0[BYTEi])
                                                         across all i), then set EARLY_UNLOAD_D0_R0
                                                         when the low two bits of this largest setting is not
                                                         3 (i.e. EARLY_UNLOAD_D0_R0 = (maxset<1:0>!=3)). */
	uint64_t init_status                  : 4;  /**< Indicates status of initialization
                                                         INIT_STATUS[n] = 1 implies rank n has been initialized
                                                         SW must set necessary INIT_STATUS bits with the
                                                         same DFM_CONFIG write that initiates
                                                         power-up/init and self-refresh exit sequences
                                                         (if the required INIT_STATUS bits are not already
                                                         set before DFM initiates the sequence).
                                                         INIT_STATUS determines the chip-selects that assert
                                                         during refresh, ZQCS, and precharge power-down and
                                                         self-refresh entry/exit SEQUENCE's.
                                                         INIT_STATUS<3:2> must be zero. */
	uint64_t mirrmask                     : 4;  /**< Mask determining which ranks are address-mirrored.
                                                         MIRRMASK<n> = 1 means Rank n addresses are mirrored
                                                         for 0 <= n <= 1
                                                         A mirrored read/write has these differences:
                                                          - DDR_BA<1> is swapped with DDR_BA<0>
                                                          - DDR_A<8> is swapped with DDR_A<7>
                                                          - DDR_A<6> is swapped with DDR_A<5>
                                                          - DDR_A<4> is swapped with DDR_A<3>
                                                         MIRRMASK<3:2> must be zero.
                                                         When RANK_ENA=0, MIRRMASK<1> MBZ */
	uint64_t rankmask                     : 4;  /**< Mask to select rank to be leveled/initialized.
                                                         To write-level/read-level/initialize rank i, set RANKMASK<i>
                                                                         RANK_ENA=1               RANK_ENA=0
                                                           RANKMASK<0> =    CS0                  CS0 and CS1
                                                           RANKMASK<1> =    CS1                      MBZ
                                                         For read/write leveling, each rank has to be leveled separately,
                                                         so RANKMASK should only have one bit set.
                                                         RANKMASK is not used during self-refresh entry/exit and
                                                         precharge power-down entry/exit instruction sequences.
                                                         RANKMASK<3:2> must be zero.
                                                         When RANK_ENA=0, RANKMASK<1> MBZ */
	uint64_t rank_ena                     : 1;  /**< RANK enable (for use with multiple ranks)
                                                         The RANK_ENA bit enables
                                                         the drive of the CS_N[1:0] and ODT_<1:0> pins differently based on the
                                                         (PBANK_LSB-1) address bit. */
	uint64_t sref_with_dll                : 1;  /**< Self-refresh entry/exit write MR1 and MR2
                                                         When set, self-refresh entry and exit instruction sequences
                                                         write MR1 and MR2 (in all ranks). (The writes occur before
                                                         self-refresh entry, and after self-refresh exit.)
                                                         When clear, self-refresh entry and exit instruction sequences
                                                         do not write any registers in the DDR3 parts. */
	uint64_t early_dqx                    : 1;  /**< Send DQx signals one CK cycle earlier for the case when
                                                         the shortest DQx lines have a larger delay than the CK line */
	uint64_t sequence                     : 3;  /**< Instruction sequence that is run after a 0->1
                                                         transition on DFM_CONFIG[INIT_START]. Self-refresh entry and
                                                         precharge power-down entry and exit SEQUENCE's can also
                                                         be initiated automatically by hardware.
                                                         0=power-up/init                  (RANKMASK used, MR0, MR1, MR2, and MR3 written)
                                                         1=read-leveling                  (RANKMASK used, MR3 written)
                                                         2=self-refresh entry             (all ranks participate, MR1 and MR2 written if SREF_WITH_DLL=1)
                                                         3=self-refresh exit,             (all ranks participate, MR1 and MR2 written if SREF_WITH_DLL=1)
                                                         4=precharge power-down entry     (all ranks participate)
                                                         5=precharge power-down exit      (all ranks participate)
                                                         6=write-leveling                 (RANKMASK used, MR1 written)
                                                         7=illegal
                                                         Precharge power-down entry and exit SEQUENCE's may
                                                         be automatically generated by the HW when IDLEPOWER!=0.
                                                         Self-refresh entry SEQUENCE's may be automatically
                                                         generated by hardware upon a chip warm or soft reset
                                                         sequence when DFM_RESET_CTL[DDR3PWARM,DDR3PSOFT] are set.
                                                         DFM writes the DFM_MODEREG_PARAMS0 and DFM_MODEREG_PARAMS1 CSR field values
                                                         to the Mode registers in the DRAM parts (MR0, MR1, MR2, and MR3) as part of some of these sequences.
                                                         Refer to the DFM_MODEREG_PARAMS0 and DFM_MODEREG_PARAMS1 descriptions for more details.
                                                         The DFR_CKE pin gets activated as part of power-up/init,
                                                         self-refresh exit, and precharge power-down exit sequences.
                                                         The DFR_CKE pin gets de-activated as part of self-refresh entry,
                                                         precharge power-down entry, or DRESET assertion.
                                                         If there are two consecutive power-up/init's without
                                                         a DRESET assertion between them, DFM asserts DFR_CKE as part of
                                                         the first power-up/init, and continues to assert DFR_CKE
                                                         through the remainder of the first and the second power-up/init.
                                                         If DFR_CKE deactivation and reactivation is needed for
                                                         a second power-up/init, a DRESET assertion is required
                                                         between the first and the second. */
	uint64_t ref_zqcs_int                 : 19; /**< Refresh & ZQCS interval represented in \#of 512 fclk
                                                         increments. A Refresh sequence is triggered when bits
                                                         [24:18] are equal to 0, and a ZQCS sequence is triggered
                                                         when [36:18] are equal to 0.
                                                         Program [24:18] to RND-DN(tREFI/clkPeriod/512)
                                                         Program [36:25] to RND-DN(ZQCS_Interval/clkPeriod/(512*64)). Note
                                                         that this value should always be greater than 32, to account for
                                                         resistor calibration delays.
                                                         000_00000000_00000000: RESERVED
                                                         Max Refresh interval = 127 * 512           = 65024 fclks
                                                         Max ZQCS interval    = (8*256*256-1) * 512 = 268434944 fclks ~ 335ms for a 1.25 ns clock
                                                         DFM_CONFIG[INIT_STATUS] determines which ranks receive
                                                         the REF / ZQCS. DFM does not send any refreshes / ZQCS's
                                                         when DFM_CONFIG[INIT_STATUS]=0. */
	uint64_t reset                        : 1;  /**< Reset oneshot pulse for refresh counter,
                                                         and DFM_OPS_CNT, DFM_IFB_CNT, and DFM_FCLK_CNT
                                                         CSR's. SW should write this to a one, then re-write
                                                         it to a zero to cause the reset. */
	uint64_t ecc_adr                      : 1;  /**< Must be zero. */
	uint64_t forcewrite                   : 4;  /**< Force the oldest outstanding write to complete after
                                                         having waited for 2^FORCEWRITE cycles.  0=disabled. */
	uint64_t idlepower                    : 3;  /**< Enter precharge power-down mode after the memory
                                                         controller has been idle for 2^(2+IDLEPOWER) cycles.
                                                         0=disabled.
                                                         This field should only be programmed after initialization.
                                                         DFM_MODEREG_PARAMS0[PPD] determines whether the DRAM DLL
                                                         is disabled during the precharge power-down. */
	uint64_t pbank_lsb                    : 4;  /**< Physical bank address bit select
                                                         Encoding used to determine which memory address
                                                         bit position represents the rank(or bunk) bit used to enable 1(of 2)
                                                         ranks(via chip enables) supported by the DFM DDR3 interface.
                                                         Reverting to the explanation for ROW_LSB, PBANK_LSB would be ROW_LSB bit +
                                                         \#rowbits + \#rankbits.
                                                         PBANK_LSB
                                                             - 0: rank = mem_adr[24]
                                                             - 1: rank = mem_adr[25]
                                                             - 2: rank = mem_adr[26]
                                                             - 3: rank = mem_adr[27]
                                                             - 4: rank = mem_adr[28]
                                                             - 5: rank = mem_adr[29]
                                                             - 6: rank = mem_adr[30]
                                                             - 7: rank = mem_adr[31]
                                                          - 8-15:  RESERVED
                                                         DESIGN NOTE: The DFM DDR3 memory bus is 16b wide, therefore DOES NOT
                                                         support standard 64b/72b DDR3 DIMM modules. The board designer should
                                                         populate the DFM DDR3 interface using either TWO x8bit DDR3 devices
                                                         (or a single x16bit device if available) to fully populate the 16b
                                                         DFM DDR3 data bus.
                                                         The DFM DDR3 memory controller supports either 1(or 2) rank(s) based
                                                         on how much total memory is desired for the DFA application. See
                                                         RANK_ENA CSR bit when enabling for dual-ranks.
                                                         SW NOTE:
                                                             1) When RANK_ENA=0, SW must properly configure the PBANK_LSB to
                                                                reference upper unused memory address bits.
                                                             2) When RANK_ENA=1 (dual ranks), SW must configure PBANK_LSB to
                                                                reference the upper most address bit based on the total size
                                                                of the rank.
                                                         For example, for a DFM DDR3 memory populated using Samsung's k4b1g0846c-f7
                                                         1Gb(256MB) (16M x 8 bit x 8 bank) DDR3 parts, the column address width = 10 and
                                                         the device row address width = 14b.  The single x8bit device contains 128MB, and
                                                         requires TWO such parts to populate the DFM 16b DDR3 interface. This then yields
                                                         a total rank size = 256MB = 2^28.
                                                         For a single-rank configuration (RANK_ENA=0), SW would program PBANK_LSB>=3 to
                                                         select mem_adr[x] bits above the legal DFM address range for mem_adr[27:0]=256MB.
                                                         For a dual-rank configuration (RANK_ENA=1), SW would program PBANK_LSB=4 to select
                                                         rank=mem_adr[28] as the bit used to determine which 256MB rank (of 512MB total) to
                                                         access (via rank chip enables - see: DFM DDR3 CS0[1:0] pins for connection to
                                                         upper and lower rank). */
	uint64_t row_lsb                      : 3;  /**< Row Address bit select
                                                         Encoding used to determine which memory address
                                                         bit position represents the low order DDR ROW address.
                                                         The DFM memory address [31:4] which references octawords
                                                         needs to be translated to DRAM addresses (bnk,row,col,bunk)
                                                         mem_adr[31:4]:
                                                           3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1
                                                           1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4
                                                          +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
                                                          |       ROW[m:n]            |     COL[13:3]       | BA
                                                          +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
                                                         See:
                                                           BA[2:0]:   mem_adr[6:4]
                                                           COL[13:0]: [mem_adr[17:7],3'd0]
                                                               NOTE: The extracted COL address is always 14b fixed size width,
                                                               and upper unused bits are ignored by the DRAM device.
                                                           ROW[15:0]: Extraction of ROW starting address bit is programmable,
                                                           and is dependent on the \#column bits supported by the DRAM device.
                                                           The actual starting bit of the ROW can actually span into the
                                                           high order bits of the COL[13:3] field described above.
                                                                  ROW_LSB    ROW[15:0]
                                                                --------------------------
                                                                   - 0:      mem_adr[26:11]
                                                                   - 1:      mem_adr[27:12]
                                                                   - 2:      mem_adr[28:13]
                                                                   - 3:      mem_adr[29:14]
                                                                   - 4:      mem_adr[30:15]
                                                                   - 5:      mem_adr[31:16]
                                                                  6,7:     [1'b0, mem_adr[31:17]]  For current DDR3 Jedec spec - UNSUPPORTED
                                                         For example, for Samsung's k4b1g0846c-f7 1Gb (16M x 8 bit x 8 bank)
                                                         DDR3 parts, the column address width = 10. Therefore,
                                                              BA[3:0] = mem_adr[6:4] / COL[9:0] = [mem_adr[13:7],3'd0], and
                                                         we would want the row starting address to be extracted from mem_adr[14].
                                                         Therefore, a ROW_LSB=3, will extract the row from mem_adr[29:14]. */
	uint64_t ecc_ena                      : 1;  /**< Must be zero. */
	uint64_t init_start                   : 1;  /**< A 0->1 transition starts the DDR memory sequence that is
                                                         selected by DFM_CONFIG[SEQUENCE].  This register is a
                                                         oneshot and clears itself each time it is set. */
#else
	uint64_t init_start                   : 1;
	uint64_t ecc_ena                      : 1;
	uint64_t row_lsb                      : 3;
	uint64_t pbank_lsb                    : 4;
	uint64_t idlepower                    : 3;
	uint64_t forcewrite                   : 4;
	uint64_t ecc_adr                      : 1;
	uint64_t reset                        : 1;
	uint64_t ref_zqcs_int                 : 19;
	uint64_t sequence                     : 3;
	uint64_t early_dqx                    : 1;
	uint64_t sref_with_dll                : 1;
	uint64_t rank_ena                     : 1;
	uint64_t rankmask                     : 4;
	uint64_t mirrmask                     : 4;
	uint64_t init_status                  : 4;
	uint64_t early_unload_d0_r0           : 1;
	uint64_t early_unload_d0_r1           : 1;
	uint64_t early_unload_d1_r0           : 1;
	uint64_t early_unload_d1_r1           : 1;
	uint64_t reserved_59_63               : 5;
#endif
	} s;
	struct cvmx_dfm_config_s              cn63xx;
	struct cvmx_dfm_config_cn63xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_55_63               : 9;
	uint64_t init_status                  : 4;  /**< Indicates status of initialization
                                                         INIT_STATUS[n] = 1 implies rank n has been initialized
                                                         SW must set necessary INIT_STATUS bits with the
                                                         same DFM_CONFIG write that initiates
                                                         power-up/init and self-refresh exit sequences
                                                         (if the required INIT_STATUS bits are not already
                                                         set before DFM initiates the sequence).
                                                         INIT_STATUS determines the chip-selects that assert
                                                         during refresh, ZQCS, and precharge power-down and
                                                         self-refresh entry/exit SEQUENCE's.
                                                         INIT_STATUS<3:2> must be zero. */
	uint64_t mirrmask                     : 4;  /**< Mask determining which ranks are address-mirrored.
                                                         MIRRMASK<n> = 1 means Rank n addresses are mirrored
                                                         for 0 <= n <= 1
                                                         A mirrored read/write has these differences:
                                                          - DDR_BA<1> is swapped with DDR_BA<0>
                                                          - DDR_A<8> is swapped with DDR_A<7>
                                                          - DDR_A<6> is swapped with DDR_A<5>
                                                          - DDR_A<4> is swapped with DDR_A<3>
                                                         MIRRMASK<3:2> must be zero.
                                                         When RANK_ENA=0, MIRRMASK<1> MBZ */
	uint64_t rankmask                     : 4;  /**< Mask to select rank to be leveled/initialized.
                                                         To write-level/read-level/initialize rank i, set RANKMASK<i>
                                                                         RANK_ENA=1               RANK_ENA=0
                                                           RANKMASK<0> =    CS0                  CS0 and CS1
                                                           RANKMASK<1> =    CS1                      MBZ
                                                         For read/write leveling, each rank has to be leveled separately,
                                                         so RANKMASK should only have one bit set.
                                                         RANKMASK is not used during self-refresh entry/exit and
                                                         precharge power-down entry/exit instruction sequences.
                                                         RANKMASK<3:2> must be zero.
                                                         When RANK_ENA=0, RANKMASK<1> MBZ */
	uint64_t rank_ena                     : 1;  /**< RANK enable (for use with multiple ranks)
                                                         The RANK_ENA bit enables
                                                         the drive of the CS_N[1:0] and ODT_<1:0> pins differently based on the
                                                         (PBANK_LSB-1) address bit. */
	uint64_t sref_with_dll                : 1;  /**< Self-refresh entry/exit write MR1 and MR2
                                                         When set, self-refresh entry and exit instruction sequences
                                                         write MR1 and MR2 (in all ranks). (The writes occur before
                                                         self-refresh entry, and after self-refresh exit.)
                                                         When clear, self-refresh entry and exit instruction sequences
                                                         do not write any registers in the DDR3 parts. */
	uint64_t early_dqx                    : 1;  /**< Send DQx signals one CK cycle earlier for the case when
                                                         the shortest DQx lines have a larger delay than the CK line */
	uint64_t sequence                     : 3;  /**< Instruction sequence that is run after a 0->1
                                                         transition on DFM_CONFIG[INIT_START]. Self-refresh entry and
                                                         precharge power-down entry and exit SEQUENCE's can also
                                                         be initiated automatically by hardware.
                                                         0=power-up/init                  (RANKMASK used, MR0, MR1, MR2, and MR3 written)
                                                         1=read-leveling                  (RANKMASK used, MR3 written)
                                                         2=self-refresh entry             (all ranks participate, MR1 and MR2 written if SREF_WITH_DLL=1)
                                                         3=self-refresh exit,             (all ranks participate, MR1 and MR2 written if SREF_WITH_DLL=1)
                                                         4=precharge power-down entry     (all ranks participate)
                                                         5=precharge power-down exit      (all ranks participate)
                                                         6=write-leveling                 (RANKMASK used, MR1 written)
                                                         7=illegal
                                                         Precharge power-down entry and exit SEQUENCE's may
                                                         be automatically generated by the HW when IDLEPOWER!=0.
                                                         Self-refresh entry SEQUENCE's may be automatically
                                                         generated by hardware upon a chip warm or soft reset
                                                         sequence when DFM_RESET_CTL[DDR3PWARM,DDR3PSOFT] are set.
                                                         DFM writes the DFM_MODEREG_PARAMS0 and DFM_MODEREG_PARAMS1 CSR field values
                                                         to the Mode registers in the DRAM parts (MR0, MR1, MR2, and MR3) as part of some of these sequences.
                                                         Refer to the DFM_MODEREG_PARAMS0 and DFM_MODEREG_PARAMS1 descriptions for more details.
                                                         The DFR_CKE pin gets activated as part of power-up/init,
                                                         self-refresh exit, and precharge power-down exit sequences.
                                                         The DFR_CKE pin gets de-activated as part of self-refresh entry,
                                                         precharge power-down entry, or DRESET assertion.
                                                         If there are two consecutive power-up/init's without
                                                         a DRESET assertion between them, DFM asserts DFR_CKE as part of
                                                         the first power-up/init, and continues to assert DFR_CKE
                                                         through the remainder of the first and the second power-up/init.
                                                         If DFR_CKE deactivation and reactivation is needed for
                                                         a second power-up/init, a DRESET assertion is required
                                                         between the first and the second. */
	uint64_t ref_zqcs_int                 : 19; /**< Refresh & ZQCS interval represented in \#of 512 fclk
                                                         increments. A Refresh sequence is triggered when bits
                                                         [24:18] are equal to 0, and a ZQCS sequence is triggered
                                                         when [36:18] are equal to 0.
                                                         Program [24:18] to RND-DN(tREFI/clkPeriod/512)
                                                         Program [36:25] to RND-DN(ZQCS_Interval/clkPeriod/(512*64)). Note
                                                         that this value should always be greater than 32, to account for
                                                         resistor calibration delays.
                                                         000_00000000_00000000: RESERVED
                                                         Max Refresh interval = 127 * 512           = 65024 fclks
                                                         Max ZQCS interval    = (8*256*256-1) * 512 = 268434944 fclks ~ 335ms for a 1.25 ns clock
                                                         DFM_CONFIG[INIT_STATUS] determines which ranks receive
                                                         the REF / ZQCS. DFM does not send any refreshes / ZQCS's
                                                         when DFM_CONFIG[INIT_STATUS]=0. */
	uint64_t reset                        : 1;  /**< Reset oneshot pulse for refresh counter,
                                                         and DFM_OPS_CNT, DFM_IFB_CNT, and DFM_FCLK_CNT
                                                         CSR's. SW should write this to a one, then re-write
                                                         it to a zero to cause the reset. */
	uint64_t ecc_adr                      : 1;  /**< Must be zero. */
	uint64_t forcewrite                   : 4;  /**< Force the oldest outstanding write to complete after
                                                         having waited for 2^FORCEWRITE cycles.  0=disabled. */
	uint64_t idlepower                    : 3;  /**< Enter precharge power-down mode after the memory
                                                         controller has been idle for 2^(2+IDLEPOWER) cycles.
                                                         0=disabled.
                                                         This field should only be programmed after initialization.
                                                         DFM_MODEREG_PARAMS0[PPD] determines whether the DRAM DLL
                                                         is disabled during the precharge power-down. */
	uint64_t pbank_lsb                    : 4;  /**< Physical bank address bit select
                                                         Encoding used to determine which memory address
                                                         bit position represents the rank(or bunk) bit used to enable 1(of 2)
                                                         ranks(via chip enables) supported by the DFM DDR3 interface.
                                                         Reverting to the explanation for ROW_LSB, PBANK_LSB would be ROW_LSB bit +
                                                         \#rowbits + \#rankbits.
                                                         PBANK_LSB
                                                             - 0: rank = mem_adr[24]
                                                             - 1: rank = mem_adr[25]
                                                             - 2: rank = mem_adr[26]
                                                             - 3: rank = mem_adr[27]
                                                             - 4: rank = mem_adr[28]
                                                             - 5: rank = mem_adr[29]
                                                             - 6: rank = mem_adr[30]
                                                             - 7: rank = mem_adr[31]
                                                          - 8-15:  RESERVED
                                                         DESIGN NOTE: The DFM DDR3 memory bus is 16b wide, therefore DOES NOT
                                                         support standard 64b/72b DDR3 DIMM modules. The board designer should
                                                         populate the DFM DDR3 interface using either TWO x8bit DDR3 devices
                                                         (or a single x16bit device if available) to fully populate the 16b
                                                         DFM DDR3 data bus.
                                                         The DFM DDR3 memory controller supports either 1(or 2) rank(s) based
                                                         on how much total memory is desired for the DFA application. See
                                                         RANK_ENA CSR bit when enabling for dual-ranks.
                                                         SW NOTE:
                                                             1) When RANK_ENA=0, SW must properly configure the PBANK_LSB to
                                                                reference upper unused memory address bits.
                                                             2) When RANK_ENA=1 (dual ranks), SW must configure PBANK_LSB to
                                                                reference the upper most address bit based on the total size
                                                                of the rank.
                                                         For example, for a DFM DDR3 memory populated using Samsung's k4b1g0846c-f7
                                                         1Gb(256MB) (16M x 8 bit x 8 bank) DDR3 parts, the column address width = 10 and
                                                         the device row address width = 14b.  The single x8bit device contains 128MB, and
                                                         requires TWO such parts to populate the DFM 16b DDR3 interface. This then yields
                                                         a total rank size = 256MB = 2^28.
                                                         For a single-rank configuration (RANK_ENA=0), SW would program PBANK_LSB>=3 to
                                                         select mem_adr[x] bits above the legal DFM address range for mem_adr[27:0]=256MB.
                                                         For a dual-rank configuration (RANK_ENA=1), SW would program PBANK_LSB=4 to select
                                                         rank=mem_adr[28] as the bit used to determine which 256MB rank (of 512MB total) to
                                                         access (via rank chip enables - see: DFM DDR3 CS0[1:0] pins for connection to
                                                         upper and lower rank). */
	uint64_t row_lsb                      : 3;  /**< Row Address bit select
                                                         Encoding used to determine which memory address
                                                         bit position represents the low order DDR ROW address.
                                                         The DFM memory address [31:4] which references octawords
                                                         needs to be translated to DRAM addresses (bnk,row,col,bunk)
                                                         mem_adr[31:4]:
                                                           3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1
                                                           1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4
                                                          +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
                                                          |       ROW[m:n]            |     COL[13:3]       | BA
                                                          +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
                                                         See:
                                                           BA[2:0]:   mem_adr[6:4]
                                                           COL[13:0]: [mem_adr[17:7],3'd0]
                                                               NOTE: The extracted COL address is always 14b fixed size width,
                                                               and upper unused bits are ignored by the DRAM device.
                                                           ROW[15:0]: Extraction of ROW starting address bit is programmable,
                                                           and is dependent on the \#column bits supported by the DRAM device.
                                                           The actual starting bit of the ROW can actually span into the
                                                           high order bits of the COL[13:3] field described above.
                                                                  ROW_LSB    ROW[15:0]
                                                                --------------------------
                                                                   - 0:      mem_adr[26:11]
                                                                   - 1:      mem_adr[27:12]
                                                                   - 2:      mem_adr[28:13]
                                                                   - 3:      mem_adr[29:14]
                                                                   - 4:      mem_adr[30:15]
                                                                   - 5:      mem_adr[31:16]
                                                                  6,7:     [1'b0, mem_adr[31:17]]  For current DDR3 Jedec spec - UNSUPPORTED
                                                         For example, for Samsung's k4b1g0846c-f7 1Gb (16M x 8 bit x 8 bank)
                                                         DDR3 parts, the column address width = 10. Therefore,
                                                              BA[3:0] = mem_adr[6:4] / COL[9:0] = [mem_adr[13:7],3'd0], and
                                                         we would want the row starting address to be extracted from mem_adr[14].
                                                         Therefore, a ROW_LSB=3, will extract the row from mem_adr[29:14]. */
	uint64_t ecc_ena                      : 1;  /**< Must be zero. */
	uint64_t init_start                   : 1;  /**< A 0->1 transition starts the DDR memory sequence that is
                                                         selected by DFM_CONFIG[SEQUENCE].  This register is a
                                                         oneshot and clears itself each time it is set. */
#else
	uint64_t init_start                   : 1;
	uint64_t ecc_ena                      : 1;
	uint64_t row_lsb                      : 3;
	uint64_t pbank_lsb                    : 4;
	uint64_t idlepower                    : 3;
	uint64_t forcewrite                   : 4;
	uint64_t ecc_adr                      : 1;
	uint64_t reset                        : 1;
	uint64_t ref_zqcs_int                 : 19;
	uint64_t sequence                     : 3;
	uint64_t early_dqx                    : 1;
	uint64_t sref_with_dll                : 1;
	uint64_t rank_ena                     : 1;
	uint64_t rankmask                     : 4;
	uint64_t mirrmask                     : 4;
	uint64_t init_status                  : 4;
	uint64_t reserved_55_63               : 9;
#endif
	} cn63xxp1;
	struct cvmx_dfm_config_s              cn66xx;
};
typedef union cvmx_dfm_config cvmx_dfm_config_t;

/**
 * cvmx_dfm_control
 *
 * DFM_CONTROL = DFM Control
 * This register is an assortment of various control fields needed by the memory controller
 */
union cvmx_dfm_control {
	uint64_t u64;
	struct cvmx_dfm_control_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_24_63               : 40;
	uint64_t rodt_bprch                   : 1;  /**< When set, the turn-off time for the ODT pin during a
                                                         RD cmd is delayed an additional DCLK cycle. */
	uint64_t wodt_bprch                   : 1;  /**< When set, the turn-off time for the ODT pin during a
                                                         WR cmd is delayed an additional DCLK cycle. */
	uint64_t bprch                        : 2;  /**< Back Porch Enable: When set, the turn-on time for
                                                         the default DDR_DQ/DQS drivers is delayed an additional BPRCH FCLK
                                                         cycles.
                                                         00 = 0 fclks
                                                         01 = 1 fclks
                                                         10 = 2 fclks
                                                         11 = 3 fclks */
	uint64_t ext_zqcs_dis                 : 1;  /**< Disable (external) auto-zqcs calibration
                                                         When clear, DFM runs external ZQ calibration */
	uint64_t int_zqcs_dis                 : 1;  /**< Disable (internal) auto-zqcs calibration
                                                         When counter is re-enabled, ZQCS is run immediately,
                                                         and then every DFM_CONFIG[REF_ZQCS_INT] fclk cycles. */
	uint64_t auto_fclkdis                 : 1;  /**< When 1, DFM will automatically shut off its internal
                                                         clock to conserve power when there is no traffic. Note
                                                         that this has no effect on the DDR3 PHY and pads clocks. */
	uint64_t xor_bank                     : 1;  /**< Must be zero. */
	uint64_t max_write_batch              : 4;  /**< Must be set to value 8 */
	uint64_t nxm_write_en                 : 1;  /**< Must be zero. */
	uint64_t elev_prio_dis                : 1;  /**< Must be zero. */
	uint64_t inorder_wr                   : 1;  /**< Must be zero. */
	uint64_t inorder_rd                   : 1;  /**< Must be zero. */
	uint64_t throttle_wr                  : 1;  /**< When set, use at most one IFB for writes
                                                         THROTTLE_RD and THROTTLE_WR must be the same value. */
	uint64_t throttle_rd                  : 1;  /**< When set, use at most one IFB for reads
                                                         THROTTLE_RD and THROTTLE_WR must be the same value. */
	uint64_t fprch2                       : 2;  /**< Front Porch Enable: When set, the turn-off
                                                         time for the default DDR_DQ/DQS drivers is FPRCH2 fclks earlier.
                                                         00 = 0 fclks
                                                         01 = 1 fclks
                                                         10 = 2 fclks
                                                         11 = RESERVED */
	uint64_t pocas                        : 1;  /**< Enable the Posted CAS feature of DDR3.
                                                         This bit should be set in conjunction with DFM_MODEREG_PARAMS[AL] */
	uint64_t ddr2t                        : 1;  /**< Turn on the DDR 2T mode. 2 cycle window for CMD and
                                                         address. This mode helps relieve setup time pressure
                                                         on the Address and command bus which nominally have
                                                         a very large fanout. Please refer to Micron's tech
                                                         note tn_47_01 titled "DDR2-533 Memory Design Guide
                                                         for Two Dimm Unbuffered Systems" for physical details. */
	uint64_t bwcnt                        : 1;  /**< Bus utilization counter Clear.
                                                         Clears the DFM_OPS_CNT, DFM_IFB_CNT, and
                                                         DFM_FCLK_CNT registers. SW should first write this
                                                         field to a one, then write this field to a zero to
                                                         clear the CSR's. */
	uint64_t rdimm_ena                    : 1;  /**< Must be zero. */
#else
	uint64_t rdimm_ena                    : 1;
	uint64_t bwcnt                        : 1;
	uint64_t ddr2t                        : 1;
	uint64_t pocas                        : 1;
	uint64_t fprch2                       : 2;
	uint64_t throttle_rd                  : 1;
	uint64_t throttle_wr                  : 1;
	uint64_t inorder_rd                   : 1;
	uint64_t inorder_wr                   : 1;
	uint64_t elev_prio_dis                : 1;
	uint64_t nxm_write_en                 : 1;
	uint64_t max_write_batch              : 4;
	uint64_t xor_bank                     : 1;
	uint64_t auto_fclkdis                 : 1;
	uint64_t int_zqcs_dis                 : 1;
	uint64_t ext_zqcs_dis                 : 1;
	uint64_t bprch                        : 2;
	uint64_t wodt_bprch                   : 1;
	uint64_t rodt_bprch                   : 1;
	uint64_t reserved_24_63               : 40;
#endif
	} s;
	struct cvmx_dfm_control_s             cn63xx;
	struct cvmx_dfm_control_cn63xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_22_63               : 42;
	uint64_t bprch                        : 2;  /**< Back Porch Enable: When set, the turn-on time for
                                                         the default DDR_DQ/DQS drivers is delayed an additional BPRCH FCLK
                                                         cycles.
                                                         00 = 0 fclks
                                                         01 = 1 fclks
                                                         10 = 2 fclks
                                                         11 = 3 fclks */
	uint64_t ext_zqcs_dis                 : 1;  /**< Disable (external) auto-zqcs calibration
                                                         When clear, DFM runs external ZQ calibration */
	uint64_t int_zqcs_dis                 : 1;  /**< Disable (internal) auto-zqcs calibration
                                                         When counter is re-enabled, ZQCS is run immediately,
                                                         and then every DFM_CONFIG[REF_ZQCS_INT] fclk cycles. */
	uint64_t auto_fclkdis                 : 1;  /**< When 1, DFM will automatically shut off its internal
                                                         clock to conserve power when there is no traffic. Note
                                                         that this has no effect on the DDR3 PHY and pads clocks. */
	uint64_t xor_bank                     : 1;  /**< Must be zero. */
	uint64_t max_write_batch              : 4;  /**< Must be set to value 8 */
	uint64_t nxm_write_en                 : 1;  /**< Must be zero. */
	uint64_t elev_prio_dis                : 1;  /**< Must be zero. */
	uint64_t inorder_wr                   : 1;  /**< Must be zero. */
	uint64_t inorder_rd                   : 1;  /**< Must be zero. */
	uint64_t throttle_wr                  : 1;  /**< When set, use at most one IFB for writes
                                                         THROTTLE_RD and THROTTLE_WR must be the same value. */
	uint64_t throttle_rd                  : 1;  /**< When set, use at most one IFB for reads
                                                         THROTTLE_RD and THROTTLE_WR must be the same value. */
	uint64_t fprch2                       : 2;  /**< Front Porch Enable: When set, the turn-off
                                                         time for the default DDR_DQ/DQS drivers is FPRCH2 fclks earlier.
                                                         00 = 0 fclks
                                                         01 = 1 fclks
                                                         10 = 2 fclks
                                                         11 = RESERVED */
	uint64_t pocas                        : 1;  /**< Enable the Posted CAS feature of DDR3.
                                                         This bit should be set in conjunction with DFM_MODEREG_PARAMS[AL] */
	uint64_t ddr2t                        : 1;  /**< Turn on the DDR 2T mode. 2 cycle window for CMD and
                                                         address. This mode helps relieve setup time pressure
                                                         on the Address and command bus which nominally have
                                                         a very large fanout. Please refer to Micron's tech
                                                         note tn_47_01 titled "DDR2-533 Memory Design Guide
                                                         for Two Dimm Unbuffered Systems" for physical details. */
	uint64_t bwcnt                        : 1;  /**< Bus utilization counter Clear.
                                                         Clears the DFM_OPS_CNT, DFM_IFB_CNT, and
                                                         DFM_FCLK_CNT registers. SW should first write this
                                                         field to a one, then write this field to a zero to
                                                         clear the CSR's. */
	uint64_t rdimm_ena                    : 1;  /**< Must be zero. */
#else
	uint64_t rdimm_ena                    : 1;
	uint64_t bwcnt                        : 1;
	uint64_t ddr2t                        : 1;
	uint64_t pocas                        : 1;
	uint64_t fprch2                       : 2;
	uint64_t throttle_rd                  : 1;
	uint64_t throttle_wr                  : 1;
	uint64_t inorder_rd                   : 1;
	uint64_t inorder_wr                   : 1;
	uint64_t elev_prio_dis                : 1;
	uint64_t nxm_write_en                 : 1;
	uint64_t max_write_batch              : 4;
	uint64_t xor_bank                     : 1;
	uint64_t auto_fclkdis                 : 1;
	uint64_t int_zqcs_dis                 : 1;
	uint64_t ext_zqcs_dis                 : 1;
	uint64_t bprch                        : 2;
	uint64_t reserved_22_63               : 42;
#endif
	} cn63xxp1;
	struct cvmx_dfm_control_s             cn66xx;
};
typedef union cvmx_dfm_control cvmx_dfm_control_t;

/**
 * cvmx_dfm_dll_ctl2
 *
 * DFM_DLL_CTL2 = DFM (Octeon) DLL control and FCLK reset
 *
 *
 * Notes:
 * DLL Bringup sequence:
 * 1. If not done already, set DFM_DLL_CTL2 = 0, except when DFM_DLL_CTL2[DRESET] = 1.
 * 2. Write 1 to DFM_DLL_CTL2[DLL_BRINGUP]
 * 3. Wait for 10 FCLK cycles, then write 1 to DFM_DLL_CTL2[QUAD_DLL_ENA]. It may not be feasible to count 10 FCLK cycles, but the
 *    idea is to configure the delay line into DLL mode by asserting DLL_BRING_UP earlier than [QUAD_DLL_ENA], even if it is one
 *    cycle early. DFM_DLL_CTL2[QUAD_DLL_ENA] must not change after this point without restarting the DFM and/or DRESET initialization
 *    sequence.
 * 4. Read L2D_BST0 and wait for the result. (L2D_BST0 is subject to change depending on how it called in o63. It is still ok to go
 *    without step 4, since step 5 has enough time)
 * 5. Wait 10 us.
 * 6. Write 0 to DFM_DLL_CTL2[DLL_BRINGUP]. DFM_DLL_CTL2[DLL_BRINGUP] must not change after this point without restarting the DFM
 *    and/or DRESET initialization sequence.
 * 7. Read L2D_BST0 and wait for the result. (same as step 4, but the idea here is the wait some time before going to step 8, even it
 *    is one cycle is fine)
 * 8. Write 0 to DFM_DLL_CTL2[DRESET].  DFM_DLL_CTL2[DRESET] must not change after this point without restarting the DFM and/or
 *    DRESET initialization sequence.
 */
union cvmx_dfm_dll_ctl2 {
	uint64_t u64;
	struct cvmx_dfm_dll_ctl2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_15_63               : 49;
	uint64_t dll_bringup                  : 1;  /**< DLL Bringup */
	uint64_t dreset                       : 1;  /**< Fclk domain reset.  The reset signal that is used by the
                                                         Fclk domain is (DRESET || ECLK_RESET). */
	uint64_t quad_dll_ena                 : 1;  /**< DLL Enable */
	uint64_t byp_sel                      : 4;  /**< Bypass select
                                                         0000 : no byte
                                                         0001 : byte 0
                                                         - ...
                                                         1001 : byte 8
                                                         1010 : all bytes
                                                         1011-1111 : Reserved */
	uint64_t byp_setting                  : 8;  /**< Bypass setting
                                                         DDR3-1600: 00100010
                                                         DDR3-1333: 00110010
                                                         DDR3-1066: 01001011
                                                         DDR3-800 : 01110101
                                                         DDR3-667 : 10010110
                                                         DDR3-600 : 10101100 */
#else
	uint64_t byp_setting                  : 8;
	uint64_t byp_sel                      : 4;
	uint64_t quad_dll_ena                 : 1;
	uint64_t dreset                       : 1;
	uint64_t dll_bringup                  : 1;
	uint64_t reserved_15_63               : 49;
#endif
	} s;
	struct cvmx_dfm_dll_ctl2_s            cn63xx;
	struct cvmx_dfm_dll_ctl2_s            cn63xxp1;
	struct cvmx_dfm_dll_ctl2_s            cn66xx;
};
typedef union cvmx_dfm_dll_ctl2 cvmx_dfm_dll_ctl2_t;

/**
 * cvmx_dfm_dll_ctl3
 *
 * DFM_DLL_CTL3 = DFM DLL control and FCLK reset
 *
 */
union cvmx_dfm_dll_ctl3 {
	uint64_t u64;
	struct cvmx_dfm_dll_ctl3_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_29_63               : 35;
	uint64_t dll_fast                     : 1;  /**< DLL lock
                                                         0 = DLL locked */
	uint64_t dll90_setting                : 8;  /**< Encoded DLL settings. Works in conjuction with
                                                         DLL90_BYTE_SEL */
	uint64_t fine_tune_mode               : 1;  /**< Fine Tune Mode */
	uint64_t dll_mode                     : 1;  /**< DLL Mode */
	uint64_t dll90_byte_sel               : 4;  /**< Observe DLL settings for selected byte
                                                         0001 : byte 0
                                                         - ...
                                                         1001 : byte 8
                                                         0000,1010-1111 : Reserved */
	uint64_t offset_ena                   : 1;  /**< Offset enable
                                                         0 = disable
                                                         1 = enable */
	uint64_t load_offset                  : 1;  /**< Load offset
                                                         0 : disable
                                                         1 : load (generates a 1 cycle pulse to the PHY)
                                                         This register is oneshot and clears itself each time
                                                         it is set */
	uint64_t mode_sel                     : 2;  /**< Mode select
                                                         00 : reset
                                                         01 : write
                                                         10 : read
                                                         11 : write & read */
	uint64_t byte_sel                     : 4;  /**< Byte select
                                                         0000 : no byte
                                                         0001 : byte 0
                                                         - ...
                                                         1001 : byte 8
                                                         1010 : all bytes
                                                         1011-1111 : Reserved */
	uint64_t offset                       : 6;  /**< Write/read offset setting
                                                         [4:0] : offset
                                                         [5]   : 0 = increment, 1 = decrement
                                                         Not a 2's complement value */
#else
	uint64_t offset                       : 6;
	uint64_t byte_sel                     : 4;
	uint64_t mode_sel                     : 2;
	uint64_t load_offset                  : 1;
	uint64_t offset_ena                   : 1;
	uint64_t dll90_byte_sel               : 4;
	uint64_t dll_mode                     : 1;
	uint64_t fine_tune_mode               : 1;
	uint64_t dll90_setting                : 8;
	uint64_t dll_fast                     : 1;
	uint64_t reserved_29_63               : 35;
#endif
	} s;
	struct cvmx_dfm_dll_ctl3_s            cn63xx;
	struct cvmx_dfm_dll_ctl3_s            cn63xxp1;
	struct cvmx_dfm_dll_ctl3_s            cn66xx;
};
typedef union cvmx_dfm_dll_ctl3 cvmx_dfm_dll_ctl3_t;

/**
 * cvmx_dfm_fclk_cnt
 *
 * DFM_FCLK_CNT  = Performance Counters
 *
 */
union cvmx_dfm_fclk_cnt {
	uint64_t u64;
	struct cvmx_dfm_fclk_cnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t fclkcnt                      : 64; /**< Performance Counter that counts fclks
                                                         64-bit counter. */
#else
	uint64_t fclkcnt                      : 64;
#endif
	} s;
	struct cvmx_dfm_fclk_cnt_s            cn63xx;
	struct cvmx_dfm_fclk_cnt_s            cn63xxp1;
	struct cvmx_dfm_fclk_cnt_s            cn66xx;
};
typedef union cvmx_dfm_fclk_cnt cvmx_dfm_fclk_cnt_t;

/**
 * cvmx_dfm_fnt_bist
 *
 * DFM_FNT_BIST = DFM Front BIST Status
 *
 * This register contains Bist Status for DFM Front
 */
union cvmx_dfm_fnt_bist {
	uint64_t u64;
	struct cvmx_dfm_fnt_bist_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_5_63                : 59;
	uint64_t cab                          : 1;  /**< Bist Results for CAB RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t mrq                          : 1;  /**< Bist Results for MRQ RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t mff                          : 1;  /**< Bist Results for MFF RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t rpb                          : 1;  /**< Bist Results for RPB RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t mwb                          : 1;  /**< Bist Results for MWB RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
#else
	uint64_t mwb                          : 1;
	uint64_t rpb                          : 1;
	uint64_t mff                          : 1;
	uint64_t mrq                          : 1;
	uint64_t cab                          : 1;
	uint64_t reserved_5_63                : 59;
#endif
	} s;
	struct cvmx_dfm_fnt_bist_s            cn63xx;
	struct cvmx_dfm_fnt_bist_cn63xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t mrq                          : 1;  /**< Bist Results for MRQ RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t mff                          : 1;  /**< Bist Results for MFF RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t rpb                          : 1;  /**< Bist Results for RPB RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
	uint64_t mwb                          : 1;  /**< Bist Results for MWB RAM
                                                         - 0: GOOD (or bist in progress/never run)
                                                         - 1: BAD */
#else
	uint64_t mwb                          : 1;
	uint64_t rpb                          : 1;
	uint64_t mff                          : 1;
	uint64_t mrq                          : 1;
	uint64_t reserved_4_63                : 60;
#endif
	} cn63xxp1;
	struct cvmx_dfm_fnt_bist_s            cn66xx;
};
typedef union cvmx_dfm_fnt_bist cvmx_dfm_fnt_bist_t;

/**
 * cvmx_dfm_fnt_ctl
 *
 * Specify the RSL base addresses for the block
 *
 *                  DFM_FNT_CTL = DFM Front Control Register
 *
 * This register contains control registers for the DFM Front Section of Logic.
 */
union cvmx_dfm_fnt_ctl {
	uint64_t u64;
	struct cvmx_dfm_fnt_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t sbe_ena                      : 1;  /**< If SBE_ENA=1 & RECC_ENA=1 then all single bit errors
                                                         which have been detected/corrected during GWALK reads,
                                                         will be reported through RWORD0[REA]=ERR code in system
                                                         memory at the conclusion of the DFA instruction.
                                                         SWNOTE: The application user may wish to report single
                                                         bit errors that were corrected through the
                                                         RWORD0[REA]=ERR codeword.
                                                         NOTE: This DOES NOT effect the reporting of SBEs in
                                                         DFM_FNT_STAT[SBE] (which were corrected if RECC_ENA=1).
                                                         This bit is only here for applications which 'MAY' want
                                                         to be alerted with an ERR completion code if there were
                                                         SBEs that were auto-corrected during GWALK instructions.
                                                         Recap: If there is a SBE and SBE_ENA==1, the "err" field
                                                         in the data returned to DFA will be set.  If SBE_ENA==0,
                                                         the "err" is always 0 when there is a SBE; however,
                                                         regardless of SBE_ENA, DBE will cause "err" to be 1. */
	uint64_t wecc_ena                     : 1;  /**< If WECC_ENA=1, HW will auto-generate(overwrite) the 10b
                                                         OWECC codeword during Memory Writes sourced by
                                                         1) DFA MLOAD instructions, or by 2) NCB-Direct CSR
                                                         mode writes to DFA memory space. The HW will insert
                                                         the 10b OWECC inband into OW-DATA[127:118].
                                                         If WECC_ENA=0, SW is responsible for generating the
                                                         10b OWECC codeword inband in the upper OW-data[127:118]
                                                         during Memory writes (to provide SEC/DED coverage for
                                                         the data during subsequent Memory reads-see RECC_ENA). */
	uint64_t recc_ena                     : 1;  /**< If RECC_ENA=1, all DFA memory reads sourced by 1) DFA
                                                         GWALK instructions or by 2) NCB-Direct CSR mode reads
                                                         to DFA memory space, will be protected by an inband 10b
                                                         OWECC SEC/DED codeword. The inband OW-DATA[127:118]
                                                         represents the inband OWECC codeword which offers single
                                                         bit error correction(SEC)/double bit error detection(DED).
                                                         [see also DFM_FNT_STAT[SBE,DBE,FADR,FSYN] status fields].
                                                         The FSYN field contains an encoded value which determines
                                                         which bit was corrected(for SBE) or detected(for DBE) to
                                                         help in bit isolation of the error.
                                                         SW NOTE: If RECC_ENA=1: An NCB-Direct CSR mode read of the
                                                         upper QW in memory will return ZEROES in the upper 10b of the
                                                         data word.
                                                         If RECC_ENA=0: An NCB-Direct CSR mode read of the upper QW in
                                                         memory will return the RAW 64bits from memory. During memory
                                                         debug, writing RECC_ENA=0 provides visibility into the raw ECC
                                                         stored in memory at that time. */
	uint64_t dfr_ena                      : 1;  /**< DFM Memory Interface Enable
                                                         The DFM powers up with the DDR3 interface disabled.
                                                         If the DFA function is required, then after poweron
                                                         software configures a stable DFM DDR3 memory clock
                                                         (see: LMCx_DDR_PLL_CTL[DFM_PS_EN, DFM_DIV_RESET]),
                                                         the DFM DDR3 memory interface can be enabled.
                                                         When disabled (DFR_ENA=0), all DFM DDR3 memory
                                                         output and bidirectional pins will be tristated.
                                                         SW NOTE: The DFR_ENA=1 write MUST occur sometime after
                                                         the DFM is brought out of reset (ie: after the
                                                         DFM_DLL_CTL2[DRESET]=0 write). */
#else
	uint64_t dfr_ena                      : 1;
	uint64_t recc_ena                     : 1;
	uint64_t wecc_ena                     : 1;
	uint64_t sbe_ena                      : 1;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_dfm_fnt_ctl_s             cn63xx;
	struct cvmx_dfm_fnt_ctl_s             cn63xxp1;
	struct cvmx_dfm_fnt_ctl_s             cn66xx;
};
typedef union cvmx_dfm_fnt_ctl cvmx_dfm_fnt_ctl_t;

/**
 * cvmx_dfm_fnt_iena
 *
 * DFM_FNT_IENA = DFM Front Interrupt Enable Mask
 *
 * This register contains error interrupt enable information for the DFM Front Section of Logic.
 */
union cvmx_dfm_fnt_iena {
	uint64_t u64;
	struct cvmx_dfm_fnt_iena_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_2_63                : 62;
	uint64_t dbe_intena                   : 1;  /**< OWECC Double Error Detected(DED) Interrupt Enable
                                                         When set, the memory controller raises a processor
                                                         interrupt on detecting an uncorrectable double bit
                                                         OWECC during a memory read. */
	uint64_t sbe_intena                   : 1;  /**< OWECC Single Error Corrected(SEC) Interrupt Enable
                                                         When set, the memory controller raises a processor
                                                         interrupt on detecting a correctable single bit
                                                         OWECC error which was corrected during a memory
                                                         read. */
#else
	uint64_t sbe_intena                   : 1;
	uint64_t dbe_intena                   : 1;
	uint64_t reserved_2_63                : 62;
#endif
	} s;
	struct cvmx_dfm_fnt_iena_s            cn63xx;
	struct cvmx_dfm_fnt_iena_s            cn63xxp1;
	struct cvmx_dfm_fnt_iena_s            cn66xx;
};
typedef union cvmx_dfm_fnt_iena cvmx_dfm_fnt_iena_t;

/**
 * cvmx_dfm_fnt_sclk
 *
 * DFM_FNT_SCLK = DFM Front SCLK Control Register
 *
 * This register contains control registers for the DFM Front Section of Logic.
 * NOTE: This register is in USCLK domain and is ised to enable the conditional SCLK grid, as well as
 * to start a software BiST sequence for the DFM sub-block. (note: the DFM has conditional clocks which
 * prevent BiST to run under reset automatically).
 */
union cvmx_dfm_fnt_sclk {
	uint64_t u64;
	struct cvmx_dfm_fnt_sclk_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_3_63                : 61;
	uint64_t clear_bist                   : 1;  /**< When START_BIST is written 0->1, if CLEAR_BIST=1, all
                                                         previous BiST state is cleared.
                                                         NOTES:
                                                         1) CLEAR_BIST must be written to 1 before START_BIST
                                                         is written to 1 using a separate CSR write.
                                                         2) CLEAR_BIST must not be changed after writing START_BIST
                                                         0->1 until the BIST operation completes. */
	uint64_t bist_start                   : 1;  /**< When software writes BIST_START=0->1, a BiST is executed
                                                         for the DFM sub-block.
                                                         NOTES:
                                                         1) This bit should only be written after BOTH sclk
                                                         and fclk have been enabled by software and are stable
                                                         (see: DFM_FNT_SCLK[SCLKDIS] and instructions on how to
                                                         enable the DFM DDR3 memory (fclk) - which requires LMC
                                                         PLL init, DFM clock divider and proper DFM DLL
                                                         initialization sequence). */
	uint64_t sclkdis                      : 1;  /**< DFM sclk disable Source
                                                         When SET, the DFM sclk are disabled (to conserve overall
                                                         chip clocking power when the DFM function is not used).
                                                         NOTE: This should only be written to a different value
                                                         during power-on SW initialization. */
#else
	uint64_t sclkdis                      : 1;
	uint64_t bist_start                   : 1;
	uint64_t clear_bist                   : 1;
	uint64_t reserved_3_63                : 61;
#endif
	} s;
	struct cvmx_dfm_fnt_sclk_s            cn63xx;
	struct cvmx_dfm_fnt_sclk_s            cn63xxp1;
	struct cvmx_dfm_fnt_sclk_s            cn66xx;
};
typedef union cvmx_dfm_fnt_sclk cvmx_dfm_fnt_sclk_t;

/**
 * cvmx_dfm_fnt_stat
 *
 * DFM_FNT_STAT = DFM Front Status Register
 *
 * This register contains error status information for the DFM Front Section of Logic.
 */
union cvmx_dfm_fnt_stat {
	uint64_t u64;
	struct cvmx_dfm_fnt_stat_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_42_63               : 22;
	uint64_t fsyn                         : 10; /**< Failing Syndrome
                                                         If SBE_ERR=1, the FSYN code determines which bit was
                                                         corrected during the OWECC check/correct.
                                                         NOTE: If both DBE_ERR/SBE_ERR are set, the DBE_ERR has
                                                         higher priority and FSYN captured will always be for the
                                                         DBE_ERR detected.
                                                         The FSYN is "locked down" when either DBE_ERR/SBE_ERR
                                                         are detected (until these bits are cleared (W1C)).
                                                         However, if an SBE_ERR occurs first, followed by a
                                                         DBE_ERR, the higher priority DBE_ERR will re-capture
                                                         the FSYN for the higher priority error case. */
	uint64_t fadr                         : 28; /**< Failing Memory octaword address
                                                         If either SBE_ERR or DBE_ERR are set, the FADR
                                                         represents the failing octaword address.
                                                         NOTE: If both DBE_ERR/SBE_ERR are set, the DBE_ERR has
                                                         higher priority and the FADR captured will always be
                                                         with the DBE_ERR detected.
                                                         The FADR is "locked down" when either DBE_ERR/SBE_ERR
                                                         are detected (until these bits are cleared (W1C)).
                                                         However, if an SBE_ERR occurs first, followed by a
                                                         DBE_ERR, the higher priority DBE_ERR will re-capture
                                                         the FADR for the higher priority error case. */
	uint64_t reserved_2_3                 : 2;
	uint64_t dbe_err                      : 1;  /**< Double bit error detected(uncorrectable) during
                                                         Memory Read.
                                                         Write of 1 will clear the corresponding error bit */
	uint64_t sbe_err                      : 1;  /**< Single bit error detected(corrected) during
                                                         Memory Read.
                                                         Write of 1 will clear the corresponding error bit */
#else
	uint64_t sbe_err                      : 1;
	uint64_t dbe_err                      : 1;
	uint64_t reserved_2_3                 : 2;
	uint64_t fadr                         : 28;
	uint64_t fsyn                         : 10;
	uint64_t reserved_42_63               : 22;
#endif
	} s;
	struct cvmx_dfm_fnt_stat_s            cn63xx;
	struct cvmx_dfm_fnt_stat_s            cn63xxp1;
	struct cvmx_dfm_fnt_stat_s            cn66xx;
};
typedef union cvmx_dfm_fnt_stat cvmx_dfm_fnt_stat_t;

/**
 * cvmx_dfm_ifb_cnt
 *
 * DFM_IFB_CNT  = Performance Counters
 *
 */
union cvmx_dfm_ifb_cnt {
	uint64_t u64;
	struct cvmx_dfm_ifb_cnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t ifbcnt                       : 64; /**< Performance Counter
                                                         64-bit counter that increments every
                                                         cycle there is something in the in-flight buffer.
                                                         Before using, clear counter via DFM_CONTROL.BWCNT. */
#else
	uint64_t ifbcnt                       : 64;
#endif
	} s;
	struct cvmx_dfm_ifb_cnt_s             cn63xx;
	struct cvmx_dfm_ifb_cnt_s             cn63xxp1;
	struct cvmx_dfm_ifb_cnt_s             cn66xx;
};
typedef union cvmx_dfm_ifb_cnt cvmx_dfm_ifb_cnt_t;

/**
 * cvmx_dfm_modereg_params0
 *
 * Notes:
 * These parameters are written into the DDR3 MR0, MR1, MR2 and MR3 registers.
 *
 */
union cvmx_dfm_modereg_params0 {
	uint64_t u64;
	struct cvmx_dfm_modereg_params0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_25_63               : 39;
	uint64_t ppd                          : 1;  /**< DLL Control for precharge powerdown
                                                         0 = Slow exit (DLL off)
                                                         1 = Fast exit (DLL on)
                                                         DFM writes this value to MR0[PPD] in the selected DDR3 parts
                                                         during power-up/init instruction sequencing.
                                                         See DFM_CONFIG[SEQUENCE,INIT_START,RANKMASK].
                                                         This value must equal the MR0[PPD] value in all the DDR3
                                                         parts attached to all ranks during normal operation. */
	uint64_t wrp                          : 3;  /**< Write recovery for auto precharge
                                                         Should be programmed to be equal to or greater than
                                                         RNDUP[tWR(ns)/tCYC(ns)]
                                                         000 = 5
                                                         001 = 5
                                                         010 = 6
                                                         011 = 7
                                                         100 = 8
                                                         101 = 10
                                                         110 = 12
                                                         111 = 14
                                                         DFM writes this value to MR0[WR] in the selected DDR3 parts
                                                         during power-up/init instruction sequencing.
                                                         See DFM_CONFIG[SEQUENCE,INIT_START,RANKMASK].
                                                         This value must equal the MR0[WR] value in all the DDR3
                                                         parts attached to all ranks during normal operation. */
	uint64_t dllr                         : 1;  /**< DLL Reset
                                                         DFM writes this value to MR0[DLL] in the selected DDR3 parts
                                                         during power-up/init instruction sequencing.
                                                         See DFM_CONFIG[SEQUENCE,INIT_START,RANKMASK].
                                                         The MR0[DLL] value must be 0 in all the DDR3
                                                         parts attached to all ranks during normal operation. */
	uint64_t tm                           : 1;  /**< Test Mode
                                                         DFM writes this value to MR0[TM] in the selected DDR3 parts
                                                         during power-up/init instruction sequencing.
                                                         See DFM_CONFIG[SEQUENCE,INIT_START,RANKMASK].
                                                         The MR0[TM] value must be 0 in all the DDR3
                                                         parts attached to all ranks during normal operation. */
	uint64_t rbt                          : 1;  /**< Read Burst Type
                                                         1 = interleaved (fixed)
                                                         DFM writes this value to MR0[RBT] in the selected DDR3 parts
                                                         during power-up/init instruction sequencing.
                                                         See DFM_CONFIG[SEQUENCE,INIT_START,RANKMASK].
                                                         The MR0[RBT] value must be 1 in all the DDR3
                                                         parts attached to all ranks during normal operation. */
	uint64_t cl                           : 4;  /**< CAS Latency
                                                         0010 = 5
                                                         0100 = 6
                                                         0110 = 7
                                                         1000 = 8
                                                         1010 = 9
                                                         1100 = 10
                                                         1110 = 11
                                                         0001 = 12
                                                         0011 = 13
                                                         0101 = 14
                                                         0111 = 15
                                                         1001 = 16
                                                         0000, 1011, 1101, 1111 = Reserved
                                                         DFM writes this value to MR0[CAS Latency / CL] in the selected DDR3 parts
                                                         during power-up/init instruction sequencing.
                                                         See DFM_CONFIG[SEQUENCE,INIT_START,RANKMASK].
                                                         This value must equal the MR0[CAS Latency / CL] value in all the DDR3
                                                         parts attached to all ranks during normal operation. */
	uint64_t bl                           : 2;  /**< Burst Length
                                                         0 = 8 (fixed)
                                                         DFM writes this value to MR0[BL] in the selected DDR3 parts
                                                         during power-up/init instruction sequencing.
                                                         See DFM_CONFIG[SEQUENCE,INIT_START,RANKMASK].
                                                         The MR0[BL] value must be 0 in all the DDR3
                                                         parts attached to all ranks during normal operation. */
	uint64_t qoff                         : 1;  /**< Qoff Enable
                                                         0 = enable
                                                         DFM writes this value to MR1[Qoff] in the selected DDR3 parts
                                                         during power-up/init and write-leveling instruction sequencing.
                                                         If DFM_CONFIG[SREF_WITH_DLL] is set, DFM also writes
                                                         this value to MR1[Qoff] in all DRAM parts in DFM_CONFIG[INIT_STATUS] ranks during self-refresh
                                                         entry and exit instruction sequences.
                                                         See DFM_CONFIG[SEQUENCE,INIT_START,RANKMASK] and
                                                         DFM_RESET_CTL[DDR3PWARM,DDR3PSOFT].
                                                         The MR1[Qoff] value must be 0 in all the DDR3
                                                         parts attached to all ranks during normal operation. */
	uint64_t tdqs                         : 1;  /**< TDQS Enable
                                                         0 = disable
                                                         DFM writes this value to MR1[TDQS] in the selected DDR3 parts
                                                         during power-up/init and write-leveling instruction sequencing.
                                                         If DFM_CONFIG[SREF_WITH_DLL] is set, DFM also writes
                                                         this value to MR1[TDQS] in all DRAM parts in DFM_CONFIG[INIT_STATUS] ranks during self-refresh
                                                         entry and exit instruction sequences.
                                                         See DFM_CONFIG[SEQUENCE,INIT_START,RANKMASK] and
                                                         DFM_RESET_CTL[DDR3PWARM,DDR3PSOFT]. */
	uint64_t wlev                         : 1;  /**< Write Leveling Enable
                                                         0 = disable
                                                         DFM writes MR1[Level]=0 in the selected DDR3 parts
                                                         during power-up/init and write-leveling instruction sequencing.
                                                         (DFM also writes MR1[Level]=1 at the beginning of a
                                                         write-leveling instruction sequence. Write-leveling can only be initiated via the
                                                         write-leveling instruction sequence.)
                                                         If DFM_CONFIG[SREF_WITH_DLL] is set, DFM also writes
                                                         MR1[Level]=0 in all DRAM parts in DFM_CONFIG[INIT_STATUS] ranks during self-refresh
                                                         entry and exit instruction sequences.
                                                         See DFM_CONFIG[SEQUENCE,INIT_START,RANKMASK] and
                                                         DFM_RESET_CTL[DDR3PWARM,DDR3PSOFT]. */
	uint64_t al                           : 2;  /**< Additive Latency
                                                         00 = 0
                                                         01 = CL-1
                                                         10 = CL-2
                                                         11 = Reserved
                                                         DFM writes this value to MR1[AL] in the selected DDR3 parts
                                                         during power-up/init and write-leveling instruction sequencing.
                                                         If DFM_CONFIG[SREF_WITH_DLL] is set, DFM also writes
                                                         this value to MR1[AL] in all DRAM parts in DFM_CONFIG[INIT_STATUS] ranks during self-refresh
                                                         entry and exit instruction sequences.
                                                         See DFM_CONFIG[SEQUENCE,INIT_START,RANKMASK] and
                                                         DFM_RESET_CTL[DDR3PWARM,DDR3PSOFT].
                                                         This value must equal the MR1[AL] value in all the DDR3
                                                         parts attached to all ranks during normal operation.
                                                         See also DFM_CONTROL[POCAS]. */
	uint64_t dll                          : 1;  /**< DLL Enable
                                                         0 = enable
                                                         1 = disable
                                                         DFM writes this value to MR1[DLL] in the selected DDR3 parts
                                                         during power-up/init and write-leveling instruction sequencing.
                                                         If DFM_CONFIG[SREF_WITH_DLL] is set, DFM also writes
                                                         this value to MR1[DLL] in all DRAM parts in DFM_CONFIG[INIT_STATUS] ranks during self-refresh
                                                         entry and exit instruction sequences.
                                                         See DFM_CONFIG[SEQUENCE,INIT_START,RANKMASK] and
                                                         DFM_RESET_CTL[DDR3PWARM,DDR3PSOFT].
                                                         This value must equal the MR1[DLL] value in all the DDR3
                                                         parts attached to all ranks during normal operation.
                                                         In dll-off mode, CL/CWL must be programmed
                                                         equal to 6/6, respectively, as per the DDR3 specifications. */
	uint64_t mpr                          : 1;  /**< MPR
                                                         DFM writes this value to MR3[MPR] in the selected DDR3 parts
                                                         during power-up/init and read-leveling instruction sequencing.
                                                         (DFM also writes MR3[MPR]=1 at the beginning of a
                                                         read-leveling instruction sequence. Read-leveling can only be initiated via the
                                                         read-leveling instruction sequence.)
                                                         See DFM_CONFIG[SEQUENCE,INIT_START,RANKMASK].
                                                         The MR3[MPR] value must be 0 in all the DDR3
                                                         parts attached to all ranks during normal operation. */
	uint64_t mprloc                       : 2;  /**< MPR Location
                                                         DFM writes this value to MR3[MPRLoc] in the selected DDR3 parts
                                                         during power-up/init and read-leveling instruction sequencing.
                                                         (DFM also writes MR3[MPRLoc]=0 at the beginning of the
                                                         read-leveling instruction sequence.)
                                                         See DFM_CONFIG[SEQUENCE,INIT_START,RANKMASK].
                                                         The MR3[MPRLoc] value must be 0 in all the DDR3
                                                         parts attached to all ranks during normal operation. */
	uint64_t cwl                          : 3;  /**< CAS Write Latency
                                                         - 000: 5
                                                         - 001: 6
                                                         - 010: 7
                                                         - 011: 8
                                                         - 100: 9
                                                         - 101: 10
                                                         - 110: 11
                                                         - 111: 12
                                                         DFM writes this value to MR2[CWL] in the selected DDR3 parts
                                                         during power-up/init instruction sequencing.
                                                         If DFM_CONFIG[SREF_WITH_DLL] is set, DFM also writes
                                                         this value to MR2[CWL] in all DRAM parts in DFM_CONFIG[INIT_STATUS] ranks during self-refresh
                                                         entry and exit instruction sequences.
                                                         See DFM_CONFIG[SEQUENCE,INIT_START,RANKMASK] and
                                                         DFM_RESET_CTL[DDR3PWARM,DDR3PSOFT].
                                                         This value must equal the MR2[CWL] value in all the DDR3
                                                         parts attached to all ranks during normal operation. */
#else
	uint64_t cwl                          : 3;
	uint64_t mprloc                       : 2;
	uint64_t mpr                          : 1;
	uint64_t dll                          : 1;
	uint64_t al                           : 2;
	uint64_t wlev                         : 1;
	uint64_t tdqs                         : 1;
	uint64_t qoff                         : 1;
	uint64_t bl                           : 2;
	uint64_t cl                           : 4;
	uint64_t rbt                          : 1;
	uint64_t tm                           : 1;
	uint64_t dllr                         : 1;
	uint64_t wrp                          : 3;
	uint64_t ppd                          : 1;
	uint64_t reserved_25_63               : 39;
#endif
	} s;
	struct cvmx_dfm_modereg_params0_s     cn63xx;
	struct cvmx_dfm_modereg_params0_s     cn63xxp1;
	struct cvmx_dfm_modereg_params0_s     cn66xx;
};
typedef union cvmx_dfm_modereg_params0 cvmx_dfm_modereg_params0_t;

/**
 * cvmx_dfm_modereg_params1
 *
 * Notes:
 * These parameters are written into the DDR3 MR0, MR1, MR2 and MR3 registers.
 *
 */
union cvmx_dfm_modereg_params1 {
	uint64_t u64;
	struct cvmx_dfm_modereg_params1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_48_63               : 16;
	uint64_t rtt_nom_11                   : 3;  /**< Must be zero */
	uint64_t dic_11                       : 2;  /**< Must be zero */
	uint64_t rtt_wr_11                    : 2;  /**< Must be zero */
	uint64_t srt_11                       : 1;  /**< Must be zero */
	uint64_t asr_11                       : 1;  /**< Must be zero */
	uint64_t pasr_11                      : 3;  /**< Must be zero */
	uint64_t rtt_nom_10                   : 3;  /**< Must be zero */
	uint64_t dic_10                       : 2;  /**< Must be zero */
	uint64_t rtt_wr_10                    : 2;  /**< Must be zero */
	uint64_t srt_10                       : 1;  /**< Must be zero */
	uint64_t asr_10                       : 1;  /**< Must be zero */
	uint64_t pasr_10                      : 3;  /**< Must be zero */
	uint64_t rtt_nom_01                   : 3;  /**< RTT_NOM Rank 1
                                                         DFM writes this value to MR1[Rtt_Nom] in the rank 1 (i.e. CS1) DDR3 parts
                                                         when selected during power-up/init instruction sequencing.
                                                         If DFM_CONFIG[SREF_WITH_DLL] is set, DFM also writes
                                                         this value to MR1[Rtt_Nom] in all DRAM parts in rank 1 during self-refresh
                                                         entry and exit instruction sequences (when DFM_CONFIG[INIT_STATUS<1>]=1).
                                                         See DFM_CONFIG[SEQUENCE,INIT_START,RANKMASK] and
                                                         DFM_RESET_CTL[DDR3PWARM,DDR3PSOFT]. */
	uint64_t dic_01                       : 2;  /**< Output Driver Impedance Control Rank 1
                                                         DFM writes this value to MR1[D.I.C.] in the rank 1 (i.e. CS1) DDR3 parts
                                                         when selected during power-up/init and write-leveling instruction sequencing.
                                                         If DFM_CONFIG[SREF_WITH_DLL] is set, DFM also writes
                                                         this value to MR1[D.I.C.] in all DRAM parts in rank 1 during self-refresh
                                                         entry and exit instruction sequences (when DFM_CONFIG[INIT_STATUS<1>]=1).
                                                         See DFM_CONFIG[SEQUENCE,INIT_START,RANKMASK] and
                                                         DFM_RESET_CTL[DDR3PWARM,DDR3PSOFT]. */
	uint64_t rtt_wr_01                    : 2;  /**< RTT_WR Rank 1
                                                         DFM writes this value to MR2[Rtt_WR] in the rank 1 (i.e. CS1) DDR3 parts
                                                         when selected during power-up/init instruction sequencing.
                                                         If DFM_CONFIG[SREF_WITH_DLL] is set, DFM also writes
                                                         this value to MR2[Rtt_WR] in all DRAM parts in rank 1 during self-refresh
                                                         entry and exit instruction sequences (when DFM_CONFIG[INIT_STATUS<1>]=1).
                                                         See DFM_CONFIG[SEQUENCE,INIT_START,RANKMASK] and
                                                         DFM_RESET_CTL[DDR3PWARM,DDR3PSOFT]. */
	uint64_t srt_01                       : 1;  /**< Self-refresh temperature range Rank 1
                                                         DFM writes this value to MR2[SRT] in the rank 1 (i.e. CS1) DDR3 parts
                                                         when selected during power-up/init instruction sequencing.
                                                         If DFM_CONFIG[SREF_WITH_DLL] is set, DFM also writes
                                                         this value to MR2[SRT] in all DRAM parts in rank 1 during self-refresh
                                                         entry and exit instruction sequences (when DFM_CONFIG[INIT_STATUS<1>]=1).
                                                         See DFM_CONFIG[SEQUENCE,INIT_START,RANKMASK] and
                                                         DFM_RESET_CTL[DDR3PWARM,DDR3PSOFT]. */
	uint64_t asr_01                       : 1;  /**< Auto self-refresh Rank 1
                                                         DFM writes this value to MR2[ASR] in the rank 1 (i.e. CS1) DDR3 parts
                                                         when selected during power-up/init instruction sequencing.
                                                         If DFM_CONFIG[SREF_WITH_DLL] is set, DFM also writes
                                                         this value to MR2[ASR] in all DRAM parts in rank 1 during self-refresh
                                                         entry and exit instruction sequences (when DFM_CONFIG[INIT_STATUS<1>]=1).
                                                         See DFM_CONFIG[SEQUENCE,INIT_START,RANKMASK] and
                                                         DFM_RESET_CTL[DDR3PWARM,DDR3PSOFT]. */
	uint64_t pasr_01                      : 3;  /**< Partial array self-refresh Rank 1
                                                         DFM writes this value to MR2[PASR] in the rank 1 (i.e. CS1) DDR3 parts
                                                         when selected during power-up/init instruction sequencing.
                                                         If DFM_CONFIG[SREF_WITH_DLL] is set, DFM also writes
                                                         this value to MR2[PASR] in all DRAM parts in rank 1 during self-refresh
                                                         entry and exit instruction sequences (when DFM_CONFIG[INIT_STATUS<1>]=1).
                                                         See DFM_CONFIG[SEQUENCE,INIT_START,RANKMASK] and
                                                         DFM_RESET_CTL[DDR3PWARM,DDR3PSOFT]. */
	uint64_t rtt_nom_00                   : 3;  /**< RTT_NOM Rank 0
                                                         DFM writes this value to MR1[Rtt_Nom] in the rank 0 (i.e. CS0) DDR3 parts
                                                         when selected during power-up/init instruction sequencing.
                                                         If DFM_CONFIG[SREF_WITH_DLL] is set, DFM also writes
                                                         this value to MR1[Rtt_Nom] in all DRAM parts in rank 0 during self-refresh
                                                         entry and exit instruction sequences (when DFM_CONFIG[INIT_STATUS<0>]=1).
                                                         See DFM_CONFIG[SEQUENCE,INIT_START,RANKMASK] and
                                                         DFM_RESET_CTL[DDR3PWARM,DDR3PSOFT]. */
	uint64_t dic_00                       : 2;  /**< Output Driver Impedance Control Rank 0
                                                         DFM writes this value to MR1[D.I.C.] in the rank 0 (i.e. CS0) DDR3 parts
                                                         when selected during power-up/init and write-leveling instruction sequencing.
                                                         If DFM_CONFIG[SREF_WITH_DLL] is set, DFM also writes
                                                         this value to MR1[D.I.C.] in all DRAM parts in rank 0 during self-refresh
                                                         entry and exit instruction sequences (when DFM_CONFIG[INIT_STATUS<0>]=1).
                                                         See DFM_CONFIG[SEQUENCE,INIT_START,RANKMASK] and
                                                         DFM_RESET_CTL[DDR3PWARM,DDR3PSOFT]. */
	uint64_t rtt_wr_00                    : 2;  /**< RTT_WR Rank 0
                                                         DFM writes this value to MR2[Rtt_WR] in the rank 0 (i.e. CS0) DDR3 parts
                                                         when selected during power-up/init instruction sequencing.
                                                         If DFM_CONFIG[SREF_WITH_DLL] is set, DFM also writes
                                                         this value to MR2[Rtt_WR] in all DRAM parts in rank 0 during self-refresh
                                                         entry and exit instruction sequences (when DFM_CONFIG[INIT_STATUS<0>]=1).
                                                         See DFM_CONFIG[SEQUENCE,INIT_START,RANKMASK] and
                                                         DFM_RESET_CTL[DDR3PWARM,DDR3PSOFT]. */
	uint64_t srt_00                       : 1;  /**< Self-refresh temperature range Rank 0
                                                         DFM writes this value to MR2[SRT] in the rank 0 (i.e. CS0) DDR3 parts
                                                         when selected during power-up/init instruction sequencing.
                                                         If DFM_CONFIG[SREF_WITH_DLL] is set, DFM also writes
                                                         this value to MR2[SRT] in all DRAM parts in rank 0 during self-refresh
                                                         entry and exit instruction sequences (when DFM_CONFIG[INIT_STATUS<0>]=1).
                                                         See DFM_CONFIG[SEQUENCE,INIT_START,RANKMASK] and
                                                         DFM_RESET_CTL[DDR3PWARM,DDR3PSOFT]. */
	uint64_t asr_00                       : 1;  /**< Auto self-refresh Rank 0
                                                         DFM writes this value to MR2[ASR] in the rank 0 (i.e. CS0) DDR3 parts
                                                         when selected during power-up/init instruction sequencing.
                                                         If DFM_CONFIG[SREF_WITH_DLL] is set, DFM also writes
                                                         this value to MR2[ASR] in all DRAM parts in rank 0 during self-refresh
                                                         entry and exit instruction sequences (when DFM_CONFIG[INIT_STATUS<0>]=1).
                                                         See DFM_CONFIG[SEQUENCE,INIT_START,RANKMASK] and
                                                         DFM_RESET_CTL[DDR3PWARM,DDR3PSOFT]. */
	uint64_t pasr_00                      : 3;  /**< Partial array self-refresh Rank 0
                                                         DFM writes this value to MR2[PASR] in the rank 0 (i.e. CS0) DDR3 parts
                                                         when selected during power-up/init instruction sequencing.
                                                         If DFM_CONFIG[SREF_WITH_DLL] is set, DFM also writes
                                                         this value to MR2[PASR] in all DRAM parts in rank 0 during self-refresh
                                                         entry and exit instruction sequences (when DFM_CONFIG[INIT_STATUS<0>]=1).
                                                         See DFM_CONFIG[SEQUENCE,INIT_START,RANKMASK] and
                                                         DFM_RESET_CTL[DDR3PWARM,DDR3PSOFT]. */
#else
	uint64_t pasr_00                      : 3;
	uint64_t asr_00                       : 1;
	uint64_t srt_00                       : 1;
	uint64_t rtt_wr_00                    : 2;
	uint64_t dic_00                       : 2;
	uint64_t rtt_nom_00                   : 3;
	uint64_t pasr_01                      : 3;
	uint64_t asr_01                       : 1;
	uint64_t srt_01                       : 1;
	uint64_t rtt_wr_01                    : 2;
	uint64_t dic_01                       : 2;
	uint64_t rtt_nom_01                   : 3;
	uint64_t pasr_10                      : 3;
	uint64_t asr_10                       : 1;
	uint64_t srt_10                       : 1;
	uint64_t rtt_wr_10                    : 2;
	uint64_t dic_10                       : 2;
	uint64_t rtt_nom_10                   : 3;
	uint64_t pasr_11                      : 3;
	uint64_t asr_11                       : 1;
	uint64_t srt_11                       : 1;
	uint64_t rtt_wr_11                    : 2;
	uint64_t dic_11                       : 2;
	uint64_t rtt_nom_11                   : 3;
	uint64_t reserved_48_63               : 16;
#endif
	} s;
	struct cvmx_dfm_modereg_params1_s     cn63xx;
	struct cvmx_dfm_modereg_params1_s     cn63xxp1;
	struct cvmx_dfm_modereg_params1_s     cn66xx;
};
typedef union cvmx_dfm_modereg_params1 cvmx_dfm_modereg_params1_t;

/**
 * cvmx_dfm_ops_cnt
 *
 * DFM_OPS_CNT  = Performance Counters
 *
 */
union cvmx_dfm_ops_cnt {
	uint64_t u64;
	struct cvmx_dfm_ops_cnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t opscnt                       : 64; /**< Performance Counter
                                                         64-bit counter that increments when the DDR3 data bus
                                                         is being used.  Before using, clear counter via
                                                         DFM_CONTROL.BWCNT
                                                           DRAM bus utilization = DFM_OPS_CNT/DFM_FCLK_CNT */
#else
	uint64_t opscnt                       : 64;
#endif
	} s;
	struct cvmx_dfm_ops_cnt_s             cn63xx;
	struct cvmx_dfm_ops_cnt_s             cn63xxp1;
	struct cvmx_dfm_ops_cnt_s             cn66xx;
};
typedef union cvmx_dfm_ops_cnt cvmx_dfm_ops_cnt_t;

/**
 * cvmx_dfm_phy_ctl
 *
 * DFM_PHY_CTL = DFM PHY Control
 *
 */
union cvmx_dfm_phy_ctl {
	uint64_t u64;
	struct cvmx_dfm_phy_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_15_63               : 49;
	uint64_t rx_always_on                 : 1;  /**< Disable dynamic DDR3 IO Rx power gating */
	uint64_t lv_mode                      : 1;  /**< Low Voltage Mode (1.35V) */
	uint64_t ck_tune1                     : 1;  /**< Clock Tune

                                                         NOTE: DFM UNUSED */
	uint64_t ck_dlyout1                   : 4;  /**< Clock delay out setting

                                                         NOTE: DFM UNUSED */
	uint64_t ck_tune0                     : 1;  /**< Clock Tune */
	uint64_t ck_dlyout0                   : 4;  /**< Clock delay out setting */
	uint64_t loopback                     : 1;  /**< Loopback enable */
	uint64_t loopback_pos                 : 1;  /**< Loopback pos mode */
	uint64_t ts_stagger                   : 1;  /**< TS Staggermode
                                                         This mode configures output drivers with 2-stage drive
                                                         strength to avoid undershoot issues on the bus when strong
                                                         drivers are suddenly turned on. When this mode is asserted,
                                                         Octeon will configure output drivers to be weak drivers
                                                         (60 ohm output impedance) at the first FCLK cycle, and
                                                         change drivers to the designated drive strengths specified
                                                         in DFM_COMP_CTL2 [CMD_CTL/CK_CTL/DQX_CTL] starting
                                                         at the following cycle */
#else
	uint64_t ts_stagger                   : 1;
	uint64_t loopback_pos                 : 1;
	uint64_t loopback                     : 1;
	uint64_t ck_dlyout0                   : 4;
	uint64_t ck_tune0                     : 1;
	uint64_t ck_dlyout1                   : 4;
	uint64_t ck_tune1                     : 1;
	uint64_t lv_mode                      : 1;
	uint64_t rx_always_on                 : 1;
	uint64_t reserved_15_63               : 49;
#endif
	} s;
	struct cvmx_dfm_phy_ctl_s             cn63xx;
	struct cvmx_dfm_phy_ctl_cn63xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_14_63               : 50;
	uint64_t lv_mode                      : 1;  /**< Low Voltage Mode (1.35V) */
	uint64_t ck_tune1                     : 1;  /**< Clock Tune

                                                         NOTE: DFM UNUSED */
	uint64_t ck_dlyout1                   : 4;  /**< Clock delay out setting

                                                         NOTE: DFM UNUSED */
	uint64_t ck_tune0                     : 1;  /**< Clock Tune */
	uint64_t ck_dlyout0                   : 4;  /**< Clock delay out setting */
	uint64_t loopback                     : 1;  /**< Loopback enable */
	uint64_t loopback_pos                 : 1;  /**< Loopback pos mode */
	uint64_t ts_stagger                   : 1;  /**< TS Staggermode
                                                         This mode configures output drivers with 2-stage drive
                                                         strength to avoid undershoot issues on the bus when strong
                                                         drivers are suddenly turned on. When this mode is asserted,
                                                         Octeon will configure output drivers to be weak drivers
                                                         (60 ohm output impedance) at the first FCLK cycle, and
                                                         change drivers to the designated drive strengths specified
                                                         in DFM_COMP_CTL2 [CMD_CTL/CK_CTL/DQX_CTL] starting
                                                         at the following cycle */
#else
	uint64_t ts_stagger                   : 1;
	uint64_t loopback_pos                 : 1;
	uint64_t loopback                     : 1;
	uint64_t ck_dlyout0                   : 4;
	uint64_t ck_tune0                     : 1;
	uint64_t ck_dlyout1                   : 4;
	uint64_t ck_tune1                     : 1;
	uint64_t lv_mode                      : 1;
	uint64_t reserved_14_63               : 50;
#endif
	} cn63xxp1;
	struct cvmx_dfm_phy_ctl_s             cn66xx;
};
typedef union cvmx_dfm_phy_ctl cvmx_dfm_phy_ctl_t;

/**
 * cvmx_dfm_reset_ctl
 *
 * Specify the RSL base addresses for the block
 *
 *
 * Notes:
 * DDR3RST - DDR3 DRAM parts have a new RESET#
 * pin that wasn't present in DDR2 parts. The
 * DDR3RST CSR field controls the assertion of
 * the new 6xxx pin that attaches to RESET#.
 * When DDR3RST is set, 6xxx asserts RESET#.
 * When DDR3RST is clear, 6xxx de-asserts
 * RESET#.
 *
 * DDR3RST is set on a cold reset. Warm and
 * soft chip resets do not affect the DDR3RST
 * value. Outside of cold reset, only software
 * CSR writes change the DDR3RST value.
 */
union cvmx_dfm_reset_ctl {
	uint64_t u64;
	struct cvmx_dfm_reset_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t ddr3psv                      : 1;  /**< Must be zero */
	uint64_t ddr3psoft                    : 1;  /**< Must be zero */
	uint64_t ddr3pwarm                    : 1;  /**< Must be zero */
	uint64_t ddr3rst                      : 1;  /**< Memory Reset
                                                         0 = Reset asserted
                                                         1 = Reset de-asserted */
#else
	uint64_t ddr3rst                      : 1;
	uint64_t ddr3pwarm                    : 1;
	uint64_t ddr3psoft                    : 1;
	uint64_t ddr3psv                      : 1;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_dfm_reset_ctl_s           cn63xx;
	struct cvmx_dfm_reset_ctl_s           cn63xxp1;
	struct cvmx_dfm_reset_ctl_s           cn66xx;
};
typedef union cvmx_dfm_reset_ctl cvmx_dfm_reset_ctl_t;

/**
 * cvmx_dfm_rlevel_ctl
 */
union cvmx_dfm_rlevel_ctl {
	uint64_t u64;
	struct cvmx_dfm_rlevel_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_22_63               : 42;
	uint64_t delay_unload_3               : 1;  /**< When set, unload the PHY silo one cycle later
                                                         during read-leveling if DFM_RLEVEL_RANKi[BYTE*<1:0>] = 3
                                                         DELAY_UNLOAD_3 should normally be set, particularly at higher speeds. */
	uint64_t delay_unload_2               : 1;  /**< When set, unload the PHY silo one cycle later
                                                         during read-leveling if DFM_RLEVEL_RANKi[BYTE*<1:0>] = 2
                                                         DELAY_UNLOAD_2 should normally not be set. */
	uint64_t delay_unload_1               : 1;  /**< When set, unload the PHY silo one cycle later
                                                         during read-leveling if DFM_RLEVEL_RANKi[BYTE*<1:0>] = 1
                                                         DELAY_UNLOAD_1 should normally not be set. */
	uint64_t delay_unload_0               : 1;  /**< When set, unload the PHY silo one cycle later
                                                         during read-leveling if DFM_RLEVEL_RANKi[BYTE*<1:0>] = 0
                                                         DELAY_UNLOAD_0 should normally not be set. */
	uint64_t bitmask                      : 8;  /**< Mask to select bit lanes on which read-leveling
                                                         feedback is returned when OR_DIS is set to 1 */
	uint64_t or_dis                       : 1;  /**< Disable or'ing of bits in a byte lane when computing
                                                         the read-leveling bitmask
                                                         OR_DIS should normally not be set. */
	uint64_t offset_en                    : 1;  /**< Use DFM_RLEVEL_CTL[OFFSET] to calibrate read
                                                         level dskew settings */
	uint64_t offset                       : 4;  /**< Pick final_setting-offset (if set) for the read level
                                                         deskew setting instead of the middle of the largest
                                                         contiguous sequence of 1's in the bitmask */
	uint64_t byte                         : 4;  /**< 0 <= BYTE <= 1
                                                         Byte index for which bitmask results are saved
                                                         in DFM_RLEVEL_DBG */
#else
	uint64_t byte                         : 4;
	uint64_t offset                       : 4;
	uint64_t offset_en                    : 1;
	uint64_t or_dis                       : 1;
	uint64_t bitmask                      : 8;
	uint64_t delay_unload_0               : 1;
	uint64_t delay_unload_1               : 1;
	uint64_t delay_unload_2               : 1;
	uint64_t delay_unload_3               : 1;
	uint64_t reserved_22_63               : 42;
#endif
	} s;
	struct cvmx_dfm_rlevel_ctl_s          cn63xx;
	struct cvmx_dfm_rlevel_ctl_cn63xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_9_63                : 55;
	uint64_t offset_en                    : 1;  /**< Use DFM_RLEVEL_CTL[OFFSET] to calibrate read
                                                         level dskew settings */
	uint64_t offset                       : 4;  /**< Pick final_setting-offset (if set) for the read level
                                                         deskew setting instead of the middle of the largest
                                                         contiguous sequence of 1's in the bitmask */
	uint64_t byte                         : 4;  /**< 0 <= BYTE <= 1
                                                         Byte index for which bitmask results are saved
                                                         in DFM_RLEVEL_DBG */
#else
	uint64_t byte                         : 4;
	uint64_t offset                       : 4;
	uint64_t offset_en                    : 1;
	uint64_t reserved_9_63                : 55;
#endif
	} cn63xxp1;
	struct cvmx_dfm_rlevel_ctl_s          cn66xx;
};
typedef union cvmx_dfm_rlevel_ctl cvmx_dfm_rlevel_ctl_t;

/**
 * cvmx_dfm_rlevel_dbg
 *
 * Notes:
 * A given read of DFM_RLEVEL_DBG returns the read-leveling pass/fail results for all possible
 * delay settings (i.e. the BITMASK) for only one byte in the last rank that the HW read-leveled.
 * DFM_RLEVEL_CTL[BYTE] selects the particular byte.
 * To get these pass/fail results for another different rank, you must run the hardware read-leveling
 * again. For example, it is possible to get the BITMASK results for every byte of every rank
 * if you run read-leveling separately for each rank, probing DFM_RLEVEL_DBG between each
 * read-leveling.
 */
union cvmx_dfm_rlevel_dbg {
	uint64_t u64;
	struct cvmx_dfm_rlevel_dbg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t bitmask                      : 64; /**< Bitmask generated during deskew settings sweep
                                                         BITMASK[n]=0 means deskew setting n failed
                                                         BITMASK[n]=1 means deskew setting n passed
                                                         for 0 <= n <= 63 */
#else
	uint64_t bitmask                      : 64;
#endif
	} s;
	struct cvmx_dfm_rlevel_dbg_s          cn63xx;
	struct cvmx_dfm_rlevel_dbg_s          cn63xxp1;
	struct cvmx_dfm_rlevel_dbg_s          cn66xx;
};
typedef union cvmx_dfm_rlevel_dbg cvmx_dfm_rlevel_dbg_t;

/**
 * cvmx_dfm_rlevel_rank#
 *
 * Notes:
 * This is TWO CSRs per DFM, one per each rank.
 *
 * Deskew setting is measured in units of 1/4 FCLK, so the above BYTE* values can range over 16 FCLKs.
 *
 * Each CSR is written by HW during a read-leveling sequence for the rank. (HW sets STATUS==3 after HW read-leveling completes for the rank.)
 * If HW is unable to find a match per DFM_RLEVEL_CTL[OFFSET_EN] and DFM_RLEVEL_CTL[OFFSET], then HW will set DFM_RLEVEL_RANKn[BYTE*<5:0>]
 * to 0.
 *
 * Each CSR may also be written by SW, but not while a read-leveling sequence is in progress. (HW sets STATUS==1 after a CSR write.)
 *
 * SW initiates a HW read-leveling sequence by programming DFM_RLEVEL_CTL and writing INIT_START=1 with SEQUENCE=1 in DFM_CONFIG.
 * See DFM_RLEVEL_CTL.
 */
union cvmx_dfm_rlevel_rankx {
	uint64_t u64;
	struct cvmx_dfm_rlevel_rankx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_56_63               : 8;
	uint64_t status                       : 2;  /**< Indicates status of the read-levelling and where
                                                         the BYTE* programmings in <35:0> came from:
                                                         0 = BYTE* values are their reset value
                                                         1 = BYTE* values were set via a CSR write to this register
                                                         2 = read-leveling sequence currently in progress (BYTE* values are unpredictable)
                                                         3 = BYTE* values came from a complete read-leveling sequence */
	uint64_t reserved_12_53               : 42;
	uint64_t byte1                        : 6;  /**< Deskew setting */
	uint64_t byte0                        : 6;  /**< Deskew setting */
#else
	uint64_t byte0                        : 6;
	uint64_t byte1                        : 6;
	uint64_t reserved_12_53               : 42;
	uint64_t status                       : 2;
	uint64_t reserved_56_63               : 8;
#endif
	} s;
	struct cvmx_dfm_rlevel_rankx_s        cn63xx;
	struct cvmx_dfm_rlevel_rankx_s        cn63xxp1;
	struct cvmx_dfm_rlevel_rankx_s        cn66xx;
};
typedef union cvmx_dfm_rlevel_rankx cvmx_dfm_rlevel_rankx_t;

/**
 * cvmx_dfm_rodt_mask
 *
 * DFM_RODT_MASK = DFM Read OnDieTermination mask
 * System designers may desire to terminate DQ/DQS/DM lines for higher frequency DDR operations
 * especially on a multi-rank system. DDR3 DQ/DM/DQS I/O's have built in
 * Termination resistor that can be turned on or off by the controller, after meeting tAOND and tAOF
 * timing requirements. Each Rank has its own ODT pin that fans out to all the memory parts
 * in that rank. System designers may prefer different combinations of ODT ON's for reads
 * into different ranks. Octeon supports full programmability by way of the mask register below.
 * Each Rank position has its own 8-bit programmable field.
 * When the controller does a read to that rank, it sets the 4 ODT pins to the MASK pins below.
 * For eg., When doing a read into Rank0, a system designer may desire to terminate the lines
 * with the resistor on Dimm0/Rank1. The mask RODT_D0_R0 would then be [00000010].
 * Octeon drives the appropriate mask values on the ODT pins by default. If this feature is not
 * required, write 0 in this register. Note that, as per the DDR3 specifications, the ODT pin
 * for the rank that is being read should always be 0.
 *
 * Notes:
 * - Notice that when there is only one rank, all valid fields must be zero.  This is because there is no
 * "other" rank to terminate lines for.  Read ODT is meant for multirank systems.
 * - For a two rank system and a read op to rank0: use RODT_D0_R0<1> to terminate lines on rank1.
 * - For a two rank system and a read op to rank1: use RODT_D0_R1<0> to terminate lines on rank0.
 * - Therefore, when a given RANK is selected, the RODT mask for that RANK is used.
 *
 * DFM always reads 128-bit words independently via one read CAS operation per word.
 * When a RODT mask bit is set, DFM asserts the OCTEON ODT output
 * pin(s) starting (CL - CWL) CK's after the read CAS operation. Then, OCTEON
 * normally continues to assert the ODT output pin(s) for 5+DFM_CONTROL[RODT_BPRCH] more CK's
 * - for a total of 6+DFM_CONTROL[RODT_BPRCH] CK's for the entire 128-bit read -
 * satisfying the 6 CK DDR3 ODTH8 requirements.
 *
 * But it is possible for OCTEON to issue two 128-bit reads separated by as few as
 * RtR = 4 or 5 (6 if DFM_CONTROL[RODT_BPRCH]=1) CK's. In that case, OCTEON asserts the ODT output pin(s)
 * for the RODT mask of the first 128-bit read for RtR CK's, then asserts
 * the ODT output pin(s) for the RODT mask of the second 128-bit read for 6+DFM_CONTROL[RODT_BPRCH] CK's
 * (or less if a third 128-bit read follows within 4 or 5 (or 6) CK's of this second 128-bit read).
 * Note that it may be necessary to force DFM to space back-to-back 128-bit reads
 * to different ranks apart by at least 6+DFM_CONTROL[RODT_BPRCH] CK's to prevent DDR3 ODTH8 violations.
 */
union cvmx_dfm_rodt_mask {
	uint64_t u64;
	struct cvmx_dfm_rodt_mask_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t rodt_d3_r1                   : 8;  /**< Must be zero. */
	uint64_t rodt_d3_r0                   : 8;  /**< Must be zero. */
	uint64_t rodt_d2_r1                   : 8;  /**< Must be zero. */
	uint64_t rodt_d2_r0                   : 8;  /**< Must be zero. */
	uint64_t rodt_d1_r1                   : 8;  /**< Must be zero. */
	uint64_t rodt_d1_r0                   : 8;  /**< Must be zero. */
	uint64_t rodt_d0_r1                   : 8;  /**< Read ODT mask RANK1
                                                         RODT_D0_R1<7:1> must be zero in all cases.
                                                         RODT_D0_R1<0> must also be zero if RANK_ENA is not set. */
	uint64_t rodt_d0_r0                   : 8;  /**< Read ODT mask RANK0
                                                         RODT_D0_R0<7:2,0> must be zero in all cases.
                                                         RODT_D0_R0<1> must also be zero if RANK_ENA is not set. */
#else
	uint64_t rodt_d0_r0                   : 8;
	uint64_t rodt_d0_r1                   : 8;
	uint64_t rodt_d1_r0                   : 8;
	uint64_t rodt_d1_r1                   : 8;
	uint64_t rodt_d2_r0                   : 8;
	uint64_t rodt_d2_r1                   : 8;
	uint64_t rodt_d3_r0                   : 8;
	uint64_t rodt_d3_r1                   : 8;
#endif
	} s;
	struct cvmx_dfm_rodt_mask_s           cn63xx;
	struct cvmx_dfm_rodt_mask_s           cn63xxp1;
	struct cvmx_dfm_rodt_mask_s           cn66xx;
};
typedef union cvmx_dfm_rodt_mask cvmx_dfm_rodt_mask_t;

/**
 * cvmx_dfm_slot_ctl0
 *
 * DFM_SLOT_CTL0 = DFM Slot Control0
 * This register is an assortment of various control fields needed by the memory controller
 *
 * Notes:
 * HW will update this register if SW has not previously written to it and when any of DFM_RLEVEL_RANKn, DFM_WLEVEL_RANKn, DFM_CONTROL and
 * DFM_MODEREG_PARAMS0 change.Ideally, this register should only be read after DFM has been initialized and DFM_RLEVEL_RANKn, DFM_WLEVEL_RANKn
 * have valid data.
 * R2W_INIT has 1 extra CK cycle built in for odt settling/channel turnaround time.
 */
union cvmx_dfm_slot_ctl0 {
	uint64_t u64;
	struct cvmx_dfm_slot_ctl0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_24_63               : 40;
	uint64_t w2w_init                     : 6;  /**< Write-to-write spacing control
                                                         for back to back accesses to the same rank and dimm */
	uint64_t w2r_init                     : 6;  /**< Write-to-read spacing control
                                                         for back to back accesses to the same rank and dimm */
	uint64_t r2w_init                     : 6;  /**< Read-to-write spacing control
                                                         for back to back accesses to the same rank and dimm */
	uint64_t r2r_init                     : 6;  /**< Read-to-read spacing control
                                                         for back to back accesses to the same rank and dimm */
#else
	uint64_t r2r_init                     : 6;
	uint64_t r2w_init                     : 6;
	uint64_t w2r_init                     : 6;
	uint64_t w2w_init                     : 6;
	uint64_t reserved_24_63               : 40;
#endif
	} s;
	struct cvmx_dfm_slot_ctl0_s           cn63xx;
	struct cvmx_dfm_slot_ctl0_s           cn63xxp1;
	struct cvmx_dfm_slot_ctl0_s           cn66xx;
};
typedef union cvmx_dfm_slot_ctl0 cvmx_dfm_slot_ctl0_t;

/**
 * cvmx_dfm_slot_ctl1
 *
 * DFM_SLOT_CTL1 = DFM Slot Control1
 * This register is an assortment of various control fields needed by the memory controller
 *
 * Notes:
 * HW will update this register if SW has not previously written to it and when any of DFM_RLEVEL_RANKn, DFM_WLEVEL_RANKn, DFM_CONTROL and
 * DFM_MODEREG_PARAMS0 change.Ideally, this register should only be read after DFM has been initialized and DFM_RLEVEL_RANKn, DFM_WLEVEL_RANKn
 * have valid data.
 * R2W_XRANK_INIT, W2R_XRANK_INIT have 1 extra CK cycle built in for odt settling/channel turnaround time.
 */
union cvmx_dfm_slot_ctl1 {
	uint64_t u64;
	struct cvmx_dfm_slot_ctl1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_24_63               : 40;
	uint64_t w2w_xrank_init               : 6;  /**< Write-to-write spacing control
                                                         for back to back accesses across ranks of the same dimm */
	uint64_t w2r_xrank_init               : 6;  /**< Write-to-read spacing control
                                                         for back to back accesses across ranks of the same dimm */
	uint64_t r2w_xrank_init               : 6;  /**< Read-to-write spacing control
                                                         for back to back accesses across ranks of the same dimm */
	uint64_t r2r_xrank_init               : 6;  /**< Read-to-read spacing control
                                                         for back to back accesses across ranks of the same dimm */
#else
	uint64_t r2r_xrank_init               : 6;
	uint64_t r2w_xrank_init               : 6;
	uint64_t w2r_xrank_init               : 6;
	uint64_t w2w_xrank_init               : 6;
	uint64_t reserved_24_63               : 40;
#endif
	} s;
	struct cvmx_dfm_slot_ctl1_s           cn63xx;
	struct cvmx_dfm_slot_ctl1_s           cn63xxp1;
	struct cvmx_dfm_slot_ctl1_s           cn66xx;
};
typedef union cvmx_dfm_slot_ctl1 cvmx_dfm_slot_ctl1_t;

/**
 * cvmx_dfm_timing_params0
 */
union cvmx_dfm_timing_params0 {
	uint64_t u64;
	struct cvmx_dfm_timing_params0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_47_63               : 17;
	uint64_t trp_ext                      : 1;  /**< Indicates tRP constraints.
                                                         Set [TRP_EXT[0:0], TRP[3:0]] (CSR field) = RNDUP[tRP(ns)/tCYC(ns)]
                                                         + (RNDUP[tRTP(ns)/tCYC(ns)])-4)-1,
                                                         where tRP, tRTP are from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP tRP=10-15ns
                                                         TYP tRTP=max(4nCK, 7.5ns) */
	uint64_t tcksre                       : 4;  /**< Indicates tCKSRE constraints.
                                                         Set TCKSRE (CSR field) = RNDUP[tCKSRE(ns)/tCYC(ns)]-1,
                                                         where tCKSRE is from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP=max(5nCK, 10ns) */
	uint64_t trp                          : 4;  /**< Indicates tRP constraints.
                                                         Set [TRP_EXT[0:0], TRP[3:0]] (CSR field) = RNDUP[tRP(ns)/tCYC(ns)]
                                                         + (RNDUP[tRTP(ns)/tCYC(ns)])-4)-1,
                                                         where tRP, tRTP are from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP tRP=10-15ns
                                                         TYP tRTP=max(4nCK, 7.5ns) */
	uint64_t tzqinit                      : 4;  /**< Indicates tZQINIT constraints.
                                                         Set TZQINIT (CSR field) = RNDUP[tZQINIT(ns)/(256*tCYC(ns))],
                                                         where tZQINIT is from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP=2 (equivalent to 512) */
	uint64_t tdllk                        : 4;  /**< Indicates tDLLk constraints.
                                                         Set TDLLK (CSR field) = RNDUP[tDLLk(ns)/(256*tCYC(ns))],
                                                         where tDLLk is from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP=2 (equivalent to 512)
                                                         This parameter is used in self-refresh exit
                                                         and assumed to be greater than tRFC */
	uint64_t tmod                         : 4;  /**< Indicates tMOD constraints.
                                                         Set TMOD (CSR field) = RNDUP[tMOD(ns)/tCYC(ns)]-1,
                                                         where tMOD is from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP=max(12nCK, 15ns) */
	uint64_t tmrd                         : 4;  /**< Indicates tMRD constraints.
                                                         Set TMRD (CSR field) = RNDUP[tMRD(ns)/tCYC(ns)]-1,
                                                         where tMRD is from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP=4nCK */
	uint64_t txpr                         : 4;  /**< Indicates tXPR constraints.
                                                         Set TXPR (CSR field) = RNDUP[tXPR(ns)/(16*tCYC(ns))],
                                                         where tXPR is from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP=max(5nCK, tRFC+10ns) */
	uint64_t tcke                         : 4;  /**< Indicates tCKE constraints.
                                                         Set TCKE (CSR field) = RNDUP[tCKE(ns)/tCYC(ns)]-1,
                                                         where tCKE is from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP=max(3nCK, 7.5/5.625/5.625/5ns) */
	uint64_t tzqcs                        : 4;  /**< Indicates tZQCS constraints.
                                                         Set TZQCS (CSR field) = RNDUP[tZQCS(ns)/(16*tCYC(ns))],
                                                         where tZQCS is from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP=4 (equivalent to 64) */
	uint64_t tckeon                       : 10; /**< Reserved. Should be written to zero. */
#else
	uint64_t tckeon                       : 10;
	uint64_t tzqcs                        : 4;
	uint64_t tcke                         : 4;
	uint64_t txpr                         : 4;
	uint64_t tmrd                         : 4;
	uint64_t tmod                         : 4;
	uint64_t tdllk                        : 4;
	uint64_t tzqinit                      : 4;
	uint64_t trp                          : 4;
	uint64_t tcksre                       : 4;
	uint64_t trp_ext                      : 1;
	uint64_t reserved_47_63               : 17;
#endif
	} s;
	struct cvmx_dfm_timing_params0_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_47_63               : 17;
	uint64_t trp_ext                      : 1;  /**< Indicates tRP constraints.
                                                         Set [TRP_EXT[0:0], TRP[3:0]] (CSR field) = RNDUP[tRP(ns)/tCYC(ns)]
                                                         + (RNDUP[tRTP(ns)/tCYC(ns)])-4)-1,
                                                         where tRP, tRTP are from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP tRP=10-15ns
                                                         TYP tRTP=max(4nCK, 7.5ns) */
	uint64_t tcksre                       : 4;  /**< Indicates tCKSRE constraints.
                                                         Set TCKSRE (CSR field) = RNDUP[tCKSRE(ns)/tCYC(ns)]-1,
                                                         where tCKSRE is from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP=max(5nCK, 10ns) */
	uint64_t trp                          : 4;  /**< Indicates tRP constraints.
                                                         Set [TRP_EXT[0:0], TRP[3:0]] (CSR field) = RNDUP[tRP(ns)/tCYC(ns)]
                                                         + (RNDUP[tRTP(ns)/tCYC(ns)])-4)-1,
                                                         where tRP, tRTP are from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP tRP=10-15ns
                                                         TYP tRTP=max(4nCK, 7.5ns) */
	uint64_t tzqinit                      : 4;  /**< Indicates tZQINIT constraints.
                                                         Set TZQINIT (CSR field) = RNDUP[tZQINIT(ns)/(256*tCYC(ns))],
                                                         where tZQINIT is from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP=2 (equivalent to 512) */
	uint64_t tdllk                        : 4;  /**< Indicates tDLLk constraints.
                                                         Set TDLLK (CSR field) = RNDUP[tDLLk(ns)/(256*tCYC(ns))],
                                                         where tDLLk is from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP=2 (equivalent to 512)
                                                         This parameter is used in self-refresh exit
                                                         and assumed to be greater than tRFC */
	uint64_t tmod                         : 4;  /**< Indicates tMOD constraints.
                                                         Set TMOD (CSR field) = RNDUP[tMOD(ns)/tCYC(ns)]-1,
                                                         where tMOD is from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP=max(12nCK, 15ns) */
	uint64_t tmrd                         : 4;  /**< Indicates tMRD constraints.
                                                         Set TMRD (CSR field) = RNDUP[tMRD(ns)/tCYC(ns)]-1,
                                                         where tMRD is from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP=4nCK */
	uint64_t txpr                         : 4;  /**< Indicates tXPR constraints.
                                                         Set TXPR (CSR field) = RNDUP[tXPR(ns)/(16*tCYC(ns))],
                                                         where tXPR is from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP=max(5nCK, tRFC+10ns) */
	uint64_t tcke                         : 4;  /**< Indicates tCKE constraints.
                                                         Set TCKE (CSR field) = RNDUP[tCKE(ns)/tCYC(ns)]-1,
                                                         where tCKE is from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP=max(3nCK, 7.5/5.625/5.625/5ns) */
	uint64_t tzqcs                        : 4;  /**< Indicates tZQCS constraints.
                                                         Set TZQCS (CSR field) = RNDUP[tZQCS(ns)/(16*tCYC(ns))],
                                                         where tZQCS is from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP=4 (equivalent to 64) */
	uint64_t reserved_0_9                 : 10;
#else
	uint64_t reserved_0_9                 : 10;
	uint64_t tzqcs                        : 4;
	uint64_t tcke                         : 4;
	uint64_t txpr                         : 4;
	uint64_t tmrd                         : 4;
	uint64_t tmod                         : 4;
	uint64_t tdllk                        : 4;
	uint64_t tzqinit                      : 4;
	uint64_t trp                          : 4;
	uint64_t tcksre                       : 4;
	uint64_t trp_ext                      : 1;
	uint64_t reserved_47_63               : 17;
#endif
	} cn63xx;
	struct cvmx_dfm_timing_params0_cn63xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_46_63               : 18;
	uint64_t tcksre                       : 4;  /**< Indicates tCKSRE constraints.
                                                         Set TCKSRE (CSR field) = RNDUP[tCKSRE(ns)/tCYC(ns)]-1,
                                                         where tCKSRE is from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP=max(5nCK, 10ns) */
	uint64_t trp                          : 4;  /**< Indicates tRP constraints.
                                                         Set TRP (CSR field) = RNDUP[tRP(ns)/tCYC(ns)]
                                                         + (RNDUP[tRTP(ns)/tCYC(ns)])-4)-1,
                                                         where tRP, tRTP are from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP tRP=10-15ns
                                                         TYP tRTP=max(4nCK, 7.5ns) */
	uint64_t tzqinit                      : 4;  /**< Indicates tZQINIT constraints.
                                                         Set TZQINIT (CSR field) = RNDUP[tZQINIT(ns)/(256*tCYC(ns))],
                                                         where tZQINIT is from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP=2 (equivalent to 512) */
	uint64_t tdllk                        : 4;  /**< Indicates tDLLk constraints.
                                                         Set TDLLK (CSR field) = RNDUP[tDLLk(ns)/(256*tCYC(ns))],
                                                         where tDLLk is from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP=2 (equivalent to 512)
                                                         This parameter is used in self-refresh exit
                                                         and assumed to be greater than tRFC */
	uint64_t tmod                         : 4;  /**< Indicates tMOD constraints.
                                                         Set TMOD (CSR field) = RNDUP[tMOD(ns)/tCYC(ns)]-1,
                                                         where tMOD is from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP=max(12nCK, 15ns) */
	uint64_t tmrd                         : 4;  /**< Indicates tMRD constraints.
                                                         Set TMRD (CSR field) = RNDUP[tMRD(ns)/tCYC(ns)]-1,
                                                         where tMRD is from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP=4nCK */
	uint64_t txpr                         : 4;  /**< Indicates tXPR constraints.
                                                         Set TXPR (CSR field) = RNDUP[tXPR(ns)/(16*tCYC(ns))],
                                                         where tXPR is from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP=max(5nCK, tRFC+10ns) */
	uint64_t tcke                         : 4;  /**< Indicates tCKE constraints.
                                                         Set TCKE (CSR field) = RNDUP[tCKE(ns)/tCYC(ns)]-1,
                                                         where tCKE is from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP=max(3nCK, 7.5/5.625/5.625/5ns) */
	uint64_t tzqcs                        : 4;  /**< Indicates tZQCS constraints.
                                                         Set TZQCS (CSR field) = RNDUP[tZQCS(ns)/(16*tCYC(ns))],
                                                         where tZQCS is from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP=4 (equivalent to 64) */
	uint64_t tckeon                       : 10; /**< Reserved. Should be written to zero. */
#else
	uint64_t tckeon                       : 10;
	uint64_t tzqcs                        : 4;
	uint64_t tcke                         : 4;
	uint64_t txpr                         : 4;
	uint64_t tmrd                         : 4;
	uint64_t tmod                         : 4;
	uint64_t tdllk                        : 4;
	uint64_t tzqinit                      : 4;
	uint64_t trp                          : 4;
	uint64_t tcksre                       : 4;
	uint64_t reserved_46_63               : 18;
#endif
	} cn63xxp1;
	struct cvmx_dfm_timing_params0_cn63xx cn66xx;
};
typedef union cvmx_dfm_timing_params0 cvmx_dfm_timing_params0_t;

/**
 * cvmx_dfm_timing_params1
 */
union cvmx_dfm_timing_params1 {
	uint64_t u64;
	struct cvmx_dfm_timing_params1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_47_63               : 17;
	uint64_t tras_ext                     : 1;  /**< Indicates tRAS constraints.
                                                         Set [TRAS_EXT[0:0], TRAS[4:0]] (CSR field) = RNDUP[tRAS(ns)/tCYC(ns)]-1,
                                                         where tRAS is from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP=35ns-9*tREFI
                                                             - 000000: RESERVED
                                                             - 000001: 2 tCYC
                                                             - 000010: 3 tCYC
                                                             - ...
                                                             - 111111: 64 tCYC */
	uint64_t txpdll                       : 5;  /**< Indicates tXPDLL constraints.
                                                         Set TXPDLL (CSR field) = RNDUP[tXPDLL(ns)/tCYC(ns)]-1,
                                                         where tXPDLL is from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP=max(10nCK, 24ns) */
	uint64_t tfaw                         : 5;  /**< Indicates tFAW constraints.
                                                         Set TFAW (CSR field) = RNDUP[tFAW(ns)/(4*tCYC(ns))],
                                                         where tFAW is from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP=30-40ns */
	uint64_t twldqsen                     : 4;  /**< Indicates tWLDQSEN constraints.
                                                         Set TWLDQSEN (CSR field) = RNDUP[tWLDQSEN(ns)/(4*tCYC(ns))],
                                                         where tWLDQSEN is from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP=max(25nCK) */
	uint64_t twlmrd                       : 4;  /**< Indicates tWLMRD constraints.
                                                         Set TWLMRD (CSR field) = RNDUP[tWLMRD(ns)/(4*tCYC(ns))],
                                                         where tWLMRD is from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP=max(40nCK) */
	uint64_t txp                          : 3;  /**< Indicates tXP constraints.
                                                         Set TXP (CSR field) = RNDUP[tXP(ns)/tCYC(ns)]-1,
                                                         where tXP is from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP=max(3nCK, 7.5ns) */
	uint64_t trrd                         : 3;  /**< Indicates tRRD constraints.
                                                         Set TRRD (CSR field) = RNDUP[tRRD(ns)/tCYC(ns)]-2,
                                                         where tRRD is from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP=max(4nCK, 10ns)
                                                            - 000: RESERVED
                                                            - 001: 3 tCYC
                                                            - ...
                                                            - 110: 8 tCYC
                                                            - 111: 9 tCYC */
	uint64_t trfc                         : 5;  /**< Indicates tRFC constraints.
                                                         Set TRFC (CSR field) = RNDUP[tRFC(ns)/(8*tCYC(ns))],
                                                         where tRFC is from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP=90-350ns
                                                              - 00000: RESERVED
                                                              - 00001: 8 tCYC
                                                              - 00010: 16 tCYC
                                                              - 00011: 24 tCYC
                                                              - 00100: 32 tCYC
                                                              - ...
                                                              - 11110: 240 tCYC
                                                              - 11111: 248 tCYC */
	uint64_t twtr                         : 4;  /**< Indicates tWTR constraints.
                                                         Set TWTR (CSR field) = RNDUP[tWTR(ns)/tCYC(ns)]-1,
                                                         where tWTR is from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP=max(4nCK, 7.5ns)
                                                             - 0000: RESERVED
                                                             - 0001: 2
                                                             - ...
                                                             - 0111: 8
                                                             - 1000-1111: RESERVED */
	uint64_t trcd                         : 4;  /**< Indicates tRCD constraints.
                                                         Set TRCD (CSR field) = RNDUP[tRCD(ns)/tCYC(ns)],
                                                         where tRCD is from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP=10-15ns
                                                             - 0000: RESERVED
                                                             - 0001: 2 (2 is the smallest value allowed)
                                                             - 0002: 2
                                                             - ...
                                                             - 1001: 9
                                                             - 1010-1111: RESERVED
                                                         In 2T mode, make this register TRCD-1, not going
                                                         below 2. */
	uint64_t tras                         : 5;  /**< Indicates tRAS constraints.
                                                         Set [TRAS_EXT[0:0], TRAS[4:0]] (CSR field) = RNDUP[tRAS(ns)/tCYC(ns)]-1,
                                                         where tRAS is from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP=35ns-9*tREFI
                                                             - 000000: RESERVED
                                                             - 000001: 2 tCYC
                                                             - 000010: 3 tCYC
                                                             - ...
                                                             - 111111: 64 tCYC */
	uint64_t tmprr                        : 4;  /**< Indicates tMPRR constraints.
                                                         Set TMPRR (CSR field) = RNDUP[tMPRR(ns)/tCYC(ns)]-1,
                                                         where tMPRR is from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP=1nCK */
#else
	uint64_t tmprr                        : 4;
	uint64_t tras                         : 5;
	uint64_t trcd                         : 4;
	uint64_t twtr                         : 4;
	uint64_t trfc                         : 5;
	uint64_t trrd                         : 3;
	uint64_t txp                          : 3;
	uint64_t twlmrd                       : 4;
	uint64_t twldqsen                     : 4;
	uint64_t tfaw                         : 5;
	uint64_t txpdll                       : 5;
	uint64_t tras_ext                     : 1;
	uint64_t reserved_47_63               : 17;
#endif
	} s;
	struct cvmx_dfm_timing_params1_s      cn63xx;
	struct cvmx_dfm_timing_params1_cn63xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_46_63               : 18;
	uint64_t txpdll                       : 5;  /**< Indicates tXPDLL constraints.
                                                         Set TXPDLL (CSR field) = RNDUP[tXPDLL(ns)/tCYC(ns)]-1,
                                                         where tXPDLL is from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP=max(10nCK, 24ns) */
	uint64_t tfaw                         : 5;  /**< Indicates tFAW constraints.
                                                         Set TFAW (CSR field) = RNDUP[tFAW(ns)/(4*tCYC(ns))],
                                                         where tFAW is from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP=30-40ns */
	uint64_t twldqsen                     : 4;  /**< Indicates tWLDQSEN constraints.
                                                         Set TWLDQSEN (CSR field) = RNDUP[tWLDQSEN(ns)/(4*tCYC(ns))],
                                                         where tWLDQSEN is from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP=max(25nCK) */
	uint64_t twlmrd                       : 4;  /**< Indicates tWLMRD constraints.
                                                         Set TWLMRD (CSR field) = RNDUP[tWLMRD(ns)/(4*tCYC(ns))],
                                                         where tWLMRD is from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP=max(40nCK) */
	uint64_t txp                          : 3;  /**< Indicates tXP constraints.
                                                         Set TXP (CSR field) = RNDUP[tXP(ns)/tCYC(ns)]-1,
                                                         where tXP is from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP=max(3nCK, 7.5ns) */
	uint64_t trrd                         : 3;  /**< Indicates tRRD constraints.
                                                         Set TRRD (CSR field) = RNDUP[tRRD(ns)/tCYC(ns)]-2,
                                                         where tRRD is from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP=max(4nCK, 10ns)
                                                            - 000: RESERVED
                                                            - 001: 3 tCYC
                                                            - ...
                                                            - 110: 8 tCYC
                                                            - 111: 9 tCYC */
	uint64_t trfc                         : 5;  /**< Indicates tRFC constraints.
                                                         Set TRFC (CSR field) = RNDUP[tRFC(ns)/(8*tCYC(ns))],
                                                         where tRFC is from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP=90-350ns
                                                              - 00000: RESERVED
                                                              - 00001: 8 tCYC
                                                              - 00010: 16 tCYC
                                                              - 00011: 24 tCYC
                                                              - 00100: 32 tCYC
                                                              - ...
                                                              - 11110: 240 tCYC
                                                              - 11111: 248 tCYC */
	uint64_t twtr                         : 4;  /**< Indicates tWTR constraints.
                                                         Set TWTR (CSR field) = RNDUP[tWTR(ns)/tCYC(ns)]-1,
                                                         where tWTR is from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP=max(4nCK, 7.5ns)
                                                             - 0000: RESERVED
                                                             - 0001: 2
                                                             - ...
                                                             - 0111: 8
                                                             - 1000-1111: RESERVED */
	uint64_t trcd                         : 4;  /**< Indicates tRCD constraints.
                                                         Set TRCD (CSR field) = RNDUP[tRCD(ns)/tCYC(ns)],
                                                         where tRCD is from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP=10-15ns
                                                             - 0000: RESERVED
                                                             - 0001: 2 (2 is the smallest value allowed)
                                                             - 0002: 2
                                                             - ...
                                                             - 1001: 9
                                                             - 1010-1111: RESERVED
                                                         In 2T mode, make this register TRCD-1, not going
                                                         below 2. */
	uint64_t tras                         : 5;  /**< Indicates tRAS constraints.
                                                         Set TRAS (CSR field) = RNDUP[tRAS(ns)/tCYC(ns)]-1,
                                                         where tRAS is from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP=35ns-9*tREFI
                                                             - 00000: RESERVED
                                                             - 00001: 2 tCYC
                                                             - 00010: 3 tCYC
                                                             - ...
                                                             - 11111: 32 tCYC */
	uint64_t tmprr                        : 4;  /**< Indicates tMPRR constraints.
                                                         Set TMPRR (CSR field) = RNDUP[tMPRR(ns)/tCYC(ns)]-1,
                                                         where tMPRR is from the DDR3 spec, and tCYC(ns)
                                                         is the DDR clock frequency (not data rate).
                                                         TYP=1nCK */
#else
	uint64_t tmprr                        : 4;
	uint64_t tras                         : 5;
	uint64_t trcd                         : 4;
	uint64_t twtr                         : 4;
	uint64_t trfc                         : 5;
	uint64_t trrd                         : 3;
	uint64_t txp                          : 3;
	uint64_t twlmrd                       : 4;
	uint64_t twldqsen                     : 4;
	uint64_t tfaw                         : 5;
	uint64_t txpdll                       : 5;
	uint64_t reserved_46_63               : 18;
#endif
	} cn63xxp1;
	struct cvmx_dfm_timing_params1_s      cn66xx;
};
typedef union cvmx_dfm_timing_params1 cvmx_dfm_timing_params1_t;

/**
 * cvmx_dfm_wlevel_ctl
 */
union cvmx_dfm_wlevel_ctl {
	uint64_t u64;
	struct cvmx_dfm_wlevel_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_22_63               : 42;
	uint64_t rtt_nom                      : 3;  /**< RTT_NOM
                                                         DFM writes a decoded value to MR1[Rtt_Nom] of the rank during
                                                         write leveling. Per JEDEC DDR3 specifications,
                                                         only values MR1[Rtt_Nom] = 1 (RQZ/4), 2 (RQZ/2), or 3 (RQZ/6)
                                                         are allowed during write leveling with output buffer enabled.
                                                         000 : DFM writes 001 (RZQ/4)   to MR1[Rtt_Nom]
                                                         001 : DFM writes 010 (RZQ/2)   to MR1[Rtt_Nom]
                                                         010 : DFM writes 011 (RZQ/6)   to MR1[Rtt_Nom]
                                                         011 : DFM writes 100 (RZQ/12)  to MR1[Rtt_Nom]
                                                         100 : DFM writes 101 (RZQ/8)   to MR1[Rtt_Nom]
                                                         101 : DFM writes 110 (Rsvd)    to MR1[Rtt_Nom]
                                                         110 : DFM writes 111 (Rsvd)    to  MR1[Rtt_Nom]
                                                         111 : DFM writes 000 (Disabled) to MR1[Rtt_Nom] */
	uint64_t bitmask                      : 8;  /**< Mask to select bit lanes on which write-leveling
                                                         feedback is returned when OR_DIS is set to 1 */
	uint64_t or_dis                       : 1;  /**< Disable or'ing of bits in a byte lane when computing
                                                         the write-leveling bitmask */
	uint64_t sset                         : 1;  /**< Run write-leveling on the current setting only. */
	uint64_t lanemask                     : 9;  /**< One-hot mask to select byte lane to be leveled by
                                                         the write-leveling sequence
                                                         Used with x16 parts where the upper and lower byte
                                                         lanes need to be leveled independently
                                                         LANEMASK<8:2> must be zero. */
#else
	uint64_t lanemask                     : 9;
	uint64_t sset                         : 1;
	uint64_t or_dis                       : 1;
	uint64_t bitmask                      : 8;
	uint64_t rtt_nom                      : 3;
	uint64_t reserved_22_63               : 42;
#endif
	} s;
	struct cvmx_dfm_wlevel_ctl_s          cn63xx;
	struct cvmx_dfm_wlevel_ctl_cn63xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_10_63               : 54;
	uint64_t sset                         : 1;  /**< Run write-leveling on the current setting only. */
	uint64_t lanemask                     : 9;  /**< One-hot mask to select byte lane to be leveled by
                                                         the write-leveling sequence
                                                         Used with x16 parts where the upper and lower byte
                                                         lanes need to be leveled independently
                                                         LANEMASK<8:2> must be zero. */
#else
	uint64_t lanemask                     : 9;
	uint64_t sset                         : 1;
	uint64_t reserved_10_63               : 54;
#endif
	} cn63xxp1;
	struct cvmx_dfm_wlevel_ctl_s          cn66xx;
};
typedef union cvmx_dfm_wlevel_ctl cvmx_dfm_wlevel_ctl_t;

/**
 * cvmx_dfm_wlevel_dbg
 *
 * Notes:
 * A given write of DFM_WLEVEL_DBG returns the write-leveling pass/fail results for all possible
 * delay settings (i.e. the BITMASK) for only one byte in the last rank that the HW write-leveled.
 * DFM_WLEVEL_DBG[BYTE] selects the particular byte.
 * To get these pass/fail results for another different rank, you must run the hardware write-leveling
 * again. For example, it is possible to get the BITMASK results for every byte of every rank
 * if you run write-leveling separately for each rank, probing DFM_WLEVEL_DBG between each
 * write-leveling.
 */
union cvmx_dfm_wlevel_dbg {
	uint64_t u64;
	struct cvmx_dfm_wlevel_dbg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_12_63               : 52;
	uint64_t bitmask                      : 8;  /**< Bitmask generated during deskew settings sweep
                                                         if DFM_WLEVEL_CTL[SSET]=0
                                                           BITMASK[n]=0 means deskew setting n failed
                                                           BITMASK[n]=1 means deskew setting n passed
                                                           for 0 <= n <= 7
                                                           BITMASK contains the first 8 results of the total 16
                                                           collected by DFM during the write-leveling sequence
                                                         else if DFM_WLEVEL_CTL[SSET]=1
                                                           BITMASK[0]=0 means curr deskew setting failed
                                                           BITMASK[0]=1 means curr deskew setting passed */
	uint64_t byte                         : 4;  /**< 0 <= BYTE <= 8 */
#else
	uint64_t byte                         : 4;
	uint64_t bitmask                      : 8;
	uint64_t reserved_12_63               : 52;
#endif
	} s;
	struct cvmx_dfm_wlevel_dbg_s          cn63xx;
	struct cvmx_dfm_wlevel_dbg_s          cn63xxp1;
	struct cvmx_dfm_wlevel_dbg_s          cn66xx;
};
typedef union cvmx_dfm_wlevel_dbg cvmx_dfm_wlevel_dbg_t;

/**
 * cvmx_dfm_wlevel_rank#
 *
 * Notes:
 * This is TWO CSRs per DFM, one per each rank. (front bunk/back bunk)
 *
 * Deskew setting is measured in units of 1/8 FCLK, so the above BYTE* values can range over 4 FCLKs.
 *
 * Assuming DFM_WLEVEL_CTL[SSET]=0, the BYTE*<2:0> values are not used during write-leveling, and
 * they are over-written by the hardware as part of the write-leveling sequence. (HW sets STATUS==3
 * after HW write-leveling completes for the rank). SW needs to set BYTE*<4:3> bits.
 *
 * Each CSR may also be written by SW, but not while a write-leveling sequence is in progress. (HW sets STATUS==1 after a CSR write.)
 *
 * SW initiates a HW write-leveling sequence by programming DFM_WLEVEL_CTL and writing RANKMASK and INIT_START=1 with SEQUENCE=6 in DFM_CONFIG.
 * DFM will then step through and accumulate write leveling results for 8 unique delay settings (twice), starting at a delay of
 * DFM_WLEVEL_RANKn[BYTE*<4:3>]*8 CK increasing by 1/8 CK each setting. HW will then set DFM_WLEVEL_RANKn[BYTE*<2:0>] to indicate the
 * first write leveling result of '1' that followed a reslt of '0' during the sequence by searching for a '1100' pattern in the generated
 * bitmask, except that DFM will always write DFM_WLEVEL_RANKn[BYTE*<0>]=0. If HW is unable to find a match for a '1100' pattern, then HW will
 * set DFM_WLEVEL_RANKn[BYTE*<2:0>] to 4.
 * See DFM_WLEVEL_CTL.
 */
union cvmx_dfm_wlevel_rankx {
	uint64_t u64;
	struct cvmx_dfm_wlevel_rankx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_47_63               : 17;
	uint64_t status                       : 2;  /**< Indicates status of the write-leveling and where
                                                         the BYTE* programmings in <44:0> came from:
                                                         0 = BYTE* values are their reset value
                                                         1 = BYTE* values were set via a CSR write to this register
                                                         2 = write-leveling sequence currently in progress (BYTE* values are unpredictable)
                                                         3 = BYTE* values came from a complete write-leveling sequence, irrespective of
                                                             which lanes are masked via DFM_WLEVEL_CTL[LANEMASK] */
	uint64_t reserved_10_44               : 35;
	uint64_t byte1                        : 5;  /**< Deskew setting
                                                         Bit 0 of BYTE1 must be zero during normal operation */
	uint64_t byte0                        : 5;  /**< Deskew setting
                                                         Bit 0 of BYTE0 must be zero during normal operation */
#else
	uint64_t byte0                        : 5;
	uint64_t byte1                        : 5;
	uint64_t reserved_10_44               : 35;
	uint64_t status                       : 2;
	uint64_t reserved_47_63               : 17;
#endif
	} s;
	struct cvmx_dfm_wlevel_rankx_s        cn63xx;
	struct cvmx_dfm_wlevel_rankx_s        cn63xxp1;
	struct cvmx_dfm_wlevel_rankx_s        cn66xx;
};
typedef union cvmx_dfm_wlevel_rankx cvmx_dfm_wlevel_rankx_t;

/**
 * cvmx_dfm_wodt_mask
 *
 * DFM_WODT_MASK = DFM Write OnDieTermination mask
 * System designers may desire to terminate DQ/DQS/DM lines for higher frequency DDR operations
 * especially on a multi-rank system. DDR3 DQ/DM/DQS I/O's have built in
 * Termination resistor that can be turned on or off by the controller, after meeting tAOND and tAOF
 * timing requirements. Each Rank has its own ODT pin that fans out to all the memory parts
 * in that rank. System designers may prefer different combinations of ODT ON's for writes
 * into different ranks. Octeon supports full programmability by way of the mask register below.
 * Each Rank position has its own 8-bit programmable field.
 * When the controller does a write to that rank, it sets the 4 ODT pins to the MASK pins below.
 * For eg., When doing a write into Rank0, a system designer may desire to terminate the lines
 * with the resistor on Dimm0/Rank1. The mask WODT_D0_R0 would then be [00000010].
 * Octeon drives the appropriate mask values on the ODT pins by default. If this feature is not
 * required, write 0 in this register.
 *
 * Notes:
 * - DFM_WODT_MASK functions a little differently than DFM_RODT_MASK.  While, in DFM_RODT_MASK, the other
 * rank(s) are ODT-ed, in DFM_WODT_MASK, the rank in which the write CAS is issued can be ODT-ed as well.
 * - For a two rank system and a write op to rank0: use RODT_D0_R0<1:0> to terminate lines on rank1 and/or rank0.
 * - For a two rank system and a write op to rank1: use RODT_D0_R1<1:0> to terminate lines on rank1 and/or rank0.
 * - When a given RANK is selected, the WODT mask for that RANK is used.
 *
 * DFM always writes 128-bit words independently via one write CAS operation per word.
 * When a WODT mask bit is set, DFM asserts the OCTEON ODT output pin(s) starting the same cycle
 * as the write CAS operation. Then, OCTEON normally continues to assert the ODT output pin(s) for five
 * more cycles - for a total of 6 cycles for the entire word write - satisfying the 6 cycle DDR3
 * ODTH8 requirements. But it is possible for DFM to issue two word writes  separated by as few
 * as WtW = 4 or 5 cycles. In that case, DFM asserts the ODT output pin(s) for the WODT mask of the
 * first word write for WtW cycles, then asserts the ODT output pin(s) for the WODT mask of the
 * second write for 6 cycles (or less if a third word write follows within 4 or 5
 * cycles of this second word write). Note that it may be necessary to force DFM to space back-to-back
 * word writes to different ranks apart by at least 6 cycles to prevent DDR3 ODTH8 violations.
 */
union cvmx_dfm_wodt_mask {
	uint64_t u64;
	struct cvmx_dfm_wodt_mask_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t wodt_d3_r1                   : 8;  /**< Not used by DFM. */
	uint64_t wodt_d3_r0                   : 8;  /**< Not used by DFM. */
	uint64_t wodt_d2_r1                   : 8;  /**< Not used by DFM. */
	uint64_t wodt_d2_r0                   : 8;  /**< Not used by DFM. */
	uint64_t wodt_d1_r1                   : 8;  /**< Not used by DFM. */
	uint64_t wodt_d1_r0                   : 8;  /**< Not used by DFM. */
	uint64_t wodt_d0_r1                   : 8;  /**< Write ODT mask RANK1
                                                         WODT_D0_R1<7:2> not used by DFM.
                                                         WODT_D0_R1<1:0> is also not used by DFM when RANK_ENA is not set. */
	uint64_t wodt_d0_r0                   : 8;  /**< Write ODT mask RANK0
                                                         WODT_D0_R0<7:2> not used by DFM. */
#else
	uint64_t wodt_d0_r0                   : 8;
	uint64_t wodt_d0_r1                   : 8;
	uint64_t wodt_d1_r0                   : 8;
	uint64_t wodt_d1_r1                   : 8;
	uint64_t wodt_d2_r0                   : 8;
	uint64_t wodt_d2_r1                   : 8;
	uint64_t wodt_d3_r0                   : 8;
	uint64_t wodt_d3_r1                   : 8;
#endif
	} s;
	struct cvmx_dfm_wodt_mask_s           cn63xx;
	struct cvmx_dfm_wodt_mask_s           cn63xxp1;
	struct cvmx_dfm_wodt_mask_s           cn66xx;
};
typedef union cvmx_dfm_wodt_mask cvmx_dfm_wodt_mask_t;

#endif
