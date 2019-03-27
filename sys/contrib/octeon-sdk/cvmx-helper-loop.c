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
 * Functions for LOOP initialization, configuration,
 * and monitoring.
 *
 * <hr>$Revision: 70030 $<hr>
 */
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
#include <asm/octeon/cvmx.h>
#include <asm/octeon/cvmx-config.h>
#ifdef CVMX_ENABLE_PKO_FUNCTIONS
#include <asm/octeon/cvmx-helper.h>
#endif
#include <asm/octeon/cvmx-pip-defs.h>
#else
#if !defined(__FreeBSD__) || !defined(_KERNEL)
#include "executive-config.h"
#include "cvmx-config.h"
#ifdef CVMX_ENABLE_PKO_FUNCTIONS

#include "cvmx.h"
#include "cvmx-helper.h"
#endif
#else
#include "cvmx.h"
#include "cvmx-helper.h"
#endif
#endif
#ifdef CVMX_ENABLE_PKO_FUNCTIONS


int __cvmx_helper_loop_enumerate(int interface)
{
	return (OCTEON_IS_MODEL(OCTEON_CN68XX) ? 8 : 4);
}

/**
 * @INTERNAL
 * Probe a LOOP interface and determine the number of ports
 * connected to it. The LOOP interface should still be down
 * after this call.
 *
 * @param interface Interface to probe
 *
 * @return Number of ports on the interface. Zero to disable.
 */
int __cvmx_helper_loop_probe(int interface)
{
	return __cvmx_helper_loop_enumerate(interface);
}


/**
 * @INTERNAL
 * Bringup and enable a LOOP interface. After this call packet
 * I/O should be fully functional. This is called with IPD
 * enabled but PKO disabled.
 *
 * @param interface Interface to bring up
 *
 * @return Zero on success, negative on failure
 */
int __cvmx_helper_loop_enable(int interface)
{
    cvmx_pip_prt_cfgx_t port_cfg;
    int num_ports, index;
    unsigned long offset;

    num_ports = __cvmx_helper_get_num_ipd_ports(interface);

    /* 
     * We need to disable length checking so packet < 64 bytes and jumbo
     * frames don't get errors
     */
    for (index = 0; index < num_ports; index++) {
        offset = ((octeon_has_feature(OCTEON_FEATURE_PKND)) ?
	    cvmx_helper_get_pknd(interface, index) :
            cvmx_helper_get_ipd_port(interface, index));

        port_cfg.u64 = cvmx_read_csr(CVMX_PIP_PRT_CFGX(offset));
        port_cfg.s.maxerr_en = 0;
        port_cfg.s.minerr_en = 0;
        cvmx_write_csr(CVMX_PIP_PRT_CFGX(offset), port_cfg.u64);
    }

    /*
     * Disable FCS stripping for loopback ports
     */
    if (!octeon_has_feature(OCTEON_FEATURE_PKND)) {
        cvmx_ipd_sub_port_fcs_t ipd_sub_port_fcs;
        ipd_sub_port_fcs.u64 = cvmx_read_csr(CVMX_IPD_SUB_PORT_FCS);
        ipd_sub_port_fcs.s.port_bit2 = 0;
        cvmx_write_csr(CVMX_IPD_SUB_PORT_FCS, ipd_sub_port_fcs.u64);
    }

    return 0;
}

#endif /* CVMX_ENABLE_PKO_FUNCTIONS */

