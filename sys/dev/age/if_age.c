/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008, Pyun YongHyeon <yongari@FreeBSD.org>
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

/* Driver for Attansic Technology Corp. L1 Gigabit Ethernet. */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/rman.h>
#include <sys/module.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>

#include <net/bpf.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/if_vlan_var.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <machine/bus.h>
#include <machine/in_cksum.h>

#include <dev/age/if_agereg.h>
#include <dev/age/if_agevar.h>

/* "device miibus" required.  See GENERIC if you get errors here. */
#include "miibus_if.h"

#define	AGE_CSUM_FEATURES	(CSUM_TCP | CSUM_UDP)

MODULE_DEPEND(age, pci, 1, 1, 1);
MODULE_DEPEND(age, ether, 1, 1, 1);
MODULE_DEPEND(age, miibus, 1, 1, 1);

/* Tunables. */
static int msi_disable = 0;
static int msix_disable = 0;
TUNABLE_INT("hw.age.msi_disable", &msi_disable);
TUNABLE_INT("hw.age.msix_disable", &msix_disable);

/*
 * Devices supported by this driver.
 */
static struct age_dev {
	uint16_t	age_vendorid;
	uint16_t	age_deviceid;
	const char	*age_name;
} age_devs[] = {
	{ VENDORID_ATTANSIC, DEVICEID_ATTANSIC_L1,
	    "Attansic Technology Corp, L1 Gigabit Ethernet" },
};

static int age_miibus_readreg(device_t, int, int);
static int age_miibus_writereg(device_t, int, int, int);
static void age_miibus_statchg(device_t);
static void age_mediastatus(struct ifnet *, struct ifmediareq *);
static int age_mediachange(struct ifnet *);
static int age_probe(device_t);
static void age_get_macaddr(struct age_softc *);
static void age_phy_reset(struct age_softc *);
static int age_attach(device_t);
static int age_detach(device_t);
static void age_sysctl_node(struct age_softc *);
static void age_dmamap_cb(void *, bus_dma_segment_t *, int, int);
static int age_check_boundary(struct age_softc *);
static int age_dma_alloc(struct age_softc *);
static void age_dma_free(struct age_softc *);
static int age_shutdown(device_t);
static void age_setwol(struct age_softc *);
static int age_suspend(device_t);
static int age_resume(device_t);
static int age_encap(struct age_softc *, struct mbuf **);
static void age_start(struct ifnet *);
static void age_start_locked(struct ifnet *);
static void age_watchdog(struct age_softc *);
static int age_ioctl(struct ifnet *, u_long, caddr_t);
static void age_mac_config(struct age_softc *);
static void age_link_task(void *, int);
static void age_stats_update(struct age_softc *);
static int age_intr(void *);
static void age_int_task(void *, int);
static void age_txintr(struct age_softc *, int);
static void age_rxeof(struct age_softc *sc, struct rx_rdesc *);
static int age_rxintr(struct age_softc *, int, int);
static void age_tick(void *);
static void age_reset(struct age_softc *);
static void age_init(void *);
static void age_init_locked(struct age_softc *);
static void age_stop(struct age_softc *);
static void age_stop_txmac(struct age_softc *);
static void age_stop_rxmac(struct age_softc *);
static void age_init_tx_ring(struct age_softc *);
static int age_init_rx_ring(struct age_softc *);
static void age_init_rr_ring(struct age_softc *);
static void age_init_cmb_block(struct age_softc *);
static void age_init_smb_block(struct age_softc *);
#ifndef __NO_STRICT_ALIGNMENT
static struct mbuf *age_fixup_rx(struct ifnet *, struct mbuf *);
#endif
static int age_newbuf(struct age_softc *, struct age_rxdesc *);
static void age_rxvlan(struct age_softc *);
static void age_rxfilter(struct age_softc *);
static int sysctl_age_stats(SYSCTL_HANDLER_ARGS);
static int sysctl_int_range(SYSCTL_HANDLER_ARGS, int, int);
static int sysctl_hw_age_proc_limit(SYSCTL_HANDLER_ARGS);
static int sysctl_hw_age_int_mod(SYSCTL_HANDLER_ARGS);


static device_method_t age_methods[] = {
	/* Device interface. */
	DEVMETHOD(device_probe,		age_probe),
	DEVMETHOD(device_attach,	age_attach),
	DEVMETHOD(device_detach,	age_detach),
	DEVMETHOD(device_shutdown,	age_shutdown),
	DEVMETHOD(device_suspend,	age_suspend),
	DEVMETHOD(device_resume,	age_resume),

	/* MII interface. */
	DEVMETHOD(miibus_readreg,	age_miibus_readreg),
	DEVMETHOD(miibus_writereg,	age_miibus_writereg),
	DEVMETHOD(miibus_statchg,	age_miibus_statchg),

	{ NULL, NULL }
};

static driver_t age_driver = {
	"age",
	age_methods,
	sizeof(struct age_softc)
};

static devclass_t age_devclass;

DRIVER_MODULE(age, pci, age_driver, age_devclass, 0, 0);
MODULE_PNP_INFO("U16:vendor;U16:device;D:#", pci, age, age_devs,
    nitems(age_devs));
DRIVER_MODULE(miibus, age, miibus_driver, miibus_devclass, 0, 0);

static struct resource_spec age_res_spec_mem[] = {
	{ SYS_RES_MEMORY,	PCIR_BAR(0),	RF_ACTIVE },
	{ -1,			0,		0 }
};

static struct resource_spec age_irq_spec_legacy[] = {
	{ SYS_RES_IRQ,		0,		RF_ACTIVE | RF_SHAREABLE },
	{ -1,			0,		0 }
};

static struct resource_spec age_irq_spec_msi[] = {
	{ SYS_RES_IRQ,		1,		RF_ACTIVE },
	{ -1,			0,		0 }
};

static struct resource_spec age_irq_spec_msix[] = {
	{ SYS_RES_IRQ,		1,		RF_ACTIVE },
	{ -1,			0,		0 }
};

/*
 *	Read a PHY register on the MII of the L1.
 */
static int
age_miibus_readreg(device_t dev, int phy, int reg)
{
	struct age_softc *sc;
	uint32_t v;
	int i;

	sc = device_get_softc(dev);

	CSR_WRITE_4(sc, AGE_MDIO, MDIO_OP_EXECUTE | MDIO_OP_READ |
	    MDIO_SUP_PREAMBLE | MDIO_CLK_25_4 | MDIO_REG_ADDR(reg));
	for (i = AGE_PHY_TIMEOUT; i > 0; i--) {
		DELAY(1);
		v = CSR_READ_4(sc, AGE_MDIO);
		if ((v & (MDIO_OP_EXECUTE | MDIO_OP_BUSY)) == 0)
			break;
	}

	if (i == 0) {
		device_printf(sc->age_dev, "phy read timeout : %d\n", reg);
		return (0);
	}

	return ((v & MDIO_DATA_MASK) >> MDIO_DATA_SHIFT);
}

/*
 *	Write a PHY register on the MII of the L1.
 */
static int
age_miibus_writereg(device_t dev, int phy, int reg, int val)
{
	struct age_softc *sc;
	uint32_t v;
	int i;

	sc = device_get_softc(dev);

	CSR_WRITE_4(sc, AGE_MDIO, MDIO_OP_EXECUTE | MDIO_OP_WRITE |
	    (val & MDIO_DATA_MASK) << MDIO_DATA_SHIFT |
	    MDIO_SUP_PREAMBLE | MDIO_CLK_25_4 | MDIO_REG_ADDR(reg));
	for (i = AGE_PHY_TIMEOUT; i > 0; i--) {
		DELAY(1);
		v = CSR_READ_4(sc, AGE_MDIO);
		if ((v & (MDIO_OP_EXECUTE | MDIO_OP_BUSY)) == 0)
			break;
	}

	if (i == 0)
		device_printf(sc->age_dev, "phy write timeout : %d\n", reg);

	return (0);
}

/*
 *	Callback from MII layer when media changes.
 */
static void
age_miibus_statchg(device_t dev)
{
	struct age_softc *sc;

	sc = device_get_softc(dev);
	taskqueue_enqueue(taskqueue_swi, &sc->age_link_task);
}

/*
 *	Get the current interface media status.
 */
static void
age_mediastatus(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct age_softc *sc;
	struct mii_data *mii;

	sc = ifp->if_softc;
	AGE_LOCK(sc);
	mii = device_get_softc(sc->age_miibus);

	mii_pollstat(mii);
	ifmr->ifm_status = mii->mii_media_status;
	ifmr->ifm_active = mii->mii_media_active;
	AGE_UNLOCK(sc);
}

/*
 *	Set hardware to newly-selected media.
 */
static int
age_mediachange(struct ifnet *ifp)
{
	struct age_softc *sc;
	struct mii_data *mii;
	struct mii_softc *miisc;
	int error;

	sc = ifp->if_softc;
	AGE_LOCK(sc);
	mii = device_get_softc(sc->age_miibus);
	LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
		PHY_RESET(miisc);
	error = mii_mediachg(mii);
	AGE_UNLOCK(sc);

	return (error);
}

static int
age_probe(device_t dev)
{
	struct age_dev *sp;
	int i;
	uint16_t vendor, devid;

	vendor = pci_get_vendor(dev);
	devid = pci_get_device(dev);
	sp = age_devs;
	for (i = 0; i < nitems(age_devs); i++, sp++) {
		if (vendor == sp->age_vendorid &&
		    devid == sp->age_deviceid) {
			device_set_desc(dev, sp->age_name);
			return (BUS_PROBE_DEFAULT);
		}
	}

	return (ENXIO);
}

static void
age_get_macaddr(struct age_softc *sc)
{
	uint32_t ea[2], reg;
	int i, vpdc;

	reg = CSR_READ_4(sc, AGE_SPI_CTRL);
	if ((reg & SPI_VPD_ENB) != 0) {
		/* Get VPD stored in TWSI EEPROM. */
		reg &= ~SPI_VPD_ENB;
		CSR_WRITE_4(sc, AGE_SPI_CTRL, reg);
	}

	if (pci_find_cap(sc->age_dev, PCIY_VPD, &vpdc) == 0) {
		/*
		 * PCI VPD capability found, let TWSI reload EEPROM.
		 * This will set ethernet address of controller.
		 */
		CSR_WRITE_4(sc, AGE_TWSI_CTRL, CSR_READ_4(sc, AGE_TWSI_CTRL) |
		    TWSI_CTRL_SW_LD_START);
		for (i = 100; i > 0; i--) {
			DELAY(1000);
			reg = CSR_READ_4(sc, AGE_TWSI_CTRL);
			if ((reg & TWSI_CTRL_SW_LD_START) == 0)
				break;
		}
		if (i == 0)
			device_printf(sc->age_dev,
			    "reloading EEPROM timeout!\n");
	} else {
		if (bootverbose)
			device_printf(sc->age_dev,
			    "PCI VPD capability not found!\n");
	}

	ea[0] = CSR_READ_4(sc, AGE_PAR0);
	ea[1] = CSR_READ_4(sc, AGE_PAR1);
	sc->age_eaddr[0] = (ea[1] >> 8) & 0xFF;
	sc->age_eaddr[1] = (ea[1] >> 0) & 0xFF;
	sc->age_eaddr[2] = (ea[0] >> 24) & 0xFF;
	sc->age_eaddr[3] = (ea[0] >> 16) & 0xFF;
	sc->age_eaddr[4] = (ea[0] >> 8) & 0xFF;
	sc->age_eaddr[5] = (ea[0] >> 0) & 0xFF;
}

static void
age_phy_reset(struct age_softc *sc)
{
	uint16_t reg, pn;
	int i, linkup;

	/* Reset PHY. */
	CSR_WRITE_4(sc, AGE_GPHY_CTRL, GPHY_CTRL_RST);
	DELAY(2000);
	CSR_WRITE_4(sc, AGE_GPHY_CTRL, GPHY_CTRL_CLR);
	DELAY(2000);

#define	ATPHY_DBG_ADDR		0x1D
#define	ATPHY_DBG_DATA		0x1E
#define	ATPHY_CDTC		0x16
#define	PHY_CDTC_ENB		0x0001
#define	PHY_CDTC_POFF		8
#define	ATPHY_CDTS		0x1C
#define	PHY_CDTS_STAT_OK	0x0000
#define	PHY_CDTS_STAT_SHORT	0x0100
#define	PHY_CDTS_STAT_OPEN	0x0200
#define	PHY_CDTS_STAT_INVAL	0x0300
#define	PHY_CDTS_STAT_MASK	0x0300

	/* Check power saving mode. Magic from Linux. */
	age_miibus_writereg(sc->age_dev, sc->age_phyaddr, MII_BMCR, BMCR_RESET);
	for (linkup = 0, pn = 0; pn < 4; pn++) {
		age_miibus_writereg(sc->age_dev, sc->age_phyaddr, ATPHY_CDTC,
		    (pn << PHY_CDTC_POFF) | PHY_CDTC_ENB);
		for (i = 200; i > 0; i--) {
			DELAY(1000);
			reg = age_miibus_readreg(sc->age_dev, sc->age_phyaddr,
			    ATPHY_CDTC);
			if ((reg & PHY_CDTC_ENB) == 0)
				break;
		}
		DELAY(1000);
		reg = age_miibus_readreg(sc->age_dev, sc->age_phyaddr,
		    ATPHY_CDTS);
		if ((reg & PHY_CDTS_STAT_MASK) != PHY_CDTS_STAT_OPEN) {
			linkup++;
			break;
		}
	}
	age_miibus_writereg(sc->age_dev, sc->age_phyaddr, MII_BMCR,
	    BMCR_RESET | BMCR_AUTOEN | BMCR_STARTNEG);
	if (linkup == 0) {
		age_miibus_writereg(sc->age_dev, sc->age_phyaddr,
		    ATPHY_DBG_ADDR, 0);
		age_miibus_writereg(sc->age_dev, sc->age_phyaddr,
		    ATPHY_DBG_DATA, 0x124E);
		age_miibus_writereg(sc->age_dev, sc->age_phyaddr,
		    ATPHY_DBG_ADDR, 1);
		reg = age_miibus_readreg(sc->age_dev, sc->age_phyaddr,
		    ATPHY_DBG_DATA);
		age_miibus_writereg(sc->age_dev, sc->age_phyaddr,
		    ATPHY_DBG_DATA, reg | 0x03);
		/* XXX */
		DELAY(1500 * 1000);
		age_miibus_writereg(sc->age_dev, sc->age_phyaddr,
		    ATPHY_DBG_ADDR, 0);
		age_miibus_writereg(sc->age_dev, sc->age_phyaddr,
		    ATPHY_DBG_DATA, 0x024E);
    }

#undef	ATPHY_DBG_ADDR
#undef	ATPHY_DBG_DATA
#undef	ATPHY_CDTC
#undef	PHY_CDTC_ENB
#undef	PHY_CDTC_POFF
#undef	ATPHY_CDTS
#undef	PHY_CDTS_STAT_OK
#undef	PHY_CDTS_STAT_SHORT
#undef	PHY_CDTS_STAT_OPEN
#undef	PHY_CDTS_STAT_INVAL
#undef	PHY_CDTS_STAT_MASK
}

static int
age_attach(device_t dev)
{
	struct age_softc *sc;
	struct ifnet *ifp;
	uint16_t burst;
	int error, i, msic, msixc, pmc;

	error = 0;
	sc = device_get_softc(dev);
	sc->age_dev = dev;

	mtx_init(&sc->age_mtx, device_get_nameunit(dev), MTX_NETWORK_LOCK,
	    MTX_DEF);
	callout_init_mtx(&sc->age_tick_ch, &sc->age_mtx, 0);
	TASK_INIT(&sc->age_int_task, 0, age_int_task, sc);
	TASK_INIT(&sc->age_link_task, 0, age_link_task, sc);

	/* Map the device. */
	pci_enable_busmaster(dev);
	sc->age_res_spec = age_res_spec_mem;
	sc->age_irq_spec = age_irq_spec_legacy;
	error = bus_alloc_resources(dev, sc->age_res_spec, sc->age_res);
	if (error != 0) {
		device_printf(dev, "cannot allocate memory resources.\n");
		goto fail;
	}

	/* Set PHY address. */
	sc->age_phyaddr = AGE_PHY_ADDR;

	/* Reset PHY. */
	age_phy_reset(sc);

	/* Reset the ethernet controller. */
	age_reset(sc);

	/* Get PCI and chip id/revision. */
	sc->age_rev = pci_get_revid(dev);
	sc->age_chip_rev = CSR_READ_4(sc, AGE_MASTER_CFG) >>
	    MASTER_CHIP_REV_SHIFT;
	if (bootverbose) {
		device_printf(dev, "PCI device revision : 0x%04x\n",
		    sc->age_rev);
		device_printf(dev, "Chip id/revision : 0x%04x\n",
		    sc->age_chip_rev);
	}

	/*
	 * XXX
	 * Unintialized hardware returns an invalid chip id/revision
	 * as well as 0xFFFFFFFF for Tx/Rx fifo length. It seems that
	 * unplugged cable results in putting hardware into automatic
	 * power down mode which in turn returns invalld chip revision.
	 */
	if (sc->age_chip_rev == 0xFFFF) {
		device_printf(dev,"invalid chip revision : 0x%04x -- "
		    "not initialized?\n", sc->age_chip_rev);
		error = ENXIO;
		goto fail;
	}

	device_printf(dev, "%d Tx FIFO, %d Rx FIFO\n",
	    CSR_READ_4(sc, AGE_SRAM_TX_FIFO_LEN),
	    CSR_READ_4(sc, AGE_SRAM_RX_FIFO_LEN));

	/* Allocate IRQ resources. */
	msixc = pci_msix_count(dev);
	msic = pci_msi_count(dev);
	if (bootverbose) {
		device_printf(dev, "MSIX count : %d\n", msixc);
		device_printf(dev, "MSI count : %d\n", msic);
	}

	/* Prefer MSIX over MSI. */
	if (msix_disable == 0 || msi_disable == 0) {
		if (msix_disable == 0 && msixc == AGE_MSIX_MESSAGES &&
		    pci_alloc_msix(dev, &msixc) == 0) {
			if (msic == AGE_MSIX_MESSAGES) {
				device_printf(dev, "Using %d MSIX messages.\n",
				    msixc);
				sc->age_flags |= AGE_FLAG_MSIX;
				sc->age_irq_spec = age_irq_spec_msix;
			} else
				pci_release_msi(dev);
		}
		if (msi_disable == 0 && (sc->age_flags & AGE_FLAG_MSIX) == 0 &&
		    msic == AGE_MSI_MESSAGES &&
		    pci_alloc_msi(dev, &msic) == 0) {
			if (msic == AGE_MSI_MESSAGES) {
				device_printf(dev, "Using %d MSI messages.\n",
				    msic);
				sc->age_flags |= AGE_FLAG_MSI;
				sc->age_irq_spec = age_irq_spec_msi;
			} else
				pci_release_msi(dev);
		}
	}

	error = bus_alloc_resources(dev, sc->age_irq_spec, sc->age_irq);
	if (error != 0) {
		device_printf(dev, "cannot allocate IRQ resources.\n");
		goto fail;
	}


	/* Get DMA parameters from PCIe device control register. */
	if (pci_find_cap(dev, PCIY_EXPRESS, &i) == 0) {
		sc->age_flags |= AGE_FLAG_PCIE;
		burst = pci_read_config(dev, i + 0x08, 2);
		/* Max read request size. */
		sc->age_dma_rd_burst = ((burst >> 12) & 0x07) <<
		    DMA_CFG_RD_BURST_SHIFT;
		/* Max payload size. */
		sc->age_dma_wr_burst = ((burst >> 5) & 0x07) <<
		    DMA_CFG_WR_BURST_SHIFT;
		if (bootverbose) {
			device_printf(dev, "Read request size : %d bytes.\n",
			    128 << ((burst >> 12) & 0x07));
			device_printf(dev, "TLP payload size : %d bytes.\n",
			    128 << ((burst >> 5) & 0x07));
		}
	} else {
		sc->age_dma_rd_burst = DMA_CFG_RD_BURST_128;
		sc->age_dma_wr_burst = DMA_CFG_WR_BURST_128;
	}

	/* Create device sysctl node. */
	age_sysctl_node(sc);

	if ((error = age_dma_alloc(sc)) != 0)
		goto fail;

	/* Load station address. */
	age_get_macaddr(sc);

	ifp = sc->age_ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(dev, "cannot allocate ifnet structure.\n");
		error = ENXIO;
		goto fail;
	}

	ifp->if_softc = sc;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = age_ioctl;
	ifp->if_start = age_start;
	ifp->if_init = age_init;
	ifp->if_snd.ifq_drv_maxlen = AGE_TX_RING_CNT - 1;
	IFQ_SET_MAXLEN(&ifp->if_snd, ifp->if_snd.ifq_drv_maxlen);
	IFQ_SET_READY(&ifp->if_snd);
	ifp->if_capabilities = IFCAP_HWCSUM | IFCAP_TSO4;
	ifp->if_hwassist = AGE_CSUM_FEATURES | CSUM_TSO;
	if (pci_find_cap(dev, PCIY_PMG, &pmc) == 0) {
		sc->age_flags |= AGE_FLAG_PMCAP;
		ifp->if_capabilities |= IFCAP_WOL_MAGIC | IFCAP_WOL_MCAST;
	}
	ifp->if_capenable = ifp->if_capabilities;

	/* Set up MII bus. */
	error = mii_attach(dev, &sc->age_miibus, ifp, age_mediachange,
	    age_mediastatus, BMSR_DEFCAPMASK, sc->age_phyaddr, MII_OFFSET_ANY,
	    0);
	if (error != 0) {
		device_printf(dev, "attaching PHYs failed\n");
		goto fail;
	}

	ether_ifattach(ifp, sc->age_eaddr);

	/* VLAN capability setup. */
	ifp->if_capabilities |= IFCAP_VLAN_MTU | IFCAP_VLAN_HWTAGGING |
	    IFCAP_VLAN_HWCSUM | IFCAP_VLAN_HWTSO;
	ifp->if_capenable = ifp->if_capabilities;

	/* Tell the upper layer(s) we support long frames. */
	ifp->if_hdrlen = sizeof(struct ether_vlan_header);

	/* Create local taskq. */
	sc->age_tq = taskqueue_create_fast("age_taskq", M_WAITOK,
	    taskqueue_thread_enqueue, &sc->age_tq);
	if (sc->age_tq == NULL) {
		device_printf(dev, "could not create taskqueue.\n");
		ether_ifdetach(ifp);
		error = ENXIO;
		goto fail;
	}
	taskqueue_start_threads(&sc->age_tq, 1, PI_NET, "%s taskq",
	    device_get_nameunit(sc->age_dev));

	if ((sc->age_flags & AGE_FLAG_MSIX) != 0)
		msic = AGE_MSIX_MESSAGES;
	else if ((sc->age_flags & AGE_FLAG_MSI) != 0)
		msic = AGE_MSI_MESSAGES;
	else
		msic = 1;
	for (i = 0; i < msic; i++) {
		error = bus_setup_intr(dev, sc->age_irq[i],
		    INTR_TYPE_NET | INTR_MPSAFE, age_intr, NULL, sc,
		    &sc->age_intrhand[i]);
		if (error != 0)
			break;
	}
	if (error != 0) {
		device_printf(dev, "could not set up interrupt handler.\n");
		taskqueue_free(sc->age_tq);
		sc->age_tq = NULL;
		ether_ifdetach(ifp);
		goto fail;
	}

fail:
	if (error != 0)
		age_detach(dev);

	return (error);
}

static int
age_detach(device_t dev)
{
	struct age_softc *sc;
	struct ifnet *ifp;
	int i, msic;

	sc = device_get_softc(dev);

	ifp = sc->age_ifp;
	if (device_is_attached(dev)) {
		AGE_LOCK(sc);
		sc->age_flags |= AGE_FLAG_DETACH;
		age_stop(sc);
		AGE_UNLOCK(sc);
		callout_drain(&sc->age_tick_ch);
		taskqueue_drain(sc->age_tq, &sc->age_int_task);
		taskqueue_drain(taskqueue_swi, &sc->age_link_task);
		ether_ifdetach(ifp);
	}

	if (sc->age_tq != NULL) {
		taskqueue_drain(sc->age_tq, &sc->age_int_task);
		taskqueue_free(sc->age_tq);
		sc->age_tq = NULL;
	}

	if (sc->age_miibus != NULL) {
		device_delete_child(dev, sc->age_miibus);
		sc->age_miibus = NULL;
	}
	bus_generic_detach(dev);
	age_dma_free(sc);

	if (ifp != NULL) {
		if_free(ifp);
		sc->age_ifp = NULL;
	}

	if ((sc->age_flags & AGE_FLAG_MSIX) != 0)
		msic = AGE_MSIX_MESSAGES;
	else if ((sc->age_flags & AGE_FLAG_MSI) != 0)
		msic = AGE_MSI_MESSAGES;
	else
		msic = 1;
	for (i = 0; i < msic; i++) {
		if (sc->age_intrhand[i] != NULL) {
			bus_teardown_intr(dev, sc->age_irq[i],
			    sc->age_intrhand[i]);
			sc->age_intrhand[i] = NULL;
		}
	}

	bus_release_resources(dev, sc->age_irq_spec, sc->age_irq);
	if ((sc->age_flags & (AGE_FLAG_MSI | AGE_FLAG_MSIX)) != 0)
		pci_release_msi(dev);
	bus_release_resources(dev, sc->age_res_spec, sc->age_res);
	mtx_destroy(&sc->age_mtx);

	return (0);
}

static void
age_sysctl_node(struct age_softc *sc)
{
	int error;

	SYSCTL_ADD_PROC(device_get_sysctl_ctx(sc->age_dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(sc->age_dev)), OID_AUTO,
	    "stats", CTLTYPE_INT | CTLFLAG_RW, sc, 0, sysctl_age_stats,
	    "I", "Statistics");

	SYSCTL_ADD_PROC(device_get_sysctl_ctx(sc->age_dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(sc->age_dev)), OID_AUTO,
	    "int_mod", CTLTYPE_INT | CTLFLAG_RW, &sc->age_int_mod, 0,
	    sysctl_hw_age_int_mod, "I", "age interrupt moderation");

	/* Pull in device tunables. */
	sc->age_int_mod = AGE_IM_TIMER_DEFAULT;
	error = resource_int_value(device_get_name(sc->age_dev),
	    device_get_unit(sc->age_dev), "int_mod", &sc->age_int_mod);
	if (error == 0) {
		if (sc->age_int_mod < AGE_IM_TIMER_MIN ||
		    sc->age_int_mod > AGE_IM_TIMER_MAX) {
			device_printf(sc->age_dev,
			    "int_mod value out of range; using default: %d\n",
			    AGE_IM_TIMER_DEFAULT);
			sc->age_int_mod = AGE_IM_TIMER_DEFAULT;
		}
	}

	SYSCTL_ADD_PROC(device_get_sysctl_ctx(sc->age_dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(sc->age_dev)), OID_AUTO,
	    "process_limit", CTLTYPE_INT | CTLFLAG_RW, &sc->age_process_limit,
	    0, sysctl_hw_age_proc_limit, "I",
	    "max number of Rx events to process");

	/* Pull in device tunables. */
	sc->age_process_limit = AGE_PROC_DEFAULT;
	error = resource_int_value(device_get_name(sc->age_dev),
	    device_get_unit(sc->age_dev), "process_limit",
	    &sc->age_process_limit);
	if (error == 0) {
		if (sc->age_process_limit < AGE_PROC_MIN ||
		    sc->age_process_limit > AGE_PROC_MAX) {
			device_printf(sc->age_dev,
			    "process_limit value out of range; "
			    "using default: %d\n", AGE_PROC_DEFAULT);
			sc->age_process_limit = AGE_PROC_DEFAULT;
		}
	}
}

struct age_dmamap_arg {
	bus_addr_t	age_busaddr;
};

static void
age_dmamap_cb(void *arg, bus_dma_segment_t *segs, int nsegs, int error)
{
	struct age_dmamap_arg *ctx;

	if (error != 0)
		return;

	KASSERT(nsegs == 1, ("%s: %d segments returned!", __func__, nsegs));

	ctx = (struct age_dmamap_arg *)arg;
	ctx->age_busaddr = segs[0].ds_addr;
}

/*
 * Attansic L1 controller have single register to specify high
 * address part of DMA blocks. So all descriptor structures and
 * DMA memory blocks should have the same high address of given
 * 4GB address space(i.e. crossing 4GB boundary is not allowed).
 */
static int
age_check_boundary(struct age_softc *sc)
{
	bus_addr_t rx_ring_end, rr_ring_end, tx_ring_end;
	bus_addr_t cmb_block_end, smb_block_end;

	/* Tx/Rx descriptor queue should reside within 4GB boundary. */
	tx_ring_end = sc->age_rdata.age_tx_ring_paddr + AGE_TX_RING_SZ;
	rx_ring_end = sc->age_rdata.age_rx_ring_paddr + AGE_RX_RING_SZ;
	rr_ring_end = sc->age_rdata.age_rr_ring_paddr + AGE_RR_RING_SZ;
	cmb_block_end = sc->age_rdata.age_cmb_block_paddr + AGE_CMB_BLOCK_SZ;
	smb_block_end = sc->age_rdata.age_smb_block_paddr + AGE_SMB_BLOCK_SZ;

	if ((AGE_ADDR_HI(tx_ring_end) !=
	    AGE_ADDR_HI(sc->age_rdata.age_tx_ring_paddr)) ||
	    (AGE_ADDR_HI(rx_ring_end) !=
	    AGE_ADDR_HI(sc->age_rdata.age_rx_ring_paddr)) ||
	    (AGE_ADDR_HI(rr_ring_end) !=
	    AGE_ADDR_HI(sc->age_rdata.age_rr_ring_paddr)) ||
	    (AGE_ADDR_HI(cmb_block_end) !=
	    AGE_ADDR_HI(sc->age_rdata.age_cmb_block_paddr)) ||
	    (AGE_ADDR_HI(smb_block_end) !=
	    AGE_ADDR_HI(sc->age_rdata.age_smb_block_paddr)))
		return (EFBIG);

	if ((AGE_ADDR_HI(tx_ring_end) != AGE_ADDR_HI(rx_ring_end)) ||
	    (AGE_ADDR_HI(tx_ring_end) != AGE_ADDR_HI(rr_ring_end)) ||
	    (AGE_ADDR_HI(tx_ring_end) != AGE_ADDR_HI(cmb_block_end)) ||
	    (AGE_ADDR_HI(tx_ring_end) != AGE_ADDR_HI(smb_block_end)))
		return (EFBIG);

	return (0);
}

static int
age_dma_alloc(struct age_softc *sc)
{
	struct age_txdesc *txd;
	struct age_rxdesc *rxd;
	bus_addr_t lowaddr;
	struct age_dmamap_arg ctx;
	int error, i;

	lowaddr = BUS_SPACE_MAXADDR;

again:
	/* Create parent ring/DMA block tag. */
	error = bus_dma_tag_create(
	    bus_get_dma_tag(sc->age_dev), /* parent */
	    1, 0,			/* alignment, boundary */
	    lowaddr,			/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    BUS_SPACE_MAXSIZE_32BIT,	/* maxsize */
	    0,				/* nsegments */
	    BUS_SPACE_MAXSIZE_32BIT,	/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->age_cdata.age_parent_tag);
	if (error != 0) {
		device_printf(sc->age_dev,
		    "could not create parent DMA tag.\n");
		goto fail;
	}

	/* Create tag for Tx ring. */
	error = bus_dma_tag_create(
	    sc->age_cdata.age_parent_tag, /* parent */
	    AGE_TX_RING_ALIGN, 0,	/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    AGE_TX_RING_SZ,		/* maxsize */
	    1,				/* nsegments */
	    AGE_TX_RING_SZ,		/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->age_cdata.age_tx_ring_tag);
	if (error != 0) {
		device_printf(sc->age_dev,
		    "could not create Tx ring DMA tag.\n");
		goto fail;
	}

	/* Create tag for Rx ring. */
	error = bus_dma_tag_create(
	    sc->age_cdata.age_parent_tag, /* parent */
	    AGE_RX_RING_ALIGN, 0,	/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    AGE_RX_RING_SZ,		/* maxsize */
	    1,				/* nsegments */
	    AGE_RX_RING_SZ,		/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->age_cdata.age_rx_ring_tag);
	if (error != 0) {
		device_printf(sc->age_dev,
		    "could not create Rx ring DMA tag.\n");
		goto fail;
	}

	/* Create tag for Rx return ring. */
	error = bus_dma_tag_create(
	    sc->age_cdata.age_parent_tag, /* parent */
	    AGE_RR_RING_ALIGN, 0,	/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    AGE_RR_RING_SZ,		/* maxsize */
	    1,				/* nsegments */
	    AGE_RR_RING_SZ,		/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->age_cdata.age_rr_ring_tag);
	if (error != 0) {
		device_printf(sc->age_dev,
		    "could not create Rx return ring DMA tag.\n");
		goto fail;
	}

	/* Create tag for coalesing message block. */
	error = bus_dma_tag_create(
	    sc->age_cdata.age_parent_tag, /* parent */
	    AGE_CMB_ALIGN, 0,		/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    AGE_CMB_BLOCK_SZ,		/* maxsize */
	    1,				/* nsegments */
	    AGE_CMB_BLOCK_SZ,		/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->age_cdata.age_cmb_block_tag);
	if (error != 0) {
		device_printf(sc->age_dev,
		    "could not create CMB DMA tag.\n");
		goto fail;
	}

	/* Create tag for statistics message block. */
	error = bus_dma_tag_create(
	    sc->age_cdata.age_parent_tag, /* parent */
	    AGE_SMB_ALIGN, 0,		/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    AGE_SMB_BLOCK_SZ,		/* maxsize */
	    1,				/* nsegments */
	    AGE_SMB_BLOCK_SZ,		/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->age_cdata.age_smb_block_tag);
	if (error != 0) {
		device_printf(sc->age_dev,
		    "could not create SMB DMA tag.\n");
		goto fail;
	}

	/* Allocate DMA'able memory and load the DMA map. */
	error = bus_dmamem_alloc(sc->age_cdata.age_tx_ring_tag,
	    (void **)&sc->age_rdata.age_tx_ring,
	    BUS_DMA_WAITOK | BUS_DMA_ZERO | BUS_DMA_COHERENT,
	    &sc->age_cdata.age_tx_ring_map);
	if (error != 0) {
		device_printf(sc->age_dev,
		    "could not allocate DMA'able memory for Tx ring.\n");
		goto fail;
	}
	ctx.age_busaddr = 0;
	error = bus_dmamap_load(sc->age_cdata.age_tx_ring_tag,
	    sc->age_cdata.age_tx_ring_map, sc->age_rdata.age_tx_ring,
	    AGE_TX_RING_SZ, age_dmamap_cb, &ctx, 0);
	if (error != 0 || ctx.age_busaddr == 0) {
		device_printf(sc->age_dev,
		    "could not load DMA'able memory for Tx ring.\n");
		goto fail;
	}
	sc->age_rdata.age_tx_ring_paddr = ctx.age_busaddr;
	/* Rx ring */
	error = bus_dmamem_alloc(sc->age_cdata.age_rx_ring_tag,
	    (void **)&sc->age_rdata.age_rx_ring,
	    BUS_DMA_WAITOK | BUS_DMA_ZERO | BUS_DMA_COHERENT,
	    &sc->age_cdata.age_rx_ring_map);
	if (error != 0) {
		device_printf(sc->age_dev,
		    "could not allocate DMA'able memory for Rx ring.\n");
		goto fail;
	}
	ctx.age_busaddr = 0;
	error = bus_dmamap_load(sc->age_cdata.age_rx_ring_tag,
	    sc->age_cdata.age_rx_ring_map, sc->age_rdata.age_rx_ring,
	    AGE_RX_RING_SZ, age_dmamap_cb, &ctx, 0);
	if (error != 0 || ctx.age_busaddr == 0) {
		device_printf(sc->age_dev,
		    "could not load DMA'able memory for Rx ring.\n");
		goto fail;
	}
	sc->age_rdata.age_rx_ring_paddr = ctx.age_busaddr;
	/* Rx return ring */
	error = bus_dmamem_alloc(sc->age_cdata.age_rr_ring_tag,
	    (void **)&sc->age_rdata.age_rr_ring,
	    BUS_DMA_WAITOK | BUS_DMA_ZERO | BUS_DMA_COHERENT,
	    &sc->age_cdata.age_rr_ring_map);
	if (error != 0) {
		device_printf(sc->age_dev,
		    "could not allocate DMA'able memory for Rx return ring.\n");
		goto fail;
	}
	ctx.age_busaddr = 0;
	error = bus_dmamap_load(sc->age_cdata.age_rr_ring_tag,
	    sc->age_cdata.age_rr_ring_map, sc->age_rdata.age_rr_ring,
	    AGE_RR_RING_SZ, age_dmamap_cb,
	    &ctx, 0);
	if (error != 0 || ctx.age_busaddr == 0) {
		device_printf(sc->age_dev,
		    "could not load DMA'able memory for Rx return ring.\n");
		goto fail;
	}
	sc->age_rdata.age_rr_ring_paddr = ctx.age_busaddr;
	/* CMB block */
	error = bus_dmamem_alloc(sc->age_cdata.age_cmb_block_tag,
	    (void **)&sc->age_rdata.age_cmb_block,
	    BUS_DMA_WAITOK | BUS_DMA_ZERO | BUS_DMA_COHERENT,
	    &sc->age_cdata.age_cmb_block_map);
	if (error != 0) {
		device_printf(sc->age_dev,
		    "could not allocate DMA'able memory for CMB block.\n");
		goto fail;
	}
	ctx.age_busaddr = 0;
	error = bus_dmamap_load(sc->age_cdata.age_cmb_block_tag,
	    sc->age_cdata.age_cmb_block_map, sc->age_rdata.age_cmb_block,
	    AGE_CMB_BLOCK_SZ, age_dmamap_cb, &ctx, 0);
	if (error != 0 || ctx.age_busaddr == 0) {
		device_printf(sc->age_dev,
		    "could not load DMA'able memory for CMB block.\n");
		goto fail;
	}
	sc->age_rdata.age_cmb_block_paddr = ctx.age_busaddr;
	/* SMB block */
	error = bus_dmamem_alloc(sc->age_cdata.age_smb_block_tag,
	    (void **)&sc->age_rdata.age_smb_block,
	    BUS_DMA_WAITOK | BUS_DMA_ZERO | BUS_DMA_COHERENT,
	    &sc->age_cdata.age_smb_block_map);
	if (error != 0) {
		device_printf(sc->age_dev,
		    "could not allocate DMA'able memory for SMB block.\n");
		goto fail;
	}
	ctx.age_busaddr = 0;
	error = bus_dmamap_load(sc->age_cdata.age_smb_block_tag,
	    sc->age_cdata.age_smb_block_map, sc->age_rdata.age_smb_block,
	    AGE_SMB_BLOCK_SZ, age_dmamap_cb, &ctx, 0);
	if (error != 0 || ctx.age_busaddr == 0) {
		device_printf(sc->age_dev,
		    "could not load DMA'able memory for SMB block.\n");
		goto fail;
	}
	sc->age_rdata.age_smb_block_paddr = ctx.age_busaddr;

	/*
	 * All ring buffer and DMA blocks should have the same
	 * high address part of 64bit DMA address space.
	 */
	if (lowaddr != BUS_SPACE_MAXADDR_32BIT &&
	    (error = age_check_boundary(sc)) != 0) {
		device_printf(sc->age_dev, "4GB boundary crossed, "
		    "switching to 32bit DMA addressing mode.\n");
		age_dma_free(sc);
		/* Limit DMA address space to 32bit and try again. */
		lowaddr = BUS_SPACE_MAXADDR_32BIT;
		goto again;
	}

	/*
	 * Create Tx/Rx buffer parent tag.
	 * L1 supports full 64bit DMA addressing in Tx/Rx buffers
	 * so it needs separate parent DMA tag.
	 * XXX
	 * It seems enabling 64bit DMA causes data corruption. Limit
	 * DMA address space to 32bit.
	 */
	error = bus_dma_tag_create(
	    bus_get_dma_tag(sc->age_dev), /* parent */
	    1, 0,			/* alignment, boundary */
	    BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    BUS_SPACE_MAXSIZE_32BIT,	/* maxsize */
	    0,				/* nsegments */
	    BUS_SPACE_MAXSIZE_32BIT,	/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->age_cdata.age_buffer_tag);
	if (error != 0) {
		device_printf(sc->age_dev,
		    "could not create parent buffer DMA tag.\n");
		goto fail;
	}

	/* Create tag for Tx buffers. */
	error = bus_dma_tag_create(
	    sc->age_cdata.age_buffer_tag, /* parent */
	    1, 0,			/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    AGE_TSO_MAXSIZE,		/* maxsize */
	    AGE_MAXTXSEGS,		/* nsegments */
	    AGE_TSO_MAXSEGSIZE,		/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->age_cdata.age_tx_tag);
	if (error != 0) {
		device_printf(sc->age_dev, "could not create Tx DMA tag.\n");
		goto fail;
	}

	/* Create tag for Rx buffers. */
	error = bus_dma_tag_create(
	    sc->age_cdata.age_buffer_tag, /* parent */
	    AGE_RX_BUF_ALIGN, 0,	/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    MCLBYTES,			/* maxsize */
	    1,				/* nsegments */
	    MCLBYTES,			/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->age_cdata.age_rx_tag);
	if (error != 0) {
		device_printf(sc->age_dev, "could not create Rx DMA tag.\n");
		goto fail;
	}

	/* Create DMA maps for Tx buffers. */
	for (i = 0; i < AGE_TX_RING_CNT; i++) {
		txd = &sc->age_cdata.age_txdesc[i];
		txd->tx_m = NULL;
		txd->tx_dmamap = NULL;
		error = bus_dmamap_create(sc->age_cdata.age_tx_tag, 0,
		    &txd->tx_dmamap);
		if (error != 0) {
			device_printf(sc->age_dev,
			    "could not create Tx dmamap.\n");
			goto fail;
		}
	}
	/* Create DMA maps for Rx buffers. */
	if ((error = bus_dmamap_create(sc->age_cdata.age_rx_tag, 0,
	    &sc->age_cdata.age_rx_sparemap)) != 0) {
		device_printf(sc->age_dev,
		    "could not create spare Rx dmamap.\n");
		goto fail;
	}
	for (i = 0; i < AGE_RX_RING_CNT; i++) {
		rxd = &sc->age_cdata.age_rxdesc[i];
		rxd->rx_m = NULL;
		rxd->rx_dmamap = NULL;
		error = bus_dmamap_create(sc->age_cdata.age_rx_tag, 0,
		    &rxd->rx_dmamap);
		if (error != 0) {
			device_printf(sc->age_dev,
			    "could not create Rx dmamap.\n");
			goto fail;
		}
	}

fail:
	return (error);
}

static void
age_dma_free(struct age_softc *sc)
{
	struct age_txdesc *txd;
	struct age_rxdesc *rxd;
	int i;

	/* Tx buffers */
	if (sc->age_cdata.age_tx_tag != NULL) {
		for (i = 0; i < AGE_TX_RING_CNT; i++) {
			txd = &sc->age_cdata.age_txdesc[i];
			if (txd->tx_dmamap != NULL) {
				bus_dmamap_destroy(sc->age_cdata.age_tx_tag,
				    txd->tx_dmamap);
				txd->tx_dmamap = NULL;
			}
		}
		bus_dma_tag_destroy(sc->age_cdata.age_tx_tag);
		sc->age_cdata.age_tx_tag = NULL;
	}
	/* Rx buffers */
	if (sc->age_cdata.age_rx_tag != NULL) {
		for (i = 0; i < AGE_RX_RING_CNT; i++) {
			rxd = &sc->age_cdata.age_rxdesc[i];
			if (rxd->rx_dmamap != NULL) {
				bus_dmamap_destroy(sc->age_cdata.age_rx_tag,
				    rxd->rx_dmamap);
				rxd->rx_dmamap = NULL;
			}
		}
		if (sc->age_cdata.age_rx_sparemap != NULL) {
			bus_dmamap_destroy(sc->age_cdata.age_rx_tag,
			    sc->age_cdata.age_rx_sparemap);
			sc->age_cdata.age_rx_sparemap = NULL;
		}
		bus_dma_tag_destroy(sc->age_cdata.age_rx_tag);
		sc->age_cdata.age_rx_tag = NULL;
	}
	/* Tx ring. */
	if (sc->age_cdata.age_tx_ring_tag != NULL) {
		if (sc->age_rdata.age_tx_ring_paddr != 0)
			bus_dmamap_unload(sc->age_cdata.age_tx_ring_tag,
			    sc->age_cdata.age_tx_ring_map);
		if (sc->age_rdata.age_tx_ring != NULL)
			bus_dmamem_free(sc->age_cdata.age_tx_ring_tag,
			    sc->age_rdata.age_tx_ring,
			    sc->age_cdata.age_tx_ring_map);
		sc->age_rdata.age_tx_ring_paddr = 0;
		sc->age_rdata.age_tx_ring = NULL;
		bus_dma_tag_destroy(sc->age_cdata.age_tx_ring_tag);
		sc->age_cdata.age_tx_ring_tag = NULL;
	}
	/* Rx ring. */
	if (sc->age_cdata.age_rx_ring_tag != NULL) {
		if (sc->age_rdata.age_rx_ring_paddr != 0)
			bus_dmamap_unload(sc->age_cdata.age_rx_ring_tag,
			    sc->age_cdata.age_rx_ring_map);
		if (sc->age_rdata.age_rx_ring != NULL)
			bus_dmamem_free(sc->age_cdata.age_rx_ring_tag,
			    sc->age_rdata.age_rx_ring,
			    sc->age_cdata.age_rx_ring_map);
		sc->age_rdata.age_rx_ring_paddr = 0;
		sc->age_rdata.age_rx_ring = NULL;
		bus_dma_tag_destroy(sc->age_cdata.age_rx_ring_tag);
		sc->age_cdata.age_rx_ring_tag = NULL;
	}
	/* Rx return ring. */
	if (sc->age_cdata.age_rr_ring_tag != NULL) {
		if (sc->age_rdata.age_rr_ring_paddr != 0)
			bus_dmamap_unload(sc->age_cdata.age_rr_ring_tag,
			    sc->age_cdata.age_rr_ring_map);
		if (sc->age_rdata.age_rr_ring != NULL)
			bus_dmamem_free(sc->age_cdata.age_rr_ring_tag,
			    sc->age_rdata.age_rr_ring,
			    sc->age_cdata.age_rr_ring_map);
		sc->age_rdata.age_rr_ring_paddr = 0;
		sc->age_rdata.age_rr_ring = NULL;
		bus_dma_tag_destroy(sc->age_cdata.age_rr_ring_tag);
		sc->age_cdata.age_rr_ring_tag = NULL;
	}
	/* CMB block */
	if (sc->age_cdata.age_cmb_block_tag != NULL) {
		if (sc->age_rdata.age_cmb_block_paddr != 0)
			bus_dmamap_unload(sc->age_cdata.age_cmb_block_tag,
			    sc->age_cdata.age_cmb_block_map);
		if (sc->age_rdata.age_cmb_block != NULL)
			bus_dmamem_free(sc->age_cdata.age_cmb_block_tag,
			    sc->age_rdata.age_cmb_block,
			    sc->age_cdata.age_cmb_block_map);
		sc->age_rdata.age_cmb_block_paddr = 0;
		sc->age_rdata.age_cmb_block = NULL;
		bus_dma_tag_destroy(sc->age_cdata.age_cmb_block_tag);
		sc->age_cdata.age_cmb_block_tag = NULL;
	}
	/* SMB block */
	if (sc->age_cdata.age_smb_block_tag != NULL) {
		if (sc->age_rdata.age_smb_block_paddr != 0)
			bus_dmamap_unload(sc->age_cdata.age_smb_block_tag,
			    sc->age_cdata.age_smb_block_map);
		if (sc->age_rdata.age_smb_block != NULL)
			bus_dmamem_free(sc->age_cdata.age_smb_block_tag,
			    sc->age_rdata.age_smb_block,
			    sc->age_cdata.age_smb_block_map);
		sc->age_rdata.age_smb_block_paddr = 0;
		sc->age_rdata.age_smb_block = NULL;
		bus_dma_tag_destroy(sc->age_cdata.age_smb_block_tag);
		sc->age_cdata.age_smb_block_tag = NULL;
	}

	if (sc->age_cdata.age_buffer_tag != NULL) {
		bus_dma_tag_destroy(sc->age_cdata.age_buffer_tag);
		sc->age_cdata.age_buffer_tag = NULL;
	}
	if (sc->age_cdata.age_parent_tag != NULL) {
		bus_dma_tag_destroy(sc->age_cdata.age_parent_tag);
		sc->age_cdata.age_parent_tag = NULL;
	}
}

/*
 *	Make sure the interface is stopped at reboot time.
 */
static int
age_shutdown(device_t dev)
{

	return (age_suspend(dev));
}

static void
age_setwol(struct age_softc *sc)
{
	struct ifnet *ifp;
	struct mii_data *mii;
	uint32_t reg, pmcs;
	uint16_t pmstat;
	int aneg, i, pmc;

	AGE_LOCK_ASSERT(sc);

	if (pci_find_cap(sc->age_dev, PCIY_PMG, &pmc) != 0) {
		CSR_WRITE_4(sc, AGE_WOL_CFG, 0);
		/*
		 * No PME capability, PHY power down.
		 * XXX
		 * Due to an unknown reason powering down PHY resulted
		 * in unexpected results such as inaccessbility of
		 * hardware of freshly rebooted system. Disable
		 * powering down PHY until I got more information for
		 * Attansic/Atheros PHY hardwares.
		 */
#ifdef notyet
		age_miibus_writereg(sc->age_dev, sc->age_phyaddr,
		    MII_BMCR, BMCR_PDOWN);
#endif
		return;
	}

	ifp = sc->age_ifp;
	if ((ifp->if_capenable & IFCAP_WOL) != 0) {
		/*
		 * Note, this driver resets the link speed to 10/100Mbps with
		 * auto-negotiation but we don't know whether that operation
		 * would succeed or not as it have no control after powering
		 * off. If the renegotiation fail WOL may not work. Running
		 * at 1Gbps will draw more power than 375mA at 3.3V which is
		 * specified in PCI specification and that would result in
		 * complete shutdowning power to ethernet controller.
		 *
		 * TODO
		 *  Save current negotiated media speed/duplex/flow-control
		 *  to softc and restore the same link again after resuming.
		 *  PHY handling such as power down/resetting to 100Mbps
		 *  may be better handled in suspend method in phy driver.
		 */
		mii = device_get_softc(sc->age_miibus);
		mii_pollstat(mii);
		aneg = 0;
		if ((mii->mii_media_status & IFM_AVALID) != 0) {
			switch IFM_SUBTYPE(mii->mii_media_active) {
			case IFM_10_T:
			case IFM_100_TX:
				goto got_link;
			case IFM_1000_T:
				aneg++;
			default:
				break;
			}
		}
		age_miibus_writereg(sc->age_dev, sc->age_phyaddr,
		    MII_100T2CR, 0);
		age_miibus_writereg(sc->age_dev, sc->age_phyaddr,
		    MII_ANAR, ANAR_TX_FD | ANAR_TX | ANAR_10_FD |
		    ANAR_10 | ANAR_CSMA);
		age_miibus_writereg(sc->age_dev, sc->age_phyaddr,
		    MII_BMCR, BMCR_RESET | BMCR_AUTOEN | BMCR_STARTNEG);
		DELAY(1000);
		if (aneg != 0) {
			/* Poll link state until age(4) get a 10/100 link. */
			for (i = 0; i < MII_ANEGTICKS_GIGE; i++) {
				mii_pollstat(mii);
				if ((mii->mii_media_status & IFM_AVALID) != 0) {
					switch (IFM_SUBTYPE(
					    mii->mii_media_active)) {
					case IFM_10_T:
					case IFM_100_TX:
						age_mac_config(sc);
						goto got_link;
					default:
						break;
					}
				}
				AGE_UNLOCK(sc);
				pause("agelnk", hz);
				AGE_LOCK(sc);
			}
			if (i == MII_ANEGTICKS_GIGE)
				device_printf(sc->age_dev,
				    "establishing link failed, "
				    "WOL may not work!");
		}
		/*
		 * No link, force MAC to have 100Mbps, full-duplex link.
		 * This is the last resort and may/may not work.
		 */
		mii->mii_media_status = IFM_AVALID | IFM_ACTIVE;
		mii->mii_media_active = IFM_ETHER | IFM_100_TX | IFM_FDX;
		age_mac_config(sc);
	}

got_link:
	pmcs = 0;
	if ((ifp->if_capenable & IFCAP_WOL_MAGIC) != 0)
		pmcs |= WOL_CFG_MAGIC | WOL_CFG_MAGIC_ENB;
	CSR_WRITE_4(sc, AGE_WOL_CFG, pmcs);
	reg = CSR_READ_4(sc, AGE_MAC_CFG);
	reg &= ~(MAC_CFG_DBG | MAC_CFG_PROMISC);
	reg &= ~(MAC_CFG_ALLMULTI | MAC_CFG_BCAST);
	if ((ifp->if_capenable & IFCAP_WOL_MCAST) != 0)
		reg |= MAC_CFG_ALLMULTI | MAC_CFG_BCAST;
	if ((ifp->if_capenable & IFCAP_WOL) != 0) {
		reg |= MAC_CFG_RX_ENB;
		CSR_WRITE_4(sc, AGE_MAC_CFG, reg);
	}

	/* Request PME. */
	pmstat = pci_read_config(sc->age_dev, pmc + PCIR_POWER_STATUS, 2);
	pmstat &= ~(PCIM_PSTAT_PME | PCIM_PSTAT_PMEENABLE);
	if ((ifp->if_capenable & IFCAP_WOL) != 0)
		pmstat |= PCIM_PSTAT_PME | PCIM_PSTAT_PMEENABLE;
	pci_write_config(sc->age_dev, pmc + PCIR_POWER_STATUS, pmstat, 2);
#ifdef notyet
	/* See above for powering down PHY issues. */
	if ((ifp->if_capenable & IFCAP_WOL) == 0) {
		/* No WOL, PHY power down. */
		age_miibus_writereg(sc->age_dev, sc->age_phyaddr,
		    MII_BMCR, BMCR_PDOWN);
	}
#endif
}

static int
age_suspend(device_t dev)
{
	struct age_softc *sc;

	sc = device_get_softc(dev);

	AGE_LOCK(sc);
	age_stop(sc);
	age_setwol(sc);
	AGE_UNLOCK(sc);

	return (0);
}

static int
age_resume(device_t dev)
{
	struct age_softc *sc;
	struct ifnet *ifp;

	sc = device_get_softc(dev);

	AGE_LOCK(sc);
	age_phy_reset(sc);
	ifp = sc->age_ifp;
	if ((ifp->if_flags & IFF_UP) != 0)
		age_init_locked(sc);

	AGE_UNLOCK(sc);

	return (0);
}

static int
age_encap(struct age_softc *sc, struct mbuf **m_head)
{
	struct age_txdesc *txd, *txd_last;
	struct tx_desc *desc;
	struct mbuf *m;
	struct ip *ip;
	struct tcphdr *tcp;
	bus_dma_segment_t txsegs[AGE_MAXTXSEGS];
	bus_dmamap_t map;
	uint32_t cflags, hdrlen, ip_off, poff, vtag;
	int error, i, nsegs, prod, si;

	AGE_LOCK_ASSERT(sc);

	M_ASSERTPKTHDR((*m_head));

	m = *m_head;
	ip = NULL;
	tcp = NULL;
	cflags = vtag = 0;
	ip_off = poff = 0;
	if ((m->m_pkthdr.csum_flags & (AGE_CSUM_FEATURES | CSUM_TSO)) != 0) {
		/*
		 * L1 requires offset of TCP/UDP payload in its Tx
		 * descriptor to perform hardware Tx checksum offload.
		 * Additionally, TSO requires IP/TCP header size and
		 * modification of IP/TCP header in order to make TSO
		 * engine work. This kind of operation takes many CPU
		 * cycles on FreeBSD so fast host CPU is needed to get
		 * smooth TSO performance.
		 */
		struct ether_header *eh;

		if (M_WRITABLE(m) == 0) {
			/* Get a writable copy. */
			m = m_dup(*m_head, M_NOWAIT);
			/* Release original mbufs. */
			m_freem(*m_head);
			if (m == NULL) {
				*m_head = NULL;
				return (ENOBUFS);
			}
			*m_head = m;
		}
		ip_off = sizeof(struct ether_header);
		m = m_pullup(m, ip_off);
		if (m == NULL) {
			*m_head = NULL;
			return (ENOBUFS);
		}
		eh = mtod(m, struct ether_header *);
		/*
		 * Check if hardware VLAN insertion is off.
		 * Additional check for LLC/SNAP frame?
		 */
		if (eh->ether_type == htons(ETHERTYPE_VLAN)) {
			ip_off = sizeof(struct ether_vlan_header);
			m = m_pullup(m, ip_off);
			if (m == NULL) {
				*m_head = NULL;
				return (ENOBUFS);
			}
		}
		m = m_pullup(m, ip_off + sizeof(struct ip));
		if (m == NULL) {
			*m_head = NULL;
			return (ENOBUFS);
		}
		ip = (struct ip *)(mtod(m, char *) + ip_off);
		poff = ip_off + (ip->ip_hl << 2);
		if ((m->m_pkthdr.csum_flags & CSUM_TSO) != 0) {
			m = m_pullup(m, poff + sizeof(struct tcphdr));
			if (m == NULL) {
				*m_head = NULL;
				return (ENOBUFS);
			}
			tcp = (struct tcphdr *)(mtod(m, char *) + poff);
			m = m_pullup(m, poff + (tcp->th_off << 2));
			if (m == NULL) {
				*m_head = NULL;
				return (ENOBUFS);
			}
			/*
			 * L1 requires IP/TCP header size and offset as
			 * well as TCP pseudo checksum which complicates
			 * TSO configuration. I guess this comes from the
			 * adherence to Microsoft NDIS Large Send
			 * specification which requires insertion of
			 * pseudo checksum by upper stack. The pseudo
			 * checksum that NDIS refers to doesn't include
			 * TCP payload length so age(4) should recompute
			 * the pseudo checksum here. Hopefully this wouldn't
			 * be much burden on modern CPUs.
			 * Reset IP checksum and recompute TCP pseudo
			 * checksum as NDIS specification said.
			 */
			ip = (struct ip *)(mtod(m, char *) + ip_off);
			tcp = (struct tcphdr *)(mtod(m, char *) + poff);
			ip->ip_sum = 0;
			tcp->th_sum = in_pseudo(ip->ip_src.s_addr,
			    ip->ip_dst.s_addr, htons(IPPROTO_TCP));
		}
		*m_head = m;
	}

	si = prod = sc->age_cdata.age_tx_prod;
	txd = &sc->age_cdata.age_txdesc[prod];
	txd_last = txd;
	map = txd->tx_dmamap;

	error =  bus_dmamap_load_mbuf_sg(sc->age_cdata.age_tx_tag, map,
	    *m_head, txsegs, &nsegs, 0);
	if (error == EFBIG) {
		m = m_collapse(*m_head, M_NOWAIT, AGE_MAXTXSEGS);
		if (m == NULL) {
			m_freem(*m_head);
			*m_head = NULL;
			return (ENOMEM);
		}
		*m_head = m;
		error = bus_dmamap_load_mbuf_sg(sc->age_cdata.age_tx_tag, map,
		    *m_head, txsegs, &nsegs, 0);
		if (error != 0) {
			m_freem(*m_head);
			*m_head = NULL;
			return (error);
		}
	} else if (error != 0)
		return (error);
	if (nsegs == 0) {
		m_freem(*m_head);
		*m_head = NULL;
		return (EIO);
	}

	/* Check descriptor overrun. */
	if (sc->age_cdata.age_tx_cnt + nsegs >= AGE_TX_RING_CNT - 2) {
		bus_dmamap_unload(sc->age_cdata.age_tx_tag, map);
		return (ENOBUFS);
	}

	m = *m_head;
	/* Configure VLAN hardware tag insertion. */
	if ((m->m_flags & M_VLANTAG) != 0) {
		vtag = AGE_TX_VLAN_TAG(m->m_pkthdr.ether_vtag);
		vtag = ((vtag << AGE_TD_VLAN_SHIFT) & AGE_TD_VLAN_MASK);
		cflags |= AGE_TD_INSERT_VLAN_TAG;
	}

	desc = NULL;
	i = 0;
	if ((m->m_pkthdr.csum_flags & CSUM_TSO) != 0) {
		/* Request TSO and set MSS. */
		cflags |= AGE_TD_TSO_IPV4;
		cflags |= AGE_TD_IPCSUM | AGE_TD_TCPCSUM;
		cflags |= ((uint32_t)m->m_pkthdr.tso_segsz <<
		    AGE_TD_TSO_MSS_SHIFT);
		/* Set IP/TCP header size. */
		cflags |= ip->ip_hl << AGE_TD_IPHDR_LEN_SHIFT;
		cflags |= tcp->th_off << AGE_TD_TSO_TCPHDR_LEN_SHIFT;
		/*
		 * L1 requires the first buffer should only hold IP/TCP
		 * header data. TCP payload should be handled in other
		 * descriptors.
		 */
		hdrlen = poff + (tcp->th_off << 2);
		desc = &sc->age_rdata.age_tx_ring[prod];
		desc->addr = htole64(txsegs[0].ds_addr);
		desc->len = htole32(AGE_TX_BYTES(hdrlen) | vtag);
		desc->flags = htole32(cflags);
		sc->age_cdata.age_tx_cnt++;
		AGE_DESC_INC(prod, AGE_TX_RING_CNT);
		if (m->m_len - hdrlen > 0) {
			/* Handle remaining payload of the 1st fragment. */
			desc = &sc->age_rdata.age_tx_ring[prod];
			desc->addr = htole64(txsegs[0].ds_addr + hdrlen);
			desc->len = htole32(AGE_TX_BYTES(m->m_len - hdrlen) |
			    vtag);
			desc->flags = htole32(cflags);
			sc->age_cdata.age_tx_cnt++;
			AGE_DESC_INC(prod, AGE_TX_RING_CNT);
		}
		/* Handle remaining fragments. */
		i = 1;
	} else if ((m->m_pkthdr.csum_flags & AGE_CSUM_FEATURES) != 0) {
		/* Configure Tx IP/TCP/UDP checksum offload. */
		cflags |= AGE_TD_CSUM;
		if ((m->m_pkthdr.csum_flags & CSUM_TCP) != 0)
			cflags |= AGE_TD_TCPCSUM;
		if ((m->m_pkthdr.csum_flags & CSUM_UDP) != 0)
			cflags |= AGE_TD_UDPCSUM;
		/* Set checksum start offset. */
		cflags |= (poff << AGE_TD_CSUM_PLOADOFFSET_SHIFT);
		/* Set checksum insertion position of TCP/UDP. */
		cflags |= ((poff + m->m_pkthdr.csum_data) <<
		    AGE_TD_CSUM_XSUMOFFSET_SHIFT);
	}
	for (; i < nsegs; i++) {
		desc = &sc->age_rdata.age_tx_ring[prod];
		desc->addr = htole64(txsegs[i].ds_addr);
		desc->len = htole32(AGE_TX_BYTES(txsegs[i].ds_len) | vtag);
		desc->flags = htole32(cflags);
		sc->age_cdata.age_tx_cnt++;
		AGE_DESC_INC(prod, AGE_TX_RING_CNT);
	}
	/* Update producer index. */
	sc->age_cdata.age_tx_prod = prod;

	/* Set EOP on the last descriptor. */
	prod = (prod + AGE_TX_RING_CNT - 1) % AGE_TX_RING_CNT;
	desc = &sc->age_rdata.age_tx_ring[prod];
	desc->flags |= htole32(AGE_TD_EOP);

	/* Lastly set TSO header and modify IP/TCP header for TSO operation. */
	if ((m->m_pkthdr.csum_flags & CSUM_TSO) != 0) {
		desc = &sc->age_rdata.age_tx_ring[si];
		desc->flags |= htole32(AGE_TD_TSO_HDR);
	}

	/* Swap dmamap of the first and the last. */
	txd = &sc->age_cdata.age_txdesc[prod];
	map = txd_last->tx_dmamap;
	txd_last->tx_dmamap = txd->tx_dmamap;
	txd->tx_dmamap = map;
	txd->tx_m = m;

	/* Sync descriptors. */
	bus_dmamap_sync(sc->age_cdata.age_tx_tag, map, BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->age_cdata.age_tx_ring_tag,
	    sc->age_cdata.age_tx_ring_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	return (0);
}

static void
age_start(struct ifnet *ifp)
{
        struct age_softc *sc;

	sc = ifp->if_softc;
	AGE_LOCK(sc);
	age_start_locked(ifp);
	AGE_UNLOCK(sc);
}

static void
age_start_locked(struct ifnet *ifp)
{
        struct age_softc *sc;
        struct mbuf *m_head;
	int enq;

	sc = ifp->if_softc;

	AGE_LOCK_ASSERT(sc);

	if ((ifp->if_drv_flags & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) !=
	    IFF_DRV_RUNNING || (sc->age_flags & AGE_FLAG_LINK) == 0)
		return;

	for (enq = 0; !IFQ_DRV_IS_EMPTY(&ifp->if_snd); ) {
		IFQ_DRV_DEQUEUE(&ifp->if_snd, m_head);
		if (m_head == NULL)
			break;
		/*
		 * Pack the data into the transmit ring. If we
		 * don't have room, set the OACTIVE flag and wait
		 * for the NIC to drain the ring.
		 */
		if (age_encap(sc, &m_head)) {
			if (m_head == NULL)
				break;
			IFQ_DRV_PREPEND(&ifp->if_snd, m_head);
			ifp->if_drv_flags |= IFF_DRV_OACTIVE;
			break;
		}

		enq++;
		/*
		 * If there's a BPF listener, bounce a copy of this frame
		 * to him.
		 */
		ETHER_BPF_MTAP(ifp, m_head);
	}

	if (enq > 0) {
		/* Update mbox. */
		AGE_COMMIT_MBOX(sc);
		/* Set a timeout in case the chip goes out to lunch. */
		sc->age_watchdog_timer = AGE_TX_TIMEOUT;
	}
}

static void
age_watchdog(struct age_softc *sc)
{
	struct ifnet *ifp;

	AGE_LOCK_ASSERT(sc);

	if (sc->age_watchdog_timer == 0 || --sc->age_watchdog_timer)
		return;

	ifp = sc->age_ifp;
	if ((sc->age_flags & AGE_FLAG_LINK) == 0) {
		if_printf(sc->age_ifp, "watchdog timeout (missed link)\n");
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
		age_init_locked(sc);
		return;
	}
	if (sc->age_cdata.age_tx_cnt == 0) {
		if_printf(sc->age_ifp,
		    "watchdog timeout (missed Tx interrupts) -- recovering\n");
		if (!IFQ_DRV_IS_EMPTY(&ifp->if_snd))
			age_start_locked(ifp);
		return;
	}
	if_printf(sc->age_ifp, "watchdog timeout\n");
	if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
	ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
	age_init_locked(sc);
	if (!IFQ_DRV_IS_EMPTY(&ifp->if_snd))
		age_start_locked(ifp);
}

static int
age_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct age_softc *sc;
	struct ifreq *ifr;
	struct mii_data *mii;
	uint32_t reg;
	int error, mask;

	sc = ifp->if_softc;
	ifr = (struct ifreq *)data;
	error = 0;
	switch (cmd) {
	case SIOCSIFMTU:
		if (ifr->ifr_mtu < ETHERMIN || ifr->ifr_mtu > AGE_JUMBO_MTU)
			error = EINVAL;
		else if (ifp->if_mtu != ifr->ifr_mtu) {
			AGE_LOCK(sc);
			ifp->if_mtu = ifr->ifr_mtu;
			if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0) {
				ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
				age_init_locked(sc);
			}
			AGE_UNLOCK(sc);
		}
		break;
	case SIOCSIFFLAGS:
		AGE_LOCK(sc);
		if ((ifp->if_flags & IFF_UP) != 0) {
			if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0) {
				if (((ifp->if_flags ^ sc->age_if_flags)
				    & (IFF_PROMISC | IFF_ALLMULTI)) != 0)
					age_rxfilter(sc);
			} else {
				if ((sc->age_flags & AGE_FLAG_DETACH) == 0)
					age_init_locked(sc);
			}
		} else {
			if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
				age_stop(sc);
		}
		sc->age_if_flags = ifp->if_flags;
		AGE_UNLOCK(sc);
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		AGE_LOCK(sc);
		if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
			age_rxfilter(sc);
		AGE_UNLOCK(sc);
		break;
	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		mii = device_get_softc(sc->age_miibus);
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, cmd);
		break;
	case SIOCSIFCAP:
		AGE_LOCK(sc);
		mask = ifr->ifr_reqcap ^ ifp->if_capenable;
		if ((mask & IFCAP_TXCSUM) != 0 &&
		    (ifp->if_capabilities & IFCAP_TXCSUM) != 0) {
			ifp->if_capenable ^= IFCAP_TXCSUM;
			if ((ifp->if_capenable & IFCAP_TXCSUM) != 0)
				ifp->if_hwassist |= AGE_CSUM_FEATURES;
			else
				ifp->if_hwassist &= ~AGE_CSUM_FEATURES;
		}
		if ((mask & IFCAP_RXCSUM) != 0 &&
		    (ifp->if_capabilities & IFCAP_RXCSUM) != 0) {
			ifp->if_capenable ^= IFCAP_RXCSUM;
			reg = CSR_READ_4(sc, AGE_MAC_CFG);
			reg &= ~MAC_CFG_RXCSUM_ENB;
			if ((ifp->if_capenable & IFCAP_RXCSUM) != 0)
				reg |= MAC_CFG_RXCSUM_ENB;
			CSR_WRITE_4(sc, AGE_MAC_CFG, reg);
		}
		if ((mask & IFCAP_TSO4) != 0 &&
		    (ifp->if_capabilities & IFCAP_TSO4) != 0) {
			ifp->if_capenable ^= IFCAP_TSO4;
			if ((ifp->if_capenable & IFCAP_TSO4) != 0)
				ifp->if_hwassist |= CSUM_TSO;
			else
				ifp->if_hwassist &= ~CSUM_TSO;
		}

		if ((mask & IFCAP_WOL_MCAST) != 0 &&
		    (ifp->if_capabilities & IFCAP_WOL_MCAST) != 0)
			ifp->if_capenable ^= IFCAP_WOL_MCAST;
		if ((mask & IFCAP_WOL_MAGIC) != 0 &&
		    (ifp->if_capabilities & IFCAP_WOL_MAGIC) != 0)
			ifp->if_capenable ^= IFCAP_WOL_MAGIC;
		if ((mask & IFCAP_VLAN_HWCSUM) != 0 &&
		    (ifp->if_capabilities & IFCAP_VLAN_HWCSUM) != 0)
			ifp->if_capenable ^= IFCAP_VLAN_HWCSUM;
		if ((mask & IFCAP_VLAN_HWTSO) != 0 &&
		    (ifp->if_capabilities & IFCAP_VLAN_HWTSO) != 0)
			ifp->if_capenable ^= IFCAP_VLAN_HWTSO;
		if ((mask & IFCAP_VLAN_HWTAGGING) != 0 &&
		    (ifp->if_capabilities & IFCAP_VLAN_HWTAGGING) != 0) {
			ifp->if_capenable ^= IFCAP_VLAN_HWTAGGING;
			if ((ifp->if_capenable & IFCAP_VLAN_HWTAGGING) == 0)
				ifp->if_capenable &= ~IFCAP_VLAN_HWTSO;
			age_rxvlan(sc);
		}
		AGE_UNLOCK(sc);
		VLAN_CAPABILITIES(ifp);
		break;
	default:
		error = ether_ioctl(ifp, cmd, data);
		break;
	}

	return (error);
}

static void
age_mac_config(struct age_softc *sc)
{
	struct mii_data *mii;
	uint32_t reg;

	AGE_LOCK_ASSERT(sc);

	mii = device_get_softc(sc->age_miibus);
	reg = CSR_READ_4(sc, AGE_MAC_CFG);
	reg &= ~MAC_CFG_FULL_DUPLEX;
	reg &= ~(MAC_CFG_TX_FC | MAC_CFG_RX_FC);
	reg &= ~MAC_CFG_SPEED_MASK;
	/* Reprogram MAC with resolved speed/duplex. */
	switch (IFM_SUBTYPE(mii->mii_media_active)) {
	case IFM_10_T:
	case IFM_100_TX:
		reg |= MAC_CFG_SPEED_10_100;
		break;
	case IFM_1000_T:
		reg |= MAC_CFG_SPEED_1000;
		break;
	}
	if ((IFM_OPTIONS(mii->mii_media_active) & IFM_FDX) != 0) {
		reg |= MAC_CFG_FULL_DUPLEX;
#ifdef notyet
		if ((IFM_OPTIONS(mii->mii_media_active) & IFM_ETH_TXPAUSE) != 0)
			reg |= MAC_CFG_TX_FC;
		if ((IFM_OPTIONS(mii->mii_media_active) & IFM_ETH_RXPAUSE) != 0)
			reg |= MAC_CFG_RX_FC;
#endif
	}

	CSR_WRITE_4(sc, AGE_MAC_CFG, reg);
}

static void
age_link_task(void *arg, int pending)
{
	struct age_softc *sc;
	struct mii_data *mii;
	struct ifnet *ifp;
	uint32_t reg;

	sc = (struct age_softc *)arg;

	AGE_LOCK(sc);
	mii = device_get_softc(sc->age_miibus);
	ifp = sc->age_ifp;
	if (mii == NULL || ifp == NULL ||
	    (ifp->if_drv_flags & IFF_DRV_RUNNING) == 0) {
		AGE_UNLOCK(sc);
		return;
	}

	sc->age_flags &= ~AGE_FLAG_LINK;
	if ((mii->mii_media_status & IFM_AVALID) != 0) {
		switch (IFM_SUBTYPE(mii->mii_media_active)) {
		case IFM_10_T:
		case IFM_100_TX:
		case IFM_1000_T:
			sc->age_flags |= AGE_FLAG_LINK;
			break;
		default:
			break;
		}
	}

	/* Stop Rx/Tx MACs. */
	age_stop_rxmac(sc);
	age_stop_txmac(sc);

	/* Program MACs with resolved speed/duplex/flow-control. */
	if ((sc->age_flags & AGE_FLAG_LINK) != 0) {
		age_mac_config(sc);
		reg = CSR_READ_4(sc, AGE_MAC_CFG);
		/* Restart DMA engine and Tx/Rx MAC. */
		CSR_WRITE_4(sc, AGE_DMA_CFG, CSR_READ_4(sc, AGE_DMA_CFG) |
		    DMA_CFG_RD_ENB | DMA_CFG_WR_ENB);
		reg |= MAC_CFG_TX_ENB | MAC_CFG_RX_ENB;
		CSR_WRITE_4(sc, AGE_MAC_CFG, reg);
	}

	AGE_UNLOCK(sc);
}

static void
age_stats_update(struct age_softc *sc)
{
	struct age_stats *stat;
	struct smb *smb;
	struct ifnet *ifp;

	AGE_LOCK_ASSERT(sc);

	stat = &sc->age_stat;

	bus_dmamap_sync(sc->age_cdata.age_smb_block_tag,
	    sc->age_cdata.age_smb_block_map,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	smb = sc->age_rdata.age_smb_block;
	if (smb->updated == 0)
		return;

	ifp = sc->age_ifp;
	/* Rx stats. */
	stat->rx_frames += smb->rx_frames;
	stat->rx_bcast_frames += smb->rx_bcast_frames;
	stat->rx_mcast_frames += smb->rx_mcast_frames;
	stat->rx_pause_frames += smb->rx_pause_frames;
	stat->rx_control_frames += smb->rx_control_frames;
	stat->rx_crcerrs += smb->rx_crcerrs;
	stat->rx_lenerrs += smb->rx_lenerrs;
	stat->rx_bytes += smb->rx_bytes;
	stat->rx_runts += smb->rx_runts;
	stat->rx_fragments += smb->rx_fragments;
	stat->rx_pkts_64 += smb->rx_pkts_64;
	stat->rx_pkts_65_127 += smb->rx_pkts_65_127;
	stat->rx_pkts_128_255 += smb->rx_pkts_128_255;
	stat->rx_pkts_256_511 += smb->rx_pkts_256_511;
	stat->rx_pkts_512_1023 += smb->rx_pkts_512_1023;
	stat->rx_pkts_1024_1518 += smb->rx_pkts_1024_1518;
	stat->rx_pkts_1519_max += smb->rx_pkts_1519_max;
	stat->rx_pkts_truncated += smb->rx_pkts_truncated;
	stat->rx_fifo_oflows += smb->rx_fifo_oflows;
	stat->rx_desc_oflows += smb->rx_desc_oflows;
	stat->rx_alignerrs += smb->rx_alignerrs;
	stat->rx_bcast_bytes += smb->rx_bcast_bytes;
	stat->rx_mcast_bytes += smb->rx_mcast_bytes;
	stat->rx_pkts_filtered += smb->rx_pkts_filtered;

	/* Tx stats. */
	stat->tx_frames += smb->tx_frames;
	stat->tx_bcast_frames += smb->tx_bcast_frames;
	stat->tx_mcast_frames += smb->tx_mcast_frames;
	stat->tx_pause_frames += smb->tx_pause_frames;
	stat->tx_excess_defer += smb->tx_excess_defer;
	stat->tx_control_frames += smb->tx_control_frames;
	stat->tx_deferred += smb->tx_deferred;
	stat->tx_bytes += smb->tx_bytes;
	stat->tx_pkts_64 += smb->tx_pkts_64;
	stat->tx_pkts_65_127 += smb->tx_pkts_65_127;
	stat->tx_pkts_128_255 += smb->tx_pkts_128_255;
	stat->tx_pkts_256_511 += smb->tx_pkts_256_511;
	stat->tx_pkts_512_1023 += smb->tx_pkts_512_1023;
	stat->tx_pkts_1024_1518 += smb->tx_pkts_1024_1518;
	stat->tx_pkts_1519_max += smb->tx_pkts_1519_max;
	stat->tx_single_colls += smb->tx_single_colls;
	stat->tx_multi_colls += smb->tx_multi_colls;
	stat->tx_late_colls += smb->tx_late_colls;
	stat->tx_excess_colls += smb->tx_excess_colls;
	stat->tx_underrun += smb->tx_underrun;
	stat->tx_desc_underrun += smb->tx_desc_underrun;
	stat->tx_lenerrs += smb->tx_lenerrs;
	stat->tx_pkts_truncated += smb->tx_pkts_truncated;
	stat->tx_bcast_bytes += smb->tx_bcast_bytes;
	stat->tx_mcast_bytes += smb->tx_mcast_bytes;

	/* Update counters in ifnet. */
	if_inc_counter(ifp, IFCOUNTER_OPACKETS, smb->tx_frames);

	if_inc_counter(ifp, IFCOUNTER_COLLISIONS, smb->tx_single_colls +
	    smb->tx_multi_colls + smb->tx_late_colls +
	    smb->tx_excess_colls * HDPX_CFG_RETRY_DEFAULT);

	if_inc_counter(ifp, IFCOUNTER_OERRORS, smb->tx_excess_colls +
	    smb->tx_late_colls + smb->tx_underrun +
	    smb->tx_pkts_truncated);

	if_inc_counter(ifp, IFCOUNTER_IPACKETS, smb->rx_frames);

	if_inc_counter(ifp, IFCOUNTER_IERRORS, smb->rx_crcerrs +
	    smb->rx_lenerrs + smb->rx_runts + smb->rx_pkts_truncated +
	    smb->rx_fifo_oflows + smb->rx_desc_oflows +
	    smb->rx_alignerrs);

	/* Update done, clear. */
	smb->updated = 0;

	bus_dmamap_sync(sc->age_cdata.age_smb_block_tag,
	    sc->age_cdata.age_smb_block_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}

static int
age_intr(void *arg)
{
	struct age_softc *sc;
	uint32_t status;

	sc = (struct age_softc *)arg;

	status = CSR_READ_4(sc, AGE_INTR_STATUS);
	if (status == 0 || (status & AGE_INTRS) == 0)
		return (FILTER_STRAY);
	/* Disable interrupts. */
	CSR_WRITE_4(sc, AGE_INTR_STATUS, status | INTR_DIS_INT);
	taskqueue_enqueue(sc->age_tq, &sc->age_int_task);

	return (FILTER_HANDLED);
}

static void
age_int_task(void *arg, int pending)
{
	struct age_softc *sc;
	struct ifnet *ifp;
	struct cmb *cmb;
	uint32_t status;

	sc = (struct age_softc *)arg;

	AGE_LOCK(sc);

	bus_dmamap_sync(sc->age_cdata.age_cmb_block_tag,
	    sc->age_cdata.age_cmb_block_map,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	cmb = sc->age_rdata.age_cmb_block;
	status = le32toh(cmb->intr_status);
	if (sc->age_morework != 0)
		status |= INTR_CMB_RX;
	if ((status & AGE_INTRS) == 0)
		goto done;

	sc->age_tpd_cons = (le32toh(cmb->tpd_cons) & TPD_CONS_MASK) >>
	    TPD_CONS_SHIFT;
	sc->age_rr_prod = (le32toh(cmb->rprod_cons) & RRD_PROD_MASK) >>
	    RRD_PROD_SHIFT;
	/* Let hardware know CMB was served. */
	cmb->intr_status = 0;
	bus_dmamap_sync(sc->age_cdata.age_cmb_block_tag,
	    sc->age_cdata.age_cmb_block_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	ifp = sc->age_ifp;
	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0) {
		if ((status & INTR_CMB_RX) != 0)
			sc->age_morework = age_rxintr(sc, sc->age_rr_prod,
			    sc->age_process_limit);
		if ((status & INTR_CMB_TX) != 0)
			age_txintr(sc, sc->age_tpd_cons);
		if ((status & (INTR_DMA_RD_TO_RST | INTR_DMA_WR_TO_RST)) != 0) {
			if ((status & INTR_DMA_RD_TO_RST) != 0)
				device_printf(sc->age_dev,
				    "DMA read error! -- resetting\n");
			if ((status & INTR_DMA_WR_TO_RST) != 0)
				device_printf(sc->age_dev,
				    "DMA write error! -- resetting\n");
			ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
			age_init_locked(sc);
		}
		if (!IFQ_DRV_IS_EMPTY(&ifp->if_snd))
			age_start_locked(ifp);
		if ((status & INTR_SMB) != 0)
			age_stats_update(sc);
	}

	/* Check whether CMB was updated while serving Tx/Rx/SMB handler. */
	bus_dmamap_sync(sc->age_cdata.age_cmb_block_tag,
	    sc->age_cdata.age_cmb_block_map,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	status = le32toh(cmb->intr_status);
	if (sc->age_morework != 0 || (status & AGE_INTRS) != 0) {
		taskqueue_enqueue(sc->age_tq, &sc->age_int_task);
		AGE_UNLOCK(sc);
		return;
	}

done:
	/* Re-enable interrupts. */
	CSR_WRITE_4(sc, AGE_INTR_STATUS, 0);
	AGE_UNLOCK(sc);
}

static void
age_txintr(struct age_softc *sc, int tpd_cons)
{
	struct ifnet *ifp;
	struct age_txdesc *txd;
	int cons, prog;

	AGE_LOCK_ASSERT(sc);

	ifp = sc->age_ifp;

	bus_dmamap_sync(sc->age_cdata.age_tx_ring_tag,
	    sc->age_cdata.age_tx_ring_map,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	/*
	 * Go through our Tx list and free mbufs for those
	 * frames which have been transmitted.
	 */
	cons = sc->age_cdata.age_tx_cons;
	for (prog = 0; cons != tpd_cons; AGE_DESC_INC(cons, AGE_TX_RING_CNT)) {
		if (sc->age_cdata.age_tx_cnt <= 0)
			break;
		prog++;
		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
		sc->age_cdata.age_tx_cnt--;
		txd = &sc->age_cdata.age_txdesc[cons];
		/*
		 * Clear Tx descriptors, it's not required but would
		 * help debugging in case of Tx issues.
		 */
		txd->tx_desc->addr = 0;
		txd->tx_desc->len = 0;
		txd->tx_desc->flags = 0;

		if (txd->tx_m == NULL)
			continue;
		/* Reclaim transmitted mbufs. */
		bus_dmamap_sync(sc->age_cdata.age_tx_tag, txd->tx_dmamap,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->age_cdata.age_tx_tag, txd->tx_dmamap);
		m_freem(txd->tx_m);
		txd->tx_m = NULL;
	}

	if (prog > 0) {
		sc->age_cdata.age_tx_cons = cons;

		/*
		 * Unarm watchdog timer only when there are no pending
		 * Tx descriptors in queue.
		 */
		if (sc->age_cdata.age_tx_cnt == 0)
			sc->age_watchdog_timer = 0;
		bus_dmamap_sync(sc->age_cdata.age_tx_ring_tag,
		    sc->age_cdata.age_tx_ring_map,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	}
}

#ifndef __NO_STRICT_ALIGNMENT
static struct mbuf *
age_fixup_rx(struct ifnet *ifp, struct mbuf *m)
{
	struct mbuf *n;
        int i;
        uint16_t *src, *dst;

	src = mtod(m, uint16_t *);
	dst = src - 3;

	if (m->m_next == NULL) {
		for (i = 0; i < (m->m_len / sizeof(uint16_t) + 1); i++)
			*dst++ = *src++;
		m->m_data -= 6;
		return (m);
	}
	/*
	 * Append a new mbuf to received mbuf chain and copy ethernet
	 * header from the mbuf chain. This can save lots of CPU
	 * cycles for jumbo frame.
	 */
	MGETHDR(n, M_NOWAIT, MT_DATA);
	if (n == NULL) {
		if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
		m_freem(m);
		return (NULL);
	}
	bcopy(m->m_data, n->m_data, ETHER_HDR_LEN);
	m->m_data += ETHER_HDR_LEN;
	m->m_len -= ETHER_HDR_LEN;
	n->m_len = ETHER_HDR_LEN;
	M_MOVE_PKTHDR(n, m);
	n->m_next = m;
	return (n);
}
#endif

/* Receive a frame. */
static void
age_rxeof(struct age_softc *sc, struct rx_rdesc *rxrd)
{
	struct age_rxdesc *rxd;
	struct ifnet *ifp;
	struct mbuf *mp, *m;
	uint32_t status, index, vtag;
	int count, nsegs;
	int rx_cons;

	AGE_LOCK_ASSERT(sc);

	ifp = sc->age_ifp;
	status = le32toh(rxrd->flags);
	index = le32toh(rxrd->index);
	rx_cons = AGE_RX_CONS(index);
	nsegs = AGE_RX_NSEGS(index);

	sc->age_cdata.age_rxlen = AGE_RX_BYTES(le32toh(rxrd->len));
	if ((status & (AGE_RRD_ERROR | AGE_RRD_LENGTH_NOK)) != 0) {
		/*
		 * We want to pass the following frames to upper
		 * layer regardless of error status of Rx return
		 * ring.
		 *
		 *  o IP/TCP/UDP checksum is bad.
		 *  o frame length and protocol specific length
		 *     does not match.
		 */
		status |= AGE_RRD_IPCSUM_NOK | AGE_RRD_TCP_UDPCSUM_NOK;
		if ((status & (AGE_RRD_CRC | AGE_RRD_CODE | AGE_RRD_DRIBBLE |
		    AGE_RRD_RUNT | AGE_RRD_OFLOW | AGE_RRD_TRUNC)) != 0)
			return;
	}

	for (count = 0; count < nsegs; count++,
	    AGE_DESC_INC(rx_cons, AGE_RX_RING_CNT)) {
		rxd = &sc->age_cdata.age_rxdesc[rx_cons];
		mp = rxd->rx_m;
		/* Add a new receive buffer to the ring. */
		if (age_newbuf(sc, rxd) != 0) {
			if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
			/* Reuse Rx buffers. */
			if (sc->age_cdata.age_rxhead != NULL)
				m_freem(sc->age_cdata.age_rxhead);
			break;
		}

		/*
		 * Assume we've received a full sized frame.
		 * Actual size is fixed when we encounter the end of
		 * multi-segmented frame.
		 */
		mp->m_len = AGE_RX_BUF_SIZE;

		/* Chain received mbufs. */
		if (sc->age_cdata.age_rxhead == NULL) {
			sc->age_cdata.age_rxhead = mp;
			sc->age_cdata.age_rxtail = mp;
		} else {
			mp->m_flags &= ~M_PKTHDR;
			sc->age_cdata.age_rxprev_tail =
			    sc->age_cdata.age_rxtail;
			sc->age_cdata.age_rxtail->m_next = mp;
			sc->age_cdata.age_rxtail = mp;
		}

		if (count == nsegs - 1) {
			/* Last desc. for this frame. */
			m = sc->age_cdata.age_rxhead;
			m->m_flags |= M_PKTHDR;
			/*
			 * It seems that L1 controller has no way
			 * to tell hardware to strip CRC bytes.
			 */
			m->m_pkthdr.len = sc->age_cdata.age_rxlen -
			    ETHER_CRC_LEN;
			if (nsegs > 1) {
				/* Set last mbuf size. */
				mp->m_len = sc->age_cdata.age_rxlen -
				    ((nsegs - 1) * AGE_RX_BUF_SIZE);
				/* Remove the CRC bytes in chained mbufs. */
				if (mp->m_len <= ETHER_CRC_LEN) {
					sc->age_cdata.age_rxtail =
					    sc->age_cdata.age_rxprev_tail;
					sc->age_cdata.age_rxtail->m_len -=
					    (ETHER_CRC_LEN - mp->m_len);
					sc->age_cdata.age_rxtail->m_next = NULL;
					m_freem(mp);
				} else {
					mp->m_len -= ETHER_CRC_LEN;
				}
			} else
				m->m_len = m->m_pkthdr.len;
			m->m_pkthdr.rcvif = ifp;
			/*
			 * Set checksum information.
			 * It seems that L1 controller can compute partial
			 * checksum. The partial checksum value can be used
			 * to accelerate checksum computation for fragmented
			 * TCP/UDP packets. Upper network stack already
			 * takes advantage of the partial checksum value in
			 * IP reassembly stage. But I'm not sure the
			 * correctness of the partial hardware checksum
			 * assistance due to lack of data sheet. If it is
			 * proven to work on L1 I'll enable it.
			 */
			if ((ifp->if_capenable & IFCAP_RXCSUM) != 0 &&
			    (status & AGE_RRD_IPV4) != 0) {
				if ((status & AGE_RRD_IPCSUM_NOK) == 0)
					m->m_pkthdr.csum_flags |=
					    CSUM_IP_CHECKED | CSUM_IP_VALID;
				if ((status & (AGE_RRD_TCP | AGE_RRD_UDP)) &&
				    (status & AGE_RRD_TCP_UDPCSUM_NOK) == 0) {
					m->m_pkthdr.csum_flags |=
					    CSUM_DATA_VALID | CSUM_PSEUDO_HDR;
					m->m_pkthdr.csum_data = 0xffff;
				}
				/*
				 * Don't mark bad checksum for TCP/UDP frames
				 * as fragmented frames may always have set
				 * bad checksummed bit of descriptor status.
				 */
			}

			/* Check for VLAN tagged frames. */
			if ((ifp->if_capenable & IFCAP_VLAN_HWTAGGING) != 0 &&
			    (status & AGE_RRD_VLAN) != 0) {
				vtag = AGE_RX_VLAN(le32toh(rxrd->vtags));
				m->m_pkthdr.ether_vtag = AGE_RX_VLAN_TAG(vtag);
				m->m_flags |= M_VLANTAG;
			}
#ifndef __NO_STRICT_ALIGNMENT
			m = age_fixup_rx(ifp, m);
			if (m != NULL)
#endif
			{
			/* Pass it on. */
			AGE_UNLOCK(sc);
			(*ifp->if_input)(ifp, m);
			AGE_LOCK(sc);
			}
		}
	}

	/* Reset mbuf chains. */
	AGE_RXCHAIN_RESET(sc);
}

static int
age_rxintr(struct age_softc *sc, int rr_prod, int count)
{
	struct rx_rdesc *rxrd;
	int rr_cons, nsegs, pktlen, prog;

	AGE_LOCK_ASSERT(sc);

	rr_cons = sc->age_cdata.age_rr_cons;
	if (rr_cons == rr_prod)
		return (0);

	bus_dmamap_sync(sc->age_cdata.age_rr_ring_tag,
	    sc->age_cdata.age_rr_ring_map,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	bus_dmamap_sync(sc->age_cdata.age_rx_ring_tag,
	    sc->age_cdata.age_rx_ring_map, BUS_DMASYNC_POSTWRITE);

	for (prog = 0; rr_cons != rr_prod; prog++) {
		if (count-- <= 0)
			break;
		rxrd = &sc->age_rdata.age_rr_ring[rr_cons];
		nsegs = AGE_RX_NSEGS(le32toh(rxrd->index));
		if (nsegs == 0)
			break;
		/*
		 * Check number of segments against received bytes.
		 * Non-matching value would indicate that hardware
		 * is still trying to update Rx return descriptors.
		 * I'm not sure whether this check is really needed.
		 */
		pktlen = AGE_RX_BYTES(le32toh(rxrd->len));
		if (nsegs != howmany(pktlen, AGE_RX_BUF_SIZE))
			break;

		/* Received a frame. */
		age_rxeof(sc, rxrd);
		/* Clear return ring. */
		rxrd->index = 0;
		AGE_DESC_INC(rr_cons, AGE_RR_RING_CNT);
		sc->age_cdata.age_rx_cons += nsegs;
		sc->age_cdata.age_rx_cons %= AGE_RX_RING_CNT;
	}

	if (prog > 0) {
		/* Update the consumer index. */
		sc->age_cdata.age_rr_cons = rr_cons;

		bus_dmamap_sync(sc->age_cdata.age_rx_ring_tag,
		    sc->age_cdata.age_rx_ring_map, BUS_DMASYNC_PREWRITE);
		/* Sync descriptors. */
		bus_dmamap_sync(sc->age_cdata.age_rr_ring_tag,
		    sc->age_cdata.age_rr_ring_map,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

		/* Notify hardware availability of new Rx buffers. */
		AGE_COMMIT_MBOX(sc);
	}

	return (count > 0 ? 0 : EAGAIN);
}

static void
age_tick(void *arg)
{
	struct age_softc *sc;
	struct mii_data *mii;

	sc = (struct age_softc *)arg;

	AGE_LOCK_ASSERT(sc);

	mii = device_get_softc(sc->age_miibus);
	mii_tick(mii);
	age_watchdog(sc);
	callout_reset(&sc->age_tick_ch, hz, age_tick, sc);
}

static void
age_reset(struct age_softc *sc)
{
	uint32_t reg;
	int i;

	CSR_WRITE_4(sc, AGE_MASTER_CFG, MASTER_RESET);
	CSR_READ_4(sc, AGE_MASTER_CFG);
	DELAY(1000);
	for (i = AGE_RESET_TIMEOUT; i > 0; i--) {
		if ((reg = CSR_READ_4(sc, AGE_IDLE_STATUS)) == 0)
			break;
		DELAY(10);
	}

	if (i == 0)
		device_printf(sc->age_dev, "reset timeout(0x%08x)!\n", reg);
	/* Initialize PCIe module. From Linux. */
	CSR_WRITE_4(sc, 0x12FC, 0x6500);
	CSR_WRITE_4(sc, 0x1008, CSR_READ_4(sc, 0x1008) | 0x8000);
}

static void
age_init(void *xsc)
{
	struct age_softc *sc;

	sc = (struct age_softc *)xsc;
	AGE_LOCK(sc);
	age_init_locked(sc);
	AGE_UNLOCK(sc);
}

static void
age_init_locked(struct age_softc *sc)
{
	struct ifnet *ifp;
	struct mii_data *mii;
	uint8_t eaddr[ETHER_ADDR_LEN];
	bus_addr_t paddr;
	uint32_t reg, fsize;
	uint32_t rxf_hi, rxf_lo, rrd_hi, rrd_lo;
	int error;

	AGE_LOCK_ASSERT(sc);

	ifp = sc->age_ifp;
	mii = device_get_softc(sc->age_miibus);

	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
		return;

	/*
	 * Cancel any pending I/O.
	 */
	age_stop(sc);

	/*
	 * Reset the chip to a known state.
	 */
	age_reset(sc);

	/* Initialize descriptors. */
	error = age_init_rx_ring(sc);
        if (error != 0) {
                device_printf(sc->age_dev, "no memory for Rx buffers.\n");
                age_stop(sc);
		return;
        }
	age_init_rr_ring(sc);
	age_init_tx_ring(sc);
	age_init_cmb_block(sc);
	age_init_smb_block(sc);

	/* Reprogram the station address. */
	bcopy(IF_LLADDR(ifp), eaddr, ETHER_ADDR_LEN);
	CSR_WRITE_4(sc, AGE_PAR0,
	    eaddr[2] << 24 | eaddr[3] << 16 | eaddr[4] << 8 | eaddr[5]);
	CSR_WRITE_4(sc, AGE_PAR1, eaddr[0] << 8 | eaddr[1]);

	/* Set descriptor base addresses. */
	paddr = sc->age_rdata.age_tx_ring_paddr;
	CSR_WRITE_4(sc, AGE_DESC_ADDR_HI, AGE_ADDR_HI(paddr));
	paddr = sc->age_rdata.age_rx_ring_paddr;
	CSR_WRITE_4(sc, AGE_DESC_RD_ADDR_LO, AGE_ADDR_LO(paddr));
	paddr = sc->age_rdata.age_rr_ring_paddr;
	CSR_WRITE_4(sc, AGE_DESC_RRD_ADDR_LO, AGE_ADDR_LO(paddr));
	paddr = sc->age_rdata.age_tx_ring_paddr;
	CSR_WRITE_4(sc, AGE_DESC_TPD_ADDR_LO, AGE_ADDR_LO(paddr));
	paddr = sc->age_rdata.age_cmb_block_paddr;
	CSR_WRITE_4(sc, AGE_DESC_CMB_ADDR_LO, AGE_ADDR_LO(paddr));
	paddr = sc->age_rdata.age_smb_block_paddr;
	CSR_WRITE_4(sc, AGE_DESC_SMB_ADDR_LO, AGE_ADDR_LO(paddr));
	/* Set Rx/Rx return descriptor counter. */
	CSR_WRITE_4(sc, AGE_DESC_RRD_RD_CNT,
	    ((AGE_RR_RING_CNT << DESC_RRD_CNT_SHIFT) &
	    DESC_RRD_CNT_MASK) |
	    ((AGE_RX_RING_CNT << DESC_RD_CNT_SHIFT) & DESC_RD_CNT_MASK));
	/* Set Tx descriptor counter. */
	CSR_WRITE_4(sc, AGE_DESC_TPD_CNT,
	    (AGE_TX_RING_CNT << DESC_TPD_CNT_SHIFT) & DESC_TPD_CNT_MASK);

	/* Tell hardware that we're ready to load descriptors. */
	CSR_WRITE_4(sc, AGE_DMA_BLOCK, DMA_BLOCK_LOAD);

	/*
	 * Initialize mailbox register.
	 * Updated producer/consumer index information is exchanged
	 * through this mailbox register. However Tx producer and
	 * Rx return consumer/Rx producer are all shared such that
	 * it's hard to separate code path between Tx and Rx without
	 * locking. If L1 hardware have a separate mail box register
	 * for Tx and Rx consumer/producer management we could have
	 * indepent Tx/Rx handler which in turn Rx handler could have
	 * been run without any locking.
	 */
	AGE_COMMIT_MBOX(sc);

	/* Configure IPG/IFG parameters. */
	CSR_WRITE_4(sc, AGE_IPG_IFG_CFG,
	    ((IPG_IFG_IPG2_DEFAULT << IPG_IFG_IPG2_SHIFT) & IPG_IFG_IPG2_MASK) |
	    ((IPG_IFG_IPG1_DEFAULT << IPG_IFG_IPG1_SHIFT) & IPG_IFG_IPG1_MASK) |
	    ((IPG_IFG_MIFG_DEFAULT << IPG_IFG_MIFG_SHIFT) & IPG_IFG_MIFG_MASK) |
	    ((IPG_IFG_IPGT_DEFAULT << IPG_IFG_IPGT_SHIFT) & IPG_IFG_IPGT_MASK));

	/* Set parameters for half-duplex media. */
	CSR_WRITE_4(sc, AGE_HDPX_CFG,
	    ((HDPX_CFG_LCOL_DEFAULT << HDPX_CFG_LCOL_SHIFT) &
	    HDPX_CFG_LCOL_MASK) |
	    ((HDPX_CFG_RETRY_DEFAULT << HDPX_CFG_RETRY_SHIFT) &
	    HDPX_CFG_RETRY_MASK) | HDPX_CFG_EXC_DEF_EN |
	    ((HDPX_CFG_ABEBT_DEFAULT << HDPX_CFG_ABEBT_SHIFT) &
	    HDPX_CFG_ABEBT_MASK) |
	    ((HDPX_CFG_JAMIPG_DEFAULT << HDPX_CFG_JAMIPG_SHIFT) &
	    HDPX_CFG_JAMIPG_MASK));

	/* Configure interrupt moderation timer. */
	CSR_WRITE_2(sc, AGE_IM_TIMER, AGE_USECS(sc->age_int_mod));
	reg = CSR_READ_4(sc, AGE_MASTER_CFG);
	reg &= ~MASTER_MTIMER_ENB;
	if (AGE_USECS(sc->age_int_mod) == 0)
		reg &= ~MASTER_ITIMER_ENB;
	else
		reg |= MASTER_ITIMER_ENB;
	CSR_WRITE_4(sc, AGE_MASTER_CFG, reg);
	if (bootverbose)
		device_printf(sc->age_dev, "interrupt moderation is %d us.\n",
		    sc->age_int_mod);
	CSR_WRITE_2(sc, AGE_INTR_CLR_TIMER, AGE_USECS(1000));

	/* Set Maximum frame size but don't let MTU be lass than ETHER_MTU. */
	if (ifp->if_mtu < ETHERMTU)
		sc->age_max_frame_size = ETHERMTU;
	else
		sc->age_max_frame_size = ifp->if_mtu;
	sc->age_max_frame_size += ETHER_HDR_LEN +
	    sizeof(struct ether_vlan_header) + ETHER_CRC_LEN;
	CSR_WRITE_4(sc, AGE_FRAME_SIZE, sc->age_max_frame_size);
	/* Configure jumbo frame. */
	fsize = roundup(sc->age_max_frame_size, sizeof(uint64_t));
	CSR_WRITE_4(sc, AGE_RXQ_JUMBO_CFG,
	    (((fsize / sizeof(uint64_t)) <<
	    RXQ_JUMBO_CFG_SZ_THRESH_SHIFT) & RXQ_JUMBO_CFG_SZ_THRESH_MASK) |
	    ((RXQ_JUMBO_CFG_LKAH_DEFAULT <<
	    RXQ_JUMBO_CFG_LKAH_SHIFT) & RXQ_JUMBO_CFG_LKAH_MASK) |
	    ((AGE_USECS(8) << RXQ_JUMBO_CFG_RRD_TIMER_SHIFT) &
	    RXQ_JUMBO_CFG_RRD_TIMER_MASK));

	/* Configure flow-control parameters. From Linux. */
	if ((sc->age_flags & AGE_FLAG_PCIE) != 0) {
		/*
		 * Magic workaround for old-L1.
		 * Don't know which hw revision requires this magic.
		 */
		CSR_WRITE_4(sc, 0x12FC, 0x6500);
		/*
		 * Another magic workaround for flow-control mode
		 * change. From Linux.
		 */
		CSR_WRITE_4(sc, 0x1008, CSR_READ_4(sc, 0x1008) | 0x8000);
	}
	/*
	 * TODO
	 *  Should understand pause parameter relationships between FIFO
	 *  size and number of Rx descriptors and Rx return descriptors.
	 *
	 *  Magic parameters came from Linux.
	 */
	switch (sc->age_chip_rev) {
	case 0x8001:
	case 0x9001:
	case 0x9002:
	case 0x9003:
		rxf_hi = AGE_RX_RING_CNT / 16;
		rxf_lo = (AGE_RX_RING_CNT * 7) / 8;
		rrd_hi = (AGE_RR_RING_CNT * 7) / 8;
		rrd_lo = AGE_RR_RING_CNT / 16;
		break;
	default:
		reg = CSR_READ_4(sc, AGE_SRAM_RX_FIFO_LEN);
		rxf_lo = reg / 16;
		if (rxf_lo < 192)
			rxf_lo = 192;
		rxf_hi = (reg * 7) / 8;
		if (rxf_hi < rxf_lo)
			rxf_hi = rxf_lo + 16;
		reg = CSR_READ_4(sc, AGE_SRAM_RRD_LEN);
		rrd_lo = reg / 8;
		rrd_hi = (reg * 7) / 8;
		if (rrd_lo < 2)
			rrd_lo = 2;
		if (rrd_hi < rrd_lo)
			rrd_hi = rrd_lo + 3;
		break;
	}
	CSR_WRITE_4(sc, AGE_RXQ_FIFO_PAUSE_THRESH,
	    ((rxf_lo << RXQ_FIFO_PAUSE_THRESH_LO_SHIFT) &
	    RXQ_FIFO_PAUSE_THRESH_LO_MASK) |
	    ((rxf_hi << RXQ_FIFO_PAUSE_THRESH_HI_SHIFT) &
	    RXQ_FIFO_PAUSE_THRESH_HI_MASK));
	CSR_WRITE_4(sc, AGE_RXQ_RRD_PAUSE_THRESH,
	    ((rrd_lo << RXQ_RRD_PAUSE_THRESH_LO_SHIFT) &
	    RXQ_RRD_PAUSE_THRESH_LO_MASK) |
	    ((rrd_hi << RXQ_RRD_PAUSE_THRESH_HI_SHIFT) &
	    RXQ_RRD_PAUSE_THRESH_HI_MASK));

	/* Configure RxQ. */
	CSR_WRITE_4(sc, AGE_RXQ_CFG,
	    ((RXQ_CFG_RD_BURST_DEFAULT << RXQ_CFG_RD_BURST_SHIFT) &
	    RXQ_CFG_RD_BURST_MASK) |
	    ((RXQ_CFG_RRD_BURST_THRESH_DEFAULT <<
	    RXQ_CFG_RRD_BURST_THRESH_SHIFT) & RXQ_CFG_RRD_BURST_THRESH_MASK) |
	    ((RXQ_CFG_RD_PREF_MIN_IPG_DEFAULT <<
	    RXQ_CFG_RD_PREF_MIN_IPG_SHIFT) & RXQ_CFG_RD_PREF_MIN_IPG_MASK) |
	    RXQ_CFG_CUT_THROUGH_ENB | RXQ_CFG_ENB);

	/* Configure TxQ. */
	CSR_WRITE_4(sc, AGE_TXQ_CFG,
	    ((TXQ_CFG_TPD_BURST_DEFAULT << TXQ_CFG_TPD_BURST_SHIFT) &
	    TXQ_CFG_TPD_BURST_MASK) |
	    ((TXQ_CFG_TX_FIFO_BURST_DEFAULT << TXQ_CFG_TX_FIFO_BURST_SHIFT) &
	    TXQ_CFG_TX_FIFO_BURST_MASK) |
	    ((TXQ_CFG_TPD_FETCH_DEFAULT <<
	    TXQ_CFG_TPD_FETCH_THRESH_SHIFT) & TXQ_CFG_TPD_FETCH_THRESH_MASK) |
	    TXQ_CFG_ENB);

	CSR_WRITE_4(sc, AGE_TX_JUMBO_TPD_TH_IPG,
	    (((fsize / sizeof(uint64_t) << TX_JUMBO_TPD_TH_SHIFT)) &
	    TX_JUMBO_TPD_TH_MASK) |
	    ((TX_JUMBO_TPD_IPG_DEFAULT << TX_JUMBO_TPD_IPG_SHIFT) &
	    TX_JUMBO_TPD_IPG_MASK));
	/* Configure DMA parameters. */
	CSR_WRITE_4(sc, AGE_DMA_CFG,
	    DMA_CFG_ENH_ORDER | DMA_CFG_RCB_64 |
	    sc->age_dma_rd_burst | DMA_CFG_RD_ENB |
	    sc->age_dma_wr_burst | DMA_CFG_WR_ENB);

	/* Configure CMB DMA write threshold. */
	CSR_WRITE_4(sc, AGE_CMB_WR_THRESH,
	    ((CMB_WR_THRESH_RRD_DEFAULT << CMB_WR_THRESH_RRD_SHIFT) &
	    CMB_WR_THRESH_RRD_MASK) |
	    ((CMB_WR_THRESH_TPD_DEFAULT << CMB_WR_THRESH_TPD_SHIFT) &
	    CMB_WR_THRESH_TPD_MASK));

	/* Set CMB/SMB timer and enable them. */
	CSR_WRITE_4(sc, AGE_CMB_WR_TIMER,
	    ((AGE_USECS(2) << CMB_WR_TIMER_TX_SHIFT) & CMB_WR_TIMER_TX_MASK) |
	    ((AGE_USECS(2) << CMB_WR_TIMER_RX_SHIFT) & CMB_WR_TIMER_RX_MASK));
	/* Request SMB updates for every seconds. */
	CSR_WRITE_4(sc, AGE_SMB_TIMER, AGE_USECS(1000 * 1000));
	CSR_WRITE_4(sc, AGE_CSMB_CTRL, CSMB_CTRL_SMB_ENB | CSMB_CTRL_CMB_ENB);

	/*
	 * Disable all WOL bits as WOL can interfere normal Rx
	 * operation.
	 */
	CSR_WRITE_4(sc, AGE_WOL_CFG, 0);

	/*
	 * Configure Tx/Rx MACs.
	 *  - Auto-padding for short frames.
	 *  - Enable CRC generation.
	 *  Start with full-duplex/1000Mbps media. Actual reconfiguration
	 *  of MAC is followed after link establishment.
	 */
	CSR_WRITE_4(sc, AGE_MAC_CFG,
	    MAC_CFG_TX_CRC_ENB | MAC_CFG_TX_AUTO_PAD |
	    MAC_CFG_FULL_DUPLEX | MAC_CFG_SPEED_1000 |
	    ((MAC_CFG_PREAMBLE_DEFAULT << MAC_CFG_PREAMBLE_SHIFT) &
	    MAC_CFG_PREAMBLE_MASK));
	/* Set up the receive filter. */
	age_rxfilter(sc);
	age_rxvlan(sc);

	reg = CSR_READ_4(sc, AGE_MAC_CFG);
	if ((ifp->if_capenable & IFCAP_RXCSUM) != 0)
		reg |= MAC_CFG_RXCSUM_ENB;

	/* Ack all pending interrupts and clear it. */
	CSR_WRITE_4(sc, AGE_INTR_STATUS, 0);
	CSR_WRITE_4(sc, AGE_INTR_MASK, AGE_INTRS);

	/* Finally enable Tx/Rx MAC. */
	CSR_WRITE_4(sc, AGE_MAC_CFG, reg | MAC_CFG_TX_ENB | MAC_CFG_RX_ENB);

	sc->age_flags &= ~AGE_FLAG_LINK;
	/* Switch to the current media. */
	mii_mediachg(mii);

	callout_reset(&sc->age_tick_ch, hz, age_tick, sc);

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
}

static void
age_stop(struct age_softc *sc)
{
	struct ifnet *ifp;
	struct age_txdesc *txd;
	struct age_rxdesc *rxd;
	uint32_t reg;
	int i;

	AGE_LOCK_ASSERT(sc);
	/*
	 * Mark the interface down and cancel the watchdog timer.
	 */
	ifp = sc->age_ifp;
	ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);
	sc->age_flags &= ~AGE_FLAG_LINK;
	callout_stop(&sc->age_tick_ch);
	sc->age_watchdog_timer = 0;

	/*
	 * Disable interrupts.
	 */
	CSR_WRITE_4(sc, AGE_INTR_MASK, 0);
	CSR_WRITE_4(sc, AGE_INTR_STATUS, 0xFFFFFFFF);
	/* Stop CMB/SMB updates. */
	CSR_WRITE_4(sc, AGE_CSMB_CTRL, 0);
	/* Stop Rx/Tx MAC. */
	age_stop_rxmac(sc);
	age_stop_txmac(sc);
	/* Stop DMA. */
	CSR_WRITE_4(sc, AGE_DMA_CFG,
	    CSR_READ_4(sc, AGE_DMA_CFG) & ~(DMA_CFG_RD_ENB | DMA_CFG_WR_ENB));
	/* Stop TxQ/RxQ. */
	CSR_WRITE_4(sc, AGE_TXQ_CFG,
	    CSR_READ_4(sc, AGE_TXQ_CFG) & ~TXQ_CFG_ENB);
	CSR_WRITE_4(sc, AGE_RXQ_CFG,
	    CSR_READ_4(sc, AGE_RXQ_CFG) & ~RXQ_CFG_ENB);
	for (i = AGE_RESET_TIMEOUT; i > 0; i--) {
		if ((reg = CSR_READ_4(sc, AGE_IDLE_STATUS)) == 0)
			break;
		DELAY(10);
	}
	if (i == 0)
		device_printf(sc->age_dev,
		    "stopping Rx/Tx MACs timed out(0x%08x)!\n", reg);

	 /* Reclaim Rx buffers that have been processed. */
	if (sc->age_cdata.age_rxhead != NULL)
		m_freem(sc->age_cdata.age_rxhead);
	AGE_RXCHAIN_RESET(sc);
	/*
	 * Free RX and TX mbufs still in the queues.
	 */
	for (i = 0; i < AGE_RX_RING_CNT; i++) {
		rxd = &sc->age_cdata.age_rxdesc[i];
		if (rxd->rx_m != NULL) {
			bus_dmamap_sync(sc->age_cdata.age_rx_tag,
			    rxd->rx_dmamap, BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(sc->age_cdata.age_rx_tag,
			    rxd->rx_dmamap);
			m_freem(rxd->rx_m);
			rxd->rx_m = NULL;
		}
        }
	for (i = 0; i < AGE_TX_RING_CNT; i++) {
		txd = &sc->age_cdata.age_txdesc[i];
		if (txd->tx_m != NULL) {
			bus_dmamap_sync(sc->age_cdata.age_tx_tag,
			    txd->tx_dmamap, BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->age_cdata.age_tx_tag,
			    txd->tx_dmamap);
			m_freem(txd->tx_m);
			txd->tx_m = NULL;
		}
        }
}

static void
age_stop_txmac(struct age_softc *sc)
{
	uint32_t reg;
	int i;

	AGE_LOCK_ASSERT(sc);

	reg = CSR_READ_4(sc, AGE_MAC_CFG);
	if ((reg & MAC_CFG_TX_ENB) != 0) {
		reg &= ~MAC_CFG_TX_ENB;
		CSR_WRITE_4(sc, AGE_MAC_CFG, reg);
	}
	/* Stop Tx DMA engine. */
	reg = CSR_READ_4(sc, AGE_DMA_CFG);
	if ((reg & DMA_CFG_RD_ENB) != 0) {
		reg &= ~DMA_CFG_RD_ENB;
		CSR_WRITE_4(sc, AGE_DMA_CFG, reg);
	}
	for (i = AGE_RESET_TIMEOUT; i > 0; i--) {
		if ((CSR_READ_4(sc, AGE_IDLE_STATUS) &
		    (IDLE_STATUS_TXMAC | IDLE_STATUS_DMARD)) == 0)
			break;
		DELAY(10);
	}
	if (i == 0)
		device_printf(sc->age_dev, "stopping TxMAC timeout!\n");
}

static void
age_stop_rxmac(struct age_softc *sc)
{
	uint32_t reg;
	int i;

	AGE_LOCK_ASSERT(sc);

	reg = CSR_READ_4(sc, AGE_MAC_CFG);
	if ((reg & MAC_CFG_RX_ENB) != 0) {
		reg &= ~MAC_CFG_RX_ENB;
		CSR_WRITE_4(sc, AGE_MAC_CFG, reg);
	}
	/* Stop Rx DMA engine. */
	reg = CSR_READ_4(sc, AGE_DMA_CFG);
	if ((reg & DMA_CFG_WR_ENB) != 0) {
		reg &= ~DMA_CFG_WR_ENB;
		CSR_WRITE_4(sc, AGE_DMA_CFG, reg);
	}
	for (i = AGE_RESET_TIMEOUT; i > 0; i--) {
		if ((CSR_READ_4(sc, AGE_IDLE_STATUS) &
		    (IDLE_STATUS_RXMAC | IDLE_STATUS_DMAWR)) == 0)
			break;
		DELAY(10);
	}
	if (i == 0)
		device_printf(sc->age_dev, "stopping RxMAC timeout!\n");
}

static void
age_init_tx_ring(struct age_softc *sc)
{
	struct age_ring_data *rd;
	struct age_txdesc *txd;
	int i;

	AGE_LOCK_ASSERT(sc);

	sc->age_cdata.age_tx_prod = 0;
	sc->age_cdata.age_tx_cons = 0;
	sc->age_cdata.age_tx_cnt = 0;

	rd = &sc->age_rdata;
	bzero(rd->age_tx_ring, AGE_TX_RING_SZ);
	for (i = 0; i < AGE_TX_RING_CNT; i++) {
		txd = &sc->age_cdata.age_txdesc[i];
		txd->tx_desc = &rd->age_tx_ring[i];
		txd->tx_m = NULL;
	}

	bus_dmamap_sync(sc->age_cdata.age_tx_ring_tag,
	    sc->age_cdata.age_tx_ring_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}

static int
age_init_rx_ring(struct age_softc *sc)
{
	struct age_ring_data *rd;
	struct age_rxdesc *rxd;
	int i;

	AGE_LOCK_ASSERT(sc);

	sc->age_cdata.age_rx_cons = AGE_RX_RING_CNT - 1;
	sc->age_morework = 0;
	rd = &sc->age_rdata;
	bzero(rd->age_rx_ring, AGE_RX_RING_SZ);
	for (i = 0; i < AGE_RX_RING_CNT; i++) {
		rxd = &sc->age_cdata.age_rxdesc[i];
		rxd->rx_m = NULL;
		rxd->rx_desc = &rd->age_rx_ring[i];
		if (age_newbuf(sc, rxd) != 0)
			return (ENOBUFS);
	}

	bus_dmamap_sync(sc->age_cdata.age_rx_ring_tag,
	    sc->age_cdata.age_rx_ring_map, BUS_DMASYNC_PREWRITE);

	return (0);
}

static void
age_init_rr_ring(struct age_softc *sc)
{
	struct age_ring_data *rd;

	AGE_LOCK_ASSERT(sc);

	sc->age_cdata.age_rr_cons = 0;
	AGE_RXCHAIN_RESET(sc);

	rd = &sc->age_rdata;
	bzero(rd->age_rr_ring, AGE_RR_RING_SZ);
	bus_dmamap_sync(sc->age_cdata.age_rr_ring_tag,
	    sc->age_cdata.age_rr_ring_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}

static void
age_init_cmb_block(struct age_softc *sc)
{
	struct age_ring_data *rd;

	AGE_LOCK_ASSERT(sc);

	rd = &sc->age_rdata;
	bzero(rd->age_cmb_block, AGE_CMB_BLOCK_SZ);
	bus_dmamap_sync(sc->age_cdata.age_cmb_block_tag,
	    sc->age_cdata.age_cmb_block_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}

static void
age_init_smb_block(struct age_softc *sc)
{
	struct age_ring_data *rd;

	AGE_LOCK_ASSERT(sc);

	rd = &sc->age_rdata;
	bzero(rd->age_smb_block, AGE_SMB_BLOCK_SZ);
	bus_dmamap_sync(sc->age_cdata.age_smb_block_tag,
	    sc->age_cdata.age_smb_block_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}

static int
age_newbuf(struct age_softc *sc, struct age_rxdesc *rxd)
{
	struct rx_desc *desc;
	struct mbuf *m;
	bus_dma_segment_t segs[1];
	bus_dmamap_t map;
	int nsegs;

	AGE_LOCK_ASSERT(sc);

	m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
	if (m == NULL)
		return (ENOBUFS);
	m->m_len = m->m_pkthdr.len = MCLBYTES;
#ifndef __NO_STRICT_ALIGNMENT
	m_adj(m, AGE_RX_BUF_ALIGN);
#endif

	if (bus_dmamap_load_mbuf_sg(sc->age_cdata.age_rx_tag,
	    sc->age_cdata.age_rx_sparemap, m, segs, &nsegs, 0) != 0) {
		m_freem(m);
		return (ENOBUFS);
	}
	KASSERT(nsegs == 1, ("%s: %d segments returned!", __func__, nsegs));

	if (rxd->rx_m != NULL) {
		bus_dmamap_sync(sc->age_cdata.age_rx_tag, rxd->rx_dmamap,
		    BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->age_cdata.age_rx_tag, rxd->rx_dmamap);
	}
	map = rxd->rx_dmamap;
	rxd->rx_dmamap = sc->age_cdata.age_rx_sparemap;
	sc->age_cdata.age_rx_sparemap = map;
	bus_dmamap_sync(sc->age_cdata.age_rx_tag, rxd->rx_dmamap,
	    BUS_DMASYNC_PREREAD);
	rxd->rx_m = m;

	desc = rxd->rx_desc;
	desc->addr = htole64(segs[0].ds_addr);
	desc->len = htole32((segs[0].ds_len & AGE_RD_LEN_MASK) <<
	    AGE_RD_LEN_SHIFT);
	return (0);
}

static void
age_rxvlan(struct age_softc *sc)
{
	struct ifnet *ifp;
	uint32_t reg;

	AGE_LOCK_ASSERT(sc);

	ifp = sc->age_ifp;
	reg = CSR_READ_4(sc, AGE_MAC_CFG);
	reg &= ~MAC_CFG_VLAN_TAG_STRIP;
	if ((ifp->if_capenable & IFCAP_VLAN_HWTAGGING) != 0)
		reg |= MAC_CFG_VLAN_TAG_STRIP;
	CSR_WRITE_4(sc, AGE_MAC_CFG, reg);
}

static void
age_rxfilter(struct age_softc *sc)
{
	struct ifnet *ifp;
	struct ifmultiaddr *ifma;
	uint32_t crc;
	uint32_t mchash[2];
	uint32_t rxcfg;

	AGE_LOCK_ASSERT(sc);

	ifp = sc->age_ifp;

	rxcfg = CSR_READ_4(sc, AGE_MAC_CFG);
	rxcfg &= ~(MAC_CFG_ALLMULTI | MAC_CFG_BCAST | MAC_CFG_PROMISC);
	if ((ifp->if_flags & IFF_BROADCAST) != 0)
		rxcfg |= MAC_CFG_BCAST;
	if ((ifp->if_flags & (IFF_PROMISC | IFF_ALLMULTI)) != 0) {
		if ((ifp->if_flags & IFF_PROMISC) != 0)
			rxcfg |= MAC_CFG_PROMISC;
		if ((ifp->if_flags & IFF_ALLMULTI) != 0)
			rxcfg |= MAC_CFG_ALLMULTI;
		CSR_WRITE_4(sc, AGE_MAR0, 0xFFFFFFFF);
		CSR_WRITE_4(sc, AGE_MAR1, 0xFFFFFFFF);
		CSR_WRITE_4(sc, AGE_MAC_CFG, rxcfg);
		return;
	}

	/* Program new filter. */
	bzero(mchash, sizeof(mchash));

	if_maddr_rlock(ifp);
	CK_STAILQ_FOREACH(ifma, &sc->age_ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		crc = ether_crc32_be(LLADDR((struct sockaddr_dl *)
		    ifma->ifma_addr), ETHER_ADDR_LEN);
		mchash[crc >> 31] |= 1 << ((crc >> 26) & 0x1f);
	}
	if_maddr_runlock(ifp);

	CSR_WRITE_4(sc, AGE_MAR0, mchash[0]);
	CSR_WRITE_4(sc, AGE_MAR1, mchash[1]);
	CSR_WRITE_4(sc, AGE_MAC_CFG, rxcfg);
}

static int
sysctl_age_stats(SYSCTL_HANDLER_ARGS)
{
	struct age_softc *sc;
	struct age_stats *stats;
	int error, result;

	result = -1;
	error = sysctl_handle_int(oidp, &result, 0, req);

	if (error != 0 || req->newptr == NULL)
		return (error);

	if (result != 1)
		return (error);

	sc = (struct age_softc *)arg1;
	stats = &sc->age_stat;
	printf("%s statistics:\n", device_get_nameunit(sc->age_dev));
	printf("Transmit good frames : %ju\n",
	    (uintmax_t)stats->tx_frames);
	printf("Transmit good broadcast frames : %ju\n",
	    (uintmax_t)stats->tx_bcast_frames);
	printf("Transmit good multicast frames : %ju\n",
	    (uintmax_t)stats->tx_mcast_frames);
	printf("Transmit pause control frames : %u\n",
	    stats->tx_pause_frames);
	printf("Transmit control frames : %u\n",
	    stats->tx_control_frames);
	printf("Transmit frames with excessive deferrals : %u\n",
	    stats->tx_excess_defer);
	printf("Transmit deferrals : %u\n",
	    stats->tx_deferred);
	printf("Transmit good octets : %ju\n",
	    (uintmax_t)stats->tx_bytes);
	printf("Transmit good broadcast octets : %ju\n",
	    (uintmax_t)stats->tx_bcast_bytes);
	printf("Transmit good multicast octets : %ju\n",
	    (uintmax_t)stats->tx_mcast_bytes);
	printf("Transmit frames 64 bytes : %ju\n",
	    (uintmax_t)stats->tx_pkts_64);
	printf("Transmit frames 65 to 127 bytes : %ju\n",
	    (uintmax_t)stats->tx_pkts_65_127);
	printf("Transmit frames 128 to 255 bytes : %ju\n",
	    (uintmax_t)stats->tx_pkts_128_255);
	printf("Transmit frames 256 to 511 bytes : %ju\n",
	    (uintmax_t)stats->tx_pkts_256_511);
	printf("Transmit frames 512 to 1024 bytes : %ju\n",
	    (uintmax_t)stats->tx_pkts_512_1023);
	printf("Transmit frames 1024 to 1518 bytes : %ju\n",
	    (uintmax_t)stats->tx_pkts_1024_1518);
	printf("Transmit frames 1519 to MTU bytes : %ju\n",
	    (uintmax_t)stats->tx_pkts_1519_max);
	printf("Transmit single collisions : %u\n",
	    stats->tx_single_colls);
	printf("Transmit multiple collisions : %u\n",
	    stats->tx_multi_colls);
	printf("Transmit late collisions : %u\n",
	    stats->tx_late_colls);
	printf("Transmit abort due to excessive collisions : %u\n",
	    stats->tx_excess_colls);
	printf("Transmit underruns due to FIFO underruns : %u\n",
	    stats->tx_underrun);
	printf("Transmit descriptor write-back errors : %u\n",
	    stats->tx_desc_underrun);
	printf("Transmit frames with length mismatched frame size : %u\n",
	    stats->tx_lenerrs);
	printf("Transmit frames with truncated due to MTU size : %u\n",
	    stats->tx_lenerrs);

	printf("Receive good frames : %ju\n",
	    (uintmax_t)stats->rx_frames);
	printf("Receive good broadcast frames : %ju\n",
	    (uintmax_t)stats->rx_bcast_frames);
	printf("Receive good multicast frames : %ju\n",
	    (uintmax_t)stats->rx_mcast_frames);
	printf("Receive pause control frames : %u\n",
	    stats->rx_pause_frames);
	printf("Receive control frames : %u\n",
	    stats->rx_control_frames);
	printf("Receive CRC errors : %u\n",
	    stats->rx_crcerrs);
	printf("Receive frames with length errors : %u\n",
	    stats->rx_lenerrs);
	printf("Receive good octets : %ju\n",
	    (uintmax_t)stats->rx_bytes);
	printf("Receive good broadcast octets : %ju\n",
	    (uintmax_t)stats->rx_bcast_bytes);
	printf("Receive good multicast octets : %ju\n",
	    (uintmax_t)stats->rx_mcast_bytes);
	printf("Receive frames too short : %u\n",
	    stats->rx_runts);
	printf("Receive fragmented frames : %ju\n",
	    (uintmax_t)stats->rx_fragments);
	printf("Receive frames 64 bytes : %ju\n",
	    (uintmax_t)stats->rx_pkts_64);
	printf("Receive frames 65 to 127 bytes : %ju\n",
	    (uintmax_t)stats->rx_pkts_65_127);
	printf("Receive frames 128 to 255 bytes : %ju\n",
	    (uintmax_t)stats->rx_pkts_128_255);
	printf("Receive frames 256 to 511 bytes : %ju\n",
	    (uintmax_t)stats->rx_pkts_256_511);
	printf("Receive frames 512 to 1024 bytes : %ju\n",
	    (uintmax_t)stats->rx_pkts_512_1023);
	printf("Receive frames 1024 to 1518 bytes : %ju\n",
	    (uintmax_t)stats->rx_pkts_1024_1518);
	printf("Receive frames 1519 to MTU bytes : %ju\n",
	    (uintmax_t)stats->rx_pkts_1519_max);
	printf("Receive frames too long : %ju\n",
	    (uint64_t)stats->rx_pkts_truncated);
	printf("Receive frames with FIFO overflow : %u\n",
	    stats->rx_fifo_oflows);
	printf("Receive frames with return descriptor overflow : %u\n",
	    stats->rx_desc_oflows);
	printf("Receive frames with alignment errors : %u\n",
	    stats->rx_alignerrs);
	printf("Receive frames dropped due to address filtering : %ju\n",
	    (uint64_t)stats->rx_pkts_filtered);

	return (error);
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
sysctl_hw_age_proc_limit(SYSCTL_HANDLER_ARGS)
{
	return (sysctl_int_range(oidp, arg1, arg2, req,
	    AGE_PROC_MIN, AGE_PROC_MAX));
}

static int
sysctl_hw_age_int_mod(SYSCTL_HANDLER_ARGS)
{

	return (sysctl_int_range(oidp, arg1, arg2, req, AGE_IM_TIMER_MIN,
	    AGE_IM_TIMER_MAX));
}
