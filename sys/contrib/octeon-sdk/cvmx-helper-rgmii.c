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
 * Functions for RGMII/GMII/MII initialization, configuration,
 * and monitoring.
 *
 * <hr>$Revision: 70030 $<hr>
 */
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
#include <asm/octeon/cvmx.h>
#include <asm/octeon/cvmx-config.h>
#ifdef CVMX_ENABLE_PKO_FUNCTIONS
#include <asm/octeon/cvmx-pko.h>
#include <asm/octeon/cvmx-helper.h>
#include <asm/octeon/cvmx-helper-board.h>
#endif
#include <asm/octeon/cvmx-asxx-defs.h>
#include <asm/octeon/cvmx-gmxx-defs.h>
#include <asm/octeon/cvmx-pko-defs.h>
#include <asm/octeon/cvmx-npi-defs.h>
#include <asm/octeon/cvmx-dbg-defs.h>

#else
#if !defined(__FreeBSD__) || !defined(_KERNEL)
#include "executive-config.h"
#include "cvmx-config.h"
#ifdef CVMX_ENABLE_PKO_FUNCTIONS

#include "cvmx.h"
#include "cvmx-sysinfo.h"
#include "cvmx-mdio.h"
#include "cvmx-pko.h"
#include "cvmx-helper.h"
#include "cvmx-helper-board.h"
#endif
#else
#include "cvmx.h"
#include "cvmx-sysinfo.h"
#include "cvmx-mdio.h"
#include "cvmx-pko.h"
#include "cvmx-helper.h"
#include "cvmx-helper-board.h"
#endif
#endif

#ifdef CVMX_ENABLE_PKO_FUNCTIONS

/**
 * @INTERNAL
 * Probe RGMII ports and determine the number present
 *
 * @param interface Interface to probe
 *
 * @return Number of RGMII/GMII/MII ports (0-4).
 */
int __cvmx_helper_rgmii_probe(int interface)
{
    int num_ports = 0;
    cvmx_gmxx_inf_mode_t mode;
    mode.u64 = cvmx_read_csr(CVMX_GMXX_INF_MODE(interface));

    if (mode.s.type)
    {
        if (OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN58XX))
        {
            cvmx_dprintf("ERROR: RGMII initialize called in SPI interface\n");
        }
        else if (OCTEON_IS_MODEL(OCTEON_CN31XX) || OCTEON_IS_MODEL(OCTEON_CN30XX) || OCTEON_IS_MODEL(OCTEON_CN50XX))
        {
            /* On these chips "type" says we're in GMII/MII mode. This
                limits us to 2 ports */
            num_ports = 2;
        }
        else
        {
            cvmx_dprintf("ERROR: Unsupported Octeon model in %s\n", __FUNCTION__);
        }
    }
    else
    {
        if (OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN58XX))
        {
            num_ports = 4;
        }
        else if (OCTEON_IS_MODEL(OCTEON_CN31XX) || OCTEON_IS_MODEL(OCTEON_CN30XX) || OCTEON_IS_MODEL(OCTEON_CN50XX))
        {
            num_ports = 3;
        }
        else
        {
            cvmx_dprintf("ERROR: Unsupported Octeon model in %s\n", __FUNCTION__);
        }
    }
    return num_ports;
}


/**
 * Put an RGMII interface in loopback mode. Internal packets sent
 * out will be received back again on the same port. Externally
 * received packets will echo back out.
 *
 * @param port   IPD port number to loop.
 */
void cvmx_helper_rgmii_internal_loopback(int port)
{
    int interface = (port >> 4) & 1;
    int index = port & 0xf;
    uint64_t tmp;

    cvmx_gmxx_prtx_cfg_t gmx_cfg;
    gmx_cfg.u64 = 0;
    gmx_cfg.s.duplex = 1;
    gmx_cfg.s.slottime = 1;
    gmx_cfg.s.speed = 1;
    cvmx_write_csr(CVMX_GMXX_TXX_CLK(index, interface), 1);
    cvmx_write_csr(CVMX_GMXX_TXX_SLOT(index, interface), 0x200);
    cvmx_write_csr(CVMX_GMXX_TXX_BURST(index, interface), 0x2000);
    cvmx_write_csr(CVMX_GMXX_PRTX_CFG(index, interface), gmx_cfg.u64);
    tmp = cvmx_read_csr(CVMX_ASXX_PRT_LOOP(interface));
    cvmx_write_csr(CVMX_ASXX_PRT_LOOP(interface), (1 << index) | tmp);
    tmp = cvmx_read_csr(CVMX_ASXX_TX_PRT_EN(interface));
    cvmx_write_csr(CVMX_ASXX_TX_PRT_EN(interface), (1 << index) | tmp);
    tmp = cvmx_read_csr(CVMX_ASXX_RX_PRT_EN(interface));
    cvmx_write_csr(CVMX_ASXX_RX_PRT_EN(interface), (1 << index) | tmp);
    gmx_cfg.s.en = 1;
    cvmx_write_csr(CVMX_GMXX_PRTX_CFG(index, interface), gmx_cfg.u64);
}


/**
 * @INTERNAL
 * Configure all of the ASX, GMX, and PKO regsiters required
 * to get RGMII to function on the supplied interface.
 *
 * @param interface PKO Interface to configure (0 or 1)
 *
 * @return Zero on success
 */
int __cvmx_helper_rgmii_enable(int interface)
{
    int num_ports = cvmx_helper_ports_on_interface(interface);
    int port;
    cvmx_gmxx_inf_mode_t mode;
    cvmx_asxx_tx_prt_en_t asx_tx;
    cvmx_asxx_rx_prt_en_t asx_rx;

    mode.u64 = cvmx_read_csr(CVMX_GMXX_INF_MODE(interface));

    if (mode.s.en == 0)
        return -1;
    if ((OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN58XX)) && mode.s.type == 1)   /* Ignore SPI interfaces */
        return -1;

    /* Configure the ASX registers needed to use the RGMII ports */
    asx_tx.u64 = 0;
    asx_tx.s.prt_en = cvmx_build_mask(num_ports);
    cvmx_write_csr(CVMX_ASXX_TX_PRT_EN(interface), asx_tx.u64);

    asx_rx.u64 = 0;
    asx_rx.s.prt_en = cvmx_build_mask(num_ports);
    cvmx_write_csr(CVMX_ASXX_RX_PRT_EN(interface), asx_rx.u64);

    /* Configure the GMX registers needed to use the RGMII ports */
    for (port=0; port<num_ports; port++)
    {
        /* Setting of CVMX_GMXX_TXX_THRESH has been moved to
            __cvmx_helper_setup_gmx() */

        /* Configure more flexible RGMII preamble checking. Pass 1 doesn't
           support this feature. */
        cvmx_gmxx_rxx_frm_ctl_t frm_ctl;
        frm_ctl.u64 = cvmx_read_csr(CVMX_GMXX_RXX_FRM_CTL(port, interface));
        frm_ctl.s.pre_free = 1;  /* New field, so must be compile time */
        cvmx_write_csr(CVMX_GMXX_RXX_FRM_CTL(port, interface), frm_ctl.u64);

        /* Each pause frame transmitted will ask for about 10M bit times
            before resume.  If buffer space comes available before that time
            has expired, an XON pause frame (0 time) will be transmitted to
            restart the flow. */
        cvmx_write_csr(CVMX_GMXX_TXX_PAUSE_PKT_TIME(port, interface), 20000);
        cvmx_write_csr(CVMX_GMXX_TXX_PAUSE_PKT_INTERVAL(port, interface), 19000);

        /*
         * Board types we have to know at compile-time.
         */
#if defined(OCTEON_BOARD_CAPK_0100ND)
        cvmx_write_csr(CVMX_ASXX_TX_CLK_SETX(port, interface), 26);
        cvmx_write_csr(CVMX_ASXX_RX_CLK_SETX(port, interface), 26);
#else
	/*
	 * Vendor-defined board types.
	 */
#if defined(OCTEON_VENDOR_LANNER)
	switch (cvmx_sysinfo_get()->board_type) {
	case CVMX_BOARD_TYPE_CUST_LANNER_MR320:
	case CVMX_BOARD_TYPE_CUST_LANNER_MR321X:
            if (port == 0) {
                cvmx_write_csr(CVMX_ASXX_TX_CLK_SETX(port, interface), 4);
	    } else {
                cvmx_write_csr(CVMX_ASXX_TX_CLK_SETX(port, interface), 7);
            }
            cvmx_write_csr(CVMX_ASXX_RX_CLK_SETX(port, interface), 0);
	    break;
	}
#else
        /*
         * For board types we can determine at runtime.
         */
        if (OCTEON_IS_MODEL(OCTEON_CN50XX))
        {
            cvmx_write_csr(CVMX_ASXX_TX_CLK_SETX(port, interface), 16);
            cvmx_write_csr(CVMX_ASXX_RX_CLK_SETX(port, interface), 16);
        }
        else
        {
            cvmx_write_csr(CVMX_ASXX_TX_CLK_SETX(port, interface), 24);
            cvmx_write_csr(CVMX_ASXX_RX_CLK_SETX(port, interface), 24);
        }
#endif
#endif
    }

    __cvmx_helper_setup_gmx(interface, num_ports);

    /* enable the ports now */
    for (port=0; port<num_ports; port++)
    {
        cvmx_gmxx_prtx_cfg_t gmx_cfg;
        cvmx_helper_link_autoconf(cvmx_helper_get_ipd_port(interface, port));
        gmx_cfg.u64 = cvmx_read_csr(CVMX_GMXX_PRTX_CFG(port, interface));
        gmx_cfg.s.en = 1;
        cvmx_write_csr(CVMX_GMXX_PRTX_CFG(port, interface), gmx_cfg.u64);
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
cvmx_helper_link_info_t __cvmx_helper_rgmii_link_get(int ipd_port)
{
    int interface = cvmx_helper_get_interface_num(ipd_port);
    int index = cvmx_helper_get_interface_index_num(ipd_port);
    cvmx_asxx_prt_loop_t asxx_prt_loop;

    asxx_prt_loop.u64 = cvmx_read_csr(CVMX_ASXX_PRT_LOOP(interface));
    if (asxx_prt_loop.s.int_loop & (1<<index))
    {
        /* Force 1Gbps full duplex on internal loopback */
        cvmx_helper_link_info_t result;
        result.u64 = 0;
        result.s.full_duplex = 1;
        result.s.link_up = 1;
        result.s.speed = 1000;
        return result;
    }
    else
        return __cvmx_helper_board_link_get(ipd_port);
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
int __cvmx_helper_rgmii_link_set(int ipd_port, cvmx_helper_link_info_t link_info)
{
    int result = 0;
    int interface = cvmx_helper_get_interface_num(ipd_port);
    int index = cvmx_helper_get_interface_index_num(ipd_port);
    cvmx_gmxx_prtx_cfg_t original_gmx_cfg;
    cvmx_gmxx_prtx_cfg_t new_gmx_cfg;
    cvmx_pko_mem_queue_qos_t pko_mem_queue_qos;
    cvmx_pko_mem_queue_qos_t pko_mem_queue_qos_save[16];
    cvmx_gmxx_tx_ovr_bp_t gmx_tx_ovr_bp;
    cvmx_gmxx_tx_ovr_bp_t gmx_tx_ovr_bp_save;
    int i;

    /* Ignore speed sets in the simulator */
    if (cvmx_sysinfo_get()->board_type == CVMX_BOARD_TYPE_SIM)
        return 0;

    /* Read the current settings so we know the current enable state */
    original_gmx_cfg.u64 = cvmx_read_csr(CVMX_GMXX_PRTX_CFG(index, interface));
    new_gmx_cfg = original_gmx_cfg;

    /* Disable the lowest level RX */
    cvmx_write_csr(CVMX_ASXX_RX_PRT_EN(interface),
                   cvmx_read_csr(CVMX_ASXX_RX_PRT_EN(interface)) & ~(1<<index));

    memset(pko_mem_queue_qos_save, 0, sizeof(pko_mem_queue_qos_save));
    /* Disable all queues so that TX should become idle */
    for (i=0; i<cvmx_pko_get_num_queues(ipd_port); i++)
    {
        int queue = cvmx_pko_get_base_queue(ipd_port) + i;
        cvmx_write_csr(CVMX_PKO_REG_READ_IDX, queue);
        pko_mem_queue_qos.u64 = cvmx_read_csr(CVMX_PKO_MEM_QUEUE_QOS);
        pko_mem_queue_qos.s.pid = ipd_port;
        pko_mem_queue_qos.s.qid = queue;
        pko_mem_queue_qos_save[i] = pko_mem_queue_qos;
        pko_mem_queue_qos.s.qos_mask = 0;
        cvmx_write_csr(CVMX_PKO_MEM_QUEUE_QOS, pko_mem_queue_qos.u64);
    }

    /* Disable backpressure */
    gmx_tx_ovr_bp.u64 = cvmx_read_csr(CVMX_GMXX_TX_OVR_BP(interface));
    gmx_tx_ovr_bp_save = gmx_tx_ovr_bp;
    gmx_tx_ovr_bp.s.bp &= ~(1<<index);
    gmx_tx_ovr_bp.s.en |= 1<<index;
    cvmx_write_csr(CVMX_GMXX_TX_OVR_BP(interface), gmx_tx_ovr_bp.u64);
    cvmx_read_csr(CVMX_GMXX_TX_OVR_BP(interface));

    /* Poll the GMX state machine waiting for it to become idle. Preferably we
        should only change speed when it is idle. If it doesn't become idle we
        will still do the speed change, but there is a slight chance that GMX
        will lockup */
    cvmx_write_csr(CVMX_NPI_DBG_SELECT, interface*0x800 + index*0x100 + 0x880);
    CVMX_WAIT_FOR_FIELD64(CVMX_DBG_DATA, cvmx_dbg_data_t, data&7, ==, 0, 10000);
    CVMX_WAIT_FOR_FIELD64(CVMX_DBG_DATA, cvmx_dbg_data_t, data&0xf, ==, 0, 10000);

    /* Disable the port before we make any changes */
    new_gmx_cfg.s.en = 0;
    cvmx_write_csr(CVMX_GMXX_PRTX_CFG(index, interface), new_gmx_cfg.u64);
    cvmx_read_csr(CVMX_GMXX_PRTX_CFG(index, interface));

    /* Set full/half duplex */
    if (!link_info.s.link_up)
        new_gmx_cfg.s.duplex = 1;   /* Force full duplex on down links */
    else
        new_gmx_cfg.s.duplex = link_info.s.full_duplex;

    /* Set the link speed. Anything unknown is set to 1Gbps */
    if (link_info.s.speed == 10)
    {
        new_gmx_cfg.s.slottime = 0;
        new_gmx_cfg.s.speed = 0;
    }
    else if (link_info.s.speed == 100)
    {
        new_gmx_cfg.s.slottime = 0;
        new_gmx_cfg.s.speed = 0;
    }
    else
    {
        new_gmx_cfg.s.slottime = 1;
        new_gmx_cfg.s.speed = 1;
    }

    /* Adjust the clocks */
    if (link_info.s.speed == 10)
    {
        cvmx_write_csr(CVMX_GMXX_TXX_CLK(index, interface), 50);
        cvmx_write_csr(CVMX_GMXX_TXX_SLOT(index, interface), 0x40);
        cvmx_write_csr(CVMX_GMXX_TXX_BURST(index, interface), 0);
    }
    else if (link_info.s.speed == 100)
    {
        cvmx_write_csr(CVMX_GMXX_TXX_CLK(index, interface), 5);
        cvmx_write_csr(CVMX_GMXX_TXX_SLOT(index, interface), 0x40);
        cvmx_write_csr(CVMX_GMXX_TXX_BURST(index, interface), 0);
    }
    else
    {
        cvmx_write_csr(CVMX_GMXX_TXX_CLK(index, interface), 1);
        cvmx_write_csr(CVMX_GMXX_TXX_SLOT(index, interface), 0x200);
        cvmx_write_csr(CVMX_GMXX_TXX_BURST(index, interface), 0x2000);
    }

    if (OCTEON_IS_MODEL(OCTEON_CN30XX) || OCTEON_IS_MODEL(OCTEON_CN50XX))
    {
        if ((link_info.s.speed == 10) || (link_info.s.speed == 100))
        {
            cvmx_gmxx_inf_mode_t mode;
            mode.u64 = cvmx_read_csr(CVMX_GMXX_INF_MODE(interface));

            /*
            ** Port  .en  .type  .p0mii  Configuration
            ** ----  ---  -----  ------  -----------------------------------------
            **  X      0     X      X    All links are disabled.
            **  0      1     X      0    Port 0 is RGMII
            **  0      1     X      1    Port 0 is MII
            **  1      1     0      X    Ports 1 and 2 are configured as RGMII ports.
            **  1      1     1      X    Port 1: GMII/MII; Port 2: disabled. GMII or
            **                           MII port is selected by GMX_PRT1_CFG[SPEED].
            */

            /* In MII mode, CLK_CNT = 1. */
            if (((index == 0) && (mode.s.p0mii == 1)) || ((index != 0) && (mode.s.type == 1)))
            {
                cvmx_write_csr(CVMX_GMXX_TXX_CLK(index, interface), 1);
            }
        }
    }

    /* Do a read to make sure all setup stuff is complete */
    cvmx_read_csr(CVMX_GMXX_PRTX_CFG(index, interface));

    /* Save the new GMX setting without enabling the port */
    cvmx_write_csr(CVMX_GMXX_PRTX_CFG(index, interface), new_gmx_cfg.u64);

    /* Enable the lowest level RX */
    cvmx_write_csr(CVMX_ASXX_RX_PRT_EN(interface),
                   cvmx_read_csr(CVMX_ASXX_RX_PRT_EN(interface)) | (1<<index));

    /* Re-enable the TX path */
    for (i=0; i<cvmx_pko_get_num_queues(ipd_port); i++)
    {
        int queue = cvmx_pko_get_base_queue(ipd_port) + i;
        cvmx_write_csr(CVMX_PKO_REG_READ_IDX, queue);
        cvmx_write_csr(CVMX_PKO_MEM_QUEUE_QOS, pko_mem_queue_qos_save[i].u64);
    }

    /* Restore backpressure */
    cvmx_write_csr(CVMX_GMXX_TX_OVR_BP(interface), gmx_tx_ovr_bp_save.u64);

    /* Restore the GMX enable state. Port config is complete */
    new_gmx_cfg.s.en = original_gmx_cfg.s.en;
    cvmx_write_csr(CVMX_GMXX_PRTX_CFG(index, interface), new_gmx_cfg.u64);

    return result;
}


/**
 * @INTERNAL
 * Configure a port for internal and/or external loopback. Internal loopback
 * causes packets sent by the port to be received by Octeon. External loopback
 * causes packets received from the wire to sent out again.
 *
 * @param ipd_port IPD/PKO port to loopback.
 * @param enable_internal
 *                 Non zero if you want internal loopback
 * @param enable_external
 *                 Non zero if you want external loopback
 *
 * @return Zero on success, negative on failure.
 */
int __cvmx_helper_rgmii_configure_loopback(int ipd_port, int enable_internal, int enable_external)
{
    int interface = cvmx_helper_get_interface_num(ipd_port);
    int index = cvmx_helper_get_interface_index_num(ipd_port);
    int original_enable;
    cvmx_gmxx_prtx_cfg_t gmx_cfg;
    cvmx_asxx_prt_loop_t asxx_prt_loop;

    /* Read the current enable state and save it */
    gmx_cfg.u64 = cvmx_read_csr(CVMX_GMXX_PRTX_CFG(index, interface));
    original_enable = gmx_cfg.s.en;
    /* Force port to be disabled */
    gmx_cfg.s.en = 0;
    if (enable_internal)
    {
        /* Force speed if we're doing internal loopback */
        gmx_cfg.s.duplex = 1;
        gmx_cfg.s.slottime = 1;
        gmx_cfg.s.speed = 1;
        cvmx_write_csr(CVMX_GMXX_TXX_CLK(index, interface), 1);
        cvmx_write_csr(CVMX_GMXX_TXX_SLOT(index, interface), 0x200);
        cvmx_write_csr(CVMX_GMXX_TXX_BURST(index, interface), 0x2000);
    }
    cvmx_write_csr(CVMX_GMXX_PRTX_CFG(index, interface), gmx_cfg.u64);

    /* Set the loopback bits */
    asxx_prt_loop.u64 = cvmx_read_csr(CVMX_ASXX_PRT_LOOP(interface));
    if (enable_internal)
        asxx_prt_loop.s.int_loop |= 1<<index;
    else
        asxx_prt_loop.s.int_loop &= ~(1<<index);
    if (enable_external)
        asxx_prt_loop.s.ext_loop |= 1<<index;
    else
        asxx_prt_loop.s.ext_loop &= ~(1<<index);
    cvmx_write_csr(CVMX_ASXX_PRT_LOOP(interface), asxx_prt_loop.u64);

    /* Force enables in internal loopback */
    if (enable_internal)
    {
        uint64_t tmp;
        tmp = cvmx_read_csr(CVMX_ASXX_TX_PRT_EN(interface));
        cvmx_write_csr(CVMX_ASXX_TX_PRT_EN(interface), (1 << index) | tmp);
        tmp = cvmx_read_csr(CVMX_ASXX_RX_PRT_EN(interface));
        cvmx_write_csr(CVMX_ASXX_RX_PRT_EN(interface), (1 << index) | tmp);
        original_enable = 1;
    }

    /* Restore the enable state */
    gmx_cfg.s.en = original_enable;
    cvmx_write_csr(CVMX_GMXX_PRTX_CFG(index, interface), gmx_cfg.u64);
    return 0;
}

#endif /* CVMX_ENABLE_PKO_FUNCTIONS */

