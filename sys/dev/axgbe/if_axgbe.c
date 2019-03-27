/*-
 * Copyright (c) 2016,2017 SoftIron Inc.
 * All rights reserved.
 *
 * This software was developed by Andrew Turner under
 * the sponsorship of SoftIron Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/queue.h>
#include <sys/rman.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sx.h>
#include <sys/taskqueue.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_media.h>
#include <net/if_types.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>

#include "miibus_if.h"

#include "xgbe.h"
#include "xgbe-common.h"

static device_probe_t	axgbe_probe;
static device_attach_t	axgbe_attach;

struct axgbe_softc {
	/* Must be first */
	struct xgbe_prv_data	prv;

	uint8_t			mac_addr[ETHER_ADDR_LEN];
	struct ifmedia		media;
};

static struct ofw_compat_data compat_data[] = {
	{ "amd,xgbe-seattle-v1a",	true },
	{ NULL,				false }
};

static struct resource_spec old_phy_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE }, /* Rx/Tx regs */
	{ SYS_RES_MEMORY,	1,	RF_ACTIVE }, /* Integration regs */
	{ SYS_RES_MEMORY,	2,	RF_ACTIVE }, /* Integration regs */
	{ SYS_RES_IRQ,		0,	RF_ACTIVE }, /* Interrupt */
	{ -1, 0 }
};

static struct resource_spec old_mac_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE }, /* MAC regs */
	{ SYS_RES_MEMORY,	1,	RF_ACTIVE }, /* PCS regs */
	{ SYS_RES_IRQ,		0,	RF_ACTIVE }, /* Device interrupt */
	/* Per-channel interrupts */
	{ SYS_RES_IRQ,		1,	RF_ACTIVE | RF_OPTIONAL },
	{ SYS_RES_IRQ,		2,	RF_ACTIVE | RF_OPTIONAL },
	{ SYS_RES_IRQ,		3,	RF_ACTIVE | RF_OPTIONAL },
	{ SYS_RES_IRQ,		4,	RF_ACTIVE | RF_OPTIONAL },
	{ -1, 0 }
};

static struct resource_spec mac_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE }, /* MAC regs */
	{ SYS_RES_MEMORY,	1,	RF_ACTIVE }, /* PCS regs */
	{ SYS_RES_MEMORY,	2,	RF_ACTIVE }, /* Rx/Tx regs */
	{ SYS_RES_MEMORY,	3,	RF_ACTIVE }, /* Integration regs */
	{ SYS_RES_MEMORY,	4,	RF_ACTIVE }, /* Integration regs */
	{ SYS_RES_IRQ,		0,	RF_ACTIVE }, /* Device interrupt */
	/* Per-channel and auto-negotiation interrupts */
	{ SYS_RES_IRQ,		1,	RF_ACTIVE },
	{ SYS_RES_IRQ,		2,	RF_ACTIVE | RF_OPTIONAL },
	{ SYS_RES_IRQ,		3,	RF_ACTIVE | RF_OPTIONAL },
	{ SYS_RES_IRQ,		4,	RF_ACTIVE | RF_OPTIONAL },
	{ SYS_RES_IRQ,		5,	RF_ACTIVE | RF_OPTIONAL },
	{ -1, 0 }
};

MALLOC_DEFINE(M_AXGBE, "axgbe", "axgbe data");

static void
axgbe_init(void *p)
{
	struct axgbe_softc *sc;
	struct ifnet *ifp;

	sc = p;
	ifp = sc->prv.netdev;
	if (ifp->if_drv_flags & IFF_DRV_RUNNING)
		return;

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
}

static int
axgbe_ioctl(struct ifnet *ifp, unsigned long command, caddr_t data)
{
	struct axgbe_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int error;

	switch(command) {
	case SIOCSIFMTU:
		if (ifr->ifr_mtu < ETHERMIN || ifr->ifr_mtu > ETHERMTU_JUMBO)
			error = EINVAL;
		else
			error = xgbe_change_mtu(ifp, ifr->ifr_mtu);
		break;
	case SIOCSIFFLAGS:
		error = 0;
		break;
	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->media, command);
		break;
	default:
		error = ether_ioctl(ifp, command, data);
		break;
	}

	return (error);
}

static void
axgbe_qflush(struct ifnet *ifp)
{

	if_qflush(ifp);
}

static int
axgbe_media_change(struct ifnet *ifp)
{
	struct axgbe_softc *sc;
	int cur_media;

	sc = ifp->if_softc;

	sx_xlock(&sc->prv.an_mutex);
	cur_media = sc->media.ifm_cur->ifm_media;

	switch (IFM_SUBTYPE(cur_media)) {
	case IFM_10G_KR:
		sc->prv.phy.speed = SPEED_10000;
		sc->prv.phy.autoneg = AUTONEG_DISABLE;
		break;
	case IFM_2500_KX:
		sc->prv.phy.speed = SPEED_2500;
		sc->prv.phy.autoneg = AUTONEG_DISABLE;
		break;
	case IFM_1000_KX:
		sc->prv.phy.speed = SPEED_1000;
		sc->prv.phy.autoneg = AUTONEG_DISABLE;
		break;
	case IFM_AUTO:
		sc->prv.phy.autoneg = AUTONEG_ENABLE;
		break;
	}
	sx_xunlock(&sc->prv.an_mutex);

	return (-sc->prv.phy_if.phy_config_aneg(&sc->prv));
}

static void
axgbe_media_status(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct axgbe_softc *sc;

	sc = ifp->if_softc;

	ifmr->ifm_status = IFM_AVALID;
	if (!sc->prv.phy.link)
		return;

	ifmr->ifm_status |= IFM_ACTIVE;
	ifmr->ifm_active = IFM_ETHER;

	if (sc->prv.phy.duplex == DUPLEX_FULL)
		ifmr->ifm_active |= IFM_FDX;
	else
		ifmr->ifm_active |= IFM_HDX;

	switch (sc->prv.phy.speed) {
	case SPEED_10000:
		ifmr->ifm_active |= IFM_10G_KR;
		break;
	case SPEED_2500:
		ifmr->ifm_active |= IFM_2500_KX;
		break;
	case SPEED_1000:
		ifmr->ifm_active |= IFM_1000_KX;
		break;
	}
}

static uint64_t
axgbe_get_counter(struct ifnet *ifp, ift_counter c)
{
	struct xgbe_prv_data *pdata = ifp->if_softc;
	struct xgbe_mmc_stats *pstats = &pdata->mmc_stats;

	DBGPR("-->%s\n", __func__);

	pdata->hw_if.read_mmc_stats(pdata);

	switch(c) {
	case IFCOUNTER_IPACKETS:
		return (pstats->rxframecount_gb);
	case IFCOUNTER_IERRORS:
		return (pstats->rxframecount_gb -
		    pstats->rxbroadcastframes_g -
		    pstats->rxmulticastframes_g -
		    pstats->rxunicastframes_g);
	case IFCOUNTER_OPACKETS:
		return (pstats->txframecount_gb);
	case IFCOUNTER_OERRORS:
		return (pstats->txframecount_gb - pstats->txframecount_g);
	case IFCOUNTER_IBYTES:
		return (pstats->rxoctetcount_gb);
	case IFCOUNTER_OBYTES:
		return (pstats->txoctetcount_gb);
	default:
		return (if_get_counter_default(ifp, c));
	}
}

static int
axgbe_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_search_compatible(dev, compat_data)->ocd_data)
		return (ENXIO);

	device_set_desc(dev, "AMD 10 Gigabit Ethernet");
	return (BUS_PROBE_DEFAULT);
}

static int
axgbe_get_optional_prop(device_t dev, phandle_t node, const char *name,
    int *data, size_t len)
{

	if (!OF_hasprop(node, name))
		return (-1);

	if (OF_getencprop(node, name, data, len) <= 0) {
		device_printf(dev,"%s property is invalid\n", name);
		return (ENXIO);
	}

	return (0);
}

static int
axgbe_attach(device_t dev)
{
	struct axgbe_softc *sc;
	struct ifnet *ifp;
	pcell_t phy_handle;
	device_t phydev;
	phandle_t node, phy_node;
	struct resource *mac_res[11];
	struct resource *phy_res[4];
	ssize_t len;
	int error, i, j;

	sc = device_get_softc(dev);

	node = ofw_bus_get_node(dev);
	if (OF_getencprop(node, "phy-handle", &phy_handle,
	    sizeof(phy_handle)) <= 0) {
		phy_node = node;

		if (bus_alloc_resources(dev, mac_spec, mac_res)) {
			device_printf(dev,
			    "could not allocate phy resources\n");
			return (ENXIO);
		}

		sc->prv.xgmac_res = mac_res[0];
		sc->prv.xpcs_res = mac_res[1];
		sc->prv.rxtx_res = mac_res[2];
		sc->prv.sir0_res = mac_res[3];
		sc->prv.sir1_res = mac_res[4];

		sc->prv.dev_irq_res = mac_res[5];
		sc->prv.per_channel_irq = OF_hasprop(node,
		    XGBE_DMA_IRQS_PROPERTY);
		for (i = 0, j = 6; j < nitems(mac_res) - 1 &&
		    mac_res[j + 1] != NULL; i++, j++) {
			if (sc->prv.per_channel_irq) {
				sc->prv.chan_irq_res[i] = mac_res[j];
			}
		}

		/* The last entry is the auto-negotiation interrupt */
		sc->prv.an_irq_res = mac_res[j];
	} else {
		phydev = OF_device_from_xref(phy_handle);
		phy_node = ofw_bus_get_node(phydev);

		if (bus_alloc_resources(phydev, old_phy_spec, phy_res)) {
			device_printf(dev,
			    "could not allocate phy resources\n");
			return (ENXIO);
		}

		if (bus_alloc_resources(dev, old_mac_spec, mac_res)) {
			device_printf(dev,
			    "could not allocate mac resources\n");
			return (ENXIO);
		}

		sc->prv.rxtx_res = phy_res[0];
		sc->prv.sir0_res = phy_res[1];
		sc->prv.sir1_res = phy_res[2];
		sc->prv.an_irq_res = phy_res[3];

		sc->prv.xgmac_res = mac_res[0];
		sc->prv.xpcs_res = mac_res[1];
		sc->prv.dev_irq_res = mac_res[2];
		sc->prv.per_channel_irq = OF_hasprop(node,
		    XGBE_DMA_IRQS_PROPERTY);
		if (sc->prv.per_channel_irq) {
			for (i = 0, j = 3; i < nitems(sc->prv.chan_irq_res) &&
			    mac_res[j] != NULL; i++, j++) {
				sc->prv.chan_irq_res[i] = mac_res[j];
			}
		}
	}

	if ((len = OF_getproplen(node, "mac-address")) < 0) {
		device_printf(dev, "No mac-address property\n");
		return (EINVAL);
	}

	if (len != ETHER_ADDR_LEN)
		return (EINVAL);

	OF_getprop(node, "mac-address", sc->mac_addr, ETHER_ADDR_LEN);

	sc->prv.netdev = ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(dev, "Cannot alloc ifnet\n");
		return (ENXIO);
	}

	sc->prv.dev = dev;
	sc->prv.dmat = bus_get_dma_tag(dev);
	sc->prv.phy.advertising = ADVERTISED_10000baseKR_Full |
	    ADVERTISED_1000baseKX_Full;


	/*
	 * Read the needed properties from the phy node.
	 */

	/* This is documented as optional, but Linux requires it */
	if (OF_getencprop(phy_node, XGBE_SPEEDSET_PROPERTY, &sc->prv.speed_set,
	    sizeof(sc->prv.speed_set)) <= 0) {
		device_printf(dev, "%s property is missing\n",
		    XGBE_SPEEDSET_PROPERTY);
		return (EINVAL);
	}

	error = axgbe_get_optional_prop(dev, phy_node, XGBE_BLWC_PROPERTY,
	    sc->prv.serdes_blwc, sizeof(sc->prv.serdes_blwc));
	if (error > 0) {
		return (error);
	} else if (error < 0) {
		sc->prv.serdes_blwc[0] = XGBE_SPEED_1000_BLWC;
		sc->prv.serdes_blwc[1] = XGBE_SPEED_2500_BLWC;
		sc->prv.serdes_blwc[2] = XGBE_SPEED_10000_BLWC;
	}

	error = axgbe_get_optional_prop(dev, phy_node, XGBE_CDR_RATE_PROPERTY,
	    sc->prv.serdes_cdr_rate, sizeof(sc->prv.serdes_cdr_rate));
	if (error > 0) {
		return (error);
	} else if (error < 0) {
		sc->prv.serdes_cdr_rate[0] = XGBE_SPEED_1000_CDR;
		sc->prv.serdes_cdr_rate[1] = XGBE_SPEED_2500_CDR;
		sc->prv.serdes_cdr_rate[2] = XGBE_SPEED_10000_CDR;
	}

	error = axgbe_get_optional_prop(dev, phy_node, XGBE_PQ_SKEW_PROPERTY,
	    sc->prv.serdes_pq_skew, sizeof(sc->prv.serdes_pq_skew));
	if (error > 0) {
		return (error);
	} else if (error < 0) {
		sc->prv.serdes_pq_skew[0] = XGBE_SPEED_1000_PQ;
		sc->prv.serdes_pq_skew[1] = XGBE_SPEED_2500_PQ;
		sc->prv.serdes_pq_skew[2] = XGBE_SPEED_10000_PQ;
	}

	error = axgbe_get_optional_prop(dev, phy_node, XGBE_TX_AMP_PROPERTY,
	    sc->prv.serdes_tx_amp, sizeof(sc->prv.serdes_tx_amp));
	if (error > 0) {
		return (error);
	} else if (error < 0) {
		sc->prv.serdes_tx_amp[0] = XGBE_SPEED_1000_TXAMP;
		sc->prv.serdes_tx_amp[1] = XGBE_SPEED_2500_TXAMP;
		sc->prv.serdes_tx_amp[2] = XGBE_SPEED_10000_TXAMP;
	}

	error = axgbe_get_optional_prop(dev, phy_node, XGBE_DFE_CFG_PROPERTY,
	    sc->prv.serdes_dfe_tap_cfg, sizeof(sc->prv.serdes_dfe_tap_cfg));
	if (error > 0) {
		return (error);
	} else if (error < 0) {
		sc->prv.serdes_dfe_tap_cfg[0] = XGBE_SPEED_1000_DFE_TAP_CONFIG;
		sc->prv.serdes_dfe_tap_cfg[1] = XGBE_SPEED_2500_DFE_TAP_CONFIG;
		sc->prv.serdes_dfe_tap_cfg[2] = XGBE_SPEED_10000_DFE_TAP_CONFIG;
	}

	error = axgbe_get_optional_prop(dev, phy_node, XGBE_DFE_ENA_PROPERTY,
	    sc->prv.serdes_dfe_tap_ena, sizeof(sc->prv.serdes_dfe_tap_ena));
	if (error > 0) {
		return (error);
	} else if (error < 0) {
		sc->prv.serdes_dfe_tap_ena[0] = XGBE_SPEED_1000_DFE_TAP_ENABLE;
		sc->prv.serdes_dfe_tap_ena[1] = XGBE_SPEED_2500_DFE_TAP_ENABLE;
		sc->prv.serdes_dfe_tap_ena[2] = XGBE_SPEED_10000_DFE_TAP_ENABLE;
	}

	/* Check if the NIC is DMA coherent */
	sc->prv.coherent = OF_hasprop(node, "dma-coherent");
	if (sc->prv.coherent) {
		sc->prv.axdomain = XGBE_DMA_OS_AXDOMAIN;
		sc->prv.arcache = XGBE_DMA_OS_ARCACHE;
		sc->prv.awcache = XGBE_DMA_OS_AWCACHE;
	} else {
		sc->prv.axdomain = XGBE_DMA_SYS_AXDOMAIN;
		sc->prv.arcache = XGBE_DMA_SYS_ARCACHE;
		sc->prv.awcache = XGBE_DMA_SYS_AWCACHE;
	}

	/* Create the lock & workqueues */
	spin_lock_init(&sc->prv.xpcs_lock);
	sc->prv.dev_workqueue = taskqueue_create("axgbe", M_WAITOK,
	    taskqueue_thread_enqueue, &sc->prv.dev_workqueue);
	taskqueue_start_threads(&sc->prv.dev_workqueue, 1, PI_NET,
	    "axgbe taskq");

	/* Set the needed pointers */
	xgbe_init_function_ptrs_phy(&sc->prv.phy_if);
	xgbe_init_function_ptrs_dev(&sc->prv.hw_if);
	xgbe_init_function_ptrs_desc(&sc->prv.desc_if);

	/* Reset the hardware */
	sc->prv.hw_if.exit(&sc->prv);

	/* Read the hardware features */
	xgbe_get_all_hw_features(&sc->prv);

	/* Set default values */
	sc->prv.pblx8 = DMA_PBL_X8_ENABLE;
	sc->prv.tx_desc_count = XGBE_TX_DESC_CNT;
	sc->prv.tx_sf_mode = MTL_TSF_ENABLE;
	sc->prv.tx_threshold = MTL_TX_THRESHOLD_64;
	sc->prv.tx_pbl = DMA_PBL_16;
	sc->prv.tx_osp_mode = DMA_OSP_ENABLE;
	sc->prv.rx_desc_count = XGBE_RX_DESC_CNT;
	sc->prv.rx_sf_mode = MTL_RSF_DISABLE;
	sc->prv.rx_threshold = MTL_RX_THRESHOLD_64;
	sc->prv.rx_pbl = DMA_PBL_16;
	sc->prv.pause_autoneg = 1;
	sc->prv.tx_pause = 1;
	sc->prv.rx_pause = 1;
	sc->prv.phy_speed = SPEED_UNKNOWN;
	sc->prv.power_down = 0;

	/* TODO: Limit to min(ncpus, hw rings) */
	sc->prv.tx_ring_count = 1;
	sc->prv.tx_q_count = 1;
	sc->prv.rx_ring_count = 1;
	sc->prv.rx_q_count = sc->prv.hw_feat.rx_q_cnt;

	/* Init the PHY */
	sc->prv.phy_if.phy_init(&sc->prv);

	/* Set the coalescing */
	xgbe_init_rx_coalesce(&sc->prv);
	xgbe_init_tx_coalesce(&sc->prv);

	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_init = axgbe_init;
        ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = axgbe_ioctl;
	ifp->if_transmit = xgbe_xmit;
	ifp->if_qflush = axgbe_qflush;
	ifp->if_get_counter = axgbe_get_counter;

	/* TODO: Support HW offload */
	ifp->if_capabilities = 0;
	ifp->if_capenable = 0;
	ifp->if_hwassist = 0;

	ether_ifattach(ifp, sc->mac_addr);

	ifmedia_init(&sc->media, IFM_IMASK, axgbe_media_change,
	    axgbe_media_status);
#ifdef notyet
	ifmedia_add(&sc->media, IFM_ETHER | IFM_10G_KR, 0, NULL);
#endif
	ifmedia_add(&sc->media, IFM_ETHER | IFM_1000_KX, 0, NULL);
	ifmedia_add(&sc->media, IFM_ETHER | IFM_AUTO, 0, NULL);
	ifmedia_set(&sc->media, IFM_ETHER | IFM_AUTO);

	set_bit(XGBE_DOWN, &sc->prv.dev_state);

	if (xgbe_open(ifp) < 0) {
		device_printf(dev, "ndo_open failed\n");
		return (ENXIO);
	}

	return (0);
}

static device_method_t axgbe_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		axgbe_probe),
	DEVMETHOD(device_attach,	axgbe_attach),

	{ 0, 0 }
};

static devclass_t axgbe_devclass;

DEFINE_CLASS_0(axgbe, axgbe_driver, axgbe_methods,
    sizeof(struct axgbe_softc));
DRIVER_MODULE(axgbe, simplebus, axgbe_driver, axgbe_devclass, 0, 0);


static struct ofw_compat_data phy_compat_data[] = {
	{ "amd,xgbe-phy-seattle-v1a",	true },
	{ NULL,				false }
};

static int
axgbephy_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_search_compatible(dev, phy_compat_data)->ocd_data)
		return (ENXIO);

	device_set_desc(dev, "AMD 10 Gigabit Ethernet");
	return (BUS_PROBE_DEFAULT);
}

static int
axgbephy_attach(device_t dev)
{
	phandle_t node;

	node = ofw_bus_get_node(dev);
	OF_device_register_xref(OF_xref_from_node(node), dev);

	return (0);
}

static device_method_t axgbephy_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		axgbephy_probe),
	DEVMETHOD(device_attach,	axgbephy_attach),

	{ 0, 0 }
};

static devclass_t axgbephy_devclass;

DEFINE_CLASS_0(axgbephy, axgbephy_driver, axgbephy_methods, 0);
EARLY_DRIVER_MODULE(axgbephy, simplebus, axgbephy_driver, axgbephy_devclass,
    0, 0, BUS_PASS_RESOURCE + BUS_PASS_ORDER_MIDDLE);
