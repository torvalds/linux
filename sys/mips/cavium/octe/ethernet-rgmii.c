/*************************************************************************
SPDX-License-Identifier: BSD-3-Clause

Copyright (c) 2003-2007  Cavium Networks (support@cavium.com). All rights
reserved.


Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.

    * Neither the name of Cavium Networks nor the names of
      its contributors may be used to endorse or promote products
      derived from this software without specific prior written
      permission.

This Software, including technical data, may be subject to U.S. export  control laws, including the U.S. Export Administration Act and its  associated regulations, and may be subject to export or import  regulations in other countries.

TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED "AS IS"
AND WITH ALL FAULTS AND CAVIUM  NETWORKS MAKES NO PROMISES, REPRESENTATIONS OR WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, WITH RESPECT TO THE SOFTWARE, INCLUDING ITS CONDITION, ITS CONFORMITY TO ANY REPRESENTATION OR DESCRIPTION, OR THE EXISTENCE OF ANY LATENT OR PATENT DEFECTS, AND CAVIUM SPECIFICALLY DISCLAIMS ALL IMPLIED (IF ANY) WARRANTIES OF TITLE, MERCHANTABILITY, NONINFRINGEMENT, FITNESS FOR A PARTICULAR PURPOSE, LACK OF VIRUSES, ACCURACY OR COMPLETENESS, QUIET ENJOYMENT, QUIET POSSESSION OR CORRESPONDENCE TO DESCRIPTION. THE ENTIRE  RISK ARISING OUT OF USE OR PERFORMANCE OF THE SOFTWARE LIES WITH YOU.

*************************************************************************/

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/mbuf.h>
#include <sys/rman.h>
#include <sys/socket.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>

#include "wrapper-cvmx-includes.h"
#include "ethernet-headers.h"

#include "octebusvar.h"

extern struct ifnet *cvm_oct_device[];

static struct mtx global_register_lock;
MTX_SYSINIT(global_register_lock, &global_register_lock,
	    "RGMII Global", MTX_SPIN);

static int number_rgmii_ports;

static void cvm_oct_rgmii_poll(struct ifnet *ifp)
{
	cvm_oct_private_t *priv = (cvm_oct_private_t *)ifp->if_softc;
	cvmx_helper_link_info_t link_info;

	/* Take the global register lock since we are going to touch
	   registers that affect more than one port */
	mtx_lock_spin(&global_register_lock);

	link_info = cvmx_helper_link_get(priv->port);
	if (link_info.u64 == priv->link_info) {

		/* If the 10Mbps preamble workaround is supported and we're
		   at 10Mbps we may need to do some special checking */
		if (USE_10MBPS_PREAMBLE_WORKAROUND && (link_info.s.speed == 10)) {

			/* Read the GMXX_RXX_INT_REG[PCTERR] bit and
			   see if we are getting preamble errors */
			int interface = INTERFACE(priv->port);
			int index = INDEX(priv->port);
			cvmx_gmxx_rxx_int_reg_t gmxx_rxx_int_reg;
			gmxx_rxx_int_reg.u64 = cvmx_read_csr(CVMX_GMXX_RXX_INT_REG(index, interface));
			if (gmxx_rxx_int_reg.s.pcterr) {

				/* We are getting preamble errors at 10Mbps.
				   Most likely the PHY is giving us packets
				   with mis aligned preambles. In order to get
				   these packets we need to disable preamble
				   checking and do it in software */
				cvmx_gmxx_rxx_frm_ctl_t gmxx_rxx_frm_ctl;
				cvmx_ipd_sub_port_fcs_t ipd_sub_port_fcs;

				/* Disable preamble checking */
				gmxx_rxx_frm_ctl.u64 = cvmx_read_csr(CVMX_GMXX_RXX_FRM_CTL(index, interface));
				gmxx_rxx_frm_ctl.s.pre_chk = 0;
				cvmx_write_csr(CVMX_GMXX_RXX_FRM_CTL(index, interface), gmxx_rxx_frm_ctl.u64);

				/* Disable FCS stripping */
				ipd_sub_port_fcs.u64 = cvmx_read_csr(CVMX_IPD_SUB_PORT_FCS);
				ipd_sub_port_fcs.s.port_bit &= 0xffffffffull ^ (1ull<<priv->port);
				cvmx_write_csr(CVMX_IPD_SUB_PORT_FCS, ipd_sub_port_fcs.u64);

				/* Clear any error bits */
				cvmx_write_csr(CVMX_GMXX_RXX_INT_REG(index, interface), gmxx_rxx_int_reg.u64);
				DEBUGPRINT("%s: Using 10Mbps with software preamble removal\n", if_name(ifp));
			}
		}
		mtx_unlock_spin(&global_register_lock);
		return;
	}

	/* If the 10Mbps preamble workaround is allowed we need to on
	   preamble checking, FCS stripping, and clear error bits on
	   every speed change. If errors occur during 10Mbps operation
	   the above code will change this stuff */
	if (USE_10MBPS_PREAMBLE_WORKAROUND) {

		cvmx_gmxx_rxx_frm_ctl_t gmxx_rxx_frm_ctl;
		cvmx_ipd_sub_port_fcs_t ipd_sub_port_fcs;
		cvmx_gmxx_rxx_int_reg_t gmxx_rxx_int_reg;
		int interface = INTERFACE(priv->port);
		int index = INDEX(priv->port);

		/* Enable preamble checking */
		gmxx_rxx_frm_ctl.u64 = cvmx_read_csr(CVMX_GMXX_RXX_FRM_CTL(index, interface));
		gmxx_rxx_frm_ctl.s.pre_chk = 1;
		cvmx_write_csr(CVMX_GMXX_RXX_FRM_CTL(index, interface), gmxx_rxx_frm_ctl.u64);
		/* Enable FCS stripping */
		ipd_sub_port_fcs.u64 = cvmx_read_csr(CVMX_IPD_SUB_PORT_FCS);
		ipd_sub_port_fcs.s.port_bit |= 1ull<<priv->port;
		cvmx_write_csr(CVMX_IPD_SUB_PORT_FCS, ipd_sub_port_fcs.u64);
		/* Clear any error bits */
		gmxx_rxx_int_reg.u64 = cvmx_read_csr(CVMX_GMXX_RXX_INT_REG(index, interface));
		cvmx_write_csr(CVMX_GMXX_RXX_INT_REG(index, interface), gmxx_rxx_int_reg.u64);
	}

	if (priv->miibus == NULL) {
		link_info = cvmx_helper_link_autoconf(priv->port);
		priv->link_info = link_info.u64;
		priv->need_link_update = 1;
	}
	mtx_unlock_spin(&global_register_lock);
}


static int cvm_oct_rgmii_rml_interrupt(void *dev_id)
{
	cvmx_npi_rsl_int_blocks_t rsl_int_blocks;
	int index;
	int return_status = FILTER_STRAY;

	rsl_int_blocks.u64 = cvmx_read_csr(CVMX_NPI_RSL_INT_BLOCKS);

	/* Check and see if this interrupt was caused by the GMX0 block */
	if (rsl_int_blocks.s.gmx0) {

		int interface = 0;
		/* Loop through every port of this interface */
		for (index = 0; index < cvmx_helper_ports_on_interface(interface); index++) {

			/* Read the GMX interrupt status bits */
			cvmx_gmxx_rxx_int_reg_t gmx_rx_int_reg;
			gmx_rx_int_reg.u64 = cvmx_read_csr(CVMX_GMXX_RXX_INT_REG(index, interface));
			gmx_rx_int_reg.u64 &= cvmx_read_csr(CVMX_GMXX_RXX_INT_EN(index, interface));
			/* Poll the port if inband status changed */
			if (gmx_rx_int_reg.s.phy_dupx || gmx_rx_int_reg.s.phy_link || gmx_rx_int_reg.s.phy_spd) {

				struct ifnet *ifp = cvm_oct_device[cvmx_helper_get_ipd_port(interface, index)];
				if (ifp)
					cvm_oct_rgmii_poll(ifp);
				gmx_rx_int_reg.u64 = 0;
				gmx_rx_int_reg.s.phy_dupx = 1;
				gmx_rx_int_reg.s.phy_link = 1;
				gmx_rx_int_reg.s.phy_spd = 1;
				cvmx_write_csr(CVMX_GMXX_RXX_INT_REG(index, interface), gmx_rx_int_reg.u64);
				return_status = FILTER_HANDLED;
			}
		}
	}

	/* Check and see if this interrupt was caused by the GMX1 block */
	if (rsl_int_blocks.s.gmx1) {

		int interface = 1;
		/* Loop through every port of this interface */
		for (index = 0; index < cvmx_helper_ports_on_interface(interface); index++) {

			/* Read the GMX interrupt status bits */
			cvmx_gmxx_rxx_int_reg_t gmx_rx_int_reg;
			gmx_rx_int_reg.u64 = cvmx_read_csr(CVMX_GMXX_RXX_INT_REG(index, interface));
			gmx_rx_int_reg.u64 &= cvmx_read_csr(CVMX_GMXX_RXX_INT_EN(index, interface));
			/* Poll the port if inband status changed */
			if (gmx_rx_int_reg.s.phy_dupx || gmx_rx_int_reg.s.phy_link || gmx_rx_int_reg.s.phy_spd) {

				struct ifnet *ifp = cvm_oct_device[cvmx_helper_get_ipd_port(interface, index)];
				if (ifp)
					cvm_oct_rgmii_poll(ifp);
				gmx_rx_int_reg.u64 = 0;
				gmx_rx_int_reg.s.phy_dupx = 1;
				gmx_rx_int_reg.s.phy_link = 1;
				gmx_rx_int_reg.s.phy_spd = 1;
				cvmx_write_csr(CVMX_GMXX_RXX_INT_REG(index, interface), gmx_rx_int_reg.u64);
				return_status = FILTER_HANDLED;
			}
		}
	}
	return return_status;
}


int cvm_oct_rgmii_init(struct ifnet *ifp)
{
	struct octebus_softc *sc;
	cvm_oct_private_t *priv = (cvm_oct_private_t *)ifp->if_softc;
	int error;
	int rid;

	if (cvm_oct_common_init(ifp) != 0)
	    return ENXIO;

	priv->open = cvm_oct_common_open;
	priv->stop = cvm_oct_common_stop;
	priv->stop(ifp);

	/* Due to GMX errata in CN3XXX series chips, it is necessary to take the
	   link down immediately whne the PHY changes state. In order to do this
	   we call the poll function every time the RGMII inband status changes.
	   This may cause problems if the PHY doesn't implement inband status
	   properly */
	if (number_rgmii_ports == 0) {
		sc = device_get_softc(device_get_parent(priv->dev));

		rid = 0;
		sc->sc_rgmii_irq = bus_alloc_resource(sc->sc_dev, SYS_RES_IRQ,
						      &rid, OCTEON_IRQ_RML,
						      OCTEON_IRQ_RML, 1,
						      RF_ACTIVE);
		if (sc->sc_rgmii_irq == NULL) {
			device_printf(sc->sc_dev, "could not allocate RGMII irq");
			return ENXIO;
		}

		error = bus_setup_intr(sc->sc_dev, sc->sc_rgmii_irq,
				       INTR_TYPE_NET | INTR_MPSAFE,
				       cvm_oct_rgmii_rml_interrupt, NULL,
				       &number_rgmii_ports, NULL);
		if (error != 0) {
			device_printf(sc->sc_dev, "could not setup RGMII irq");
			return error;
		}
	}
	number_rgmii_ports++;

	/* Only true RGMII ports need to be polled. In GMII mode, port 0 is really
	   a RGMII port */
	if (((priv->imode == CVMX_HELPER_INTERFACE_MODE_GMII) && (priv->port == 0)) ||
	    (priv->imode == CVMX_HELPER_INTERFACE_MODE_RGMII)) {

		if (cvmx_sysinfo_get()->board_type != CVMX_BOARD_TYPE_SIM) {

			cvmx_gmxx_rxx_int_en_t gmx_rx_int_en;
			int interface = INTERFACE(priv->port);
			int index = INDEX(priv->port);

			/* Enable interrupts on inband status changes for this port */
			gmx_rx_int_en.u64 = cvmx_read_csr(CVMX_GMXX_RXX_INT_EN(index, interface));
			gmx_rx_int_en.s.phy_dupx = 1;
			gmx_rx_int_en.s.phy_link = 1;
			gmx_rx_int_en.s.phy_spd = 1;
			cvmx_write_csr(CVMX_GMXX_RXX_INT_EN(index, interface), gmx_rx_int_en.u64);
			priv->poll = cvm_oct_rgmii_poll;
		}
	}

	return 0;
}

void cvm_oct_rgmii_uninit(struct ifnet *ifp)
{
	cvm_oct_private_t *priv = (cvm_oct_private_t *)ifp->if_softc;
	cvm_oct_common_uninit(ifp);

	/* Only true RGMII ports need to be polled. In GMII mode, port 0 is really
	   a RGMII port */
	if (((priv->imode == CVMX_HELPER_INTERFACE_MODE_GMII) && (priv->port == 0)) ||
	    (priv->imode == CVMX_HELPER_INTERFACE_MODE_RGMII)) {

		if (cvmx_sysinfo_get()->board_type != CVMX_BOARD_TYPE_SIM) {

			cvmx_gmxx_rxx_int_en_t gmx_rx_int_en;
			int interface = INTERFACE(priv->port);
			int index = INDEX(priv->port);

			/* Disable interrupts on inband status changes for this port */
			gmx_rx_int_en.u64 = cvmx_read_csr(CVMX_GMXX_RXX_INT_EN(index, interface));
			gmx_rx_int_en.s.phy_dupx = 0;
			gmx_rx_int_en.s.phy_link = 0;
			gmx_rx_int_en.s.phy_spd = 0;
			cvmx_write_csr(CVMX_GMXX_RXX_INT_EN(index, interface), gmx_rx_int_en.u64);
		}
	}

	/* Remove the interrupt handler when the last port is removed */
	number_rgmii_ports--;
	if (number_rgmii_ports == 0)
		panic("%s: need to implement IRQ release.", __func__);
}

