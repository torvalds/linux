/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002-2009 Sam Leffler, Errno Consulting
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
 *
 * $FreeBSD$
 */
#ifndef	__IF_ATH_TX_H__
#define	__IF_ATH_TX_H__

/*
 * some general macros
 */
#define	INCR(_l, _sz)		(_l) ++; (_l) &= ((_sz) - 1)
/*
 * return block-ack bitmap index given sequence and starting sequence
 */
#define	ATH_BA_INDEX(_st, _seq)	(((_seq) - (_st)) & (IEEE80211_SEQ_RANGE - 1))

#define	WME_BA_BMP_SIZE	64
#define	WME_MAX_BA	WME_BA_BMP_SIZE

/*
 * How 'busy' to try and keep the hardware txq
 */
#define	ATH_AGGR_MIN_QDEPTH		2
#define	ATH_NONAGGR_MIN_QDEPTH		32

/*
 * Watermark for scheduling TIDs in order to maximise aggregation.
 *
 * If hwq_depth is greater than this, don't schedule the TID
 * for packet scheduling - the hardware is already busy servicing
 * this TID.
 *
 * If hwq_depth is less than this, schedule the TID for packet
 * scheduling in the completion handler.
 */
#define	ATH_AGGR_SCHED_HIGH		4
#define	ATH_AGGR_SCHED_LOW		2

/*
 * return whether a bit at index _n in bitmap _bm is set
 * _sz is the size of the bitmap
 */
#define	ATH_BA_ISSET(_bm, _n)	(((_n) < (WME_BA_BMP_SIZE)) &&		\
	    ((_bm)[(_n) >> 5] & (1 << ((_n) & 31))))


/* extracting the seqno from buffer seqno */
#define	SEQNO(_a)	((_a) >> IEEE80211_SEQ_SEQ_SHIFT)

/*
 * Whether the current sequence number is within the
 * BAW.
 */
#define	BAW_WITHIN(_start, _bawsz, _seqno)	\
	    ((((_seqno) - (_start)) & 4095) < (_bawsz))

/*
 * Maximum aggregate size
 */
#define	ATH_AGGR_MAXSIZE	65530

extern void ath_tx_node_flush(struct ath_softc *sc, struct ath_node *an);
extern void ath_tx_txq_drain(struct ath_softc *sc, struct ath_txq *txq);
extern void ath_txfrag_cleanup(struct ath_softc *sc, ath_bufhead *frags,
    struct ieee80211_node *ni);
extern int ath_txfrag_setup(struct ath_softc *sc, ath_bufhead *frags,
    struct mbuf *m0, struct ieee80211_node *ni);
extern int ath_tx_start(struct ath_softc *sc, struct ieee80211_node *ni,
    struct ath_buf *bf, struct mbuf *m0);
extern int ath_raw_xmit(struct ieee80211_node *ni, struct mbuf *m,
    const struct ieee80211_bpf_params *params);

/* software queue stuff */
extern void ath_tx_swq(struct ath_softc *sc, struct ieee80211_node *ni,
    struct ath_txq *txq, int queue_to_head, struct ath_buf *bf);
extern void ath_tx_tid_init(struct ath_softc *sc, struct ath_node *an);
extern void ath_tx_tid_hw_queue_aggr(struct ath_softc *sc, struct ath_node *an,
    struct ath_tid *tid);
extern void ath_tx_tid_hw_queue_norm(struct ath_softc *sc, struct ath_node *an,
    struct ath_tid *tid);
extern void ath_txq_sched(struct ath_softc *sc, struct ath_txq *txq);
extern void ath_tx_normal_comp(struct ath_softc *sc, struct ath_buf *bf,
    int fail);
extern void ath_tx_aggr_comp(struct ath_softc *sc, struct ath_buf *bf,
    int fail);
extern void ath_tx_addto_baw(struct ath_softc *sc, struct ath_node *an,
    struct ath_tid *tid, struct ath_buf *bf);
extern struct ieee80211_tx_ampdu * ath_tx_get_tx_tid(struct ath_node *an,
    int tid);
extern void ath_tx_tid_sched(struct ath_softc *sc, struct ath_tid *tid);

/* TX addba handling */
extern	int ath_addba_request(struct ieee80211_node *ni,
    struct ieee80211_tx_ampdu *tap, int dialogtoken,
    int baparamset, int batimeout);
extern	int ath_addba_response(struct ieee80211_node *ni,
    struct ieee80211_tx_ampdu *tap, int dialogtoken,
    int code, int batimeout);
extern	void ath_addba_stop(struct ieee80211_node *ni,
    struct ieee80211_tx_ampdu *tap);
extern	void ath_bar_response(struct ieee80211_node *ni,
     struct ieee80211_tx_ampdu *tap, int status);
extern	void ath_addba_response_timeout(struct ieee80211_node *ni,
    struct ieee80211_tx_ampdu *tap);

/*
 * AP mode power save handling (of stations)
 */
extern	void ath_tx_node_sleep(struct ath_softc *sc, struct ath_node *an);
extern	void ath_tx_node_wakeup(struct ath_softc *sc, struct ath_node *an);
extern	int ath_tx_node_is_asleep(struct ath_softc *sc, struct ath_node *an);
extern	void ath_tx_node_reassoc(struct ath_softc *sc, struct ath_node *an);

/*
 * Hardware queue stuff
 */
extern	void ath_tx_push_pending(struct ath_softc *sc, struct ath_txq *txq);

/*
 * Misc debugging stuff
 */
#ifdef	ATH_DEBUG_ALQ
extern	void ath_tx_alq_post(struct ath_softc *sc, struct ath_buf *bf_first);
#endif	/* ATH_DEBUG_ALQ */

/*
 * Setup path
 */
#define	ath_txdma_setup(_sc)			\
	(_sc)->sc_tx.xmit_setup(_sc)
#define	ath_txdma_teardown(_sc)			\
	(_sc)->sc_tx.xmit_teardown(_sc)
#define	ath_txq_restart_dma(_sc, _txq)		\
	(_sc)->sc_tx.xmit_dma_restart((_sc), (_txq))
#define	ath_tx_handoff(_sc, _txq, _bf)		\
	(_sc)->sc_tx.xmit_handoff((_sc), (_txq), (_bf))
#define	ath_draintxq(_sc, _rtype)		\
	(_sc)->sc_tx.xmit_drain((_sc), (_rtype))

extern	void ath_xmit_setup_legacy(struct ath_softc *sc);

#endif
