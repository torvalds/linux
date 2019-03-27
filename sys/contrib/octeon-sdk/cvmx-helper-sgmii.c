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
 * Functions for SGMII initialization, configuration,
 * and monitoring.
 *
 * <hr>$Revision: 70030 $<hr>
 */
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
#include <asm/octeon/cvmx.h>
#include <asm/octeon/cvmx-config.h>
#include <asm/octeon/cvmx-clock.h>
#include <asm/octeon/cvmx-qlm.h>
#ifdef CVMX_ENABLE_PKO_FUNCTIONS
#include <asm/octeon/cvmx-helper.h>
#include <asm/octeon/cvmx-helper-board.h>
#include <asm/octeon/cvmx-helper-cfg.h>
#endif
#include <asm/octeon/cvmx-pcsx-defs.h>
#include <asm/octeon/cvmx-gmxx-defs.h>
#include <asm/octeon/cvmx-ciu-defs.h>
#else
#if !defined(__FreeBSD__) || !defined(_KERNEL)
#include "executive-config.h"
#include "cvmx-config.h"
#ifdef CVMX_ENABLE_PKO_FUNCTIONS

#include "cvmx.h"
#include "cvmx-sysinfo.h"
#include "cvmx-mdio.h"
#include "cvmx-helper.h"
#include "cvmx-helper-board.h"
#include "cvmx-helper-cfg.h"
#include "cvmx-qlm.h"
#endif
#else
#include "cvmx.h"
#include "cvmx-sysinfo.h"
#include "cvmx-mdio.h"
#include "cvmx-helper.h"
#include "cvmx-helper-board.h"
#include "cvmx-qlm.h"
#endif
#endif

#ifdef CVMX_ENABLE_PKO_FUNCTIONS

/**
 * @INTERNAL
 * Perform initialization required only once for an SGMII port.
 *
 * @param interface Interface to init
 * @param index     Index of prot on the interface
 *
 * @return Zero on success, negative on failure
 */
static int __cvmx_helper_sgmii_hardware_init_one_time(int interface, int index)
{
    const uint64_t clock_mhz = cvmx_clock_get_rate(CVMX_CLOCK_SCLK) / 1000000;
    cvmx_pcsx_miscx_ctl_reg_t pcsx_miscx_ctl_reg;
    cvmx_pcsx_linkx_timer_count_reg_t pcsx_linkx_timer_count_reg;
    cvmx_gmxx_prtx_cfg_t gmxx_prtx_cfg;

    /* Disable GMX */
    gmxx_prtx_cfg.u64 = cvmx_read_csr(CVMX_GMXX_PRTX_CFG(index, interface));
    gmxx_prtx_cfg.s.en = 0;
    cvmx_write_csr(CVMX_GMXX_PRTX_CFG(index, interface), gmxx_prtx_cfg.u64);

    /* Write PCS*_LINK*_TIMER_COUNT_REG[COUNT] with the appropriate
        value. 1000BASE-X specifies a 10ms interval. SGMII specifies a 1.6ms
        interval. */
    pcsx_miscx_ctl_reg.u64 = cvmx_read_csr(CVMX_PCSX_MISCX_CTL_REG(index, interface));
    pcsx_linkx_timer_count_reg.u64 = cvmx_read_csr(CVMX_PCSX_LINKX_TIMER_COUNT_REG(index, interface));
    if (pcsx_miscx_ctl_reg.s.mode
#if defined(OCTEON_VENDOR_GEFES)
	/* GEF Fiber SFP testing on W5650 showed this to cause link issues for 1000BASE-X*/
	&& (cvmx_sysinfo_get()->board_type != CVMX_BOARD_TYPE_CUST_W5650)
	&& (cvmx_sysinfo_get()->board_type != CVMX_BOARD_TYPE_CUST_W63XX)
#endif
	)
    {
        /* 1000BASE-X */
        pcsx_linkx_timer_count_reg.s.count = (10000ull * clock_mhz) >> 10;
    }
    else
    {
        /* SGMII */
        pcsx_linkx_timer_count_reg.s.count = (1600ull * clock_mhz) >> 10;
    }
    cvmx_write_csr(CVMX_PCSX_LINKX_TIMER_COUNT_REG(index, interface), pcsx_linkx_timer_count_reg.u64);

    /* Write the advertisement register to be used as the
        tx_Config_Reg<D15:D0> of the autonegotiation.
        In 1000BASE-X mode, tx_Config_Reg<D15:D0> is PCS*_AN*_ADV_REG.
        In SGMII PHY mode, tx_Config_Reg<D15:D0> is PCS*_SGM*_AN_ADV_REG.
        In SGMII MAC mode, tx_Config_Reg<D15:D0> is the fixed value 0x4001, so
        this step can be skipped. */
    if (pcsx_miscx_ctl_reg.s.mode)
    {
        /* 1000BASE-X */
        cvmx_pcsx_anx_adv_reg_t pcsx_anx_adv_reg;
        pcsx_anx_adv_reg.u64 = cvmx_read_csr(CVMX_PCSX_ANX_ADV_REG(index, interface));
        pcsx_anx_adv_reg.s.rem_flt = 0;
        pcsx_anx_adv_reg.s.pause = 3;
        pcsx_anx_adv_reg.s.hfd = 1;
        pcsx_anx_adv_reg.s.fd = 1;
        cvmx_write_csr(CVMX_PCSX_ANX_ADV_REG(index, interface), pcsx_anx_adv_reg.u64);
    }
    else
    {
#ifdef CVMX_HELPER_CONFIG_NO_PHY
        /* If the interface does not have PHY, then set explicitly in PHY mode
           so that link will be set during auto negotiation. */
        if (!pcsx_miscx_ctl_reg.s.mac_phy) 
        {
            cvmx_dprintf("SGMII%d%d: Forcing PHY mode as PHY address is not set\n", interface, index);
            pcsx_miscx_ctl_reg.s.mac_phy = 1;
            cvmx_write_csr(CVMX_PCSX_MISCX_CTL_REG(index, interface), pcsx_miscx_ctl_reg.u64);
        }
#endif
        if (pcsx_miscx_ctl_reg.s.mac_phy)
        {
            /* PHY Mode */
            cvmx_pcsx_sgmx_an_adv_reg_t pcsx_sgmx_an_adv_reg;
            pcsx_sgmx_an_adv_reg.u64 = cvmx_read_csr(CVMX_PCSX_SGMX_AN_ADV_REG(index, interface));
            pcsx_sgmx_an_adv_reg.s.dup = 1;
            pcsx_sgmx_an_adv_reg.s.speed= 2;
            cvmx_write_csr(CVMX_PCSX_SGMX_AN_ADV_REG(index, interface), pcsx_sgmx_an_adv_reg.u64);
        }
        else
        {
            /* MAC Mode - Nothing to do */
        }
    }
    return 0;
}

static int __cvmx_helper_need_g15618(void)
{
    if (cvmx_sysinfo_get()->board_type == CVMX_BOARD_TYPE_SIM
        || OCTEON_IS_MODEL(OCTEON_CN63XX_PASS1_X)
        || OCTEON_IS_MODEL(OCTEON_CN63XX_PASS2_0)
        || OCTEON_IS_MODEL(OCTEON_CN63XX_PASS2_1)
        || OCTEON_IS_MODEL(OCTEON_CN66XX_PASS1_X)
        || OCTEON_IS_MODEL(OCTEON_CN68XX_PASS1_X))
	return 1;
    else
	return 0;
 }

/**
 * @INTERNAL
 * Initialize the SERTES link for the first time or after a loss
 * of link.
 *
 * @param interface Interface to init
 * @param index     Index of prot on the interface
 *
 * @return Zero on success, negative on failure
 */
static int __cvmx_helper_sgmii_hardware_init_link(int interface, int index)
{
    cvmx_pcsx_mrx_control_reg_t control_reg;
    uint64_t link_timeout;

#if defined(OCTEON_VENDOR_GEFES)
    if (cvmx_sysinfo_get()->board_type == CVMX_BOARD_TYPE_CUST_TNPA5651X) {
	    return 0;  /* no auto-negotiation */
    }
#endif

 
    /* Take PCS through a reset sequence.
        PCS*_MR*_CONTROL_REG[PWR_DN] should be cleared to zero.
        Write PCS*_MR*_CONTROL_REG[RESET]=1 (while not changing the value of
            the other PCS*_MR*_CONTROL_REG bits).
        Read PCS*_MR*_CONTROL_REG[RESET] until it changes value to zero. */
    control_reg.u64 = cvmx_read_csr(CVMX_PCSX_MRX_CONTROL_REG(index, interface));

    /* Errata G-15618 requires disabling PCS soft reset in CN63XX pass upto 2.1. */
    if (!__cvmx_helper_need_g15618())
    {
    	link_timeout = 200000;
#if defined(OCTEON_VENDOR_GEFES)
        if( (cvmx_sysinfo_get()->board_type == CVMX_BOARD_TYPE_CUST_TNPA56X4) && (interface == 0) )
        {
    	    link_timeout = 5000000;
        } 
#endif
        control_reg.s.reset = 1;
        cvmx_write_csr(CVMX_PCSX_MRX_CONTROL_REG(index, interface), control_reg.u64);
        if (CVMX_WAIT_FOR_FIELD64(CVMX_PCSX_MRX_CONTROL_REG(index, interface), cvmx_pcsx_mrx_control_reg_t, reset, ==, 0, link_timeout))
        {
            cvmx_dprintf("SGMII%d: Timeout waiting for port %d to finish reset\n", interface, index);
            return -1;
        }
    }

    /* Write PCS*_MR*_CONTROL_REG[RST_AN]=1 to ensure a fresh sgmii negotiation starts. */
    control_reg.s.rst_an = 1;
    control_reg.s.an_en = 1;
    control_reg.s.pwr_dn = 0;
    cvmx_write_csr(CVMX_PCSX_MRX_CONTROL_REG(index, interface), control_reg.u64);

    /* Wait for PCS*_MR*_STATUS_REG[AN_CPT] to be set, indicating that
        sgmii autonegotiation is complete. In MAC mode this isn't an ethernet
        link, but a link between Octeon and the PHY */
    if ((cvmx_sysinfo_get()->board_type != CVMX_BOARD_TYPE_SIM) &&
        CVMX_WAIT_FOR_FIELD64(CVMX_PCSX_MRX_STATUS_REG(index, interface), cvmx_pcsx_mrx_status_reg_t, an_cpt, ==, 1, 10000))
    {
        //cvmx_dprintf("SGMII%d: Port %d link timeout\n", interface, index);
        return -1;
    }
    return 0;
}


/**
 * @INTERNAL
 * Configure an SGMII link to the specified speed after the SERTES
 * link is up.
 *
 * @param interface Interface to init
 * @param index     Index of prot on the interface
 * @param link_info Link state to configure
 *
 * @return Zero on success, negative on failure
 */
static int __cvmx_helper_sgmii_hardware_init_link_speed(int interface, int index, cvmx_helper_link_info_t link_info)
{
    int is_enabled;
    cvmx_gmxx_prtx_cfg_t gmxx_prtx_cfg;
    cvmx_pcsx_miscx_ctl_reg_t pcsx_miscx_ctl_reg;

#if defined(OCTEON_VENDOR_GEFES)
    if (cvmx_sysinfo_get()->board_type == CVMX_BOARD_TYPE_CUST_TNPA5651X)
	    return 0;  /* no auto-negotiation */
#endif

    /* Disable GMX before we make any changes. Remember the enable state */
    gmxx_prtx_cfg.u64 = cvmx_read_csr(CVMX_GMXX_PRTX_CFG(index, interface));
    is_enabled = gmxx_prtx_cfg.s.en;
    gmxx_prtx_cfg.s.en = 0;
    cvmx_write_csr(CVMX_GMXX_PRTX_CFG(index, interface), gmxx_prtx_cfg.u64);

    /* Wait for GMX to be idle */
    if (CVMX_WAIT_FOR_FIELD64(CVMX_GMXX_PRTX_CFG(index, interface), cvmx_gmxx_prtx_cfg_t, rx_idle, ==, 1, 10000) ||
        CVMX_WAIT_FOR_FIELD64(CVMX_GMXX_PRTX_CFG(index, interface), cvmx_gmxx_prtx_cfg_t, tx_idle, ==, 1, 10000))
    {
        cvmx_dprintf("SGMII%d: Timeout waiting for port %d to be idle\n", interface, index);
        return -1;
    }

    /* Read GMX CFG again to make sure the disable completed */
    gmxx_prtx_cfg.u64 = cvmx_read_csr(CVMX_GMXX_PRTX_CFG(index, interface));

    /* Get the misc control for PCS. We will need to set the duplication amount */
    pcsx_miscx_ctl_reg.u64 = cvmx_read_csr(CVMX_PCSX_MISCX_CTL_REG(index, interface));

    /* Use GMXENO to force the link down if the status we get says it should be down */
    pcsx_miscx_ctl_reg.s.gmxeno = !link_info.s.link_up;

    /* Only change the duplex setting if the link is up */
    if (link_info.s.link_up)
        gmxx_prtx_cfg.s.duplex = link_info.s.full_duplex;

    /* Do speed based setting for GMX */
    switch (link_info.s.speed)
    {
        case 10:
            gmxx_prtx_cfg.s.speed = 0;
            gmxx_prtx_cfg.s.speed_msb = 1;
            gmxx_prtx_cfg.s.slottime = 0;
            pcsx_miscx_ctl_reg.s.samp_pt = 25; /* Setting from GMX-603 */
            cvmx_write_csr(CVMX_GMXX_TXX_SLOT(index, interface), 64);
            cvmx_write_csr(CVMX_GMXX_TXX_BURST(index, interface), 0);
            break;
        case 100:
            gmxx_prtx_cfg.s.speed = 0;
            gmxx_prtx_cfg.s.speed_msb = 0;
            gmxx_prtx_cfg.s.slottime = 0;
            pcsx_miscx_ctl_reg.s.samp_pt = 0x5;
            cvmx_write_csr(CVMX_GMXX_TXX_SLOT(index, interface), 64);
            cvmx_write_csr(CVMX_GMXX_TXX_BURST(index, interface), 0);
            break;
        case 1000:
            gmxx_prtx_cfg.s.speed = 1;
            gmxx_prtx_cfg.s.speed_msb = 0;
            gmxx_prtx_cfg.s.slottime = 1;
            pcsx_miscx_ctl_reg.s.samp_pt = 1;
            cvmx_write_csr(CVMX_GMXX_TXX_SLOT(index, interface), 512);
	    if (gmxx_prtx_cfg.s.duplex)
                cvmx_write_csr(CVMX_GMXX_TXX_BURST(index, interface), 0); // full duplex
	    else
                cvmx_write_csr(CVMX_GMXX_TXX_BURST(index, interface), 8192); // half duplex
            break;
        default:
            break;
    }

    /* Write the new misc control for PCS */
    cvmx_write_csr(CVMX_PCSX_MISCX_CTL_REG(index, interface), pcsx_miscx_ctl_reg.u64);

    /* Write the new GMX settings with the port still disabled */
    cvmx_write_csr(CVMX_GMXX_PRTX_CFG(index, interface), gmxx_prtx_cfg.u64);

    /* Read GMX CFG again to make sure the config completed */
    gmxx_prtx_cfg.u64 = cvmx_read_csr(CVMX_GMXX_PRTX_CFG(index, interface));

    /* Restore the enabled / disabled state */
    gmxx_prtx_cfg.s.en = is_enabled;
    cvmx_write_csr(CVMX_GMXX_PRTX_CFG(index, interface), gmxx_prtx_cfg.u64);

    return 0;
}


/**
 * @INTERNAL
 * Bring up the SGMII interface to be ready for packet I/O but
 * leave I/O disabled using the GMX override. This function
 * follows the bringup documented in 10.6.3 of the manual.
 *
 * @param interface Interface to bringup
 * @param num_ports Number of ports on the interface
 *
 * @return Zero on success, negative on failure
 */
static int __cvmx_helper_sgmii_hardware_init(int interface, int num_ports)
{
    int index;
    int do_link_set = 1;

    /* CN63XX Pass 1.0 errata G-14395 requires the QLM De-emphasis be programmed */
    if (OCTEON_IS_MODEL(OCTEON_CN63XX_PASS1_0))
    {
        cvmx_ciu_qlm2_t ciu_qlm;
        ciu_qlm.u64 = cvmx_read_csr(CVMX_CIU_QLM2);
        ciu_qlm.s.txbypass = 1;
        ciu_qlm.s.txdeemph = 0xf;
        ciu_qlm.s.txmargin = 0xd;
        cvmx_write_csr(CVMX_CIU_QLM2, ciu_qlm.u64);
    }

    /* CN63XX Pass 2.0 and 2.1 errata G-15273 requires the QLM De-emphasis be
        programmed when using a 156.25Mhz ref clock */
    if (OCTEON_IS_MODEL(OCTEON_CN63XX_PASS2_0) ||
        OCTEON_IS_MODEL(OCTEON_CN63XX_PASS2_1))
    {
        /* Read the QLM speed pins */
        cvmx_mio_rst_boot_t mio_rst_boot;
        mio_rst_boot.u64 = cvmx_read_csr(CVMX_MIO_RST_BOOT);

        if (mio_rst_boot.cn63xx.qlm2_spd == 4)
        {
            cvmx_ciu_qlm2_t ciu_qlm;
            ciu_qlm.u64 = cvmx_read_csr(CVMX_CIU_QLM2);
            ciu_qlm.s.txbypass = 1;
            ciu_qlm.s.txdeemph = 0x0;
            ciu_qlm.s.txmargin = 0xf;
            cvmx_write_csr(CVMX_CIU_QLM2, ciu_qlm.u64);
        }
    }

    __cvmx_helper_setup_gmx(interface, num_ports);

    for (index=0; index<num_ports; index++)
    {
        int ipd_port = cvmx_helper_get_ipd_port(interface, index);
        __cvmx_helper_sgmii_hardware_init_one_time(interface, index);
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
        /* Linux kernel driver will call ....link_set with the proper link
           state. In the simulator there is no link state polling and
           hence it is set from here. */
        if (!(cvmx_sysinfo_get()->board_type == CVMX_BOARD_TYPE_SIM))
            do_link_set = 0;
#endif
        if (do_link_set)
            __cvmx_helper_sgmii_link_set(ipd_port, __cvmx_helper_sgmii_link_get(ipd_port));
    }

    return 0;
}

int __cvmx_helper_sgmii_enumerate(int interface)
{
    if (OCTEON_IS_MODEL(OCTEON_CNF71XX))
        return 2;

    return 4;
}

/**
 * @INTERNAL
 * Probe a SGMII interface and determine the number of ports
 * connected to it. The SGMII interface should still be down after
 * this call.
 *
 * @param interface Interface to probe
 *
 * @return Number of ports on the interface. Zero to disable.
 */
int __cvmx_helper_sgmii_probe(int interface)
{
    cvmx_gmxx_inf_mode_t mode;

    /* Check if QLM is configured correct for SGMII, verify the speed 
       as well as mode */
    if (OCTEON_IS_MODEL(OCTEON_CN6XXX))
    {
        int qlm = cvmx_qlm_interface(interface);

        if (cvmx_qlm_get_status(qlm) != 1)
            return 0;
    }

    /* Due to errata GMX-700 on CN56XXp1.x and CN52XXp1.x, the interface
       needs to be enabled before IPD otherwise per port backpressure
       may not work properly */
    mode.u64 = cvmx_read_csr(CVMX_GMXX_INF_MODE(interface));
    mode.s.en = 1;
    cvmx_write_csr(CVMX_GMXX_INF_MODE(interface), mode.u64);

    return __cvmx_helper_sgmii_enumerate(interface);
}


/**
 * @INTERNAL
 * Bringup and enable a SGMII interface. After this call packet
 * I/O should be fully functional. This is called with IPD
 * enabled but PKO disabled.
 *
 * @param interface Interface to bring up
 *
 * @return Zero on success, negative on failure
 */
int __cvmx_helper_sgmii_enable(int interface)
{
    int num_ports = cvmx_helper_ports_on_interface(interface);
    int index;

    /* Setup PKND and BPID */
    if (octeon_has_feature(OCTEON_FEATURE_PKND))
    {
        for (index = 0; index < num_ports; index++)
        {
            cvmx_gmxx_bpid_msk_t bpid_msk;
            cvmx_gmxx_bpid_mapx_t bpid_map;
            cvmx_gmxx_prtx_cfg_t gmxx_prtx_cfg;

            /* Setup PKIND */
            gmxx_prtx_cfg.u64 = cvmx_read_csr(CVMX_GMXX_PRTX_CFG(index, interface));
            gmxx_prtx_cfg.s.pknd = cvmx_helper_get_pknd(interface, index);
            cvmx_write_csr(CVMX_GMXX_PRTX_CFG(index, interface), gmxx_prtx_cfg.u64);

            /* Setup BPID */
            bpid_map.u64 = cvmx_read_csr(CVMX_GMXX_BPID_MAPX(index, interface));
            bpid_map.s.val = 1;
            bpid_map.s.bpid = cvmx_helper_get_bpid(interface, index);
            cvmx_write_csr(CVMX_GMXX_BPID_MAPX(index, interface), bpid_map.u64);

            bpid_msk.u64 = cvmx_read_csr(CVMX_GMXX_BPID_MSK(interface));
            bpid_msk.s.msk_or |= (1<<index);
            bpid_msk.s.msk_and &= ~(1<<index);
            cvmx_write_csr(CVMX_GMXX_BPID_MSK(interface), bpid_msk.u64);
        }
    }

    __cvmx_helper_sgmii_hardware_init(interface, num_ports);

    /* CN68XX adds the padding and FCS in PKO, not GMX */
    if (OCTEON_IS_MODEL(OCTEON_CN68XX))
    {
	cvmx_gmxx_txx_append_t gmxx_txx_append_cfg;

        for (index = 0; index < num_ports; index++)
	{
	    gmxx_txx_append_cfg.u64 = cvmx_read_csr(
	        CVMX_GMXX_TXX_APPEND(index, interface));
	    gmxx_txx_append_cfg.s.fcs = 0;
	    gmxx_txx_append_cfg.s.pad = 0;
            cvmx_write_csr(CVMX_GMXX_TXX_APPEND(index, interface), 
	        gmxx_txx_append_cfg.u64);
	}
    }

    for (index=0; index<num_ports; index++)
    {
        cvmx_gmxx_txx_append_t append_cfg;
        cvmx_gmxx_txx_sgmii_ctl_t sgmii_ctl;
        cvmx_gmxx_prtx_cfg_t gmxx_prtx_cfg;

        /* Clear the align bit if preamble is set to attain maximum tx rate. */
        append_cfg.u64 = cvmx_read_csr(CVMX_GMXX_TXX_APPEND(index, interface));
        sgmii_ctl.u64 = cvmx_read_csr(CVMX_GMXX_TXX_SGMII_CTL(index, interface));
        sgmii_ctl.s.align = append_cfg.s.preamble ? 0 : 1;
        cvmx_write_csr(CVMX_GMXX_TXX_SGMII_CTL(index, interface), sgmii_ctl.u64);

        gmxx_prtx_cfg.u64 = cvmx_read_csr(CVMX_GMXX_PRTX_CFG(index, interface));
        gmxx_prtx_cfg.s.en = 1;
        cvmx_write_csr(CVMX_GMXX_PRTX_CFG(index, interface), gmxx_prtx_cfg.u64);
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
cvmx_helper_link_info_t __cvmx_helper_sgmii_link_get(int ipd_port)
{
    cvmx_helper_link_info_t result;
    cvmx_pcsx_miscx_ctl_reg_t pcsx_miscx_ctl_reg;
    int interface = cvmx_helper_get_interface_num(ipd_port);
    int index = cvmx_helper_get_interface_index_num(ipd_port);
    cvmx_pcsx_mrx_control_reg_t pcsx_mrx_control_reg;
    int speed = 1000;
    int qlm;

    if (cvmx_sysinfo_get()->board_type == CVMX_BOARD_TYPE_SIM)
    {
        /* The simulator gives you a simulated 1Gbps full duplex link */
        result.s.link_up = 1;
        result.s.full_duplex = 1;
        result.s.speed = speed;
        return result;
    }

    if (OCTEON_IS_MODEL(OCTEON_CN66XX))
    {
        cvmx_gmxx_inf_mode_t inf_mode;
        inf_mode.u64 = cvmx_read_csr(CVMX_GMXX_INF_MODE(interface));
        if (inf_mode.s.rate & (1<<index))
            speed = 2500;
        else
            speed = 1000;
    }
    else if (OCTEON_IS_MODEL(OCTEON_CN6XXX))
    {
        qlm = cvmx_qlm_interface(interface);

        speed = cvmx_qlm_get_gbaud_mhz(qlm) * 8 / 10;
    }

    result.u64 = 0;

    pcsx_mrx_control_reg.u64 = cvmx_read_csr(CVMX_PCSX_MRX_CONTROL_REG(index, interface));
    if (pcsx_mrx_control_reg.s.loopbck1)
    {
        /* Force 1Gbps full duplex link for internal loopback */
        result.s.link_up = 1;
        result.s.full_duplex = 1;
        result.s.speed = speed;
        return result;
    }


    pcsx_miscx_ctl_reg.u64 = cvmx_read_csr(CVMX_PCSX_MISCX_CTL_REG(index, interface));
    if (pcsx_miscx_ctl_reg.s.mode)
    {
#if defined(OCTEON_VENDOR_GEFES)
        /* 1000BASE-X */
        int interface = cvmx_helper_get_interface_num(ipd_port);
        int index = cvmx_helper_get_interface_index_num(ipd_port);
        cvmx_pcsx_miscx_ctl_reg_t mode_type;
        cvmx_pcsx_anx_results_reg_t inband_status;
        cvmx_pcsx_mrx_status_reg_t mrx_status;
        cvmx_pcsx_anx_adv_reg_t anxx_adv;

        anxx_adv.u64 = cvmx_read_csr(CVMX_PCSX_ANX_ADV_REG(index, interface));
        mrx_status.u64 = cvmx_read_csr(CVMX_PCSX_MRX_STATUS_REG(index, interface));
        mode_type.u64 = cvmx_read_csr(CVMX_PCSX_MISCX_CTL_REG(index, interface));

        /* Read Octeon's inband status */
        inband_status.u64 = cvmx_read_csr(CVMX_PCSX_ANX_RESULTS_REG(index, interface));

        result.s.link_up = inband_status.s.link_ok;/* this is only accurate for 1000-base x */
        
        result.s.full_duplex = inband_status.s.dup;
        switch (inband_status.s.spd)
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
#endif /* Actually not 100% this is GEFES specific */
    }
    else
    {
        if (pcsx_miscx_ctl_reg.s.mac_phy)
        {
            /* PHY Mode */
            cvmx_pcsx_mrx_status_reg_t pcsx_mrx_status_reg;
            cvmx_pcsx_anx_results_reg_t pcsx_anx_results_reg;

            /* Don't bother continuing if the SERTES low level link is down */
            pcsx_mrx_status_reg.u64 = cvmx_read_csr(CVMX_PCSX_MRX_STATUS_REG(index, interface));
            if (pcsx_mrx_status_reg.s.lnk_st == 0)
            {
                if (__cvmx_helper_sgmii_hardware_init_link(interface, index) != 0)
                    return result;
            }

            /* Read the autoneg results */
            pcsx_anx_results_reg.u64 = cvmx_read_csr(CVMX_PCSX_ANX_RESULTS_REG(index, interface));
            if (pcsx_anx_results_reg.s.an_cpt)
            {
                /* Auto negotiation is complete. Set status accordingly */
                result.s.full_duplex = pcsx_anx_results_reg.s.dup;
                result.s.link_up = pcsx_anx_results_reg.s.link_ok;
                switch (pcsx_anx_results_reg.s.spd)
                {
                    case 0:
                        result.s.speed = speed / 100;
                        break;
                    case 1:
                        result.s.speed = speed / 10;
                        break;
                    case 2:
                        result.s.speed = speed;
                        break;
                    default:
                        result.s.speed = 0;
                        result.s.link_up = 0;
                        break;
                }
            }
            else
            {
                /* Auto negotiation isn't complete. Return link down */
                result.s.speed = 0;
                result.s.link_up = 0;
            }
        }
        else /* MAC Mode */
        {
            result = __cvmx_helper_board_link_get(ipd_port);
        }
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
int __cvmx_helper_sgmii_link_set(int ipd_port, cvmx_helper_link_info_t link_info)
{
    int interface = cvmx_helper_get_interface_num(ipd_port);
    int index = cvmx_helper_get_interface_index_num(ipd_port);

    if (link_info.s.link_up || !__cvmx_helper_need_g15618()) {
	__cvmx_helper_sgmii_hardware_init_link(interface, index);
    } else {
	cvmx_pcsx_mrx_control_reg_t control_reg;
	cvmx_pcsx_miscx_ctl_reg_t pcsx_miscx_ctl_reg;

	control_reg.u64 = cvmx_read_csr(CVMX_PCSX_MRX_CONTROL_REG(index, interface));
	control_reg.s.an_en = 0;
	cvmx_write_csr(CVMX_PCSX_MRX_CONTROL_REG(index, interface), control_reg.u64);
	cvmx_read_csr(CVMX_PCSX_MRX_CONTROL_REG(index, interface));
	/* Use GMXENO to force the link down it will get reenabled later... */
	pcsx_miscx_ctl_reg.s.gmxeno = 1;
	cvmx_write_csr(CVMX_PCSX_MISCX_CTL_REG(index, interface), pcsx_miscx_ctl_reg.u64);
	cvmx_read_csr(CVMX_PCSX_MISCX_CTL_REG(index, interface));
	return 0;
    }
    return __cvmx_helper_sgmii_hardware_init_link_speed(interface, index, link_info);
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
int __cvmx_helper_sgmii_configure_loopback(int ipd_port, int enable_internal, int enable_external)
{
    int interface = cvmx_helper_get_interface_num(ipd_port);
    int index = cvmx_helper_get_interface_index_num(ipd_port);
    cvmx_pcsx_mrx_control_reg_t pcsx_mrx_control_reg;
    cvmx_pcsx_miscx_ctl_reg_t pcsx_miscx_ctl_reg;

    pcsx_mrx_control_reg.u64 = cvmx_read_csr(CVMX_PCSX_MRX_CONTROL_REG(index, interface));
    pcsx_mrx_control_reg.s.loopbck1 = enable_internal;
    cvmx_write_csr(CVMX_PCSX_MRX_CONTROL_REG(index, interface), pcsx_mrx_control_reg.u64);

    pcsx_miscx_ctl_reg.u64 = cvmx_read_csr(CVMX_PCSX_MISCX_CTL_REG(index, interface));
    pcsx_miscx_ctl_reg.s.loopbck2 = enable_external;
    cvmx_write_csr(CVMX_PCSX_MISCX_CTL_REG(index, interface), pcsx_miscx_ctl_reg.u64);

    __cvmx_helper_sgmii_hardware_init_link(interface, index);
    return 0;
}

#endif /* CVMX_ENABLE_PKO_FUNCTIONS */
