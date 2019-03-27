
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
 * Helper utilities for qlm_jtag.
 *
 * <hr>$Revision: 42480 $<hr>
 */
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
#include <asm/octeon/cvmx.h>
#include <asm/octeon/cvmx-clock.h>
#include <asm/octeon/cvmx-helper-jtag.h>
#else
#if !defined(__FreeBSD__) || !defined(_KERNEL)
#include "executive-config.h"
#include "cvmx-config.h"
#endif
#include "cvmx.h"
#if defined(__FreeBSD__) && defined(_KERNEL)
#include "cvmx-helper-jtag.h"
#endif
#endif

/**
 * Initialize the internal QLM JTAG logic to allow programming
 * of the JTAG chain by the cvmx_helper_qlm_jtag_*() functions.
 * These functions should only be used at the direction of Cavium
 * Networks. Programming incorrect values into the JTAG chain
 * can cause chip damage.
 */
void cvmx_helper_qlm_jtag_init(void)
{
    cvmx_ciu_qlm_jtgc_t jtgc;
    int clock_div = 0;
    int divisor;

    divisor = cvmx_clock_get_rate(CVMX_CLOCK_SCLK) / (1000000 *
        (OCTEON_IS_MODEL(OCTEON_CN68XX) ? 10 : 25));

    divisor = (divisor-1)>>2;
    /* Convert the divisor into a power of 2 shift */
    while (divisor)
    {
        clock_div++;
        divisor>>=1;
    }

    /* Clock divider for QLM JTAG operations.  sclk is divided by 2^(CLK_DIV + 2) */
    jtgc.u64 = 0;
    jtgc.s.clk_div = clock_div;
    jtgc.s.mux_sel = 0;
    if (OCTEON_IS_MODEL(OCTEON_CN52XX))
        jtgc.s.bypass = 0x3;
    else if (OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX))
        jtgc.s.bypass = 0x7;
    else
        jtgc.s.bypass = 0xf;
    if (OCTEON_IS_MODEL(OCTEON_CN68XX))
        jtgc.s.bypass_ext = 1;
    cvmx_write_csr(CVMX_CIU_QLM_JTGC, jtgc.u64);
    cvmx_read_csr(CVMX_CIU_QLM_JTGC);
}


/**
 * Write up to 32bits into the QLM jtag chain. Bits are shifted
 * into the MSB and out the LSB, so you should shift in the low
 * order bits followed by the high order bits. The JTAG chain for
 * CN52XX and CN56XX is 4 * 268 bits long, or 1072. The JTAG chain
 * for CN63XX is 4 * 300 bits long, or 1200.
 *
 * @param qlm    QLM to shift value into
 * @param bits   Number of bits to shift in (1-32).
 * @param data   Data to shift in. Bit 0 enters the chain first, followed by
 *               bit 1, etc.
 *
 * @return The low order bits of the JTAG chain that shifted out of the
 *         circle.
 */
uint32_t cvmx_helper_qlm_jtag_shift(int qlm, int bits, uint32_t data)
{
    cvmx_ciu_qlm_jtgc_t jtgc;
    cvmx_ciu_qlm_jtgd_t jtgd;

    jtgc.u64 = cvmx_read_csr(CVMX_CIU_QLM_JTGC);
    jtgc.s.mux_sel = qlm;
    if (!OCTEON_IS_MODEL(OCTEON_CN6XXX) && !OCTEON_IS_MODEL(OCTEON_CNF7XXX))
        jtgc.s.bypass = 1<<qlm;
    cvmx_write_csr(CVMX_CIU_QLM_JTGC, jtgc.u64);
    cvmx_read_csr(CVMX_CIU_QLM_JTGC);

    jtgd.u64 = 0;
    jtgd.s.shift = 1;
    jtgd.s.shft_cnt = bits-1;
    jtgd.s.shft_reg = data;
    if (!OCTEON_IS_MODEL(OCTEON_CN56XX_PASS1_X))
        jtgd.s.select = 1 << qlm;
    cvmx_write_csr(CVMX_CIU_QLM_JTGD, jtgd.u64);
    do
    {
        jtgd.u64 = cvmx_read_csr(CVMX_CIU_QLM_JTGD);
    } while (jtgd.s.shift);
    return jtgd.s.shft_reg >> (32-bits);
}


/**
 * Shift long sequences of zeros into the QLM JTAG chain. It is
 * common to need to shift more than 32 bits of zeros into the
 * chain. This function is a convience wrapper around
 * cvmx_helper_qlm_jtag_shift() to shift more than 32 bits of
 * zeros at a time.
 *
 * @param qlm    QLM to shift zeros into
 * @param bits
 */
void cvmx_helper_qlm_jtag_shift_zeros(int qlm, int bits)
{
    while (bits > 0)
    {
        int n = bits;
        if (n > 32)
            n = 32;
        cvmx_helper_qlm_jtag_shift(qlm, n, 0);
        bits -= n;
    }
}


/**
 * Program the QLM JTAG chain into all lanes of the QLM. You must
 * have already shifted in the proper number of bits into the
 * JTAG chain. Updating invalid values can possibly cause chip damage.
 *
 * @param qlm    QLM to program
 */
void cvmx_helper_qlm_jtag_update(int qlm)
{
    cvmx_ciu_qlm_jtgc_t jtgc;
    cvmx_ciu_qlm_jtgd_t jtgd;

    jtgc.u64 = cvmx_read_csr(CVMX_CIU_QLM_JTGC);
    jtgc.s.mux_sel = qlm;
    if (!OCTEON_IS_MODEL(OCTEON_CN6XXX) && !OCTEON_IS_MODEL(OCTEON_CNF7XXX))
        jtgc.s.bypass = 1<<qlm;

    cvmx_write_csr(CVMX_CIU_QLM_JTGC, jtgc.u64);
    cvmx_read_csr(CVMX_CIU_QLM_JTGC);

    /* Update the new data */
    jtgd.u64 = 0;
    jtgd.s.update = 1;
    if (!OCTEON_IS_MODEL(OCTEON_CN56XX_PASS1_X))
        jtgd.s.select = 1 << qlm;
    cvmx_write_csr(CVMX_CIU_QLM_JTGD, jtgd.u64);
    do
    {
        jtgd.u64 = cvmx_read_csr(CVMX_CIU_QLM_JTGD);
    } while (jtgd.s.update);
}


/**
 * Load the QLM JTAG chain with data from all lanes of the QLM.
 *
 * @param qlm    QLM to program
 */
void cvmx_helper_qlm_jtag_capture(int qlm)
{
    cvmx_ciu_qlm_jtgc_t jtgc;
    cvmx_ciu_qlm_jtgd_t jtgd;

    jtgc.u64 = cvmx_read_csr(CVMX_CIU_QLM_JTGC);
    jtgc.s.mux_sel = qlm;
    if (!OCTEON_IS_MODEL(OCTEON_CN6XXX) && !OCTEON_IS_MODEL(OCTEON_CNF7XXX))
        jtgc.s.bypass = 1<<qlm;

    cvmx_write_csr(CVMX_CIU_QLM_JTGC, jtgc.u64);
    cvmx_read_csr(CVMX_CIU_QLM_JTGC);

    jtgd.u64 = 0;
    jtgd.s.capture = 1;
    if (!OCTEON_IS_MODEL(OCTEON_CN56XX_PASS1_X))
        jtgd.s.select = 1 << qlm;
    cvmx_write_csr(CVMX_CIU_QLM_JTGD, jtgd.u64);
    do
    {
        jtgd.u64 = cvmx_read_csr(CVMX_CIU_QLM_JTGD);
    } while (jtgd.s.capture);
}
