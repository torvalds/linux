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
 * Interface to RAID block. This is not available on all chips.
 *
 * <hr>$Revision: 70030 $<hr>
 */
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
#include <linux/module.h>
#include <asm/octeon/cvmx.h>
#include <asm/octeon/cvmx-config.h>
#include <asm/octeon/cvmx-cmd-queue.h>
#include <asm/octeon/cvmx-raid.h>
#else
#include "executive-config.h"
#include "cvmx-config.h"
#include "cvmx.h"
#include "cvmx-cmd-queue.h"
#include "cvmx-raid.h"
#endif

#ifdef CVMX_ENABLE_PKO_FUNCTIONS

/**
 * Initialize the RAID block
 *
 * @param polynomial Coefficients for the RAID polynomial
 *
 * @return Zero on success, negative on failure
 */
int cvmx_raid_initialize(cvmx_rad_reg_polynomial_t polynomial)
{
    cvmx_cmd_queue_result_t result;
    cvmx_rad_reg_cmd_buf_t rad_reg_cmd_buf;

    cvmx_write_csr(CVMX_RAD_REG_POLYNOMIAL, polynomial.u64);

    result = cvmx_cmd_queue_initialize(CVMX_CMD_QUEUE_RAID, 0,
                                       CVMX_FPA_OUTPUT_BUFFER_POOL,
                                       CVMX_FPA_OUTPUT_BUFFER_POOL_SIZE);
    if (result != CVMX_CMD_QUEUE_SUCCESS)
        return -1;

    rad_reg_cmd_buf.u64 = 0;
    rad_reg_cmd_buf.s.dwb = CVMX_FPA_OUTPUT_BUFFER_POOL_SIZE/128;
    rad_reg_cmd_buf.s.pool = CVMX_FPA_OUTPUT_BUFFER_POOL;
    rad_reg_cmd_buf.s.size = CVMX_FPA_OUTPUT_BUFFER_POOL_SIZE/8;
    rad_reg_cmd_buf.s.ptr = cvmx_ptr_to_phys(cvmx_cmd_queue_buffer(CVMX_CMD_QUEUE_RAID))>>7;
    cvmx_write_csr(CVMX_RAD_REG_CMD_BUF, rad_reg_cmd_buf.u64);
    return 0;
}
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
EXPORT_SYMBOL(cvmx_raid_initialize);
#endif

/**
 * Shutdown the RAID block. RAID must be idle when
 * this function is called.
 *
 * @return Zero on success, negative on failure
 */
int cvmx_raid_shutdown(void)
{
    cvmx_rad_reg_ctl_t rad_reg_ctl;

    if (cvmx_cmd_queue_length(CVMX_CMD_QUEUE_RAID))
    {
        cvmx_dprintf("ERROR: cvmx_raid_shutdown: RAID not idle.\n");
        return -1;
    }

    rad_reg_ctl.u64 = cvmx_read_csr(CVMX_RAD_REG_CTL);
    rad_reg_ctl.s.reset = 1;
    cvmx_write_csr(CVMX_RAD_REG_CTL, rad_reg_ctl.u64);
    cvmx_wait(100);

    cvmx_cmd_queue_shutdown(CVMX_CMD_QUEUE_RAID);
    cvmx_write_csr(CVMX_RAD_REG_CMD_BUF, 0);
    return 0;
}
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
EXPORT_SYMBOL(cvmx_raid_shutdown);
#endif

/**
 * Submit a command to the RAID block
 *
 * @param num_words Number of command words to submit
 * @param words     Command words
 *
 * @return Zero on success, negative on failure
 */
int cvmx_raid_submit(int num_words, cvmx_raid_word_t words[])
{
    cvmx_cmd_queue_result_t result = cvmx_cmd_queue_write(CVMX_CMD_QUEUE_RAID, 1, num_words, (uint64_t *)words);
    if (result == CVMX_CMD_QUEUE_SUCCESS)
        cvmx_write_csr(CVMX_ADDR_DID(CVMX_FULL_DID(14, 0)), num_words);
    return result;
}
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
EXPORT_SYMBOL(cvmx_raid_submit);
#endif
#endif
