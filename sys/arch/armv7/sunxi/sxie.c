/*	$OpenBSD: sxie.c,v 1.34 2023/11/10 15:51:19 bluhm Exp $	*/
/*
 * Copyright (c) 2012-2013 Patrick Wildt <patrick@blueri.se>
 * Copyright (c) 2013 Artturi Alm
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* TODO this should use dedicated dma for RX, at least */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/queue.h>
#include <sys/device.h>
#include <sys/mbuf.h>
#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include "bpfilter.h"

#include <net/if.h>
#include <net/if_media.h>
#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <dev/mii/miivar.h>

#include <dev/fdt/sunxireg.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/ofw_regulator.h>
#include <dev/ofw/fdt.h>

/* configuration registers */
#define	SXIE_CR			0x0000
#define	SXIE_TXMODE		0x0004
#define	SXIE_TXFLOW		0x0008
#define	SXIE_TXCR0		0x000c
#define	SXIE_TXCR1		0x0010
#define	SXIE_TXINS		0x0014
#define	SXIE_TXPKTLEN0		0x0018
#define	SXIE_TXPKTLEN1		0x001c
#define	SXIE_TXSR		0x0020
#define	SXIE_TXIO0		0x0024
#define	SXIE_TXIO1		0x0028
#define	SXIE_TXTSVL0		0x002c
#define	SXIE_TXTSVH0		0x0030
#define	SXIE_TXTSVL1		0x0034
#define	SXIE_TXTSVH1		0x0038
#define	SXIE_RXCR		0x003c
#define	SXIE_RXHASH0		0x0040
#define	SXIE_RXHASH1		0x0044
#define	SXIE_RXSR		0x0048
#define	SXIE_RXIO		0x004C
#define	SXIE_RXFBC		0x0050
#define	SXIE_INTCR		0x0054
#define	SXIE_INTSR		0x0058
#define	SXIE_MACCR0		0x005C
#define	SXIE_MACCR1		0x0060
#define	SXIE_MACIPGT		0x0064
#define	SXIE_MACIPGR		0x0068
#define	SXIE_MACCLRT		0x006C
#define	SXIE_MACMFL		0x0070
#define	SXIE_MACSUPP		0x0074
#define	SXIE_MACTEST		0x0078
#define	SXIE_MACMCFG		0x007C
#define	SXIE_MACMCMD		0x0080
#define	SXIE_MACMADR		0x0084
#define	SXIE_MACMWTD		0x0088
#define	SXIE_MACMRDD		0x008C
#define	SXIE_MACMIND		0x0090
#define	SXIE_MACSSRR		0x0094
#define	SXIE_MACA0		0x0098
#define	SXIE_MACA1		0x009c
#define	SXIE_MACA2		0x00a0

/* i once spent hours on pretty defines, cvs up ate 'em. these shall do */
#define SXIE_INTR_ENABLE		0x010f
#define SXIE_INTR_DISABLE	0x0000
#define SXIE_INTR_CLEAR		0x0000

#define SXIE_TX_FIFO0		0x0001
#define SXIE_TX_FIFO1		0x0002

#define	SXIE_RX_ENABLE		0x0004
#define	SXIE_TX_ENABLE		0x0003
#define	SXIE_RXTX_ENABLE		0x0007

#define	SXIE_RXDRQM		0x0002
#define	SXIE_RXTM		0x0004
#define	SXIE_RXFLUSH		0x0008
#define	SXIE_RXPA		0x0010
#define	SXIE_RXPCF		0x0020
#define	SXIE_RXPCRCE		0x0040
#define	SXIE_RXPLE		0x0080
#define	SXIE_RXPOR		0x0100
#define	SXIE_RXUCAD		0x10000
#define	SXIE_RXDAF		0x20000
#define	SXIE_RXMCO		0x100000
#define	SXIE_RXMHF		0x200000
#define	SXIE_RXBCO		0x400000
#define	SXIE_RXSAF		0x1000000
#define	SXIE_RXSAIF		0x2000000

#define	SXIE_MACRXFC		0x0004
#define	SXIE_MACTXFC		0x0008
#define SXIE_MACSOFTRESET	0x8000

#define	SXIE_MACDUPLEX		0x0001	/* full = 1 */
#define	SXIE_MACFLC		0x0002
#define	SXIE_MACHF		0x0004
#define	SXIE_MACDCRC		0x0008
#define	SXIE_MACCRC		0x0010
#define	SXIE_MACPC		0x0020
#define	SXIE_MACVC		0x0040
#define	SXIE_MACADP		0x0080
#define	SXIE_MACPRE		0x0100
#define	SXIE_MACLPE		0x0200
#define	SXIE_MACNB		0x1000
#define	SXIE_MACBNB		0x2000
#define	SXIE_MACED		0x4000

#define	SXIE_RX_ERRLENOOR	0x0040
#define	SXIE_RX_ERRLENCHK	0x0020
#define	SXIE_RX_ERRCRC		0x0010
#define	SXIE_RX_ERRRCV		0x0008 /* XXX receive code violation ? */
#define	SXIE_RX_ERRMASK		0x0070

#define	SXIE_MII_TIMEOUT	100
#define SXIE_MAX_RXD		8
#define SXIE_MAX_PKT_SIZE	ETHER_MAX_DIX_LEN

#define SXIE_ROUNDUP(size, unit) (((size) + (unit) - 1) & ~((unit) - 1))

struct sxie_softc {
	struct device			sc_dev;
	struct arpcom			sc_ac;
	struct mii_data			sc_mii;
	int				sc_phyno;
	bus_space_tag_t			sc_iot;
	bus_space_handle_t		sc_ioh;
	bus_space_handle_t		sc_sid_ioh;
	void				*sc_ih; /* Interrupt handler */
	uint32_t			intr_status; /* soft interrupt status */
	uint32_t			pauseframe;
	uint32_t			txf_inuse;
};

struct sxie_softc *sxie_sc;

int	sxie_match(struct device *, void *, void *);
void	sxie_attach(struct device *, struct device *, void *);
void	sxie_setup_interface(struct sxie_softc *, struct device *);
void	sxie_socware_init(struct sxie_softc *);
int	sxie_ioctl(struct ifnet *, u_long, caddr_t);
void	sxie_start(struct ifnet *);
void	sxie_watchdog(struct ifnet *);
void	sxie_init(struct sxie_softc *);
void	sxie_stop(struct sxie_softc *);
void	sxie_reset(struct sxie_softc *);
void	sxie_iff(struct sxie_softc *, struct ifnet *);
int	sxie_intr(void *);
void	sxie_recv(struct sxie_softc *);
int	sxie_miibus_readreg(struct device *, int, int);
void	sxie_miibus_writereg(struct device *, int, int, int);
void	sxie_miibus_statchg(struct device *);
int	sxie_ifm_change(struct ifnet *);
void	sxie_ifm_status(struct ifnet *, struct ifmediareq *);

const struct cfattach sxie_ca = {
	sizeof (struct sxie_softc), sxie_match, sxie_attach
};

struct cfdriver sxie_cd = {
	NULL, "sxie", DV_IFNET
};

int
sxie_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "allwinner,sun4i-a10-emac");
}

void
sxie_attach(struct device *parent, struct device *self, void *aux)
{
	struct sxie_softc *sc = (struct sxie_softc *) self;
	struct fdt_attach_args *faa = aux;
	struct mii_data *mii;
	struct ifnet *ifp;
	int phy, node, phy_supply, phyloc = MII_PHY_ANY;
	int s;

	if (faa->fa_nreg < 1)
		return;

	pinctrl_byname(faa->fa_node, "default");

	sc->sc_iot = faa->fa_iot;

	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh))
		panic("sxie_attach: bus_space_map ioh failed!");

	if (bus_space_map(sc->sc_iot, SID_ADDR, SID_SIZE, 0, &sc->sc_sid_ioh))
		panic("sxie_attach: bus_space_map sid_ioh failed!");

	clock_enable_all(faa->fa_node);

	/* Lookup PHY. */
	phy = OF_getpropint(faa->fa_node, "phy", 0);
	if (phy == 0)
		phy = OF_getpropint(faa->fa_node, "phy-handle", 0);
	node = OF_getnodebyphandle(phy);
	if (node) {
		phyloc = OF_getpropint(node, "reg", MII_PHY_ANY);

		/* Power up PHY. */
		phy_supply = OF_getpropint(OF_parent(node), "phy-supply", 0);
		if (phy_supply)
			regulator_enable(phy_supply);
	}
	sc->sc_phyno = phyloc == MII_PHY_ANY ? 1 : phyloc;

	sxie_socware_init(sc);
	sc->txf_inuse = 0;

	sc->sc_ih = arm_intr_establish_fdt(faa->fa_node, IPL_NET,
	    sxie_intr, sc, sc->sc_dev.dv_xname);

	s = splnet();

	printf(", address %s\n", ether_sprintf(sc->sc_ac.ac_enaddr));

	/* XXX verify flags & capabilities */
	ifp = &sc->sc_ac.ac_if;
	ifp->if_softc = sc;
	strlcpy(ifp->if_xname, sc->sc_dev.dv_xname, IFNAMSIZ);
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = sxie_ioctl;
	ifp->if_start = sxie_start;
	ifp->if_watchdog = sxie_watchdog;
	ifp->if_capabilities = IFCAP_VLAN_MTU; /* XXX status check in recv? */

	ifq_init_maxlen(&ifp->if_snd, IFQ_MAXLEN);

	/* Initialize MII/media info. */
	mii = &sc->sc_mii;
	mii->mii_ifp = ifp;
	mii->mii_readreg = sxie_miibus_readreg;
	mii->mii_writereg = sxie_miibus_writereg;
	mii->mii_statchg = sxie_miibus_statchg;

	ifmedia_init(&mii->mii_media, 0, sxie_ifm_change, sxie_ifm_status);
	mii_attach(self, mii, 0xffffffff, phyloc, MII_OFFSET_ANY, 0);

	if (LIST_FIRST(&mii->mii_phys) == NULL) {
		ifmedia_add(&mii->mii_media, IFM_ETHER | IFM_NONE, 0, NULL);
		ifmedia_set(&mii->mii_media, IFM_ETHER | IFM_NONE);
	} else
		ifmedia_set(&mii->mii_media, IFM_ETHER | IFM_AUTO);

	if_attach(ifp);
	ether_ifattach(ifp);
	splx(s);

	sxie_sc = sc;
}

void
sxie_socware_init(struct sxie_softc *sc)
{
	int have_mac = 0;
	uint32_t reg;

	/* MII clock cfg */
	SXICMS4(sc, SXIE_MACMCFG, 15 << 2, 13 << 2);

	SXIWRITE4(sc, SXIE_INTCR, SXIE_INTR_DISABLE);
	SXISET4(sc, SXIE_INTSR, SXIE_INTR_CLEAR);

	/*
	 * If u-boot doesn't set emac, use the Security ID area
	 * to generate a consistent MAC address of.
	 */
	reg = SXIREAD4(sc, SXIE_MACA0);
	if (reg != 0) {
		sc->sc_ac.ac_enaddr[3] = reg >> 16 & 0xff;
		sc->sc_ac.ac_enaddr[4] = reg >> 8 & 0xff;
		sc->sc_ac.ac_enaddr[5] = reg & 0xff;
		reg = SXIREAD4(sc, SXIE_MACA1);
		sc->sc_ac.ac_enaddr[0] = reg >> 16 & 0xff;
		sc->sc_ac.ac_enaddr[1] = reg >> 8 & 0xff;
		sc->sc_ac.ac_enaddr[2] = reg & 0xff;

		have_mac = 1;
	}

	reg = bus_space_read_4(sc->sc_iot, sc->sc_sid_ioh, 0x0);

	if (!have_mac && reg != 0) {
		sc->sc_ac.ac_enaddr[0] = 0x02;
		sc->sc_ac.ac_enaddr[1] = reg & 0xff;
		reg = bus_space_read_4(sc->sc_iot, sc->sc_sid_ioh, 0x0c);
		sc->sc_ac.ac_enaddr[2] = reg >> 24 & 0xff;
		sc->sc_ac.ac_enaddr[3] = reg >> 16 & 0xff;
		sc->sc_ac.ac_enaddr[4] = reg >> 8 & 0xff;
		sc->sc_ac.ac_enaddr[5] = reg & 0xff;

		have_mac = 1;
	}

	if (!have_mac)
		ether_fakeaddr(&sc->sc_ac.ac_if);
}

void
sxie_setup_interface(struct sxie_softc *sc, struct device *dev)
{
	uint32_t clr_m, set_m;

	/* configure TX */
	SXICMS4(sc, SXIE_TXMODE, 3, 1);	/* cpu mode */

	/* configure RX */
	clr_m = SXIE_RXDRQM | SXIE_RXTM | SXIE_RXPA | SXIE_RXPCF |
	    SXIE_RXPCRCE | SXIE_RXPLE | SXIE_RXMHF | SXIE_RXSAF |
	    SXIE_RXSAIF;
	set_m = SXIE_RXPOR | SXIE_RXUCAD | SXIE_RXDAF | SXIE_RXBCO;
	SXICMS4(sc, SXIE_RXCR, clr_m, set_m);

	/* configure MAC */
	SXISET4(sc, SXIE_MACCR0, SXIE_MACTXFC | SXIE_MACRXFC);
	clr_m =	SXIE_MACHF | SXIE_MACDCRC | SXIE_MACVC | SXIE_MACADP |
	    SXIE_MACPRE | SXIE_MACLPE | SXIE_MACNB | SXIE_MACBNB |
	    SXIE_MACED;
	set_m = SXIE_MACFLC | SXIE_MACCRC | SXIE_MACPC;
	set_m |= sxie_miibus_readreg(dev, sc->sc_phyno, 0) >> 8 & 1;
	SXICMS4(sc, SXIE_MACCR1, clr_m, set_m);

	/* XXX */
	SXIWRITE4(sc, SXIE_MACIPGT, 0x0015);
	SXIWRITE4(sc, SXIE_MACIPGR, 0x0c12);

	/* XXX set collision window */
	SXIWRITE4(sc, SXIE_MACCLRT, 0x370f);

	/* set max frame length */
	SXIWRITE4(sc, SXIE_MACMFL, SXIE_MAX_PKT_SIZE);

	/* set lladdr */
	SXIWRITE4(sc, SXIE_MACA0,
	    sc->sc_ac.ac_enaddr[3] << 16 |
	    sc->sc_ac.ac_enaddr[4] << 8 |
	    sc->sc_ac.ac_enaddr[5]);
	SXIWRITE4(sc, SXIE_MACA1,
	    sc->sc_ac.ac_enaddr[0] << 16 |
	    sc->sc_ac.ac_enaddr[1] << 8 |
	    sc->sc_ac.ac_enaddr[2]);

	sxie_reset(sc);
	/* XXX possibly missing delay in here. */
}

void
sxie_init(struct sxie_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct device *dev = (struct device *)sc;
	int phyreg;
	
	sxie_reset(sc);

	SXIWRITE4(sc, SXIE_INTCR, SXIE_INTR_DISABLE);

	SXISET4(sc, SXIE_INTSR, SXIE_INTR_CLEAR);

	SXISET4(sc, SXIE_RXCR, SXIE_RXFLUSH);

	/* soft reset */
	SXICLR4(sc, SXIE_MACCR0, SXIE_MACSOFTRESET);

	/* zero rx counter */
	SXIWRITE4(sc, SXIE_RXFBC, 0);

	sxie_setup_interface(sc, dev);

	/* power up PHY */
	sxie_miibus_writereg(dev, sc->sc_phyno, 0,
	    sxie_miibus_readreg(dev, sc->sc_phyno, 0) & ~(1 << 11));
	delay(1000);
	phyreg = sxie_miibus_readreg(dev, sc->sc_phyno, 0);

	/* set duplex */
	SXICMS4(sc, SXIE_MACCR1, 1, phyreg >> 8 & 1);

	/* set speed */
	SXICMS4(sc, SXIE_MACSUPP, 1 << 8, (phyreg >> 13 & 1) << 8);

	SXISET4(sc, SXIE_CR, SXIE_RXTX_ENABLE);

	/* Indicate we are up and running. */
	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	SXISET4(sc, SXIE_INTCR, SXIE_INTR_ENABLE);

	sxie_start(ifp);
}

int
sxie_intr(void *arg)
{
	struct sxie_softc *sc = arg;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	uint32_t pending;

	SXIWRITE4(sc, SXIE_INTCR, SXIE_INTR_DISABLE);

	pending = SXIREAD4(sc, SXIE_INTSR);
	SXIWRITE4(sc, SXIE_INTSR, pending);

	/*
	 * Handle incoming packets.
	 */
	if (pending & 0x0100) {
		if (ifp->if_flags & IFF_RUNNING)
			sxie_recv(sc);
	}

	if (pending & (SXIE_TX_FIFO0 | SXIE_TX_FIFO1)) {
		sc->txf_inuse &= ~pending;
		if (sc->txf_inuse == 0)
			ifp->if_timer = 0;
		else
			ifp->if_timer = 5;

		if (ifq_is_oactive(&ifp->if_snd))
			ifq_restart(&ifp->if_snd);
	}

	SXISET4(sc, SXIE_INTCR, SXIE_INTR_ENABLE);

	return 1;
}

/*
 * XXX there's secondary tx fifo to be used.
 */
void
sxie_start(struct ifnet *ifp)
{
	struct sxie_softc *sc = ifp->if_softc;
	struct mbuf *m;
	struct mbuf *head;
	uint8_t *td;
	uint32_t fifo;
	uint32_t txbuf[SXIE_MAX_PKT_SIZE / sizeof(uint32_t)]; /* XXX !!! */

	if (!(ifp->if_flags & IFF_RUNNING) || ifq_is_oactive(&ifp->if_snd))
		return;


	td = (uint8_t *)&txbuf[0];
	m = NULL;
	head = NULL;

	for (;;) {
		if (sc->txf_inuse == (SXIE_TX_FIFO0 | SXIE_TX_FIFO1)) {
			ifq_set_oactive(&ifp->if_snd);
			break;
		}

		m = ifq_dequeue(&ifp->if_snd);
		if (m == NULL)
			break;

		if (m->m_pkthdr.len > SXIE_MAX_PKT_SIZE) {
			m_freem(m);
			continue;
		}

#if NBPFILTER > 0
		if (ifp->if_bpf)
			bpf_mtap(ifp->if_bpf, m, BPF_DIRECTION_OUT);
#endif

		/* select fifo */
		if (sc->txf_inuse & SXIE_TX_FIFO0) {
			sc->txf_inuse |= SXIE_TX_FIFO1;
			fifo = 1;
		} else {
			sc->txf_inuse |= SXIE_TX_FIFO0;
			fifo = 0;
		}
		SXIWRITE4(sc, SXIE_TXINS, fifo);

		/* set packet length */
		SXIWRITE4(sc, SXIE_TXPKTLEN0 + (fifo * 4), m->m_pkthdr.len);

		/* copy the actual packet to fifo XXX through 'align buffer' */
		m_copydata(m, 0, m->m_pkthdr.len, td);
		bus_space_write_multi_4(sc->sc_iot, sc->sc_ioh,
		    SXIE_TXIO0,
		    (uint32_t *)td, SXIE_ROUNDUP(m->m_pkthdr.len, 4) >> 2);

		/* transmit to PHY from fifo */
		SXISET4(sc, SXIE_TXCR0 + (fifo * 4), 1);
		ifp->if_timer = 5;

		m_freem(m);
	}
}

void
sxie_stop(struct sxie_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;

	sxie_reset(sc);

	ifp->if_flags &= ~IFF_RUNNING;
	ifp->if_timer = 0;
	ifq_clr_oactive(&ifp->if_snd);
}

void
sxie_reset(struct sxie_softc *sc)
{
	/* reset the controller */
	SXIWRITE4(sc, SXIE_CR, 0);
	delay(200);
	SXIWRITE4(sc, SXIE_CR, 1);
	delay(200);
}

void
sxie_watchdog(struct ifnet *ifp)
{
	struct sxie_softc *sc = ifp->if_softc;
	if (sc->pauseframe) {
		ifp->if_timer = 5;
		return;
	}
	printf("%s: watchdog tx timeout\n", sc->sc_dev.dv_xname);
	ifp->if_oerrors++;
	sxie_init(sc);
	sxie_start(ifp);
}

/*
 * XXX DMA?
 */
void
sxie_recv(struct sxie_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	uint32_t fbc, reg;
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct mbuf *m;
	uint16_t pktstat;
	int16_t pktlen;
	int rlen;
	char rxbuf[SXIE_MAX_PKT_SIZE]; /* XXX !!! */
trynext:
	fbc = SXIREAD4(sc, SXIE_RXFBC);
	if (!fbc)
		goto done;

	/*
	 * first bit of MSB is packet valid flag,
	 * it is 'padded' with 0x43414d = "MAC"
	 */
	reg = SXIREAD4(sc, SXIE_RXIO);
	if (reg != 0x0143414d) {	/* invalid packet */
		/* disable, flush, enable */
		SXICLR4(sc, SXIE_CR, SXIE_RX_ENABLE);
		SXISET4(sc, SXIE_RXCR, SXIE_RXFLUSH);
		while (SXIREAD4(sc, SXIE_RXCR) & SXIE_RXFLUSH);
		SXISET4(sc, SXIE_CR, SXIE_RX_ENABLE);

		goto err_out;
	}
	
	reg = SXIREAD4(sc, SXIE_RXIO);
	pktstat = (uint16_t)reg >> 16;
	pktlen = (int16_t)reg; /* length of useful data */

	if (pktstat & SXIE_RX_ERRMASK || pktlen < ETHER_MIN_LEN) {
		ifp->if_ierrors++;
		goto trynext;
	}
	if (pktlen > SXIE_MAX_PKT_SIZE)
		pktlen = SXIE_MAX_PKT_SIZE; /* XXX is truncating ok? */

	/* read the actual packet from fifo XXX through 'align buffer'.. */
	if (pktlen & 3)
		rlen = SXIE_ROUNDUP(pktlen, 4);
	else
		rlen = pktlen;
	bus_space_read_multi_4(sc->sc_iot, sc->sc_ioh,
	    SXIE_RXIO, (uint32_t *)&rxbuf[0], rlen >> 2);

	m = m_devget(&rxbuf[0], pktlen, ETHER_ALIGN);
	if (m == NULL) {
		ifp->if_ierrors++;
		goto err_out;
	}

	ml_enqueue(&ml, m);
	goto trynext;
err_out:
	ifp->if_ierrors++;
done:
	if_input(ifp, &ml);
}

int
sxie_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct sxie_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int s, error = 0;

	s = splnet();

	switch (cmd) {
	case SIOCSIFADDR:
		if (!(ifp->if_flags & IFF_UP)) {
			ifp->if_flags |= IFF_UP;
			sxie_init(sc);
		}
		break;
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				error = ENETRESET;
			else
				sxie_init(sc);
		} else if (ifp->if_flags & IFF_RUNNING)
			sxie_stop(sc);
		break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_mii.mii_media, cmd);
		break;
	default:
		error = ether_ioctl(ifp, &sc->sc_ac, cmd, data);
	}
	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			sxie_iff(sc, ifp);
		error = 0;
	}

	splx(s);
	return error;
}

void
sxie_iff(struct sxie_softc *sc, struct ifnet *ifp)
{
	/* XXX set interface features */
}

/*
 * MII
 */
int
sxie_miibus_readreg(struct device *dev, int phy, int reg)
{
	struct sxie_softc *sc = (struct sxie_softc *)dev;
	int timo = SXIE_MII_TIMEOUT;

	SXIWRITE4(sc, SXIE_MACMADR, phy << 8 | reg);

	SXIWRITE4(sc, SXIE_MACMCMD, 1);
	while (SXIREAD4(sc, SXIE_MACMIND) & 1 && --timo)
		delay(10);
#ifdef DIAGNOSTIC
	if (!timo)
		printf("%s: sxie_miibus_readreg timeout.\n",
		    sc->sc_dev.dv_xname);
#endif

	SXIWRITE4(sc, SXIE_MACMCMD, 0);
	
	return SXIREAD4(sc, SXIE_MACMRDD) & 0xffff;
}

void
sxie_miibus_writereg(struct device *dev, int phy, int reg, int val)
{
	struct sxie_softc *sc = (struct sxie_softc *)dev;
	int timo = SXIE_MII_TIMEOUT;

	SXIWRITE4(sc, SXIE_MACMADR, phy << 8 | reg);

	SXIWRITE4(sc, SXIE_MACMCMD, 1);
	while (SXIREAD4(sc, SXIE_MACMIND) & 1 && --timo)
		delay(10);
#ifdef DIAGNOSTIC
	if (!timo)
		printf("%s: sxie_miibus_readreg timeout.\n",
		    sc->sc_dev.dv_xname);
#endif

	SXIWRITE4(sc, SXIE_MACMCMD, 0);

	SXIWRITE4(sc, SXIE_MACMWTD, val);
}

void
sxie_miibus_statchg(struct device *dev)
{
	/* XXX */
#if 0
	struct sxie_softc *sc = (struct sxie_softc *)dev;

	switch (IFM_SUBTYPE(sc->sc_mii.mii_media_active)) {
	case IFM_10_T:
	case IFM_100_TX:
	/*case IFM_1000_T: only on GMAC */
		break;
	default:
		break;
	}
#endif
}

int
sxie_ifm_change(struct ifnet *ifp)
{
	struct sxie_softc *sc = ifp->if_softc;
	struct mii_data *mii = &sc->sc_mii;

	if (mii->mii_instance) {
		struct mii_softc *miisc;

		LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
			mii_phy_reset(miisc);
	}
	return mii_mediachg(mii);
}

void
sxie_ifm_status(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct sxie_softc *sc = (struct sxie_softc *)ifp->if_softc;

	mii_pollstat(&sc->sc_mii);
	ifmr->ifm_active = sc->sc_mii.mii_media_active;
	ifmr->ifm_status = sc->sc_mii.mii_media_status;
}
