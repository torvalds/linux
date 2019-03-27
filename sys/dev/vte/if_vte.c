/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010, Pyun YongHyeon <yongari@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* Driver for DM&P Electronics, Inc, Vortex86 RDC R6040 FastEthernet. */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/rman.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>

#include <net/bpf.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_llc.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/if_vlan_var.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <machine/bus.h>

#include <dev/vte/if_vtereg.h>
#include <dev/vte/if_vtevar.h>

/* "device miibus" required.  See GENERIC if you get errors here. */
#include "miibus_if.h"

MODULE_DEPEND(vte, pci, 1, 1, 1);
MODULE_DEPEND(vte, ether, 1, 1, 1);
MODULE_DEPEND(vte, miibus, 1, 1, 1);

/* Tunables. */
static int tx_deep_copy = 1;
TUNABLE_INT("hw.vte.tx_deep_copy", &tx_deep_copy);

/*
 * Devices supported by this driver.
 */
static const struct vte_ident vte_ident_table[] = {
	{ VENDORID_RDC, DEVICEID_RDC_R6040, "RDC R6040 FastEthernet"},
	{ 0, 0, NULL}
};

static int	vte_attach(device_t);
static int	vte_detach(device_t);
static int	vte_dma_alloc(struct vte_softc *);
static void	vte_dma_free(struct vte_softc *);
static void	vte_dmamap_cb(void *, bus_dma_segment_t *, int, int);
static struct vte_txdesc *
		vte_encap(struct vte_softc *, struct mbuf **);
static const struct vte_ident *
		vte_find_ident(device_t);
#ifndef __NO_STRICT_ALIGNMENT
static struct mbuf *
		vte_fixup_rx(struct ifnet *, struct mbuf *);
#endif
static void	vte_get_macaddr(struct vte_softc *);
static void	vte_init(void *);
static void	vte_init_locked(struct vte_softc *);
static int	vte_init_rx_ring(struct vte_softc *);
static int	vte_init_tx_ring(struct vte_softc *);
static void	vte_intr(void *);
static int	vte_ioctl(struct ifnet *, u_long, caddr_t);
static uint64_t	vte_get_counter(struct ifnet *, ift_counter);
static void	vte_mac_config(struct vte_softc *);
static int	vte_miibus_readreg(device_t, int, int);
static void	vte_miibus_statchg(device_t);
static int	vte_miibus_writereg(device_t, int, int, int);
static int	vte_mediachange(struct ifnet *);
static int	vte_mediachange_locked(struct ifnet *);
static void	vte_mediastatus(struct ifnet *, struct ifmediareq *);
static int	vte_newbuf(struct vte_softc *, struct vte_rxdesc *);
static int	vte_probe(device_t);
static void	vte_reset(struct vte_softc *);
static int	vte_resume(device_t);
static void	vte_rxeof(struct vte_softc *);
static void	vte_rxfilter(struct vte_softc *);
static int	vte_shutdown(device_t);
static void	vte_start(struct ifnet *);
static void	vte_start_locked(struct vte_softc *);
static void	vte_start_mac(struct vte_softc *);
static void	vte_stats_clear(struct vte_softc *);
static void	vte_stats_update(struct vte_softc *);
static void	vte_stop(struct vte_softc *);
static void	vte_stop_mac(struct vte_softc *);
static int	vte_suspend(device_t);
static void	vte_sysctl_node(struct vte_softc *);
static void	vte_tick(void *);
static void	vte_txeof(struct vte_softc *);
static void	vte_watchdog(struct vte_softc *);
static int	sysctl_int_range(SYSCTL_HANDLER_ARGS, int, int);
static int	sysctl_hw_vte_int_mod(SYSCTL_HANDLER_ARGS);

static device_method_t vte_methods[] = {
	/* Device interface. */
	DEVMETHOD(device_probe,		vte_probe),
	DEVMETHOD(device_attach,	vte_attach),
	DEVMETHOD(device_detach,	vte_detach),
	DEVMETHOD(device_shutdown,	vte_shutdown),
	DEVMETHOD(device_suspend,	vte_suspend),
	DEVMETHOD(device_resume,	vte_resume),

	/* MII interface. */
	DEVMETHOD(miibus_readreg,	vte_miibus_readreg),
	DEVMETHOD(miibus_writereg,	vte_miibus_writereg),
	DEVMETHOD(miibus_statchg,	vte_miibus_statchg),

	DEVMETHOD_END
};

static driver_t vte_driver = {
	"vte",
	vte_methods,
	sizeof(struct vte_softc)
};

static devclass_t vte_devclass;

DRIVER_MODULE(vte, pci, vte_driver, vte_devclass, 0, 0);
DRIVER_MODULE(miibus, vte, miibus_driver, miibus_devclass, 0, 0);

static int
vte_miibus_readreg(device_t dev, int phy, int reg)
{
	struct vte_softc *sc;
	int i;

	sc = device_get_softc(dev);

	CSR_WRITE_2(sc, VTE_MMDIO, MMDIO_READ |
	    (phy << MMDIO_PHY_ADDR_SHIFT) | (reg << MMDIO_REG_ADDR_SHIFT));
	for (i = VTE_PHY_TIMEOUT; i > 0; i--) {
		DELAY(5);
		if ((CSR_READ_2(sc, VTE_MMDIO) & MMDIO_READ) == 0)
			break;
	}

	if (i == 0) {
		device_printf(sc->vte_dev, "phy read timeout : %d\n", reg);
		return (0);
	}

	return (CSR_READ_2(sc, VTE_MMRD));
}

static int
vte_miibus_writereg(device_t dev, int phy, int reg, int val)
{
	struct vte_softc *sc;
	int i;

	sc = device_get_softc(dev);

	CSR_WRITE_2(sc, VTE_MMWD, val);
	CSR_WRITE_2(sc, VTE_MMDIO, MMDIO_WRITE |
	    (phy << MMDIO_PHY_ADDR_SHIFT) | (reg << MMDIO_REG_ADDR_SHIFT));
	for (i = VTE_PHY_TIMEOUT; i > 0; i--) {
		DELAY(5);
		if ((CSR_READ_2(sc, VTE_MMDIO) & MMDIO_WRITE) == 0)
			break;
	}

	if (i == 0)
		device_printf(sc->vte_dev, "phy write timeout : %d\n", reg);

	return (0);
}

static void
vte_miibus_statchg(device_t dev)
{
	struct vte_softc *sc;
	struct mii_data *mii;
	struct ifnet *ifp;
	uint16_t val;

	sc = device_get_softc(dev);

	mii = device_get_softc(sc->vte_miibus);
	ifp = sc->vte_ifp;
	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0)
		return;

	sc->vte_flags &= ~VTE_FLAG_LINK;
	if ((mii->mii_media_status & (IFM_ACTIVE | IFM_AVALID)) ==
	    (IFM_ACTIVE | IFM_AVALID)) {
		switch (IFM_SUBTYPE(mii->mii_media_active)) {
		case IFM_10_T:
		case IFM_100_TX:
			sc->vte_flags |= VTE_FLAG_LINK;
			break;
		default:
			break;
		}
	}

	/* Stop RX/TX MACs. */
	vte_stop_mac(sc);
	/* Program MACs with resolved duplex and flow control. */
	if ((sc->vte_flags & VTE_FLAG_LINK) != 0) {
		/*
		 * Timer waiting time : (63 + TIMER * 64) MII clock.
		 * MII clock : 25MHz(100Mbps) or 2.5MHz(10Mbps).
		 */
		if (IFM_SUBTYPE(mii->mii_media_active) == IFM_100_TX)
			val = 18 << VTE_IM_TIMER_SHIFT;
		else
			val = 1 << VTE_IM_TIMER_SHIFT;
		val |= sc->vte_int_rx_mod << VTE_IM_BUNDLE_SHIFT;
		/* 48.6us for 100Mbps, 50.8us for 10Mbps */
		CSR_WRITE_2(sc, VTE_MRICR, val);

		if (IFM_SUBTYPE(mii->mii_media_active) == IFM_100_TX)
			val = 18 << VTE_IM_TIMER_SHIFT;
		else
			val = 1 << VTE_IM_TIMER_SHIFT;
		val |= sc->vte_int_tx_mod << VTE_IM_BUNDLE_SHIFT;
		/* 48.6us for 100Mbps, 50.8us for 10Mbps */
		CSR_WRITE_2(sc, VTE_MTICR, val);

		vte_mac_config(sc);
		vte_start_mac(sc);
	}
}

static void
vte_mediastatus(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct vte_softc *sc;
	struct mii_data *mii;

	sc = ifp->if_softc;
	VTE_LOCK(sc);
	if ((ifp->if_flags & IFF_UP) == 0) {
		VTE_UNLOCK(sc);
		return;
	}
	mii = device_get_softc(sc->vte_miibus);

	mii_pollstat(mii);
	ifmr->ifm_status = mii->mii_media_status;
	ifmr->ifm_active = mii->mii_media_active;
	VTE_UNLOCK(sc);
}

static int
vte_mediachange(struct ifnet *ifp)
{
	struct vte_softc *sc;
	int error;

	sc = ifp->if_softc;
	VTE_LOCK(sc);
	error = vte_mediachange_locked(ifp);
	VTE_UNLOCK(sc);
	return (error);
}

static int
vte_mediachange_locked(struct ifnet *ifp)
{
	struct vte_softc *sc;
	struct mii_data *mii;
	struct mii_softc *miisc;
	int error;

	sc = ifp->if_softc;
	mii = device_get_softc(sc->vte_miibus);
	LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
		PHY_RESET(miisc);
	error = mii_mediachg(mii);

	return (error);
}

static const struct vte_ident *
vte_find_ident(device_t dev)
{
	const struct vte_ident *ident;
	uint16_t vendor, devid;

	vendor = pci_get_vendor(dev);
	devid = pci_get_device(dev);
	for (ident = vte_ident_table; ident->name != NULL; ident++) {
		if (vendor == ident->vendorid && devid == ident->deviceid)
			return (ident);
	}

	return (NULL);
}

static int
vte_probe(device_t dev)
{
	const struct vte_ident *ident;

	ident = vte_find_ident(dev);
	if (ident != NULL) {
		device_set_desc(dev, ident->name);
		return (BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
}

static void
vte_get_macaddr(struct vte_softc *sc)
{
	uint16_t mid;

	/*
	 * It seems there is no way to reload station address and
	 * it is supposed to be set by BIOS.
	 */
	mid = CSR_READ_2(sc, VTE_MID0L);
	sc->vte_eaddr[0] = (mid >> 0) & 0xFF;
	sc->vte_eaddr[1] = (mid >> 8) & 0xFF;
	mid = CSR_READ_2(sc, VTE_MID0M);
	sc->vte_eaddr[2] = (mid >> 0) & 0xFF;
	sc->vte_eaddr[3] = (mid >> 8) & 0xFF;
	mid = CSR_READ_2(sc, VTE_MID0H);
	sc->vte_eaddr[4] = (mid >> 0) & 0xFF;
	sc->vte_eaddr[5] = (mid >> 8) & 0xFF;
}

static int
vte_attach(device_t dev)
{
	struct vte_softc *sc;
	struct ifnet *ifp;
	uint16_t macid;
	int error, rid;

	error = 0;
	sc = device_get_softc(dev);
	sc->vte_dev = dev;

	mtx_init(&sc->vte_mtx, device_get_nameunit(dev), MTX_NETWORK_LOCK,
	    MTX_DEF);
	callout_init_mtx(&sc->vte_tick_ch, &sc->vte_mtx, 0);
	sc->vte_ident = vte_find_ident(dev);

	/* Map the device. */
	pci_enable_busmaster(dev);
	sc->vte_res_id = PCIR_BAR(1);
	sc->vte_res_type = SYS_RES_MEMORY;
	sc->vte_res = bus_alloc_resource_any(dev, sc->vte_res_type,
	    &sc->vte_res_id, RF_ACTIVE);
	if (sc->vte_res == NULL) {
		sc->vte_res_id = PCIR_BAR(0);
		sc->vte_res_type = SYS_RES_IOPORT;
		sc->vte_res = bus_alloc_resource_any(dev, sc->vte_res_type,
		    &sc->vte_res_id, RF_ACTIVE);
		if (sc->vte_res == NULL) {
			device_printf(dev, "cannot map memory/ports.\n");
			mtx_destroy(&sc->vte_mtx);
			return (ENXIO);
		}
	}
	if (bootverbose) {
		device_printf(dev, "using %s space register mapping\n",
		    sc->vte_res_type == SYS_RES_MEMORY ? "memory" : "I/O");
		device_printf(dev, "MAC Identifier : 0x%04x\n",
		    CSR_READ_2(sc, VTE_MACID));
		macid = CSR_READ_2(sc, VTE_MACID_REV);
		device_printf(dev, "MAC Id. 0x%02x, Rev. 0x%02x\n",
		    (macid & VTE_MACID_MASK) >> VTE_MACID_SHIFT,
		    (macid & VTE_MACID_REV_MASK) >> VTE_MACID_REV_SHIFT);
	}

	rid = 0;
	sc->vte_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_SHAREABLE | RF_ACTIVE);
	if (sc->vte_irq == NULL) {
		device_printf(dev, "cannot allocate IRQ resources.\n");
		error = ENXIO;
		goto fail;
	}

	/* Reset the ethernet controller. */
	vte_reset(sc);

	if ((error = vte_dma_alloc(sc)) != 0)
		goto fail;

	/* Create device sysctl node. */
	vte_sysctl_node(sc);

	/* Load station address. */
	vte_get_macaddr(sc);

	ifp = sc->vte_ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(dev, "cannot allocate ifnet structure.\n");
		error = ENXIO;
		goto fail;
	}

	ifp->if_softc = sc;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = vte_ioctl;
	ifp->if_start = vte_start;
	ifp->if_init = vte_init;
	ifp->if_get_counter = vte_get_counter;
	ifp->if_snd.ifq_drv_maxlen = VTE_TX_RING_CNT - 1;
	IFQ_SET_MAXLEN(&ifp->if_snd, ifp->if_snd.ifq_drv_maxlen);
	IFQ_SET_READY(&ifp->if_snd);

	/*
	 * Set up MII bus.
	 * BIOS would have initialized VTE_MPSCCR to catch PHY
	 * status changes so driver may be able to extract
	 * configured PHY address.  Since it's common to see BIOS
	 * fails to initialize the register(including the sample
	 * board I have), let mii(4) probe it.  This is more
	 * reliable than relying on BIOS's initialization.
	 *
	 * Advertising flow control capability to mii(4) was
	 * intentionally disabled due to severe problems in TX
	 * pause frame generation.  See vte_rxeof() for more
	 * details.
	 */
	error = mii_attach(dev, &sc->vte_miibus, ifp, vte_mediachange,
	    vte_mediastatus, BMSR_DEFCAPMASK, MII_PHY_ANY, MII_OFFSET_ANY, 0);
	if (error != 0) {
		device_printf(dev, "attaching PHYs failed\n");
		goto fail;
	}

	ether_ifattach(ifp, sc->vte_eaddr);

	/* VLAN capability setup. */
	ifp->if_capabilities |= IFCAP_VLAN_MTU;
	ifp->if_capenable = ifp->if_capabilities;
	/* Tell the upper layer we support VLAN over-sized frames. */
	ifp->if_hdrlen = sizeof(struct ether_vlan_header);

	error = bus_setup_intr(dev, sc->vte_irq, INTR_TYPE_NET | INTR_MPSAFE,
	    NULL, vte_intr, sc, &sc->vte_intrhand);
	if (error != 0) {
		device_printf(dev, "could not set up interrupt handler.\n");
		ether_ifdetach(ifp);
		goto fail;
	}

fail:
	if (error != 0)
		vte_detach(dev);

	return (error);
}

static int
vte_detach(device_t dev)
{
	struct vte_softc *sc;
	struct ifnet *ifp;

	sc = device_get_softc(dev);

	ifp = sc->vte_ifp;
	if (device_is_attached(dev)) {
		VTE_LOCK(sc);
		vte_stop(sc);
		VTE_UNLOCK(sc);
		callout_drain(&sc->vte_tick_ch);
		ether_ifdetach(ifp);
	}

	if (sc->vte_miibus != NULL) {
		device_delete_child(dev, sc->vte_miibus);
		sc->vte_miibus = NULL;
	}
	bus_generic_detach(dev);

	if (sc->vte_intrhand != NULL) {
		bus_teardown_intr(dev, sc->vte_irq, sc->vte_intrhand);
		sc->vte_intrhand = NULL;
	}
	if (sc->vte_irq != NULL) {
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->vte_irq);
		sc->vte_irq = NULL;
	}
	if (sc->vte_res != NULL) {
		bus_release_resource(dev, sc->vte_res_type, sc->vte_res_id,
		    sc->vte_res);
		sc->vte_res = NULL;
	}
	if (ifp != NULL) {
		if_free(ifp);
		sc->vte_ifp = NULL;
	}
	vte_dma_free(sc);
	mtx_destroy(&sc->vte_mtx);

	return (0);
}

#define	VTE_SYSCTL_STAT_ADD32(c, h, n, p, d)	\
	    SYSCTL_ADD_UINT(c, h, OID_AUTO, n, CTLFLAG_RD, p, 0, d)

static void
vte_sysctl_node(struct vte_softc *sc)
{
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid_list *child, *parent;
	struct sysctl_oid *tree;
	struct vte_hw_stats *stats;
	int error;

	stats = &sc->vte_stats;
	ctx = device_get_sysctl_ctx(sc->vte_dev);
	child = SYSCTL_CHILDREN(device_get_sysctl_tree(sc->vte_dev));

	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "int_rx_mod",
	    CTLTYPE_INT | CTLFLAG_RW, &sc->vte_int_rx_mod, 0,
	    sysctl_hw_vte_int_mod, "I", "vte RX interrupt moderation");
	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "int_tx_mod",
	    CTLTYPE_INT | CTLFLAG_RW, &sc->vte_int_tx_mod, 0,
	    sysctl_hw_vte_int_mod, "I", "vte TX interrupt moderation");
	/* Pull in device tunables. */
	sc->vte_int_rx_mod = VTE_IM_RX_BUNDLE_DEFAULT;
	error = resource_int_value(device_get_name(sc->vte_dev),
	    device_get_unit(sc->vte_dev), "int_rx_mod", &sc->vte_int_rx_mod);
	if (error == 0) {
		if (sc->vte_int_rx_mod < VTE_IM_BUNDLE_MIN ||
		    sc->vte_int_rx_mod > VTE_IM_BUNDLE_MAX) {
			device_printf(sc->vte_dev, "int_rx_mod value out of "
			    "range; using default: %d\n",
			    VTE_IM_RX_BUNDLE_DEFAULT);
			sc->vte_int_rx_mod = VTE_IM_RX_BUNDLE_DEFAULT;
		}
	}

	sc->vte_int_tx_mod = VTE_IM_TX_BUNDLE_DEFAULT;
	error = resource_int_value(device_get_name(sc->vte_dev),
	    device_get_unit(sc->vte_dev), "int_tx_mod", &sc->vte_int_tx_mod);
	if (error == 0) {
		if (sc->vte_int_tx_mod < VTE_IM_BUNDLE_MIN ||
		    sc->vte_int_tx_mod > VTE_IM_BUNDLE_MAX) {
			device_printf(sc->vte_dev, "int_tx_mod value out of "
			    "range; using default: %d\n",
			    VTE_IM_TX_BUNDLE_DEFAULT);
			sc->vte_int_tx_mod = VTE_IM_TX_BUNDLE_DEFAULT;
		}
	}

	tree = SYSCTL_ADD_NODE(ctx, child, OID_AUTO, "stats", CTLFLAG_RD,
	    NULL, "VTE statistics");
	parent = SYSCTL_CHILDREN(tree);

	/* RX statistics. */
	tree = SYSCTL_ADD_NODE(ctx, parent, OID_AUTO, "rx", CTLFLAG_RD,
	    NULL, "RX MAC statistics");
	child = SYSCTL_CHILDREN(tree);
	VTE_SYSCTL_STAT_ADD32(ctx, child, "good_frames",
	    &stats->rx_frames, "Good frames");
	VTE_SYSCTL_STAT_ADD32(ctx, child, "good_bcast_frames",
	    &stats->rx_bcast_frames, "Good broadcast frames");
	VTE_SYSCTL_STAT_ADD32(ctx, child, "good_mcast_frames",
	    &stats->rx_mcast_frames, "Good multicast frames");
	VTE_SYSCTL_STAT_ADD32(ctx, child, "runt",
	    &stats->rx_runts, "Too short frames");
	VTE_SYSCTL_STAT_ADD32(ctx, child, "crc_errs",
	    &stats->rx_crcerrs, "CRC errors");
	VTE_SYSCTL_STAT_ADD32(ctx, child, "long_frames",
	    &stats->rx_long_frames,
	    "Frames that have longer length than maximum packet length");
	VTE_SYSCTL_STAT_ADD32(ctx, child, "fifo_full",
	    &stats->rx_fifo_full, "FIFO full");
	VTE_SYSCTL_STAT_ADD32(ctx, child, "desc_unavail",
	    &stats->rx_desc_unavail, "Descriptor unavailable frames");
	VTE_SYSCTL_STAT_ADD32(ctx, child, "pause_frames",
	    &stats->rx_pause_frames, "Pause control frames");

	/* TX statistics. */
	tree = SYSCTL_ADD_NODE(ctx, parent, OID_AUTO, "tx", CTLFLAG_RD,
	    NULL, "TX MAC statistics");
	child = SYSCTL_CHILDREN(tree);
	VTE_SYSCTL_STAT_ADD32(ctx, child, "good_frames",
	    &stats->tx_frames, "Good frames");
	VTE_SYSCTL_STAT_ADD32(ctx, child, "underruns",
	    &stats->tx_underruns, "FIFO underruns");
	VTE_SYSCTL_STAT_ADD32(ctx, child, "late_colls",
	    &stats->tx_late_colls, "Late collisions");
	VTE_SYSCTL_STAT_ADD32(ctx, child, "pause_frames",
	    &stats->tx_pause_frames, "Pause control frames");
}

#undef VTE_SYSCTL_STAT_ADD32

struct vte_dmamap_arg {
	bus_addr_t	vte_busaddr;
};

static void
vte_dmamap_cb(void *arg, bus_dma_segment_t *segs, int nsegs, int error)
{
	struct vte_dmamap_arg *ctx;

	if (error != 0)
		return;

	KASSERT(nsegs == 1, ("%s: %d segments returned!", __func__, nsegs));

	ctx = (struct vte_dmamap_arg *)arg;
	ctx->vte_busaddr = segs[0].ds_addr;
}

static int
vte_dma_alloc(struct vte_softc *sc)
{
	struct vte_txdesc *txd;
	struct vte_rxdesc *rxd;
	struct vte_dmamap_arg ctx;
	int error, i;

	/* Create parent DMA tag. */
	error = bus_dma_tag_create(
	    bus_get_dma_tag(sc->vte_dev), /* parent */
	    1, 0,			/* alignment, boundary */
	    BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    BUS_SPACE_MAXSIZE_32BIT,	/* maxsize */
	    0,				/* nsegments */
	    BUS_SPACE_MAXSIZE_32BIT,	/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->vte_cdata.vte_parent_tag);
	if (error != 0) {
		device_printf(sc->vte_dev,
		    "could not create parent DMA tag.\n");
		goto fail;
	}

	/* Create DMA tag for TX descriptor ring. */
	error = bus_dma_tag_create(
	    sc->vte_cdata.vte_parent_tag, /* parent */
	    VTE_TX_RING_ALIGN, 0,	/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    VTE_TX_RING_SZ,		/* maxsize */
	    1,				/* nsegments */
	    VTE_TX_RING_SZ,		/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->vte_cdata.vte_tx_ring_tag);
	if (error != 0) {
		device_printf(sc->vte_dev,
		    "could not create TX ring DMA tag.\n");
		goto fail;
	}

	/* Create DMA tag for RX free descriptor ring. */
	error = bus_dma_tag_create(
	    sc->vte_cdata.vte_parent_tag, /* parent */
	    VTE_RX_RING_ALIGN, 0,	/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    VTE_RX_RING_SZ,		/* maxsize */
	    1,				/* nsegments */
	    VTE_RX_RING_SZ,		/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->vte_cdata.vte_rx_ring_tag);
	if (error != 0) {
		device_printf(sc->vte_dev,
		    "could not create RX ring DMA tag.\n");
		goto fail;
	}

	/* Allocate DMA'able memory and load the DMA map for TX ring. */
	error = bus_dmamem_alloc(sc->vte_cdata.vte_tx_ring_tag,
	    (void **)&sc->vte_cdata.vte_tx_ring,
	    BUS_DMA_WAITOK | BUS_DMA_ZERO | BUS_DMA_COHERENT,
	    &sc->vte_cdata.vte_tx_ring_map);
	if (error != 0) {
		device_printf(sc->vte_dev,
		    "could not allocate DMA'able memory for TX ring.\n");
		goto fail;
	}
	ctx.vte_busaddr = 0;
	error = bus_dmamap_load(sc->vte_cdata.vte_tx_ring_tag,
	    sc->vte_cdata.vte_tx_ring_map, sc->vte_cdata.vte_tx_ring,
	    VTE_TX_RING_SZ, vte_dmamap_cb, &ctx, 0);
	if (error != 0 || ctx.vte_busaddr == 0) {
		device_printf(sc->vte_dev,
		    "could not load DMA'able memory for TX ring.\n");
		goto fail;
	}
	sc->vte_cdata.vte_tx_ring_paddr = ctx.vte_busaddr;

	/* Allocate DMA'able memory and load the DMA map for RX ring. */
	error = bus_dmamem_alloc(sc->vte_cdata.vte_rx_ring_tag,
	    (void **)&sc->vte_cdata.vte_rx_ring,
	    BUS_DMA_WAITOK | BUS_DMA_ZERO | BUS_DMA_COHERENT,
	    &sc->vte_cdata.vte_rx_ring_map);
	if (error != 0) {
		device_printf(sc->vte_dev,
		    "could not allocate DMA'able memory for RX ring.\n");
		goto fail;
	}
	ctx.vte_busaddr = 0;
	error = bus_dmamap_load(sc->vte_cdata.vte_rx_ring_tag,
	    sc->vte_cdata.vte_rx_ring_map, sc->vte_cdata.vte_rx_ring,
	    VTE_RX_RING_SZ, vte_dmamap_cb, &ctx, 0);
	if (error != 0 || ctx.vte_busaddr == 0) {
		device_printf(sc->vte_dev,
		    "could not load DMA'able memory for RX ring.\n");
		goto fail;
	}
	sc->vte_cdata.vte_rx_ring_paddr = ctx.vte_busaddr;

	/* Create TX buffer parent tag. */
	error = bus_dma_tag_create(
	    bus_get_dma_tag(sc->vte_dev), /* parent */
	    1, 0,			/* alignment, boundary */
	    BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    BUS_SPACE_MAXSIZE_32BIT,	/* maxsize */
	    0,				/* nsegments */
	    BUS_SPACE_MAXSIZE_32BIT,	/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->vte_cdata.vte_buffer_tag);
	if (error != 0) {
		device_printf(sc->vte_dev,
		    "could not create parent buffer DMA tag.\n");
		goto fail;
	}

	/* Create DMA tag for TX buffers. */
	error = bus_dma_tag_create(
	    sc->vte_cdata.vte_buffer_tag, /* parent */
	    1, 0,			/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    MCLBYTES,			/* maxsize */
	    1,				/* nsegments */
	    MCLBYTES,			/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->vte_cdata.vte_tx_tag);
	if (error != 0) {
		device_printf(sc->vte_dev, "could not create TX DMA tag.\n");
		goto fail;
	}

	/* Create DMA tag for RX buffers. */
	error = bus_dma_tag_create(
	    sc->vte_cdata.vte_buffer_tag, /* parent */
	    VTE_RX_BUF_ALIGN, 0,	/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    MCLBYTES,			/* maxsize */
	    1,				/* nsegments */
	    MCLBYTES,			/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->vte_cdata.vte_rx_tag);
	if (error != 0) {
		device_printf(sc->vte_dev, "could not create RX DMA tag.\n");
		goto fail;
	}
	/* Create DMA maps for TX buffers. */
	for (i = 0; i < VTE_TX_RING_CNT; i++) {
		txd = &sc->vte_cdata.vte_txdesc[i];
		txd->tx_m = NULL;
		txd->tx_dmamap = NULL;
		error = bus_dmamap_create(sc->vte_cdata.vte_tx_tag, 0,
		    &txd->tx_dmamap);
		if (error != 0) {
			device_printf(sc->vte_dev,
			    "could not create TX dmamap.\n");
			goto fail;
		}
	}
	/* Create DMA maps for RX buffers. */
	if ((error = bus_dmamap_create(sc->vte_cdata.vte_rx_tag, 0,
	    &sc->vte_cdata.vte_rx_sparemap)) != 0) {
		device_printf(sc->vte_dev,
		    "could not create spare RX dmamap.\n");
		goto fail;
	}
	for (i = 0; i < VTE_RX_RING_CNT; i++) {
		rxd = &sc->vte_cdata.vte_rxdesc[i];
		rxd->rx_m = NULL;
		rxd->rx_dmamap = NULL;
		error = bus_dmamap_create(sc->vte_cdata.vte_rx_tag, 0,
		    &rxd->rx_dmamap);
		if (error != 0) {
			device_printf(sc->vte_dev,
			    "could not create RX dmamap.\n");
			goto fail;
		}
	}

fail:
	return (error);
}

static void
vte_dma_free(struct vte_softc *sc)
{
	struct vte_txdesc *txd;
	struct vte_rxdesc *rxd;
	int i;

	/* TX buffers. */
	if (sc->vte_cdata.vte_tx_tag != NULL) {
		for (i = 0; i < VTE_TX_RING_CNT; i++) {
			txd = &sc->vte_cdata.vte_txdesc[i];
			if (txd->tx_dmamap != NULL) {
				bus_dmamap_destroy(sc->vte_cdata.vte_tx_tag,
				    txd->tx_dmamap);
				txd->tx_dmamap = NULL;
			}
		}
		bus_dma_tag_destroy(sc->vte_cdata.vte_tx_tag);
		sc->vte_cdata.vte_tx_tag = NULL;
	}
	/* RX buffers */
	if (sc->vte_cdata.vte_rx_tag != NULL) {
		for (i = 0; i < VTE_RX_RING_CNT; i++) {
			rxd = &sc->vte_cdata.vte_rxdesc[i];
			if (rxd->rx_dmamap != NULL) {
				bus_dmamap_destroy(sc->vte_cdata.vte_rx_tag,
				    rxd->rx_dmamap);
				rxd->rx_dmamap = NULL;
			}
		}
		if (sc->vte_cdata.vte_rx_sparemap != NULL) {
			bus_dmamap_destroy(sc->vte_cdata.vte_rx_tag,
			    sc->vte_cdata.vte_rx_sparemap);
			sc->vte_cdata.vte_rx_sparemap = NULL;
		}
		bus_dma_tag_destroy(sc->vte_cdata.vte_rx_tag);
		sc->vte_cdata.vte_rx_tag = NULL;
	}
	/* TX descriptor ring. */
	if (sc->vte_cdata.vte_tx_ring_tag != NULL) {
		if (sc->vte_cdata.vte_tx_ring_paddr != 0)
			bus_dmamap_unload(sc->vte_cdata.vte_tx_ring_tag,
			    sc->vte_cdata.vte_tx_ring_map);
		if (sc->vte_cdata.vte_tx_ring != NULL)
			bus_dmamem_free(sc->vte_cdata.vte_tx_ring_tag,
			    sc->vte_cdata.vte_tx_ring,
			    sc->vte_cdata.vte_tx_ring_map);
		sc->vte_cdata.vte_tx_ring = NULL;
		sc->vte_cdata.vte_tx_ring_paddr = 0;
		bus_dma_tag_destroy(sc->vte_cdata.vte_tx_ring_tag);
		sc->vte_cdata.vte_tx_ring_tag = NULL;
	}
	/* RX ring. */
	if (sc->vte_cdata.vte_rx_ring_tag != NULL) {
		if (sc->vte_cdata.vte_rx_ring_paddr != 0)
			bus_dmamap_unload(sc->vte_cdata.vte_rx_ring_tag,
			    sc->vte_cdata.vte_rx_ring_map);
		if (sc->vte_cdata.vte_rx_ring != NULL)
			bus_dmamem_free(sc->vte_cdata.vte_rx_ring_tag,
			    sc->vte_cdata.vte_rx_ring,
			    sc->vte_cdata.vte_rx_ring_map);
		sc->vte_cdata.vte_rx_ring = NULL;
		sc->vte_cdata.vte_rx_ring_paddr = 0;
		bus_dma_tag_destroy(sc->vte_cdata.vte_rx_ring_tag);
		sc->vte_cdata.vte_rx_ring_tag = NULL;
	}
	if (sc->vte_cdata.vte_buffer_tag != NULL) {
		bus_dma_tag_destroy(sc->vte_cdata.vte_buffer_tag);
		sc->vte_cdata.vte_buffer_tag = NULL;
	}
	if (sc->vte_cdata.vte_parent_tag != NULL) {
		bus_dma_tag_destroy(sc->vte_cdata.vte_parent_tag);
		sc->vte_cdata.vte_parent_tag = NULL;
	}
}

static int
vte_shutdown(device_t dev)
{

	return (vte_suspend(dev));
}

static int
vte_suspend(device_t dev)
{
	struct vte_softc *sc;
	struct ifnet *ifp;

	sc = device_get_softc(dev);

	VTE_LOCK(sc);
	ifp = sc->vte_ifp;
	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
		vte_stop(sc);
	VTE_UNLOCK(sc);

	return (0);
}

static int
vte_resume(device_t dev)
{
	struct vte_softc *sc;
	struct ifnet *ifp;

	sc = device_get_softc(dev);

	VTE_LOCK(sc);
	ifp = sc->vte_ifp;
	if ((ifp->if_flags & IFF_UP) != 0) {
		ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
		vte_init_locked(sc);
	}
	VTE_UNLOCK(sc);

	return (0);
}

static struct vte_txdesc *
vte_encap(struct vte_softc *sc, struct mbuf **m_head)
{
	struct vte_txdesc *txd;
	struct mbuf *m, *n;
	bus_dma_segment_t txsegs[1];
	int copy, error, nsegs, padlen;

	VTE_LOCK_ASSERT(sc);

	M_ASSERTPKTHDR((*m_head));

	txd = &sc->vte_cdata.vte_txdesc[sc->vte_cdata.vte_tx_prod];
	m = *m_head;
	/*
	 * Controller doesn't auto-pad, so we have to make sure pad
	 * short frames out to the minimum frame length.
	 */
	if (m->m_pkthdr.len < VTE_MIN_FRAMELEN)
		padlen = VTE_MIN_FRAMELEN - m->m_pkthdr.len;
	else
		padlen = 0;

	/*
	 * Controller does not support multi-fragmented TX buffers.
	 * Controller spends most of its TX processing time in
	 * de-fragmenting TX buffers.  Either faster CPU or more
	 * advanced controller DMA engine is required to speed up
	 * TX path processing.
	 * To mitigate the de-fragmenting issue, perform deep copy
	 * from fragmented mbuf chains to a pre-allocated mbuf
	 * cluster with extra cost of kernel memory.  For frames
	 * that is composed of single TX buffer, the deep copy is
	 * bypassed.
	 */
	if (tx_deep_copy != 0) {
		copy = 0;
		if (m->m_next != NULL)
			copy++;
		if (padlen > 0 && (M_WRITABLE(m) == 0 ||
		    padlen > M_TRAILINGSPACE(m)))
			copy++;
		if (copy != 0) {
			/* Avoid expensive m_defrag(9) and do deep copy. */
			n = sc->vte_cdata.vte_txmbufs[sc->vte_cdata.vte_tx_prod];
			m_copydata(m, 0, m->m_pkthdr.len, mtod(n, char *));
			n->m_pkthdr.len = m->m_pkthdr.len;
			n->m_len = m->m_pkthdr.len;
			m = n;
			txd->tx_flags |= VTE_TXMBUF;
		}

		if (padlen > 0) {
			/* Zero out the bytes in the pad area. */
			bzero(mtod(m, char *) + m->m_pkthdr.len, padlen);
			m->m_pkthdr.len += padlen;
			m->m_len = m->m_pkthdr.len;
		}
	} else {
		if (M_WRITABLE(m) == 0) {
			if (m->m_next != NULL || padlen > 0) {
				/* Get a writable copy. */
				m = m_dup(*m_head, M_NOWAIT);
				/* Release original mbuf chains. */
				m_freem(*m_head);
				if (m == NULL) {
					*m_head = NULL;
					return (NULL);
				}
				*m_head = m;
			}
		}

		if (m->m_next != NULL) {
			m = m_defrag(*m_head, M_NOWAIT);
			if (m == NULL) {
				m_freem(*m_head);
				*m_head = NULL;
				return (NULL);
			}
			*m_head = m;
		}

		if (padlen > 0) {
			if (M_TRAILINGSPACE(m) < padlen) {
				m = m_defrag(*m_head, M_NOWAIT);
				if (m == NULL) {
					m_freem(*m_head);
					*m_head = NULL;
					return (NULL);
				}
				*m_head = m;
			}
			/* Zero out the bytes in the pad area. */
			bzero(mtod(m, char *) + m->m_pkthdr.len, padlen);
			m->m_pkthdr.len += padlen;
			m->m_len = m->m_pkthdr.len;
		}
	}

	error = bus_dmamap_load_mbuf_sg(sc->vte_cdata.vte_tx_tag,
	    txd->tx_dmamap, m, txsegs, &nsegs, 0);
	if (error != 0) {
		txd->tx_flags &= ~VTE_TXMBUF;
		return (NULL);
	}
	KASSERT(nsegs == 1, ("%s: %d segments returned!", __func__, nsegs));
	bus_dmamap_sync(sc->vte_cdata.vte_tx_tag, txd->tx_dmamap,
	    BUS_DMASYNC_PREWRITE);

	txd->tx_desc->dtlen = htole16(VTE_TX_LEN(txsegs[0].ds_len));
	txd->tx_desc->dtbp = htole32(txsegs[0].ds_addr);
	sc->vte_cdata.vte_tx_cnt++;
	/* Update producer index. */
	VTE_DESC_INC(sc->vte_cdata.vte_tx_prod, VTE_TX_RING_CNT);

	/* Finally hand over ownership to controller. */
	txd->tx_desc->dtst = htole16(VTE_DTST_TX_OWN);
	txd->tx_m = m;

	return (txd);
}

static void
vte_start(struct ifnet *ifp)
{
	struct vte_softc *sc;

	sc = ifp->if_softc;
	VTE_LOCK(sc);
	vte_start_locked(sc);
	VTE_UNLOCK(sc);
}

static void
vte_start_locked(struct vte_softc *sc)
{
	struct ifnet *ifp;
	struct vte_txdesc *txd;
	struct mbuf *m_head;
	int enq;

	ifp = sc->vte_ifp;

	if ((ifp->if_drv_flags & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) !=
	    IFF_DRV_RUNNING || (sc->vte_flags & VTE_FLAG_LINK) == 0)
		return;

	for (enq = 0; !IFQ_DRV_IS_EMPTY(&ifp->if_snd); ) {
		/* Reserve one free TX descriptor. */
		if (sc->vte_cdata.vte_tx_cnt >= VTE_TX_RING_CNT - 1) {
			ifp->if_drv_flags |= IFF_DRV_OACTIVE;
			break;
		}
		IFQ_DRV_DEQUEUE(&ifp->if_snd, m_head);
		if (m_head == NULL)
			break;
		/*
		 * Pack the data into the transmit ring. If we
		 * don't have room, set the OACTIVE flag and wait
		 * for the NIC to drain the ring.
		 */
		if ((txd = vte_encap(sc, &m_head)) == NULL) {
			if (m_head != NULL)
				IFQ_DRV_PREPEND(&ifp->if_snd, m_head);
			break;
		}

		enq++;
		/*
		 * If there's a BPF listener, bounce a copy of this frame
		 * to him.
		 */
		ETHER_BPF_MTAP(ifp, m_head);
		/* Free consumed TX frame. */
		if ((txd->tx_flags & VTE_TXMBUF) != 0)
			m_freem(m_head);
	}

	if (enq > 0) {
		bus_dmamap_sync(sc->vte_cdata.vte_tx_ring_tag,
		    sc->vte_cdata.vte_tx_ring_map, BUS_DMASYNC_PREREAD |
		    BUS_DMASYNC_PREWRITE);
		CSR_WRITE_2(sc, VTE_TX_POLL, TX_POLL_START);
		sc->vte_watchdog_timer = VTE_TX_TIMEOUT;
	}
}

static void
vte_watchdog(struct vte_softc *sc)
{
	struct ifnet *ifp;

	VTE_LOCK_ASSERT(sc);

	if (sc->vte_watchdog_timer == 0 || --sc->vte_watchdog_timer)
		return;

	ifp = sc->vte_ifp;
	if_printf(sc->vte_ifp, "watchdog timeout -- resetting\n");
	if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
	ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
	vte_init_locked(sc);
	if (!IFQ_DRV_IS_EMPTY(&ifp->if_snd))
		vte_start_locked(sc);
}

static int
vte_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct vte_softc *sc;
	struct ifreq *ifr;
	struct mii_data *mii;
	int error;

	sc = ifp->if_softc;
	ifr = (struct ifreq *)data;
	error = 0;
	switch (cmd) {
	case SIOCSIFFLAGS:
		VTE_LOCK(sc);
		if ((ifp->if_flags & IFF_UP) != 0) {
			if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0 &&
			    ((ifp->if_flags ^ sc->vte_if_flags) &
			    (IFF_PROMISC | IFF_ALLMULTI)) != 0)
				vte_rxfilter(sc);
			else
				vte_init_locked(sc);
		} else if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
			vte_stop(sc);
		sc->vte_if_flags = ifp->if_flags;
		VTE_UNLOCK(sc);
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		VTE_LOCK(sc);
		if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
			vte_rxfilter(sc);
		VTE_UNLOCK(sc);
		break;
	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		mii = device_get_softc(sc->vte_miibus);
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, cmd);
		break;
	default:
		error = ether_ioctl(ifp, cmd, data);
		break;
	}

	return (error);
}

static void
vte_mac_config(struct vte_softc *sc)
{
	struct mii_data *mii;
	uint16_t mcr;

	VTE_LOCK_ASSERT(sc);

	mii = device_get_softc(sc->vte_miibus);
	mcr = CSR_READ_2(sc, VTE_MCR0);
	mcr &= ~(MCR0_FC_ENB | MCR0_FULL_DUPLEX);
	if ((IFM_OPTIONS(mii->mii_media_active) & IFM_FDX) != 0) {
		mcr |= MCR0_FULL_DUPLEX;
#ifdef notyet
		if ((IFM_OPTIONS(mii->mii_media_active) & IFM_ETH_TXPAUSE) != 0)
			mcr |= MCR0_FC_ENB;
		/*
		 * The data sheet is not clear whether the controller
		 * honors received pause frames or not.  The is no
		 * separate control bit for RX pause frame so just
		 * enable MCR0_FC_ENB bit.
		 */
		if ((IFM_OPTIONS(mii->mii_media_active) & IFM_ETH_RXPAUSE) != 0)
			mcr |= MCR0_FC_ENB;
#endif
	}
	CSR_WRITE_2(sc, VTE_MCR0, mcr);
}

static void
vte_stats_clear(struct vte_softc *sc)
{

	/* Reading counter registers clears its contents. */
	CSR_READ_2(sc, VTE_CNT_RX_DONE);
	CSR_READ_2(sc, VTE_CNT_MECNT0);
	CSR_READ_2(sc, VTE_CNT_MECNT1);
	CSR_READ_2(sc, VTE_CNT_MECNT2);
	CSR_READ_2(sc, VTE_CNT_MECNT3);
	CSR_READ_2(sc, VTE_CNT_TX_DONE);
	CSR_READ_2(sc, VTE_CNT_MECNT4);
	CSR_READ_2(sc, VTE_CNT_PAUSE);
}

static void
vte_stats_update(struct vte_softc *sc)
{
	struct vte_hw_stats *stat;
	uint16_t value;

	VTE_LOCK_ASSERT(sc);

	stat = &sc->vte_stats;

	CSR_READ_2(sc, VTE_MECISR);
	/* RX stats. */
	stat->rx_frames += CSR_READ_2(sc, VTE_CNT_RX_DONE);
	value = CSR_READ_2(sc, VTE_CNT_MECNT0);
	stat->rx_bcast_frames += (value >> 8);
	stat->rx_mcast_frames += (value & 0xFF);
	value = CSR_READ_2(sc, VTE_CNT_MECNT1);
	stat->rx_runts += (value >> 8);
	stat->rx_crcerrs += (value & 0xFF);
	value = CSR_READ_2(sc, VTE_CNT_MECNT2);
	stat->rx_long_frames += (value & 0xFF);
	value = CSR_READ_2(sc, VTE_CNT_MECNT3);
	stat->rx_fifo_full += (value >> 8);
	stat->rx_desc_unavail += (value & 0xFF);

	/* TX stats. */
	stat->tx_frames += CSR_READ_2(sc, VTE_CNT_TX_DONE);
	value = CSR_READ_2(sc, VTE_CNT_MECNT4);
	stat->tx_underruns += (value >> 8);
	stat->tx_late_colls += (value & 0xFF);

	value = CSR_READ_2(sc, VTE_CNT_PAUSE);
	stat->tx_pause_frames += (value >> 8);
	stat->rx_pause_frames += (value & 0xFF);
}

static uint64_t
vte_get_counter(struct ifnet *ifp, ift_counter cnt)
{
	struct vte_softc *sc;
	struct vte_hw_stats *stat;

	sc = if_getsoftc(ifp);
	stat = &sc->vte_stats;

	switch (cnt) {
	case IFCOUNTER_OPACKETS:
		return (stat->tx_frames);
	case IFCOUNTER_COLLISIONS:
		return (stat->tx_late_colls);
	case IFCOUNTER_OERRORS:
		return (stat->tx_late_colls + stat->tx_underruns);
	case IFCOUNTER_IPACKETS:
		return (stat->rx_frames);
	case IFCOUNTER_IERRORS:
		return (stat->rx_crcerrs + stat->rx_runts +
		    stat->rx_long_frames + stat->rx_fifo_full);
	default:
		return (if_get_counter_default(ifp, cnt));
	}
}

static void
vte_intr(void *arg)
{
	struct vte_softc *sc;
	struct ifnet *ifp;
	uint16_t status;
	int n;

	sc = (struct vte_softc *)arg;
	VTE_LOCK(sc);

	ifp = sc->vte_ifp;
	/* Reading VTE_MISR acknowledges interrupts. */
	status = CSR_READ_2(sc, VTE_MISR);
	if ((status & VTE_INTRS) == 0) {
		/* Not ours. */
		VTE_UNLOCK(sc);
		return;
	}

	/* Disable interrupts. */
	CSR_WRITE_2(sc, VTE_MIER, 0);
	for (n = 8; (status & VTE_INTRS) != 0;) {
		if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0)
			break;
		if ((status & (MISR_RX_DONE | MISR_RX_DESC_UNAVAIL |
		    MISR_RX_FIFO_FULL)) != 0)
			vte_rxeof(sc);
		if ((status & MISR_TX_DONE) != 0)
			vte_txeof(sc);
		if ((status & MISR_EVENT_CNT_OFLOW) != 0)
			vte_stats_update(sc);
		if (!IFQ_DRV_IS_EMPTY(&ifp->if_snd))
			vte_start_locked(sc);
		if (--n > 0)
			status = CSR_READ_2(sc, VTE_MISR);
		else
			break;
	}

	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0) {
		/* Re-enable interrupts. */
		CSR_WRITE_2(sc, VTE_MIER, VTE_INTRS);
	}
	VTE_UNLOCK(sc);
}

static void
vte_txeof(struct vte_softc *sc)
{
	struct ifnet *ifp;
	struct vte_txdesc *txd;
	uint16_t status;
	int cons, prog;

	VTE_LOCK_ASSERT(sc);

	ifp = sc->vte_ifp;

	if (sc->vte_cdata.vte_tx_cnt == 0)
		return;
	bus_dmamap_sync(sc->vte_cdata.vte_tx_ring_tag,
	    sc->vte_cdata.vte_tx_ring_map, BUS_DMASYNC_POSTREAD |
	    BUS_DMASYNC_POSTWRITE);
	cons = sc->vte_cdata.vte_tx_cons;
	/*
	 * Go through our TX list and free mbufs for those
	 * frames which have been transmitted.
	 */
	for (prog = 0; sc->vte_cdata.vte_tx_cnt > 0; prog++) {
		txd = &sc->vte_cdata.vte_txdesc[cons];
		status = le16toh(txd->tx_desc->dtst);
		if ((status & VTE_DTST_TX_OWN) != 0)
			break;
		sc->vte_cdata.vte_tx_cnt--;
		/* Reclaim transmitted mbufs. */
		bus_dmamap_sync(sc->vte_cdata.vte_tx_tag, txd->tx_dmamap,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->vte_cdata.vte_tx_tag, txd->tx_dmamap);
		if ((txd->tx_flags & VTE_TXMBUF) == 0)
			m_freem(txd->tx_m);
		txd->tx_flags &= ~VTE_TXMBUF;
		txd->tx_m = NULL;
		prog++;
		VTE_DESC_INC(cons, VTE_TX_RING_CNT);
	}

	if (prog > 0) {
		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
		sc->vte_cdata.vte_tx_cons = cons;
		/*
		 * Unarm watchdog timer only when there is no pending
		 * frames in TX queue.
		 */
		if (sc->vte_cdata.vte_tx_cnt == 0)
			sc->vte_watchdog_timer = 0;
	}
}

static int
vte_newbuf(struct vte_softc *sc, struct vte_rxdesc *rxd)
{
	struct mbuf *m;
	bus_dma_segment_t segs[1];
	bus_dmamap_t map;
	int nsegs;

	m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
	if (m == NULL)
		return (ENOBUFS);
	m->m_len = m->m_pkthdr.len = MCLBYTES;
	m_adj(m, sizeof(uint32_t));

	if (bus_dmamap_load_mbuf_sg(sc->vte_cdata.vte_rx_tag,
	    sc->vte_cdata.vte_rx_sparemap, m, segs, &nsegs, 0) != 0) {
		m_freem(m);
		return (ENOBUFS);
	}
	KASSERT(nsegs == 1, ("%s: %d segments returned!", __func__, nsegs));

	if (rxd->rx_m != NULL) {
		bus_dmamap_sync(sc->vte_cdata.vte_rx_tag, rxd->rx_dmamap,
		    BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->vte_cdata.vte_rx_tag, rxd->rx_dmamap);
	}
	map = rxd->rx_dmamap;
	rxd->rx_dmamap = sc->vte_cdata.vte_rx_sparemap;
	sc->vte_cdata.vte_rx_sparemap = map;
	bus_dmamap_sync(sc->vte_cdata.vte_rx_tag, rxd->rx_dmamap,
	    BUS_DMASYNC_PREREAD);
	rxd->rx_m = m;
	rxd->rx_desc->drbp = htole32(segs[0].ds_addr);
	rxd->rx_desc->drlen = htole16(VTE_RX_LEN(segs[0].ds_len));
	rxd->rx_desc->drst = htole16(VTE_DRST_RX_OWN);

	return (0);
}

/*
 * It's not supposed to see this controller on strict-alignment
 * architectures but make it work for completeness.
 */
#ifndef __NO_STRICT_ALIGNMENT
static struct mbuf *
vte_fixup_rx(struct ifnet *ifp, struct mbuf *m)
{
        uint16_t *src, *dst;
        int i;

	src = mtod(m, uint16_t *);
	dst = src - 1;

	for (i = 0; i < (m->m_len / sizeof(uint16_t) + 1); i++)
		*dst++ = *src++;
	m->m_data -= ETHER_ALIGN;
	return (m);
}
#endif

static void
vte_rxeof(struct vte_softc *sc)
{
	struct ifnet *ifp;
	struct vte_rxdesc *rxd;
	struct mbuf *m;
	uint16_t status, total_len;
	int cons, prog;

	bus_dmamap_sync(sc->vte_cdata.vte_rx_ring_tag,
	    sc->vte_cdata.vte_rx_ring_map, BUS_DMASYNC_POSTREAD |
	    BUS_DMASYNC_POSTWRITE);
	cons = sc->vte_cdata.vte_rx_cons;
	ifp = sc->vte_ifp;
	for (prog = 0; (ifp->if_drv_flags & IFF_DRV_RUNNING) != 0; prog++,
	    VTE_DESC_INC(cons, VTE_RX_RING_CNT)) {
		rxd = &sc->vte_cdata.vte_rxdesc[cons];
		status = le16toh(rxd->rx_desc->drst);
		if ((status & VTE_DRST_RX_OWN) != 0)
			break;
		total_len = VTE_RX_LEN(le16toh(rxd->rx_desc->drlen));
		m = rxd->rx_m;
		if ((status & VTE_DRST_RX_OK) == 0) {
			/* Discard errored frame. */
			rxd->rx_desc->drlen =
			    htole16(MCLBYTES - sizeof(uint32_t));
			rxd->rx_desc->drst = htole16(VTE_DRST_RX_OWN);
			continue;
		}
		if (vte_newbuf(sc, rxd) != 0) {
			if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
			rxd->rx_desc->drlen =
			    htole16(MCLBYTES - sizeof(uint32_t));
			rxd->rx_desc->drst = htole16(VTE_DRST_RX_OWN);
			continue;
		}

		/*
		 * It seems there is no way to strip FCS bytes.
		 */
		m->m_pkthdr.len = m->m_len = total_len - ETHER_CRC_LEN;
		m->m_pkthdr.rcvif = ifp;
#ifndef __NO_STRICT_ALIGNMENT
		vte_fixup_rx(ifp, m);
#endif
		VTE_UNLOCK(sc);
		(*ifp->if_input)(ifp, m);
		VTE_LOCK(sc);
	}

	if (prog > 0) {
		/* Update the consumer index. */
		sc->vte_cdata.vte_rx_cons = cons;
		/*
		 * Sync updated RX descriptors such that controller see
		 * modified RX buffer addresses.
		 */
		bus_dmamap_sync(sc->vte_cdata.vte_rx_ring_tag,
		    sc->vte_cdata.vte_rx_ring_map,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
#ifdef notyet
		/*
		 * Update residue counter.  Controller does not
		 * keep track of number of available RX descriptors
		 * such that driver should have to update VTE_MRDCR
		 * to make controller know how many free RX
		 * descriptors were added to controller.  This is
		 * a similar mechanism used in VIA velocity
		 * controllers and it indicates controller just
		 * polls OWN bit of current RX descriptor pointer.
		 * A couple of severe issues were seen on sample
		 * board where the controller continuously emits TX
		 * pause frames once RX pause threshold crossed.
		 * Once triggered it never recovered form that
		 * state, I couldn't find a way to make it back to
		 * work at least.  This issue effectively
		 * disconnected the system from network.  Also, the
		 * controller used 00:00:00:00:00:00 as source
		 * station address of TX pause frame. Probably this
		 * is one of reason why vendor recommends not to
		 * enable flow control on R6040 controller.
		 */
		CSR_WRITE_2(sc, VTE_MRDCR, prog |
		    (((VTE_RX_RING_CNT * 2) / 10) <<
		    VTE_MRDCR_RX_PAUSE_THRESH_SHIFT));
#endif
	}
}

static void
vte_tick(void *arg)
{
	struct vte_softc *sc;
	struct mii_data *mii;

	sc = (struct vte_softc *)arg;

	VTE_LOCK_ASSERT(sc);

	mii = device_get_softc(sc->vte_miibus);
	mii_tick(mii);
	vte_stats_update(sc);
	vte_txeof(sc);
	vte_watchdog(sc);
	callout_reset(&sc->vte_tick_ch, hz, vte_tick, sc);
}

static void
vte_reset(struct vte_softc *sc)
{
	uint16_t mcr;
	int i;

	mcr = CSR_READ_2(sc, VTE_MCR1);
	CSR_WRITE_2(sc, VTE_MCR1, mcr | MCR1_MAC_RESET);
	for (i = VTE_RESET_TIMEOUT; i > 0; i--) {
		DELAY(10);
		if ((CSR_READ_2(sc, VTE_MCR1) & MCR1_MAC_RESET) == 0)
			break;
	}
	if (i == 0)
		device_printf(sc->vte_dev, "reset timeout(0x%04x)!\n", mcr);
	/*
	 * Follow the guide of vendor recommended way to reset MAC.
	 * Vendor confirms relying on MCR1_MAC_RESET of VTE_MCR1 is
	 * not reliable so manually reset internal state machine.
	 */
	CSR_WRITE_2(sc, VTE_MACSM, 0x0002);
	CSR_WRITE_2(sc, VTE_MACSM, 0);
	DELAY(5000);
}

static void
vte_init(void *xsc)
{
	struct vte_softc *sc;

	sc = (struct vte_softc *)xsc;
	VTE_LOCK(sc);
	vte_init_locked(sc);
	VTE_UNLOCK(sc);
}

static void
vte_init_locked(struct vte_softc *sc)
{
	struct ifnet *ifp;
	bus_addr_t paddr;
	uint8_t *eaddr;

	VTE_LOCK_ASSERT(sc);

	ifp = sc->vte_ifp;

	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
		return;
	/*
	 * Cancel any pending I/O.
	 */
	vte_stop(sc);
	/*
	 * Reset the chip to a known state.
	 */
	vte_reset(sc);

	/* Initialize RX descriptors. */
	if (vte_init_rx_ring(sc) != 0) {
		device_printf(sc->vte_dev, "no memory for RX buffers.\n");
		vte_stop(sc);
		return;
	}
	if (vte_init_tx_ring(sc) != 0) {
		device_printf(sc->vte_dev, "no memory for TX buffers.\n");
		vte_stop(sc);
		return;
	}

	/*
	 * Reprogram the station address.  Controller supports up
	 * to 4 different station addresses so driver programs the
	 * first station address as its own ethernet address and
	 * configure the remaining three addresses as perfect
	 * multicast addresses.
	 */
	eaddr = IF_LLADDR(sc->vte_ifp);
	CSR_WRITE_2(sc, VTE_MID0L, eaddr[1] << 8 | eaddr[0]);
	CSR_WRITE_2(sc, VTE_MID0M, eaddr[3] << 8 | eaddr[2]);
	CSR_WRITE_2(sc, VTE_MID0H, eaddr[5] << 8 | eaddr[4]);

	/* Set TX descriptor base addresses. */
	paddr = sc->vte_cdata.vte_tx_ring_paddr;
	CSR_WRITE_2(sc, VTE_MTDSA1, paddr >> 16);
	CSR_WRITE_2(sc, VTE_MTDSA0, paddr & 0xFFFF);
	/* Set RX descriptor base addresses. */
	paddr = sc->vte_cdata.vte_rx_ring_paddr;
	CSR_WRITE_2(sc, VTE_MRDSA1, paddr >> 16);
	CSR_WRITE_2(sc, VTE_MRDSA0, paddr & 0xFFFF);
	/*
	 * Initialize RX descriptor residue counter and set RX
	 * pause threshold to 20% of available RX descriptors.
	 * See comments on vte_rxeof() for details on flow control
	 * issues.
	 */
	CSR_WRITE_2(sc, VTE_MRDCR, (VTE_RX_RING_CNT & VTE_MRDCR_RESIDUE_MASK) |
	    (((VTE_RX_RING_CNT * 2) / 10) << VTE_MRDCR_RX_PAUSE_THRESH_SHIFT));

	/*
	 * Always use maximum frame size that controller can
	 * support.  Otherwise received frames that has longer
	 * frame length than vte(4) MTU would be silently dropped
	 * in controller.  This would break path-MTU discovery as
	 * sender wouldn't get any responses from receiver. The
	 * RX buffer size should be multiple of 4.
	 * Note, jumbo frames are silently ignored by controller
	 * and even MAC counters do not detect them.
	 */
	CSR_WRITE_2(sc, VTE_MRBSR, VTE_RX_BUF_SIZE_MAX);

	/* Configure FIFO. */
	CSR_WRITE_2(sc, VTE_MBCR, MBCR_FIFO_XFER_LENGTH_16 |
	    MBCR_TX_FIFO_THRESH_64 | MBCR_RX_FIFO_THRESH_16 |
	    MBCR_SDRAM_BUS_REQ_TIMER_DEFAULT);

	/*
	 * Configure TX/RX MACs.  Actual resolved duplex and flow
	 * control configuration is done after detecting a valid
	 * link.  Note, we don't generate early interrupt here
	 * as well since FreeBSD does not have interrupt latency
	 * problems like Windows.
	 */
	CSR_WRITE_2(sc, VTE_MCR0, MCR0_ACCPT_LONG_PKT);
	/*
	 * We manually keep track of PHY status changes to
	 * configure resolved duplex and flow control since only
	 * duplex configuration can be automatically reflected to
	 * MCR0.
	 */
	CSR_WRITE_2(sc, VTE_MCR1, MCR1_PKT_LENGTH_1537 |
	    MCR1_EXCESS_COL_RETRY_16);

	/* Initialize RX filter. */
	vte_rxfilter(sc);

	/* Disable TX/RX interrupt moderation control. */
	CSR_WRITE_2(sc, VTE_MRICR, 0);
	CSR_WRITE_2(sc, VTE_MTICR, 0);

	/* Enable MAC event counter interrupts. */
	CSR_WRITE_2(sc, VTE_MECIER, VTE_MECIER_INTRS);
	/* Clear MAC statistics. */
	vte_stats_clear(sc);

	/* Acknowledge all pending interrupts and clear it. */
	CSR_WRITE_2(sc, VTE_MIER, VTE_INTRS);
	CSR_WRITE_2(sc, VTE_MISR, 0);

	sc->vte_flags &= ~VTE_FLAG_LINK;
	/* Switch to the current media. */
	vte_mediachange_locked(ifp);

	callout_reset(&sc->vte_tick_ch, hz, vte_tick, sc);

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
}

static void
vte_stop(struct vte_softc *sc)
{
	struct ifnet *ifp;
	struct vte_txdesc *txd;
	struct vte_rxdesc *rxd;
	int i;

	VTE_LOCK_ASSERT(sc);
	/*
	 * Mark the interface down and cancel the watchdog timer.
	 */
	ifp = sc->vte_ifp;
	ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);
	sc->vte_flags &= ~VTE_FLAG_LINK;
	callout_stop(&sc->vte_tick_ch);
	sc->vte_watchdog_timer = 0;
	vte_stats_update(sc);
	/* Disable interrupts. */
	CSR_WRITE_2(sc, VTE_MIER, 0);
	CSR_WRITE_2(sc, VTE_MECIER, 0);
	/* Stop RX/TX MACs. */
	vte_stop_mac(sc);
	/* Clear interrupts. */
	CSR_READ_2(sc, VTE_MISR);
	/*
	 * Free TX/RX mbufs still in the queues.
	 */
	for (i = 0; i < VTE_RX_RING_CNT; i++) {
		rxd = &sc->vte_cdata.vte_rxdesc[i];
		if (rxd->rx_m != NULL) {
			bus_dmamap_sync(sc->vte_cdata.vte_rx_tag,
			    rxd->rx_dmamap, BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(sc->vte_cdata.vte_rx_tag,
			    rxd->rx_dmamap);
			m_freem(rxd->rx_m);
			rxd->rx_m = NULL;
		}
	}
	for (i = 0; i < VTE_TX_RING_CNT; i++) {
		txd = &sc->vte_cdata.vte_txdesc[i];
		if (txd->tx_m != NULL) {
			bus_dmamap_sync(sc->vte_cdata.vte_tx_tag,
			    txd->tx_dmamap, BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->vte_cdata.vte_tx_tag,
			    txd->tx_dmamap);
			if ((txd->tx_flags & VTE_TXMBUF) == 0)
				m_freem(txd->tx_m);
			txd->tx_m = NULL;
			txd->tx_flags &= ~VTE_TXMBUF;
		}
	}
	/* Free TX mbuf pools used for deep copy. */
	for (i = 0; i < VTE_TX_RING_CNT; i++) {
		if (sc->vte_cdata.vte_txmbufs[i] != NULL) {
			m_freem(sc->vte_cdata.vte_txmbufs[i]);
			sc->vte_cdata.vte_txmbufs[i] = NULL;
		}
	}
}

static void
vte_start_mac(struct vte_softc *sc)
{
	uint16_t mcr;
	int i;

	VTE_LOCK_ASSERT(sc);

	/* Enable RX/TX MACs. */
	mcr = CSR_READ_2(sc, VTE_MCR0);
	if ((mcr & (MCR0_RX_ENB | MCR0_TX_ENB)) !=
	    (MCR0_RX_ENB | MCR0_TX_ENB)) {
		mcr |= MCR0_RX_ENB | MCR0_TX_ENB;
		CSR_WRITE_2(sc, VTE_MCR0, mcr);
		for (i = VTE_TIMEOUT; i > 0; i--) {
			mcr = CSR_READ_2(sc, VTE_MCR0);
			if ((mcr & (MCR0_RX_ENB | MCR0_TX_ENB)) ==
			    (MCR0_RX_ENB | MCR0_TX_ENB))
				break;
			DELAY(10);
		}
		if (i == 0)
			device_printf(sc->vte_dev,
			    "could not enable RX/TX MAC(0x%04x)!\n", mcr);
	}
}

static void
vte_stop_mac(struct vte_softc *sc)
{
	uint16_t mcr;
	int i;

	VTE_LOCK_ASSERT(sc);

	/* Disable RX/TX MACs. */
	mcr = CSR_READ_2(sc, VTE_MCR0);
	if ((mcr & (MCR0_RX_ENB | MCR0_TX_ENB)) != 0) {
		mcr &= ~(MCR0_RX_ENB | MCR0_TX_ENB);
		CSR_WRITE_2(sc, VTE_MCR0, mcr);
		for (i = VTE_TIMEOUT; i > 0; i--) {
			mcr = CSR_READ_2(sc, VTE_MCR0);
			if ((mcr & (MCR0_RX_ENB | MCR0_TX_ENB)) == 0)
				break;
			DELAY(10);
		}
		if (i == 0)
			device_printf(sc->vte_dev,
			    "could not disable RX/TX MAC(0x%04x)!\n", mcr);
	}
}

static int
vte_init_tx_ring(struct vte_softc *sc)
{
	struct vte_tx_desc *desc;
	struct vte_txdesc *txd;
	bus_addr_t addr;
	int i;

	VTE_LOCK_ASSERT(sc);

	sc->vte_cdata.vte_tx_prod = 0;
	sc->vte_cdata.vte_tx_cons = 0;
	sc->vte_cdata.vte_tx_cnt = 0;

	/* Pre-allocate TX mbufs for deep copy. */
	if (tx_deep_copy != 0) {
		for (i = 0; i < VTE_TX_RING_CNT; i++) {
			sc->vte_cdata.vte_txmbufs[i] = m_getcl(M_NOWAIT,
			    MT_DATA, M_PKTHDR);
			if (sc->vte_cdata.vte_txmbufs[i] == NULL)
				return (ENOBUFS);
			sc->vte_cdata.vte_txmbufs[i]->m_pkthdr.len = MCLBYTES;
			sc->vte_cdata.vte_txmbufs[i]->m_len = MCLBYTES;
		}
	}
	desc = sc->vte_cdata.vte_tx_ring;
	bzero(desc, VTE_TX_RING_SZ);
	for (i = 0; i < VTE_TX_RING_CNT; i++) {
		txd = &sc->vte_cdata.vte_txdesc[i];
		txd->tx_m = NULL;
		if (i != VTE_TX_RING_CNT - 1)
			addr = sc->vte_cdata.vte_tx_ring_paddr +
			    sizeof(struct vte_tx_desc) * (i + 1);
		else
			addr = sc->vte_cdata.vte_tx_ring_paddr +
			    sizeof(struct vte_tx_desc) * 0;
		desc = &sc->vte_cdata.vte_tx_ring[i];
		desc->dtnp = htole32(addr);
		txd->tx_desc = desc;
	}

	bus_dmamap_sync(sc->vte_cdata.vte_tx_ring_tag,
	    sc->vte_cdata.vte_tx_ring_map, BUS_DMASYNC_PREREAD |
	    BUS_DMASYNC_PREWRITE);
	return (0);
}

static int
vte_init_rx_ring(struct vte_softc *sc)
{
	struct vte_rx_desc *desc;
	struct vte_rxdesc *rxd;
	bus_addr_t addr;
	int i;

	VTE_LOCK_ASSERT(sc);

	sc->vte_cdata.vte_rx_cons = 0;
	desc = sc->vte_cdata.vte_rx_ring;
	bzero(desc, VTE_RX_RING_SZ);
	for (i = 0; i < VTE_RX_RING_CNT; i++) {
		rxd = &sc->vte_cdata.vte_rxdesc[i];
		rxd->rx_m = NULL;
		if (i != VTE_RX_RING_CNT - 1)
			addr = sc->vte_cdata.vte_rx_ring_paddr +
			    sizeof(struct vte_rx_desc) * (i + 1);
		else
			addr = sc->vte_cdata.vte_rx_ring_paddr +
			    sizeof(struct vte_rx_desc) * 0;
		desc = &sc->vte_cdata.vte_rx_ring[i];
		desc->drnp = htole32(addr);
		rxd->rx_desc = desc;
		if (vte_newbuf(sc, rxd) != 0)
			return (ENOBUFS);
	}

	bus_dmamap_sync(sc->vte_cdata.vte_rx_ring_tag,
	    sc->vte_cdata.vte_rx_ring_map, BUS_DMASYNC_PREREAD |
	    BUS_DMASYNC_PREWRITE);

	return (0);
}

static void
vte_rxfilter(struct vte_softc *sc)
{
	struct ifnet *ifp;
	struct ifmultiaddr *ifma;
	uint8_t *eaddr;
	uint32_t crc;
	uint16_t rxfilt_perf[VTE_RXFILT_PERFECT_CNT][3];
	uint16_t mchash[4], mcr;
	int i, nperf;

	VTE_LOCK_ASSERT(sc);

	ifp = sc->vte_ifp;

	bzero(mchash, sizeof(mchash));
	for (i = 0; i < VTE_RXFILT_PERFECT_CNT; i++) {
		rxfilt_perf[i][0] = 0xFFFF;
		rxfilt_perf[i][1] = 0xFFFF;
		rxfilt_perf[i][2] = 0xFFFF;
	}

	mcr = CSR_READ_2(sc, VTE_MCR0);
	mcr &= ~(MCR0_PROMISC | MCR0_MULTICAST);
	mcr |= MCR0_BROADCAST_DIS;
	if ((ifp->if_flags & IFF_BROADCAST) != 0)
		mcr &= ~MCR0_BROADCAST_DIS;
	if ((ifp->if_flags & (IFF_PROMISC | IFF_ALLMULTI)) != 0) {
		if ((ifp->if_flags & IFF_PROMISC) != 0)
			mcr |= MCR0_PROMISC;
		if ((ifp->if_flags & IFF_ALLMULTI) != 0)
			mcr |= MCR0_MULTICAST;
		mchash[0] = 0xFFFF;
		mchash[1] = 0xFFFF;
		mchash[2] = 0xFFFF;
		mchash[3] = 0xFFFF;
		goto chipit;
	}

	nperf = 0;
	if_maddr_rlock(ifp);
	CK_STAILQ_FOREACH(ifma, &sc->vte_ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		/*
		 * Program the first 3 multicast groups into
		 * the perfect filter.  For all others, use the
		 * hash table.
		 */
		if (nperf < VTE_RXFILT_PERFECT_CNT) {
			eaddr = LLADDR((struct sockaddr_dl *)ifma->ifma_addr);
			rxfilt_perf[nperf][0] = eaddr[1] << 8 | eaddr[0];
			rxfilt_perf[nperf][1] = eaddr[3] << 8 | eaddr[2];
			rxfilt_perf[nperf][2] = eaddr[5] << 8 | eaddr[4];
			nperf++;
			continue;
		}
		crc = ether_crc32_be(LLADDR((struct sockaddr_dl *)
		    ifma->ifma_addr), ETHER_ADDR_LEN);
		mchash[crc >> 30] |= 1 << ((crc >> 26) & 0x0F);
	}
	if_maddr_runlock(ifp);
	if (mchash[0] != 0 || mchash[1] != 0 || mchash[2] != 0 ||
	    mchash[3] != 0)
		mcr |= MCR0_MULTICAST;

chipit:
	/* Program multicast hash table. */
	CSR_WRITE_2(sc, VTE_MAR0, mchash[0]);
	CSR_WRITE_2(sc, VTE_MAR1, mchash[1]);
	CSR_WRITE_2(sc, VTE_MAR2, mchash[2]);
	CSR_WRITE_2(sc, VTE_MAR3, mchash[3]);
	/* Program perfect filter table. */
	for (i = 0; i < VTE_RXFILT_PERFECT_CNT; i++) {
		CSR_WRITE_2(sc, VTE_RXFILTER_PEEFECT_BASE + 8 * i + 0,
		    rxfilt_perf[i][0]);
		CSR_WRITE_2(sc, VTE_RXFILTER_PEEFECT_BASE + 8 * i + 2,
		    rxfilt_perf[i][1]);
		CSR_WRITE_2(sc, VTE_RXFILTER_PEEFECT_BASE + 8 * i + 4,
		    rxfilt_perf[i][2]);
	}
	CSR_WRITE_2(sc, VTE_MCR0, mcr);
	CSR_READ_2(sc, VTE_MCR0);
}

static int
sysctl_int_range(SYSCTL_HANDLER_ARGS, int low, int high)
{
	int error, value;

	if (arg1 == NULL)
		return (EINVAL);
	value = *(int *)arg1;
	error = sysctl_handle_int(oidp, &value, 0, req);
	if (error || req->newptr == NULL)
		return (error);
	if (value < low || value > high)
		return (EINVAL);
	*(int *)arg1 = value;

	return (0);
}

static int
sysctl_hw_vte_int_mod(SYSCTL_HANDLER_ARGS)
{

	return (sysctl_int_range(oidp, arg1, arg2, req,
	    VTE_IM_BUNDLE_MIN, VTE_IM_BUNDLE_MAX));
}
