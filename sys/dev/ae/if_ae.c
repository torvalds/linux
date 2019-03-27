/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Stanislav Sedov <stas@FreeBSD.org>.
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Driver for Attansic Technology Corp. L2 FastEthernet adapter.
 *
 * This driver is heavily based on age(4) Attansic L1 driver by Pyun YongHyeon.
 */

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
#include <sys/mutex.h>
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

#include "miibus_if.h"

#include "if_aereg.h"
#include "if_aevar.h"

/*
 * Devices supported by this driver.
 */
static struct ae_dev {
	uint16_t	vendorid;
	uint16_t	deviceid;
	const char	*name;
} ae_devs[] = {
	{ VENDORID_ATTANSIC, DEVICEID_ATTANSIC_L2,
		"Attansic Technology Corp, L2 FastEthernet" },
};
#define	AE_DEVS_COUNT nitems(ae_devs)

static struct resource_spec ae_res_spec_mem[] = {
	{ SYS_RES_MEMORY,       PCIR_BAR(0),    RF_ACTIVE },
	{ -1,			0,		0 }
};
static struct resource_spec ae_res_spec_irq[] = {
	{ SYS_RES_IRQ,		0,		RF_ACTIVE | RF_SHAREABLE },
	{ -1,			0,		0 }
};
static struct resource_spec ae_res_spec_msi[] = {
	{ SYS_RES_IRQ,		1,		RF_ACTIVE },
	{ -1,			0,		0 }
};

static int	ae_probe(device_t dev);
static int	ae_attach(device_t dev);
static void	ae_pcie_init(ae_softc_t *sc);
static void	ae_phy_reset(ae_softc_t *sc);
static void	ae_phy_init(ae_softc_t *sc);
static int	ae_reset(ae_softc_t *sc);
static void	ae_init(void *arg);
static int	ae_init_locked(ae_softc_t *sc);
static int	ae_detach(device_t dev);
static int	ae_miibus_readreg(device_t dev, int phy, int reg);
static int	ae_miibus_writereg(device_t dev, int phy, int reg, int val);
static void	ae_miibus_statchg(device_t dev);
static void	ae_mediastatus(struct ifnet *ifp, struct ifmediareq *ifmr);
static int	ae_mediachange(struct ifnet *ifp);
static void	ae_retrieve_address(ae_softc_t *sc);
static void	ae_dmamap_cb(void *arg, bus_dma_segment_t *segs, int nsegs,
    int error);
static int	ae_alloc_rings(ae_softc_t *sc);
static void	ae_dma_free(ae_softc_t *sc);
static int	ae_shutdown(device_t dev);
static int	ae_suspend(device_t dev);
static void	ae_powersave_disable(ae_softc_t *sc);
static void	ae_powersave_enable(ae_softc_t *sc);
static int	ae_resume(device_t dev);
static unsigned int	ae_tx_avail_size(ae_softc_t *sc);
static int	ae_encap(ae_softc_t *sc, struct mbuf **m_head);
static void	ae_start(struct ifnet *ifp);
static void	ae_start_locked(struct ifnet *ifp);
static void	ae_link_task(void *arg, int pending);
static void	ae_stop_rxmac(ae_softc_t *sc);
static void	ae_stop_txmac(ae_softc_t *sc);
static void	ae_mac_config(ae_softc_t *sc);
static int	ae_intr(void *arg);
static void	ae_int_task(void *arg, int pending);
static void	ae_tx_intr(ae_softc_t *sc);
static void	ae_rxeof(ae_softc_t *sc, ae_rxd_t *rxd);
static void	ae_rx_intr(ae_softc_t *sc);
static void	ae_watchdog(ae_softc_t *sc);
static void	ae_tick(void *arg);
static void	ae_rxfilter(ae_softc_t *sc);
static void	ae_rxvlan(ae_softc_t *sc);
static int	ae_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data);
static void	ae_stop(ae_softc_t *sc);
static int	ae_check_eeprom_present(ae_softc_t *sc, int *vpdc);
static int	ae_vpd_read_word(ae_softc_t *sc, int reg, uint32_t *word);
static int	ae_get_vpd_eaddr(ae_softc_t *sc, uint32_t *eaddr);
static int	ae_get_reg_eaddr(ae_softc_t *sc, uint32_t *eaddr);
static void	ae_update_stats_rx(uint16_t flags, ae_stats_t *stats);
static void	ae_update_stats_tx(uint16_t flags, ae_stats_t *stats);
static void	ae_init_tunables(ae_softc_t *sc);

static device_method_t ae_methods[] = {
	/* Device interface. */
	DEVMETHOD(device_probe,		ae_probe),
	DEVMETHOD(device_attach,	ae_attach),
	DEVMETHOD(device_detach,	ae_detach),
	DEVMETHOD(device_shutdown,	ae_shutdown),
	DEVMETHOD(device_suspend,	ae_suspend),
	DEVMETHOD(device_resume,	ae_resume),

	/* MII interface. */
	DEVMETHOD(miibus_readreg,	ae_miibus_readreg),
	DEVMETHOD(miibus_writereg,	ae_miibus_writereg),
	DEVMETHOD(miibus_statchg,	ae_miibus_statchg),

	{ NULL, NULL }
};
static driver_t ae_driver = {
        "ae",
        ae_methods,
        sizeof(ae_softc_t)
};
static devclass_t ae_devclass;

DRIVER_MODULE(ae, pci, ae_driver, ae_devclass, 0, 0);
MODULE_PNP_INFO("U16:vendor;U16:device;D:#", pci, ae, ae_devs,
    nitems(ae_devs));
DRIVER_MODULE(miibus, ae, miibus_driver, miibus_devclass, 0, 0);
MODULE_DEPEND(ae, pci, 1, 1, 1);
MODULE_DEPEND(ae, ether, 1, 1, 1);
MODULE_DEPEND(ae, miibus, 1, 1, 1);

/*
 * Tunables.
 */
static int msi_disable = 0;
TUNABLE_INT("hw.ae.msi_disable", &msi_disable);

#define	AE_READ_4(sc, reg) \
	bus_read_4((sc)->mem[0], (reg))
#define	AE_READ_2(sc, reg) \
	bus_read_2((sc)->mem[0], (reg))
#define	AE_READ_1(sc, reg) \
	bus_read_1((sc)->mem[0], (reg))
#define	AE_WRITE_4(sc, reg, val) \
	bus_write_4((sc)->mem[0], (reg), (val))
#define	AE_WRITE_2(sc, reg, val) \
	bus_write_2((sc)->mem[0], (reg), (val))
#define	AE_WRITE_1(sc, reg, val) \
	bus_write_1((sc)->mem[0], (reg), (val))
#define	AE_PHY_READ(sc, reg) \
	ae_miibus_readreg(sc->dev, 0, reg)
#define	AE_PHY_WRITE(sc, reg, val) \
	ae_miibus_writereg(sc->dev, 0, reg, val)
#define	AE_CHECK_EADDR_VALID(eaddr) \
	((eaddr[0] == 0 && eaddr[1] == 0) || \
	(eaddr[0] == 0xffffffff && eaddr[1] == 0xffff))
#define	AE_RXD_VLAN(vtag) \
	(((vtag) >> 4) | (((vtag) & 0x07) << 13) | (((vtag) & 0x08) << 9))
#define	AE_TXD_VLAN(vtag) \
	(((vtag) << 4) | (((vtag) >> 13) & 0x07) | (((vtag) >> 9) & 0x08))

static int
ae_probe(device_t dev)
{
	uint16_t deviceid, vendorid;
	int i;

	vendorid = pci_get_vendor(dev);
	deviceid = pci_get_device(dev);

	/*
	 * Search through the list of supported devs for matching one.
	 */
	for (i = 0; i < AE_DEVS_COUNT; i++) {
		if (vendorid == ae_devs[i].vendorid &&
		    deviceid == ae_devs[i].deviceid) {
			device_set_desc(dev, ae_devs[i].name);
			return (BUS_PROBE_DEFAULT);
		}
	}
	return (ENXIO);
}

static int
ae_attach(device_t dev)
{
	ae_softc_t *sc;
	struct ifnet *ifp;
	uint8_t chiprev;
	uint32_t pcirev;
	int nmsi, pmc;
	int error;

	sc = device_get_softc(dev); /* Automatically allocated and zeroed
				       on attach. */
	KASSERT(sc != NULL, ("[ae, %d]: sc is NULL", __LINE__));
	sc->dev = dev;

	/*
	 * Initialize mutexes and tasks.
	 */
	mtx_init(&sc->mtx, device_get_nameunit(dev), MTX_NETWORK_LOCK, MTX_DEF);
	callout_init_mtx(&sc->tick_ch, &sc->mtx, 0);
	TASK_INIT(&sc->int_task, 0, ae_int_task, sc);
	TASK_INIT(&sc->link_task, 0, ae_link_task, sc);

	pci_enable_busmaster(dev);		/* Enable bus mastering. */

	sc->spec_mem = ae_res_spec_mem;

	/*
	 * Allocate memory-mapped registers.
	 */
	error = bus_alloc_resources(dev, sc->spec_mem, sc->mem);
	if (error != 0) {
		device_printf(dev, "could not allocate memory resources.\n");
		sc->spec_mem = NULL;
		goto fail;
	}

	/*
	 * Retrieve PCI and chip revisions.
	 */
	pcirev = pci_get_revid(dev);
	chiprev = (AE_READ_4(sc, AE_MASTER_REG) >> AE_MASTER_REVNUM_SHIFT) &
	    AE_MASTER_REVNUM_MASK;
	if (bootverbose) {
		device_printf(dev, "pci device revision: %#04x\n", pcirev);
		device_printf(dev, "chip id: %#02x\n", chiprev);
	}
	nmsi = pci_msi_count(dev);
	if (bootverbose)
		device_printf(dev, "MSI count: %d.\n", nmsi);

	/*
	 * Allocate interrupt resources.
	 */
	if (msi_disable == 0 && nmsi == 1) {
		error = pci_alloc_msi(dev, &nmsi);
		if (error == 0) {
			device_printf(dev, "Using MSI messages.\n");
			sc->spec_irq = ae_res_spec_msi;
			error = bus_alloc_resources(dev, sc->spec_irq, sc->irq);
			if (error != 0) {
				device_printf(dev, "MSI allocation failed.\n");
				sc->spec_irq = NULL;
				pci_release_msi(dev);
			} else {
				sc->flags |= AE_FLAG_MSI;
			}
		}
	}
	if (sc->spec_irq == NULL) {
		sc->spec_irq = ae_res_spec_irq;
		error = bus_alloc_resources(dev, sc->spec_irq, sc->irq);
		if (error != 0) {
			device_printf(dev, "could not allocate IRQ resources.\n");
			sc->spec_irq = NULL;
			goto fail;
		}
	}
	
	ae_init_tunables(sc);

	ae_phy_reset(sc);		/* Reset PHY. */
	error = ae_reset(sc);		/* Reset the controller itself. */
	if (error != 0)
		goto fail;

	ae_pcie_init(sc);

	ae_retrieve_address(sc);	/* Load MAC address. */

	error = ae_alloc_rings(sc);	/* Allocate ring buffers. */
	if (error != 0)
		goto fail;

	ifp = sc->ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(dev, "could not allocate ifnet structure.\n");
		error = ENXIO;
		goto fail;
	}

	ifp->if_softc = sc;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = ae_ioctl;
	ifp->if_start = ae_start;
	ifp->if_init = ae_init;
	ifp->if_capabilities = IFCAP_VLAN_MTU | IFCAP_VLAN_HWTAGGING;
	ifp->if_hwassist = 0;
	ifp->if_snd.ifq_drv_maxlen = ifqmaxlen;
	IFQ_SET_MAXLEN(&ifp->if_snd, ifp->if_snd.ifq_drv_maxlen);
	IFQ_SET_READY(&ifp->if_snd);
	if (pci_find_cap(dev, PCIY_PMG, &pmc) == 0) {
		ifp->if_capabilities |= IFCAP_WOL_MAGIC;
		sc->flags |= AE_FLAG_PMG;
	}
	ifp->if_capenable = ifp->if_capabilities;

	/*
	 * Configure and attach MII bus.
	 */
	error = mii_attach(dev, &sc->miibus, ifp, ae_mediachange,
	    ae_mediastatus, BMSR_DEFCAPMASK, AE_PHYADDR_DEFAULT,
	    MII_OFFSET_ANY, 0);
	if (error != 0) {
		device_printf(dev, "attaching PHYs failed\n");
		goto fail;
	}

	ether_ifattach(ifp, sc->eaddr);
	/* Tell the upper layer(s) we support long frames. */
	ifp->if_hdrlen = sizeof(struct ether_vlan_header);

	/*
	 * Create and run all helper tasks.
	 */
	sc->tq = taskqueue_create_fast("ae_taskq", M_WAITOK,
            taskqueue_thread_enqueue, &sc->tq);
	if (sc->tq == NULL) {
		device_printf(dev, "could not create taskqueue.\n");
		ether_ifdetach(ifp);
		error = ENXIO;
		goto fail;
	}
	taskqueue_start_threads(&sc->tq, 1, PI_NET, "%s taskq",
	    device_get_nameunit(sc->dev));

	/*
	 * Configure interrupt handlers.
	 */
	error = bus_setup_intr(dev, sc->irq[0], INTR_TYPE_NET | INTR_MPSAFE,
	    ae_intr, NULL, sc, &sc->intrhand);
	if (error != 0) {
		device_printf(dev, "could not set up interrupt handler.\n");
		taskqueue_free(sc->tq);
		sc->tq = NULL;
		ether_ifdetach(ifp);
		goto fail;
	}

	gone_by_fcp101_dev(dev);

fail:
	if (error != 0)
		ae_detach(dev);
	
	return (error);
}

#define	AE_SYSCTL(stx, parent, name, desc, ptr)	\
	SYSCTL_ADD_UINT(ctx, parent, OID_AUTO, name, CTLFLAG_RD, ptr, 0, desc)

static void
ae_init_tunables(ae_softc_t *sc)
{
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *root, *stats, *stats_rx, *stats_tx;
	struct ae_stats *ae_stats;

	KASSERT(sc != NULL, ("[ae, %d]: sc is NULL", __LINE__));
	ae_stats = &sc->stats;

	ctx = device_get_sysctl_ctx(sc->dev);
	root = device_get_sysctl_tree(sc->dev);
	stats = SYSCTL_ADD_NODE(ctx, SYSCTL_CHILDREN(root), OID_AUTO, "stats",
	    CTLFLAG_RD, NULL, "ae statistics");

	/*
	 * Receiver statistcics.
	 */
	stats_rx = SYSCTL_ADD_NODE(ctx, SYSCTL_CHILDREN(stats), OID_AUTO, "rx",
	    CTLFLAG_RD, NULL, "Rx MAC statistics");
	AE_SYSCTL(ctx, SYSCTL_CHILDREN(stats_rx), "bcast",
	    "broadcast frames", &ae_stats->rx_bcast);
	AE_SYSCTL(ctx, SYSCTL_CHILDREN(stats_rx), "mcast",
	    "multicast frames", &ae_stats->rx_mcast);
	AE_SYSCTL(ctx, SYSCTL_CHILDREN(stats_rx), "pause",
	    "PAUSE frames", &ae_stats->rx_pause);
	AE_SYSCTL(ctx, SYSCTL_CHILDREN(stats_rx), "control",
	    "control frames", &ae_stats->rx_ctrl);
	AE_SYSCTL(ctx, SYSCTL_CHILDREN(stats_rx), "crc_errors",
	    "frames with CRC errors", &ae_stats->rx_crcerr);
	AE_SYSCTL(ctx, SYSCTL_CHILDREN(stats_rx), "code_errors",
	    "frames with invalid opcode", &ae_stats->rx_codeerr);
	AE_SYSCTL(ctx, SYSCTL_CHILDREN(stats_rx), "runt",
	    "runt frames", &ae_stats->rx_runt);
	AE_SYSCTL(ctx, SYSCTL_CHILDREN(stats_rx), "frag",
	    "fragmented frames", &ae_stats->rx_frag);
	AE_SYSCTL(ctx, SYSCTL_CHILDREN(stats_rx), "align_errors",
	    "frames with alignment errors", &ae_stats->rx_align);
	AE_SYSCTL(ctx, SYSCTL_CHILDREN(stats_rx), "truncated",
	    "frames truncated due to Rx FIFO inderrun", &ae_stats->rx_trunc);

	/*
	 * Receiver statistcics.
	 */
	stats_tx = SYSCTL_ADD_NODE(ctx, SYSCTL_CHILDREN(stats), OID_AUTO, "tx",
	    CTLFLAG_RD, NULL, "Tx MAC statistics");
	AE_SYSCTL(ctx, SYSCTL_CHILDREN(stats_tx), "bcast",
	    "broadcast frames", &ae_stats->tx_bcast);
	AE_SYSCTL(ctx, SYSCTL_CHILDREN(stats_tx), "mcast",
	    "multicast frames", &ae_stats->tx_mcast);
	AE_SYSCTL(ctx, SYSCTL_CHILDREN(stats_tx), "pause",
	    "PAUSE frames", &ae_stats->tx_pause);
	AE_SYSCTL(ctx, SYSCTL_CHILDREN(stats_tx), "control",
	    "control frames", &ae_stats->tx_ctrl);
	AE_SYSCTL(ctx, SYSCTL_CHILDREN(stats_tx), "defers",
	    "deferrals occuried", &ae_stats->tx_defer);
	AE_SYSCTL(ctx, SYSCTL_CHILDREN(stats_tx), "exc_defers",
	    "excessive deferrals occuried", &ae_stats->tx_excdefer);
	AE_SYSCTL(ctx, SYSCTL_CHILDREN(stats_tx), "singlecols",
	    "single collisions occuried", &ae_stats->tx_singlecol);
	AE_SYSCTL(ctx, SYSCTL_CHILDREN(stats_tx), "multicols",
	    "multiple collisions occuried", &ae_stats->tx_multicol);
	AE_SYSCTL(ctx, SYSCTL_CHILDREN(stats_tx), "latecols",
	    "late collisions occuried", &ae_stats->tx_latecol);
	AE_SYSCTL(ctx, SYSCTL_CHILDREN(stats_tx), "aborts",
	    "transmit aborts due collisions", &ae_stats->tx_abortcol);
	AE_SYSCTL(ctx, SYSCTL_CHILDREN(stats_tx), "underruns",
	    "Tx FIFO underruns", &ae_stats->tx_underrun);
}

static void
ae_pcie_init(ae_softc_t *sc)
{

	AE_WRITE_4(sc, AE_PCIE_LTSSM_TESTMODE_REG, AE_PCIE_LTSSM_TESTMODE_DEFAULT);
	AE_WRITE_4(sc, AE_PCIE_DLL_TX_CTRL_REG, AE_PCIE_DLL_TX_CTRL_DEFAULT);
}

static void
ae_phy_reset(ae_softc_t *sc)
{

	AE_WRITE_4(sc, AE_PHY_ENABLE_REG, AE_PHY_ENABLE);
	DELAY(1000);	/* XXX: pause(9) ? */
}

static int
ae_reset(ae_softc_t *sc)
{
	int i;

	/*
	 * Issue a soft reset.
	 */
	AE_WRITE_4(sc, AE_MASTER_REG, AE_MASTER_SOFT_RESET);
	bus_barrier(sc->mem[0], AE_MASTER_REG, 4,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	
	/*
	 * Wait for reset to complete.
	 */
	for (i = 0; i < AE_RESET_TIMEOUT; i++) {
		if ((AE_READ_4(sc, AE_MASTER_REG) & AE_MASTER_SOFT_RESET) == 0)
			break;
		DELAY(10);
	}
	if (i == AE_RESET_TIMEOUT) {
		device_printf(sc->dev, "reset timeout.\n");
		return (ENXIO);
	}

	/*
	 * Wait for everything to enter idle state.
	 */
	for (i = 0; i < AE_IDLE_TIMEOUT; i++) {
		if (AE_READ_4(sc, AE_IDLE_REG) == 0)
			break;
		DELAY(100);
	}
	if (i == AE_IDLE_TIMEOUT) {
		device_printf(sc->dev, "could not enter idle state.\n");
		return (ENXIO);
	}
	return (0);
}

static void
ae_init(void *arg)
{
	ae_softc_t *sc;

	sc = (ae_softc_t *)arg;
	AE_LOCK(sc);
	ae_init_locked(sc);
	AE_UNLOCK(sc);
}

static void
ae_phy_init(ae_softc_t *sc)
{

	/*
	 * Enable link status change interrupt.
	 * XXX magic numbers.
	 */
#ifdef notyet
	AE_PHY_WRITE(sc, 18, 0xc00);
#endif
}

static int
ae_init_locked(ae_softc_t *sc)
{
	struct ifnet *ifp;
	struct mii_data *mii;
	uint8_t eaddr[ETHER_ADDR_LEN];
	uint32_t val;
	bus_addr_t addr;

	AE_LOCK_ASSERT(sc);

	ifp = sc->ifp;
	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
		return (0);
	mii = device_get_softc(sc->miibus);

	ae_stop(sc);
	ae_reset(sc);
	ae_pcie_init(sc);		/* Initialize PCIE stuff. */
	ae_phy_init(sc);
	ae_powersave_disable(sc);

	/*
	 * Clear and disable interrupts.
	 */
	AE_WRITE_4(sc, AE_ISR_REG, 0xffffffff);

	/*
	 * Set the MAC address.
	 */
	bcopy(IF_LLADDR(ifp), eaddr, ETHER_ADDR_LEN);
	val = eaddr[2] << 24 | eaddr[3] << 16 | eaddr[4] << 8 | eaddr[5];
	AE_WRITE_4(sc, AE_EADDR0_REG, val);
	val = eaddr[0] << 8 | eaddr[1];
	AE_WRITE_4(sc, AE_EADDR1_REG, val);

	bzero(sc->rxd_base_dma, AE_RXD_COUNT_DEFAULT * 1536 + AE_RXD_PADDING);
	bzero(sc->txd_base, AE_TXD_BUFSIZE_DEFAULT);
	bzero(sc->txs_base, AE_TXS_COUNT_DEFAULT * 4);
	/*
	 * Set ring buffers base addresses.
	 */
	addr = sc->dma_rxd_busaddr;
	AE_WRITE_4(sc, AE_DESC_ADDR_HI_REG, BUS_ADDR_HI(addr));
	AE_WRITE_4(sc, AE_RXD_ADDR_LO_REG, BUS_ADDR_LO(addr));
	addr = sc->dma_txd_busaddr;
	AE_WRITE_4(sc, AE_TXD_ADDR_LO_REG, BUS_ADDR_LO(addr));
	addr = sc->dma_txs_busaddr;
	AE_WRITE_4(sc, AE_TXS_ADDR_LO_REG, BUS_ADDR_LO(addr));

	/*
	 * Configure ring buffers sizes.
	 */
	AE_WRITE_2(sc, AE_RXD_COUNT_REG, AE_RXD_COUNT_DEFAULT);
	AE_WRITE_2(sc, AE_TXD_BUFSIZE_REG, AE_TXD_BUFSIZE_DEFAULT / 4);
	AE_WRITE_2(sc, AE_TXS_COUNT_REG, AE_TXS_COUNT_DEFAULT);

	/*
	 * Configure interframe gap parameters.
	 */
	val = ((AE_IFG_TXIPG_DEFAULT << AE_IFG_TXIPG_SHIFT) &
	    AE_IFG_TXIPG_MASK) |
	    ((AE_IFG_RXIPG_DEFAULT << AE_IFG_RXIPG_SHIFT) &
	    AE_IFG_RXIPG_MASK) |
	    ((AE_IFG_IPGR1_DEFAULT << AE_IFG_IPGR1_SHIFT) &
	    AE_IFG_IPGR1_MASK) |
	    ((AE_IFG_IPGR2_DEFAULT << AE_IFG_IPGR2_SHIFT) &
	    AE_IFG_IPGR2_MASK);
	AE_WRITE_4(sc, AE_IFG_REG, val);

	/*
	 * Configure half-duplex operation.
	 */
	val = ((AE_HDPX_LCOL_DEFAULT << AE_HDPX_LCOL_SHIFT) &
	    AE_HDPX_LCOL_MASK) |
	    ((AE_HDPX_RETRY_DEFAULT << AE_HDPX_RETRY_SHIFT) &
	    AE_HDPX_RETRY_MASK) |
	    ((AE_HDPX_ABEBT_DEFAULT << AE_HDPX_ABEBT_SHIFT) &
	    AE_HDPX_ABEBT_MASK) |
	    ((AE_HDPX_JAMIPG_DEFAULT << AE_HDPX_JAMIPG_SHIFT) &
	    AE_HDPX_JAMIPG_MASK) | AE_HDPX_EXC_EN;
	AE_WRITE_4(sc, AE_HDPX_REG, val);

	/*
	 * Configure interrupt moderate timer.
	 */
	AE_WRITE_2(sc, AE_IMT_REG, AE_IMT_DEFAULT);
	val = AE_READ_4(sc, AE_MASTER_REG);
	val |= AE_MASTER_IMT_EN;
	AE_WRITE_4(sc, AE_MASTER_REG, val);

	/*
	 * Configure interrupt clearing timer.
	 */
	AE_WRITE_2(sc, AE_ICT_REG, AE_ICT_DEFAULT);

	/*
	 * Configure MTU.
	 */
	val = ifp->if_mtu + ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN +
	    ETHER_CRC_LEN;
	AE_WRITE_2(sc, AE_MTU_REG, val);

	/*
	 * Configure cut-through threshold.
	 */
	AE_WRITE_4(sc, AE_CUT_THRESH_REG, AE_CUT_THRESH_DEFAULT);

	/*
	 * Configure flow control.
	 */
	AE_WRITE_2(sc, AE_FLOW_THRESH_HI_REG, (AE_RXD_COUNT_DEFAULT / 8) * 7);
	AE_WRITE_2(sc, AE_FLOW_THRESH_LO_REG, (AE_RXD_COUNT_MIN / 8) >
	    (AE_RXD_COUNT_DEFAULT / 12) ? (AE_RXD_COUNT_MIN / 8) :
	    (AE_RXD_COUNT_DEFAULT / 12));

	/*
	 * Init mailboxes.
	 */
	sc->txd_cur = sc->rxd_cur = 0;
	sc->txs_ack = sc->txd_ack = 0;
	sc->rxd_cur = 0;
	AE_WRITE_2(sc, AE_MB_TXD_IDX_REG, sc->txd_cur);
	AE_WRITE_2(sc, AE_MB_RXD_IDX_REG, sc->rxd_cur);

	sc->tx_inproc = 0;	/* Number of packets the chip processes now. */
	sc->flags |= AE_FLAG_TXAVAIL;	/* Free Tx's available. */

	/*
	 * Enable DMA.
	 */
	AE_WRITE_1(sc, AE_DMAREAD_REG, AE_DMAREAD_EN);
	AE_WRITE_1(sc, AE_DMAWRITE_REG, AE_DMAWRITE_EN);

	/*
	 * Check if everything is OK.
	 */
	val = AE_READ_4(sc, AE_ISR_REG);
	if ((val & AE_ISR_PHY_LINKDOWN) != 0) {
		device_printf(sc->dev, "Initialization failed.\n");
		return (ENXIO);
	}

	/*
	 * Clear interrupt status.
	 */
	AE_WRITE_4(sc, AE_ISR_REG, 0x3fffffff);
	AE_WRITE_4(sc, AE_ISR_REG, 0x0);

	/*
	 * Enable interrupts.
	 */
	val = AE_READ_4(sc, AE_MASTER_REG);
	AE_WRITE_4(sc, AE_MASTER_REG, val | AE_MASTER_MANUAL_INT);
	AE_WRITE_4(sc, AE_IMR_REG, AE_IMR_DEFAULT);

	/*
	 * Disable WOL.
	 */
	AE_WRITE_4(sc, AE_WOL_REG, 0);

	/*
	 * Configure MAC.
	 */
	val = AE_MAC_TX_CRC_EN | AE_MAC_TX_AUTOPAD |
	    AE_MAC_FULL_DUPLEX | AE_MAC_CLK_PHY |
	    AE_MAC_TX_FLOW_EN | AE_MAC_RX_FLOW_EN |
	    ((AE_HALFBUF_DEFAULT << AE_HALFBUF_SHIFT) & AE_HALFBUF_MASK) |
	    ((AE_MAC_PREAMBLE_DEFAULT << AE_MAC_PREAMBLE_SHIFT) &
	    AE_MAC_PREAMBLE_MASK);
	AE_WRITE_4(sc, AE_MAC_REG, val);

	/*
	 * Configure Rx MAC.
	 */
	ae_rxfilter(sc);
	ae_rxvlan(sc);

	/*
	 * Enable Tx/Rx.
	 */
	val = AE_READ_4(sc, AE_MAC_REG);
	AE_WRITE_4(sc, AE_MAC_REG, val | AE_MAC_TX_EN | AE_MAC_RX_EN);

	sc->flags &= ~AE_FLAG_LINK;
	mii_mediachg(mii);	/* Switch to the current media. */

	callout_reset(&sc->tick_ch, hz, ae_tick, sc);

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

#ifdef AE_DEBUG
	device_printf(sc->dev, "Initialization complete.\n");
#endif

	return (0);
}

static int
ae_detach(device_t dev)
{
	struct ae_softc *sc;
	struct ifnet *ifp;

	sc = device_get_softc(dev);
	KASSERT(sc != NULL, ("[ae: %d]: sc is NULL", __LINE__));
	ifp = sc->ifp;
	if (device_is_attached(dev)) {
		AE_LOCK(sc);
		sc->flags |= AE_FLAG_DETACH;
		ae_stop(sc);
		AE_UNLOCK(sc);
		callout_drain(&sc->tick_ch);
		taskqueue_drain(sc->tq, &sc->int_task);
		taskqueue_drain(taskqueue_swi, &sc->link_task);
		ether_ifdetach(ifp);
	}
	if (sc->tq != NULL) {
		taskqueue_drain(sc->tq, &sc->int_task);
		taskqueue_free(sc->tq);
		sc->tq = NULL;
	}
	if (sc->miibus != NULL) {
		device_delete_child(dev, sc->miibus);
		sc->miibus = NULL;
	}
	bus_generic_detach(sc->dev);
	ae_dma_free(sc);
	if (sc->intrhand != NULL) {
		bus_teardown_intr(dev, sc->irq[0], sc->intrhand);
		sc->intrhand = NULL;
	}
	if (ifp != NULL) {
		if_free(ifp);
		sc->ifp = NULL;
	}
	if (sc->spec_irq != NULL)
		bus_release_resources(dev, sc->spec_irq, sc->irq);
	if (sc->spec_mem != NULL)
		bus_release_resources(dev, sc->spec_mem, sc->mem);
	if ((sc->flags & AE_FLAG_MSI) != 0)
		pci_release_msi(dev);
	mtx_destroy(&sc->mtx);

	return (0);
}

static int
ae_miibus_readreg(device_t dev, int phy, int reg)
{
	ae_softc_t *sc;
	uint32_t val;
	int i;

	sc = device_get_softc(dev);
	KASSERT(sc != NULL, ("[ae, %d]: sc is NULL", __LINE__));

	/*
	 * Locking is done in upper layers.
	 */

	val = ((reg << AE_MDIO_REGADDR_SHIFT) & AE_MDIO_REGADDR_MASK) |
	    AE_MDIO_START | AE_MDIO_READ | AE_MDIO_SUP_PREAMBLE |
	    ((AE_MDIO_CLK_25_4 << AE_MDIO_CLK_SHIFT) & AE_MDIO_CLK_MASK);
	AE_WRITE_4(sc, AE_MDIO_REG, val);

	/*
	 * Wait for operation to complete.
	 */
	for (i = 0; i < AE_MDIO_TIMEOUT; i++) {
		DELAY(2);
		val = AE_READ_4(sc, AE_MDIO_REG);
		if ((val & (AE_MDIO_START | AE_MDIO_BUSY)) == 0)
			break;
	}
	if (i == AE_MDIO_TIMEOUT) {
		device_printf(sc->dev, "phy read timeout: %d.\n", reg);
		return (0);
	}
	return ((val << AE_MDIO_DATA_SHIFT) & AE_MDIO_DATA_MASK);
}

static int
ae_miibus_writereg(device_t dev, int phy, int reg, int val)
{
	ae_softc_t *sc;
	uint32_t aereg;
	int i;

	sc = device_get_softc(dev);
	KASSERT(sc != NULL, ("[ae, %d]: sc is NULL", __LINE__));

	/*
	 * Locking is done in upper layers.
	 */

	aereg = ((reg << AE_MDIO_REGADDR_SHIFT) & AE_MDIO_REGADDR_MASK) |
	    AE_MDIO_START | AE_MDIO_SUP_PREAMBLE |
	    ((AE_MDIO_CLK_25_4 << AE_MDIO_CLK_SHIFT) & AE_MDIO_CLK_MASK) |
	    ((val << AE_MDIO_DATA_SHIFT) & AE_MDIO_DATA_MASK);
	AE_WRITE_4(sc, AE_MDIO_REG, aereg);

	/*
	 * Wait for operation to complete.
	 */
	for (i = 0; i < AE_MDIO_TIMEOUT; i++) {
		DELAY(2);
		aereg = AE_READ_4(sc, AE_MDIO_REG);
		if ((aereg & (AE_MDIO_START | AE_MDIO_BUSY)) == 0)
			break;
	}
	if (i == AE_MDIO_TIMEOUT) {
		device_printf(sc->dev, "phy write timeout: %d.\n", reg);
	}
	return (0);
}

static void
ae_miibus_statchg(device_t dev)
{
	ae_softc_t *sc;

	sc = device_get_softc(dev);
	taskqueue_enqueue(taskqueue_swi, &sc->link_task);
}

static void
ae_mediastatus(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	ae_softc_t *sc;
	struct mii_data *mii;

	sc = ifp->if_softc;
	KASSERT(sc != NULL, ("[ae, %d]: sc is NULL", __LINE__));

	AE_LOCK(sc);
	mii = device_get_softc(sc->miibus);
	mii_pollstat(mii);
	ifmr->ifm_status = mii->mii_media_status;
	ifmr->ifm_active = mii->mii_media_active;
	AE_UNLOCK(sc);
}

static int
ae_mediachange(struct ifnet *ifp)
{
	ae_softc_t *sc;
	struct mii_data *mii;
	struct mii_softc *mii_sc;
	int error;

	/* XXX: check IFF_UP ?? */
	sc = ifp->if_softc;
	KASSERT(sc != NULL, ("[ae, %d]: sc is NULL", __LINE__));
	AE_LOCK(sc);
	mii = device_get_softc(sc->miibus);
	LIST_FOREACH(mii_sc, &mii->mii_phys, mii_list)
		PHY_RESET(mii_sc);
	error = mii_mediachg(mii);
	AE_UNLOCK(sc);

	return (error);
}

static int
ae_check_eeprom_present(ae_softc_t *sc, int *vpdc)
{
	int error;
	uint32_t val;

	KASSERT(vpdc != NULL, ("[ae, %d]: vpdc is NULL!\n", __LINE__));

	/*
	 * Not sure why, but Linux does this.
	 */
	val = AE_READ_4(sc, AE_SPICTL_REG);
	if ((val & AE_SPICTL_VPD_EN) != 0) {
		val &= ~AE_SPICTL_VPD_EN;
		AE_WRITE_4(sc, AE_SPICTL_REG, val);
	}
	error = pci_find_cap(sc->dev, PCIY_VPD, vpdc);
	return (error);
}

static int
ae_vpd_read_word(ae_softc_t *sc, int reg, uint32_t *word)
{
	uint32_t val;
	int i;

	AE_WRITE_4(sc, AE_VPD_DATA_REG, 0);	/* Clear register value. */

	/*
	 * VPD registers start at offset 0x100. Read them.
	 */
	val = 0x100 + reg * 4;
	AE_WRITE_4(sc, AE_VPD_CAP_REG, (val << AE_VPD_CAP_ADDR_SHIFT) &
	    AE_VPD_CAP_ADDR_MASK);
	for (i = 0; i < AE_VPD_TIMEOUT; i++) {
		DELAY(2000);
		val = AE_READ_4(sc, AE_VPD_CAP_REG);
		if ((val & AE_VPD_CAP_DONE) != 0)
			break;
	}
	if (i == AE_VPD_TIMEOUT) {
		device_printf(sc->dev, "timeout reading VPD register %d.\n",
		    reg);
		return (ETIMEDOUT);
	}
	*word = AE_READ_4(sc, AE_VPD_DATA_REG);
	return (0);
}

static int
ae_get_vpd_eaddr(ae_softc_t *sc, uint32_t *eaddr)
{
	uint32_t word, reg, val;
	int error;
	int found;
	int vpdc;
	int i;

	KASSERT(sc != NULL, ("[ae, %d]: sc is NULL", __LINE__));
	KASSERT(eaddr != NULL, ("[ae, %d]: eaddr is NULL", __LINE__));

	/*
	 * Check for EEPROM.
	 */
	error = ae_check_eeprom_present(sc, &vpdc);
	if (error != 0)
		return (error);

	/*
	 * Read the VPD configuration space.
	 * Each register is prefixed with signature,
	 * so we can check if it is valid.
	 */
	for (i = 0, found = 0; i < AE_VPD_NREGS; i++) {
		error = ae_vpd_read_word(sc, i, &word);
		if (error != 0)
			break;

		/*
		 * Check signature.
		 */
		if ((word & AE_VPD_SIG_MASK) != AE_VPD_SIG)
			break;
		reg = word >> AE_VPD_REG_SHIFT;
		i++;	/* Move to the next word. */

		if (reg != AE_EADDR0_REG && reg != AE_EADDR1_REG)
			continue;

		error = ae_vpd_read_word(sc, i, &val);
		if (error != 0)
			break;
		if (reg == AE_EADDR0_REG)
			eaddr[0] = val;
		else
			eaddr[1] = val;
		found++;
	}

	if (found < 2)
		return (ENOENT);
	
	eaddr[1] &= 0xffff;	/* Only last 2 bytes are used. */
	if (AE_CHECK_EADDR_VALID(eaddr) != 0) {
		if (bootverbose)
			device_printf(sc->dev,
			    "VPD ethernet address registers are invalid.\n");
		return (EINVAL);
	}
	return (0);
}

static int
ae_get_reg_eaddr(ae_softc_t *sc, uint32_t *eaddr)
{

	/*
	 * BIOS is supposed to set this.
	 */
	eaddr[0] = AE_READ_4(sc, AE_EADDR0_REG);
	eaddr[1] = AE_READ_4(sc, AE_EADDR1_REG);
	eaddr[1] &= 0xffff;	/* Only last 2 bytes are used. */

	if (AE_CHECK_EADDR_VALID(eaddr) != 0) {
		if (bootverbose)
			device_printf(sc->dev,
			    "Ethernet address registers are invalid.\n");
		return (EINVAL);
	}
	return (0);
}

static void
ae_retrieve_address(ae_softc_t *sc)
{
	uint32_t eaddr[2] = {0, 0};
	int error;

	/*
	 *Check for EEPROM.
	 */
	error = ae_get_vpd_eaddr(sc, eaddr);
	if (error != 0)
		error = ae_get_reg_eaddr(sc, eaddr);
	if (error != 0) {
		if (bootverbose)
			device_printf(sc->dev,
			    "Generating random ethernet address.\n");
		eaddr[0] = arc4random();

		/*
		 * Set OUI to ASUSTek COMPUTER INC.
		 */
		sc->eaddr[0] = 0x02;	/* U/L bit set. */
		sc->eaddr[1] = 0x1f;
		sc->eaddr[2] = 0xc6;
		sc->eaddr[3] = (eaddr[0] >> 16) & 0xff;
		sc->eaddr[4] = (eaddr[0] >> 8) & 0xff;
		sc->eaddr[5] = (eaddr[0] >> 0) & 0xff;
	} else {
		sc->eaddr[0] = (eaddr[1] >> 8) & 0xff;
		sc->eaddr[1] = (eaddr[1] >> 0) & 0xff;
		sc->eaddr[2] = (eaddr[0] >> 24) & 0xff;
		sc->eaddr[3] = (eaddr[0] >> 16) & 0xff;
		sc->eaddr[4] = (eaddr[0] >> 8) & 0xff;
		sc->eaddr[5] = (eaddr[0] >> 0) & 0xff;
	}
}

static void
ae_dmamap_cb(void *arg, bus_dma_segment_t *segs, int nsegs, int error)
{
	bus_addr_t *addr = arg;

	if (error != 0)
		return;
	KASSERT(nsegs == 1, ("[ae, %d]: %d segments instead of 1!", __LINE__,
	    nsegs));
	*addr = segs[0].ds_addr;
}

static int
ae_alloc_rings(ae_softc_t *sc)
{
	bus_addr_t busaddr;
	int error;

	/*
	 * Create parent DMA tag.
	 */
	error = bus_dma_tag_create(bus_get_dma_tag(sc->dev),
	    1, 0, BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR,
	    NULL, NULL, BUS_SPACE_MAXSIZE_32BIT, 0,
	    BUS_SPACE_MAXSIZE_32BIT, 0, NULL, NULL,
	    &sc->dma_parent_tag);
	if (error != 0) {
		device_printf(sc->dev, "could not creare parent DMA tag.\n");
		return (error);
	}

	/*
	 * Create DMA tag for TxD.
	 */
	error = bus_dma_tag_create(sc->dma_parent_tag,
	    8, 0, BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
	    NULL, NULL, AE_TXD_BUFSIZE_DEFAULT, 1,
	    AE_TXD_BUFSIZE_DEFAULT, 0, NULL, NULL,
	    &sc->dma_txd_tag);
	if (error != 0) {
		device_printf(sc->dev, "could not creare TxD DMA tag.\n");
		return (error);
	}

	/*
	 * Create DMA tag for TxS.
	 */
	error = bus_dma_tag_create(sc->dma_parent_tag,
	    8, 0, BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
	    NULL, NULL, AE_TXS_COUNT_DEFAULT * 4, 1,
	    AE_TXS_COUNT_DEFAULT * 4, 0, NULL, NULL,
	    &sc->dma_txs_tag);
	if (error != 0) {
		device_printf(sc->dev, "could not creare TxS DMA tag.\n");
		return (error);
	}

	/*
	 * Create DMA tag for RxD.
	 */
	error = bus_dma_tag_create(sc->dma_parent_tag,
	    128, 0, BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
	    NULL, NULL, AE_RXD_COUNT_DEFAULT * 1536 + AE_RXD_PADDING, 1,
	    AE_RXD_COUNT_DEFAULT * 1536 + AE_RXD_PADDING, 0, NULL, NULL,
	    &sc->dma_rxd_tag);
	if (error != 0) {
		device_printf(sc->dev, "could not creare TxS DMA tag.\n");
		return (error);
	}

	/*
	 * Allocate TxD DMA memory.
	 */
	error = bus_dmamem_alloc(sc->dma_txd_tag, (void **)&sc->txd_base,
	    BUS_DMA_WAITOK | BUS_DMA_ZERO | BUS_DMA_COHERENT,
	    &sc->dma_txd_map);
	if (error != 0) {
		device_printf(sc->dev,
		    "could not allocate DMA memory for TxD ring.\n");
		return (error);
	}
	error = bus_dmamap_load(sc->dma_txd_tag, sc->dma_txd_map, sc->txd_base,
	    AE_TXD_BUFSIZE_DEFAULT, ae_dmamap_cb, &busaddr, BUS_DMA_NOWAIT);
	if (error != 0 || busaddr == 0) {
		device_printf(sc->dev,
		    "could not load DMA map for TxD ring.\n");
		return (error);
	}
	sc->dma_txd_busaddr = busaddr;

	/*
	 * Allocate TxS DMA memory.
	 */
	error = bus_dmamem_alloc(sc->dma_txs_tag, (void **)&sc->txs_base,
	    BUS_DMA_WAITOK | BUS_DMA_ZERO | BUS_DMA_COHERENT,
	    &sc->dma_txs_map);
	if (error != 0) {
		device_printf(sc->dev,
		    "could not allocate DMA memory for TxS ring.\n");
		return (error);
	}
	error = bus_dmamap_load(sc->dma_txs_tag, sc->dma_txs_map, sc->txs_base,
	    AE_TXS_COUNT_DEFAULT * 4, ae_dmamap_cb, &busaddr, BUS_DMA_NOWAIT);
	if (error != 0 || busaddr == 0) {
		device_printf(sc->dev,
		    "could not load DMA map for TxS ring.\n");
		return (error);
	}
	sc->dma_txs_busaddr = busaddr;

	/*
	 * Allocate RxD DMA memory.
	 */
	error = bus_dmamem_alloc(sc->dma_rxd_tag, (void **)&sc->rxd_base_dma,
	    BUS_DMA_WAITOK | BUS_DMA_ZERO | BUS_DMA_COHERENT,
	    &sc->dma_rxd_map);
	if (error != 0) {
		device_printf(sc->dev,
		    "could not allocate DMA memory for RxD ring.\n");
		return (error);
	}
	error = bus_dmamap_load(sc->dma_rxd_tag, sc->dma_rxd_map,
	    sc->rxd_base_dma, AE_RXD_COUNT_DEFAULT * 1536 + AE_RXD_PADDING,
	    ae_dmamap_cb, &busaddr, BUS_DMA_NOWAIT);
	if (error != 0 || busaddr == 0) {
		device_printf(sc->dev,
		    "could not load DMA map for RxD ring.\n");
		return (error);
	}
	sc->dma_rxd_busaddr = busaddr + AE_RXD_PADDING;
	sc->rxd_base = (ae_rxd_t *)(sc->rxd_base_dma + AE_RXD_PADDING);

	return (0);
}

static void
ae_dma_free(ae_softc_t *sc)
{

	if (sc->dma_txd_tag != NULL) {
		if (sc->dma_txd_busaddr != 0)
			bus_dmamap_unload(sc->dma_txd_tag, sc->dma_txd_map);
		if (sc->txd_base != NULL)
			bus_dmamem_free(sc->dma_txd_tag, sc->txd_base,
			    sc->dma_txd_map);
		bus_dma_tag_destroy(sc->dma_txd_tag);
		sc->dma_txd_tag = NULL;
		sc->txd_base = NULL;
		sc->dma_txd_busaddr = 0;
	}
	if (sc->dma_txs_tag != NULL) {
		if (sc->dma_txs_busaddr != 0)
			bus_dmamap_unload(sc->dma_txs_tag, sc->dma_txs_map);
		if (sc->txs_base != NULL)
			bus_dmamem_free(sc->dma_txs_tag, sc->txs_base,
			    sc->dma_txs_map);
		bus_dma_tag_destroy(sc->dma_txs_tag);
		sc->dma_txs_tag = NULL;
		sc->txs_base = NULL;
		sc->dma_txs_busaddr = 0;
	}
	if (sc->dma_rxd_tag != NULL) {
		if (sc->dma_rxd_busaddr != 0)
			bus_dmamap_unload(sc->dma_rxd_tag, sc->dma_rxd_map);
		if (sc->rxd_base_dma != NULL)
			bus_dmamem_free(sc->dma_rxd_tag, sc->rxd_base_dma,
			    sc->dma_rxd_map);
		bus_dma_tag_destroy(sc->dma_rxd_tag);
		sc->dma_rxd_tag = NULL;
		sc->rxd_base_dma = NULL;
		sc->dma_rxd_busaddr = 0;
	}
	if (sc->dma_parent_tag != NULL) {
		bus_dma_tag_destroy(sc->dma_parent_tag);
		sc->dma_parent_tag = NULL;
	}
}

static int
ae_shutdown(device_t dev)
{
	ae_softc_t *sc;
	int error;

	sc = device_get_softc(dev);
	KASSERT(sc != NULL, ("[ae: %d]: sc is NULL", __LINE__));

	error = ae_suspend(dev);
	AE_LOCK(sc);
	ae_powersave_enable(sc);
	AE_UNLOCK(sc);
	return (error);
}

static void
ae_powersave_disable(ae_softc_t *sc)
{
	uint32_t val;
	
	AE_LOCK_ASSERT(sc);

	AE_PHY_WRITE(sc, AE_PHY_DBG_ADDR, 0);
	val = AE_PHY_READ(sc, AE_PHY_DBG_DATA);
	if (val & AE_PHY_DBG_POWERSAVE) {
		val &= ~AE_PHY_DBG_POWERSAVE;
		AE_PHY_WRITE(sc, AE_PHY_DBG_DATA, val);
		DELAY(1000);
	}
}

static void
ae_powersave_enable(ae_softc_t *sc)
{
	uint32_t val;
	
	AE_LOCK_ASSERT(sc);

	/*
	 * XXX magic numbers.
	 */
	AE_PHY_WRITE(sc, AE_PHY_DBG_ADDR, 0);
	val = AE_PHY_READ(sc, AE_PHY_DBG_DATA);
	AE_PHY_WRITE(sc, AE_PHY_DBG_ADDR, val | 0x1000);
	AE_PHY_WRITE(sc, AE_PHY_DBG_ADDR, 2);
	AE_PHY_WRITE(sc, AE_PHY_DBG_DATA, 0x3000);
	AE_PHY_WRITE(sc, AE_PHY_DBG_ADDR, 3);
	AE_PHY_WRITE(sc, AE_PHY_DBG_DATA, 0);
}

static void
ae_pm_init(ae_softc_t *sc)
{
	struct ifnet *ifp;
	uint32_t val;
	uint16_t pmstat;
	struct mii_data *mii;
	int pmc;

	AE_LOCK_ASSERT(sc);

	ifp = sc->ifp;
	if ((sc->flags & AE_FLAG_PMG) == 0) {
		/* Disable WOL entirely. */
		AE_WRITE_4(sc, AE_WOL_REG, 0);
		return;
	}

	/*
	 * Configure WOL if enabled.
	 */
	if ((ifp->if_capenable & IFCAP_WOL) != 0) {
		mii = device_get_softc(sc->miibus);
		mii_pollstat(mii);
		if ((mii->mii_media_status & IFM_AVALID) != 0 &&
		    (mii->mii_media_status & IFM_ACTIVE) != 0) {
			AE_WRITE_4(sc, AE_WOL_REG, AE_WOL_MAGIC | \
			    AE_WOL_MAGIC_PME);

			/*
			 * Configure MAC.
			 */
			val = AE_MAC_RX_EN | AE_MAC_CLK_PHY | \
			    AE_MAC_TX_CRC_EN | AE_MAC_TX_AUTOPAD | \
			    ((AE_HALFBUF_DEFAULT << AE_HALFBUF_SHIFT) & \
			    AE_HALFBUF_MASK) | \
			    ((AE_MAC_PREAMBLE_DEFAULT << \
			    AE_MAC_PREAMBLE_SHIFT) & AE_MAC_PREAMBLE_MASK) | \
			    AE_MAC_BCAST_EN | AE_MAC_MCAST_EN;
			if ((IFM_OPTIONS(mii->mii_media_active) & \
			    IFM_FDX) != 0)
				val |= AE_MAC_FULL_DUPLEX;
			AE_WRITE_4(sc, AE_MAC_REG, val);
			    
		} else {	/* No link. */
			AE_WRITE_4(sc, AE_WOL_REG, AE_WOL_LNKCHG | \
			    AE_WOL_LNKCHG_PME);
			AE_WRITE_4(sc, AE_MAC_REG, 0);
		}
	} else {
		ae_powersave_enable(sc);
	}

	/*
	 * PCIE hacks. Magic numbers.
	 */
	val = AE_READ_4(sc, AE_PCIE_PHYMISC_REG);
	val |= AE_PCIE_PHYMISC_FORCE_RCV_DET;
	AE_WRITE_4(sc, AE_PCIE_PHYMISC_REG, val);
	val = AE_READ_4(sc, AE_PCIE_DLL_TX_CTRL_REG);
	val |= AE_PCIE_DLL_TX_CTRL_SEL_NOR_CLK;
	AE_WRITE_4(sc, AE_PCIE_DLL_TX_CTRL_REG, val);

	/*
	 * Configure PME.
	 */
	if (pci_find_cap(sc->dev, PCIY_PMG, &pmc) == 0) {
		pmstat = pci_read_config(sc->dev, pmc + PCIR_POWER_STATUS, 2);
		pmstat &= ~(PCIM_PSTAT_PME | PCIM_PSTAT_PMEENABLE);
		if ((ifp->if_capenable & IFCAP_WOL) != 0)
			pmstat |= PCIM_PSTAT_PME | PCIM_PSTAT_PMEENABLE;
		pci_write_config(sc->dev, pmc + PCIR_POWER_STATUS, pmstat, 2);
	}
}

static int
ae_suspend(device_t dev)
{
	ae_softc_t *sc;

	sc = device_get_softc(dev);

	AE_LOCK(sc);
	ae_stop(sc);
	ae_pm_init(sc);
	AE_UNLOCK(sc);

	return (0);
}

static int
ae_resume(device_t dev)
{
	ae_softc_t *sc;

	sc = device_get_softc(dev);
	KASSERT(sc != NULL, ("[ae, %d]: sc is NULL", __LINE__));

	AE_LOCK(sc);
	AE_READ_4(sc, AE_WOL_REG);	/* Clear WOL status. */
	if ((sc->ifp->if_flags & IFF_UP) != 0)
		ae_init_locked(sc);
	AE_UNLOCK(sc);

	return (0);
}

static unsigned int
ae_tx_avail_size(ae_softc_t *sc)
{
	unsigned int avail;
	
	if (sc->txd_cur >= sc->txd_ack)
		avail = AE_TXD_BUFSIZE_DEFAULT - (sc->txd_cur - sc->txd_ack);
	else
		avail = sc->txd_ack - sc->txd_cur;

	return (avail);
}

static int
ae_encap(ae_softc_t *sc, struct mbuf **m_head)
{
	struct mbuf *m0;
	ae_txd_t *hdr;
	unsigned int to_end;
	uint16_t len;

	AE_LOCK_ASSERT(sc);

	m0 = *m_head;
	len = m0->m_pkthdr.len;
	
	if ((sc->flags & AE_FLAG_TXAVAIL) == 0 ||
	    len + sizeof(ae_txd_t) + 3 > ae_tx_avail_size(sc)) {
#ifdef AE_DEBUG
		if_printf(sc->ifp, "No free Tx available.\n");
#endif
		return ENOBUFS;
	}

	hdr = (ae_txd_t *)(sc->txd_base + sc->txd_cur);
	bzero(hdr, sizeof(*hdr));
	/* Skip header size. */
	sc->txd_cur = (sc->txd_cur + sizeof(ae_txd_t)) % AE_TXD_BUFSIZE_DEFAULT;
	/* Space available to the end of the ring */
	to_end = AE_TXD_BUFSIZE_DEFAULT - sc->txd_cur;
	if (to_end >= len) {
		m_copydata(m0, 0, len, (caddr_t)(sc->txd_base + sc->txd_cur));
	} else {
		m_copydata(m0, 0, to_end, (caddr_t)(sc->txd_base +
		    sc->txd_cur));
		m_copydata(m0, to_end, len - to_end, (caddr_t)sc->txd_base);
	}

	/*
	 * Set TxD flags and parameters.
	 */
	if ((m0->m_flags & M_VLANTAG) != 0) {
		hdr->vlan = htole16(AE_TXD_VLAN(m0->m_pkthdr.ether_vtag));
		hdr->len = htole16(len | AE_TXD_INSERT_VTAG);
	} else {
		hdr->len = htole16(len);
	}

	/*
	 * Set current TxD position and round up to a 4-byte boundary.
	 */
	sc->txd_cur = ((sc->txd_cur + len + 3) & ~3) % AE_TXD_BUFSIZE_DEFAULT;
	if (sc->txd_cur == sc->txd_ack)
		sc->flags &= ~AE_FLAG_TXAVAIL;
#ifdef AE_DEBUG
	if_printf(sc->ifp, "New txd_cur = %d.\n", sc->txd_cur);
#endif

	/*
	 * Update TxS position and check if there are empty TxS available.
	 */
	sc->txs_base[sc->txs_cur].flags &= ~htole16(AE_TXS_UPDATE);
	sc->txs_cur = (sc->txs_cur + 1) % AE_TXS_COUNT_DEFAULT;
	if (sc->txs_cur == sc->txs_ack)
		sc->flags &= ~AE_FLAG_TXAVAIL;

	/*
	 * Synchronize DMA memory.
	 */
	bus_dmamap_sync(sc->dma_txd_tag, sc->dma_txd_map, BUS_DMASYNC_PREREAD |
	    BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->dma_txs_tag, sc->dma_txs_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	return (0);
}

static void
ae_start(struct ifnet *ifp)
{
	ae_softc_t *sc;

	sc = ifp->if_softc;
	AE_LOCK(sc);
	ae_start_locked(ifp);
	AE_UNLOCK(sc);
}

static void
ae_start_locked(struct ifnet *ifp)
{
	ae_softc_t *sc;
	unsigned int count;
	struct mbuf *m0;
	int error;

	sc = ifp->if_softc;
	KASSERT(sc != NULL, ("[ae, %d]: sc is NULL", __LINE__));
	AE_LOCK_ASSERT(sc);

#ifdef AE_DEBUG
	if_printf(ifp, "Start called.\n");
#endif

	if ((ifp->if_drv_flags & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) !=
	    IFF_DRV_RUNNING || (sc->flags & AE_FLAG_LINK) == 0)
		return;

	count = 0;
	while (!IFQ_DRV_IS_EMPTY(&ifp->if_snd)) {
		IFQ_DRV_DEQUEUE(&ifp->if_snd, m0);
		if (m0 == NULL)
			break;	/* Nothing to do. */

		error = ae_encap(sc, &m0);
		if (error != 0) {
			if (m0 != NULL) {
				IFQ_DRV_PREPEND(&ifp->if_snd, m0);
				ifp->if_drv_flags |= IFF_DRV_OACTIVE;
#ifdef AE_DEBUG
				if_printf(ifp, "Setting OACTIVE.\n");
#endif
			}
			break;
		}
		count++;
		sc->tx_inproc++;

		/* Bounce a copy of the frame to BPF. */
		ETHER_BPF_MTAP(ifp, m0);

		m_freem(m0);
	}

	if (count > 0) {	/* Something was dequeued. */
		AE_WRITE_2(sc, AE_MB_TXD_IDX_REG, sc->txd_cur / 4);
		sc->wd_timer = AE_TX_TIMEOUT;	/* Load watchdog. */
#ifdef AE_DEBUG
		if_printf(ifp, "%d packets dequeued.\n", count);
		if_printf(ifp, "Tx pos now is %d.\n", sc->txd_cur);
#endif
	}
}

static void
ae_link_task(void *arg, int pending)
{
	ae_softc_t *sc;
	struct mii_data *mii;
	struct ifnet *ifp;
	uint32_t val;

	sc = (ae_softc_t *)arg;
	KASSERT(sc != NULL, ("[ae, %d]: sc is NULL", __LINE__));
	AE_LOCK(sc);

	ifp = sc->ifp;
	mii = device_get_softc(sc->miibus);
	if (mii == NULL || ifp == NULL ||
	    (ifp->if_drv_flags & IFF_DRV_RUNNING) == 0) {
		AE_UNLOCK(sc);	/* XXX: could happen? */
		return;
	}
	
	sc->flags &= ~AE_FLAG_LINK;
	if ((mii->mii_media_status & (IFM_AVALID | IFM_ACTIVE)) ==
	    (IFM_AVALID | IFM_ACTIVE)) {
		switch(IFM_SUBTYPE(mii->mii_media_active)) {
		case IFM_10_T:
		case IFM_100_TX:
			sc->flags |= AE_FLAG_LINK;
			break;
		default:
			break;
		}
	}

	/*
	 * Stop Rx/Tx MACs.
	 */
	ae_stop_rxmac(sc);
	ae_stop_txmac(sc);

	if ((sc->flags & AE_FLAG_LINK) != 0) {
		ae_mac_config(sc);

		/*
		 * Restart DMA engines.
		 */
		AE_WRITE_1(sc, AE_DMAREAD_REG, AE_DMAREAD_EN);
		AE_WRITE_1(sc, AE_DMAWRITE_REG, AE_DMAWRITE_EN);

		/*
		 * Enable Rx and Tx MACs.
		 */
		val = AE_READ_4(sc, AE_MAC_REG);
		val |= AE_MAC_TX_EN | AE_MAC_RX_EN;
		AE_WRITE_4(sc, AE_MAC_REG, val);
	}
	AE_UNLOCK(sc);
}

static void
ae_stop_rxmac(ae_softc_t *sc)
{
	uint32_t val;
	int i;

	AE_LOCK_ASSERT(sc);

	/*
	 * Stop Rx MAC engine.
	 */
	val = AE_READ_4(sc, AE_MAC_REG);
	if ((val & AE_MAC_RX_EN) != 0) {
		val &= ~AE_MAC_RX_EN;
		AE_WRITE_4(sc, AE_MAC_REG, val);
	}

	/*
	 * Stop Rx DMA engine.
	 */
	if (AE_READ_1(sc, AE_DMAWRITE_REG) == AE_DMAWRITE_EN)
		AE_WRITE_1(sc, AE_DMAWRITE_REG, 0);

	/*
	 * Wait for IDLE state.
	 */
	for (i = 0; i < AE_IDLE_TIMEOUT; i++) {
		val = AE_READ_4(sc, AE_IDLE_REG);
		if ((val & (AE_IDLE_RXMAC | AE_IDLE_DMAWRITE)) == 0)
			break;
		DELAY(100);
	}
	if (i == AE_IDLE_TIMEOUT)
		device_printf(sc->dev, "timed out while stopping Rx MAC.\n");
}

static void
ae_stop_txmac(ae_softc_t *sc)
{
	uint32_t val;
	int i;

	AE_LOCK_ASSERT(sc);

	/*
	 * Stop Tx MAC engine.
	 */
	val = AE_READ_4(sc, AE_MAC_REG);
	if ((val & AE_MAC_TX_EN) != 0) {
		val &= ~AE_MAC_TX_EN;
		AE_WRITE_4(sc, AE_MAC_REG, val);
	}

	/*
	 * Stop Tx DMA engine.
	 */
	if (AE_READ_1(sc, AE_DMAREAD_REG) == AE_DMAREAD_EN)
		AE_WRITE_1(sc, AE_DMAREAD_REG, 0);

	/*
	 * Wait for IDLE state.
	 */
	for (i = 0; i < AE_IDLE_TIMEOUT; i++) {
		val = AE_READ_4(sc, AE_IDLE_REG);
		if ((val & (AE_IDLE_TXMAC | AE_IDLE_DMAREAD)) == 0)
			break;
		DELAY(100);
	}
	if (i == AE_IDLE_TIMEOUT)
		device_printf(sc->dev, "timed out while stopping Tx MAC.\n");
}

static void
ae_mac_config(ae_softc_t *sc)
{
	struct mii_data *mii;
	uint32_t val;

	AE_LOCK_ASSERT(sc);

	mii = device_get_softc(sc->miibus);
	val = AE_READ_4(sc, AE_MAC_REG);
	val &= ~AE_MAC_FULL_DUPLEX;
	/* XXX disable AE_MAC_TX_FLOW_EN? */

	if ((IFM_OPTIONS(mii->mii_media_active) & IFM_FDX) != 0)
		val |= AE_MAC_FULL_DUPLEX;

	AE_WRITE_4(sc, AE_MAC_REG, val);
}

static int
ae_intr(void *arg)
{
	ae_softc_t *sc;
	uint32_t val;

	sc = (ae_softc_t *)arg;
	KASSERT(sc != NULL, ("[ae, %d]: sc is NULL", __LINE__));

	val = AE_READ_4(sc, AE_ISR_REG);
	if (val == 0 || (val & AE_IMR_DEFAULT) == 0)
		return (FILTER_STRAY);

	/* Disable interrupts. */
	AE_WRITE_4(sc, AE_ISR_REG, AE_ISR_DISABLE);

	/* Schedule interrupt processing. */
	taskqueue_enqueue(sc->tq, &sc->int_task);

	return (FILTER_HANDLED);
}

static void
ae_int_task(void *arg, int pending)
{
	ae_softc_t *sc;
	struct ifnet *ifp;
	uint32_t val;

	sc = (ae_softc_t *)arg;

	AE_LOCK(sc);

	ifp = sc->ifp;

	val = AE_READ_4(sc, AE_ISR_REG);	/* Read interrupt status. */
	if (val == 0) {
		AE_UNLOCK(sc);
		return;
	}

	/*
	 * Clear interrupts and disable them.
	 */
	AE_WRITE_4(sc, AE_ISR_REG, val | AE_ISR_DISABLE);

#ifdef AE_DEBUG
	if_printf(ifp, "Interrupt received: 0x%08x\n", val);
#endif

	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0) {
		if ((val & (AE_ISR_DMAR_TIMEOUT | AE_ISR_DMAW_TIMEOUT |
		    AE_ISR_PHY_LINKDOWN)) != 0) {
			ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
			ae_init_locked(sc);
			AE_UNLOCK(sc);
			return;
		}
		if ((val & AE_ISR_TX_EVENT) != 0)
			ae_tx_intr(sc);
		if ((val & AE_ISR_RX_EVENT) != 0)
			ae_rx_intr(sc);
		/*
		 * Re-enable interrupts.
		 */
		AE_WRITE_4(sc, AE_ISR_REG, 0);

		if ((sc->flags & AE_FLAG_TXAVAIL) != 0) {
			if (!IFQ_DRV_IS_EMPTY(&ifp->if_snd))
				ae_start_locked(ifp);
		}
	}

	AE_UNLOCK(sc);
}

static void
ae_tx_intr(ae_softc_t *sc)
{
	struct ifnet *ifp;
	ae_txd_t *txd;
	ae_txs_t *txs;
	uint16_t flags;

	AE_LOCK_ASSERT(sc);

	ifp = sc->ifp;

#ifdef AE_DEBUG
	if_printf(ifp, "Tx interrupt occuried.\n");
#endif

	/*
	 * Syncronize DMA buffers.
	 */
	bus_dmamap_sync(sc->dma_txd_tag, sc->dma_txd_map,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	bus_dmamap_sync(sc->dma_txs_tag, sc->dma_txs_map,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	for (;;) {
		txs = sc->txs_base + sc->txs_ack;
		flags = le16toh(txs->flags);
		if ((flags & AE_TXS_UPDATE) == 0)
			break;
		txs->flags = htole16(flags & ~AE_TXS_UPDATE);
		/* Update stats. */
		ae_update_stats_tx(flags, &sc->stats);

		/*
		 * Update TxS position.
		 */
		sc->txs_ack = (sc->txs_ack + 1) % AE_TXS_COUNT_DEFAULT;
		sc->flags |= AE_FLAG_TXAVAIL;

		txd = (ae_txd_t *)(sc->txd_base + sc->txd_ack);
		if (txs->len != txd->len)
			device_printf(sc->dev, "Size mismatch: TxS:%d TxD:%d\n",
			    le16toh(txs->len), le16toh(txd->len));

		/*
		 * Move txd ack and align on 4-byte boundary.
		 */
		sc->txd_ack = ((sc->txd_ack + le16toh(txd->len) +
		    sizeof(ae_txs_t) + 3) & ~3) % AE_TXD_BUFSIZE_DEFAULT;

		if ((flags & AE_TXS_SUCCESS) != 0)
			if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
		else
			if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);

		sc->tx_inproc--;
	}

	if ((sc->flags & AE_FLAG_TXAVAIL) != 0)
		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
	if (sc->tx_inproc < 0) {
		if_printf(ifp, "Received stray Tx interrupt(s).\n");
		sc->tx_inproc = 0;
	}

	if (sc->tx_inproc == 0)
		sc->wd_timer = 0;	/* Unarm watchdog. */

	/*
	 * Syncronize DMA buffers.
	 */
	bus_dmamap_sync(sc->dma_txd_tag, sc->dma_txd_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->dma_txs_tag, sc->dma_txs_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}

static void
ae_rxeof(ae_softc_t *sc, ae_rxd_t *rxd)
{
	struct ifnet *ifp;
	struct mbuf *m;
	unsigned int size;
	uint16_t flags;

	AE_LOCK_ASSERT(sc);

	ifp = sc->ifp;
	flags = le16toh(rxd->flags);

#ifdef AE_DEBUG
	if_printf(ifp, "Rx interrupt occuried.\n");
#endif
	size = le16toh(rxd->len) - ETHER_CRC_LEN;
	if (size < (ETHER_MIN_LEN - ETHER_CRC_LEN - ETHER_VLAN_ENCAP_LEN)) {
		if_printf(ifp, "Runt frame received.");
		if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
		return;
	}

	m = m_devget(&rxd->data[0], size, ETHER_ALIGN, ifp, NULL);
	if (m == NULL) {
		if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
		return;
	}

	if ((ifp->if_capenable & IFCAP_VLAN_HWTAGGING) != 0 &&
	    (flags & AE_RXD_HAS_VLAN) != 0) {
		m->m_pkthdr.ether_vtag = AE_RXD_VLAN(le16toh(rxd->vlan));
		m->m_flags |= M_VLANTAG;
	}

	if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
	/*
	 * Pass it through.
	 */
	AE_UNLOCK(sc);
	(*ifp->if_input)(ifp, m);
	AE_LOCK(sc);
}

static void
ae_rx_intr(ae_softc_t *sc)
{
	ae_rxd_t *rxd;
	struct ifnet *ifp;
	uint16_t flags;
	int count;

	KASSERT(sc != NULL, ("[ae, %d]: sc is NULL!", __LINE__));

	AE_LOCK_ASSERT(sc);

	ifp = sc->ifp;

	/*
	 * Syncronize DMA buffers.
	 */
	bus_dmamap_sync(sc->dma_rxd_tag, sc->dma_rxd_map,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	for (count = 0;; count++) {
		rxd = (ae_rxd_t *)(sc->rxd_base + sc->rxd_cur);
		flags = le16toh(rxd->flags);
		if ((flags & AE_RXD_UPDATE) == 0)
			break;
		rxd->flags = htole16(flags & ~AE_RXD_UPDATE);
		/* Update stats. */
		ae_update_stats_rx(flags, &sc->stats);

		/*
		 * Update position index.
		 */
		sc->rxd_cur = (sc->rxd_cur + 1) % AE_RXD_COUNT_DEFAULT;

		if ((flags & AE_RXD_SUCCESS) != 0)
			ae_rxeof(sc, rxd);
		else
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
	}

	if (count > 0) {
		bus_dmamap_sync(sc->dma_rxd_tag, sc->dma_rxd_map,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
		/*
		 * Update Rx index.
		 */
		AE_WRITE_2(sc, AE_MB_RXD_IDX_REG, sc->rxd_cur);
	}
}

static void
ae_watchdog(ae_softc_t *sc)
{
	struct ifnet *ifp;

	KASSERT(sc != NULL, ("[ae, %d]: sc is NULL!", __LINE__));
	AE_LOCK_ASSERT(sc);
	ifp = sc->ifp;

	if (sc->wd_timer == 0 || --sc->wd_timer != 0)
		return;		/* Noting to do. */

	if ((sc->flags & AE_FLAG_LINK) == 0)
		if_printf(ifp, "watchdog timeout (missed link).\n");
	else
		if_printf(ifp, "watchdog timeout - resetting.\n");

	if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
	ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
	ae_init_locked(sc);
	if (!IFQ_DRV_IS_EMPTY(&ifp->if_snd))
		ae_start_locked(ifp);
}

static void
ae_tick(void *arg)
{
	ae_softc_t *sc;
	struct mii_data *mii;

	sc = (ae_softc_t *)arg;
	KASSERT(sc != NULL, ("[ae, %d]: sc is NULL!", __LINE__));
	AE_LOCK_ASSERT(sc);

	mii = device_get_softc(sc->miibus);
	mii_tick(mii);
	ae_watchdog(sc);	/* Watchdog check. */
	callout_reset(&sc->tick_ch, hz, ae_tick, sc);
}

static void
ae_rxvlan(ae_softc_t *sc)
{
	struct ifnet *ifp;
	uint32_t val;

	AE_LOCK_ASSERT(sc);
	ifp = sc->ifp;
	val = AE_READ_4(sc, AE_MAC_REG);
	val &= ~AE_MAC_RMVLAN_EN;
	if ((ifp->if_capenable & IFCAP_VLAN_HWTAGGING) != 0)
		val |= AE_MAC_RMVLAN_EN;
	AE_WRITE_4(sc, AE_MAC_REG, val);
}

static void
ae_rxfilter(ae_softc_t *sc)
{
	struct ifnet *ifp;
	struct ifmultiaddr *ifma;
	uint32_t crc;
	uint32_t mchash[2];
	uint32_t rxcfg;

	KASSERT(sc != NULL, ("[ae, %d]: sc is NULL!", __LINE__));

	AE_LOCK_ASSERT(sc);

	ifp = sc->ifp;

	rxcfg = AE_READ_4(sc, AE_MAC_REG);
	rxcfg &= ~(AE_MAC_MCAST_EN | AE_MAC_BCAST_EN | AE_MAC_PROMISC_EN);

	if ((ifp->if_flags & IFF_BROADCAST) != 0)
		rxcfg |= AE_MAC_BCAST_EN;
	if ((ifp->if_flags & IFF_PROMISC) != 0)
		rxcfg |= AE_MAC_PROMISC_EN;
	if ((ifp->if_flags & IFF_ALLMULTI) != 0)
		rxcfg |= AE_MAC_MCAST_EN;

	/*
	 * Wipe old settings.
	 */
	AE_WRITE_4(sc, AE_REG_MHT0, 0);
	AE_WRITE_4(sc, AE_REG_MHT1, 0);
	if ((ifp->if_flags & (IFF_PROMISC | IFF_ALLMULTI)) != 0) {
		AE_WRITE_4(sc, AE_REG_MHT0, 0xffffffff);
		AE_WRITE_4(sc, AE_REG_MHT1, 0xffffffff);
		AE_WRITE_4(sc, AE_MAC_REG, rxcfg);
		return;
	}

	/*
	 * Load multicast tables.
	 */
	bzero(mchash, sizeof(mchash));
	if_maddr_rlock(ifp);
	CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		crc = ether_crc32_be(LLADDR((struct sockaddr_dl *)
			ifma->ifma_addr), ETHER_ADDR_LEN);
		mchash[crc >> 31] |= 1 << ((crc >> 26) & 0x1f);
	}
	if_maddr_runlock(ifp);
	AE_WRITE_4(sc, AE_REG_MHT0, mchash[0]);
	AE_WRITE_4(sc, AE_REG_MHT1, mchash[1]);
	AE_WRITE_4(sc, AE_MAC_REG, rxcfg);
}

static int
ae_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct ae_softc *sc;
	struct ifreq *ifr;
	struct mii_data *mii;
	int error, mask;

	sc = ifp->if_softc;
	ifr = (struct ifreq *)data;
	error = 0;

	switch (cmd) {
	case SIOCSIFMTU:
		if (ifr->ifr_mtu < ETHERMIN || ifr->ifr_mtu > ETHERMTU)
			error = EINVAL;
		else if (ifp->if_mtu != ifr->ifr_mtu) {
			AE_LOCK(sc);
			ifp->if_mtu = ifr->ifr_mtu;
			if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0) {
				ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
				ae_init_locked(sc);
			}
			AE_UNLOCK(sc);
		}
		break;
	case SIOCSIFFLAGS:
		AE_LOCK(sc);
		if ((ifp->if_flags & IFF_UP) != 0) {
			if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0) {
				if (((ifp->if_flags ^ sc->if_flags)
				    & (IFF_PROMISC | IFF_ALLMULTI)) != 0)
					ae_rxfilter(sc);
			} else {
				if ((sc->flags & AE_FLAG_DETACH) == 0)
					ae_init_locked(sc);
			}
		} else {
			if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
				ae_stop(sc);
		}
		sc->if_flags = ifp->if_flags;
		AE_UNLOCK(sc);
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		AE_LOCK(sc);
		if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
			ae_rxfilter(sc);
		AE_UNLOCK(sc);
		break;
	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		mii = device_get_softc(sc->miibus);
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, cmd);
		break;
	case SIOCSIFCAP:
		AE_LOCK(sc);
		mask = ifr->ifr_reqcap ^ ifp->if_capenable;
		if ((mask & IFCAP_VLAN_HWTAGGING) != 0 &&
		    (ifp->if_capabilities & IFCAP_VLAN_HWTAGGING) != 0) {
			ifp->if_capenable ^= IFCAP_VLAN_HWTAGGING;
			ae_rxvlan(sc);
		}
		VLAN_CAPABILITIES(ifp);
		AE_UNLOCK(sc);
		break;
	default:
		error = ether_ioctl(ifp, cmd, data);
		break;
	}
	return (error);
}

static void
ae_stop(ae_softc_t *sc)
{
	struct ifnet *ifp;
	int i;

	AE_LOCK_ASSERT(sc);

	ifp = sc->ifp;
	ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);
	sc->flags &= ~AE_FLAG_LINK;
	sc->wd_timer = 0;	/* Cancel watchdog. */
	callout_stop(&sc->tick_ch);

	/*
	 * Clear and disable interrupts.
	 */
	AE_WRITE_4(sc, AE_IMR_REG, 0);
	AE_WRITE_4(sc, AE_ISR_REG, 0xffffffff);

	/*
	 * Stop Rx/Tx MACs.
	 */
	ae_stop_txmac(sc);
	ae_stop_rxmac(sc);

	/*
	 * Stop DMA engines.
	 */
	AE_WRITE_1(sc, AE_DMAREAD_REG, ~AE_DMAREAD_EN);
	AE_WRITE_1(sc, AE_DMAWRITE_REG, ~AE_DMAWRITE_EN);

	/*
	 * Wait for everything to enter idle state.
	 */
	for (i = 0; i < AE_IDLE_TIMEOUT; i++) {
		if (AE_READ_4(sc, AE_IDLE_REG) == 0)
			break;
		DELAY(100);
	}
	if (i == AE_IDLE_TIMEOUT)
		device_printf(sc->dev, "could not enter idle state in stop.\n");
}

static void
ae_update_stats_tx(uint16_t flags, ae_stats_t *stats)
{

	if ((flags & AE_TXS_BCAST) != 0)
		stats->tx_bcast++;
	if ((flags & AE_TXS_MCAST) != 0)
		stats->tx_mcast++;
	if ((flags & AE_TXS_PAUSE) != 0)
		stats->tx_pause++;
	if ((flags & AE_TXS_CTRL) != 0)
		stats->tx_ctrl++;
	if ((flags & AE_TXS_DEFER) != 0)
		stats->tx_defer++;
	if ((flags & AE_TXS_EXCDEFER) != 0)
		stats->tx_excdefer++;
	if ((flags & AE_TXS_SINGLECOL) != 0)
		stats->tx_singlecol++;
	if ((flags & AE_TXS_MULTICOL) != 0)
		stats->tx_multicol++;
	if ((flags & AE_TXS_LATECOL) != 0)
		stats->tx_latecol++;
	if ((flags & AE_TXS_ABORTCOL) != 0)
		stats->tx_abortcol++;
	if ((flags & AE_TXS_UNDERRUN) != 0)
		stats->tx_underrun++;
}

static void
ae_update_stats_rx(uint16_t flags, ae_stats_t *stats)
{

	if ((flags & AE_RXD_BCAST) != 0)
		stats->rx_bcast++;
	if ((flags & AE_RXD_MCAST) != 0)
		stats->rx_mcast++;
	if ((flags & AE_RXD_PAUSE) != 0)
		stats->rx_pause++;
	if ((flags & AE_RXD_CTRL) != 0)
		stats->rx_ctrl++;
	if ((flags & AE_RXD_CRCERR) != 0)
		stats->rx_crcerr++;
	if ((flags & AE_RXD_CODEERR) != 0)
		stats->rx_codeerr++;
	if ((flags & AE_RXD_RUNT) != 0)
		stats->rx_runt++;
	if ((flags & AE_RXD_FRAG) != 0)
		stats->rx_frag++;
	if ((flags & AE_RXD_TRUNC) != 0)
		stats->rx_trunc++;
	if ((flags & AE_RXD_ALIGN) != 0)
		stats->rx_align++;
}
