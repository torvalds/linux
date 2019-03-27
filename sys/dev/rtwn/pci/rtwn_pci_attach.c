/*	$OpenBSD: if_urtwn.c,v 1.16 2011/02/10 17:26:40 jakemsr Exp $	*/

/*-
 * Copyright (c) 2010 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2014 Kevin Lo <kevlo@FreeBSD.org>
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

#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/linker.h>
#include <sys/kdb.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <net/if.h>
#include <net/ethernet.h>
#include <net/if_media.h>

#include <net80211/ieee80211_var.h>

#include <dev/rtwn/if_rtwnreg.h>
#include <dev/rtwn/if_rtwnvar.h>
#include <dev/rtwn/if_rtwn_nop.h>
#include <dev/rtwn/if_rtwn_debug.h>

#include <dev/rtwn/pci/rtwn_pci_var.h>

#include <dev/rtwn/pci/rtwn_pci_attach.h>
#include <dev/rtwn/pci/rtwn_pci_reg.h>
#include <dev/rtwn/pci/rtwn_pci_rx.h>
#include <dev/rtwn/pci/rtwn_pci_tx.h>

#include <dev/rtwn/rtl8192c/pci/r92ce_reg.h>


static device_probe_t	rtwn_pci_probe;
static device_attach_t	rtwn_pci_attach;
static device_detach_t	rtwn_pci_detach;
static device_shutdown_t rtwn_pci_shutdown;
static device_suspend_t	rtwn_pci_suspend;
static device_resume_t	rtwn_pci_resume;

static int	rtwn_pci_alloc_rx_list(struct rtwn_softc *);
static void	rtwn_pci_reset_rx_list(struct rtwn_softc *);
static void	rtwn_pci_free_rx_list(struct rtwn_softc *);
static int	rtwn_pci_alloc_tx_list(struct rtwn_softc *, int);
static void	rtwn_pci_reset_tx_ring_stopped(struct rtwn_softc *, int);
static void	rtwn_pci_reset_beacon_ring(struct rtwn_softc *, int);
static void	rtwn_pci_reset_tx_list(struct rtwn_softc *,
		    struct ieee80211vap *, int);
static void	rtwn_pci_free_tx_list(struct rtwn_softc *, int);
static void	rtwn_pci_reset_lists(struct rtwn_softc *,
		    struct ieee80211vap *);
static int	rtwn_pci_fw_write_block(struct rtwn_softc *,
		    const uint8_t *, uint16_t, int);
static uint16_t	rtwn_pci_get_qmap(struct rtwn_softc *);
static void	rtwn_pci_set_desc_addr(struct rtwn_softc *);
static void	rtwn_pci_beacon_update_begin(struct rtwn_softc *,
		    struct ieee80211vap *);
static void	rtwn_pci_beacon_update_end(struct rtwn_softc *,
		    struct ieee80211vap *);
static void	rtwn_pci_attach_methods(struct rtwn_softc *);


static const struct rtwn_pci_ident *
rtwn_pci_probe_sub(device_t dev)
{
	const struct rtwn_pci_ident *ident;
	int vendor_id, device_id;

	vendor_id = pci_get_vendor(dev);
	device_id = pci_get_device(dev);

	for (ident = rtwn_pci_ident_table; ident->name != NULL; ident++)
		if (vendor_id == ident->vendor && device_id == ident->device)
			return (ident);

	return (NULL);
}

static int
rtwn_pci_probe(device_t dev)
{
	const struct rtwn_pci_ident *ident;

	ident = rtwn_pci_probe_sub(dev);
	if (ident != NULL) {
		device_set_desc(dev, ident->name);
		return (BUS_PROBE_DEFAULT);
	}
	return (ENXIO);
}

static int
rtwn_pci_alloc_rx_list(struct rtwn_softc *sc)
{
	struct rtwn_pci_softc *pc = RTWN_PCI_SOFTC(sc);
	struct rtwn_rx_ring *rx_ring = &pc->rx_ring;
	struct rtwn_rx_data *rx_data;
	bus_size_t size;
	int i, error;

	/* Allocate Rx descriptors. */
	size = sizeof(struct rtwn_rx_stat_pci) * RTWN_PCI_RX_LIST_COUNT;
	error = bus_dma_tag_create(bus_get_dma_tag(sc->sc_dev), 1, 0,
	    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL, NULL,
	    size, 1, size, 0, NULL, NULL, &rx_ring->desc_dmat);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not create rx desc DMA tag\n");
		goto fail;
	}

	error = bus_dmamem_alloc(rx_ring->desc_dmat, (void **)&rx_ring->desc,
	    BUS_DMA_NOWAIT | BUS_DMA_ZERO | BUS_DMA_COHERENT,
	    &rx_ring->desc_map);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not allocate rx desc\n");
		goto fail;
	}
	error = bus_dmamap_load(rx_ring->desc_dmat, rx_ring->desc_map,
	    rx_ring->desc, size, rtwn_pci_dma_map_addr, &rx_ring->paddr, 0);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not load rx desc DMA map\n");
		goto fail;
	}
	bus_dmamap_sync(rx_ring->desc_dmat, rx_ring->desc_map,
	    BUS_DMASYNC_PREWRITE);

	/* Create RX buffer DMA tag. */
	error = bus_dma_tag_create(bus_get_dma_tag(sc->sc_dev), 1, 0,
	    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL, NULL,
	    MJUMPAGESIZE, 1, MJUMPAGESIZE, 0, NULL, NULL, &rx_ring->data_dmat);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not create rx buf DMA tag\n");
		goto fail;
	}

	/* Allocate Rx buffers. */
	for (i = 0; i < RTWN_PCI_RX_LIST_COUNT; i++) {
		rx_data = &rx_ring->rx_data[i];
		error = bus_dmamap_create(rx_ring->data_dmat, 0, &rx_data->map);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "could not create rx buf DMA map\n");
			goto fail;
		}

		rx_data->m = m_getjcl(M_NOWAIT, MT_DATA, M_PKTHDR,
		    MJUMPAGESIZE);
		if (rx_data->m == NULL) {
			device_printf(sc->sc_dev,
			    "could not allocate rx mbuf\n");
			error = ENOMEM;
			goto fail;
		}

		error = bus_dmamap_load(rx_ring->data_dmat, rx_data->map,
		    mtod(rx_data->m, void *), MJUMPAGESIZE,
		    rtwn_pci_dma_map_addr, &rx_data->paddr, BUS_DMA_NOWAIT);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "could not load rx buf DMA map");
			goto fail;
		}

		rtwn_pci_setup_rx_desc(pc, &rx_ring->desc[i], rx_data->paddr,
		    MJUMPAGESIZE, i);
	}
	rx_ring->cur = 0;

	return (0);

fail:
	rtwn_pci_free_rx_list(sc);
	return (error);
}

static void
rtwn_pci_reset_rx_list(struct rtwn_softc *sc)
{
	struct rtwn_pci_softc *pc = RTWN_PCI_SOFTC(sc);
	struct rtwn_rx_ring *rx_ring = &pc->rx_ring;
	struct rtwn_rx_data *rx_data;
	int i;

	for (i = 0; i < RTWN_PCI_RX_LIST_COUNT; i++) {
		rx_data = &rx_ring->rx_data[i];
		rtwn_pci_setup_rx_desc(pc, &rx_ring->desc[i],
		    rx_data->paddr, MJUMPAGESIZE, i);
	}
	rx_ring->cur = 0;
}

static void
rtwn_pci_free_rx_list(struct rtwn_softc *sc)
{
	struct rtwn_pci_softc *pc = RTWN_PCI_SOFTC(sc);
	struct rtwn_rx_ring *rx_ring = &pc->rx_ring;
	struct rtwn_rx_data *rx_data;
	int i;

	if (rx_ring->desc_dmat != NULL) {
		if (rx_ring->desc != NULL) {
			bus_dmamap_sync(rx_ring->desc_dmat,
			    rx_ring->desc_map,
			    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(rx_ring->desc_dmat,
			    rx_ring->desc_map);
			bus_dmamem_free(rx_ring->desc_dmat, rx_ring->desc,
			    rx_ring->desc_map);
			rx_ring->desc = NULL;
		}
		bus_dma_tag_destroy(rx_ring->desc_dmat);
		rx_ring->desc_dmat = NULL;
	}

	for (i = 0; i < RTWN_PCI_RX_LIST_COUNT; i++) {
		rx_data = &rx_ring->rx_data[i];

		if (rx_data->m != NULL) {
			bus_dmamap_sync(rx_ring->data_dmat,
			    rx_data->map, BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(rx_ring->data_dmat, rx_data->map);
			m_freem(rx_data->m);
			rx_data->m = NULL;
		}
		bus_dmamap_destroy(rx_ring->data_dmat, rx_data->map);
		rx_data->map = NULL;
	}
	if (rx_ring->data_dmat != NULL) {
		bus_dma_tag_destroy(rx_ring->data_dmat);
		rx_ring->data_dmat = NULL;
	}
}

static int
rtwn_pci_alloc_tx_list(struct rtwn_softc *sc, int qid)
{
	struct rtwn_pci_softc *pc = RTWN_PCI_SOFTC(sc);
	struct rtwn_tx_ring *tx_ring = &pc->tx_ring[qid];
	bus_size_t size;
	int i, error;

	size = sc->txdesc_len * RTWN_PCI_TX_LIST_COUNT;
	error = bus_dma_tag_create(bus_get_dma_tag(sc->sc_dev), PAGE_SIZE, 0,
	    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL, NULL,
	    size, 1, size, 0, NULL, NULL, &tx_ring->desc_dmat);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not create tx ring DMA tag\n");
		goto fail;
	}

	error = bus_dmamem_alloc(tx_ring->desc_dmat, &tx_ring->desc,
	    BUS_DMA_NOWAIT | BUS_DMA_ZERO, &tx_ring->desc_map);
	if (error != 0) {
		device_printf(sc->sc_dev, "can't map tx ring DMA memory\n");
		goto fail;
	}
	error = bus_dmamap_load(tx_ring->desc_dmat, tx_ring->desc_map,
	    tx_ring->desc, size, rtwn_pci_dma_map_addr, &tx_ring->paddr,
	    BUS_DMA_NOWAIT);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not load desc DMA map\n");
		goto fail;
	}
	bus_dmamap_sync(tx_ring->desc_dmat, tx_ring->desc_map,
	    BUS_DMASYNC_PREWRITE);

	error = bus_dma_tag_create(bus_get_dma_tag(sc->sc_dev), 1, 0,
	    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL, NULL,
	    MJUMPAGESIZE, 1, MJUMPAGESIZE, 0, NULL, NULL, &tx_ring->data_dmat);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not create tx buf DMA tag\n");
		goto fail;
	}

	for (i = 0; i < RTWN_PCI_TX_LIST_COUNT; i++) {
		struct rtwn_tx_data *tx_data = &tx_ring->tx_data[i];
		void *tx_desc = (uint8_t *)tx_ring->desc + sc->txdesc_len * i;
		uint32_t next_desc_addr = tx_ring->paddr +
		    sc->txdesc_len * ((i + 1) % RTWN_PCI_TX_LIST_COUNT);

		rtwn_pci_setup_tx_desc(pc, tx_desc, next_desc_addr);

		error = bus_dmamap_create(tx_ring->data_dmat, 0, &tx_data->map);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "could not create tx buf DMA map\n");
			return (error);
		}
		tx_data->m = NULL;
		tx_data->ni = NULL;
	}
	return (0);

fail:
	rtwn_pci_free_tx_list(sc, qid);
	return (error);
}

static void
rtwn_pci_reset_tx_ring_stopped(struct rtwn_softc *sc, int qid)
{
	struct rtwn_pci_softc *pc = RTWN_PCI_SOFTC(sc);
	struct rtwn_tx_ring *ring = &pc->tx_ring[qid];
	int i;

	for (i = 0; i < RTWN_PCI_TX_LIST_COUNT; i++) {
		struct rtwn_tx_data *data = &ring->tx_data[i];
		void *desc = (uint8_t *)ring->desc + sc->txdesc_len * i;

		rtwn_pci_copy_tx_desc(pc, desc, NULL);

		if (data->m != NULL) {
			bus_dmamap_sync(ring->data_dmat, data->map,
			    BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(ring->data_dmat, data->map);
			m_freem(data->m);
			data->m = NULL;
		}
		if (data->ni != NULL) {
			ieee80211_free_node(data->ni);
			data->ni = NULL;
		}
	}

	bus_dmamap_sync(ring->desc_dmat, ring->desc_map,
	    BUS_DMASYNC_POSTWRITE);

	sc->qfullmsk &= ~(1 << qid);
	ring->queued = 0;
	ring->last = ring->cur = 0;
}

/*
 * Clear entry 0 (or 1) in the beacon queue (other are not used).
 */
static void
rtwn_pci_reset_beacon_ring(struct rtwn_softc *sc, int id)
{
	struct rtwn_pci_softc *pc = RTWN_PCI_SOFTC(sc);
	struct rtwn_tx_ring *ring = &pc->tx_ring[RTWN_PCI_BEACON_QUEUE];
	struct rtwn_tx_data *data = &ring->tx_data[id];
	struct rtwn_tx_desc_common *txd = (struct rtwn_tx_desc_common *)
	    ((uint8_t *)ring->desc + id * sc->txdesc_len);

	bus_dmamap_sync(ring->desc_dmat, ring->desc_map, BUS_DMASYNC_POSTREAD);
	if (txd->flags0 & RTWN_FLAGS0_OWN) {
		/* Clear OWN bit. */
		txd->flags0 &= ~RTWN_FLAGS0_OWN;
		bus_dmamap_sync(ring->desc_dmat, ring->desc_map,
		    BUS_DMASYNC_PREWRITE);

		/* Unload mbuf. */
		bus_dmamap_sync(ring->data_dmat, data->map,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(ring->data_dmat, data->map);
	}
}

/*
 * Drop stale entries from Tx ring before the vap will be deleted.
 * In case if vap is NULL just free everything and reset cur / last pointers.
 */
static void
rtwn_pci_reset_tx_list(struct rtwn_softc *sc, struct ieee80211vap *vap,
    int qid)
{
	int i;

	if (vap == NULL) {
		if (qid != RTWN_PCI_BEACON_QUEUE) {
			/*
			 * Device was stopped; just clear all entries.
			 */
			rtwn_pci_reset_tx_ring_stopped(sc, qid);
		} else {
			for (i = 0; i < RTWN_PORT_COUNT; i++)
				rtwn_pci_reset_beacon_ring(sc, i);
		}
	} else if (qid == RTWN_PCI_BEACON_QUEUE &&
		   (vap->iv_opmode == IEEE80211_M_HOSTAP ||
		    vap->iv_opmode == IEEE80211_M_IBSS)) {
		struct rtwn_vap *uvp = RTWN_VAP(vap);

		rtwn_pci_reset_beacon_ring(sc, uvp->id);
	} else {
		struct rtwn_pci_softc *pc = RTWN_PCI_SOFTC(sc);
		struct rtwn_tx_ring *ring = &pc->tx_ring[qid];

		for (i = 0; i < RTWN_PCI_TX_LIST_COUNT; i++) {
			struct rtwn_tx_data *data = &ring->tx_data[i];
			if (data->ni != NULL && data->ni->ni_vap == vap) {
				/*
				 * NB: if some vap is still running
				 * rtwn_pci_tx_done() will free the mbuf;
				 * otherwise, rtwn_stop() will reset all rings
				 * after device shutdown.
				 */
				ieee80211_free_node(data->ni);
				data->ni = NULL;
			}
		}
	}
}

static void
rtwn_pci_free_tx_list(struct rtwn_softc *sc, int qid)
{
	struct rtwn_pci_softc *pc = RTWN_PCI_SOFTC(sc);
	struct rtwn_tx_ring *tx_ring = &pc->tx_ring[qid];
	struct rtwn_tx_data *tx_data;
	int i;

	if (tx_ring->desc_dmat != NULL) {
		if (tx_ring->desc != NULL) {
			bus_dmamap_sync(tx_ring->desc_dmat,
			    tx_ring->desc_map, BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(tx_ring->desc_dmat,
			    tx_ring->desc_map);
			bus_dmamem_free(tx_ring->desc_dmat, tx_ring->desc,
			    tx_ring->desc_map);
		}
		bus_dma_tag_destroy(tx_ring->desc_dmat);
	}

	for (i = 0; i < RTWN_PCI_TX_LIST_COUNT; i++) {
		tx_data = &tx_ring->tx_data[i];

		if (tx_data->m != NULL) {
			bus_dmamap_sync(tx_ring->data_dmat, tx_data->map,
			    BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(tx_ring->data_dmat, tx_data->map);
			m_freem(tx_data->m);
			tx_data->m = NULL;
		}
	}
	if (tx_ring->data_dmat != NULL) {
		bus_dma_tag_destroy(tx_ring->data_dmat);
		tx_ring->data_dmat = NULL;
	}

	sc->qfullmsk &= ~(1 << qid);
	tx_ring->queued = 0;
	tx_ring->last = tx_ring->cur = 0;
}

static void
rtwn_pci_reset_lists(struct rtwn_softc *sc, struct ieee80211vap *vap)
{
	int i;

	for (i = 0; i < RTWN_PCI_NTXQUEUES; i++)
		rtwn_pci_reset_tx_list(sc, vap, i);

	if (vap == NULL) {
		sc->qfullmsk = 0;
		rtwn_pci_reset_rx_list(sc);
	}
}

static int
rtwn_pci_fw_write_block(struct rtwn_softc *sc, const uint8_t *buf,
    uint16_t reg, int mlen)
{
	int i;

	for (i = 0; i < mlen; i++)
		rtwn_pci_write_1(sc, reg++, buf[i]);

	/* NB: cannot fail */
	return (0);
}

static uint16_t
rtwn_pci_get_qmap(struct rtwn_softc *sc)
{
	struct rtwn_pci_softc *pc = RTWN_PCI_SOFTC(sc);

	KASSERT(pc->pc_qmap != 0, ("%s: qmap is not set!\n", __func__));

	return (pc->pc_qmap);
}

static void
rtwn_pci_set_desc_addr(struct rtwn_softc *sc)
{
	struct rtwn_pci_softc *pc = RTWN_PCI_SOFTC(sc);

	RTWN_DPRINTF(sc, RTWN_DEBUG_RESET, "%s: addresses:\n"
	    "bk: %08jX, be: %08jX, vi: %08jX, vo: %08jX\n"
	    "bcn: %08jX, mgt: %08jX, high: %08jX, rx: %08jX\n",
	    __func__, (uintmax_t)pc->tx_ring[RTWN_PCI_BK_QUEUE].paddr,
	    (uintmax_t)pc->tx_ring[RTWN_PCI_BE_QUEUE].paddr,
	    (uintmax_t)pc->tx_ring[RTWN_PCI_VI_QUEUE].paddr,
	    (uintmax_t)pc->tx_ring[RTWN_PCI_VO_QUEUE].paddr,
	    (uintmax_t)pc->tx_ring[RTWN_PCI_BEACON_QUEUE].paddr,
	    (uintmax_t)pc->tx_ring[RTWN_PCI_MGNT_QUEUE].paddr,
	    (uintmax_t)pc->tx_ring[RTWN_PCI_HIGH_QUEUE].paddr,
	    (uintmax_t)pc->rx_ring.paddr);

	/* Set Tx Configuration Register. */
	rtwn_pci_write_4(sc, R92C_TCR, pc->tcr);

	/* Configure Tx DMA. */
	rtwn_pci_write_4(sc, R92C_BKQ_DESA,
	    pc->tx_ring[RTWN_PCI_BK_QUEUE].paddr);
	rtwn_pci_write_4(sc, R92C_BEQ_DESA,
	    pc->tx_ring[RTWN_PCI_BE_QUEUE].paddr);
	rtwn_pci_write_4(sc, R92C_VIQ_DESA,
	    pc->tx_ring[RTWN_PCI_VI_QUEUE].paddr);
	rtwn_pci_write_4(sc, R92C_VOQ_DESA,
	    pc->tx_ring[RTWN_PCI_VO_QUEUE].paddr);
	rtwn_pci_write_4(sc, R92C_BCNQ_DESA,
	    pc->tx_ring[RTWN_PCI_BEACON_QUEUE].paddr);
	rtwn_pci_write_4(sc, R92C_MGQ_DESA,
	    pc->tx_ring[RTWN_PCI_MGNT_QUEUE].paddr);
	rtwn_pci_write_4(sc, R92C_HQ_DESA,
	    pc->tx_ring[RTWN_PCI_HIGH_QUEUE].paddr);

	/* Configure Rx DMA. */
	rtwn_pci_write_4(sc, R92C_RX_DESA, pc->rx_ring.paddr);
}

static void
rtwn_pci_beacon_update_begin(struct rtwn_softc *sc, struct ieee80211vap *vap)
{
	struct rtwn_vap *rvp = RTWN_VAP(vap);

	RTWN_ASSERT_LOCKED(sc);

	rtwn_beacon_enable(sc, rvp->id, 0);
}

static void
rtwn_pci_beacon_update_end(struct rtwn_softc *sc, struct ieee80211vap *vap)
{
	struct rtwn_vap *rvp = RTWN_VAP(vap);

	RTWN_ASSERT_LOCKED(sc);

	if (rvp->curr_mode != R92C_MSR_NOLINK)
		rtwn_beacon_enable(sc, rvp->id, 1);
}

static void
rtwn_pci_attach_methods(struct rtwn_softc *sc)
{
	sc->sc_write_1		= rtwn_pci_write_1;
	sc->sc_write_2		= rtwn_pci_write_2;
	sc->sc_write_4		= rtwn_pci_write_4;
	sc->sc_read_1		= rtwn_pci_read_1;
	sc->sc_read_2		= rtwn_pci_read_2;
	sc->sc_read_4		= rtwn_pci_read_4;
	sc->sc_delay		= rtwn_pci_delay;
	sc->sc_tx_start		= rtwn_pci_tx_start;
	sc->sc_reset_lists	= rtwn_pci_reset_lists;
	sc->sc_abort_xfers	= rtwn_nop_softc;
	sc->sc_fw_write_block	= rtwn_pci_fw_write_block;
	sc->sc_get_qmap		= rtwn_pci_get_qmap;
	sc->sc_set_desc_addr	= rtwn_pci_set_desc_addr;
	sc->sc_drop_incorrect_tx = rtwn_nop_softc;
	sc->sc_beacon_update_begin = rtwn_pci_beacon_update_begin;
	sc->sc_beacon_update_end = rtwn_pci_beacon_update_end;
	sc->sc_beacon_unload	= rtwn_pci_reset_beacon_ring;

	sc->bcn_check_interval	= 25000;
}

static int
rtwn_pci_attach(device_t dev)
{
	const struct rtwn_pci_ident *ident;
	struct rtwn_pci_softc *pc = device_get_softc(dev);
	struct rtwn_softc *sc = &pc->pc_sc;
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t lcsr;
	int cap_off, i, error, rid;

	ident = rtwn_pci_probe_sub(dev);
	if (ident == NULL)
		return (ENXIO);

	/*
	 * Get the offset of the PCI Express Capability Structure in PCI
	 * Configuration Space.
	 */
	error = pci_find_cap(dev, PCIY_EXPRESS, &cap_off);
	if (error != 0) {
		device_printf(dev, "PCIe capability structure not found!\n");
		return (error);
	}

	/* Enable bus-mastering. */
	pci_enable_busmaster(dev);

	rid = PCIR_BAR(2);
	pc->mem = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (pc->mem == NULL) {
		device_printf(dev, "can't map mem space\n");
		return (ENOMEM);
	}
	pc->pc_st = rman_get_bustag(pc->mem);
	pc->pc_sh = rman_get_bushandle(pc->mem);

	/* Install interrupt handler. */
	rid = 1;
	if (pci_alloc_msi(dev, &rid) == 0)
		rid = 1;
	else
		rid = 0;
	pc->irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid, RF_ACTIVE |
	    (rid != 0 ? 0 : RF_SHAREABLE));
	if (pc->irq == NULL) {
		device_printf(dev, "can't map interrupt\n");
		goto detach;
	}

	/* Disable PCIe Active State Power Management (ASPM). */
	lcsr = pci_read_config(dev, cap_off + PCIER_LINK_CTL, 4);
	lcsr &= ~PCIEM_LINK_CTL_ASPMC;
	pci_write_config(dev, cap_off + PCIER_LINK_CTL, lcsr, 4);

	sc->sc_dev = dev;
	ic->ic_name = device_get_nameunit(dev);

	/* Need to be initialized early. */
	rtwn_sysctlattach(sc);
	mtx_init(&sc->sc_mtx, ic->ic_name, MTX_NETWORK_LOCK, MTX_DEF);

	rtwn_pci_attach_methods(sc);
	rtwn_pci_attach_private(pc, ident->chip);

	/* Allocate Tx/Rx buffers. */
	error = rtwn_pci_alloc_rx_list(sc);
	if (error != 0) {
		device_printf(dev,
		    "could not allocate Rx buffers, error %d\n",
		    error);
		goto detach;
	}
	for (i = 0; i < RTWN_PCI_NTXQUEUES; i++) {
		error = rtwn_pci_alloc_tx_list(sc, i);
		if (error != 0) {
			device_printf(dev,
			    "could not allocate Tx buffers, error %d\n",
			    error);
			goto detach;
		}
	}

	/* Generic attach. */
	error = rtwn_attach(sc);
	if (error != 0)
		goto detach;

	/*
	 * Hook our interrupt after all initialization is complete.
	 */
	error = bus_setup_intr(dev, pc->irq, INTR_TYPE_NET | INTR_MPSAFE,
	    NULL, rtwn_pci_intr, sc, &pc->pc_ih);
	if (error != 0) {
		device_printf(dev, "can't establish interrupt, error %d\n",
		    error);
		goto detach;
	}

	return (0);

detach:
	rtwn_pci_detach(dev);		/* failure */
	return (ENXIO);
}

static int
rtwn_pci_detach(device_t dev)
{
	struct rtwn_pci_softc *pc = device_get_softc(dev);
	struct rtwn_softc *sc = &pc->pc_sc;
	int i;

	/* Generic detach. */
	rtwn_detach(sc);

	/* Uninstall interrupt handler. */
	if (pc->irq != NULL) {
		bus_teardown_intr(dev, pc->irq, pc->pc_ih);
		bus_release_resource(dev, SYS_RES_IRQ, rman_get_rid(pc->irq),
		    pc->irq);
		pci_release_msi(dev);
	}

	/* Free Tx/Rx buffers. */
	for (i = 0; i < RTWN_PCI_NTXQUEUES; i++)
		rtwn_pci_free_tx_list(sc, i);
	rtwn_pci_free_rx_list(sc);

	if (pc->mem != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY,
		    rman_get_rid(pc->mem), pc->mem);

	rtwn_detach_private(sc);
	mtx_destroy(&sc->sc_mtx);

	return (0);
}

static int
rtwn_pci_shutdown(device_t self)
{
	struct rtwn_pci_softc *pc = device_get_softc(self);

	ieee80211_stop_all(&pc->pc_sc.sc_ic);
	return (0);
}

static int
rtwn_pci_suspend(device_t self)
{
	struct rtwn_pci_softc *pc = device_get_softc(self);

	rtwn_suspend(&pc->pc_sc);

	return (0);
}

static int
rtwn_pci_resume(device_t self)
{
	struct rtwn_pci_softc *pc = device_get_softc(self);

	rtwn_resume(&pc->pc_sc);

	return (0);
}

static device_method_t rtwn_pci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		rtwn_pci_probe),
	DEVMETHOD(device_attach,	rtwn_pci_attach),
	DEVMETHOD(device_detach,	rtwn_pci_detach),
	DEVMETHOD(device_shutdown,	rtwn_pci_shutdown),
	DEVMETHOD(device_suspend,	rtwn_pci_suspend),
	DEVMETHOD(device_resume,	rtwn_pci_resume),

	DEVMETHOD_END
};

static driver_t rtwn_pci_driver = {
	"rtwn",
	rtwn_pci_methods,
	sizeof(struct rtwn_pci_softc)
};

static devclass_t rtwn_pci_devclass;

DRIVER_MODULE(rtwn_pci, pci, rtwn_pci_driver, rtwn_pci_devclass, NULL, NULL);
MODULE_VERSION(rtwn_pci, 1);
MODULE_DEPEND(rtwn_pci, pci, 1, 1, 1);
MODULE_DEPEND(rtwn_pci, wlan, 1, 1, 1);
MODULE_DEPEND(rtwn_pci, rtwn, 2, 2, 2);
