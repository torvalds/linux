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
 * Functions for SRIO initialization, configuration,
 * and monitoring.
 *
 * <hr>$Revision: 41586 $<hr>
 */
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
#include <asm/octeon/cvmx.h>
#include <asm/octeon/cvmx-config.h>
#include <asm/octeon/cvmx-clock.h>
#include <asm/octeon/cvmx-helper.h>
#include <asm/octeon/cvmx-qlm.h>
#include <asm/octeon/cvmx-srio.h>
#include <asm/octeon/cvmx-pip-defs.h>
#include <asm/octeon/cvmx-sriox-defs.h>
#include <asm/octeon/cvmx-sriomaintx-defs.h>
#include <asm/octeon/cvmx-dpi-defs.h>
#else
#if !defined(__FreeBSD__) || !defined(_KERNEL)
#include "executive-config.h"
#include "cvmx-config.h"
#ifdef CVMX_ENABLE_PKO_FUNCTIONS

#include "cvmx.h"
#include "cvmx-helper.h"
#include "cvmx-srio.h"
#endif
#include "cvmx-qlm.h"
#else
#include "cvmx.h"
#include "cvmx-helper.h"
#include "cvmx-qlm.h"
#include "cvmx-srio.h"
#endif
#endif

#ifdef CVMX_ENABLE_PKO_FUNCTIONS

/**
 * @INTERNAL
 * Probe a SRIO interface and determine the number of ports
 * connected to it. The SRIO interface should still be down
 * after this call.
 *
 * @param interface Interface to probe
 *
 * @return Number of ports on the interface. Zero to disable.
 */
int __cvmx_helper_srio_probe(int interface)
{
    cvmx_sriox_status_reg_t srio0_status_reg;
    cvmx_sriox_status_reg_t srio1_status_reg;

    if (!octeon_has_feature(OCTEON_FEATURE_SRIO))
        return 0;

    /* Read MIO_QLMX_CFG CSRs to find SRIO status. */
    if (OCTEON_IS_MODEL(OCTEON_CN66XX))
    {
        int status = cvmx_qlm_get_status(0);
        int srio_port = interface - 4;
        switch(srio_port)
        {
            case 0:  /* 1x4 lane */
                if (status == 4)
                    return 2;
                break;
            case 2:  /* 2x2 lane */
                if (status == 5)
                    return 2;
                break;
            case 1: /* 4x1 long/short */
            case 3: /* 4x1 long/short */
                if (status == 6)
                    return 2;
                break;
        }
        return 0;
    }

    srio0_status_reg.u64 = cvmx_read_csr(CVMX_SRIOX_STATUS_REG(0));
    srio1_status_reg.u64 = cvmx_read_csr(CVMX_SRIOX_STATUS_REG(1));
    if (srio0_status_reg.s.srio || srio1_status_reg.s.srio)
        return 2;
    else
        return 0;
}


/**
 * @INTERNAL
 * Bringup and enable SRIO interface. After this call packet
 * I/O should be fully functional. This is called with IPD
 * enabled but PKO disabled.
 *
 * @param interface Interface to bring up
 *
 * @return Zero on success, negative on failure
 */
int __cvmx_helper_srio_enable(int interface)
{
    int num_ports = cvmx_helper_ports_on_interface(interface);
    int index;
    cvmx_sriomaintx_core_enables_t sriomaintx_core_enables;
    cvmx_sriox_imsg_ctrl_t sriox_imsg_ctrl;
    cvmx_sriox_status_reg_t srio_status_reg;
    cvmx_dpi_ctl_t dpi_ctl;
    int srio_port = interface - 4;

    /* All SRIO ports have a cvmx_srio_rx_message_header_t header
        on them that must be skipped by IPD */
    for (index=0; index<num_ports; index++)
    {
        cvmx_pip_prt_cfgx_t port_config;
        cvmx_sriox_omsg_portx_t sriox_omsg_portx;
        cvmx_sriox_omsg_sp_mrx_t sriox_omsg_sp_mrx;
        cvmx_sriox_omsg_fmp_mrx_t sriox_omsg_fmp_mrx;
        cvmx_sriox_omsg_nmp_mrx_t sriox_omsg_nmp_mrx;
        int ipd_port = cvmx_helper_get_ipd_port(interface, index);
        port_config.u64 = cvmx_read_csr(CVMX_PIP_PRT_CFGX(ipd_port));
        /* Only change the skip if the user hasn't already set it */
        if (!port_config.s.skip)
        {
            port_config.s.skip = sizeof(cvmx_srio_rx_message_header_t);
            cvmx_write_csr(CVMX_PIP_PRT_CFGX(ipd_port), port_config.u64);
        }

        /* Enable TX with PKO */
        sriox_omsg_portx.u64 = cvmx_read_csr(CVMX_SRIOX_OMSG_PORTX(index, srio_port));
        sriox_omsg_portx.s.port = (srio_port) * 2 + index;
        sriox_omsg_portx.s.enable = 1;
        cvmx_write_csr(CVMX_SRIOX_OMSG_PORTX(index, srio_port), sriox_omsg_portx.u64);

        /* Allow OMSG controller to send regardless of the state of any other
            controller. Allow messages to different IDs and MBOXes to go in
            parallel */
        sriox_omsg_sp_mrx.u64 = 0;
        sriox_omsg_sp_mrx.s.xmbox_sp = 1;
        sriox_omsg_sp_mrx.s.ctlr_sp = 1;
        sriox_omsg_sp_mrx.s.ctlr_fmp = 1;
        sriox_omsg_sp_mrx.s.ctlr_nmp = 1;
        sriox_omsg_sp_mrx.s.id_sp = 1;
        sriox_omsg_sp_mrx.s.id_fmp = 1;
        sriox_omsg_sp_mrx.s.id_nmp = 1;
        sriox_omsg_sp_mrx.s.mbox_sp = 1;
        sriox_omsg_sp_mrx.s.mbox_fmp = 1;
        sriox_omsg_sp_mrx.s.mbox_nmp = 1;
        sriox_omsg_sp_mrx.s.all_psd = 1;
        cvmx_write_csr(CVMX_SRIOX_OMSG_SP_MRX(index, srio_port), sriox_omsg_sp_mrx.u64);

        /* Allow OMSG controller to send regardless of the state of any other
            controller. Allow messages to different IDs and MBOXes to go in
            parallel */
        sriox_omsg_fmp_mrx.u64 = 0;
        sriox_omsg_fmp_mrx.s.ctlr_sp = 1;
        sriox_omsg_fmp_mrx.s.ctlr_fmp = 1;
        sriox_omsg_fmp_mrx.s.ctlr_nmp = 1;
        sriox_omsg_fmp_mrx.s.id_sp = 1;
        sriox_omsg_fmp_mrx.s.id_fmp = 1;
        sriox_omsg_fmp_mrx.s.id_nmp = 1;
        sriox_omsg_fmp_mrx.s.mbox_sp = 1;
        sriox_omsg_fmp_mrx.s.mbox_fmp = 1;
        sriox_omsg_fmp_mrx.s.mbox_nmp = 1;
        sriox_omsg_fmp_mrx.s.all_psd = 1;
        cvmx_write_csr(CVMX_SRIOX_OMSG_FMP_MRX(index, srio_port), sriox_omsg_fmp_mrx.u64);

        /* Once the first part of a message is accepted, always acept the rest
            of the message */
        sriox_omsg_nmp_mrx.u64 = 0;
        sriox_omsg_nmp_mrx.s.all_sp = 1;
        sriox_omsg_nmp_mrx.s.all_fmp = 1;
        sriox_omsg_nmp_mrx.s.all_nmp = 1;
        cvmx_write_csr(CVMX_SRIOX_OMSG_NMP_MRX(index, srio_port), sriox_omsg_nmp_mrx.u64);

    }

    /* Choose the receive controller based on the mailbox */
    sriox_imsg_ctrl.u64 = cvmx_read_csr(CVMX_SRIOX_IMSG_CTRL(srio_port));
    sriox_imsg_ctrl.s.prt_sel = 0;
    sriox_imsg_ctrl.s.mbox = 0xa;
    cvmx_write_csr(CVMX_SRIOX_IMSG_CTRL(srio_port), sriox_imsg_ctrl.u64);

    /* DPI must be enabled for us to RX messages */
    dpi_ctl.u64 = cvmx_read_csr(CVMX_DPI_CTL);
    dpi_ctl.s.clk = 1;
    dpi_ctl.s.en = 1;
    cvmx_write_csr(CVMX_DPI_CTL, dpi_ctl.u64);

    /* Make sure register access is allowed */
    srio_status_reg.u64 = cvmx_read_csr(CVMX_SRIOX_STATUS_REG(srio_port));
    if (!srio_status_reg.s.access)
        return 0;

    /* Enable RX */
    if (!cvmx_srio_config_read32(srio_port, 0, -1, 0, 0,
        CVMX_SRIOMAINTX_CORE_ENABLES(srio_port), &sriomaintx_core_enables.u32))
    {
        sriomaintx_core_enables.s.imsg0 = 1;
        sriomaintx_core_enables.s.imsg1 = 1;
        cvmx_srio_config_write32(srio_port, 0, -1, 0, 0,
            CVMX_SRIOMAINTX_CORE_ENABLES(srio_port), sriomaintx_core_enables.u32);
    }

    return 0;
}

/**
 * @INTERNAL
 * Return the link state of an IPD/PKO port as returned by SRIO link status.
 *
 * @param ipd_port IPD/PKO port to query
 *
 * @return Link state
 */
cvmx_helper_link_info_t __cvmx_helper_srio_link_get(int ipd_port)
{
    int interface = cvmx_helper_get_interface_num(ipd_port);
    int srio_port = interface - 4;
    cvmx_helper_link_info_t result;
    cvmx_sriox_status_reg_t srio_status_reg;
    cvmx_sriomaintx_port_0_err_stat_t sriomaintx_port_0_err_stat;
    cvmx_sriomaintx_port_0_ctl_t sriomaintx_port_0_ctl;
    cvmx_sriomaintx_port_0_ctl2_t sriomaintx_port_0_ctl2;

    result.u64 = 0;

    /* Make sure register access is allowed */
    srio_status_reg.u64 = cvmx_read_csr(CVMX_SRIOX_STATUS_REG(srio_port));
    if (!srio_status_reg.s.access)
        return result;

    /* Read the port link status */
    if (cvmx_srio_config_read32(srio_port, 0, -1, 0, 0,
        CVMX_SRIOMAINTX_PORT_0_ERR_STAT(srio_port),
        &sriomaintx_port_0_err_stat.u32))
        return result;

    /* Return if link is down */
    if (!sriomaintx_port_0_err_stat.s.pt_ok)
        return result;

    /* Read the port link width and speed */
    if (cvmx_srio_config_read32(srio_port, 0, -1, 0, 0,
        CVMX_SRIOMAINTX_PORT_0_CTL(srio_port),
        &sriomaintx_port_0_ctl.u32))
        return result;
    if (cvmx_srio_config_read32(srio_port, 0, -1, 0, 0,
        CVMX_SRIOMAINTX_PORT_0_CTL2(srio_port),
        &sriomaintx_port_0_ctl2.u32))
        return result;

    /* Link is up */
    result.s.full_duplex = 1;
    result.s.link_up = 1;
    switch (sriomaintx_port_0_ctl2.s.sel_baud)
    {
        case 1:
            result.s.speed = 1250;
            break;
        case 2:
            result.s.speed = 2500;
            break;
        case 3:
            result.s.speed = 3125;
            break;
        case 4:
            result.s.speed = 5000;
            break;
        case 5:
            result.s.speed = 6250;
            break;
        default:
            result.s.speed = 0;
            break;
    }
    switch (sriomaintx_port_0_ctl.s.it_width)
    {
        case 2: /* Four lanes */
            result.s.speed += 40000;
            break;
        case 3: /* Two lanes */
            result.s.speed += 20000;
            break;
        default: /* One lane */
            result.s.speed += 10000;
            break;
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
int __cvmx_helper_srio_link_set(int ipd_port, cvmx_helper_link_info_t link_info)
{
    return 0;
}

#endif /* CVMX_ENABLE_PKO_FUNCTIONS */

