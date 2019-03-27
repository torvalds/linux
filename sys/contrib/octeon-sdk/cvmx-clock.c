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
 *
 * Interface to Core, IO and DDR Clock.
 *
 * <hr>$Revision: 45089 $<hr>
*/

#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
#include <linux/module.h>
#include <asm/octeon/octeon.h>
#include <asm/octeon/cvmx-clock.h>
#include <asm/octeon/cvmx-npei-defs.h>
#include <asm/octeon/cvmx-pexp-defs.h>
#include <asm/octeon/cvmx-dbg-defs.h>
#else
#if !defined(__FreeBSD__) || !defined(_KERNEL)
#include "executive-config.h"
#endif
#include "cvmx.h"
#endif

#ifndef CVMX_BUILD_FOR_UBOOT
static uint64_t rate_eclk = 0;
static uint64_t rate_sclk = 0;
static uint64_t rate_dclk = 0;
#endif

/**
 * Get clock rate based on the clock type.
 *
 * @param clock - Enumeration of the clock type.
 * @return      - return the clock rate.
 */
uint64_t cvmx_clock_get_rate(cvmx_clock_t clock)
{
    const uint64_t REF_CLOCK = 50000000;

#ifdef CVMX_BUILD_FOR_UBOOT
    uint64_t rate_eclk = 0;
    uint64_t rate_sclk = 0;
    uint64_t rate_dclk = 0;
#endif

    if (cvmx_unlikely(!rate_eclk))
    {
	/* Note: The order of these checks is important.
	** octeon_has_feature(OCTEON_FEATURE_PCIE) is true for both 6XXX
	** and 52XX/56XX, so OCTEON_FEATURE_NPEI _must_ be checked first */
        if (octeon_has_feature(OCTEON_FEATURE_NPEI))
        {
            cvmx_npei_dbg_data_t npei_dbg_data;
            npei_dbg_data.u64 = cvmx_read_csr(CVMX_PEXP_NPEI_DBG_DATA);
            rate_eclk =  REF_CLOCK * npei_dbg_data.s.c_mul;
            rate_sclk = rate_eclk;
        }
        else if (octeon_has_feature(OCTEON_FEATURE_PCIE))
        {
            cvmx_mio_rst_boot_t mio_rst_boot;
            mio_rst_boot.u64 = cvmx_read_csr(CVMX_MIO_RST_BOOT);
            rate_eclk =  REF_CLOCK * mio_rst_boot.s.c_mul;
            rate_sclk = REF_CLOCK * mio_rst_boot.s.pnr_mul;
        }
        else
        {
            cvmx_dbg_data_t dbg_data;
            dbg_data.u64 = cvmx_read_csr(CVMX_DBG_DATA);
            rate_eclk =  REF_CLOCK * dbg_data.s.c_mul;
            rate_sclk = rate_eclk;
        }
    }

    switch (clock)
    {
        case CVMX_CLOCK_SCLK:
        case CVMX_CLOCK_TIM:
        case CVMX_CLOCK_IPD:
            return rate_sclk;

        case CVMX_CLOCK_RCLK:
        case CVMX_CLOCK_CORE:
            return rate_eclk;

        case CVMX_CLOCK_DDR:
#if !defined(CVMX_BUILD_FOR_LINUX_HOST) && !defined(CVMX_BUILD_FOR_TOOLCHAIN)
            if (cvmx_unlikely(!rate_dclk))
                rate_dclk = cvmx_sysinfo_get()->dram_data_rate_hz;
#endif
            return rate_dclk;
    }

    cvmx_dprintf("cvmx_clock_get_rate: Unknown clock type\n");
    return 0;
}
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
EXPORT_SYMBOL(cvmx_clock_get_rate);
#endif
