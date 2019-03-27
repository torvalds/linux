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
 * cvmx-spx0-defs.h
 *
 * Configuration and status register (CSR) type definitions for
 * Octeon spx0.
 *
 * This file is auto generated. Do not edit.
 *
 * <hr>$Revision$<hr>
 *
 */
#ifndef __CVMX_SPX0_DEFS_H__
#define __CVMX_SPX0_DEFS_H__

#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_SPX0_PLL_BW_CTL CVMX_SPX0_PLL_BW_CTL_FUNC()
static inline uint64_t CVMX_SPX0_PLL_BW_CTL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN38XX)))
		cvmx_warn("CVMX_SPX0_PLL_BW_CTL not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180090000388ull);
}
#else
#define CVMX_SPX0_PLL_BW_CTL (CVMX_ADD_IO_SEG(0x0001180090000388ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_SPX0_PLL_SETTING CVMX_SPX0_PLL_SETTING_FUNC()
static inline uint64_t CVMX_SPX0_PLL_SETTING_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN38XX)))
		cvmx_warn("CVMX_SPX0_PLL_SETTING not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180090000380ull);
}
#else
#define CVMX_SPX0_PLL_SETTING (CVMX_ADD_IO_SEG(0x0001180090000380ull))
#endif

/**
 * cvmx_spx0_pll_bw_ctl
 */
union cvmx_spx0_pll_bw_ctl {
	uint64_t u64;
	struct cvmx_spx0_pll_bw_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_5_63                : 59;
	uint64_t bw_ctl                       : 5;  /**< Core PLL bandwidth control */
#else
	uint64_t bw_ctl                       : 5;
	uint64_t reserved_5_63                : 59;
#endif
	} s;
	struct cvmx_spx0_pll_bw_ctl_s         cn38xx;
	struct cvmx_spx0_pll_bw_ctl_s         cn38xxp2;
};
typedef union cvmx_spx0_pll_bw_ctl cvmx_spx0_pll_bw_ctl_t;

/**
 * cvmx_spx0_pll_setting
 */
union cvmx_spx0_pll_setting {
	uint64_t u64;
	struct cvmx_spx0_pll_setting_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_17_63               : 47;
	uint64_t setting                      : 17; /**< Core PLL setting */
#else
	uint64_t setting                      : 17;
	uint64_t reserved_17_63               : 47;
#endif
	} s;
	struct cvmx_spx0_pll_setting_s        cn38xx;
	struct cvmx_spx0_pll_setting_s        cn38xxp2;
};
typedef union cvmx_spx0_pll_setting cvmx_spx0_pll_setting_t;

#endif
