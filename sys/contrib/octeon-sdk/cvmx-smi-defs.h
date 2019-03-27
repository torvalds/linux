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
 * cvmx-smi-defs.h
 *
 * Configuration and status register (CSR) type definitions for
 * Octeon smi.
 *
 * This file is auto generated. Do not edit.
 *
 * <hr>$Revision$<hr>
 *
 */
#ifndef __CVMX_SMI_DEFS_H__
#define __CVMX_SMI_DEFS_H__

#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_SMI_DRV_CTL CVMX_SMI_DRV_CTL_FUNC()
static inline uint64_t CVMX_SMI_DRV_CTL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_SMI_DRV_CTL not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001180000001828ull);
}
#else
#define CVMX_SMI_DRV_CTL (CVMX_ADD_IO_SEG(0x0001180000001828ull))
#endif

/**
 * cvmx_smi_drv_ctl
 *
 * SMI_DRV_CTL = SMI Drive Strength Control
 *
 */
union cvmx_smi_drv_ctl {
	uint64_t u64;
	struct cvmx_smi_drv_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_14_63               : 50;
	uint64_t pctl                         : 6;  /**< PCTL Drive strength control bits
                                                         Assuming a 50ohm termination
                                                         3.3v supply = 19
                                                         2.5v supply = TBD */
	uint64_t reserved_6_7                 : 2;
	uint64_t nctl                         : 6;  /**< NCTL Drive strength control bits
                                                         Assuming a 50ohm termination
                                                         3.3v supply = 15
                                                         2.5v supply = TBD */
#else
	uint64_t nctl                         : 6;
	uint64_t reserved_6_7                 : 2;
	uint64_t pctl                         : 6;
	uint64_t reserved_14_63               : 50;
#endif
	} s;
	struct cvmx_smi_drv_ctl_s             cn61xx;
	struct cvmx_smi_drv_ctl_s             cn63xx;
	struct cvmx_smi_drv_ctl_s             cn63xxp1;
	struct cvmx_smi_drv_ctl_s             cn66xx;
	struct cvmx_smi_drv_ctl_s             cn68xx;
	struct cvmx_smi_drv_ctl_s             cn68xxp1;
	struct cvmx_smi_drv_ctl_s             cnf71xx;
};
typedef union cvmx_smi_drv_ctl cvmx_smi_drv_ctl_t;

#endif
