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
 * cvmx-asxx-defs.h
 *
 * Configuration and status register (CSR) type definitions for
 * Octeon asxx.
 *
 * This file is auto generated. Do not edit.
 *
 * <hr>$Revision$<hr>
 *
 */
#ifndef __CVMX_ASXX_DEFS_H__
#define __CVMX_ASXX_DEFS_H__

#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ASXX_GMII_RX_CLK_SET(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_ASXX_GMII_RX_CLK_SET(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0000180ull);
}
#else
#define CVMX_ASXX_GMII_RX_CLK_SET(block_id) (CVMX_ADD_IO_SEG(0x00011800B0000180ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ASXX_GMII_RX_DAT_SET(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_ASXX_GMII_RX_DAT_SET(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0000188ull);
}
#else
#define CVMX_ASXX_GMII_RX_DAT_SET(block_id) (CVMX_ADD_IO_SEG(0x00011800B0000188ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ASXX_INT_EN(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_ASXX_INT_EN(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0000018ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_ASXX_INT_EN(block_id) (CVMX_ADD_IO_SEG(0x00011800B0000018ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ASXX_INT_REG(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_ASXX_INT_REG(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0000010ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_ASXX_INT_REG(block_id) (CVMX_ADD_IO_SEG(0x00011800B0000010ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ASXX_MII_RX_DAT_SET(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_ASXX_MII_RX_DAT_SET(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0000190ull);
}
#else
#define CVMX_ASXX_MII_RX_DAT_SET(block_id) (CVMX_ADD_IO_SEG(0x00011800B0000190ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ASXX_PRT_LOOP(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_ASXX_PRT_LOOP(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0000040ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_ASXX_PRT_LOOP(block_id) (CVMX_ADD_IO_SEG(0x00011800B0000040ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ASXX_RLD_BYPASS(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_ASXX_RLD_BYPASS(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0000248ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_ASXX_RLD_BYPASS(block_id) (CVMX_ADD_IO_SEG(0x00011800B0000248ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ASXX_RLD_BYPASS_SETTING(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_ASXX_RLD_BYPASS_SETTING(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0000250ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_ASXX_RLD_BYPASS_SETTING(block_id) (CVMX_ADD_IO_SEG(0x00011800B0000250ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ASXX_RLD_COMP(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_ASXX_RLD_COMP(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0000220ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_ASXX_RLD_COMP(block_id) (CVMX_ADD_IO_SEG(0x00011800B0000220ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ASXX_RLD_DATA_DRV(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_ASXX_RLD_DATA_DRV(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0000218ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_ASXX_RLD_DATA_DRV(block_id) (CVMX_ADD_IO_SEG(0x00011800B0000218ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ASXX_RLD_FCRAM_MODE(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_ASXX_RLD_FCRAM_MODE(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0000210ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_ASXX_RLD_FCRAM_MODE(block_id) (CVMX_ADD_IO_SEG(0x00011800B0000210ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ASXX_RLD_NCTL_STRONG(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_ASXX_RLD_NCTL_STRONG(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0000230ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_ASXX_RLD_NCTL_STRONG(block_id) (CVMX_ADD_IO_SEG(0x00011800B0000230ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ASXX_RLD_NCTL_WEAK(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_ASXX_RLD_NCTL_WEAK(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0000240ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_ASXX_RLD_NCTL_WEAK(block_id) (CVMX_ADD_IO_SEG(0x00011800B0000240ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ASXX_RLD_PCTL_STRONG(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_ASXX_RLD_PCTL_STRONG(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0000228ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_ASXX_RLD_PCTL_STRONG(block_id) (CVMX_ADD_IO_SEG(0x00011800B0000228ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ASXX_RLD_PCTL_WEAK(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_ASXX_RLD_PCTL_WEAK(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0000238ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_ASXX_RLD_PCTL_WEAK(block_id) (CVMX_ADD_IO_SEG(0x00011800B0000238ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ASXX_RLD_SETTING(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_ASXX_RLD_SETTING(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0000258ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_ASXX_RLD_SETTING(block_id) (CVMX_ADD_IO_SEG(0x00011800B0000258ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ASXX_RX_CLK_SETX(unsigned long offset, unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && (((offset <= 2)) && ((block_id == 0)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && (((offset <= 2)) && ((block_id == 0)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && (((offset <= 3)) && ((block_id <= 1)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && (((offset <= 2)) && ((block_id == 0)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && (((offset <= 3)) && ((block_id <= 1))))))
		cvmx_warn("CVMX_ASXX_RX_CLK_SETX(%lu,%lu) is invalid on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0000020ull) + (((offset) & 3) + ((block_id) & 1) * 0x1000000ull) * 8;
}
#else
#define CVMX_ASXX_RX_CLK_SETX(offset, block_id) (CVMX_ADD_IO_SEG(0x00011800B0000020ull) + (((offset) & 3) + ((block_id) & 1) * 0x1000000ull) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ASXX_RX_PRT_EN(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_ASXX_RX_PRT_EN(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0000000ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_ASXX_RX_PRT_EN(block_id) (CVMX_ADD_IO_SEG(0x00011800B0000000ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ASXX_RX_WOL(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_ASXX_RX_WOL(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0000100ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_ASXX_RX_WOL(block_id) (CVMX_ADD_IO_SEG(0x00011800B0000100ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ASXX_RX_WOL_MSK(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_ASXX_RX_WOL_MSK(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0000108ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_ASXX_RX_WOL_MSK(block_id) (CVMX_ADD_IO_SEG(0x00011800B0000108ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ASXX_RX_WOL_POWOK(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_ASXX_RX_WOL_POWOK(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0000118ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_ASXX_RX_WOL_POWOK(block_id) (CVMX_ADD_IO_SEG(0x00011800B0000118ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ASXX_RX_WOL_SIG(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_ASXX_RX_WOL_SIG(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0000110ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_ASXX_RX_WOL_SIG(block_id) (CVMX_ADD_IO_SEG(0x00011800B0000110ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ASXX_TX_CLK_SETX(unsigned long offset, unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && (((offset <= 2)) && ((block_id == 0)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && (((offset <= 2)) && ((block_id == 0)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && (((offset <= 3)) && ((block_id <= 1)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && (((offset <= 2)) && ((block_id == 0)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && (((offset <= 3)) && ((block_id <= 1))))))
		cvmx_warn("CVMX_ASXX_TX_CLK_SETX(%lu,%lu) is invalid on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0000048ull) + (((offset) & 3) + ((block_id) & 1) * 0x1000000ull) * 8;
}
#else
#define CVMX_ASXX_TX_CLK_SETX(offset, block_id) (CVMX_ADD_IO_SEG(0x00011800B0000048ull) + (((offset) & 3) + ((block_id) & 1) * 0x1000000ull) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ASXX_TX_COMP_BYP(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_ASXX_TX_COMP_BYP(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0000068ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_ASXX_TX_COMP_BYP(block_id) (CVMX_ADD_IO_SEG(0x00011800B0000068ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ASXX_TX_HI_WATERX(unsigned long offset, unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && (((offset <= 2)) && ((block_id == 0)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && (((offset <= 2)) && ((block_id == 0)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && (((offset <= 3)) && ((block_id <= 1)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && (((offset <= 2)) && ((block_id == 0)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && (((offset <= 3)) && ((block_id <= 1))))))
		cvmx_warn("CVMX_ASXX_TX_HI_WATERX(%lu,%lu) is invalid on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0000080ull) + (((offset) & 3) + ((block_id) & 1) * 0x1000000ull) * 8;
}
#else
#define CVMX_ASXX_TX_HI_WATERX(offset, block_id) (CVMX_ADD_IO_SEG(0x00011800B0000080ull) + (((offset) & 3) + ((block_id) & 1) * 0x1000000ull) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_ASXX_TX_PRT_EN(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_ASXX_TX_PRT_EN(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0000008ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_ASXX_TX_PRT_EN(block_id) (CVMX_ADD_IO_SEG(0x00011800B0000008ull) + ((block_id) & 1) * 0x8000000ull)
#endif

/**
 * cvmx_asx#_gmii_rx_clk_set
 *
 * ASX_GMII_RX_CLK_SET = GMII Clock delay setting
 *
 */
union cvmx_asxx_gmii_rx_clk_set {
	uint64_t u64;
	struct cvmx_asxx_gmii_rx_clk_set_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_5_63                : 59;
	uint64_t setting                      : 5;  /**< Setting to place on the RXCLK (GMII receive clk)
                                                         delay line.  The intrinsic delay can range from
                                                         50ps to 80ps per tap. */
#else
	uint64_t setting                      : 5;
	uint64_t reserved_5_63                : 59;
#endif
	} s;
	struct cvmx_asxx_gmii_rx_clk_set_s    cn30xx;
	struct cvmx_asxx_gmii_rx_clk_set_s    cn31xx;
	struct cvmx_asxx_gmii_rx_clk_set_s    cn50xx;
};
typedef union cvmx_asxx_gmii_rx_clk_set cvmx_asxx_gmii_rx_clk_set_t;

/**
 * cvmx_asx#_gmii_rx_dat_set
 *
 * ASX_GMII_RX_DAT_SET = GMII Clock delay setting
 *
 */
union cvmx_asxx_gmii_rx_dat_set {
	uint64_t u64;
	struct cvmx_asxx_gmii_rx_dat_set_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_5_63                : 59;
	uint64_t setting                      : 5;  /**< Setting to place on the RXD (GMII receive data)
                                                         delay lines.  The intrinsic delay can range from
                                                         50ps to 80ps per tap. */
#else
	uint64_t setting                      : 5;
	uint64_t reserved_5_63                : 59;
#endif
	} s;
	struct cvmx_asxx_gmii_rx_dat_set_s    cn30xx;
	struct cvmx_asxx_gmii_rx_dat_set_s    cn31xx;
	struct cvmx_asxx_gmii_rx_dat_set_s    cn50xx;
};
typedef union cvmx_asxx_gmii_rx_dat_set cvmx_asxx_gmii_rx_dat_set_t;

/**
 * cvmx_asx#_int_en
 *
 * ASX_INT_EN = Interrupt Enable
 *
 */
union cvmx_asxx_int_en {
	uint64_t u64;
	struct cvmx_asxx_int_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_12_63               : 52;
	uint64_t txpsh                        : 4;  /**< TX FIFO overflow on RMGII port */
	uint64_t txpop                        : 4;  /**< TX FIFO underflow on RMGII port */
	uint64_t ovrflw                       : 4;  /**< RX FIFO overflow on RMGII port */
#else
	uint64_t ovrflw                       : 4;
	uint64_t txpop                        : 4;
	uint64_t txpsh                        : 4;
	uint64_t reserved_12_63               : 52;
#endif
	} s;
	struct cvmx_asxx_int_en_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_11_63               : 53;
	uint64_t txpsh                        : 3;  /**< TX FIFO overflow on RMGII port */
	uint64_t reserved_7_7                 : 1;
	uint64_t txpop                        : 3;  /**< TX FIFO underflow on RMGII port */
	uint64_t reserved_3_3                 : 1;
	uint64_t ovrflw                       : 3;  /**< RX FIFO overflow on RMGII port */
#else
	uint64_t ovrflw                       : 3;
	uint64_t reserved_3_3                 : 1;
	uint64_t txpop                        : 3;
	uint64_t reserved_7_7                 : 1;
	uint64_t txpsh                        : 3;
	uint64_t reserved_11_63               : 53;
#endif
	} cn30xx;
	struct cvmx_asxx_int_en_cn30xx        cn31xx;
	struct cvmx_asxx_int_en_s             cn38xx;
	struct cvmx_asxx_int_en_s             cn38xxp2;
	struct cvmx_asxx_int_en_cn30xx        cn50xx;
	struct cvmx_asxx_int_en_s             cn58xx;
	struct cvmx_asxx_int_en_s             cn58xxp1;
};
typedef union cvmx_asxx_int_en cvmx_asxx_int_en_t;

/**
 * cvmx_asx#_int_reg
 *
 * ASX_INT_REG = Interrupt Register
 *
 */
union cvmx_asxx_int_reg {
	uint64_t u64;
	struct cvmx_asxx_int_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_12_63               : 52;
	uint64_t txpsh                        : 4;  /**< TX FIFO overflow on RMGII port */
	uint64_t txpop                        : 4;  /**< TX FIFO underflow on RMGII port */
	uint64_t ovrflw                       : 4;  /**< RX FIFO overflow on RMGII port */
#else
	uint64_t ovrflw                       : 4;
	uint64_t txpop                        : 4;
	uint64_t txpsh                        : 4;
	uint64_t reserved_12_63               : 52;
#endif
	} s;
	struct cvmx_asxx_int_reg_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_11_63               : 53;
	uint64_t txpsh                        : 3;  /**< TX FIFO overflow on RMGII port */
	uint64_t reserved_7_7                 : 1;
	uint64_t txpop                        : 3;  /**< TX FIFO underflow on RMGII port */
	uint64_t reserved_3_3                 : 1;
	uint64_t ovrflw                       : 3;  /**< RX FIFO overflow on RMGII port */
#else
	uint64_t ovrflw                       : 3;
	uint64_t reserved_3_3                 : 1;
	uint64_t txpop                        : 3;
	uint64_t reserved_7_7                 : 1;
	uint64_t txpsh                        : 3;
	uint64_t reserved_11_63               : 53;
#endif
	} cn30xx;
	struct cvmx_asxx_int_reg_cn30xx       cn31xx;
	struct cvmx_asxx_int_reg_s            cn38xx;
	struct cvmx_asxx_int_reg_s            cn38xxp2;
	struct cvmx_asxx_int_reg_cn30xx       cn50xx;
	struct cvmx_asxx_int_reg_s            cn58xx;
	struct cvmx_asxx_int_reg_s            cn58xxp1;
};
typedef union cvmx_asxx_int_reg cvmx_asxx_int_reg_t;

/**
 * cvmx_asx#_mii_rx_dat_set
 *
 * ASX_MII_RX_DAT_SET = GMII Clock delay setting
 *
 */
union cvmx_asxx_mii_rx_dat_set {
	uint64_t u64;
	struct cvmx_asxx_mii_rx_dat_set_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_5_63                : 59;
	uint64_t setting                      : 5;  /**< Setting to place on the RXD (MII receive data)
                                                         delay lines.  The intrinsic delay can range from
                                                         50ps to 80ps per tap. */
#else
	uint64_t setting                      : 5;
	uint64_t reserved_5_63                : 59;
#endif
	} s;
	struct cvmx_asxx_mii_rx_dat_set_s     cn30xx;
	struct cvmx_asxx_mii_rx_dat_set_s     cn50xx;
};
typedef union cvmx_asxx_mii_rx_dat_set cvmx_asxx_mii_rx_dat_set_t;

/**
 * cvmx_asx#_prt_loop
 *
 * ASX_PRT_LOOP = Internal Loopback mode - TX FIFO output goes into RX FIFO (and maybe pins)
 *
 */
union cvmx_asxx_prt_loop {
	uint64_t u64;
	struct cvmx_asxx_prt_loop_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_8_63                : 56;
	uint64_t ext_loop                     : 4;  /**< External Loopback Enable
                                                         0 = No Loopback (TX FIFO is filled by RMGII)
                                                         1 = RX FIFO drives the TX FIFO
                                                             - GMX_PRT_CFG[DUPLEX] must be 1 (FullDuplex)
                                                             - GMX_PRT_CFG[SPEED] must be 1  (GigE speed)
                                                             - core clock > 250MHZ
                                                             - rxc must not deviate from the +-50ppm
                                                             - if txc>rxc, idle cycle may drop over time */
	uint64_t int_loop                     : 4;  /**< Internal Loopback Enable
                                                         0 = No Loopback (RX FIFO is filled by RMGII pins)
                                                         1 = TX FIFO drives the RX FIFO
                                                         Note, in internal loop-back mode, the RGMII link
                                                         status is not used (since there is no real PHY).
                                                         Software cannot use the inband status. */
#else
	uint64_t int_loop                     : 4;
	uint64_t ext_loop                     : 4;
	uint64_t reserved_8_63                : 56;
#endif
	} s;
	struct cvmx_asxx_prt_loop_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_7_63                : 57;
	uint64_t ext_loop                     : 3;  /**< External Loopback Enable
                                                         0 = No Loopback (TX FIFO is filled by RMGII)
                                                         1 = RX FIFO drives the TX FIFO
                                                             - GMX_PRT_CFG[DUPLEX] must be 1 (FullDuplex)
                                                             - GMX_PRT_CFG[SPEED] must be 1  (GigE speed)
                                                             - core clock > 250MHZ
                                                             - rxc must not deviate from the +-50ppm
                                                             - if txc>rxc, idle cycle may drop over time */
	uint64_t reserved_3_3                 : 1;
	uint64_t int_loop                     : 3;  /**< Internal Loopback Enable
                                                         0 = No Loopback (RX FIFO is filled by RMGII pins)
                                                         1 = TX FIFO drives the RX FIFO
                                                             - GMX_PRT_CFG[DUPLEX] must be 1 (FullDuplex)
                                                             - GMX_PRT_CFG[SPEED] must be 1  (GigE speed)
                                                             - GMX_TX_CLK[CLK_CNT] must be 1
                                                         Note, in internal loop-back mode, the RGMII link
                                                         status is not used (since there is no real PHY).
                                                         Software cannot use the inband status. */
#else
	uint64_t int_loop                     : 3;
	uint64_t reserved_3_3                 : 1;
	uint64_t ext_loop                     : 3;
	uint64_t reserved_7_63                : 57;
#endif
	} cn30xx;
	struct cvmx_asxx_prt_loop_cn30xx      cn31xx;
	struct cvmx_asxx_prt_loop_s           cn38xx;
	struct cvmx_asxx_prt_loop_s           cn38xxp2;
	struct cvmx_asxx_prt_loop_cn30xx      cn50xx;
	struct cvmx_asxx_prt_loop_s           cn58xx;
	struct cvmx_asxx_prt_loop_s           cn58xxp1;
};
typedef union cvmx_asxx_prt_loop cvmx_asxx_prt_loop_t;

/**
 * cvmx_asx#_rld_bypass
 *
 * ASX_RLD_BYPASS
 *
 */
union cvmx_asxx_rld_bypass {
	uint64_t u64;
	struct cvmx_asxx_rld_bypass_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_1_63                : 63;
	uint64_t bypass                       : 1;  /**< When set, the rld_dll setting is bypassed with
                                                         ASX_RLD_BYPASS_SETTING */
#else
	uint64_t bypass                       : 1;
	uint64_t reserved_1_63                : 63;
#endif
	} s;
	struct cvmx_asxx_rld_bypass_s         cn38xx;
	struct cvmx_asxx_rld_bypass_s         cn38xxp2;
	struct cvmx_asxx_rld_bypass_s         cn58xx;
	struct cvmx_asxx_rld_bypass_s         cn58xxp1;
};
typedef union cvmx_asxx_rld_bypass cvmx_asxx_rld_bypass_t;

/**
 * cvmx_asx#_rld_bypass_setting
 *
 * ASX_RLD_BYPASS_SETTING
 *
 */
union cvmx_asxx_rld_bypass_setting {
	uint64_t u64;
	struct cvmx_asxx_rld_bypass_setting_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_5_63                : 59;
	uint64_t setting                      : 5;  /**< The rld_dll setting bypass value */
#else
	uint64_t setting                      : 5;
	uint64_t reserved_5_63                : 59;
#endif
	} s;
	struct cvmx_asxx_rld_bypass_setting_s cn38xx;
	struct cvmx_asxx_rld_bypass_setting_s cn38xxp2;
	struct cvmx_asxx_rld_bypass_setting_s cn58xx;
	struct cvmx_asxx_rld_bypass_setting_s cn58xxp1;
};
typedef union cvmx_asxx_rld_bypass_setting cvmx_asxx_rld_bypass_setting_t;

/**
 * cvmx_asx#_rld_comp
 *
 * ASX_RLD_COMP
 *
 */
union cvmx_asxx_rld_comp {
	uint64_t u64;
	struct cvmx_asxx_rld_comp_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_9_63                : 55;
	uint64_t pctl                         : 5;  /**< PCTL Compensation Value
                                                         These bits reflect the computed compensation
                                                          values from the built-in compensation circuit. */
	uint64_t nctl                         : 4;  /**< These bits reflect the computed compensation
                                                         values from the built-in compensation circuit. */
#else
	uint64_t nctl                         : 4;
	uint64_t pctl                         : 5;
	uint64_t reserved_9_63                : 55;
#endif
	} s;
	struct cvmx_asxx_rld_comp_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_8_63                : 56;
	uint64_t pctl                         : 4;  /**< These bits reflect the computed compensation
                                                         values from the built-in compensation circuit. */
	uint64_t nctl                         : 4;  /**< These bits reflect the computed compensation
                                                         values from the built-in compensation circuit. */
#else
	uint64_t nctl                         : 4;
	uint64_t pctl                         : 4;
	uint64_t reserved_8_63                : 56;
#endif
	} cn38xx;
	struct cvmx_asxx_rld_comp_cn38xx      cn38xxp2;
	struct cvmx_asxx_rld_comp_s           cn58xx;
	struct cvmx_asxx_rld_comp_s           cn58xxp1;
};
typedef union cvmx_asxx_rld_comp cvmx_asxx_rld_comp_t;

/**
 * cvmx_asx#_rld_data_drv
 *
 * ASX_RLD_DATA_DRV
 *
 */
union cvmx_asxx_rld_data_drv {
	uint64_t u64;
	struct cvmx_asxx_rld_data_drv_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_8_63                : 56;
	uint64_t pctl                         : 4;  /**< These bits specify a driving strength (positive
                                                         integer) for the RLD I/Os when the built-in
                                                         compensation circuit is bypassed. */
	uint64_t nctl                         : 4;  /**< These bits specify a driving strength (positive
                                                         integer) for the RLD I/Os when the built-in
                                                         compensation circuit is bypassed. */
#else
	uint64_t nctl                         : 4;
	uint64_t pctl                         : 4;
	uint64_t reserved_8_63                : 56;
#endif
	} s;
	struct cvmx_asxx_rld_data_drv_s       cn38xx;
	struct cvmx_asxx_rld_data_drv_s       cn38xxp2;
	struct cvmx_asxx_rld_data_drv_s       cn58xx;
	struct cvmx_asxx_rld_data_drv_s       cn58xxp1;
};
typedef union cvmx_asxx_rld_data_drv cvmx_asxx_rld_data_drv_t;

/**
 * cvmx_asx#_rld_fcram_mode
 *
 * ASX_RLD_FCRAM_MODE
 *
 */
union cvmx_asxx_rld_fcram_mode {
	uint64_t u64;
	struct cvmx_asxx_rld_fcram_mode_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_1_63                : 63;
	uint64_t mode                         : 1;  /**< Memory Mode
                                                         - 0: RLDRAM
                                                         - 1: FCRAM */
#else
	uint64_t mode                         : 1;
	uint64_t reserved_1_63                : 63;
#endif
	} s;
	struct cvmx_asxx_rld_fcram_mode_s     cn38xx;
	struct cvmx_asxx_rld_fcram_mode_s     cn38xxp2;
};
typedef union cvmx_asxx_rld_fcram_mode cvmx_asxx_rld_fcram_mode_t;

/**
 * cvmx_asx#_rld_nctl_strong
 *
 * ASX_RLD_NCTL_STRONG
 *
 */
union cvmx_asxx_rld_nctl_strong {
	uint64_t u64;
	struct cvmx_asxx_rld_nctl_strong_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_5_63                : 59;
	uint64_t nctl                         : 5;  /**< Duke's drive control */
#else
	uint64_t nctl                         : 5;
	uint64_t reserved_5_63                : 59;
#endif
	} s;
	struct cvmx_asxx_rld_nctl_strong_s    cn38xx;
	struct cvmx_asxx_rld_nctl_strong_s    cn38xxp2;
	struct cvmx_asxx_rld_nctl_strong_s    cn58xx;
	struct cvmx_asxx_rld_nctl_strong_s    cn58xxp1;
};
typedef union cvmx_asxx_rld_nctl_strong cvmx_asxx_rld_nctl_strong_t;

/**
 * cvmx_asx#_rld_nctl_weak
 *
 * ASX_RLD_NCTL_WEAK
 *
 */
union cvmx_asxx_rld_nctl_weak {
	uint64_t u64;
	struct cvmx_asxx_rld_nctl_weak_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_5_63                : 59;
	uint64_t nctl                         : 5;  /**< UNUSED (not needed for CN58XX) */
#else
	uint64_t nctl                         : 5;
	uint64_t reserved_5_63                : 59;
#endif
	} s;
	struct cvmx_asxx_rld_nctl_weak_s      cn38xx;
	struct cvmx_asxx_rld_nctl_weak_s      cn38xxp2;
	struct cvmx_asxx_rld_nctl_weak_s      cn58xx;
	struct cvmx_asxx_rld_nctl_weak_s      cn58xxp1;
};
typedef union cvmx_asxx_rld_nctl_weak cvmx_asxx_rld_nctl_weak_t;

/**
 * cvmx_asx#_rld_pctl_strong
 *
 * ASX_RLD_PCTL_STRONG
 *
 */
union cvmx_asxx_rld_pctl_strong {
	uint64_t u64;
	struct cvmx_asxx_rld_pctl_strong_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_5_63                : 59;
	uint64_t pctl                         : 5;  /**< Duke's drive control */
#else
	uint64_t pctl                         : 5;
	uint64_t reserved_5_63                : 59;
#endif
	} s;
	struct cvmx_asxx_rld_pctl_strong_s    cn38xx;
	struct cvmx_asxx_rld_pctl_strong_s    cn38xxp2;
	struct cvmx_asxx_rld_pctl_strong_s    cn58xx;
	struct cvmx_asxx_rld_pctl_strong_s    cn58xxp1;
};
typedef union cvmx_asxx_rld_pctl_strong cvmx_asxx_rld_pctl_strong_t;

/**
 * cvmx_asx#_rld_pctl_weak
 *
 * ASX_RLD_PCTL_WEAK
 *
 */
union cvmx_asxx_rld_pctl_weak {
	uint64_t u64;
	struct cvmx_asxx_rld_pctl_weak_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_5_63                : 59;
	uint64_t pctl                         : 5;  /**< UNUSED (not needed for CN58XX) */
#else
	uint64_t pctl                         : 5;
	uint64_t reserved_5_63                : 59;
#endif
	} s;
	struct cvmx_asxx_rld_pctl_weak_s      cn38xx;
	struct cvmx_asxx_rld_pctl_weak_s      cn38xxp2;
	struct cvmx_asxx_rld_pctl_weak_s      cn58xx;
	struct cvmx_asxx_rld_pctl_weak_s      cn58xxp1;
};
typedef union cvmx_asxx_rld_pctl_weak cvmx_asxx_rld_pctl_weak_t;

/**
 * cvmx_asx#_rld_setting
 *
 * ASX_RLD_SETTING
 *
 */
union cvmx_asxx_rld_setting {
	uint64_t u64;
	struct cvmx_asxx_rld_setting_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_13_63               : 51;
	uint64_t dfaset                       : 5;  /**< RLD ClkGen DLL Setting(debug) */
	uint64_t dfalag                       : 1;  /**< RLD ClkGen DLL Lag Error(debug) */
	uint64_t dfalead                      : 1;  /**< RLD ClkGen DLL Lead Error(debug) */
	uint64_t dfalock                      : 1;  /**< RLD ClkGen DLL Lock acquisition(debug) */
	uint64_t setting                      : 5;  /**< RLDCK90 DLL Setting(debug) */
#else
	uint64_t setting                      : 5;
	uint64_t dfalock                      : 1;
	uint64_t dfalead                      : 1;
	uint64_t dfalag                       : 1;
	uint64_t dfaset                       : 5;
	uint64_t reserved_13_63               : 51;
#endif
	} s;
	struct cvmx_asxx_rld_setting_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_5_63                : 59;
	uint64_t setting                      : 5;  /**< This is the read-only true rld dll_setting. */
#else
	uint64_t setting                      : 5;
	uint64_t reserved_5_63                : 59;
#endif
	} cn38xx;
	struct cvmx_asxx_rld_setting_cn38xx   cn38xxp2;
	struct cvmx_asxx_rld_setting_s        cn58xx;
	struct cvmx_asxx_rld_setting_s        cn58xxp1;
};
typedef union cvmx_asxx_rld_setting cvmx_asxx_rld_setting_t;

/**
 * cvmx_asx#_rx_clk_set#
 *
 * ASX_RX_CLK_SET = RGMII Clock delay setting
 *
 *
 * Notes:
 * Setting to place on the open-loop RXC (RGMII receive clk)
 * delay line, which can delay the recieved clock. This
 * can be used if the board and/or transmitting device
 * has not otherwise delayed the clock.
 *
 * A value of SETTING=0 disables the delay line. The delay
 * line should be disabled unless the transmitter or board
 * does not delay the clock.
 *
 * Note that this delay line provides only a coarse control
 * over the delay. Generally, it can only reliably provide
 * a delay in the range 1.25-2.5ns, which may not be adequate
 * for some system applications.
 *
 * The open loop delay line selects
 * from among a series of tap positions. Each incremental
 * tap position adds a delay of 50ps to 135ps per tap, depending
 * on the chip, its temperature, and the voltage.
 * To achieve from 1.25-2.5ns of delay on the recieved
 * clock, a fixed value of SETTING=24 may work.
 * For more precision, we recommend the following settings
 * based on the chip voltage:
 *
 *    VDD           SETTING
 *  -----------------------------
 *    1.0             18
 *    1.05            19
 *    1.1             21
 *    1.15            22
 *    1.2             23
 *    1.25            24
 *    1.3             25
 */
union cvmx_asxx_rx_clk_setx {
	uint64_t u64;
	struct cvmx_asxx_rx_clk_setx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_5_63                : 59;
	uint64_t setting                      : 5;  /**< Setting to place on the open-loop RXC delay line */
#else
	uint64_t setting                      : 5;
	uint64_t reserved_5_63                : 59;
#endif
	} s;
	struct cvmx_asxx_rx_clk_setx_s        cn30xx;
	struct cvmx_asxx_rx_clk_setx_s        cn31xx;
	struct cvmx_asxx_rx_clk_setx_s        cn38xx;
	struct cvmx_asxx_rx_clk_setx_s        cn38xxp2;
	struct cvmx_asxx_rx_clk_setx_s        cn50xx;
	struct cvmx_asxx_rx_clk_setx_s        cn58xx;
	struct cvmx_asxx_rx_clk_setx_s        cn58xxp1;
};
typedef union cvmx_asxx_rx_clk_setx cvmx_asxx_rx_clk_setx_t;

/**
 * cvmx_asx#_rx_prt_en
 *
 * ASX_RX_PRT_EN = RGMII Port Enable
 *
 */
union cvmx_asxx_rx_prt_en {
	uint64_t u64;
	struct cvmx_asxx_rx_prt_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t prt_en                       : 4;  /**< Port enable.  Must be set for Octane to receive
                                                         RMGII traffic.  When this bit clear on a given
                                                         port, then the all RGMII cycles will appear as
                                                         inter-frame cycles. */
#else
	uint64_t prt_en                       : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_asxx_rx_prt_en_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_3_63                : 61;
	uint64_t prt_en                       : 3;  /**< Port enable.  Must be set for Octane to receive
                                                         RMGII traffic.  When this bit clear on a given
                                                         port, then the all RGMII cycles will appear as
                                                         inter-frame cycles. */
#else
	uint64_t prt_en                       : 3;
	uint64_t reserved_3_63                : 61;
#endif
	} cn30xx;
	struct cvmx_asxx_rx_prt_en_cn30xx     cn31xx;
	struct cvmx_asxx_rx_prt_en_s          cn38xx;
	struct cvmx_asxx_rx_prt_en_s          cn38xxp2;
	struct cvmx_asxx_rx_prt_en_cn30xx     cn50xx;
	struct cvmx_asxx_rx_prt_en_s          cn58xx;
	struct cvmx_asxx_rx_prt_en_s          cn58xxp1;
};
typedef union cvmx_asxx_rx_prt_en cvmx_asxx_rx_prt_en_t;

/**
 * cvmx_asx#_rx_wol
 *
 * ASX_RX_WOL = RGMII RX Wake on LAN status register
 *
 */
union cvmx_asxx_rx_wol {
	uint64_t u64;
	struct cvmx_asxx_rx_wol_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_2_63                : 62;
	uint64_t status                       : 1;  /**< Copy of PMCSR[15] - PME_status */
	uint64_t enable                       : 1;  /**< Copy of PMCSR[8]  - PME_enable */
#else
	uint64_t enable                       : 1;
	uint64_t status                       : 1;
	uint64_t reserved_2_63                : 62;
#endif
	} s;
	struct cvmx_asxx_rx_wol_s             cn38xx;
	struct cvmx_asxx_rx_wol_s             cn38xxp2;
};
typedef union cvmx_asxx_rx_wol cvmx_asxx_rx_wol_t;

/**
 * cvmx_asx#_rx_wol_msk
 *
 * ASX_RX_WOL_MSK = RGMII RX Wake on LAN byte mask
 *
 */
union cvmx_asxx_rx_wol_msk {
	uint64_t u64;
	struct cvmx_asxx_rx_wol_msk_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t msk                          : 64; /**< Bytes to include in the CRC signature */
#else
	uint64_t msk                          : 64;
#endif
	} s;
	struct cvmx_asxx_rx_wol_msk_s         cn38xx;
	struct cvmx_asxx_rx_wol_msk_s         cn38xxp2;
};
typedef union cvmx_asxx_rx_wol_msk cvmx_asxx_rx_wol_msk_t;

/**
 * cvmx_asx#_rx_wol_powok
 *
 * ASX_RX_WOL_POWOK = RGMII RX Wake on LAN Power OK
 *
 */
union cvmx_asxx_rx_wol_powok {
	uint64_t u64;
	struct cvmx_asxx_rx_wol_powok_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_1_63                : 63;
	uint64_t powerok                      : 1;  /**< Power OK */
#else
	uint64_t powerok                      : 1;
	uint64_t reserved_1_63                : 63;
#endif
	} s;
	struct cvmx_asxx_rx_wol_powok_s       cn38xx;
	struct cvmx_asxx_rx_wol_powok_s       cn38xxp2;
};
typedef union cvmx_asxx_rx_wol_powok cvmx_asxx_rx_wol_powok_t;

/**
 * cvmx_asx#_rx_wol_sig
 *
 * ASX_RX_WOL_SIG = RGMII RX Wake on LAN CRC signature
 *
 */
union cvmx_asxx_rx_wol_sig {
	uint64_t u64;
	struct cvmx_asxx_rx_wol_sig_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t sig                          : 32; /**< CRC signature */
#else
	uint64_t sig                          : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_asxx_rx_wol_sig_s         cn38xx;
	struct cvmx_asxx_rx_wol_sig_s         cn38xxp2;
};
typedef union cvmx_asxx_rx_wol_sig cvmx_asxx_rx_wol_sig_t;

/**
 * cvmx_asx#_tx_clk_set#
 *
 * ASX_TX_CLK_SET = RGMII Clock delay setting
 *
 *
 * Notes:
 * Setting to place on the open-loop TXC (RGMII transmit clk)
 * delay line, which can delay the transmited clock. This
 * can be used if the board and/or transmitting device
 * has not otherwise delayed the clock.
 *
 * A value of SETTING=0 disables the delay line. The delay
 * line should be disabled unless the transmitter or board
 * does not delay the clock.
 *
 * Note that this delay line provides only a coarse control
 * over the delay. Generally, it can only reliably provide
 * a delay in the range 1.25-2.5ns, which may not be adequate
 * for some system applications.
 *
 * The open loop delay line selects
 * from among a series of tap positions. Each incremental
 * tap position adds a delay of 50ps to 135ps per tap, depending
 * on the chip, its temperature, and the voltage.
 * To achieve from 1.25-2.5ns of delay on the recieved
 * clock, a fixed value of SETTING=24 may work.
 * For more precision, we recommend the following settings
 * based on the chip voltage:
 *
 *    VDD           SETTING
 *  -----------------------------
 *    1.0             18
 *    1.05            19
 *    1.1             21
 *    1.15            22
 *    1.2             23
 *    1.25            24
 *    1.3             25
 */
union cvmx_asxx_tx_clk_setx {
	uint64_t u64;
	struct cvmx_asxx_tx_clk_setx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_5_63                : 59;
	uint64_t setting                      : 5;  /**< Setting to place on the open-loop TXC delay line */
#else
	uint64_t setting                      : 5;
	uint64_t reserved_5_63                : 59;
#endif
	} s;
	struct cvmx_asxx_tx_clk_setx_s        cn30xx;
	struct cvmx_asxx_tx_clk_setx_s        cn31xx;
	struct cvmx_asxx_tx_clk_setx_s        cn38xx;
	struct cvmx_asxx_tx_clk_setx_s        cn38xxp2;
	struct cvmx_asxx_tx_clk_setx_s        cn50xx;
	struct cvmx_asxx_tx_clk_setx_s        cn58xx;
	struct cvmx_asxx_tx_clk_setx_s        cn58xxp1;
};
typedef union cvmx_asxx_tx_clk_setx cvmx_asxx_tx_clk_setx_t;

/**
 * cvmx_asx#_tx_comp_byp
 *
 * ASX_TX_COMP_BYP = RGMII Clock delay setting
 *
 */
union cvmx_asxx_tx_comp_byp {
	uint64_t u64;
	struct cvmx_asxx_tx_comp_byp_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_0_63                : 64;
#else
	uint64_t reserved_0_63                : 64;
#endif
	} s;
	struct cvmx_asxx_tx_comp_byp_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_9_63                : 55;
	uint64_t bypass                       : 1;  /**< Compensation bypass */
	uint64_t pctl                         : 4;  /**< PCTL Compensation Value (see Duke) */
	uint64_t nctl                         : 4;  /**< NCTL Compensation Value (see Duke) */
#else
	uint64_t nctl                         : 4;
	uint64_t pctl                         : 4;
	uint64_t bypass                       : 1;
	uint64_t reserved_9_63                : 55;
#endif
	} cn30xx;
	struct cvmx_asxx_tx_comp_byp_cn30xx   cn31xx;
	struct cvmx_asxx_tx_comp_byp_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_8_63                : 56;
	uint64_t pctl                         : 4;  /**< PCTL Compensation Value (see Duke) */
	uint64_t nctl                         : 4;  /**< NCTL Compensation Value (see Duke) */
#else
	uint64_t nctl                         : 4;
	uint64_t pctl                         : 4;
	uint64_t reserved_8_63                : 56;
#endif
	} cn38xx;
	struct cvmx_asxx_tx_comp_byp_cn38xx   cn38xxp2;
	struct cvmx_asxx_tx_comp_byp_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_17_63               : 47;
	uint64_t bypass                       : 1;  /**< Compensation bypass */
	uint64_t reserved_13_15               : 3;
	uint64_t pctl                         : 5;  /**< PCTL Compensation Value (see Duke) */
	uint64_t reserved_5_7                 : 3;
	uint64_t nctl                         : 5;  /**< NCTL Compensation Value (see Duke) */
#else
	uint64_t nctl                         : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t pctl                         : 5;
	uint64_t reserved_13_15               : 3;
	uint64_t bypass                       : 1;
	uint64_t reserved_17_63               : 47;
#endif
	} cn50xx;
	struct cvmx_asxx_tx_comp_byp_cn58xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_13_63               : 51;
	uint64_t pctl                         : 5;  /**< PCTL Compensation Value (see Duke) */
	uint64_t reserved_5_7                 : 3;
	uint64_t nctl                         : 5;  /**< NCTL Compensation Value (see Duke) */
#else
	uint64_t nctl                         : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t pctl                         : 5;
	uint64_t reserved_13_63               : 51;
#endif
	} cn58xx;
	struct cvmx_asxx_tx_comp_byp_cn58xx   cn58xxp1;
};
typedef union cvmx_asxx_tx_comp_byp cvmx_asxx_tx_comp_byp_t;

/**
 * cvmx_asx#_tx_hi_water#
 *
 * ASX_TX_HI_WATER = RGMII TX FIFO Hi WaterMark
 *
 */
union cvmx_asxx_tx_hi_waterx {
	uint64_t u64;
	struct cvmx_asxx_tx_hi_waterx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t mark                         : 4;  /**< TX FIFO HiWatermark to stall GMX
                                                         Value of 0 maps to 16
                                                         Reset value changed from 10 in pass1
                                                         Pass1 settings (assuming 125 tclk)
                                                         - 325-375: 12
                                                         - 375-437: 11
                                                         - 437-550: 10
                                                         - 550-687:  9 */
#else
	uint64_t mark                         : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_asxx_tx_hi_waterx_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_3_63                : 61;
	uint64_t mark                         : 3;  /**< TX FIFO HiWatermark to stall GMX
                                                         Value 0 maps to 8. */
#else
	uint64_t mark                         : 3;
	uint64_t reserved_3_63                : 61;
#endif
	} cn30xx;
	struct cvmx_asxx_tx_hi_waterx_cn30xx  cn31xx;
	struct cvmx_asxx_tx_hi_waterx_s       cn38xx;
	struct cvmx_asxx_tx_hi_waterx_s       cn38xxp2;
	struct cvmx_asxx_tx_hi_waterx_cn30xx  cn50xx;
	struct cvmx_asxx_tx_hi_waterx_s       cn58xx;
	struct cvmx_asxx_tx_hi_waterx_s       cn58xxp1;
};
typedef union cvmx_asxx_tx_hi_waterx cvmx_asxx_tx_hi_waterx_t;

/**
 * cvmx_asx#_tx_prt_en
 *
 * ASX_TX_PRT_EN = RGMII Port Enable
 *
 */
union cvmx_asxx_tx_prt_en {
	uint64_t u64;
	struct cvmx_asxx_tx_prt_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t prt_en                       : 4;  /**< Port enable.  Must be set for Octane to send
                                                         RMGII traffic.   When this bit clear on a given
                                                         port, then all RGMII cycles will appear as
                                                         inter-frame cycles. */
#else
	uint64_t prt_en                       : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_asxx_tx_prt_en_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_3_63                : 61;
	uint64_t prt_en                       : 3;  /**< Port enable.  Must be set for Octane to send
                                                         RMGII traffic.   When this bit clear on a given
                                                         port, then all RGMII cycles will appear as
                                                         inter-frame cycles. */
#else
	uint64_t prt_en                       : 3;
	uint64_t reserved_3_63                : 61;
#endif
	} cn30xx;
	struct cvmx_asxx_tx_prt_en_cn30xx     cn31xx;
	struct cvmx_asxx_tx_prt_en_s          cn38xx;
	struct cvmx_asxx_tx_prt_en_s          cn38xxp2;
	struct cvmx_asxx_tx_prt_en_cn30xx     cn50xx;
	struct cvmx_asxx_tx_prt_en_s          cn58xx;
	struct cvmx_asxx_tx_prt_en_s          cn58xxp1;
};
typedef union cvmx_asxx_tx_prt_en cvmx_asxx_tx_prt_en_t;

#endif
