/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2007 Sepherosa Ziehau.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Sepherosa Ziehau <sepherosa@gmail.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $DragonFly: src/sys/dev/netif/et/if_et.c,v 1.10 2008/05/18 07:47:14 sephe Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/proc.h>
#include <sys/rman.h>
#include <sys/module.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/bpf.h>
#include <net/if_arp.h>
#include <net/if_media.h>
#include <net/if_vlan_var.h>

#include <machine/bus.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/et/if_etreg.h>
#include <dev/et/if_etvar.h>

#include "miibus_if.h"

MODULE_DEPEND(et, pci, 1, 1, 1);
MODULE_DEPEND(et, ether, 1, 1, 1);
MODULE_DEPEND(et, miibus, 1, 1, 1);

/* Tunables. */
static int msi_disable = 0;
TUNABLE_INT("hw.et.msi_disable", &msi_disable);

#define	ET_CSUM_FEATURES	(CSUM_IP | CSUM_TCP | CSUM_UDP)

static int	et_probe(device_t);
static int	et_attach(device_t);
static int	et_detach(device_t);
static int	et_shutdown(device_t);
static int	et_suspend(device_t);
static int	et_resume(device_t);

static int	et_miibus_readreg(device_t, int, int);
static int	et_miibus_writereg(device_t, int, int, int);
static void	et_miibus_statchg(device_t);

static void	et_init_locked(struct et_softc *);
static void	et_init(void *);
static int	et_ioctl(struct ifnet *, u_long, caddr_t);
static void	et_start_locked(struct ifnet *);
static void	et_start(struct ifnet *);
static int	et_watchdog(struct et_softc *);
static int	et_ifmedia_upd_locked(struct ifnet *);
static int	et_ifmedia_upd(struct ifnet *);
static void	et_ifmedia_sts(struct ifnet *, struct ifmediareq *);
static uint64_t	et_get_counter(struct ifnet *, ift_counter);

static void	et_add_sysctls(struct et_softc *);
static int	et_sysctl_rx_intr_npkts(SYSCTL_HANDLER_ARGS);
static int	et_sysctl_rx_intr_delay(SYSCTL_HANDLER_ARGS);

static void	et_intr(void *);
static void	et_rxeof(struct et_softc *);
static void	et_txeof(struct et_softc *);

static int	et_dma_alloc(struct et_softc *);
static void	et_dma_free(struct et_softc *);
static void	et_dma_map_addr(void *, bus_dma_segment_t *, int, int);
static int	et_dma_ring_alloc(struct et_softc *, bus_size_t, bus_size_t,
		    bus_dma_tag_t *, uint8_t **, bus_dmamap_t *, bus_addr_t *,
		    const char *);
static void	et_dma_ring_free(struct et_softc *, bus_dma_tag_t *, uint8_t **,
		    bus_dmamap_t, bus_addr_t *);
static void	et_init_tx_ring(struct et_softc *);
static int	et_init_rx_ring(struct et_softc *);
static void	et_free_tx_ring(struct et_softc *);
static void	et_free_rx_ring(struct et_softc *);
static int	et_encap(struct et_softc *, struct mbuf **);
static int	et_newbuf_cluster(struct et_rxbuf_data *, int);
static int	et_newbuf_hdr(struct et_rxbuf_data *, int);
static void	et_rxbuf_discard(struct et_rxbuf_data *, int);

static void	et_stop(struct et_softc *);
static int	et_chip_init(struct et_softc *);
static void	et_chip_attach(struct et_softc *);
static void	et_init_mac(struct et_softc *);
static void	et_init_rxmac(struct et_softc *);
static void	et_init_txmac(struct et_softc *);
static int	et_init_rxdma(struct et_softc *);
static int	et_init_txdma(struct et_softc *);
static int	et_start_rxdma(struct et_softc *);
static int	et_start_txdma(struct et_softc *);
static int	et_stop_rxdma(struct et_softc *);
static int	et_stop_txdma(struct et_softc *);
static void	et_reset(struct et_softc *);
static int	et_bus_config(struct et_softc *);
static void	et_get_eaddr(device_t, uint8_t[]);
static void	et_setmulti(struct et_softc *);
static void	et_tick(void *);
static void	et_stats_update(struct et_softc *);

static const struct et_dev {
	uint16_t	vid;
	uint16_t	did;
	const char	*desc;
} et_devices[] = {
	{ PCI_VENDOR_LUCENT, PCI_PRODUCT_LUCENT_ET1310,
	  "Agere ET1310 Gigabit Ethernet" },
	{ PCI_VENDOR_LUCENT, PCI_PRODUCT_LUCENT_ET1310_FAST,
	  "Agere ET1310 Fast Ethernet" },
	{ 0, 0, NULL }
};

static device_method_t et_methods[] = {
	DEVMETHOD(device_probe,		et_probe),
	DEVMETHOD(device_attach,	et_attach),
	DEVMETHOD(device_detach,	et_detach),
	DEVMETHOD(device_shutdown,	et_shutdown),
	DEVMETHOD(device_suspend,	et_suspend),
	DEVMETHOD(device_resume,	et_resume),

	DEVMETHOD(miibus_readreg,	et_miibus_readreg),
	DEVMETHOD(miibus_writereg,	et_miibus_writereg),
	DEVMETHOD(miibus_statchg,	et_miibus_statchg),

	DEVMETHOD_END
};

static driver_t et_driver = {
	"et",
	et_methods,
	sizeof(struct et_softc)
};

static devclass_t et_devclass;

DRIVER_MODULE(et, pci, et_driver, et_devclass, 0, 0);
MODULE_PNP_INFO("U16:vendor;U16:device;D:#", pci, et, et_devices,
    nitems(et_devices) - 1);
DRIVER_MODULE(miibus, et, miibus_driver, miibus_devclass, 0, 0);

static int	et_rx_intr_npkts = 32;
static int	et_rx_intr_delay = 20;		/* x10 usec */
static int	et_tx_intr_nsegs = 126;
static uint32_t	et_timer = 1000 * 1000 * 1000;	/* nanosec */

TUNABLE_INT("hw.et.timer", &et_timer);
TUNABLE_INT("hw.et.rx_intr_npkts", &et_rx_intr_npkts);
TUNABLE_INT("hw.et.rx_intr_delay", &et_rx_intr_delay);
TUNABLE_INT("hw.et.tx_intr_nsegs", &et_tx_intr_nsegs);

static int
et_probe(device_t dev)
{
	const struct et_dev *d;
	uint16_t did, vid;

	vid = pci_get_vendor(dev);
	did = pci_get_device(dev);

	for (d = et_devices; d->desc != NULL; ++d) {
		if (vid == d->vid && did == d->did) {
			device_set_desc(dev, d->desc);
			return (BUS_PROBE_DEFAULT);
		}
	}
	return (ENXIO);
}

static int
et_attach(device_t dev)
{
	struct et_softc *sc;
	struct ifnet *ifp;
	uint8_t eaddr[ETHER_ADDR_LEN];
	uint32_t pmcfg;
	int cap, error, msic;

	sc = device_get_softc(dev);
	sc->dev = dev;
	mtx_init(&sc->sc_mtx, device_get_nameunit(dev), MTX_NETWORK_LOCK,
	    MTX_DEF);
	callout_init_mtx(&sc->sc_tick, &sc->sc_mtx, 0);

	ifp = sc->ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(dev, "can not if_alloc()\n");
		error = ENOSPC;
		goto fail;
	}

	/*
	 * Initialize tunables
	 */
	sc->sc_rx_intr_npkts = et_rx_intr_npkts;
	sc->sc_rx_intr_delay = et_rx_intr_delay;
	sc->sc_tx_intr_nsegs = et_tx_intr_nsegs;
	sc->sc_timer = et_timer;

	/* Enable bus mastering */
	pci_enable_busmaster(dev);

	/*
	 * Allocate IO memory
	 */
	sc->sc_mem_rid = PCIR_BAR(0);
	sc->sc_mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->sc_mem_rid, RF_ACTIVE);
	if (sc->sc_mem_res == NULL) {
		device_printf(dev, "can't allocate IO memory\n");
		return (ENXIO);
	}

	msic = 0;
	if (pci_find_cap(dev, PCIY_EXPRESS, &cap) == 0) {
		sc->sc_expcap = cap;
		sc->sc_flags |= ET_FLAG_PCIE;
		msic = pci_msi_count(dev);
		if (bootverbose)
			device_printf(dev, "MSI count: %d\n", msic);
	}
	if (msic > 0 && msi_disable == 0) {
		msic = 1;
		if (pci_alloc_msi(dev, &msic) == 0) {
			if (msic == 1) {
				device_printf(dev, "Using %d MSI message\n",
				    msic);
				sc->sc_flags |= ET_FLAG_MSI;
			} else
				pci_release_msi(dev);
		}
	}

	/*
	 * Allocate IRQ
	 */
	if ((sc->sc_flags & ET_FLAG_MSI) == 0) {
		sc->sc_irq_rid = 0;
		sc->sc_irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ,
		    &sc->sc_irq_rid, RF_SHAREABLE | RF_ACTIVE);
	} else {
		sc->sc_irq_rid = 1;
		sc->sc_irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ,
		    &sc->sc_irq_rid, RF_ACTIVE);
	}
	if (sc->sc_irq_res == NULL) {
		device_printf(dev, "can't allocate irq\n");
		error = ENXIO;
		goto fail;
	}

	if (pci_get_device(dev) == PCI_PRODUCT_LUCENT_ET1310_FAST)
		sc->sc_flags |= ET_FLAG_FASTETHER;

	error = et_bus_config(sc);
	if (error)
		goto fail;

	et_get_eaddr(dev, eaddr);

	/* Take PHY out of COMA and enable clocks. */
	pmcfg = ET_PM_SYSCLK_GATE | ET_PM_TXCLK_GATE | ET_PM_RXCLK_GATE;
	if ((sc->sc_flags & ET_FLAG_FASTETHER) == 0)
		pmcfg |= EM_PM_GIGEPHY_ENB;
	CSR_WRITE_4(sc, ET_PM, pmcfg);

	et_reset(sc);

	error = et_dma_alloc(sc);
	if (error)
		goto fail;

	ifp->if_softc = sc;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_init = et_init;
	ifp->if_ioctl = et_ioctl;
	ifp->if_start = et_start;
	ifp->if_get_counter = et_get_counter;
	ifp->if_capabilities = IFCAP_TXCSUM | IFCAP_VLAN_MTU;
	ifp->if_capenable = ifp->if_capabilities;
	ifp->if_snd.ifq_drv_maxlen = ET_TX_NDESC - 1;
	IFQ_SET_MAXLEN(&ifp->if_snd, ET_TX_NDESC - 1);
	IFQ_SET_READY(&ifp->if_snd);

	et_chip_attach(sc);

	error = mii_attach(dev, &sc->sc_miibus, ifp, et_ifmedia_upd,
	    et_ifmedia_sts, BMSR_DEFCAPMASK, MII_PHY_ANY, MII_OFFSET_ANY,
	    MIIF_DOPAUSE);
	if (error) {
		device_printf(dev, "attaching PHYs failed\n");
		goto fail;
	}

	ether_ifattach(ifp, eaddr);

	/* Tell the upper layer(s) we support long frames. */
	ifp->if_hdrlen = sizeof(struct ether_vlan_header);

	error = bus_setup_intr(dev, sc->sc_irq_res, INTR_TYPE_NET | INTR_MPSAFE,
	    NULL, et_intr, sc, &sc->sc_irq_handle);
	if (error) {
		ether_ifdetach(ifp);
		device_printf(dev, "can't setup intr\n");
		goto fail;
	}

	et_add_sysctls(sc);

	return (0);
fail:
	et_detach(dev);
	return (error);
}

static int
et_detach(device_t dev)
{
	struct et_softc *sc;

	sc = device_get_softc(dev);
	if (device_is_attached(dev)) {
		ether_ifdetach(sc->ifp);
		ET_LOCK(sc);
		et_stop(sc);
		ET_UNLOCK(sc);
		callout_drain(&sc->sc_tick);
	}

	if (sc->sc_miibus != NULL)
		device_delete_child(dev, sc->sc_miibus);
	bus_generic_detach(dev);

	if (sc->sc_irq_handle != NULL)
		bus_teardown_intr(dev, sc->sc_irq_res, sc->sc_irq_handle);
	if (sc->sc_irq_res != NULL)
		bus_release_resource(dev, SYS_RES_IRQ,
		    rman_get_rid(sc->sc_irq_res), sc->sc_irq_res);
	if ((sc->sc_flags & ET_FLAG_MSI) != 0)
		pci_release_msi(dev);
	if (sc->sc_mem_res != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY,
		    rman_get_rid(sc->sc_mem_res), sc->sc_mem_res);

	if (sc->ifp != NULL)
		if_free(sc->ifp);

	et_dma_free(sc);

	mtx_destroy(&sc->sc_mtx);

	return (0);
}

static int
et_shutdown(device_t dev)
{
	struct et_softc *sc;

	sc = device_get_softc(dev);
	ET_LOCK(sc);
	et_stop(sc);
	ET_UNLOCK(sc);
	return (0);
}

static int
et_miibus_readreg(device_t dev, int phy, int reg)
{
	struct et_softc *sc;
	uint32_t val;
	int i, ret;

	sc = device_get_softc(dev);
	/* Stop any pending operations */
	CSR_WRITE_4(sc, ET_MII_CMD, 0);

	val = (phy << ET_MII_ADDR_PHY_SHIFT) & ET_MII_ADDR_PHY_MASK;
	val |= (reg << ET_MII_ADDR_REG_SHIFT) & ET_MII_ADDR_REG_MASK;
	CSR_WRITE_4(sc, ET_MII_ADDR, val);

	/* Start reading */
	CSR_WRITE_4(sc, ET_MII_CMD, ET_MII_CMD_READ);

#define NRETRY	50

	for (i = 0; i < NRETRY; ++i) {
		val = CSR_READ_4(sc, ET_MII_IND);
		if ((val & (ET_MII_IND_BUSY | ET_MII_IND_INVALID)) == 0)
			break;
		DELAY(50);
	}
	if (i == NRETRY) {
		if_printf(sc->ifp,
			  "read phy %d, reg %d timed out\n", phy, reg);
		ret = 0;
		goto back;
	}

#undef NRETRY

	val = CSR_READ_4(sc, ET_MII_STAT);
	ret = val & ET_MII_STAT_VALUE_MASK;

back:
	/* Make sure that the current operation is stopped */
	CSR_WRITE_4(sc, ET_MII_CMD, 0);
	return (ret);
}

static int
et_miibus_writereg(device_t dev, int phy, int reg, int val0)
{
	struct et_softc *sc;
	uint32_t val;
	int i;

	sc = device_get_softc(dev);
	/* Stop any pending operations */
	CSR_WRITE_4(sc, ET_MII_CMD, 0);

	val = (phy << ET_MII_ADDR_PHY_SHIFT) & ET_MII_ADDR_PHY_MASK;
	val |= (reg << ET_MII_ADDR_REG_SHIFT) & ET_MII_ADDR_REG_MASK;
	CSR_WRITE_4(sc, ET_MII_ADDR, val);

	/* Start writing */
	CSR_WRITE_4(sc, ET_MII_CTRL,
	    (val0 << ET_MII_CTRL_VALUE_SHIFT) & ET_MII_CTRL_VALUE_MASK);

#define NRETRY 100

	for (i = 0; i < NRETRY; ++i) {
		val = CSR_READ_4(sc, ET_MII_IND);
		if ((val & ET_MII_IND_BUSY) == 0)
			break;
		DELAY(50);
	}
	if (i == NRETRY) {
		if_printf(sc->ifp,
			  "write phy %d, reg %d timed out\n", phy, reg);
		et_miibus_readreg(dev, phy, reg);
	}

#undef NRETRY

	/* Make sure that the current operation is stopped */
	CSR_WRITE_4(sc, ET_MII_CMD, 0);
	return (0);
}

static void
et_miibus_statchg(device_t dev)
{
	struct et_softc *sc;
	struct mii_data *mii;
	struct ifnet *ifp;
	uint32_t cfg1, cfg2, ctrl;
	int i;

	sc = device_get_softc(dev);

	mii = device_get_softc(sc->sc_miibus);
	ifp = sc->ifp;
	if (mii == NULL || ifp == NULL ||
	    (ifp->if_drv_flags & IFF_DRV_RUNNING) == 0)
		return;

	sc->sc_flags &= ~ET_FLAG_LINK;
	if ((mii->mii_media_status & (IFM_ACTIVE | IFM_AVALID)) ==
	    (IFM_ACTIVE | IFM_AVALID)) {
		switch (IFM_SUBTYPE(mii->mii_media_active)) {
		case IFM_10_T:
		case IFM_100_TX:
			sc->sc_flags |= ET_FLAG_LINK;
			break;
		case IFM_1000_T:
			if ((sc->sc_flags & ET_FLAG_FASTETHER) == 0)
				sc->sc_flags |= ET_FLAG_LINK;
			break;
		}
	}

	/* XXX Stop TX/RX MAC? */
	if ((sc->sc_flags & ET_FLAG_LINK) == 0)
		return;

	/* Program MACs with resolved speed/duplex/flow-control. */
	ctrl = CSR_READ_4(sc, ET_MAC_CTRL);
	ctrl &= ~(ET_MAC_CTRL_GHDX | ET_MAC_CTRL_MODE_MII);
	cfg1 = CSR_READ_4(sc, ET_MAC_CFG1);
	cfg1 &= ~(ET_MAC_CFG1_TXFLOW | ET_MAC_CFG1_RXFLOW |
	    ET_MAC_CFG1_LOOPBACK);
	cfg2 = CSR_READ_4(sc, ET_MAC_CFG2);
	cfg2 &= ~(ET_MAC_CFG2_MODE_MII | ET_MAC_CFG2_MODE_GMII |
	    ET_MAC_CFG2_FDX | ET_MAC_CFG2_BIGFRM);
	cfg2 |= ET_MAC_CFG2_LENCHK | ET_MAC_CFG2_CRC | ET_MAC_CFG2_PADCRC |
	    ((7 << ET_MAC_CFG2_PREAMBLE_LEN_SHIFT) &
	    ET_MAC_CFG2_PREAMBLE_LEN_MASK);

	if (IFM_SUBTYPE(mii->mii_media_active) == IFM_1000_T)
		cfg2 |= ET_MAC_CFG2_MODE_GMII;
	else {
		cfg2 |= ET_MAC_CFG2_MODE_MII;
		ctrl |= ET_MAC_CTRL_MODE_MII;
	}

	if (IFM_OPTIONS(mii->mii_media_active) & IFM_FDX) {
		cfg2 |= ET_MAC_CFG2_FDX;
		/*
		 * Controller lacks automatic TX pause frame
		 * generation so it should be handled by driver.
		 * Even though driver can send pause frame with
		 * arbitrary pause time, controller does not
		 * provide a way that tells how many free RX
		 * buffers are available in controller.  This
		 * limitation makes it hard to generate XON frame
		 * in time on driver side so don't enable TX flow
		 * control.
		 */
#ifdef notyet
		if (IFM_OPTIONS(mii->mii_media_active) & IFM_ETH_TXPAUSE)
			cfg1 |= ET_MAC_CFG1_TXFLOW;
#endif
		if (IFM_OPTIONS(mii->mii_media_active) & IFM_ETH_RXPAUSE)
			cfg1 |= ET_MAC_CFG1_RXFLOW;
	} else
		ctrl |= ET_MAC_CTRL_GHDX;

	CSR_WRITE_4(sc, ET_MAC_CTRL, ctrl);
	CSR_WRITE_4(sc, ET_MAC_CFG2, cfg2);
	cfg1 |= ET_MAC_CFG1_TXEN | ET_MAC_CFG1_RXEN;
	CSR_WRITE_4(sc, ET_MAC_CFG1, cfg1);

#define NRETRY	50

	for (i = 0; i < NRETRY; ++i) {
		cfg1 = CSR_READ_4(sc, ET_MAC_CFG1);
		if ((cfg1 & (ET_MAC_CFG1_SYNC_TXEN | ET_MAC_CFG1_SYNC_RXEN)) ==
		    (ET_MAC_CFG1_SYNC_TXEN | ET_MAC_CFG1_SYNC_RXEN))
			break;
		DELAY(100);
	}
	if (i == NRETRY)
		if_printf(ifp, "can't enable RX/TX\n");
	sc->sc_flags |= ET_FLAG_TXRX_ENABLED;

#undef NRETRY
}

static int
et_ifmedia_upd_locked(struct ifnet *ifp)
{
	struct et_softc *sc;
	struct mii_data *mii;
	struct mii_softc *miisc;

	sc = ifp->if_softc;
	mii = device_get_softc(sc->sc_miibus);
	LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
		PHY_RESET(miisc);
	return (mii_mediachg(mii));
}

static int
et_ifmedia_upd(struct ifnet *ifp)
{
	struct et_softc *sc;
	int res;

	sc = ifp->if_softc;
	ET_LOCK(sc);
	res = et_ifmedia_upd_locked(ifp);
	ET_UNLOCK(sc);

	return (res);
}

static void
et_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct et_softc *sc;
	struct mii_data *mii;

	sc = ifp->if_softc;
	ET_LOCK(sc);
	if ((ifp->if_flags & IFF_UP) == 0) {
		ET_UNLOCK(sc);
		return;
	}

	mii = device_get_softc(sc->sc_miibus);
	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
	ET_UNLOCK(sc);
}

static void
et_stop(struct et_softc *sc)
{
	struct ifnet *ifp;

	ET_LOCK_ASSERT(sc);

	ifp = sc->ifp;
	callout_stop(&sc->sc_tick);
	/* Disable interrupts. */
	CSR_WRITE_4(sc, ET_INTR_MASK, 0xffffffff);

	CSR_WRITE_4(sc, ET_MAC_CFG1, CSR_READ_4(sc, ET_MAC_CFG1) & ~(
	    ET_MAC_CFG1_TXEN | ET_MAC_CFG1_RXEN));
	DELAY(100);

	et_stop_rxdma(sc);
	et_stop_txdma(sc);
	et_stats_update(sc);

	et_free_tx_ring(sc);
	et_free_rx_ring(sc);

	sc->sc_tx = 0;
	sc->sc_tx_intr = 0;
	sc->sc_flags &= ~ET_FLAG_TXRX_ENABLED;

	sc->watchdog_timer = 0;
	ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);
}

static int
et_bus_config(struct et_softc *sc)
{
	uint32_t val, max_plsz;
	uint16_t ack_latency, replay_timer;

	/*
	 * Test whether EEPROM is valid
	 * NOTE: Read twice to get the correct value
	 */
	pci_read_config(sc->dev, ET_PCIR_EEPROM_STATUS, 1);
	val = pci_read_config(sc->dev, ET_PCIR_EEPROM_STATUS, 1);
	if (val & ET_PCIM_EEPROM_STATUS_ERROR) {
		device_printf(sc->dev, "EEPROM status error 0x%02x\n", val);
		return (ENXIO);
	}

	/* TODO: LED */

	if ((sc->sc_flags & ET_FLAG_PCIE) == 0)
		return (0);

	/*
	 * Configure ACK latency and replay timer according to
	 * max playload size
	 */
	val = pci_read_config(sc->dev,
	    sc->sc_expcap + PCIER_DEVICE_CAP, 4);
	max_plsz = val & PCIEM_CAP_MAX_PAYLOAD;

	switch (max_plsz) {
	case ET_PCIV_DEVICE_CAPS_PLSZ_128:
		ack_latency = ET_PCIV_ACK_LATENCY_128;
		replay_timer = ET_PCIV_REPLAY_TIMER_128;
		break;

	case ET_PCIV_DEVICE_CAPS_PLSZ_256:
		ack_latency = ET_PCIV_ACK_LATENCY_256;
		replay_timer = ET_PCIV_REPLAY_TIMER_256;
		break;

	default:
		ack_latency = pci_read_config(sc->dev, ET_PCIR_ACK_LATENCY, 2);
		replay_timer = pci_read_config(sc->dev,
		    ET_PCIR_REPLAY_TIMER, 2);
		device_printf(sc->dev, "ack latency %u, replay timer %u\n",
			      ack_latency, replay_timer);
		break;
	}
	if (ack_latency != 0) {
		pci_write_config(sc->dev, ET_PCIR_ACK_LATENCY, ack_latency, 2);
		pci_write_config(sc->dev, ET_PCIR_REPLAY_TIMER, replay_timer,
		    2);
	}

	/*
	 * Set L0s and L1 latency timer to 2us
	 */
	val = pci_read_config(sc->dev, ET_PCIR_L0S_L1_LATENCY, 4);
	val &= ~(PCIEM_LINK_CAP_L0S_EXIT | PCIEM_LINK_CAP_L1_EXIT);
	/* L0s exit latency : 2us */
	val |= 0x00005000;
	/* L1 exit latency : 2us */
	val |= 0x00028000;
	pci_write_config(sc->dev, ET_PCIR_L0S_L1_LATENCY, val, 4);

	/*
	 * Set max read request size to 2048 bytes
	 */
	pci_set_max_read_req(sc->dev, 2048);

	return (0);
}

static void
et_get_eaddr(device_t dev, uint8_t eaddr[])
{
	uint32_t val;
	int i;

	val = pci_read_config(dev, ET_PCIR_MAC_ADDR0, 4);
	for (i = 0; i < 4; ++i)
		eaddr[i] = (val >> (8 * i)) & 0xff;

	val = pci_read_config(dev, ET_PCIR_MAC_ADDR1, 2);
	for (; i < ETHER_ADDR_LEN; ++i)
		eaddr[i] = (val >> (8 * (i - 4))) & 0xff;
}

static void
et_reset(struct et_softc *sc)
{

	CSR_WRITE_4(sc, ET_MAC_CFG1,
		    ET_MAC_CFG1_RST_TXFUNC | ET_MAC_CFG1_RST_RXFUNC |
		    ET_MAC_CFG1_RST_TXMC | ET_MAC_CFG1_RST_RXMC |
		    ET_MAC_CFG1_SIM_RST | ET_MAC_CFG1_SOFT_RST);

	CSR_WRITE_4(sc, ET_SWRST,
		    ET_SWRST_TXDMA | ET_SWRST_RXDMA |
		    ET_SWRST_TXMAC | ET_SWRST_RXMAC |
		    ET_SWRST_MAC | ET_SWRST_MAC_STAT | ET_SWRST_MMC);

	CSR_WRITE_4(sc, ET_MAC_CFG1,
		    ET_MAC_CFG1_RST_TXFUNC | ET_MAC_CFG1_RST_RXFUNC |
		    ET_MAC_CFG1_RST_TXMC | ET_MAC_CFG1_RST_RXMC);
	CSR_WRITE_4(sc, ET_MAC_CFG1, 0);
	/* Disable interrupts. */
	CSR_WRITE_4(sc, ET_INTR_MASK, 0xffffffff);
}

struct et_dmamap_arg {
	bus_addr_t	et_busaddr;
};

static void
et_dma_map_addr(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	struct et_dmamap_arg *ctx;

	if (error)
		return;

	KASSERT(nseg == 1, ("%s: %d segments returned!", __func__, nseg));

	ctx = arg;
	ctx->et_busaddr = segs->ds_addr;
}

static int
et_dma_ring_alloc(struct et_softc *sc, bus_size_t alignment, bus_size_t maxsize,
    bus_dma_tag_t *tag, uint8_t **ring, bus_dmamap_t *map, bus_addr_t *paddr,
    const char *msg)
{
	struct et_dmamap_arg ctx;
	int error;

	error = bus_dma_tag_create(sc->sc_dtag, alignment, 0, BUS_SPACE_MAXADDR,
	    BUS_SPACE_MAXADDR, NULL, NULL, maxsize, 1, maxsize, 0, NULL, NULL,
	    tag);
	if (error != 0) {
		device_printf(sc->dev, "could not create %s dma tag\n", msg);
		return (error);
	}
	/* Allocate DMA'able memory for ring. */
	error = bus_dmamem_alloc(*tag, (void **)ring,
	    BUS_DMA_NOWAIT | BUS_DMA_ZERO | BUS_DMA_COHERENT, map);
	if (error != 0) {
		device_printf(sc->dev,
		    "could not allocate DMA'able memory for %s\n", msg);
		return (error);
	}
	/* Load the address of the ring. */
	ctx.et_busaddr = 0;
	error = bus_dmamap_load(*tag, *map, *ring, maxsize, et_dma_map_addr,
	    &ctx, BUS_DMA_NOWAIT);
	if (error != 0) {
		device_printf(sc->dev,
		    "could not load DMA'able memory for %s\n", msg);
		return (error);
	}
	*paddr = ctx.et_busaddr;
	return (0);
}

static void
et_dma_ring_free(struct et_softc *sc, bus_dma_tag_t *tag, uint8_t **ring,
    bus_dmamap_t map, bus_addr_t *paddr)
{

	if (*paddr != 0) {
		bus_dmamap_unload(*tag, map);
		*paddr = 0;
	}
	if (*ring != NULL) {
		bus_dmamem_free(*tag, *ring, map);
		*ring = NULL;
	}
	if (*tag) {
		bus_dma_tag_destroy(*tag);
		*tag = NULL;
	}
}

static int
et_dma_alloc(struct et_softc *sc)
{
	struct et_txdesc_ring *tx_ring;
	struct et_rxdesc_ring *rx_ring;
	struct et_rxstat_ring *rxst_ring;
	struct et_rxstatus_data *rxsd;
	struct et_rxbuf_data *rbd;
        struct et_txbuf_data *tbd;
	struct et_txstatus_data *txsd;
	int i, error;

	error = bus_dma_tag_create(bus_get_dma_tag(sc->dev), 1, 0,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR, NULL, NULL,
	    BUS_SPACE_MAXSIZE_32BIT, 0, BUS_SPACE_MAXSIZE_32BIT, 0, NULL, NULL,
	    &sc->sc_dtag);
	if (error != 0) {
		device_printf(sc->dev, "could not allocate parent dma tag\n");
		return (error);
	}

	/* TX ring. */
	tx_ring = &sc->sc_tx_ring;
	error = et_dma_ring_alloc(sc, ET_RING_ALIGN, ET_TX_RING_SIZE,
	    &tx_ring->tr_dtag, (uint8_t **)&tx_ring->tr_desc, &tx_ring->tr_dmap,
	    &tx_ring->tr_paddr, "TX ring");
	if (error)
		return (error);

	/* TX status block. */
	txsd = &sc->sc_tx_status;
	error = et_dma_ring_alloc(sc, ET_STATUS_ALIGN, sizeof(uint32_t),
	    &txsd->txsd_dtag, (uint8_t **)&txsd->txsd_status, &txsd->txsd_dmap,
	    &txsd->txsd_paddr, "TX status block");
	if (error)
		return (error);

	/* RX ring 0, used as to recive small sized frames. */
	rx_ring = &sc->sc_rx_ring[0];
	error = et_dma_ring_alloc(sc, ET_RING_ALIGN, ET_RX_RING_SIZE,
	    &rx_ring->rr_dtag, (uint8_t **)&rx_ring->rr_desc, &rx_ring->rr_dmap,
	    &rx_ring->rr_paddr, "RX ring 0");
	rx_ring->rr_posreg = ET_RX_RING0_POS;
	if (error)
		return (error);

	/* RX ring 1, used as to store normal sized frames. */
	rx_ring = &sc->sc_rx_ring[1];
	error = et_dma_ring_alloc(sc, ET_RING_ALIGN, ET_RX_RING_SIZE,
	    &rx_ring->rr_dtag, (uint8_t **)&rx_ring->rr_desc, &rx_ring->rr_dmap,
	    &rx_ring->rr_paddr, "RX ring 1");
	rx_ring->rr_posreg = ET_RX_RING1_POS;
	if (error)
		return (error);

	/* RX stat ring. */
	rxst_ring = &sc->sc_rxstat_ring;
	error = et_dma_ring_alloc(sc, ET_RING_ALIGN, ET_RXSTAT_RING_SIZE,
	    &rxst_ring->rsr_dtag, (uint8_t **)&rxst_ring->rsr_stat,
	    &rxst_ring->rsr_dmap, &rxst_ring->rsr_paddr, "RX stat ring");
	if (error)
		return (error);

	/* RX status block. */
	rxsd = &sc->sc_rx_status;
	error = et_dma_ring_alloc(sc, ET_STATUS_ALIGN,
	    sizeof(struct et_rxstatus), &rxsd->rxsd_dtag,
	    (uint8_t **)&rxsd->rxsd_status, &rxsd->rxsd_dmap,
	    &rxsd->rxsd_paddr, "RX status block");
	if (error)
		return (error);

	/* Create parent DMA tag for mbufs. */
	error = bus_dma_tag_create(bus_get_dma_tag(sc->dev), 1, 0,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR, NULL, NULL,
	    BUS_SPACE_MAXSIZE_32BIT, 0, BUS_SPACE_MAXSIZE_32BIT, 0, NULL, NULL,
	    &sc->sc_mbuf_dtag);
	if (error != 0) {
		device_printf(sc->dev,
		    "could not allocate parent dma tag for mbuf\n");
		return (error);
	}

	/* Create DMA tag for mini RX mbufs to use RX ring 0. */
	error = bus_dma_tag_create(sc->sc_mbuf_dtag, 1, 0,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR, NULL, NULL, MHLEN, 1,
	    MHLEN, 0, NULL, NULL, &sc->sc_rx_mini_tag);
	if (error) {
		device_printf(sc->dev, "could not create mini RX dma tag\n");
		return (error);
	}

	/* Create DMA tag for standard RX mbufs to use RX ring 1. */
	error = bus_dma_tag_create(sc->sc_mbuf_dtag, 1, 0,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR, NULL, NULL, MCLBYTES, 1,
	    MCLBYTES, 0, NULL, NULL, &sc->sc_rx_tag);
	if (error) {
		device_printf(sc->dev, "could not create RX dma tag\n");
		return (error);
	}

	/* Create DMA tag for TX mbufs. */
	error = bus_dma_tag_create(sc->sc_mbuf_dtag, 1, 0,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR, NULL, NULL,
	    MCLBYTES * ET_NSEG_MAX, ET_NSEG_MAX, MCLBYTES, 0, NULL, NULL,
	    &sc->sc_tx_tag);
	if (error) {
		device_printf(sc->dev, "could not create TX dma tag\n");
		return (error);
	}

	/* Initialize RX ring 0. */
	rbd = &sc->sc_rx_data[0];
	rbd->rbd_bufsize = ET_RXDMA_CTRL_RING0_128;
	rbd->rbd_newbuf = et_newbuf_hdr;
	rbd->rbd_discard = et_rxbuf_discard;
	rbd->rbd_softc = sc;
	rbd->rbd_ring = &sc->sc_rx_ring[0];
	/* Create DMA maps for mini RX buffers, ring 0. */
	for (i = 0; i < ET_RX_NDESC; i++) {
		error = bus_dmamap_create(sc->sc_rx_mini_tag, 0,
		    &rbd->rbd_buf[i].rb_dmap);
		if (error) {
			device_printf(sc->dev,
			    "could not create DMA map for mini RX mbufs\n");
			return (error);
		}
	}

	/* Create a spare DMA map for mini RX buffers, ring 0. */
	error = bus_dmamap_create(sc->sc_rx_mini_tag, 0,
	    &sc->sc_rx_mini_sparemap);
	if (error) {
		device_printf(sc->dev,
		    "could not create spare DMA map for mini RX mbuf\n");
		return (error);
	}

	/* Initialize RX ring 1. */
	rbd = &sc->sc_rx_data[1];
	rbd->rbd_bufsize = ET_RXDMA_CTRL_RING1_2048;
	rbd->rbd_newbuf = et_newbuf_cluster;
	rbd->rbd_discard = et_rxbuf_discard;
	rbd->rbd_softc = sc;
	rbd->rbd_ring = &sc->sc_rx_ring[1];
	/* Create DMA maps for standard RX buffers, ring 1. */
	for (i = 0; i < ET_RX_NDESC; i++) {
		error = bus_dmamap_create(sc->sc_rx_tag, 0,
		    &rbd->rbd_buf[i].rb_dmap);
		if (error) {
			device_printf(sc->dev,
			    "could not create DMA map for mini RX mbufs\n");
			return (error);
		}
	}

	/* Create a spare DMA map for standard RX buffers, ring 1. */
	error = bus_dmamap_create(sc->sc_rx_tag, 0, &sc->sc_rx_sparemap);
	if (error) {
		device_printf(sc->dev,
		    "could not create spare DMA map for RX mbuf\n");
		return (error);
	}

	/* Create DMA maps for TX buffers. */
	tbd = &sc->sc_tx_data;
	for (i = 0; i < ET_TX_NDESC; i++) {
		error = bus_dmamap_create(sc->sc_tx_tag, 0,
		    &tbd->tbd_buf[i].tb_dmap);
		if (error) {
			device_printf(sc->dev,
			    "could not create DMA map for TX mbufs\n");
			return (error);
		}
	}

	return (0);
}

static void
et_dma_free(struct et_softc *sc)
{
	struct et_txdesc_ring *tx_ring;
	struct et_rxdesc_ring *rx_ring;
	struct et_txstatus_data *txsd;
	struct et_rxstat_ring *rxst_ring;
	struct et_rxstatus_data *rxsd;
	struct et_rxbuf_data *rbd;
        struct et_txbuf_data *tbd;
	int i;

	/* Destroy DMA maps for mini RX buffers, ring 0. */
	rbd = &sc->sc_rx_data[0];
	for (i = 0; i < ET_RX_NDESC; i++) {
		if (rbd->rbd_buf[i].rb_dmap) {
			bus_dmamap_destroy(sc->sc_rx_mini_tag,
			    rbd->rbd_buf[i].rb_dmap);
			rbd->rbd_buf[i].rb_dmap = NULL;
		}
	}
	if (sc->sc_rx_mini_sparemap) {
		bus_dmamap_destroy(sc->sc_rx_mini_tag, sc->sc_rx_mini_sparemap);
		sc->sc_rx_mini_sparemap = NULL;
	}
	if (sc->sc_rx_mini_tag) {
		bus_dma_tag_destroy(sc->sc_rx_mini_tag);
		sc->sc_rx_mini_tag = NULL;
	}

	/* Destroy DMA maps for standard RX buffers, ring 1. */
	rbd = &sc->sc_rx_data[1];
	for (i = 0; i < ET_RX_NDESC; i++) {
		if (rbd->rbd_buf[i].rb_dmap) {
			bus_dmamap_destroy(sc->sc_rx_tag,
			    rbd->rbd_buf[i].rb_dmap);
			rbd->rbd_buf[i].rb_dmap = NULL;
		}
	}
	if (sc->sc_rx_sparemap) {
		bus_dmamap_destroy(sc->sc_rx_tag, sc->sc_rx_sparemap);
		sc->sc_rx_sparemap = NULL;
	}
	if (sc->sc_rx_tag) {
		bus_dma_tag_destroy(sc->sc_rx_tag);
		sc->sc_rx_tag = NULL;
	}

	/* Destroy DMA maps for TX buffers. */
	tbd = &sc->sc_tx_data;
	for (i = 0; i < ET_TX_NDESC; i++) {
		if (tbd->tbd_buf[i].tb_dmap) {
			bus_dmamap_destroy(sc->sc_tx_tag,
			    tbd->tbd_buf[i].tb_dmap);
			tbd->tbd_buf[i].tb_dmap = NULL;
		}
	}
	if (sc->sc_tx_tag) {
		bus_dma_tag_destroy(sc->sc_tx_tag);
		sc->sc_tx_tag = NULL;
	}

	/* Destroy mini RX ring, ring 0. */
	rx_ring = &sc->sc_rx_ring[0];
	et_dma_ring_free(sc, &rx_ring->rr_dtag, (void *)&rx_ring->rr_desc,
	    rx_ring->rr_dmap, &rx_ring->rr_paddr);
	/* Destroy standard RX ring, ring 1. */
	rx_ring = &sc->sc_rx_ring[1];
	et_dma_ring_free(sc, &rx_ring->rr_dtag, (void *)&rx_ring->rr_desc,
	    rx_ring->rr_dmap, &rx_ring->rr_paddr);
	/* Destroy RX stat ring. */
	rxst_ring = &sc->sc_rxstat_ring;
	et_dma_ring_free(sc, &rxst_ring->rsr_dtag, (void *)&rxst_ring->rsr_stat,
	    rxst_ring->rsr_dmap, &rxst_ring->rsr_paddr);
	/* Destroy RX status block. */
	rxsd = &sc->sc_rx_status;
	et_dma_ring_free(sc, &rxst_ring->rsr_dtag, (void *)&rxst_ring->rsr_stat,
	    rxst_ring->rsr_dmap, &rxst_ring->rsr_paddr);
	/* Destroy TX ring. */
	tx_ring = &sc->sc_tx_ring;
	et_dma_ring_free(sc, &tx_ring->tr_dtag, (void *)&tx_ring->tr_desc,
	    tx_ring->tr_dmap, &tx_ring->tr_paddr);
	/* Destroy TX status block. */
	txsd = &sc->sc_tx_status;
	et_dma_ring_free(sc, &txsd->txsd_dtag, (void *)&txsd->txsd_status,
	    txsd->txsd_dmap, &txsd->txsd_paddr);

	/* Destroy the parent tag. */
	if (sc->sc_dtag) {
		bus_dma_tag_destroy(sc->sc_dtag);
		sc->sc_dtag = NULL;
	}
}

static void
et_chip_attach(struct et_softc *sc)
{
	uint32_t val;

	/*
	 * Perform minimal initialization
	 */

	/* Disable loopback */
	CSR_WRITE_4(sc, ET_LOOPBACK, 0);

	/* Reset MAC */
	CSR_WRITE_4(sc, ET_MAC_CFG1,
		    ET_MAC_CFG1_RST_TXFUNC | ET_MAC_CFG1_RST_RXFUNC |
		    ET_MAC_CFG1_RST_TXMC | ET_MAC_CFG1_RST_RXMC |
		    ET_MAC_CFG1_SIM_RST | ET_MAC_CFG1_SOFT_RST);

	/*
	 * Setup half duplex mode
	 */
	val = (10 << ET_MAC_HDX_ALT_BEB_TRUNC_SHIFT) |
	    (15 << ET_MAC_HDX_REXMIT_MAX_SHIFT) |
	    (55 << ET_MAC_HDX_COLLWIN_SHIFT) |
	    ET_MAC_HDX_EXC_DEFER;
	CSR_WRITE_4(sc, ET_MAC_HDX, val);

	/* Clear MAC control */
	CSR_WRITE_4(sc, ET_MAC_CTRL, 0);

	/* Reset MII */
	CSR_WRITE_4(sc, ET_MII_CFG, ET_MII_CFG_CLKRST);

	/* Bring MAC out of reset state */
	CSR_WRITE_4(sc, ET_MAC_CFG1, 0);

	/* Enable memory controllers */
	CSR_WRITE_4(sc, ET_MMC_CTRL, ET_MMC_CTRL_ENABLE);
}

static void
et_intr(void *xsc)
{
	struct et_softc *sc;
	struct ifnet *ifp;
	uint32_t status;

	sc = xsc;
	ET_LOCK(sc);
	ifp = sc->ifp;
	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0)
		goto done;

	status = CSR_READ_4(sc, ET_INTR_STATUS);
	if ((status & ET_INTRS) == 0)
		goto done;

	/* Disable further interrupts. */
	CSR_WRITE_4(sc, ET_INTR_MASK, 0xffffffff);

	if (status & (ET_INTR_RXDMA_ERROR | ET_INTR_TXDMA_ERROR)) {
		device_printf(sc->dev, "DMA error(0x%08x) -- resetting\n",
		    status);
		ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
		et_init_locked(sc);
		ET_UNLOCK(sc);
		return;
	}
	if (status & ET_INTR_RXDMA)
		et_rxeof(sc);
	if (status & (ET_INTR_TXDMA | ET_INTR_TIMER))
		et_txeof(sc);
	if (status & ET_INTR_TIMER)
		CSR_WRITE_4(sc, ET_TIMER, sc->sc_timer);
	if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
		CSR_WRITE_4(sc, ET_INTR_MASK, ~ET_INTRS);
		if (!IFQ_DRV_IS_EMPTY(&ifp->if_snd))
			et_start_locked(ifp);
	}
done:
	ET_UNLOCK(sc);
}

static void
et_init_locked(struct et_softc *sc)
{
	struct ifnet *ifp;
	int error;

	ET_LOCK_ASSERT(sc);

	ifp = sc->ifp;
	if (ifp->if_drv_flags & IFF_DRV_RUNNING)
		return;

	et_stop(sc);
	et_reset(sc);

	et_init_tx_ring(sc);
	error = et_init_rx_ring(sc);
	if (error)
		return;

	error = et_chip_init(sc);
	if (error)
		goto fail;

	/*
	 * Start TX/RX DMA engine
	 */
	error = et_start_rxdma(sc);
	if (error)
		return;

	error = et_start_txdma(sc);
	if (error)
		return;

	/* Enable interrupts. */
	CSR_WRITE_4(sc, ET_INTR_MASK, ~ET_INTRS);

	CSR_WRITE_4(sc, ET_TIMER, sc->sc_timer);

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

	sc->sc_flags &= ~ET_FLAG_LINK;
	et_ifmedia_upd_locked(ifp);

	callout_reset(&sc->sc_tick, hz, et_tick, sc);

fail:
	if (error)
		et_stop(sc);
}

static void
et_init(void *xsc)
{
	struct et_softc *sc = xsc;

	ET_LOCK(sc);
	et_init_locked(sc);
	ET_UNLOCK(sc);
}

static int
et_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct et_softc *sc;
	struct mii_data *mii;
	struct ifreq *ifr;
	int error, mask, max_framelen;

	sc = ifp->if_softc;
	ifr = (struct ifreq *)data;
	error = 0;

/* XXX LOCKSUSED */
	switch (cmd) {
	case SIOCSIFFLAGS:
		ET_LOCK(sc);
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
				if ((ifp->if_flags ^ sc->sc_if_flags) &
				(IFF_ALLMULTI | IFF_PROMISC | IFF_BROADCAST))
					et_setmulti(sc);
			} else {
				et_init_locked(sc);
			}
		} else {
			if (ifp->if_drv_flags & IFF_DRV_RUNNING)
				et_stop(sc);
		}
		sc->sc_if_flags = ifp->if_flags;
		ET_UNLOCK(sc);
		break;

	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		mii = device_get_softc(sc->sc_miibus);
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, cmd);
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
			ET_LOCK(sc);
			et_setmulti(sc);
			ET_UNLOCK(sc);
		}
		break;

	case SIOCSIFMTU:
		ET_LOCK(sc);
#if 0
		if (sc->sc_flags & ET_FLAG_JUMBO)
			max_framelen = ET_JUMBO_FRAMELEN;
		else
#endif
			max_framelen = MCLBYTES - 1;

		if (ET_FRAMELEN(ifr->ifr_mtu) > max_framelen) {
			error = EOPNOTSUPP;
			ET_UNLOCK(sc);
			break;
		}

		if (ifp->if_mtu != ifr->ifr_mtu) {
			ifp->if_mtu = ifr->ifr_mtu;
			if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
				ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
				et_init_locked(sc);
			}
		}
		ET_UNLOCK(sc);
		break;

	case SIOCSIFCAP:
		ET_LOCK(sc);
		mask = ifr->ifr_reqcap ^ ifp->if_capenable;
		if ((mask & IFCAP_TXCSUM) != 0 &&
		    (IFCAP_TXCSUM & ifp->if_capabilities) != 0) {
			ifp->if_capenable ^= IFCAP_TXCSUM;
			if ((IFCAP_TXCSUM & ifp->if_capenable) != 0)
				ifp->if_hwassist |= ET_CSUM_FEATURES;
			else
				ifp->if_hwassist &= ~ET_CSUM_FEATURES;
		}
		ET_UNLOCK(sc);
		break;

	default:
		error = ether_ioctl(ifp, cmd, data);
		break;
	}
	return (error);
}

static void
et_start_locked(struct ifnet *ifp)
{
	struct et_softc *sc;
	struct mbuf *m_head = NULL;
	struct et_txdesc_ring *tx_ring;
	struct et_txbuf_data *tbd;
	uint32_t tx_ready_pos;
	int enq;

	sc = ifp->if_softc;
	ET_LOCK_ASSERT(sc);

	if ((ifp->if_drv_flags & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) !=
	    IFF_DRV_RUNNING ||
	    (sc->sc_flags & (ET_FLAG_LINK | ET_FLAG_TXRX_ENABLED)) !=
	    (ET_FLAG_LINK | ET_FLAG_TXRX_ENABLED))
		return;

	/*
	 * Driver does not request TX completion interrupt for every
	 * queued frames to prevent generating excessive interrupts.
	 * This means driver may wait for TX completion interrupt even
	 * though some frames were successfully transmitted.  Reclaiming
	 * transmitted frames will ensure driver see all available
	 * descriptors.
	 */
	tbd = &sc->sc_tx_data;
	if (tbd->tbd_used > (ET_TX_NDESC * 2) / 3)
		et_txeof(sc);

	for (enq = 0; !IFQ_DRV_IS_EMPTY(&ifp->if_snd); ) {
		if (tbd->tbd_used + ET_NSEG_SPARE >= ET_TX_NDESC) {
			ifp->if_drv_flags |= IFF_DRV_OACTIVE;
			break;
		}

		IFQ_DRV_DEQUEUE(&ifp->if_snd, m_head);
		if (m_head == NULL)
			break;

		if (et_encap(sc, &m_head)) {
			if (m_head == NULL) {
				if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
				break;
			}
			IFQ_DRV_PREPEND(&ifp->if_snd, m_head);
			if (tbd->tbd_used > 0)
				ifp->if_drv_flags |= IFF_DRV_OACTIVE;
			break;
		}
		enq++;
		ETHER_BPF_MTAP(ifp, m_head);
	}

	if (enq > 0) {
		tx_ring = &sc->sc_tx_ring;
		bus_dmamap_sync(tx_ring->tr_dtag, tx_ring->tr_dmap,
		    BUS_DMASYNC_PREWRITE);
		tx_ready_pos = tx_ring->tr_ready_index &
		    ET_TX_READY_POS_INDEX_MASK;
		if (tx_ring->tr_ready_wrap)
			tx_ready_pos |= ET_TX_READY_POS_WRAP;
		CSR_WRITE_4(sc, ET_TX_READY_POS, tx_ready_pos);
		sc->watchdog_timer = 5;
	}
}

static void
et_start(struct ifnet *ifp)
{
	struct et_softc *sc;

	sc = ifp->if_softc;
	ET_LOCK(sc);
	et_start_locked(ifp);
	ET_UNLOCK(sc);
}

static int
et_watchdog(struct et_softc *sc)
{
	uint32_t status;

	ET_LOCK_ASSERT(sc);

	if (sc->watchdog_timer == 0 || --sc->watchdog_timer)
		return (0);

	bus_dmamap_sync(sc->sc_tx_status.txsd_dtag, sc->sc_tx_status.txsd_dmap,
	    BUS_DMASYNC_POSTREAD);
	status = le32toh(*(sc->sc_tx_status.txsd_status));
	if_printf(sc->ifp, "watchdog timed out (0x%08x) -- resetting\n",
	    status);

	if_inc_counter(sc->ifp, IFCOUNTER_OERRORS, 1);
	sc->ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
	et_init_locked(sc);
	return (EJUSTRETURN);
}

static int
et_stop_rxdma(struct et_softc *sc)
{

	CSR_WRITE_4(sc, ET_RXDMA_CTRL,
		    ET_RXDMA_CTRL_HALT | ET_RXDMA_CTRL_RING1_ENABLE);

	DELAY(5);
	if ((CSR_READ_4(sc, ET_RXDMA_CTRL) & ET_RXDMA_CTRL_HALTED) == 0) {
		if_printf(sc->ifp, "can't stop RX DMA engine\n");
		return (ETIMEDOUT);
	}
	return (0);
}

static int
et_stop_txdma(struct et_softc *sc)
{

	CSR_WRITE_4(sc, ET_TXDMA_CTRL,
		    ET_TXDMA_CTRL_HALT | ET_TXDMA_CTRL_SINGLE_EPKT);
	return (0);
}

static void
et_free_tx_ring(struct et_softc *sc)
{
	struct et_txdesc_ring *tx_ring;
	struct et_txbuf_data *tbd;
	struct et_txbuf *tb;
	int i;

	tbd = &sc->sc_tx_data;
	tx_ring = &sc->sc_tx_ring;
	for (i = 0; i < ET_TX_NDESC; ++i) {
		tb = &tbd->tbd_buf[i];
		if (tb->tb_mbuf != NULL) {
			bus_dmamap_sync(sc->sc_tx_tag, tb->tb_dmap,
			    BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_mbuf_dtag, tb->tb_dmap);
			m_freem(tb->tb_mbuf);
			tb->tb_mbuf = NULL;
		}
	}
}

static void
et_free_rx_ring(struct et_softc *sc)
{
	struct et_rxbuf_data *rbd;
	struct et_rxdesc_ring *rx_ring;
	struct et_rxbuf *rb;
	int i;

	/* Ring 0 */
	rx_ring = &sc->sc_rx_ring[0];
	rbd = &sc->sc_rx_data[0];
	for (i = 0; i < ET_RX_NDESC; ++i) {
		rb = &rbd->rbd_buf[i];
		if (rb->rb_mbuf != NULL) {
			bus_dmamap_sync(sc->sc_rx_mini_tag, rx_ring->rr_dmap,
			    BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(sc->sc_rx_mini_tag, rb->rb_dmap);
			m_freem(rb->rb_mbuf);
			rb->rb_mbuf = NULL;
		}
	}

	/* Ring 1 */
	rx_ring = &sc->sc_rx_ring[1];
	rbd = &sc->sc_rx_data[1];
	for (i = 0; i < ET_RX_NDESC; ++i) {
		rb = &rbd->rbd_buf[i];
		if (rb->rb_mbuf != NULL) {
			bus_dmamap_sync(sc->sc_rx_tag, rx_ring->rr_dmap,
			    BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(sc->sc_rx_tag, rb->rb_dmap);
			m_freem(rb->rb_mbuf);
			rb->rb_mbuf = NULL;
		}
	}
}

static void
et_setmulti(struct et_softc *sc)
{
	struct ifnet *ifp;
	uint32_t hash[4] = { 0, 0, 0, 0 };
	uint32_t rxmac_ctrl, pktfilt;
	struct ifmultiaddr *ifma;
	int i, count;

	ET_LOCK_ASSERT(sc);
	ifp = sc->ifp;

	pktfilt = CSR_READ_4(sc, ET_PKTFILT);
	rxmac_ctrl = CSR_READ_4(sc, ET_RXMAC_CTRL);

	pktfilt &= ~(ET_PKTFILT_BCAST | ET_PKTFILT_MCAST | ET_PKTFILT_UCAST);
	if (ifp->if_flags & (IFF_PROMISC | IFF_ALLMULTI)) {
		rxmac_ctrl |= ET_RXMAC_CTRL_NO_PKTFILT;
		goto back;
	}

	count = 0;
	if_maddr_rlock(ifp);
	CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		uint32_t *hp, h;

		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;

		h = ether_crc32_be(LLADDR((struct sockaddr_dl *)
				   ifma->ifma_addr), ETHER_ADDR_LEN);
		h = (h & 0x3f800000) >> 23;

		hp = &hash[0];
		if (h >= 32 && h < 64) {
			h -= 32;
			hp = &hash[1];
		} else if (h >= 64 && h < 96) {
			h -= 64;
			hp = &hash[2];
		} else if (h >= 96) {
			h -= 96;
			hp = &hash[3];
		}
		*hp |= (1 << h);

		++count;
	}
	if_maddr_runlock(ifp);

	for (i = 0; i < 4; ++i)
		CSR_WRITE_4(sc, ET_MULTI_HASH + (i * 4), hash[i]);

	if (count > 0)
		pktfilt |= ET_PKTFILT_MCAST;
	rxmac_ctrl &= ~ET_RXMAC_CTRL_NO_PKTFILT;
back:
	CSR_WRITE_4(sc, ET_PKTFILT, pktfilt);
	CSR_WRITE_4(sc, ET_RXMAC_CTRL, rxmac_ctrl);
}

static int
et_chip_init(struct et_softc *sc)
{
	struct ifnet *ifp;
	uint32_t rxq_end;
	int error, frame_len, rxmem_size;

	ifp = sc->ifp;
	/*
	 * Split 16Kbytes internal memory between TX and RX
	 * according to frame length.
	 */
	frame_len = ET_FRAMELEN(ifp->if_mtu);
	if (frame_len < 2048) {
		rxmem_size = ET_MEM_RXSIZE_DEFAULT;
	} else if (frame_len <= ET_RXMAC_CUT_THRU_FRMLEN) {
		rxmem_size = ET_MEM_SIZE / 2;
	} else {
		rxmem_size = ET_MEM_SIZE -
		roundup(frame_len + ET_MEM_TXSIZE_EX, ET_MEM_UNIT);
	}
	rxq_end = ET_QUEUE_ADDR(rxmem_size);

	CSR_WRITE_4(sc, ET_RXQUEUE_START, ET_QUEUE_ADDR_START);
	CSR_WRITE_4(sc, ET_RXQUEUE_END, rxq_end);
	CSR_WRITE_4(sc, ET_TXQUEUE_START, rxq_end + 1);
	CSR_WRITE_4(sc, ET_TXQUEUE_END, ET_QUEUE_ADDR_END);

	/* No loopback */
	CSR_WRITE_4(sc, ET_LOOPBACK, 0);

	/* Clear MSI configure */
	if ((sc->sc_flags & ET_FLAG_MSI) == 0)
		CSR_WRITE_4(sc, ET_MSI_CFG, 0);

	/* Disable timer */
	CSR_WRITE_4(sc, ET_TIMER, 0);

	/* Initialize MAC */
	et_init_mac(sc);

	/* Enable memory controllers */
	CSR_WRITE_4(sc, ET_MMC_CTRL, ET_MMC_CTRL_ENABLE);

	/* Initialize RX MAC */
	et_init_rxmac(sc);

	/* Initialize TX MAC */
	et_init_txmac(sc);

	/* Initialize RX DMA engine */
	error = et_init_rxdma(sc);
	if (error)
		return (error);

	/* Initialize TX DMA engine */
	error = et_init_txdma(sc);
	if (error)
		return (error);

	return (0);
}

static void
et_init_tx_ring(struct et_softc *sc)
{
	struct et_txdesc_ring *tx_ring;
	struct et_txbuf_data *tbd;
	struct et_txstatus_data *txsd;

	tx_ring = &sc->sc_tx_ring;
	bzero(tx_ring->tr_desc, ET_TX_RING_SIZE);
	bus_dmamap_sync(tx_ring->tr_dtag, tx_ring->tr_dmap,
	    BUS_DMASYNC_PREWRITE);

	tbd = &sc->sc_tx_data;
	tbd->tbd_start_index = 0;
	tbd->tbd_start_wrap = 0;
	tbd->tbd_used = 0;

	txsd = &sc->sc_tx_status;
	bzero(txsd->txsd_status, sizeof(uint32_t));
	bus_dmamap_sync(txsd->txsd_dtag, txsd->txsd_dmap,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}

static int
et_init_rx_ring(struct et_softc *sc)
{
	struct et_rxstatus_data *rxsd;
	struct et_rxstat_ring *rxst_ring;
	struct et_rxbuf_data *rbd;
	int i, error, n;

	for (n = 0; n < ET_RX_NRING; ++n) {
		rbd = &sc->sc_rx_data[n];
		for (i = 0; i < ET_RX_NDESC; ++i) {
			error = rbd->rbd_newbuf(rbd, i);
			if (error) {
				if_printf(sc->ifp, "%d ring %d buf, "
					  "newbuf failed: %d\n", n, i, error);
				return (error);
			}
		}
	}

	rxsd = &sc->sc_rx_status;
	bzero(rxsd->rxsd_status, sizeof(struct et_rxstatus));
	bus_dmamap_sync(rxsd->rxsd_dtag, rxsd->rxsd_dmap,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	rxst_ring = &sc->sc_rxstat_ring;
	bzero(rxst_ring->rsr_stat, ET_RXSTAT_RING_SIZE);
	bus_dmamap_sync(rxst_ring->rsr_dtag, rxst_ring->rsr_dmap,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	return (0);
}

static int
et_init_rxdma(struct et_softc *sc)
{
	struct et_rxstatus_data *rxsd;
	struct et_rxstat_ring *rxst_ring;
	struct et_rxdesc_ring *rx_ring;
	int error;

	error = et_stop_rxdma(sc);
	if (error) {
		if_printf(sc->ifp, "can't init RX DMA engine\n");
		return (error);
	}

	/*
	 * Install RX status
	 */
	rxsd = &sc->sc_rx_status;
	CSR_WRITE_4(sc, ET_RX_STATUS_HI, ET_ADDR_HI(rxsd->rxsd_paddr));
	CSR_WRITE_4(sc, ET_RX_STATUS_LO, ET_ADDR_LO(rxsd->rxsd_paddr));

	/*
	 * Install RX stat ring
	 */
	rxst_ring = &sc->sc_rxstat_ring;
	CSR_WRITE_4(sc, ET_RXSTAT_HI, ET_ADDR_HI(rxst_ring->rsr_paddr));
	CSR_WRITE_4(sc, ET_RXSTAT_LO, ET_ADDR_LO(rxst_ring->rsr_paddr));
	CSR_WRITE_4(sc, ET_RXSTAT_CNT, ET_RX_NSTAT - 1);
	CSR_WRITE_4(sc, ET_RXSTAT_POS, 0);
	CSR_WRITE_4(sc, ET_RXSTAT_MINCNT, ((ET_RX_NSTAT * 15) / 100) - 1);

	/* Match ET_RXSTAT_POS */
	rxst_ring->rsr_index = 0;
	rxst_ring->rsr_wrap = 0;

	/*
	 * Install the 2nd RX descriptor ring
	 */
	rx_ring = &sc->sc_rx_ring[1];
	CSR_WRITE_4(sc, ET_RX_RING1_HI, ET_ADDR_HI(rx_ring->rr_paddr));
	CSR_WRITE_4(sc, ET_RX_RING1_LO, ET_ADDR_LO(rx_ring->rr_paddr));
	CSR_WRITE_4(sc, ET_RX_RING1_CNT, ET_RX_NDESC - 1);
	CSR_WRITE_4(sc, ET_RX_RING1_POS, ET_RX_RING1_POS_WRAP);
	CSR_WRITE_4(sc, ET_RX_RING1_MINCNT, ((ET_RX_NDESC * 15) / 100) - 1);

	/* Match ET_RX_RING1_POS */
	rx_ring->rr_index = 0;
	rx_ring->rr_wrap = 1;

	/*
	 * Install the 1st RX descriptor ring
	 */
	rx_ring = &sc->sc_rx_ring[0];
	CSR_WRITE_4(sc, ET_RX_RING0_HI, ET_ADDR_HI(rx_ring->rr_paddr));
	CSR_WRITE_4(sc, ET_RX_RING0_LO, ET_ADDR_LO(rx_ring->rr_paddr));
	CSR_WRITE_4(sc, ET_RX_RING0_CNT, ET_RX_NDESC - 1);
	CSR_WRITE_4(sc, ET_RX_RING0_POS, ET_RX_RING0_POS_WRAP);
	CSR_WRITE_4(sc, ET_RX_RING0_MINCNT, ((ET_RX_NDESC * 15) / 100) - 1);

	/* Match ET_RX_RING0_POS */
	rx_ring->rr_index = 0;
	rx_ring->rr_wrap = 1;

	/*
	 * RX intr moderation
	 */
	CSR_WRITE_4(sc, ET_RX_INTR_NPKTS, sc->sc_rx_intr_npkts);
	CSR_WRITE_4(sc, ET_RX_INTR_DELAY, sc->sc_rx_intr_delay);

	return (0);
}

static int
et_init_txdma(struct et_softc *sc)
{
	struct et_txdesc_ring *tx_ring;
	struct et_txstatus_data *txsd;
	int error;

	error = et_stop_txdma(sc);
	if (error) {
		if_printf(sc->ifp, "can't init TX DMA engine\n");
		return (error);
	}

	/*
	 * Install TX descriptor ring
	 */
	tx_ring = &sc->sc_tx_ring;
	CSR_WRITE_4(sc, ET_TX_RING_HI, ET_ADDR_HI(tx_ring->tr_paddr));
	CSR_WRITE_4(sc, ET_TX_RING_LO, ET_ADDR_LO(tx_ring->tr_paddr));
	CSR_WRITE_4(sc, ET_TX_RING_CNT, ET_TX_NDESC - 1);

	/*
	 * Install TX status
	 */
	txsd = &sc->sc_tx_status;
	CSR_WRITE_4(sc, ET_TX_STATUS_HI, ET_ADDR_HI(txsd->txsd_paddr));
	CSR_WRITE_4(sc, ET_TX_STATUS_LO, ET_ADDR_LO(txsd->txsd_paddr));

	CSR_WRITE_4(sc, ET_TX_READY_POS, 0);

	/* Match ET_TX_READY_POS */
	tx_ring->tr_ready_index = 0;
	tx_ring->tr_ready_wrap = 0;

	return (0);
}

static void
et_init_mac(struct et_softc *sc)
{
	struct ifnet *ifp;
	const uint8_t *eaddr;
	uint32_t val;

	/* Reset MAC */
	CSR_WRITE_4(sc, ET_MAC_CFG1,
		    ET_MAC_CFG1_RST_TXFUNC | ET_MAC_CFG1_RST_RXFUNC |
		    ET_MAC_CFG1_RST_TXMC | ET_MAC_CFG1_RST_RXMC |
		    ET_MAC_CFG1_SIM_RST | ET_MAC_CFG1_SOFT_RST);

	/*
	 * Setup inter packet gap
	 */
	val = (56 << ET_IPG_NONB2B_1_SHIFT) |
	    (88 << ET_IPG_NONB2B_2_SHIFT) |
	    (80 << ET_IPG_MINIFG_SHIFT) |
	    (96 << ET_IPG_B2B_SHIFT);
	CSR_WRITE_4(sc, ET_IPG, val);

	/*
	 * Setup half duplex mode
	 */
	val = (10 << ET_MAC_HDX_ALT_BEB_TRUNC_SHIFT) |
	    (15 << ET_MAC_HDX_REXMIT_MAX_SHIFT) |
	    (55 << ET_MAC_HDX_COLLWIN_SHIFT) |
	    ET_MAC_HDX_EXC_DEFER;
	CSR_WRITE_4(sc, ET_MAC_HDX, val);

	/* Clear MAC control */
	CSR_WRITE_4(sc, ET_MAC_CTRL, 0);

	/* Reset MII */
	CSR_WRITE_4(sc, ET_MII_CFG, ET_MII_CFG_CLKRST);

	/*
	 * Set MAC address
	 */
	ifp = sc->ifp;
	eaddr = IF_LLADDR(ifp);
	val = eaddr[2] | (eaddr[3] << 8) | (eaddr[4] << 16) | (eaddr[5] << 24);
	CSR_WRITE_4(sc, ET_MAC_ADDR1, val);
	val = (eaddr[0] << 16) | (eaddr[1] << 24);
	CSR_WRITE_4(sc, ET_MAC_ADDR2, val);

	/* Set max frame length */
	CSR_WRITE_4(sc, ET_MAX_FRMLEN, ET_FRAMELEN(ifp->if_mtu));

	/* Bring MAC out of reset state */
	CSR_WRITE_4(sc, ET_MAC_CFG1, 0);
}

static void
et_init_rxmac(struct et_softc *sc)
{
	struct ifnet *ifp;
	const uint8_t *eaddr;
	uint32_t val;
	int i;

	/* Disable RX MAC and WOL */
	CSR_WRITE_4(sc, ET_RXMAC_CTRL, ET_RXMAC_CTRL_WOL_DISABLE);

	/*
	 * Clear all WOL related registers
	 */
	for (i = 0; i < 3; ++i)
		CSR_WRITE_4(sc, ET_WOL_CRC + (i * 4), 0);
	for (i = 0; i < 20; ++i)
		CSR_WRITE_4(sc, ET_WOL_MASK + (i * 4), 0);

	/*
	 * Set WOL source address.  XXX is this necessary?
	 */
	ifp = sc->ifp;
	eaddr = IF_LLADDR(ifp);
	val = (eaddr[2] << 24) | (eaddr[3] << 16) | (eaddr[4] << 8) | eaddr[5];
	CSR_WRITE_4(sc, ET_WOL_SA_LO, val);
	val = (eaddr[0] << 8) | eaddr[1];
	CSR_WRITE_4(sc, ET_WOL_SA_HI, val);

	/* Clear packet filters */
	CSR_WRITE_4(sc, ET_PKTFILT, 0);

	/* No ucast filtering */
	CSR_WRITE_4(sc, ET_UCAST_FILTADDR1, 0);
	CSR_WRITE_4(sc, ET_UCAST_FILTADDR2, 0);
	CSR_WRITE_4(sc, ET_UCAST_FILTADDR3, 0);

	if (ET_FRAMELEN(ifp->if_mtu) > ET_RXMAC_CUT_THRU_FRMLEN) {
		/*
		 * In order to transmit jumbo packets greater than
		 * ET_RXMAC_CUT_THRU_FRMLEN bytes, the FIFO between
		 * RX MAC and RX DMA needs to be reduced in size to
		 * (ET_MEM_SIZE - ET_MEM_TXSIZE_EX - framelen).  In
		 * order to implement this, we must use "cut through"
		 * mode in the RX MAC, which chops packets down into
		 * segments.  In this case we selected 256 bytes,
		 * since this is the size of the PCI-Express TLP's
		 * that the ET1310 uses.
		 */
		val = (ET_RXMAC_SEGSZ(256) & ET_RXMAC_MC_SEGSZ_MAX_MASK) |
		      ET_RXMAC_MC_SEGSZ_ENABLE;
	} else {
		val = 0;
	}
	CSR_WRITE_4(sc, ET_RXMAC_MC_SEGSZ, val);

	CSR_WRITE_4(sc, ET_RXMAC_MC_WATERMARK, 0);

	/* Initialize RX MAC management register */
	CSR_WRITE_4(sc, ET_RXMAC_MGT, 0);

	CSR_WRITE_4(sc, ET_RXMAC_SPACE_AVL, 0);

	CSR_WRITE_4(sc, ET_RXMAC_MGT,
		    ET_RXMAC_MGT_PASS_ECRC |
		    ET_RXMAC_MGT_PASS_ELEN |
		    ET_RXMAC_MGT_PASS_ETRUNC |
		    ET_RXMAC_MGT_CHECK_PKT);

	/*
	 * Configure runt filtering (may not work on certain chip generation)
	 */
	val = (ETHER_MIN_LEN << ET_PKTFILT_MINLEN_SHIFT) &
	    ET_PKTFILT_MINLEN_MASK;
	val |= ET_PKTFILT_FRAG;
	CSR_WRITE_4(sc, ET_PKTFILT, val);

	/* Enable RX MAC but leave WOL disabled */
	CSR_WRITE_4(sc, ET_RXMAC_CTRL,
		    ET_RXMAC_CTRL_WOL_DISABLE | ET_RXMAC_CTRL_ENABLE);

	/*
	 * Setup multicast hash and allmulti/promisc mode
	 */
	et_setmulti(sc);
}

static void
et_init_txmac(struct et_softc *sc)
{

	/* Disable TX MAC and FC(?) */
	CSR_WRITE_4(sc, ET_TXMAC_CTRL, ET_TXMAC_CTRL_FC_DISABLE);

	/*
	 * Initialize pause time.
	 * This register should be set before XON/XOFF frame is
	 * sent by driver.
	 */
	CSR_WRITE_4(sc, ET_TXMAC_FLOWCTRL, 0 << ET_TXMAC_FLOWCTRL_CFPT_SHIFT);

	/* Enable TX MAC but leave FC(?) diabled */
	CSR_WRITE_4(sc, ET_TXMAC_CTRL,
		    ET_TXMAC_CTRL_ENABLE | ET_TXMAC_CTRL_FC_DISABLE);
}

static int
et_start_rxdma(struct et_softc *sc)
{
	uint32_t val;

	val = (sc->sc_rx_data[0].rbd_bufsize & ET_RXDMA_CTRL_RING0_SIZE_MASK) |
	    ET_RXDMA_CTRL_RING0_ENABLE;
	val |= (sc->sc_rx_data[1].rbd_bufsize & ET_RXDMA_CTRL_RING1_SIZE_MASK) |
	    ET_RXDMA_CTRL_RING1_ENABLE;

	CSR_WRITE_4(sc, ET_RXDMA_CTRL, val);

	DELAY(5);

	if (CSR_READ_4(sc, ET_RXDMA_CTRL) & ET_RXDMA_CTRL_HALTED) {
		if_printf(sc->ifp, "can't start RX DMA engine\n");
		return (ETIMEDOUT);
	}
	return (0);
}

static int
et_start_txdma(struct et_softc *sc)
{

	CSR_WRITE_4(sc, ET_TXDMA_CTRL, ET_TXDMA_CTRL_SINGLE_EPKT);
	return (0);
}

static void
et_rxeof(struct et_softc *sc)
{
	struct et_rxstatus_data *rxsd;
	struct et_rxstat_ring *rxst_ring;
	struct et_rxbuf_data *rbd;
	struct et_rxdesc_ring *rx_ring;
	struct et_rxstat *st;
	struct ifnet *ifp;
	struct mbuf *m;
	uint32_t rxstat_pos, rxring_pos;
	uint32_t rxst_info1, rxst_info2, rxs_stat_ring;
	int buflen, buf_idx, npost[2], ring_idx;
	int rxst_index, rxst_wrap;

	ET_LOCK_ASSERT(sc);

	ifp = sc->ifp;
	rxsd = &sc->sc_rx_status;
	rxst_ring = &sc->sc_rxstat_ring;

	if ((sc->sc_flags & ET_FLAG_TXRX_ENABLED) == 0)
		return;

	bus_dmamap_sync(rxsd->rxsd_dtag, rxsd->rxsd_dmap,
	    BUS_DMASYNC_POSTREAD);
	bus_dmamap_sync(rxst_ring->rsr_dtag, rxst_ring->rsr_dmap,
	    BUS_DMASYNC_POSTREAD);

	npost[0] = npost[1] = 0;
	rxs_stat_ring = le32toh(rxsd->rxsd_status->rxs_stat_ring);
	rxst_wrap = (rxs_stat_ring & ET_RXS_STATRING_WRAP) ? 1 : 0;
	rxst_index = (rxs_stat_ring & ET_RXS_STATRING_INDEX_MASK) >>
	    ET_RXS_STATRING_INDEX_SHIFT;

	while (rxst_index != rxst_ring->rsr_index ||
	    rxst_wrap != rxst_ring->rsr_wrap) {
		if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0)
			break;

		MPASS(rxst_ring->rsr_index < ET_RX_NSTAT);
		st = &rxst_ring->rsr_stat[rxst_ring->rsr_index];
		rxst_info1 = le32toh(st->rxst_info1);
		rxst_info2 = le32toh(st->rxst_info2);
		buflen = (rxst_info2 & ET_RXST_INFO2_LEN_MASK) >>
		    ET_RXST_INFO2_LEN_SHIFT;
		buf_idx = (rxst_info2 & ET_RXST_INFO2_BUFIDX_MASK) >>
		    ET_RXST_INFO2_BUFIDX_SHIFT;
		ring_idx = (rxst_info2 & ET_RXST_INFO2_RINGIDX_MASK) >>
		    ET_RXST_INFO2_RINGIDX_SHIFT;

		if (++rxst_ring->rsr_index == ET_RX_NSTAT) {
			rxst_ring->rsr_index = 0;
			rxst_ring->rsr_wrap ^= 1;
		}
		rxstat_pos = rxst_ring->rsr_index & ET_RXSTAT_POS_INDEX_MASK;
		if (rxst_ring->rsr_wrap)
			rxstat_pos |= ET_RXSTAT_POS_WRAP;
		CSR_WRITE_4(sc, ET_RXSTAT_POS, rxstat_pos);

		if (ring_idx >= ET_RX_NRING) {
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			if_printf(ifp, "invalid ring index %d\n", ring_idx);
			continue;
		}
		if (buf_idx >= ET_RX_NDESC) {
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			if_printf(ifp, "invalid buf index %d\n", buf_idx);
			continue;
		}

		rbd = &sc->sc_rx_data[ring_idx];
		m = rbd->rbd_buf[buf_idx].rb_mbuf;
		if ((rxst_info1 & ET_RXST_INFO1_OK) == 0){
			/* Discard errored frame. */
			rbd->rbd_discard(rbd, buf_idx);
		} else if (rbd->rbd_newbuf(rbd, buf_idx) != 0) {
			/* No available mbufs, discard it. */
			if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
			rbd->rbd_discard(rbd, buf_idx);
		} else {
			buflen -= ETHER_CRC_LEN;
			if (buflen < ETHER_HDR_LEN) {
				m_freem(m);
				if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			} else {
				m->m_pkthdr.len = m->m_len = buflen;
				m->m_pkthdr.rcvif = ifp;
				ET_UNLOCK(sc);
				ifp->if_input(ifp, m);
				ET_LOCK(sc);
			}
		}

		rx_ring = &sc->sc_rx_ring[ring_idx];
		if (buf_idx != rx_ring->rr_index) {
			if_printf(ifp,
			    "WARNING!! ring %d, buf_idx %d, rr_idx %d\n",
			    ring_idx, buf_idx, rx_ring->rr_index);
		}

		MPASS(rx_ring->rr_index < ET_RX_NDESC);
		if (++rx_ring->rr_index == ET_RX_NDESC) {
			rx_ring->rr_index = 0;
			rx_ring->rr_wrap ^= 1;
		}
		rxring_pos = rx_ring->rr_index & ET_RX_RING_POS_INDEX_MASK;
		if (rx_ring->rr_wrap)
			rxring_pos |= ET_RX_RING_POS_WRAP;
		CSR_WRITE_4(sc, rx_ring->rr_posreg, rxring_pos);
	}

	bus_dmamap_sync(rxsd->rxsd_dtag, rxsd->rxsd_dmap,
	    BUS_DMASYNC_PREREAD);
	bus_dmamap_sync(rxst_ring->rsr_dtag, rxst_ring->rsr_dmap,
	    BUS_DMASYNC_PREREAD);
}

static int
et_encap(struct et_softc *sc, struct mbuf **m0)
{
	struct et_txdesc_ring *tx_ring;
	struct et_txbuf_data *tbd;
	struct et_txdesc *td;
	struct mbuf *m;
	bus_dma_segment_t segs[ET_NSEG_MAX];
	bus_dmamap_t map;
	uint32_t csum_flags, last_td_ctrl2;
	int error, i, idx, first_idx, last_idx, nsegs;

	tx_ring = &sc->sc_tx_ring;
	MPASS(tx_ring->tr_ready_index < ET_TX_NDESC);
	tbd = &sc->sc_tx_data;
	first_idx = tx_ring->tr_ready_index;
	map = tbd->tbd_buf[first_idx].tb_dmap;

	error = bus_dmamap_load_mbuf_sg(sc->sc_tx_tag, map, *m0, segs, &nsegs,
	    0);
	if (error == EFBIG) {
		m = m_collapse(*m0, M_NOWAIT, ET_NSEG_MAX);
		if (m == NULL) {
			m_freem(*m0);
			*m0 = NULL;
			return (ENOMEM);
		}
		*m0 = m;
		error = bus_dmamap_load_mbuf_sg(sc->sc_tx_tag, map, *m0, segs,
		    &nsegs, 0);
		if (error != 0) {
			m_freem(*m0);
                        *m0 = NULL;
			return (error);
		}
	} else if (error != 0)
		return (error);

	/* Check for descriptor overruns. */
	if (tbd->tbd_used + nsegs > ET_TX_NDESC - 1) {
		bus_dmamap_unload(sc->sc_tx_tag, map);
		return (ENOBUFS);
	}
	bus_dmamap_sync(sc->sc_tx_tag, map, BUS_DMASYNC_PREWRITE);

	last_td_ctrl2 = ET_TDCTRL2_LAST_FRAG;
	sc->sc_tx += nsegs;
	if (sc->sc_tx / sc->sc_tx_intr_nsegs != sc->sc_tx_intr) {
		sc->sc_tx_intr = sc->sc_tx / sc->sc_tx_intr_nsegs;
		last_td_ctrl2 |= ET_TDCTRL2_INTR;
	}

	m = *m0;
	csum_flags = 0;
	if ((m->m_pkthdr.csum_flags & ET_CSUM_FEATURES) != 0) {
		if ((m->m_pkthdr.csum_flags & CSUM_IP) != 0)
			csum_flags |= ET_TDCTRL2_CSUM_IP;
		if ((m->m_pkthdr.csum_flags & CSUM_UDP) != 0)
			csum_flags |= ET_TDCTRL2_CSUM_UDP;
		else if ((m->m_pkthdr.csum_flags & CSUM_TCP) != 0)
			csum_flags |= ET_TDCTRL2_CSUM_TCP;
	}
	last_idx = -1;
	for (i = 0; i < nsegs; ++i) {
		idx = (first_idx + i) % ET_TX_NDESC;
		td = &tx_ring->tr_desc[idx];
		td->td_addr_hi = htole32(ET_ADDR_HI(segs[i].ds_addr));
		td->td_addr_lo = htole32(ET_ADDR_LO(segs[i].ds_addr));
		td->td_ctrl1 =  htole32(segs[i].ds_len & ET_TDCTRL1_LEN_MASK);
		if (i == nsegs - 1) {
			/* Last frag */
			td->td_ctrl2 = htole32(last_td_ctrl2 | csum_flags);
			last_idx = idx;
		} else
			td->td_ctrl2 = htole32(csum_flags);

		MPASS(tx_ring->tr_ready_index < ET_TX_NDESC);
		if (++tx_ring->tr_ready_index == ET_TX_NDESC) {
			tx_ring->tr_ready_index = 0;
			tx_ring->tr_ready_wrap ^= 1;
		}
	}
	td = &tx_ring->tr_desc[first_idx];
	/* First frag */
	td->td_ctrl2 |= htole32(ET_TDCTRL2_FIRST_FRAG);

	MPASS(last_idx >= 0);
	tbd->tbd_buf[first_idx].tb_dmap = tbd->tbd_buf[last_idx].tb_dmap;
	tbd->tbd_buf[last_idx].tb_dmap = map;
	tbd->tbd_buf[last_idx].tb_mbuf = m;

	tbd->tbd_used += nsegs;
	MPASS(tbd->tbd_used <= ET_TX_NDESC);

	return (0);
}

static void
et_txeof(struct et_softc *sc)
{
	struct et_txdesc_ring *tx_ring;
	struct et_txbuf_data *tbd;
	struct et_txbuf *tb;
	struct ifnet *ifp;
	uint32_t tx_done;
	int end, wrap;

	ET_LOCK_ASSERT(sc);

	ifp = sc->ifp;
	tx_ring = &sc->sc_tx_ring;
	tbd = &sc->sc_tx_data;

	if ((sc->sc_flags & ET_FLAG_TXRX_ENABLED) == 0)
		return;

	if (tbd->tbd_used == 0)
		return;

	bus_dmamap_sync(tx_ring->tr_dtag, tx_ring->tr_dmap,
	    BUS_DMASYNC_POSTWRITE);

	tx_done = CSR_READ_4(sc, ET_TX_DONE_POS);
	end = tx_done & ET_TX_DONE_POS_INDEX_MASK;
	wrap = (tx_done & ET_TX_DONE_POS_WRAP) ? 1 : 0;

	while (tbd->tbd_start_index != end || tbd->tbd_start_wrap != wrap) {
		MPASS(tbd->tbd_start_index < ET_TX_NDESC);
		tb = &tbd->tbd_buf[tbd->tbd_start_index];
		if (tb->tb_mbuf != NULL) {
			bus_dmamap_sync(sc->sc_tx_tag, tb->tb_dmap,
			    BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_tx_tag, tb->tb_dmap);
			m_freem(tb->tb_mbuf);
			tb->tb_mbuf = NULL;
		}

		if (++tbd->tbd_start_index == ET_TX_NDESC) {
			tbd->tbd_start_index = 0;
			tbd->tbd_start_wrap ^= 1;
		}

		MPASS(tbd->tbd_used > 0);
		tbd->tbd_used--;
	}

	if (tbd->tbd_used == 0)
		sc->watchdog_timer = 0;
	if (tbd->tbd_used + ET_NSEG_SPARE < ET_TX_NDESC)
		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
}

static void
et_tick(void *xsc)
{
	struct et_softc *sc;
	struct ifnet *ifp;
	struct mii_data *mii;

	sc = xsc;
	ET_LOCK_ASSERT(sc);
	ifp = sc->ifp;
	mii = device_get_softc(sc->sc_miibus);

	mii_tick(mii);
	et_stats_update(sc);
	if (et_watchdog(sc) == EJUSTRETURN)
		return;
	callout_reset(&sc->sc_tick, hz, et_tick, sc);
}

static int
et_newbuf_cluster(struct et_rxbuf_data *rbd, int buf_idx)
{
	struct et_softc *sc;
	struct et_rxdesc *desc;
	struct et_rxbuf *rb;
	struct mbuf *m;
	bus_dma_segment_t segs[1];
	bus_dmamap_t dmap;
	int nsegs;

	MPASS(buf_idx < ET_RX_NDESC);
	m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
	if (m == NULL)
		return (ENOBUFS);
	m->m_len = m->m_pkthdr.len = MCLBYTES;
	m_adj(m, ETHER_ALIGN);

	sc = rbd->rbd_softc;
	rb = &rbd->rbd_buf[buf_idx];

	if (bus_dmamap_load_mbuf_sg(sc->sc_rx_tag, sc->sc_rx_sparemap, m,
	    segs, &nsegs, 0) != 0) {
		m_freem(m);
		return (ENOBUFS);
	}
	KASSERT(nsegs == 1, ("%s: %d segments returned!", __func__, nsegs));

	if (rb->rb_mbuf != NULL) {
		bus_dmamap_sync(sc->sc_rx_tag, rb->rb_dmap,
		    BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->sc_rx_tag, rb->rb_dmap);
	}
	dmap = rb->rb_dmap;
	rb->rb_dmap = sc->sc_rx_sparemap;
	sc->sc_rx_sparemap = dmap;
	bus_dmamap_sync(sc->sc_rx_tag, rb->rb_dmap, BUS_DMASYNC_PREREAD);

	rb->rb_mbuf = m;
	desc = &rbd->rbd_ring->rr_desc[buf_idx];
	desc->rd_addr_hi = htole32(ET_ADDR_HI(segs[0].ds_addr));
	desc->rd_addr_lo = htole32(ET_ADDR_LO(segs[0].ds_addr));
	desc->rd_ctrl = htole32(buf_idx & ET_RDCTRL_BUFIDX_MASK);
	bus_dmamap_sync(rbd->rbd_ring->rr_dtag, rbd->rbd_ring->rr_dmap,
	    BUS_DMASYNC_PREWRITE);
	return (0);
}

static void
et_rxbuf_discard(struct et_rxbuf_data *rbd, int buf_idx)
{
	struct et_rxdesc *desc;

	desc = &rbd->rbd_ring->rr_desc[buf_idx];
	desc->rd_ctrl = htole32(buf_idx & ET_RDCTRL_BUFIDX_MASK);
	bus_dmamap_sync(rbd->rbd_ring->rr_dtag, rbd->rbd_ring->rr_dmap,
	    BUS_DMASYNC_PREWRITE);
}

static int
et_newbuf_hdr(struct et_rxbuf_data *rbd, int buf_idx)
{
	struct et_softc *sc;
	struct et_rxdesc *desc;
	struct et_rxbuf *rb;
	struct mbuf *m;
	bus_dma_segment_t segs[1];
	bus_dmamap_t dmap;
	int nsegs;

	MPASS(buf_idx < ET_RX_NDESC);
	MGETHDR(m, M_NOWAIT, MT_DATA);
	if (m == NULL)
		return (ENOBUFS);
	m->m_len = m->m_pkthdr.len = MHLEN;
	m_adj(m, ETHER_ALIGN);

	sc = rbd->rbd_softc;
	rb = &rbd->rbd_buf[buf_idx];

	if (bus_dmamap_load_mbuf_sg(sc->sc_rx_mini_tag, sc->sc_rx_mini_sparemap,
	    m, segs, &nsegs, 0) != 0) {
		m_freem(m);
		return (ENOBUFS);
	}
	KASSERT(nsegs == 1, ("%s: %d segments returned!", __func__, nsegs));

	if (rb->rb_mbuf != NULL) {
		bus_dmamap_sync(sc->sc_rx_mini_tag, rb->rb_dmap,
		    BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->sc_rx_mini_tag, rb->rb_dmap);
	}
	dmap = rb->rb_dmap;
	rb->rb_dmap = sc->sc_rx_mini_sparemap;
	sc->sc_rx_mini_sparemap = dmap;
	bus_dmamap_sync(sc->sc_rx_mini_tag, rb->rb_dmap, BUS_DMASYNC_PREREAD);

	rb->rb_mbuf = m;
	desc = &rbd->rbd_ring->rr_desc[buf_idx];
	desc->rd_addr_hi = htole32(ET_ADDR_HI(segs[0].ds_addr));
	desc->rd_addr_lo = htole32(ET_ADDR_LO(segs[0].ds_addr));
	desc->rd_ctrl = htole32(buf_idx & ET_RDCTRL_BUFIDX_MASK);
	bus_dmamap_sync(rbd->rbd_ring->rr_dtag, rbd->rbd_ring->rr_dmap,
	    BUS_DMASYNC_PREWRITE);
	return (0);
}

#define	ET_SYSCTL_STAT_ADD32(c, h, n, p, d)	\
	    SYSCTL_ADD_UINT(c, h, OID_AUTO, n, CTLFLAG_RD, p, 0, d)
#define	ET_SYSCTL_STAT_ADD64(c, h, n, p, d)	\
	    SYSCTL_ADD_UQUAD(c, h, OID_AUTO, n, CTLFLAG_RD, p, d)

/*
 * Create sysctl tree
 */
static void
et_add_sysctls(struct et_softc * sc)
{
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid_list *children, *parent;
	struct sysctl_oid *tree;
	struct et_hw_stats *stats;

	ctx = device_get_sysctl_ctx(sc->dev);
	children = SYSCTL_CHILDREN(device_get_sysctl_tree(sc->dev));

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "rx_intr_npkts",
	    CTLTYPE_INT | CTLFLAG_RW, sc, 0, et_sysctl_rx_intr_npkts, "I",
	    "RX IM, # packets per RX interrupt");
	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "rx_intr_delay",
	    CTLTYPE_INT | CTLFLAG_RW, sc, 0, et_sysctl_rx_intr_delay, "I",
	    "RX IM, RX interrupt delay (x10 usec)");
	SYSCTL_ADD_INT(ctx, children, OID_AUTO, "tx_intr_nsegs",
	    CTLFLAG_RW, &sc->sc_tx_intr_nsegs, 0,
	    "TX IM, # segments per TX interrupt");
	SYSCTL_ADD_UINT(ctx, children, OID_AUTO, "timer",
	    CTLFLAG_RW, &sc->sc_timer, 0, "TX timer");

	tree = SYSCTL_ADD_NODE(ctx, children, OID_AUTO, "stats", CTLFLAG_RD,
	    NULL, "ET statistics");
        parent = SYSCTL_CHILDREN(tree);

	/* TX/RX statistics. */
	stats = &sc->sc_stats;
	ET_SYSCTL_STAT_ADD64(ctx, parent, "frames_64", &stats->pkts_64,
	    "0 to 64 bytes frames");
	ET_SYSCTL_STAT_ADD64(ctx, parent, "frames_65_127", &stats->pkts_65,
	    "65 to 127 bytes frames");
	ET_SYSCTL_STAT_ADD64(ctx, parent, "frames_128_255", &stats->pkts_128,
	    "128 to 255 bytes frames");
	ET_SYSCTL_STAT_ADD64(ctx, parent, "frames_256_511", &stats->pkts_256,
	    "256 to 511 bytes frames");
	ET_SYSCTL_STAT_ADD64(ctx, parent, "frames_512_1023", &stats->pkts_512,
	    "512 to 1023 bytes frames");
	ET_SYSCTL_STAT_ADD64(ctx, parent, "frames_1024_1518", &stats->pkts_1024,
	    "1024 to 1518 bytes frames");
	ET_SYSCTL_STAT_ADD64(ctx, parent, "frames_1519_1522", &stats->pkts_1519,
	    "1519 to 1522 bytes frames");

	/* RX statistics. */
	tree = SYSCTL_ADD_NODE(ctx, parent, OID_AUTO, "rx", CTLFLAG_RD,
	    NULL, "RX MAC statistics");
	children = SYSCTL_CHILDREN(tree);
	ET_SYSCTL_STAT_ADD64(ctx, children, "bytes",
	    &stats->rx_bytes, "Good bytes");
	ET_SYSCTL_STAT_ADD64(ctx, children, "frames",
	    &stats->rx_frames, "Good frames");
	ET_SYSCTL_STAT_ADD32(ctx, children, "crc_errs",
	    &stats->rx_crcerrs, "CRC errors");
	ET_SYSCTL_STAT_ADD64(ctx, children, "mcast_frames",
	    &stats->rx_mcast, "Multicast frames");
	ET_SYSCTL_STAT_ADD64(ctx, children, "bcast_frames",
	    &stats->rx_bcast, "Broadcast frames");
	ET_SYSCTL_STAT_ADD32(ctx, children, "control",
	    &stats->rx_control, "Control frames");
	ET_SYSCTL_STAT_ADD32(ctx, children, "pause",
	    &stats->rx_pause, "Pause frames");
	ET_SYSCTL_STAT_ADD32(ctx, children, "unknown_control",
	    &stats->rx_unknown_control, "Unknown control frames");
	ET_SYSCTL_STAT_ADD32(ctx, children, "align_errs",
	    &stats->rx_alignerrs, "Alignment errors");
	ET_SYSCTL_STAT_ADD32(ctx, children, "len_errs",
	    &stats->rx_lenerrs, "Frames with length mismatched");
	ET_SYSCTL_STAT_ADD32(ctx, children, "code_errs",
	    &stats->rx_codeerrs, "Frames with code error");
	ET_SYSCTL_STAT_ADD32(ctx, children, "cs_errs",
	    &stats->rx_cserrs, "Frames with carrier sense error");
	ET_SYSCTL_STAT_ADD32(ctx, children, "runts",
	    &stats->rx_runts, "Too short frames");
	ET_SYSCTL_STAT_ADD64(ctx, children, "oversize",
	    &stats->rx_oversize, "Oversized frames");
	ET_SYSCTL_STAT_ADD32(ctx, children, "fragments",
	    &stats->rx_fragments, "Fragmented frames");
	ET_SYSCTL_STAT_ADD32(ctx, children, "jabbers",
	    &stats->rx_jabbers, "Frames with jabber error");
	ET_SYSCTL_STAT_ADD32(ctx, children, "drop",
	    &stats->rx_drop, "Dropped frames");

	/* TX statistics. */
	tree = SYSCTL_ADD_NODE(ctx, parent, OID_AUTO, "tx", CTLFLAG_RD,
	    NULL, "TX MAC statistics");
	children = SYSCTL_CHILDREN(tree);
	ET_SYSCTL_STAT_ADD64(ctx, children, "bytes",
	    &stats->tx_bytes, "Good bytes");
	ET_SYSCTL_STAT_ADD64(ctx, children, "frames",
	    &stats->tx_frames, "Good frames");
	ET_SYSCTL_STAT_ADD64(ctx, children, "mcast_frames",
	    &stats->tx_mcast, "Multicast frames");
	ET_SYSCTL_STAT_ADD64(ctx, children, "bcast_frames",
	    &stats->tx_bcast, "Broadcast frames");
	ET_SYSCTL_STAT_ADD32(ctx, children, "pause",
	    &stats->tx_pause, "Pause frames");
	ET_SYSCTL_STAT_ADD32(ctx, children, "deferred",
	    &stats->tx_deferred, "Deferred frames");
	ET_SYSCTL_STAT_ADD32(ctx, children, "excess_deferred",
	    &stats->tx_excess_deferred, "Excessively deferred frames");
	ET_SYSCTL_STAT_ADD32(ctx, children, "single_colls",
	    &stats->tx_single_colls, "Single collisions");
	ET_SYSCTL_STAT_ADD32(ctx, children, "multi_colls",
	    &stats->tx_multi_colls, "Multiple collisions");
	ET_SYSCTL_STAT_ADD32(ctx, children, "late_colls",
	    &stats->tx_late_colls, "Late collisions");
	ET_SYSCTL_STAT_ADD32(ctx, children, "excess_colls",
	    &stats->tx_excess_colls, "Excess collisions");
	ET_SYSCTL_STAT_ADD32(ctx, children, "total_colls",
	    &stats->tx_total_colls, "Total collisions");
	ET_SYSCTL_STAT_ADD32(ctx, children, "pause_honored",
	    &stats->tx_pause_honored, "Honored pause frames");
	ET_SYSCTL_STAT_ADD32(ctx, children, "drop",
	    &stats->tx_drop, "Dropped frames");
	ET_SYSCTL_STAT_ADD32(ctx, children, "jabbers",
	    &stats->tx_jabbers, "Frames with jabber errors");
	ET_SYSCTL_STAT_ADD32(ctx, children, "crc_errs",
	    &stats->tx_crcerrs, "Frames with CRC errors");
	ET_SYSCTL_STAT_ADD32(ctx, children, "control",
	    &stats->tx_control, "Control frames");
	ET_SYSCTL_STAT_ADD64(ctx, children, "oversize",
	    &stats->tx_oversize, "Oversized frames");
	ET_SYSCTL_STAT_ADD32(ctx, children, "undersize",
	    &stats->tx_undersize, "Undersized frames");
	ET_SYSCTL_STAT_ADD32(ctx, children, "fragments",
	    &stats->tx_fragments, "Fragmented frames");
}

#undef	ET_SYSCTL_STAT_ADD32
#undef	ET_SYSCTL_STAT_ADD64

static int
et_sysctl_rx_intr_npkts(SYSCTL_HANDLER_ARGS)
{
	struct et_softc *sc;
	struct ifnet *ifp;
	int error, v;

	sc = arg1;
	ifp = sc->ifp;
	v = sc->sc_rx_intr_npkts;
	error = sysctl_handle_int(oidp, &v, 0, req);
	if (error || req->newptr == NULL)
		goto back;
	if (v <= 0) {
		error = EINVAL;
		goto back;
	}

	if (sc->sc_rx_intr_npkts != v) {
		if (ifp->if_drv_flags & IFF_DRV_RUNNING)
			CSR_WRITE_4(sc, ET_RX_INTR_NPKTS, v);
		sc->sc_rx_intr_npkts = v;
	}
back:
	return (error);
}

static int
et_sysctl_rx_intr_delay(SYSCTL_HANDLER_ARGS)
{
	struct et_softc *sc;
	struct ifnet *ifp;
	int error, v;

	sc = arg1;
	ifp = sc->ifp;
	v = sc->sc_rx_intr_delay;
	error = sysctl_handle_int(oidp, &v, 0, req);
	if (error || req->newptr == NULL)
		goto back;
	if (v <= 0) {
		error = EINVAL;
		goto back;
	}

	if (sc->sc_rx_intr_delay != v) {
		if (ifp->if_drv_flags & IFF_DRV_RUNNING)
			CSR_WRITE_4(sc, ET_RX_INTR_DELAY, v);
		sc->sc_rx_intr_delay = v;
	}
back:
	return (error);
}

static void
et_stats_update(struct et_softc *sc)
{
	struct et_hw_stats *stats;

	stats = &sc->sc_stats;
	stats->pkts_64 += CSR_READ_4(sc, ET_STAT_PKTS_64);
	stats->pkts_65 += CSR_READ_4(sc, ET_STAT_PKTS_65_127);
	stats->pkts_128 += CSR_READ_4(sc, ET_STAT_PKTS_128_255);
	stats->pkts_256 += CSR_READ_4(sc, ET_STAT_PKTS_256_511);
	stats->pkts_512 += CSR_READ_4(sc, ET_STAT_PKTS_512_1023);
	stats->pkts_1024 += CSR_READ_4(sc, ET_STAT_PKTS_1024_1518);
	stats->pkts_1519 += CSR_READ_4(sc, ET_STAT_PKTS_1519_1522);

	stats->rx_bytes += CSR_READ_4(sc, ET_STAT_RX_BYTES);
	stats->rx_frames += CSR_READ_4(sc, ET_STAT_RX_FRAMES);
	stats->rx_crcerrs += CSR_READ_4(sc, ET_STAT_RX_CRC_ERR);
	stats->rx_mcast += CSR_READ_4(sc, ET_STAT_RX_MCAST);
	stats->rx_bcast += CSR_READ_4(sc, ET_STAT_RX_BCAST);
	stats->rx_control += CSR_READ_4(sc, ET_STAT_RX_CTL);
	stats->rx_pause += CSR_READ_4(sc, ET_STAT_RX_PAUSE);
	stats->rx_unknown_control += CSR_READ_4(sc, ET_STAT_RX_UNKNOWN_CTL);
	stats->rx_alignerrs += CSR_READ_4(sc, ET_STAT_RX_ALIGN_ERR);
	stats->rx_lenerrs += CSR_READ_4(sc, ET_STAT_RX_LEN_ERR);
	stats->rx_codeerrs += CSR_READ_4(sc, ET_STAT_RX_CODE_ERR);
	stats->rx_cserrs += CSR_READ_4(sc, ET_STAT_RX_CS_ERR);
	stats->rx_runts += CSR_READ_4(sc, ET_STAT_RX_RUNT);
	stats->rx_oversize += CSR_READ_4(sc, ET_STAT_RX_OVERSIZE);
	stats->rx_fragments += CSR_READ_4(sc, ET_STAT_RX_FRAG);
	stats->rx_jabbers += CSR_READ_4(sc, ET_STAT_RX_JABBER);
	stats->rx_drop += CSR_READ_4(sc, ET_STAT_RX_DROP);

	stats->tx_bytes += CSR_READ_4(sc, ET_STAT_TX_BYTES);
	stats->tx_frames += CSR_READ_4(sc, ET_STAT_TX_FRAMES);
	stats->tx_mcast += CSR_READ_4(sc, ET_STAT_TX_MCAST);
	stats->tx_bcast += CSR_READ_4(sc, ET_STAT_TX_BCAST);
	stats->tx_pause += CSR_READ_4(sc, ET_STAT_TX_PAUSE);
	stats->tx_deferred += CSR_READ_4(sc, ET_STAT_TX_DEFER);
	stats->tx_excess_deferred += CSR_READ_4(sc, ET_STAT_TX_EXCESS_DEFER);
	stats->tx_single_colls += CSR_READ_4(sc, ET_STAT_TX_SINGLE_COL);
	stats->tx_multi_colls += CSR_READ_4(sc, ET_STAT_TX_MULTI_COL);
	stats->tx_late_colls += CSR_READ_4(sc, ET_STAT_TX_LATE_COL);
	stats->tx_excess_colls += CSR_READ_4(sc, ET_STAT_TX_EXCESS_COL);
	stats->tx_total_colls += CSR_READ_4(sc, ET_STAT_TX_TOTAL_COL);
	stats->tx_pause_honored += CSR_READ_4(sc, ET_STAT_TX_PAUSE_HONOR);
	stats->tx_drop += CSR_READ_4(sc, ET_STAT_TX_DROP);
	stats->tx_jabbers += CSR_READ_4(sc, ET_STAT_TX_JABBER);
	stats->tx_crcerrs += CSR_READ_4(sc, ET_STAT_TX_CRC_ERR);
	stats->tx_control += CSR_READ_4(sc, ET_STAT_TX_CTL);
	stats->tx_oversize += CSR_READ_4(sc, ET_STAT_TX_OVERSIZE);
	stats->tx_undersize += CSR_READ_4(sc, ET_STAT_TX_UNDERSIZE);
	stats->tx_fragments += CSR_READ_4(sc, ET_STAT_TX_FRAG);
}

static uint64_t
et_get_counter(struct ifnet *ifp, ift_counter cnt)
{
	struct et_softc *sc;
	struct et_hw_stats *stats;

	sc = if_getsoftc(ifp);
	stats = &sc->sc_stats;

	switch (cnt) {
	case IFCOUNTER_OPACKETS:
		return (stats->tx_frames);
	case IFCOUNTER_COLLISIONS:
		return (stats->tx_total_colls);
	case IFCOUNTER_OERRORS:
		return (stats->tx_drop + stats->tx_jabbers +
		    stats->tx_crcerrs + stats->tx_excess_deferred +
		    stats->tx_late_colls);
	case IFCOUNTER_IPACKETS:
		return (stats->rx_frames);
	case IFCOUNTER_IERRORS:
		return (stats->rx_crcerrs + stats->rx_alignerrs +
		    stats->rx_lenerrs + stats->rx_codeerrs + stats->rx_cserrs +
		    stats->rx_runts + stats->rx_jabbers + stats->rx_drop);
	default:
		return (if_get_counter_default(ifp, cnt));
	}
}

static int
et_suspend(device_t dev)
{
	struct et_softc *sc;
	uint32_t pmcfg;

	sc = device_get_softc(dev);
	ET_LOCK(sc);
	if ((sc->ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
		et_stop(sc);
	/* Diable all clocks and put PHY into COMA. */
	pmcfg = CSR_READ_4(sc, ET_PM);
	pmcfg &= ~(EM_PM_GIGEPHY_ENB | ET_PM_SYSCLK_GATE | ET_PM_TXCLK_GATE |
	    ET_PM_RXCLK_GATE);
	pmcfg |= ET_PM_PHY_SW_COMA;
	CSR_WRITE_4(sc, ET_PM, pmcfg);
	ET_UNLOCK(sc);
	return (0);
}

static int
et_resume(device_t dev)
{
	struct et_softc *sc;
	uint32_t pmcfg;

	sc = device_get_softc(dev);
	ET_LOCK(sc);
	/* Take PHY out of COMA and enable clocks. */
	pmcfg = ET_PM_SYSCLK_GATE | ET_PM_TXCLK_GATE | ET_PM_RXCLK_GATE;
	if ((sc->sc_flags & ET_FLAG_FASTETHER) == 0)
		pmcfg |= EM_PM_GIGEPHY_ENB;
	CSR_WRITE_4(sc, ET_PM, pmcfg);
	if ((sc->ifp->if_flags & IFF_UP) != 0)
		et_init_locked(sc);
	ET_UNLOCK(sc);
	return (0);
}
