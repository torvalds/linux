/*	$OpenBSD: if_age.c,v 1.40 2024/05/24 06:02:53 jsg Exp $	*/

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
 */

/* Driver for Attansic Technology Corp. L1 Gigabit Ethernet. */

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
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/pci/if_agereg.h>

int	age_match(struct device *, void *, void *);
void	age_attach(struct device *, struct device *, void *);
int	age_detach(struct device *, int);

int	age_miibus_readreg(struct device *, int, int);
void	age_miibus_writereg(struct device *, int, int, int);
void	age_miibus_statchg(struct device *);

int	age_init(struct ifnet *);
int	age_ioctl(struct ifnet *, u_long, caddr_t);
void	age_start(struct ifnet *);
void	age_watchdog(struct ifnet *);
void	age_mediastatus(struct ifnet *, struct ifmediareq *);
int	age_mediachange(struct ifnet *);

int	age_intr(void *);
int	age_dma_alloc(struct age_softc *);
void	age_dma_free(struct age_softc *);
void	age_get_macaddr(struct age_softc *);
void	age_phy_reset(struct age_softc *);

int	age_encap(struct age_softc *, struct mbuf *);
void	age_init_tx_ring(struct age_softc *);
int	age_init_rx_ring(struct age_softc *);
void	age_init_rr_ring(struct age_softc *);
void	age_init_cmb_block(struct age_softc *);
void	age_init_smb_block(struct age_softc *);
int	age_newbuf(struct age_softc *, struct age_rxdesc *);
void	age_mac_config(struct age_softc *);
void	age_txintr(struct age_softc *, int);
void	age_rxeof(struct age_softc *sc, struct rx_rdesc *);
void	age_rxintr(struct age_softc *, int);
void	age_tick(void *);
void	age_reset(struct age_softc *);
void	age_stop(struct age_softc *);
void	age_stats_update(struct age_softc *);
void	age_stop_txmac(struct age_softc *);
void	age_stop_rxmac(struct age_softc *);
void	age_rxvlan(struct age_softc *sc);
void	age_iff(struct age_softc *);

const struct pci_matchid age_devices[] = {
	{ PCI_VENDOR_ATTANSIC, PCI_PRODUCT_ATTANSIC_L1 }
};

const struct cfattach age_ca = {
	sizeof (struct age_softc), age_match, age_attach
};

struct cfdriver age_cd = {
	 NULL, "age", DV_IFNET
};

int agedebug = 0;
#define	DPRINTF(x)	do { if (agedebug) printf x; } while (0)

#define AGE_CSUM_FEATURES	(M_TCP_CSUM_OUT | M_UDP_CSUM_OUT)

int
age_match(struct device *dev, void *match, void *aux)
{
	 return pci_matchbyid((struct pci_attach_args *)aux, age_devices,
	     sizeof (age_devices) / sizeof (age_devices[0]));
}

void
age_attach(struct device *parent, struct device *self, void *aux)
{
	struct age_softc *sc = (struct age_softc *)self;
	struct pci_attach_args *pa = aux;
	pci_chipset_tag_t pc = pa->pa_pc;
	pci_intr_handle_t ih;
	const char *intrstr;
	struct ifnet *ifp;
	pcireg_t memtype;
	int error = 0;

	/*
	 * Allocate IO memory
	 */
	memtype = pci_mapreg_type(pa->pa_pc, pa->pa_tag, AGE_PCIR_BAR);
	if (pci_mapreg_map(pa, AGE_PCIR_BAR, memtype, 0, &sc->sc_mem_bt,
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
	sc->sc_irq_handle = pci_intr_establish(pc, ih, IPL_NET, age_intr, sc,
	    sc->sc_dev.dv_xname);
	if (sc->sc_irq_handle == NULL) {
		printf(": could not establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		goto fail;
	}
	printf(": %s", intrstr);

	sc->sc_dmat = pa->pa_dmat;
	sc->sc_pct = pa->pa_pc;
	sc->sc_pcitag = pa->pa_tag;

	/* Set PHY address. */
	sc->age_phyaddr = AGE_PHY_ADDR;

	/* Reset PHY. */
	age_phy_reset(sc);

	/* Reset the ethernet controller. */
	age_reset(sc);

	/* Get PCI and chip id/revision. */
	sc->age_rev = PCI_REVISION(pa->pa_class);
	sc->age_chip_rev = CSR_READ_4(sc, AGE_MASTER_CFG) >> 
	    MASTER_CHIP_REV_SHIFT;
	if (agedebug) {
		printf("%s: PCI device revision : 0x%04x\n",
		    sc->sc_dev.dv_xname, sc->age_rev);
		printf("%s: Chip id/revision : 0x%04x\n",
		    sc->sc_dev.dv_xname, sc->age_chip_rev);
	}

	if (agedebug) {
		printf("%s: %d Tx FIFO, %d Rx FIFO\n", sc->sc_dev.dv_xname,
		    CSR_READ_4(sc, AGE_SRAM_TX_FIFO_LEN),
		    CSR_READ_4(sc, AGE_SRAM_RX_FIFO_LEN));
	}

	/* Set max allowable DMA size. */
	sc->age_dma_rd_burst = DMA_CFG_RD_BURST_128;
	sc->age_dma_wr_burst = DMA_CFG_WR_BURST_128;

	/* Allocate DMA stuffs */
	error = age_dma_alloc(sc);
	if (error)
		goto fail;

	/* Load station address. */
	age_get_macaddr(sc);

	ifp = &sc->sc_arpcom.ac_if;
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = age_ioctl;
	ifp->if_start = age_start;
	ifp->if_watchdog = age_watchdog;
	ifq_init_maxlen(&ifp->if_snd, AGE_TX_RING_CNT - 1);
	bcopy(sc->age_eaddr, sc->sc_arpcom.ac_enaddr, ETHER_ADDR_LEN);
	bcopy(sc->sc_dev.dv_xname, ifp->if_xname, IFNAMSIZ);

	ifp->if_capabilities = IFCAP_VLAN_MTU;

#ifdef AGE_CHECKSUM
	ifp->if_capabilities |= IFCAP_CSUM_IPv4 | IFCAP_CSUM_TCPv4 |
				IFCAP_CSUM_UDPv4;
#endif

#if NVLAN > 0
	ifp->if_capabilities |= IFCAP_VLAN_HWTAGGING;
#endif

	printf(", address %s\n", ether_sprintf(sc->sc_arpcom.ac_enaddr));

	/* Set up MII bus. */
	sc->sc_miibus.mii_ifp = ifp;
	sc->sc_miibus.mii_readreg = age_miibus_readreg;
	sc->sc_miibus.mii_writereg = age_miibus_writereg;
	sc->sc_miibus.mii_statchg = age_miibus_statchg;

	ifmedia_init(&sc->sc_miibus.mii_media, 0, age_mediachange,
	    age_mediastatus);
	mii_attach(self, &sc->sc_miibus, 0xffffffff, MII_PHY_ANY,
	   MII_OFFSET_ANY, MIIF_DOPAUSE);

	if (LIST_FIRST(&sc->sc_miibus.mii_phys) == NULL) {
		printf("%s: no PHY found!\n", sc->sc_dev.dv_xname);
		ifmedia_add(&sc->sc_miibus.mii_media, IFM_ETHER | IFM_MANUAL,
		    0, NULL);
		ifmedia_set(&sc->sc_miibus.mii_media, IFM_ETHER | IFM_MANUAL);
	} else
		ifmedia_set(&sc->sc_miibus.mii_media, IFM_ETHER | IFM_AUTO);

	if_attach(ifp);
	ether_ifattach(ifp);

	timeout_set(&sc->age_tick_ch, age_tick, sc);

	return;
fail:
	age_dma_free(sc);
	if (sc->sc_irq_handle != NULL)
		pci_intr_disestablish(pc, sc->sc_irq_handle);
	if (sc->sc_mem_size)
		bus_space_unmap(sc->sc_mem_bt, sc->sc_mem_bh, sc->sc_mem_size);
}

int
age_detach(struct device *self, int flags)
{
	struct age_softc *sc = (struct age_softc *)self;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	int s;

	s = splnet();
	age_stop(sc);
	splx(s);

	mii_detach(&sc->sc_miibus, MII_PHY_ANY, MII_OFFSET_ANY);

	/* Delete all remaining media. */
	ifmedia_delete_instance(&sc->sc_miibus.mii_media, IFM_INST_ANY);

	ether_ifdetach(ifp);
	if_detach(ifp);
	age_dma_free(sc);

	if (sc->sc_irq_handle != NULL) {
		pci_intr_disestablish(sc->sc_pct, sc->sc_irq_handle);
		sc->sc_irq_handle = NULL;
	}

	return (0);
}

/*
 *	Read a PHY register on the MII of the L1.
 */
int
age_miibus_readreg(struct device *dev, int phy, int reg)
{
	struct age_softc *sc = (struct age_softc *)dev;
	uint32_t v;
	int i;

	if (phy != sc->age_phyaddr)
		return (0);

	CSR_WRITE_4(sc, AGE_MDIO, MDIO_OP_EXECUTE | MDIO_OP_READ |
	    MDIO_SUP_PREAMBLE | MDIO_CLK_25_4 | MDIO_REG_ADDR(reg));
	for (i = AGE_PHY_TIMEOUT; i > 0; i--) {
		DELAY(1);
		v = CSR_READ_4(sc, AGE_MDIO);
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

/*
 * 	Write a PHY register on the MII of the L1.
 */
void
age_miibus_writereg(struct device *dev, int phy, int reg, int val)
{
	struct age_softc *sc = (struct age_softc *)dev;
	uint32_t v;
	int i;

	if (phy != sc->age_phyaddr)
		return;

	CSR_WRITE_4(sc, AGE_MDIO, MDIO_OP_EXECUTE | MDIO_OP_WRITE |
	    (val & MDIO_DATA_MASK) << MDIO_DATA_SHIFT |
	    MDIO_SUP_PREAMBLE | MDIO_CLK_25_4 | MDIO_REG_ADDR(reg));

	for (i = AGE_PHY_TIMEOUT; i > 0; i--) {
		DELAY(1);
		v = CSR_READ_4(sc, AGE_MDIO);
		if ((v & (MDIO_OP_EXECUTE | MDIO_OP_BUSY)) == 0)
			break;
	}

	if (i == 0) {
		printf("%s: phy write timeout: phy %d, reg %d\n",
		    sc->sc_dev.dv_xname, phy, reg);
	}
}

/*
 *	Callback from MII layer when media changes.
 */
void
age_miibus_statchg(struct device *dev)
{
	struct age_softc *sc = (struct age_softc *)dev;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct mii_data *mii = &sc->sc_miibus;

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		return;

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
		uint32_t reg;

		age_mac_config(sc);
		reg = CSR_READ_4(sc, AGE_MAC_CFG);
		/* Restart DMA engine and Tx/Rx MAC. */
		CSR_WRITE_4(sc, AGE_DMA_CFG, CSR_READ_4(sc, AGE_DMA_CFG) |
		    DMA_CFG_RD_ENB | DMA_CFG_WR_ENB);
		reg |= MAC_CFG_TX_ENB | MAC_CFG_RX_ENB;
		CSR_WRITE_4(sc, AGE_MAC_CFG, reg);
	}
}

/*
 *	Get the current interface media status.
 */
void
age_mediastatus(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct age_softc *sc = ifp->if_softc;
	struct mii_data *mii = &sc->sc_miibus;

	mii_pollstat(mii);
	ifmr->ifm_status = mii->mii_media_status;
	ifmr->ifm_active = mii->mii_media_active;
}

/*
 *	Set hardware to newly-selected media.
 */
int
age_mediachange(struct ifnet *ifp)
{
	struct age_softc *sc = ifp->if_softc;
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
age_intr(void *arg)
{
        struct age_softc *sc = arg;
        struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct cmb *cmb;
        uint32_t status;

	status = CSR_READ_4(sc, AGE_INTR_STATUS);
	if (status == 0 || (status & AGE_INTRS) == 0)
		return (0);

	/* Disable interrupts. */
	CSR_WRITE_4(sc, AGE_INTR_STATUS, status | INTR_DIS_INT);

	bus_dmamap_sync(sc->sc_dmat, sc->age_cdata.age_cmb_block_map, 0,
	    sc->age_cdata.age_cmb_block_map->dm_mapsize,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	cmb = sc->age_rdata.age_cmb_block;
	status = letoh32(cmb->intr_status);
	if ((status & AGE_INTRS) == 0)
		goto back;

	sc->age_tpd_cons = (letoh32(cmb->tpd_cons) & TPD_CONS_MASK) >>
	    TPD_CONS_SHIFT;
	sc->age_rr_prod = (letoh32(cmb->rprod_cons) & RRD_PROD_MASK) >>
	    RRD_PROD_SHIFT;
	/* Let hardware know CMB was served. */
	cmb->intr_status = 0;
	bus_dmamap_sync(sc->sc_dmat, sc->age_cdata.age_cmb_block_map, 0,
	    sc->age_cdata.age_cmb_block_map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);

	if (ifp->if_flags & IFF_RUNNING) {
		if (status & INTR_CMB_RX)
			age_rxintr(sc, sc->age_rr_prod);

		if (status & INTR_CMB_TX)
			age_txintr(sc, sc->age_tpd_cons);

		if (status & (INTR_DMA_RD_TO_RST | INTR_DMA_WR_TO_RST)) {
			if (status & INTR_DMA_RD_TO_RST)
				printf("%s: DMA read error! -- resetting\n",
				    sc->sc_dev.dv_xname);
			if (status & INTR_DMA_WR_TO_RST)
				printf("%s: DMA write error! -- resetting\n",
				    sc->sc_dev.dv_xname);
			age_init(ifp);
		}

		age_start(ifp);

		if (status & INTR_SMB)
			age_stats_update(sc);
	}

	/* Check whether CMB was updated while serving Tx/Rx/SMB handler. */
	bus_dmamap_sync(sc->sc_dmat, sc->age_cdata.age_cmb_block_map, 0,
	    sc->age_cdata.age_cmb_block_map->dm_mapsize,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

back:
	/* Re-enable interrupts. */
	CSR_WRITE_4(sc, AGE_INTR_STATUS, 0);

	return (1);
}

void
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

	if (pci_get_capability(sc->sc_pct, sc->sc_pcitag,
	    PCI_CAP_VPD, &vpdc, NULL)) {
		/*
		 * PCI VPD capability found, let TWSI reload EEPROM.
		 * This will set Ethernet address of controller.
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
			printf("%s: reloading EEPROM timeout!\n",
			    sc->sc_dev.dv_xname);
	} else {
		if (agedebug)
			printf("%s: PCI VPD capability not found!\n", 
			    sc->sc_dev.dv_xname);
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

void
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
	age_miibus_writereg(&sc->sc_dev, sc->age_phyaddr, MII_BMCR, BMCR_RESET);
	for (linkup = 0, pn = 0; pn < 4; pn++) {
		age_miibus_writereg(&sc->sc_dev, sc->age_phyaddr, ATPHY_CDTC,
		    (pn << PHY_CDTC_POFF) | PHY_CDTC_ENB);
		for (i = 200; i > 0; i--) {
			DELAY(1000);
			reg = age_miibus_readreg(&sc->sc_dev, sc->age_phyaddr,
			    ATPHY_CDTC);
			if ((reg & PHY_CDTC_ENB) == 0)
				break;
		}
		DELAY(1000);
		reg = age_miibus_readreg(&sc->sc_dev, sc->age_phyaddr,
		    ATPHY_CDTS);
		if ((reg & PHY_CDTS_STAT_MASK) != PHY_CDTS_STAT_OPEN) {
			linkup++;
			break;
		}
	}
	age_miibus_writereg(&sc->sc_dev, sc->age_phyaddr, MII_BMCR,
	    BMCR_RESET | BMCR_AUTOEN | BMCR_STARTNEG);
	if (linkup == 0) {
		age_miibus_writereg(&sc->sc_dev, sc->age_phyaddr,
		    ATPHY_DBG_ADDR, 0);
		age_miibus_writereg(&sc->sc_dev, sc->age_phyaddr,
		    ATPHY_DBG_DATA, 0x124E);
		age_miibus_writereg(&sc->sc_dev, sc->age_phyaddr,
		    ATPHY_DBG_ADDR, 1);
		reg = age_miibus_readreg(&sc->sc_dev, sc->age_phyaddr,
		    ATPHY_DBG_DATA);
		age_miibus_writereg(&sc->sc_dev, sc->age_phyaddr,
		    ATPHY_DBG_DATA, reg | 0x03);
		/* XXX */
		DELAY(1500 * 1000);
		age_miibus_writereg(&sc->sc_dev, sc->age_phyaddr,
		    ATPHY_DBG_ADDR, 0);
		age_miibus_writereg(&sc->sc_dev, sc->age_phyaddr,
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

int
age_dma_alloc(struct age_softc *sc)
{
	struct age_txdesc *txd;
	struct age_rxdesc *rxd;
	int nsegs, error, i;

	/*
	 * Create DMA stuffs for TX ring
	 */
	error = bus_dmamap_create(sc->sc_dmat, AGE_TX_RING_SZ, 1, 
	    AGE_TX_RING_SZ, 0, BUS_DMA_NOWAIT, &sc->age_cdata.age_tx_ring_map);
	if (error)
		return (ENOBUFS);

	/* Allocate DMA'able memory for TX ring */
	error = bus_dmamem_alloc(sc->sc_dmat, AGE_TX_RING_SZ, 
	    ETHER_ALIGN, 0, &sc->age_rdata.age_tx_ring_seg, 1, 
	    &nsegs, BUS_DMA_WAITOK | BUS_DMA_ZERO);
	if (error) {
		printf("%s: could not allocate DMA'able memory for Tx ring.\n",
		    sc->sc_dev.dv_xname);
		return error;
	}

	error = bus_dmamem_map(sc->sc_dmat, &sc->age_rdata.age_tx_ring_seg,
	    nsegs, AGE_TX_RING_SZ, (caddr_t *)&sc->age_rdata.age_tx_ring,
	    BUS_DMA_NOWAIT);
	if (error) 
		return (ENOBUFS);

	/*  Load the DMA map for Tx ring. */
	error = bus_dmamap_load(sc->sc_dmat, sc->age_cdata.age_tx_ring_map,
	    sc->age_rdata.age_tx_ring, AGE_TX_RING_SZ, NULL, BUS_DMA_WAITOK);
	if (error) {
		printf("%s: could not load DMA'able memory for Tx ring.\n",
		    sc->sc_dev.dv_xname);
		bus_dmamem_free(sc->sc_dmat, 
		    (bus_dma_segment_t *)&sc->age_rdata.age_tx_ring, 1);
		return error;
	}

	sc->age_rdata.age_tx_ring_paddr = 
	    sc->age_cdata.age_tx_ring_map->dm_segs[0].ds_addr;

	/*
	 * Create DMA stuffs for RX ring
	 */
	error = bus_dmamap_create(sc->sc_dmat, AGE_RX_RING_SZ, 1, 
	    AGE_RX_RING_SZ, 0, BUS_DMA_NOWAIT, &sc->age_cdata.age_rx_ring_map);
	if (error) 
		return (ENOBUFS);

	/* Allocate DMA'able memory for RX ring */
	error = bus_dmamem_alloc(sc->sc_dmat, AGE_RX_RING_SZ, 
	    ETHER_ALIGN, 0, &sc->age_rdata.age_rx_ring_seg, 1, 
	    &nsegs, BUS_DMA_WAITOK | BUS_DMA_ZERO);
	if (error) {
		printf("%s: could not allocate DMA'able memory for Rx ring.\n",
		    sc->sc_dev.dv_xname);
		return error;
	}

	error = bus_dmamem_map(sc->sc_dmat, &sc->age_rdata.age_rx_ring_seg,
	    nsegs, AGE_RX_RING_SZ, (caddr_t *)&sc->age_rdata.age_rx_ring,
	    BUS_DMA_NOWAIT);
	if (error)
		return (ENOBUFS);

	/* Load the DMA map for Rx ring. */
	error = bus_dmamap_load(sc->sc_dmat, sc->age_cdata.age_rx_ring_map,
	    sc->age_rdata.age_rx_ring, AGE_RX_RING_SZ, NULL, BUS_DMA_WAITOK);
	if (error) {
		printf("%s: could not load DMA'able memory for Rx ring.\n",
		    sc->sc_dev.dv_xname);
		bus_dmamem_free(sc->sc_dmat,
		    (bus_dma_segment_t *)sc->age_rdata.age_rx_ring, 1);
		return error;
	}

	sc->age_rdata.age_rx_ring_paddr = 
	    sc->age_cdata.age_rx_ring_map->dm_segs[0].ds_addr;

	/*
	 * Create DMA stuffs for RX return ring
	 */
	error = bus_dmamap_create(sc->sc_dmat, AGE_RR_RING_SZ, 1, 
	    AGE_RR_RING_SZ, 0, BUS_DMA_NOWAIT, &sc->age_cdata.age_rr_ring_map);
	if (error)
		return (ENOBUFS);

	/* Allocate DMA'able memory for RX return ring */
	error = bus_dmamem_alloc(sc->sc_dmat, AGE_RR_RING_SZ, 
	    ETHER_ALIGN, 0, &sc->age_rdata.age_rr_ring_seg, 1, 
	    &nsegs, BUS_DMA_WAITOK | BUS_DMA_ZERO);
	if (error) {
		printf("%s: could not allocate DMA'able memory for Rx "
		    "return ring.\n", sc->sc_dev.dv_xname);
		return error;
	}

	error = bus_dmamem_map(sc->sc_dmat, &sc->age_rdata.age_rr_ring_seg,
	    nsegs, AGE_RR_RING_SZ, (caddr_t *)&sc->age_rdata.age_rr_ring,
	    BUS_DMA_NOWAIT);
	if (error)
		return (ENOBUFS);

	/*  Load the DMA map for Rx return ring. */
	error = bus_dmamap_load(sc->sc_dmat, sc->age_cdata.age_rr_ring_map,
	    sc->age_rdata.age_rr_ring, AGE_RR_RING_SZ, NULL, BUS_DMA_WAITOK);
	if (error) {
		printf("%s: could not load DMA'able memory for Rx return ring."
		    "\n", sc->sc_dev.dv_xname);
		bus_dmamem_free(sc->sc_dmat,
		    (bus_dma_segment_t *)&sc->age_rdata.age_rr_ring, 1);
		return error;
	}

	sc->age_rdata.age_rr_ring_paddr = 
	    sc->age_cdata.age_rr_ring_map->dm_segs[0].ds_addr;

	/*
	 * Create DMA stuffs for CMB block 
	 */
	error = bus_dmamap_create(sc->sc_dmat, AGE_CMB_BLOCK_SZ, 1, 
	    AGE_CMB_BLOCK_SZ, 0, BUS_DMA_NOWAIT, 
	    &sc->age_cdata.age_cmb_block_map);
	if (error) 
		return (ENOBUFS);

	/* Allocate DMA'able memory for CMB block */
	error = bus_dmamem_alloc(sc->sc_dmat, AGE_CMB_BLOCK_SZ, 
	    ETHER_ALIGN, 0, &sc->age_rdata.age_cmb_block_seg, 1, 
	    &nsegs, BUS_DMA_WAITOK | BUS_DMA_ZERO);
	if (error) {
		printf("%s: could not allocate DMA'able memory for "
		    "CMB block\n", sc->sc_dev.dv_xname);
		return error;
	}

	error = bus_dmamem_map(sc->sc_dmat, &sc->age_rdata.age_cmb_block_seg,
	    nsegs, AGE_CMB_BLOCK_SZ, (caddr_t *)&sc->age_rdata.age_cmb_block,
	    BUS_DMA_NOWAIT);
	if (error)
		return (ENOBUFS);

	/*  Load the DMA map for CMB block. */
	error = bus_dmamap_load(sc->sc_dmat, sc->age_cdata.age_cmb_block_map,
	    sc->age_rdata.age_cmb_block, AGE_CMB_BLOCK_SZ, NULL, 
	    BUS_DMA_WAITOK);
	if (error) {
		printf("%s: could not load DMA'able memory for CMB block\n",
		    sc->sc_dev.dv_xname);
		bus_dmamem_free(sc->sc_dmat,
		    (bus_dma_segment_t *)&sc->age_rdata.age_cmb_block, 1);
		return error;
	}

	sc->age_rdata.age_cmb_block_paddr = 
	    sc->age_cdata.age_cmb_block_map->dm_segs[0].ds_addr;

	/*
	 * Create DMA stuffs for SMB block
	 */
	error = bus_dmamap_create(sc->sc_dmat, AGE_SMB_BLOCK_SZ, 1, 
	    AGE_SMB_BLOCK_SZ, 0, BUS_DMA_NOWAIT, 
	    &sc->age_cdata.age_smb_block_map);
	if (error)
		return (ENOBUFS);

	/* Allocate DMA'able memory for SMB block */
	error = bus_dmamem_alloc(sc->sc_dmat, AGE_SMB_BLOCK_SZ, 
	    ETHER_ALIGN, 0, &sc->age_rdata.age_smb_block_seg, 1, 
	    &nsegs, BUS_DMA_WAITOK | BUS_DMA_ZERO);
	if (error) {
		printf("%s: could not allocate DMA'able memory for "
		    "SMB block\n", sc->sc_dev.dv_xname);
		return error;
	}

	error = bus_dmamem_map(sc->sc_dmat, &sc->age_rdata.age_smb_block_seg,
	    nsegs, AGE_SMB_BLOCK_SZ, (caddr_t *)&sc->age_rdata.age_smb_block,
	    BUS_DMA_NOWAIT);
	if (error)
		return (ENOBUFS);

	/*  Load the DMA map for SMB block */
	error = bus_dmamap_load(sc->sc_dmat, sc->age_cdata.age_smb_block_map,
	    sc->age_rdata.age_smb_block, AGE_SMB_BLOCK_SZ, NULL, 
	    BUS_DMA_WAITOK);
	if (error) {
		printf("%s: could not load DMA'able memory for SMB block\n",
		    sc->sc_dev.dv_xname);
		bus_dmamem_free(sc->sc_dmat,
		    (bus_dma_segment_t *)&sc->age_rdata.age_smb_block, 1);
		return error;
	}

	sc->age_rdata.age_smb_block_paddr = 
	    sc->age_cdata.age_smb_block_map->dm_segs[0].ds_addr;

	/* Create DMA maps for Tx buffers. */
	for (i = 0; i < AGE_TX_RING_CNT; i++) {
		txd = &sc->age_cdata.age_txdesc[i];
		txd->tx_m = NULL;
		txd->tx_dmamap = NULL;
		error = bus_dmamap_create(sc->sc_dmat, AGE_TSO_MAXSIZE,
		    AGE_MAXTXSEGS, AGE_TSO_MAXSEGSIZE, 0, BUS_DMA_NOWAIT,
		    &txd->tx_dmamap);
		if (error) {
			printf("%s: could not create Tx dmamap.\n",
			    sc->sc_dev.dv_xname);
			return error;
		}
	}

	/* Create DMA maps for Rx buffers. */
	error = bus_dmamap_create(sc->sc_dmat, MCLBYTES, 1, MCLBYTES, 0,
	    BUS_DMA_NOWAIT, &sc->age_cdata.age_rx_sparemap);
	if (error) {
		printf("%s: could not create spare Rx dmamap.\n", 
		    sc->sc_dev.dv_xname);
		return error;
	}
	for (i = 0; i < AGE_RX_RING_CNT; i++) {
		rxd = &sc->age_cdata.age_rxdesc[i];
		rxd->rx_m = NULL;
		rxd->rx_dmamap = NULL;
		error = bus_dmamap_create(sc->sc_dmat, MCLBYTES, 1,
		    MCLBYTES, 0, BUS_DMA_NOWAIT, &rxd->rx_dmamap);
		if (error) {
			printf("%s: could not create Rx dmamap.\n",
			    sc->sc_dev.dv_xname);
			return error;
		}
	}

	return (0);
}

void
age_dma_free(struct age_softc *sc)
{
	struct age_txdesc *txd;
	struct age_rxdesc *rxd;
	int i;

	/* Tx buffers */
	for (i = 0; i < AGE_TX_RING_CNT; i++) {
		txd = &sc->age_cdata.age_txdesc[i];
		if (txd->tx_dmamap != NULL) {
			bus_dmamap_destroy(sc->sc_dmat, txd->tx_dmamap);
			txd->tx_dmamap = NULL;
		}
	}
	/* Rx buffers */
	for (i = 0; i < AGE_RX_RING_CNT; i++) {
		rxd = &sc->age_cdata.age_rxdesc[i];
		if (rxd->rx_dmamap != NULL) {
			bus_dmamap_destroy(sc->sc_dmat, rxd->rx_dmamap);
			rxd->rx_dmamap = NULL;
		}
	}
	if (sc->age_cdata.age_rx_sparemap != NULL) {
		bus_dmamap_destroy(sc->sc_dmat, sc->age_cdata.age_rx_sparemap);
		sc->age_cdata.age_rx_sparemap = NULL;
	}

	/* Tx ring. */
	if (sc->age_cdata.age_tx_ring_map != NULL)
		bus_dmamap_unload(sc->sc_dmat, sc->age_cdata.age_tx_ring_map);
	if (sc->age_cdata.age_tx_ring_map != NULL &&
	    sc->age_rdata.age_tx_ring != NULL)
		bus_dmamem_free(sc->sc_dmat,
		    (bus_dma_segment_t *)sc->age_rdata.age_tx_ring, 1);
	sc->age_rdata.age_tx_ring = NULL;
	sc->age_cdata.age_tx_ring_map = NULL;

	/* Rx ring. */
	if (sc->age_cdata.age_rx_ring_map != NULL) 
		bus_dmamap_unload(sc->sc_dmat, sc->age_cdata.age_rx_ring_map);
	if (sc->age_cdata.age_rx_ring_map != NULL &&
	    sc->age_rdata.age_rx_ring != NULL)
		bus_dmamem_free(sc->sc_dmat, 
		    (bus_dma_segment_t *)sc->age_rdata.age_rx_ring, 1);
	sc->age_rdata.age_rx_ring = NULL;
	sc->age_cdata.age_rx_ring_map = NULL;

	/* Rx return ring. */
	if (sc->age_cdata.age_rr_ring_map != NULL)
		bus_dmamap_unload(sc->sc_dmat, sc->age_cdata.age_rr_ring_map);
	if (sc->age_cdata.age_rr_ring_map != NULL &&
	    sc->age_rdata.age_rr_ring != NULL)
		bus_dmamem_free(sc->sc_dmat, 
		    (bus_dma_segment_t *)sc->age_rdata.age_rr_ring, 1);
	sc->age_rdata.age_rr_ring = NULL;
	sc->age_cdata.age_rr_ring_map = NULL;

	/* CMB block */
	if (sc->age_cdata.age_cmb_block_map != NULL)
		bus_dmamap_unload(sc->sc_dmat, sc->age_cdata.age_cmb_block_map);
	if (sc->age_cdata.age_cmb_block_map != NULL &&
	    sc->age_rdata.age_cmb_block != NULL)
		bus_dmamem_free(sc->sc_dmat,
		    (bus_dma_segment_t *)sc->age_rdata.age_cmb_block, 1);
	sc->age_rdata.age_cmb_block = NULL;
	sc->age_cdata.age_cmb_block_map = NULL;

	/* SMB block */
	if (sc->age_cdata.age_smb_block_map != NULL)
		bus_dmamap_unload(sc->sc_dmat, sc->age_cdata.age_smb_block_map);
	if (sc->age_cdata.age_smb_block_map != NULL &&
	    sc->age_rdata.age_smb_block != NULL)
		bus_dmamem_free(sc->sc_dmat, 
		    (bus_dma_segment_t *)sc->age_rdata.age_smb_block, 1);
	sc->age_rdata.age_smb_block = NULL;
	sc->age_cdata.age_smb_block_map = NULL;
}

void
age_start(struct ifnet *ifp)
{
        struct age_softc *sc = ifp->if_softc;
        struct mbuf *m;
	int enq;

	if (!(ifp->if_flags & IFF_RUNNING) || ifq_is_oactive(&ifp->if_snd))
		return;
	if ((sc->age_flags & AGE_FLAG_LINK) == 0)
		return;
	if (ifq_empty(&ifp->if_snd))
		return;

	enq = 0;
	for (;;) {
		if (sc->age_cdata.age_tx_cnt + AGE_MAXTXSEGS >=
		    AGE_TX_RING_CNT - 2) {
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
		if (age_encap(sc, m) != 0) {
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
		/* Update mbox. */
		AGE_COMMIT_MBOX(sc);
		/* Set a timeout in case the chip goes out to lunch. */
		ifp->if_timer = AGE_TX_TIMEOUT;
	}
}

void
age_watchdog(struct ifnet *ifp)
{
	struct age_softc *sc = ifp->if_softc;

	if ((sc->age_flags & AGE_FLAG_LINK) == 0) {
		printf("%s: watchdog timeout (missed link)\n",
		    sc->sc_dev.dv_xname);
		ifp->if_oerrors++;
		age_init(ifp);
		return;
	}

	if (sc->age_cdata.age_tx_cnt == 0) {
		printf("%s: watchdog timeout (missed Tx interrupts) "
		    "-- recovering\n", sc->sc_dev.dv_xname);
		age_start(ifp);
		return;
	}

	printf("%s: watchdog timeout\n", sc->sc_dev.dv_xname);
	ifp->if_oerrors++;
	age_init(ifp);
	age_start(ifp);
}

int
age_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct age_softc *sc = ifp->if_softc;
	struct mii_data *mii = &sc->sc_miibus;
	struct ifreq *ifr = (struct ifreq *)data;
	int s, error = 0;

	s = splnet();

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		if (!(ifp->if_flags & IFF_RUNNING))
			 age_init(ifp);
		break;

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				error = ENETRESET;
			else
				age_init(ifp);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				age_stop(sc);
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
			age_iff(sc);
		error = 0;
	}

	splx(s);
	return (error);
}

void
age_mac_config(struct age_softc *sc)
{
	struct mii_data *mii = &sc->sc_miibus;
	uint32_t reg;

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
		if ((IFM_OPTIONS(mii->mii_media_active) & IFM_ETH_TXPAUSE) != 0)
			reg |= MAC_CFG_TX_FC;
		if ((IFM_OPTIONS(mii->mii_media_active) & IFM_ETH_RXPAUSE) != 0)
			reg |= MAC_CFG_RX_FC;
	}

	CSR_WRITE_4(sc, AGE_MAC_CFG, reg);
}

int
age_encap(struct age_softc *sc, struct mbuf *m)
{
	struct age_txdesc *txd, *txd_last;
	struct tx_desc *desc;
	bus_dmamap_t map;
	uint32_t cflags, poff, vtag;
	int error, i, prod;

	cflags = vtag = 0;
	poff = 0;

	prod = sc->age_cdata.age_tx_prod;
	txd = &sc->age_cdata.age_txdesc[prod];
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

	/* Configure Tx IP/TCP/UDP checksum offload. */
	if ((m->m_pkthdr.csum_flags & AGE_CSUM_FEATURES) != 0) {
		cflags |= AGE_TD_CSUM;
		if ((m->m_pkthdr.csum_flags & M_TCP_CSUM_OUT) != 0)
			cflags |= AGE_TD_TCPCSUM;
		if ((m->m_pkthdr.csum_flags & M_UDP_CSUM_OUT) != 0)
			cflags |= AGE_TD_UDPCSUM;
		/* Set checksum start offset. */
		cflags |= (poff << AGE_TD_CSUM_PLOADOFFSET_SHIFT);
	}

#if NVLAN > 0
	/* Configure VLAN hardware tag insertion. */
	if (m->m_flags & M_VLANTAG) {
		vtag = AGE_TX_VLAN_TAG(m->m_pkthdr.ether_vtag);
		vtag = ((vtag << AGE_TD_VLAN_SHIFT) & AGE_TD_VLAN_MASK);
		cflags |= AGE_TD_INSERT_VLAN_TAG;
	}
#endif

	desc = NULL;
	for (i = 0; i < map->dm_nsegs; i++) {
		desc = &sc->age_rdata.age_tx_ring[prod];
		desc->addr = htole64(map->dm_segs[i].ds_addr);
		desc->len = 
		    htole32(AGE_TX_BYTES(map->dm_segs[i].ds_len) | vtag);
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

	/* Swap dmamap of the first and the last. */
	txd = &sc->age_cdata.age_txdesc[prod];
	map = txd_last->tx_dmamap;
	txd_last->tx_dmamap = txd->tx_dmamap;
	txd->tx_dmamap = map;
	txd->tx_m = m;

	/* Sync descriptors. */
	bus_dmamap_sync(sc->sc_dmat, map, 0, map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->sc_dmat, sc->age_cdata.age_tx_ring_map, 0,
	    sc->age_cdata.age_tx_ring_map->dm_mapsize,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	return (0);

 drop:
	m_freem(m);
	return (error);
}

void
age_txintr(struct age_softc *sc, int tpd_cons)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct age_txdesc *txd;
	int cons, prog;

	bus_dmamap_sync(sc->sc_dmat, sc->age_cdata.age_tx_ring_map, 0,
	    sc->age_cdata.age_tx_ring_map->dm_mapsize,
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
		ifq_clr_oactive(&ifp->if_snd);
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
		bus_dmamap_sync(sc->sc_dmat, txd->tx_dmamap, 0,
		    txd->tx_dmamap->dm_mapsize, BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, txd->tx_dmamap);
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
			ifp->if_timer = 0;

		bus_dmamap_sync(sc->sc_dmat, sc->age_cdata.age_tx_ring_map, 0,
		    sc->age_cdata.age_tx_ring_map->dm_mapsize,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	}
}

/* Receive a frame. */
void
age_rxeof(struct age_softc *sc, struct rx_rdesc *rxrd)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct age_rxdesc *rxd;
	struct rx_desc *desc;
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct mbuf *mp, *m;
	uint32_t status, index;
	int count, nsegs, pktlen;
	int rx_cons;

	status = letoh32(rxrd->flags);
	index = letoh32(rxrd->index);
	rx_cons = AGE_RX_CONS(index);
	nsegs = AGE_RX_NSEGS(index);

	sc->age_cdata.age_rxlen = AGE_RX_BYTES(letoh32(rxrd->len));
	if ((status & AGE_RRD_ERROR) != 0 &&
	    (status & (AGE_RRD_CRC | AGE_RRD_CODE | AGE_RRD_DRIBBLE |
	    AGE_RRD_RUNT | AGE_RRD_OFLOW | AGE_RRD_TRUNC)) != 0) {
		/*
		 * We want to pass the following frames to upper
		 * layer regardless of error status of Rx return
		 * ring.
		 *
		 *  o IP/TCP/UDP checksum is bad.
		 *  o frame length and protocol specific length
		 *     does not match.
		 */
		sc->age_cdata.age_rx_cons += nsegs;
		sc->age_cdata.age_rx_cons %= AGE_RX_RING_CNT;
		return;
	}

	pktlen = 0;
	for (count = 0; count < nsegs; count++,
	    AGE_DESC_INC(rx_cons, AGE_RX_RING_CNT)) {
		rxd = &sc->age_cdata.age_rxdesc[rx_cons];
		mp = rxd->rx_m;
		desc = rxd->rx_desc;
		/* Add a new receive buffer to the ring. */
		if (age_newbuf(sc, rxd) != 0) {
			ifp->if_iqdrops++;
			/* Reuse Rx buffers. */
			if (sc->age_cdata.age_rxhead != NULL) {
				m_freem(sc->age_cdata.age_rxhead);
				AGE_RXCHAIN_RESET(sc);
			}
			break;
		}

		/* The length of the first mbuf is computed last. */
		if (count != 0) {
			mp->m_len = AGE_RX_BYTES(letoh32(desc->len));
			pktlen += mp->m_len;
		}

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
			/*
			 * It seems that L1 controller has no way
			 * to tell hardware to strip CRC bytes.
			 */
			sc->age_cdata.age_rxlen -= ETHER_CRC_LEN;
			if (nsegs > 1) {
				/* Remove the CRC bytes in chained mbufs. */
				pktlen -= ETHER_CRC_LEN;
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
			}

			m = sc->age_cdata.age_rxhead;
			m->m_flags |= M_PKTHDR;
			m->m_pkthdr.len = sc->age_cdata.age_rxlen;
			/* Set the first mbuf length. */
			m->m_len = sc->age_cdata.age_rxlen - pktlen;

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
			if (status & AGE_RRD_IPV4) {
				if ((status & AGE_RRD_IPCSUM_NOK) == 0)
					m->m_pkthdr.csum_flags |= 
					    M_IPV4_CSUM_IN_OK;
				if ((status & (AGE_RRD_TCP | AGE_RRD_UDP)) &&
				    (status & AGE_RRD_TCP_UDPCSUM_NOK) == 0) {
					m->m_pkthdr.csum_flags |=
					    M_TCP_CSUM_IN_OK | M_UDP_CSUM_IN_OK;
				}
				/*
				 * Don't mark bad checksum for TCP/UDP frames
				 * as fragmented frames may always have set
				 * bad checksummed bit of descriptor status.
				 */
			}
#if NVLAN > 0
			/* Check for VLAN tagged frames. */
			if (status & AGE_RRD_VLAN) {
				u_int32_t vtag = AGE_RX_VLAN(letoh32(rxrd->vtags));
				m->m_pkthdr.ether_vtag =
				    AGE_RX_VLAN_TAG(vtag);
				m->m_flags |= M_VLANTAG;
			}
#endif

			ml_enqueue(&ml, m);

			/* Reset mbuf chains. */
			AGE_RXCHAIN_RESET(sc);
		}
	}

	if_input(ifp, &ml);

	if (count != nsegs) {
		sc->age_cdata.age_rx_cons += nsegs;
		sc->age_cdata.age_rx_cons %= AGE_RX_RING_CNT;
	} else
		sc->age_cdata.age_rx_cons = rx_cons;
}

void
age_rxintr(struct age_softc *sc, int rr_prod)
{
	struct rx_rdesc *rxrd;
	int rr_cons, nsegs, pktlen, prog;

	rr_cons = sc->age_cdata.age_rr_cons;
	if (rr_cons == rr_prod)
		return;

	bus_dmamap_sync(sc->sc_dmat, sc->age_cdata.age_rr_ring_map, 0,
	    sc->age_cdata.age_rr_ring_map->dm_mapsize, 
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	bus_dmamap_sync(sc->sc_dmat, sc->age_cdata.age_rx_ring_map, 0,
	    sc->age_cdata.age_rx_ring_map->dm_mapsize,
	    BUS_DMASYNC_POSTWRITE);

	for (prog = 0; rr_cons != rr_prod; prog++) {
		rxrd = &sc->age_rdata.age_rr_ring[rr_cons];
		nsegs = AGE_RX_NSEGS(letoh32(rxrd->index));
		if (nsegs == 0)
			break;
		/*
		 * Check number of segments against received bytes
		 * Non-matching value would indicate that hardware
		 * is still trying to update Rx return descriptors.
		 * I'm not sure whether this check is really needed.
		 */
		pktlen = AGE_RX_BYTES(letoh32(rxrd->len));
		if (nsegs != ((pktlen + (MCLBYTES - ETHER_ALIGN - 1)) /
		    (MCLBYTES - ETHER_ALIGN)))
			break;

		/* Received a frame. */
		age_rxeof(sc, rxrd);

		/* Clear return ring. */
		rxrd->index = 0;
		AGE_DESC_INC(rr_cons, AGE_RR_RING_CNT);
	}

	if (prog > 0) {
		/* Update the consumer index. */
		sc->age_cdata.age_rr_cons = rr_cons;

		bus_dmamap_sync(sc->sc_dmat, sc->age_cdata.age_rx_ring_map, 0,
		    sc->age_cdata.age_rx_ring_map->dm_mapsize,
		    BUS_DMASYNC_PREWRITE);
		/* Sync descriptors. */
		bus_dmamap_sync(sc->sc_dmat, sc->age_cdata.age_rr_ring_map, 0,
		    sc->age_cdata.age_rr_ring_map->dm_mapsize,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

		/* Notify hardware availability of new Rx buffers. */
		AGE_COMMIT_MBOX(sc);
	}
}

void
age_tick(void *xsc)
{
	struct age_softc *sc = xsc;
	struct mii_data *mii = &sc->sc_miibus;
	int s;

	s = splnet();
	mii_tick(mii);
	timeout_add_sec(&sc->age_tick_ch, 1);
	splx(s);
}

void
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
		printf("%s: reset timeout(0x%08x)!\n", sc->sc_dev.dv_xname,
		    reg);

	/* Initialize PCIe module. From Linux. */
	CSR_WRITE_4(sc, 0x12FC, 0x6500);
	CSR_WRITE_4(sc, 0x1008, CSR_READ_4(sc, 0x1008) | 0x8000);
}

int
age_init(struct ifnet *ifp)
{
	struct age_softc *sc = ifp->if_softc;
	struct mii_data *mii = &sc->sc_miibus;
	uint8_t eaddr[ETHER_ADDR_LEN];
	bus_addr_t paddr;
	uint32_t reg, fsize;
	uint32_t rxf_hi, rxf_lo, rrd_hi, rrd_lo;
	int error;

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
		printf("%s: no memory for Rx buffers.\n", sc->sc_dev.dv_xname);
                age_stop(sc);
		return (error);
        }
	age_init_rr_ring(sc);
	age_init_tx_ring(sc);
	age_init_cmb_block(sc);
	age_init_smb_block(sc);

	/* Reprogram the station address. */
	bcopy(LLADDR(ifp->if_sadl), eaddr, ETHER_ADDR_LEN);
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
	 * independent Tx/Rx handler which in turn Rx handler could have
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
	sc->age_int_mod = AGE_IM_TIMER_DEFAULT;
	CSR_WRITE_2(sc, AGE_IM_TIMER, AGE_USECS(sc->age_int_mod));
	reg = CSR_READ_4(sc, AGE_MASTER_CFG);
	reg &= ~MASTER_MTIMER_ENB;
	if (AGE_USECS(sc->age_int_mod) == 0)
		reg &= ~MASTER_ITIMER_ENB;
	else
		reg |= MASTER_ITIMER_ENB;
	CSR_WRITE_4(sc, AGE_MASTER_CFG, reg);
	if (agedebug)
		printf("%s: interrupt moderation is %d us.\n", 
		    sc->sc_dev.dv_xname, sc->age_int_mod);
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
	age_iff(sc);

	age_rxvlan(sc);

	reg = CSR_READ_4(sc, AGE_MAC_CFG);
	reg |= MAC_CFG_RXCSUM_ENB;

	/* Ack all pending interrupts and clear it. */
	CSR_WRITE_4(sc, AGE_INTR_STATUS, 0);
	CSR_WRITE_4(sc, AGE_INTR_MASK, AGE_INTRS);

	/* Finally enable Tx/Rx MAC. */
	CSR_WRITE_4(sc, AGE_MAC_CFG, reg | MAC_CFG_TX_ENB | MAC_CFG_RX_ENB);

	sc->age_flags &= ~AGE_FLAG_LINK;

	/* Switch to the current media. */
	mii_mediachg(mii);

	timeout_add_sec(&sc->age_tick_ch, 1);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	return (0);
}

void
age_stop(struct age_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct age_txdesc *txd;
	struct age_rxdesc *rxd;
	uint32_t reg;
	int i;

	/*
	 * Mark the interface down and cancel the watchdog timer.
	 */
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
	ifp->if_timer = 0;

	sc->age_flags &= ~AGE_FLAG_LINK;
	timeout_del(&sc->age_tick_ch);

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
		printf("%s: stopping Rx/Tx MACs timed out(0x%08x)!\n",
		    sc->sc_dev.dv_xname, reg);

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
			bus_dmamap_sync(sc->sc_dmat, rxd->rx_dmamap, 0,
			    rxd->rx_dmamap->dm_mapsize, BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(sc->sc_dmat, rxd->rx_dmamap);
			m_freem(rxd->rx_m);
			rxd->rx_m = NULL;
		}
	}
	for (i = 0; i < AGE_TX_RING_CNT; i++) {
		txd = &sc->age_cdata.age_txdesc[i];
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
age_stats_update(struct age_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct age_stats *stat;
	struct smb *smb;

	stat = &sc->age_stat;

	bus_dmamap_sync(sc->sc_dmat, sc->age_cdata.age_smb_block_map, 0,
	    sc->age_cdata.age_smb_block_map->dm_mapsize, BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	smb = sc->age_rdata.age_smb_block;
	if (smb->updated == 0)
		return;

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

	ifp->if_collisions += smb->tx_single_colls +
	    smb->tx_multi_colls + smb->tx_late_colls +
	    smb->tx_excess_colls * HDPX_CFG_RETRY_DEFAULT;

	ifp->if_oerrors += smb->tx_excess_colls +
	    smb->tx_late_colls + smb->tx_underrun +
	    smb->tx_pkts_truncated;

	ifp->if_ierrors += smb->rx_crcerrs + smb->rx_lenerrs +
	    smb->rx_runts + smb->rx_pkts_truncated +
	    smb->rx_fifo_oflows + smb->rx_desc_oflows +
	    smb->rx_alignerrs;

	/* Update done, clear. */
	smb->updated = 0;

	bus_dmamap_sync(sc->sc_dmat, sc->age_cdata.age_smb_block_map, 0,
	    sc->age_cdata.age_smb_block_map->dm_mapsize,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}

void
age_stop_txmac(struct age_softc *sc)
{
	uint32_t reg;
	int i;

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
		printf("%s: stopping TxMAC timeout!\n", sc->sc_dev.dv_xname);
}

void
age_stop_rxmac(struct age_softc *sc)
{
	uint32_t reg;
	int i;

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
		printf("%s: stopping RxMAC timeout!\n", sc->sc_dev.dv_xname);
}

void
age_init_tx_ring(struct age_softc *sc)
{
	struct age_ring_data *rd;
	struct age_txdesc *txd;
	int i;

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

	bus_dmamap_sync(sc->sc_dmat, sc->age_cdata.age_tx_ring_map, 0,
	    sc->age_cdata.age_tx_ring_map->dm_mapsize,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}

int
age_init_rx_ring(struct age_softc *sc)
{
	struct age_ring_data *rd;
	struct age_rxdesc *rxd;
	int i;

	sc->age_cdata.age_rx_cons = AGE_RX_RING_CNT - 1;
	rd = &sc->age_rdata;
	bzero(rd->age_rx_ring, AGE_RX_RING_SZ);
	for (i = 0; i < AGE_RX_RING_CNT; i++) {
		rxd = &sc->age_cdata.age_rxdesc[i];
		rxd->rx_m = NULL;
		rxd->rx_desc = &rd->age_rx_ring[i];
		if (age_newbuf(sc, rxd) != 0)
			return (ENOBUFS);
	}

	bus_dmamap_sync(sc->sc_dmat, sc->age_cdata.age_rx_ring_map, 0,
	    sc->age_cdata.age_rx_ring_map->dm_mapsize, BUS_DMASYNC_PREWRITE);

	return (0);
}

void
age_init_rr_ring(struct age_softc *sc)
{
	struct age_ring_data *rd;

	sc->age_cdata.age_rr_cons = 0;
	AGE_RXCHAIN_RESET(sc);

	rd = &sc->age_rdata;
	bzero(rd->age_rr_ring, AGE_RR_RING_SZ);
	bus_dmamap_sync(sc->sc_dmat, sc->age_cdata.age_rr_ring_map, 0,
	    sc->age_cdata.age_rr_ring_map->dm_mapsize,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}

void
age_init_cmb_block(struct age_softc *sc)
{
	struct age_ring_data *rd;

	rd = &sc->age_rdata;
	bzero(rd->age_cmb_block, AGE_CMB_BLOCK_SZ);
	bus_dmamap_sync(sc->sc_dmat, sc->age_cdata.age_cmb_block_map, 0,
	    sc->age_cdata.age_cmb_block_map->dm_mapsize,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}

void
age_init_smb_block(struct age_softc *sc)
{
	struct age_ring_data *rd;

	rd = &sc->age_rdata;
	bzero(rd->age_smb_block, AGE_SMB_BLOCK_SZ);
	bus_dmamap_sync(sc->sc_dmat, sc->age_cdata.age_smb_block_map, 0,
	    sc->age_cdata.age_smb_block_map->dm_mapsize,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}

int
age_newbuf(struct age_softc *sc, struct age_rxdesc *rxd)
{
	struct rx_desc *desc;
	struct mbuf *m;
	bus_dmamap_t map;
	int error;

	MGETHDR(m, M_DONTWAIT, MT_DATA);
	if (m == NULL)
		return (ENOBUFS);
	MCLGET(m, M_DONTWAIT);
	if (!(m->m_flags & M_EXT)) {
		 m_freem(m);
		 return (ENOBUFS);
	}

	m->m_len = m->m_pkthdr.len = MCLBYTES;
	m_adj(m, ETHER_ALIGN);

	error = bus_dmamap_load_mbuf(sc->sc_dmat,
	    sc->age_cdata.age_rx_sparemap, m, BUS_DMA_NOWAIT);

	if (error != 0) {
		m_freem(m);
		printf("%s: can't load RX mbuf\n", sc->sc_dev.dv_xname);
		return (error);
	}

	if (rxd->rx_m != NULL) {
		bus_dmamap_sync(sc->sc_dmat, rxd->rx_dmamap, 0,
		    rxd->rx_dmamap->dm_mapsize, BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->sc_dmat, rxd->rx_dmamap);
	}
	map = rxd->rx_dmamap;
	rxd->rx_dmamap = sc->age_cdata.age_rx_sparemap;
	sc->age_cdata.age_rx_sparemap = map;
	bus_dmamap_sync(sc->sc_dmat, rxd->rx_dmamap, 0,
	    rxd->rx_dmamap->dm_mapsize, BUS_DMASYNC_PREREAD);
	rxd->rx_m = m;

	desc = rxd->rx_desc;
	desc->addr = htole64(rxd->rx_dmamap->dm_segs[0].ds_addr);
	desc->len = 
	    htole32((rxd->rx_dmamap->dm_segs[0].ds_len & AGE_RD_LEN_MASK) <<
	    AGE_RD_LEN_SHIFT);

	return (0);
}

void
age_rxvlan(struct age_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	uint32_t reg;

	reg = CSR_READ_4(sc, AGE_MAC_CFG);
	reg &= ~MAC_CFG_VLAN_TAG_STRIP;
	if (ifp->if_capabilities & IFCAP_VLAN_HWTAGGING)
		reg |= MAC_CFG_VLAN_TAG_STRIP;
	CSR_WRITE_4(sc, AGE_MAC_CFG, reg);
}

void
age_iff(struct age_softc *sc)
{
	struct arpcom *ac = &sc->sc_arpcom;
	struct ifnet *ifp = &ac->ac_if;
	struct ether_multi *enm;
	struct ether_multistep step;
	uint32_t crc;
	uint32_t mchash[2];
	uint32_t rxcfg;

	rxcfg = CSR_READ_4(sc, AGE_MAC_CFG);
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
			crc = ether_crc32_be(enm->enm_addrlo, 
			    ETHER_ADDR_LEN);

			mchash[crc >> 31] |= 1 << ((crc >> 26) & 0x1f);

			ETHER_NEXT_MULTI(step, enm);
		}
	}

	CSR_WRITE_4(sc, AGE_MAR0, mchash[0]);
	CSR_WRITE_4(sc, AGE_MAR1, mchash[1]);
	CSR_WRITE_4(sc, AGE_MAC_CFG, rxcfg);
}
