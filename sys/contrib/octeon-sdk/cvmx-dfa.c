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
 * Support library for the CN31XX, CN38XX, and CN58XX hardware DFA engine.
 *
 * <hr>$Revision: 70030 $<hr>
 */
#include "executive-config.h"
#ifdef CVMX_ENABLE_DFA_FUNCTIONS

#include "cvmx-config.h"
#include "cvmx.h"
#include "cvmx-fau.h"
#include "cvmx-dfa.h"



/**
 * Initialize the DFA hardware before use
 */
int cvmx_dfa_initialize(void)
{
    cvmx_dfa_difctl_t control;
    void *initial_base_address;
    cvmx_dfa_state_t initial_state;
    if (!octeon_has_feature(OCTEON_FEATURE_DFA))
    {
        cvmx_dprintf("ERROR: attempting to initialize DFA when no DFA hardware present\n.");
        return -1;
    }

    control.u64 = 0;
    control.s.dwbcnt = CVMX_FPA_DFA_POOL_SIZE / 128;
    control.s.pool = CVMX_FPA_DFA_POOL;
    control.s.size = (CVMX_FPA_DFA_POOL_SIZE - 8) / sizeof(cvmx_dfa_command_t);
    CVMX_SYNCWS;
    cvmx_write_csr(CVMX_DFA_DIFCTL, control.u64);

    initial_base_address = cvmx_fpa_alloc(CVMX_FPA_DFA_POOL);

    initial_state.u64 = 0;
    initial_state.s.base_address_div16 = (CAST64(initial_base_address))/16;
    cvmx_fau_atomic_write64(CVMX_FAU_DFA_STATE, initial_state.u64);

    CVMX_SYNCWS;
    cvmx_write_csr(CVMX_DFA_DIFRDPTR, cvmx_ptr_to_phys(initial_base_address));

    return 0;
}


/**
 * Shutdown and cleanup resources used by the DFA
 */
void cvmx_dfa_shutdown(void)
{
    void *final_base_address;
    cvmx_dfa_state_t final_state;

    CVMX_SYNCWS;

    final_state.u64 = cvmx_fau_fetch_and_add64(CVMX_FAU_DFA_STATE, 0);

    // make sure the carry is clear
    final_base_address = CASTPTR(void, (final_state.s2.base_address_div32 * 32ull));

    if (final_base_address)
    {
        cvmx_fpa_free(final_base_address, CVMX_FPA_DFA_POOL, 0);
    }

    CVMX_SYNCWS;
    final_state.u64 = 0;
    cvmx_fau_atomic_write64(CVMX_FAU_DFA_STATE, final_state.u64);
}

#endif
