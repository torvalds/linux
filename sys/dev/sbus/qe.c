/*	$OpenBSD: qe.c,v 1.43 2022/10/16 01:22:40 jsg Exp $	*/
/*	$NetBSD: qe.c,v 1.16 2001/03/30 17:30:18 christos Exp $	*/

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
 * Copyright (c) 1998 Jason L. Wright.
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

/*
 * Driver for the SBus qec+qe QuadEthernet board.
 *
 * This driver was written using the AMD MACE Am79C940 documentation, some
 * ideas gleaned from the S/Linux driver for this card, Solaris header files,
 * and a loan of a card from Paul Southworth of the Internet Engineering
 * Group (www.ieng.com).
 */

#define QEDEBUG

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
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
#include <dev/sbus/qecreg.h>
#include <dev/sbus/qecvar.h>
#include <dev/sbus/qereg.h>

struct qe_softc {
	struct	device	sc_dev;		/* base device */
	bus_space_tag_t	sc_bustag;	/* bus & dma tags */
	bus_dma_tag_t	sc_dmatag;
	bus_dmamap_t	sc_dmamap;
	struct	arpcom sc_arpcom;
	struct	ifmedia sc_ifmedia;	/* interface media */

	struct	qec_softc *sc_qec;	/* QEC parent */

	bus_space_handle_t	sc_qr;	/* QEC registers */
	bus_space_handle_t	sc_mr;	/* MACE registers */
	bus_space_handle_t	sc_cr;	/* channel registers */

	int	sc_channel;		/* channel number */
	u_int	sc_rev;			/* board revision */

	int	sc_burst;

	struct  qec_ring	sc_rb;	/* Packet Ring Buffer */

#ifdef QEDEBUG
	int	sc_debug;
#endif
};

int	qematch(struct device *, void *, void *);
void	qeattach(struct device *, struct device *, void *);

void	qeinit(struct qe_softc *);
void	qestart(struct ifnet *);
void	qestop(struct qe_softc *);
void	qewatchdog(struct ifnet *);
int	qeioctl(struct ifnet *, u_long, caddr_t);
void	qereset(struct qe_softc *);

int	qeintr(void *);
int	qe_eint(struct qe_softc *, u_int32_t);
int	qe_rint(struct qe_softc *);
int	qe_tint(struct qe_softc *);
void	qe_mcreset(struct qe_softc *);

int	qe_put(struct qe_softc *, int, struct mbuf *);
void	qe_read(struct qe_softc *, int, int);
struct mbuf	*qe_get(struct qe_softc *, int, int);

/* ifmedia callbacks */
void	qe_ifmedia_sts(struct ifnet *, struct ifmediareq *);
int	qe_ifmedia_upd(struct ifnet *);

const struct cfattach qe_ca = {
	sizeof(struct qe_softc), qematch, qeattach
};

struct cfdriver qe_cd = {
	NULL, "qe", DV_IFNET
};

int
qematch(struct device *parent, void *vcf, void *aux)
{
	struct cfdata *cf = vcf;
	struct sbus_attach_args *sa = aux;

	return (strcmp(cf->cf_driver->cd_name, sa->sa_name) == 0);
}

void
qeattach(struct device *parent, struct device *self, void *aux)
{
	struct sbus_attach_args *sa = aux;
	struct qec_softc *qec = (struct qec_softc *)parent;
	struct qe_softc *sc = (struct qe_softc *)self;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	int node = sa->sa_node;
	bus_dma_tag_t dmatag = sa->sa_dmatag;
	bus_dma_segment_t seg;
	bus_size_t size;
	int rseg, error;
	extern void myetheraddr(u_char *);

	/* Pass on the bus tags */
	sc->sc_bustag = sa->sa_bustag;
	sc->sc_dmatag = sa->sa_dmatag;

	if (sa->sa_nreg < 2) {
		printf("%s: only %d register sets\n",
		    self->dv_xname, sa->sa_nreg);
		return;
	}

	if (sbus_bus_map(sa->sa_bustag, sa->sa_reg[0].sbr_slot,
	    (bus_addr_t)sa->sa_reg[0].sbr_offset,
	    (bus_size_t)sa->sa_reg[0].sbr_size, 0, 0, &sc->sc_cr) != 0) {
		printf("%s: cannot map registers\n", self->dv_xname);
		return;
	}

	if (sbus_bus_map(sa->sa_bustag, sa->sa_reg[1].sbr_slot,
	    (bus_addr_t)sa->sa_reg[1].sbr_offset,
	    (bus_size_t)sa->sa_reg[1].sbr_size, 0, 0, &sc->sc_mr) != 0) {
		printf("%s: cannot map registers\n", self->dv_xname);
		return;
	}

	sc->sc_rev = getpropint(node, "mace-version", -1);
	printf(" rev %x", sc->sc_rev);

	sc->sc_qec = qec;
	sc->sc_qr = qec->sc_regs;

	sc->sc_channel = getpropint(node, "channel#", -1);
	sc->sc_burst = qec->sc_burst;

	qestop(sc);

	/* Note: no interrupt level passed */
	if (bus_intr_establish(sa->sa_bustag, 0, IPL_NET, 0, qeintr, sc,
	    self->dv_xname) == NULL) {
		printf(": no interrupt established\n");
		return;
	}

	myetheraddr(sc->sc_arpcom.ac_enaddr);

	/*
	 * Allocate descriptor ring and buffers.
	 */

	/* for now, allocate as many bufs as there are ring descriptors */
	sc->sc_rb.rb_ntbuf = QEC_XD_RING_MAXSIZE;
	sc->sc_rb.rb_nrbuf = QEC_XD_RING_MAXSIZE;

	size =
	    QEC_XD_RING_MAXSIZE * sizeof(struct qec_xd) +
	    QEC_XD_RING_MAXSIZE * sizeof(struct qec_xd) +
	    sc->sc_rb.rb_ntbuf * QE_PKT_BUF_SZ +
	    sc->sc_rb.rb_nrbuf * QE_PKT_BUF_SZ;

	/* Get a DMA handle */
	if ((error = bus_dmamap_create(dmatag, size, 1, size, 0,
	    BUS_DMA_NOWAIT, &sc->sc_dmamap)) != 0) {
		printf("%s: DMA map create error %d\n", self->dv_xname, error);
		return;
	}

	/* Allocate DMA buffer */
	if ((error = bus_dmamem_alloc(dmatag, size, 0, 0,
	    &seg, 1, &rseg, BUS_DMA_NOWAIT)) != 0) {
		printf("%s: DMA buffer alloc error %d\n",
			self->dv_xname, error);
		return;
	}

	/* Map DMA buffer in CPU addressable space */
	if ((error = bus_dmamem_map(dmatag, &seg, rseg, size,
	    &sc->sc_rb.rb_membase,
	    BUS_DMA_NOWAIT|BUS_DMA_COHERENT)) != 0) {
		printf("%s: DMA buffer map error %d\n",
		    self->dv_xname, error);
		bus_dmamem_free(dmatag, &seg, rseg);
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

	/* Initialize media properties */
	ifmedia_init(&sc->sc_ifmedia, 0, qe_ifmedia_upd, qe_ifmedia_sts);
	ifmedia_add(&sc->sc_ifmedia,
	    IFM_MAKEWORD(IFM_ETHER,IFM_10_T,0,0), 0, NULL);
	ifmedia_add(&sc->sc_ifmedia,
	    IFM_MAKEWORD(IFM_ETHER,IFM_10_5,0,0), 0, NULL);
	ifmedia_add(&sc->sc_ifmedia,
	    IFM_MAKEWORD(IFM_ETHER,IFM_AUTO,0,0), 0, NULL);
	ifmedia_set(&sc->sc_ifmedia, IFM_ETHER|IFM_AUTO);

	bcopy(sc->sc_dev.dv_xname, ifp->if_xname, IFNAMSIZ);
	ifp->if_softc = sc;
	ifp->if_start = qestart;
	ifp->if_ioctl = qeioctl;
	ifp->if_watchdog = qewatchdog;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX |
	    IFF_MULTICAST;

	/* Attach the interface. */
	if_attach(ifp);
	ether_ifattach(ifp);

	printf(" address %s\n", ether_sprintf(sc->sc_arpcom.ac_enaddr));
}

/*
 * Pull data off an interface.
 * Len is the length of data, with local net header stripped.
 * We copy the data into mbufs.  When full cluster sized units are present,
 * we copy into clusters.
 */
struct mbuf *
qe_get(struct qe_softc *sc, int idx, int totlen)
{
	struct mbuf *m;
	struct mbuf *top, **mp;
	int len, pad, boff = 0;
	caddr_t bp;

	bp = sc->sc_rb.rb_rxbuf + (idx % sc->sc_rb.rb_nrbuf) * QE_PKT_BUF_SZ;

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
 * Routine to copy from mbuf chain to transmit buffer in
 * network buffer memory.
 */
__inline__ int
qe_put(struct qe_softc *sc, int idx, struct mbuf *m)
{
	struct mbuf *n;
	int len, tlen = 0, boff = 0;
	caddr_t bp;

	bp = sc->sc_rb.rb_txbuf + (idx % sc->sc_rb.rb_ntbuf) * QE_PKT_BUF_SZ;

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
 * Pass a packet to the higher levels.
 */
__inline__ void
qe_read(struct qe_softc *sc, int idx, int len)
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
	m = qe_get(sc, idx, len);
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
qestart(struct ifnet *ifp)
{
	struct qe_softc *sc = (struct qe_softc *)ifp->if_softc;
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
		len = qe_put(sc, bix, m);

		/*
		 * Initialize transmit registers and start transmission
		 */
		txd[bix].xd_flags = QEC_XD_OWN | QEC_XD_SOP | QEC_XD_EOP |
		    (len & QEC_XD_LENGTH);
		bus_space_write_4(sc->sc_bustag, sc->sc_cr, QE_CRI_CTRL,
		    QE_CR_CTRL_TWAKEUP);

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
qestop(struct qe_softc *sc)
{
	bus_space_tag_t t = sc->sc_bustag;
	bus_space_handle_t mr = sc->sc_mr;
	bus_space_handle_t cr = sc->sc_cr;
	int n;

	/* Stop the schwurst */
	bus_space_write_1(t, mr, QE_MRI_BIUCC, QE_MR_BIUCC_SWRST);
	for (n = 200; n > 0; n--) {
		if ((bus_space_read_1(t, mr, QE_MRI_BIUCC) &
		    QE_MR_BIUCC_SWRST) == 0)
			break;
		DELAY(20);
	}

	/* then reset */
	bus_space_write_4(t, cr, QE_CRI_CTRL, QE_CR_CTRL_RESET);
	for (n = 200; n > 0; n--) {
		if ((bus_space_read_4(t, cr, QE_CRI_CTRL) &
		    QE_CR_CTRL_RESET) == 0)
			break;
		DELAY(20);
	}
}

/*
 * Reset interface.
 */
void
qereset(struct qe_softc *sc)
{
	int s;

	s = splnet();
	qestop(sc);
	qeinit(sc);
	splx(s);
}

void
qewatchdog(struct ifnet *ifp)
{
	struct qe_softc *sc = ifp->if_softc;

	log(LOG_ERR, "%s: device timeout\n", sc->sc_dev.dv_xname);
	ifp->if_oerrors++;

	qereset(sc);
}

/*
 * Interrupt dispatch.
 */
int
qeintr(void *arg)
{
	struct qe_softc *sc = (struct qe_softc *)arg;
	bus_space_tag_t t = sc->sc_bustag;
	u_int32_t qecstat, qestat;
	int r = 0;

	/* Read QEC status and channel status */
	qecstat = bus_space_read_4(t, sc->sc_qr, QEC_QRI_STAT);
#ifdef QEDEBUG
	if (sc->sc_debug) {
		printf("qe%d: intr: qecstat=%x\n", sc->sc_channel, qecstat);
	}
#endif

	/* Filter out status for this channel */
	qecstat = qecstat >> (4 * sc->sc_channel);
	if ((qecstat & 0xf) == 0)
		return (r);

	qestat = bus_space_read_4(t, sc->sc_cr, QE_CRI_STAT);

#ifdef QEDEBUG
	if (sc->sc_debug) {
		int i;
		bus_space_tag_t t = sc->sc_bustag;
		bus_space_handle_t mr = sc->sc_mr;

		printf("qe%d: intr: qestat=%b\n", sc->sc_channel,
		    qestat, QE_CR_STAT_BITS);

		printf("MACE registers:\n");
		for (i = 0 ; i < 32; i++) {
			printf("  m[%d]=%x,", i, bus_space_read_1(t, mr, i));
			if (((i+1) & 7) == 0)
				printf("\n");
		}
	}
#endif

	if (qestat & QE_CR_STAT_ALLERRORS) {
#ifdef QEDEBUG
		if (sc->sc_debug)
			printf("qe%d: eint: qestat=%b\n", sc->sc_channel,
			    qestat, QE_CR_STAT_BITS);
#endif
		r |= qe_eint(sc, qestat);
		if (r == -1)
			return (1);
	}

	if (qestat & QE_CR_STAT_TXIRQ)
		r |= qe_tint(sc);

	if (qestat & QE_CR_STAT_RXIRQ)
		r |= qe_rint(sc);

	return (1);
}

/*
 * Transmit interrupt.
 */
int
qe_tint(struct qe_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	unsigned int bix, txflags;

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

	if (sc->sc_rb.rb_td_nbusy == 0)
		ifp->if_timer = 0;

	if (sc->sc_rb.rb_tdtail != bix) {
		sc->sc_rb.rb_tdtail = bix;
		if (ifq_is_oactive(&ifp->if_snd)) {
			ifq_clr_oactive(&ifp->if_snd);
			qestart(ifp);
		}
	}

	return (1);
}

/*
 * Receive interrupt.
 */
int
qe_rint(struct qe_softc *sc)
{
	struct qec_xd *xd = sc->sc_rb.rb_rxd;
	unsigned int bix, len;
	unsigned int nrbuf = sc->sc_rb.rb_nrbuf;
#ifdef QEDEBUG
	int npackets = 0;
#endif

	bix = sc->sc_rb.rb_rdtail;

	/*
	 * Process all buffers with valid data.
	 */
	for (;;) {
		len = xd[bix].xd_flags;
		if (len & QEC_XD_OWN)
			break;

#ifdef QEDEBUG
		npackets++;
#endif

		len &= QEC_XD_LENGTH;
		len -= 4;
		qe_read(sc, bix, len);

		/* ... */
		xd[(bix+nrbuf) % QEC_XD_RING_MAXSIZE].xd_flags =
		    QEC_XD_OWN | (QE_PKT_BUF_SZ & QEC_XD_LENGTH);

		if (++bix == QEC_XD_RING_MAXSIZE)
			bix = 0;
	}
#ifdef QEDEBUG
	if (npackets == 0 && sc->sc_debug)
		printf("%s: rint: no packets; rb index %d; status 0x%x\n",
		    sc->sc_dev.dv_xname, bix, len);
#endif

	sc->sc_rb.rb_rdtail = bix;

	return (1);
}

/*
 * Error interrupt.
 */
int
qe_eint(struct qe_softc *sc, u_int32_t why)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	int r = 0, rst = 0;

	if (why & QE_CR_STAT_EDEFER) {
		printf("%s: excessive tx defers.\n", sc->sc_dev.dv_xname);
		r |= 1;
		ifp->if_oerrors++;
	}

	if (why & QE_CR_STAT_CLOSS) {
		ifp->if_oerrors++;
		r |= 1;
	}

	if (why & QE_CR_STAT_ERETRIES) {
		printf("%s: excessive tx retries\n", sc->sc_dev.dv_xname);
		ifp->if_oerrors++;
		r |= 1;
		rst = 1;
	}


	if (why & QE_CR_STAT_LCOLL) {
		printf("%s: late tx transmission\n", sc->sc_dev.dv_xname);
		ifp->if_oerrors++;
		r |= 1;
		rst = 1;
	}

	if (why & QE_CR_STAT_FUFLOW) {
		printf("%s: tx fifo underflow\n", sc->sc_dev.dv_xname);
		ifp->if_oerrors++;
		r |= 1;
		rst = 1;
	}

	if (why & QE_CR_STAT_JERROR) {
		printf("%s: jabber seen\n", sc->sc_dev.dv_xname);
		r |= 1;
	}

	if (why & QE_CR_STAT_BERROR) {
		printf("%s: babble seen\n", sc->sc_dev.dv_xname);
		r |= 1;
	}

	if (why & QE_CR_STAT_TCCOFLOW) {
		ifp->if_collisions += 256;
		ifp->if_oerrors += 256;
		r |= 1;
	}

	if (why & QE_CR_STAT_TXDERROR) {
		printf("%s: tx descriptor is bad\n", sc->sc_dev.dv_xname);
		rst = 1;
		r |= 1;
	}

	if (why & QE_CR_STAT_TXLERR) {
		printf("%s: tx late error\n", sc->sc_dev.dv_xname);
		ifp->if_oerrors++;
		rst = 1;
		r |= 1;
	}

	if (why & QE_CR_STAT_TXPERR) {
		printf("%s: tx dma parity error\n", sc->sc_dev.dv_xname);
		ifp->if_oerrors++;
		rst = 1;
		r |= 1;
	}

	if (why & QE_CR_STAT_TXSERR) {
		printf("%s: tx dma sbus error ack\n", sc->sc_dev.dv_xname);
		ifp->if_oerrors++;
		rst = 1;
		r |= 1;
	}

	if (why & QE_CR_STAT_RCCOFLOW) {
		ifp->if_collisions += 256;
		ifp->if_ierrors += 256;
		r |= 1;
	}

	if (why & QE_CR_STAT_RUOFLOW) {
		ifp->if_ierrors += 256;
		r |= 1;
	}

	if (why & QE_CR_STAT_MCOFLOW) {
		ifp->if_ierrors += 256;
		r |= 1;
	}

	if (why & QE_CR_STAT_RXFOFLOW) {
		printf("%s: rx fifo overflow\n", sc->sc_dev.dv_xname);
		ifp->if_ierrors++;
		r |= 1;
	}

	if (why & QE_CR_STAT_RLCOLL) {
		printf("%s: rx late collision\n", sc->sc_dev.dv_xname);
		ifp->if_ierrors++;
		ifp->if_collisions++;
		r |= 1;
	}

	if (why & QE_CR_STAT_FCOFLOW) {
		ifp->if_ierrors += 256;
		r |= 1;
	}

	if (why & QE_CR_STAT_CECOFLOW) {
		ifp->if_ierrors += 256;
		r |= 1;
	}

	if (why & QE_CR_STAT_RXDROP) {
		printf("%s: rx packet dropped\n", sc->sc_dev.dv_xname);
		ifp->if_ierrors++;
		r |= 1;
	}

	if (why & QE_CR_STAT_RXSMALL) {
		printf("%s: rx buffer too small\n", sc->sc_dev.dv_xname);
		ifp->if_ierrors++;
		r |= 1;
		rst = 1;
	}

	if (why & QE_CR_STAT_RXLERR) {
		printf("%s: rx late error\n", sc->sc_dev.dv_xname);
		ifp->if_ierrors++;
		r |= 1;
		rst = 1;
	}

	if (why & QE_CR_STAT_RXPERR) {
		printf("%s: rx dma parity error\n", sc->sc_dev.dv_xname);
		ifp->if_ierrors++;
		r |= 1;
		rst = 1;
	}

	if (why & QE_CR_STAT_RXSERR) {
		printf("%s: rx dma sbus error ack\n", sc->sc_dev.dv_xname);
		ifp->if_ierrors++;
		r |= 1;
		rst = 1;
	}

	if (r == 0)
		printf("%s: unexpected interrupt error: %08x\n",
			sc->sc_dev.dv_xname, why);

	if (rst) {
		printf("%s: resetting...\n", sc->sc_dev.dv_xname);
		qereset(sc);
		return (-1);
	}

	return (r);
}

int
qeioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct qe_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int s, error = 0;

	s = splnet();

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		qeinit(sc);
		break;

	case SIOCSIFFLAGS:
		if ((ifp->if_flags & IFF_UP) == 0 &&
		    (ifp->if_flags & IFF_RUNNING) != 0) {
			/*
			 * If interface is marked down and it is running, then
			 * stop it.
			 */
			qestop(sc);
			ifp->if_flags &= ~IFF_RUNNING;
		} else if ((ifp->if_flags & IFF_UP) != 0 &&
			   (ifp->if_flags & IFF_RUNNING) == 0) {
			/*
			 * If interface is marked up and it is stopped, then
			 * start it.
			 */
			qeinit(sc);
		} else {
			/*
			 * Reset the interface to pick up changes in any other
			 * flags that affect hardware registers.
			 */
			qestop(sc);
			qeinit(sc);
		}
#ifdef QEDEBUG
		sc->sc_debug = (ifp->if_flags & IFF_DEBUG) != 0 ? 1 : 0;
#endif
		break;

	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_ifmedia, cmd);
		break;

	default:
		error = ether_ioctl(ifp, &sc->sc_arpcom, cmd, data);
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			qe_mcreset(sc);
		error = 0;
	}

	splx(s);
	return (error);
}


void
qeinit(struct qe_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	bus_space_tag_t t = sc->sc_bustag;
	bus_space_handle_t cr = sc->sc_cr;
	bus_space_handle_t mr = sc->sc_mr;
	struct qec_softc *qec = sc->sc_qec;
	u_int32_t qecaddr;
	u_int8_t *ea;
	int s;

	s = splnet();

	qestop(sc);

	/*
	 * Allocate descriptor ring and buffers
	 */
	qec_meminit(&sc->sc_rb, QE_PKT_BUF_SZ);

	/* Channel registers: */
	bus_space_write_4(t, cr, QE_CRI_RXDS, (u_int32_t)sc->sc_rb.rb_rxddma);
	bus_space_write_4(t, cr, QE_CRI_TXDS, (u_int32_t)sc->sc_rb.rb_txddma);

	bus_space_write_4(t, cr, QE_CRI_RIMASK, 0);
	bus_space_write_4(t, cr, QE_CRI_TIMASK, 0);
	bus_space_write_4(t, cr, QE_CRI_QMASK, 0);
	bus_space_write_4(t, cr, QE_CRI_MMASK, QE_CR_MMASK_RXCOLL);
	bus_space_write_4(t, cr, QE_CRI_CCNT, 0);
	bus_space_write_4(t, cr, QE_CRI_PIPG, 0);

	qecaddr = sc->sc_channel * qec->sc_msize;
	bus_space_write_4(t, cr, QE_CRI_RXWBUF, qecaddr);
	bus_space_write_4(t, cr, QE_CRI_RXRBUF, qecaddr);
	bus_space_write_4(t, cr, QE_CRI_TXWBUF, qecaddr + qec->sc_rsize);
	bus_space_write_4(t, cr, QE_CRI_TXRBUF, qecaddr + qec->sc_rsize);

	/*
	 * When switching from mace<->qec always guarantee an sbus
	 * turnaround (if last op was read, perform a dummy write, and
	 * vice versa).
	 */
	bus_space_read_4(t, cr, QE_CRI_QMASK);

	/* MACE registers: */
	bus_space_write_1(t, mr, QE_MRI_PHYCC, QE_MR_PHYCC_ASEL);
	bus_space_write_1(t, mr, QE_MRI_XMTFC, QE_MR_XMTFC_APADXMT);
	bus_space_write_1(t, mr, QE_MRI_RCVFC, 0);

	/*
	 * Mask MACE's receive interrupt, since we're being notified
	 * by the QEC after DMA completes.
	 */
	bus_space_write_1(t, mr, QE_MRI_IMR,
	    QE_MR_IMR_CERRM | QE_MR_IMR_RCVINTM);

	bus_space_write_1(t, mr, QE_MRI_BIUCC,
	    QE_MR_BIUCC_BSWAP | QE_MR_BIUCC_64TS);

	bus_space_write_1(t, mr, QE_MRI_FIFOFC,
	    QE_MR_FIFOCC_TXF16 | QE_MR_FIFOCC_RXF32 |
	    QE_MR_FIFOCC_RFWU | QE_MR_FIFOCC_TFWU);

	bus_space_write_1(t, mr, QE_MRI_PLSCC, QE_MR_PLSCC_TP);

	/*
	 * Station address
	 */
	ea = sc->sc_arpcom.ac_enaddr;
	bus_space_write_1(t, mr, QE_MRI_IAC,
	    QE_MR_IAC_ADDRCHG | QE_MR_IAC_PHYADDR);
	bus_space_write_multi_1(t, mr, QE_MRI_PADR, ea, 6);

	/* Apply media settings */
	qe_ifmedia_upd(ifp);

	/*
	 * Clear Logical address filter
	 */
	bus_space_write_1(t, mr, QE_MRI_IAC,
	    QE_MR_IAC_ADDRCHG | QE_MR_IAC_LOGADDR);
	bus_space_set_multi_1(t, mr, QE_MRI_LADRF, 0, 8);
	bus_space_write_1(t, mr, QE_MRI_IAC, 0);

	/* Clear missed packet count (register cleared on read) */
	(void)bus_space_read_1(t, mr, QE_MRI_MPC);

#if 0
	/* test register: */
	bus_space_write_1(t, mr, QE_MRI_UTR, 0);
#endif

	/* Reset multicast filter */
	qe_mcreset(sc);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
	splx(s);
}

/*
 * Reset multicast filter.
 */
void
qe_mcreset(struct qe_softc *sc)
{
	struct arpcom *ac = &sc->sc_arpcom;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	bus_space_tag_t t = sc->sc_bustag;
	bus_space_handle_t mr = sc->sc_mr;
	struct ether_multi *enm;
	struct ether_multistep step;
	u_int32_t crc;
	u_int16_t hash[4];
	u_int8_t octet, maccc, *ladrp = (u_int8_t *)&hash[0];
	int i, j;

	/* We also enable transmitter & receiver here */
	maccc = QE_MR_MACCC_ENXMT | QE_MR_MACCC_ENRCV;

	if (ifp->if_flags & IFF_PROMISC) {
		maccc |= QE_MR_MACCC_PROM;
		bus_space_write_1(t, mr, QE_MRI_MACCC, maccc);
		return;
	}

	if (ac->ac_multirangecnt > 0)
		ifp->if_flags |= IFF_ALLMULTI;

	if (ifp->if_flags & IFF_ALLMULTI) {
		bus_space_write_1(t, mr, QE_MRI_IAC,
		    QE_MR_IAC_ADDRCHG | QE_MR_IAC_LOGADDR);
		bus_space_set_multi_1(t, mr, QE_MRI_LADRF, 0xff, 8);
		bus_space_write_1(t, mr, QE_MRI_IAC, 0);
		bus_space_write_1(t, mr, QE_MRI_MACCC, maccc);
		return;
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

	bus_space_write_1(t, mr, QE_MRI_IAC,
	    QE_MR_IAC_ADDRCHG | QE_MR_IAC_LOGADDR);
	bus_space_write_multi_1(t, mr, QE_MRI_LADRF, ladrp, 8);
	bus_space_write_1(t, mr, QE_MRI_IAC, 0);
	bus_space_write_1(t, mr, QE_MRI_MACCC, maccc);
}

/*
 * Get current media settings.
 */
void
qe_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct qe_softc *sc = ifp->if_softc;
	u_int8_t phycc;

	ifmr->ifm_active = IFM_ETHER | IFM_10_T;
	phycc = bus_space_read_1(sc->sc_bustag, sc->sc_mr, QE_MRI_PHYCC);
	if ((phycc & QE_MR_PHYCC_DLNKTST) == 0) {
		ifmr->ifm_status |= IFM_AVALID;
		if (phycc & QE_MR_PHYCC_LNKFL)
			ifmr->ifm_status &= ~IFM_ACTIVE;
		else
			ifmr->ifm_status |= IFM_ACTIVE;
	}
}

/*
 * Set media options.
 */
int
qe_ifmedia_upd(struct ifnet *ifp)
{
	struct qe_softc *sc = ifp->if_softc;
	uint64_t media = sc->sc_ifmedia.ifm_media;

	if (IFM_TYPE(media) != IFM_ETHER)
		return (EINVAL);

	if (IFM_SUBTYPE(media) != IFM_10_T)
		return (EINVAL);

	return (0);
}
