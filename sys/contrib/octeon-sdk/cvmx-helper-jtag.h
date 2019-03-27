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
 *  Helper utilities for qlm_jtag.
 *
 * <hr>$Revision: 41586 $<hr>
 */

#ifndef __CVMX_HELPER_JTAG_H__
#define __CVMX_HELPER_JTAG_H__

/**
 * Initialize the internal QLM JTAG logic to allow programming
 * of the JTAG chain by the cvmx_helper_qlm_jtag_*() functions.
 * These functions should only be used at the direction of Cavium
 * Networks. Programming incorrect values into the JTAG chain
 * can cause chip damage.
 */
extern void cvmx_helper_qlm_jtag_init(void);

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
extern uint32_t cvmx_helper_qlm_jtag_shift(int qlm, int bits, uint32_t data);

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
extern void cvmx_helper_qlm_jtag_shift_zeros(int qlm, int bits);

/**
 * Program the QLM JTAG chain into all lanes of the QLM. You must
 * have already shifted in the proper number of bits into the
 * JTAG chain. Updating invalid values can possibly cause chip damage.
 *
 * @param qlm    QLM to program
 */
extern void cvmx_helper_qlm_jtag_update(int qlm);

/**
 * Load the QLM JTAG chain with data from all lanes of the QLM.
 *
 * @param qlm    QLM to program
 */
extern void cvmx_helper_qlm_jtag_capture(int qlm);

#endif  /* __CVMX_HELPER_JTAG_H__ */
