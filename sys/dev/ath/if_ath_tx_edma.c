/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Adrian Chadd <adrian@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Driver for the Atheros Wireless LAN controller.
 *
 * This software is derived from work of Atsushi Onoe; his contribution
 * is greatly appreciated.
 */

#include "opt_inet.h"
#include "opt_ath.h"
/*
 * This is needed for register operations which are performed
 * by the driver - eg, calls to ath_hal_gettsf32().
 *
 * It's also required for any AH_DEBUG checks in here, eg the
 * module dependencies.
 */
#include "opt_ah.h"
#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/errno.h>
#include <sys/callout.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/kthread.h>
#include <sys/taskqueue.h>
#include <sys/priv.h>
#include <sys/module.h>
#include <sys/ktr.h>
#include <sys/smp.h>	/* for mp_ncpus */

#include <machine/bus.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_llc.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_regdomain.h>
#ifdef IEEE80211_SUPPORT_SUPERG
#include <net80211/ieee80211_superg.h>
#endif
#ifdef IEEE80211_SUPPORT_TDMA
#include <net80211/ieee80211_tdma.h>
#endif

#include <net/bpf.h>

#ifdef INET
#include <netinet/in.h>
#include <netinet/if_ether.h>
#endif

#include <dev/ath/if_athvar.h>
#include <dev/ath/ath_hal/ah_devid.h>		/* XXX for softled */
#include <dev/ath/ath_hal/ah_diagcodes.h>

#include <dev/ath/if_ath_debug.h>
#include <dev/ath/if_ath_misc.h>
#include <dev/ath/if_ath_tsf.h>
#include <dev/ath/if_ath_tx.h>
#include <dev/ath/if_ath_sysctl.h>
#include <dev/ath/if_ath_led.h>
#include <dev/ath/if_ath_keycache.h>
#include <dev/ath/if_ath_rx.h>
#include <dev/ath/if_ath_beacon.h>
#include <dev/ath/if_athdfs.h>
#include <dev/ath/if_ath_descdma.h>

#ifdef ATH_TX99_DIAG
#include <dev/ath/ath_tx99/ath_tx99.h>
#endif

#include <dev/ath/if_ath_tx_edma.h>

#ifdef	ATH_DEBUG_ALQ
#include <dev/ath/if_ath_alq.h>
#endif

/*
 * some general macros
 */
#define	INCR(_l, _sz)		(_l) ++; (_l) &= ((_sz) - 1)
#define	DECR(_l, _sz)		(_l) --; (_l) &= ((_sz) - 1)

/*
 * XXX doesn't belong here, and should be tunable
 */
#define	ATH_TXSTATUS_RING_SIZE	512

MALLOC_DECLARE(M_ATHDEV);

static void ath_edma_tx_processq(struct ath_softc *sc, int dosched);

#ifdef	ATH_DEBUG_ALQ
static void
ath_tx_alq_edma_push(struct ath_softc *sc, int txq, int nframes,
    int fifo_depth, int frame_cnt)
{
	struct if_ath_alq_tx_fifo_push aq;

	aq.txq = htobe32(txq);
	aq.nframes = htobe32(nframes);
	aq.fifo_depth = htobe32(fifo_depth);
	aq.frame_cnt = htobe32(frame_cnt);

	if_ath_alq_post(&sc->sc_alq, ATH_ALQ_TX_FIFO_PUSH,
	    sizeof(aq),
	    (const char *) &aq);
}
#endif	/* ATH_DEBUG_ALQ */

/*
 * XXX TODO: push an aggregate as a single FIFO slot, even though
 * it may not meet the TXOP for say, DBA-gated traffic in TDMA mode.
 *
 * The TX completion code handles a TX FIFO slot having multiple frames,
 * aggregate or otherwise, but it may just make things easier to deal
 * with.
 *
 * XXX TODO: track the number of aggregate subframes and put that in the
 * push alq message.
 */
static void
ath_tx_edma_push_staging_list(struct ath_softc *sc, struct ath_txq *txq,
    int limit)
{
	struct ath_buf *bf, *bf_last;
	struct ath_buf *bfi, *bfp;
	int i, sqdepth;
	TAILQ_HEAD(axq_q_f_s, ath_buf)  sq;

	ATH_TXQ_LOCK_ASSERT(txq);

	DPRINTF(sc, ATH_DEBUG_XMIT | ATH_DEBUG_TX_PROC,
	    "%s: called; TXQ=%d, fifo.depth=%d, axq_q empty=%d\n",
	    __func__,
	    txq->axq_qnum,
	    txq->axq_fifo_depth,
	    !! (TAILQ_EMPTY(&txq->axq_q)));

	/*
	 * Don't bother doing any work if it's full.
	 */
	if (txq->axq_fifo_depth >= HAL_TXFIFO_DEPTH)
		return;

	if (TAILQ_EMPTY(&txq->axq_q))
		return;

	TAILQ_INIT(&sq);

	/*
	 * First pass - walk sq, queue up to 'limit' entries,
	 * subtract them from the staging queue.
	 */
	sqdepth = 0;
	for (i = 0; i < limit; i++) {
		/* Grab the head entry */
		bf = ATH_TXQ_FIRST(txq);
		if (bf == NULL)
			break;
		ATH_TXQ_REMOVE(txq, bf, bf_list);

		/* Queue it into our staging list */
		TAILQ_INSERT_TAIL(&sq, bf, bf_list);

		/* Ensure the flags are cleared */
		bf->bf_flags &= ~(ATH_BUF_FIFOPTR | ATH_BUF_FIFOEND);
		sqdepth++;
	}

	/*
	 * Ok, so now we have a staging list of up to 'limit'
	 * frames from the txq.  Now let's wrap that up
	 * into its own list and pass that to the hardware
	 * as one FIFO entry.
	 */

	bf = TAILQ_FIRST(&sq);
	bf_last = TAILQ_LAST(&sq, axq_q_s);

	/*
	 * Ok, so here's the gymnastics reqiured to make this
	 * all sensible.
	 */

	/*
	 * Tag the first/last buffer appropriately.
	 */
	bf->bf_flags |= ATH_BUF_FIFOPTR;
	bf_last->bf_flags |= ATH_BUF_FIFOEND;

	/*
	 * Walk the descriptor list and link them appropriately.
	 */
	bfp = NULL;
	TAILQ_FOREACH(bfi, &sq, bf_list) {
		if (bfp != NULL) {
			ath_hal_settxdesclink(sc->sc_ah, bfp->bf_lastds,
			    bfi->bf_daddr);
		}
		bfp = bfi;
	}

	i = 0;
	TAILQ_FOREACH(bfi, &sq, bf_list) {
#ifdef	ATH_DEBUG
		if (sc->sc_debug & ATH_DEBUG_XMIT_DESC)
			ath_printtxbuf(sc, bfi, txq->axq_qnum, i, 0);
#endif/* ATH_DEBUG */
#ifdef	ATH_DEBUG_ALQ
		if (if_ath_alq_checkdebug(&sc->sc_alq, ATH_ALQ_EDMA_TXDESC))
			ath_tx_alq_post(sc, bfi);
#endif /* ATH_DEBUG_ALQ */
		i++;
	}

	/*
	 * We now need to push this set of frames onto the tail
	 * of the FIFO queue.  We don't adjust the aggregate
	 * count, only the queue depth counter(s).
	 * We also need to blank the link pointer now.
	 */

	TAILQ_CONCAT(&txq->fifo.axq_q, &sq, bf_list);
	/* Bump total queue tracking in FIFO queue */
	txq->fifo.axq_depth += sqdepth;

	/* Bump FIFO queue */
	txq->axq_fifo_depth++;
	DPRINTF(sc, ATH_DEBUG_XMIT | ATH_DEBUG_TX_PROC,
	    "%s: queued %d packets; depth=%d, fifo depth=%d\n",
	    __func__, sqdepth, txq->fifo.axq_depth, txq->axq_fifo_depth);

	/* Push the first entry into the hardware */
	ath_hal_puttxbuf(sc->sc_ah, txq->axq_qnum, bf->bf_daddr);

	/* Push start on the DMA if it's not already started */
	ath_hal_txstart(sc->sc_ah, txq->axq_qnum);

#ifdef	ATH_DEBUG_ALQ
	ath_tx_alq_edma_push(sc, txq->axq_qnum, sqdepth,
	    txq->axq_fifo_depth,
	    txq->fifo.axq_depth);
#endif /* ATH_DEBUG_ALQ */
}

#define	TX_BATCH_SIZE	32

/*
 * Push some frames into the TX FIFO if we have space.
 */
static void
ath_edma_tx_fifo_fill(struct ath_softc *sc, struct ath_txq *txq)
{

	ATH_TXQ_LOCK_ASSERT(txq);

	DPRINTF(sc, ATH_DEBUG_TX_PROC,
	    "%s: Q%d: called; fifo.depth=%d, fifo depth=%d, depth=%d, aggr_depth=%d\n",
	    __func__,
	    txq->axq_qnum,
	    txq->fifo.axq_depth,
	    txq->axq_fifo_depth,
	    txq->axq_depth,
	    txq->axq_aggr_depth);

	/*
	 * For now, push up to 32 frames per TX FIFO slot.
	 * If more are in the hardware queue then they'll
	 * get populated when we try to send another frame
	 * or complete a frame - so at most there'll be
	 * 32 non-AMPDU frames per node/TID anyway.
	 *
	 * Note that the hardware staging queue will limit
	 * how many frames in total we will have pushed into
	 * here.
	 *
	 * Later on, we'll want to push less frames into
	 * the TX FIFO since we don't want to necessarily
	 * fill tens or hundreds of milliseconds of potential
	 * frames.
	 *
	 * However, we need more frames right now because of
	 * how the MAC implements the frame scheduling policy.
	 * It only ungates a single FIFO entry at a time,
	 * and will run that until CHNTIME expires or the
	 * end of that FIFO entry descriptor list is reached.
	 * So for TDMA we suffer a big performance penalty -
	 * single TX FIFO entries mean the MAC only sends out
	 * one frame per DBA event, which turned out on average
	 * 6ms per TX frame.
	 *
	 * So, for aggregates it's okay - it'll push two at a
	 * time and this will just do them more efficiently.
	 * For non-aggregates it'll do 4 at a time, up to the
	 * non-aggr limit (non_aggr, which is 32.)  They should
	 * be time based rather than a hard count, but I also
	 * do need sleep.
	 */

	/*
	 * Do some basic, basic batching to the hardware
	 * queue.
	 *
	 * If we have TX_BATCH_SIZE entries in the staging
	 * queue, then let's try to send them all in one hit.
	 *
	 * Ensure we don't push more than TX_BATCH_SIZE worth
	 * in, otherwise we end up draining 8 slots worth of
	 * 32 frames into the hardware queue and then we don't
	 * attempt to push more frames in until we empty the
	 * FIFO.
	 */
	if (txq->axq_depth >= TX_BATCH_SIZE / 2 &&
	    txq->fifo.axq_depth <= TX_BATCH_SIZE) {
		ath_tx_edma_push_staging_list(sc, txq, TX_BATCH_SIZE);
	}

	/*
	 * Aggregate check: if we have less than two FIFO slots
	 * busy and we have some aggregate frames, queue it.
	 *
	 * Now, ideally we'd just check to see if the scheduler
	 * has given us aggregate frames and push them into the FIFO
	 * as individual slots, as honestly we should just be pushing
	 * a single aggregate in as one FIFO slot.
	 *
	 * Let's do that next once I know this works.
	 */
	else if (txq->axq_aggr_depth > 0 && txq->axq_fifo_depth < 2)
		ath_tx_edma_push_staging_list(sc, txq, TX_BATCH_SIZE);

	/*
	 *
	 * If we have less, and the TXFIFO isn't empty, let's
	 * wait until we've finished sending the FIFO.
	 *
	 * If we have less, and the TXFIFO is empty, then
	 * send them.
	 */
	else if (txq->axq_fifo_depth == 0) {
		ath_tx_edma_push_staging_list(sc, txq, TX_BATCH_SIZE);
	}
}

/*
 * Re-initialise the DMA FIFO with the current contents of
 * said TXQ.
 *
 * This should only be called as part of the chip reset path, as it
 * assumes the FIFO is currently empty.
 */
static void
ath_edma_dma_restart(struct ath_softc *sc, struct ath_txq *txq)
{
	struct ath_buf *bf;
	int i = 0;
	int fifostart = 1;
	int old_fifo_depth;

	DPRINTF(sc, ATH_DEBUG_RESET, "%s: Q%d: called\n",
	    __func__,
	    txq->axq_qnum);

	ATH_TXQ_LOCK_ASSERT(txq);

	/*
	 * Let's log if the tracked FIFO depth doesn't match
	 * what we actually push in.
	 */
	old_fifo_depth = txq->axq_fifo_depth;
	txq->axq_fifo_depth = 0;

	/*
	 * Walk the FIFO staging list, looking for "head" entries.
	 * Since we may have a partially completed list of frames,
	 * we push the first frame we see into the FIFO and re-mark
	 * it as the head entry.  We then skip entries until we see
	 * FIFO end, at which point we get ready to push another
	 * entry into the FIFO.
	 */
	TAILQ_FOREACH(bf, &txq->fifo.axq_q, bf_list) {
		/*
		 * If we're looking for FIFOEND and we haven't found
		 * it, skip.
		 *
		 * If we're looking for FIFOEND and we've found it,
		 * reset for another descriptor.
		 */
#ifdef	ATH_DEBUG
		if (sc->sc_debug & ATH_DEBUG_XMIT_DESC)
			ath_printtxbuf(sc, bf, txq->axq_qnum, i, 0);
#endif/* ATH_DEBUG */
#ifdef	ATH_DEBUG_ALQ
		if (if_ath_alq_checkdebug(&sc->sc_alq, ATH_ALQ_EDMA_TXDESC))
			ath_tx_alq_post(sc, bf);
#endif /* ATH_DEBUG_ALQ */

		if (fifostart == 0) {
			if (bf->bf_flags & ATH_BUF_FIFOEND)
				fifostart = 1;
			continue;
		}

		/* Make sure we're not overflowing the FIFO! */
		if (txq->axq_fifo_depth >= HAL_TXFIFO_DEPTH) {
			device_printf(sc->sc_dev,
			    "%s: Q%d: more frames in the queue; FIFO depth=%d?!\n",
			    __func__,
			    txq->axq_qnum,
			    txq->axq_fifo_depth);
		}

#if 0
		DPRINTF(sc, ATH_DEBUG_RESET,
		    "%s: Q%d: depth=%d: pushing bf=%p; start=%d, end=%d\n",
		    __func__,
		    txq->axq_qnum,
		    txq->axq_fifo_depth,
		    bf,
		    !! (bf->bf_flags & ATH_BUF_FIFOPTR),
		    !! (bf->bf_flags & ATH_BUF_FIFOEND));
#endif

		/*
		 * Set this to be the first buffer in the FIFO
		 * list - even if it's also the last buffer in
		 * a FIFO list!
		 */
		bf->bf_flags |= ATH_BUF_FIFOPTR;

		/* Push it into the FIFO and bump the FIFO count */
		ath_hal_puttxbuf(sc->sc_ah, txq->axq_qnum, bf->bf_daddr);
		txq->axq_fifo_depth++;

		/*
		 * If this isn't the last entry either, let's
		 * clear fifostart so we continue looking for
		 * said last entry.
		 */
		if (! (bf->bf_flags & ATH_BUF_FIFOEND))
			fifostart = 0;
		i++;
	}

	/* Only bother starting the queue if there's something in it */
	if (i > 0)
		ath_hal_txstart(sc->sc_ah, txq->axq_qnum);

	DPRINTF(sc, ATH_DEBUG_RESET, "%s: Q%d: FIFO depth was %d, is %d\n",
	    __func__,
	    txq->axq_qnum,
	    old_fifo_depth,
	    txq->axq_fifo_depth);

	/* And now, let's check! */
	if (txq->axq_fifo_depth != old_fifo_depth) {
		device_printf(sc->sc_dev,
		    "%s: Q%d: FIFO depth should be %d, is %d\n",
		    __func__,
		    txq->axq_qnum,
		    old_fifo_depth,
		    txq->axq_fifo_depth);
	}
}

/*
 * Hand off this frame to a hardware queue.
 *
 * Things are a bit hairy in the EDMA world.  The TX FIFO is only
 * 8 entries deep, so we need to keep track of exactly what we've
 * pushed into the FIFO and what's just sitting in the TX queue,
 * waiting to go out.
 *
 * So this is split into two halves - frames get appended to the
 * TXQ; then a scheduler is called to push some frames into the
 * actual TX FIFO.
 */
static void
ath_edma_xmit_handoff_hw(struct ath_softc *sc, struct ath_txq *txq,
    struct ath_buf *bf)
{

	ATH_TXQ_LOCK(txq);

	KASSERT((bf->bf_flags & ATH_BUF_BUSY) == 0,
	    ("%s: busy status 0x%x", __func__, bf->bf_flags));

	/*
	 * XXX TODO: write a hard-coded check to ensure that
	 * the queue id in the TX descriptor matches txq->axq_qnum.
	 */

	/* Update aggr stats */
	if (bf->bf_state.bfs_aggr)
		txq->axq_aggr_depth++;

	/* Push and update frame stats */
	ATH_TXQ_INSERT_TAIL(txq, bf, bf_list);

	/*
	 * Finally, call the FIFO schedule routine to schedule some
	 * frames to the FIFO.
	 */
	ath_edma_tx_fifo_fill(sc, txq);
	ATH_TXQ_UNLOCK(txq);
}

/*
 * Hand off this frame to a multicast software queue.
 *
 * The EDMA TX CABQ will get a list of chained frames, chained
 * together using the next pointer.  The single head of that
 * particular queue is pushed to the hardware CABQ.
 */
static void
ath_edma_xmit_handoff_mcast(struct ath_softc *sc, struct ath_txq *txq,
    struct ath_buf *bf)
{

	ATH_TX_LOCK_ASSERT(sc);
	KASSERT((bf->bf_flags & ATH_BUF_BUSY) == 0,
	    ("%s: busy status 0x%x", __func__, bf->bf_flags));

	ATH_TXQ_LOCK(txq);
	/*
	 * XXX this is mostly duplicated in ath_tx_handoff_mcast().
	 */
	if (ATH_TXQ_LAST(txq, axq_q_s) != NULL) {
		struct ath_buf *bf_last = ATH_TXQ_LAST(txq, axq_q_s);
		struct ieee80211_frame *wh;

		/* mark previous frame */
		wh = mtod(bf_last->bf_m, struct ieee80211_frame *);
		wh->i_fc[1] |= IEEE80211_FC1_MORE_DATA;

		/* re-sync buffer to memory */
		bus_dmamap_sync(sc->sc_dmat, bf_last->bf_dmamap,
		   BUS_DMASYNC_PREWRITE);

		/* link descriptor */
		ath_hal_settxdesclink(sc->sc_ah,
		    bf_last->bf_lastds,
		    bf->bf_daddr);
	}
#ifdef	ATH_DEBUG_ALQ
	if (if_ath_alq_checkdebug(&sc->sc_alq, ATH_ALQ_EDMA_TXDESC))
		ath_tx_alq_post(sc, bf);
#endif	/* ATH_DEBUG_ALQ */
	ATH_TXQ_INSERT_TAIL(txq, bf, bf_list);
	ATH_TXQ_UNLOCK(txq);
}

/*
 * Handoff this frame to the hardware.
 *
 * For the multicast queue, this will treat it as a software queue
 * and append it to the list, after updating the MORE_DATA flag
 * in the previous frame.  The cabq processing code will ensure
 * that the queue contents gets transferred over.
 *
 * For the hardware queues, this will queue a frame to the queue
 * like before, then populate the FIFO from that.  Since the
 * EDMA hardware has 8 FIFO slots per TXQ, this ensures that
 * frames such as management frames don't get prematurely dropped.
 *
 * This does imply that a similar flush-hwq-to-fifoq method will
 * need to be called from the processq function, before the
 * per-node software scheduler is called.
 */
static void
ath_edma_xmit_handoff(struct ath_softc *sc, struct ath_txq *txq,
    struct ath_buf *bf)
{

	DPRINTF(sc, ATH_DEBUG_XMIT_DESC,
	    "%s: called; bf=%p, txq=%p, qnum=%d\n",
	    __func__,
	    bf,
	    txq,
	    txq->axq_qnum);

	if (txq->axq_qnum == ATH_TXQ_SWQ)
		ath_edma_xmit_handoff_mcast(sc, txq, bf);
	else
		ath_edma_xmit_handoff_hw(sc, txq, bf);
}

static int
ath_edma_setup_txfifo(struct ath_softc *sc, int qnum)
{
	struct ath_tx_edma_fifo *te = &sc->sc_txedma[qnum];

	te->m_fifo = malloc(sizeof(struct ath_buf *) * HAL_TXFIFO_DEPTH,
	    M_ATHDEV,
	    M_NOWAIT | M_ZERO);
	if (te->m_fifo == NULL) {
		device_printf(sc->sc_dev, "%s: malloc failed\n",
		    __func__);
		return (-ENOMEM);
	}

	/*
	 * Set initial "empty" state.
	 */
	te->m_fifo_head = te->m_fifo_tail = te->m_fifo_depth = 0;
	
	return (0);
}

static int
ath_edma_free_txfifo(struct ath_softc *sc, int qnum)
{
	struct ath_tx_edma_fifo *te = &sc->sc_txedma[qnum];

	/* XXX TODO: actually deref the ath_buf entries? */
	free(te->m_fifo, M_ATHDEV);
	return (0);
}

static int
ath_edma_dma_txsetup(struct ath_softc *sc)
{
	int error;
	int i;

	error = ath_descdma_alloc_desc(sc, &sc->sc_txsdma,
	    NULL, "txcomp", sc->sc_tx_statuslen, ATH_TXSTATUS_RING_SIZE);
	if (error != 0)
		return (error);

	ath_hal_setuptxstatusring(sc->sc_ah,
	    (void *) sc->sc_txsdma.dd_desc,
	    sc->sc_txsdma.dd_desc_paddr,
	    ATH_TXSTATUS_RING_SIZE);

	for (i = 0; i < HAL_NUM_TX_QUEUES; i++) {
		ath_edma_setup_txfifo(sc, i);
	}

	return (0);
}

static int
ath_edma_dma_txteardown(struct ath_softc *sc)
{
	int i;

	for (i = 0; i < HAL_NUM_TX_QUEUES; i++) {
		ath_edma_free_txfifo(sc, i);
	}

	ath_descdma_cleanup(sc, &sc->sc_txsdma, NULL);
	return (0);
}

/*
 * Drain all TXQs, potentially after completing the existing completed
 * frames.
 */
static void
ath_edma_tx_drain(struct ath_softc *sc, ATH_RESET_TYPE reset_type)
{
	int i;

	DPRINTF(sc, ATH_DEBUG_RESET, "%s: called\n", __func__);

	(void) ath_stoptxdma(sc);

	/*
	 * If reset type is noloss, the TX FIFO needs to be serviced
	 * and those frames need to be handled.
	 *
	 * Otherwise, just toss everything in each TX queue.
	 */
	if (reset_type == ATH_RESET_NOLOSS) {
		ath_edma_tx_processq(sc, 0);
		for (i = 0; i < HAL_NUM_TX_QUEUES; i++) {
			if (ATH_TXQ_SETUP(sc, i)) {
				ATH_TXQ_LOCK(&sc->sc_txq[i]);
				/*
				 * Free the holding buffer; DMA is now
				 * stopped.
				 */
				ath_txq_freeholdingbuf(sc, &sc->sc_txq[i]);
				/*
				 * Reset the link pointer to NULL; there's
				 * no frames to chain DMA to.
				 */
				sc->sc_txq[i].axq_link = NULL;
				ATH_TXQ_UNLOCK(&sc->sc_txq[i]);
			}
		}
	} else {
		for (i = 0; i < HAL_NUM_TX_QUEUES; i++) {
			if (ATH_TXQ_SETUP(sc, i))
				ath_tx_draintxq(sc, &sc->sc_txq[i]);
		}
	}

	/* XXX dump out the TX completion FIFO contents */

	/* XXX dump out the frames */

	sc->sc_wd_timer = 0;
}

/*
 * TX completion tasklet.
 */

static void
ath_edma_tx_proc(void *arg, int npending)
{
	struct ath_softc *sc = (struct ath_softc *) arg;

	ATH_PCU_LOCK(sc);
	sc->sc_txproc_cnt++;
	ATH_PCU_UNLOCK(sc);

	ATH_LOCK(sc);
	ath_power_set_power_state(sc, HAL_PM_AWAKE);
	ATH_UNLOCK(sc);

#if 0
	DPRINTF(sc, ATH_DEBUG_TX_PROC, "%s: called, npending=%d\n",
	    __func__, npending);
#endif
	ath_edma_tx_processq(sc, 1);


	ATH_PCU_LOCK(sc);
	sc->sc_txproc_cnt--;
	ATH_PCU_UNLOCK(sc);

	ATH_LOCK(sc);
	ath_power_restore_power_state(sc);
	ATH_UNLOCK(sc);

	ath_tx_kick(sc);
}

/*
 * Process the TX status queue.
 */
static void
ath_edma_tx_processq(struct ath_softc *sc, int dosched)
{
	struct ath_hal *ah = sc->sc_ah;
	HAL_STATUS status;
	struct ath_tx_status ts;
	struct ath_txq *txq;
	struct ath_buf *bf;
	struct ieee80211_node *ni;
	int nacked = 0;
	int idx;
	int i;

#ifdef	ATH_DEBUG
	/* XXX */
	uint32_t txstatus[32];
#endif

	DPRINTF(sc, ATH_DEBUG_TX_PROC, "%s: called\n", __func__);

	for (idx = 0; ; idx++) {
		bzero(&ts, sizeof(ts));

		ATH_TXSTATUS_LOCK(sc);
#ifdef	ATH_DEBUG
		ath_hal_gettxrawtxdesc(ah, txstatus);
#endif
		status = ath_hal_txprocdesc(ah, NULL, (void *) &ts);
		ATH_TXSTATUS_UNLOCK(sc);

		if (status == HAL_EINPROGRESS) {
			DPRINTF(sc, ATH_DEBUG_TX_PROC,
			    "%s: (%d): EINPROGRESS\n",
			    __func__, idx);
			break;
		}

#ifdef	ATH_DEBUG
		if (sc->sc_debug & ATH_DEBUG_TX_PROC)
			if (ts.ts_queue_id != sc->sc_bhalq)
			ath_printtxstatbuf(sc, NULL, txstatus, ts.ts_queue_id,
			    idx, (status == HAL_OK));
#endif

		/*
		 * If there is an error with this descriptor, continue
		 * processing.
		 *
		 * XXX TBD: log some statistics?
		 */
		if (status == HAL_EIO) {
			device_printf(sc->sc_dev, "%s: invalid TX status?\n",
			    __func__);
			break;
		}

#if defined(ATH_DEBUG_ALQ) && defined(ATH_DEBUG)
		if (if_ath_alq_checkdebug(&sc->sc_alq, ATH_ALQ_EDMA_TXSTATUS)) {
			if_ath_alq_post(&sc->sc_alq, ATH_ALQ_EDMA_TXSTATUS,
			    sc->sc_tx_statuslen,
			    (char *) txstatus);
		}
#endif /* ATH_DEBUG_ALQ */

		/*
		 * At this point we have a valid status descriptor.
		 * The QID and descriptor ID (which currently isn't set)
		 * is part of the status.
		 *
		 * We then assume that the descriptor in question is the
		 * -head- of the given QID.  Eventually we should verify
		 * this by using the descriptor ID.
		 */

		/*
		 * The beacon queue is not currently a "real" queue.
		 * Frames aren't pushed onto it and the lock isn't setup.
		 * So skip it for now; the beacon handling code will
		 * free and alloc more beacon buffers as appropriate.
		 */
		if (ts.ts_queue_id == sc->sc_bhalq)
			continue;

		txq = &sc->sc_txq[ts.ts_queue_id];

		ATH_TXQ_LOCK(txq);
		bf = ATH_TXQ_FIRST(&txq->fifo);

		/*
		 * Work around the situation where I'm seeing notifications
		 * for Q1 when no frames are available.  That needs to be
		 * debugged but not by crashing _here_.
		 */
		if (bf == NULL) {
			device_printf(sc->sc_dev, "%s: Q%d: empty?\n",
			    __func__,
			    ts.ts_queue_id);
			ATH_TXQ_UNLOCK(txq);
			continue;
		}

		DPRINTF(sc, ATH_DEBUG_TX_PROC, "%s: Q%d, bf=%p, start=%d, end=%d\n",
		    __func__,
		    ts.ts_queue_id, bf,
		    !! (bf->bf_flags & ATH_BUF_FIFOPTR),
		    !! (bf->bf_flags & ATH_BUF_FIFOEND));

		/* XXX TODO: actually output debugging info about this */

#if 0
		/* XXX assert the buffer/descriptor matches the status descid */
		if (ts.ts_desc_id != bf->bf_descid) {
			device_printf(sc->sc_dev,
			    "%s: mismatched descid (qid=%d, tsdescid=%d, "
			    "bfdescid=%d\n",
			    __func__,
			    ts.ts_queue_id,
			    ts.ts_desc_id,
			    bf->bf_descid);
		}
#endif

		/* This removes the buffer and decrements the queue depth */
		ATH_TXQ_REMOVE(&txq->fifo, bf, bf_list);
		if (bf->bf_state.bfs_aggr)
			txq->axq_aggr_depth--;

		/*
		 * If this was the end of a FIFO set, decrement FIFO depth
		 */
		if (bf->bf_flags & ATH_BUF_FIFOEND)
			txq->axq_fifo_depth--;

		/*
		 * If this isn't the final buffer in a FIFO set, mark
		 * the buffer as busy so it goes onto the holding queue.
		 */
		if (! (bf->bf_flags & ATH_BUF_FIFOEND))
			bf->bf_flags |= ATH_BUF_BUSY;

		DPRINTF(sc, ATH_DEBUG_TX_PROC, "%s: Q%d: FIFO depth is now %d (%d)\n",
		    __func__,
		    txq->axq_qnum,
		    txq->axq_fifo_depth,
		    txq->fifo.axq_depth);

		/* XXX assert FIFO depth >= 0 */
		ATH_TXQ_UNLOCK(txq);

		/*
		 * Outside of the TX lock - if the buffer is end
		 * end buffer in this FIFO, we don't need a holding
		 * buffer any longer.
		 */
		if (bf->bf_flags & ATH_BUF_FIFOEND) {
			ATH_TXQ_LOCK(txq);
			ath_txq_freeholdingbuf(sc, txq);
			ATH_TXQ_UNLOCK(txq);
		}

		/*
		 * First we need to make sure ts_rate is valid.
		 *
		 * Pre-EDMA chips pass the whole TX descriptor to
		 * the proctxdesc function which will then fill out
		 * ts_rate based on the ts_finaltsi (final TX index)
		 * in the TX descriptor.  However the TX completion
		 * FIFO doesn't have this information.  So here we
		 * do a separate HAL call to populate that information.
		 *
		 * The same problem exists with ts_longretry.
		 * The FreeBSD HAL corrects ts_longretry in the HAL layer;
		 * the AR9380 HAL currently doesn't.  So until the HAL
		 * is imported and this can be added, we correct for it
		 * here.
		 */
		/* XXX TODO */
		/* XXX faked for now. Ew. */
		if (ts.ts_finaltsi < 4) {
			ts.ts_rate =
			    bf->bf_state.bfs_rc[ts.ts_finaltsi].ratecode;
			switch (ts.ts_finaltsi) {
			case 3: ts.ts_longretry +=
			    bf->bf_state.bfs_rc[2].tries;
			case 2: ts.ts_longretry +=
			    bf->bf_state.bfs_rc[1].tries;
			case 1: ts.ts_longretry +=
			    bf->bf_state.bfs_rc[0].tries;
			}
		} else {
			device_printf(sc->sc_dev, "%s: finaltsi=%d\n",
			    __func__,
			    ts.ts_finaltsi);
			ts.ts_rate = bf->bf_state.bfs_rc[0].ratecode;
		}

		/*
		 * XXX This is terrible.
		 *
		 * Right now, some code uses the TX status that is
		 * passed in here, but the completion handlers in the
		 * software TX path also use bf_status.ds_txstat.
		 * Ew.  That should all go away.
		 *
		 * XXX It's also possible the rate control completion
		 * routine is called twice.
		 */
		memcpy(&bf->bf_status, &ts, sizeof(ts));

		ni = bf->bf_node;

		/* Update RSSI */
		/* XXX duplicate from ath_tx_processq */
		if (ni != NULL && ts.ts_status == 0 &&
		    ((bf->bf_state.bfs_txflags & HAL_TXDESC_NOACK) == 0)) {
			nacked++;
			sc->sc_stats.ast_tx_rssi = ts.ts_rssi;
			ATH_RSSI_LPF(sc->sc_halstats.ns_avgtxrssi,
			    ts.ts_rssi);
		}

		/* Handle frame completion and rate control update */
		ath_tx_process_buf_completion(sc, txq, &ts, bf);

		/* NB: bf is invalid at this point */
	}

	sc->sc_wd_timer = 0;

	/*
	 * XXX It's inefficient to do this if the FIFO queue is full,
	 * but there's no easy way right now to only populate
	 * the txq task for _one_ TXQ.  This should be fixed.
	 */
	if (dosched) {
		/* Attempt to schedule more hardware frames to the TX FIFO */
		for (i = 0; i < HAL_NUM_TX_QUEUES; i++) {
			if (ATH_TXQ_SETUP(sc, i)) {
				ATH_TX_LOCK(sc);
				ath_txq_sched(sc, &sc->sc_txq[i]);
				ATH_TX_UNLOCK(sc);

				ATH_TXQ_LOCK(&sc->sc_txq[i]);
				ath_edma_tx_fifo_fill(sc, &sc->sc_txq[i]);
				ATH_TXQ_UNLOCK(&sc->sc_txq[i]);
			}
		}
		/* Kick software scheduler */
		ath_tx_swq_kick(sc);
	}

	DPRINTF(sc, ATH_DEBUG_TX_PROC, "%s: end\n", __func__);
}

static void
ath_edma_attach_comp_func(struct ath_softc *sc)
{

	TASK_INIT(&sc->sc_txtask, 0, ath_edma_tx_proc, sc);
}

void
ath_xmit_setup_edma(struct ath_softc *sc)
{

	/* Fetch EDMA field and buffer sizes */
	(void) ath_hal_gettxdesclen(sc->sc_ah, &sc->sc_tx_desclen);
	(void) ath_hal_gettxstatuslen(sc->sc_ah, &sc->sc_tx_statuslen);
	(void) ath_hal_getntxmaps(sc->sc_ah, &sc->sc_tx_nmaps);

	if (bootverbose) {
		device_printf(sc->sc_dev, "TX descriptor length: %d\n",
		    sc->sc_tx_desclen);
		device_printf(sc->sc_dev, "TX status length: %d\n",
		    sc->sc_tx_statuslen);
		device_printf(sc->sc_dev, "TX buffers per descriptor: %d\n",
		    sc->sc_tx_nmaps);
	}

	sc->sc_tx.xmit_setup = ath_edma_dma_txsetup;
	sc->sc_tx.xmit_teardown = ath_edma_dma_txteardown;
	sc->sc_tx.xmit_attach_comp_func = ath_edma_attach_comp_func;

	sc->sc_tx.xmit_dma_restart = ath_edma_dma_restart;
	sc->sc_tx.xmit_handoff = ath_edma_xmit_handoff;
	sc->sc_tx.xmit_drain = ath_edma_tx_drain;
}
