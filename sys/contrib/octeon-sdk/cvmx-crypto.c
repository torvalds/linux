
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
 * Helper utilities for enabling crypto.
 *
 * <hr>$Revision: $<hr>
 */
#include "executive-config.h"
#include "cvmx-config.h"
#include "cvmx.h"

int cvmx_crypto_dormant_enable(uint64_t key)
{
    if (octeon_has_feature(OCTEON_FEATURE_CRYPTO))
        return 1;

    if (octeon_has_feature(OCTEON_FEATURE_DORM_CRYPTO)) {
        cvmx_rnm_eer_key_t v;
        v.s.key = key;
        cvmx_write_csr(CVMX_RNM_EER_KEY, v.u64);
    }

    return octeon_has_feature(OCTEON_FEATURE_CRYPTO);
}

uint64_t cvmx_crypto_dormant_dbg(void)
{
    cvmx_rnm_eer_dbg_t dbg;

    if (!octeon_has_feature(OCTEON_FEATURE_DORM_CRYPTO))
        return 0;

    dbg.u64 = cvmx_read_csr(CVMX_RNM_EER_DBG);
    return dbg.s.dat;
}


