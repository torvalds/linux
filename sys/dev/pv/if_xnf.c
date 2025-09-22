/*	$OpenBSD: if_xnf.c,v 1.70 2024/05/24 10:05:55 jsg Exp $	*/

/*
 * Copyright (c) 2015, 2016 Mike Belopuhov
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

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/atomic.h>
#include <sys/device.h>
#include <sys/mbuf.h>
#include <sys/queue.h>
#include <sys/sockio.h>
#include <sys/task.h>

#include <machine/bus.h>

#include <dev/pv/xenreg.h>
#include <dev/pv/xenvar.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

/* #define XNF_DEBUG */

#ifdef XNF_DEBUG
#define DPRINTF(x...)		printf(x)
#else
#define DPRINTF(x...)
#endif

/*
 * Rx ring
 */

struct xnf_rx_req {
	uint16_t		 rxq_id;
	uint16_t		 rxq_pad;
	uint32_t		 rxq_ref;
} __packed;

struct xnf_rx_rsp {
	uint16_t		 rxp_id;
	uint16_t		 rxp_offset;
	uint16_t		 rxp_flags;
#define  XNF_RXF_CSUM_VALID	  0x0001
#define  XNF_RXF_CSUM_BLANK	  0x0002
#define  XNF_RXF_CHUNK		  0x0004
#define  XNF_RXF_MGMT		  0x0008
	int16_t			 rxp_status;
} __packed;

union xnf_rx_desc {
	struct xnf_rx_req	 rxd_req;
	struct xnf_rx_rsp	 rxd_rsp;
} __packed;

#define XNF_RX_DESC		256
#define XNF_MCLEN		PAGE_SIZE
#define XNF_RX_MIN		32

struct xnf_rx_ring {
	volatile uint32_t	 rxr_prod;
	volatile uint32_t	 rxr_prod_event;
	volatile uint32_t	 rxr_cons;
	volatile uint32_t	 rxr_cons_event;
	uint32_t		 rxr_reserved[12];
	union xnf_rx_desc	 rxr_desc[XNF_RX_DESC];
} __packed;


/*
 * Tx ring
 */

struct xnf_tx_req {
	uint32_t		 txq_ref;
	uint16_t		 txq_offset;
	uint16_t		 txq_flags;
#define  XNF_TXF_CSUM_BLANK	  0x0001
#define  XNF_TXF_CSUM_VALID	  0x0002
#define  XNF_TXF_CHUNK		  0x0004
#define  XNF_TXF_ETXRA		  0x0008
	uint16_t		 txq_id;
	uint16_t		 txq_size;
} __packed;

struct xnf_tx_rsp {
	uint16_t		 txp_id;
	int16_t			 txp_status;
} __packed;

union xnf_tx_desc {
	struct xnf_tx_req	 txd_req;
	struct xnf_tx_rsp	 txd_rsp;
} __packed;

#define XNF_TX_DESC		256
#define XNF_TX_FRAG		18

struct xnf_tx_ring {
	volatile uint32_t	 txr_prod;
	volatile uint32_t	 txr_prod_event;
	volatile uint32_t	 txr_cons;
	volatile uint32_t	 txr_cons_event;
	uint32_t		 txr_reserved[12];
	union xnf_tx_desc	 txr_desc[XNF_TX_DESC];
} __packed;

struct xnf_tx_buf {
	uint32_t		 txb_ndesc;
	bus_dmamap_t		 txb_dmap;
	struct mbuf		*txb_mbuf;
};

/* Management frame, "extra info" in Xen parlance */
struct xnf_mgmt {
	uint8_t			 mg_type;
#define  XNF_MGMT_MCAST_ADD	2
#define  XNF_MGMT_MCAST_DEL	3
	uint8_t			 mg_flags;
	union {
		uint8_t		 mgu_mcaddr[ETHER_ADDR_LEN];
		uint16_t	 mgu_pad[3];
	} u;
#define mg_mcaddr		 u.mgu_mcaddr
} __packed;


struct xnf_softc {
	struct device		 sc_dev;
	struct device		*sc_parent;
	char			 sc_node[XEN_MAX_NODE_LEN];
	char			 sc_backend[XEN_MAX_BACKEND_LEN];
	bus_dma_tag_t		 sc_dmat;
	int			 sc_domid;

	struct arpcom		 sc_ac;
	struct ifmedia		 sc_media;

	xen_intr_handle_t	 sc_xih;

	int			 sc_caps;
#define  XNF_CAP_SG		  0x0001
#define  XNF_CAP_CSUM4		  0x0002
#define  XNF_CAP_CSUM6		  0x0004
#define  XNF_CAP_MCAST		  0x0008
#define  XNF_CAP_SPLIT		  0x0010
#define  XNF_CAP_MULTIQ		  0x0020

	/* Rx ring */
	struct xnf_rx_ring	*sc_rx_ring;
	bus_dmamap_t		 sc_rx_rmap;		  /* map for the ring */
	bus_dma_segment_t	 sc_rx_seg;
	uint32_t		 sc_rx_ref;		  /* grant table ref */
	uint32_t		 sc_rx_cons;
	struct mbuf		*sc_rx_buf[XNF_RX_DESC];
	bus_dmamap_t		 sc_rx_dmap[XNF_RX_DESC]; /* maps for packets */
	struct mbuf		*sc_rx_cbuf[2];	  	  /* chain handling */

	/* Tx ring */
	struct xnf_tx_ring	*sc_tx_ring;
	bus_dmamap_t		 sc_tx_rmap;		  /* map for the ring */
	bus_dma_segment_t	 sc_tx_seg;
	uint32_t		 sc_tx_ref;		  /* grant table ref */
	uint32_t		 sc_tx_cons;
	int			 sc_tx_frags;
	uint32_t		 sc_tx_next;		  /* next buffer */
	volatile unsigned int	 sc_tx_avail;
	struct xnf_tx_buf	 sc_tx_buf[XNF_TX_DESC];
};

int	xnf_match(struct device *, void *, void *);
void	xnf_attach(struct device *, struct device *, void *);
int	xnf_detach(struct device *, int);
int	xnf_lladdr(struct xnf_softc *);
int	xnf_ioctl(struct ifnet *, u_long, caddr_t);
int	xnf_media_change(struct ifnet *);
void	xnf_media_status(struct ifnet *, struct ifmediareq *);
int	xnf_iff(struct xnf_softc *);
void	xnf_init(struct xnf_softc *);
void	xnf_stop(struct xnf_softc *);
void	xnf_start(struct ifqueue *);
int	xnf_encap(struct xnf_softc *, struct mbuf *, uint32_t *);
void	xnf_intr(void *);
void	xnf_watchdog(struct ifnet *);
void	xnf_txeof(struct xnf_softc *);
void	xnf_rxeof(struct xnf_softc *);
int	xnf_rx_ring_fill(struct xnf_softc *);
int	xnf_rx_ring_create(struct xnf_softc *);
void	xnf_rx_ring_drain(struct xnf_softc *);
void	xnf_rx_ring_destroy(struct xnf_softc *);
int	xnf_tx_ring_create(struct xnf_softc *);
void	xnf_tx_ring_drain(struct xnf_softc *);
void	xnf_tx_ring_destroy(struct xnf_softc *);
int	xnf_capabilities(struct xnf_softc *sc);
int	xnf_init_backend(struct xnf_softc *);

struct cfdriver xnf_cd = {
	NULL, "xnf", DV_IFNET
};

const struct cfattach xnf_ca = {
	sizeof(struct xnf_softc), xnf_match, xnf_attach, xnf_detach
};

int
xnf_match(struct device *parent, void *match, void *aux)
{
	struct xen_attach_args *xa = aux;

	if (strcmp("vif", xa->xa_name))
		return (0);

	return (1);
}

void
xnf_attach(struct device *parent, struct device *self, void *aux)
{
	struct xen_attach_args *xa = aux;
	struct xnf_softc *sc = (struct xnf_softc *)self;
	struct ifnet *ifp = &sc->sc_ac.ac_if;

	sc->sc_parent = parent;
	sc->sc_dmat = xa->xa_dmat;
	sc->sc_domid = xa->xa_domid;

	memcpy(sc->sc_node, xa->xa_node, XEN_MAX_NODE_LEN);
	memcpy(sc->sc_backend, xa->xa_backend, XEN_MAX_BACKEND_LEN);

	strlcpy(ifp->if_xname, sc->sc_dev.dv_xname, IFNAMSIZ);

	if (xnf_lladdr(sc)) {
		printf(": failed to obtain MAC address\n");
		return;
	}

	if (xen_intr_establish(0, &sc->sc_xih, sc->sc_domid, xnf_intr, sc,
	    ifp->if_xname)) {
		printf(": failed to establish an interrupt\n");
		return;
	}
	xen_intr_mask(sc->sc_xih);

	printf(" backend %d channel %u: address %s\n", sc->sc_domid,
	    sc->sc_xih, ether_sprintf(sc->sc_ac.ac_enaddr));

	if (xnf_capabilities(sc)) {
		xen_intr_disestablish(sc->sc_xih);
		return;
	}

	if (sc->sc_caps & XNF_CAP_SG)
		ifp->if_hardmtu = 9000;

	if (xnf_rx_ring_create(sc)) {
		xen_intr_disestablish(sc->sc_xih);
		return;
	}
	if (xnf_tx_ring_create(sc)) {
		xen_intr_disestablish(sc->sc_xih);
		xnf_rx_ring_destroy(sc);
		return;
	}
	if (xnf_init_backend(sc)) {
		xen_intr_disestablish(sc->sc_xih);
		xnf_rx_ring_destroy(sc);
		xnf_tx_ring_destroy(sc);
		return;
	}

	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_xflags = IFXF_MPSAFE;
	ifp->if_ioctl = xnf_ioctl;
	ifp->if_qstart = xnf_start;
	ifp->if_watchdog = xnf_watchdog;
	ifp->if_softc = sc;

	ifp->if_capabilities = IFCAP_VLAN_MTU;
	if (sc->sc_caps & XNF_CAP_CSUM4)
		ifp->if_capabilities |= IFCAP_CSUM_TCPv4 | IFCAP_CSUM_UDPv4;
	if (sc->sc_caps & XNF_CAP_CSUM6)
		ifp->if_capabilities |= IFCAP_CSUM_TCPv6 | IFCAP_CSUM_UDPv6;

	ifq_init_maxlen(&ifp->if_snd, XNF_TX_DESC - 1);

	ifmedia_init(&sc->sc_media, IFM_IMASK, xnf_media_change,
	    xnf_media_status);
	ifmedia_add(&sc->sc_media, IFM_ETHER | IFM_MANUAL, 0, NULL);
	ifmedia_set(&sc->sc_media, IFM_ETHER | IFM_MANUAL);

	if_attach(ifp);
	ether_ifattach(ifp);

	/* Kick out emulated em's and re's */
	xen_unplug_emulated(parent, XEN_UNPLUG_NIC);
}

int
xnf_detach(struct device *self, int flags)
{
	struct xnf_softc *sc = (struct xnf_softc *)self;
	struct ifnet *ifp = &sc->sc_ac.ac_if;

	xnf_stop(sc);

	ether_ifdetach(ifp);
	if_detach(ifp);

	xen_intr_disestablish(sc->sc_xih);

	if (sc->sc_tx_ring)
		xnf_tx_ring_destroy(sc);
	if (sc->sc_rx_ring)
		xnf_rx_ring_destroy(sc);

	return (0);
}

static int
nibble(int ch)
{
	if (ch >= '0' && ch <= '9')
		return (ch - '0');
	if (ch >= 'A' && ch <= 'F')
		return (10 + ch - 'A');
	if (ch >= 'a' && ch <= 'f')
		return (10 + ch - 'a');
	return (-1);
}

int
xnf_lladdr(struct xnf_softc *sc)
{
	char enaddr[ETHER_ADDR_LEN];
	char mac[32];
	int i, j, lo, hi;

	if (xs_getprop(sc->sc_parent, sc->sc_backend, "mac", mac, sizeof(mac)))
		return (-1);

	for (i = 0, j = 0; j < ETHER_ADDR_LEN; i += 3, j++) {
		if ((hi = nibble(mac[i])) == -1 ||
		    (lo = nibble(mac[i+1])) == -1)
			return (-1);
		enaddr[j] = hi << 4 | lo;
	}

	memcpy(sc->sc_ac.ac_enaddr, enaddr, ETHER_ADDR_LEN);
	return (0);
}

int
xnf_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct xnf_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int s, error = 0;

	s = splnet();

	switch (command) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		if (!(ifp->if_flags & IFF_RUNNING))
			xnf_init(sc);
		break;
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				error = ENETRESET;
			else
				xnf_init(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				xnf_stop(sc);
		}
		break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_media, command);
		break;
	default:
		error = ether_ioctl(ifp, &sc->sc_ac, command, data);
		break;
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			xnf_iff(sc);
		error = 0;
	}

	splx(s);

	return (error);
}

int
xnf_media_change(struct ifnet *ifp)
{
	return (0);
}

void
xnf_media_status(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	ifmr->ifm_status = IFM_ACTIVE | IFM_AVALID;
	ifmr->ifm_active = IFM_ETHER | IFM_MANUAL;
}

int
xnf_iff(struct xnf_softc *sc)
{
	return (0);
}

void
xnf_init(struct xnf_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;

	xnf_stop(sc);

	xnf_iff(sc);

	xnf_rx_ring_fill(sc);

	if (xen_intr_unmask(sc->sc_xih)) {
		printf("%s: failed to enable interrupts\n", ifp->if_xname);
		xnf_stop(sc);
		return;
	}

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
}

void
xnf_stop(struct xnf_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;

	ifp->if_flags &= ~IFF_RUNNING;

	xen_intr_mask(sc->sc_xih);

	ifp->if_timer = 0;

	ifq_barrier(&ifp->if_snd);
	xen_intr_barrier(sc->sc_xih);

	ifq_clr_oactive(&ifp->if_snd);

	if (sc->sc_tx_ring)
		xnf_tx_ring_drain(sc);
	if (sc->sc_rx_ring)
		xnf_rx_ring_drain(sc);
}

void
xnf_start(struct ifqueue *ifq)
{
	struct ifnet *ifp = ifq->ifq_if;
	struct xnf_softc *sc = ifp->if_softc;
	struct xnf_tx_ring *txr = sc->sc_tx_ring;
	struct mbuf *m;
	int pkts = 0;
	uint32_t prod, oprod;

	bus_dmamap_sync(sc->sc_dmat, sc->sc_tx_rmap, 0, 0,
	    BUS_DMASYNC_POSTREAD);

	prod = oprod = txr->txr_prod;

	for (;;) {
		if (((XNF_TX_DESC - (prod - sc->sc_tx_cons)) <
		    sc->sc_tx_frags) || !sc->sc_tx_avail) {
			/* transient */
			ifq_set_oactive(ifq);
			break;
		}

		m = ifq_dequeue(ifq);
		if (m == NULL)
			break;

		if (xnf_encap(sc, m, &prod)) {
			/* the chain is too large */
			ifp->if_oerrors++;
			m_freem(m);
			continue;
		}

#if NBPFILTER > 0
		if (ifp->if_bpf)
			bpf_mtap_ether(ifp->if_bpf, m, BPF_DIRECTION_OUT);
#endif
		pkts++;
	}
	if (pkts > 0) {
		txr->txr_prod = prod;
		if (txr->txr_cons_event <= txr->txr_cons)
			txr->txr_cons_event = txr->txr_cons +
			    ((txr->txr_prod - txr->txr_cons) >> 1) + 1;
		bus_dmamap_sync(sc->sc_dmat, sc->sc_tx_rmap, 0, 0,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
		if (prod - txr->txr_prod_event < prod - oprod)
			xen_intr_signal(sc->sc_xih);
		ifp->if_timer = 5;
	}
}

static inline int
xnf_fragcount(struct mbuf *m_head)
{
	struct mbuf *m;
	vaddr_t va, va0;
	int n = 0;

	for (m = m_head; m != NULL; m = m->m_next) {
		if (m->m_len == 0)
			continue;
		     /* start of the buffer */
		for (va0 = va = mtod(m, vaddr_t);
		     /* does the buffer end on this page? */
		     va + (PAGE_SIZE - (va & PAGE_MASK)) < va0 + m->m_len;
		     /* move on to the next page */
		     va += PAGE_SIZE - (va & PAGE_MASK))
			n++;
		n++;
	}
	return (n);
}

int
xnf_encap(struct xnf_softc *sc, struct mbuf *m_head, uint32_t *prod)
{
	struct xnf_tx_ring *txr = sc->sc_tx_ring;
	struct xnf_tx_buf *txb = NULL;
	union xnf_tx_desc *txd = NULL;
	struct mbuf *m, **next;
	uint32_t oprod = *prod;
	uint16_t id;
	int i, flags, n, used = 0;

	if ((xnf_fragcount(m_head) > sc->sc_tx_frags) &&
	    m_defrag(m_head, M_DONTWAIT))
		return (ENOBUFS);

	flags = (sc->sc_domid << 16) | BUS_DMA_WRITE | BUS_DMA_NOWAIT;

	next = &m_head->m_next;
	for (m = m_head; m != NULL; m = *next) {
		/* Unlink and free zero length nodes. */
		if (m->m_len == 0) {
			*next = m->m_next;
			m_free(m);
			continue;
		}
		next = &m->m_next;

		i = *prod & (XNF_TX_DESC - 1);
		txd = &txr->txr_desc[i];

		/*
		 * Find an unused TX buffer.  We're guaranteed to find one
		 * because xnf_encap cannot be called with sc_tx_avail == 0.
		 */
		do {
			id = sc->sc_tx_next++ & (XNF_TX_DESC - 1);
			txb = &sc->sc_tx_buf[id];
		} while (txb->txb_mbuf);

		if (bus_dmamap_load(sc->sc_dmat, txb->txb_dmap, m->m_data,
		    m->m_len, NULL, flags)) {
			DPRINTF("%s: failed to load %u bytes @%lu\n",
			    sc->sc_dev.dv_xname, m->m_len,
			    mtod(m, vaddr_t) & PAGE_MASK);
			goto unroll;
		}

		for (n = 0; n < txb->txb_dmap->dm_nsegs; n++) {
			i = *prod & (XNF_TX_DESC - 1);
			txd = &txr->txr_desc[i];

			if (m == m_head && n == 0) {
				if (m->m_pkthdr.csum_flags &
				    (M_TCP_CSUM_OUT | M_UDP_CSUM_OUT))
					txd->txd_req.txq_flags =
					    XNF_TXF_CSUM_BLANK |
					    XNF_TXF_CSUM_VALID;
				txd->txd_req.txq_size = m->m_pkthdr.len;
			} else {
				txd->txd_req.txq_size =
				    txb->txb_dmap->dm_segs[n].ds_len;
			}
			txd->txd_req.txq_ref =
			    txb->txb_dmap->dm_segs[n].ds_addr;
			if (n == 0)
				txd->txd_req.txq_offset =
				    mtod(m, vaddr_t) & PAGE_MASK;
			/* The chunk flag will be removed from the last one */
			txd->txd_req.txq_flags |= XNF_TXF_CHUNK;
			txd->txd_req.txq_id = id;

			txb->txb_ndesc++;
			(*prod)++;
		}

		txb->txb_mbuf = m;
		used++;
	}

	/* Clear the chunk flag from the last segment */
	txd->txd_req.txq_flags &= ~XNF_TXF_CHUNK;
	bus_dmamap_sync(sc->sc_dmat, sc->sc_tx_rmap, 0, 0,
	    BUS_DMASYNC_PREWRITE);

	KASSERT(sc->sc_tx_avail > used);
	atomic_sub_int(&sc->sc_tx_avail, used);

	return (0);

 unroll:
	DPRINTF("%s: unrolling from %u to %u\n", sc->sc_dev.dv_xname,
	    *prod, oprod);
	for (; *prod != oprod; (*prod)--) {
		i = (*prod - 1) & (XNF_TX_DESC - 1);
		txd = &txr->txr_desc[i];
		id = txd->txd_req.txq_id;
		txb = &sc->sc_tx_buf[id];

		memset(txd, 0, sizeof(*txd));

		if (txb->txb_mbuf) {
			bus_dmamap_sync(sc->sc_dmat, txb->txb_dmap, 0, 0,
			    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_dmat, txb->txb_dmap);

			txb->txb_mbuf = NULL;
			txb->txb_ndesc = 0;
		}
	}
	return (ENOBUFS);
}

void
xnf_intr(void *arg)
{
	struct xnf_softc *sc = arg;
	struct ifnet *ifp = &sc->sc_ac.ac_if;

	if (ifp->if_flags & IFF_RUNNING) {
		xnf_txeof(sc);
		xnf_rxeof(sc);
	}
}

void
xnf_watchdog(struct ifnet *ifp)
{
	struct xnf_softc *sc = ifp->if_softc;
	struct xnf_tx_ring *txr = sc->sc_tx_ring;

	printf("%s: tx stuck: prod %u cons %u,%u evt %u,%u\n",
	    ifp->if_xname, txr->txr_prod, txr->txr_cons, sc->sc_tx_cons,
	    txr->txr_prod_event, txr->txr_cons_event);
}

void
xnf_txeof(struct xnf_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct xnf_tx_ring *txr = sc->sc_tx_ring;
	struct xnf_tx_buf *txb;
	union xnf_tx_desc *txd;
	uint done = 0;
	uint32_t cons;
	uint16_t id;
	int i;

	bus_dmamap_sync(sc->sc_dmat, sc->sc_tx_rmap, 0, 0,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	for (cons = sc->sc_tx_cons; cons != txr->txr_cons; cons++) {
		i = cons & (XNF_TX_DESC - 1);
		txd = &txr->txr_desc[i];
		id = txd->txd_rsp.txp_id;
		KASSERT(id < XNF_TX_DESC);
		txb = &sc->sc_tx_buf[id];

		KASSERT(txb->txb_ndesc > 0);
		if (--txb->txb_ndesc == 0) {
			bus_dmamap_sync(sc->sc_dmat, txb->txb_dmap, 0, 0,
			    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_dmat, txb->txb_dmap);

			m_free(txb->txb_mbuf);
			txb->txb_mbuf = NULL;
			done++;
		}

		memset(txd, 0, sizeof(*txd));
	}

	sc->sc_tx_cons = cons;
	txr->txr_cons_event = sc->sc_tx_cons +
	    ((txr->txr_prod - sc->sc_tx_cons) >> 1) + 1;
	bus_dmamap_sync(sc->sc_dmat, sc->sc_tx_rmap, 0, 0,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	atomic_add_int(&sc->sc_tx_avail, done);

	if (sc->sc_tx_cons == txr->txr_prod)
		ifp->if_timer = 0;
	if (ifq_is_oactive(&ifp->if_snd))
		ifq_restart(&ifp->if_snd);
}

void
xnf_rxeof(struct xnf_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct xnf_rx_ring *rxr = sc->sc_rx_ring;
	union xnf_rx_desc *rxd;
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct mbuf *fmp = sc->sc_rx_cbuf[0];
	struct mbuf *lmp = sc->sc_rx_cbuf[1];
	struct mbuf *m;
	bus_dmamap_t dmap;
	uint32_t cons;
	uint16_t id;
	int i, flags, len, offset;

	bus_dmamap_sync(sc->sc_dmat, sc->sc_rx_rmap, 0, 0,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	for (cons = sc->sc_rx_cons; cons != rxr->rxr_cons; cons++) {
		i = cons & (XNF_RX_DESC - 1);
		rxd = &rxr->rxr_desc[i];

		id = rxd->rxd_rsp.rxp_id;
		len = rxd->rxd_rsp.rxp_status;
		flags = rxd->rxd_rsp.rxp_flags;
		offset = rxd->rxd_rsp.rxp_offset;

		KASSERT(id < XNF_RX_DESC);

		dmap = sc->sc_rx_dmap[id];
		bus_dmamap_sync(sc->sc_dmat, dmap, 0, 0,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, dmap);

		m = sc->sc_rx_buf[id];
		KASSERT(m != NULL);
		sc->sc_rx_buf[id] = NULL;

		if (flags & XNF_RXF_MGMT) {
			printf("%s: management data present\n",
			    ifp->if_xname);
			m_freem(m);
			continue;
		}

		if (flags & XNF_RXF_CSUM_VALID)
			m->m_pkthdr.csum_flags = M_TCP_CSUM_IN_OK |
			    M_UDP_CSUM_IN_OK;

		if (len < 0 || (len + offset > PAGE_SIZE)) {
			ifp->if_ierrors++;
			m_freem(m);
			continue;
		}

		m->m_len = len;
		m->m_data += offset;

		if (fmp == NULL) {
			m->m_pkthdr.len = len;
			fmp = m;
		} else {
			m->m_flags &= ~M_PKTHDR;
			lmp->m_next = m;
			fmp->m_pkthdr.len += m->m_len;
		}
		lmp = m;

		if (flags & XNF_RXF_CHUNK) {
			sc->sc_rx_cbuf[0] = fmp;
			sc->sc_rx_cbuf[1] = lmp;
			continue;
		}

		m = fmp;

		ml_enqueue(&ml, m);
		sc->sc_rx_cbuf[0] = sc->sc_rx_cbuf[1] = fmp = lmp = NULL;

		memset(rxd, 0, sizeof(*rxd));
		rxd->rxd_req.rxq_id = id;
	}

	sc->sc_rx_cons = cons;
	rxr->rxr_cons_event = sc->sc_rx_cons + 1;
	bus_dmamap_sync(sc->sc_dmat, sc->sc_rx_rmap, 0, 0,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	if_input(ifp, &ml);

	if (xnf_rx_ring_fill(sc) || (sc->sc_rx_cons != rxr->rxr_cons))
		xen_intr_schedule(sc->sc_xih);
}

int
xnf_rx_ring_fill(struct xnf_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct xnf_rx_ring *rxr = sc->sc_rx_ring;
	union xnf_rx_desc *rxd;
	bus_dmamap_t dmap;
	struct mbuf *m;
	uint32_t cons, prod, oprod;
	uint16_t id;
	int i, flags, resched = 0;

	cons = rxr->rxr_cons;
	prod = oprod = rxr->rxr_prod;

	while (prod - cons < XNF_RX_DESC) {
		i = prod & (XNF_RX_DESC - 1);
		rxd = &rxr->rxr_desc[i];

		id = rxd->rxd_rsp.rxp_id;
		KASSERT(id < XNF_RX_DESC);
		if (sc->sc_rx_buf[id])
			break;
		m = MCLGETL(NULL, M_DONTWAIT, XNF_MCLEN);
		if (m == NULL)
			break;
		m->m_len = m->m_pkthdr.len = XNF_MCLEN;
		dmap = sc->sc_rx_dmap[id];
		flags = (sc->sc_domid << 16) | BUS_DMA_READ | BUS_DMA_NOWAIT;
		if (bus_dmamap_load_mbuf(sc->sc_dmat, dmap, m, flags)) {
			m_freem(m);
			break;
		}
		sc->sc_rx_buf[id] = m;
		rxd->rxd_req.rxq_ref = dmap->dm_segs[0].ds_addr;
		bus_dmamap_sync(sc->sc_dmat, dmap, 0, 0, BUS_DMASYNC_PREWRITE);
		prod++;
	}

	rxr->rxr_prod = prod;
	bus_dmamap_sync(sc->sc_dmat, sc->sc_rx_rmap, 0, 0,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	if ((prod - cons < XNF_RX_MIN) && (ifp->if_flags & IFF_RUNNING))
		resched = 1;
	if (prod - rxr->rxr_prod_event < prod - oprod)
		xen_intr_signal(sc->sc_xih);

	return (resched);
}

int
xnf_rx_ring_create(struct xnf_softc *sc)
{
	int i, flags, rsegs;

	/* Allocate a page of memory for the ring */
	if (bus_dmamem_alloc(sc->sc_dmat, PAGE_SIZE, PAGE_SIZE, 0,
	    &sc->sc_rx_seg, 1, &rsegs, BUS_DMA_ZERO | BUS_DMA_NOWAIT)) {
		printf("%s: failed to allocate memory for the rx ring\n",
		    sc->sc_dev.dv_xname);
		return (-1);
	}
	/* Map in the allocated memory into the ring structure */
	if (bus_dmamem_map(sc->sc_dmat, &sc->sc_rx_seg, 1, PAGE_SIZE,
	    (caddr_t *)(&sc->sc_rx_ring), BUS_DMA_NOWAIT)) {
		printf("%s: failed to map memory for the rx ring\n",
		    sc->sc_dev.dv_xname);
		goto errout;
	}
	/* Create a map to load the ring memory into */
	if (bus_dmamap_create(sc->sc_dmat, PAGE_SIZE, 1, PAGE_SIZE, 0,
	    BUS_DMA_NOWAIT, &sc->sc_rx_rmap)) {
		printf("%s: failed to create a memory map for the rx ring\n",
		    sc->sc_dev.dv_xname);
		goto errout;
	}
	/* Load the ring into the ring map to extract the PA */
	flags = (sc->sc_domid << 16) | BUS_DMA_NOWAIT;
	if (bus_dmamap_load(sc->sc_dmat, sc->sc_rx_rmap, sc->sc_rx_ring,
	    PAGE_SIZE, NULL, flags)) {
		printf("%s: failed to load the rx ring map\n",
		    sc->sc_dev.dv_xname);
		goto errout;
	}
	sc->sc_rx_ref = sc->sc_rx_rmap->dm_segs[0].ds_addr;

	sc->sc_rx_ring->rxr_prod_event = sc->sc_rx_ring->rxr_cons_event = 1;

	for (i = 0; i < XNF_RX_DESC; i++) {
		if (bus_dmamap_create(sc->sc_dmat, XNF_MCLEN, 1, XNF_MCLEN,
		    PAGE_SIZE, BUS_DMA_NOWAIT, &sc->sc_rx_dmap[i])) {
			printf("%s: failed to create a memory map for the"
			    " rx slot %d\n", sc->sc_dev.dv_xname, i);
			goto errout;
		}
		sc->sc_rx_ring->rxr_desc[i].rxd_req.rxq_id = i;
	}

	return (0);

 errout:
	xnf_rx_ring_destroy(sc);
	return (-1);
}

void
xnf_rx_ring_drain(struct xnf_softc *sc)
{
	struct xnf_rx_ring *rxr = sc->sc_rx_ring;

	if (sc->sc_rx_cons != rxr->rxr_cons)
		xnf_rxeof(sc);
}

void
xnf_rx_ring_destroy(struct xnf_softc *sc)
{
	int i;

	for (i = 0; i < XNF_RX_DESC; i++) {
		if (sc->sc_rx_buf[i] == NULL)
			continue;
		bus_dmamap_sync(sc->sc_dmat, sc->sc_rx_dmap[i], 0, 0,
		    BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->sc_dmat, sc->sc_rx_dmap[i]);
		m_freem(sc->sc_rx_buf[i]);
		sc->sc_rx_buf[i] = NULL;
	}

	for (i = 0; i < XNF_RX_DESC; i++) {
		if (sc->sc_rx_dmap[i] == NULL)
			continue;
		bus_dmamap_destroy(sc->sc_dmat, sc->sc_rx_dmap[i]);
		sc->sc_rx_dmap[i] = NULL;
	}
	if (sc->sc_rx_rmap) {
		bus_dmamap_sync(sc->sc_dmat, sc->sc_rx_rmap, 0, 0,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, sc->sc_rx_rmap);
		bus_dmamap_destroy(sc->sc_dmat, sc->sc_rx_rmap);
	}
	if (sc->sc_rx_ring) {
		bus_dmamem_unmap(sc->sc_dmat, (caddr_t)sc->sc_rx_ring,
		    PAGE_SIZE);
		bus_dmamem_free(sc->sc_dmat, &sc->sc_rx_seg, 1);
	}
	sc->sc_rx_ring = NULL;
	sc->sc_rx_rmap = NULL;
	sc->sc_rx_cons = 0;
}

int
xnf_tx_ring_create(struct xnf_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	int i, flags, nsegs, rsegs;
	bus_size_t segsz;

	sc->sc_tx_frags = sc->sc_caps & XNF_CAP_SG ? XNF_TX_FRAG : 1;

	/* Allocate a page of memory for the ring */
	if (bus_dmamem_alloc(sc->sc_dmat, PAGE_SIZE, PAGE_SIZE, 0,
	    &sc->sc_tx_seg, 1, &rsegs, BUS_DMA_ZERO | BUS_DMA_NOWAIT)) {
		printf("%s: failed to allocate memory for the tx ring\n",
		    sc->sc_dev.dv_xname);
		return (-1);
	}
	/* Map in the allocated memory into the ring structure */
	if (bus_dmamem_map(sc->sc_dmat, &sc->sc_tx_seg, 1, PAGE_SIZE,
	    (caddr_t *)&sc->sc_tx_ring, BUS_DMA_NOWAIT)) {
		printf("%s: failed to map memory for the tx ring\n",
		    sc->sc_dev.dv_xname);
		goto errout;
	}
	/* Create a map to load the ring memory into */
	if (bus_dmamap_create(sc->sc_dmat, PAGE_SIZE, 1, PAGE_SIZE, 0,
	    BUS_DMA_NOWAIT, &sc->sc_tx_rmap)) {
		printf("%s: failed to create a memory map for the tx ring\n",
		    sc->sc_dev.dv_xname);
		goto errout;
	}
	/* Load the ring into the ring map to extract the PA */
	flags = (sc->sc_domid << 16) | BUS_DMA_NOWAIT;
	if (bus_dmamap_load(sc->sc_dmat, sc->sc_tx_rmap, sc->sc_tx_ring,
	    PAGE_SIZE, NULL, flags)) {
		printf("%s: failed to load the tx ring map\n",
		    sc->sc_dev.dv_xname);
		goto errout;
	}
	sc->sc_tx_ref = sc->sc_tx_rmap->dm_segs[0].ds_addr;

	sc->sc_tx_ring->txr_prod_event = sc->sc_tx_ring->txr_cons_event = 1;

	if (sc->sc_caps & XNF_CAP_SG) {
		nsegs = roundup(ifp->if_hardmtu, XNF_MCLEN) / XNF_MCLEN + 1;
		segsz = nsegs * XNF_MCLEN;
	} else {
		nsegs = 1;
		segsz = XNF_MCLEN;
	}
	for (i = 0; i < XNF_TX_DESC; i++) {
		if (bus_dmamap_create(sc->sc_dmat, segsz, nsegs, XNF_MCLEN,
		    PAGE_SIZE, BUS_DMA_NOWAIT, &sc->sc_tx_buf[i].txb_dmap)) {
			printf("%s: failed to create a memory map for the"
			    " tx slot %d\n", sc->sc_dev.dv_xname, i);
			goto errout;
		}
	}

	sc->sc_tx_avail = XNF_TX_DESC;
	sc->sc_tx_next = 0;

	return (0);

 errout:
	xnf_tx_ring_destroy(sc);
	return (-1);
}

void
xnf_tx_ring_drain(struct xnf_softc *sc)
{
	struct xnf_tx_ring *txr = sc->sc_tx_ring;

	if (sc->sc_tx_cons != txr->txr_cons)
		xnf_txeof(sc);
}

void
xnf_tx_ring_destroy(struct xnf_softc *sc)
{
	int i;

	for (i = 0; i < XNF_TX_DESC; i++) {
		if (sc->sc_tx_buf[i].txb_dmap == NULL)
			continue;
		bus_dmamap_sync(sc->sc_dmat, sc->sc_tx_buf[i].txb_dmap, 0, 0,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, sc->sc_tx_buf[i].txb_dmap);
		bus_dmamap_destroy(sc->sc_dmat, sc->sc_tx_buf[i].txb_dmap);
		sc->sc_tx_buf[i].txb_dmap = NULL;
		if (sc->sc_tx_buf[i].txb_mbuf == NULL)
			continue;
		m_free(sc->sc_tx_buf[i].txb_mbuf);
		sc->sc_tx_buf[i].txb_mbuf = NULL;
		sc->sc_tx_buf[i].txb_ndesc = 0;
	}
	if (sc->sc_tx_rmap) {
		bus_dmamap_sync(sc->sc_dmat, sc->sc_tx_rmap, 0, 0,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, sc->sc_tx_rmap);
		bus_dmamap_destroy(sc->sc_dmat, sc->sc_tx_rmap);
	}
	if (sc->sc_tx_ring) {
		bus_dmamem_unmap(sc->sc_dmat, (caddr_t)sc->sc_tx_ring,
		    PAGE_SIZE);
		bus_dmamem_free(sc->sc_dmat, &sc->sc_tx_seg, 1);
	}
	sc->sc_tx_ring = NULL;
	sc->sc_tx_rmap = NULL;
	sc->sc_tx_avail = XNF_TX_DESC;
	sc->sc_tx_next = 0;
}

int
xnf_capabilities(struct xnf_softc *sc)
{
	unsigned long long res;
	const char *prop;
	int error;

	/* Query scatter-gather capability */
	prop = "feature-sg";
	if ((error = xs_getnum(sc->sc_parent, sc->sc_backend, prop, &res)) != 0
	    && error != ENOENT)
		goto errout;
	if (error == 0 && res == 1)
		sc->sc_caps |= XNF_CAP_SG;

#if 0
	/* Query IPv4 checksum offloading capability, enabled by default */
	sc->sc_caps |= XNF_CAP_CSUM4;
	prop = "feature-no-csum-offload";
	if ((error = xs_getnum(sc->sc_parent, sc->sc_backend, prop, &res)) != 0
	    && error != ENOENT)
		goto errout;
	if (error == 0 && res == 1)
		sc->sc_caps &= ~XNF_CAP_CSUM4;

	/* Query IPv6 checksum offloading capability */
	prop = "feature-ipv6-csum-offload";
	if ((error = xs_getnum(sc->sc_parent, sc->sc_backend, prop, &res)) != 0
	    && error != ENOENT)
		goto errout;
	if (error == 0 && res == 1)
		sc->sc_caps |= XNF_CAP_CSUM6;
#endif

	/* Query multicast traffic control capability */
	prop = "feature-multicast-control";
	if ((error = xs_getnum(sc->sc_parent, sc->sc_backend, prop, &res)) != 0
	    && error != ENOENT)
		goto errout;
	if (error == 0 && res == 1)
		sc->sc_caps |= XNF_CAP_MCAST;

	/* Query split Rx/Tx event channel capability */
	prop = "feature-split-event-channels";
	if ((error = xs_getnum(sc->sc_parent, sc->sc_backend, prop, &res)) != 0
	    && error != ENOENT)
		goto errout;
	if (error == 0 && res == 1)
		sc->sc_caps |= XNF_CAP_SPLIT;

	/* Query multiqueue capability */
	prop = "multi-queue-max-queues";
	if ((error = xs_getnum(sc->sc_parent, sc->sc_backend, prop, &res)) != 0
	    && error != ENOENT)
		goto errout;
	if (error == 0)
		sc->sc_caps |= XNF_CAP_MULTIQ;

	DPRINTF("%s: capabilities %b\n", sc->sc_dev.dv_xname, sc->sc_caps,
	    "\20\006MULTIQ\005SPLIT\004MCAST\003CSUM6\002CSUM4\001SG");
	return (0);

 errout:
	printf("%s: failed to read \"%s\" property\n", sc->sc_dev.dv_xname,
	    prop);
	return (-1);
}

int
xnf_init_backend(struct xnf_softc *sc)
{
	const char *prop;

	/* Plumb the Rx ring */
	prop = "rx-ring-ref";
	if (xs_setnum(sc->sc_parent, sc->sc_node, prop, sc->sc_rx_ref))
		goto errout;
	/* Enable "copy" mode */
	prop = "request-rx-copy";
	if (xs_setnum(sc->sc_parent, sc->sc_node, prop, 1))
		goto errout;
	/* Enable notify mode */
	prop = "feature-rx-notify";
	if (xs_setnum(sc->sc_parent, sc->sc_node, prop, 1))
		goto errout;

	/* Plumb the Tx ring */
	prop = "tx-ring-ref";
	if (xs_setnum(sc->sc_parent, sc->sc_node, prop, sc->sc_tx_ref))
		goto errout;
	/* Enable scatter-gather mode */
	if (sc->sc_tx_frags > 1) {
		prop = "feature-sg";
		if (xs_setnum(sc->sc_parent, sc->sc_node, prop, 1))
			goto errout;
	}

	/* Disable IPv4 checksum offloading */
	if (!(sc->sc_caps & XNF_CAP_CSUM4)) {
		prop = "feature-no-csum-offload";
		if (xs_setnum(sc->sc_parent, sc->sc_node, prop, 1))
			goto errout;
	}

	/* Enable IPv6 checksum offloading */
	if (sc->sc_caps & XNF_CAP_CSUM6) {
		prop = "feature-ipv6-csum-offload";
		if (xs_setnum(sc->sc_parent, sc->sc_node, prop, 1))
			goto errout;
	}

	/* Plumb the event channel port */
	prop = "event-channel";
	if (xs_setnum(sc->sc_parent, sc->sc_node, prop, sc->sc_xih))
		goto errout;

	/* Connect the device */
	prop = "state";
	if (xs_setprop(sc->sc_parent, sc->sc_node, prop, XEN_STATE_CONNECTED,
	    strlen(XEN_STATE_CONNECTED)))
		goto errout;

	return (0);

 errout:
	printf("%s: failed to set \"%s\" property\n", sc->sc_dev.dv_xname, prop);
	return (-1);
}
