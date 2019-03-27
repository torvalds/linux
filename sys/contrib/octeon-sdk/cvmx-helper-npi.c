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
 * Functions for NPI initialization, configuration,
 * and monitoring.
 *
 * <hr>$Revision: 70030 $<hr>
 */
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
#include <asm/octeon/cvmx.h>
#include <asm/octeon/cvmx-config.h>
#ifdef CVMX_ENABLE_PKO_FUNCTIONS
#include <asm/octeon/cvmx-helper.h>
#include <asm/octeon/cvmx-pko.h>
#include <asm/octeon/cvmx-pexp-defs.h>
#include <asm/octeon/cvmx-sli-defs.h>
#endif
#include <asm/octeon/cvmx-pip-defs.h>
#else
#if !defined(__FreeBSD__) || !defined(_KERNEL)
#include "executive-config.h"
#include "cvmx-config.h"
#include "cvmx.h"
#include "cvmx-pko.h"
#ifdef CVMX_ENABLE_PKO_FUNCTIONS
#include "cvmx-helper.h"
#endif
#else
#include "cvmx.h"
#include "cvmx-pko.h"
#include "cvmx-helper.h"
#endif
#endif

#ifdef CVMX_ENABLE_PKO_FUNCTIONS

/**
 * @INTERNAL
 * Probe a NPI interface and determine the number of ports
 * connected to it. The NPI interface should still be down
 * after this call.
 *
 * @param interface Interface to probe
 *
 * @return Number of ports on the interface. Zero to disable.
 */
int __cvmx_helper_npi_probe(int interface)
{
#if CVMX_PKO_QUEUES_PER_PORT_PCI > 0
    if (OCTEON_IS_MODEL(OCTEON_CN68XX))
        return 32;
#if 0
    /* Technically CN30XX, CN31XX, and CN50XX contain packet engines, but
        nobody ever uses them. Since this is the case, we disable them here */
    else if (OCTEON_IS_MODEL(OCTEON_CN31XX) || OCTEON_IS_MODEL(OCTEON_CN50XX))
        return 2;
    else if (OCTEON_IS_MODEL(OCTEON_CN30XX))
        return 1;
#endif
    else if (!(OCTEON_IS_MODEL(OCTEON_CN52XX_PASS1_X) || OCTEON_IS_MODEL(OCTEON_CN56XX_PASS1_X) || OCTEON_IS_MODEL(OCTEON_CN31XX) || OCTEON_IS_MODEL(OCTEON_CN50XX) || OCTEON_IS_MODEL(OCTEON_CN30XX)))
        return 4; /* The packet engines didn't exist before cn56xx pass 2 */
#endif
    return 0;
}


/**
 * @INTERNAL
 * Bringup and enable a NPI interface. After this call packet
 * I/O should be fully functional. This is called with IPD
 * enabled but PKO disabled.
 *
 * @param interface Interface to bring up
 *
 * @return Zero on success, negative on failure
 */
int __cvmx_helper_npi_enable(int interface)
{
    int num_ports = cvmx_helper_ports_on_interface(interface);

    /* On CN50XX, CN52XX, and CN56XX we need to disable length checking
        so packet < 64 bytes and jumbo frames don't get errors */
    if (!OCTEON_IS_MODEL(OCTEON_CN3XXX) && !OCTEON_IS_MODEL(OCTEON_CN58XX))
    {
        int port;
        for (port=0; port<num_ports; port++)
        {
            cvmx_pip_prt_cfgx_t port_cfg;
            int ipd_port = (OCTEON_IS_MODEL(OCTEON_CN68XX)) ?
	        cvmx_helper_get_pknd(interface, port) :
		cvmx_helper_get_ipd_port(interface, port);
            port_cfg.u64 = cvmx_read_csr(CVMX_PIP_PRT_CFGX(ipd_port));
            port_cfg.s.lenerr_en = 0;
            port_cfg.s.maxerr_en = 0;
            port_cfg.s.minerr_en = 0;
            cvmx_write_csr(CVMX_PIP_PRT_CFGX(ipd_port), port_cfg.u64);

            if (OCTEON_IS_MODEL(OCTEON_CN68XX))
            {
                /*
                 * Set up pknd and bpid
                 */
		cvmx_sli_portx_pkind_t config;
		config.u64 = cvmx_read_csr(CVMX_PEXP_SLI_PORTX_PKIND(port));
		config.s.bpkind = cvmx_helper_get_bpid(interface, port);
		config.s.pkind = cvmx_helper_get_pknd(interface, port);
		cvmx_write_csr(CVMX_PEXP_SLI_PORTX_PKIND(port), config.u64);
            }
        }
    }

    if (OCTEON_IS_MODEL(OCTEON_CN68XX))
    {
	/*
	 * Set up pko pipes.
	 */
	cvmx_sli_tx_pipe_t config;
	config.u64 = cvmx_read_csr(CVMX_PEXP_SLI_TX_PIPE);
	config.s.base = __cvmx_pko_get_pipe (interface, 0);
#ifdef CVMX_HELPER_NPI_MAX_PIPES
	config.s.nump = CVMX_HELPER_NPI_MAX_PIPES;
#else
	config.s.nump = num_ports;
#endif
	cvmx_write_csr(CVMX_PEXP_SLI_TX_PIPE, config.u64);
    }


    /* Enables are controlled by the remote host, so nothing to do here */
    return 0;
}

#endif /* CVMX_ENABLE_PKO_FUNCTIONS */

