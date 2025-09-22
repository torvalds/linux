/*	$OpenBSD: if_ale.c,v 1.52 2024/08/31 16:23:09 deraadt Exp $	*/
/*-
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
 *
 * $FreeBSD: src/sys/dev/ale/if_ale.c,v 1.3 2008/12/03 09:01:12 yongari Exp $
 */

/* Driver for Atheros AR8121/AR8113/AR8114 PCIe Ethernet. */

#include "bpfilter.h"
#include "vlan.h"

#include <sys/param.h>
#include <sys/endian.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/queue.h>
#include <sys/device.h>
#include <sys/timeout.h>

#include <machine/bus.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_llc.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/if_ether.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/pci/if_alereg.h>

int	ale_match(struct device *, void *, void *);
void	ale_attach(struct device *, struct device *, void *);
int	ale_detach(struct device *, int);
int	ale_activate(struct device *, int);

int	ale_miibus_readreg(struct device *, int, int);
void	ale_miibus_writereg(struct device *, int, int, int);
void	ale_miibus_statchg(struct device *);

int	ale_init(struct ifnet *);
void	ale_start(struct ifnet *);
int	ale_ioctl(struct ifnet *, u_long, caddr_t);
void	ale_watchdog(struct ifnet *);
int	ale_mediachange(struct ifnet *);
void	ale_mediastatus(struct ifnet *, struct ifmediareq *);

int	ale_intr(void *);
int	ale_rxeof(struct ale_softc *sc);
void	ale_rx_update_page(struct ale_softc *, struct ale_rx_page **,
	    uint32_t, uint32_t *);
void	ale_rxcsum(struct ale_softc *, struct mbuf *, uint32_t);
void	ale_txeof(struct ale_softc *);

int	ale_dma_alloc(struct ale_softc *);
void	ale_dma_free(struct ale_softc *);
int	ale_encap(struct ale_softc *, struct mbuf *);
void	ale_init_rx_pages(struct ale_softc *);
void	ale_init_tx_ring(struct ale_softc *);

void	ale_stop(struct ale_softc *);
void	ale_tick(void *);
void	ale_get_macaddr(struct ale_softc *);
void	ale_mac_config(struct ale_softc *);
void	ale_phy_reset(struct ale_softc *);
void	ale_reset(struct ale_softc *);
void	ale_iff(struct ale_softc *);
void	ale_rxvlan(struct ale_softc *);
void	ale_stats_clear(struct ale_softc *);
void	ale_stats_update(struct ale_softc *);
void	ale_stop_mac(struct ale_softc *);

const struct pci_matchid ale_devices[] = {
	{ PCI_VENDOR_ATTANSIC, PCI_PRODUCT_ATTANSIC_L1E }
};

const struct cfattach ale_ca = {
	sizeof (struct ale_softc), ale_match, ale_attach, NULL,
	ale_activate
};

struct cfdriver ale_cd = {
	NULL, "ale", DV_IFNET
};

int aledebug = 0;
#define DPRINTF(x)	do { if (aledebug) printf x; } while (0)

#define ALE_CSUM_FEATURES	(M_TCP_CSUM_OUT | M_UDP_CSUM_OUT)

int
ale_miibus_readreg(struct device *dev, int phy, int reg)
{
	struct ale_softc *sc = (struct ale_softc *)dev;
	uint32_t v;
	int i;

	if (phy != sc->ale_phyaddr)
		return (0);

	if ((sc->ale_flags & ALE_FLAG_FASTETHER) != 0 &&
	    reg == MII_EXTSR)
		return (0);

	CSR_WRITE_4(sc, ALE_MDIO, MDIO_OP_EXECUTE | MDIO_OP_READ |
	    MDIO_SUP_PREAMBLE | MDIO_CLK_25_4 | MDIO_REG_ADDR(reg));
	for (i = ALE_PHY_TIMEOUT; i > 0; i--) {
		DELAY(5);
		v = CSR_READ_4(sc, ALE_MDIO);
		if ((v & (MDIO_OP_EXECUTE | MDIO_OP_BUSY)) == 0)
			break;
	}

	if (i == 0) {
		printf("%s: phy read timeout: phy %d, reg %d\n",
		    sc->sc_dev.dv_xname, phy, reg);
		return (0);
	}

	return ((v & MDIO_DATA_MASK) >> MDIO_DATA_SHIFT);
}

void
ale_miibus_writereg(struct device *dev, int phy, int reg, int val)
{
	struct ale_softc *sc = (struct ale_softc *)dev;
	uint32_t v;
	int i;

	if (phy != sc->ale_phyaddr)
		return;

	CSR_WRITE_4(sc, ALE_MDIO, MDIO_OP_EXECUTE | MDIO_OP_WRITE |
	    (val & MDIO_DATA_MASK) << MDIO_DATA_SHIFT |
	    MDIO_SUP_PREAMBLE | MDIO_CLK_25_4 | MDIO_REG_ADDR(reg));
	for (i = ALE_PHY_TIMEOUT; i > 0; i--) {
		DELAY(5);
		v = CSR_READ_4(sc, ALE_MDIO);
		if ((v & (MDIO_OP_EXECUTE | MDIO_OP_BUSY)) == 0)
			break;
	}

	if (i == 0)
		printf("%s: phy write timeout: phy %d, reg %d\n",
		    sc->sc_dev.dv_xname, phy, reg);
}

void
ale_miibus_statchg(struct device *dev)
{
	struct ale_softc *sc = (struct ale_softc *)dev;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct mii_data *mii = &sc->sc_miibus;
	uint32_t reg;

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		return;

	sc->ale_flags &= ~ALE_FLAG_LINK;
	if ((mii->mii_media_status & (IFM_ACTIVE | IFM_AVALID)) ==
	    (IFM_ACTIVE | IFM_AVALID)) {
		switch (IFM_SUBTYPE(mii->mii_media_active)) {
		case IFM_10_T:
		case IFM_100_TX:
			sc->ale_flags |= ALE_FLAG_LINK;
			break;

		case IFM_1000_T:
			if ((sc->ale_flags & ALE_FLAG_FASTETHER) == 0)
				sc->ale_flags |= ALE_FLAG_LINK;
			break;

		default:
			break;
		}
	}

	/* Stop Rx/Tx MACs. */
	ale_stop_mac(sc);

	/* Program MACs with resolved speed/duplex/flow-control. */
	if ((sc->ale_flags & ALE_FLAG_LINK) != 0) {
		ale_mac_config(sc);
		/* Reenable Tx/Rx MACs. */
		reg = CSR_READ_4(sc, ALE_MAC_CFG);
		reg |= MAC_CFG_TX_ENB | MAC_CFG_RX_ENB;
		CSR_WRITE_4(sc, ALE_MAC_CFG, reg);
	}
}

void
ale_mediastatus(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct ale_softc *sc = ifp->if_softc;
	struct mii_data *mii = &sc->sc_miibus;

	if ((ifp->if_flags & IFF_UP) == 0)
		return;

	mii_pollstat(mii);
	ifmr->ifm_status = mii->mii_media_status;
	ifmr->ifm_active = mii->mii_media_active;
}

int
ale_mediachange(struct ifnet *ifp)
{
	struct ale_softc *sc = ifp->if_softc;
	struct mii_data *mii = &sc->sc_miibus;
	int error;

	if (mii->mii_instance != 0) {
		struct mii_softc *miisc;

		LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
			mii_phy_reset(miisc);
	}
	error = mii_mediachg(mii);

	return (error);
}

int
ale_match(struct device *dev, void *match, void *aux)
{
	return pci_matchbyid((struct pci_attach_args *)aux, ale_devices,
	    sizeof (ale_devices) / sizeof (ale_devices[0]));
}

void
ale_get_macaddr(struct ale_softc *sc)
{
	uint32_t ea[2], reg;
	int i, vpdc;

	reg = CSR_READ_4(sc, ALE_SPI_CTRL);
	if ((reg & SPI_VPD_ENB) != 0) {
		reg &= ~SPI_VPD_ENB;
		CSR_WRITE_4(sc, ALE_SPI_CTRL, reg);
	}

	if (pci_get_capability(sc->sc_pct, sc->sc_pcitag, PCI_CAP_VPD, 
	    &vpdc, NULL)) {
		/*
		 * PCI VPD capability found, let TWSI reload EEPROM.
		 * This will set ethernet address of controller.
		 */
		CSR_WRITE_4(sc, ALE_TWSI_CTRL, CSR_READ_4(sc, ALE_TWSI_CTRL) |
		    TWSI_CTRL_SW_LD_START);
		for (i = 100; i > 0; i--) {
			DELAY(1000);
			reg = CSR_READ_4(sc, ALE_TWSI_CTRL);
			if ((reg & TWSI_CTRL_SW_LD_START) == 0)
				break;
		}
		if (i == 0)
			printf("%s: reloading EEPROM timeout!\n",
			    sc->sc_dev.dv_xname);
	} else {
		if (aledebug)
			printf("%s: PCI VPD capability not found!\n",
			    sc->sc_dev.dv_xname);
	}

	ea[0] = CSR_READ_4(sc, ALE_PAR0);
	ea[1] = CSR_READ_4(sc, ALE_PAR1);
	sc->ale_eaddr[0] = (ea[1] >> 8) & 0xFF;
	sc->ale_eaddr[1] = (ea[1] >> 0) & 0xFF;
	sc->ale_eaddr[2] = (ea[0] >> 24) & 0xFF;
	sc->ale_eaddr[3] = (ea[0] >> 16) & 0xFF;
	sc->ale_eaddr[4] = (ea[0] >> 8) & 0xFF;
	sc->ale_eaddr[5] = (ea[0] >> 0) & 0xFF;
}

void
ale_phy_reset(struct ale_softc *sc)
{
	/* Reset magic from Linux. */
	CSR_WRITE_2(sc, ALE_GPHY_CTRL,
	    GPHY_CTRL_HIB_EN | GPHY_CTRL_HIB_PULSE | GPHY_CTRL_SEL_ANA_RESET |
	    GPHY_CTRL_PHY_PLL_ON);
	DELAY(1000);
	CSR_WRITE_2(sc, ALE_GPHY_CTRL,
	    GPHY_CTRL_EXT_RESET | GPHY_CTRL_HIB_EN | GPHY_CTRL_HIB_PULSE |
	    GPHY_CTRL_SEL_ANA_RESET | GPHY_CTRL_PHY_PLL_ON);
	DELAY(1000);

#define	ATPHY_DBG_ADDR		0x1D
#define	ATPHY_DBG_DATA		0x1E

	/* Enable hibernation mode. */
	ale_miibus_writereg(&sc->sc_dev, sc->ale_phyaddr,
	    ATPHY_DBG_ADDR, 0x0B);
	ale_miibus_writereg(&sc->sc_dev, sc->ale_phyaddr,
	    ATPHY_DBG_DATA, 0xBC00);
	/* Set Class A/B for all modes. */
	ale_miibus_writereg(&sc->sc_dev, sc->ale_phyaddr,
	    ATPHY_DBG_ADDR, 0x00);
	ale_miibus_writereg(&sc->sc_dev, sc->ale_phyaddr,
	    ATPHY_DBG_DATA, 0x02EF);
	/* Enable 10BT power saving. */
	ale_miibus_writereg(&sc->sc_dev, sc->ale_phyaddr,
	    ATPHY_DBG_ADDR, 0x12);
	ale_miibus_writereg(&sc->sc_dev, sc->ale_phyaddr,
	    ATPHY_DBG_DATA, 0x4C04);
	/* Adjust 1000T power. */
	ale_miibus_writereg(&sc->sc_dev, sc->ale_phyaddr,
	    ATPHY_DBG_ADDR, 0x04);
	ale_miibus_writereg(&sc->sc_dev, sc->ale_phyaddr,
	    ATPHY_DBG_ADDR, 0x8BBB);
	/* 10BT center tap voltage. */
	ale_miibus_writereg(&sc->sc_dev, sc->ale_phyaddr,
	    ATPHY_DBG_ADDR, 0x05);
	ale_miibus_writereg(&sc->sc_dev, sc->ale_phyaddr,
	    ATPHY_DBG_ADDR, 0x2C46);

#undef	ATPHY_DBG_ADDR
#undef	ATPHY_DBG_DATA
	DELAY(1000);
}

void
ale_attach(struct device *parent, struct device *self, void *aux)
{
	struct ale_softc *sc = (struct ale_softc *)self;
	struct pci_attach_args *pa = aux;
	pci_chipset_tag_t pc = pa->pa_pc;
	pci_intr_handle_t ih;
	const char *intrstr;
	struct ifnet *ifp;
	pcireg_t memtype;
	int mii_flags, error = 0;
	uint32_t rxf_len, txf_len;
	const char *chipname;

	/*
	 * Allocate IO memory
	 */
	memtype = pci_mapreg_type(pa->pa_pc, pa->pa_tag, ALE_PCIR_BAR);
	if (pci_mapreg_map(pa, ALE_PCIR_BAR, memtype, 0, &sc->sc_mem_bt,
	    &sc->sc_mem_bh, NULL, &sc->sc_mem_size, 0)) {
		printf(": can't map mem space\n");
		return;
	}

	if (pci_intr_map_msi(pa, &ih) != 0 && pci_intr_map(pa, &ih) != 0) {
		printf(": can't map interrupt\n");
		goto fail;
	}

	/*
	 * Allocate IRQ
	 */
	intrstr = pci_intr_string(pc, ih);
	sc->sc_irq_handle = pci_intr_establish(pc, ih, IPL_NET, ale_intr, sc,
	    sc->sc_dev.dv_xname);
	if (sc->sc_irq_handle == NULL) {
		printf(": could not establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		goto fail;
	}

	sc->sc_dmat = pa->pa_dmat;
	sc->sc_pct = pa->pa_pc;
	sc->sc_pcitag = pa->pa_tag;

	/* Set PHY address. */
	sc->ale_phyaddr = ALE_PHY_ADDR;

	/* Reset PHY. */
	ale_phy_reset(sc);

	/* Reset the ethernet controller. */
	ale_reset(sc);

	/* Get PCI and chip id/revision. */
	sc->ale_rev = PCI_REVISION(pa->pa_class);
	if (sc->ale_rev >= 0xF0) {
		/* L2E Rev. B. AR8114 */
		sc->ale_flags |= ALE_FLAG_FASTETHER;
		chipname = "AR8114";
	} else {
		if ((CSR_READ_4(sc, ALE_PHY_STATUS) & PHY_STATUS_100M) != 0) {
			/* L1E AR8121 */
			sc->ale_flags |= ALE_FLAG_JUMBO;
			chipname = "AR8121";
		} else {
			/* L2E Rev. A. AR8113 */
			sc->ale_flags |= ALE_FLAG_FASTETHER;
			chipname = "AR8113";
		}
	}

	printf(": %s, %s", chipname, intrstr);

	/*
	 * All known controllers seems to require 4 bytes alignment
	 * of Tx buffers to make Tx checksum offload with custom
	 * checksum generation method work.
	 */
	sc->ale_flags |= ALE_FLAG_TXCSUM_BUG;

	/*
	 * All known controllers seems to have issues on Rx checksum
	 * offload for fragmented IP datagrams.
	 */
	sc->ale_flags |= ALE_FLAG_RXCSUM_BUG;

	/*
	 * Don't use Tx CMB. It is known to cause RRS update failure
	 * under certain circumstances. Typical phenomenon of the
	 * issue would be unexpected sequence number encountered in
	 * Rx handler.
	 */
	sc->ale_flags |= ALE_FLAG_TXCMB_BUG;
	sc->ale_chip_rev = CSR_READ_4(sc, ALE_MASTER_CFG) >>
	    MASTER_CHIP_REV_SHIFT;
	if (aledebug) {
		printf("%s: PCI device revision : 0x%04x\n",
		    sc->sc_dev.dv_xname, sc->ale_rev);
		printf("%s: Chip id/revision : 0x%04x\n",
		    sc->sc_dev.dv_xname, sc->ale_chip_rev);
	}

	/*
	 * Uninitialized hardware returns an invalid chip id/revision
	 * as well as 0xFFFFFFFF for Tx/Rx fifo length.
	 */
	txf_len = CSR_READ_4(sc, ALE_SRAM_TX_FIFO_LEN);
	rxf_len = CSR_READ_4(sc, ALE_SRAM_RX_FIFO_LEN);
	if (sc->ale_chip_rev == 0xFFFF || txf_len == 0xFFFFFFFF ||
	    rxf_len == 0xFFFFFFF) {
		printf("%s: chip revision : 0x%04x, %u Tx FIFO "
		    "%u Rx FIFO -- not initialized?\n", sc->sc_dev.dv_xname,
		    sc->ale_chip_rev, txf_len, rxf_len);
		goto fail;
	}

	if (aledebug) {
		printf("%s: %u Tx FIFO, %u Rx FIFO\n", sc->sc_dev.dv_xname,
		    txf_len, rxf_len);
	}

	/* Set max allowable DMA size. */
	sc->ale_dma_rd_burst = DMA_CFG_RD_BURST_128;
	sc->ale_dma_wr_burst = DMA_CFG_WR_BURST_128;

	error = ale_dma_alloc(sc);
	if (error)
		goto fail;

	/* Load station address. */
	ale_get_macaddr(sc);

	ifp = &sc->sc_arpcom.ac_if;
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = ale_ioctl;
	ifp->if_start = ale_start;
	ifp->if_watchdog = ale_watchdog;
	ifq_init_maxlen(&ifp->if_snd, ALE_TX_RING_CNT - 1);
	bcopy(sc->ale_eaddr, sc->sc_arpcom.ac_enaddr, ETHER_ADDR_LEN);
	bcopy(sc->sc_dev.dv_xname, ifp->if_xname, IFNAMSIZ);

	ifp->if_capabilities = IFCAP_VLAN_MTU;

#ifdef ALE_CHECKSUM
	ifp->if_capabilities |= IFCAP_CSUM_IPv4 | IFCAP_CSUM_TCPv4 |
	    IFCAP_CSUM_UDPv4;
#endif

#if NVLAN > 0
	ifp->if_capabilities |= IFCAP_VLAN_HWTAGGING;
#endif

	printf(", address %s\n", ether_sprintf(sc->sc_arpcom.ac_enaddr));

	/* Set up MII bus. */
	sc->sc_miibus.mii_ifp = ifp;
	sc->sc_miibus.mii_readreg = ale_miibus_readreg;
	sc->sc_miibus.mii_writereg = ale_miibus_writereg;
	sc->sc_miibus.mii_statchg = ale_miibus_statchg;

	ifmedia_init(&sc->sc_miibus.mii_media, 0, ale_mediachange,
	    ale_mediastatus);
	mii_flags = 0;
	if ((sc->ale_flags & ALE_FLAG_JUMBO) != 0)
		mii_flags |= MIIF_DOPAUSE;
	mii_attach(self, &sc->sc_miibus, 0xffffffff, MII_PHY_ANY,
	    MII_OFFSET_ANY, mii_flags);

	if (LIST_FIRST(&sc->sc_miibus.mii_phys) == NULL) {
		printf("%s: no PHY found!\n", sc->sc_dev.dv_xname);
		ifmedia_add(&sc->sc_miibus.mii_media, IFM_ETHER | IFM_MANUAL,
		    0, NULL);
		ifmedia_set(&sc->sc_miibus.mii_media, IFM_ETHER | IFM_MANUAL);
	} else
		ifmedia_set(&sc->sc_miibus.mii_media, IFM_ETHER | IFM_AUTO);

	if_attach(ifp);
	ether_ifattach(ifp);

	timeout_set(&sc->ale_tick_ch, ale_tick, sc);

	return;
fail:
	ale_dma_free(sc);
	if (sc->sc_irq_handle != NULL)
		pci_intr_disestablish(pc, sc->sc_irq_handle);
	if (sc->sc_mem_size)
		bus_space_unmap(sc->sc_mem_bt, sc->sc_mem_bh, sc->sc_mem_size);
}

int
ale_detach(struct device *self, int flags)
{
	struct ale_softc *sc = (struct ale_softc *)self;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	int s;

	s = splnet();
	ale_stop(sc);
	splx(s);

	mii_detach(&sc->sc_miibus, MII_PHY_ANY, MII_OFFSET_ANY);

	/* Delete all remaining media. */
	ifmedia_delete_instance(&sc->sc_miibus.mii_media, IFM_INST_ANY);

	ether_ifdetach(ifp);
	if_detach(ifp);
	ale_dma_free(sc);

	if (sc->sc_irq_handle != NULL) {
		pci_intr_disestablish(sc->sc_pct, sc->sc_irq_handle);
		sc->sc_irq_handle = NULL;
	}

	return (0);
}

int
ale_activate(struct device *self, int act)
{
	struct ale_softc *sc = (struct ale_softc *)self;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;

	switch (act) {
	case DVACT_SUSPEND:
		if (ifp->if_flags & IFF_RUNNING)
			ale_stop(sc);
		break;
	case DVACT_RESUME:
		if (ifp->if_flags & IFF_UP)
			ale_init(ifp);
		break;
	}
	return (0);
}

int
ale_dma_alloc(struct ale_softc *sc)
{
	struct ale_txdesc *txd;
	int nsegs, error, guard_size, i;

	if ((sc->ale_flags & ALE_FLAG_JUMBO) != 0)
		guard_size = ALE_JUMBO_FRAMELEN;
	else
		guard_size = ALE_MAX_FRAMELEN;
	sc->ale_pagesize = roundup(guard_size + ALE_RX_PAGE_SZ,
	    ALE_RX_PAGE_ALIGN);

	/*
	 * Create DMA stuffs for TX ring
	 */
	error = bus_dmamap_create(sc->sc_dmat, ALE_TX_RING_SZ, 1,
	    ALE_TX_RING_SZ, 0, BUS_DMA_NOWAIT, &sc->ale_cdata.ale_tx_ring_map);
	if (error)
		return (ENOBUFS);

	/* Allocate DMA'able memory for TX ring */
	error = bus_dmamem_alloc(sc->sc_dmat, ALE_TX_RING_SZ, 
	    ETHER_ALIGN, 0, &sc->ale_cdata.ale_tx_ring_seg, 1,
	    &nsegs, BUS_DMA_WAITOK | BUS_DMA_ZERO);
	if (error) {
		printf("%s: could not allocate DMA'able memory for Tx ring.\n",
		    sc->sc_dev.dv_xname);
		return error;
	}

	error = bus_dmamem_map(sc->sc_dmat, &sc->ale_cdata.ale_tx_ring_seg,
	    nsegs, ALE_TX_RING_SZ, (caddr_t *)&sc->ale_cdata.ale_tx_ring,
	    BUS_DMA_NOWAIT);
	if (error)
		return (ENOBUFS);

	/* Load the DMA map for Tx ring. */
	error = bus_dmamap_load(sc->sc_dmat, sc->ale_cdata.ale_tx_ring_map, 
	    sc->ale_cdata.ale_tx_ring, ALE_TX_RING_SZ, NULL, BUS_DMA_WAITOK);
	if (error) {
		printf("%s: could not load DMA'able memory for Tx ring.\n",
		    sc->sc_dev.dv_xname);
		bus_dmamem_free(sc->sc_dmat, 
		    (bus_dma_segment_t *)&sc->ale_cdata.ale_tx_ring, 1);
		return error;
	}
	sc->ale_cdata.ale_tx_ring_paddr = 
	    sc->ale_cdata.ale_tx_ring_map->dm_segs[0].ds_addr;

	for (i = 0; i < ALE_RX_PAGES; i++) {
		/*
		 * Create DMA stuffs for RX pages
		 */
		error = bus_dmamap_create(sc->sc_dmat, sc->ale_pagesize, 1,
		    sc->ale_pagesize, 0, BUS_DMA_NOWAIT, 
		    &sc->ale_cdata.ale_rx_page[i].page_map);
		if (error)
			return (ENOBUFS);

		/* Allocate DMA'able memory for RX pages */
		error = bus_dmamem_alloc(sc->sc_dmat, sc->ale_pagesize,
		    ETHER_ALIGN, 0, &sc->ale_cdata.ale_rx_page[i].page_seg,
		    1, &nsegs, BUS_DMA_WAITOK | BUS_DMA_ZERO);
		if (error) {
			printf("%s: could not allocate DMA'able memory for "
			    "Rx ring.\n", sc->sc_dev.dv_xname);
			return error;
		}
		error = bus_dmamem_map(sc->sc_dmat, 
		    &sc->ale_cdata.ale_rx_page[i].page_seg, nsegs,
		    sc->ale_pagesize, 
		    (caddr_t *)&sc->ale_cdata.ale_rx_page[i].page_addr,
		    BUS_DMA_NOWAIT);
		if (error)
			return (ENOBUFS);

		/* Load the DMA map for Rx pages. */
		error = bus_dmamap_load(sc->sc_dmat,
		    sc->ale_cdata.ale_rx_page[i].page_map,
		    sc->ale_cdata.ale_rx_page[i].page_addr,
		    sc->ale_pagesize, NULL, BUS_DMA_WAITOK);
		if (error) {
			printf("%s: could not load DMA'able memory for "
			    "Rx pages.\n", sc->sc_dev.dv_xname);
			bus_dmamem_free(sc->sc_dmat,
			    (bus_dma_segment_t *)sc->ale_cdata.ale_rx_page[i].page_addr, 1);
			return error;
		}
		sc->ale_cdata.ale_rx_page[i].page_paddr =
		    sc->ale_cdata.ale_rx_page[i].page_map->dm_segs[0].ds_addr;
	}

	/*
	 * Create DMA stuffs for Tx CMB.
	 */
	error = bus_dmamap_create(sc->sc_dmat, ALE_TX_CMB_SZ, 1,
	    ALE_TX_CMB_SZ, 0, BUS_DMA_NOWAIT, &sc->ale_cdata.ale_tx_cmb_map);
	if (error)
		return (ENOBUFS);

	/* Allocate DMA'able memory for Tx CMB. */
	error = bus_dmamem_alloc(sc->sc_dmat, ALE_TX_CMB_SZ, ETHER_ALIGN, 0,
	    &sc->ale_cdata.ale_tx_cmb_seg, 1, &nsegs,
	    BUS_DMA_WAITOK |BUS_DMA_ZERO);

	if (error) {
		printf("%s: could not allocate DMA'able memory for Tx CMB.\n",
		    sc->sc_dev.dv_xname);
		return error;
	}

	error = bus_dmamem_map(sc->sc_dmat, &sc->ale_cdata.ale_tx_cmb_seg,
	    nsegs, ALE_TX_CMB_SZ, (caddr_t *)&sc->ale_cdata.ale_tx_cmb,
	    BUS_DMA_NOWAIT);
	if (error) 
		return (ENOBUFS);

	/* Load the DMA map for Tx CMB. */
	error = bus_dmamap_load(sc->sc_dmat, sc->ale_cdata.ale_tx_cmb_map, 
	    sc->ale_cdata.ale_tx_cmb, ALE_TX_CMB_SZ, NULL, BUS_DMA_WAITOK);
	if (error) {
		printf("%s: could not load DMA'able memory for Tx CMB.\n",
		    sc->sc_dev.dv_xname);
		bus_dmamem_free(sc->sc_dmat,
		    (bus_dma_segment_t *)&sc->ale_cdata.ale_tx_cmb, 1);
		return error;
	}

	sc->ale_cdata.ale_tx_cmb_paddr = 
	    sc->ale_cdata.ale_tx_cmb_map->dm_segs[0].ds_addr;

	for (i = 0; i < ALE_RX_PAGES; i++) {
		/*
		 * Create DMA stuffs for Rx CMB.
		 */
		error = bus_dmamap_create(sc->sc_dmat, ALE_RX_CMB_SZ, 1,
		    ALE_RX_CMB_SZ, 0, BUS_DMA_NOWAIT,
		    &sc->ale_cdata.ale_rx_page[i].cmb_map);
		if (error)
			return (ENOBUFS);

		/* Allocate DMA'able memory for Rx CMB */
		error = bus_dmamem_alloc(sc->sc_dmat, ALE_RX_CMB_SZ,
		    ETHER_ALIGN, 0, &sc->ale_cdata.ale_rx_page[i].cmb_seg, 1,
		    &nsegs, BUS_DMA_WAITOK | BUS_DMA_ZERO);
		if (error) {
			printf("%s: could not allocate DMA'able memory for "
			    "Rx CMB\n", sc->sc_dev.dv_xname);
			return error;
		}
		error = bus_dmamem_map(sc->sc_dmat, 
		    &sc->ale_cdata.ale_rx_page[i].cmb_seg, nsegs,
		    ALE_RX_CMB_SZ, 
		    (caddr_t *)&sc->ale_cdata.ale_rx_page[i].cmb_addr,
		    BUS_DMA_NOWAIT);
		if (error)
			return (ENOBUFS);

		/* Load the DMA map for Rx CMB */
		error = bus_dmamap_load(sc->sc_dmat,
		    sc->ale_cdata.ale_rx_page[i].cmb_map,
		    sc->ale_cdata.ale_rx_page[i].cmb_addr,
		    ALE_RX_CMB_SZ, NULL, BUS_DMA_WAITOK);
		if (error) {
			printf("%s: could not load DMA'able memory for Rx CMB"
			    "\n", sc->sc_dev.dv_xname);
			bus_dmamem_free(sc->sc_dmat,
			    (bus_dma_segment_t *)&sc->ale_cdata.ale_rx_page[i].cmb_addr, 1);
			return error;
		}
		sc->ale_cdata.ale_rx_page[i].cmb_paddr =
		    sc->ale_cdata.ale_rx_page[i].cmb_map->dm_segs[0].ds_addr;
	}


	/* Create DMA maps for Tx buffers. */
	for (i = 0; i < ALE_TX_RING_CNT; i++) {
		txd = &sc->ale_cdata.ale_txdesc[i];
		txd->tx_m = NULL;
		txd->tx_dmamap = NULL;
		error = bus_dmamap_create(sc->sc_dmat, ALE_TSO_MAXSIZE,
		    ALE_MAXTXSEGS, ALE_TSO_MAXSEGSIZE, 0, BUS_DMA_NOWAIT,
		    &txd->tx_dmamap);
		if (error) {
			printf("%s: could not create Tx dmamap.\n",
			    sc->sc_dev.dv_xname);
			return error;
		}
	}

	return (0);
}

void
ale_dma_free(struct ale_softc *sc)
{
	struct ale_txdesc *txd;
	int i;

	/* Tx buffers. */
	for (i = 0; i < ALE_TX_RING_CNT; i++) {
		txd = &sc->ale_cdata.ale_txdesc[i];
		if (txd->tx_dmamap != NULL) {
			bus_dmamap_destroy(sc->sc_dmat, txd->tx_dmamap);
			txd->tx_dmamap = NULL;
		}
	}

	/* Tx descriptor ring. */
	if (sc->ale_cdata.ale_tx_ring_map != NULL)
		bus_dmamap_unload(sc->sc_dmat, sc->ale_cdata.ale_tx_ring_map);
	if (sc->ale_cdata.ale_tx_ring_map != NULL &&
	    sc->ale_cdata.ale_tx_ring != NULL)
		bus_dmamem_free(sc->sc_dmat,
		    (bus_dma_segment_t *)sc->ale_cdata.ale_tx_ring, 1);
	sc->ale_cdata.ale_tx_ring = NULL;
	sc->ale_cdata.ale_tx_ring_map = NULL;

	/* Rx page block. */
	for (i = 0; i < ALE_RX_PAGES; i++) {
		if (sc->ale_cdata.ale_rx_page[i].page_map != NULL)
			bus_dmamap_unload(sc->sc_dmat,
			    sc->ale_cdata.ale_rx_page[i].page_map);
		if (sc->ale_cdata.ale_rx_page[i].page_map != NULL &&
		    sc->ale_cdata.ale_rx_page[i].page_addr != NULL)
			bus_dmamem_free(sc->sc_dmat,
			    (bus_dma_segment_t *)sc->ale_cdata.ale_rx_page[i].page_addr, 1);
		sc->ale_cdata.ale_rx_page[i].page_addr = NULL;
		sc->ale_cdata.ale_rx_page[i].page_map = NULL;
	}

	/* Rx CMB. */
	for (i = 0; i < ALE_RX_PAGES; i++) {
		if (sc->ale_cdata.ale_rx_page[i].cmb_map != NULL)
			bus_dmamap_unload(sc->sc_dmat,
			    sc->ale_cdata.ale_rx_page[i].cmb_map);
		if (sc->ale_cdata.ale_rx_page[i].cmb_map != NULL &&
		    sc->ale_cdata.ale_rx_page[i].cmb_addr != NULL)
			bus_dmamem_free(sc->sc_dmat,
			    (bus_dma_segment_t *)sc->ale_cdata.ale_rx_page[i].cmb_addr, 1);
		sc->ale_cdata.ale_rx_page[i].cmb_addr = NULL;
		sc->ale_cdata.ale_rx_page[i].cmb_map = NULL;
	}

	/* Tx CMB. */
	if (sc->ale_cdata.ale_tx_cmb_map != NULL)
		bus_dmamap_unload(sc->sc_dmat, sc->ale_cdata.ale_tx_cmb_map);
	if (sc->ale_cdata.ale_tx_cmb_map != NULL &&
	    sc->ale_cdata.ale_tx_cmb != NULL)
		bus_dmamem_free(sc->sc_dmat,
		    (bus_dma_segment_t *)sc->ale_cdata.ale_tx_cmb, 1);
	sc->ale_cdata.ale_tx_cmb = NULL;
	sc->ale_cdata.ale_tx_cmb_map = NULL;

}

int
ale_encap(struct ale_softc *sc, struct mbuf *m)
{
	struct ale_txdesc *txd, *txd_last;
	struct tx_desc *desc;
	bus_dmamap_t map;
	uint32_t cflags, poff, vtag;
	int error, i, prod;

	cflags = vtag = 0;
	poff = 0;

	prod = sc->ale_cdata.ale_tx_prod;
	txd = &sc->ale_cdata.ale_txdesc[prod];
	txd_last = txd;
	map = txd->tx_dmamap;

	error = bus_dmamap_load_mbuf(sc->sc_dmat, map, m, BUS_DMA_NOWAIT);
	if (error != 0 && error != EFBIG)
		goto drop;
	if (error != 0) {
		if (m_defrag(m, M_DONTWAIT)) {
			error = ENOBUFS;
			goto drop;
		}
		error = bus_dmamap_load_mbuf(sc->sc_dmat, map, m,
		    BUS_DMA_NOWAIT);
		if (error != 0)
			goto drop;
	}

	bus_dmamap_sync(sc->sc_dmat, map, 0, map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);

	/* Configure Tx checksum offload. */
	if ((m->m_pkthdr.csum_flags & ALE_CSUM_FEATURES) != 0) {
		/*
		 * AR81xx supports Tx custom checksum offload feature
		 * that offloads single 16bit checksum computation.
		 * So you can choose one among IP, TCP and UDP.
		 * Normally driver sets checksum start/insertion
		 * position from the information of TCP/UDP frame as
		 * TCP/UDP checksum takes more time than that of IP.
		 * However it seems that custom checksum offload
		 * requires 4 bytes aligned Tx buffers due to hardware
		 * bug.
		 * AR81xx also supports explicit Tx checksum computation
		 * if it is told that the size of IP header and TCP
		 * header(for UDP, the header size does not matter
		 * because it's fixed length). However with this scheme
		 * TSO does not work so you have to choose one either
		 * TSO or explicit Tx checksum offload. I chosen TSO
		 * plus custom checksum offload with work-around which
		 * will cover most common usage for this consumer
		 * ethernet controller. The work-around takes a lot of
		 * CPU cycles if Tx buffer is not aligned on 4 bytes
		 * boundary, though.
		 */
		cflags |= ALE_TD_CXSUM;
		/* Set checksum start offset. */
		cflags |= (poff << ALE_TD_CSUM_PLOADOFFSET_SHIFT);
	}

#if NVLAN > 0
	/* Configure VLAN hardware tag insertion. */
	if (m->m_flags & M_VLANTAG) {
		vtag = ALE_TX_VLAN_TAG(m->m_pkthdr.ether_vtag);
		vtag = ((vtag << ALE_TD_VLAN_SHIFT) & ALE_TD_VLAN_MASK);
		cflags |= ALE_TD_INSERT_VLAN_TAG;
	}
#endif

	desc = NULL;
	for (i = 0; i < map->dm_nsegs; i++) {
		desc = &sc->ale_cdata.ale_tx_ring[prod];
		desc->addr = htole64(map->dm_segs[i].ds_addr);
		desc->len = 
		    htole32(ALE_TX_BYTES(map->dm_segs[i].ds_len) | vtag);
		desc->flags = htole32(cflags);
		sc->ale_cdata.ale_tx_cnt++;
		ALE_DESC_INC(prod, ALE_TX_RING_CNT);
	}

	/* Update producer index. */
	sc->ale_cdata.ale_tx_prod = prod;

	/* Finally set EOP on the last descriptor. */
	prod = (prod + ALE_TX_RING_CNT - 1) % ALE_TX_RING_CNT;
	desc = &sc->ale_cdata.ale_tx_ring[prod];
	desc->flags |= htole32(ALE_TD_EOP);

	/* Swap dmamap of the first and the last. */
	txd = &sc->ale_cdata.ale_txdesc[prod];
	map = txd_last->tx_dmamap;
	txd_last->tx_dmamap = txd->tx_dmamap;
	txd->tx_dmamap = map;
	txd->tx_m = m;

	/* Sync descriptors. */
	bus_dmamap_sync(sc->sc_dmat, sc->ale_cdata.ale_tx_ring_map, 0,
	    sc->ale_cdata.ale_tx_ring_map->dm_mapsize,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	return (0);

 drop:
	m_freem(m);
	return (error);
}

void
ale_start(struct ifnet *ifp)
{
        struct ale_softc *sc = ifp->if_softc;
	struct mbuf *m;
	int enq;

	/* Reclaim transmitted frames. */
	if (sc->ale_cdata.ale_tx_cnt >= ALE_TX_DESC_HIWAT)
		ale_txeof(sc);

	if (!(ifp->if_flags & IFF_RUNNING) || ifq_is_oactive(&ifp->if_snd))
		return;
	if ((sc->ale_flags & ALE_FLAG_LINK) == 0)
		return;
	if (ifq_empty(&ifp->if_snd))
		return;

	enq = 0;
	for (;;) {
		/* Check descriptor overrun. */
		if (sc->ale_cdata.ale_tx_cnt + ALE_MAXTXSEGS >=
		    ALE_TX_RING_CNT - 2) {
			ifq_set_oactive(&ifp->if_snd);
			break;
		}

		m = ifq_dequeue(&ifp->if_snd);
		if (m == NULL)
			break;

		/*
		 * Pack the data into the transmit ring. If we
		 * don't have room, set the OACTIVE flag and wait
		 * for the NIC to drain the ring.
		 */
		if (ale_encap(sc, m) != 0) {
			ifp->if_oerrors++;
			continue;
		}

		enq = 1;

#if NBPFILTER > 0
		/*
		 * If there's a BPF listener, bounce a copy of this frame
		 * to him.
		 */
		if (ifp->if_bpf != NULL)
			bpf_mtap_ether(ifp->if_bpf, m, BPF_DIRECTION_OUT);
#endif
	}

	if (enq) {
		/* Kick. */
		CSR_WRITE_4(sc, ALE_MBOX_TPD_PROD_IDX,
		    sc->ale_cdata.ale_tx_prod);

		/* Set a timeout in case the chip goes out to lunch. */
		ifp->if_timer = ALE_TX_TIMEOUT;
	}
}

void
ale_watchdog(struct ifnet *ifp)
{
	struct ale_softc *sc = ifp->if_softc;

	if ((sc->ale_flags & ALE_FLAG_LINK) == 0) {
		printf("%s: watchdog timeout (missed link)\n",
		    sc->sc_dev.dv_xname);
		ifp->if_oerrors++;
		ale_init(ifp);
		return;
	}

	printf("%s: watchdog timeout\n", sc->sc_dev.dv_xname);
	ifp->if_oerrors++;
	ale_init(ifp);
	ale_start(ifp);
}

int
ale_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct ale_softc *sc = ifp->if_softc;
	struct mii_data *mii = &sc->sc_miibus;
	struct ifreq *ifr = (struct ifreq *)data;
	int s, error = 0;

	s = splnet();

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		if (!(ifp->if_flags & IFF_RUNNING))
			ale_init(ifp);
		break;

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				error = ENETRESET;
			else
				ale_init(ifp);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				ale_stop(sc);
		}
		break;

	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, cmd);
		break;

	default:
		error = ether_ioctl(ifp, &sc->sc_arpcom, cmd, data);
		break;
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			ale_iff(sc);
		error = 0;
	}

	splx(s);
	return (error);
}

void
ale_mac_config(struct ale_softc *sc)
{
	struct mii_data *mii;
	uint32_t reg;

	mii = &sc->sc_miibus;
	reg = CSR_READ_4(sc, ALE_MAC_CFG);
	reg &= ~(MAC_CFG_FULL_DUPLEX | MAC_CFG_TX_FC | MAC_CFG_RX_FC |
	    MAC_CFG_SPEED_MASK);
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
		if ((IFM_OPTIONS(mii->mii_media_active) & IFM_ETH_TXPAUSE) != 0)
			reg |= MAC_CFG_TX_FC;
		if ((IFM_OPTIONS(mii->mii_media_active) & IFM_ETH_RXPAUSE) != 0)
			reg |= MAC_CFG_RX_FC;
	}
	CSR_WRITE_4(sc, ALE_MAC_CFG, reg);
}

void
ale_stats_clear(struct ale_softc *sc)
{
	struct smb sb;
	uint32_t *reg;
	int i;

	for (reg = &sb.rx_frames, i = 0; reg <= &sb.rx_pkts_filtered; reg++) {
		CSR_READ_4(sc, ALE_RX_MIB_BASE + i);
		i += sizeof(uint32_t);
	}
	/* Read Tx statistics. */
	for (reg = &sb.tx_frames, i = 0; reg <= &sb.tx_mcast_bytes; reg++) {
		CSR_READ_4(sc, ALE_TX_MIB_BASE + i);
		i += sizeof(uint32_t);
	}
}

void
ale_stats_update(struct ale_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct ale_hw_stats *stat;
	struct smb sb, *smb;
	uint32_t *reg;
	int i;

	stat = &sc->ale_stats;
	smb = &sb;

	/* Read Rx statistics. */
	for (reg = &sb.rx_frames, i = 0; reg <= &sb.rx_pkts_filtered; reg++) {
		*reg = CSR_READ_4(sc, ALE_RX_MIB_BASE + i);
		i += sizeof(uint32_t);
	}
	/* Read Tx statistics. */
	for (reg = &sb.tx_frames, i = 0; reg <= &sb.tx_mcast_bytes; reg++) {
		*reg = CSR_READ_4(sc, ALE_TX_MIB_BASE + i);
		i += sizeof(uint32_t);
	}

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
	stat->rx_rrs_errs += smb->rx_rrs_errs;
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

	ifp->if_collisions += smb->tx_single_colls +
	    smb->tx_multi_colls * 2 + smb->tx_late_colls +
	    smb->tx_excess_colls * HDPX_CFG_RETRY_DEFAULT;

	ifp->if_oerrors += smb->tx_late_colls + smb->tx_excess_colls +
	    smb->tx_underrun + smb->tx_pkts_truncated;

	ifp->if_ierrors += smb->rx_crcerrs + smb->rx_lenerrs +
	    smb->rx_runts + smb->rx_pkts_truncated +
	    smb->rx_fifo_oflows + smb->rx_rrs_errs +
	    smb->rx_alignerrs;
}

int
ale_intr(void *xsc)
{
	struct ale_softc *sc = xsc;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	uint32_t status;

	status = CSR_READ_4(sc, ALE_INTR_STATUS);
	if ((status & ALE_INTRS) == 0)
		return (0);

	/* Acknowledge and disable interrupts. */
	CSR_WRITE_4(sc, ALE_INTR_STATUS, status | INTR_DIS_INT);

	if (ifp->if_flags & IFF_RUNNING) {
		int error;

		error = ale_rxeof(sc);
		if (error) {
			sc->ale_stats.reset_brk_seq++;
			ale_init(ifp);
			return (0);
		}

		if (status & (INTR_DMA_RD_TO_RST | INTR_DMA_WR_TO_RST)) {
			if (status & INTR_DMA_RD_TO_RST)
				printf("%s: DMA read error! -- resetting\n",
				    sc->sc_dev.dv_xname);
			if (status & INTR_DMA_WR_TO_RST)
				printf("%s: DMA write error! -- resetting\n",
				    sc->sc_dev.dv_xname);
			ale_init(ifp);
			return (0);
		}

		ale_txeof(sc);
		ale_start(ifp);
	}

	/* Re-enable interrupts. */
	CSR_WRITE_4(sc, ALE_INTR_STATUS, 0x7FFFFFFF);
	return (1);
}

void
ale_txeof(struct ale_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct ale_txdesc *txd;
	uint32_t cons, prod;
	int prog;

	if (sc->ale_cdata.ale_tx_cnt == 0)
		return;

	bus_dmamap_sync(sc->sc_dmat, sc->ale_cdata.ale_tx_ring_map, 0,
	    sc->ale_cdata.ale_tx_ring_map->dm_mapsize, BUS_DMASYNC_POSTREAD);
	if ((sc->ale_flags & ALE_FLAG_TXCMB_BUG) == 0) {
		bus_dmamap_sync(sc->sc_dmat, sc->ale_cdata.ale_tx_cmb_map, 0,
		    sc->ale_cdata.ale_tx_cmb_map->dm_mapsize, 
		    BUS_DMASYNC_POSTREAD);
		prod = *sc->ale_cdata.ale_tx_cmb & TPD_CNT_MASK;
	} else
		prod = CSR_READ_2(sc, ALE_TPD_CONS_IDX);
	cons = sc->ale_cdata.ale_tx_cons;
	/*
	 * Go through our Tx list and free mbufs for those
	 * frames which have been transmitted.
	 */
	for (prog = 0; cons != prod; prog++,
	     ALE_DESC_INC(cons, ALE_TX_RING_CNT)) {
		if (sc->ale_cdata.ale_tx_cnt <= 0)
			break;
		prog++;
		ifq_clr_oactive(&ifp->if_snd);
		sc->ale_cdata.ale_tx_cnt--;
		txd = &sc->ale_cdata.ale_txdesc[cons];
		if (txd->tx_m != NULL) {
			/* Reclaim transmitted mbufs. */
			bus_dmamap_sync(sc->sc_dmat, txd->tx_dmamap, 0,
			    txd->tx_dmamap->dm_mapsize, BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_dmat, txd->tx_dmamap);
			m_freem(txd->tx_m);
			txd->tx_m = NULL;
		}
	}

	if (prog > 0) {
		sc->ale_cdata.ale_tx_cons = cons;
		/*
		 * Unarm watchdog timer only when there is no pending
		 * Tx descriptors in queue.
		 */
		if (sc->ale_cdata.ale_tx_cnt == 0)
			ifp->if_timer = 0;
	}
}

void
ale_rx_update_page(struct ale_softc *sc, struct ale_rx_page **page,
    uint32_t length, uint32_t *prod)
{
	struct ale_rx_page *rx_page;

	rx_page = *page;
	/* Update consumer position. */
	rx_page->cons += roundup(length + sizeof(struct rx_rs),
	    ALE_RX_PAGE_ALIGN);
	if (rx_page->cons >= ALE_RX_PAGE_SZ) {
		/*
		 * End of Rx page reached, let hardware reuse
		 * this page.
		 */
		rx_page->cons = 0;
		*rx_page->cmb_addr = 0;
		bus_dmamap_sync(sc->sc_dmat, rx_page->cmb_map, 0,
		    rx_page->cmb_map->dm_mapsize,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
		CSR_WRITE_1(sc, ALE_RXF0_PAGE0 + sc->ale_cdata.ale_rx_curp,
		    RXF_VALID);
		/* Switch to alternate Rx page. */
		sc->ale_cdata.ale_rx_curp ^= 1;
		rx_page = *page =
		    &sc->ale_cdata.ale_rx_page[sc->ale_cdata.ale_rx_curp];
		/* Page flipped, sync CMB and Rx page. */
		bus_dmamap_sync(sc->sc_dmat, rx_page->page_map, 0,
		    rx_page->page_map->dm_mapsize,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
		bus_dmamap_sync(sc->sc_dmat, rx_page->cmb_map, 0,
		    rx_page->cmb_map->dm_mapsize,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
		/* Sync completed, cache updated producer index. */
		*prod = *rx_page->cmb_addr;
	}
}


/*
 * It seems that AR81xx controller can compute partial checksum.
 * The partial checksum value can be used to accelerate checksum
 * computation for fragmented TCP/UDP packets. Upper network stack
 * already takes advantage of the partial checksum value in IP
 * reassembly stage. But I'm not sure the correctness of the
 * partial hardware checksum assistance due to lack of data sheet.
 * In addition, the Rx feature of controller that requires copying
 * for every frames effectively nullifies one of most nice offload
 * capability of controller.
 */
void
ale_rxcsum(struct ale_softc *sc, struct mbuf *m, uint32_t status)
{
	struct ip *ip;
	char *p;

	if ((status & ALE_RD_IPCSUM_NOK) == 0)
		m->m_pkthdr.csum_flags |= M_IPV4_CSUM_IN_OK;

	if ((sc->ale_flags & ALE_FLAG_RXCSUM_BUG) == 0) {
		if (((status & ALE_RD_IPV4_FRAG) == 0) &&
		    ((status & (ALE_RD_TCP | ALE_RD_UDP)) != 0) &&
		    ((status & ALE_RD_TCP_UDPCSUM_NOK) == 0)) {
			m->m_pkthdr.csum_flags |=
			    M_TCP_CSUM_IN_OK | M_UDP_CSUM_IN_OK;
		}
	} else {
		if ((status & (ALE_RD_TCP | ALE_RD_UDP)) != 0 &&
		    (status & ALE_RD_TCP_UDPCSUM_NOK) == 0) {
			p = mtod(m, char *);
			p += ETHER_HDR_LEN;
			if ((status & ALE_RD_802_3) != 0)
				p += LLC_SNAPFRAMELEN;
#if NVLAN > 0
			if (status & ALE_RD_VLAN)
				p += EVL_ENCAPLEN;
#endif
			ip = (struct ip *)p;
			if (ip->ip_off != 0 && (status & ALE_RD_IPV4_DF) == 0)
				return;
			m->m_pkthdr.csum_flags |=
			    M_TCP_CSUM_IN_OK | M_UDP_CSUM_IN_OK;
		}
	}
	/*
	 * Don't mark bad checksum for TCP/UDP frames
	 * as fragmented frames may always have set
	 * bad checksummed bit of frame status.
	 */
}

/* Process received frames. */
int
ale_rxeof(struct ale_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct ale_rx_page *rx_page;
	struct rx_rs *rs;
	struct mbuf *m;
	uint32_t length, prod, seqno, status;
	int prog;

	rx_page = &sc->ale_cdata.ale_rx_page[sc->ale_cdata.ale_rx_curp];
	bus_dmamap_sync(sc->sc_dmat, rx_page->cmb_map, 0,
	    rx_page->cmb_map->dm_mapsize,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	bus_dmamap_sync(sc->sc_dmat, rx_page->page_map, 0,
	    rx_page->page_map->dm_mapsize,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	/*
	 * Don't directly access producer index as hardware may
	 * update it while Rx handler is in progress. It would
	 * be even better if there is a way to let hardware
	 * know how far driver processed its received frames.
	 * Alternatively, hardware could provide a way to disable
	 * CMB updates until driver acknowledges the end of CMB
	 * access.
	 */
	prod = *rx_page->cmb_addr;
	for (prog = 0; ; prog++) {
		if (rx_page->cons >= prod)
			break;
		rs = (struct rx_rs *)(rx_page->page_addr + rx_page->cons);
		seqno = ALE_RX_SEQNO(letoh32(rs->seqno));
		if (sc->ale_cdata.ale_rx_seqno != seqno) {
			/*
			 * Normally I believe this should not happen unless
			 * severe driver bug or corrupted memory. However
			 * it seems to happen under certain conditions which
			 * is triggered by abrupt Rx events such as initiation
			 * of bulk transfer of remote host. It's not easy to
			 * reproduce this and I doubt it could be related
			 * with FIFO overflow of hardware or activity of Tx
			 * CMB updates. I also remember similar behaviour
			 * seen on Realtek 8139 which uses resembling Rx
			 * scheme.
			 */
			if (aledebug)
				printf("%s: garbled seq: %u, expected: %u -- "
				    "resetting!\n", sc->sc_dev.dv_xname,
				    seqno, sc->ale_cdata.ale_rx_seqno);
			return (EIO);
		}
		/* Frame received. */
		sc->ale_cdata.ale_rx_seqno++;
		length = ALE_RX_BYTES(letoh32(rs->length));
		status = letoh32(rs->flags);
		if (status & ALE_RD_ERROR) {
			/*
			 * We want to pass the following frames to upper
			 * layer regardless of error status of Rx return
			 * status.
			 *
			 *  o IP/TCP/UDP checksum is bad.
			 *  o frame length and protocol specific length
			 *     does not match.
			 */
			if (status & (ALE_RD_CRC | ALE_RD_CODE |
			    ALE_RD_DRIBBLE | ALE_RD_RUNT | ALE_RD_OFLOW |
			    ALE_RD_TRUNC)) {
				ale_rx_update_page(sc, &rx_page, length, &prod);
				continue;
			}
		}
		/*
		 * m_devget(9) is major bottle-neck of ale(4)(It comes
		 * from hardware limitation). For jumbo frames we could
		 * get a slightly better performance if driver use
		 * m_getjcl(9) with proper buffer size argument. However
		 * that would make code more complicated and I don't
		 * think users would expect good Rx performance numbers
		 * on these low-end consumer ethernet controller.
		 */
		m = m_devget((char *)(rs + 1), length - ETHER_CRC_LEN,
		    ETHER_ALIGN);
		if (m == NULL) {
			ifp->if_iqdrops++;
			ale_rx_update_page(sc, &rx_page, length, &prod);
			continue;
		}
		if (status & ALE_RD_IPV4)
			ale_rxcsum(sc, m, status);
#if NVLAN > 0
		if (status & ALE_RD_VLAN) {
			uint32_t vtags = ALE_RX_VLAN(letoh32(rs->vtags));
			m->m_pkthdr.ether_vtag = ALE_RX_VLAN_TAG(vtags);
			m->m_flags |= M_VLANTAG;
		}
#endif

		ml_enqueue(&ml, m);

		ale_rx_update_page(sc, &rx_page, length, &prod);
	}

	if_input(ifp, &ml);

	return 0;
}

void
ale_tick(void *xsc)
{
	struct ale_softc *sc = xsc;
	struct mii_data *mii = &sc->sc_miibus;
	int s;

	s = splnet();
	mii_tick(mii);
	ale_stats_update(sc);

	timeout_add_sec(&sc->ale_tick_ch, 1);
	splx(s);
}

void
ale_reset(struct ale_softc *sc)
{
	uint32_t reg;
	int i;

	/* Initialize PCIe module. From Linux. */
	CSR_WRITE_4(sc, 0x1008, CSR_READ_4(sc, 0x1008) | 0x8000);

	CSR_WRITE_4(sc, ALE_MASTER_CFG, MASTER_RESET);
	for (i = ALE_RESET_TIMEOUT; i > 0; i--) {
		DELAY(10);
		if ((CSR_READ_4(sc, ALE_MASTER_CFG) & MASTER_RESET) == 0)
			break;
	}
	if (i == 0)
		printf("%s: master reset timeout!\n", sc->sc_dev.dv_xname);

	for (i = ALE_RESET_TIMEOUT; i > 0; i--) {
		if ((reg = CSR_READ_4(sc, ALE_IDLE_STATUS)) == 0)
			break;
		DELAY(10);
	}

	if (i == 0)
		printf("%s: reset timeout(0x%08x)!\n", sc->sc_dev.dv_xname,
		    reg);
}

int
ale_init(struct ifnet *ifp)
{
	struct ale_softc *sc = ifp->if_softc;
	struct mii_data *mii;
	uint8_t eaddr[ETHER_ADDR_LEN];
	bus_addr_t paddr;
	uint32_t reg, rxf_hi, rxf_lo;

	/*
	 * Cancel any pending I/O.
	 */
	ale_stop(sc);

	/*
	 * Reset the chip to a known state.
	 */
	ale_reset(sc);

	/* Initialize Tx descriptors, DMA memory blocks. */
	ale_init_rx_pages(sc);
	ale_init_tx_ring(sc);

	/* Reprogram the station address. */
	bcopy(LLADDR(ifp->if_sadl), eaddr, ETHER_ADDR_LEN);
	CSR_WRITE_4(sc, ALE_PAR0,
	    eaddr[2] << 24 | eaddr[3] << 16 | eaddr[4] << 8 | eaddr[5]);
	CSR_WRITE_4(sc, ALE_PAR1, eaddr[0] << 8 | eaddr[1]);

	/*
	 * Clear WOL status and disable all WOL feature as WOL
	 * would interfere Rx operation under normal environments.
	 */
	CSR_READ_4(sc, ALE_WOL_CFG);
	CSR_WRITE_4(sc, ALE_WOL_CFG, 0);

	/*
	 * Set Tx descriptor/RXF0/CMB base addresses. They share
	 * the same high address part of DMAable region.
	 */
	paddr = sc->ale_cdata.ale_tx_ring_paddr;
	CSR_WRITE_4(sc, ALE_TPD_ADDR_HI, ALE_ADDR_HI(paddr));
	CSR_WRITE_4(sc, ALE_TPD_ADDR_LO, ALE_ADDR_LO(paddr));
	CSR_WRITE_4(sc, ALE_TPD_CNT,
	    (ALE_TX_RING_CNT << TPD_CNT_SHIFT) & TPD_CNT_MASK);

	/* Set Rx page base address, note we use single queue. */
	paddr = sc->ale_cdata.ale_rx_page[0].page_paddr;
	CSR_WRITE_4(sc, ALE_RXF0_PAGE0_ADDR_LO, ALE_ADDR_LO(paddr));
	paddr = sc->ale_cdata.ale_rx_page[1].page_paddr;
	CSR_WRITE_4(sc, ALE_RXF0_PAGE1_ADDR_LO, ALE_ADDR_LO(paddr));

	/* Set Tx/Rx CMB addresses. */
	paddr = sc->ale_cdata.ale_tx_cmb_paddr;
	CSR_WRITE_4(sc, ALE_TX_CMB_ADDR_LO, ALE_ADDR_LO(paddr));
	paddr = sc->ale_cdata.ale_rx_page[0].cmb_paddr;
	CSR_WRITE_4(sc, ALE_RXF0_CMB0_ADDR_LO, ALE_ADDR_LO(paddr));
	paddr = sc->ale_cdata.ale_rx_page[1].cmb_paddr;
	CSR_WRITE_4(sc, ALE_RXF0_CMB1_ADDR_LO, ALE_ADDR_LO(paddr));

	/* Mark RXF0 is valid. */
	CSR_WRITE_1(sc, ALE_RXF0_PAGE0, RXF_VALID);
	CSR_WRITE_1(sc, ALE_RXF0_PAGE1, RXF_VALID);
	/*
	 * No need to initialize RFX1/RXF2/RXF3. We don't use
	 * multi-queue yet.
	 */

	/* Set Rx page size, excluding guard frame size. */
	CSR_WRITE_4(sc, ALE_RXF_PAGE_SIZE, ALE_RX_PAGE_SZ);

	/* Tell hardware that we're ready to load DMA blocks. */
	CSR_WRITE_4(sc, ALE_DMA_BLOCK, DMA_BLOCK_LOAD);

	/* Set Rx/Tx interrupt trigger threshold. */
	CSR_WRITE_4(sc, ALE_INT_TRIG_THRESH, (1 << INT_TRIG_RX_THRESH_SHIFT) |
	    (4 << INT_TRIG_TX_THRESH_SHIFT));
	/*
	 * XXX
	 * Set interrupt trigger timer, its purpose and relation
	 * with interrupt moderation mechanism is not clear yet.
	 */
	CSR_WRITE_4(sc, ALE_INT_TRIG_TIMER,
	    ((ALE_USECS(10) << INT_TRIG_RX_TIMER_SHIFT) |
	    (ALE_USECS(1000) << INT_TRIG_TX_TIMER_SHIFT)));

	/* Configure interrupt moderation timer. */
	sc->ale_int_rx_mod = ALE_IM_RX_TIMER_DEFAULT;
	sc->ale_int_tx_mod = ALE_IM_TX_TIMER_DEFAULT;
	reg = ALE_USECS(sc->ale_int_rx_mod) << IM_TIMER_RX_SHIFT;
	reg |= ALE_USECS(sc->ale_int_tx_mod) << IM_TIMER_TX_SHIFT;
	CSR_WRITE_4(sc, ALE_IM_TIMER, reg);
	reg = CSR_READ_4(sc, ALE_MASTER_CFG);
	reg &= ~(MASTER_CHIP_REV_MASK | MASTER_CHIP_ID_MASK);
	reg &= ~(MASTER_IM_RX_TIMER_ENB | MASTER_IM_TX_TIMER_ENB);
	if (ALE_USECS(sc->ale_int_rx_mod) != 0)
		reg |= MASTER_IM_RX_TIMER_ENB;
	if (ALE_USECS(sc->ale_int_tx_mod) != 0)
		reg |= MASTER_IM_TX_TIMER_ENB;
	CSR_WRITE_4(sc, ALE_MASTER_CFG, reg);
	CSR_WRITE_2(sc, ALE_INTR_CLR_TIMER, ALE_USECS(1000));

	/* Set Maximum frame size of controller. */
	if (ifp->if_mtu < ETHERMTU)
		sc->ale_max_frame_size = ETHERMTU;
	else
		sc->ale_max_frame_size = ifp->if_mtu;
	sc->ale_max_frame_size += ETHER_HDR_LEN + EVL_ENCAPLEN + ETHER_CRC_LEN;
	CSR_WRITE_4(sc, ALE_FRAME_SIZE, sc->ale_max_frame_size);

	/* Configure IPG/IFG parameters. */
	CSR_WRITE_4(sc, ALE_IPG_IFG_CFG,
	    ((IPG_IFG_IPGT_DEFAULT << IPG_IFG_IPGT_SHIFT) & IPG_IFG_IPGT_MASK) |
	    ((IPG_IFG_MIFG_DEFAULT << IPG_IFG_MIFG_SHIFT) & IPG_IFG_MIFG_MASK) |
	    ((IPG_IFG_IPG1_DEFAULT << IPG_IFG_IPG1_SHIFT) & IPG_IFG_IPG1_MASK) |
	    ((IPG_IFG_IPG2_DEFAULT << IPG_IFG_IPG2_SHIFT) & IPG_IFG_IPG2_MASK));

	/* Set parameters for half-duplex media. */
	CSR_WRITE_4(sc, ALE_HDPX_CFG,
	    ((HDPX_CFG_LCOL_DEFAULT << HDPX_CFG_LCOL_SHIFT) &
	    HDPX_CFG_LCOL_MASK) |
	    ((HDPX_CFG_RETRY_DEFAULT << HDPX_CFG_RETRY_SHIFT) &
	    HDPX_CFG_RETRY_MASK) | HDPX_CFG_EXC_DEF_EN |
	    ((HDPX_CFG_ABEBT_DEFAULT << HDPX_CFG_ABEBT_SHIFT) &
	    HDPX_CFG_ABEBT_MASK) |
	    ((HDPX_CFG_JAMIPG_DEFAULT << HDPX_CFG_JAMIPG_SHIFT) &
	    HDPX_CFG_JAMIPG_MASK));

	/* Configure Tx jumbo frame parameters. */
	if ((sc->ale_flags & ALE_FLAG_JUMBO) != 0) {
		if (ifp->if_mtu < ETHERMTU)
			reg = sc->ale_max_frame_size;
		else if (ifp->if_mtu < 6 * 1024)
			reg = (sc->ale_max_frame_size * 2) / 3;
		else
			reg = sc->ale_max_frame_size / 2;
		CSR_WRITE_4(sc, ALE_TX_JUMBO_THRESH,
		    roundup(reg, TX_JUMBO_THRESH_UNIT) >>
		    TX_JUMBO_THRESH_UNIT_SHIFT);
	}

	/* Configure TxQ. */
	reg = (128 << (sc->ale_dma_rd_burst >> DMA_CFG_RD_BURST_SHIFT))
	    << TXQ_CFG_TX_FIFO_BURST_SHIFT;
	reg |= (TXQ_CFG_TPD_BURST_DEFAULT << TXQ_CFG_TPD_BURST_SHIFT) &
	    TXQ_CFG_TPD_BURST_MASK;
	CSR_WRITE_4(sc, ALE_TXQ_CFG, reg | TXQ_CFG_ENHANCED_MODE | TXQ_CFG_ENB);

	/* Configure Rx jumbo frame & flow control parameters. */
	if ((sc->ale_flags & ALE_FLAG_JUMBO) != 0) {
		reg = roundup(sc->ale_max_frame_size, RX_JUMBO_THRESH_UNIT);
		CSR_WRITE_4(sc, ALE_RX_JUMBO_THRESH,
		    (((reg >> RX_JUMBO_THRESH_UNIT_SHIFT) <<
		    RX_JUMBO_THRESH_MASK_SHIFT) & RX_JUMBO_THRESH_MASK) |
		    ((RX_JUMBO_LKAH_DEFAULT << RX_JUMBO_LKAH_SHIFT) &
		    RX_JUMBO_LKAH_MASK));
		reg = CSR_READ_4(sc, ALE_SRAM_RX_FIFO_LEN);
		rxf_hi = (reg * 7) / 10;
		rxf_lo = (reg * 3)/ 10;
		CSR_WRITE_4(sc, ALE_RX_FIFO_PAUSE_THRESH,
		    ((rxf_lo << RX_FIFO_PAUSE_THRESH_LO_SHIFT) &
		    RX_FIFO_PAUSE_THRESH_LO_MASK) |
		    ((rxf_hi << RX_FIFO_PAUSE_THRESH_HI_SHIFT) &
		     RX_FIFO_PAUSE_THRESH_HI_MASK));
	}

	/* Disable RSS. */
	CSR_WRITE_4(sc, ALE_RSS_IDT_TABLE0, 0);
	CSR_WRITE_4(sc, ALE_RSS_CPU, 0);

	/* Configure RxQ. */
	CSR_WRITE_4(sc, ALE_RXQ_CFG,
	    RXQ_CFG_ALIGN_32 | RXQ_CFG_CUT_THROUGH_ENB | RXQ_CFG_ENB);

	/* Configure DMA parameters. */
	reg = 0;
	if ((sc->ale_flags & ALE_FLAG_TXCMB_BUG) == 0)
		reg |= DMA_CFG_TXCMB_ENB;
	CSR_WRITE_4(sc, ALE_DMA_CFG,
	    DMA_CFG_OUT_ORDER | DMA_CFG_RD_REQ_PRI | DMA_CFG_RCB_64 |
	    sc->ale_dma_rd_burst | reg |
	    sc->ale_dma_wr_burst | DMA_CFG_RXCMB_ENB |
	    ((DMA_CFG_RD_DELAY_CNT_DEFAULT << DMA_CFG_RD_DELAY_CNT_SHIFT) &
	    DMA_CFG_RD_DELAY_CNT_MASK) |
	    ((DMA_CFG_WR_DELAY_CNT_DEFAULT << DMA_CFG_WR_DELAY_CNT_SHIFT) &
	    DMA_CFG_WR_DELAY_CNT_MASK));

	/*
	 * Hardware can be configured to issue SMB interrupt based
	 * on programmed interval. Since there is a callout that is
	 * invoked for every hz in driver we use that instead of
	 * relying on periodic SMB interrupt.
	 */
	CSR_WRITE_4(sc, ALE_SMB_STAT_TIMER, ALE_USECS(0));

	/* Clear MAC statistics. */
	ale_stats_clear(sc);

	/*
	 * Configure Tx/Rx MACs.
	 *  - Auto-padding for short frames.
	 *  - Enable CRC generation.
	 *  Actual reconfiguration of MAC for resolved speed/duplex
	 *  is followed after detection of link establishment.
	 *  AR81xx always does checksum computation regardless of
	 *  MAC_CFG_RXCSUM_ENB bit. In fact, setting the bit will
	 *  cause Rx handling issue for fragmented IP datagrams due
	 *  to silicon bug.
	 */
	reg = MAC_CFG_TX_CRC_ENB | MAC_CFG_TX_AUTO_PAD | MAC_CFG_FULL_DUPLEX |
	    ((MAC_CFG_PREAMBLE_DEFAULT << MAC_CFG_PREAMBLE_SHIFT) &
	    MAC_CFG_PREAMBLE_MASK);
	if ((sc->ale_flags & ALE_FLAG_FASTETHER) != 0)
		reg |= MAC_CFG_SPEED_10_100;
	else
		reg |= MAC_CFG_SPEED_1000;
	CSR_WRITE_4(sc, ALE_MAC_CFG, reg);

	/* Set up the receive filter. */
	ale_iff(sc);

	ale_rxvlan(sc);

	/* Acknowledge all pending interrupts and clear it. */
	CSR_WRITE_4(sc, ALE_INTR_MASK, ALE_INTRS);
	CSR_WRITE_4(sc, ALE_INTR_STATUS, 0xFFFFFFFF);
	CSR_WRITE_4(sc, ALE_INTR_STATUS, 0);

	sc->ale_flags &= ~ALE_FLAG_LINK;

	/* Switch to the current media. */
	mii = &sc->sc_miibus;
	mii_mediachg(mii);

	timeout_add_sec(&sc->ale_tick_ch, 1);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	return 0;
}

void
ale_stop(struct ale_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct ale_txdesc *txd;
	uint32_t reg;
	int i;

	/*
	 * Mark the interface down and cancel the watchdog timer.
	 */
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
	ifp->if_timer = 0;

	timeout_del(&sc->ale_tick_ch);
	sc->ale_flags &= ~ALE_FLAG_LINK;

	ale_stats_update(sc);

	/* Disable interrupts. */
	CSR_WRITE_4(sc, ALE_INTR_MASK, 0);
	CSR_WRITE_4(sc, ALE_INTR_STATUS, 0xFFFFFFFF);

	/* Disable queue processing and DMA. */
	reg = CSR_READ_4(sc, ALE_TXQ_CFG);
	reg &= ~TXQ_CFG_ENB;
	CSR_WRITE_4(sc, ALE_TXQ_CFG, reg);
	reg = CSR_READ_4(sc, ALE_RXQ_CFG);
	reg &= ~RXQ_CFG_ENB;
	CSR_WRITE_4(sc, ALE_RXQ_CFG, reg);
	reg = CSR_READ_4(sc, ALE_DMA_CFG);
	reg &= ~(DMA_CFG_TXCMB_ENB | DMA_CFG_RXCMB_ENB);
	CSR_WRITE_4(sc, ALE_DMA_CFG, reg);
	DELAY(1000);

	/* Stop Rx/Tx MACs. */
	ale_stop_mac(sc);

	/* Disable interrupts again? XXX */
	CSR_WRITE_4(sc, ALE_INTR_STATUS, 0xFFFFFFFF);

	/*
	 * Free TX mbufs still in the queues.
	 */
	for (i = 0; i < ALE_TX_RING_CNT; i++) {
		txd = &sc->ale_cdata.ale_txdesc[i];
		if (txd->tx_m != NULL) {
			bus_dmamap_sync(sc->sc_dmat, txd->tx_dmamap, 0,
			    txd->tx_dmamap->dm_mapsize, BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_dmat, txd->tx_dmamap);
			m_freem(txd->tx_m);
			txd->tx_m = NULL;
		}
        }
}

void
ale_stop_mac(struct ale_softc *sc)
{
	uint32_t reg;
	int i;

	reg = CSR_READ_4(sc, ALE_MAC_CFG);
	if ((reg & (MAC_CFG_TX_ENB | MAC_CFG_RX_ENB)) != 0) {
		reg &= ~(MAC_CFG_TX_ENB | MAC_CFG_RX_ENB);
		CSR_WRITE_4(sc, ALE_MAC_CFG, reg);
	}

	for (i = ALE_TIMEOUT; i > 0; i--) {
		reg = CSR_READ_4(sc, ALE_IDLE_STATUS);
		if (reg == 0)
			break;
		DELAY(10);
	}
	if (i == 0)
		printf("%s: could not disable Tx/Rx MAC(0x%08x)!\n",
		    sc->sc_dev.dv_xname, reg);
}

void
ale_init_tx_ring(struct ale_softc *sc)
{
	struct ale_txdesc *txd;
	int i;

	sc->ale_cdata.ale_tx_prod = 0;
	sc->ale_cdata.ale_tx_cons = 0;
	sc->ale_cdata.ale_tx_cnt = 0;

	bzero(sc->ale_cdata.ale_tx_ring, ALE_TX_RING_SZ);
	bzero(sc->ale_cdata.ale_tx_cmb, ALE_TX_CMB_SZ);
	for (i = 0; i < ALE_TX_RING_CNT; i++) {
		txd = &sc->ale_cdata.ale_txdesc[i];
		txd->tx_m = NULL;
	}
	*sc->ale_cdata.ale_tx_cmb = 0;
	bus_dmamap_sync(sc->sc_dmat, sc->ale_cdata.ale_tx_cmb_map, 0,
	    sc->ale_cdata.ale_tx_cmb_map->dm_mapsize,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->sc_dmat, sc->ale_cdata.ale_tx_ring_map, 0,
	    sc->ale_cdata.ale_tx_ring_map->dm_mapsize,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}

void
ale_init_rx_pages(struct ale_softc *sc)
{
	struct ale_rx_page *rx_page;
	int i;

	sc->ale_cdata.ale_rx_seqno = 0;
	sc->ale_cdata.ale_rx_curp = 0;

	for (i = 0; i < ALE_RX_PAGES; i++) {
		rx_page = &sc->ale_cdata.ale_rx_page[i];
		bzero(rx_page->page_addr, sc->ale_pagesize);
		bzero(rx_page->cmb_addr, ALE_RX_CMB_SZ);
		rx_page->cons = 0;
		*rx_page->cmb_addr = 0;
		bus_dmamap_sync(sc->sc_dmat, rx_page->page_map, 0,
		    rx_page->page_map->dm_mapsize,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
		bus_dmamap_sync(sc->sc_dmat, rx_page->cmb_map, 0,
		    rx_page->cmb_map->dm_mapsize,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	}
}

void
ale_rxvlan(struct ale_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	uint32_t reg;

	reg = CSR_READ_4(sc, ALE_MAC_CFG);
	reg &= ~MAC_CFG_VLAN_TAG_STRIP;
	if (ifp->if_capabilities & IFCAP_VLAN_HWTAGGING)
		reg |= MAC_CFG_VLAN_TAG_STRIP;
	CSR_WRITE_4(sc, ALE_MAC_CFG, reg);
}

void
ale_iff(struct ale_softc *sc)
{
	struct arpcom *ac = &sc->sc_arpcom;
	struct ifnet *ifp = &ac->ac_if;
	struct ether_multi *enm;
	struct ether_multistep step;
	uint32_t crc;
	uint32_t mchash[2];
	uint32_t rxcfg;

	rxcfg = CSR_READ_4(sc, ALE_MAC_CFG);
	rxcfg &= ~(MAC_CFG_ALLMULTI | MAC_CFG_BCAST | MAC_CFG_PROMISC);
	ifp->if_flags &= ~IFF_ALLMULTI;

	/*
	 * Always accept broadcast frames.
	 */
	rxcfg |= MAC_CFG_BCAST;

	if (ifp->if_flags & IFF_PROMISC || ac->ac_multirangecnt > 0) {
		ifp->if_flags |= IFF_ALLMULTI;
		if (ifp->if_flags & IFF_PROMISC)
			rxcfg |= MAC_CFG_PROMISC;
		else
			rxcfg |= MAC_CFG_ALLMULTI;
		mchash[0] = mchash[1] = 0xFFFFFFFF;
	} else {
		/* Program new filter. */
		bzero(mchash, sizeof(mchash));

		ETHER_FIRST_MULTI(step, ac, enm);
		while (enm != NULL) {
			crc = ether_crc32_be(enm->enm_addrlo, ETHER_ADDR_LEN);

			mchash[crc >> 31] |= 1 << ((crc >> 26) & 0x1f);

			ETHER_NEXT_MULTI(step, enm);
		}
	}

	CSR_WRITE_4(sc, ALE_MAR0, mchash[0]);
	CSR_WRITE_4(sc, ALE_MAR1, mchash[1]);
	CSR_WRITE_4(sc, ALE_MAC_CFG, rxcfg);
}
