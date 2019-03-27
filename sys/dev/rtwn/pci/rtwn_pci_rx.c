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
#include <net/ethernet.h>
#include <net/if_media.h>

#include <net80211/ieee80211_var.h>

#include <dev/rtwn/if_rtwnreg.h>
#include <dev/rtwn/if_rtwnvar.h>
#include <dev/rtwn/if_rtwn_debug.h>
#include <dev/rtwn/if_rtwn_rx.h>
#include <dev/rtwn/if_rtwn_task.h>
#include <dev/rtwn/if_rtwn_tx.h>

#include <dev/rtwn/pci/rtwn_pci_var.h>
#include <dev/rtwn/pci/rtwn_pci_rx.h>


void
rtwn_pci_dma_map_addr(void *arg, bus_dma_segment_t *segs, int nsegs,
    int error)
{

	if (error != 0)
		return;
	KASSERT(nsegs == 1, ("too many DMA segments, %d should be 1", nsegs));
	*(bus_addr_t *)arg = segs[0].ds_addr;
}

void
rtwn_pci_setup_rx_desc(struct rtwn_pci_softc *pc,
    struct rtwn_rx_stat_pci *desc, bus_addr_t addr, size_t len, int idx)
{

	memset(desc, 0, sizeof(*desc));
	desc->rxdw0 = htole32(SM(RTWN_RXDW0_PKTLEN, len) |
		((idx == RTWN_PCI_RX_LIST_COUNT - 1) ? RTWN_RXDW0_EOR : 0));
	desc->rxbufaddr = htole32(addr);
	bus_space_barrier(pc->pc_st, pc->pc_sh, 0, pc->pc_mapsize,
	    BUS_SPACE_BARRIER_WRITE);
	desc->rxdw0 |= htole32(RTWN_RXDW0_OWN);
}

static void
rtwn_pci_rx_frame(struct rtwn_pci_softc *pc)
{
	struct rtwn_softc *sc = &pc->pc_sc;
	struct rtwn_rx_ring *ring = &pc->rx_ring;
	struct rtwn_rx_stat_pci *rx_desc = &ring->desc[ring->cur];
	struct rtwn_rx_data *rx_data = &ring->rx_data[ring->cur];
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni;
	uint32_t rxdw0;
	struct mbuf *m, *m1;
	int infosz, pktlen, shift, error;

	/* Dump Rx descriptor. */
	RTWN_DPRINTF(sc, RTWN_DEBUG_RECV_DESC,
	    "%s: dw: 0 %08X, 1 %08X, 2 %08X, 3 %08X, 4 %08X, tsfl %08X, "
	    "addr: %08X (64: %08X)\n",
	    __func__, le32toh(rx_desc->rxdw0), le32toh(rx_desc->rxdw1),
	    le32toh(rx_desc->rxdw2), le32toh(rx_desc->rxdw3),
	    le32toh(rx_desc->rxdw4), le32toh(rx_desc->tsf_low),
	    le32toh(rx_desc->rxbufaddr), le32toh(rx_desc->rxbufaddr64));

	rxdw0 = le32toh(rx_desc->rxdw0);
	if (__predict_false(rxdw0 & (RTWN_RXDW0_CRCERR | RTWN_RXDW0_ICVERR))) {
		/*
		 * This should not happen since we setup our Rx filter
		 * to not receive these frames.
		 */
		RTWN_DPRINTF(sc, RTWN_DEBUG_RECV,
		    "%s: RX flags error (%s)\n", __func__,
		    rxdw0 & RTWN_RXDW0_CRCERR ? "CRC" : "ICV");
		goto fail;
	}

	pktlen = MS(rxdw0, RTWN_RXDW0_PKTLEN);
	if (__predict_false(pktlen < sizeof(struct ieee80211_frame_ack) ||
	    pktlen > MJUMPAGESIZE)) {
		RTWN_DPRINTF(sc, RTWN_DEBUG_RECV,
		    "%s: frame is too short/long: %d\n", __func__, pktlen);
		goto fail;
	}

	infosz = MS(rxdw0, RTWN_RXDW0_INFOSZ) * 8;
	shift = MS(rxdw0, RTWN_RXDW0_SHIFT);

	m1 = m_getjcl(M_NOWAIT, MT_DATA, M_PKTHDR, MJUMPAGESIZE);
	if (__predict_false(m1 == NULL)) {
		device_printf(sc->sc_dev, "%s: could not allocate RX mbuf\n",
		    __func__);
		goto fail;
	}
	bus_dmamap_sync(ring->data_dmat, rx_data->map, BUS_DMASYNC_POSTREAD);
	bus_dmamap_unload(ring->data_dmat, rx_data->map);

	error = bus_dmamap_load(ring->data_dmat, rx_data->map, mtod(m1, void *),
	    MJUMPAGESIZE, rtwn_pci_dma_map_addr, &rx_data->paddr, 0);
	if (error != 0) {
		m_freem(m1);

		error = bus_dmamap_load(ring->data_dmat, rx_data->map,
		    mtod(rx_data->m, void *), MJUMPAGESIZE,
		    rtwn_pci_dma_map_addr, &rx_data->paddr, BUS_DMA_NOWAIT);
		if (error != 0)
			panic("%s: could not load old RX mbuf",
			    device_get_name(sc->sc_dev));

		goto fail;
	}

	/* Finalize mbuf. */
	m = rx_data->m;
	rx_data->m = m1;
	m->m_pkthdr.len = m->m_len = pktlen + infosz + shift;

	ni = rtwn_rx_common(sc, m, rx_desc);

	RTWN_DPRINTF(sc, RTWN_DEBUG_RECV,
	    "%s: Rx frame len %d, infosz %d, shift %d\n",
	    __func__, pktlen, infosz, shift);

	/* Send the frame to the 802.11 layer. */
	RTWN_UNLOCK(sc);
	if (ni != NULL) {
		(void)ieee80211_input_mimo(ni, m);
		/* Node is no longer needed. */
		ieee80211_free_node(ni);
	} else
		(void)ieee80211_input_mimo_all(ic, m);

	RTWN_LOCK(sc);

	return;

fail:
	counter_u64_add(ic->ic_ierrors, 1);
}

static int
rtwn_pci_rx_buf_copy(struct rtwn_pci_softc *pc)
{
	struct rtwn_rx_ring *ring = &pc->rx_ring;
	struct rtwn_rx_stat_pci *rx_desc = &ring->desc[ring->cur];
	struct rtwn_rx_data *rx_data = &ring->rx_data[ring->cur];
	uint32_t rxdw0;
	int desc_size, pktlen;

	/*
	 * NB: tx_report() / c2h_report() expects to see USB Rx
	 * descriptor - same as for PCIe, but without rxbufaddr* fields.
	 */
	desc_size = sizeof(struct rtwn_rx_stat_common);
	KASSERT(sizeof(pc->pc_rx_buf) >= desc_size,
	    ("adjust size for PCIe Rx buffer!"));

	memcpy(pc->pc_rx_buf, rx_desc, desc_size);

	rxdw0 = le32toh(rx_desc->rxdw0);
	pktlen = MS(rxdw0, RTWN_RXDW0_PKTLEN);

	if (pktlen > sizeof(pc->pc_rx_buf) - desc_size)
	{
		/* Looks like an ordinary Rx frame. */
		return (desc_size);
	}

	bus_dmamap_sync(ring->data_dmat, rx_data->map, BUS_DMASYNC_POSTREAD);
	memcpy(pc->pc_rx_buf + desc_size, mtod(rx_data->m, void *), pktlen);

	return (desc_size + pktlen);
}

static void
rtwn_pci_tx_report(struct rtwn_pci_softc *pc, int len)
{
	struct rtwn_softc *sc = &pc->pc_sc;

	if (sc->sc_ratectl != RTWN_RATECTL_NET80211) {
		/* shouldn't happen */
		device_printf(sc->sc_dev,
		    "%s called while ratectl = %d!\n",
		     __func__, sc->sc_ratectl);
		return;
	}

	RTWN_NT_LOCK(sc);
	rtwn_handle_tx_report(sc, pc->pc_rx_buf, len);
	RTWN_NT_UNLOCK(sc);

#ifdef IEEE80211_SUPPORT_SUPERG
	/*
	 * NB: this will executed only when 'report' bit is set.
	 */
	if (sc->sc_tx_n_active > 0 && --sc->sc_tx_n_active <= 1)
		rtwn_cmd_sleepable(sc, NULL, 0, rtwn_ff_flush_all);
#endif
}

static void
rtwn_pci_c2h_report(struct rtwn_pci_softc *pc, int len)
{
	rtwn_handle_c2h_report(&pc->pc_sc, pc->pc_rx_buf, len);
}

static void
rtwn_pci_tx_done(struct rtwn_softc *sc, int qid)
{
	struct rtwn_pci_softc *pc = RTWN_PCI_SOFTC(sc);
	struct rtwn_tx_ring *ring = &pc->tx_ring[qid];
	struct rtwn_tx_desc_common *desc;
	struct rtwn_tx_data *data;

	RTWN_DPRINTF(sc, RTWN_DEBUG_INTR, "%s: qid %d, last %d, cur %d\n",
	    __func__, qid, ring->last, ring->cur);

	bus_dmamap_sync(ring->desc_dmat, ring->desc_map,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	while(ring->last != ring->cur) {
		data = &ring->tx_data[ring->last];
		desc = (struct rtwn_tx_desc_common *)
		    ((uint8_t *)ring->desc + sc->txdesc_len * ring->last);

		KASSERT(data->m != NULL, ("no mbuf"));

		if (desc->flags0 & RTWN_FLAGS0_OWN)
			break;

		/* Unmap and free mbuf. */
		bus_dmamap_sync(ring->data_dmat, data->map,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(ring->data_dmat, data->map);

		if (data->ni != NULL) {	/* not a beacon frame */
			ieee80211_tx_complete(data->ni, data->m, 0);

			data->ni = NULL;
			ring->queued--;
			KASSERT(ring->queued >= 0,
			    ("ring->queued (qid %d) underflow!\n", qid));
		} else
			m_freem(data->m);

		data->m = NULL;
		ring->last = (ring->last + 1) % RTWN_PCI_TX_LIST_COUNT;
#ifndef D4054
		if (ring->queued > 0)
			sc->sc_tx_timer = 5;
		else
			sc->sc_tx_timer = 0;
#endif
	}

	if ((sc->qfullmsk & (1 << qid)) != 0 &&
	    ring->queued < (RTWN_PCI_TX_LIST_COUNT - 1)) {
		sc->qfullmsk &= ~(1 << qid);
		rtwn_start(sc);
	}

#ifdef  IEEE80211_SUPPORT_SUPERG
	/*
	 * If the TX active queue drops below a certain
	 * threshold, ensure we age fast-frames out so they're
	 * transmitted.
	 */
	if (sc->sc_ratectl != RTWN_RATECTL_NET80211 && ring->queued <= 1) {
		/*
		 * XXX TODO: just make this a callout timer schedule
		 * so we can flush the FF staging queue if we're
		 * approaching idle.
		 */
		rtwn_cmd_sleepable(sc, NULL, 0, rtwn_ff_flush_all);
	}
#endif
}

static void
rtwn_pci_rx_done(struct rtwn_softc *sc)
{
	struct rtwn_pci_softc *pc = RTWN_PCI_SOFTC(sc);
	struct rtwn_rx_ring *ring = &pc->rx_ring;
	struct rtwn_rx_stat_pci *rx_desc;
	struct rtwn_rx_data *rx_data;
	int len;

	bus_dmamap_sync(ring->desc_dmat, ring->desc_map, BUS_DMASYNC_POSTREAD);

	for (;;) {
		rx_desc = &ring->desc[ring->cur];
		rx_data = &ring->rx_data[ring->cur];

		if (le32toh(rx_desc->rxdw0) & RTWN_RXDW0_OWN)
			break;

		len = rtwn_pci_rx_buf_copy(pc);

		switch (rtwn_classify_intr(sc, pc->pc_rx_buf, len)) {
		case RTWN_RX_DATA:
			rtwn_pci_rx_frame(pc);
			break;
		case RTWN_RX_TX_REPORT:
			rtwn_pci_tx_report(pc, len);
			break;
		case RTWN_RX_OTHER:
			rtwn_pci_c2h_report(pc, len);
			break;
		default:
			/* NOTREACHED */
			KASSERT(0, ("unknown Rx classification code"));
			break;
		}

		/* Update / reset RX descriptor (and set OWN bit). */
		rtwn_pci_setup_rx_desc(pc, rx_desc, rx_data->paddr,
		    MJUMPAGESIZE, ring->cur);

		if (!(sc->sc_flags & RTWN_RUNNING))
			return;

		/* NB: device can reuse current descriptor. */
		bus_dmamap_sync(ring->desc_dmat, ring->desc_map,
		    BUS_DMASYNC_POSTREAD);

		if (le32toh(rx_desc->rxdw0) & RTWN_RXDW0_OWN)
			ring->cur = (ring->cur + 1) % RTWN_PCI_RX_LIST_COUNT;
	}
}

void
rtwn_pci_intr(void *arg)
{
	struct rtwn_softc *sc = arg;
	struct rtwn_pci_softc *pc = RTWN_PCI_SOFTC(sc);
	int i, status, tx_rings;

	RTWN_LOCK(sc);
	status = rtwn_pci_get_intr_status(pc, &tx_rings);
	RTWN_DPRINTF(sc, RTWN_DEBUG_INTR, "%s: status %08X, tx_rings %08X\n",
	    __func__, status, tx_rings);
	if (status == 0 && tx_rings == 0)
		goto unlock;

	if (status & (RTWN_PCI_INTR_RX | RTWN_PCI_INTR_TX_REPORT)) {
		rtwn_pci_rx_done(sc);
		if (!(sc->sc_flags & RTWN_RUNNING))
			goto unlock;
	}

	if (tx_rings != 0)
		for (i = 0; i < RTWN_PCI_NTXQUEUES; i++)
			if (tx_rings & (1 << i))
				rtwn_pci_tx_done(sc, i);

	if (sc->sc_flags & RTWN_RUNNING)
		rtwn_pci_enable_intr(pc);
unlock:
	RTWN_UNLOCK(sc);
}
