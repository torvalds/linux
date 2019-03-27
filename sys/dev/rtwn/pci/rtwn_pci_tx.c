/*	$OpenBSD: if_rtwn.c,v 1.6 2015/08/28 00:03:53 deraadt Exp $	*/

/*-
 * Copyright (c) 2010 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2015 Stefan Sperling <stsp@openbsd.org>
 * Copyright (c) 2016 Andriy Voskoboinyk <avos@FreeBSD.org>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/taskqueue.h>
#include <sys/bus.h>
#include <sys/endian.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/ethernet.h>
#include <net/if_media.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_radiotap.h>

#include <dev/rtwn/if_rtwnreg.h>
#include <dev/rtwn/if_rtwnvar.h>
#include <dev/rtwn/if_rtwn_debug.h>

#include <dev/rtwn/pci/rtwn_pci_var.h>
#include <dev/rtwn/pci/rtwn_pci_tx.h>

#include <dev/rtwn/rtl8192c/pci/r92ce_reg.h>


static struct mbuf *
rtwn_mbuf_defrag(struct mbuf *m0, int how)
{
	struct mbuf *m = NULL;

	KASSERT(m0->m_flags & M_PKTHDR,
	    ("M_PKTHDR flag is absent (m %p)!", m0));

	/* NB: we need _exactly_ one mbuf (no less, no more). */
	if (m0->m_pkthdr.len > MJUMPAGESIZE) {
		/* XXX MJUM9BYTES? */
		return (NULL);
	} else if (m0->m_pkthdr.len > MCLBYTES) {
		m = m_getjcl(how, MT_DATA, M_PKTHDR, MJUMPAGESIZE);
		if (m == NULL)
			return (NULL);

		if (m_dup_pkthdr(m, m0, how) == 0) {
			m_freem(m);
			return (NULL);
		}

		m_copydata(m0, 0, m0->m_pkthdr.len, mtod(m, caddr_t));
		m->m_len = m->m_pkthdr.len;
		m_freem(m0);

		return (m);
	} else
		return (m_defrag(m0, how));
}

static int
rtwn_pci_tx_start_frame(struct rtwn_softc *sc, struct ieee80211_node *ni,
    struct mbuf *m, uint8_t *tx_desc, uint8_t type)
{
	struct rtwn_pci_softc *pc = RTWN_PCI_SOFTC(sc);
	struct rtwn_tx_ring *ring;
	struct rtwn_tx_data *data;
	struct rtwn_tx_desc_common *txd;
	bus_dma_segment_t segs[1];
	uint8_t qid;
	int nsegs, error;

	RTWN_ASSERT_LOCKED(sc);

	switch (type) {
	case IEEE80211_FC0_TYPE_CTL:
	case IEEE80211_FC0_TYPE_MGT:
		qid = RTWN_PCI_MGNT_QUEUE;
		break;
	default:
		qid = M_WME_GETAC(m);
		break;
	}

	ring = &pc->tx_ring[qid];
	data = &ring->tx_data[ring->cur];
	if (data->m != NULL) {
		RTWN_DPRINTF(sc, RTWN_DEBUG_XMIT,
		    "%s: ring #%u is full (m %p)\n", __func__, qid, data->m);
		return (ENOBUFS);
	}

	txd = (struct rtwn_tx_desc_common *)
	    ((uint8_t *)ring->desc + sc->txdesc_len * ring->cur);
	if (txd->flags0 & RTWN_FLAGS0_OWN) {
		device_printf(sc->sc_dev,
		    "%s: OWN bit is set (tx desc %d, ring %u)!\n",
		    __func__, ring->cur, qid);
		return (ENOBUFS);
	}

	/* Copy Tx descriptor. */
	rtwn_pci_copy_tx_desc(pc, txd, tx_desc);
	txd->pktlen = htole16(m->m_pkthdr.len);
	txd->offset = sc->txdesc_len;

	error = bus_dmamap_load_mbuf_sg(ring->data_dmat, data->map, m, segs,
	    &nsegs, BUS_DMA_NOWAIT);
	if (error != 0 && error != EFBIG) {
		device_printf(sc->sc_dev, "can't map mbuf (error %d)\n",
		    error);
		return (error);
	}
	if (error != 0) {
		struct mbuf *mnew;

		mnew = rtwn_mbuf_defrag(m, M_NOWAIT);
		if (mnew == NULL) {
			device_printf(sc->sc_dev, "can't defragment mbuf\n");
			return (ENOBUFS);
		}
		m = mnew;

		error = bus_dmamap_load_mbuf_sg(ring->data_dmat, data->map, m,
		    segs, &nsegs, BUS_DMA_NOWAIT);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "can't map mbuf (error %d)\n", error);
			if (ni != NULL) {
				if_inc_counter(ni->ni_vap->iv_ifp,
				    IFCOUNTER_OERRORS, 1);
				ieee80211_free_node(ni);
			}
			m_freem(m);
			return (0);	/* XXX */
		}
	}

	rtwn_pci_tx_postsetup(pc, txd, segs);
	txd->flags0 |= RTWN_FLAGS0_OWN;

	/* Dump Tx descriptor. */
	rtwn_dump_tx_desc(sc, txd);

	bus_dmamap_sync(ring->desc_dmat, ring->desc_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(ring->data_dmat, data->map, BUS_DMASYNC_PREWRITE);

	data->m = m;
	data->ni = ni;

	ring->cur = (ring->cur + 1) % RTWN_PCI_TX_LIST_COUNT;

	ring->queued++;
	if (ring->queued >= (RTWN_PCI_TX_LIST_COUNT - 1))
		sc->qfullmsk |= (1 << qid);

#ifndef D4054
	sc->sc_tx_timer = 5;
#endif

	/* Kick TX. */
	rtwn_write_2(sc, R92C_PCIE_CTRL_REG, (1 << qid));

	return (0);
}

static int
rtwn_pci_tx_start_beacon(struct rtwn_softc *sc, struct mbuf *m,
    uint8_t *tx_desc, int id)
{
	struct rtwn_pci_softc *pc = RTWN_PCI_SOFTC(sc);
	struct rtwn_tx_ring *ring;
	struct rtwn_tx_data *data;
	struct rtwn_tx_desc_common *txd;
	bus_dma_segment_t segs[1];
	int nsegs, error, own;

	RTWN_ASSERT_LOCKED(sc);

	KASSERT(id == 0 || id == 1, ("bogus vap id %d\n", id));

	ring = &pc->tx_ring[RTWN_PCI_BEACON_QUEUE];
	data = &ring->tx_data[id];
	txd = (struct rtwn_tx_desc_common *)
	    ((uint8_t *)ring->desc + id * sc->txdesc_len);

	bus_dmamap_sync(ring->desc_dmat, ring->desc_map,
	    BUS_DMASYNC_POSTREAD);
	own = !!(txd->flags0 & RTWN_FLAGS0_OWN);
	error = 0;
	if (!own || txd->pktlen != htole16(m->m_pkthdr.len)) {
		if (!own) {
			/* Copy Tx descriptor. */
			rtwn_pci_copy_tx_desc(pc, txd, tx_desc);
			txd->offset = sc->txdesc_len;
		} else {
			/* Reload mbuf. */
			bus_dmamap_unload(ring->data_dmat, data->map);
		}

		error = bus_dmamap_load_mbuf_sg(ring->data_dmat,
		    data->map, m, segs, &nsegs, BUS_DMA_NOWAIT);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "can't map beacon (error %d)\n", error);
			txd->flags0 &= ~RTWN_FLAGS0_OWN;
			goto end;
		}

		txd->pktlen = htole16(m->m_pkthdr.len);
		rtwn_pci_tx_postsetup(pc, txd, segs);
		txd->flags0 |= RTWN_FLAGS0_OWN;
end:
		bus_dmamap_sync(ring->desc_dmat, ring->desc_map,
		    BUS_DMASYNC_PREWRITE);
	}

	/* Dump Tx descriptor. */
	rtwn_dump_tx_desc(sc, txd);

	bus_dmamap_sync(ring->data_dmat, data->map, BUS_DMASYNC_PREWRITE);

	return (0);
}

int
rtwn_pci_tx_start(struct rtwn_softc *sc, struct ieee80211_node *ni,
    struct mbuf *m, uint8_t *tx_desc, uint8_t type, int id)
{
	int error = 0;

	RTWN_ASSERT_LOCKED(sc);

	if (ni == NULL)		/* beacon frame */
		error = rtwn_pci_tx_start_beacon(sc, m, tx_desc, id);
	else
		error = rtwn_pci_tx_start_frame(sc, ni, m, tx_desc, type);

	return (error);
}
