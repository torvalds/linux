/*	$OpenBSD: be.c,v 1.45 2024/05/13 01:15:53 jsg Exp $	*/
/*	$NetBSD: be.c,v 1.26 2001/03/20 15:39:20 pk Exp $	*/

/*-
 * Copyright (c) 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Paul Kranenburg.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 1998 Theo de Raadt and Jason L. Wright.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/timeout.h>
#include <sys/kernel.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/syslog.h>
#include <sys/device.h>
#include <sys/malloc.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <machine/bus.h>
#include <machine/intr.h>
#include <machine/autoconf.h>

#include <dev/sbus/sbusvar.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <dev/sbus/qecreg.h>
#include <dev/sbus/qecvar.h>
#include <dev/sbus/bereg.h>

struct be_softc {
	struct	device	sc_dev;
	bus_space_tag_t	sc_bustag;	/* bus & dma tags */
	bus_dma_tag_t	sc_dmatag;
	bus_dmamap_t	sc_dmamap;
	struct	arpcom sc_arpcom;
	/*struct	ifmedia sc_ifmedia;	-* interface media */
	struct mii_data	sc_mii;		/* MII media control */
#define sc_media	sc_mii.mii_media/* shorthand */
	int		sc_phys[2];	/* MII instance -> phy */

	struct timeout sc_tick_ch;

	/*
	 * Some `mii_softc' items we need to emulate MII operation
	 * for our internal transceiver.
	 */
	int		sc_mii_inst;	/* instance of internal phy */
	uint64_t	sc_mii_active;	/* currently active medium */
	int		sc_mii_ticks;	/* tick counter */
	int		sc_mii_flags;	/* phy status flags */
#define MIIF_HAVELINK	0x04000000
	int		sc_intphy_curspeed;	/* Established link speed */

	struct	qec_softc *sc_qec;	/* QEC parent */

	bus_space_handle_t	sc_qr;	/* QEC registers */
	bus_space_handle_t	sc_br;	/* BE registers */
	bus_space_handle_t	sc_cr;	/* channel registers */
	bus_space_handle_t	sc_tr;	/* transceiver registers */

	u_int	sc_rev;

	int	sc_channel;		/* channel number */
	int	sc_burst;

	struct  qec_ring	sc_rb;	/* Packet Ring Buffer */
};

int	bematch(struct device *, void *, void *);
void	beattach(struct device *, struct device *, void *);

void	beinit(struct be_softc *);
void	bestart(struct ifnet *);
void	bestop(struct be_softc *);
void	bewatchdog(struct ifnet *);
int	beioctl(struct ifnet *, u_long, caddr_t);
void	bereset(struct be_softc *);

int	beintr(void *);
int	berint(struct be_softc *);
int	betint(struct be_softc *);
int	beqint(struct be_softc *, u_int32_t);
int	beeint(struct be_softc *, u_int32_t);

static void	be_read(struct be_softc *, int, int);
static int	be_put(struct be_softc *, int, struct mbuf *);
static struct mbuf *be_get(struct be_softc *, int, int);

void	be_pal_gate(struct be_softc *, int);

/* ifmedia callbacks */
void	be_ifmedia_sts(struct ifnet *, struct ifmediareq *);
int	be_ifmedia_upd(struct ifnet *);

void	be_mcreset(struct be_softc *);

/* MII methods & callbacks */
static int	be_mii_readreg(struct device *, int, int);
static void	be_mii_writereg(struct device *, int, int, int);
static void	be_mii_statchg(struct device *);

/* MII helpers */
static void	be_mii_sync(struct be_softc *);
static void	be_mii_sendbits(struct be_softc *, int, u_int32_t, int);
static int	be_mii_reset(struct be_softc *, int);
static int	be_tcvr_read_bit(struct be_softc *, int);
static void	be_tcvr_write_bit(struct be_softc *, int, int);

void	be_tick(void *);
void	be_intphy_status(struct be_softc *);
int	be_intphy_service(struct be_softc *, struct mii_data *, int);


const struct cfattach be_ca = {
	sizeof(struct be_softc), bematch, beattach
};

struct cfdriver be_cd = {
	NULL, "be", DV_IFNET
};

int
bematch(struct device *parent, void *vcf, void *aux)
{
	struct cfdata *cf = vcf;
	struct sbus_attach_args *sa = aux;

	return (strcmp(cf->cf_driver->cd_name, sa->sa_name) == 0);
}

void
beattach(struct device *parent, struct device *self, void *aux)
{
	struct sbus_attach_args *sa = aux;
	struct qec_softc *qec = (struct qec_softc *)parent;
	struct be_softc *sc = (struct be_softc *)self;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct mii_data *mii = &sc->sc_mii;
	struct mii_softc *child;
	int node = sa->sa_node;
	bus_dma_tag_t dmatag = sa->sa_dmatag;
	bus_dma_segment_t seg;
	bus_size_t size;
	uint64_t instance;
	int rseg, error;
	u_int32_t v;
	extern void myetheraddr(u_char *);

	/* Pass on the bus tags */
	sc->sc_bustag = sa->sa_bustag;
	sc->sc_dmatag = sa->sa_dmatag;

	if (sa->sa_nreg < 3) {
		printf("%s: only %d register sets\n",
		    self->dv_xname, sa->sa_nreg);
		return;
	}

	if (sbus_bus_map(sa->sa_bustag, sa->sa_reg[0].sbr_slot,
	    (bus_addr_t)sa->sa_reg[0].sbr_offset,
	    (bus_size_t)sa->sa_reg[0].sbr_size, 0, 0, &sc->sc_cr) != 0) {
		printf("beattach: cannot map registers\n");
		return;
	}

	if (sbus_bus_map(sa->sa_bustag, sa->sa_reg[1].sbr_slot,
	    (bus_addr_t)sa->sa_reg[1].sbr_offset,
	    (bus_size_t)sa->sa_reg[1].sbr_size, 0, 0, &sc->sc_br) != 0) {
		printf("beattach: cannot map registers\n");
		return;
	}

	if (sbus_bus_map(sa->sa_bustag, sa->sa_reg[2].sbr_slot,
	    (bus_addr_t)sa->sa_reg[2].sbr_offset,
	    (bus_size_t)sa->sa_reg[2].sbr_size, 0, 0, &sc->sc_tr) != 0) {
		printf("beattach: cannot map registers\n");
		return;
	}

	sc->sc_qec = qec;
	sc->sc_qr = qec->sc_regs;

	sc->sc_rev = getpropint(node, "board-version", -1);
	printf(" rev %x", sc->sc_rev);

	bestop(sc);

	sc->sc_channel = getpropint(node, "channel#", -1);
	if (sc->sc_channel == -1)
		sc->sc_channel = 0;

	sc->sc_burst = getpropint(node, "burst-sizes", -1);
	if (sc->sc_burst == -1)
		sc->sc_burst = qec->sc_burst;

	/* Clamp at parent's burst sizes */
	sc->sc_burst &= qec->sc_burst;

	/* Establish interrupt handler */
	if (sa->sa_nintr == 0 || bus_intr_establish(sa->sa_bustag, sa->sa_pri,
	    IPL_NET, 0, beintr, sc, self->dv_xname) == NULL) {
		printf(": no interrupt established\n");
		return;
	}

	myetheraddr(sc->sc_arpcom.ac_enaddr);
	printf(" address %s\n", ether_sprintf(sc->sc_arpcom.ac_enaddr));

	/*
	 * Allocate descriptor ring and buffers.
	 */

	/* for now, allocate as many bufs as there are ring descriptors */
	sc->sc_rb.rb_ntbuf = QEC_XD_RING_MAXSIZE;
	sc->sc_rb.rb_nrbuf = QEC_XD_RING_MAXSIZE;

	size =	QEC_XD_RING_MAXSIZE * sizeof(struct qec_xd) +
		QEC_XD_RING_MAXSIZE * sizeof(struct qec_xd) +
		sc->sc_rb.rb_ntbuf * BE_PKT_BUF_SZ +
		sc->sc_rb.rb_nrbuf * BE_PKT_BUF_SZ;

	/* Get a DMA handle */
	if ((error = bus_dmamap_create(dmatag, size, 1, size, 0,
	    BUS_DMA_NOWAIT, &sc->sc_dmamap)) != 0) {
		printf("%s: DMA map create error %d\n", self->dv_xname, error);
		return;
	}

	/* Allocate DMA buffer */
	if ((error = bus_dmamem_alloc(sa->sa_dmatag, size, 0, 0,
	    &seg, 1, &rseg, BUS_DMA_NOWAIT)) != 0) {
		printf("%s: DMA buffer alloc error %d\n",
			self->dv_xname, error);
		return;
	}

	/* Map DMA memory in CPU addressable space */
	if ((error = bus_dmamem_map(sa->sa_dmatag, &seg, rseg, size,
	    &sc->sc_rb.rb_membase, BUS_DMA_NOWAIT|BUS_DMA_COHERENT)) != 0) {
		printf("%s: DMA buffer map error %d\n",
			self->dv_xname, error);
		bus_dmamem_free(sa->sa_dmatag, &seg, rseg);
		return;
	}

	/* Load the buffer */
	if ((error = bus_dmamap_load(dmatag, sc->sc_dmamap,
	    sc->sc_rb.rb_membase, size, NULL, BUS_DMA_NOWAIT)) != 0) {
		printf("%s: DMA buffer map load error %d\n",
		    self->dv_xname, error);
		bus_dmamem_unmap(dmatag, sc->sc_rb.rb_membase, size);
		bus_dmamem_free(dmatag, &seg, rseg);
		return;
	}
	sc->sc_rb.rb_dmabase = sc->sc_dmamap->dm_segs[0].ds_addr;

	/*
	 * Initialize our media structures and MII info.
	 */
	mii->mii_ifp = ifp;
	mii->mii_readreg = be_mii_readreg;
	mii->mii_writereg = be_mii_writereg;
	mii->mii_statchg = be_mii_statchg;

	ifmedia_init(&mii->mii_media, 0, be_ifmedia_upd, be_ifmedia_sts);

	timeout_set(&sc->sc_tick_ch, be_tick, sc);

	/*
	 * Initialize transceiver and determine which PHY connection to use.
	 */
	be_mii_sync(sc);
	v = bus_space_read_4(sc->sc_bustag, sc->sc_tr, BE_TRI_MGMTPAL);

	instance = 0;

	if ((v & MGMT_PAL_EXT_MDIO) != 0) {

		mii_attach(&sc->sc_dev, mii, 0xffffffff, BE_PHY_EXTERNAL,
		    MII_OFFSET_ANY, 0);

		child = LIST_FIRST(&mii->mii_phys);
		if (child == NULL) {
			/* No PHY attached */
			ifmedia_add(&sc->sc_media,
			    IFM_MAKEWORD(IFM_ETHER,IFM_NONE,0,instance),
			    0, NULL);
			ifmedia_set(&sc->sc_media,
			    IFM_MAKEWORD(IFM_ETHER,IFM_NONE,0,instance));
		} else {
			/*
			 * Note: we support just one PHY on the external
			 * MII connector.
			 */
#ifdef DIAGNOSTIC
			if (LIST_NEXT(child, mii_list) != NULL) {
				printf("%s: spurious MII device %s attached\n",
				    sc->sc_dev.dv_xname,
				    child->mii_dev.dv_xname);
			}
#endif
			if (child->mii_phy != BE_PHY_EXTERNAL ||
			    child->mii_inst > 0) {
				printf("%s: cannot accommodate MII device %s"
				    " at phy %d, instance %lld\n",
				    sc->sc_dev.dv_xname,
				    child->mii_dev.dv_xname,
				    child->mii_phy, child->mii_inst);
			} else {
				sc->sc_phys[instance] = child->mii_phy;
			}

			/*
			 * XXX - we can really do the following ONLY if the
			 * phy indeed has the auto negotiation capability!!
			 */
			ifmedia_set(&sc->sc_media,
			    IFM_MAKEWORD(IFM_ETHER,IFM_AUTO,0,instance));

			/* Mark our current media setting */
			be_pal_gate(sc, BE_PHY_EXTERNAL);
			instance++;
		}

	}

	if ((v & MGMT_PAL_INT_MDIO) != 0) {
		/*
		 * The be internal phy looks vaguely like MII hardware,
		 * but not enough to be able to use the MII device
		 * layer. Hence, we have to take care of media selection
		 * ourselves.
		 */

		sc->sc_mii_inst = instance;
		sc->sc_phys[instance] = BE_PHY_INTERNAL;

		/* Use `ifm_data' to store BMCR bits */
		ifmedia_add(&sc->sc_media,
		    IFM_MAKEWORD(IFM_ETHER,IFM_10_T,0,instance), 0, NULL);
		ifmedia_add(&sc->sc_media,
		    IFM_MAKEWORD(IFM_ETHER,IFM_100_TX,0,instance),
		    BMCR_S100, NULL);
		ifmedia_add(&sc->sc_media,
		    IFM_MAKEWORD(IFM_ETHER,IFM_AUTO,0,instance), 0, NULL);

		printf("on-board transceiver at %s: 10baseT, 100baseTX, auto\n",
		    self->dv_xname);

		be_mii_reset(sc, BE_PHY_INTERNAL);
		/* Only set default medium here if there's no external PHY */
		if (instance == 0) {
			be_pal_gate(sc, BE_PHY_INTERNAL);
			ifmedia_set(&sc->sc_media,
			    IFM_MAKEWORD(IFM_ETHER,IFM_AUTO,0,instance));
		} else
			be_mii_writereg((void *)sc,
			    BE_PHY_INTERNAL, MII_BMCR, BMCR_ISO);
	}

	bcopy(sc->sc_dev.dv_xname, ifp->if_xname, IFNAMSIZ);
	ifp->if_softc = sc;
	ifp->if_start = bestart;
	ifp->if_ioctl = beioctl;
	ifp->if_watchdog = bewatchdog;
	ifp->if_flags =
	    IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;

	/* Attach the interface. */
	if_attach(ifp);
	ether_ifattach(ifp);
}


/*
 * Routine to copy from mbuf chain to transmit buffer in
 * network buffer memory.
 */
static __inline__ int
be_put(struct be_softc *sc, int idx, struct mbuf *m)
{
	struct mbuf *n;
	int len, tlen = 0, boff = 0;
	caddr_t bp;

	bp = sc->sc_rb.rb_txbuf + (idx % sc->sc_rb.rb_ntbuf) * BE_PKT_BUF_SZ;

	for (; m; m = n) {
		len = m->m_len;
		if (len == 0) {
			n = m_free(m);
			continue;
		}
		bcopy(mtod(m, caddr_t), bp+boff, len);
		boff += len;
		tlen += len;
		n = m_free(m);
	}
	return (tlen);
}

/*
 * Pull data off an interface.
 * Len is the length of data, with local net header stripped.
 * We copy the data into mbufs.  When full cluster sized units are present,
 * we copy into clusters.
 */
static __inline__ struct mbuf *
be_get(struct be_softc *sc, int idx, int totlen)
{
	struct mbuf *m;
	struct mbuf *top, **mp;
	int len, pad, boff = 0;
	caddr_t bp;

	bp = sc->sc_rb.rb_rxbuf + (idx % sc->sc_rb.rb_nrbuf) * BE_PKT_BUF_SZ;

	MGETHDR(m, M_DONTWAIT, MT_DATA);
	if (m == NULL)
		return (NULL);
	m->m_pkthdr.len = totlen;

	pad = ALIGN(sizeof(struct ether_header)) - sizeof(struct ether_header);
	m->m_data += pad;
	len = MHLEN - pad;
	top = NULL;
	mp = &top;

	while (totlen > 0) {
		if (top) {
			MGET(m, M_DONTWAIT, MT_DATA);
			if (m == NULL) {
				m_freem(top);
				return (NULL);
			}
			len = MLEN;
		}
		if (top && totlen >= MINCLSIZE) {
			MCLGET(m, M_DONTWAIT);
			if (m->m_flags & M_EXT)
				len = MCLBYTES;
		}
		m->m_len = len = min(totlen, len);
		bcopy(bp + boff, mtod(m, caddr_t), len);
		boff += len;
		totlen -= len;
		*mp = m;
		mp = &m->m_next;
	}

	return (top);
}

/*
 * Pass a packet to the higher levels.
 */
static __inline__ void
be_read(struct be_softc *sc, int idx, int len)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct mbuf *m;

	if (len <= sizeof(struct ether_header) ||
	    len > ETHERMTU + sizeof(struct ether_header)) {

		printf("%s: invalid packet size %d; dropping\n",
		    ifp->if_xname, len);

		ifp->if_ierrors++;
		return;
	}

	/*
	 * Pull packet off interface.
	 */
	m = be_get(sc, idx, len);
	if (m == NULL) {
		ifp->if_ierrors++;
		return;
	}

	ml_enqueue(&ml, m);
	if_input(ifp, &ml);
}

/*
 * Start output on interface.
 * We make two assumptions here:
 *  1) that the current priority is set to splnet _before_ this code
 *     is called *and* is returned to the appropriate priority after
 *     return
 *  2) that the IFF_OACTIVE flag is checked before this code is called
 *     (i.e. that the output part of the interface is idle)
 */
void
bestart(struct ifnet *ifp)
{
	struct be_softc *sc = (struct be_softc *)ifp->if_softc;
	struct qec_xd *txd = sc->sc_rb.rb_txd;
	struct mbuf *m;
	unsigned int bix, len;
	unsigned int ntbuf = sc->sc_rb.rb_ntbuf;

	if (!(ifp->if_flags & IFF_RUNNING) || ifq_is_oactive(&ifp->if_snd))
		return;

	bix = sc->sc_rb.rb_tdhead;

	for (;;) {
		m = ifq_dequeue(&ifp->if_snd);
		if (m == NULL)
			break;

#if NBPFILTER > 0
		/*
		 * If BPF is listening on this interface, let it see the
		 * packet before we commit it to the wire.
		 */
		if (ifp->if_bpf)
			bpf_mtap(ifp->if_bpf, m, BPF_DIRECTION_OUT);
#endif

		/*
		 * Copy the mbuf chain into the transmit buffer.
		 */
		len = be_put(sc, bix, m);

		/*
		 * Initialize transmit registers and start transmission
		 */
		txd[bix].xd_flags = QEC_XD_OWN | QEC_XD_SOP | QEC_XD_EOP |
				    (len & QEC_XD_LENGTH);
		bus_space_write_4(sc->sc_bustag, sc->sc_cr, BE_CRI_CTRL,
				  BE_CR_CTRL_TWAKEUP);

		if (++bix == QEC_XD_RING_MAXSIZE)
			bix = 0;

		if (++sc->sc_rb.rb_td_nbusy == ntbuf) {
			ifq_set_oactive(&ifp->if_snd);
			break;
		}
	}

	sc->sc_rb.rb_tdhead = bix;
}

void
bestop(struct be_softc *sc)
{
	int n;
	bus_space_tag_t t = sc->sc_bustag;
	bus_space_handle_t br = sc->sc_br;

	timeout_del(&sc->sc_tick_ch);

	/* Down the MII. */
	mii_down(&sc->sc_mii);
	(void)be_intphy_service(sc, &sc->sc_mii, MII_DOWN);

	/* Stop the transmitter */
	bus_space_write_4(t, br, BE_BRI_TXCFG, 0);
	for (n = 32; n > 0; n--) {
		if (bus_space_read_4(t, br, BE_BRI_TXCFG) == 0)
			break;
		DELAY(20);
	}

	/* Stop the receiver */
	bus_space_write_4(t, br, BE_BRI_RXCFG, 0);
	for (n = 32; n > 0; n--) {
		if (bus_space_read_4(t, br, BE_BRI_RXCFG) == 0)
			break;
		DELAY(20);
	}
}

/*
 * Reset interface.
 */
void
bereset(struct be_softc *sc)
{
	int s;

	s = splnet();
	bestop(sc);
	if ((sc->sc_arpcom.ac_if.if_flags & IFF_UP) != 0)
		beinit(sc);
	splx(s);
}

void
bewatchdog(struct ifnet *ifp)
{
	struct be_softc *sc = ifp->if_softc;

	log(LOG_ERR, "%s: device timeout\n", sc->sc_dev.dv_xname);
	++sc->sc_arpcom.ac_if.if_oerrors;
	bereset(sc);
}

int
beintr(void *v)
{
	struct be_softc *sc = (struct be_softc *)v;
	bus_space_tag_t t = sc->sc_bustag;
	u_int32_t whyq, whyb, whyc;
	int r = 0;

	/* Read QEC status, channel status and BE status */
	whyq = bus_space_read_4(t, sc->sc_qr, QEC_QRI_STAT);
	whyc = bus_space_read_4(t, sc->sc_cr, BE_CRI_STAT);
	whyb = bus_space_read_4(t, sc->sc_br, BE_BRI_STAT);

	if (whyq & QEC_STAT_BM)
		r |= beeint(sc, whyb);

	if (whyq & QEC_STAT_ER)
		r |= beqint(sc, whyc);

	if (whyq & QEC_STAT_TX && whyc & BE_CR_STAT_TXIRQ)
		r |= betint(sc);

	if (whyq & QEC_STAT_RX && whyc & BE_CR_STAT_RXIRQ)
		r |= berint(sc);

	return (r);
}

/*
 * QEC Interrupt.
 */
int
beqint(struct be_softc *sc, u_int32_t why)
{
	int r = 0, rst = 0;

	if (why & BE_CR_STAT_TXIRQ)
		r |= 1;
	if (why & BE_CR_STAT_RXIRQ)
		r |= 1;

	if (why & BE_CR_STAT_BERROR) {
		r |= 1;
		rst = 1;
		printf("%s: bigmac error\n", sc->sc_dev.dv_xname);
	}

	if (why & BE_CR_STAT_TXDERR) {
		r |= 1;
		rst = 1;
		printf("%s: bogus tx descriptor\n", sc->sc_dev.dv_xname);
	}

	if (why & (BE_CR_STAT_TXLERR | BE_CR_STAT_TXPERR | BE_CR_STAT_TXSERR)) {
		r |= 1;
		rst = 1;
		printf("%s: tx dma error ( ", sc->sc_dev.dv_xname);
		if (why & BE_CR_STAT_TXLERR)
			printf("Late ");
		if (why & BE_CR_STAT_TXPERR)
			printf("Parity ");
		if (why & BE_CR_STAT_TXSERR)
			printf("Generic ");
		printf(")\n");
	}

	if (why & BE_CR_STAT_RXDROP) {
		r |= 1;
		rst = 1;
		printf("%s: out of rx descriptors\n", sc->sc_dev.dv_xname);
	}

	if (why & BE_CR_STAT_RXSMALL) {
		r |= 1;
		rst = 1;
		printf("%s: rx descriptor too small\n", sc->sc_dev.dv_xname);
	}

	if (why & (BE_CR_STAT_RXLERR | BE_CR_STAT_RXPERR | BE_CR_STAT_RXSERR)) {
		r |= 1;
		rst = 1;
		printf("%s: rx dma error ( ", sc->sc_dev.dv_xname);
		if (why & BE_CR_STAT_RXLERR)
			printf("Late ");
		if (why & BE_CR_STAT_RXPERR)
			printf("Parity ");
		if (why & BE_CR_STAT_RXSERR)
			printf("Generic ");
		printf(")\n");
	}

	if (!r) {
		rst = 1;
		printf("%s: unexpected error interrupt %08x\n",
			sc->sc_dev.dv_xname, why);
	}

	if (rst) {
		printf("%s: resetting\n", sc->sc_dev.dv_xname);
		bereset(sc);
	}

	return (r);
}

/*
 * Error interrupt.
 */
int
beeint(struct be_softc *sc, u_int32_t why)
{
	int r = 0, rst = 0;

	if (why & BE_BR_STAT_RFIFOVF) {
		r |= 1;
		rst = 1;
		printf("%s: receive fifo overrun\n", sc->sc_dev.dv_xname);
	}
	if (why & BE_BR_STAT_TFIFO_UND) {
		r |= 1;
		rst = 1;
		printf("%s: transmit fifo underrun\n", sc->sc_dev.dv_xname);
	}
	if (why & BE_BR_STAT_MAXPKTERR) {
		r |= 1;
		rst = 1;
		printf("%s: max packet size error\n", sc->sc_dev.dv_xname);
	}

	if (!r) {
		rst = 1;
		printf("%s: unexpected error interrupt %08x\n",
			sc->sc_dev.dv_xname, why);
	}

	if (rst) {
		printf("%s: resetting\n", sc->sc_dev.dv_xname);
		bereset(sc);
	}

	return (r);
}

/*
 * Transmit interrupt.
 */
int
betint(struct be_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	bus_space_tag_t t = sc->sc_bustag;
	bus_space_handle_t br = sc->sc_br;
	unsigned int bix, txflags;

	/*
	 * Unload collision counters
	 */
	ifp->if_collisions +=
		bus_space_read_4(t, br, BE_BRI_NCCNT) +
		bus_space_read_4(t, br, BE_BRI_FCCNT) +
		bus_space_read_4(t, br, BE_BRI_EXCNT) +
		bus_space_read_4(t, br, BE_BRI_LTCNT);

	/*
	 * the clear the hardware counters
	 */
	bus_space_write_4(t, br, BE_BRI_NCCNT, 0);
	bus_space_write_4(t, br, BE_BRI_FCCNT, 0);
	bus_space_write_4(t, br, BE_BRI_EXCNT, 0);
	bus_space_write_4(t, br, BE_BRI_LTCNT, 0);

	bix = sc->sc_rb.rb_tdtail;

	for (;;) {
		if (sc->sc_rb.rb_td_nbusy <= 0)
			break;

		txflags = sc->sc_rb.rb_txd[bix].xd_flags;

		if (txflags & QEC_XD_OWN)
			break;

		ifq_clr_oactive(&ifp->if_snd);

		if (++bix == QEC_XD_RING_MAXSIZE)
			bix = 0;

		--sc->sc_rb.rb_td_nbusy;
	}

	sc->sc_rb.rb_tdtail = bix;

	bestart(ifp);

	if (sc->sc_rb.rb_td_nbusy == 0)
		ifp->if_timer = 0;

	return (1);
}

/*
 * Receive interrupt.
 */
int
berint(struct be_softc *sc)
{
	struct qec_xd *xd = sc->sc_rb.rb_rxd;
	unsigned int bix, len;
	unsigned int nrbuf = sc->sc_rb.rb_nrbuf;

	bix = sc->sc_rb.rb_rdtail;

	/*
	 * Process all buffers with valid data.
	 */
	for (;;) {
		len = xd[bix].xd_flags;
		if (len & QEC_XD_OWN)
			break;

		len &= QEC_XD_LENGTH;
		be_read(sc, bix, len);

		/* ... */
		xd[(bix+nrbuf) % QEC_XD_RING_MAXSIZE].xd_flags =
			QEC_XD_OWN | (BE_PKT_BUF_SZ & QEC_XD_LENGTH);

		if (++bix == QEC_XD_RING_MAXSIZE)
			bix = 0;
	}

	sc->sc_rb.rb_rdtail = bix;

	return (1);
}

int
beioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct be_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int s, error = 0;

	s = splnet();

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		beinit(sc);
		break;

	case SIOCSIFFLAGS:
		if ((ifp->if_flags & IFF_UP) == 0 &&
		    (ifp->if_flags & IFF_RUNNING) != 0) {
			/*
			 * If interface is marked down and it is running, then
			 * stop it.
			 */
			bestop(sc);
			ifp->if_flags &= ~IFF_RUNNING;
		} else if ((ifp->if_flags & IFF_UP) != 0 &&
		    (ifp->if_flags & IFF_RUNNING) == 0) {
			/*
			 * If interface is marked up and it is stopped, then
			 * start it.
			 */
			beinit(sc);
		} else {
			/*
			 * Reset the interface to pick up changes in any other
			 * flags that affect hardware registers.
			 */
			bestop(sc);
			beinit(sc);
		}
#ifdef BEDEBUG
		if (ifp->if_flags & IFF_DEBUG)
			sc->sc_debug = 1;
		else
			sc->sc_debug = 0;
#endif
		break;

	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_media, cmd);
		break;

	default:
		error = ether_ioctl(ifp, &sc->sc_arpcom, cmd, data);
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			be_mcreset(sc);
		error = 0;
	}

	splx(s);
	return (error);
}


void
beinit(struct be_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	bus_space_tag_t t = sc->sc_bustag;
	bus_space_handle_t br = sc->sc_br;
	bus_space_handle_t cr = sc->sc_cr;
	struct qec_softc *qec = sc->sc_qec;
	u_int32_t v;
	u_int32_t qecaddr;
	u_int8_t *ea;
	int s;

	s = splnet();

	qec_meminit(&sc->sc_rb, BE_PKT_BUF_SZ);

	bestop(sc);

	ea = sc->sc_arpcom.ac_enaddr;
	bus_space_write_4(t, br, BE_BRI_MACADDR0, (ea[0] << 8) | ea[1]);
	bus_space_write_4(t, br, BE_BRI_MACADDR1, (ea[2] << 8) | ea[3]);
	bus_space_write_4(t, br, BE_BRI_MACADDR2, (ea[4] << 8) | ea[5]);

	/* Clear hash table */
	bus_space_write_4(t, br, BE_BRI_HASHTAB0, 0);
	bus_space_write_4(t, br, BE_BRI_HASHTAB1, 0);
	bus_space_write_4(t, br, BE_BRI_HASHTAB2, 0);
	bus_space_write_4(t, br, BE_BRI_HASHTAB3, 0);

	/* Re-initialize RX configuration */
	v = BE_BR_RXCFG_FIFO;
	bus_space_write_4(t, br, BE_BRI_RXCFG, v);

	be_mcreset(sc);

	bus_space_write_4(t, br, BE_BRI_RANDSEED, 0xbd);

	bus_space_write_4(t, br, BE_BRI_XIFCFG,
			  BE_BR_XCFG_ODENABLE | BE_BR_XCFG_RESV);

	bus_space_write_4(t, br, BE_BRI_JSIZE, 4);

	/*
	 * Turn off counter expiration interrupts as well as
	 * 'gotframe' and 'sentframe'
	 */
	bus_space_write_4(t, br, BE_BRI_IMASK,
			  BE_BR_IMASK_GOTFRAME	|
			  BE_BR_IMASK_RCNTEXP	|
			  BE_BR_IMASK_ACNTEXP	|
			  BE_BR_IMASK_CCNTEXP	|
			  BE_BR_IMASK_LCNTEXP	|
			  BE_BR_IMASK_CVCNTEXP	|
			  BE_BR_IMASK_SENTFRAME	|
			  BE_BR_IMASK_NCNTEXP	|
			  BE_BR_IMASK_ECNTEXP	|
			  BE_BR_IMASK_LCCNTEXP	|
			  BE_BR_IMASK_FCNTEXP	|
			  BE_BR_IMASK_DTIMEXP);

	/* Channel registers: */
	bus_space_write_4(t, cr, BE_CRI_RXDS, (u_int32_t)sc->sc_rb.rb_rxddma);
	bus_space_write_4(t, cr, BE_CRI_TXDS, (u_int32_t)sc->sc_rb.rb_txddma);

	qecaddr = sc->sc_channel * qec->sc_msize;
	bus_space_write_4(t, cr, BE_CRI_RXWBUF, qecaddr);
	bus_space_write_4(t, cr, BE_CRI_RXRBUF, qecaddr);
	bus_space_write_4(t, cr, BE_CRI_TXWBUF, qecaddr + qec->sc_rsize);
	bus_space_write_4(t, cr, BE_CRI_TXRBUF, qecaddr + qec->sc_rsize);

	bus_space_write_4(t, cr, BE_CRI_RIMASK, 0);
	bus_space_write_4(t, cr, BE_CRI_TIMASK, 0);
	bus_space_write_4(t, cr, BE_CRI_QMASK, 0);
	bus_space_write_4(t, cr, BE_CRI_BMASK, 0);
	bus_space_write_4(t, cr, BE_CRI_CCNT, 0);

	/* Enable transmitter */
	bus_space_write_4(t, br, BE_BRI_TXCFG,
			  BE_BR_TXCFG_FIFO | BE_BR_TXCFG_ENABLE);

	/* Enable receiver */
	v = bus_space_read_4(t, br, BE_BRI_RXCFG);
	v |= BE_BR_RXCFG_FIFO | BE_BR_RXCFG_ENABLE;
	bus_space_write_4(t, br, BE_BRI_RXCFG, v);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	be_ifmedia_upd(ifp);
	timeout_add_sec(&sc->sc_tick_ch, 1);
	splx(s);
}

void
be_mcreset(struct be_softc *sc)
{
	struct arpcom *ac = &sc->sc_arpcom;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	bus_space_tag_t t = sc->sc_bustag;
	bus_space_handle_t br = sc->sc_br;
	u_int32_t crc;
	u_int16_t hash[4];
	u_int8_t octet;
	u_int32_t v;
	int i, j;
	struct ether_multi *enm;
	struct ether_multistep step;

	if (ifp->if_flags & IFF_PROMISC) {
		v = bus_space_read_4(t, br, BE_BRI_RXCFG);
		v |= BE_BR_RXCFG_PMISC;
		bus_space_write_4(t, br, BE_BRI_RXCFG, v);
		return;
	}

	if (ac->ac_multirangecnt > 0)
		ifp->if_flags |= IFF_ALLMULTI;

	if (ifp->if_flags & IFF_ALLMULTI) {
		hash[3] = hash[2] = hash[1] = hash[0] = 0xffff;
		goto chipit;
	}

	hash[3] = hash[2] = hash[1] = hash[0] = 0;

	ETHER_FIRST_MULTI(step, ac, enm);
	while (enm != NULL) {
		crc = 0xffffffff;

		for (i = 0; i < ETHER_ADDR_LEN; i++) {
			octet = enm->enm_addrlo[i];

			for (j = 0; j < 8; j++) {
				if ((crc & 1) ^ (octet & 1)) {
					crc >>= 1;
					crc ^= MC_POLY_LE;
				}
				else
					crc >>= 1;
				octet >>= 1;
			}
		}

		crc >>= 26;
		hash[crc >> 4] |= 1 << (crc & 0xf);
		ETHER_NEXT_MULTI(step, enm);
	}

	ifp->if_flags &= ~IFF_ALLMULTI;

chipit:
	/* Enable the hash filter */
	bus_space_write_4(t, br, BE_BRI_HASHTAB0, hash[0]);
	bus_space_write_4(t, br, BE_BRI_HASHTAB1, hash[1]);
	bus_space_write_4(t, br, BE_BRI_HASHTAB2, hash[2]);
	bus_space_write_4(t, br, BE_BRI_HASHTAB3, hash[3]);

	v = bus_space_read_4(t, br, BE_BRI_RXCFG);
	v &= ~BE_BR_RXCFG_PMISC;
	v |= BE_BR_RXCFG_HENABLE;
	bus_space_write_4(t, br, BE_BRI_RXCFG, v);
}

/*
 * Set the tcvr to an idle state
 */
void
be_mii_sync(struct be_softc *sc)
{
	bus_space_tag_t t = sc->sc_bustag;
	bus_space_handle_t tr = sc->sc_tr;
	int n = 32;

	while (n--) {
		bus_space_write_4(t, tr, BE_TRI_MGMTPAL,
		    MGMT_PAL_INT_MDIO | MGMT_PAL_EXT_MDIO | MGMT_PAL_OENAB);
		(void)bus_space_read_4(t, tr, BE_TRI_MGMTPAL);
		bus_space_write_4(t, tr, BE_TRI_MGMTPAL,
		    MGMT_PAL_INT_MDIO | MGMT_PAL_EXT_MDIO |
		    MGMT_PAL_OENAB | MGMT_PAL_DCLOCK);
		(void)bus_space_read_4(t, tr, BE_TRI_MGMTPAL);
	}
}

void
be_pal_gate(struct be_softc *sc, int phy)
{
	bus_space_tag_t t = sc->sc_bustag;
	bus_space_handle_t tr = sc->sc_tr;
	u_int32_t v;

	be_mii_sync(sc);

	v = ~(TCVR_PAL_EXTLBACK | TCVR_PAL_MSENSE | TCVR_PAL_LTENABLE);
	if (phy == BE_PHY_INTERNAL)
		v &= ~TCVR_PAL_SERIAL;

	bus_space_write_4(t, tr, BE_TRI_TCVRPAL, v);
	(void)bus_space_read_4(t, tr, BE_TRI_TCVRPAL);
}

static int
be_tcvr_read_bit(struct be_softc *sc, int phy)
{
	bus_space_tag_t t = sc->sc_bustag;
	bus_space_handle_t tr = sc->sc_tr;
	int ret;

	if (phy == BE_PHY_INTERNAL) {
		bus_space_write_4(t, tr, BE_TRI_MGMTPAL, MGMT_PAL_EXT_MDIO);
		(void)bus_space_read_4(t, tr, BE_TRI_MGMTPAL);
		bus_space_write_4(t, tr, BE_TRI_MGMTPAL,
		    MGMT_PAL_EXT_MDIO | MGMT_PAL_DCLOCK);
		(void)bus_space_read_4(t, tr, BE_TRI_MGMTPAL);
		ret = (bus_space_read_4(t, tr, BE_TRI_MGMTPAL) &
			MGMT_PAL_INT_MDIO) >> MGMT_PAL_INT_MDIO_SHIFT;
	} else {
		bus_space_write_4(t, tr, BE_TRI_MGMTPAL, MGMT_PAL_INT_MDIO);
		(void)bus_space_read_4(t, tr, BE_TRI_MGMTPAL);
		ret = (bus_space_read_4(t, tr, BE_TRI_MGMTPAL) &
		    MGMT_PAL_EXT_MDIO) >> MGMT_PAL_EXT_MDIO_SHIFT;
		bus_space_write_4(t, tr, BE_TRI_MGMTPAL,
		    MGMT_PAL_INT_MDIO | MGMT_PAL_DCLOCK);
		(void)bus_space_read_4(t, tr, BE_TRI_MGMTPAL);
	}

	return (ret);
}

static void
be_tcvr_write_bit(struct be_softc *sc, int phy, int bit)
{
	bus_space_tag_t t = sc->sc_bustag;
	bus_space_handle_t tr = sc->sc_tr;
	u_int32_t v;

	if (phy == BE_PHY_INTERNAL) {
		v = ((bit & 1) << MGMT_PAL_INT_MDIO_SHIFT) |
		    MGMT_PAL_OENAB | MGMT_PAL_EXT_MDIO;
	} else {
		v = ((bit & 1) << MGMT_PAL_EXT_MDIO_SHIFT)
		    | MGMT_PAL_OENAB | MGMT_PAL_INT_MDIO;
	}
	bus_space_write_4(t, tr, BE_TRI_MGMTPAL, v);
	(void)bus_space_read_4(t, tr, BE_TRI_MGMTPAL);
	bus_space_write_4(t, tr, BE_TRI_MGMTPAL, v | MGMT_PAL_DCLOCK);
	(void)bus_space_read_4(t, tr, BE_TRI_MGMTPAL);
}

static void
be_mii_sendbits(struct be_softc *sc, int phy, u_int32_t data, int nbits)
{
	int i;

	for (i = 1 << (nbits - 1); i != 0; i >>= 1)
		be_tcvr_write_bit(sc, phy, (data & i) != 0);
}

static int
be_mii_readreg(struct device *self, int phy, int reg)
{
	struct be_softc *sc = (struct be_softc *)self;
	int val = 0, i;

	/*
	 * Read the PHY register by manually driving the MII control lines.
	 */
	be_mii_sync(sc);
	be_mii_sendbits(sc, phy, MII_COMMAND_START, 2);
	be_mii_sendbits(sc, phy, MII_COMMAND_READ, 2);
	be_mii_sendbits(sc, phy, phy, 5);
	be_mii_sendbits(sc, phy, reg, 5);

	(void) be_tcvr_read_bit(sc, phy);
	(void) be_tcvr_read_bit(sc, phy);

	for (i = 15; i >= 0; i--)
		val |= (be_tcvr_read_bit(sc, phy) << i);

	(void) be_tcvr_read_bit(sc, phy);
	(void) be_tcvr_read_bit(sc, phy);
	(void) be_tcvr_read_bit(sc, phy);

	return (val);
}

void
be_mii_writereg(struct device *self, int phy, int reg, int val)
{
	struct be_softc *sc = (struct be_softc *)self;
	int i;

	/*
	 * Write the PHY register by manually driving the MII control lines.
	 */
	be_mii_sync(sc);
	be_mii_sendbits(sc, phy, MII_COMMAND_START, 2);
	be_mii_sendbits(sc, phy, MII_COMMAND_WRITE, 2);
	be_mii_sendbits(sc, phy, phy, 5);
	be_mii_sendbits(sc, phy, reg, 5);

	be_tcvr_write_bit(sc, phy, 1);
	be_tcvr_write_bit(sc, phy, 0);

	for (i = 15; i >= 0; i--)
		be_tcvr_write_bit(sc, phy, (val >> i) & 1);
}

int
be_mii_reset(struct be_softc *sc, int phy)
{
	int n;

	be_mii_writereg((struct device *)sc, phy, MII_BMCR,
	    BMCR_LOOP | BMCR_PDOWN | BMCR_ISO);
	be_mii_writereg((struct device *)sc, phy, MII_BMCR, BMCR_RESET);

	for (n = 16; n >= 0; n--) {
		int bmcr = be_mii_readreg((struct device *)sc, phy, MII_BMCR);
		if ((bmcr & BMCR_RESET) == 0)
			break;
		DELAY(20);
	}
	if (n == 0) {
		printf("%s: bmcr reset failed\n", sc->sc_dev.dv_xname);
		return (EIO);
	}

	return (0);
}

void
be_tick(void *arg)
{
	struct be_softc *sc = arg;
	int s = splnet();

	mii_tick(&sc->sc_mii);
	(void)be_intphy_service(sc, &sc->sc_mii, MII_TICK);

	timeout_add_sec(&sc->sc_tick_ch, 1);
	splx(s);
}

void
be_mii_statchg(struct device *self)
{
	struct be_softc *sc = (struct be_softc *)self;
	bus_space_tag_t t = sc->sc_bustag;
	bus_space_handle_t br = sc->sc_br;
	u_int64_t instance;
	u_int32_t v;

	instance = IFM_INST(sc->sc_mii.mii_media.ifm_cur->ifm_media);
#ifdef DIAGNOSTIC
	if (instance > 1)
		panic("be_mii_statchg: instance %lld out of range", instance);
#endif

	/* Update duplex mode in TX configuration */
	v = bus_space_read_4(t, br, BE_BRI_TXCFG);
	if ((IFM_OPTIONS(sc->sc_mii.mii_media_active) & IFM_FDX) != 0)
		v |= BE_BR_TXCFG_FULLDPLX;
	else
		v &= ~BE_BR_TXCFG_FULLDPLX;
	bus_space_write_4(t, br, BE_BRI_TXCFG, v);

	/* Change to appropriate gate in transceiver PAL */
	be_pal_gate(sc, sc->sc_phys[instance]);
}

/*
 * Get current media settings.
 */
void
be_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct be_softc *sc = ifp->if_softc;

	mii_pollstat(&sc->sc_mii);
	(void)be_intphy_service(sc, &sc->sc_mii, MII_POLLSTAT);

	ifmr->ifm_status = sc->sc_mii.mii_media_status;
	ifmr->ifm_active = sc->sc_mii.mii_media_active;
	return;
}

/*
 * Set media options.
 */
int
be_ifmedia_upd(struct ifnet *ifp)
{
	struct be_softc *sc = ifp->if_softc;
	int error;

	if ((error = mii_mediachg(&sc->sc_mii)) != 0)
		return (error);

	return (be_intphy_service(sc, &sc->sc_mii, MII_MEDIACHG));
}

/*
 * Service routine for our pseudo-MII internal transceiver.
 */
int
be_intphy_service(struct be_softc *sc, struct mii_data *mii, int cmd)
{
	struct ifmedia_entry *ife = mii->mii_media.ifm_cur;
	int bmcr, bmsr;
	int error;

	switch (cmd) {
	case MII_POLLSTAT:
		/*
		 * If we're not polling our PHY instance, just return.
		 */
		if (IFM_INST(ife->ifm_media) != sc->sc_mii_inst)
			return (0);

		break;

	case MII_MEDIACHG:

		/*
		 * If the media indicates a different PHY instance,
		 * isolate ourselves.
		 */
		if (IFM_INST(ife->ifm_media) != sc->sc_mii_inst) {
			bmcr = be_mii_readreg((void *)sc,
			    BE_PHY_INTERNAL, MII_BMCR);
			be_mii_writereg((void *)sc,
			    BE_PHY_INTERNAL, MII_BMCR, bmcr | BMCR_ISO);
			sc->sc_mii_flags &= ~MIIF_HAVELINK;
			sc->sc_intphy_curspeed = 0;
			return (0);
		}


		if ((error = be_mii_reset(sc, BE_PHY_INTERNAL)) != 0)
			return (error);

		bmcr = be_mii_readreg((void *)sc, BE_PHY_INTERNAL, MII_BMCR);

		/*
		 * Select the new mode and take out of isolation
		 */
		if (IFM_SUBTYPE(ife->ifm_media) == IFM_100_TX)
			bmcr |= BMCR_S100;
		else if (IFM_SUBTYPE(ife->ifm_media) == IFM_10_T)
			bmcr &= ~BMCR_S100;
		else if (IFM_SUBTYPE(ife->ifm_media) == IFM_AUTO) {
			if ((sc->sc_mii_flags & MIIF_HAVELINK) != 0) {
				bmcr &= ~BMCR_S100;
				bmcr |= sc->sc_intphy_curspeed;
			} else {
				/* Keep isolated until link is up */
				bmcr |= BMCR_ISO;
				sc->sc_mii_flags |= MIIF_DOINGAUTO;
			}
		}

		if ((IFM_OPTIONS(ife->ifm_media) & IFM_FDX) != 0)
			bmcr |= BMCR_FDX;
		else
			bmcr &= ~BMCR_FDX;

		be_mii_writereg((void *)sc, BE_PHY_INTERNAL, MII_BMCR, bmcr);
		break;

	case MII_TICK:
		/*
		 * If we're not currently selected, just return.
		 */
		if (IFM_INST(ife->ifm_media) != sc->sc_mii_inst)
			return (0);

		/* Only used for automatic media selection */
		if (IFM_SUBTYPE(ife->ifm_media) != IFM_AUTO)
			return (0);

		/* Is the interface even up? */
		if ((mii->mii_ifp->if_flags & IFF_UP) == 0)
			return (0);

		/*
		 * Check link status; if we don't have a link, try another
		 * speed. We can't detect duplex mode, so half-duplex is
		 * what we have to settle for.
		 */

		/* Read twice in case the register is latched */
		bmsr = be_mii_readreg((void *)sc, BE_PHY_INTERNAL, MII_BMSR) |
		    be_mii_readreg((void *)sc, BE_PHY_INTERNAL, MII_BMSR);

		if ((bmsr & BMSR_LINK) != 0) {
			/* We have a carrier */
			bmcr = be_mii_readreg((void *)sc,
			    BE_PHY_INTERNAL, MII_BMCR);

			if ((sc->sc_mii_flags & MIIF_DOINGAUTO) != 0) {
				bmcr = be_mii_readreg((void *)sc,
				    BE_PHY_INTERNAL, MII_BMCR);

				sc->sc_mii_flags |= MIIF_HAVELINK;
				sc->sc_intphy_curspeed = (bmcr & BMCR_S100);
				sc->sc_mii_flags &= ~MIIF_DOINGAUTO;

				bmcr &= ~BMCR_ISO;
				be_mii_writereg((void *)sc,
				    BE_PHY_INTERNAL, MII_BMCR, bmcr);

				printf("%s: link up at %s Mbps\n",
				    sc->sc_dev.dv_xname,
				    (bmcr & BMCR_S100) ? "100" : "10");
			}
			sc->sc_mii_ticks = 0;
			return (0);
		}

		if ((sc->sc_mii_flags & MIIF_DOINGAUTO) == 0) {
			sc->sc_mii_flags |= MIIF_DOINGAUTO;
			sc->sc_mii_flags &= ~MIIF_HAVELINK;
			sc->sc_intphy_curspeed = 0;
			printf("%s: link down\n", sc->sc_dev.dv_xname);
		}

		/* Only retry autonegotiation every 5 seconds. */
		if (++sc->sc_mii_ticks < 5)
			return(0);

		sc->sc_mii_ticks = 0;
		bmcr = be_mii_readreg((void *)sc, BE_PHY_INTERNAL, MII_BMCR);
		/* Just flip the fast speed bit */
		bmcr ^= BMCR_S100;
		be_mii_writereg((void *)sc, BE_PHY_INTERNAL, MII_BMCR, bmcr);

		break;

	case MII_DOWN:
		/* Isolate this phy */
		bmcr = be_mii_readreg((void *)sc, BE_PHY_INTERNAL, MII_BMCR);
		be_mii_writereg((void *)sc,
		    BE_PHY_INTERNAL, MII_BMCR, bmcr | BMCR_ISO);
		return (0);
	}

	/* Update the media status. */
	be_intphy_status(sc);

	/* Callback if something changed. */
	if (sc->sc_mii_active != mii->mii_media_active || cmd == MII_MEDIACHG) {
		(*mii->mii_statchg)((struct device *)sc);
		sc->sc_mii_active = mii->mii_media_active;
	}
	return (0);
}

/*
 * Determine status of internal transceiver
 */
void
be_intphy_status(struct be_softc *sc)
{
	struct mii_data *mii = &sc->sc_mii;
	uint64_t media_active, media_status;
	int bmcr, bmsr;

	media_status = IFM_AVALID;
	media_active = 0;

	/*
	 * Internal transceiver; do the work here.
	 */
	bmcr = be_mii_readreg((struct device *)sc, BE_PHY_INTERNAL, MII_BMCR);

	switch (bmcr & (BMCR_S100 | BMCR_FDX)) {
	case (BMCR_S100 | BMCR_FDX):
		media_active = IFM_ETHER | IFM_100_TX | IFM_FDX;
		break;
	case BMCR_S100:
		media_active = IFM_ETHER | IFM_100_TX | IFM_HDX;
		break;
	case BMCR_FDX:
		media_active = IFM_ETHER | IFM_10_T | IFM_FDX;
		break;
	case 0:
		media_active = IFM_ETHER | IFM_10_T | IFM_HDX;
		break;
	}

	/* Read twice in case the register is latched */
	bmsr = be_mii_readreg((struct device *)sc, BE_PHY_INTERNAL, MII_BMSR)|
	       be_mii_readreg((struct device *)sc, BE_PHY_INTERNAL, MII_BMSR);
	if (bmsr & BMSR_LINK)
		media_status |= IFM_ACTIVE;

	mii->mii_media_status = media_status;
	mii->mii_media_active = media_active;
}
