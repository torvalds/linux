/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010-2016 Solarflare Communications Inc.
 * All rights reserved.
 *
 * This software was developed in part by Philip Paeps under contract for
 * Solarflare Communications, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation are
 * those of the authors and should not be interpreted as representing official
 * policies, either expressed or implied, of the FreeBSD Project.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/systm.h>
#include <sys/taskqueue.h>

#include "common/efx.h"

#include "sfxge.h"

static void
sfxge_ev_qcomplete(struct sfxge_evq *evq, boolean_t eop)
{
	struct sfxge_softc *sc;
	unsigned int index;
	struct sfxge_rxq *rxq;
	struct sfxge_txq *txq;

	SFXGE_EVQ_LOCK_ASSERT_OWNED(evq);

	sc = evq->sc;
	index = evq->index;
	rxq = sc->rxq[index];

	if ((txq = evq->txq) != NULL) {
		evq->txq = NULL;
		evq->txqs = &(evq->txq);

		do {
			struct sfxge_txq *next;

			next = txq->next;
			txq->next = NULL;

			KASSERT(txq->evq_index == index,
			    ("txq->evq_index != index"));

			if (txq->pending != txq->completed)
				sfxge_tx_qcomplete(txq, evq);

			txq = next;
		} while (txq != NULL);
	}

	if (rxq->pending != rxq->completed)
		sfxge_rx_qcomplete(rxq, eop);
}

static struct sfxge_rxq *
sfxge_get_rxq_by_label(struct sfxge_evq *evq, uint32_t label)
{
	struct sfxge_rxq *rxq;

	KASSERT(label == 0, ("unexpected rxq label != 0"));

	rxq = evq->sc->rxq[evq->index];

	KASSERT(rxq != NULL, ("rxq == NULL"));
	KASSERT(evq->index == rxq->index, ("evq->index != rxq->index"));

	return (rxq);
}

static boolean_t
sfxge_ev_rx(void *arg, uint32_t label, uint32_t id, uint32_t size,
	    uint16_t flags)
{
	struct sfxge_evq *evq;
	struct sfxge_softc *sc;
	struct sfxge_rxq *rxq;
	unsigned int stop;
	unsigned int delta;
	struct sfxge_rx_sw_desc *rx_desc;

	evq = arg;
	SFXGE_EVQ_LOCK_ASSERT_OWNED(evq);

	sc = evq->sc;

	if (evq->exception)
		goto done;

	rxq = sfxge_get_rxq_by_label(evq, label);
	if (__predict_false(rxq->init_state != SFXGE_RXQ_STARTED))
		goto done;

	stop = (id + 1) & rxq->ptr_mask;
	id = rxq->pending & rxq->ptr_mask;
	delta = (stop >= id) ? (stop - id) : (rxq->entries - id + stop);
	rxq->pending += delta;

	if (delta != 1) {
		if ((delta <= 0) ||
		    (delta > efx_nic_cfg_get(sc->enp)->enc_rx_batch_max)) {
			evq->exception = B_TRUE;

			device_printf(sc->dev, "RX completion out of order"
						  " (id=%#x delta=%u flags=%#x); resetting\n",
						  id, delta, flags);
			sfxge_schedule_reset(sc);

			goto done;
		}
	}

	rx_desc = &rxq->queue[id];

	prefetch_read_many(rx_desc->mbuf);

	for (; id != stop; id = (id + 1) & rxq->ptr_mask) {
		rx_desc = &rxq->queue[id];
		KASSERT(rx_desc->flags == EFX_DISCARD,
				("rx_desc->flags != EFX_DISCARD"));
		rx_desc->flags = flags;

		KASSERT(size < (1 << 16), ("size > (1 << 16)"));
		rx_desc->size = (uint16_t)size;
	}

	evq->rx_done++;

	if (rxq->pending - rxq->completed >= SFXGE_RX_BATCH)
		sfxge_ev_qcomplete(evq, B_FALSE);

done:
	return (evq->rx_done >= SFXGE_EV_BATCH);
}

static boolean_t
sfxge_ev_exception(void *arg, uint32_t code, uint32_t data)
{
	struct sfxge_evq *evq;
	struct sfxge_softc *sc;

	evq = (struct sfxge_evq *)arg;
	SFXGE_EVQ_LOCK_ASSERT_OWNED(evq);

	sc = evq->sc;

	DBGPRINT(sc->dev, "[%d] %s", evq->index,
			  (code == EFX_EXCEPTION_RX_RECOVERY) ? "RX_RECOVERY" :
			  (code == EFX_EXCEPTION_RX_DSC_ERROR) ? "RX_DSC_ERROR" :
			  (code == EFX_EXCEPTION_TX_DSC_ERROR) ? "TX_DSC_ERROR" :
			  (code == EFX_EXCEPTION_UNKNOWN_SENSOREVT) ? "UNKNOWN_SENSOREVT" :
			  (code == EFX_EXCEPTION_FWALERT_SRAM) ? "FWALERT_SRAM" :
			  (code == EFX_EXCEPTION_UNKNOWN_FWALERT) ? "UNKNOWN_FWALERT" :
			  (code == EFX_EXCEPTION_RX_ERROR) ? "RX_ERROR" :
			  (code == EFX_EXCEPTION_TX_ERROR) ? "TX_ERROR" :
			  (code == EFX_EXCEPTION_EV_ERROR) ? "EV_ERROR" :
			  "UNKNOWN");

	evq->exception = B_TRUE;

	if (code != EFX_EXCEPTION_UNKNOWN_SENSOREVT) {
		device_printf(sc->dev,
			      "hardware exception (code=%u); resetting\n",
			      code);
		sfxge_schedule_reset(sc);
	}

	return (B_FALSE);
}

static boolean_t
sfxge_ev_rxq_flush_done(void *arg, uint32_t rxq_index)
{
	struct sfxge_evq *evq;
	struct sfxge_softc *sc;
	struct sfxge_rxq *rxq;
	unsigned int index;
	uint16_t magic;

	evq = (struct sfxge_evq *)arg;
	SFXGE_EVQ_LOCK_ASSERT_OWNED(evq);

	sc = evq->sc;
	rxq = sc->rxq[rxq_index];

	KASSERT(rxq != NULL, ("rxq == NULL"));

	/* Resend a software event on the correct queue */
	index = rxq->index;
	if (index == evq->index) {
		sfxge_rx_qflush_done(rxq);
		return (B_FALSE);
	}

	evq = sc->evq[index];
	magic = sfxge_sw_ev_rxq_magic(SFXGE_SW_EV_RX_QFLUSH_DONE, rxq);

	KASSERT(evq->init_state == SFXGE_EVQ_STARTED,
	    ("evq not started"));
	efx_ev_qpost(evq->common, magic);

	return (B_FALSE);
}

static boolean_t
sfxge_ev_rxq_flush_failed(void *arg, uint32_t rxq_index)
{
	struct sfxge_evq *evq;
	struct sfxge_softc *sc;
	struct sfxge_rxq *rxq;
	unsigned int index;
	uint16_t magic;

	evq = (struct sfxge_evq *)arg;
	SFXGE_EVQ_LOCK_ASSERT_OWNED(evq);

	sc = evq->sc;
	rxq = sc->rxq[rxq_index];

	KASSERT(rxq != NULL, ("rxq == NULL"));

	/* Resend a software event on the correct queue */
	index = rxq->index;
	evq = sc->evq[index];
	magic = sfxge_sw_ev_rxq_magic(SFXGE_SW_EV_RX_QFLUSH_FAILED, rxq);

	KASSERT(evq->init_state == SFXGE_EVQ_STARTED,
	    ("evq not started"));
	efx_ev_qpost(evq->common, magic);

	return (B_FALSE);
}

static struct sfxge_txq *
sfxge_get_txq_by_label(struct sfxge_evq *evq, enum sfxge_txq_type label)
{
	unsigned int index;

	KASSERT((evq->sc->txq_dynamic_cksum_toggle_supported) ? (label == 0) :
		((evq->index == 0 && label < SFXGE_TXQ_NTYPES) ||
		 (label == SFXGE_TXQ_IP_TCP_UDP_CKSUM)),
		("unexpected txq label"));

	index = (evq->index == 0) ?
		label : (evq->index - 1 + SFXGE_EVQ0_N_TXQ(evq->sc));
	return (evq->sc->txq[index]);
}

static boolean_t
sfxge_ev_tx(void *arg, uint32_t label, uint32_t id)
{
	struct sfxge_evq *evq;
	struct sfxge_txq *txq;
	unsigned int stop;
	unsigned int delta;

	evq = (struct sfxge_evq *)arg;
	SFXGE_EVQ_LOCK_ASSERT_OWNED(evq);

	txq = sfxge_get_txq_by_label(evq, label);

	KASSERT(txq != NULL, ("txq == NULL"));
	KASSERT(evq->index == txq->evq_index,
	    ("evq->index != txq->evq_index"));

	if (__predict_false(txq->init_state != SFXGE_TXQ_STARTED))
		goto done;

	stop = (id + 1) & txq->ptr_mask;
	id = txq->pending & txq->ptr_mask;

	delta = (stop >= id) ? (stop - id) : (txq->entries - id + stop);
	txq->pending += delta;

	evq->tx_done++;

	if (txq->next == NULL &&
	    evq->txqs != &(txq->next)) {
		*(evq->txqs) = txq;
		evq->txqs = &(txq->next);
	}

	if (txq->pending - txq->completed >= SFXGE_TX_BATCH)
		sfxge_tx_qcomplete(txq, evq);

done:
	return (evq->tx_done >= SFXGE_EV_BATCH);
}

static boolean_t
sfxge_ev_txq_flush_done(void *arg, uint32_t txq_index)
{
	struct sfxge_evq *evq;
	struct sfxge_softc *sc;
	struct sfxge_txq *txq;
	uint16_t magic;

	evq = (struct sfxge_evq *)arg;
	SFXGE_EVQ_LOCK_ASSERT_OWNED(evq);

	sc = evq->sc;
	txq = sc->txq[txq_index];

	KASSERT(txq != NULL, ("txq == NULL"));
	KASSERT(txq->init_state == SFXGE_TXQ_INITIALIZED,
	    ("txq not initialized"));

	if (txq->evq_index == evq->index) {
		sfxge_tx_qflush_done(txq);
		return (B_FALSE);
	}

	/* Resend a software event on the correct queue */
	evq = sc->evq[txq->evq_index];
	magic = sfxge_sw_ev_txq_magic(SFXGE_SW_EV_TX_QFLUSH_DONE, txq);

	KASSERT(evq->init_state == SFXGE_EVQ_STARTED,
	    ("evq not started"));
	efx_ev_qpost(evq->common, magic);

	return (B_FALSE);
}

static boolean_t
sfxge_ev_software(void *arg, uint16_t magic)
{
	struct sfxge_evq *evq;
	struct sfxge_softc *sc;
	unsigned int label;

	evq = (struct sfxge_evq *)arg;
	SFXGE_EVQ_LOCK_ASSERT_OWNED(evq);

	sc = evq->sc;

	label = magic & SFXGE_MAGIC_DMAQ_LABEL_MASK;
	magic &= ~SFXGE_MAGIC_DMAQ_LABEL_MASK;

	switch (magic) {
	case SFXGE_SW_EV_MAGIC(SFXGE_SW_EV_RX_QFLUSH_DONE):
		sfxge_rx_qflush_done(sfxge_get_rxq_by_label(evq, label));
		break;

	case SFXGE_SW_EV_MAGIC(SFXGE_SW_EV_RX_QFLUSH_FAILED):
		sfxge_rx_qflush_failed(sfxge_get_rxq_by_label(evq, label));
		break;

	case SFXGE_SW_EV_MAGIC(SFXGE_SW_EV_RX_QREFILL):
		sfxge_rx_qrefill(sfxge_get_rxq_by_label(evq, label));
		break;

	case SFXGE_SW_EV_MAGIC(SFXGE_SW_EV_TX_QFLUSH_DONE): {
		struct sfxge_txq *txq = sfxge_get_txq_by_label(evq, label);

		KASSERT(txq != NULL, ("txq == NULL"));
		KASSERT(evq->index == txq->evq_index,
		    ("evq->index != txq->evq_index"));

		sfxge_tx_qflush_done(txq);
		break;
	}
	default:
		break;
	}

	return (B_FALSE);
}

static boolean_t
sfxge_ev_sram(void *arg, uint32_t code)
{
	(void)arg;
	(void)code;

	switch (code) {
	case EFX_SRAM_UPDATE:
		EFSYS_PROBE(sram_update);
		break;

	case EFX_SRAM_CLEAR:
		EFSYS_PROBE(sram_clear);
		break;

	case EFX_SRAM_ILLEGAL_CLEAR:
		EFSYS_PROBE(sram_illegal_clear);
		break;

	default:
		KASSERT(B_FALSE, ("Impossible SRAM event"));
		break;
	}

	return (B_FALSE);
}

static boolean_t
sfxge_ev_timer(void *arg, uint32_t index)
{
	(void)arg;
	(void)index;

	return (B_FALSE);
}

static boolean_t
sfxge_ev_wake_up(void *arg, uint32_t index)
{
	(void)arg;
	(void)index;

	return (B_FALSE);
}

#if EFSYS_OPT_QSTATS

static void
sfxge_evq_stat_update(struct sfxge_evq *evq)
{
	clock_t now;

	SFXGE_EVQ_LOCK(evq);

	if (__predict_false(evq->init_state != SFXGE_EVQ_STARTED))
		goto out;

	now = ticks;
	if ((unsigned int)(now - evq->stats_update_time) < (unsigned int)hz)
		goto out;

	evq->stats_update_time = now;
	efx_ev_qstats_update(evq->common, evq->stats);

out:
	SFXGE_EVQ_UNLOCK(evq);
}

static int
sfxge_evq_stat_handler(SYSCTL_HANDLER_ARGS)
{
	struct sfxge_evq *evq = arg1;
	struct sfxge_softc *sc = evq->sc;
	unsigned int id = arg2;

	SFXGE_ADAPTER_LOCK(sc);

	sfxge_evq_stat_update(evq);

	SFXGE_ADAPTER_UNLOCK(sc);

	return (SYSCTL_OUT(req, &evq->stats[id], sizeof(evq->stats[id])));
}

static int
sfxge_evq_stat_init(struct sfxge_evq *evq)
{
	struct sfxge_softc *sc = evq->sc;
	struct sysctl_ctx_list *ctx = device_get_sysctl_ctx(sc->dev);
	char name[16];
	struct sysctl_oid *evq_stats_node;
	unsigned int id;

	snprintf(name, sizeof(name), "%u", evq->index);
	evq_stats_node = SYSCTL_ADD_NODE(ctx,
					 SYSCTL_CHILDREN(sc->evqs_stats_node),
					 OID_AUTO, name, CTLFLAG_RD, NULL, "");
	if (evq_stats_node == NULL)
		return (ENOMEM);

	for (id = 0; id < EV_NQSTATS; id++) {
		SYSCTL_ADD_PROC(
			ctx, SYSCTL_CHILDREN(evq_stats_node),
			OID_AUTO, efx_ev_qstat_name(sc->enp, id),
			CTLTYPE_U64|CTLFLAG_RD,
			evq, id, sfxge_evq_stat_handler, "Q",
			"");
	}

	return (0);
}

static void
sfxge_ev_stat_update(struct sfxge_softc *sc)
{
	struct sfxge_evq *evq;
	unsigned int index;
	clock_t now;
	unsigned int id;

	SFXGE_ADAPTER_LOCK(sc);

	now = ticks;
	if ((unsigned int)(now - sc->ev_stats_update_time) < (unsigned int)hz)
		goto out;

	sc->ev_stats_update_time = now;

	memset(sc->ev_stats, 0, sizeof(sc->ev_stats));

	/* Update and add event counts from each event queue in turn */
	for (index = 0; index < sc->evq_count; index++) {
		evq = sc->evq[index];
		sfxge_evq_stat_update(evq);
		for (id = 0; id < EV_NQSTATS; id++)
			sc->ev_stats[id] += evq->stats[id];
	}
out:
	SFXGE_ADAPTER_UNLOCK(sc);
}

static int
sfxge_ev_stat_handler(SYSCTL_HANDLER_ARGS)
{
	struct sfxge_softc *sc = arg1;
	unsigned int id = arg2;

	sfxge_ev_stat_update(sc);

	return (SYSCTL_OUT(req, &sc->ev_stats[id], sizeof(sc->ev_stats[id])));
}

static void
sfxge_ev_stat_init(struct sfxge_softc *sc)
{
	struct sysctl_ctx_list *ctx = device_get_sysctl_ctx(sc->dev);
	struct sysctl_oid_list *stat_list;
	unsigned int id;
	char name[40];

	stat_list = SYSCTL_CHILDREN(sc->stats_node);

	for (id = 0; id < EV_NQSTATS; id++) {
		snprintf(name, sizeof(name), "ev_%s",
			 efx_ev_qstat_name(sc->enp, id));
		SYSCTL_ADD_PROC(
			ctx, stat_list,
			OID_AUTO, name, CTLTYPE_U64|CTLFLAG_RD,
			sc, id, sfxge_ev_stat_handler, "Q",
			"");
	}
}

#endif /* EFSYS_OPT_QSTATS */

static void
sfxge_ev_qmoderate(struct sfxge_softc *sc, unsigned int idx, unsigned int us)
{
	struct sfxge_evq *evq;
	efx_evq_t *eep;

	evq = sc->evq[idx];
	eep = evq->common;

	KASSERT(evq->init_state == SFXGE_EVQ_STARTED,
	    ("evq->init_state != SFXGE_EVQ_STARTED"));

	(void)efx_ev_qmoderate(eep, us);
}

static int
sfxge_int_mod_handler(SYSCTL_HANDLER_ARGS)
{
	struct sfxge_softc *sc = arg1;
	struct sfxge_intr *intr = &sc->intr;
	unsigned int moderation;
	int error;
	unsigned int index;

	SFXGE_ADAPTER_LOCK(sc);

	if (req->newptr != NULL) {
		if ((error = SYSCTL_IN(req, &moderation, sizeof(moderation)))
		    != 0)
			goto out;

		/* We may not be calling efx_ev_qmoderate() now,
		 * so we have to range-check the value ourselves.
		 */
		if (moderation >
		    efx_nic_cfg_get(sc->enp)->enc_evq_timer_max_us) {
			error = EINVAL;
			goto out;
		}

		sc->ev_moderation = moderation;
		if (intr->state == SFXGE_INTR_STARTED) {
			for (index = 0; index < sc->evq_count; index++)
				sfxge_ev_qmoderate(sc, index, moderation);
		}
	} else {
		error = SYSCTL_OUT(req, &sc->ev_moderation,
				   sizeof(sc->ev_moderation));
	}

out:
	SFXGE_ADAPTER_UNLOCK(sc);

	return (error);
}

static boolean_t
sfxge_ev_initialized(void *arg)
{
	struct sfxge_evq *evq;

	evq = (struct sfxge_evq *)arg;
	SFXGE_EVQ_LOCK_ASSERT_OWNED(evq);

	/* Init done events may be duplicated on 7xxx */
	KASSERT(evq->init_state == SFXGE_EVQ_STARTING ||
		evq->init_state == SFXGE_EVQ_STARTED,
	    ("evq not starting"));

	evq->init_state = SFXGE_EVQ_STARTED;

	return (0);
}

static boolean_t
sfxge_ev_link_change(void *arg, efx_link_mode_t	link_mode)
{
	struct sfxge_evq *evq;
	struct sfxge_softc *sc;

	evq = (struct sfxge_evq *)arg;
	SFXGE_EVQ_LOCK_ASSERT_OWNED(evq);

	sc = evq->sc;

	sfxge_mac_link_update(sc, link_mode);

	return (0);
}

static const efx_ev_callbacks_t sfxge_ev_callbacks = {
	.eec_initialized	= sfxge_ev_initialized,
	.eec_rx			= sfxge_ev_rx,
	.eec_tx			= sfxge_ev_tx,
	.eec_exception		= sfxge_ev_exception,
	.eec_rxq_flush_done	= sfxge_ev_rxq_flush_done,
	.eec_rxq_flush_failed	= sfxge_ev_rxq_flush_failed,
	.eec_txq_flush_done	= sfxge_ev_txq_flush_done,
	.eec_software		= sfxge_ev_software,
	.eec_sram		= sfxge_ev_sram,
	.eec_wake_up		= sfxge_ev_wake_up,
	.eec_timer		= sfxge_ev_timer,
	.eec_link_change	= sfxge_ev_link_change,
};


int
sfxge_ev_qpoll(struct sfxge_evq *evq)
{
	int rc;

	SFXGE_EVQ_LOCK(evq);

	if (__predict_false(evq->init_state != SFXGE_EVQ_STARTING &&
			    evq->init_state != SFXGE_EVQ_STARTED)) {
		rc = EINVAL;
		goto fail;
	}

	/* Synchronize the DMA memory for reading */
	bus_dmamap_sync(evq->mem.esm_tag, evq->mem.esm_map,
	    BUS_DMASYNC_POSTREAD);

	KASSERT(evq->rx_done == 0, ("evq->rx_done != 0"));
	KASSERT(evq->tx_done == 0, ("evq->tx_done != 0"));
	KASSERT(evq->txq == NULL, ("evq->txq != NULL"));
	KASSERT(evq->txqs == &evq->txq, ("evq->txqs != &evq->txq"));

	/* Poll the queue */
	efx_ev_qpoll(evq->common, &evq->read_ptr, &sfxge_ev_callbacks, evq);

	evq->rx_done = 0;
	evq->tx_done = 0;

	/* Perform any pending completion processing */
	sfxge_ev_qcomplete(evq, B_TRUE);

	/* Re-prime the event queue for interrupts */
	if ((rc = efx_ev_qprime(evq->common, evq->read_ptr)) != 0)
		goto fail;

	SFXGE_EVQ_UNLOCK(evq);

	return (0);

fail:
	SFXGE_EVQ_UNLOCK(evq);
	return (rc);
}

static void
sfxge_ev_qstop(struct sfxge_softc *sc, unsigned int index)
{
	struct sfxge_evq *evq;

	evq = sc->evq[index];

	KASSERT(evq->init_state == SFXGE_EVQ_STARTED,
	    ("evq->init_state != SFXGE_EVQ_STARTED"));

	SFXGE_EVQ_LOCK(evq);
	evq->init_state = SFXGE_EVQ_INITIALIZED;
	evq->read_ptr = 0;
	evq->exception = B_FALSE;

#if EFSYS_OPT_QSTATS
	/* Add event counts before discarding the common evq state */
	efx_ev_qstats_update(evq->common, evq->stats);
#endif

	efx_ev_qdestroy(evq->common);
	efx_sram_buf_tbl_clear(sc->enp, evq->buf_base_id,
	    EFX_EVQ_NBUFS(evq->entries));
	SFXGE_EVQ_UNLOCK(evq);
}

static int
sfxge_ev_qstart(struct sfxge_softc *sc, unsigned int index)
{
	struct sfxge_evq *evq;
	efsys_mem_t *esmp;
	int count;
	int rc;

	evq = sc->evq[index];
	esmp = &evq->mem;

	KASSERT(evq->init_state == SFXGE_EVQ_INITIALIZED,
	    ("evq->init_state != SFXGE_EVQ_INITIALIZED"));

	/* Clear all events. */
	(void)memset(esmp->esm_base, 0xff, EFX_EVQ_SIZE(evq->entries));

	/* Program the buffer table. */
	if ((rc = efx_sram_buf_tbl_set(sc->enp, evq->buf_base_id, esmp,
	    EFX_EVQ_NBUFS(evq->entries))) != 0)
		return (rc);

	/* Create the common code event queue. */
	if ((rc = efx_ev_qcreate(sc->enp, index, esmp, evq->entries,
	    evq->buf_base_id, sc->ev_moderation, EFX_EVQ_FLAGS_TYPE_AUTO,
	    &evq->common)) != 0)
		goto fail;

	SFXGE_EVQ_LOCK(evq);

	/* Prime the event queue for interrupts */
	if ((rc = efx_ev_qprime(evq->common, evq->read_ptr)) != 0)
		goto fail2;

	evq->init_state = SFXGE_EVQ_STARTING;

	SFXGE_EVQ_UNLOCK(evq);

	/* Wait for the initialization event */
	count = 0;
	do {
		/* Pause for 100 ms */
		pause("sfxge evq init", hz / 10);

		/* Check to see if the test event has been processed */
		if (evq->init_state == SFXGE_EVQ_STARTED)
			goto done;

	} while (++count < 20);

	rc = ETIMEDOUT;
	goto fail3;

done:
	return (0);

fail3:
	SFXGE_EVQ_LOCK(evq);
	evq->init_state = SFXGE_EVQ_INITIALIZED;
fail2:
	SFXGE_EVQ_UNLOCK(evq);
	efx_ev_qdestroy(evq->common);
fail:
	efx_sram_buf_tbl_clear(sc->enp, evq->buf_base_id,
	    EFX_EVQ_NBUFS(evq->entries));

	return (rc);
}

void
sfxge_ev_stop(struct sfxge_softc *sc)
{
	struct sfxge_intr *intr;
	efx_nic_t *enp;
	int index;

	intr = &sc->intr;
	enp = sc->enp;

	KASSERT(intr->state == SFXGE_INTR_STARTED,
	    ("Interrupts not started"));

	/* Stop the event queue(s) */
	index = sc->evq_count;
	while (--index >= 0)
		sfxge_ev_qstop(sc, index);

	/* Tear down the event module */
	efx_ev_fini(enp);
}

int
sfxge_ev_start(struct sfxge_softc *sc)
{
	struct sfxge_intr *intr;
	int index;
	int rc;

	intr = &sc->intr;

	KASSERT(intr->state == SFXGE_INTR_STARTED,
	    ("intr->state != SFXGE_INTR_STARTED"));

	/* Initialize the event module */
	if ((rc = efx_ev_init(sc->enp)) != 0)
		return (rc);

	/* Start the event queues */
	for (index = 0; index < sc->evq_count; index++) {
		if ((rc = sfxge_ev_qstart(sc, index)) != 0)
			goto fail;
	}

	return (0);

fail:
	/* Stop the event queue(s) */
	while (--index >= 0)
		sfxge_ev_qstop(sc, index);

	/* Tear down the event module */
	efx_ev_fini(sc->enp);

	return (rc);
}

static void
sfxge_ev_qfini(struct sfxge_softc *sc, unsigned int index)
{
	struct sfxge_evq *evq;

	evq = sc->evq[index];

	KASSERT(evq->init_state == SFXGE_EVQ_INITIALIZED,
	    ("evq->init_state != SFXGE_EVQ_INITIALIZED"));
	KASSERT(evq->txqs == &evq->txq, ("evq->txqs != &evq->txq"));

	sfxge_dma_free(&evq->mem);

	sc->evq[index] = NULL;

	SFXGE_EVQ_LOCK_DESTROY(evq);

	free(evq, M_SFXGE);
}

static int
sfxge_ev_qinit(struct sfxge_softc *sc, unsigned int index)
{
	struct sfxge_evq *evq;
	efsys_mem_t *esmp;
	int rc;

	KASSERT(index < SFXGE_RX_SCALE_MAX, ("index >= SFXGE_RX_SCALE_MAX"));

	evq = malloc(sizeof(struct sfxge_evq), M_SFXGE, M_ZERO | M_WAITOK);
	evq->sc = sc;
	evq->index = index;
	sc->evq[index] = evq;
	esmp = &evq->mem;

	/* Build an event queue with room for one event per tx and rx buffer,
	 * plus some extra for link state events and MCDI completions.
	 * There are three tx queues in the first event queue and one in
	 * other.
	 */
	if (index == 0)
		evq->entries =
			ROUNDUP_POW_OF_TWO(sc->rxq_entries +
					   3 * sc->txq_entries +
					   128);
	else
		evq->entries =
			ROUNDUP_POW_OF_TWO(sc->rxq_entries +
					   sc->txq_entries +
					   128);

	/* Initialise TX completion list */
	evq->txqs = &evq->txq;

	/* Allocate DMA space. */
	if ((rc = sfxge_dma_alloc(sc, EFX_EVQ_SIZE(evq->entries), esmp)) != 0)
		return (rc);

	/* Allocate buffer table entries. */
	sfxge_sram_buf_tbl_alloc(sc, EFX_EVQ_NBUFS(evq->entries),
				 &evq->buf_base_id);

	SFXGE_EVQ_LOCK_INIT(evq, device_get_nameunit(sc->dev), index);

	evq->init_state = SFXGE_EVQ_INITIALIZED;

#if EFSYS_OPT_QSTATS
	rc = sfxge_evq_stat_init(evq);
	if (rc != 0)
		goto fail_evq_stat_init;
#endif

	return (0);

#if EFSYS_OPT_QSTATS
fail_evq_stat_init:
	evq->init_state = SFXGE_EVQ_UNINITIALIZED;
	SFXGE_EVQ_LOCK_DESTROY(evq);
	sfxge_dma_free(esmp);
	sc->evq[index] = NULL;
	free(evq, M_SFXGE);

	return (rc);
#endif
}

void
sfxge_ev_fini(struct sfxge_softc *sc)
{
	struct sfxge_intr *intr;
	int index;

	intr = &sc->intr;

	KASSERT(intr->state == SFXGE_INTR_INITIALIZED,
	    ("intr->state != SFXGE_INTR_INITIALIZED"));

	sc->ev_moderation = 0;

	/* Tear down the event queue(s). */
	index = sc->evq_count;
	while (--index >= 0)
		sfxge_ev_qfini(sc, index);

	sc->evq_count = 0;
}

int
sfxge_ev_init(struct sfxge_softc *sc)
{
	struct sysctl_ctx_list *sysctl_ctx = device_get_sysctl_ctx(sc->dev);
	struct sysctl_oid *sysctl_tree = device_get_sysctl_tree(sc->dev);
	struct sfxge_intr *intr;
	int index;
	int rc;

	intr = &sc->intr;

	sc->evq_count = intr->n_alloc;

	KASSERT(intr->state == SFXGE_INTR_INITIALIZED,
	    ("intr->state != SFXGE_INTR_INITIALIZED"));

	/* Set default interrupt moderation; add a sysctl to
	 * read and change it.
	 */
	sc->ev_moderation = SFXGE_MODERATION;
	SYSCTL_ADD_PROC(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree),
			OID_AUTO, "int_mod", CTLTYPE_UINT|CTLFLAG_RW,
			sc, 0, sfxge_int_mod_handler, "IU",
			"sfxge interrupt moderation (us)");

#if EFSYS_OPT_QSTATS
	sc->evqs_stats_node = SYSCTL_ADD_NODE(
		device_get_sysctl_ctx(sc->dev), SYSCTL_CHILDREN(sc->stats_node),
		OID_AUTO, "evq", CTLFLAG_RD, NULL, "Event queues stats");
	if (sc->evqs_stats_node == NULL) {
		rc = ENOMEM;
		goto fail_evqs_stats_node;
	}
#endif

	/*
	 * Initialize the event queue(s) - one per interrupt.
	 */
	for (index = 0; index < sc->evq_count; index++) {
		if ((rc = sfxge_ev_qinit(sc, index)) != 0)
			goto fail;
	}

#if EFSYS_OPT_QSTATS
	sfxge_ev_stat_init(sc);
#endif

	return (0);

fail:
	while (--index >= 0)
		sfxge_ev_qfini(sc, index);

#if EFSYS_OPT_QSTATS
fail_evqs_stats_node:
#endif
	sc->evq_count = 0;
	return (rc);
}
