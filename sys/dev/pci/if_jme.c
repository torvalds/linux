/*	$OpenBSD: if_jme.c,v 1.58 2024/05/24 06:02:53 jsg Exp $	*/
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
 * $FreeBSD: src/sys/dev/jme/if_jme.c,v 1.2 2008/07/18 04:20:48 yongari Exp $
 * $DragonFly: src/sys/dev/netif/jme/if_jme.c,v 1.7 2008/09/13 04:04:39 sephe Exp $
 */

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

#include <dev/mii/miivar.h>
#include <dev/mii/jmphyreg.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/pci/if_jmereg.h>
#include <dev/pci/if_jmevar.h>

/* Define the following to disable printing Rx errors. */
#undef	JME_SHOW_ERRORS

int	jme_match(struct device *, void *, void *);
void	jme_map_intr_vector(struct jme_softc *);
void	jme_attach(struct device *, struct device *, void *);
int	jme_detach(struct device *, int);

int	jme_miibus_readreg(struct device *, int, int);
void	jme_miibus_writereg(struct device *, int, int, int);
void	jme_miibus_statchg(struct device *);

int	jme_init(struct ifnet *);
int	jme_ioctl(struct ifnet *, u_long, caddr_t);

void	jme_start(struct ifnet *);
void	jme_watchdog(struct ifnet *);
void	jme_mediastatus(struct ifnet *, struct ifmediareq *);
int	jme_mediachange(struct ifnet *);

int	jme_intr(void *);
void	jme_txeof(struct jme_softc *);
void	jme_rxeof(struct jme_softc *);

int	jme_dma_alloc(struct jme_softc *);
void	jme_dma_free(struct jme_softc *);
int	jme_init_rx_ring(struct jme_softc *);
void	jme_init_tx_ring(struct jme_softc *);
void	jme_init_ssb(struct jme_softc *);
int	jme_newbuf(struct jme_softc *, struct jme_rxdesc *);
int	jme_encap(struct jme_softc *, struct mbuf *);
void	jme_rxpkt(struct jme_softc *);

void	jme_tick(void *);
void	jme_stop(struct jme_softc *);
void	jme_reset(struct jme_softc *);
void	jme_set_vlan(struct jme_softc *);
void	jme_iff(struct jme_softc *);
void	jme_stop_tx(struct jme_softc *);
void	jme_stop_rx(struct jme_softc *);
void	jme_mac_config(struct jme_softc *);
void	jme_reg_macaddr(struct jme_softc *, uint8_t[]);
int	jme_eeprom_macaddr(struct jme_softc *, uint8_t[]);
int	jme_eeprom_read_byte(struct jme_softc *, uint8_t, uint8_t *);
void	jme_discard_rxbufs(struct jme_softc *, int, int);
#ifdef notyet
void	jme_setwol(struct jme_softc *);
void	jme_setlinkspeed(struct jme_softc *);
#endif

/*
 * Devices supported by this driver.
 */
const struct pci_matchid jme_devices[] = {
	{ PCI_VENDOR_JMICRON, PCI_PRODUCT_JMICRON_JMC250 },
	{ PCI_VENDOR_JMICRON, PCI_PRODUCT_JMICRON_JMC260 }
};

const struct cfattach jme_ca = {
	sizeof (struct jme_softc), jme_match, jme_attach
};

struct cfdriver jme_cd = {
	NULL, "jme", DV_IFNET
};

int jmedebug = 0;
#define DPRINTF(x)	do { if (jmedebug) printf x; } while (0)

/*
 *	Read a PHY register on the MII of the JMC250.
 */
int
jme_miibus_readreg(struct device *dev, int phy, int reg)
{
	struct jme_softc *sc = (struct jme_softc *)dev;
	uint32_t val;
	int i;

	/* For FPGA version, PHY address 0 should be ignored. */
	if ((sc->jme_caps & JME_CAP_FPGA) && phy == 0)
		return (0);

	CSR_WRITE_4(sc, JME_SMI, SMI_OP_READ | SMI_OP_EXECUTE |
	    SMI_PHY_ADDR(phy) | SMI_REG_ADDR(reg));

	for (i = JME_PHY_TIMEOUT; i > 0; i--) {
		DELAY(1);
		if (((val = CSR_READ_4(sc, JME_SMI)) & SMI_OP_EXECUTE) == 0)
			break;
	}
	if (i == 0) {
		printf("%s: phy read timeout: phy %d, reg %d\n",
		    sc->sc_dev.dv_xname, phy, reg);
		return (0);
	}

	return ((val & SMI_DATA_MASK) >> SMI_DATA_SHIFT);
}

/*
 *	Write a PHY register on the MII of the JMC250.
 */
void
jme_miibus_writereg(struct device *dev, int phy, int reg, int val)
{
	struct jme_softc *sc = (struct jme_softc *)dev;
	int i;

	/* For FPGA version, PHY address 0 should be ignored. */
	if ((sc->jme_caps & JME_CAP_FPGA) && phy == 0)
		return;

	CSR_WRITE_4(sc, JME_SMI, SMI_OP_WRITE | SMI_OP_EXECUTE |
	    ((val << SMI_DATA_SHIFT) & SMI_DATA_MASK) |
	    SMI_PHY_ADDR(phy) | SMI_REG_ADDR(reg));

	for (i = JME_PHY_TIMEOUT; i > 0; i--) {
		DELAY(1);
		if (((val = CSR_READ_4(sc, JME_SMI)) & SMI_OP_EXECUTE) == 0)
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
jme_miibus_statchg(struct device *dev)
{
	struct jme_softc *sc = (struct jme_softc *)dev;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct mii_data *mii;
	struct jme_txdesc *txd;
	bus_addr_t paddr;
	int i;

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		return;

	mii = &sc->sc_miibus;

	sc->jme_flags &= ~JME_FLAG_LINK;
	if ((mii->mii_media_status & IFM_AVALID) != 0) {
		switch (IFM_SUBTYPE(mii->mii_media_active)) {
		case IFM_10_T:
		case IFM_100_TX:
			sc->jme_flags |= JME_FLAG_LINK;
			break;
		case IFM_1000_T:
			if (sc->jme_caps & JME_CAP_FASTETH)
				break;
			sc->jme_flags |= JME_FLAG_LINK;
			break;
		default:
			break;
		}
	}

	/*
	 * Disabling Rx/Tx MACs have a side-effect of resetting
	 * JME_TXNDA/JME_RXNDA register to the first address of
	 * Tx/Rx descriptor address. So driver should reset its
	 * internal producer/consumer pointer and reclaim any
	 * allocated resources.  Note, just saving the value of
	 * JME_TXNDA and JME_RXNDA registers before stopping MAC
	 * and restoring JME_TXNDA/JME_RXNDA register is not
	 * sufficient to make sure correct MAC state because
	 * stopping MAC operation can take a while and hardware
	 * might have updated JME_TXNDA/JME_RXNDA registers
	 * during the stop operation.
	 */

	/* Disable interrupts */
	CSR_WRITE_4(sc, JME_INTR_MASK_CLR, JME_INTRS);

	/* Stop driver */
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
	ifp->if_timer = 0;
	timeout_del(&sc->jme_tick_ch);

	/* Stop receiver/transmitter. */
	jme_stop_rx(sc);
	jme_stop_tx(sc);

	jme_rxeof(sc);
	m_freem(sc->jme_cdata.jme_rxhead);
	JME_RXCHAIN_RESET(sc);

	jme_txeof(sc);
	if (sc->jme_cdata.jme_tx_cnt != 0) {
		/* Remove queued packets for transmit. */
		for (i = 0; i < JME_TX_RING_CNT; i++) {
			txd = &sc->jme_cdata.jme_txdesc[i];
			if (txd->tx_m != NULL) {
				bus_dmamap_unload(sc->sc_dmat, txd->tx_dmamap);
				m_freem(txd->tx_m);
				txd->tx_m = NULL;
				txd->tx_ndesc = 0;
				ifp->if_oerrors++;
			}
		}
	}

	/*
	 * Reuse configured Rx descriptors and reset
	 * producer/consumer index.
	 */
	sc->jme_cdata.jme_rx_cons = 0;

	jme_init_tx_ring(sc);

	/* Initialize shadow status block. */
	jme_init_ssb(sc);

	/* Program MAC with resolved speed/duplex/flow-control. */
	if (sc->jme_flags & JME_FLAG_LINK) {
		jme_mac_config(sc);

		CSR_WRITE_4(sc, JME_RXCSR, sc->jme_rxcsr);
		CSR_WRITE_4(sc, JME_TXCSR, sc->jme_txcsr);

		/* Set Tx ring address to the hardware. */
		paddr = JME_TX_RING_ADDR(sc, 0);
		CSR_WRITE_4(sc, JME_TXDBA_HI, JME_ADDR_HI(paddr));
		CSR_WRITE_4(sc, JME_TXDBA_LO, JME_ADDR_LO(paddr));

		/* Set Rx ring address to the hardware. */
		paddr = JME_RX_RING_ADDR(sc, 0);
		CSR_WRITE_4(sc, JME_RXDBA_HI, JME_ADDR_HI(paddr));
		CSR_WRITE_4(sc, JME_RXDBA_LO, JME_ADDR_LO(paddr));

		/* Restart receiver/transmitter. */
		CSR_WRITE_4(sc, JME_RXCSR, sc->jme_rxcsr | RXCSR_RX_ENB |
		    RXCSR_RXQ_START);
		CSR_WRITE_4(sc, JME_TXCSR, sc->jme_txcsr | TXCSR_TX_ENB);
	}

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
	timeout_add_sec(&sc->jme_tick_ch, 1);

	/* Reenable interrupts. */
	CSR_WRITE_4(sc, JME_INTR_MASK_SET, JME_INTRS);
}

/*
 *	Get the current interface media status.
 */
void
jme_mediastatus(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct jme_softc *sc = ifp->if_softc;
	struct mii_data *mii = &sc->sc_miibus;

	mii_pollstat(mii);
	ifmr->ifm_status = mii->mii_media_status;
	ifmr->ifm_active = mii->mii_media_active;
}

/*
 *	Set hardware to newly-selected media.
 */
int
jme_mediachange(struct ifnet *ifp)
{
	struct jme_softc *sc = ifp->if_softc;
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
jme_match(struct device *dev, void *match, void *aux)
{
	return pci_matchbyid((struct pci_attach_args *)aux, jme_devices,
	    sizeof (jme_devices) / sizeof (jme_devices[0]));
}

int
jme_eeprom_read_byte(struct jme_softc *sc, uint8_t addr, uint8_t *val)
{
	uint32_t reg;
	int i;

	*val = 0;
	for (i = JME_TIMEOUT; i > 0; i--) {
		reg = CSR_READ_4(sc, JME_SMBCSR);
		if ((reg & SMBCSR_HW_BUSY_MASK) == SMBCSR_HW_IDLE)
			break;
		DELAY(1);
	}

	if (i == 0) {
		printf("%s: EEPROM idle timeout!\n", sc->sc_dev.dv_xname);
		return (ETIMEDOUT);
	}

	reg = ((uint32_t)addr << SMBINTF_ADDR_SHIFT) & SMBINTF_ADDR_MASK;
	CSR_WRITE_4(sc, JME_SMBINTF, reg | SMBINTF_RD | SMBINTF_CMD_TRIGGER);
	for (i = JME_TIMEOUT; i > 0; i--) {
		DELAY(1);
		reg = CSR_READ_4(sc, JME_SMBINTF);
		if ((reg & SMBINTF_CMD_TRIGGER) == 0)
			break;
	}

	if (i == 0) {
		printf("%s: EEPROM read timeout!\n", sc->sc_dev.dv_xname);
		return (ETIMEDOUT);
	}

	reg = CSR_READ_4(sc, JME_SMBINTF);
	*val = (reg & SMBINTF_RD_DATA_MASK) >> SMBINTF_RD_DATA_SHIFT;

	return (0);
}

int
jme_eeprom_macaddr(struct jme_softc *sc, uint8_t eaddr[])
{
	uint8_t fup, reg, val;
	uint32_t offset;
	int match;

	offset = 0;
	if (jme_eeprom_read_byte(sc, offset++, &fup) != 0 ||
	    fup != JME_EEPROM_SIG0)
		return (ENOENT);
	if (jme_eeprom_read_byte(sc, offset++, &fup) != 0 ||
	    fup != JME_EEPROM_SIG1)
		return (ENOENT);
	match = 0;
	do {
		if (jme_eeprom_read_byte(sc, offset, &fup) != 0)
			break;
		if (JME_EEPROM_MKDESC(JME_EEPROM_FUNC0, JME_EEPROM_PAGE_BAR1) ==
		    (fup & (JME_EEPROM_FUNC_MASK | JME_EEPROM_PAGE_MASK))) {
			if (jme_eeprom_read_byte(sc, offset + 1, &reg) != 0)
				break;
			if (reg >= JME_PAR0 &&
			    reg < JME_PAR0 + ETHER_ADDR_LEN) {
				if (jme_eeprom_read_byte(sc, offset + 2,
				    &val) != 0)
					break;
				eaddr[reg - JME_PAR0] = val;
				match++;
			}
		}
		/* Check for the end of EEPROM descriptor. */
		if ((fup & JME_EEPROM_DESC_END) == JME_EEPROM_DESC_END)
			break;
		/* Try next eeprom descriptor. */
		offset += JME_EEPROM_DESC_BYTES;
	} while (match != ETHER_ADDR_LEN && offset < JME_EEPROM_END);

	if (match == ETHER_ADDR_LEN)
		return (0);

	return (ENOENT);
}

void
jme_reg_macaddr(struct jme_softc *sc, uint8_t eaddr[])
{
	uint32_t par0, par1;

	/* Read station address. */
	par0 = CSR_READ_4(sc, JME_PAR0);
	par1 = CSR_READ_4(sc, JME_PAR1);
	par1 &= 0xFFFF;

	eaddr[0] = (par0 >> 0) & 0xFF;
	eaddr[1] = (par0 >> 8) & 0xFF;
	eaddr[2] = (par0 >> 16) & 0xFF;
	eaddr[3] = (par0 >> 24) & 0xFF;
	eaddr[4] = (par1 >> 0) & 0xFF;
	eaddr[5] = (par1 >> 8) & 0xFF;
}

void
jme_map_intr_vector(struct jme_softc *sc)
{
	uint32_t map[MSINUM_NUM_INTR_SOURCE / JME_MSI_MESSAGES];

	bzero(map, sizeof(map));

	/* Map Tx interrupts source to MSI/MSIX vector 2. */
	map[MSINUM_REG_INDEX(N_INTR_TXQ0_COMP)] =
	    MSINUM_INTR_SOURCE(2, N_INTR_TXQ0_COMP);
	map[MSINUM_REG_INDEX(N_INTR_TXQ1_COMP)] |=
	    MSINUM_INTR_SOURCE(2, N_INTR_TXQ1_COMP);
	map[MSINUM_REG_INDEX(N_INTR_TXQ2_COMP)] |=
	    MSINUM_INTR_SOURCE(2, N_INTR_TXQ2_COMP);
	map[MSINUM_REG_INDEX(N_INTR_TXQ3_COMP)] |=
	    MSINUM_INTR_SOURCE(2, N_INTR_TXQ3_COMP);
	map[MSINUM_REG_INDEX(N_INTR_TXQ4_COMP)] |=
	    MSINUM_INTR_SOURCE(2, N_INTR_TXQ4_COMP);
	map[MSINUM_REG_INDEX(N_INTR_TXQ4_COMP)] |=
	    MSINUM_INTR_SOURCE(2, N_INTR_TXQ5_COMP);
	map[MSINUM_REG_INDEX(N_INTR_TXQ6_COMP)] |=
	    MSINUM_INTR_SOURCE(2, N_INTR_TXQ6_COMP);
	map[MSINUM_REG_INDEX(N_INTR_TXQ7_COMP)] |=
	    MSINUM_INTR_SOURCE(2, N_INTR_TXQ7_COMP);
	map[MSINUM_REG_INDEX(N_INTR_TXQ_COAL)] |=
	    MSINUM_INTR_SOURCE(2, N_INTR_TXQ_COAL);
	map[MSINUM_REG_INDEX(N_INTR_TXQ_COAL_TO)] |=
	    MSINUM_INTR_SOURCE(2, N_INTR_TXQ_COAL_TO);

	/* Map Rx interrupts source to MSI/MSIX vector 1. */
	map[MSINUM_REG_INDEX(N_INTR_RXQ0_COMP)] =
	    MSINUM_INTR_SOURCE(1, N_INTR_RXQ0_COMP);
	map[MSINUM_REG_INDEX(N_INTR_RXQ1_COMP)] =
	    MSINUM_INTR_SOURCE(1, N_INTR_RXQ1_COMP);
	map[MSINUM_REG_INDEX(N_INTR_RXQ2_COMP)] =
	    MSINUM_INTR_SOURCE(1, N_INTR_RXQ2_COMP);
	map[MSINUM_REG_INDEX(N_INTR_RXQ3_COMP)] =
	    MSINUM_INTR_SOURCE(1, N_INTR_RXQ3_COMP);
	map[MSINUM_REG_INDEX(N_INTR_RXQ0_DESC_EMPTY)] =
	    MSINUM_INTR_SOURCE(1, N_INTR_RXQ0_DESC_EMPTY);
	map[MSINUM_REG_INDEX(N_INTR_RXQ1_DESC_EMPTY)] =
	    MSINUM_INTR_SOURCE(1, N_INTR_RXQ1_DESC_EMPTY);
	map[MSINUM_REG_INDEX(N_INTR_RXQ2_DESC_EMPTY)] =
	    MSINUM_INTR_SOURCE(1, N_INTR_RXQ2_DESC_EMPTY);
	map[MSINUM_REG_INDEX(N_INTR_RXQ3_DESC_EMPTY)] =
	    MSINUM_INTR_SOURCE(1, N_INTR_RXQ3_DESC_EMPTY);
	map[MSINUM_REG_INDEX(N_INTR_RXQ0_COAL)] =
	    MSINUM_INTR_SOURCE(1, N_INTR_RXQ0_COAL);
	map[MSINUM_REG_INDEX(N_INTR_RXQ1_COAL)] =
	    MSINUM_INTR_SOURCE(1, N_INTR_RXQ1_COAL);
	map[MSINUM_REG_INDEX(N_INTR_RXQ2_COAL)] =
	    MSINUM_INTR_SOURCE(1, N_INTR_RXQ2_COAL);
	map[MSINUM_REG_INDEX(N_INTR_RXQ3_COAL)] =
	    MSINUM_INTR_SOURCE(1, N_INTR_RXQ3_COAL);
	map[MSINUM_REG_INDEX(N_INTR_RXQ0_COAL_TO)] =
	    MSINUM_INTR_SOURCE(1, N_INTR_RXQ0_COAL_TO);
	map[MSINUM_REG_INDEX(N_INTR_RXQ1_COAL_TO)] =
	    MSINUM_INTR_SOURCE(1, N_INTR_RXQ1_COAL_TO);
	map[MSINUM_REG_INDEX(N_INTR_RXQ2_COAL_TO)] =
	    MSINUM_INTR_SOURCE(1, N_INTR_RXQ2_COAL_TO);
	map[MSINUM_REG_INDEX(N_INTR_RXQ3_COAL_TO)] =
	    MSINUM_INTR_SOURCE(1, N_INTR_RXQ3_COAL_TO);

	/* Map all other interrupts source to MSI/MSIX vector 0. */
	CSR_WRITE_4(sc, JME_MSINUM_BASE + sizeof(uint32_t) * 0, map[0]);
	CSR_WRITE_4(sc, JME_MSINUM_BASE + sizeof(uint32_t) * 1, map[1]);
	CSR_WRITE_4(sc, JME_MSINUM_BASE + sizeof(uint32_t) * 2, map[2]);
	CSR_WRITE_4(sc, JME_MSINUM_BASE + sizeof(uint32_t) * 3, map[3]);
}

void
jme_attach(struct device *parent, struct device *self, void *aux)
{
	struct jme_softc *sc = (struct jme_softc *)self;
	struct pci_attach_args *pa = aux;
	pci_chipset_tag_t pc = pa->pa_pc;
	pci_intr_handle_t ih;
	const char *intrstr;
	pcireg_t memtype;

	struct ifnet *ifp;
	uint32_t reg;
	int error = 0;

	/*
	 * Allocate IO memory
	 *
	 * JMC250 supports both memory mapped and I/O register space
	 * access.  Because I/O register access should use different
	 * BARs to access registers it's waste of time to use I/O
	 * register space access.  JMC250 uses 16K to map entire memory
	 * space.
	 */

	memtype = pci_mapreg_type(pa->pa_pc, pa->pa_tag, JME_PCIR_BAR);
	if (pci_mapreg_map(pa, JME_PCIR_BAR, memtype, 0, &sc->jme_mem_bt,
	    &sc->jme_mem_bh, NULL, &sc->jme_mem_size, 0)) {
		printf(": can't map mem space\n");
		return;
	}

	if (pci_intr_map_msi(pa, &ih) == 0)
		jme_map_intr_vector(sc);
	else if (pci_intr_map(pa, &ih) != 0) {
		printf(": can't map interrupt\n");
		return;
	}

	/*
	 * Allocate IRQ
	 */
	intrstr = pci_intr_string(pc, ih);
	sc->sc_irq_handle = pci_intr_establish(pc, ih, IPL_NET, jme_intr, sc,
	    sc->sc_dev.dv_xname);
	if (sc->sc_irq_handle == NULL) {
		printf(": could not establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		return;
	}
	printf(": %s", intrstr);

	sc->sc_dmat = pa->pa_dmat;
	sc->jme_pct = pa->pa_pc;
	sc->jme_pcitag = pa->pa_tag;

	/*
	 * Extract FPGA revision
	 */
	reg = CSR_READ_4(sc, JME_CHIPMODE);
	if (((reg & CHIPMODE_FPGA_REV_MASK) >> CHIPMODE_FPGA_REV_SHIFT) !=
	    CHIPMODE_NOT_FPGA) {
		sc->jme_caps |= JME_CAP_FPGA;

		if (jmedebug) {
			printf("%s: FPGA revision : 0x%04x\n",
			    sc->sc_dev.dv_xname, 
			    (reg & CHIPMODE_FPGA_REV_MASK) >>
			    CHIPMODE_FPGA_REV_SHIFT);
		}
	}

	sc->jme_revfm = (reg & CHIPMODE_REVFM_MASK) >> CHIPMODE_REVFM_SHIFT;

	if (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_JMICRON_JMC250 &&
	    PCI_REVISION(pa->pa_class) == JME_REV_JMC250_A2)
		sc->jme_workaround |= JME_WA_CRCERRORS | JME_WA_PACKETLOSS;

	/* Reset the ethernet controller. */
	jme_reset(sc);

	/* Get station address. */
	reg = CSR_READ_4(sc, JME_SMBCSR);
	if (reg & SMBCSR_EEPROM_PRESENT)
		error = jme_eeprom_macaddr(sc, sc->sc_arpcom.ac_enaddr);
	if (error != 0 || (reg & SMBCSR_EEPROM_PRESENT) == 0) {
		if (error != 0 && (jmedebug)) {
			printf("%s: ethernet hardware address "
			    "not found in EEPROM.\n", sc->sc_dev.dv_xname);
		}
		jme_reg_macaddr(sc, sc->sc_arpcom.ac_enaddr);
	}

	printf(", address %s\n", ether_sprintf(sc->sc_arpcom.ac_enaddr));

	/*
	 * Save PHY address.
	 * Integrated JR0211 has fixed PHY address whereas FPGA version
	 * requires PHY probing to get correct PHY address.
	 */
	if ((sc->jme_caps & JME_CAP_FPGA) == 0) {
		sc->jme_phyaddr = CSR_READ_4(sc, JME_GPREG0) &
		    GPREG0_PHY_ADDR_MASK;
		if (jmedebug) {
			printf("%s: PHY is at address %d.\n",
			    sc->sc_dev.dv_xname, sc->jme_phyaddr);
		}
	} else {
		sc->jme_phyaddr = 0;
	}

	/* Set max allowable DMA size. */
	sc->jme_tx_dma_size = TXCSR_DMA_SIZE_512;
	sc->jme_rx_dma_size = RXCSR_DMA_SIZE_128;

#ifdef notyet
	if (pci_find_extcap(dev, PCIY_PMG, &pmc) == 0)
		sc->jme_caps |= JME_CAP_PMCAP;
#endif

	/* Allocate DMA stuffs */
	error = jme_dma_alloc(sc);
	if (error)
		goto fail;

	ifp = &sc->sc_arpcom.ac_if;
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = jme_ioctl;
	ifp->if_start = jme_start;
	ifp->if_watchdog = jme_watchdog;
	ifq_init_maxlen(&ifp->if_snd, JME_TX_RING_CNT - 1);
	strlcpy(ifp->if_xname, sc->sc_dev.dv_xname, IFNAMSIZ);

	ifp->if_capabilities = IFCAP_VLAN_MTU | IFCAP_CSUM_IPv4 |
	    IFCAP_CSUM_TCPv4 | IFCAP_CSUM_UDPv4 | IFCAP_CSUM_TCPv6 |
	    IFCAP_CSUM_UDPv6;

#if NVLAN > 0
	ifp->if_capabilities |= IFCAP_VLAN_HWTAGGING;
#endif

	/* Set up MII bus. */
	sc->sc_miibus.mii_ifp = ifp;
	sc->sc_miibus.mii_readreg = jme_miibus_readreg;
	sc->sc_miibus.mii_writereg = jme_miibus_writereg;
	sc->sc_miibus.mii_statchg = jme_miibus_statchg;

	ifmedia_init(&sc->sc_miibus.mii_media, 0, jme_mediachange,
	    jme_mediastatus);
	mii_attach(self, &sc->sc_miibus, 0xffffffff,
	    sc->jme_caps & JME_CAP_FPGA ? MII_PHY_ANY : sc->jme_phyaddr,
	    MII_OFFSET_ANY, MIIF_DOPAUSE);

	if (LIST_FIRST(&sc->sc_miibus.mii_phys) == NULL) {
		printf("%s: no PHY found!\n", sc->sc_dev.dv_xname);
		ifmedia_add(&sc->sc_miibus.mii_media, IFM_ETHER | IFM_MANUAL,
		    0, NULL);
		ifmedia_set(&sc->sc_miibus.mii_media, IFM_ETHER | IFM_MANUAL);
	} else
		ifmedia_set(&sc->sc_miibus.mii_media, IFM_ETHER | IFM_AUTO);

	/*
	 * Save PHYADDR for FPGA mode PHY not handled, not production hw
	 */

	if_attach(ifp);
	ether_ifattach(ifp);

	timeout_set(&sc->jme_tick_ch, jme_tick, sc);

	return;
fail:
	jme_detach(&sc->sc_dev, 0);
}

int
jme_detach(struct device *self, int flags)
{
	struct jme_softc *sc = (struct jme_softc *)self;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	int s;

	s = splnet();
	jme_stop(sc);
	splx(s);

	mii_detach(&sc->sc_miibus, MII_PHY_ANY, MII_OFFSET_ANY);

	/* Delete all remaining media. */
	ifmedia_delete_instance(&sc->sc_miibus.mii_media, IFM_INST_ANY);

	ether_ifdetach(ifp);
	if_detach(ifp);
	jme_dma_free(sc);

	if (sc->sc_irq_handle != NULL) {
		pci_intr_disestablish(sc->jme_pct, sc->sc_irq_handle);
		sc->sc_irq_handle = NULL;
	}

	return (0);
}

int
jme_dma_alloc(struct jme_softc *sc)
{
	struct jme_txdesc *txd;
	struct jme_rxdesc *rxd;
	int error, i, nsegs;

	/*
	 * Create DMA stuffs for TX ring
	 */

	error = bus_dmamap_create(sc->sc_dmat, JME_TX_RING_SIZE, 1,
	    JME_TX_RING_SIZE, 0, BUS_DMA_NOWAIT,
	    &sc->jme_cdata.jme_tx_ring_map);
	if (error)
		return (ENOBUFS);

	/* Allocate DMA'able memory for TX ring */
	error = bus_dmamem_alloc(sc->sc_dmat, JME_TX_RING_SIZE, ETHER_ALIGN, 0,
	    &sc->jme_rdata.jme_tx_ring_seg, 1, &nsegs,
	    BUS_DMA_WAITOK);
/* XXX zero */
	if (error) {
		printf("%s: could not allocate DMA'able memory for Tx ring.\n",
		    sc->sc_dev.dv_xname);
		return error;
	}

	error = bus_dmamem_map(sc->sc_dmat, &sc->jme_rdata.jme_tx_ring_seg,
	    nsegs, JME_TX_RING_SIZE, (caddr_t *)&sc->jme_rdata.jme_tx_ring,
	    BUS_DMA_NOWAIT);
	if (error)
		return (ENOBUFS);

	/*  Load the DMA map for Tx ring. */
	error = bus_dmamap_load(sc->sc_dmat,
	    sc->jme_cdata.jme_tx_ring_map, sc->jme_rdata.jme_tx_ring,
	    JME_TX_RING_SIZE, NULL, BUS_DMA_NOWAIT);
	if (error) {
		printf("%s: could not load DMA'able memory for Tx ring.\n",
		    sc->sc_dev.dv_xname);
		bus_dmamem_free(sc->sc_dmat,
		    (bus_dma_segment_t *)&sc->jme_rdata.jme_tx_ring, 1);
		return error;
	}
	sc->jme_rdata.jme_tx_ring_paddr =
	    sc->jme_cdata.jme_tx_ring_map->dm_segs[0].ds_addr;

	/*
	 * Create DMA stuffs for RX ring
	 */

	error = bus_dmamap_create(sc->sc_dmat, JME_RX_RING_SIZE, 1,
	    JME_RX_RING_SIZE, 0, BUS_DMA_NOWAIT,
	    &sc->jme_cdata.jme_rx_ring_map);
	if (error)
		return (ENOBUFS);

	/* Allocate DMA'able memory for RX ring */
	error = bus_dmamem_alloc(sc->sc_dmat, JME_RX_RING_SIZE, ETHER_ALIGN, 0,
	    &sc->jme_rdata.jme_rx_ring_seg, 1, &nsegs,
	    BUS_DMA_WAITOK | BUS_DMA_ZERO);
/* XXX zero */
	if (error) {
		printf("%s: could not allocate DMA'able memory for Rx ring.\n",
		    sc->sc_dev.dv_xname);
		return error;
	}

	error = bus_dmamem_map(sc->sc_dmat, &sc->jme_rdata.jme_rx_ring_seg,
	    nsegs, JME_RX_RING_SIZE, (caddr_t *)&sc->jme_rdata.jme_rx_ring,
	    BUS_DMA_NOWAIT);
	if (error)
		return (ENOBUFS);

	/* Load the DMA map for Rx ring. */
	error = bus_dmamap_load(sc->sc_dmat,
	    sc->jme_cdata.jme_rx_ring_map, sc->jme_rdata.jme_rx_ring,
	    JME_RX_RING_SIZE, NULL, BUS_DMA_NOWAIT);
	if (error) {
		printf("%s: could not load DMA'able memory for Rx ring.\n",
		    sc->sc_dev.dv_xname);
		bus_dmamem_free(sc->sc_dmat,
		    (bus_dma_segment_t *)sc->jme_rdata.jme_rx_ring, 1);
		return error;
	}
	sc->jme_rdata.jme_rx_ring_paddr =
	    sc->jme_cdata.jme_rx_ring_map->dm_segs[0].ds_addr;

#if 0
	/* Tx/Rx descriptor queue should reside within 4GB boundary. */
	tx_ring_end = sc->jme_rdata.jme_tx_ring_paddr + JME_TX_RING_SIZE;
	rx_ring_end = sc->jme_rdata.jme_rx_ring_paddr + JME_RX_RING_SIZE;
	if ((JME_ADDR_HI(tx_ring_end) !=
	     JME_ADDR_HI(sc->jme_rdata.jme_tx_ring_paddr)) ||
	    (JME_ADDR_HI(rx_ring_end) !=
	     JME_ADDR_HI(sc->jme_rdata.jme_rx_ring_paddr))) {
		printf("%s: 4GB boundary crossed, switching to 32bit "
		    "DMA address mode.\n", sc->sc_dev.dv_xname);
		jme_dma_free(sc);
		/* Limit DMA address space to 32bit and try again. */
		lowaddr = BUS_SPACE_MAXADDR_32BIT;
		goto again;
	}
#endif

	/*
	 * Create DMA stuffs for shadow status block
	 */

	error = bus_dmamap_create(sc->sc_dmat, JME_SSB_SIZE, 1,
	    JME_SSB_SIZE, 0, BUS_DMA_NOWAIT, &sc->jme_cdata.jme_ssb_map);
	if (error)
		return (ENOBUFS);

	/* Allocate DMA'able memory for shared status block. */
	error = bus_dmamem_alloc(sc->sc_dmat, JME_SSB_SIZE, 1, 0,
	    &sc->jme_rdata.jme_ssb_block_seg, 1, &nsegs, BUS_DMA_WAITOK);
	if (error) {
		printf("%s: could not allocate DMA'able "
		    "memory for shared status block.\n", sc->sc_dev.dv_xname);
		return error;
	}

	error = bus_dmamem_map(sc->sc_dmat, &sc->jme_rdata.jme_ssb_block_seg,
	    nsegs, JME_SSB_SIZE, (caddr_t *)&sc->jme_rdata.jme_ssb_block,
	    BUS_DMA_NOWAIT);
	if (error)
		return (ENOBUFS);

	/* Load the DMA map for shared status block */
	error = bus_dmamap_load(sc->sc_dmat,
	    sc->jme_cdata.jme_ssb_map, sc->jme_rdata.jme_ssb_block,
	    JME_SSB_SIZE, NULL, BUS_DMA_NOWAIT);
	if (error) {
		printf("%s: could not load DMA'able memory "
		    "for shared status block.\n", sc->sc_dev.dv_xname);
		bus_dmamem_free(sc->sc_dmat,
		    (bus_dma_segment_t *)sc->jme_rdata.jme_ssb_block, 1);
		return error;
	}
	sc->jme_rdata.jme_ssb_block_paddr =
	    sc->jme_cdata.jme_ssb_map->dm_segs[0].ds_addr;

	/*
	 * Create DMA stuffs for TX buffers
	 */

	/* Create DMA maps for Tx buffers. */
	for (i = 0; i < JME_TX_RING_CNT; i++) {
		txd = &sc->jme_cdata.jme_txdesc[i];
		error = bus_dmamap_create(sc->sc_dmat, JME_TSO_MAXSIZE,
		    JME_MAXTXSEGS, JME_TSO_MAXSEGSIZE, 0, BUS_DMA_NOWAIT,
		    &txd->tx_dmamap);
		if (error) {
			int j;

			printf("%s: could not create %dth Tx dmamap.\n",
			    sc->sc_dev.dv_xname, i);

			for (j = 0; j < i; ++j) {
				txd = &sc->jme_cdata.jme_txdesc[j];
				bus_dmamap_destroy(sc->sc_dmat, txd->tx_dmamap);
			}
			return error;
		}

	}

	/*
	 * Create DMA stuffs for RX buffers
	 */

	/* Create DMA maps for Rx buffers. */
	error = bus_dmamap_create(sc->sc_dmat, MCLBYTES, 1, MCLBYTES,
	    0, BUS_DMA_NOWAIT, &sc->jme_cdata.jme_rx_sparemap);
	if (error) {
		printf("%s: could not create spare Rx dmamap.\n",
		    sc->sc_dev.dv_xname);
		return error;
	}
	for (i = 0; i < JME_RX_RING_CNT; i++) {
		rxd = &sc->jme_cdata.jme_rxdesc[i];
		error = bus_dmamap_create(sc->sc_dmat, MCLBYTES, 1, MCLBYTES,
		    0, BUS_DMA_NOWAIT, &rxd->rx_dmamap);
		if (error) {
			int j;

			printf("%s: could not create %dth Rx dmamap.\n",
			    sc->sc_dev.dv_xname, i);

			for (j = 0; j < i; ++j) {
				rxd = &sc->jme_cdata.jme_rxdesc[j];
				bus_dmamap_destroy(sc->sc_dmat, rxd->rx_dmamap);
			}
			bus_dmamap_destroy(sc->sc_dmat,
			    sc->jme_cdata.jme_rx_sparemap);
			sc->jme_cdata.jme_rx_tag = NULL;
			return error;
		}
	}

	return 0;
}

void
jme_dma_free(struct jme_softc *sc)
{
	struct jme_txdesc *txd;
	struct jme_rxdesc *rxd;
	int i;

	/* Tx ring */
	bus_dmamap_unload(sc->sc_dmat,
	    sc->jme_cdata.jme_tx_ring_map);
	bus_dmamem_free(sc->sc_dmat,
	    (bus_dma_segment_t *)sc->jme_rdata.jme_tx_ring, 1);

	/* Rx ring */
	bus_dmamap_unload(sc->sc_dmat,
	    sc->jme_cdata.jme_rx_ring_map);
	bus_dmamem_free(sc->sc_dmat,
	    (bus_dma_segment_t *)sc->jme_rdata.jme_rx_ring, 1);

	/* Tx buffers */
	for (i = 0; i < JME_TX_RING_CNT; i++) {
		txd = &sc->jme_cdata.jme_txdesc[i];
		bus_dmamap_destroy(sc->sc_dmat, txd->tx_dmamap);
	}

	/* Rx buffers */
	for (i = 0; i < JME_RX_RING_CNT; i++) {
		rxd = &sc->jme_cdata.jme_rxdesc[i];
		bus_dmamap_destroy(sc->sc_dmat, rxd->rx_dmamap);
	}
	bus_dmamap_destroy(sc->sc_dmat,
	    sc->jme_cdata.jme_rx_sparemap);

	/* Shadow status block. */
	bus_dmamap_unload(sc->sc_dmat,
	    sc->jme_cdata.jme_ssb_map);
	bus_dmamem_free(sc->sc_dmat,
	    (bus_dma_segment_t *)sc->jme_rdata.jme_ssb_block, 1);
}

#ifdef notyet
/*
 * Unlike other ethernet controllers, JMC250 requires
 * explicit resetting link speed to 10/100Mbps as gigabit
 * link will consume more power than 375mA.
 * Note, we reset the link speed to 10/100Mbps with
 * auto-negotiation but we don't know whether that operation
 * would succeed or not as we have no control after powering
 * off. If the renegotiation fail WOL may not work. Running
 * at 1Gbps draws more power than 375mA at 3.3V which is
 * specified in PCI specification and that would result in
 * a complete shutdown of power to the ethernet controller.
 *
 * TODO
 *  Save current negotiated media speed/duplex/flow-control
 *  to softc and restore the same link again after resuming.
 *  PHY handling such as power down/resetting to 100Mbps
 *  may be better handled in suspend method in phy driver.
 */
void
jme_setlinkspeed(struct jme_softc *sc)
{
	struct mii_data *mii;
	int aneg, i;

	JME_LOCK_ASSERT(sc);

	mii = &sc->sc_miibus;
	mii_pollstat(mii);
	aneg = 0;
	if ((mii->mii_media_status & IFM_AVALID) != 0) {
		switch IFM_SUBTYPE(mii->mii_media_active) {
		case IFM_10_T:
		case IFM_100_TX:
			return;
		case IFM_1000_T:
			aneg++;
		default:
			break;
		}
	}
	jme_miibus_writereg(&sc->sc_dev, sc->jme_phyaddr, MII_100T2CR, 0);
	jme_miibus_writereg(&sc->sc_dev, sc->jme_phyaddr, MII_ANAR,
	    ANAR_TX_FD | ANAR_TX | ANAR_10_FD | ANAR_10 | ANAR_CSMA);
	jme_miibus_writereg(&sc->sc_dev, sc->jme_phyaddr, MII_BMCR,
	    BMCR_AUTOEN | BMCR_STARTNEG);
	DELAY(1000);
	if (aneg != 0) {
		/* Poll link state until jme(4) get a 10/100 link. */
		for (i = 0; i < MII_ANEGTICKS_GIGE; i++) {
			mii_pollstat(mii);
			if ((mii->mii_media_status & IFM_AVALID) != 0) {
				switch (IFM_SUBTYPE(mii->mii_media_active)) {
				case IFM_10_T:
				case IFM_100_TX:
					jme_mac_config(sc);
					return;
				default:
					break;
				}
			}
			JME_UNLOCK(sc);
			pause("jmelnk", hz);
			JME_LOCK(sc);
		}
		if (i == MII_ANEGTICKS_GIGE)
			printf("%s: establishing link failed, "
			    "WOL may not work!\n", sc->sc_dev.dv_xname);
	}
	/*
	 * No link, force MAC to have 100Mbps, full-duplex link.
	 * This is the last resort and may/may not work.
	 */
	mii->mii_media_status = IFM_AVALID | IFM_ACTIVE;
	mii->mii_media_active = IFM_ETHER | IFM_100_TX | IFM_FDX;
	jme_mac_config(sc);
}

void
jme_setwol(struct jme_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	uint32_t gpr, pmcs;
	uint16_t pmstat;
	int pmc;

	if (pci_find_extcap(sc->sc_dev, PCIY_PMG, &pmc) != 0) {
		/* No PME capability, PHY power down. */
		jme_miibus_writereg(&sc->sc_dev, sc->jme_phyaddr,
		    MII_BMCR, BMCR_PDOWN);
		return;
	}

	gpr = CSR_READ_4(sc, JME_GPREG0) & ~GPREG0_PME_ENB;
	pmcs = CSR_READ_4(sc, JME_PMCS);
	pmcs &= ~PMCS_WOL_ENB_MASK;
	if ((ifp->if_capenable & IFCAP_WOL_MAGIC) != 0) {
		pmcs |= PMCS_MAGIC_FRAME | PMCS_MAGIC_FRAME_ENB;
		/* Enable PME message. */
		gpr |= GPREG0_PME_ENB;
		/* For gigabit controllers, reset link speed to 10/100. */
		if ((sc->jme_caps & JME_CAP_FASTETH) == 0)
			jme_setlinkspeed(sc);
	}

	CSR_WRITE_4(sc, JME_PMCS, pmcs);
	CSR_WRITE_4(sc, JME_GPREG0, gpr);

	/* Request PME. */
	pmstat = pci_read_config(sc->sc_dev, pmc + PCIR_POWER_STATUS, 2);
	pmstat &= ~(PCIM_PSTAT_PME | PCIM_PSTAT_PMEENABLE);
	if ((ifp->if_capenable & IFCAP_WOL) != 0)
		pmstat |= PCIM_PSTAT_PME | PCIM_PSTAT_PMEENABLE;
	pci_write_config(sc->sc_dev, pmc + PCIR_POWER_STATUS, pmstat, 2);
	if ((ifp->if_capenable & IFCAP_WOL) == 0) {
		/* No WOL, PHY power down. */
		jme_miibus_writereg(&sc->sc_dev, sc->jme_phyaddr,
		    MII_BMCR, BMCR_PDOWN);
	}
}
#endif

int
jme_encap(struct jme_softc *sc, struct mbuf *m)
{
	struct jme_txdesc *txd;
	struct jme_desc *desc;
	int error, i, prod;
	uint32_t cflags;

	prod = sc->jme_cdata.jme_tx_prod;
	txd = &sc->jme_cdata.jme_txdesc[prod];

	error = bus_dmamap_load_mbuf(sc->sc_dmat, txd->tx_dmamap,
	    m, BUS_DMA_NOWAIT);
	if (error != 0 && error != EFBIG)
		goto drop;
	if (error != 0) {
		if (m_defrag(m, M_DONTWAIT)) {
			error = ENOBUFS;
			goto drop;
		}
		error = bus_dmamap_load_mbuf(sc->sc_dmat, txd->tx_dmamap,
					     m, BUS_DMA_NOWAIT);
		if (error != 0)
			goto drop;
	}

	cflags = 0;

	/* Configure checksum offload. */
	if (m->m_pkthdr.csum_flags & M_IPV4_CSUM_OUT)
		cflags |= JME_TD_IPCSUM;
	if (m->m_pkthdr.csum_flags & M_TCP_CSUM_OUT)
		cflags |= JME_TD_TCPCSUM;
	if (m->m_pkthdr.csum_flags & M_UDP_CSUM_OUT)
		cflags |= JME_TD_UDPCSUM;

#if NVLAN > 0
	/* Configure VLAN. */
	if (m->m_flags & M_VLANTAG) {
		cflags |= (m->m_pkthdr.ether_vtag & JME_TD_VLAN_MASK);
		cflags |= JME_TD_VLAN_TAG;
	}
#endif

	desc = &sc->jme_rdata.jme_tx_ring[prod];
	desc->flags = htole32(cflags);
	desc->buflen = 0;
	desc->addr_hi = htole32(m->m_pkthdr.len);
	desc->addr_lo = 0;
	sc->jme_cdata.jme_tx_cnt++;
	JME_DESC_INC(prod, JME_TX_RING_CNT);
	for (i = 0; i < txd->tx_dmamap->dm_nsegs; i++) {
		desc = &sc->jme_rdata.jme_tx_ring[prod];
		desc->flags = htole32(JME_TD_OWN | JME_TD_64BIT);
		desc->buflen = htole32(txd->tx_dmamap->dm_segs[i].ds_len);
		desc->addr_hi =
		    htole32(JME_ADDR_HI(txd->tx_dmamap->dm_segs[i].ds_addr));
		desc->addr_lo =
		    htole32(JME_ADDR_LO(txd->tx_dmamap->dm_segs[i].ds_addr));
		sc->jme_cdata.jme_tx_cnt++;
		JME_DESC_INC(prod, JME_TX_RING_CNT);
	}

	/* Update producer index. */
	sc->jme_cdata.jme_tx_prod = prod;
	/*
	 * Finally request interrupt and give the first descriptor
	 * ownership to hardware.
	 */
	desc = txd->tx_desc;
	desc->flags |= htole32(JME_TD_OWN | JME_TD_INTR);

	txd->tx_m = m;
	txd->tx_ndesc = txd->tx_dmamap->dm_nsegs + JME_TXD_RSVD;

	/* Sync descriptors. */
	bus_dmamap_sync(sc->sc_dmat, txd->tx_dmamap, 0,
	    txd->tx_dmamap->dm_mapsize, BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->sc_dmat, sc->jme_cdata.jme_tx_ring_map, 0,
	     sc->jme_cdata.jme_tx_ring_map->dm_mapsize, BUS_DMASYNC_PREWRITE);

	return (0);

  drop:
	m_freem(m);
	return (error);
}

void
jme_start(struct ifnet *ifp)
{
	struct jme_softc *sc = ifp->if_softc;
	struct mbuf *m;
	int enq = 0;

	/* Reclaim transmitted frames. */
	if (sc->jme_cdata.jme_tx_cnt >= JME_TX_DESC_HIWAT)
		jme_txeof(sc);

	if (!(ifp->if_flags & IFF_RUNNING) || ifq_is_oactive(&ifp->if_snd))
		return;
	if ((sc->jme_flags & JME_FLAG_LINK) == 0)
		return;  
	if (ifq_empty(&ifp->if_snd))
		return;

	for (;;) {
		/*
		 * Check number of available TX descs, always
		 * leave JME_TXD_RSVD free TX descs.
		 */
		if (sc->jme_cdata.jme_tx_cnt + JME_TXD_RSVD >
		    JME_TX_RING_CNT - JME_TXD_RSVD) {
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
		if (jme_encap(sc, m) != 0) {
			ifp->if_oerrors++;
			continue;
		}

		enq++;

#if NBPFILTER > 0
		/*
		 * If there's a BPF listener, bounce a copy of this frame
		 * to him.
		 */
		if (ifp->if_bpf != NULL)
			bpf_mtap_ether(ifp->if_bpf, m, BPF_DIRECTION_OUT);
#endif
	}

	if (enq > 0) {
		/*
		 * Reading TXCSR takes very long time under heavy load
		 * so cache TXCSR value and writes the ORed value with
		 * the kick command to the TXCSR. This saves one register
		 * access cycle.
		 */
		CSR_WRITE_4(sc, JME_TXCSR, sc->jme_txcsr | TXCSR_TX_ENB |
		    TXCSR_TXQ_N_START(TXCSR_TXQ0));
		/* Set a timeout in case the chip goes out to lunch. */
		ifp->if_timer = JME_TX_TIMEOUT;
	}
}

void
jme_watchdog(struct ifnet *ifp)
{
	struct jme_softc *sc = ifp->if_softc;

	if ((sc->jme_flags & JME_FLAG_LINK) == 0) {
		printf("%s: watchdog timeout (missed link)\n",
		    sc->sc_dev.dv_xname);
		ifp->if_oerrors++;
		jme_init(ifp);
		return;
	}

	jme_txeof(sc);
	if (sc->jme_cdata.jme_tx_cnt == 0) {
		printf("%s: watchdog timeout (missed Tx interrupts) "
			  "-- recovering\n", sc->sc_dev.dv_xname);
		jme_start(ifp);
		return;
	}

	printf("%s: watchdog timeout\n", sc->sc_dev.dv_xname);
	ifp->if_oerrors++;
	jme_init(ifp);
	jme_start(ifp);
}

int
jme_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct jme_softc *sc = ifp->if_softc;
	struct mii_data *mii = &sc->sc_miibus;
	struct ifreq *ifr = (struct ifreq *)data;
	int error = 0, s;

	s = splnet();

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		if (!(ifp->if_flags & IFF_RUNNING))
			jme_init(ifp);
		break;

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				error = ENETRESET;
			else
				jme_init(ifp);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				jme_stop(sc);
		}
		break;

	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, cmd);
		break;

	default:
		error = ether_ioctl(ifp, &sc->sc_arpcom, cmd, data);
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			jme_iff(sc);
		error = 0;
	}

	splx(s);
	return (error);
}

void
jme_mac_config(struct jme_softc *sc)
{
	struct mii_data *mii;
	uint32_t ghc, rxmac, txmac, txpause, gp1;
	int phyconf = JMPHY_CONF_DEFFIFO, hdx = 0;

	mii = &sc->sc_miibus;

	CSR_WRITE_4(sc, JME_GHC, GHC_RESET);
	DELAY(10);
	CSR_WRITE_4(sc, JME_GHC, 0);
	ghc = 0;
	rxmac = CSR_READ_4(sc, JME_RXMAC);
	rxmac &= ~RXMAC_FC_ENB;
	txmac = CSR_READ_4(sc, JME_TXMAC);
	txmac &= ~(TXMAC_CARRIER_EXT | TXMAC_FRAME_BURST);
	txpause = CSR_READ_4(sc, JME_TXPFC);
	txpause &= ~TXPFC_PAUSE_ENB;
	if ((IFM_OPTIONS(mii->mii_media_active) & IFM_FDX) != 0) {
		ghc |= GHC_FULL_DUPLEX;
		rxmac &= ~RXMAC_COLL_DET_ENB;
		txmac &= ~(TXMAC_COLL_ENB | TXMAC_CARRIER_SENSE |
		    TXMAC_BACKOFF | TXMAC_CARRIER_EXT |
		    TXMAC_FRAME_BURST);
		if ((IFM_OPTIONS(mii->mii_media_active) & IFM_ETH_TXPAUSE) != 0)
			txpause |= TXPFC_PAUSE_ENB;
		if ((IFM_OPTIONS(mii->mii_media_active) & IFM_ETH_RXPAUSE) != 0)
			rxmac |= RXMAC_FC_ENB;
		/* Disable retry transmit timer/retry limit. */
		CSR_WRITE_4(sc, JME_TXTRHD, CSR_READ_4(sc, JME_TXTRHD) &
		    ~(TXTRHD_RT_PERIOD_ENB | TXTRHD_RT_LIMIT_ENB));
	} else {
		rxmac |= RXMAC_COLL_DET_ENB;
		txmac |= TXMAC_COLL_ENB | TXMAC_CARRIER_SENSE | TXMAC_BACKOFF;
		/* Enable retry transmit timer/retry limit. */
		CSR_WRITE_4(sc, JME_TXTRHD, CSR_READ_4(sc, JME_TXTRHD) |
		    TXTRHD_RT_PERIOD_ENB | TXTRHD_RT_LIMIT_ENB);
	}

	/*
	 * Reprogram Tx/Rx MACs with resolved speed/duplex.
	 */
	gp1 = CSR_READ_4(sc, JME_GPREG1);
	gp1 &= ~GPREG1_HALF_PATCH;

	if ((IFM_OPTIONS(mii->mii_media_active) & IFM_FDX) == 0)
		hdx = 1;

	switch (IFM_SUBTYPE(mii->mii_media_active)) {
	case IFM_10_T:
		ghc |= GHC_SPEED_10;
		if (hdx)
			gp1 |= GPREG1_HALF_PATCH;
		break;

	case IFM_100_TX:
		ghc |= GHC_SPEED_100;
		if (hdx)
			gp1 |= GPREG1_HALF_PATCH;

		/*
		 * Use extended FIFO depth to workaround CRC errors
		 * emitted by chips before JMC250B
		 */
		phyconf = JMPHY_CONF_EXTFIFO;
		break;

	case IFM_1000_T:
		if (sc->jme_caps & JME_CAP_FASTETH)
			break;

		ghc |= GHC_SPEED_1000;
		if (hdx)
			txmac |= TXMAC_CARRIER_EXT | TXMAC_FRAME_BURST;
		break;

	default:
		break;
	}

	if (sc->jme_revfm >= 2) {
		/* set clock sources for tx mac and offload engine */
		if (IFM_SUBTYPE(mii->mii_media_active) == IFM_1000_T)
			ghc |= GHC_TCPCK_1000 | GHC_TXCK_1000;
		else
			ghc |= GHC_TCPCK_10_100 | GHC_TXCK_10_100;
	}

	CSR_WRITE_4(sc, JME_GHC, ghc);
	CSR_WRITE_4(sc, JME_RXMAC, rxmac);
	CSR_WRITE_4(sc, JME_TXMAC, txmac);
	CSR_WRITE_4(sc, JME_TXPFC, txpause);

	if (sc->jme_workaround & JME_WA_CRCERRORS) {
		jme_miibus_writereg(&sc->sc_dev, sc->jme_phyaddr,
				    JMPHY_CONF, phyconf);
	}
	if (sc->jme_workaround & JME_WA_PACKETLOSS)
		CSR_WRITE_4(sc, JME_GPREG1, gp1);
}

int
jme_intr(void *xsc)
{
	struct jme_softc *sc = xsc;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	uint32_t status;
	int claimed = 0;

	status = CSR_READ_4(sc, JME_INTR_REQ_STATUS);
	if (status == 0 || status == 0xFFFFFFFF)
		return (0);

	/* Disable interrupts. */
	CSR_WRITE_4(sc, JME_INTR_MASK_CLR, JME_INTRS);

	status = CSR_READ_4(sc, JME_INTR_STATUS);
	if ((status & JME_INTRS) == 0 || status == 0xFFFFFFFF)
		goto back;

	/* Reset PCC counter/timer and Ack interrupts. */
	status &= ~(INTR_TXQ_COMP | INTR_RXQ_COMP);
	if (status & (INTR_TXQ_COAL | INTR_TXQ_COAL_TO))
		status |= INTR_TXQ_COAL | INTR_TXQ_COAL_TO | INTR_TXQ_COMP;
	if (status & (INTR_RXQ_COAL | INTR_RXQ_COAL_TO))
		status |= INTR_RXQ_COAL | INTR_RXQ_COAL_TO | INTR_RXQ_COMP;
	CSR_WRITE_4(sc, JME_INTR_STATUS, status);

	if (ifp->if_flags & IFF_RUNNING) {
		if (status & (INTR_RXQ_COAL | INTR_RXQ_COAL_TO))
			jme_rxeof(sc);

		if (status & INTR_RXQ_DESC_EMPTY) {
			/*
			 * Notify hardware availability of new Rx buffers.
			 * Reading RXCSR takes very long time under heavy
			 * load so cache RXCSR value and writes the ORed
			 * value with the kick command to the RXCSR. This
			 * saves one register access cycle.
			 */
			CSR_WRITE_4(sc, JME_RXCSR, sc->jme_rxcsr |
			    RXCSR_RX_ENB | RXCSR_RXQ_START);
		}

		if (status & (INTR_TXQ_COAL | INTR_TXQ_COAL_TO)) {
			jme_txeof(sc);
			jme_start(ifp);
		}
	}
	claimed = 1;
back:
	/* Reenable interrupts. */
	CSR_WRITE_4(sc, JME_INTR_MASK_SET, JME_INTRS);

	return (claimed);
}

void
jme_txeof(struct jme_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct jme_txdesc *txd;
	uint32_t status;
	int cons, nsegs;

	cons = sc->jme_cdata.jme_tx_cons;
	if (cons == sc->jme_cdata.jme_tx_prod)
		return;

	bus_dmamap_sync(sc->sc_dmat, sc->jme_cdata.jme_tx_ring_map, 0,
	    sc->jme_cdata.jme_tx_ring_map->dm_mapsize, BUS_DMASYNC_POSTREAD);

	/*
	 * Go through our Tx list and free mbufs for those
	 * frames which have been transmitted.
	 */
	while (cons != sc->jme_cdata.jme_tx_prod) {
		txd = &sc->jme_cdata.jme_txdesc[cons];

		if (txd->tx_m == NULL)
			panic("%s: freeing NULL mbuf!", sc->sc_dev.dv_xname);

		status = letoh32(txd->tx_desc->flags);
		if ((status & JME_TD_OWN) == JME_TD_OWN)
			break;

		if (status & (JME_TD_TMOUT | JME_TD_RETRY_EXP)) {
			ifp->if_oerrors++;
		} else {
			if (status & JME_TD_COLLISION) {
				ifp->if_collisions +=
				    letoh32(txd->tx_desc->buflen) &
				    JME_TD_BUF_LEN_MASK;
			}
		}

		/*
		 * Only the first descriptor of multi-descriptor
		 * transmission is updated so driver have to skip entire
		 * chained buffers for the transmitted frame. In other
		 * words, JME_TD_OWN bit is valid only at the first
		 * descriptor of a multi-descriptor transmission.
		 */
		for (nsegs = 0; nsegs < txd->tx_ndesc; nsegs++) {
			sc->jme_rdata.jme_tx_ring[cons].flags = 0;
			JME_DESC_INC(cons, JME_TX_RING_CNT);
		}

		/* Reclaim transferred mbufs. */
		bus_dmamap_unload(sc->sc_dmat, txd->tx_dmamap);
		m_freem(txd->tx_m);
		txd->tx_m = NULL;
		sc->jme_cdata.jme_tx_cnt -= txd->tx_ndesc;
		if (sc->jme_cdata.jme_tx_cnt < 0)
			panic("%s: Active Tx desc counter was garbled",
			    sc->sc_dev.dv_xname);
		txd->tx_ndesc = 0;
	}
	sc->jme_cdata.jme_tx_cons = cons;

	if (sc->jme_cdata.jme_tx_cnt == 0)
		ifp->if_timer = 0;

	if (sc->jme_cdata.jme_tx_cnt + JME_TXD_RSVD <=
	    JME_TX_RING_CNT - JME_TXD_RSVD)
		ifq_clr_oactive(&ifp->if_snd);

	bus_dmamap_sync(sc->sc_dmat, sc->jme_cdata.jme_tx_ring_map, 0,
	    sc->jme_cdata.jme_tx_ring_map->dm_mapsize, BUS_DMASYNC_PREWRITE);
}

void
jme_discard_rxbufs(struct jme_softc *sc, int cons, int count)
{
	int i;

	for (i = 0; i < count; ++i) {
		struct jme_desc *desc = &sc->jme_rdata.jme_rx_ring[cons];

		desc->flags = htole32(JME_RD_OWN | JME_RD_INTR | JME_RD_64BIT);
		desc->buflen = htole32(MCLBYTES);
		JME_DESC_INC(cons, JME_RX_RING_CNT);
	}
}

/* Receive a frame. */
void
jme_rxpkt(struct jme_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct jme_desc *desc;
	struct jme_rxdesc *rxd;
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct mbuf *mp, *m;
	uint32_t flags, status;
	int cons, count, nsegs;

	cons = sc->jme_cdata.jme_rx_cons;
	desc = &sc->jme_rdata.jme_rx_ring[cons];
	flags = letoh32(desc->flags);
	status = letoh32(desc->buflen);
	nsegs = JME_RX_NSEGS(status);

	if (status & JME_RX_ERR_STAT) {
		ifp->if_ierrors++;
		jme_discard_rxbufs(sc, cons, nsegs);
#ifdef JME_SHOW_ERRORS
		printf("%s : receive error = 0x%b\n",
		    sc->sc_dev.dv_xname, JME_RX_ERR(status), JME_RX_ERR_BITS);
#endif
		sc->jme_cdata.jme_rx_cons += nsegs;
		sc->jme_cdata.jme_rx_cons %= JME_RX_RING_CNT;
		return;
	}

	sc->jme_cdata.jme_rxlen = JME_RX_BYTES(status) - JME_RX_PAD_BYTES;
	for (count = 0; count < nsegs; count++,
	     JME_DESC_INC(cons, JME_RX_RING_CNT)) {
		rxd = &sc->jme_cdata.jme_rxdesc[cons];
		mp = rxd->rx_m;

		/* Add a new receive buffer to the ring. */
		if (jme_newbuf(sc, rxd) != 0) {
			ifp->if_iqdrops++;
			/* Reuse buffer. */
			jme_discard_rxbufs(sc, cons, nsegs - count);
			if (sc->jme_cdata.jme_rxhead != NULL) {
				m_freem(sc->jme_cdata.jme_rxhead);
				JME_RXCHAIN_RESET(sc);
			}
			break;
		}

		/*
		 * Assume we've received a full sized frame.
		 * Actual size is fixed when we encounter the end of
		 * multi-segmented frame.
		 */
		mp->m_len = MCLBYTES;

		/* Chain received mbufs. */
		if (sc->jme_cdata.jme_rxhead == NULL) {
			sc->jme_cdata.jme_rxhead = mp;
			sc->jme_cdata.jme_rxtail = mp;
		} else {
			/*
			 * Receive processor can receive a maximum frame
			 * size of 65535 bytes.
			 */
			mp->m_flags &= ~M_PKTHDR;
			sc->jme_cdata.jme_rxtail->m_next = mp;
			sc->jme_cdata.jme_rxtail = mp;
		}

		if (count == nsegs - 1) {
			/* Last desc. for this frame. */
			m = sc->jme_cdata.jme_rxhead;
			/* XXX assert PKTHDR? */
			m->m_flags |= M_PKTHDR;
			m->m_pkthdr.len = sc->jme_cdata.jme_rxlen;
			if (nsegs > 1) {
				/* Set first mbuf size. */
				m->m_len = MCLBYTES - JME_RX_PAD_BYTES;
				/* Set last mbuf size. */
				mp->m_len = sc->jme_cdata.jme_rxlen -
				    ((MCLBYTES - JME_RX_PAD_BYTES) +
				    (MCLBYTES * (nsegs - 2)));
			} else {
				m->m_len = sc->jme_cdata.jme_rxlen;
			}

			/*
			 * Account for 10bytes auto padding which is used
			 * to align IP header on 32bit boundary. Also note,
			 * CRC bytes is automatically removed by the
			 * hardware.
			 */
			m->m_data += JME_RX_PAD_BYTES;

			/* Set checksum information. */
			if (flags & (JME_RD_IPV4|JME_RD_IPV6)) {
				if ((flags & JME_RD_IPV4) &&
				    (flags & JME_RD_IPCSUM))
					m->m_pkthdr.csum_flags |=
					    M_IPV4_CSUM_IN_OK;
				if ((flags & JME_RD_MORE_FRAG) == 0 &&
				    ((flags & (JME_RD_TCP | JME_RD_TCPCSUM)) ==
				     (JME_RD_TCP | JME_RD_TCPCSUM) ||
				     (flags & (JME_RD_UDP | JME_RD_UDPCSUM)) ==
				     (JME_RD_UDP | JME_RD_UDPCSUM))) {
					m->m_pkthdr.csum_flags |=
					    M_TCP_CSUM_IN_OK | M_UDP_CSUM_IN_OK;
				}
			}

#if NVLAN > 0
			/* Check for VLAN tagged packets. */
			if (flags & JME_RD_VLAN_TAG) {
				m->m_pkthdr.ether_vtag = flags & JME_RD_VLAN_MASK;
				m->m_flags |= M_VLANTAG;
			}
#endif

			ml_enqueue(&ml, m);

			/* Reset mbuf chains. */
			JME_RXCHAIN_RESET(sc);
		}
	}

	if_input(ifp, &ml);

	sc->jme_cdata.jme_rx_cons += nsegs;
	sc->jme_cdata.jme_rx_cons %= JME_RX_RING_CNT;
}

void
jme_rxeof(struct jme_softc *sc)
{
	struct jme_desc *desc;
	int nsegs, prog, pktlen;

	bus_dmamap_sync(sc->sc_dmat, sc->jme_cdata.jme_rx_ring_map, 0,
	    sc->jme_cdata.jme_rx_ring_map->dm_mapsize, BUS_DMASYNC_POSTREAD);

	prog = 0;
	for (;;) {
		desc = &sc->jme_rdata.jme_rx_ring[sc->jme_cdata.jme_rx_cons];
		if ((letoh32(desc->flags) & JME_RD_OWN) == JME_RD_OWN)
			break;
		if ((letoh32(desc->buflen) & JME_RD_VALID) == 0)
			break;

		/*
		 * Check number of segments against received bytes.
		 * Non-matching value would indicate that hardware
		 * is still trying to update Rx descriptors. I'm not
		 * sure whether this check is needed.
		 */
		nsegs = JME_RX_NSEGS(letoh32(desc->buflen));
		pktlen = JME_RX_BYTES(letoh32(desc->buflen));
		if (nsegs != howmany(pktlen, MCLBYTES)) {
			printf("%s: RX fragment count(%d) "
			    "and packet size(%d) mismatch\n",
			     sc->sc_dev.dv_xname, nsegs, pktlen);
			break;
		}

		/* Received a frame. */
		jme_rxpkt(sc);
		prog++;
	}

	if (prog > 0) {
		bus_dmamap_sync(sc->sc_dmat, sc->jme_cdata.jme_rx_ring_map, 0,
		    sc->jme_cdata.jme_rx_ring_map->dm_mapsize, BUS_DMASYNC_PREWRITE);
	}
}

void
jme_tick(void *xsc)
{
	struct jme_softc *sc = xsc;
	struct mii_data *mii = &sc->sc_miibus;
	int s;

	s = splnet();
	mii_tick(mii);
	timeout_add_sec(&sc->jme_tick_ch, 1);
	splx(s);
}

void
jme_reset(struct jme_softc *sc)
{
#ifdef foo
	/* Stop receiver, transmitter. */
	jme_stop_rx(sc);
	jme_stop_tx(sc);
#endif
	CSR_WRITE_4(sc, JME_GHC, GHC_RESET);
	DELAY(10);
	CSR_WRITE_4(sc, JME_GHC, 0);
}

int
jme_init(struct ifnet *ifp)
{
	struct jme_softc *sc = ifp->if_softc;
	struct mii_data *mii;
	uint8_t eaddr[ETHER_ADDR_LEN];
	bus_addr_t paddr;
	uint32_t reg;
	int error;

	/*
	 * Cancel any pending I/O.
	 */
	jme_stop(sc);

	/*
	 * Reset the chip to a known state.
	 */
	jme_reset(sc);

	/* Init descriptors. */
	error = jme_init_rx_ring(sc);
        if (error != 0) {
                printf("%s: initialization failed: no memory for Rx buffers.\n",
		    sc->sc_dev.dv_xname);
                jme_stop(sc);
		return (error);
        }
	jme_init_tx_ring(sc);

	/* Initialize shadow status block. */
	jme_init_ssb(sc);

	/* Reprogram the station address. */
	bcopy(LLADDR(ifp->if_sadl), eaddr, ETHER_ADDR_LEN);
	CSR_WRITE_4(sc, JME_PAR0,
	    eaddr[3] << 24 | eaddr[2] << 16 | eaddr[1] << 8 | eaddr[0]);
	CSR_WRITE_4(sc, JME_PAR1, eaddr[5] << 8 | eaddr[4]);

	/*
	 * Configure Tx queue.
	 *  Tx priority queue weight value : 0
	 *  Tx FIFO threshold for processing next packet : 16QW
	 *  Maximum Tx DMA length : 512
	 *  Allow Tx DMA burst.
	 */
	sc->jme_txcsr = TXCSR_TXQ_N_SEL(TXCSR_TXQ0);
	sc->jme_txcsr |= TXCSR_TXQ_WEIGHT(TXCSR_TXQ_WEIGHT_MIN);
	sc->jme_txcsr |= TXCSR_FIFO_THRESH_16QW;
	sc->jme_txcsr |= sc->jme_tx_dma_size;
	sc->jme_txcsr |= TXCSR_DMA_BURST;
	CSR_WRITE_4(sc, JME_TXCSR, sc->jme_txcsr);

	/* Set Tx descriptor counter. */
	CSR_WRITE_4(sc, JME_TXQDC, JME_TX_RING_CNT);

	/* Set Tx ring address to the hardware. */
	paddr = JME_TX_RING_ADDR(sc, 0);
	CSR_WRITE_4(sc, JME_TXDBA_HI, JME_ADDR_HI(paddr));
	CSR_WRITE_4(sc, JME_TXDBA_LO, JME_ADDR_LO(paddr));

	/* Configure TxMAC parameters. */
	reg = TXMAC_IFG1_DEFAULT | TXMAC_IFG2_DEFAULT | TXMAC_IFG_ENB;
	reg |= TXMAC_THRESH_1_PKT;
	reg |= TXMAC_CRC_ENB | TXMAC_PAD_ENB;
	CSR_WRITE_4(sc, JME_TXMAC, reg);

	/*
	 * Configure Rx queue.
	 *  FIFO full threshold for transmitting Tx pause packet : 128T
	 *  FIFO threshold for processing next packet : 128QW
	 *  Rx queue 0 select
	 *  Max Rx DMA length : 128
	 *  Rx descriptor retry : 32
	 *  Rx descriptor retry time gap : 256ns
	 *  Don't receive runt/bad frame.
	 */
	sc->jme_rxcsr = RXCSR_FIFO_FTHRESH_128T;

	/*
	 * Since Rx FIFO size is 4K bytes, receiving frames larger
	 * than 4K bytes will suffer from Rx FIFO overruns. So
	 * decrease FIFO threshold to reduce the FIFO overruns for
	 * frames larger than 4000 bytes.
	 * For best performance of standard MTU sized frames use
	 * maximum allowable FIFO threshold, which is 32QW for
	 * chips with a full mask >= 2 otherwise 128QW. FIFO
	 * thresholds of 64QW and 128QW are not valid for chips
	 * with a full mask >= 2.
	 */
	if (sc->jme_revfm >= 2)
		sc->jme_rxcsr |= RXCSR_FIFO_THRESH_16QW;
	else {
		if ((ifp->if_mtu + ETHER_HDR_LEN + ETHER_CRC_LEN +
		    ETHER_VLAN_ENCAP_LEN) > JME_RX_FIFO_SIZE)
			sc->jme_rxcsr |= RXCSR_FIFO_THRESH_16QW;
		else
			sc->jme_rxcsr |= RXCSR_FIFO_THRESH_128QW;
	}
	sc->jme_rxcsr |= sc->jme_rx_dma_size | RXCSR_RXQ_N_SEL(RXCSR_RXQ0);
	sc->jme_rxcsr |= RXCSR_DESC_RT_CNT(RXCSR_DESC_RT_CNT_DEFAULT);
	sc->jme_rxcsr |= RXCSR_DESC_RT_GAP_256 & RXCSR_DESC_RT_GAP_MASK;
	/* XXX TODO DROP_BAD */
	CSR_WRITE_4(sc, JME_RXCSR, sc->jme_rxcsr);

	/* Set Rx descriptor counter. */
	CSR_WRITE_4(sc, JME_RXQDC, JME_RX_RING_CNT);

	/* Set Rx ring address to the hardware. */
	paddr = JME_RX_RING_ADDR(sc, 0);
	CSR_WRITE_4(sc, JME_RXDBA_HI, JME_ADDR_HI(paddr));
	CSR_WRITE_4(sc, JME_RXDBA_LO, JME_ADDR_LO(paddr));

	/* Clear receive filter. */
	CSR_WRITE_4(sc, JME_RXMAC, 0);

	/* Set up the receive filter. */
	jme_iff(sc);

	jme_set_vlan(sc);

	/*
	 * Disable all WOL bits as WOL can interfere normal Rx
	 * operation. Also clear WOL detection status bits.
	 */
	reg = CSR_READ_4(sc, JME_PMCS);
	reg &= ~PMCS_WOL_ENB_MASK;
	CSR_WRITE_4(sc, JME_PMCS, reg);

	/*
	 * Pad 10bytes right before received frame. This will greatly
	 * help Rx performance on strict-alignment architectures as
	 * it does not need to copy the frame to align the payload.
	 */
	reg = CSR_READ_4(sc, JME_RXMAC);
	reg |= RXMAC_PAD_10BYTES;
	reg |= RXMAC_CSUM_ENB;
	CSR_WRITE_4(sc, JME_RXMAC, reg);

	/* Configure general purpose reg0 */
	reg = CSR_READ_4(sc, JME_GPREG0);
	reg &= ~GPREG0_PCC_UNIT_MASK;
	/* Set PCC timer resolution to micro-seconds unit. */
	reg |= GPREG0_PCC_UNIT_US;
	/*
	 * Disable all shadow register posting as we have to read
	 * JME_INTR_STATUS register in jme_intr. Also it seems
	 * that it's hard to synchronize interrupt status between
	 * hardware and software with shadow posting due to
	 * requirements of bus_dmamap_sync(9).
	 */
	reg |= GPREG0_SH_POST_DW7_DIS | GPREG0_SH_POST_DW6_DIS |
	    GPREG0_SH_POST_DW5_DIS | GPREG0_SH_POST_DW4_DIS |
	    GPREG0_SH_POST_DW3_DIS | GPREG0_SH_POST_DW2_DIS |
	    GPREG0_SH_POST_DW1_DIS | GPREG0_SH_POST_DW0_DIS;
	/* Disable posting of DW0. */
	reg &= ~GPREG0_POST_DW0_ENB;
	/* Clear PME message. */
	reg &= ~GPREG0_PME_ENB;
	/* Set PHY address. */
	reg &= ~GPREG0_PHY_ADDR_MASK;
	reg |= sc->jme_phyaddr;
	CSR_WRITE_4(sc, JME_GPREG0, reg);

	/* Configure Tx queue 0 packet completion coalescing. */
	sc->jme_tx_coal_to = PCCTX_COAL_TO_DEFAULT;
	reg = (sc->jme_tx_coal_to << PCCTX_COAL_TO_SHIFT) &
	    PCCTX_COAL_TO_MASK;
	sc->jme_tx_coal_pkt = PCCTX_COAL_PKT_DEFAULT;
	reg |= (sc->jme_tx_coal_pkt << PCCTX_COAL_PKT_SHIFT) &
	    PCCTX_COAL_PKT_MASK;
	reg |= PCCTX_COAL_TXQ0;
	CSR_WRITE_4(sc, JME_PCCTX, reg);

	/* Configure Rx queue 0 packet completion coalescing. */
	sc->jme_rx_coal_to = PCCRX_COAL_TO_DEFAULT;
	reg = (sc->jme_rx_coal_to << PCCRX_COAL_TO_SHIFT) &
	    PCCRX_COAL_TO_MASK;
	sc->jme_rx_coal_pkt = PCCRX_COAL_PKT_DEFAULT;
	reg |= (sc->jme_rx_coal_pkt << PCCRX_COAL_PKT_SHIFT) &
	    PCCRX_COAL_PKT_MASK;
	CSR_WRITE_4(sc, JME_PCCRX0, reg);

	/* Configure shadow status block but don't enable posting. */
	paddr = sc->jme_rdata.jme_ssb_block_paddr;
	CSR_WRITE_4(sc, JME_SHBASE_ADDR_HI, JME_ADDR_HI(paddr));
	CSR_WRITE_4(sc, JME_SHBASE_ADDR_LO, JME_ADDR_LO(paddr));

	/* Disable Timer 1 and Timer 2. */
	CSR_WRITE_4(sc, JME_TIMER1, 0);
	CSR_WRITE_4(sc, JME_TIMER2, 0);

	/* Configure retry transmit period, retry limit value. */
	CSR_WRITE_4(sc, JME_TXTRHD,
	    ((TXTRHD_RT_PERIOD_DEFAULT << TXTRHD_RT_PERIOD_SHIFT) &
	    TXTRHD_RT_PERIOD_MASK) |
	    ((TXTRHD_RT_LIMIT_DEFAULT << TXTRHD_RT_LIMIT_SHIFT) &
	    TXTRHD_RT_LIMIT_SHIFT));

	/* Disable RSS. */
	CSR_WRITE_4(sc, JME_RSSC, RSSC_DIS_RSS);

	/* Initialize the interrupt mask. */
	CSR_WRITE_4(sc, JME_INTR_MASK_SET, JME_INTRS);
	CSR_WRITE_4(sc, JME_INTR_STATUS, 0xFFFFFFFF);

	/*
	 * Enabling Tx/Rx DMA engines and Rx queue processing is
	 * done after detection of valid link in jme_miibus_statchg.
	 */
	sc->jme_flags &= ~JME_FLAG_LINK;

	/* Set the current media. */
	mii = &sc->sc_miibus;
	mii_mediachg(mii);

	timeout_add_sec(&sc->jme_tick_ch, 1);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	return (0);
}

void
jme_stop(struct jme_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct jme_txdesc *txd;
	struct jme_rxdesc *rxd;
	int i;

	/*
	 * Mark the interface down and cancel the watchdog timer.
	 */
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
	ifp->if_timer = 0;

	timeout_del(&sc->jme_tick_ch);
	sc->jme_flags &= ~JME_FLAG_LINK;

	/*
	 * Disable interrupts.
	 */
	CSR_WRITE_4(sc, JME_INTR_MASK_CLR, JME_INTRS);
	CSR_WRITE_4(sc, JME_INTR_STATUS, 0xFFFFFFFF);

	/* Disable updating shadow status block. */
	CSR_WRITE_4(sc, JME_SHBASE_ADDR_LO,
	    CSR_READ_4(sc, JME_SHBASE_ADDR_LO) & ~SHBASE_POST_ENB);

	/* Stop receiver, transmitter. */
	jme_stop_rx(sc);
	jme_stop_tx(sc);

#ifdef foo
	 /* Reclaim Rx/Tx buffers that have been completed. */
	jme_rxeof(sc);
	m_freem(sc->jme_cdata.jme_rxhead);
	JME_RXCHAIN_RESET(sc);
	jme_txeof(sc);
#endif

	/*
	 * Free partial finished RX segments
	 */
	m_freem(sc->jme_cdata.jme_rxhead);
	JME_RXCHAIN_RESET(sc);

	/*
	 * Free RX and TX mbufs still in the queues.
	 */
	for (i = 0; i < JME_RX_RING_CNT; i++) {
		rxd = &sc->jme_cdata.jme_rxdesc[i];
		if (rxd->rx_m != NULL) {
			bus_dmamap_unload(sc->sc_dmat, rxd->rx_dmamap);
			m_freem(rxd->rx_m);
			rxd->rx_m = NULL;
		}
        }
	for (i = 0; i < JME_TX_RING_CNT; i++) {
		txd = &sc->jme_cdata.jme_txdesc[i];
		if (txd->tx_m != NULL) {
			bus_dmamap_unload(sc->sc_dmat, txd->tx_dmamap);
			m_freem(txd->tx_m);
			txd->tx_m = NULL;
			txd->tx_ndesc = 0;
		}
        }
}

void
jme_stop_tx(struct jme_softc *sc)
{
	uint32_t reg;
	int i;

	reg = CSR_READ_4(sc, JME_TXCSR);
	if ((reg & TXCSR_TX_ENB) == 0)
		return;
	reg &= ~TXCSR_TX_ENB;
	CSR_WRITE_4(sc, JME_TXCSR, reg);
	for (i = JME_TIMEOUT; i > 0; i--) {
		DELAY(1);
		if ((CSR_READ_4(sc, JME_TXCSR) & TXCSR_TX_ENB) == 0)
			break;
	}
	if (i == 0)
		printf("%s: stopping transmitter timeout!\n",
		    sc->sc_dev.dv_xname);
}

void
jme_stop_rx(struct jme_softc *sc)
{
	uint32_t reg;
	int i;

	reg = CSR_READ_4(sc, JME_RXCSR);
	if ((reg & RXCSR_RX_ENB) == 0)
		return;
	reg &= ~RXCSR_RX_ENB;
	CSR_WRITE_4(sc, JME_RXCSR, reg);
	for (i = JME_TIMEOUT; i > 0; i--) {
		DELAY(1);
		if ((CSR_READ_4(sc, JME_RXCSR) & RXCSR_RX_ENB) == 0)
			break;
	}
	if (i == 0)
		printf("%s: stopping receiver timeout!\n", sc->sc_dev.dv_xname);
}

void
jme_init_tx_ring(struct jme_softc *sc)
{
	struct jme_ring_data *rd;
	struct jme_txdesc *txd;
	int i;

	sc->jme_cdata.jme_tx_prod = 0;
	sc->jme_cdata.jme_tx_cons = 0;
	sc->jme_cdata.jme_tx_cnt = 0;

	rd = &sc->jme_rdata;
	bzero(rd->jme_tx_ring, JME_TX_RING_SIZE);
	for (i = 0; i < JME_TX_RING_CNT; i++) {
		txd = &sc->jme_cdata.jme_txdesc[i];
		txd->tx_m = NULL;
		txd->tx_desc = &rd->jme_tx_ring[i];
		txd->tx_ndesc = 0;
	}

	bus_dmamap_sync(sc->sc_dmat, sc->jme_cdata.jme_tx_ring_map, 0,
	    sc->jme_cdata.jme_tx_ring_map->dm_mapsize, BUS_DMASYNC_PREWRITE);
}

void
jme_init_ssb(struct jme_softc *sc)
{
	struct jme_ring_data *rd;

	rd = &sc->jme_rdata;
	bzero(rd->jme_ssb_block, JME_SSB_SIZE);
	bus_dmamap_sync(sc->sc_dmat, sc->jme_cdata.jme_ssb_map, 0,
	    sc->jme_cdata.jme_ssb_map->dm_mapsize, BUS_DMASYNC_PREWRITE);
}

int
jme_init_rx_ring(struct jme_softc *sc)
{
	struct jme_ring_data *rd;
	struct jme_rxdesc *rxd;
	int i;

	KASSERT(sc->jme_cdata.jme_rxhead == NULL &&
		 sc->jme_cdata.jme_rxtail == NULL &&
		 sc->jme_cdata.jme_rxlen == 0);
	sc->jme_cdata.jme_rx_cons = 0;

	rd = &sc->jme_rdata;
	bzero(rd->jme_rx_ring, JME_RX_RING_SIZE);
	for (i = 0; i < JME_RX_RING_CNT; i++) {
		int error;

		rxd = &sc->jme_cdata.jme_rxdesc[i];
		rxd->rx_m = NULL;
		rxd->rx_desc = &rd->jme_rx_ring[i];
		error = jme_newbuf(sc, rxd);
		if (error)
			return (error);
	}

	bus_dmamap_sync(sc->sc_dmat, sc->jme_cdata.jme_rx_ring_map, 0,
	    sc->jme_cdata.jme_rx_ring_map->dm_mapsize, BUS_DMASYNC_PREWRITE);

	return (0);
}

int
jme_newbuf(struct jme_softc *sc, struct jme_rxdesc *rxd)
{
	struct jme_desc *desc;
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

	/*
	 * JMC250 has 64bit boundary alignment limitation so jme(4)
	 * takes advantage of 10 bytes padding feature of hardware
	 * in order not to copy entire frame to align IP header on
	 * 32bit boundary.
	 */
	m->m_len = m->m_pkthdr.len = MCLBYTES;

	error = bus_dmamap_load_mbuf(sc->sc_dmat,
	    sc->jme_cdata.jme_rx_sparemap, m, BUS_DMA_NOWAIT);

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
	rxd->rx_dmamap = sc->jme_cdata.jme_rx_sparemap;
	sc->jme_cdata.jme_rx_sparemap = map;
	rxd->rx_m = m;

	desc = rxd->rx_desc;
	desc->buflen = htole32(rxd->rx_dmamap->dm_segs[0].ds_len);
	desc->addr_lo =
	    htole32(JME_ADDR_LO(rxd->rx_dmamap->dm_segs[0].ds_addr));
	desc->addr_hi =
	    htole32(JME_ADDR_HI(rxd->rx_dmamap->dm_segs[0].ds_addr));
	desc->flags = htole32(JME_RD_OWN | JME_RD_INTR | JME_RD_64BIT);

	return (0);
}

void
jme_set_vlan(struct jme_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	uint32_t reg;

	reg = CSR_READ_4(sc, JME_RXMAC);
	reg &= ~RXMAC_VLAN_ENB;
	if (ifp->if_capabilities & IFCAP_VLAN_HWTAGGING)
		reg |= RXMAC_VLAN_ENB;
	CSR_WRITE_4(sc, JME_RXMAC, reg);
}

void
jme_iff(struct jme_softc *sc)
{
	struct arpcom *ac = &sc->sc_arpcom;
	struct ifnet *ifp = &ac->ac_if;
	struct ether_multi *enm;
	struct ether_multistep step;
	uint32_t crc;
	uint32_t mchash[2];
	uint32_t rxcfg;

	rxcfg = CSR_READ_4(sc, JME_RXMAC);
	rxcfg &= ~(RXMAC_BROADCAST | RXMAC_PROMISC | RXMAC_MULTICAST |
	    RXMAC_ALLMULTI);
	ifp->if_flags &= ~IFF_ALLMULTI;

	/*
	 * Always accept frames destined to our station address.
	 * Always accept broadcast frames.
	 */
	rxcfg |= RXMAC_UNICAST | RXMAC_BROADCAST;

	if (ifp->if_flags & IFF_PROMISC || ac->ac_multirangecnt > 0) {
		ifp->if_flags |= IFF_ALLMULTI;
		if (ifp->if_flags & IFF_PROMISC)
			rxcfg |= RXMAC_PROMISC;
		else
			rxcfg |= RXMAC_ALLMULTI;
		mchash[0] = mchash[1] = 0xFFFFFFFF;
	} else {
		/*
		 * Set up the multicast address filter by passing all
		 * multicast addresses through a CRC generator, and then
		 * using the low-order 6 bits as an index into the 64 bit
		 * multicast hash table.  The high order bits select the
		 * register, while the rest of the bits select the bit
		 * within the register.
		 */
		rxcfg |= RXMAC_MULTICAST;
		bzero(mchash, sizeof(mchash));

		ETHER_FIRST_MULTI(step, ac, enm);
		while (enm != NULL) {
			crc = ether_crc32_be(enm->enm_addrlo, ETHER_ADDR_LEN);

			/* Just want the 6 least significant bits. */
			crc &= 0x3f;

			/* Set the corresponding bit in the hash table. */
			mchash[crc >> 5] |= 1 << (crc & 0x1f);

			ETHER_NEXT_MULTI(step, enm);
		}
	}

	CSR_WRITE_4(sc, JME_MAR0, mchash[0]);
	CSR_WRITE_4(sc, JME_MAR1, mchash[1]);
	CSR_WRITE_4(sc, JME_RXMAC, rxcfg);
}
