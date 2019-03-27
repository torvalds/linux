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

#ifndef __CVMX_CLOCK_H__
#define __CVMX_CLOCK_H__

#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
#include <asm/octeon/octeon.h>
#include <asm/octeon/cvmx-lmcx-defs.h>
#else
#include "cvmx.h"
#endif

#ifdef	__cplusplus
extern "C" {
#endif

/**
 * Enumeration of different Clocks in Octeon.
 */
typedef enum{
    CVMX_CLOCK_RCLK,        /**< Clock used by cores, coherent bus and L2 cache. */
    CVMX_CLOCK_SCLK,        /**< Clock used by IO blocks. */
    CVMX_CLOCK_DDR,         /**< Clock used by DRAM */
    CVMX_CLOCK_CORE,        /**< Alias for CVMX_CLOCK_RCLK */
    CVMX_CLOCK_TIM,         /**< Alias for CVMX_CLOCK_SCLK */
    CVMX_CLOCK_IPD,         /**< Alias for CVMX_CLOCK_SCLK */
} cvmx_clock_t;

/**
 * Get cycle count based on the clock type.
 *
 * @param clock - Enumeration of the clock type.
 * @return      - Get the number of cycles executed so far.
 */
static inline uint64_t cvmx_clock_get_count(cvmx_clock_t clock)
{
    switch(clock)
    {
        case CVMX_CLOCK_RCLK:
        case CVMX_CLOCK_CORE:
        {
#ifndef __mips__
            return cvmx_read_csr(CVMX_IPD_CLK_COUNT);
#elif defined(CVMX_ABI_O32)
            uint32_t tmp_low, tmp_hi;

            asm volatile (
               "   .set push                    \n"
               "   .set mips64r2                \n"
               "   .set noreorder               \n"
               "   rdhwr %[tmpl], $31           \n"
               "   dsrl  %[tmph], %[tmpl], 32   \n"
               "   sll   %[tmpl], 0             \n"
               "   sll   %[tmph], 0             \n"
               "   .set pop                 \n"
                  : [tmpl] "=&r" (tmp_low), [tmph] "=&r" (tmp_hi) : );

            return(((uint64_t)tmp_hi << 32) + tmp_low);
#else
            uint64_t cycle;
            CVMX_RDHWR(cycle, 31);
            return(cycle);
#endif
        }

        case CVMX_CLOCK_SCLK:
        case CVMX_CLOCK_TIM:
        case CVMX_CLOCK_IPD:
            return cvmx_read_csr(CVMX_IPD_CLK_COUNT);

        case CVMX_CLOCK_DDR:
            if (OCTEON_IS_MODEL(OCTEON_CN6XXX) || OCTEON_IS_MODEL(OCTEON_CNF7XXX))
                return cvmx_read_csr(CVMX_LMCX_DCLK_CNT(0));
            else
                return ((cvmx_read_csr(CVMX_LMCX_DCLK_CNT_HI(0)) << 32) | cvmx_read_csr(CVMX_LMCX_DCLK_CNT_LO(0)));
    }

    cvmx_dprintf("cvmx_clock_get_count: Unknown clock type\n");
    return 0;
}

extern uint64_t cvmx_clock_get_rate(cvmx_clock_t clock);

#ifdef	__cplusplus
}
#endif

#endif /* __CVMX_CLOCK_H__ */
