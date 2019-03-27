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
 * Functions for SPI initialization, configuration,
 * and monitoring.
 *
 * <hr>$Revision: 70030 $<hr>
 */
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
#include <asm/octeon/cvmx.h>
#include <asm/octeon/cvmx-config.h>
#ifdef CVMX_ENABLE_PKO_FUNCTIONS
#include <asm/octeon/cvmx-spi.h>
#include <asm/octeon/cvmx-helper.h>
#endif
#include <asm/octeon/cvmx-pko-defs.h>
#include <asm/octeon/cvmx-pip-defs.h>
#else
#if !defined(__FreeBSD__) || !defined(_KERNEL)
#include "executive-config.h"
#include "cvmx-config.h"
#ifdef CVMX_ENABLE_PKO_FUNCTIONS

#include "cvmx.h"
#include "cvmx-spi.h"
#include "cvmx-sysinfo.h"
#include "cvmx-helper.h"
#endif
#else
#include "cvmx.h"
#include "cvmx-spi.h"
#include "cvmx-sysinfo.h"
#include "cvmx-helper.h"
#endif
#endif

#ifdef CVMX_ENABLE_PKO_FUNCTIONS

/* CVMX_HELPER_SPI_TIMEOUT is used to determine how long the SPI initialization
    routines wait for SPI training. You can override the value using
    executive-config.h if necessary */
#ifndef CVMX_HELPER_SPI_TIMEOUT
#define CVMX_HELPER_SPI_TIMEOUT 10
#endif

int __cvmx_helper_spi_enumerate(int interface)
{
#if defined(OCTEON_VENDOR_LANNER)
    if (cvmx_sysinfo_get()->board_type == CVMX_BOARD_TYPE_CUST_LANNER_MR955)
    {
        cvmx_pko_reg_crc_enable_t enable;

        enable.u64 = cvmx_read_csr(CVMX_PKO_REG_CRC_ENABLE);
        enable.s.enable &= 0xffff << (16 - (interface*16));
        cvmx_write_csr(CVMX_PKO_REG_CRC_ENABLE, enable.u64);

	if (interface == 1)
	    return 12;
	/* XXX This is not entirely true.  */
	return 0;
    }
#endif

#if defined(OCTEON_VENDOR_RADISYS)
    if (cvmx_sysinfo_get()->board_type == CVMX_BOARD_TYPE_CUST_RADISYS_RSYS4GBE) {
	    if (interface == 0)
		    return 13;
	    if (interface == 1)
		    return 8;
	    return 0;
    }
#endif

	if ((cvmx_sysinfo_get()->board_type != CVMX_BOARD_TYPE_SIM) &&
	    cvmx_spi4000_is_present(interface))
		return 10;
	else
		return 16;
}

/**
 * @INTERNAL
 * Probe a SPI interface and determine the number of ports
 * connected to it. The SPI interface should still be down after
 * this call.
 *
 * @param interface Interface to probe
 *
 * @return Number of ports on the interface. Zero to disable.
 */
int __cvmx_helper_spi_probe(int interface)
{
	int num_ports = __cvmx_helper_spi_enumerate(interface);

	if (num_ports == 16) {
		cvmx_pko_reg_crc_enable_t enable;
		/*
		 * Unlike the SPI4000, most SPI devices don't
		 * automatically put on the L2 CRC. For everything
		 * except for the SPI4000 have PKO append the L2 CRC
		 * to the packet
		 */
		enable.u64 = cvmx_read_csr(CVMX_PKO_REG_CRC_ENABLE);
		enable.s.enable |= 0xffff << (interface*16);
		cvmx_write_csr(CVMX_PKO_REG_CRC_ENABLE, enable.u64);
	}
	__cvmx_helper_setup_gmx(interface, num_ports);
	return num_ports;
}


/**
 * @INTERNAL
 * Bringup and enable a SPI interface. After this call packet I/O
 * should be fully functional. This is called with IPD enabled but
 * PKO disabled.
 *
 * @param interface Interface to bring up
 *
 * @return Zero on success, negative on failure
 */
int __cvmx_helper_spi_enable(int interface)
{
    /* Normally the ethernet L2 CRC is checked and stripped in the GMX block.
        When you are using SPI, this isn' the case and IPD needs to check
        the L2 CRC */
    int num_ports = cvmx_helper_ports_on_interface(interface);
    int ipd_port;
    for (ipd_port=interface*16; ipd_port<interface*16+num_ports; ipd_port++)
    {
        cvmx_pip_prt_cfgx_t port_config;
        port_config.u64 = cvmx_read_csr(CVMX_PIP_PRT_CFGX(ipd_port));
        port_config.s.crc_en = 1;
#ifdef OCTEON_VENDOR_RADISYS
	/*
	 * Incoming packets on the RSYS4GBE have the FCS stripped.
	 */
	if (cvmx_sysinfo_get()->board_type == CVMX_BOARD_TYPE_CUST_RADISYS_RSYS4GBE)
		port_config.s.crc_en = 0;
#endif
        cvmx_write_csr(CVMX_PIP_PRT_CFGX(ipd_port), port_config.u64);
    }

    if (cvmx_sysinfo_get()->board_type != CVMX_BOARD_TYPE_SIM)
    {
        cvmx_spi_start_interface(interface, CVMX_SPI_MODE_DUPLEX, CVMX_HELPER_SPI_TIMEOUT, num_ports);
        if (cvmx_spi4000_is_present(interface))
            cvmx_spi4000_initialize(interface);
    }
    return 0;
}

/**
 * @INTERNAL
 * Return the link state of an IPD/PKO port as returned by
 * auto negotiation. The result of this function may not match
 * Octeon's link config if auto negotiation has changed since
 * the last call to cvmx_helper_link_set().
 *
 * @param ipd_port IPD/PKO port to query
 *
 * @return Link state
 */
cvmx_helper_link_info_t __cvmx_helper_spi_link_get(int ipd_port)
{
    cvmx_helper_link_info_t result;
    int interface = cvmx_helper_get_interface_num(ipd_port);
    int index = cvmx_helper_get_interface_index_num(ipd_port);
    result.u64 = 0;

    if (cvmx_sysinfo_get()->board_type == CVMX_BOARD_TYPE_SIM)
    {
        /* The simulator gives you a simulated full duplex link */
        result.s.link_up = 1;
        result.s.full_duplex = 1;
        result.s.speed = 10000;
    }
    else if (cvmx_spi4000_is_present(interface))
    {
        cvmx_gmxx_rxx_rx_inbnd_t inband = cvmx_spi4000_check_speed(interface, index);
        result.s.link_up = inband.s.status;
        result.s.full_duplex = inband.s.duplex;
        switch (inband.s.speed)
        {
            case 0: /* 10 Mbps */
                result.s.speed = 10;
                break;
            case 1: /* 100 Mbps */
                result.s.speed = 100;
                break;
            case 2: /* 1 Gbps */
                result.s.speed = 1000;
                break;
            case 3: /* Illegal */
                result.s.speed = 0;
                result.s.link_up = 0;
                break;
        }
    }
    else
    {
        /* For generic SPI we can't determine the link, just return some
            sane results */
        result.s.link_up = 1;
        result.s.full_duplex = 1;
        result.s.speed = 10000;
    }
    return result;
}


/**
 * @INTERNAL
 * Configure an IPD/PKO port for the specified link state. This
 * function does not influence auto negotiation at the PHY level.
 * The passed link state must always match the link state returned
 * by cvmx_helper_link_get(). It is normally best to use
 * cvmx_helper_link_autoconf() instead.
 *
 * @param ipd_port  IPD/PKO port to configure
 * @param link_info The new link state
 *
 * @return Zero on success, negative on failure
 */
int __cvmx_helper_spi_link_set(int ipd_port, cvmx_helper_link_info_t link_info)
{
    /* Nothing to do. If we have a SPI4000 then the setup was already performed
        by cvmx_spi4000_check_speed(). If not then there isn't any link
        info */
    return 0;
}

#endif /* CVMX_ENABLE_PKO_FUNCTIONS */

