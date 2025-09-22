/*	$OpenBSD: if_lii.c,v 1.48 2024/08/31 16:23:09 deraadt Exp $	*/

/*
 *  Copyright (c) 2007 The NetBSD Foundation.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 *  ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 *  TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 *  PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 *  BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Driver for Attansic/Atheros's L2 Fast Ethernet controller
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/device.h>
#include <sys/timeout.h>

#include <machine/bus.h>

#include <net/if.h>
#include <net/if_media.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <dev/mii/miivar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/pci/if_liireg.h>

/*#define LII_DEBUG*/
#ifdef LII_DEBUG
#define DPRINTF(x)	printf x
#else
#define DPRINTF(x)
#endif

struct lii_softc {
	struct device		sc_dev;
	pci_chipset_tag_t	sc_pc;
	pcitag_t		sc_tag;

	bus_space_tag_t		sc_mmiot;
	bus_space_handle_t	sc_mmioh;
	bus_size_t		sc_mmios;

	/*
	 * We allocate a big chunk of DMA-safe memory for all data exchanges.
	 * It is unfortunate that this chip doesn't seem to do scatter-gather.
	 */
	bus_dma_tag_t		sc_dmat;
	bus_dmamap_t		sc_ringmap;
	bus_dma_segment_t	sc_ringseg;

	uint8_t			*sc_ring; /* the whole area */
	size_t			sc_ringsize;

	struct rx_pkt		*sc_rxp; /* the part used for RX */
	struct tx_pkt_status	*sc_txs; /* the parts used for TX */
	bus_addr_t		sc_txsp;
	char			*sc_txdbase;
	bus_addr_t		sc_txdp;

	unsigned int		sc_rxcur;
	/* the active area is [ack; cur[ */
	int			sc_txs_cur;
	int			sc_txs_ack;
	int			sc_txd_cur;
	int			sc_txd_ack;
	int			sc_free_tx_slots;

	void			*sc_ih;

	struct arpcom		sc_ac;
	struct mii_data		sc_mii;
	struct timeout		sc_tick;

	int			(*sc_memread)(struct lii_softc *, uint32_t,
				     uint32_t *);
};

#define DEVNAME(_s)	((_s)->sc_dev.dv_xname)

int	lii_match(struct device *, void *, void *);
void	lii_attach(struct device *, struct device *, void *);
int	lii_activate(struct device *, int);

struct cfdriver lii_cd = {
	0,
	"lii",
	DV_IFNET
};

const struct cfattach lii_ca = {
	sizeof(struct lii_softc),
	lii_match,
	lii_attach,
	NULL,
	lii_activate
};

int	lii_reset(struct lii_softc *);
int	lii_eeprom_present(struct lii_softc *);
void	lii_read_macaddr(struct lii_softc *, uint8_t *);
int	lii_eeprom_read(struct lii_softc *, uint32_t, uint32_t *);
void	lii_spi_configure(struct lii_softc *);
int	lii_spi_read(struct lii_softc *, uint32_t, uint32_t *);
void	lii_iff(struct lii_softc *);
void	lii_tick(void *);

int	lii_alloc_rings(struct lii_softc *);
int	lii_free_tx_space(struct lii_softc *);
void	lii_tx_put(struct lii_softc *, struct mbuf *);

int	lii_mii_readreg(struct device *, int, int);
void	lii_mii_writereg(struct device *, int, int, int);
void	lii_mii_statchg(struct device *);

int	lii_media_change(struct ifnet *);
void	lii_media_status(struct ifnet *, struct ifmediareq *);

int	lii_init(struct ifnet *);
void	lii_start(struct ifnet *);
void	lii_stop(struct ifnet *);
void	lii_watchdog(struct ifnet *);
int	lii_ioctl(struct ifnet *, u_long, caddr_t);

int	lii_intr(void *);
void	lii_rxintr(struct lii_softc *);
void	lii_txintr(struct lii_softc *);

const struct pci_matchid lii_devices[] = {
	{ PCI_VENDOR_ATTANSIC, PCI_PRODUCT_ATTANSIC_L2 }
};

#define LII_READ_4(sc,reg) \
    bus_space_read_4((sc)->sc_mmiot, (sc)->sc_mmioh, (reg))
#define LII_READ_2(sc,reg) \
    bus_space_read_2((sc)->sc_mmiot, (sc)->sc_mmioh, (reg))
#define LII_READ_1(sc,reg) \
    bus_space_read_1((sc)->sc_mmiot, (sc)->sc_mmioh, (reg))
#define LII_WRITE_4(sc,reg,val) \
    bus_space_write_4((sc)->sc_mmiot, (sc)->sc_mmioh, (reg), (val))
#define LII_WRITE_2(sc,reg,val) \
    bus_space_write_2((sc)->sc_mmiot, (sc)->sc_mmioh, (reg), (val))
#define LII_WRITE_1(sc,reg,val) \
    bus_space_write_1((sc)->sc_mmiot, (sc)->sc_mmioh, (reg), (val))

/*
 * Those are the default Linux parameters.
 */

#define AT_TXD_NUM		64
#define AT_TXD_BUFFER_SIZE	8192
#define AT_RXD_NUM		64

/* Pad the RXD buffer so that the packets are on a 128-byte boundary. */
#define AT_RXD_PADDING		120

int
lii_match(struct device *parent, void *match, void *aux)
{
	return (pci_matchbyid((struct pci_attach_args *)aux, lii_devices,   
	    nitems(lii_devices)));
}

void
lii_attach(struct device *parent, struct device *self, void *aux)
{
	struct lii_softc *sc = (struct lii_softc *)self;
	struct pci_attach_args *pa = aux;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	pci_intr_handle_t ih;
	pcireg_t memtype;

	sc->sc_pc = pa->pa_pc;
	sc->sc_tag = pa->pa_tag;
	sc->sc_dmat = pa->pa_dmat;

	memtype = pci_mapreg_type(sc->sc_pc, sc->sc_tag, PCI_MAPREG_START);
	if (pci_mapreg_map(pa, PCI_MAPREG_START, memtype, 0,  &sc->sc_mmiot, 
	    &sc->sc_mmioh, NULL, &sc->sc_mmios, 0)) {
		printf(": can't map mem space\n");
		return;
	}

	if (lii_reset(sc))
		goto unmap;

	lii_spi_configure(sc);

	if (lii_eeprom_present(sc))
		sc->sc_memread = lii_eeprom_read;
	else
		sc->sc_memread = lii_spi_read;

	lii_read_macaddr(sc, sc->sc_ac.ac_enaddr);

	if (pci_intr_map(pa, &ih) != 0) {
		printf(": can't map interrupt\n");
		goto unmap;
	}
	sc->sc_ih = pci_intr_establish(sc->sc_pc, ih, IPL_NET,
	    lii_intr, sc, DEVNAME(sc));
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt\n");
		goto unmap;
	}

	if (lii_alloc_rings(sc))
		goto deintr;

	printf(": %s, address %s\n", pci_intr_string(sc->sc_pc, ih),
	    ether_sprintf(sc->sc_ac.ac_enaddr));

	timeout_set(&sc->sc_tick, lii_tick, sc);

	sc->sc_mii.mii_ifp = ifp;
	sc->sc_mii.mii_readreg = lii_mii_readreg;
	sc->sc_mii.mii_writereg = lii_mii_writereg;
	sc->sc_mii.mii_statchg = lii_mii_statchg;
	ifmedia_init(&sc->sc_mii.mii_media, IFM_IMASK, lii_media_change,
	    lii_media_status);
	mii_attach(self, &sc->sc_mii, 0xffffffff, 1,
	    MII_OFFSET_ANY, 0);
	ifmedia_set(&sc->sc_mii.mii_media, IFM_ETHER|IFM_AUTO);

	strlcpy(ifp->if_xname, DEVNAME(sc), IFNAMSIZ);
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_capabilities = IFCAP_VLAN_MTU;
	ifp->if_ioctl = lii_ioctl;
	ifp->if_start = lii_start;
	ifp->if_watchdog = lii_watchdog;

	if_attach(ifp);
	ether_ifattach(ifp);

	return;

deintr:
	pci_intr_disestablish(sc->sc_pc, sc->sc_ih);
unmap:
	bus_space_unmap(sc->sc_mmiot, sc->sc_mmioh, sc->sc_mmios);
	return;
}

int
lii_activate(struct device *self, int act)
{
	struct lii_softc *sc = (struct lii_softc *)self;
	struct ifnet *ifp = &sc->sc_ac.ac_if;

	switch (act) {
	case DVACT_SUSPEND:
		if (ifp->if_flags & IFF_RUNNING)
			lii_stop(ifp);
		break;
	case DVACT_RESUME:
		if (ifp->if_flags & IFF_UP)
			lii_init(ifp);
		break;
	}
	return (0);
}

int
lii_reset(struct lii_softc *sc)
{
	int i;

	DPRINTF(("lii_reset\n"));

	LII_WRITE_4(sc, LII_SMC, SMC_SOFT_RST);
	DELAY(1000);

	for (i = 0; i < 10; ++i) {
		if (LII_READ_4(sc, LII_BIS) == 0)
			break;
		DELAY(1000);
	}

	if (i == 10) {
		printf("%s: reset failed\n", DEVNAME(sc));
		return 1;
	}

	LII_WRITE_4(sc, LII_PHYC, PHYC_ENABLE);
	DELAY(10);

	/* Init PCI-Express module */
	/* Magic Numbers Warning */
	LII_WRITE_4(sc, 0x12fc, 0x00006500);
	LII_WRITE_4(sc, 0x1008, 0x00008000 |
	    LII_READ_4(sc, 0x1008));

	return 0;
}

int
lii_eeprom_present(struct lii_softc *sc)
{
	uint32_t val;

	val = LII_READ_4(sc, LII_SFC);
	if (val & SFC_EN_VPD)
		LII_WRITE_4(sc, LII_SFC, val & ~(SFC_EN_VPD));

	return pci_get_capability(sc->sc_pc, sc->sc_tag, PCI_CAP_VPD,
	    NULL, NULL) == 1;
}

int
lii_eeprom_read(struct lii_softc *sc, uint32_t reg, uint32_t *val)
{
	return pci_vpd_read(sc->sc_pc, sc->sc_tag, reg, 1, (pcireg_t *)val);
}

void
lii_spi_configure(struct lii_softc *sc)
{
	/*
	 * We don't offer a way to configure the SPI Flash vendor parameter, so
	 * the table is given for reference
	 */
	static const struct lii_spi_flash_vendor {
	    const char *sfv_name;
	    const uint8_t sfv_opcodes[9];
	} lii_sfv[] = {
	    { "Atmel", { 0x00, 0x03, 0x02, 0x06, 0x04, 0x05, 0x15, 0x52, 0x62 } },
	    { "SST",   { 0x01, 0x03, 0x02, 0x06, 0x04, 0x05, 0x90, 0x20, 0x60 } },
	    { "ST",    { 0x01, 0x03, 0x02, 0x06, 0x04, 0x05, 0xab, 0xd8, 0xc7 } },
	};
#define SF_OPCODE_WRSR	0
#define SF_OPCODE_READ	1
#define SF_OPCODE_PRGM	2
#define SF_OPCODE_WREN	3
#define SF_OPCODE_WRDI	4
#define SF_OPCODE_RDSR	5
#define SF_OPCODE_RDID	6
#define SF_OPCODE_SECT_ER	7
#define SF_OPCODE_CHIP_ER	8

#define SF_DEFAULT_VENDOR	0
	static const uint8_t vendor = SF_DEFAULT_VENDOR;

	/*
	 * Why isn't WRDI used?  Heck if I know.
	 */

	LII_WRITE_1(sc, LII_SFOP_WRSR,
	    lii_sfv[vendor].sfv_opcodes[SF_OPCODE_WRSR]);
	LII_WRITE_1(sc, LII_SFOP_READ,
	    lii_sfv[vendor].sfv_opcodes[SF_OPCODE_READ]);
	LII_WRITE_1(sc, LII_SFOP_PROGRAM,
	    lii_sfv[vendor].sfv_opcodes[SF_OPCODE_PRGM]);
	LII_WRITE_1(sc, LII_SFOP_WREN,
	    lii_sfv[vendor].sfv_opcodes[SF_OPCODE_WREN]);
	LII_WRITE_1(sc, LII_SFOP_RDSR,
	    lii_sfv[vendor].sfv_opcodes[SF_OPCODE_RDSR]);
	LII_WRITE_1(sc, LII_SFOP_RDID,
	    lii_sfv[vendor].sfv_opcodes[SF_OPCODE_RDID]);
	LII_WRITE_1(sc, LII_SFOP_SC_ERASE,
	    lii_sfv[vendor].sfv_opcodes[SF_OPCODE_SECT_ER]);
	LII_WRITE_1(sc, LII_SFOP_CHIP_ERASE,
	    lii_sfv[vendor].sfv_opcodes[SF_OPCODE_CHIP_ER]);
}

#define MAKE_SFC(cssetup, clkhi, clklo, cshold, cshi, ins) \
    ( (((cssetup) & SFC_CS_SETUP_MASK)	\
        << SFC_CS_SETUP_SHIFT)		\
    | (((clkhi) & SFC_CLK_HI_MASK)	\
        << SFC_CLK_HI_SHIFT)		\
    | (((clklo) & SFC_CLK_LO_MASK)	\
        << SFC_CLK_LO_SHIFT)		\
    | (((cshold) & SFC_CS_HOLD_MASK)	\
        << SFC_CS_HOLD_SHIFT)		\
    | (((cshi) & SFC_CS_HI_MASK)	\
        << SFC_CS_HI_SHIFT)		\
    | (((ins) & SFC_INS_MASK)		\
        << SFC_INS_SHIFT))

#define CUSTOM_SPI_CS_SETUP	2
#define CUSTOM_SPI_CLK_HI	2
#define CUSTOM_SPI_CLK_LO	2
#define CUSTOM_SPI_CS_HOLD	2
#define CUSTOM_SPI_CS_HI	3

int
lii_spi_read(struct lii_softc *sc, uint32_t reg, uint32_t *val)
{
	uint32_t v;
	int i;

	LII_WRITE_4(sc, LII_SF_DATA, 0);
	LII_WRITE_4(sc, LII_SF_ADDR, reg);

	v = SFC_WAIT_READY |
	    MAKE_SFC(CUSTOM_SPI_CS_SETUP, CUSTOM_SPI_CLK_HI,
	         CUSTOM_SPI_CLK_LO, CUSTOM_SPI_CS_HOLD, CUSTOM_SPI_CS_HI, 1);

	LII_WRITE_4(sc, LII_SFC, v);
	v |= SFC_START;
	LII_WRITE_4(sc, LII_SFC, v);

	for (i = 0; i < 10; ++i) {
		DELAY(1000);
		if (!(LII_READ_4(sc, LII_SFC) & SFC_START))
			break;
	}
	if (i == 10)
		return EBUSY;

	*val = LII_READ_4(sc, LII_SF_DATA);
	return 0;
}

void
lii_read_macaddr(struct lii_softc *sc, uint8_t *ea)
{
	uint32_t offset = 0x100;
	uint32_t val, val1, addr0 = 0, addr1 = 0;
	uint8_t found = 0;

	while ((*sc->sc_memread)(sc, offset, &val) == 0) {
		offset += 4;

		/* Each chunk of data starts with a signature */
		if ((val & 0xff) != 0x5a)
			break;
		if ((*sc->sc_memread)(sc, offset, &val1))
			break;

		offset += 4;

		val >>= 16;
		switch (val) {
		case LII_MAC_ADDR_0:
			addr0 = val1;
			++found;
			break;
		case LII_MAC_ADDR_1:
			addr1 = val1;
			++found;
			break;
		default:
			continue;
		}
	}

#ifdef LII_DEBUG
	if (found < 2)
		printf(": error reading MAC address, using registers...\n");
#endif

	addr0 = htole32(addr0);
	addr1 = htole32(addr1);

	if ((addr0 == 0xffffff && (addr1 & 0xffff) == 0xffff) ||
	    (addr0 == 0 && (addr1 & 0xffff) == 0)) {
		addr0 = htole32(LII_READ_4(sc, LII_MAC_ADDR_0));
		addr1 = htole32(LII_READ_4(sc, LII_MAC_ADDR_1));
	}

	ea[0] = (addr1 & 0x0000ff00) >> 8;
	ea[1] = (addr1 & 0x000000ff);
	ea[2] = (addr0 & 0xff000000) >> 24;
	ea[3] = (addr0 & 0x00ff0000) >> 16;
	ea[4] = (addr0 & 0x0000ff00) >> 8;
	ea[5] = (addr0 & 0x000000ff);
}

int
lii_mii_readreg(struct device *dev, int phy, int reg)
{
	struct lii_softc *sc = (struct lii_softc *)dev;
	uint32_t val;
	int i;

	val = (reg & MDIOC_REG_MASK) << MDIOC_REG_SHIFT;

	val |= MDIOC_START | MDIOC_SUP_PREAMBLE;
	val |= MDIOC_CLK_25_4 << MDIOC_CLK_SEL_SHIFT;

	val |= MDIOC_READ;

	LII_WRITE_4(sc, LII_MDIOC, val);

	for (i = 0; i < MDIO_WAIT_TIMES; ++i) {
		DELAY(2);
		val = LII_READ_4(sc, LII_MDIOC);
		if ((val & (MDIOC_START | MDIOC_BUSY)) == 0)
			break;
	}

	if (i == MDIO_WAIT_TIMES) {
		printf("%s: timeout reading PHY %d reg %d\n", DEVNAME(sc), phy,
		    reg);
	}

	return (val & 0x0000ffff);
}

void
lii_mii_writereg(struct device *dev, int phy, int reg, int data)
{
	struct lii_softc *sc = (struct lii_softc *)dev;
	uint32_t val;
	int i;

	val = (reg & MDIOC_REG_MASK) << MDIOC_REG_SHIFT;
	val |= (data & MDIOC_DATA_MASK) << MDIOC_DATA_SHIFT;

	val |= MDIOC_START | MDIOC_SUP_PREAMBLE;
	val |= MDIOC_CLK_25_4 << MDIOC_CLK_SEL_SHIFT;

	/* val |= MDIOC_WRITE; */

	LII_WRITE_4(sc, LII_MDIOC, val);

	for (i = 0; i < MDIO_WAIT_TIMES; ++i) {
		DELAY(2);
		val = LII_READ_4(sc, LII_MDIOC);
		if ((val & (MDIOC_START | MDIOC_BUSY)) == 0)
			break;
	}

	if (i == MDIO_WAIT_TIMES) {
		printf("%s: timeout writing PHY %d reg %d\n", DEVNAME(sc), phy,
		    reg);
	}
}

void
lii_mii_statchg(struct device *dev)
{
	struct lii_softc *sc = (struct lii_softc *)dev;
	uint32_t val;

	DPRINTF(("lii_mii_statchg\n"));

	val = LII_READ_4(sc, LII_MACC);

	if ((sc->sc_mii.mii_media_active & IFM_GMASK) == IFM_FDX)
		val |= MACC_FDX;
	else
		val &= ~MACC_FDX;

	LII_WRITE_4(sc, LII_MACC, val);
}

int
lii_media_change(struct ifnet *ifp)
{
	struct lii_softc *sc = ifp->if_softc;

	DPRINTF(("lii_media_change\n"));

	if (ifp->if_flags & IFF_UP)
		mii_mediachg(&sc->sc_mii);
	return 0;
}

void
lii_media_status(struct ifnet *ifp, struct ifmediareq *imr)
{
	struct lii_softc *sc = ifp->if_softc;

	DPRINTF(("lii_media_status\n"));

	mii_pollstat(&sc->sc_mii);
	imr->ifm_status = sc->sc_mii.mii_media_status;
	imr->ifm_active = sc->sc_mii.mii_media_active;
}

int
lii_init(struct ifnet *ifp)
{
	struct lii_softc *sc = ifp->if_softc;
	uint32_t val;
	int error;

	DPRINTF(("lii_init\n"));

	lii_stop(ifp);

	memset(sc->sc_ring, 0, sc->sc_ringsize);

	/* Disable all interrupts */
	LII_WRITE_4(sc, LII_ISR, 0xffffffff);

	LII_WRITE_4(sc, LII_DESC_BASE_ADDR_HI, 0);
/* XXX
	    sc->sc_ringmap->dm_segs[0].ds_addr >> 32);
*/
	LII_WRITE_4(sc, LII_RXD_BASE_ADDR_LO,
	    (sc->sc_ringmap->dm_segs[0].ds_addr & 0xffffffff)
	    + AT_RXD_PADDING);
	LII_WRITE_4(sc, LII_TXS_BASE_ADDR_LO,
	    sc->sc_txsp & 0xffffffff);
	LII_WRITE_4(sc, LII_TXD_BASE_ADDR_LO,
	    sc->sc_txdp & 0xffffffff);

	LII_WRITE_2(sc, LII_TXD_BUFFER_SIZE, AT_TXD_BUFFER_SIZE / 4);
	LII_WRITE_2(sc, LII_TXS_NUM_ENTRIES, AT_TXD_NUM);
	LII_WRITE_2(sc, LII_RXD_NUM_ENTRIES, AT_RXD_NUM);

	/*
	 * Inter Packet Gap Time = 0x60 (IPGT)
	 * Minimum inter-frame gap for RX = 0x50 (MIFG)
	 * 64-bit Carrier-Sense window = 0x40 (IPGR1)
	 * 96-bit IPG window = 0x60 (IPGR2)
	 */
	LII_WRITE_4(sc, LII_MIPFG, 0x60405060);

	/*
	 * Collision window = 0x37 (LCOL)
	 * Maximum # of retrans = 0xf (RETRY)
	 * Maximum binary expansion # = 0xa (ABEBT)
	 * IPG to start jam = 0x7 (JAMIPG)
	*/
	LII_WRITE_4(sc, LII_MHDC, 0x07a0f037 |
	     MHDC_EXC_DEF_EN);

	/* 100 means 200us */
	LII_WRITE_2(sc, LII_IMTIV, 100);
	LII_WRITE_2(sc, LII_SMC, SMC_ITIMER_EN);

	/* 500000 means 100ms */
	LII_WRITE_2(sc, LII_IALTIV, 50000);

	LII_WRITE_4(sc, LII_MTU, ETHER_MAX_LEN + ETHER_VLAN_ENCAP_LEN);

	/* unit unknown for TX cur-through threshold */
	LII_WRITE_4(sc, LII_TX_CUT_THRESH, 0x177);

	LII_WRITE_2(sc, LII_PAUSE_ON_TH, AT_RXD_NUM * 7 / 8);
	LII_WRITE_2(sc, LII_PAUSE_OFF_TH, AT_RXD_NUM / 12);

	sc->sc_rxcur = 0;
	sc->sc_txs_cur = sc->sc_txs_ack = 0;
	sc->sc_txd_cur = sc->sc_txd_ack = 0;
	sc->sc_free_tx_slots = 1;
	LII_WRITE_2(sc, LII_MB_TXD_WR_IDX, sc->sc_txd_cur);
	LII_WRITE_2(sc, LII_MB_RXD_RD_IDX, sc->sc_rxcur);

	LII_WRITE_1(sc, LII_DMAR, DMAR_EN);
	LII_WRITE_1(sc, LII_DMAW, DMAW_EN);

	LII_WRITE_4(sc, LII_SMC, LII_READ_4(sc, LII_SMC) | SMC_MANUAL_INT);

	error = ((LII_READ_4(sc, LII_ISR) & ISR_PHY_LINKDOWN) != 0);
	LII_WRITE_4(sc, LII_ISR, 0x3fffffff);
	LII_WRITE_4(sc, LII_ISR, 0);
	if (error) {
		printf("%s: init failed\n", DEVNAME(sc));
		goto out;
	}

	/*
	 * Initialise MAC. 
	 */
	val = LII_READ_4(sc, LII_MACC) & MACC_FDX;

	val |= MACC_RX_EN | MACC_TX_EN | MACC_MACLP_CLK_PHY |
	    MACC_TX_FLOW_EN | MACC_RX_FLOW_EN | MACC_ADD_CRC |
	    MACC_PAD;

	val |= 7 << MACC_PREAMBLE_LEN_SHIFT;
	val |= 2 << MACC_HDX_LEFT_BUF_SHIFT;

	LII_WRITE_4(sc, LII_MACC, val);

	/* Set the hardware MAC address. */
	LII_WRITE_4(sc, LII_MAC_ADDR_0, letoh32((sc->sc_ac.ac_enaddr[2] << 24) |
	    (sc->sc_ac.ac_enaddr[3] << 16) | (sc->sc_ac.ac_enaddr[4] << 8) |
	    sc->sc_ac.ac_enaddr[5]));
	LII_WRITE_4(sc, LII_MAC_ADDR_1,
	    letoh32((sc->sc_ac.ac_enaddr[0] << 8) | sc->sc_ac.ac_enaddr[1]));

	/* Program promiscuous mode and multicast filters. */
	lii_iff(sc);

	mii_mediachg(&sc->sc_mii);

	LII_WRITE_4(sc, LII_IMR, IMR_NORMAL_MASK);

	timeout_add_sec(&sc->sc_tick, 1);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

out:
	return error;
}

void
lii_tx_put(struct lii_softc *sc, struct mbuf *m)
{
	int left;
	struct tx_pkt_header *tph =
	    (struct tx_pkt_header *)(sc->sc_txdbase + sc->sc_txd_cur);

	memset(tph, 0, sizeof *tph);
	tph->txph_size = m->m_pkthdr.len;

	sc->sc_txd_cur = (sc->sc_txd_cur + 4) % AT_TXD_BUFFER_SIZE;

	/*
	 * We already know we have enough space, so if there is a part of the
	 * space ahead of txd_cur that is active, it doesn't matter because
	 * left will be large enough even without it.
	 */
	left  = AT_TXD_BUFFER_SIZE - sc->sc_txd_cur;

	if (left > m->m_pkthdr.len) {
		m_copydata(m, 0, m->m_pkthdr.len,
		    sc->sc_txdbase + sc->sc_txd_cur);
		sc->sc_txd_cur += m->m_pkthdr.len;
	} else {
		m_copydata(m, 0, left, sc->sc_txdbase + sc->sc_txd_cur);
		m_copydata(m, left, m->m_pkthdr.len - left, sc->sc_txdbase);
		sc->sc_txd_cur = m->m_pkthdr.len - left;
	}

	/* Round to a 32-bit boundary */
	sc->sc_txd_cur = ((sc->sc_txd_cur + 3) & ~3) % AT_TXD_BUFFER_SIZE;
	if (sc->sc_txd_cur == sc->sc_txd_ack)
		sc->sc_free_tx_slots = 0;
}

int
lii_free_tx_space(struct lii_softc *sc)
{
	int space;

	if (sc->sc_txd_cur >= sc->sc_txd_ack)
		space = (AT_TXD_BUFFER_SIZE - sc->sc_txd_cur) +
		    sc->sc_txd_ack;
	else
		space = sc->sc_txd_ack - sc->sc_txd_cur;

	/* Account for the tx_pkt_header */
	return (space - 4);
}

void
lii_start(struct ifnet *ifp)
{
	struct lii_softc *sc = ifp->if_softc;
	struct mbuf *m0;

	DPRINTF(("lii_start\n"));

	if (!(ifp->if_flags & IFF_RUNNING) || ifq_is_oactive(&ifp->if_snd))
		return;

	for (;;) {
		m0 = ifq_deq_begin(&ifp->if_snd);
		if (m0 == NULL)
			break;

		if (!sc->sc_free_tx_slots ||
		    lii_free_tx_space(sc) < m0->m_pkthdr.len) {
			ifq_deq_rollback(&ifp->if_snd, m0);
			ifq_set_oactive(&ifp->if_snd);
			break;
		}

		lii_tx_put(sc, m0);

		DPRINTF(("lii_start: put %d\n", sc->sc_txs_cur));

		sc->sc_txs[sc->sc_txs_cur].txps_update = 0;
		sc->sc_txs_cur = (sc->sc_txs_cur + 1) % AT_TXD_NUM;
		if (sc->sc_txs_cur == sc->sc_txs_ack)
			sc->sc_free_tx_slots = 0;

		LII_WRITE_2(sc, LII_MB_TXD_WR_IDX, sc->sc_txd_cur/4);

		ifq_deq_commit(&ifp->if_snd, m0);

#if NBPFILTER > 0
		if (ifp->if_bpf != NULL)
			bpf_mtap(ifp->if_bpf, m0, BPF_DIRECTION_OUT);
#endif
		m_freem(m0);
	}
}

void
lii_stop(struct ifnet *ifp)
{
	struct lii_softc *sc = ifp->if_softc;

	timeout_del(&sc->sc_tick);

	ifp->if_timer = 0;
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	mii_down(&sc->sc_mii);

	lii_reset(sc);

	LII_WRITE_4(sc, LII_IMR, 0);
}

int
lii_intr(void *v)
{
	struct lii_softc *sc = v;
	uint32_t status;

	status = LII_READ_4(sc, LII_ISR);
	if (status == 0)
		return 0;

	DPRINTF(("lii_intr (%x)\n", status));

	/* Clear the interrupt and disable them */
	LII_WRITE_4(sc, LII_ISR, status | ISR_DIS_INT);

	if (status & (ISR_PHY | ISR_MANUAL)) {
		/* Ack PHY interrupt.  Magic register */
		if (status & ISR_PHY)
			(void)lii_mii_readreg(&sc->sc_dev, 1, 19);
		mii_mediachg(&sc->sc_mii);
	}

	if (status & (ISR_DMAR_TO_RST | ISR_DMAW_TO_RST | ISR_PHY_LINKDOWN)) {
		lii_init(&sc->sc_ac.ac_if);
		return 1;
	}

	if (status & ISR_RX_EVENT) {
#ifdef LII_DEBUG
		if (!(status & ISR_RS_UPDATE))
			printf("rxintr %08x\n", status);
#endif
		lii_rxintr(sc);
	}

	if (status & ISR_TX_EVENT)
		lii_txintr(sc);

	/* Re-enable interrupts */
	LII_WRITE_4(sc, LII_ISR, 0);

	return 1;
}

void
lii_rxintr(struct lii_softc *sc)
{
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct rx_pkt *rxp;
	struct mbuf *m;
	uint16_t size;

	DPRINTF(("lii_rxintr\n"));

	for (;;) {
		rxp = &sc->sc_rxp[sc->sc_rxcur];
		if (rxp->rxp_update == 0)
			break;

		DPRINTF(("lii_rxintr: getting %u (%u) [%x]\n", sc->sc_rxcur,
		    rxp->rxp_size, rxp->rxp_flags));
		sc->sc_rxcur = (sc->sc_rxcur + 1) % AT_RXD_NUM;
		rxp->rxp_update = 0;
		if (!(rxp->rxp_flags & LII_RXF_SUCCESS)) {
			++ifp->if_ierrors;
			continue;
		}

		MGETHDR(m, M_DONTWAIT, MT_DATA);
		if (m == NULL) {
			++ifp->if_ierrors;
			continue;
		}
		size = rxp->rxp_size - ETHER_CRC_LEN;
		if (size > MHLEN) {
			MCLGET(m, M_DONTWAIT);
			if ((m->m_flags & M_EXT) == 0) {
				m_freem(m);
				++ifp->if_ierrors;
				continue;
			}
		}

		/* Copy the packet without the FCS */
		m->m_pkthdr.len = m->m_len = size;
		memcpy(mtod(m, void *), &rxp->rxp_data[0], size);

		ml_enqueue(&ml, m);
	}

	if_input(ifp, &ml);

	LII_WRITE_4(sc, LII_MB_RXD_RD_IDX, sc->sc_rxcur);
}

void
lii_txintr(struct lii_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct tx_pkt_status *txs;
	struct tx_pkt_header *txph;

	DPRINTF(("lii_txintr\n"));

	for (;;) {
		txs = &sc->sc_txs[sc->sc_txs_ack];
		if (txs->txps_update == 0)
			break;
		DPRINTF(("lii_txintr: ack'd %d\n", sc->sc_txs_ack));
		sc->sc_txs_ack = (sc->sc_txs_ack + 1) % AT_TXD_NUM;
		sc->sc_free_tx_slots = 1;

		txs->txps_update = 0;

		txph =  (struct tx_pkt_header *)
		    (sc->sc_txdbase + sc->sc_txd_ack);

		if (txph->txph_size != txs->txps_size) {
			printf("%s: mismatched status and packet\n",
			    DEVNAME(sc));
		}

		/*
		 * Move ack by the packet size, taking the packet header in
		 * account and round to the next 32-bit boundary
		 * (7 = sizeof(header) + 3)
		 */
		sc->sc_txd_ack = (sc->sc_txd_ack + txph->txph_size + 7 ) & ~3;
		sc->sc_txd_ack %= AT_TXD_BUFFER_SIZE;

		if (!ISSET(txs->txps_flags, LII_TXF_SUCCESS))
			++ifp->if_oerrors;
		ifq_clr_oactive(&ifp->if_snd);
	}

	if (sc->sc_free_tx_slots)
		lii_start(ifp);
}

int
lii_alloc_rings(struct lii_softc *sc)
{
	int nsegs;
	bus_size_t bs;

	/*
	 * We need a big chunk of DMA-friendly memory because descriptors
	 * are not separate from data on that crappy hardware, which means
	 * we'll have to copy data from and to that memory zone to and from
	 * the mbufs.
	 *
	 * How lame is that?  Using the default values from the Linux driver,
	 * we allocate space for receiving up to 64 full-size Ethernet frames,
	 * and only 8kb for transmitting up to 64 Ethernet frames.
	 */

	sc->sc_ringsize = bs = AT_RXD_PADDING
	    + AT_RXD_NUM * sizeof(struct rx_pkt)
	    + AT_TXD_NUM * sizeof(struct tx_pkt_status)
	    + AT_TXD_BUFFER_SIZE;

	if (bus_dmamap_create(sc->sc_dmat, bs, 1, bs, (1<<30),
	    BUS_DMA_NOWAIT, &sc->sc_ringmap) != 0) {
		printf(": failed to create DMA map\n");
		return 1;
	}

	if (bus_dmamem_alloc(sc->sc_dmat, bs, PAGE_SIZE, (1<<30),
	    &sc->sc_ringseg, 1, &nsegs, BUS_DMA_NOWAIT) != 0) {
		printf(": failed to allocate DMA memory\n");
		goto destroy;
	}

	if (bus_dmamem_map(sc->sc_dmat, &sc->sc_ringseg, nsegs, bs,
	    (caddr_t *)&sc->sc_ring, BUS_DMA_NOWAIT) != 0) {
		printf(": failed to map DMA memory\n");
		goto free;
	}

	if (bus_dmamap_load(sc->sc_dmat, sc->sc_ringmap, sc->sc_ring,
	    bs, NULL, BUS_DMA_NOWAIT) != 0) {
		printf(": failed to load DMA memory\n");
		goto unmap;
	}

	sc->sc_rxp = (void *)(sc->sc_ring + AT_RXD_PADDING);
	sc->sc_txs = (void *)(sc->sc_ring + AT_RXD_PADDING
	    + AT_RXD_NUM * sizeof(struct rx_pkt));
	sc->sc_txdbase = ((char *)sc->sc_txs)
	    + AT_TXD_NUM * sizeof(struct tx_pkt_status);
	sc->sc_txsp = sc->sc_ringmap->dm_segs[0].ds_addr
	    + ((char *)sc->sc_txs - (char *)sc->sc_ring);
	sc->sc_txdp = sc->sc_ringmap->dm_segs[0].ds_addr
	    + ((char *)sc->sc_txdbase - (char *)sc->sc_ring);

	return 0;

unmap:
	bus_dmamem_unmap(sc->sc_dmat, sc->sc_ring, bs);
free:
	bus_dmamem_free(sc->sc_dmat, &sc->sc_ringseg, nsegs);
destroy:
	bus_dmamap_destroy(sc->sc_dmat, sc->sc_ringmap);
	return 1;
}

void
lii_watchdog(struct ifnet *ifp)
{
	struct lii_softc *sc = ifp->if_softc;

	printf("%s: watchdog timeout\n", DEVNAME(sc));
	++ifp->if_oerrors;
	lii_init(ifp);
}

int
lii_ioctl(struct ifnet *ifp, u_long cmd, caddr_t addr)
{
	struct lii_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)addr;
	int s, error = 0;

	s = splnet();

	switch(cmd) {
	case SIOCSIFADDR:
		SET(ifp->if_flags, IFF_UP);
		/* FALLTHROUGH */

	case SIOCSIFFLAGS:
		if (ISSET(ifp->if_flags, IFF_UP)) {
			if (ISSET(ifp->if_flags, IFF_RUNNING))
				error = ENETRESET;
			else
				lii_init(ifp);
		} else {
			if (ISSET(ifp->if_flags, IFF_RUNNING))
				lii_stop(ifp);
		}
		break;

	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_mii.mii_media, cmd);
		break;

	default:
		error = ether_ioctl(ifp, &sc->sc_ac, cmd, addr);
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			lii_iff(sc);
		error = 0;
	}

	splx(s);
	return error;
}

void
lii_iff(struct lii_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct arpcom *ac = &sc->sc_ac;
	struct ether_multi *enm;
	struct ether_multistep step;
	uint32_t hashes[2];
	uint32_t crc, val;

	val = LII_READ_4(sc, LII_MACC);
	val &= ~(MACC_ALLMULTI_EN | MACC_BCAST_EN | MACC_PROMISC_EN);
	ifp->if_flags &= ~IFF_ALLMULTI;

	/*
	 * Always accept broadcast frames.
	 */
	val |= MACC_BCAST_EN;

	if (ifp->if_flags & IFF_PROMISC || ac->ac_multirangecnt > 0) {
		ifp->if_flags |= IFF_ALLMULTI;
		if (ifp->if_flags & IFF_PROMISC)
			val |= MACC_PROMISC_EN;
		else
			val |= MACC_ALLMULTI_EN;
		hashes[0] = hashes[1] = 0xFFFFFFFF;
	} else {
		/* Program new filter. */
		bzero(hashes, sizeof(hashes));

		ETHER_FIRST_MULTI(step, ac, enm);
		while (enm != NULL) {
			crc = ether_crc32_be(enm->enm_addrlo,
			    ETHER_ADDR_LEN);

			hashes[((crc >> 31) & 0x1)] |=
			    (1 << ((crc >> 26) & 0x1f));

			ETHER_NEXT_MULTI(step, enm);
		}
	}

	LII_WRITE_4(sc, LII_MHT, hashes[0]);
	LII_WRITE_4(sc, LII_MHT + 4, hashes[1]);
	LII_WRITE_4(sc, LII_MACC, val);
}

void
lii_tick(void *v)
{
	struct lii_softc *sc = v;
	int s;

	s = splnet();
	mii_tick(&sc->sc_mii);
	splx(s);

	timeout_add_sec(&sc->sc_tick, 1);
}
