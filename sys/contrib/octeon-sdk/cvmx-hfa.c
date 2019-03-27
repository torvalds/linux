/***********************license start***************
 * Copyright (c) 2011  Cavium Inc. (support@cavium.com). All rights
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
 * Support library for the CN63XX, CN68XX hardware HFA engine.
 *
 */
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
#include <asm/octeon/cvmx.h>
#include <asm/octeon/cvmx-config.h>
#include <asm/octeon/cvmx-pko.h>
#include <asm/octeon/cvmx-helper.h>
#include <asm/octeon/cvmx-clock.h>
#include <asm/octeon/cvmx-dfa-defs.h>
#include <asm/octeon/cvmx-hfa.h>
#else
#include "executive-config.h"
#ifdef CVMX_ENABLE_DFA_FUNCTIONS

#include "cvmx-config.h"
#include "cvmx.h"
#include "cvmx-fau.h"
#include "cvmx-cmd-queue.h"
#include "cvmx-hfa.h"
#endif
#endif

#ifdef CVMX_ENABLE_DFA_FUNCTIONS

/**
 * Initialize the DFA block
 *
 * @return Zero on success, negative on failure
 */
int cvmx_hfa_initialize(void)
{
    cvmx_dfa_difctl_t control;
    cvmx_cmd_queue_result_t result;
    void *initial_base_address;
    int cmdsize;

    cmdsize = ((CVMX_FPA_DFA_POOL_SIZE - 8) / sizeof (cvmx_dfa_command_t)) *
        sizeof (cvmx_dfa_command_t);
    result = cvmx_cmd_queue_initialize(CVMX_CMD_QUEUE_DFA, 0,
                                       CVMX_FPA_DFA_POOL, cmdsize + 8);
    if (result != CVMX_CMD_QUEUE_SUCCESS)
        return -1;

    control.u64 = 0;
    control.s.dwbcnt = CVMX_FPA_DFA_POOL_SIZE / 128;
    control.s.pool = CVMX_FPA_DFA_POOL;
    control.s.size = cmdsize / sizeof(cvmx_dfa_command_t);
    CVMX_SYNCWS;
    cvmx_write_csr(CVMX_DFA_DIFCTL, control.u64);
    initial_base_address = cvmx_cmd_queue_buffer(CVMX_CMD_QUEUE_DFA);
    CVMX_SYNCWS;
    cvmx_write_csr(CVMX_DFA_DIFRDPTR, cvmx_ptr_to_phys(initial_base_address));
    cvmx_read_csr(CVMX_DFA_DIFRDPTR); /* Read to make sure setup is complete */
    return 0;
}
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
EXPORT_SYMBOL(cvmx_hfa_initialize);
#endif

/**
 * Shutdown the DFA block. DFA must be idle when
 * this function is called.
 *
 * @return Zero on success, negative on failure
 */
int cvmx_hfa_shutdown(void)
{
    if (cvmx_cmd_queue_length(CVMX_CMD_QUEUE_DFA))
    {
        cvmx_dprintf("ERROR: cvmx_hfa_shutdown: DFA not idle.\n");
        return -1;
    }
    cvmx_cmd_queue_shutdown(CVMX_CMD_QUEUE_DFA);
    return 0;
}
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
EXPORT_SYMBOL(cvmx_hfa_shutdown);
#endif

/**
 * Submit a command to the DFA block
 *
 * @param command DFA command to submit
 *
 * @return Zero on success, negative on failure
 */
int cvmx_hfa_submit(cvmx_dfa_command_t *command)
{
    cvmx_cmd_queue_result_t result = cvmx_cmd_queue_write(CVMX_CMD_QUEUE_DFA, 1, 4, command->u64);
    if (result == CVMX_CMD_QUEUE_SUCCESS)
        cvmx_write_csr(CVMX_DFA_DBELL, 1);
    return result;
}
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
EXPORT_SYMBOL(cvmx_hfa_submit);
#endif

void *hfa_bootmem_alloc (uint64_t size, uint64_t alignment)
{
    int64_t address;

    address = cvmx_bootmem_phy_alloc(size, 0, 0, alignment, 0);

    if (address > 0)
        return cvmx_phys_to_ptr(address);
    else
        return NULL;
}

#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
EXPORT_SYMBOL(hfa_bootmem_alloc);
#endif

int  hfa_bootmem_free (void *ptr, uint64_t size)
{	
	uint64_t address;
	address = cvmx_ptr_to_phys (ptr);
	return __cvmx_bootmem_phy_free (address, size, 0);
}

#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
EXPORT_SYMBOL(hfa_bootmem_free);
#endif

#endif
