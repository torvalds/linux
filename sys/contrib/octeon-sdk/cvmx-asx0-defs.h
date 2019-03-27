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
 * cvmx-asx0-defs.h
 *
 * Configuration and status register (CSR) type definitions for
 * Octeon asx0.
 *
 * This file is auto generated. Do not edit.
 *
 * <hr>$Revision$<hr>
 *
 */
#ifndef __CVMX_ASX0_DEFS_H__
#define __CVMX_ASX0_DEFS_H__

#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ASX0_DBG_DATA_DRV CVMX_ASX0_DBG_DATA_DRV_FUNC()
static inline uint64_t CVMX_ASX0_DBG_DATA_DRV_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_ASX0_DBG_DATA_DRV not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800B0000208ull);
}
#else
#define CVMX_ASX0_DBG_DATA_DRV (CVMX_ADD_IO_SEG(0x00011800B0000208ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_ASX0_DBG_DATA_ENABLE CVMX_ASX0_DBG_DATA_ENABLE_FUNC()
static inline uint64_t CVMX_ASX0_DBG_DATA_ENABLE_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)))
		cvmx_warn("CVMX_ASX0_DBG_DATA_ENABLE not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800B0000200ull);
}
#else
#define CVMX_ASX0_DBG_DATA_ENABLE (CVMX_ADD_IO_SEG(0x00011800B0000200ull))
#endif

/**
 * cvmx_asx0_dbg_data_drv
 *
 * ASX_DBG_DATA_DRV
 *
 */
union cvmx_asx0_dbg_data_drv {
	uint64_t u64;
	struct cvmx_asx0_dbg_data_drv_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_9_63                : 55;
	uint64_t pctl                         : 5;  /**< These bits control the driving strength of the dbg
                                                         interface. */
	uint64_t nctl                         : 4;  /**< These bits control the driving strength of the dbg
                                                         interface. */
#else
	uint64_t nctl                         : 4;
	uint64_t pctl                         : 5;
	uint64_t reserved_9_63                : 55;
#endif
	} s;
	struct cvmx_asx0_dbg_data_drv_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_8_63                : 56;
	uint64_t pctl                         : 4;  /**< These bits control the driving strength of the dbg
                                                         interface. */
	uint64_t nctl                         : 4;  /**< These bits control the driving strength of the dbg
                                                         interface. */
#else
	uint64_t nctl                         : 4;
	uint64_t pctl                         : 4;
	uint64_t reserved_8_63                : 56;
#endif
	} cn38xx;
	struct cvmx_asx0_dbg_data_drv_cn38xx  cn38xxp2;
	struct cvmx_asx0_dbg_data_drv_s       cn58xx;
	struct cvmx_asx0_dbg_data_drv_s       cn58xxp1;
};
typedef union cvmx_asx0_dbg_data_drv cvmx_asx0_dbg_data_drv_t;

/**
 * cvmx_asx0_dbg_data_enable
 *
 * ASX_DBG_DATA_ENABLE
 *
 */
union cvmx_asx0_dbg_data_enable {
	uint64_t u64;
	struct cvmx_asx0_dbg_data_enable_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_1_63                : 63;
	uint64_t en                           : 1;  /**< A 1->0 transistion, turns the dbg interface OFF. */
#else
	uint64_t en                           : 1;
	uint64_t reserved_1_63                : 63;
#endif
	} s;
	struct cvmx_asx0_dbg_data_enable_s    cn38xx;
	struct cvmx_asx0_dbg_data_enable_s    cn38xxp2;
	struct cvmx_asx0_dbg_data_enable_s    cn58xx;
	struct cvmx_asx0_dbg_data_enable_s    cn58xxp1;
};
typedef union cvmx_asx0_dbg_data_enable cvmx_asx0_dbg_data_enable_t;

#endif
