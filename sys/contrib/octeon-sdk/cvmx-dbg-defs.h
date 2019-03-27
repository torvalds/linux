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
 * cvmx-dbg-defs.h
 *
 * Configuration and status register (CSR) type definitions for
 * Octeon dbg.
 *
 * This file is auto generated. Do not edit.
 *
 * <hr>$Revision$<hr>
 *
 */
#ifndef __CVMX_DBG_DEFS_H__
#define __CVMX_DBG_DEFS_H__

#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_DBG_DATA CVMX_DBG_DATA_FUNC()
static inline uint64_t CVMX_DBG_DATA_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN3XXX) || OCTEON_IS_MODEL(OCTEON_CN50XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_DBG_DATA not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011F00000001E8ull);
}
#else
#define CVMX_DBG_DATA (CVMX_ADD_IO_SEG(0x00011F00000001E8ull))
#endif

/**
 * cvmx_dbg_data
 *
 * DBG_DATA = Debug Data Register
 *
 * Value returned on the debug-data lines from the RSLs
 */
union cvmx_dbg_data {
	uint64_t u64;
	struct cvmx_dbg_data_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_23_63               : 41;
	uint64_t c_mul                        : 5;  /**< C_MUL pins sampled at DCOK assertion */
	uint64_t dsel_ext                     : 1;  /**< Allows changes in the external pins to set the
                                                         debug select value. */
	uint64_t data                         : 17; /**< Value on the debug data lines. */
#else
	uint64_t data                         : 17;
	uint64_t dsel_ext                     : 1;
	uint64_t c_mul                        : 5;
	uint64_t reserved_23_63               : 41;
#endif
	} s;
	struct cvmx_dbg_data_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_31_63               : 33;
	uint64_t pll_mul                      : 3;  /**< pll_mul pins sampled at DCOK assertion */
	uint64_t reserved_23_27               : 5;
	uint64_t c_mul                        : 5;  /**< Core PLL multiplier sampled at DCOK assertion */
	uint64_t dsel_ext                     : 1;  /**< Allows changes in the external pins to set the
                                                         debug select value. */
	uint64_t data                         : 17; /**< Value on the debug data lines. */
#else
	uint64_t data                         : 17;
	uint64_t dsel_ext                     : 1;
	uint64_t c_mul                        : 5;
	uint64_t reserved_23_27               : 5;
	uint64_t pll_mul                      : 3;
	uint64_t reserved_31_63               : 33;
#endif
	} cn30xx;
	struct cvmx_dbg_data_cn30xx           cn31xx;
	struct cvmx_dbg_data_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_29_63               : 35;
	uint64_t d_mul                        : 4;  /**< D_MUL pins sampled on DCOK assertion */
	uint64_t dclk_mul2                    : 1;  /**< Should always be set for fast DDR-II operation */
	uint64_t cclk_div2                    : 1;  /**< Should always be clear for fast core clock */
	uint64_t c_mul                        : 5;  /**< C_MUL pins sampled at DCOK assertion */
	uint64_t dsel_ext                     : 1;  /**< Allows changes in the external pins to set the
                                                         debug select value. */
	uint64_t data                         : 17; /**< Value on the debug data lines. */
#else
	uint64_t data                         : 17;
	uint64_t dsel_ext                     : 1;
	uint64_t c_mul                        : 5;
	uint64_t cclk_div2                    : 1;
	uint64_t dclk_mul2                    : 1;
	uint64_t d_mul                        : 4;
	uint64_t reserved_29_63               : 35;
#endif
	} cn38xx;
	struct cvmx_dbg_data_cn38xx           cn38xxp2;
	struct cvmx_dbg_data_cn30xx           cn50xx;
	struct cvmx_dbg_data_cn58xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_29_63               : 35;
	uint64_t rem                          : 6;  /**< Remaining debug_select pins sampled at DCOK */
	uint64_t c_mul                        : 5;  /**< C_MUL pins sampled at DCOK assertion */
	uint64_t dsel_ext                     : 1;  /**< Allows changes in the external pins to set the
                                                         debug select value. */
	uint64_t data                         : 17; /**< Value on the debug data lines. */
#else
	uint64_t data                         : 17;
	uint64_t dsel_ext                     : 1;
	uint64_t c_mul                        : 5;
	uint64_t rem                          : 6;
	uint64_t reserved_29_63               : 35;
#endif
	} cn58xx;
	struct cvmx_dbg_data_cn58xx           cn58xxp1;
};
typedef union cvmx_dbg_data cvmx_dbg_data_t;

#endif
