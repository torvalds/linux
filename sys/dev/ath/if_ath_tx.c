/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002-2009 Sam Leffler, Errno Consulting
 * Copyright (c) 2010-2012 Adrian Chadd, Xenion Pty Ltd
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
#include <sys/ktr.h>

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
#include <net80211/ieee80211_ht.h>

#include <net/bpf.h>

#ifdef INET
#include <netinet/in.h>
#include <netinet/if_ether.h>
#endif

#include <dev/ath/if_athvar.h>
#include <dev/ath/ath_hal/ah_devid.h>		/* XXX for softled */
#include <dev/ath/ath_hal/ah_diagcodes.h>

#include <dev/ath/if_ath_debug.h>

#ifdef ATH_TX99_DIAG
#include <dev/ath/ath_tx99/ath_tx99.h>
#endif

#include <dev/ath/if_ath_misc.h>
#include <dev/ath/if_ath_tx.h>
#include <dev/ath/if_ath_tx_ht.h>

#ifdef	ATH_DEBUG_ALQ
#include <dev/ath/if_ath_alq.h>
#endif

/*
 * How many retries to perform in software
 */
#define	SWMAX_RETRIES		10

/*
 * What queue to throw the non-QoS TID traffic into
 */
#define	ATH_NONQOS_TID_AC	WME_AC_VO

#if 0
static int ath_tx_node_is_asleep(struct ath_softc *sc, struct ath_node *an);
#endif
static int ath_tx_ampdu_pending(struct ath_softc *sc, struct ath_node *an,
    int tid);
static int ath_tx_ampdu_running(struct ath_softc *sc, struct ath_node *an,
    int tid);
static ieee80211_seq ath_tx_tid_seqno_assign(struct ath_softc *sc,
    struct ieee80211_node *ni, struct ath_buf *bf, struct mbuf *m0);
static int ath_tx_action_frame_override_queue(struct ath_softc *sc,
    struct ieee80211_node *ni, struct mbuf *m0, int *tid);
static struct ath_buf *
ath_tx_retry_clone(struct ath_softc *sc, struct ath_node *an,
    struct ath_tid *tid, struct ath_buf *bf);

#ifdef	ATH_DEBUG_ALQ
void
ath_tx_alq_post(struct ath_softc *sc, struct ath_buf *bf_first)
{
	struct ath_buf *bf;
	int i, n;
	const char *ds;

	/* XXX we should skip out early if debugging isn't enabled! */
	bf = bf_first;

	while (bf != NULL) {
		/* XXX should ensure bf_nseg > 0! */
		if (bf->bf_nseg == 0)
			break;
		n = ((bf->bf_nseg - 1) / sc->sc_tx_nmaps) + 1;
		for (i = 0, ds = (const char *) bf->bf_desc;
		    i < n;
		    i++, ds += sc->sc_tx_desclen) {
			if_ath_alq_post(&sc->sc_alq,
			    ATH_ALQ_EDMA_TXDESC,
			    sc->sc_tx_desclen,
			    ds);
		}
		bf = bf->bf_next;
	}
}
#endif /* ATH_DEBUG_ALQ */

/*
 * Whether to use the 11n rate scenario functions or not
 */
static inline int
ath_tx_is_11n(struct ath_softc *sc)
{
	return ((sc->sc_ah->ah_magic == 0x20065416) ||
		    (sc->sc_ah->ah_magic == 0x19741014));
}

/*
 * Obtain the current TID from the given frame.
 *
 * Non-QoS frames get mapped to a TID so frames consistently
 * go on a sensible queue.
 */
static int
ath_tx_gettid(struct ath_softc *sc, const struct mbuf *m0)
{
	const struct ieee80211_frame *wh;

	wh = mtod(m0, const struct ieee80211_frame *);

	/* Non-QoS: map frame to a TID queue for software queueing */
	if (! IEEE80211_QOS_HAS_SEQ(wh))
		return (WME_AC_TO_TID(M_WME_GETAC(m0)));

	/* QoS - fetch the TID from the header, ignore mbuf WME */
	return (ieee80211_gettid(wh));
}

static void
ath_tx_set_retry(struct ath_softc *sc, struct ath_buf *bf)
{
	struct ieee80211_frame *wh;

	wh = mtod(bf->bf_m, struct ieee80211_frame *);
	/* Only update/resync if needed */
	if (bf->bf_state.bfs_isretried == 0) {
		wh->i_fc[1] |= IEEE80211_FC1_RETRY;
		bus_dmamap_sync(sc->sc_dmat, bf->bf_dmamap,
		    BUS_DMASYNC_PREWRITE);
	}
	bf->bf_state.bfs_isretried = 1;
	bf->bf_state.bfs_retries ++;
}

/*
 * Determine what the correct AC queue for the given frame
 * should be.
 *
 * For QoS frames, obey the TID.  That way things like
 * management frames that are related to a given TID
 * are thus serialised with the rest of the TID traffic,
 * regardless of net80211 overriding priority.
 *
 * For non-QoS frames, return the mbuf WMI priority.
 *
 * This has implications that higher priority non-QoS traffic
 * may end up being scheduled before other non-QoS traffic,
 * leading to out-of-sequence packets being emitted.
 *
 * (It'd be nice to log/count this so we can see if it
 * really is a problem.)
 *
 * TODO: maybe we should throw multicast traffic, QoS or
 * otherwise, into a separate TX queue?
 */
static int
ath_tx_getac(struct ath_softc *sc, const struct mbuf *m0)
{
	const struct ieee80211_frame *wh;

	wh = mtod(m0, const struct ieee80211_frame *);

	/*
	 * QoS data frame (sequence number or otherwise) -
	 * return hardware queue mapping for the underlying
	 * TID.
	 */
	if (IEEE80211_QOS_HAS_SEQ(wh))
		return TID_TO_WME_AC(ieee80211_gettid(wh));

	/*
	 * Otherwise - return mbuf QoS pri.
	 */
	return (M_WME_GETAC(m0));
}

void
ath_txfrag_cleanup(struct ath_softc *sc,
	ath_bufhead *frags, struct ieee80211_node *ni)
{
	struct ath_buf *bf, *next;

	ATH_TXBUF_LOCK_ASSERT(sc);

	TAILQ_FOREACH_SAFE(bf, frags, bf_list, next) {
		/* NB: bf assumed clean */
		TAILQ_REMOVE(frags, bf, bf_list);
		ath_returnbuf_head(sc, bf);
		ieee80211_node_decref(ni);
	}
}

/*
 * Setup xmit of a fragmented frame.  Allocate a buffer
 * for each frag and bump the node reference count to
 * reflect the held reference to be setup by ath_tx_start.
 */
int
ath_txfrag_setup(struct ath_softc *sc, ath_bufhead *frags,
	struct mbuf *m0, struct ieee80211_node *ni)
{
	struct mbuf *m;
	struct ath_buf *bf;

	ATH_TXBUF_LOCK(sc);
	for (m = m0->m_nextpkt; m != NULL; m = m->m_nextpkt) {
		/* XXX non-management? */
		bf = _ath_getbuf_locked(sc, ATH_BUFTYPE_NORMAL);
		if (bf == NULL) {	/* out of buffers, cleanup */
			DPRINTF(sc, ATH_DEBUG_XMIT, "%s: no buffer?\n",
			    __func__);
			ath_txfrag_cleanup(sc, frags, ni);
			break;
		}
		ieee80211_node_incref(ni);
		TAILQ_INSERT_TAIL(frags, bf, bf_list);
	}
	ATH_TXBUF_UNLOCK(sc);

	return !TAILQ_EMPTY(frags);
}

static int
ath_tx_dmasetup(struct ath_softc *sc, struct ath_buf *bf, struct mbuf *m0)
{
	struct mbuf *m;
	int error;

	/*
	 * Load the DMA map so any coalescing is done.  This
	 * also calculates the number of descriptors we need.
	 */
	error = bus_dmamap_load_mbuf_sg(sc->sc_dmat, bf->bf_dmamap, m0,
				     bf->bf_segs, &bf->bf_nseg,
				     BUS_DMA_NOWAIT);
	if (error == EFBIG) {
		/* XXX packet requires too many descriptors */
		bf->bf_nseg = ATH_MAX_SCATTER + 1;
	} else if (error != 0) {
		sc->sc_stats.ast_tx_busdma++;
		ieee80211_free_mbuf(m0);
		return error;
	}
	/*
	 * Discard null packets and check for packets that
	 * require too many TX descriptors.  We try to convert
	 * the latter to a cluster.
	 */
	if (bf->bf_nseg > ATH_MAX_SCATTER) {		/* too many desc's, linearize */
		sc->sc_stats.ast_tx_linear++;
		m = m_collapse(m0, M_NOWAIT, ATH_MAX_SCATTER);
		if (m == NULL) {
			ieee80211_free_mbuf(m0);
			sc->sc_stats.ast_tx_nombuf++;
			return ENOMEM;
		}
		m0 = m;
		error = bus_dmamap_load_mbuf_sg(sc->sc_dmat, bf->bf_dmamap, m0,
					     bf->bf_segs, &bf->bf_nseg,
					     BUS_DMA_NOWAIT);
		if (error != 0) {
			sc->sc_stats.ast_tx_busdma++;
			ieee80211_free_mbuf(m0);
			return error;
		}
		KASSERT(bf->bf_nseg <= ATH_MAX_SCATTER,
		    ("too many segments after defrag; nseg %u", bf->bf_nseg));
	} else if (bf->bf_nseg == 0) {		/* null packet, discard */
		sc->sc_stats.ast_tx_nodata++;
		ieee80211_free_mbuf(m0);
		return EIO;
	}
	DPRINTF(sc, ATH_DEBUG_XMIT, "%s: m %p len %u\n",
		__func__, m0, m0->m_pkthdr.len);
	bus_dmamap_sync(sc->sc_dmat, bf->bf_dmamap, BUS_DMASYNC_PREWRITE);
	bf->bf_m = m0;

	return 0;
}

/*
 * Chain together segments+descriptors for a frame - 11n or otherwise.
 *
 * For aggregates, this is called on each frame in the aggregate.
 */
static void
ath_tx_chaindesclist(struct ath_softc *sc, struct ath_desc *ds0,
    struct ath_buf *bf, int is_aggr, int is_first_subframe,
    int is_last_subframe)
{
	struct ath_hal *ah = sc->sc_ah;
	char *ds;
	int i, bp, dsp;
	HAL_DMA_ADDR bufAddrList[4];
	uint32_t segLenList[4];
	int numTxMaps = 1;
	int isFirstDesc = 1;

	/*
	 * XXX There's txdma and txdma_mgmt; the descriptor
	 * sizes must match.
	 */
	struct ath_descdma *dd = &sc->sc_txdma;

	/*
	 * Fillin the remainder of the descriptor info.
	 */

	/*
	 * We need the number of TX data pointers in each descriptor.
	 * EDMA and later chips support 4 TX buffers per descriptor;
	 * previous chips just support one.
	 */
	numTxMaps = sc->sc_tx_nmaps;

	/*
	 * For EDMA and later chips ensure the TX map is fully populated
	 * before advancing to the next descriptor.
	 */
	ds = (char *) bf->bf_desc;
	bp = dsp = 0;
	bzero(bufAddrList, sizeof(bufAddrList));
	bzero(segLenList, sizeof(segLenList));
	for (i = 0; i < bf->bf_nseg; i++) {
		bufAddrList[bp] = bf->bf_segs[i].ds_addr;
		segLenList[bp] = bf->bf_segs[i].ds_len;
		bp++;

		/*
		 * Go to the next segment if this isn't the last segment
		 * and there's space in the current TX map.
		 */
		if ((i != bf->bf_nseg - 1) && (bp < numTxMaps))
			continue;

		/*
		 * Last segment or we're out of buffer pointers.
		 */
		bp = 0;

		if (i == bf->bf_nseg - 1)
			ath_hal_settxdesclink(ah, (struct ath_desc *) ds, 0);
		else
			ath_hal_settxdesclink(ah, (struct ath_desc *) ds,
			    bf->bf_daddr + dd->dd_descsize * (dsp + 1));

		/*
		 * XXX This assumes that bfs_txq is the actual destination
		 * hardware queue at this point.  It may not have been
		 * assigned, it may actually be pointing to the multicast
		 * software TXQ id.  These must be fixed!
		 */
		ath_hal_filltxdesc(ah, (struct ath_desc *) ds
			, bufAddrList
			, segLenList
			, bf->bf_descid		/* XXX desc id */
			, bf->bf_state.bfs_tx_queue
			, isFirstDesc		/* first segment */
			, i == bf->bf_nseg - 1	/* last segment */
			, (struct ath_desc *) ds0	/* first descriptor */
		);

		/*
		 * Make sure the 11n aggregate fields are cleared.
		 *
		 * XXX TODO: this doesn't need to be called for
		 * aggregate frames; as it'll be called on all
		 * sub-frames.  Since the descriptors are in
		 * non-cacheable memory, this leads to some
		 * rather slow writes on MIPS/ARM platforms.
		 */
		if (ath_tx_is_11n(sc))
			ath_hal_clr11n_aggr(sc->sc_ah, (struct ath_desc *) ds);

		/*
		 * If 11n is enabled, set it up as if it's an aggregate
		 * frame.
		 */
		if (is_last_subframe) {
			ath_hal_set11n_aggr_last(sc->sc_ah,
			    (struct ath_desc *) ds);
		} else if (is_aggr) {
			/*
			 * This clears the aggrlen field; so
			 * the caller needs to call set_aggr_first()!
			 *
			 * XXX TODO: don't call this for the first
			 * descriptor in the first frame in an
			 * aggregate!
			 */
			ath_hal_set11n_aggr_middle(sc->sc_ah,
			    (struct ath_desc *) ds,
			    bf->bf_state.bfs_ndelim);
		}
		isFirstDesc = 0;
		bf->bf_lastds = (struct ath_desc *) ds;

		/*
		 * Don't forget to skip to the next descriptor.
		 */
		ds += sc->sc_tx_desclen;
		dsp++;

		/*
		 * .. and don't forget to blank these out!
		 */
		bzero(bufAddrList, sizeof(bufAddrList));
		bzero(segLenList, sizeof(segLenList));
	}
	bus_dmamap_sync(sc->sc_dmat, bf->bf_dmamap, BUS_DMASYNC_PREWRITE);
}

/*
 * Set the rate control fields in the given descriptor based on
 * the bf_state fields and node state.
 *
 * The bfs fields should already be set with the relevant rate
 * control information, including whether MRR is to be enabled.
 *
 * Since the FreeBSD HAL currently sets up the first TX rate
 * in ath_hal_setuptxdesc(), this will setup the MRR
 * conditionally for the pre-11n chips, and call ath_buf_set_rate
 * unconditionally for 11n chips. These require the 11n rate
 * scenario to be set if MCS rates are enabled, so it's easier
 * to just always call it. The caller can then only set rates 2, 3
 * and 4 if multi-rate retry is needed.
 */
static void
ath_tx_set_ratectrl(struct ath_softc *sc, struct ieee80211_node *ni,
    struct ath_buf *bf)
{
	struct ath_rc_series *rc = bf->bf_state.bfs_rc;

	/* If mrr is disabled, blank tries 1, 2, 3 */
	if (! bf->bf_state.bfs_ismrr)
		rc[1].tries = rc[2].tries = rc[3].tries = 0;

#if 0
	/*
	 * If NOACK is set, just set ntries=1.
	 */
	else if (bf->bf_state.bfs_txflags & HAL_TXDESC_NOACK) {
		rc[1].tries = rc[2].tries = rc[3].tries = 0;
		rc[0].tries = 1;
	}
#endif

	/*
	 * Always call - that way a retried descriptor will
	 * have the MRR fields overwritten.
	 *
	 * XXX TODO: see if this is really needed - setting up
	 * the first descriptor should set the MRR fields to 0
	 * for us anyway.
	 */
	if (ath_tx_is_11n(sc)) {
		ath_buf_set_rate(sc, ni, bf);
	} else {
		ath_hal_setupxtxdesc(sc->sc_ah, bf->bf_desc
			, rc[1].ratecode, rc[1].tries
			, rc[2].ratecode, rc[2].tries
			, rc[3].ratecode, rc[3].tries
		);
	}
}

/*
 * Setup segments+descriptors for an 11n aggregate.
 * bf_first is the first buffer in the aggregate.
 * The descriptor list must already been linked together using
 * bf->bf_next.
 */
static void
ath_tx_setds_11n(struct ath_softc *sc, struct ath_buf *bf_first)
{
	struct ath_buf *bf, *bf_prev = NULL;
	struct ath_desc *ds0 = bf_first->bf_desc;

	DPRINTF(sc, ATH_DEBUG_SW_TX_AGGR, "%s: nframes=%d, al=%d\n",
	    __func__, bf_first->bf_state.bfs_nframes,
	    bf_first->bf_state.bfs_al);

	bf = bf_first;

	if (bf->bf_state.bfs_txrate0 == 0)
		DPRINTF(sc, ATH_DEBUG_SW_TX_AGGR, "%s: bf=%p, txrate0=%d\n",
		    __func__, bf, 0);
	if (bf->bf_state.bfs_rc[0].ratecode == 0)
		DPRINTF(sc, ATH_DEBUG_SW_TX_AGGR, "%s: bf=%p, rix0=%d\n",
		    __func__, bf, 0);

	/*
	 * Setup all descriptors of all subframes - this will
	 * call ath_hal_set11naggrmiddle() on every frame.
	 */
	while (bf != NULL) {
		DPRINTF(sc, ATH_DEBUG_SW_TX_AGGR,
		    "%s: bf=%p, nseg=%d, pktlen=%d, seqno=%d\n",
		    __func__, bf, bf->bf_nseg, bf->bf_state.bfs_pktlen,
		    SEQNO(bf->bf_state.bfs_seqno));

		/*
		 * Setup the initial fields for the first descriptor - all
		 * the non-11n specific stuff.
		 */
		ath_hal_setuptxdesc(sc->sc_ah, bf->bf_desc
			, bf->bf_state.bfs_pktlen	/* packet length */
			, bf->bf_state.bfs_hdrlen	/* header length */
			, bf->bf_state.bfs_atype	/* Atheros packet type */
			, bf->bf_state.bfs_txpower	/* txpower */
			, bf->bf_state.bfs_txrate0
			, bf->bf_state.bfs_try0		/* series 0 rate/tries */
			, bf->bf_state.bfs_keyix	/* key cache index */
			, bf->bf_state.bfs_txantenna	/* antenna mode */
			, bf->bf_state.bfs_txflags | HAL_TXDESC_INTREQ	/* flags */
			, bf->bf_state.bfs_ctsrate	/* rts/cts rate */
			, bf->bf_state.bfs_ctsduration	/* rts/cts duration */
		);

		/*
		 * First descriptor? Setup the rate control and initial
		 * aggregate header information.
		 */
		if (bf == bf_first) {
			/*
			 * setup first desc with rate and aggr info
			 */
			ath_tx_set_ratectrl(sc, bf->bf_node, bf);
		}

		/*
		 * Setup the descriptors for a multi-descriptor frame.
		 * This is both aggregate and non-aggregate aware.
		 */
		ath_tx_chaindesclist(sc, ds0, bf,
		    1, /* is_aggr */
		    !! (bf == bf_first), /* is_first_subframe */
		    !! (bf->bf_next == NULL) /* is_last_subframe */
		    );

		if (bf == bf_first) {
			/*
			 * Initialise the first 11n aggregate with the
			 * aggregate length and aggregate enable bits.
			 */
			ath_hal_set11n_aggr_first(sc->sc_ah,
			    ds0,
			    bf->bf_state.bfs_al,
			    bf->bf_state.bfs_ndelim);
		}

		/*
		 * Link the last descriptor of the previous frame
		 * to the beginning descriptor of this frame.
		 */
		if (bf_prev != NULL)
			ath_hal_settxdesclink(sc->sc_ah, bf_prev->bf_lastds,
			    bf->bf_daddr);

		/* Save a copy so we can link the next descriptor in */
		bf_prev = bf;
		bf = bf->bf_next;
	}

	/*
	 * Set the first descriptor bf_lastds field to point to
	 * the last descriptor in the last subframe, that's where
	 * the status update will occur.
	 */
	bf_first->bf_lastds = bf_prev->bf_lastds;

	/*
	 * And bf_last in the first descriptor points to the end of
	 * the aggregate list.
	 */
	bf_first->bf_last = bf_prev;

	/*
	 * For non-AR9300 NICs, which require the rate control
	 * in the final descriptor - let's set that up now.
	 *
	 * This is because the filltxdesc() HAL call doesn't
	 * populate the last segment with rate control information
	 * if firstSeg is also true.  For non-aggregate frames
	 * that is fine, as the first frame already has rate control
	 * info.  But if the last frame in an aggregate has one
	 * descriptor, both firstseg and lastseg will be true and
	 * the rate info isn't copied.
	 *
	 * This is inefficient on MIPS/ARM platforms that have
	 * non-cachable memory for TX descriptors, but we'll just
	 * make do for now.
	 *
	 * As to why the rate table is stashed in the last descriptor
	 * rather than the first descriptor?  Because proctxdesc()
	 * is called on the final descriptor in an MPDU or A-MPDU -
	 * ie, the one that gets updated by the hardware upon
	 * completion.  That way proctxdesc() doesn't need to know
	 * about the first _and_ last TX descriptor.
	 */
	ath_hal_setuplasttxdesc(sc->sc_ah, bf_prev->bf_lastds, ds0);

	DPRINTF(sc, ATH_DEBUG_SW_TX_AGGR, "%s: end\n", __func__);
}

/*
 * Hand-off a frame to the multicast TX queue.
 *
 * This is a software TXQ which will be appended to the CAB queue
 * during the beacon setup code.
 *
 * XXX TODO: since the AR9300 EDMA TX queue support wants the QCU ID
 * as part of the TX descriptor, bf_state.bfs_tx_queue must be updated
 * with the actual hardware txq, or all of this will fall apart.
 *
 * XXX It may not be a bad idea to just stuff the QCU ID into bf_state
 * and retire bfs_tx_queue; then make sure the CABQ QCU ID is populated
 * correctly.
 */
static void
ath_tx_handoff_mcast(struct ath_softc *sc, struct ath_txq *txq,
    struct ath_buf *bf)
{
	ATH_TX_LOCK_ASSERT(sc);

	KASSERT((bf->bf_flags & ATH_BUF_BUSY) == 0,
	     ("%s: busy status 0x%x", __func__, bf->bf_flags));

	/*
	 * Ensure that the tx queue is the cabq, so things get
	 * mapped correctly.
	 */
	if (bf->bf_state.bfs_tx_queue != sc->sc_cabq->axq_qnum) {
		DPRINTF(sc, ATH_DEBUG_XMIT,
		    "%s: bf=%p, bfs_tx_queue=%d, axq_qnum=%d\n",
		    __func__, bf, bf->bf_state.bfs_tx_queue,
		    txq->axq_qnum);
	}

	ATH_TXQ_LOCK(txq);
	if (ATH_TXQ_LAST(txq, axq_q_s) != NULL) {
		struct ath_buf *bf_last = ATH_TXQ_LAST(txq, axq_q_s);
		struct ieee80211_frame *wh;

		/* mark previous frame */
		wh = mtod(bf_last->bf_m, struct ieee80211_frame *);
		wh->i_fc[1] |= IEEE80211_FC1_MORE_DATA;
		bus_dmamap_sync(sc->sc_dmat, bf_last->bf_dmamap,
		    BUS_DMASYNC_PREWRITE);

		/* link descriptor */
		ath_hal_settxdesclink(sc->sc_ah,
		    bf_last->bf_lastds,
		    bf->bf_daddr);
	}
	ATH_TXQ_INSERT_TAIL(txq, bf, bf_list);
	ATH_TXQ_UNLOCK(txq);
}

/*
 * Hand-off packet to a hardware queue.
 */
static void
ath_tx_handoff_hw(struct ath_softc *sc, struct ath_txq *txq,
    struct ath_buf *bf)
{
	struct ath_hal *ah = sc->sc_ah;
	struct ath_buf *bf_first;

	/*
	 * Insert the frame on the outbound list and pass it on
	 * to the hardware.  Multicast frames buffered for power
	 * save stations and transmit from the CAB queue are stored
	 * on a s/w only queue and loaded on to the CAB queue in
	 * the SWBA handler since frames only go out on DTIM and
	 * to avoid possible races.
	 */
	ATH_TX_LOCK_ASSERT(sc);
	KASSERT((bf->bf_flags & ATH_BUF_BUSY) == 0,
	     ("%s: busy status 0x%x", __func__, bf->bf_flags));
	KASSERT(txq->axq_qnum != ATH_TXQ_SWQ,
	     ("ath_tx_handoff_hw called for mcast queue"));

	/*
	 * XXX We should instead just verify that sc_txstart_cnt
	 * or ath_txproc_cnt > 0.  That would mean that
	 * the reset is going to be waiting for us to complete.
	 */
	if (sc->sc_txproc_cnt == 0 && sc->sc_txstart_cnt == 0) {
		device_printf(sc->sc_dev,
		    "%s: TX dispatch without holding txcount/txstart refcnt!\n",
		    __func__);
	}

	/*
	 * XXX .. this is going to cause the hardware to get upset;
	 * so we really should find some way to drop or queue
	 * things.
	 */

	ATH_TXQ_LOCK(txq);

	/*
	 * XXX TODO: if there's a holdingbf, then
	 * ATH_TXQ_PUTRUNNING should be clear.
	 *
	 * If there is a holdingbf and the list is empty,
	 * then axq_link should be pointing to the holdingbf.
	 *
	 * Otherwise it should point to the last descriptor
	 * in the last ath_buf.
	 *
	 * In any case, we should really ensure that we
	 * update the previous descriptor link pointer to
	 * this descriptor, regardless of all of the above state.
	 *
	 * For now this is captured by having axq_link point
	 * to either the holdingbf (if the TXQ list is empty)
	 * or the end of the list (if the TXQ list isn't empty.)
	 * I'd rather just kill axq_link here and do it as above.
	 */

	/*
	 * Append the frame to the TX queue.
	 */
	ATH_TXQ_INSERT_TAIL(txq, bf, bf_list);
	ATH_KTR(sc, ATH_KTR_TX, 3,
	    "ath_tx_handoff: non-tdma: txq=%u, add bf=%p "
	    "depth=%d",
	    txq->axq_qnum,
	    bf,
	    txq->axq_depth);

	/*
	 * If there's a link pointer, update it.
	 *
	 * XXX we should replace this with the above logic, just
	 * to kill axq_link with fire.
	 */
	if (txq->axq_link != NULL) {
		*txq->axq_link = bf->bf_daddr;
		DPRINTF(sc, ATH_DEBUG_XMIT,
		    "%s: link[%u](%p)=%p (%p) depth %d\n", __func__,
		    txq->axq_qnum, txq->axq_link,
		    (caddr_t)bf->bf_daddr, bf->bf_desc,
		    txq->axq_depth);
		ATH_KTR(sc, ATH_KTR_TX, 5,
		    "ath_tx_handoff: non-tdma: link[%u](%p)=%p (%p) "
		    "lastds=%d",
		    txq->axq_qnum, txq->axq_link,
		    (caddr_t)bf->bf_daddr, bf->bf_desc,
		    bf->bf_lastds);
	}

	/*
	 * If we've not pushed anything into the hardware yet,
	 * push the head of the queue into the TxDP.
	 *
	 * Once we've started DMA, there's no guarantee that
	 * updating the TxDP with a new value will actually work.
	 * So we just don't do that - if we hit the end of the list,
	 * we keep that buffer around (the "holding buffer") and
	 * re-start DMA by updating the link pointer of _that_
	 * descriptor and then restart DMA.
	 */
	if (! (txq->axq_flags & ATH_TXQ_PUTRUNNING)) {
		bf_first = TAILQ_FIRST(&txq->axq_q);
		txq->axq_flags |= ATH_TXQ_PUTRUNNING;
		ath_hal_puttxbuf(ah, txq->axq_qnum, bf_first->bf_daddr);
		DPRINTF(sc, ATH_DEBUG_XMIT,
		    "%s: TXDP[%u] = %p (%p) depth %d\n",
		    __func__, txq->axq_qnum,
		    (caddr_t)bf_first->bf_daddr, bf_first->bf_desc,
		    txq->axq_depth);
		ATH_KTR(sc, ATH_KTR_TX, 5,
		    "ath_tx_handoff: TXDP[%u] = %p (%p) "
		    "lastds=%p depth %d",
		    txq->axq_qnum,
		    (caddr_t)bf_first->bf_daddr, bf_first->bf_desc,
		    bf_first->bf_lastds,
		    txq->axq_depth);
	}

	/*
	 * Ensure that the bf TXQ matches this TXQ, so later
	 * checking and holding buffer manipulation is sane.
	 */
	if (bf->bf_state.bfs_tx_queue != txq->axq_qnum) {
		DPRINTF(sc, ATH_DEBUG_XMIT,
		    "%s: bf=%p, bfs_tx_queue=%d, axq_qnum=%d\n",
		    __func__, bf, bf->bf_state.bfs_tx_queue,
		    txq->axq_qnum);
	}

	/*
	 * Track aggregate queue depth.
	 */
	if (bf->bf_state.bfs_aggr)
		txq->axq_aggr_depth++;

	/*
	 * Update the link pointer.
	 */
	ath_hal_gettxdesclinkptr(ah, bf->bf_lastds, &txq->axq_link);

	/*
	 * Start DMA.
	 *
	 * If we wrote a TxDP above, DMA will start from here.
	 *
	 * If DMA is running, it'll do nothing.
	 *
	 * If the DMA engine hit the end of the QCU list (ie LINK=NULL,
	 * or VEOL) then it stops at the last transmitted write.
	 * We then append a new frame by updating the link pointer
	 * in that descriptor and then kick TxE here; it will re-read
	 * that last descriptor and find the new descriptor to transmit.
	 *
	 * This is why we keep the holding descriptor around.
	 */
	ath_hal_txstart(ah, txq->axq_qnum);
	ATH_TXQ_UNLOCK(txq);
	ATH_KTR(sc, ATH_KTR_TX, 1,
	    "ath_tx_handoff: txq=%u, txstart", txq->axq_qnum);
}

/*
 * Restart TX DMA for the given TXQ.
 *
 * This must be called whether the queue is empty or not.
 */
static void
ath_legacy_tx_dma_restart(struct ath_softc *sc, struct ath_txq *txq)
{
	struct ath_buf *bf, *bf_last;

	ATH_TXQ_LOCK_ASSERT(txq);

	/* XXX make this ATH_TXQ_FIRST */
	bf = TAILQ_FIRST(&txq->axq_q);
	bf_last = ATH_TXQ_LAST(txq, axq_q_s);

	if (bf == NULL)
		return;

	DPRINTF(sc, ATH_DEBUG_RESET,
	    "%s: Q%d: bf=%p, bf_last=%p, daddr=0x%08x\n",
	    __func__,
	    txq->axq_qnum,
	    bf,
	    bf_last,
	    (uint32_t) bf->bf_daddr);

#ifdef	ATH_DEBUG
	if (sc->sc_debug & ATH_DEBUG_RESET)
		ath_tx_dump(sc, txq);
#endif

	/*
	 * This is called from a restart, so DMA is known to be
	 * completely stopped.
	 */
	KASSERT((!(txq->axq_flags & ATH_TXQ_PUTRUNNING)),
	    ("%s: Q%d: called with PUTRUNNING=1\n",
	    __func__,
	    txq->axq_qnum));

	ath_hal_puttxbuf(sc->sc_ah, txq->axq_qnum, bf->bf_daddr);
	txq->axq_flags |= ATH_TXQ_PUTRUNNING;

	ath_hal_gettxdesclinkptr(sc->sc_ah, bf_last->bf_lastds,
	    &txq->axq_link);
	ath_hal_txstart(sc->sc_ah, txq->axq_qnum);
}

/*
 * Hand off a packet to the hardware (or mcast queue.)
 *
 * The relevant hardware txq should be locked.
 */
static void
ath_legacy_xmit_handoff(struct ath_softc *sc, struct ath_txq *txq,
    struct ath_buf *bf)
{
	ATH_TX_LOCK_ASSERT(sc);

#ifdef	ATH_DEBUG_ALQ
	if (if_ath_alq_checkdebug(&sc->sc_alq, ATH_ALQ_EDMA_TXDESC))
		ath_tx_alq_post(sc, bf);
#endif

	if (txq->axq_qnum == ATH_TXQ_SWQ)
		ath_tx_handoff_mcast(sc, txq, bf);
	else
		ath_tx_handoff_hw(sc, txq, bf);
}

static int
ath_tx_tag_crypto(struct ath_softc *sc, struct ieee80211_node *ni,
    struct mbuf *m0, int iswep, int isfrag, int *hdrlen, int *pktlen,
    int *keyix)
{
	DPRINTF(sc, ATH_DEBUG_XMIT,
	    "%s: hdrlen=%d, pktlen=%d, isfrag=%d, iswep=%d, m0=%p\n",
	    __func__,
	    *hdrlen,
	    *pktlen,
	    isfrag,
	    iswep,
	    m0);

	if (iswep) {
		const struct ieee80211_cipher *cip;
		struct ieee80211_key *k;

		/*
		 * Construct the 802.11 header+trailer for an encrypted
		 * frame. The only reason this can fail is because of an
		 * unknown or unsupported cipher/key type.
		 */
		k = ieee80211_crypto_encap(ni, m0);
		if (k == NULL) {
			/*
			 * This can happen when the key is yanked after the
			 * frame was queued.  Just discard the frame; the
			 * 802.11 layer counts failures and provides
			 * debugging/diagnostics.
			 */
			return (0);
		}
		/*
		 * Adjust the packet + header lengths for the crypto
		 * additions and calculate the h/w key index.  When
		 * a s/w mic is done the frame will have had any mic
		 * added to it prior to entry so m0->m_pkthdr.len will
		 * account for it. Otherwise we need to add it to the
		 * packet length.
		 */
		cip = k->wk_cipher;
		(*hdrlen) += cip->ic_header;
		(*pktlen) += cip->ic_header + cip->ic_trailer;
		/* NB: frags always have any TKIP MIC done in s/w */
		if ((k->wk_flags & IEEE80211_KEY_SWMIC) == 0 && !isfrag)
			(*pktlen) += cip->ic_miclen;
		(*keyix) = k->wk_keyix;
	} else if (ni->ni_ucastkey.wk_cipher == &ieee80211_cipher_none) {
		/*
		 * Use station key cache slot, if assigned.
		 */
		(*keyix) = ni->ni_ucastkey.wk_keyix;
		if ((*keyix) == IEEE80211_KEYIX_NONE)
			(*keyix) = HAL_TXKEYIX_INVALID;
	} else
		(*keyix) = HAL_TXKEYIX_INVALID;

	return (1);
}

/*
 * Calculate whether interoperability protection is required for
 * this frame.
 *
 * This requires the rate control information be filled in,
 * as the protection requirement depends upon the current
 * operating mode / PHY.
 */
static void
ath_tx_calc_protection(struct ath_softc *sc, struct ath_buf *bf)
{
	struct ieee80211_frame *wh;
	uint8_t rix;
	uint16_t flags;
	int shortPreamble;
	const HAL_RATE_TABLE *rt = sc->sc_currates;
	struct ieee80211com *ic = &sc->sc_ic;

	flags = bf->bf_state.bfs_txflags;
	rix = bf->bf_state.bfs_rc[0].rix;
	shortPreamble = bf->bf_state.bfs_shpream;
	wh = mtod(bf->bf_m, struct ieee80211_frame *);

	/* Disable frame protection for TOA probe frames */
	if (bf->bf_flags & ATH_BUF_TOA_PROBE) {
		/* XXX count */
		flags &= ~(HAL_TXDESC_CTSENA | HAL_TXDESC_RTSENA);
		bf->bf_state.bfs_doprot = 0;
		goto finish;
	}

	/*
	 * If 802.11g protection is enabled, determine whether
	 * to use RTS/CTS or just CTS.  Note that this is only
	 * done for OFDM unicast frames.
	 */
	if ((ic->ic_flags & IEEE80211_F_USEPROT) &&
	    rt->info[rix].phy == IEEE80211_T_OFDM &&
	    (flags & HAL_TXDESC_NOACK) == 0) {
		bf->bf_state.bfs_doprot = 1;
		/* XXX fragments must use CCK rates w/ protection */
		if (ic->ic_protmode == IEEE80211_PROT_RTSCTS) {
			flags |= HAL_TXDESC_RTSENA;
		} else if (ic->ic_protmode == IEEE80211_PROT_CTSONLY) {
			flags |= HAL_TXDESC_CTSENA;
		}
		/*
		 * For frags it would be desirable to use the
		 * highest CCK rate for RTS/CTS.  But stations
		 * farther away may detect it at a lower CCK rate
		 * so use the configured protection rate instead
		 * (for now).
		 */
		sc->sc_stats.ast_tx_protect++;
	}

	/*
	 * If 11n protection is enabled and it's a HT frame,
	 * enable RTS.
	 *
	 * XXX ic_htprotmode or ic_curhtprotmode?
	 * XXX should it_htprotmode only matter if ic_curhtprotmode 
	 * XXX indicates it's not a HT pure environment?
	 */
	if ((ic->ic_htprotmode == IEEE80211_PROT_RTSCTS) &&
	    rt->info[rix].phy == IEEE80211_T_HT &&
	    (flags & HAL_TXDESC_NOACK) == 0) {
		flags |= HAL_TXDESC_RTSENA;
		sc->sc_stats.ast_tx_htprotect++;
	}

finish:
	bf->bf_state.bfs_txflags = flags;
}

/*
 * Update the frame duration given the currently selected rate.
 *
 * This also updates the frame duration value, so it will require
 * a DMA flush.
 */
static void
ath_tx_calc_duration(struct ath_softc *sc, struct ath_buf *bf)
{
	struct ieee80211_frame *wh;
	uint8_t rix;
	uint16_t flags;
	int shortPreamble;
	struct ath_hal *ah = sc->sc_ah;
	const HAL_RATE_TABLE *rt = sc->sc_currates;
	int isfrag = bf->bf_m->m_flags & M_FRAG;

	flags = bf->bf_state.bfs_txflags;
	rix = bf->bf_state.bfs_rc[0].rix;
	shortPreamble = bf->bf_state.bfs_shpream;
	wh = mtod(bf->bf_m, struct ieee80211_frame *);

	/*
	 * Calculate duration.  This logically belongs in the 802.11
	 * layer but it lacks sufficient information to calculate it.
	 */
	if ((flags & HAL_TXDESC_NOACK) == 0 &&
	    (wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) != IEEE80211_FC0_TYPE_CTL) {
		u_int16_t dur;
		if (shortPreamble)
			dur = rt->info[rix].spAckDuration;
		else
			dur = rt->info[rix].lpAckDuration;
		if (wh->i_fc[1] & IEEE80211_FC1_MORE_FRAG) {
			dur += dur;		/* additional SIFS+ACK */
			/*
			 * Include the size of next fragment so NAV is
			 * updated properly.  The last fragment uses only
			 * the ACK duration
			 *
			 * XXX TODO: ensure that the rate lookup for each
			 * fragment is the same as the rate used by the
			 * first fragment!
			 */
			dur += ath_hal_computetxtime(ah,
			    rt,
			    bf->bf_nextfraglen,
			    rix, shortPreamble,
			    AH_TRUE);
		}
		if (isfrag) {
			/*
			 * Force hardware to use computed duration for next
			 * fragment by disabling multi-rate retry which updates
			 * duration based on the multi-rate duration table.
			 */
			bf->bf_state.bfs_ismrr = 0;
			bf->bf_state.bfs_try0 = ATH_TXMGTTRY;
			/* XXX update bfs_rc[0].try? */
		}

		/* Update the duration field itself */
		*(u_int16_t *)wh->i_dur = htole16(dur);
	}
}

static uint8_t
ath_tx_get_rtscts_rate(struct ath_hal *ah, const HAL_RATE_TABLE *rt,
    int cix, int shortPreamble)
{
	uint8_t ctsrate;

	/*
	 * CTS transmit rate is derived from the transmit rate
	 * by looking in the h/w rate table.  We must also factor
	 * in whether or not a short preamble is to be used.
	 */
	/* NB: cix is set above where RTS/CTS is enabled */
	KASSERT(cix != 0xff, ("cix not setup"));
	ctsrate = rt->info[cix].rateCode;

	/* XXX this should only matter for legacy rates */
	if (shortPreamble)
		ctsrate |= rt->info[cix].shortPreamble;

	return (ctsrate);
}

/*
 * Calculate the RTS/CTS duration for legacy frames.
 */
static int
ath_tx_calc_ctsduration(struct ath_hal *ah, int rix, int cix,
    int shortPreamble, int pktlen, const HAL_RATE_TABLE *rt,
    int flags)
{
	int ctsduration = 0;

	/* This mustn't be called for HT modes */
	if (rt->info[cix].phy == IEEE80211_T_HT) {
		printf("%s: HT rate where it shouldn't be (0x%x)\n",
		    __func__, rt->info[cix].rateCode);
		return (-1);
	}

	/*
	 * Compute the transmit duration based on the frame
	 * size and the size of an ACK frame.  We call into the
	 * HAL to do the computation since it depends on the
	 * characteristics of the actual PHY being used.
	 *
	 * NB: CTS is assumed the same size as an ACK so we can
	 *     use the precalculated ACK durations.
	 */
	if (shortPreamble) {
		if (flags & HAL_TXDESC_RTSENA)		/* SIFS + CTS */
			ctsduration += rt->info[cix].spAckDuration;
		ctsduration += ath_hal_computetxtime(ah,
			rt, pktlen, rix, AH_TRUE, AH_TRUE);
		if ((flags & HAL_TXDESC_NOACK) == 0)	/* SIFS + ACK */
			ctsduration += rt->info[rix].spAckDuration;
	} else {
		if (flags & HAL_TXDESC_RTSENA)		/* SIFS + CTS */
			ctsduration += rt->info[cix].lpAckDuration;
		ctsduration += ath_hal_computetxtime(ah,
			rt, pktlen, rix, AH_FALSE, AH_TRUE);
		if ((flags & HAL_TXDESC_NOACK) == 0)	/* SIFS + ACK */
			ctsduration += rt->info[rix].lpAckDuration;
	}

	return (ctsduration);
}

/*
 * Update the given ath_buf with updated rts/cts setup and duration
 * values.
 *
 * To support rate lookups for each software retry, the rts/cts rate
 * and cts duration must be re-calculated.
 *
 * This function assumes the RTS/CTS flags have been set as needed;
 * mrr has been disabled; and the rate control lookup has been done.
 *
 * XXX TODO: MRR need only be disabled for the pre-11n NICs.
 * XXX The 11n NICs support per-rate RTS/CTS configuration.
 */
static void
ath_tx_set_rtscts(struct ath_softc *sc, struct ath_buf *bf)
{
	uint16_t ctsduration = 0;
	uint8_t ctsrate = 0;
	uint8_t rix = bf->bf_state.bfs_rc[0].rix;
	uint8_t cix = 0;
	const HAL_RATE_TABLE *rt = sc->sc_currates;

	/*
	 * No RTS/CTS enabled? Don't bother.
	 */
	if ((bf->bf_state.bfs_txflags &
	    (HAL_TXDESC_RTSENA | HAL_TXDESC_CTSENA)) == 0) {
		/* XXX is this really needed? */
		bf->bf_state.bfs_ctsrate = 0;
		bf->bf_state.bfs_ctsduration = 0;
		return;
	}

	/*
	 * If protection is enabled, use the protection rix control
	 * rate. Otherwise use the rate0 control rate.
	 */
	if (bf->bf_state.bfs_doprot)
		rix = sc->sc_protrix;
	else
		rix = bf->bf_state.bfs_rc[0].rix;

	/*
	 * If the raw path has hard-coded ctsrate0 to something,
	 * use it.
	 */
	if (bf->bf_state.bfs_ctsrate0 != 0)
		cix = ath_tx_findrix(sc, bf->bf_state.bfs_ctsrate0);
	else
		/* Control rate from above */
		cix = rt->info[rix].controlRate;

	/* Calculate the rtscts rate for the given cix */
	ctsrate = ath_tx_get_rtscts_rate(sc->sc_ah, rt, cix,
	    bf->bf_state.bfs_shpream);

	/* The 11n chipsets do ctsduration calculations for you */
	if (! ath_tx_is_11n(sc))
		ctsduration = ath_tx_calc_ctsduration(sc->sc_ah, rix, cix,
		    bf->bf_state.bfs_shpream, bf->bf_state.bfs_pktlen,
		    rt, bf->bf_state.bfs_txflags);

	/* Squirrel away in ath_buf */
	bf->bf_state.bfs_ctsrate = ctsrate;
	bf->bf_state.bfs_ctsduration = ctsduration;
	
	/*
	 * Must disable multi-rate retry when using RTS/CTS.
	 */
	if (!sc->sc_mrrprot) {
		bf->bf_state.bfs_ismrr = 0;
		bf->bf_state.bfs_try0 =
		    bf->bf_state.bfs_rc[0].tries = ATH_TXMGTTRY; /* XXX ew */
	}
}

/*
 * Setup the descriptor chain for a normal or fast-frame
 * frame.
 *
 * XXX TODO: extend to include the destination hardware QCU ID.
 * Make sure that is correct.  Make sure that when being added
 * to the mcastq, the CABQ QCUID is set or things will get a bit
 * odd.
 */
static void
ath_tx_setds(struct ath_softc *sc, struct ath_buf *bf)
{
	struct ath_desc *ds = bf->bf_desc;
	struct ath_hal *ah = sc->sc_ah;

	if (bf->bf_state.bfs_txrate0 == 0)
		DPRINTF(sc, ATH_DEBUG_XMIT, 
		    "%s: bf=%p, txrate0=%d\n", __func__, bf, 0);

	ath_hal_setuptxdesc(ah, ds
		, bf->bf_state.bfs_pktlen	/* packet length */
		, bf->bf_state.bfs_hdrlen	/* header length */
		, bf->bf_state.bfs_atype	/* Atheros packet type */
		, bf->bf_state.bfs_txpower	/* txpower */
		, bf->bf_state.bfs_txrate0
		, bf->bf_state.bfs_try0		/* series 0 rate/tries */
		, bf->bf_state.bfs_keyix	/* key cache index */
		, bf->bf_state.bfs_txantenna	/* antenna mode */
		, bf->bf_state.bfs_txflags	/* flags */
		, bf->bf_state.bfs_ctsrate	/* rts/cts rate */
		, bf->bf_state.bfs_ctsduration	/* rts/cts duration */
	);

	/*
	 * This will be overriden when the descriptor chain is written.
	 */
	bf->bf_lastds = ds;
	bf->bf_last = bf;

	/* Set rate control and descriptor chain for this frame */
	ath_tx_set_ratectrl(sc, bf->bf_node, bf);
	ath_tx_chaindesclist(sc, ds, bf, 0, 0, 0);
}

/*
 * Do a rate lookup.
 *
 * This performs a rate lookup for the given ath_buf only if it's required.
 * Non-data frames and raw frames don't require it.
 *
 * This populates the primary and MRR entries; MRR values are
 * then disabled later on if something requires it (eg RTS/CTS on
 * pre-11n chipsets.
 *
 * This needs to be done before the RTS/CTS fields are calculated
 * as they may depend upon the rate chosen.
 */
static void
ath_tx_do_ratelookup(struct ath_softc *sc, struct ath_buf *bf)
{
	uint8_t rate, rix;
	int try0;

	if (! bf->bf_state.bfs_doratelookup)
		return;

	/* Get rid of any previous state */
	bzero(bf->bf_state.bfs_rc, sizeof(bf->bf_state.bfs_rc));

	ATH_NODE_LOCK(ATH_NODE(bf->bf_node));
	ath_rate_findrate(sc, ATH_NODE(bf->bf_node), bf->bf_state.bfs_shpream,
	    bf->bf_state.bfs_pktlen, &rix, &try0, &rate);

	/* In case MRR is disabled, make sure rc[0] is setup correctly */
	bf->bf_state.bfs_rc[0].rix = rix;
	bf->bf_state.bfs_rc[0].ratecode = rate;
	bf->bf_state.bfs_rc[0].tries = try0;

	if (bf->bf_state.bfs_ismrr && try0 != ATH_TXMAXTRY)
		ath_rate_getxtxrates(sc, ATH_NODE(bf->bf_node), rix,
		    bf->bf_state.bfs_rc);
	ATH_NODE_UNLOCK(ATH_NODE(bf->bf_node));

	sc->sc_txrix = rix;	/* for LED blinking */
	sc->sc_lastdatarix = rix;	/* for fast frames */
	bf->bf_state.bfs_try0 = try0;
	bf->bf_state.bfs_txrate0 = rate;
}

/*
 * Update the CLRDMASK bit in the ath_buf if it needs to be set.
 */
static void
ath_tx_update_clrdmask(struct ath_softc *sc, struct ath_tid *tid,
    struct ath_buf *bf)
{
	struct ath_node *an = ATH_NODE(bf->bf_node);

	ATH_TX_LOCK_ASSERT(sc);

	if (an->clrdmask == 1) {
		bf->bf_state.bfs_txflags |= HAL_TXDESC_CLRDMASK;
		an->clrdmask = 0;
	}
}

/*
 * Return whether this frame should be software queued or
 * direct dispatched.
 *
 * When doing powersave, BAR frames should be queued but other management
 * frames should be directly sent.
 *
 * When not doing powersave, stick BAR frames into the hardware queue
 * so it goes out even though the queue is paused.
 *
 * For now, management frames are also software queued by default.
 */
static int
ath_tx_should_swq_frame(struct ath_softc *sc, struct ath_node *an,
    struct mbuf *m0, int *queue_to_head)
{
	struct ieee80211_node *ni = &an->an_node;
	struct ieee80211_frame *wh;
	uint8_t type, subtype;

	wh = mtod(m0, struct ieee80211_frame *);
	type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;
	subtype = wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;

	(*queue_to_head) = 0;

	/* If it's not in powersave - direct-dispatch BAR */
	if ((ATH_NODE(ni)->an_is_powersave == 0)
	    && type == IEEE80211_FC0_TYPE_CTL &&
	    subtype == IEEE80211_FC0_SUBTYPE_BAR) {
		DPRINTF(sc, ATH_DEBUG_SW_TX,
		    "%s: BAR: TX'ing direct\n", __func__);
		return (0);
	} else if ((ATH_NODE(ni)->an_is_powersave == 1)
	    && type == IEEE80211_FC0_TYPE_CTL &&
	    subtype == IEEE80211_FC0_SUBTYPE_BAR) {
		/* BAR TX whilst asleep; queue */
		DPRINTF(sc, ATH_DEBUG_SW_TX,
		    "%s: swq: TX'ing\n", __func__);
		(*queue_to_head) = 1;
		return (1);
	} else if ((ATH_NODE(ni)->an_is_powersave == 1)
	    && (type == IEEE80211_FC0_TYPE_MGT ||
	        type == IEEE80211_FC0_TYPE_CTL)) {
		/*
		 * Other control/mgmt frame; bypass software queuing
		 * for now!
		 */
		DPRINTF(sc, ATH_DEBUG_XMIT, 
		    "%s: %6D: Node is asleep; sending mgmt "
		    "(type=%d, subtype=%d)\n",
		    __func__, ni->ni_macaddr, ":", type, subtype);
		return (0);
	} else {
		return (1);
	}
}


/*
 * Transmit the given frame to the hardware.
 *
 * The frame must already be setup; rate control must already have
 * been done.
 *
 * XXX since the TXQ lock is being held here (and I dislike holding
 * it for this long when not doing software aggregation), later on
 * break this function into "setup_normal" and "xmit_normal". The
 * lock only needs to be held for the ath_tx_handoff call.
 *
 * XXX we don't update the leak count here - if we're doing
 * direct frame dispatch, we need to be able to do it without
 * decrementing the leak count (eg multicast queue frames.)
 */
static void
ath_tx_xmit_normal(struct ath_softc *sc, struct ath_txq *txq,
    struct ath_buf *bf)
{
	struct ath_node *an = ATH_NODE(bf->bf_node);
	struct ath_tid *tid = &an->an_tid[bf->bf_state.bfs_tid];

	ATH_TX_LOCK_ASSERT(sc);

	/*
	 * For now, just enable CLRDMASK. ath_tx_xmit_normal() does
	 * set a completion handler however it doesn't (yet) properly
	 * handle the strict ordering requirements needed for normal,
	 * non-aggregate session frames.
	 *
	 * Once this is implemented, only set CLRDMASK like this for
	 * frames that must go out - eg management/raw frames.
	 */
	bf->bf_state.bfs_txflags |= HAL_TXDESC_CLRDMASK;

	/* Setup the descriptor before handoff */
	ath_tx_do_ratelookup(sc, bf);
	ath_tx_calc_duration(sc, bf);
	ath_tx_calc_protection(sc, bf);
	ath_tx_set_rtscts(sc, bf);
	ath_tx_rate_fill_rcflags(sc, bf);
	ath_tx_setds(sc, bf);

	/* Track per-TID hardware queue depth correctly */
	tid->hwq_depth++;

	/* Assign the completion handler */
	bf->bf_comp = ath_tx_normal_comp;

	/* Hand off to hardware */
	ath_tx_handoff(sc, txq, bf);
}

/*
 * Do the basic frame setup stuff that's required before the frame
 * is added to a software queue.
 *
 * All frames get mostly the same treatment and it's done once.
 * Retransmits fiddle with things like the rate control setup,
 * setting the retransmit bit in the packet; doing relevant DMA/bus
 * syncing and relinking it (back) into the hardware TX queue.
 *
 * Note that this may cause the mbuf to be reallocated, so
 * m0 may not be valid.
 */
static int
ath_tx_normal_setup(struct ath_softc *sc, struct ieee80211_node *ni,
    struct ath_buf *bf, struct mbuf *m0, struct ath_txq *txq)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211com *ic = &sc->sc_ic;
	int error, iswep, ismcast, isfrag, ismrr;
	int keyix, hdrlen, pktlen, try0 = 0;
	u_int8_t rix = 0, txrate = 0;
	struct ath_desc *ds;
	struct ieee80211_frame *wh;
	u_int subtype, flags;
	HAL_PKT_TYPE atype;
	const HAL_RATE_TABLE *rt;
	HAL_BOOL shortPreamble;
	struct ath_node *an;

	/* XXX TODO: this pri is only used for non-QoS check, right? */
	u_int pri;

	/*
	 * To ensure that both sequence numbers and the CCMP PN handling
	 * is "correct", make sure that the relevant TID queue is locked.
	 * Otherwise the CCMP PN and seqno may appear out of order, causing
	 * re-ordered frames to have out of order CCMP PN's, resulting
	 * in many, many frame drops.
	 */
	ATH_TX_LOCK_ASSERT(sc);

	wh = mtod(m0, struct ieee80211_frame *);
	iswep = wh->i_fc[1] & IEEE80211_FC1_PROTECTED;
	ismcast = IEEE80211_IS_MULTICAST(wh->i_addr1);
	isfrag = m0->m_flags & M_FRAG;
	hdrlen = ieee80211_anyhdrsize(wh);
	/*
	 * Packet length must not include any
	 * pad bytes; deduct them here.
	 */
	pktlen = m0->m_pkthdr.len - (hdrlen & 3);

	/* Handle encryption twiddling if needed */
	if (! ath_tx_tag_crypto(sc, ni, m0, iswep, isfrag, &hdrlen,
	    &pktlen, &keyix)) {
		ieee80211_free_mbuf(m0);
		return EIO;
	}

	/* packet header may have moved, reset our local pointer */
	wh = mtod(m0, struct ieee80211_frame *);

	pktlen += IEEE80211_CRC_LEN;

	/*
	 * Load the DMA map so any coalescing is done.  This
	 * also calculates the number of descriptors we need.
	 */
	error = ath_tx_dmasetup(sc, bf, m0);
	if (error != 0)
		return error;
	KASSERT((ni != NULL), ("%s: ni=NULL!", __func__));
	bf->bf_node = ni;			/* NB: held reference */
	m0 = bf->bf_m;				/* NB: may have changed */
	wh = mtod(m0, struct ieee80211_frame *);

	/* setup descriptors */
	ds = bf->bf_desc;
	rt = sc->sc_currates;
	KASSERT(rt != NULL, ("no rate table, mode %u", sc->sc_curmode));

	/*
	 * NB: the 802.11 layer marks whether or not we should
	 * use short preamble based on the current mode and
	 * negotiated parameters.
	 */
	if ((ic->ic_flags & IEEE80211_F_SHPREAMBLE) &&
	    (ni->ni_capinfo & IEEE80211_CAPINFO_SHORT_PREAMBLE)) {
		shortPreamble = AH_TRUE;
		sc->sc_stats.ast_tx_shortpre++;
	} else {
		shortPreamble = AH_FALSE;
	}

	an = ATH_NODE(ni);
	//flags = HAL_TXDESC_CLRDMASK;		/* XXX needed for crypto errs */
	flags = 0;
	ismrr = 0;				/* default no multi-rate retry*/

	pri = ath_tx_getac(sc, m0);			/* honor classification */
	/* XXX use txparams instead of fixed values */
	/*
	 * Calculate Atheros packet type from IEEE80211 packet header,
	 * setup for rate calculations, and select h/w transmit queue.
	 */
	switch (wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) {
	case IEEE80211_FC0_TYPE_MGT:
		subtype = wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;
		if (subtype == IEEE80211_FC0_SUBTYPE_BEACON)
			atype = HAL_PKT_TYPE_BEACON;
		else if (subtype == IEEE80211_FC0_SUBTYPE_PROBE_RESP)
			atype = HAL_PKT_TYPE_PROBE_RESP;
		else if (subtype == IEEE80211_FC0_SUBTYPE_ATIM)
			atype = HAL_PKT_TYPE_ATIM;
		else
			atype = HAL_PKT_TYPE_NORMAL;	/* XXX */
		rix = an->an_mgmtrix;
		txrate = rt->info[rix].rateCode;
		if (shortPreamble)
			txrate |= rt->info[rix].shortPreamble;
		try0 = ATH_TXMGTTRY;
		flags |= HAL_TXDESC_INTREQ;	/* force interrupt */
		break;
	case IEEE80211_FC0_TYPE_CTL:
		atype = HAL_PKT_TYPE_PSPOLL;	/* stop setting of duration */
		rix = an->an_mgmtrix;
		txrate = rt->info[rix].rateCode;
		if (shortPreamble)
			txrate |= rt->info[rix].shortPreamble;
		try0 = ATH_TXMGTTRY;
		flags |= HAL_TXDESC_INTREQ;	/* force interrupt */
		break;
	case IEEE80211_FC0_TYPE_DATA:
		atype = HAL_PKT_TYPE_NORMAL;		/* default */
		/*
		 * Data frames: multicast frames go out at a fixed rate,
		 * EAPOL frames use the mgmt frame rate; otherwise consult
		 * the rate control module for the rate to use.
		 */
		if (ismcast) {
			rix = an->an_mcastrix;
			txrate = rt->info[rix].rateCode;
			if (shortPreamble)
				txrate |= rt->info[rix].shortPreamble;
			try0 = 1;
		} else if (m0->m_flags & M_EAPOL) {
			/* XXX? maybe always use long preamble? */
			rix = an->an_mgmtrix;
			txrate = rt->info[rix].rateCode;
			if (shortPreamble)
				txrate |= rt->info[rix].shortPreamble;
			try0 = ATH_TXMAXTRY;	/* XXX?too many? */
		} else {
			/*
			 * Do rate lookup on each TX, rather than using
			 * the hard-coded TX information decided here.
			 */
			ismrr = 1;
			bf->bf_state.bfs_doratelookup = 1;
		}

		/*
		 * Check whether to set NOACK for this WME category or not.
		 */
		if (ieee80211_wme_vap_ac_is_noack(vap, pri))
			flags |= HAL_TXDESC_NOACK;
		break;
	default:
		device_printf(sc->sc_dev, "bogus frame type 0x%x (%s)\n",
		    wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK, __func__);
		/* XXX statistic */
		/* XXX free tx dmamap */
		ieee80211_free_mbuf(m0);
		return EIO;
	}

	/*
	 * There are two known scenarios where the frame AC doesn't match
	 * what the destination TXQ is.
	 *
	 * + non-QoS frames (eg management?) that the net80211 stack has
	 *   assigned a higher AC to, but since it's a non-QoS TID, it's
	 *   being thrown into TID 16.  TID 16 gets the AC_BE queue.
	 *   It's quite possible that management frames should just be
	 *   direct dispatched to hardware rather than go via the software
	 *   queue; that should be investigated in the future.  There are
	 *   some specific scenarios where this doesn't make sense, mostly
	 *   surrounding ADDBA request/response - hence why that is special
	 *   cased.
	 *
	 * + Multicast frames going into the VAP mcast queue.  That shows up
	 *   as "TXQ 11".
	 *
	 * This driver should eventually support separate TID and TXQ locking,
	 * allowing for arbitrary AC frames to appear on arbitrary software
	 * queues, being queued to the "correct" hardware queue when needed.
	 */
#if 0
	if (txq != sc->sc_ac2q[pri]) {
		DPRINTF(sc, ATH_DEBUG_XMIT, 
		    "%s: txq=%p (%d), pri=%d, pri txq=%p (%d)\n",
		    __func__,
		    txq,
		    txq->axq_qnum,
		    pri,
		    sc->sc_ac2q[pri],
		    sc->sc_ac2q[pri]->axq_qnum);
	}
#endif

	/*
	 * Calculate miscellaneous flags.
	 */
	if (ismcast) {
		flags |= HAL_TXDESC_NOACK;	/* no ack on broad/multicast */
	} else if (pktlen > vap->iv_rtsthreshold &&
	    (ni->ni_ath_flags & IEEE80211_NODE_FF) == 0) {
		flags |= HAL_TXDESC_RTSENA;	/* RTS based on frame length */
		sc->sc_stats.ast_tx_rts++;
	}
	if (flags & HAL_TXDESC_NOACK)		/* NB: avoid double counting */
		sc->sc_stats.ast_tx_noack++;
#ifdef IEEE80211_SUPPORT_TDMA
	if (sc->sc_tdma && (flags & HAL_TXDESC_NOACK) == 0) {
		DPRINTF(sc, ATH_DEBUG_TDMA,
		    "%s: discard frame, ACK required w/ TDMA\n", __func__);
		sc->sc_stats.ast_tdma_ack++;
		/* XXX free tx dmamap */
		ieee80211_free_mbuf(m0);
		return EIO;
	}
#endif

	/*
	 * If it's a frame to do location reporting on,
	 * communicate it to the HAL.
	 */
	if (ieee80211_get_toa_params(m0, NULL)) {
		device_printf(sc->sc_dev,
		    "%s: setting TX positioning bit\n", __func__);
		flags |= HAL_TXDESC_POS;

		/*
		 * Note: The hardware reports timestamps for
		 * each of the RX'ed packets as part of the packet
		 * exchange.  So this means things like RTS/CTS
		 * exchanges, as well as the final ACK.
		 *
		 * So, if you send a RTS-protected NULL data frame,
		 * you'll get an RX report for the RTS response, then
		 * an RX report for the NULL frame, and then the TX
		 * completion at the end.
		 *
		 * NOTE: it doesn't work right for CCK frames;
		 * there's no channel info data provided unless
		 * it's OFDM or HT.  Will have to dig into it.
		 */
		flags &= ~(HAL_TXDESC_RTSENA | HAL_TXDESC_CTSENA);
		bf->bf_flags |= ATH_BUF_TOA_PROBE;
	}

#if 0
	/*
	 * Placeholder: if you want to transmit with the azimuth
	 * timestamp in the end of the payload, here's where you
	 * should set the TXDESC field.
	 */
	flags |= HAL_TXDESC_HWTS;
#endif

	/*
	 * Determine if a tx interrupt should be generated for
	 * this descriptor.  We take a tx interrupt to reap
	 * descriptors when the h/w hits an EOL condition or
	 * when the descriptor is specifically marked to generate
	 * an interrupt.  We periodically mark descriptors in this
	 * way to insure timely replenishing of the supply needed
	 * for sending frames.  Defering interrupts reduces system
	 * load and potentially allows more concurrent work to be
	 * done but if done to aggressively can cause senders to
	 * backup.
	 *
	 * NB: use >= to deal with sc_txintrperiod changing
	 *     dynamically through sysctl.
	 */
	if (flags & HAL_TXDESC_INTREQ) {
		txq->axq_intrcnt = 0;
	} else if (++txq->axq_intrcnt >= sc->sc_txintrperiod) {
		flags |= HAL_TXDESC_INTREQ;
		txq->axq_intrcnt = 0;
	}

	/* This point forward is actual TX bits */

	/*
	 * At this point we are committed to sending the frame
	 * and we don't need to look at m_nextpkt; clear it in
	 * case this frame is part of frag chain.
	 */
	m0->m_nextpkt = NULL;

	if (IFF_DUMPPKTS(sc, ATH_DEBUG_XMIT))
		ieee80211_dump_pkt(ic, mtod(m0, const uint8_t *), m0->m_len,
		    sc->sc_hwmap[rix].ieeerate, -1);

	if (ieee80211_radiotap_active_vap(vap)) {
		sc->sc_tx_th.wt_flags = sc->sc_hwmap[rix].txflags;
		if (iswep)
			sc->sc_tx_th.wt_flags |= IEEE80211_RADIOTAP_F_WEP;
		if (isfrag)
			sc->sc_tx_th.wt_flags |= IEEE80211_RADIOTAP_F_FRAG;
		sc->sc_tx_th.wt_rate = sc->sc_hwmap[rix].ieeerate;
		sc->sc_tx_th.wt_txpower = ieee80211_get_node_txpower(ni);
		sc->sc_tx_th.wt_antenna = sc->sc_txantenna;

		ieee80211_radiotap_tx(vap, m0);
	}

	/* Blank the legacy rate array */
	bzero(&bf->bf_state.bfs_rc, sizeof(bf->bf_state.bfs_rc));

	/*
	 * ath_buf_set_rate needs at least one rate/try to setup
	 * the rate scenario.
	 */
	bf->bf_state.bfs_rc[0].rix = rix;
	bf->bf_state.bfs_rc[0].tries = try0;
	bf->bf_state.bfs_rc[0].ratecode = txrate;

	/* Store the decided rate index values away */
	bf->bf_state.bfs_pktlen = pktlen;
	bf->bf_state.bfs_hdrlen = hdrlen;
	bf->bf_state.bfs_atype = atype;
	bf->bf_state.bfs_txpower = ieee80211_get_node_txpower(ni);
	bf->bf_state.bfs_txrate0 = txrate;
	bf->bf_state.bfs_try0 = try0;
	bf->bf_state.bfs_keyix = keyix;
	bf->bf_state.bfs_txantenna = sc->sc_txantenna;
	bf->bf_state.bfs_txflags = flags;
	bf->bf_state.bfs_shpream = shortPreamble;

	/* XXX this should be done in ath_tx_setrate() */
	bf->bf_state.bfs_ctsrate0 = 0;	/* ie, no hard-coded ctsrate */
	bf->bf_state.bfs_ctsrate = 0;	/* calculated later */
	bf->bf_state.bfs_ctsduration = 0;
	bf->bf_state.bfs_ismrr = ismrr;

	return 0;
}

/*
 * Queue a frame to the hardware or software queue.
 *
 * This can be called by the net80211 code.
 *
 * XXX what about locking? Or, push the seqno assign into the
 * XXX aggregate scheduler so its serialised?
 *
 * XXX When sending management frames via ath_raw_xmit(),
 *     should CLRDMASK be set unconditionally?
 */
int
ath_tx_start(struct ath_softc *sc, struct ieee80211_node *ni,
    struct ath_buf *bf, struct mbuf *m0)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ath_vap *avp = ATH_VAP(vap);
	int r = 0;
	u_int pri;
	int tid;
	struct ath_txq *txq;
	int ismcast;
	const struct ieee80211_frame *wh;
	int is_ampdu, is_ampdu_tx, is_ampdu_pending;
	ieee80211_seq seqno;
	uint8_t type, subtype;
	int queue_to_head;

	ATH_TX_LOCK_ASSERT(sc);

	/*
	 * Determine the target hardware queue.
	 *
	 * For multicast frames, the txq gets overridden appropriately
	 * depending upon the state of PS.  If powersave is enabled
	 * then they get added to the cabq for later transmit.
	 *
	 * The "fun" issue here is that group addressed frames should
	 * have the sequence number from a different pool, rather than
	 * the per-TID pool.  That means that even QoS group addressed
	 * frames will have a sequence number from that global value,
	 * which means if we transmit different group addressed frames
	 * at different traffic priorities, the sequence numbers will
	 * all be out of whack.  So - chances are, the right thing
	 * to do here is to always put group addressed frames into the BE
	 * queue, and ignore the TID for queue selection.
	 *
	 * For any other frame, we do a TID/QoS lookup inside the frame
	 * to see what the TID should be. If it's a non-QoS frame, the
	 * AC and TID are overridden. The TID/TXQ code assumes the
	 * TID is on a predictable hardware TXQ, so we don't support
	 * having a node TID queued to multiple hardware TXQs.
	 * This may change in the future but would require some locking
	 * fudgery.
	 */
	pri = ath_tx_getac(sc, m0);
	tid = ath_tx_gettid(sc, m0);

	txq = sc->sc_ac2q[pri];
	wh = mtod(m0, struct ieee80211_frame *);
	ismcast = IEEE80211_IS_MULTICAST(wh->i_addr1);
	type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;
	subtype = wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;

	/*
	 * Enforce how deep the multicast queue can grow.
	 *
	 * XXX duplicated in ath_raw_xmit().
	 */
	if (IEEE80211_IS_MULTICAST(wh->i_addr1)) {
		if (sc->sc_cabq->axq_depth + sc->sc_cabq->fifo.axq_depth
		    > sc->sc_txq_mcastq_maxdepth) {
			sc->sc_stats.ast_tx_mcastq_overflow++;
			m_freem(m0);
			return (ENOBUFS);
		}
	}

	/*
	 * Enforce how deep the unicast queue can grow.
	 *
	 * If the node is in power save then we don't want
	 * the software queue to grow too deep, or a node may
	 * end up consuming all of the ath_buf entries.
	 *
	 * For now, only do this for DATA frames.
	 *
	 * We will want to cap how many management/control
	 * frames get punted to the software queue so it doesn't
	 * fill up.  But the correct solution isn't yet obvious.
	 * In any case, this check should at least let frames pass
	 * that we are direct-dispatching.
	 *
	 * XXX TODO: duplicate this to the raw xmit path!
	 */
	if (type == IEEE80211_FC0_TYPE_DATA &&
	    ATH_NODE(ni)->an_is_powersave &&
	    ATH_NODE(ni)->an_swq_depth >
	     sc->sc_txq_node_psq_maxdepth) {
		sc->sc_stats.ast_tx_node_psq_overflow++;
		m_freem(m0);
		return (ENOBUFS);
	}

	/* A-MPDU TX */
	is_ampdu_tx = ath_tx_ampdu_running(sc, ATH_NODE(ni), tid);
	is_ampdu_pending = ath_tx_ampdu_pending(sc, ATH_NODE(ni), tid);
	is_ampdu = is_ampdu_tx | is_ampdu_pending;

	DPRINTF(sc, ATH_DEBUG_SW_TX, "%s: tid=%d, ac=%d, is_ampdu=%d\n",
	    __func__, tid, pri, is_ampdu);

	/* Set local packet state, used to queue packets to hardware */
	bf->bf_state.bfs_tid = tid;
	bf->bf_state.bfs_tx_queue = txq->axq_qnum;
	bf->bf_state.bfs_pri = pri;

#if 1
	/*
	 * When servicing one or more stations in power-save mode
	 * (or) if there is some mcast data waiting on the mcast
	 * queue (to prevent out of order delivery) multicast frames
	 * must be bufferd until after the beacon.
	 *
	 * TODO: we should lock the mcastq before we check the length.
	 */
	if (sc->sc_cabq_enable && ismcast && (vap->iv_ps_sta || avp->av_mcastq.axq_depth)) {
		txq = &avp->av_mcastq;
		/*
		 * Mark the frame as eventually belonging on the CAB
		 * queue, so the descriptor setup functions will
		 * correctly initialise the descriptor 'qcuId' field.
		 */
		bf->bf_state.bfs_tx_queue = sc->sc_cabq->axq_qnum;
	}
#endif

	/* Do the generic frame setup */
	/* XXX should just bzero the bf_state? */
	bf->bf_state.bfs_dobaw = 0;

	/* A-MPDU TX? Manually set sequence number */
	/*
	 * Don't do it whilst pending; the net80211 layer still
	 * assigns them.
	 *
	 * Don't assign A-MPDU sequence numbers to group address
	 * frames; they come from a different sequence number space.
	 */
	if (is_ampdu_tx && (! IEEE80211_IS_MULTICAST(wh->i_addr1))) {
		/*
		 * Always call; this function will
		 * handle making sure that null data frames
		 * and group-addressed frames don't get a sequence number
		 * from the current TID and thus mess with the BAW.
		 */
		seqno = ath_tx_tid_seqno_assign(sc, ni, bf, m0);

		/*
		 * Don't add QoS NULL frames and group-addressed frames
		 * to the BAW.
		 */
		if (IEEE80211_QOS_HAS_SEQ(wh) &&
		    (! IEEE80211_IS_MULTICAST(wh->i_addr1)) &&
		    (subtype != IEEE80211_FC0_SUBTYPE_QOS_NULL)) {
			bf->bf_state.bfs_dobaw = 1;
		}
	}

	/*
	 * If needed, the sequence number has been assigned.
	 * Squirrel it away somewhere easy to get to.
	 */
	bf->bf_state.bfs_seqno = M_SEQNO_GET(m0) << IEEE80211_SEQ_SEQ_SHIFT;

	/* Is ampdu pending? fetch the seqno and print it out */
	if (is_ampdu_pending)
		DPRINTF(sc, ATH_DEBUG_SW_TX,
		    "%s: tid %d: ampdu pending, seqno %d\n",
		    __func__, tid, M_SEQNO_GET(m0));

	/* This also sets up the DMA map; crypto; frame parameters, etc */
	r = ath_tx_normal_setup(sc, ni, bf, m0, txq);

	if (r != 0)
		goto done;

	/* At this point m0 could have changed! */
	m0 = bf->bf_m;

#if 1
	/*
	 * If it's a multicast frame, do a direct-dispatch to the
	 * destination hardware queue. Don't bother software
	 * queuing it.
	 */
	/*
	 * If it's a BAR frame, do a direct dispatch to the
	 * destination hardware queue. Don't bother software
	 * queuing it, as the TID will now be paused.
	 * Sending a BAR frame can occur from the net80211 txa timer
	 * (ie, retries) or from the ath txtask (completion call.)
	 * It queues directly to hardware because the TID is paused
	 * at this point (and won't be unpaused until the BAR has
	 * either been TXed successfully or max retries has been
	 * reached.)
	 */
	/*
	 * Until things are better debugged - if this node is asleep
	 * and we're sending it a non-BAR frame, direct dispatch it.
	 * Why? Because we need to figure out what's actually being
	 * sent - eg, during reassociation/reauthentication after
	 * the node (last) disappeared whilst asleep, the driver should
	 * have unpaused/unsleep'ed the node.  So until that is
	 * sorted out, use this workaround.
	 */
	if (txq == &avp->av_mcastq) {
		DPRINTF(sc, ATH_DEBUG_SW_TX,
		    "%s: bf=%p: mcastq: TX'ing\n", __func__, bf);
		bf->bf_state.bfs_txflags |= HAL_TXDESC_CLRDMASK;
		ath_tx_xmit_normal(sc, txq, bf);
	} else if (ath_tx_should_swq_frame(sc, ATH_NODE(ni), m0,
	    &queue_to_head)) {
		ath_tx_swq(sc, ni, txq, queue_to_head, bf);
	} else {
		bf->bf_state.bfs_txflags |= HAL_TXDESC_CLRDMASK;
		ath_tx_xmit_normal(sc, txq, bf);
	}
#else
	/*
	 * For now, since there's no software queue,
	 * direct-dispatch to the hardware.
	 */
	bf->bf_state.bfs_txflags |= HAL_TXDESC_CLRDMASK;
	/*
	 * Update the current leak count if
	 * we're leaking frames; and set the
	 * MORE flag as appropriate.
	 */
	ath_tx_leak_count_update(sc, tid, bf);
	ath_tx_xmit_normal(sc, txq, bf);
#endif
done:
	return 0;
}

static int
ath_tx_raw_start(struct ath_softc *sc, struct ieee80211_node *ni,
	struct ath_buf *bf, struct mbuf *m0,
	const struct ieee80211_bpf_params *params)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = ni->ni_vap;
	int error, ismcast, ismrr;
	int keyix, hdrlen, pktlen, try0, txantenna;
	u_int8_t rix, txrate;
	struct ieee80211_frame *wh;
	u_int flags;
	HAL_PKT_TYPE atype;
	const HAL_RATE_TABLE *rt;
	struct ath_desc *ds;
	u_int pri;
	int o_tid = -1;
	int do_override;
	uint8_t type, subtype;
	int queue_to_head;
	struct ath_node *an = ATH_NODE(ni);

	ATH_TX_LOCK_ASSERT(sc);

	wh = mtod(m0, struct ieee80211_frame *);
	ismcast = IEEE80211_IS_MULTICAST(wh->i_addr1);
	hdrlen = ieee80211_anyhdrsize(wh);
	/*
	 * Packet length must not include any
	 * pad bytes; deduct them here.
	 */
	/* XXX honor IEEE80211_BPF_DATAPAD */
	pktlen = m0->m_pkthdr.len - (hdrlen & 3) + IEEE80211_CRC_LEN;

	type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;
	subtype = wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;

	ATH_KTR(sc, ATH_KTR_TX, 2,
	     "ath_tx_raw_start: ni=%p, bf=%p, raw", ni, bf);

	DPRINTF(sc, ATH_DEBUG_SW_TX, "%s: ismcast=%d\n",
	    __func__, ismcast);

	pri = params->ibp_pri & 3;
	/* Override pri if the frame isn't a QoS one */
	if (! IEEE80211_QOS_HAS_SEQ(wh))
		pri = ath_tx_getac(sc, m0);

	/* XXX If it's an ADDBA, override the correct queue */
	do_override = ath_tx_action_frame_override_queue(sc, ni, m0, &o_tid);

	/* Map ADDBA to the correct priority */
	if (do_override) {
#if 1
		DPRINTF(sc, ATH_DEBUG_XMIT, 
		    "%s: overriding tid %d pri %d -> %d\n",
		    __func__, o_tid, pri, TID_TO_WME_AC(o_tid));
#endif
		pri = TID_TO_WME_AC(o_tid);
	}

	/*
	 * "pri" is the hardware queue to transmit on.
	 *
	 * Look at the description in ath_tx_start() to understand
	 * what needs to be "fixed" here so we just use the TID
	 * for QoS frames.
	 */

	/* Handle encryption twiddling if needed */
	if (! ath_tx_tag_crypto(sc, ni,
	    m0, params->ibp_flags & IEEE80211_BPF_CRYPTO, 0,
	    &hdrlen, &pktlen, &keyix)) {
		ieee80211_free_mbuf(m0);
		return EIO;
	}
	/* packet header may have moved, reset our local pointer */
	wh = mtod(m0, struct ieee80211_frame *);

	/* Do the generic frame setup */
	/* XXX should just bzero the bf_state? */
	bf->bf_state.bfs_dobaw = 0;

	error = ath_tx_dmasetup(sc, bf, m0);
	if (error != 0)
		return error;
	m0 = bf->bf_m;				/* NB: may have changed */
	wh = mtod(m0, struct ieee80211_frame *);
	KASSERT((ni != NULL), ("%s: ni=NULL!", __func__));
	bf->bf_node = ni;			/* NB: held reference */

	/* Always enable CLRDMASK for raw frames for now.. */
	flags = HAL_TXDESC_CLRDMASK;		/* XXX needed for crypto errs */
	flags |= HAL_TXDESC_INTREQ;		/* force interrupt */
	if (params->ibp_flags & IEEE80211_BPF_RTS)
		flags |= HAL_TXDESC_RTSENA;
	else if (params->ibp_flags & IEEE80211_BPF_CTS) {
		/* XXX assume 11g/11n protection? */
		bf->bf_state.bfs_doprot = 1;
		flags |= HAL_TXDESC_CTSENA;
	}
	/* XXX leave ismcast to injector? */
	if ((params->ibp_flags & IEEE80211_BPF_NOACK) || ismcast)
		flags |= HAL_TXDESC_NOACK;

	rt = sc->sc_currates;
	KASSERT(rt != NULL, ("no rate table, mode %u", sc->sc_curmode));

	/* Fetch first rate information */
	rix = ath_tx_findrix(sc, params->ibp_rate0);
	try0 = params->ibp_try0;

	/*
	 * Override EAPOL rate as appropriate.
	 */
	if (m0->m_flags & M_EAPOL) {
		/* XXX? maybe always use long preamble? */
		rix = an->an_mgmtrix;
		try0 = ATH_TXMAXTRY;	/* XXX?too many? */
	}

	/*
	 * If it's a frame to do location reporting on,
	 * communicate it to the HAL.
	 */
	if (ieee80211_get_toa_params(m0, NULL)) {
		device_printf(sc->sc_dev,
		    "%s: setting TX positioning bit\n", __func__);
		flags |= HAL_TXDESC_POS;
		flags &= ~(HAL_TXDESC_RTSENA | HAL_TXDESC_CTSENA);
		bf->bf_flags |= ATH_BUF_TOA_PROBE;
	}

	txrate = rt->info[rix].rateCode;
	if (params->ibp_flags & IEEE80211_BPF_SHORTPRE)
		txrate |= rt->info[rix].shortPreamble;
	sc->sc_txrix = rix;
	ismrr = (params->ibp_try1 != 0);
	txantenna = params->ibp_pri >> 2;
	if (txantenna == 0)			/* XXX? */
		txantenna = sc->sc_txantenna;

	/*
	 * Since ctsrate is fixed, store it away for later
	 * use when the descriptor fields are being set.
	 */
	if (flags & (HAL_TXDESC_RTSENA|HAL_TXDESC_CTSENA))
		bf->bf_state.bfs_ctsrate0 = params->ibp_ctsrate;

	/*
	 * NB: we mark all packets as type PSPOLL so the h/w won't
	 * set the sequence number, duration, etc.
	 */
	atype = HAL_PKT_TYPE_PSPOLL;

	if (IFF_DUMPPKTS(sc, ATH_DEBUG_XMIT))
		ieee80211_dump_pkt(ic, mtod(m0, caddr_t), m0->m_len,
		    sc->sc_hwmap[rix].ieeerate, -1);

	if (ieee80211_radiotap_active_vap(vap)) {
		sc->sc_tx_th.wt_flags = sc->sc_hwmap[rix].txflags;
		if (wh->i_fc[1] & IEEE80211_FC1_PROTECTED)
			sc->sc_tx_th.wt_flags |= IEEE80211_RADIOTAP_F_WEP;
		if (m0->m_flags & M_FRAG)
			sc->sc_tx_th.wt_flags |= IEEE80211_RADIOTAP_F_FRAG;
		sc->sc_tx_th.wt_rate = sc->sc_hwmap[rix].ieeerate;
		sc->sc_tx_th.wt_txpower = MIN(params->ibp_power,
		    ieee80211_get_node_txpower(ni));
		sc->sc_tx_th.wt_antenna = sc->sc_txantenna;

		ieee80211_radiotap_tx(vap, m0);
	}

	/*
	 * Formulate first tx descriptor with tx controls.
	 */
	ds = bf->bf_desc;
	/* XXX check return value? */

	/* Store the decided rate index values away */
	bf->bf_state.bfs_pktlen = pktlen;
	bf->bf_state.bfs_hdrlen = hdrlen;
	bf->bf_state.bfs_atype = atype;
	bf->bf_state.bfs_txpower = MIN(params->ibp_power,
	    ieee80211_get_node_txpower(ni));
	bf->bf_state.bfs_txrate0 = txrate;
	bf->bf_state.bfs_try0 = try0;
	bf->bf_state.bfs_keyix = keyix;
	bf->bf_state.bfs_txantenna = txantenna;
	bf->bf_state.bfs_txflags = flags;
	bf->bf_state.bfs_shpream =
	    !! (params->ibp_flags & IEEE80211_BPF_SHORTPRE);

	/* Set local packet state, used to queue packets to hardware */
	bf->bf_state.bfs_tid = WME_AC_TO_TID(pri);
	bf->bf_state.bfs_tx_queue = sc->sc_ac2q[pri]->axq_qnum;
	bf->bf_state.bfs_pri = pri;

	/* XXX this should be done in ath_tx_setrate() */
	bf->bf_state.bfs_ctsrate = 0;
	bf->bf_state.bfs_ctsduration = 0;
	bf->bf_state.bfs_ismrr = ismrr;

	/* Blank the legacy rate array */
	bzero(&bf->bf_state.bfs_rc, sizeof(bf->bf_state.bfs_rc));

	bf->bf_state.bfs_rc[0].rix = rix;
	bf->bf_state.bfs_rc[0].tries = try0;
	bf->bf_state.bfs_rc[0].ratecode = txrate;

	if (ismrr) {
		int rix;

		rix = ath_tx_findrix(sc, params->ibp_rate1);
		bf->bf_state.bfs_rc[1].rix = rix;
		bf->bf_state.bfs_rc[1].tries = params->ibp_try1;

		rix = ath_tx_findrix(sc, params->ibp_rate2);
		bf->bf_state.bfs_rc[2].rix = rix;
		bf->bf_state.bfs_rc[2].tries = params->ibp_try2;

		rix = ath_tx_findrix(sc, params->ibp_rate3);
		bf->bf_state.bfs_rc[3].rix = rix;
		bf->bf_state.bfs_rc[3].tries = params->ibp_try3;
	}
	/*
	 * All the required rate control decisions have been made;
	 * fill in the rc flags.
	 */
	ath_tx_rate_fill_rcflags(sc, bf);

	/* NB: no buffered multicast in power save support */

	/*
	 * If we're overiding the ADDBA destination, dump directly
	 * into the hardware queue, right after any pending
	 * frames to that node are.
	 */
	DPRINTF(sc, ATH_DEBUG_SW_TX, "%s: dooverride=%d\n",
	    __func__, do_override);

#if 1
	/*
	 * Put addba frames in the right place in the right TID/HWQ.
	 */
	if (do_override) {
		bf->bf_state.bfs_txflags |= HAL_TXDESC_CLRDMASK;
		/*
		 * XXX if it's addba frames, should we be leaking
		 * them out via the frame leak method?
		 * XXX for now let's not risk it; but we may wish
		 * to investigate this later.
		 */
		ath_tx_xmit_normal(sc, sc->sc_ac2q[pri], bf);
	} else if (ath_tx_should_swq_frame(sc, ATH_NODE(ni), m0,
	    &queue_to_head)) {
		/* Queue to software queue */
		ath_tx_swq(sc, ni, sc->sc_ac2q[pri], queue_to_head, bf);
	} else {
		bf->bf_state.bfs_txflags |= HAL_TXDESC_CLRDMASK;
		ath_tx_xmit_normal(sc, sc->sc_ac2q[pri], bf);
	}
#else
	/* Direct-dispatch to the hardware */
	bf->bf_state.bfs_txflags |= HAL_TXDESC_CLRDMASK;
	/*
	 * Update the current leak count if
	 * we're leaking frames; and set the
	 * MORE flag as appropriate.
	 */
	ath_tx_leak_count_update(sc, tid, bf);
	ath_tx_xmit_normal(sc, sc->sc_ac2q[pri], bf);
#endif
	return 0;
}

/*
 * Send a raw frame.
 *
 * This can be called by net80211.
 */
int
ath_raw_xmit(struct ieee80211_node *ni, struct mbuf *m,
	const struct ieee80211_bpf_params *params)
{
	struct ieee80211com *ic = ni->ni_ic;
	struct ath_softc *sc = ic->ic_softc;
	struct ath_buf *bf;
	struct ieee80211_frame *wh = mtod(m, struct ieee80211_frame *);
	int error = 0;

	ATH_PCU_LOCK(sc);
	if (sc->sc_inreset_cnt > 0) {
		DPRINTF(sc, ATH_DEBUG_XMIT, 
		    "%s: sc_inreset_cnt > 0; bailing\n", __func__);
		error = EIO;
		ATH_PCU_UNLOCK(sc);
		goto badbad;
	}
	sc->sc_txstart_cnt++;
	ATH_PCU_UNLOCK(sc);

	/* Wake the hardware up already */
	ATH_LOCK(sc);
	ath_power_set_power_state(sc, HAL_PM_AWAKE);
	ATH_UNLOCK(sc);

	ATH_TX_LOCK(sc);

	if (!sc->sc_running || sc->sc_invalid) {
		DPRINTF(sc, ATH_DEBUG_XMIT, "%s: discard frame, r/i: %d/%d",
		    __func__, sc->sc_running, sc->sc_invalid);
		m_freem(m);
		error = ENETDOWN;
		goto bad;
	}

	/*
	 * Enforce how deep the multicast queue can grow.
	 *
	 * XXX duplicated in ath_tx_start().
	 */
	if (IEEE80211_IS_MULTICAST(wh->i_addr1)) {
		if (sc->sc_cabq->axq_depth + sc->sc_cabq->fifo.axq_depth
		    > sc->sc_txq_mcastq_maxdepth) {
			sc->sc_stats.ast_tx_mcastq_overflow++;
			error = ENOBUFS;
		}

		if (error != 0) {
			m_freem(m);
			goto bad;
		}
	}

	/*
	 * Grab a TX buffer and associated resources.
	 */
	bf = ath_getbuf(sc, ATH_BUFTYPE_MGMT);
	if (bf == NULL) {
		sc->sc_stats.ast_tx_nobuf++;
		m_freem(m);
		error = ENOBUFS;
		goto bad;
	}
	ATH_KTR(sc, ATH_KTR_TX, 3, "ath_raw_xmit: m=%p, params=%p, bf=%p\n",
	    m, params,  bf);

	if (params == NULL) {
		/*
		 * Legacy path; interpret frame contents to decide
		 * precisely how to send the frame.
		 */
		if (ath_tx_start(sc, ni, bf, m)) {
			error = EIO;		/* XXX */
			goto bad2;
		}
	} else {
		/*
		 * Caller supplied explicit parameters to use in
		 * sending the frame.
		 */
		if (ath_tx_raw_start(sc, ni, bf, m, params)) {
			error = EIO;		/* XXX */
			goto bad2;
		}
	}
	sc->sc_wd_timer = 5;
	sc->sc_stats.ast_tx_raw++;

	/*
	 * Update the TIM - if there's anything queued to the
	 * software queue and power save is enabled, we should
	 * set the TIM.
	 */
	ath_tx_update_tim(sc, ni, 1);

	ATH_TX_UNLOCK(sc);

	ATH_PCU_LOCK(sc);
	sc->sc_txstart_cnt--;
	ATH_PCU_UNLOCK(sc);


	/* Put the hardware back to sleep if required */
	ATH_LOCK(sc);
	ath_power_restore_power_state(sc);
	ATH_UNLOCK(sc);

	return 0;

bad2:
	ATH_KTR(sc, ATH_KTR_TX, 3, "ath_raw_xmit: bad2: m=%p, params=%p, "
	    "bf=%p",
	    m,
	    params,
	    bf);
	ATH_TXBUF_LOCK(sc);
	ath_returnbuf_head(sc, bf);
	ATH_TXBUF_UNLOCK(sc);

bad:
	ATH_TX_UNLOCK(sc);

	ATH_PCU_LOCK(sc);
	sc->sc_txstart_cnt--;
	ATH_PCU_UNLOCK(sc);

	/* Put the hardware back to sleep if required */
	ATH_LOCK(sc);
	ath_power_restore_power_state(sc);
	ATH_UNLOCK(sc);

badbad:
	ATH_KTR(sc, ATH_KTR_TX, 2, "ath_raw_xmit: bad0: m=%p, params=%p",
	    m, params);
	sc->sc_stats.ast_tx_raw_fail++;

	return error;
}

/* Some helper functions */

/*
 * ADDBA (and potentially others) need to be placed in the same
 * hardware queue as the TID/node it's relating to. This is so
 * it goes out after any pending non-aggregate frames to the
 * same node/TID.
 *
 * If this isn't done, the ADDBA can go out before the frames
 * queued in hardware. Even though these frames have a sequence
 * number -earlier- than the ADDBA can be transmitted (but
 * no frames whose sequence numbers are after the ADDBA should
 * be!) they'll arrive after the ADDBA - and the receiving end
 * will simply drop them as being out of the BAW.
 *
 * The frames can't be appended to the TID software queue - it'll
 * never be sent out. So these frames have to be directly
 * dispatched to the hardware, rather than queued in software.
 * So if this function returns true, the TXQ has to be
 * overridden and it has to be directly dispatched.
 *
 * It's a dirty hack, but someone's gotta do it.
 */

/*
 * XXX doesn't belong here!
 */
static int
ieee80211_is_action(struct ieee80211_frame *wh)
{
	/* Type: Management frame? */
	if ((wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) !=
	    IEEE80211_FC0_TYPE_MGT)
		return 0;

	/* Subtype: Action frame? */
	if ((wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) !=
	    IEEE80211_FC0_SUBTYPE_ACTION)
		return 0;

	return 1;
}

#define	MS(_v, _f)	(((_v) & _f) >> _f##_S)
/*
 * Return an alternate TID for ADDBA request frames.
 *
 * Yes, this likely should be done in the net80211 layer.
 */
static int
ath_tx_action_frame_override_queue(struct ath_softc *sc,
    struct ieee80211_node *ni,
    struct mbuf *m0, int *tid)
{
	struct ieee80211_frame *wh = mtod(m0, struct ieee80211_frame *);
	struct ieee80211_action_ba_addbarequest *ia;
	uint8_t *frm;
	uint16_t baparamset;

	/* Not action frame? Bail */
	if (! ieee80211_is_action(wh))
		return 0;

	/* XXX Not needed for frames we send? */
#if 0
	/* Correct length? */
	if (! ieee80211_parse_action(ni, m))
		return 0;
#endif

	/* Extract out action frame */
	frm = (u_int8_t *)&wh[1];
	ia = (struct ieee80211_action_ba_addbarequest *) frm;

	/* Not ADDBA? Bail */
	if (ia->rq_header.ia_category != IEEE80211_ACTION_CAT_BA)
		return 0;
	if (ia->rq_header.ia_action != IEEE80211_ACTION_BA_ADDBA_REQUEST)
		return 0;

	/* Extract TID, return it */
	baparamset = le16toh(ia->rq_baparamset);
	*tid = (int) MS(baparamset, IEEE80211_BAPS_TID);

	return 1;
}
#undef	MS

/* Per-node software queue operations */

/*
 * Add the current packet to the given BAW.
 * It is assumed that the current packet
 *
 * + fits inside the BAW;
 * + already has had a sequence number allocated.
 *
 * Since the BAW status may be modified by both the ath task and
 * the net80211/ifnet contexts, the TID must be locked.
 */
void
ath_tx_addto_baw(struct ath_softc *sc, struct ath_node *an,
    struct ath_tid *tid, struct ath_buf *bf)
{
	int index, cindex;
	struct ieee80211_tx_ampdu *tap;

	ATH_TX_LOCK_ASSERT(sc);

	if (bf->bf_state.bfs_isretried)
		return;

	tap = ath_tx_get_tx_tid(an, tid->tid);

	if (! bf->bf_state.bfs_dobaw) {
		DPRINTF(sc, ATH_DEBUG_SW_TX_BAW,
		    "%s: dobaw=0, seqno=%d, window %d:%d\n",
		    __func__, SEQNO(bf->bf_state.bfs_seqno),
		    tap->txa_start, tap->txa_wnd);
	}

	if (bf->bf_state.bfs_addedbaw)
		DPRINTF(sc, ATH_DEBUG_SW_TX_BAW,
		    "%s: re-added? tid=%d, seqno %d; window %d:%d; "
		    "baw head=%d tail=%d\n",
		    __func__, tid->tid, SEQNO(bf->bf_state.bfs_seqno),
		    tap->txa_start, tap->txa_wnd, tid->baw_head,
		    tid->baw_tail);

	/*
	 * Verify that the given sequence number is not outside of the
	 * BAW.  Complain loudly if that's the case.
	 */
	if (! BAW_WITHIN(tap->txa_start, tap->txa_wnd,
	    SEQNO(bf->bf_state.bfs_seqno))) {
		DPRINTF(sc, ATH_DEBUG_SW_TX_BAW,
		    "%s: bf=%p: outside of BAW?? tid=%d, seqno %d; window %d:%d; "
		    "baw head=%d tail=%d\n",
		    __func__, bf, tid->tid, SEQNO(bf->bf_state.bfs_seqno),
		    tap->txa_start, tap->txa_wnd, tid->baw_head,
		    tid->baw_tail);
	}

	/*
	 * ni->ni_txseqs[] is the currently allocated seqno.
	 * the txa state contains the current baw start.
	 */
	index  = ATH_BA_INDEX(tap->txa_start, SEQNO(bf->bf_state.bfs_seqno));
	cindex = (tid->baw_head + index) & (ATH_TID_MAX_BUFS - 1);
	DPRINTF(sc, ATH_DEBUG_SW_TX_BAW,
	    "%s: tid=%d, seqno %d; window %d:%d; index=%d cindex=%d "
	    "baw head=%d tail=%d\n",
	    __func__, tid->tid, SEQNO(bf->bf_state.bfs_seqno),
	    tap->txa_start, tap->txa_wnd, index, cindex, tid->baw_head,
	    tid->baw_tail);


#if 0
	assert(tid->tx_buf[cindex] == NULL);
#endif
	if (tid->tx_buf[cindex] != NULL) {
		DPRINTF(sc, ATH_DEBUG_SW_TX_BAW,
		    "%s: ba packet dup (index=%d, cindex=%d, "
		    "head=%d, tail=%d)\n",
		    __func__, index, cindex, tid->baw_head, tid->baw_tail);
		DPRINTF(sc, ATH_DEBUG_SW_TX_BAW,
		    "%s: BA bf: %p; seqno=%d ; new bf: %p; seqno=%d\n",
		    __func__,
		    tid->tx_buf[cindex],
		    SEQNO(tid->tx_buf[cindex]->bf_state.bfs_seqno),
		    bf,
		    SEQNO(bf->bf_state.bfs_seqno)
		);
	}
	tid->tx_buf[cindex] = bf;

	if (index >= ((tid->baw_tail - tid->baw_head) &
	    (ATH_TID_MAX_BUFS - 1))) {
		tid->baw_tail = cindex;
		INCR(tid->baw_tail, ATH_TID_MAX_BUFS);
	}
}

/*
 * Flip the BAW buffer entry over from the existing one to the new one.
 *
 * When software retransmitting a (sub-)frame, it is entirely possible that
 * the frame ath_buf is marked as BUSY and can't be immediately reused.
 * In that instance the buffer is cloned and the new buffer is used for
 * retransmit. We thus need to update the ath_buf slot in the BAW buf
 * tracking array to maintain consistency.
 */
static void
ath_tx_switch_baw_buf(struct ath_softc *sc, struct ath_node *an,
    struct ath_tid *tid, struct ath_buf *old_bf, struct ath_buf *new_bf)
{
	int index, cindex;
	struct ieee80211_tx_ampdu *tap;
	int seqno = SEQNO(old_bf->bf_state.bfs_seqno);

	ATH_TX_LOCK_ASSERT(sc);

	tap = ath_tx_get_tx_tid(an, tid->tid);
	index  = ATH_BA_INDEX(tap->txa_start, seqno);
	cindex = (tid->baw_head + index) & (ATH_TID_MAX_BUFS - 1);

	/*
	 * Just warn for now; if it happens then we should find out
	 * about it. It's highly likely the aggregation session will
	 * soon hang.
	 */
	if (old_bf->bf_state.bfs_seqno != new_bf->bf_state.bfs_seqno) {
		DPRINTF(sc, ATH_DEBUG_SW_TX_BAW,
		    "%s: retransmitted buffer"
		    " has mismatching seqno's, BA session may hang.\n",
		    __func__);
		DPRINTF(sc, ATH_DEBUG_SW_TX_BAW,
		    "%s: old seqno=%d, new_seqno=%d\n", __func__,
		    old_bf->bf_state.bfs_seqno, new_bf->bf_state.bfs_seqno);
	}

	if (tid->tx_buf[cindex] != old_bf) {
		DPRINTF(sc, ATH_DEBUG_SW_TX_BAW,
		    "%s: ath_buf pointer incorrect; "
		    " has m BA session may hang.\n", __func__);
		DPRINTF(sc, ATH_DEBUG_SW_TX_BAW,
		    "%s: old bf=%p, new bf=%p\n", __func__, old_bf, new_bf);
	}

	tid->tx_buf[cindex] = new_bf;
}

/*
 * seq_start - left edge of BAW
 * seq_next - current/next sequence number to allocate
 *
 * Since the BAW status may be modified by both the ath task and
 * the net80211/ifnet contexts, the TID must be locked.
 */
static void
ath_tx_update_baw(struct ath_softc *sc, struct ath_node *an,
    struct ath_tid *tid, const struct ath_buf *bf)
{
	int index, cindex;
	struct ieee80211_tx_ampdu *tap;
	int seqno = SEQNO(bf->bf_state.bfs_seqno);

	ATH_TX_LOCK_ASSERT(sc);

	tap = ath_tx_get_tx_tid(an, tid->tid);
	index  = ATH_BA_INDEX(tap->txa_start, seqno);
	cindex = (tid->baw_head + index) & (ATH_TID_MAX_BUFS - 1);

	DPRINTF(sc, ATH_DEBUG_SW_TX_BAW,
	    "%s: tid=%d, baw=%d:%d, seqno=%d, index=%d, cindex=%d, "
	    "baw head=%d, tail=%d\n",
	    __func__, tid->tid, tap->txa_start, tap->txa_wnd, seqno, index,
	    cindex, tid->baw_head, tid->baw_tail);

	/*
	 * If this occurs then we have a big problem - something else
	 * has slid tap->txa_start along without updating the BAW
	 * tracking start/end pointers. Thus the TX BAW state is now
	 * completely busted.
	 *
	 * But for now, since I haven't yet fixed TDMA and buffer cloning,
	 * it's quite possible that a cloned buffer is making its way
	 * here and causing it to fire off. Disable TDMA for now.
	 */
	if (tid->tx_buf[cindex] != bf) {
		DPRINTF(sc, ATH_DEBUG_SW_TX_BAW,
		    "%s: comp bf=%p, seq=%d; slot bf=%p, seqno=%d\n",
		    __func__, bf, SEQNO(bf->bf_state.bfs_seqno),
		    tid->tx_buf[cindex],
		    (tid->tx_buf[cindex] != NULL) ?
		      SEQNO(tid->tx_buf[cindex]->bf_state.bfs_seqno) : -1);
	}

	tid->tx_buf[cindex] = NULL;

	while (tid->baw_head != tid->baw_tail &&
	    !tid->tx_buf[tid->baw_head]) {
		INCR(tap->txa_start, IEEE80211_SEQ_RANGE);
		INCR(tid->baw_head, ATH_TID_MAX_BUFS);
	}
	DPRINTF(sc, ATH_DEBUG_SW_TX_BAW,
	    "%s: tid=%d: baw is now %d:%d, baw head=%d\n",
	    __func__, tid->tid, tap->txa_start, tap->txa_wnd, tid->baw_head);
}

static void
ath_tx_leak_count_update(struct ath_softc *sc, struct ath_tid *tid,
    struct ath_buf *bf)
{
	struct ieee80211_frame *wh;

	ATH_TX_LOCK_ASSERT(sc);

	if (tid->an->an_leak_count > 0) {
		wh = mtod(bf->bf_m, struct ieee80211_frame *);

		/*
		 * Update MORE based on the software/net80211 queue states.
		 */
		if ((tid->an->an_stack_psq > 0)
		    || (tid->an->an_swq_depth > 0))
			wh->i_fc[1] |= IEEE80211_FC1_MORE_DATA;
		else
			wh->i_fc[1] &= ~IEEE80211_FC1_MORE_DATA;

		DPRINTF(sc, ATH_DEBUG_NODE_PWRSAVE,
		    "%s: %6D: leak count = %d, psq=%d, swq=%d, MORE=%d\n",
		    __func__,
		    tid->an->an_node.ni_macaddr,
		    ":",
		    tid->an->an_leak_count,
		    tid->an->an_stack_psq,
		    tid->an->an_swq_depth,
		    !! (wh->i_fc[1] & IEEE80211_FC1_MORE_DATA));

		/*
		 * Re-sync the underlying buffer.
		 */
		bus_dmamap_sync(sc->sc_dmat, bf->bf_dmamap,
		    BUS_DMASYNC_PREWRITE);

		tid->an->an_leak_count --;
	}
}

static int
ath_tx_tid_can_tx_or_sched(struct ath_softc *sc, struct ath_tid *tid)
{

	ATH_TX_LOCK_ASSERT(sc);

	if (tid->an->an_leak_count > 0) {
		return (1);
	}
	if (tid->paused)
		return (0);
	return (1);
}

/*
 * Mark the current node/TID as ready to TX.
 *
 * This is done to make it easy for the software scheduler to
 * find which nodes have data to send.
 *
 * The TXQ lock must be held.
 */
void
ath_tx_tid_sched(struct ath_softc *sc, struct ath_tid *tid)
{
	struct ath_txq *txq = sc->sc_ac2q[tid->ac];

	ATH_TX_LOCK_ASSERT(sc);

	/*
	 * If we are leaking out a frame to this destination
	 * for PS-POLL, ensure that we allow scheduling to
	 * occur.
	 */
	if (! ath_tx_tid_can_tx_or_sched(sc, tid))
		return;		/* paused, can't schedule yet */

	if (tid->sched)
		return;		/* already scheduled */

	tid->sched = 1;

#if 0
	/*
	 * If this is a sleeping node we're leaking to, given
	 * it a higher priority.  This is so bad for QoS it hurts.
	 */
	if (tid->an->an_leak_count) {
		TAILQ_INSERT_HEAD(&txq->axq_tidq, tid, axq_qelem);
	} else {
		TAILQ_INSERT_TAIL(&txq->axq_tidq, tid, axq_qelem);
	}
#endif

	/*
	 * We can't do the above - it'll confuse the TXQ software
	 * scheduler which will keep checking the _head_ TID
	 * in the list to see if it has traffic.  If we queue
	 * a TID to the head of the list and it doesn't transmit,
	 * we'll check it again.
	 *
	 * So, get the rest of this leaking frames support working
	 * and reliable first and _then_ optimise it so they're
	 * pushed out in front of any other pending software
	 * queued nodes.
	 */
	TAILQ_INSERT_TAIL(&txq->axq_tidq, tid, axq_qelem);
}

/*
 * Mark the current node as no longer needing to be polled for
 * TX packets.
 *
 * The TXQ lock must be held.
 */
static void
ath_tx_tid_unsched(struct ath_softc *sc, struct ath_tid *tid)
{
	struct ath_txq *txq = sc->sc_ac2q[tid->ac];

	ATH_TX_LOCK_ASSERT(sc);

	if (tid->sched == 0)
		return;

	tid->sched = 0;
	TAILQ_REMOVE(&txq->axq_tidq, tid, axq_qelem);
}

/*
 * Assign a sequence number manually to the given frame.
 *
 * This should only be called for A-MPDU TX frames.
 *
 * Note: for group addressed frames, the sequence number
 * should be from NONQOS_TID, and net80211 should have
 * already assigned it for us.
 */
static ieee80211_seq
ath_tx_tid_seqno_assign(struct ath_softc *sc, struct ieee80211_node *ni,
    struct ath_buf *bf, struct mbuf *m0)
{
	struct ieee80211_frame *wh;
	int tid;
	ieee80211_seq seqno;
	uint8_t subtype;

	wh = mtod(m0, struct ieee80211_frame *);
	tid = ieee80211_gettid(wh);

	DPRINTF(sc, ATH_DEBUG_SW_TX, "%s: tid=%d, qos has seq=%d\n",
	    __func__, tid, IEEE80211_QOS_HAS_SEQ(wh));

	/* XXX Is it a control frame? Ignore */

	/* Does the packet require a sequence number? */
	if (! IEEE80211_QOS_HAS_SEQ(wh))
		return -1;

	ATH_TX_LOCK_ASSERT(sc);

	/*
	 * Is it a QOS NULL Data frame? Give it a sequence number from
	 * the default TID (IEEE80211_NONQOS_TID.)
	 *
	 * The RX path of everything I've looked at doesn't include the NULL
	 * data frame sequence number in the aggregation state updates, so
	 * assigning it a sequence number there will cause a BAW hole on the
	 * RX side.
	 */
	subtype = wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;
	if (subtype == IEEE80211_FC0_SUBTYPE_QOS_NULL) {
		/* XXX no locking for this TID? This is a bit of a problem. */
		seqno = ni->ni_txseqs[IEEE80211_NONQOS_TID];
		INCR(ni->ni_txseqs[IEEE80211_NONQOS_TID], IEEE80211_SEQ_RANGE);
	} else if (IEEE80211_IS_MULTICAST(wh->i_addr1)) {
		/*
		 * group addressed frames get a sequence number from
		 * a different sequence number space.
		 */
		seqno = ni->ni_txseqs[IEEE80211_NONQOS_TID];
		INCR(ni->ni_txseqs[IEEE80211_NONQOS_TID], IEEE80211_SEQ_RANGE);
	} else {
		/* Manually assign sequence number */
		seqno = ni->ni_txseqs[tid];
		INCR(ni->ni_txseqs[tid], IEEE80211_SEQ_RANGE);
	}
	*(uint16_t *)&wh->i_seq[0] = htole16(seqno << IEEE80211_SEQ_SEQ_SHIFT);
	M_SEQNO_SET(m0, seqno);

	/* Return so caller can do something with it if needed */
	DPRINTF(sc, ATH_DEBUG_SW_TX,
	    "%s:  -> subtype=0x%x, tid=%d, seqno=%d\n",
	    __func__, subtype, tid, seqno);
	return seqno;
}

/*
 * Attempt to direct dispatch an aggregate frame to hardware.
 * If the frame is out of BAW, queue.
 * Otherwise, schedule it as a single frame.
 */
static void
ath_tx_xmit_aggr(struct ath_softc *sc, struct ath_node *an,
    struct ath_txq *txq, struct ath_buf *bf)
{
	struct ath_tid *tid = &an->an_tid[bf->bf_state.bfs_tid];
	struct ieee80211_tx_ampdu *tap;

	ATH_TX_LOCK_ASSERT(sc);

	tap = ath_tx_get_tx_tid(an, tid->tid);

	/* paused? queue */
	if (! ath_tx_tid_can_tx_or_sched(sc, tid)) {
		ATH_TID_INSERT_HEAD(tid, bf, bf_list);
		/* XXX don't sched - we're paused! */
		return;
	}

	/* outside baw? queue */
	if (bf->bf_state.bfs_dobaw &&
	    (! BAW_WITHIN(tap->txa_start, tap->txa_wnd,
	    SEQNO(bf->bf_state.bfs_seqno)))) {
		ATH_TID_INSERT_HEAD(tid, bf, bf_list);
		ath_tx_tid_sched(sc, tid);
		return;
	}

	/*
	 * This is a temporary check and should be removed once
	 * all the relevant code paths have been fixed.
	 *
	 * During aggregate retries, it's possible that the head
	 * frame will fail (which has the bfs_aggr and bfs_nframes
	 * fields set for said aggregate) and will be retried as
	 * a single frame.  In this instance, the values should
	 * be reset or the completion code will get upset with you.
	 */
	if (bf->bf_state.bfs_aggr != 0 || bf->bf_state.bfs_nframes > 1) {
		DPRINTF(sc, ATH_DEBUG_SW_TX_AGGR,
		    "%s: bfs_aggr=%d, bfs_nframes=%d\n", __func__,
		    bf->bf_state.bfs_aggr, bf->bf_state.bfs_nframes);
		bf->bf_state.bfs_aggr = 0;
		bf->bf_state.bfs_nframes = 1;
	}

	/* Update CLRDMASK just before this frame is queued */
	ath_tx_update_clrdmask(sc, tid, bf);

	/* Direct dispatch to hardware */
	ath_tx_do_ratelookup(sc, bf);
	ath_tx_calc_duration(sc, bf);
	ath_tx_calc_protection(sc, bf);
	ath_tx_set_rtscts(sc, bf);
	ath_tx_rate_fill_rcflags(sc, bf);
	ath_tx_setds(sc, bf);

	/* Statistics */
	sc->sc_aggr_stats.aggr_low_hwq_single_pkt++;

	/* Track per-TID hardware queue depth correctly */
	tid->hwq_depth++;

	/* Add to BAW */
	if (bf->bf_state.bfs_dobaw) {
		ath_tx_addto_baw(sc, an, tid, bf);
		bf->bf_state.bfs_addedbaw = 1;
	}

	/* Set completion handler, multi-frame aggregate or not */
	bf->bf_comp = ath_tx_aggr_comp;

	/*
	 * Update the current leak count if
	 * we're leaking frames; and set the
	 * MORE flag as appropriate.
	 */
	ath_tx_leak_count_update(sc, tid, bf);

	/* Hand off to hardware */
	ath_tx_handoff(sc, txq, bf);
}

/*
 * Attempt to send the packet.
 * If the queue isn't busy, direct-dispatch.
 * If the queue is busy enough, queue the given packet on the
 *  relevant software queue.
 */
void
ath_tx_swq(struct ath_softc *sc, struct ieee80211_node *ni,
    struct ath_txq *txq, int queue_to_head, struct ath_buf *bf)
{
	struct ath_node *an = ATH_NODE(ni);
	struct ieee80211_frame *wh;
	struct ath_tid *atid;
	int pri, tid;
	struct mbuf *m0 = bf->bf_m;

	ATH_TX_LOCK_ASSERT(sc);

	/* Fetch the TID - non-QoS frames get assigned to TID 16 */
	wh = mtod(m0, struct ieee80211_frame *);
	pri = ath_tx_getac(sc, m0);
	tid = ath_tx_gettid(sc, m0);
	atid = &an->an_tid[tid];

	DPRINTF(sc, ATH_DEBUG_SW_TX, "%s: bf=%p, pri=%d, tid=%d, qos=%d\n",
	    __func__, bf, pri, tid, IEEE80211_QOS_HAS_SEQ(wh));

	/* Set local packet state, used to queue packets to hardware */
	/* XXX potentially duplicate info, re-check */
	bf->bf_state.bfs_tid = tid;
	bf->bf_state.bfs_tx_queue = txq->axq_qnum;
	bf->bf_state.bfs_pri = pri;

	/*
	 * If the hardware queue isn't busy, queue it directly.
	 * If the hardware queue is busy, queue it.
	 * If the TID is paused or the traffic it outside BAW, software
	 * queue it.
	 *
	 * If the node is in power-save and we're leaking a frame,
	 * leak a single frame.
	 */
	if (! ath_tx_tid_can_tx_or_sched(sc, atid)) {
		/* TID is paused, queue */
		DPRINTF(sc, ATH_DEBUG_SW_TX, "%s: paused\n", __func__);
		/*
		 * If the caller requested that it be sent at a high
		 * priority, queue it at the head of the list.
		 */
		if (queue_to_head)
			ATH_TID_INSERT_HEAD(atid, bf, bf_list);
		else
			ATH_TID_INSERT_TAIL(atid, bf, bf_list);
	} else if (ath_tx_ampdu_pending(sc, an, tid)) {
		/* AMPDU pending; queue */
		DPRINTF(sc, ATH_DEBUG_SW_TX, "%s: pending\n", __func__);
		ATH_TID_INSERT_TAIL(atid, bf, bf_list);
		/* XXX sched? */
	} else if (ath_tx_ampdu_running(sc, an, tid)) {
		/*
		 * AMPDU running, queue single-frame if the hardware queue
		 * isn't busy.
		 *
		 * If the hardware queue is busy, sending an aggregate frame
		 * then just hold off so we can queue more aggregate frames.
		 *
		 * Otherwise we may end up with single frames leaking through
		 * because we are dispatching them too quickly.
		 *
		 * TODO: maybe we should treat this as two policies - minimise
		 * latency, or maximise throughput.  Then for BE/BK we can
		 * maximise throughput, and VO/VI (if AMPDU is enabled!)
		 * minimise latency.
		 */

		/*
		 * Always queue the frame to the tail of the list.
		 */
		ATH_TID_INSERT_TAIL(atid, bf, bf_list);

		/*
		 * If the hardware queue isn't busy, direct dispatch
		 * the head frame in the list.
		 *
		 * Note: if we're say, configured to do ADDBA but not A-MPDU
		 * then maybe we want to still queue two non-aggregate frames
		 * to the hardware.  Again with the per-TID policy
		 * configuration..)
		 *
		 * Otherwise, schedule the TID.
		 */
		/* XXX TXQ locking */
		if (txq->axq_depth + txq->fifo.axq_depth == 0) {

			bf = ATH_TID_FIRST(atid);
			ATH_TID_REMOVE(atid, bf, bf_list);

			/*
			 * Ensure it's definitely treated as a non-AMPDU
			 * frame - this information may have been left
			 * over from a previous attempt.
			 */
			bf->bf_state.bfs_aggr = 0;
			bf->bf_state.bfs_nframes = 1;

			/* Queue to the hardware */
			ath_tx_xmit_aggr(sc, an, txq, bf);
			DPRINTF(sc, ATH_DEBUG_SW_TX,
			    "%s: xmit_aggr\n",
			    __func__);
		} else {
			DPRINTF(sc, ATH_DEBUG_SW_TX,
			    "%s: ampdu; swq'ing\n",
			    __func__);

			ath_tx_tid_sched(sc, atid);
		}
	/*
	 * If we're not doing A-MPDU, be prepared to direct dispatch
	 * up to both limits if possible.  This particular corner
	 * case may end up with packet starvation between aggregate
	 * traffic and non-aggregate traffic: we want to ensure
	 * that non-aggregate stations get a few frames queued to the
	 * hardware before the aggregate station(s) get their chance.
	 *
	 * So if you only ever see a couple of frames direct dispatched
	 * to the hardware from a non-AMPDU client, check both here
	 * and in the software queue dispatcher to ensure that those
	 * non-AMPDU stations get a fair chance to transmit.
	 */
	/* XXX TXQ locking */
	} else if ((txq->axq_depth + txq->fifo.axq_depth < sc->sc_hwq_limit_nonaggr) &&
		    (txq->axq_aggr_depth < sc->sc_hwq_limit_aggr)) {
		/* AMPDU not running, attempt direct dispatch */
		DPRINTF(sc, ATH_DEBUG_SW_TX, "%s: xmit_normal\n", __func__);
		/* See if clrdmask needs to be set */
		ath_tx_update_clrdmask(sc, atid, bf);

		/*
		 * Update the current leak count if
		 * we're leaking frames; and set the
		 * MORE flag as appropriate.
		 */
		ath_tx_leak_count_update(sc, atid, bf);

		/*
		 * Dispatch the frame.
		 */
		ath_tx_xmit_normal(sc, txq, bf);
	} else {
		/* Busy; queue */
		DPRINTF(sc, ATH_DEBUG_SW_TX, "%s: swq'ing\n", __func__);
		ATH_TID_INSERT_TAIL(atid, bf, bf_list);
		ath_tx_tid_sched(sc, atid);
	}
}

/*
 * Only set the clrdmask bit if none of the nodes are currently
 * filtered.
 *
 * XXX TODO: go through all the callers and check to see
 * which are being called in the context of looping over all
 * TIDs (eg, if all tids are being paused, resumed, etc.)
 * That'll avoid O(n^2) complexity here.
 */
static void
ath_tx_set_clrdmask(struct ath_softc *sc, struct ath_node *an)
{
	int i;

	ATH_TX_LOCK_ASSERT(sc);

	for (i = 0; i < IEEE80211_TID_SIZE; i++) {
		if (an->an_tid[i].isfiltered == 1)
			return;
	}
	an->clrdmask = 1;
}

/*
 * Configure the per-TID node state.
 *
 * This likely belongs in if_ath_node.c but I can't think of anywhere
 * else to put it just yet.
 *
 * This sets up the SLISTs and the mutex as appropriate.
 */
void
ath_tx_tid_init(struct ath_softc *sc, struct ath_node *an)
{
	int i, j;
	struct ath_tid *atid;

	for (i = 0; i < IEEE80211_TID_SIZE; i++) {
		atid = &an->an_tid[i];

		/* XXX now with this bzer(), is the field 0'ing needed? */
		bzero(atid, sizeof(*atid));

		TAILQ_INIT(&atid->tid_q);
		TAILQ_INIT(&atid->filtq.tid_q);
		atid->tid = i;
		atid->an = an;
		for (j = 0; j < ATH_TID_MAX_BUFS; j++)
			atid->tx_buf[j] = NULL;
		atid->baw_head = atid->baw_tail = 0;
		atid->paused = 0;
		atid->sched = 0;
		atid->hwq_depth = 0;
		atid->cleanup_inprogress = 0;
		if (i == IEEE80211_NONQOS_TID)
			atid->ac = ATH_NONQOS_TID_AC;
		else
			atid->ac = TID_TO_WME_AC(i);
	}
	an->clrdmask = 1;	/* Always start by setting this bit */
}

/*
 * Pause the current TID. This stops packets from being transmitted
 * on it.
 *
 * Since this is also called from upper layers as well as the driver,
 * it will get the TID lock.
 */
static void
ath_tx_tid_pause(struct ath_softc *sc, struct ath_tid *tid)
{

	ATH_TX_LOCK_ASSERT(sc);
	tid->paused++;
	DPRINTF(sc, ATH_DEBUG_SW_TX_CTRL, "%s: [%6D]: tid=%d, paused = %d\n",
	    __func__,
	    tid->an->an_node.ni_macaddr, ":",
	    tid->tid,
	    tid->paused);
}

/*
 * Unpause the current TID, and schedule it if needed.
 */
static void
ath_tx_tid_resume(struct ath_softc *sc, struct ath_tid *tid)
{
	ATH_TX_LOCK_ASSERT(sc);

	/*
	 * There's some odd places where ath_tx_tid_resume() is called
	 * when it shouldn't be; this works around that particular issue
	 * until it's actually resolved.
	 */
	if (tid->paused == 0) {
		device_printf(sc->sc_dev,
		    "%s: [%6D]: tid=%d, paused=0?\n",
		    __func__,
		    tid->an->an_node.ni_macaddr, ":",
		    tid->tid);
	} else {
		tid->paused--;
	}

	DPRINTF(sc, ATH_DEBUG_SW_TX_CTRL,
	    "%s: [%6D]: tid=%d, unpaused = %d\n",
	    __func__,
	    tid->an->an_node.ni_macaddr, ":",
	    tid->tid,
	    tid->paused);

	if (tid->paused)
		return;

	/*
	 * Override the clrdmask configuration for the next frame
	 * from this TID, just to get the ball rolling.
	 */
	ath_tx_set_clrdmask(sc, tid->an);

	if (tid->axq_depth == 0)
		return;

	/* XXX isfiltered shouldn't ever be 0 at this point */
	if (tid->isfiltered == 1) {
		DPRINTF(sc, ATH_DEBUG_SW_TX_CTRL, "%s: filtered?!\n",
		    __func__);
		return;
	}

	ath_tx_tid_sched(sc, tid);

	/*
	 * Queue the software TX scheduler.
	 */
	ath_tx_swq_kick(sc);
}

/*
 * Add the given ath_buf to the TID filtered frame list.
 * This requires the TID be filtered.
 */
static void
ath_tx_tid_filt_addbuf(struct ath_softc *sc, struct ath_tid *tid,
    struct ath_buf *bf)
{

	ATH_TX_LOCK_ASSERT(sc);

	if (!tid->isfiltered)
		DPRINTF(sc, ATH_DEBUG_SW_TX_FILT, "%s: not filtered?!\n",
		    __func__);

	DPRINTF(sc, ATH_DEBUG_SW_TX_FILT, "%s: bf=%p\n", __func__, bf);

	/* Set the retry bit and bump the retry counter */
	ath_tx_set_retry(sc, bf);
	sc->sc_stats.ast_tx_swfiltered++;

	ATH_TID_FILT_INSERT_TAIL(tid, bf, bf_list);
}

/*
 * Handle a completed filtered frame from the given TID.
 * This just enables/pauses the filtered frame state if required
 * and appends the filtered frame to the filtered queue.
 */
static void
ath_tx_tid_filt_comp_buf(struct ath_softc *sc, struct ath_tid *tid,
    struct ath_buf *bf)
{

	ATH_TX_LOCK_ASSERT(sc);

	if (! tid->isfiltered) {
		DPRINTF(sc, ATH_DEBUG_SW_TX_FILT, "%s: tid=%d; filter transition\n",
		    __func__, tid->tid);
		tid->isfiltered = 1;
		ath_tx_tid_pause(sc, tid);
	}

	/* Add the frame to the filter queue */
	ath_tx_tid_filt_addbuf(sc, tid, bf);
}

/*
 * Complete the filtered frame TX completion.
 *
 * If there are no more frames in the hardware queue, unpause/unfilter
 * the TID if applicable.  Otherwise we will wait for a node PS transition
 * to unfilter.
 */
static void
ath_tx_tid_filt_comp_complete(struct ath_softc *sc, struct ath_tid *tid)
{
	struct ath_buf *bf;
	int do_resume = 0;

	ATH_TX_LOCK_ASSERT(sc);

	if (tid->hwq_depth != 0)
		return;

	DPRINTF(sc, ATH_DEBUG_SW_TX_FILT, "%s: tid=%d, hwq=0, transition back\n",
	    __func__, tid->tid);
	if (tid->isfiltered == 1) {
		tid->isfiltered = 0;
		do_resume = 1;
	}

	/* XXX ath_tx_tid_resume() also calls ath_tx_set_clrdmask()! */
	ath_tx_set_clrdmask(sc, tid->an);

	/* XXX this is really quite inefficient */
	while ((bf = ATH_TID_FILT_LAST(tid, ath_bufhead_s)) != NULL) {
		ATH_TID_FILT_REMOVE(tid, bf, bf_list);
		ATH_TID_INSERT_HEAD(tid, bf, bf_list);
	}

	/* And only resume if we had paused before */
	if (do_resume)
		ath_tx_tid_resume(sc, tid);
}

/*
 * Called when a single (aggregate or otherwise) frame is completed.
 *
 * Returns 0 if the buffer could be added to the filtered list
 * (cloned or otherwise), 1 if the buffer couldn't be added to the
 * filtered list (failed clone; expired retry) and the caller should
 * free it and handle it like a failure (eg by sending a BAR.)
 *
 * since the buffer may be cloned, bf must be not touched after this
 * if the return value is 0.
 */
static int
ath_tx_tid_filt_comp_single(struct ath_softc *sc, struct ath_tid *tid,
    struct ath_buf *bf)
{
	struct ath_buf *nbf;
	int retval;

	ATH_TX_LOCK_ASSERT(sc);

	/*
	 * Don't allow a filtered frame to live forever.
	 */
	if (bf->bf_state.bfs_retries > SWMAX_RETRIES) {
		sc->sc_stats.ast_tx_swretrymax++;
		DPRINTF(sc, ATH_DEBUG_SW_TX_FILT,
		    "%s: bf=%p, seqno=%d, exceeded retries\n",
		    __func__,
		    bf,
		    SEQNO(bf->bf_state.bfs_seqno));
		retval = 1; /* error */
		goto finish;
	}

	/*
	 * A busy buffer can't be added to the retry list.
	 * It needs to be cloned.
	 */
	if (bf->bf_flags & ATH_BUF_BUSY) {
		nbf = ath_tx_retry_clone(sc, tid->an, tid, bf);
		DPRINTF(sc, ATH_DEBUG_SW_TX_FILT,
		    "%s: busy buffer clone: %p -> %p\n",
		    __func__, bf, nbf);
	} else {
		nbf = bf;
	}

	if (nbf == NULL) {
		DPRINTF(sc, ATH_DEBUG_SW_TX_FILT,
		    "%s: busy buffer couldn't be cloned (%p)!\n",
		    __func__, bf);
		retval = 1; /* error */
	} else {
		ath_tx_tid_filt_comp_buf(sc, tid, nbf);
		retval = 0; /* ok */
	}
finish:
	ath_tx_tid_filt_comp_complete(sc, tid);

	return (retval);
}

static void
ath_tx_tid_filt_comp_aggr(struct ath_softc *sc, struct ath_tid *tid,
    struct ath_buf *bf_first, ath_bufhead *bf_q)
{
	struct ath_buf *bf, *bf_next, *nbf;

	ATH_TX_LOCK_ASSERT(sc);

	bf = bf_first;
	while (bf) {
		bf_next = bf->bf_next;
		bf->bf_next = NULL;	/* Remove it from the aggr list */

		/*
		 * Don't allow a filtered frame to live forever.
		 */
		if (bf->bf_state.bfs_retries > SWMAX_RETRIES) {
			sc->sc_stats.ast_tx_swretrymax++;
			DPRINTF(sc, ATH_DEBUG_SW_TX_FILT,
			    "%s: tid=%d, bf=%p, seqno=%d, exceeded retries\n",
			    __func__,
			    tid->tid,
			    bf,
			    SEQNO(bf->bf_state.bfs_seqno));
			TAILQ_INSERT_TAIL(bf_q, bf, bf_list);
			goto next;
		}

		if (bf->bf_flags & ATH_BUF_BUSY) {
			nbf = ath_tx_retry_clone(sc, tid->an, tid, bf);
			DPRINTF(sc, ATH_DEBUG_SW_TX_FILT,
			    "%s: tid=%d, busy buffer cloned: %p -> %p, seqno=%d\n",
			    __func__, tid->tid, bf, nbf, SEQNO(bf->bf_state.bfs_seqno));
		} else {
			nbf = bf;
		}

		/*
		 * If the buffer couldn't be cloned, add it to bf_q;
		 * the caller will free the buffer(s) as required.
		 */
		if (nbf == NULL) {
			DPRINTF(sc, ATH_DEBUG_SW_TX_FILT,
			    "%s: tid=%d, buffer couldn't be cloned! (%p) seqno=%d\n",
			    __func__, tid->tid, bf, SEQNO(bf->bf_state.bfs_seqno));
			TAILQ_INSERT_TAIL(bf_q, bf, bf_list);
		} else {
			ath_tx_tid_filt_comp_buf(sc, tid, nbf);
		}
next:
		bf = bf_next;
	}

	ath_tx_tid_filt_comp_complete(sc, tid);
}

/*
 * Suspend the queue because we need to TX a BAR.
 */
static void
ath_tx_tid_bar_suspend(struct ath_softc *sc, struct ath_tid *tid)
{

	ATH_TX_LOCK_ASSERT(sc);

	DPRINTF(sc, ATH_DEBUG_SW_TX_BAR,
	    "%s: tid=%d, bar_wait=%d, bar_tx=%d, called\n",
	    __func__,
	    tid->tid,
	    tid->bar_wait,
	    tid->bar_tx);

	/* We shouldn't be called when bar_tx is 1 */
	if (tid->bar_tx) {
		DPRINTF(sc, ATH_DEBUG_SW_TX_BAR,
		    "%s: bar_tx is 1?!\n", __func__);
	}

	/* If we've already been called, just be patient. */
	if (tid->bar_wait)
		return;

	/* Wait! */
	tid->bar_wait = 1;

	/* Only one pause, no matter how many frames fail */
	ath_tx_tid_pause(sc, tid);
}

/*
 * We've finished with BAR handling - either we succeeded or
 * failed. Either way, unsuspend TX.
 */
static void
ath_tx_tid_bar_unsuspend(struct ath_softc *sc, struct ath_tid *tid)
{

	ATH_TX_LOCK_ASSERT(sc);

	DPRINTF(sc, ATH_DEBUG_SW_TX_BAR,
	    "%s: %6D: TID=%d, called\n",
	    __func__,
	    tid->an->an_node.ni_macaddr,
	    ":",
	    tid->tid);

	if (tid->bar_tx == 0 || tid->bar_wait == 0) {
		DPRINTF(sc, ATH_DEBUG_SW_TX_BAR,
		    "%s: %6D: TID=%d, bar_tx=%d, bar_wait=%d: ?\n",
		    __func__, tid->an->an_node.ni_macaddr, ":",
		    tid->tid, tid->bar_tx, tid->bar_wait);
	}

	tid->bar_tx = tid->bar_wait = 0;
	ath_tx_tid_resume(sc, tid);
}

/*
 * Return whether we're ready to TX a BAR frame.
 *
 * Requires the TID lock be held.
 */
static int
ath_tx_tid_bar_tx_ready(struct ath_softc *sc, struct ath_tid *tid)
{

	ATH_TX_LOCK_ASSERT(sc);

	if (tid->bar_wait == 0 || tid->hwq_depth > 0)
		return (0);

	DPRINTF(sc, ATH_DEBUG_SW_TX_BAR,
	    "%s: %6D: TID=%d, bar ready\n",
	    __func__,
	    tid->an->an_node.ni_macaddr,
	    ":",
	    tid->tid);

	return (1);
}

/*
 * Check whether the current TID is ready to have a BAR
 * TXed and if so, do the TX.
 *
 * Since the TID/TXQ lock can't be held during a call to
 * ieee80211_send_bar(), we have to do the dirty thing of unlocking it,
 * sending the BAR and locking it again.
 *
 * Eventually, the code to send the BAR should be broken out
 * from this routine so the lock doesn't have to be reacquired
 * just to be immediately dropped by the caller.
 */
static void
ath_tx_tid_bar_tx(struct ath_softc *sc, struct ath_tid *tid)
{
	struct ieee80211_tx_ampdu *tap;

	ATH_TX_LOCK_ASSERT(sc);

	DPRINTF(sc, ATH_DEBUG_SW_TX_BAR,
	    "%s: %6D: TID=%d, called\n",
	    __func__,
	    tid->an->an_node.ni_macaddr,
	    ":",
	    tid->tid);

	tap = ath_tx_get_tx_tid(tid->an, tid->tid);

	/*
	 * This is an error condition!
	 */
	if (tid->bar_wait == 0 || tid->bar_tx == 1) {
		DPRINTF(sc, ATH_DEBUG_SW_TX_BAR,
		    "%s: %6D: TID=%d, bar_tx=%d, bar_wait=%d: ?\n",
		    __func__, tid->an->an_node.ni_macaddr, ":",
		    tid->tid, tid->bar_tx, tid->bar_wait);
		return;
	}

	/* Don't do anything if we still have pending frames */
	if (tid->hwq_depth > 0) {
		DPRINTF(sc, ATH_DEBUG_SW_TX_BAR,
		    "%s: %6D: TID=%d, hwq_depth=%d, waiting\n",
		    __func__,
		    tid->an->an_node.ni_macaddr,
		    ":",
		    tid->tid,
		    tid->hwq_depth);
		return;
	}

	/* We're now about to TX */
	tid->bar_tx = 1;

	/*
	 * Override the clrdmask configuration for the next frame,
	 * just to get the ball rolling.
	 */
	ath_tx_set_clrdmask(sc, tid->an);

	/*
	 * Calculate new BAW left edge, now that all frames have either
	 * succeeded or failed.
	 *
	 * XXX verify this is _actually_ the valid value to begin at!
	 */
	DPRINTF(sc, ATH_DEBUG_SW_TX_BAR,
	    "%s: %6D: TID=%d, new BAW left edge=%d\n",
	    __func__,
	    tid->an->an_node.ni_macaddr,
	    ":",
	    tid->tid,
	    tap->txa_start);

	/* Try sending the BAR frame */
	/* We can't hold the lock here! */

	ATH_TX_UNLOCK(sc);
	if (ieee80211_send_bar(&tid->an->an_node, tap, tap->txa_start) == 0) {
		/* Success? Now we wait for notification that it's done */
		ATH_TX_LOCK(sc);
		return;
	}

	/* Failure? For now, warn loudly and continue */
	ATH_TX_LOCK(sc);
	DPRINTF(sc, ATH_DEBUG_SW_TX_BAR,
	    "%s: %6D: TID=%d, failed to TX BAR, continue!\n",
	    __func__, tid->an->an_node.ni_macaddr, ":",
	    tid->tid);
	ath_tx_tid_bar_unsuspend(sc, tid);
}

static void
ath_tx_tid_drain_pkt(struct ath_softc *sc, struct ath_node *an,
    struct ath_tid *tid, ath_bufhead *bf_cq, struct ath_buf *bf)
{

	ATH_TX_LOCK_ASSERT(sc);

	/*
	 * If the current TID is running AMPDU, update
	 * the BAW.
	 */
	if (ath_tx_ampdu_running(sc, an, tid->tid) &&
	    bf->bf_state.bfs_dobaw) {
		/*
		 * Only remove the frame from the BAW if it's
		 * been transmitted at least once; this means
		 * the frame was in the BAW to begin with.
		 */
		if (bf->bf_state.bfs_retries > 0) {
			ath_tx_update_baw(sc, an, tid, bf);
			bf->bf_state.bfs_dobaw = 0;
		}
#if 0
		/*
		 * This has become a non-fatal error now
		 */
		if (! bf->bf_state.bfs_addedbaw)
			DPRINTF(sc, ATH_DEBUG_SW_TX_BAW
			    "%s: wasn't added: seqno %d\n",
			    __func__, SEQNO(bf->bf_state.bfs_seqno));
#endif
	}

	/* Strip it out of an aggregate list if it was in one */
	bf->bf_next = NULL;

	/* Insert on the free queue to be freed by the caller */
	TAILQ_INSERT_TAIL(bf_cq, bf, bf_list);
}

static void
ath_tx_tid_drain_print(struct ath_softc *sc, struct ath_node *an,
    const char *pfx, struct ath_tid *tid, struct ath_buf *bf)
{
	struct ieee80211_node *ni = &an->an_node;
	struct ath_txq *txq;
	struct ieee80211_tx_ampdu *tap;

	txq = sc->sc_ac2q[tid->ac];
	tap = ath_tx_get_tx_tid(an, tid->tid);

	DPRINTF(sc, ATH_DEBUG_SW_TX | ATH_DEBUG_RESET,
	    "%s: %s: %6D: bf=%p: addbaw=%d, dobaw=%d, "
	    "seqno=%d, retry=%d\n",
	    __func__,
	    pfx,
	    ni->ni_macaddr,
	    ":",
	    bf,
	    bf->bf_state.bfs_addedbaw,
	    bf->bf_state.bfs_dobaw,
	    SEQNO(bf->bf_state.bfs_seqno),
	    bf->bf_state.bfs_retries);
	DPRINTF(sc, ATH_DEBUG_SW_TX | ATH_DEBUG_RESET,
	    "%s: %s: %6D: bf=%p: txq[%d] axq_depth=%d, axq_aggr_depth=%d\n",
	    __func__,
	    pfx,
	    ni->ni_macaddr,
	    ":",
	    bf,
	    txq->axq_qnum,
	    txq->axq_depth,
	    txq->axq_aggr_depth);
	DPRINTF(sc, ATH_DEBUG_SW_TX | ATH_DEBUG_RESET,
	    "%s: %s: %6D: bf=%p: tid txq_depth=%d hwq_depth=%d, bar_wait=%d, "
	      "isfiltered=%d\n",
	    __func__,
	    pfx,
	    ni->ni_macaddr,
	    ":",
	    bf,
	    tid->axq_depth,
	    tid->hwq_depth,
	    tid->bar_wait,
	    tid->isfiltered);
	DPRINTF(sc, ATH_DEBUG_SW_TX | ATH_DEBUG_RESET,
	    "%s: %s: %6D: tid %d: "
	    "sched=%d, paused=%d, "
	    "incomp=%d, baw_head=%d, "
	    "baw_tail=%d txa_start=%d, ni_txseqs=%d\n",
	     __func__,
	     pfx,
	     ni->ni_macaddr,
	     ":",
	     tid->tid,
	     tid->sched, tid->paused,
	     tid->incomp, tid->baw_head,
	     tid->baw_tail, tap == NULL ? -1 : tap->txa_start,
	     ni->ni_txseqs[tid->tid]);

	/* XXX Dump the frame, see what it is? */
	if (IFF_DUMPPKTS(sc, ATH_DEBUG_XMIT))
		ieee80211_dump_pkt(ni->ni_ic,
		    mtod(bf->bf_m, const uint8_t *),
		    bf->bf_m->m_len, 0, -1);
}

/*
 * Free any packets currently pending in the software TX queue.
 *
 * This will be called when a node is being deleted.
 *
 * It can also be called on an active node during an interface
 * reset or state transition.
 *
 * (From Linux/reference):
 *
 * TODO: For frame(s) that are in the retry state, we will reuse the
 * sequence number(s) without setting the retry bit. The
 * alternative is to give up on these and BAR the receiver's window
 * forward.
 */
static void
ath_tx_tid_drain(struct ath_softc *sc, struct ath_node *an,
    struct ath_tid *tid, ath_bufhead *bf_cq)
{
	struct ath_buf *bf;
	struct ieee80211_tx_ampdu *tap;
	struct ieee80211_node *ni = &an->an_node;
	int t;

	tap = ath_tx_get_tx_tid(an, tid->tid);

	ATH_TX_LOCK_ASSERT(sc);

	/* Walk the queue, free frames */
	t = 0;
	for (;;) {
		bf = ATH_TID_FIRST(tid);
		if (bf == NULL) {
			break;
		}

		if (t == 0) {
			ath_tx_tid_drain_print(sc, an, "norm", tid, bf);
//			t = 1;
		}

		ATH_TID_REMOVE(tid, bf, bf_list);
		ath_tx_tid_drain_pkt(sc, an, tid, bf_cq, bf);
	}

	/* And now, drain the filtered frame queue */
	t = 0;
	for (;;) {
		bf = ATH_TID_FILT_FIRST(tid);
		if (bf == NULL)
			break;

		if (t == 0) {
			ath_tx_tid_drain_print(sc, an, "filt", tid, bf);
//			t = 1;
		}

		ATH_TID_FILT_REMOVE(tid, bf, bf_list);
		ath_tx_tid_drain_pkt(sc, an, tid, bf_cq, bf);
	}

	/*
	 * Override the clrdmask configuration for the next frame
	 * in case there is some future transmission, just to get
	 * the ball rolling.
	 *
	 * This won't hurt things if the TID is about to be freed.
	 */
	ath_tx_set_clrdmask(sc, tid->an);

	/*
	 * Now that it's completed, grab the TID lock and update
	 * the sequence number and BAW window.
	 * Because sequence numbers have been assigned to frames
	 * that haven't been sent yet, it's entirely possible
	 * we'll be called with some pending frames that have not
	 * been transmitted.
	 *
	 * The cleaner solution is to do the sequence number allocation
	 * when the packet is first transmitted - and thus the "retries"
	 * check above would be enough to update the BAW/seqno.
	 */

	/* But don't do it for non-QoS TIDs */
	if (tap) {
#if 1
		DPRINTF(sc, ATH_DEBUG_SW_TX_CTRL,
		    "%s: %6D: node %p: TID %d: sliding BAW left edge to %d\n",
		    __func__,
		    ni->ni_macaddr,
		    ":",
		    an,
		    tid->tid,
		    tap->txa_start);
#endif
		ni->ni_txseqs[tid->tid] = tap->txa_start;
		tid->baw_tail = tid->baw_head;
	}
}

/*
 * Reset the TID state.  This must be only called once the node has
 * had its frames flushed from this TID, to ensure that no other
 * pause / unpause logic can kick in.
 */
static void
ath_tx_tid_reset(struct ath_softc *sc, struct ath_tid *tid)
{

#if 0
	tid->bar_wait = tid->bar_tx = tid->isfiltered = 0;
	tid->paused = tid->sched = tid->addba_tx_pending = 0;
	tid->incomp = tid->cleanup_inprogress = 0;
#endif

	/*
	 * If we have a bar_wait set, we need to unpause the TID
	 * here.  Otherwise once cleanup has finished, the TID won't
	 * have the right paused counter.
	 *
	 * XXX I'm not going through resume here - I don't want the
	 * node to be rescheuled just yet.  This however should be
	 * methodized!
	 */
	if (tid->bar_wait) {
		if (tid->paused > 0) {
			tid->paused --;
		}
	}

	/*
	 * XXX same with a currently filtered TID.
	 *
	 * Since this is being called during a flush, we assume that
	 * the filtered frame list is actually empty.
	 *
	 * XXX TODO: add in a check to ensure that the filtered queue
	 * depth is actually 0!
	 */
	if (tid->isfiltered) {
		if (tid->paused > 0) {
			tid->paused --;
		}
	}

	/*
	 * Clear BAR, filtered frames, scheduled and ADDBA pending.
	 * The TID may be going through cleanup from the last association
	 * where things in the BAW are still in the hardware queue.
	 */
	tid->bar_wait = 0;
	tid->bar_tx = 0;
	tid->isfiltered = 0;
	tid->sched = 0;
	tid->addba_tx_pending = 0;

	/*
	 * XXX TODO: it may just be enough to walk the HWQs and mark
	 * frames for that node as non-aggregate; or mark the ath_node
	 * with something that indicates that aggregation is no longer
	 * occurring.  Then we can just toss the BAW complaints and
	 * do a complete hard reset of state here - no pause, no
	 * complete counter, etc.
	 */

}

/*
 * Flush all software queued packets for the given node.
 *
 * This occurs when a completion handler frees the last buffer
 * for a node, and the node is thus freed. This causes the node
 * to be cleaned up, which ends up calling ath_tx_node_flush.
 */
void
ath_tx_node_flush(struct ath_softc *sc, struct ath_node *an)
{
	int tid;
	ath_bufhead bf_cq;
	struct ath_buf *bf;

	TAILQ_INIT(&bf_cq);

	ATH_KTR(sc, ATH_KTR_NODE, 1, "ath_tx_node_flush: flush node; ni=%p",
	    &an->an_node);

	ATH_TX_LOCK(sc);
	DPRINTF(sc, ATH_DEBUG_NODE,
	    "%s: %6D: flush; is_powersave=%d, stack_psq=%d, tim=%d, "
	    "swq_depth=%d, clrdmask=%d, leak_count=%d\n",
	    __func__,
	    an->an_node.ni_macaddr,
	    ":",
	    an->an_is_powersave,
	    an->an_stack_psq,
	    an->an_tim_set,
	    an->an_swq_depth,
	    an->clrdmask,
	    an->an_leak_count);

	for (tid = 0; tid < IEEE80211_TID_SIZE; tid++) {
		struct ath_tid *atid = &an->an_tid[tid];

		/* Free packets */
		ath_tx_tid_drain(sc, an, atid, &bf_cq);

		/* Remove this tid from the list of active tids */
		ath_tx_tid_unsched(sc, atid);

		/* Reset the per-TID pause, BAR, etc state */
		ath_tx_tid_reset(sc, atid);
	}

	/*
	 * Clear global leak count
	 */
	an->an_leak_count = 0;
	ATH_TX_UNLOCK(sc);

	/* Handle completed frames */
	while ((bf = TAILQ_FIRST(&bf_cq)) != NULL) {
		TAILQ_REMOVE(&bf_cq, bf, bf_list);
		ath_tx_default_comp(sc, bf, 0);
	}
}

/*
 * Drain all the software TXQs currently with traffic queued.
 */
void
ath_tx_txq_drain(struct ath_softc *sc, struct ath_txq *txq)
{
	struct ath_tid *tid;
	ath_bufhead bf_cq;
	struct ath_buf *bf;

	TAILQ_INIT(&bf_cq);
	ATH_TX_LOCK(sc);

	/*
	 * Iterate over all active tids for the given txq,
	 * flushing and unsched'ing them
	 */
	while (! TAILQ_EMPTY(&txq->axq_tidq)) {
		tid = TAILQ_FIRST(&txq->axq_tidq);
		ath_tx_tid_drain(sc, tid->an, tid, &bf_cq);
		ath_tx_tid_unsched(sc, tid);
	}

	ATH_TX_UNLOCK(sc);

	while ((bf = TAILQ_FIRST(&bf_cq)) != NULL) {
		TAILQ_REMOVE(&bf_cq, bf, bf_list);
		ath_tx_default_comp(sc, bf, 0);
	}
}

/*
 * Handle completion of non-aggregate session frames.
 *
 * This (currently) doesn't implement software retransmission of
 * non-aggregate frames!
 *
 * Software retransmission of non-aggregate frames needs to obey
 * the strict sequence number ordering, and drop any frames that
 * will fail this.
 *
 * For now, filtered frames and frame transmission will cause
 * all kinds of issues.  So we don't support them.
 *
 * So anyone queuing frames via ath_tx_normal_xmit() or
 * ath_tx_hw_queue_norm() must override and set CLRDMASK.
 */
void
ath_tx_normal_comp(struct ath_softc *sc, struct ath_buf *bf, int fail)
{
	struct ieee80211_node *ni = bf->bf_node;
	struct ath_node *an = ATH_NODE(ni);
	int tid = bf->bf_state.bfs_tid;
	struct ath_tid *atid = &an->an_tid[tid];
	struct ath_tx_status *ts = &bf->bf_status.ds_txstat;

	/* The TID state is protected behind the TXQ lock */
	ATH_TX_LOCK(sc);

	DPRINTF(sc, ATH_DEBUG_SW_TX, "%s: bf=%p: fail=%d, hwq_depth now %d\n",
	    __func__, bf, fail, atid->hwq_depth - 1);

	atid->hwq_depth--;

#if 0
	/*
	 * If the frame was filtered, stick it on the filter frame
	 * queue and complain about it.  It shouldn't happen!
	 */
	if ((ts->ts_status & HAL_TXERR_FILT) ||
	    (ts->ts_status != 0 && atid->isfiltered)) {
		DPRINTF(sc, ATH_DEBUG_SW_TX,
		    "%s: isfiltered=%d, ts_status=%d: huh?\n",
		    __func__,
		    atid->isfiltered,
		    ts->ts_status);
		ath_tx_tid_filt_comp_buf(sc, atid, bf);
	}
#endif
	if (atid->isfiltered)
		DPRINTF(sc, ATH_DEBUG_SW_TX, "%s: filtered?!\n", __func__);
	if (atid->hwq_depth < 0)
		DPRINTF(sc, ATH_DEBUG_SW_TX, "%s: hwq_depth < 0: %d\n",
		    __func__, atid->hwq_depth);

	/* If the TID is being cleaned up, track things */
	/* XXX refactor! */
	if (atid->cleanup_inprogress) {
		atid->incomp--;
		if (atid->incomp == 0) {
			DPRINTF(sc, ATH_DEBUG_SW_TX_CTRL,
			    "%s: TID %d: cleaned up! resume!\n",
			    __func__, tid);
			atid->cleanup_inprogress = 0;
			ath_tx_tid_resume(sc, atid);
		}
	}

	/*
	 * If the queue is filtered, potentially mark it as complete
	 * and reschedule it as needed.
	 *
	 * This is required as there may be a subsequent TX descriptor
	 * for this end-node that has CLRDMASK set, so it's quite possible
	 * that a filtered frame will be followed by a non-filtered
	 * (complete or otherwise) frame.
	 *
	 * XXX should we do this before we complete the frame?
	 */
	if (atid->isfiltered)
		ath_tx_tid_filt_comp_complete(sc, atid);
	ATH_TX_UNLOCK(sc);

	/*
	 * punt to rate control if we're not being cleaned up
	 * during a hw queue drain and the frame wanted an ACK.
	 */
	if (fail == 0 && ((bf->bf_state.bfs_txflags & HAL_TXDESC_NOACK) == 0))
		ath_tx_update_ratectrl(sc, ni, bf->bf_state.bfs_rc,
		    ts, bf->bf_state.bfs_pktlen,
		    1, (ts->ts_status == 0) ? 0 : 1);

	ath_tx_default_comp(sc, bf, fail);
}

/*
 * Handle cleanup of aggregate session packets that aren't
 * an A-MPDU.
 *
 * There's no need to update the BAW here - the session is being
 * torn down.
 */
static void
ath_tx_comp_cleanup_unaggr(struct ath_softc *sc, struct ath_buf *bf)
{
	struct ieee80211_node *ni = bf->bf_node;
	struct ath_node *an = ATH_NODE(ni);
	int tid = bf->bf_state.bfs_tid;
	struct ath_tid *atid = &an->an_tid[tid];

	DPRINTF(sc, ATH_DEBUG_SW_TX_CTRL, "%s: TID %d: incomp=%d\n",
	    __func__, tid, atid->incomp);

	ATH_TX_LOCK(sc);
	atid->incomp--;

	/* XXX refactor! */
	if (bf->bf_state.bfs_dobaw) {
		ath_tx_update_baw(sc, an, atid, bf);
		if (!bf->bf_state.bfs_addedbaw)
			DPRINTF(sc, ATH_DEBUG_SW_TX,
			    "%s: wasn't added: seqno %d\n",
			    __func__, SEQNO(bf->bf_state.bfs_seqno));
	}

	if (atid->incomp == 0) {
		DPRINTF(sc, ATH_DEBUG_SW_TX_CTRL,
		    "%s: TID %d: cleaned up! resume!\n",
		    __func__, tid);
		atid->cleanup_inprogress = 0;
		ath_tx_tid_resume(sc, atid);
	}
	ATH_TX_UNLOCK(sc);

	ath_tx_default_comp(sc, bf, 0);
}


/*
 * This as it currently stands is a bit dumb.  Ideally we'd just
 * fail the frame the normal way and have it permanently fail
 * via the normal aggregate completion path.
 */
static void
ath_tx_tid_cleanup_frame(struct ath_softc *sc, struct ath_node *an,
    int tid, struct ath_buf *bf_head, ath_bufhead *bf_cq)
{
	struct ath_tid *atid = &an->an_tid[tid];
	struct ath_buf *bf, *bf_next;

	ATH_TX_LOCK_ASSERT(sc);

	/*
	 * Remove this frame from the queue.
	 */
	ATH_TID_REMOVE(atid, bf_head, bf_list);

	/*
	 * Loop over all the frames in the aggregate.
	 */
	bf = bf_head;
	while (bf != NULL) {
		bf_next = bf->bf_next;	/* next aggregate frame, or NULL */

		/*
		 * If it's been added to the BAW we need to kick
		 * it out of the BAW before we continue.
		 *
		 * XXX if it's an aggregate, assert that it's in the
		 * BAW - we shouldn't have it be in an aggregate
		 * otherwise!
		 */
		if (bf->bf_state.bfs_addedbaw) {
			ath_tx_update_baw(sc, an, atid, bf);
			bf->bf_state.bfs_dobaw = 0;
		}

		/*
		 * Give it the default completion handler.
		 */
		bf->bf_comp = ath_tx_normal_comp;
		bf->bf_next = NULL;

		/*
		 * Add it to the list to free.
		 */
		TAILQ_INSERT_TAIL(bf_cq, bf, bf_list);

		/*
		 * Now advance to the next frame in the aggregate.
		 */
		bf = bf_next;
	}
}

/*
 * Performs transmit side cleanup when TID changes from aggregated to
 * unaggregated and during reassociation.
 *
 * For now, this just tosses everything from the TID software queue
 * whether or not it has been retried and marks the TID as
 * pending completion if there's anything for this TID queued to
 * the hardware.
 *
 * The caller is responsible for pausing the TID and unpausing the
 * TID if no cleanup was required. Otherwise the cleanup path will
 * unpause the TID once the last hardware queued frame is completed.
 */
static void
ath_tx_tid_cleanup(struct ath_softc *sc, struct ath_node *an, int tid,
    ath_bufhead *bf_cq)
{
	struct ath_tid *atid = &an->an_tid[tid];
	struct ath_buf *bf, *bf_next;

	ATH_TX_LOCK_ASSERT(sc);

	DPRINTF(sc, ATH_DEBUG_SW_TX_BAW,
	    "%s: TID %d: called; inprogress=%d\n", __func__, tid,
	    atid->cleanup_inprogress);

	/*
	 * Move the filtered frames to the TX queue, before
	 * we run off and discard/process things.
	 */

	/* XXX this is really quite inefficient */
	while ((bf = ATH_TID_FILT_LAST(atid, ath_bufhead_s)) != NULL) {
		ATH_TID_FILT_REMOVE(atid, bf, bf_list);
		ATH_TID_INSERT_HEAD(atid, bf, bf_list);
	}

	/*
	 * Update the frames in the software TX queue:
	 *
	 * + Discard retry frames in the queue
	 * + Fix the completion function to be non-aggregate
	 */
	bf = ATH_TID_FIRST(atid);
	while (bf) {
		/*
		 * Grab the next frame in the list, we may
		 * be fiddling with the list.
		 */
		bf_next = TAILQ_NEXT(bf, bf_list);

		/*
		 * Free the frame and all subframes.
		 */
		ath_tx_tid_cleanup_frame(sc, an, tid, bf, bf_cq);

		/*
		 * Next frame!
		 */
		bf = bf_next;
	}

	/*
	 * If there's anything in the hardware queue we wait
	 * for the TID HWQ to empty.
	 */
	if (atid->hwq_depth > 0) {
		/*
		 * XXX how about we kill atid->incomp, and instead
		 * replace it with a macro that checks that atid->hwq_depth
		 * is 0?
		 */
		atid->incomp = atid->hwq_depth;
		atid->cleanup_inprogress = 1;
	}

	if (atid->cleanup_inprogress)
		DPRINTF(sc, ATH_DEBUG_SW_TX_CTRL,
		    "%s: TID %d: cleanup needed: %d packets\n",
		    __func__, tid, atid->incomp);

	/* Owner now must free completed frames */
}

static struct ath_buf *
ath_tx_retry_clone(struct ath_softc *sc, struct ath_node *an,
    struct ath_tid *tid, struct ath_buf *bf)
{
	struct ath_buf *nbf;
	int error;

	/*
	 * Clone the buffer.  This will handle the dma unmap and
	 * copy the node reference to the new buffer.  If this
	 * works out, 'bf' will have no DMA mapping, no mbuf
	 * pointer and no node reference.
	 */
	nbf = ath_buf_clone(sc, bf);

#if 0
	DPRINTF(sc, ATH_DEBUG_XMIT, "%s: ATH_BUF_BUSY; cloning\n",
	    __func__);
#endif

	if (nbf == NULL) {
		/* Failed to clone */
		DPRINTF(sc, ATH_DEBUG_XMIT,
		    "%s: failed to clone a busy buffer\n",
		    __func__);
		return NULL;
	}

	/* Setup the dma for the new buffer */
	error = ath_tx_dmasetup(sc, nbf, nbf->bf_m);
	if (error != 0) {
		DPRINTF(sc, ATH_DEBUG_XMIT,
		    "%s: failed to setup dma for clone\n",
		    __func__);
		/*
		 * Put this at the head of the list, not tail;
		 * that way it doesn't interfere with the
		 * busy buffer logic (which uses the tail of
		 * the list.)
		 */
		ATH_TXBUF_LOCK(sc);
		ath_returnbuf_head(sc, nbf);
		ATH_TXBUF_UNLOCK(sc);
		return NULL;
	}

	/* Update BAW if required, before we free the original buf */
	if (bf->bf_state.bfs_dobaw)
		ath_tx_switch_baw_buf(sc, an, tid, bf, nbf);

	/* Free original buffer; return new buffer */
	ath_freebuf(sc, bf);

	return nbf;
}

/*
 * Handle retrying an unaggregate frame in an aggregate
 * session.
 *
 * If too many retries occur, pause the TID, wait for
 * any further retransmits (as there's no reason why
 * non-aggregate frames in an aggregate session are
 * transmitted in-order; they just have to be in-BAW)
 * and then queue a BAR.
 */
static void
ath_tx_aggr_retry_unaggr(struct ath_softc *sc, struct ath_buf *bf)
{
	struct ieee80211_node *ni = bf->bf_node;
	struct ath_node *an = ATH_NODE(ni);
	int tid = bf->bf_state.bfs_tid;
	struct ath_tid *atid = &an->an_tid[tid];
	struct ieee80211_tx_ampdu *tap;

	ATH_TX_LOCK(sc);

	tap = ath_tx_get_tx_tid(an, tid);

	/*
	 * If the buffer is marked as busy, we can't directly
	 * reuse it. Instead, try to clone the buffer.
	 * If the clone is successful, recycle the old buffer.
	 * If the clone is unsuccessful, set bfs_retries to max
	 * to force the next bit of code to free the buffer
	 * for us.
	 */
	if ((bf->bf_state.bfs_retries < SWMAX_RETRIES) &&
	    (bf->bf_flags & ATH_BUF_BUSY)) {
		struct ath_buf *nbf;
		nbf = ath_tx_retry_clone(sc, an, atid, bf);
		if (nbf)
			/* bf has been freed at this point */
			bf = nbf;
		else
			bf->bf_state.bfs_retries = SWMAX_RETRIES + 1;
	}

	if (bf->bf_state.bfs_retries >= SWMAX_RETRIES) {
		DPRINTF(sc, ATH_DEBUG_SW_TX_RETRIES,
		    "%s: exceeded retries; seqno %d\n",
		    __func__, SEQNO(bf->bf_state.bfs_seqno));
		sc->sc_stats.ast_tx_swretrymax++;

		/* Update BAW anyway */
		if (bf->bf_state.bfs_dobaw) {
			ath_tx_update_baw(sc, an, atid, bf);
			if (! bf->bf_state.bfs_addedbaw)
				DPRINTF(sc, ATH_DEBUG_SW_TX_BAW,
				    "%s: wasn't added: seqno %d\n",
				    __func__, SEQNO(bf->bf_state.bfs_seqno));
		}
		bf->bf_state.bfs_dobaw = 0;

		/* Suspend the TX queue and get ready to send the BAR */
		ath_tx_tid_bar_suspend(sc, atid);

		/* Send the BAR if there are no other frames waiting */
		if (ath_tx_tid_bar_tx_ready(sc, atid))
			ath_tx_tid_bar_tx(sc, atid);

		ATH_TX_UNLOCK(sc);

		/* Free buffer, bf is free after this call */
		ath_tx_default_comp(sc, bf, 0);
		return;
	}

	/*
	 * This increments the retry counter as well as
	 * sets the retry flag in the ath_buf and packet
	 * body.
	 */
	ath_tx_set_retry(sc, bf);
	sc->sc_stats.ast_tx_swretries++;

	/*
	 * Insert this at the head of the queue, so it's
	 * retried before any current/subsequent frames.
	 */
	ATH_TID_INSERT_HEAD(atid, bf, bf_list);
	ath_tx_tid_sched(sc, atid);
	/* Send the BAR if there are no other frames waiting */
	if (ath_tx_tid_bar_tx_ready(sc, atid))
		ath_tx_tid_bar_tx(sc, atid);

	ATH_TX_UNLOCK(sc);
}

/*
 * Common code for aggregate excessive retry/subframe retry.
 * If retrying, queues buffers to bf_q. If not, frees the
 * buffers.
 *
 * XXX should unify this with ath_tx_aggr_retry_unaggr()
 */
static int
ath_tx_retry_subframe(struct ath_softc *sc, struct ath_buf *bf,
    ath_bufhead *bf_q)
{
	struct ieee80211_node *ni = bf->bf_node;
	struct ath_node *an = ATH_NODE(ni);
	int tid = bf->bf_state.bfs_tid;
	struct ath_tid *atid = &an->an_tid[tid];

	ATH_TX_LOCK_ASSERT(sc);

	/* XXX clr11naggr should be done for all subframes */
	ath_hal_clr11n_aggr(sc->sc_ah, bf->bf_desc);
	ath_hal_set11nburstduration(sc->sc_ah, bf->bf_desc, 0);

	/* ath_hal_set11n_virtualmorefrag(sc->sc_ah, bf->bf_desc, 0); */

	/*
	 * If the buffer is marked as busy, we can't directly
	 * reuse it. Instead, try to clone the buffer.
	 * If the clone is successful, recycle the old buffer.
	 * If the clone is unsuccessful, set bfs_retries to max
	 * to force the next bit of code to free the buffer
	 * for us.
	 */
	if ((bf->bf_state.bfs_retries < SWMAX_RETRIES) &&
	    (bf->bf_flags & ATH_BUF_BUSY)) {
		struct ath_buf *nbf;
		nbf = ath_tx_retry_clone(sc, an, atid, bf);
		if (nbf)
			/* bf has been freed at this point */
			bf = nbf;
		else
			bf->bf_state.bfs_retries = SWMAX_RETRIES + 1;
	}

	if (bf->bf_state.bfs_retries >= SWMAX_RETRIES) {
		sc->sc_stats.ast_tx_swretrymax++;
		DPRINTF(sc, ATH_DEBUG_SW_TX_RETRIES,
		    "%s: max retries: seqno %d\n",
		    __func__, SEQNO(bf->bf_state.bfs_seqno));
		ath_tx_update_baw(sc, an, atid, bf);
		if (!bf->bf_state.bfs_addedbaw)
			DPRINTF(sc, ATH_DEBUG_SW_TX_BAW,
			    "%s: wasn't added: seqno %d\n",
			    __func__, SEQNO(bf->bf_state.bfs_seqno));
		bf->bf_state.bfs_dobaw = 0;
		return 1;
	}

	ath_tx_set_retry(sc, bf);
	sc->sc_stats.ast_tx_swretries++;
	bf->bf_next = NULL;		/* Just to make sure */

	/* Clear the aggregate state */
	bf->bf_state.bfs_aggr = 0;
	bf->bf_state.bfs_ndelim = 0;	/* ??? needed? */
	bf->bf_state.bfs_nframes = 1;

	TAILQ_INSERT_TAIL(bf_q, bf, bf_list);
	return 0;
}

/*
 * error pkt completion for an aggregate destination
 */
static void
ath_tx_comp_aggr_error(struct ath_softc *sc, struct ath_buf *bf_first,
    struct ath_tid *tid)
{
	struct ieee80211_node *ni = bf_first->bf_node;
	struct ath_node *an = ATH_NODE(ni);
	struct ath_buf *bf_next, *bf;
	ath_bufhead bf_q;
	int drops = 0;
	struct ieee80211_tx_ampdu *tap;
	ath_bufhead bf_cq;

	TAILQ_INIT(&bf_q);
	TAILQ_INIT(&bf_cq);

	/*
	 * Update rate control - all frames have failed.
	 *
	 * XXX use the length in the first frame in the series;
	 * XXX just so things are consistent for now.
	 */
	ath_tx_update_ratectrl(sc, ni, bf_first->bf_state.bfs_rc,
	    &bf_first->bf_status.ds_txstat,
	    bf_first->bf_state.bfs_pktlen,
	    bf_first->bf_state.bfs_nframes, bf_first->bf_state.bfs_nframes);

	ATH_TX_LOCK(sc);
	tap = ath_tx_get_tx_tid(an, tid->tid);
	sc->sc_stats.ast_tx_aggr_failall++;

	/* Retry all subframes */
	bf = bf_first;
	while (bf) {
		bf_next = bf->bf_next;
		bf->bf_next = NULL;	/* Remove it from the aggr list */
		sc->sc_stats.ast_tx_aggr_fail++;
		if (ath_tx_retry_subframe(sc, bf, &bf_q)) {
			drops++;
			bf->bf_next = NULL;
			TAILQ_INSERT_TAIL(&bf_cq, bf, bf_list);
		}
		bf = bf_next;
	}

	/* Prepend all frames to the beginning of the queue */
	while ((bf = TAILQ_LAST(&bf_q, ath_bufhead_s)) != NULL) {
		TAILQ_REMOVE(&bf_q, bf, bf_list);
		ATH_TID_INSERT_HEAD(tid, bf, bf_list);
	}

	/*
	 * Schedule the TID to be re-tried.
	 */
	ath_tx_tid_sched(sc, tid);

	/*
	 * send bar if we dropped any frames
	 *
	 * Keep the txq lock held for now, as we need to ensure
	 * that ni_txseqs[] is consistent (as it's being updated
	 * in the ifnet TX context or raw TX context.)
	 */
	if (drops) {
		/* Suspend the TX queue and get ready to send the BAR */
		ath_tx_tid_bar_suspend(sc, tid);
	}

	/*
	 * Send BAR if required
	 */
	if (ath_tx_tid_bar_tx_ready(sc, tid))
		ath_tx_tid_bar_tx(sc, tid);

	ATH_TX_UNLOCK(sc);

	/* Complete frames which errored out */
	while ((bf = TAILQ_FIRST(&bf_cq)) != NULL) {
		TAILQ_REMOVE(&bf_cq, bf, bf_list);
		ath_tx_default_comp(sc, bf, 0);
	}
}

/*
 * Handle clean-up of packets from an aggregate list.
 *
 * There's no need to update the BAW here - the session is being
 * torn down.
 */
static void
ath_tx_comp_cleanup_aggr(struct ath_softc *sc, struct ath_buf *bf_first)
{
	struct ath_buf *bf, *bf_next;
	struct ieee80211_node *ni = bf_first->bf_node;
	struct ath_node *an = ATH_NODE(ni);
	int tid = bf_first->bf_state.bfs_tid;
	struct ath_tid *atid = &an->an_tid[tid];

	ATH_TX_LOCK(sc);

	/* update incomp */
	atid->incomp--;

	/* Update the BAW */
	bf = bf_first;
	while (bf) {
		/* XXX refactor! */
		if (bf->bf_state.bfs_dobaw) {
			ath_tx_update_baw(sc, an, atid, bf);
			if (!bf->bf_state.bfs_addedbaw)
				DPRINTF(sc, ATH_DEBUG_SW_TX,
				    "%s: wasn't added: seqno %d\n",
				    __func__, SEQNO(bf->bf_state.bfs_seqno));
		}
		bf = bf->bf_next;
	}

	if (atid->incomp == 0) {
		DPRINTF(sc, ATH_DEBUG_SW_TX_CTRL,
		    "%s: TID %d: cleaned up! resume!\n",
		    __func__, tid);
		atid->cleanup_inprogress = 0;
		ath_tx_tid_resume(sc, atid);
	}

	/* Send BAR if required */
	/* XXX why would we send a BAR when transitioning to non-aggregation? */
	/*
	 * XXX TODO: we should likely just tear down the BAR state here,
	 * rather than sending a BAR.
	 */
	if (ath_tx_tid_bar_tx_ready(sc, atid))
		ath_tx_tid_bar_tx(sc, atid);

	ATH_TX_UNLOCK(sc);

	/* Handle frame completion as individual frames */
	bf = bf_first;
	while (bf) {
		bf_next = bf->bf_next;
		bf->bf_next = NULL;
		ath_tx_default_comp(sc, bf, 1);
		bf = bf_next;
	}
}

/*
 * Handle completion of an set of aggregate frames.
 *
 * Note: the completion handler is the last descriptor in the aggregate,
 * not the last descriptor in the first frame.
 */
static void
ath_tx_aggr_comp_aggr(struct ath_softc *sc, struct ath_buf *bf_first,
    int fail)
{
	//struct ath_desc *ds = bf->bf_lastds;
	struct ieee80211_node *ni = bf_first->bf_node;
	struct ath_node *an = ATH_NODE(ni);
	int tid = bf_first->bf_state.bfs_tid;
	struct ath_tid *atid = &an->an_tid[tid];
	struct ath_tx_status ts;
	struct ieee80211_tx_ampdu *tap;
	ath_bufhead bf_q;
	ath_bufhead bf_cq;
	int seq_st, tx_ok;
	int hasba, isaggr;
	uint32_t ba[2];
	struct ath_buf *bf, *bf_next;
	int ba_index;
	int drops = 0;
	int nframes = 0, nbad = 0, nf;
	int pktlen;
	/* XXX there's too much on the stack? */
	struct ath_rc_series rc[ATH_RC_NUM];
	int txseq;

	DPRINTF(sc, ATH_DEBUG_SW_TX_AGGR, "%s: called; hwq_depth=%d\n",
	    __func__, atid->hwq_depth);

	/*
	 * Take a copy; this may be needed -after- bf_first
	 * has been completed and freed.
	 */
	ts = bf_first->bf_status.ds_txstat;

	TAILQ_INIT(&bf_q);
	TAILQ_INIT(&bf_cq);

	/* The TID state is kept behind the TXQ lock */
	ATH_TX_LOCK(sc);

	atid->hwq_depth--;
	if (atid->hwq_depth < 0)
		DPRINTF(sc, ATH_DEBUG_SW_TX_AGGR, "%s: hwq_depth < 0: %d\n",
		    __func__, atid->hwq_depth);

	/*
	 * If the TID is filtered, handle completing the filter
	 * transition before potentially kicking it to the cleanup
	 * function.
	 *
	 * XXX this is duplicate work, ew.
	 */
	if (atid->isfiltered)
		ath_tx_tid_filt_comp_complete(sc, atid);

	/*
	 * Punt cleanup to the relevant function, not our problem now
	 */
	if (atid->cleanup_inprogress) {
		if (atid->isfiltered)
			DPRINTF(sc, ATH_DEBUG_SW_TX_AGGR,
			    "%s: isfiltered=1, normal_comp?\n",
			    __func__);
		ATH_TX_UNLOCK(sc);
		ath_tx_comp_cleanup_aggr(sc, bf_first);
		return;
	}

	/*
	 * If the frame is filtered, transition to filtered frame
	 * mode and add this to the filtered frame list.
	 *
	 * XXX TODO: figure out how this interoperates with
	 * BAR, pause and cleanup states.
	 */
	if ((ts.ts_status & HAL_TXERR_FILT) ||
	    (ts.ts_status != 0 && atid->isfiltered)) {
		if (fail != 0)
			DPRINTF(sc, ATH_DEBUG_SW_TX_AGGR,
			    "%s: isfiltered=1, fail=%d\n", __func__, fail);
		ath_tx_tid_filt_comp_aggr(sc, atid, bf_first, &bf_cq);

		/* Remove from BAW */
		TAILQ_FOREACH_SAFE(bf, &bf_cq, bf_list, bf_next) {
			if (bf->bf_state.bfs_addedbaw)
				drops++;
			if (bf->bf_state.bfs_dobaw) {
				ath_tx_update_baw(sc, an, atid, bf);
				if (!bf->bf_state.bfs_addedbaw)
					DPRINTF(sc, ATH_DEBUG_SW_TX_AGGR,
					    "%s: wasn't added: seqno %d\n",
					    __func__,
					    SEQNO(bf->bf_state.bfs_seqno));
			}
			bf->bf_state.bfs_dobaw = 0;
		}
		/*
		 * If any intermediate frames in the BAW were dropped when
		 * handling filtering things, send a BAR.
		 */
		if (drops)
			ath_tx_tid_bar_suspend(sc, atid);

		/*
		 * Finish up by sending a BAR if required and freeing
		 * the frames outside of the TX lock.
		 */
		goto finish_send_bar;
	}

	/*
	 * XXX for now, use the first frame in the aggregate for
	 * XXX rate control completion; it's at least consistent.
	 */
	pktlen = bf_first->bf_state.bfs_pktlen;

	/*
	 * Handle errors first!
	 *
	 * Here, handle _any_ error as a "exceeded retries" error.
	 * Later on (when filtered frames are to be specially handled)
	 * it'll have to be expanded.
	 */
#if 0
	if (ts.ts_status & HAL_TXERR_XRETRY) {
#endif
	if (ts.ts_status != 0) {
		ATH_TX_UNLOCK(sc);
		ath_tx_comp_aggr_error(sc, bf_first, atid);
		return;
	}

	tap = ath_tx_get_tx_tid(an, tid);

	/*
	 * extract starting sequence and block-ack bitmap
	 */
	/* XXX endian-ness of seq_st, ba? */
	seq_st = ts.ts_seqnum;
	hasba = !! (ts.ts_flags & HAL_TX_BA);
	tx_ok = (ts.ts_status == 0);
	isaggr = bf_first->bf_state.bfs_aggr;
	ba[0] = ts.ts_ba_low;
	ba[1] = ts.ts_ba_high;

	/*
	 * Copy the TX completion status and the rate control
	 * series from the first descriptor, as it may be freed
	 * before the rate control code can get its grubby fingers
	 * into things.
	 */
	memcpy(rc, bf_first->bf_state.bfs_rc, sizeof(rc));

	DPRINTF(sc, ATH_DEBUG_SW_TX_AGGR,
	    "%s: txa_start=%d, tx_ok=%d, status=%.8x, flags=%.8x, "
	    "isaggr=%d, seq_st=%d, hasba=%d, ba=%.8x, %.8x\n",
	    __func__, tap->txa_start, tx_ok, ts.ts_status, ts.ts_flags,
	    isaggr, seq_st, hasba, ba[0], ba[1]);

	/*
	 * The reference driver doesn't do this; it simply ignores
	 * this check in its entirety.
	 *
	 * I've seen this occur when using iperf to send traffic
	 * out tid 1 - the aggregate frames are all marked as TID 1,
	 * but the TXSTATUS has TID=0.  So, let's just ignore this
	 * check.
	 */
#if 0
	/* Occasionally, the MAC sends a tx status for the wrong TID. */
	if (tid != ts.ts_tid) {
		DPRINTF(sc, ATH_DEBUG_SW_TX_AGGR, "%s: tid %d != hw tid %d\n",
		    __func__, tid, ts.ts_tid);
		tx_ok = 0;
	}
#endif

	/* AR5416 BA bug; this requires an interface reset */
	if (isaggr && tx_ok && (! hasba)) {
		device_printf(sc->sc_dev,
		    "%s: AR5416 bug: hasba=%d; txok=%d, isaggr=%d, "
		    "seq_st=%d\n",
		    __func__, hasba, tx_ok, isaggr, seq_st);
		/* XXX TODO: schedule an interface reset */
#ifdef ATH_DEBUG
		ath_printtxbuf(sc, bf_first,
		    sc->sc_ac2q[atid->ac]->axq_qnum, 0, 0);
#endif
	}

	/*
	 * Walk the list of frames, figure out which ones were correctly
	 * sent and which weren't.
	 */
	bf = bf_first;
	nf = bf_first->bf_state.bfs_nframes;

	/* bf_first is going to be invalid once this list is walked */
	bf_first = NULL;

	/*
	 * Walk the list of completed frames and determine
	 * which need to be completed and which need to be
	 * retransmitted.
	 *
	 * For completed frames, the completion functions need
	 * to be called at the end of this function as the last
	 * node reference may free the node.
	 *
	 * Finally, since the TXQ lock can't be held during the
	 * completion callback (to avoid lock recursion),
	 * the completion calls have to be done outside of the
	 * lock.
	 */
	while (bf) {
		nframes++;
		ba_index = ATH_BA_INDEX(seq_st,
		    SEQNO(bf->bf_state.bfs_seqno));
		bf_next = bf->bf_next;
		bf->bf_next = NULL;	/* Remove it from the aggr list */

		DPRINTF(sc, ATH_DEBUG_SW_TX_AGGR,
		    "%s: checking bf=%p seqno=%d; ack=%d\n",
		    __func__, bf, SEQNO(bf->bf_state.bfs_seqno),
		    ATH_BA_ISSET(ba, ba_index));

		if (tx_ok && ATH_BA_ISSET(ba, ba_index)) {
			sc->sc_stats.ast_tx_aggr_ok++;
			ath_tx_update_baw(sc, an, atid, bf);
			bf->bf_state.bfs_dobaw = 0;
			if (!bf->bf_state.bfs_addedbaw)
				DPRINTF(sc, ATH_DEBUG_SW_TX_AGGR,
				    "%s: wasn't added: seqno %d\n",
				    __func__, SEQNO(bf->bf_state.bfs_seqno));
			bf->bf_next = NULL;
			TAILQ_INSERT_TAIL(&bf_cq, bf, bf_list);
		} else {
			sc->sc_stats.ast_tx_aggr_fail++;
			if (ath_tx_retry_subframe(sc, bf, &bf_q)) {
				drops++;
				bf->bf_next = NULL;
				TAILQ_INSERT_TAIL(&bf_cq, bf, bf_list);
			}
			nbad++;
		}
		bf = bf_next;
	}

	/*
	 * Now that the BAW updates have been done, unlock
	 *
	 * txseq is grabbed before the lock is released so we
	 * have a consistent view of what -was- in the BAW.
	 * Anything after this point will not yet have been
	 * TXed.
	 */
	txseq = tap->txa_start;
	ATH_TX_UNLOCK(sc);

	if (nframes != nf)
		DPRINTF(sc, ATH_DEBUG_SW_TX_AGGR,
		    "%s: num frames seen=%d; bf nframes=%d\n",
		    __func__, nframes, nf);

	/*
	 * Now we know how many frames were bad, call the rate
	 * control code.
	 */
	if (fail == 0)
		ath_tx_update_ratectrl(sc, ni, rc, &ts, pktlen, nframes,
		    nbad);

	/*
	 * send bar if we dropped any frames
	 */
	if (drops) {
		/* Suspend the TX queue and get ready to send the BAR */
		ATH_TX_LOCK(sc);
		ath_tx_tid_bar_suspend(sc, atid);
		ATH_TX_UNLOCK(sc);
	}

	DPRINTF(sc, ATH_DEBUG_SW_TX_AGGR,
	    "%s: txa_start now %d\n", __func__, tap->txa_start);

	ATH_TX_LOCK(sc);

	/* Prepend all frames to the beginning of the queue */
	while ((bf = TAILQ_LAST(&bf_q, ath_bufhead_s)) != NULL) {
		TAILQ_REMOVE(&bf_q, bf, bf_list);
		ATH_TID_INSERT_HEAD(atid, bf, bf_list);
	}

	/*
	 * Reschedule to grab some further frames.
	 */
	ath_tx_tid_sched(sc, atid);

	/*
	 * If the queue is filtered, re-schedule as required.
	 *
	 * This is required as there may be a subsequent TX descriptor
	 * for this end-node that has CLRDMASK set, so it's quite possible
	 * that a filtered frame will be followed by a non-filtered
	 * (complete or otherwise) frame.
	 *
	 * XXX should we do this before we complete the frame?
	 */
	if (atid->isfiltered)
		ath_tx_tid_filt_comp_complete(sc, atid);

finish_send_bar:

	/*
	 * Send BAR if required
	 */
	if (ath_tx_tid_bar_tx_ready(sc, atid))
		ath_tx_tid_bar_tx(sc, atid);

	ATH_TX_UNLOCK(sc);

	/* Do deferred completion */
	while ((bf = TAILQ_FIRST(&bf_cq)) != NULL) {
		TAILQ_REMOVE(&bf_cq, bf, bf_list);
		ath_tx_default_comp(sc, bf, 0);
	}
}

/*
 * Handle completion of unaggregated frames in an ADDBA
 * session.
 *
 * Fail is set to 1 if the entry is being freed via a call to
 * ath_tx_draintxq().
 */
static void
ath_tx_aggr_comp_unaggr(struct ath_softc *sc, struct ath_buf *bf, int fail)
{
	struct ieee80211_node *ni = bf->bf_node;
	struct ath_node *an = ATH_NODE(ni);
	int tid = bf->bf_state.bfs_tid;
	struct ath_tid *atid = &an->an_tid[tid];
	struct ath_tx_status ts;
	int drops = 0;

	/*
	 * Take a copy of this; filtering/cloning the frame may free the
	 * bf pointer.
	 */
	ts = bf->bf_status.ds_txstat;

	/*
	 * Update rate control status here, before we possibly
	 * punt to retry or cleanup.
	 *
	 * Do it outside of the TXQ lock.
	 */
	if (fail == 0 && ((bf->bf_state.bfs_txflags & HAL_TXDESC_NOACK) == 0))
		ath_tx_update_ratectrl(sc, ni, bf->bf_state.bfs_rc,
		    &bf->bf_status.ds_txstat,
		    bf->bf_state.bfs_pktlen,
		    1, (ts.ts_status == 0) ? 0 : 1);

	/*
	 * This is called early so atid->hwq_depth can be tracked.
	 * This unfortunately means that it's released and regrabbed
	 * during retry and cleanup. That's rather inefficient.
	 */
	ATH_TX_LOCK(sc);

	if (tid == IEEE80211_NONQOS_TID)
		DPRINTF(sc, ATH_DEBUG_SW_TX, "%s: TID=16!\n", __func__);

	DPRINTF(sc, ATH_DEBUG_SW_TX,
	    "%s: bf=%p: tid=%d, hwq_depth=%d, seqno=%d\n",
	    __func__, bf, bf->bf_state.bfs_tid, atid->hwq_depth,
	    SEQNO(bf->bf_state.bfs_seqno));

	atid->hwq_depth--;
	if (atid->hwq_depth < 0)
		DPRINTF(sc, ATH_DEBUG_SW_TX, "%s: hwq_depth < 0: %d\n",
		    __func__, atid->hwq_depth);

	/*
	 * If the TID is filtered, handle completing the filter
	 * transition before potentially kicking it to the cleanup
	 * function.
	 */
	if (atid->isfiltered)
		ath_tx_tid_filt_comp_complete(sc, atid);

	/*
	 * If a cleanup is in progress, punt to comp_cleanup;
	 * rather than handling it here. It's thus their
	 * responsibility to clean up, call the completion
	 * function in net80211, etc.
	 */
	if (atid->cleanup_inprogress) {
		if (atid->isfiltered)
			DPRINTF(sc, ATH_DEBUG_SW_TX,
			    "%s: isfiltered=1, normal_comp?\n",
			    __func__);
		ATH_TX_UNLOCK(sc);
		DPRINTF(sc, ATH_DEBUG_SW_TX, "%s: cleanup_unaggr\n",
		    __func__);
		ath_tx_comp_cleanup_unaggr(sc, bf);
		return;
	}

	/*
	 * XXX TODO: how does cleanup, BAR and filtered frame handling
	 * overlap?
	 *
	 * If the frame is filtered OR if it's any failure but
	 * the TID is filtered, the frame must be added to the
	 * filtered frame list.
	 *
	 * However - a busy buffer can't be added to the filtered
	 * list as it will end up being recycled without having
	 * been made available for the hardware.
	 */
	if ((ts.ts_status & HAL_TXERR_FILT) ||
	    (ts.ts_status != 0 && atid->isfiltered)) {
		int freeframe;

		if (fail != 0)
			DPRINTF(sc, ATH_DEBUG_SW_TX,
			    "%s: isfiltered=1, fail=%d\n",
			    __func__, fail);
		freeframe = ath_tx_tid_filt_comp_single(sc, atid, bf);
		/*
		 * If freeframe=0 then bf is no longer ours; don't
		 * touch it.
		 */
		if (freeframe) {
			/* Remove from BAW */
			if (bf->bf_state.bfs_addedbaw)
				drops++;
			if (bf->bf_state.bfs_dobaw) {
				ath_tx_update_baw(sc, an, atid, bf);
				if (!bf->bf_state.bfs_addedbaw)
					DPRINTF(sc, ATH_DEBUG_SW_TX,
					    "%s: wasn't added: seqno %d\n",
					    __func__, SEQNO(bf->bf_state.bfs_seqno));
			}
			bf->bf_state.bfs_dobaw = 0;
		}

		/*
		 * If the frame couldn't be filtered, treat it as a drop and
		 * prepare to send a BAR.
		 */
		if (freeframe && drops)
			ath_tx_tid_bar_suspend(sc, atid);

		/*
		 * Send BAR if required
		 */
		if (ath_tx_tid_bar_tx_ready(sc, atid))
			ath_tx_tid_bar_tx(sc, atid);

		ATH_TX_UNLOCK(sc);
		/*
		 * If freeframe is set, then the frame couldn't be
		 * cloned and bf is still valid.  Just complete/free it.
		 */
		if (freeframe)
			ath_tx_default_comp(sc, bf, fail);

		return;
	}
	/*
	 * Don't bother with the retry check if all frames
	 * are being failed (eg during queue deletion.)
	 */
#if 0
	if (fail == 0 && ts->ts_status & HAL_TXERR_XRETRY) {
#endif
	if (fail == 0 && ts.ts_status != 0) {
		ATH_TX_UNLOCK(sc);
		DPRINTF(sc, ATH_DEBUG_SW_TX, "%s: retry_unaggr\n",
		    __func__);
		ath_tx_aggr_retry_unaggr(sc, bf);
		return;
	}

	/* Success? Complete */
	DPRINTF(sc, ATH_DEBUG_SW_TX, "%s: TID=%d, seqno %d\n",
	    __func__, tid, SEQNO(bf->bf_state.bfs_seqno));
	if (bf->bf_state.bfs_dobaw) {
		ath_tx_update_baw(sc, an, atid, bf);
		bf->bf_state.bfs_dobaw = 0;
		if (!bf->bf_state.bfs_addedbaw)
			DPRINTF(sc, ATH_DEBUG_SW_TX,
			    "%s: wasn't added: seqno %d\n",
			    __func__, SEQNO(bf->bf_state.bfs_seqno));
	}

	/*
	 * If the queue is filtered, re-schedule as required.
	 *
	 * This is required as there may be a subsequent TX descriptor
	 * for this end-node that has CLRDMASK set, so it's quite possible
	 * that a filtered frame will be followed by a non-filtered
	 * (complete or otherwise) frame.
	 *
	 * XXX should we do this before we complete the frame?
	 */
	if (atid->isfiltered)
		ath_tx_tid_filt_comp_complete(sc, atid);

	/*
	 * Send BAR if required
	 */
	if (ath_tx_tid_bar_tx_ready(sc, atid))
		ath_tx_tid_bar_tx(sc, atid);

	ATH_TX_UNLOCK(sc);

	ath_tx_default_comp(sc, bf, fail);
	/* bf is freed at this point */
}

void
ath_tx_aggr_comp(struct ath_softc *sc, struct ath_buf *bf, int fail)
{
	if (bf->bf_state.bfs_aggr)
		ath_tx_aggr_comp_aggr(sc, bf, fail);
	else
		ath_tx_aggr_comp_unaggr(sc, bf, fail);
}

/*
 * Schedule some packets from the given node/TID to the hardware.
 *
 * This is the aggregate version.
 */
void
ath_tx_tid_hw_queue_aggr(struct ath_softc *sc, struct ath_node *an,
    struct ath_tid *tid)
{
	struct ath_buf *bf;
	struct ath_txq *txq = sc->sc_ac2q[tid->ac];
	struct ieee80211_tx_ampdu *tap;
	ATH_AGGR_STATUS status;
	ath_bufhead bf_q;

	DPRINTF(sc, ATH_DEBUG_SW_TX, "%s: tid=%d\n", __func__, tid->tid);
	ATH_TX_LOCK_ASSERT(sc);

	/*
	 * XXX TODO: If we're called for a queue that we're leaking frames to,
	 * ensure we only leak one.
	 */

	tap = ath_tx_get_tx_tid(an, tid->tid);

	if (tid->tid == IEEE80211_NONQOS_TID)
		DPRINTF(sc, ATH_DEBUG_SW_TX, 
		    "%s: called for TID=NONQOS_TID?\n", __func__);

	for (;;) {
		status = ATH_AGGR_DONE;

		/*
		 * If the upper layer has paused the TID, don't
		 * queue any further packets.
		 *
		 * This can also occur from the completion task because
		 * of packet loss; but as its serialised with this code,
		 * it won't "appear" half way through queuing packets.
		 */
		if (! ath_tx_tid_can_tx_or_sched(sc, tid))
			break;

		bf = ATH_TID_FIRST(tid);
		if (bf == NULL) {
			break;
		}

		/*
		 * If the packet doesn't fall within the BAW (eg a NULL
		 * data frame), schedule it directly; continue.
		 */
		if (! bf->bf_state.bfs_dobaw) {
			DPRINTF(sc, ATH_DEBUG_SW_TX_AGGR,
			    "%s: non-baw packet\n",
			    __func__);
			ATH_TID_REMOVE(tid, bf, bf_list);

			if (bf->bf_state.bfs_nframes > 1)
				DPRINTF(sc, ATH_DEBUG_SW_TX, 
				    "%s: aggr=%d, nframes=%d\n",
				    __func__,
				    bf->bf_state.bfs_aggr,
				    bf->bf_state.bfs_nframes);

			/*
			 * This shouldn't happen - such frames shouldn't
			 * ever have been queued as an aggregate in the
			 * first place.  However, make sure the fields
			 * are correctly setup just to be totally sure.
			 */
			bf->bf_state.bfs_aggr = 0;
			bf->bf_state.bfs_nframes = 1;

			/* Update CLRDMASK just before this frame is queued */
			ath_tx_update_clrdmask(sc, tid, bf);

			ath_tx_do_ratelookup(sc, bf);
			ath_tx_calc_duration(sc, bf);
			ath_tx_calc_protection(sc, bf);
			ath_tx_set_rtscts(sc, bf);
			ath_tx_rate_fill_rcflags(sc, bf);
			ath_tx_setds(sc, bf);
			ath_hal_clr11n_aggr(sc->sc_ah, bf->bf_desc);

			sc->sc_aggr_stats.aggr_nonbaw_pkt++;

			/* Queue the packet; continue */
			goto queuepkt;
		}

		TAILQ_INIT(&bf_q);

		/*
		 * Do a rate control lookup on the first frame in the
		 * list. The rate control code needs that to occur
		 * before it can determine whether to TX.
		 * It's inaccurate because the rate control code doesn't
		 * really "do" aggregate lookups, so it only considers
		 * the size of the first frame.
		 */
		ath_tx_do_ratelookup(sc, bf);
		bf->bf_state.bfs_rc[3].rix = 0;
		bf->bf_state.bfs_rc[3].tries = 0;

		ath_tx_calc_duration(sc, bf);
		ath_tx_calc_protection(sc, bf);

		ath_tx_set_rtscts(sc, bf);
		ath_tx_rate_fill_rcflags(sc, bf);

		status = ath_tx_form_aggr(sc, an, tid, &bf_q);

		DPRINTF(sc, ATH_DEBUG_SW_TX_AGGR,
		    "%s: ath_tx_form_aggr() status=%d\n", __func__, status);

		/*
		 * No frames to be picked up - out of BAW
		 */
		if (TAILQ_EMPTY(&bf_q))
			break;

		/*
		 * This assumes that the descriptor list in the ath_bufhead
		 * are already linked together via bf_next pointers.
		 */
		bf = TAILQ_FIRST(&bf_q);

		if (status == ATH_AGGR_8K_LIMITED)
			sc->sc_aggr_stats.aggr_rts_aggr_limited++;

		/*
		 * If it's the only frame send as non-aggregate
		 * assume that ath_tx_form_aggr() has checked
		 * whether it's in the BAW and added it appropriately.
		 */
		if (bf->bf_state.bfs_nframes == 1) {
			DPRINTF(sc, ATH_DEBUG_SW_TX_AGGR,
			    "%s: single-frame aggregate\n", __func__);

			/* Update CLRDMASK just before this frame is queued */
			ath_tx_update_clrdmask(sc, tid, bf);

			bf->bf_state.bfs_aggr = 0;
			bf->bf_state.bfs_ndelim = 0;
			ath_tx_setds(sc, bf);
			ath_hal_clr11n_aggr(sc->sc_ah, bf->bf_desc);
			if (status == ATH_AGGR_BAW_CLOSED)
				sc->sc_aggr_stats.aggr_baw_closed_single_pkt++;
			else
				sc->sc_aggr_stats.aggr_single_pkt++;
		} else {
			DPRINTF(sc, ATH_DEBUG_SW_TX_AGGR,
			    "%s: multi-frame aggregate: %d frames, "
			    "length %d\n",
			     __func__, bf->bf_state.bfs_nframes,
			    bf->bf_state.bfs_al);
			bf->bf_state.bfs_aggr = 1;
			sc->sc_aggr_stats.aggr_pkts[bf->bf_state.bfs_nframes]++;
			sc->sc_aggr_stats.aggr_aggr_pkt++;

			/* Update CLRDMASK just before this frame is queued */
			ath_tx_update_clrdmask(sc, tid, bf);

			/*
			 * Calculate the duration/protection as required.
			 */
			ath_tx_calc_duration(sc, bf);
			ath_tx_calc_protection(sc, bf);

			/*
			 * Update the rate and rtscts information based on the
			 * rate decision made by the rate control code;
			 * the first frame in the aggregate needs it.
			 */
			ath_tx_set_rtscts(sc, bf);

			/*
			 * Setup the relevant descriptor fields
			 * for aggregation. The first descriptor
			 * already points to the rest in the chain.
			 */
			ath_tx_setds_11n(sc, bf);

		}
	queuepkt:
		/* Set completion handler, multi-frame aggregate or not */
		bf->bf_comp = ath_tx_aggr_comp;

		if (bf->bf_state.bfs_tid == IEEE80211_NONQOS_TID)
			DPRINTF(sc, ATH_DEBUG_SW_TX, "%s: TID=16?\n", __func__);

		/*
		 * Update leak count and frame config if were leaking frames.
		 *
		 * XXX TODO: it should update all frames in an aggregate
		 * correctly!
		 */
		ath_tx_leak_count_update(sc, tid, bf);

		/* Punt to txq */
		ath_tx_handoff(sc, txq, bf);

		/* Track outstanding buffer count to hardware */
		/* aggregates are "one" buffer */
		tid->hwq_depth++;

		/*
		 * Break out if ath_tx_form_aggr() indicated
		 * there can't be any further progress (eg BAW is full.)
		 * Checking for an empty txq is done above.
		 *
		 * XXX locking on txq here?
		 */
		/* XXX TXQ locking */
		if (txq->axq_aggr_depth >= sc->sc_hwq_limit_aggr ||
		    (status == ATH_AGGR_BAW_CLOSED ||
		     status == ATH_AGGR_LEAK_CLOSED))
			break;
	}
}

/*
 * Schedule some packets from the given node/TID to the hardware.
 *
 * XXX TODO: this routine doesn't enforce the maximum TXQ depth.
 * It just dumps frames into the TXQ.  We should limit how deep
 * the transmit queue can grow for frames dispatched to the given
 * TXQ.
 *
 * To avoid locking issues, either we need to own the TXQ lock
 * at this point, or we need to pass in the maximum frame count
 * from the caller.
 */
void
ath_tx_tid_hw_queue_norm(struct ath_softc *sc, struct ath_node *an,
    struct ath_tid *tid)
{
	struct ath_buf *bf;
	struct ath_txq *txq = sc->sc_ac2q[tid->ac];

	DPRINTF(sc, ATH_DEBUG_SW_TX, "%s: node %p: TID %d: called\n",
	    __func__, an, tid->tid);

	ATH_TX_LOCK_ASSERT(sc);

	/* Check - is AMPDU pending or running? then print out something */
	if (ath_tx_ampdu_pending(sc, an, tid->tid))
		DPRINTF(sc, ATH_DEBUG_SW_TX, "%s: tid=%d, ampdu pending?\n",
		    __func__, tid->tid);
	if (ath_tx_ampdu_running(sc, an, tid->tid))
		DPRINTF(sc, ATH_DEBUG_SW_TX, "%s: tid=%d, ampdu running?\n",
		    __func__, tid->tid);

	for (;;) {

		/*
		 * If the upper layers have paused the TID, don't
		 * queue any further packets.
		 *
		 * XXX if we are leaking frames, make sure we decrement
		 * that counter _and_ we continue here.
		 */
		if (! ath_tx_tid_can_tx_or_sched(sc, tid))
			break;

		bf = ATH_TID_FIRST(tid);
		if (bf == NULL) {
			break;
		}

		ATH_TID_REMOVE(tid, bf, bf_list);

		/* Sanity check! */
		if (tid->tid != bf->bf_state.bfs_tid) {
			DPRINTF(sc, ATH_DEBUG_SW_TX, "%s: bfs_tid %d !="
			    " tid %d\n", __func__, bf->bf_state.bfs_tid,
			    tid->tid);
		}
		/* Normal completion handler */
		bf->bf_comp = ath_tx_normal_comp;

		/*
		 * Override this for now, until the non-aggregate
		 * completion handler correctly handles software retransmits.
		 */
		bf->bf_state.bfs_txflags |= HAL_TXDESC_CLRDMASK;

		/* Update CLRDMASK just before this frame is queued */
		ath_tx_update_clrdmask(sc, tid, bf);

		/* Program descriptors + rate control */
		ath_tx_do_ratelookup(sc, bf);
		ath_tx_calc_duration(sc, bf);
		ath_tx_calc_protection(sc, bf);
		ath_tx_set_rtscts(sc, bf);
		ath_tx_rate_fill_rcflags(sc, bf);
		ath_tx_setds(sc, bf);

		/*
		 * Update the current leak count if
		 * we're leaking frames; and set the
		 * MORE flag as appropriate.
		 */
		ath_tx_leak_count_update(sc, tid, bf);

		/* Track outstanding buffer count to hardware */
		/* aggregates are "one" buffer */
		tid->hwq_depth++;

		/* Punt to hardware or software txq */
		ath_tx_handoff(sc, txq, bf);
	}
}

/*
 * Schedule some packets to the given hardware queue.
 *
 * This function walks the list of TIDs (ie, ath_node TIDs
 * with queued traffic) and attempts to schedule traffic
 * from them.
 *
 * TID scheduling is implemented as a FIFO, with TIDs being
 * added to the end of the queue after some frames have been
 * scheduled.
 */
void
ath_txq_sched(struct ath_softc *sc, struct ath_txq *txq)
{
	struct ath_tid *tid, *next, *last;

	ATH_TX_LOCK_ASSERT(sc);

	/*
	 * For non-EDMA chips, aggr frames that have been built are
	 * in axq_aggr_depth, whether they've been scheduled or not.
	 * There's no FIFO, so txq->axq_depth is what's been scheduled
	 * to the hardware.
	 *
	 * For EDMA chips, we do it in two stages.  The existing code
	 * builds a list of frames to go to the hardware and the EDMA
	 * code turns it into a single entry to push into the FIFO.
	 * That way we don't take up one packet per FIFO slot.
	 * We do push one aggregate per FIFO slot though, just to keep
	 * things simple.
	 *
	 * The FIFO depth is what's in the hardware; the txq->axq_depth
	 * is what's been scheduled to the FIFO.
	 *
	 * fifo.axq_depth is the number of frames (or aggregates) pushed
	 *  into the EDMA FIFO.  For multi-frame lists, this is the number
	 *  of frames pushed in.
	 * axq_fifo_depth is the number of FIFO slots currently busy.
	 */

	/* For EDMA and non-EDMA, check built/scheduled against aggr limit */
	if (txq->axq_aggr_depth >= sc->sc_hwq_limit_aggr) {
		sc->sc_aggr_stats.aggr_sched_nopkt++;
		return;
	}

	/*
	 * For non-EDMA chips, axq_depth is the "what's scheduled to
	 * the hardware list".  For EDMA it's "What's built for the hardware"
	 * and fifo.axq_depth is how many frames have been dispatched
	 * already to the hardware.
	 */
	if (txq->axq_depth + txq->fifo.axq_depth >= sc->sc_hwq_limit_nonaggr) {
		sc->sc_aggr_stats.aggr_sched_nopkt++;
		return;
	}

	last = TAILQ_LAST(&txq->axq_tidq, axq_t_s);

	TAILQ_FOREACH_SAFE(tid, &txq->axq_tidq, axq_qelem, next) {
		/*
		 * Suspend paused queues here; they'll be resumed
		 * once the addba completes or times out.
		 */
		DPRINTF(sc, ATH_DEBUG_SW_TX, "%s: tid=%d, paused=%d\n",
		    __func__, tid->tid, tid->paused);
		ath_tx_tid_unsched(sc, tid);
		/*
		 * This node may be in power-save and we're leaking
		 * a frame; be careful.
		 */
		if (! ath_tx_tid_can_tx_or_sched(sc, tid)) {
			goto loop_done;
		}
		if (ath_tx_ampdu_running(sc, tid->an, tid->tid))
			ath_tx_tid_hw_queue_aggr(sc, tid->an, tid);
		else
			ath_tx_tid_hw_queue_norm(sc, tid->an, tid);

		/* Not empty? Re-schedule */
		if (tid->axq_depth != 0)
			ath_tx_tid_sched(sc, tid);

		/*
		 * Give the software queue time to aggregate more
		 * packets.  If we aren't running aggregation then
		 * we should still limit the hardware queue depth.
		 */
		/* XXX TXQ locking */
		if (txq->axq_aggr_depth + txq->fifo.axq_depth >= sc->sc_hwq_limit_aggr) {
			break;
		}
		if (txq->axq_depth >= sc->sc_hwq_limit_nonaggr) {
			break;
		}
loop_done:
		/*
		 * If this was the last entry on the original list, stop.
		 * Otherwise nodes that have been rescheduled onto the end
		 * of the TID FIFO list will just keep being rescheduled.
		 *
		 * XXX What should we do about nodes that were paused
		 * but are pending a leaking frame in response to a ps-poll?
		 * They'll be put at the front of the list; so they'll
		 * prematurely trigger this condition! Ew.
		 */
		if (tid == last)
			break;
	}
}

/*
 * TX addba handling
 */

/*
 * Return net80211 TID struct pointer, or NULL for none
 */
struct ieee80211_tx_ampdu *
ath_tx_get_tx_tid(struct ath_node *an, int tid)
{
	struct ieee80211_node *ni = &an->an_node;
	struct ieee80211_tx_ampdu *tap;

	if (tid == IEEE80211_NONQOS_TID)
		return NULL;

	tap = &ni->ni_tx_ampdu[tid];
	return tap;
}

/*
 * Is AMPDU-TX running?
 */
static int
ath_tx_ampdu_running(struct ath_softc *sc, struct ath_node *an, int tid)
{
	struct ieee80211_tx_ampdu *tap;

	if (tid == IEEE80211_NONQOS_TID)
		return 0;

	tap = ath_tx_get_tx_tid(an, tid);
	if (tap == NULL)
		return 0;	/* Not valid; default to not running */

	return !! (tap->txa_flags & IEEE80211_AGGR_RUNNING);
}

/*
 * Is AMPDU-TX negotiation pending?
 */
static int
ath_tx_ampdu_pending(struct ath_softc *sc, struct ath_node *an, int tid)
{
	struct ieee80211_tx_ampdu *tap;

	if (tid == IEEE80211_NONQOS_TID)
		return 0;

	tap = ath_tx_get_tx_tid(an, tid);
	if (tap == NULL)
		return 0;	/* Not valid; default to not pending */

	return !! (tap->txa_flags & IEEE80211_AGGR_XCHGPEND);
}

/*
 * Is AMPDU-TX pending for the given TID?
 */


/*
 * Method to handle sending an ADDBA request.
 *
 * We tap this so the relevant flags can be set to pause the TID
 * whilst waiting for the response.
 *
 * XXX there's no timeout handler we can override?
 */
int
ath_addba_request(struct ieee80211_node *ni, struct ieee80211_tx_ampdu *tap,
    int dialogtoken, int baparamset, int batimeout)
{
	struct ath_softc *sc = ni->ni_ic->ic_softc;
	int tid = tap->txa_tid;
	struct ath_node *an = ATH_NODE(ni);
	struct ath_tid *atid = &an->an_tid[tid];

	/*
	 * XXX danger Will Robinson!
	 *
	 * Although the taskqueue may be running and scheduling some more
	 * packets, these should all be _before_ the addba sequence number.
	 * However, net80211 will keep self-assigning sequence numbers
	 * until addba has been negotiated.
	 *
	 * In the past, these packets would be "paused" (which still works
	 * fine, as they're being scheduled to the driver in the same
	 * serialised method which is calling the addba request routine)
	 * and when the aggregation session begins, they'll be dequeued
	 * as aggregate packets and added to the BAW. However, now there's
	 * a "bf->bf_state.bfs_dobaw" flag, and this isn't set for these
	 * packets. Thus they never get included in the BAW tracking and
	 * this can cause the initial burst of packets after the addba
	 * negotiation to "hang", as they quickly fall outside the BAW.
	 *
	 * The "eventual" solution should be to tag these packets with
	 * dobaw. Although net80211 has given us a sequence number,
	 * it'll be "after" the left edge of the BAW and thus it'll
	 * fall within it.
	 */
	ATH_TX_LOCK(sc);
	/*
	 * This is a bit annoying.  Until net80211 HT code inherits some
	 * (any) locking, we may have this called in parallel BUT only
	 * one response/timeout will be called.  Grr.
	 */
	if (atid->addba_tx_pending == 0) {
		ath_tx_tid_pause(sc, atid);
		atid->addba_tx_pending = 1;
	}
	ATH_TX_UNLOCK(sc);

	DPRINTF(sc, ATH_DEBUG_SW_TX_CTRL,
	    "%s: %6D: called; dialogtoken=%d, baparamset=%d, batimeout=%d\n",
	    __func__,
	    ni->ni_macaddr,
	    ":",
	    dialogtoken, baparamset, batimeout);
	DPRINTF(sc, ATH_DEBUG_SW_TX_CTRL,
	    "%s: txa_start=%d, ni_txseqs=%d\n",
	    __func__, tap->txa_start, ni->ni_txseqs[tid]);

	return sc->sc_addba_request(ni, tap, dialogtoken, baparamset,
	    batimeout);
}

/*
 * Handle an ADDBA response.
 *
 * We unpause the queue so TX'ing can resume.
 *
 * Any packets TX'ed from this point should be "aggregate" (whether
 * aggregate or not) so the BAW is updated.
 *
 * Note! net80211 keeps self-assigning sequence numbers until
 * ampdu is negotiated. This means the initially-negotiated BAW left
 * edge won't match the ni->ni_txseq.
 *
 * So, being very dirty, the BAW left edge is "slid" here to match
 * ni->ni_txseq.
 *
 * What likely SHOULD happen is that all packets subsequent to the
 * addba request should be tagged as aggregate and queued as non-aggregate
 * frames; thus updating the BAW. For now though, I'll just slide the
 * window.
 */
int
ath_addba_response(struct ieee80211_node *ni, struct ieee80211_tx_ampdu *tap,
    int status, int code, int batimeout)
{
	struct ath_softc *sc = ni->ni_ic->ic_softc;
	int tid = tap->txa_tid;
	struct ath_node *an = ATH_NODE(ni);
	struct ath_tid *atid = &an->an_tid[tid];
	int r;

	DPRINTF(sc, ATH_DEBUG_SW_TX_CTRL,
	    "%s: %6D: called; status=%d, code=%d, batimeout=%d\n", __func__,
	    ni->ni_macaddr,
	    ":",
	    status, code, batimeout);

	DPRINTF(sc, ATH_DEBUG_SW_TX_CTRL,
	    "%s: txa_start=%d, ni_txseqs=%d\n",
	    __func__, tap->txa_start, ni->ni_txseqs[tid]);

	/*
	 * Call this first, so the interface flags get updated
	 * before the TID is unpaused. Otherwise a race condition
	 * exists where the unpaused TID still doesn't yet have
	 * IEEE80211_AGGR_RUNNING set.
	 */
	r = sc->sc_addba_response(ni, tap, status, code, batimeout);

	ATH_TX_LOCK(sc);
	atid->addba_tx_pending = 0;
	/*
	 * XXX dirty!
	 * Slide the BAW left edge to wherever net80211 left it for us.
	 * Read above for more information.
	 */
	tap->txa_start = ni->ni_txseqs[tid];
	ath_tx_tid_resume(sc, atid);
	ATH_TX_UNLOCK(sc);
	return r;
}


/*
 * Stop ADDBA on a queue.
 *
 * This can be called whilst BAR TX is currently active on the queue,
 * so make sure this is unblocked before continuing.
 */
void
ath_addba_stop(struct ieee80211_node *ni, struct ieee80211_tx_ampdu *tap)
{
	struct ath_softc *sc = ni->ni_ic->ic_softc;
	int tid = tap->txa_tid;
	struct ath_node *an = ATH_NODE(ni);
	struct ath_tid *atid = &an->an_tid[tid];
	ath_bufhead bf_cq;
	struct ath_buf *bf;

	DPRINTF(sc, ATH_DEBUG_SW_TX_CTRL, "%s: %6D: called\n",
	    __func__,
	    ni->ni_macaddr,
	    ":");

	/*
	 * Pause TID traffic early, so there aren't any races
	 * Unblock the pending BAR held traffic, if it's currently paused.
	 */
	ATH_TX_LOCK(sc);
	ath_tx_tid_pause(sc, atid);
	if (atid->bar_wait) {
		/*
		 * bar_unsuspend() expects bar_tx == 1, as it should be
		 * called from the TX completion path.  This quietens
		 * the warning.  It's cleared for us anyway.
		 */
		atid->bar_tx = 1;
		ath_tx_tid_bar_unsuspend(sc, atid);
	}
	ATH_TX_UNLOCK(sc);

	/* There's no need to hold the TXQ lock here */
	sc->sc_addba_stop(ni, tap);

	/*
	 * ath_tx_tid_cleanup will resume the TID if possible, otherwise
	 * it'll set the cleanup flag, and it'll be unpaused once
	 * things have been cleaned up.
	 */
	TAILQ_INIT(&bf_cq);
	ATH_TX_LOCK(sc);

	/*
	 * In case there's a followup call to this, only call it
	 * if we don't have a cleanup in progress.
	 *
	 * Since we've paused the queue above, we need to make
	 * sure we unpause if there's already a cleanup in
	 * progress - it means something else is also doing
	 * this stuff, so we don't need to also keep it paused.
	 */
	if (atid->cleanup_inprogress) {
		ath_tx_tid_resume(sc, atid);
	} else {
		ath_tx_tid_cleanup(sc, an, tid, &bf_cq);
		/*
		 * Unpause the TID if no cleanup is required.
		 */
		if (! atid->cleanup_inprogress)
			ath_tx_tid_resume(sc, atid);
	}
	ATH_TX_UNLOCK(sc);

	/* Handle completing frames and fail them */
	while ((bf = TAILQ_FIRST(&bf_cq)) != NULL) {
		TAILQ_REMOVE(&bf_cq, bf, bf_list);
		ath_tx_default_comp(sc, bf, 1);
	}

}

/*
 * Handle a node reassociation.
 *
 * We may have a bunch of frames queued to the hardware; those need
 * to be marked as cleanup.
 */
void
ath_tx_node_reassoc(struct ath_softc *sc, struct ath_node *an)
{
	struct ath_tid *tid;
	int i;
	ath_bufhead bf_cq;
	struct ath_buf *bf;

	TAILQ_INIT(&bf_cq);

	ATH_TX_UNLOCK_ASSERT(sc);

	ATH_TX_LOCK(sc);
	for (i = 0; i < IEEE80211_TID_SIZE; i++) {
		tid = &an->an_tid[i];
		if (tid->hwq_depth == 0)
			continue;
		DPRINTF(sc, ATH_DEBUG_NODE,
		    "%s: %6D: TID %d: cleaning up TID\n",
		    __func__,
		    an->an_node.ni_macaddr,
		    ":",
		    i);
		/*
		 * In case there's a followup call to this, only call it
		 * if we don't have a cleanup in progress.
		 */
		if (! tid->cleanup_inprogress) {
			ath_tx_tid_pause(sc, tid);
			ath_tx_tid_cleanup(sc, an, i, &bf_cq);
			/*
			 * Unpause the TID if no cleanup is required.
			 */
			if (! tid->cleanup_inprogress)
				ath_tx_tid_resume(sc, tid);
		}
	}
	ATH_TX_UNLOCK(sc);

	/* Handle completing frames and fail them */
	while ((bf = TAILQ_FIRST(&bf_cq)) != NULL) {
		TAILQ_REMOVE(&bf_cq, bf, bf_list);
		ath_tx_default_comp(sc, bf, 1);
	}
}

/*
 * Note: net80211 bar_timeout() doesn't call this function on BAR failure;
 * it simply tears down the aggregation session. Ew.
 *
 * It however will call ieee80211_ampdu_stop() which will call
 * ic->ic_addba_stop().
 *
 * XXX This uses a hard-coded max BAR count value; the whole
 * XXX BAR TX success or failure should be better handled!
 */
void
ath_bar_response(struct ieee80211_node *ni, struct ieee80211_tx_ampdu *tap,
    int status)
{
	struct ath_softc *sc = ni->ni_ic->ic_softc;
	int tid = tap->txa_tid;
	struct ath_node *an = ATH_NODE(ni);
	struct ath_tid *atid = &an->an_tid[tid];
	int attempts = tap->txa_attempts;
	int old_txa_start;

	DPRINTF(sc, ATH_DEBUG_SW_TX_BAR,
	    "%s: %6D: called; txa_tid=%d, atid->tid=%d, status=%d, attempts=%d, txa_start=%d, txa_seqpending=%d\n",
	    __func__,
	    ni->ni_macaddr,
	    ":",
	    tap->txa_tid,
	    atid->tid,
	    status,
	    attempts,
	    tap->txa_start,
	    tap->txa_seqpending);

	/* Note: This may update the BAW details */
	/*
	 * XXX What if this does slide the BAW along? We need to somehow
	 * XXX either fix things when it does happen, or prevent the
	 * XXX seqpending value to be anything other than exactly what
	 * XXX the hell we want!
	 *
	 * XXX So for now, how I do this inside the TX lock for now
	 * XXX and just correct it afterwards? The below condition should
	 * XXX never happen and if it does I need to fix all kinds of things.
	 */
	ATH_TX_LOCK(sc);
	old_txa_start = tap->txa_start;
	sc->sc_bar_response(ni, tap, status);
	if (tap->txa_start != old_txa_start) {
		device_printf(sc->sc_dev, "%s: tid=%d; txa_start=%d, old=%d, adjusting\n",
		    __func__,
		    tid,
		    tap->txa_start,
		    old_txa_start);
	}
	tap->txa_start = old_txa_start;
	ATH_TX_UNLOCK(sc);

	/* Unpause the TID */
	/*
	 * XXX if this is attempt=50, the TID will be downgraded
	 * XXX to a non-aggregate session. So we must unpause the
	 * XXX TID here or it'll never be done.
	 *
	 * Also, don't call it if bar_tx/bar_wait are 0; something
	 * has beaten us to the punch? (XXX figure out what?)
	 */
	if (status == 0 || attempts == 50) {
		ATH_TX_LOCK(sc);
		if (atid->bar_tx == 0 || atid->bar_wait == 0)
			DPRINTF(sc, ATH_DEBUG_SW_TX_BAR,
			    "%s: huh? bar_tx=%d, bar_wait=%d\n",
			    __func__,
			    atid->bar_tx, atid->bar_wait);
		else
			ath_tx_tid_bar_unsuspend(sc, atid);
		ATH_TX_UNLOCK(sc);
	}
}

/*
 * This is called whenever the pending ADDBA request times out.
 * Unpause and reschedule the TID.
 */
void
ath_addba_response_timeout(struct ieee80211_node *ni,
    struct ieee80211_tx_ampdu *tap)
{
	struct ath_softc *sc = ni->ni_ic->ic_softc;
	int tid = tap->txa_tid;
	struct ath_node *an = ATH_NODE(ni);
	struct ath_tid *atid = &an->an_tid[tid];

	DPRINTF(sc, ATH_DEBUG_SW_TX_CTRL,
	    "%s: %6D: TID=%d, called; resuming\n",
	    __func__,
	    ni->ni_macaddr,
	    ":",
	    tid);

	ATH_TX_LOCK(sc);
	atid->addba_tx_pending = 0;
	ATH_TX_UNLOCK(sc);

	/* Note: This updates the aggregate state to (again) pending */
	sc->sc_addba_response_timeout(ni, tap);

	/* Unpause the TID; which reschedules it */
	ATH_TX_LOCK(sc);
	ath_tx_tid_resume(sc, atid);
	ATH_TX_UNLOCK(sc);
}

/*
 * Check if a node is asleep or not.
 */
int
ath_tx_node_is_asleep(struct ath_softc *sc, struct ath_node *an)
{

	ATH_TX_LOCK_ASSERT(sc);

	return (an->an_is_powersave);
}

/*
 * Mark a node as currently "in powersaving."
 * This suspends all traffic on the node.
 *
 * This must be called with the node/tx locks free.
 *
 * XXX TODO: the locking silliness below is due to how the node
 * locking currently works.  Right now, the node lock is grabbed
 * to do rate control lookups and these are done with the TX
 * queue lock held.  This means the node lock can't be grabbed
 * first here or a LOR will occur.
 *
 * Eventually (hopefully!) the TX path code will only grab
 * the TXQ lock when transmitting and the ath_node lock when
 * doing node/TID operations.  There are other complications -
 * the sched/unsched operations involve walking the per-txq
 * 'active tid' list and this requires both locks to be held.
 */
void
ath_tx_node_sleep(struct ath_softc *sc, struct ath_node *an)
{
	struct ath_tid *atid;
	struct ath_txq *txq;
	int tid;

	ATH_TX_UNLOCK_ASSERT(sc);

	/* Suspend all traffic on the node */
	ATH_TX_LOCK(sc);

	if (an->an_is_powersave) {
		DPRINTF(sc, ATH_DEBUG_XMIT,
		    "%s: %6D: node was already asleep!\n",
		    __func__, an->an_node.ni_macaddr, ":");
		ATH_TX_UNLOCK(sc);
		return;
	}

	for (tid = 0; tid < IEEE80211_TID_SIZE; tid++) {
		atid = &an->an_tid[tid];
		txq = sc->sc_ac2q[atid->ac];

		ath_tx_tid_pause(sc, atid);
	}

	/* Mark node as in powersaving */
	an->an_is_powersave = 1;

	ATH_TX_UNLOCK(sc);
}

/*
 * Mark a node as currently "awake."
 * This resumes all traffic to the node.
 */
void
ath_tx_node_wakeup(struct ath_softc *sc, struct ath_node *an)
{
	struct ath_tid *atid;
	struct ath_txq *txq;
	int tid;

	ATH_TX_UNLOCK_ASSERT(sc);

	ATH_TX_LOCK(sc);

	/* !? */
	if (an->an_is_powersave == 0) {
		ATH_TX_UNLOCK(sc);
		DPRINTF(sc, ATH_DEBUG_XMIT,
		    "%s: an=%p: node was already awake\n",
		    __func__, an);
		return;
	}

	/* Mark node as awake */
	an->an_is_powersave = 0;
	/*
	 * Clear any pending leaked frame requests
	 */
	an->an_leak_count = 0;

	for (tid = 0; tid < IEEE80211_TID_SIZE; tid++) {
		atid = &an->an_tid[tid];
		txq = sc->sc_ac2q[atid->ac];

		ath_tx_tid_resume(sc, atid);
	}
	ATH_TX_UNLOCK(sc);
}

static int
ath_legacy_dma_txsetup(struct ath_softc *sc)
{

	/* nothing new needed */
	return (0);
}

static int
ath_legacy_dma_txteardown(struct ath_softc *sc)
{

	/* nothing new needed */
	return (0);
}

void
ath_xmit_setup_legacy(struct ath_softc *sc)
{
	/*
	 * For now, just set the descriptor length to sizeof(ath_desc);
	 * worry about extracting the real length out of the HAL later.
	 */
	sc->sc_tx_desclen = sizeof(struct ath_desc);
	sc->sc_tx_statuslen = sizeof(struct ath_desc);
	sc->sc_tx_nmaps = 1;	/* only one buffer per TX desc */

	sc->sc_tx.xmit_setup = ath_legacy_dma_txsetup;
	sc->sc_tx.xmit_teardown = ath_legacy_dma_txteardown;
	sc->sc_tx.xmit_attach_comp_func = ath_legacy_attach_comp_func;

	sc->sc_tx.xmit_dma_restart = ath_legacy_tx_dma_restart;
	sc->sc_tx.xmit_handoff = ath_legacy_xmit_handoff;

	sc->sc_tx.xmit_drain = ath_legacy_tx_drain;
}
