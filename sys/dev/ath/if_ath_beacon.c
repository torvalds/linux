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

#include <net/bpf.h>

#ifdef INET
#include <netinet/in.h>
#include <netinet/if_ether.h>
#endif

#include <dev/ath/if_athvar.h>

#include <dev/ath/if_ath_debug.h>
#include <dev/ath/if_ath_misc.h>
#include <dev/ath/if_ath_tx.h>
#include <dev/ath/if_ath_beacon.h>

#ifdef ATH_TX99_DIAG
#include <dev/ath/ath_tx99/ath_tx99.h>
#endif

/*
 * Setup a h/w transmit queue for beacons.
 */
int
ath_beaconq_setup(struct ath_softc *sc)
{
	struct ath_hal *ah = sc->sc_ah;
	HAL_TXQ_INFO qi;

	memset(&qi, 0, sizeof(qi));
	qi.tqi_aifs = HAL_TXQ_USEDEFAULT;
	qi.tqi_cwmin = HAL_TXQ_USEDEFAULT;
	qi.tqi_cwmax = HAL_TXQ_USEDEFAULT;
	/* NB: for dynamic turbo, don't enable any other interrupts */
	qi.tqi_qflags = HAL_TXQ_TXDESCINT_ENABLE;
	if (sc->sc_isedma)
		qi.tqi_qflags |= HAL_TXQ_TXOKINT_ENABLE |
		    HAL_TXQ_TXERRINT_ENABLE;

	return ath_hal_setuptxqueue(ah, HAL_TX_QUEUE_BEACON, &qi);
}

/*
 * Setup the transmit queue parameters for the beacon queue.
 */
int
ath_beaconq_config(struct ath_softc *sc)
{
#define	ATH_EXPONENT_TO_VALUE(v)	((1<<(v))-1)
	struct ieee80211com *ic = &sc->sc_ic;
	struct ath_hal *ah = sc->sc_ah;
	HAL_TXQ_INFO qi;

	ath_hal_gettxqueueprops(ah, sc->sc_bhalq, &qi);
	if (ic->ic_opmode == IEEE80211_M_HOSTAP ||
	    ic->ic_opmode == IEEE80211_M_MBSS) {
		/*
		 * Always burst out beacon and CAB traffic.
		 */
		qi.tqi_aifs = ATH_BEACON_AIFS_DEFAULT;
		qi.tqi_cwmin = ATH_BEACON_CWMIN_DEFAULT;
		qi.tqi_cwmax = ATH_BEACON_CWMAX_DEFAULT;
	} else {
		struct chanAccParams chp;
		struct wmeParams *wmep;

		ieee80211_wme_ic_getparams(ic, &chp);
		wmep = &chp.cap_wmeParams[WME_AC_BE];

		/*
		 * Adhoc mode; important thing is to use 2x cwmin.
		 */
		qi.tqi_aifs = wmep->wmep_aifsn;
		qi.tqi_cwmin = 2*ATH_EXPONENT_TO_VALUE(wmep->wmep_logcwmin);
		qi.tqi_cwmax = ATH_EXPONENT_TO_VALUE(wmep->wmep_logcwmax);
	}

	if (!ath_hal_settxqueueprops(ah, sc->sc_bhalq, &qi)) {
		device_printf(sc->sc_dev, "unable to update parameters for "
			"beacon hardware queue!\n");
		return 0;
	} else {
		ath_hal_resettxqueue(ah, sc->sc_bhalq); /* push to h/w */
		return 1;
	}
#undef ATH_EXPONENT_TO_VALUE
}

/*
 * Allocate and setup an initial beacon frame.
 */
int
ath_beacon_alloc(struct ath_softc *sc, struct ieee80211_node *ni)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ath_vap *avp = ATH_VAP(vap);
	struct ath_buf *bf;
	struct mbuf *m;
	int error;

	bf = avp->av_bcbuf;
	DPRINTF(sc, ATH_DEBUG_NODE, "%s: bf_m=%p, bf_node=%p\n",
	    __func__, bf->bf_m, bf->bf_node);
	if (bf->bf_m != NULL) {
		bus_dmamap_unload(sc->sc_dmat, bf->bf_dmamap);
		m_freem(bf->bf_m);
		bf->bf_m = NULL;
	}
	if (bf->bf_node != NULL) {
		ieee80211_free_node(bf->bf_node);
		bf->bf_node = NULL;
	}

	/*
	 * NB: the beacon data buffer must be 32-bit aligned;
	 * we assume the mbuf routines will return us something
	 * with this alignment (perhaps should assert).
	 */
	m = ieee80211_beacon_alloc(ni);
	if (m == NULL) {
		device_printf(sc->sc_dev, "%s: cannot get mbuf\n", __func__);
		sc->sc_stats.ast_be_nombuf++;
		return ENOMEM;
	}
	error = bus_dmamap_load_mbuf_sg(sc->sc_dmat, bf->bf_dmamap, m,
				     bf->bf_segs, &bf->bf_nseg,
				     BUS_DMA_NOWAIT);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "%s: cannot map mbuf, bus_dmamap_load_mbuf_sg returns %d\n",
		    __func__, error);
		m_freem(m);
		return error;
	}

	/*
	 * Calculate a TSF adjustment factor required for staggered
	 * beacons.  Note that we assume the format of the beacon
	 * frame leaves the tstamp field immediately following the
	 * header.
	 */
	if (sc->sc_stagbeacons && avp->av_bslot > 0) {
		uint64_t tsfadjust;
		struct ieee80211_frame *wh;

		/*
		 * The beacon interval is in TU's; the TSF is in usecs.
		 * We figure out how many TU's to add to align the timestamp
		 * then convert to TSF units and handle byte swapping before
		 * inserting it in the frame.  The hardware will then add this
		 * each time a beacon frame is sent.  Note that we align vap's
		 * 1..N and leave vap 0 untouched.  This means vap 0 has a
		 * timestamp in one beacon interval while the others get a
		 * timstamp aligned to the next interval.
		 */
		tsfadjust = ni->ni_intval *
		    (ATH_BCBUF - avp->av_bslot) / ATH_BCBUF;
		tsfadjust = htole64(tsfadjust << 10);	/* TU -> TSF */

		DPRINTF(sc, ATH_DEBUG_BEACON,
		    "%s: %s beacons bslot %d intval %u tsfadjust %llu\n",
		    __func__, sc->sc_stagbeacons ? "stagger" : "burst",
		    avp->av_bslot, ni->ni_intval,
		    (long long unsigned) le64toh(tsfadjust));

		wh = mtod(m, struct ieee80211_frame *);
		memcpy(&wh[1], &tsfadjust, sizeof(tsfadjust));
	}
	bf->bf_m = m;
	bf->bf_node = ieee80211_ref_node(ni);

	return 0;
}

/*
 * Setup the beacon frame for transmit.
 */
static void
ath_beacon_setup(struct ath_softc *sc, struct ath_buf *bf)
{
#define	USE_SHPREAMBLE(_ic) \
	(((_ic)->ic_flags & (IEEE80211_F_SHPREAMBLE | IEEE80211_F_USEBARKER))\
		== IEEE80211_F_SHPREAMBLE)
	struct ieee80211_node *ni = bf->bf_node;
	struct ieee80211com *ic = ni->ni_ic;
	struct mbuf *m = bf->bf_m;
	struct ath_hal *ah = sc->sc_ah;
	struct ath_desc *ds;
	int flags, antenna;
	const HAL_RATE_TABLE *rt;
	u_int8_t rix, rate;
	HAL_DMA_ADDR bufAddrList[4];
	uint32_t segLenList[4];
	HAL_11N_RATE_SERIES rc[4];

	DPRINTF(sc, ATH_DEBUG_BEACON_PROC, "%s: m %p len %u\n",
		__func__, m, m->m_len);

	/* setup descriptors */
	ds = bf->bf_desc;
	bf->bf_last = bf;
	bf->bf_lastds = ds;

	flags = HAL_TXDESC_NOACK;
	if (ic->ic_opmode == IEEE80211_M_IBSS && sc->sc_hasveol) {
		/* self-linked descriptor */
		ath_hal_settxdesclink(sc->sc_ah, ds, bf->bf_daddr);
		flags |= HAL_TXDESC_VEOL;
		/*
		 * Let hardware handle antenna switching.
		 */
		antenna = sc->sc_txantenna;
	} else {
		ath_hal_settxdesclink(sc->sc_ah, ds, 0);
		/*
		 * Switch antenna every 4 beacons.
		 * XXX assumes two antenna
		 */
		if (sc->sc_txantenna != 0)
			antenna = sc->sc_txantenna;
		else if (sc->sc_stagbeacons && sc->sc_nbcnvaps != 0)
			antenna = ((sc->sc_stats.ast_be_xmit / sc->sc_nbcnvaps) & 4 ? 2 : 1);
		else
			antenna = (sc->sc_stats.ast_be_xmit & 4 ? 2 : 1);
	}

	KASSERT(bf->bf_nseg == 1,
		("multi-segment beacon frame; nseg %u", bf->bf_nseg));

	/*
	 * Calculate rate code.
	 * XXX everything at min xmit rate
	 */
	rix = 0;
	rt = sc->sc_currates;
	rate = rt->info[rix].rateCode;
	if (USE_SHPREAMBLE(ic))
		rate |= rt->info[rix].shortPreamble;
	ath_hal_setuptxdesc(ah, ds
		, m->m_len + IEEE80211_CRC_LEN	/* frame length */
		, sizeof(struct ieee80211_frame)/* header length */
		, HAL_PKT_TYPE_BEACON		/* Atheros packet type */
		, ieee80211_get_node_txpower(ni)	/* txpower XXX */
		, rate, 1			/* series 0 rate/tries */
		, HAL_TXKEYIX_INVALID		/* no encryption */
		, antenna			/* antenna mode */
		, flags				/* no ack, veol for beacons */
		, 0				/* rts/cts rate */
		, 0				/* rts/cts duration */
	);

	/*
	 * The EDMA HAL currently assumes that _all_ rate control
	 * settings are done in ath_hal_set11nratescenario(), rather
	 * than in ath_hal_setuptxdesc().
	 */
	if (sc->sc_isedma) {
		memset(&rc, 0, sizeof(rc));

		rc[0].ChSel = sc->sc_txchainmask;
		rc[0].Tries = 1;
		rc[0].Rate = rt->info[rix].rateCode;
		rc[0].RateIndex = rix;
		rc[0].tx_power_cap = 0x3f;
		rc[0].PktDuration =
		    ath_hal_computetxtime(ah, rt, roundup(m->m_len, 4),
		        rix, 0, AH_TRUE);
		ath_hal_set11nratescenario(ah, ds, 0, 0, rc, 4, flags);
	}

	/* NB: beacon's BufLen must be a multiple of 4 bytes */
	segLenList[0] = roundup(m->m_len, 4);
	segLenList[1] = segLenList[2] = segLenList[3] = 0;
	bufAddrList[0] = bf->bf_segs[0].ds_addr;
	bufAddrList[1] = bufAddrList[2] = bufAddrList[3] = 0;
	ath_hal_filltxdesc(ah, ds
		, bufAddrList
		, segLenList
		, 0				/* XXX desc id */
		, sc->sc_bhalq			/* hardware TXQ */
		, AH_TRUE			/* first segment */
		, AH_TRUE			/* last segment */
		, ds				/* first descriptor */
	);
#if 0
	ath_desc_swap(ds);
#endif
#undef USE_SHPREAMBLE
}

void
ath_beacon_update(struct ieee80211vap *vap, int item)
{
	struct ieee80211_beacon_offsets *bo = &vap->iv_bcn_off;

	setbit(bo->bo_flags, item);
}

/*
 * Handle a beacon miss.
 */
void
ath_beacon_miss(struct ath_softc *sc)
{
	HAL_SURVEY_SAMPLE hs;
	HAL_BOOL ret;
	uint32_t hangs;

	bzero(&hs, sizeof(hs));

	ret = ath_hal_get_mib_cycle_counts(sc->sc_ah, &hs);

	if (ath_hal_gethangstate(sc->sc_ah, 0xffff, &hangs) && hangs != 0) {
		DPRINTF(sc, ATH_DEBUG_BEACON,
		    "%s: hang=0x%08x\n",
		    __func__,
		    hangs);
	}

#ifdef	ATH_DEBUG_ALQ
	if (if_ath_alq_checkdebug(&sc->sc_alq, ATH_ALQ_MISSED_BEACON))
		if_ath_alq_post(&sc->sc_alq, ATH_ALQ_MISSED_BEACON, 0, NULL);
#endif

	DPRINTF(sc, ATH_DEBUG_BEACON,
	    "%s: valid=%d, txbusy=%u, rxbusy=%u, chanbusy=%u, "
	    "extchanbusy=%u, cyclecount=%u\n",
	    __func__,
	    ret,
	    hs.tx_busy,
	    hs.rx_busy,
	    hs.chan_busy,
	    hs.ext_chan_busy,
	    hs.cycle_count);
}

/*
 * Transmit a beacon frame at SWBA.  Dynamic updates to the
 * frame contents are done as needed and the slot time is
 * also adjusted based on current state.
 */
void
ath_beacon_proc(void *arg, int pending)
{
	struct ath_softc *sc = arg;
	struct ath_hal *ah = sc->sc_ah;
	struct ieee80211vap *vap;
	struct ath_buf *bf;
	int slot, otherant;
	uint32_t bfaddr;

	DPRINTF(sc, ATH_DEBUG_BEACON_PROC, "%s: pending %u\n",
		__func__, pending);
	/*
	 * Check if the previous beacon has gone out.  If
	 * not don't try to post another, skip this period
	 * and wait for the next.  Missed beacons indicate
	 * a problem and should not occur.  If we miss too
	 * many consecutive beacons reset the device.
	 */
	if (ath_hal_numtxpending(ah, sc->sc_bhalq) != 0) {
		sc->sc_bmisscount++;
		sc->sc_stats.ast_be_missed++;
		ath_beacon_miss(sc);
		DPRINTF(sc, ATH_DEBUG_BEACON,
			"%s: missed %u consecutive beacons\n",
			__func__, sc->sc_bmisscount);
		if (sc->sc_bmisscount >= ath_bstuck_threshold)
			taskqueue_enqueue(sc->sc_tq, &sc->sc_bstucktask);
		return;
	}
	if (sc->sc_bmisscount != 0) {
		DPRINTF(sc, ATH_DEBUG_BEACON,
			"%s: resume beacon xmit after %u misses\n",
			__func__, sc->sc_bmisscount);
		sc->sc_bmisscount = 0;
#ifdef	ATH_DEBUG_ALQ
		if (if_ath_alq_checkdebug(&sc->sc_alq, ATH_ALQ_RESUME_BEACON))
			if_ath_alq_post(&sc->sc_alq, ATH_ALQ_RESUME_BEACON, 0, NULL);
#endif
	}

	if (sc->sc_stagbeacons) {			/* staggered beacons */
		struct ieee80211com *ic = &sc->sc_ic;
		uint32_t tsftu;

		tsftu = ath_hal_gettsf32(ah) >> 10;
		/* XXX lintval */
		slot = ((tsftu % ic->ic_lintval) * ATH_BCBUF) / ic->ic_lintval;
		vap = sc->sc_bslot[(slot+1) % ATH_BCBUF];
		bfaddr = 0;
		if (vap != NULL && vap->iv_state >= IEEE80211_S_RUN) {
			bf = ath_beacon_generate(sc, vap);
			if (bf != NULL)
				bfaddr = bf->bf_daddr;
		}
	} else {					/* burst'd beacons */
		uint32_t *bflink = &bfaddr;

		for (slot = 0; slot < ATH_BCBUF; slot++) {
			vap = sc->sc_bslot[slot];
			if (vap != NULL && vap->iv_state >= IEEE80211_S_RUN) {
				bf = ath_beacon_generate(sc, vap);
				/*
				 * XXX TODO: this should use settxdesclinkptr()
				 * otherwise it won't work for EDMA chipsets!
				 */
				if (bf != NULL) {
					/* XXX should do this using the ds */
					*bflink = bf->bf_daddr;
					ath_hal_gettxdesclinkptr(sc->sc_ah,
					    bf->bf_desc, &bflink);
				}
			}
		}
		/*
		 * XXX TODO: this should use settxdesclinkptr()
		 * otherwise it won't work for EDMA chipsets!
		 */
		*bflink = 0;				/* terminate list */
	}

	/*
	 * Handle slot time change when a non-ERP station joins/leaves
	 * an 11g network.  The 802.11 layer notifies us via callback,
	 * we mark updateslot, then wait one beacon before effecting
	 * the change.  This gives associated stations at least one
	 * beacon interval to note the state change.
	 */
	/* XXX locking */
	if (sc->sc_updateslot == UPDATE) {
		sc->sc_updateslot = COMMIT;	/* commit next beacon */
		sc->sc_slotupdate = slot;
	} else if (sc->sc_updateslot == COMMIT && sc->sc_slotupdate == slot)
		ath_setslottime(sc);		/* commit change to h/w */

	/*
	 * Check recent per-antenna transmit statistics and flip
	 * the default antenna if noticeably more frames went out
	 * on the non-default antenna.
	 * XXX assumes 2 anntenae
	 */
	if (!sc->sc_diversity && (!sc->sc_stagbeacons || slot == 0)) {
		otherant = sc->sc_defant & 1 ? 2 : 1;
		if (sc->sc_ant_tx[otherant] > sc->sc_ant_tx[sc->sc_defant] + 2)
			ath_setdefantenna(sc, otherant);
		sc->sc_ant_tx[1] = sc->sc_ant_tx[2] = 0;
	}

	/* Program the CABQ with the contents of the CABQ txq and start it */
	ATH_TXQ_LOCK(sc->sc_cabq);
	ath_beacon_cabq_start(sc);
	ATH_TXQ_UNLOCK(sc->sc_cabq);

	/* Program the new beacon frame if we have one for this interval */
	if (bfaddr != 0) {
		/*
		 * Stop any current dma and put the new frame on the queue.
		 * This should never fail since we check above that no frames
		 * are still pending on the queue.
		 */
		if (! sc->sc_isedma) {
			if (!ath_hal_stoptxdma(ah, sc->sc_bhalq)) {
				DPRINTF(sc, ATH_DEBUG_ANY,
					"%s: beacon queue %u did not stop?\n",
					__func__, sc->sc_bhalq);
			}
		}
		/* NB: cabq traffic should already be queued and primed */

		ath_hal_puttxbuf(ah, sc->sc_bhalq, bfaddr);
		ath_hal_txstart(ah, sc->sc_bhalq);

		sc->sc_stats.ast_be_xmit++;
	}
}

static void
ath_beacon_cabq_start_edma(struct ath_softc *sc)
{
	struct ath_buf *bf, *bf_last;
	struct ath_txq *cabq = sc->sc_cabq;
#if 0
	struct ath_buf *bfi;
	int i = 0;
#endif

	ATH_TXQ_LOCK_ASSERT(cabq);

	if (TAILQ_EMPTY(&cabq->axq_q))
		return;
	bf = TAILQ_FIRST(&cabq->axq_q);
	bf_last = TAILQ_LAST(&cabq->axq_q, axq_q_s);

	/*
	 * This is a dirty, dirty hack to push the contents of
	 * the cabq staging queue into the FIFO.
	 *
	 * This ideally should live in the EDMA code file
	 * and only push things into the CABQ if there's a FIFO
	 * slot.
	 *
	 * We can't treat this like a normal TX queue because
	 * in the case of multi-VAP traffic, we may have to flush
	 * the CABQ each new (staggered) beacon that goes out.
	 * But for non-staggered beacons, we could in theory
	 * handle multicast traffic for all VAPs in one FIFO
	 * push.  Just keep all of this in mind if you're wondering
	 * how to correctly/better handle multi-VAP CABQ traffic
	 * with EDMA.
	 */

	/*
	 * Is the CABQ FIFO free? If not, complain loudly and
	 * don't queue anything.  Maybe we'll flush the CABQ
	 * traffic, maybe we won't.  But that'll happen next
	 * beacon interval.
	 */
	if (cabq->axq_fifo_depth >= HAL_TXFIFO_DEPTH) {
		device_printf(sc->sc_dev,
		    "%s: Q%d: CAB FIFO queue=%d?\n",
		    __func__,
		    cabq->axq_qnum,
		    cabq->axq_fifo_depth);
		return;
	}

	/*
	 * Ok, so here's the gymnastics reqiured to make this
	 * all sensible.
	 */

	/*
	 * Tag the first/last buffer appropriately.
	 */
	bf->bf_flags |= ATH_BUF_FIFOPTR;
	bf_last->bf_flags |= ATH_BUF_FIFOEND;

#if 0
	i = 0;
	TAILQ_FOREACH(bfi, &cabq->axq_q, bf_list) {
		ath_printtxbuf(sc, bf, cabq->axq_qnum, i, 0);
		i++;
	}
#endif

	/*
	 * We now need to push this set of frames onto the tail
	 * of the FIFO queue.  We don't adjust the aggregate
	 * count, only the queue depth counter(s).
	 * We also need to blank the link pointer now.
	 */
	TAILQ_CONCAT(&cabq->fifo.axq_q, &cabq->axq_q, bf_list);
	cabq->axq_link = NULL;
	cabq->fifo.axq_depth += cabq->axq_depth;
	cabq->axq_depth = 0;

	/* Bump FIFO queue */
	cabq->axq_fifo_depth++;

	/* Push the first entry into the hardware */
	ath_hal_puttxbuf(sc->sc_ah, cabq->axq_qnum, bf->bf_daddr);
	cabq->axq_flags |= ATH_TXQ_PUTRUNNING;

	/* NB: gated by beacon so safe to start here */
	ath_hal_txstart(sc->sc_ah, cabq->axq_qnum);

}

static void
ath_beacon_cabq_start_legacy(struct ath_softc *sc)
{
	struct ath_buf *bf;
	struct ath_txq *cabq = sc->sc_cabq;

	ATH_TXQ_LOCK_ASSERT(cabq);
	if (TAILQ_EMPTY(&cabq->axq_q))
		return;
	bf = TAILQ_FIRST(&cabq->axq_q);

	/* Push the first entry into the hardware */
	ath_hal_puttxbuf(sc->sc_ah, cabq->axq_qnum, bf->bf_daddr);
	cabq->axq_flags |= ATH_TXQ_PUTRUNNING;

	/* NB: gated by beacon so safe to start here */
	ath_hal_txstart(sc->sc_ah, cabq->axq_qnum);
}

/*
 * Start CABQ transmission - this assumes that all frames are prepped
 * and ready in the CABQ.
 */
void
ath_beacon_cabq_start(struct ath_softc *sc)
{
	struct ath_txq *cabq = sc->sc_cabq;

	ATH_TXQ_LOCK_ASSERT(cabq);

	if (TAILQ_EMPTY(&cabq->axq_q))
		return;

	if (sc->sc_isedma)
		ath_beacon_cabq_start_edma(sc);
	else
		ath_beacon_cabq_start_legacy(sc);
}

struct ath_buf *
ath_beacon_generate(struct ath_softc *sc, struct ieee80211vap *vap)
{
	struct ath_vap *avp = ATH_VAP(vap);
	struct ath_txq *cabq = sc->sc_cabq;
	struct ath_buf *bf;
	struct mbuf *m;
	int nmcastq, error;

	KASSERT(vap->iv_state >= IEEE80211_S_RUN,
	    ("not running, state %d", vap->iv_state));
	KASSERT(avp->av_bcbuf != NULL, ("no beacon buffer"));

	/*
	 * Update dynamic beacon contents.  If this returns
	 * non-zero then we need to remap the memory because
	 * the beacon frame changed size (probably because
	 * of the TIM bitmap).
	 */
	bf = avp->av_bcbuf;
	m = bf->bf_m;
	/* XXX lock mcastq? */
	nmcastq = avp->av_mcastq.axq_depth;

	if (ieee80211_beacon_update(bf->bf_node, m, nmcastq)) {
		/* XXX too conservative? */
		bus_dmamap_unload(sc->sc_dmat, bf->bf_dmamap);
		error = bus_dmamap_load_mbuf_sg(sc->sc_dmat, bf->bf_dmamap, m,
					     bf->bf_segs, &bf->bf_nseg,
					     BUS_DMA_NOWAIT);
		if (error != 0) {
			if_printf(vap->iv_ifp,
			    "%s: bus_dmamap_load_mbuf_sg failed, error %u\n",
			    __func__, error);
			return NULL;
		}
	}
	if ((vap->iv_bcn_off.bo_tim[4] & 1) && cabq->axq_depth) {
		DPRINTF(sc, ATH_DEBUG_BEACON,
		    "%s: cabq did not drain, mcastq %u cabq %u\n",
		    __func__, nmcastq, cabq->axq_depth);
		sc->sc_stats.ast_cabq_busy++;
		if (sc->sc_nvaps > 1 && sc->sc_stagbeacons) {
			/*
			 * CABQ traffic from a previous vap is still pending.
			 * We must drain the q before this beacon frame goes
			 * out as otherwise this vap's stations will get cab
			 * frames from a different vap.
			 * XXX could be slow causing us to miss DBA
			 */
			/*
			 * XXX TODO: this doesn't stop CABQ DMA - it assumes
			 * that since we're about to transmit a beacon, we've
			 * already stopped transmitting on the CABQ.  But this
			 * doesn't at all mean that the CABQ DMA QCU will
			 * accept a new TXDP!  So what, should we do a DMA
			 * stop? What if it fails?
			 *
			 * More thought is required here.
			 */
			/*
			 * XXX can we even stop TX DMA here? Check what the
			 * reference driver does for cabq for beacons, given
			 * that stopping TX requires RX is paused.
			 */
			ath_tx_draintxq(sc, cabq);
		}
	}
	ath_beacon_setup(sc, bf);
	bus_dmamap_sync(sc->sc_dmat, bf->bf_dmamap, BUS_DMASYNC_PREWRITE);

	/*
	 * XXX TODO: tie into net80211 for quiet time IE update and program
	 * local AP timer if we require it.  The process of updating the
	 * beacon will also update the IE with the relevant counters.
	 */

	/*
	 * Enable the CAB queue before the beacon queue to
	 * insure cab frames are triggered by this beacon.
	 */
	if (vap->iv_bcn_off.bo_tim[4] & 1) {

		/* NB: only at DTIM */
		ATH_TXQ_LOCK(&avp->av_mcastq);
		if (nmcastq) {
			struct ath_buf *bfm, *bfc_last;

			/*
			 * Move frames from the s/w mcast q to the h/w cab q.
			 *
			 * XXX TODO: if we chain together multiple VAPs
			 * worth of CABQ traffic, should we keep the
			 * MORE data bit set on the last frame of each
			 * intermediary VAP (ie, only clear the MORE
			 * bit of the last frame on the last vap?)
			 */
			bfm = TAILQ_FIRST(&avp->av_mcastq.axq_q);
			ATH_TXQ_LOCK(cabq);

			/*
			 * If there's already a frame on the CABQ, we
			 * need to link to the end of the last frame.
			 * We can't use axq_link here because
			 * EDMA descriptors require some recalculation
			 * (checksum) to occur.
			 */
			bfc_last = ATH_TXQ_LAST(cabq, axq_q_s);
			if (bfc_last != NULL) {
				ath_hal_settxdesclink(sc->sc_ah,
				    bfc_last->bf_lastds,
				    bfm->bf_daddr);
			}
			ath_txqmove(cabq, &avp->av_mcastq);
			ATH_TXQ_UNLOCK(cabq);
			/*
			 * XXX not entirely accurate, in case a mcast
			 * queue frame arrived before we grabbed the TX
			 * lock.
			 */
			sc->sc_stats.ast_cabq_xmit += nmcastq;
		}
		ATH_TXQ_UNLOCK(&avp->av_mcastq);
	}
	return bf;
}

void
ath_beacon_start_adhoc(struct ath_softc *sc, struct ieee80211vap *vap)
{
	struct ath_vap *avp = ATH_VAP(vap);
	struct ath_hal *ah = sc->sc_ah;
	struct ath_buf *bf;
	struct mbuf *m;
	int error;

	KASSERT(avp->av_bcbuf != NULL, ("no beacon buffer"));

	/*
	 * Update dynamic beacon contents.  If this returns
	 * non-zero then we need to remap the memory because
	 * the beacon frame changed size (probably because
	 * of the TIM bitmap).
	 */
	bf = avp->av_bcbuf;
	m = bf->bf_m;
	if (ieee80211_beacon_update(bf->bf_node, m, 0)) {
		/* XXX too conservative? */
		bus_dmamap_unload(sc->sc_dmat, bf->bf_dmamap);
		error = bus_dmamap_load_mbuf_sg(sc->sc_dmat, bf->bf_dmamap, m,
					     bf->bf_segs, &bf->bf_nseg,
					     BUS_DMA_NOWAIT);
		if (error != 0) {
			if_printf(vap->iv_ifp,
			    "%s: bus_dmamap_load_mbuf_sg failed, error %u\n",
			    __func__, error);
			return;
		}
	}
	ath_beacon_setup(sc, bf);
	bus_dmamap_sync(sc->sc_dmat, bf->bf_dmamap, BUS_DMASYNC_PREWRITE);

	/* NB: caller is known to have already stopped tx dma */
	ath_hal_puttxbuf(ah, sc->sc_bhalq, bf->bf_daddr);
	ath_hal_txstart(ah, sc->sc_bhalq);
}

/*
 * Reclaim beacon resources and return buffer to the pool.
 */
void
ath_beacon_return(struct ath_softc *sc, struct ath_buf *bf)
{

	DPRINTF(sc, ATH_DEBUG_NODE, "%s: free bf=%p, bf_m=%p, bf_node=%p\n",
	    __func__, bf, bf->bf_m, bf->bf_node);
	if (bf->bf_m != NULL) {
		bus_dmamap_unload(sc->sc_dmat, bf->bf_dmamap);
		m_freem(bf->bf_m);
		bf->bf_m = NULL;
	}
	if (bf->bf_node != NULL) {
		ieee80211_free_node(bf->bf_node);
		bf->bf_node = NULL;
	}
	TAILQ_INSERT_TAIL(&sc->sc_bbuf, bf, bf_list);
}

/*
 * Reclaim beacon resources.
 */
void
ath_beacon_free(struct ath_softc *sc)
{
	struct ath_buf *bf;

	TAILQ_FOREACH(bf, &sc->sc_bbuf, bf_list) {
		DPRINTF(sc, ATH_DEBUG_NODE,
		    "%s: free bf=%p, bf_m=%p, bf_node=%p\n",
		        __func__, bf, bf->bf_m, bf->bf_node);
		if (bf->bf_m != NULL) {
			bus_dmamap_unload(sc->sc_dmat, bf->bf_dmamap);
			m_freem(bf->bf_m);
			bf->bf_m = NULL;
		}
		if (bf->bf_node != NULL) {
			ieee80211_free_node(bf->bf_node);
			bf->bf_node = NULL;
		}
	}
}

/*
 * Configure the beacon and sleep timers.
 *
 * When operating as an AP this resets the TSF and sets
 * up the hardware to notify us when we need to issue beacons.
 *
 * When operating in station mode this sets up the beacon
 * timers according to the timestamp of the last received
 * beacon and the current TSF, configures PCF and DTIM
 * handling, programs the sleep registers so the hardware
 * will wakeup in time to receive beacons, and configures
 * the beacon miss handling so we'll receive a BMISS
 * interrupt when we stop seeing beacons from the AP
 * we've associated with.
 */
void
ath_beacon_config(struct ath_softc *sc, struct ieee80211vap *vap)
{
#define	TSF_TO_TU(_h,_l) \
	((((u_int32_t)(_h)) << 22) | (((u_int32_t)(_l)) >> 10))
#define	FUDGE	2
	struct ath_hal *ah = sc->sc_ah;
	struct ath_vap *avp;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni;
	u_int32_t nexttbtt, intval, tsftu;
	u_int32_t nexttbtt_u8, intval_u8;
	u_int64_t tsf, tsf_beacon;

	if (vap == NULL)
		vap = TAILQ_FIRST(&ic->ic_vaps);	/* XXX */
	/*
	 * Just ensure that we aren't being called when the last
	 * VAP is destroyed.
	 */
	if (vap == NULL) {
		device_printf(sc->sc_dev, "%s: called with no VAPs\n",
		    __func__);
		return;
	}

	/* Now that we have a vap, we can do this bit */
	avp = ATH_VAP(vap);

	ni = ieee80211_ref_node(vap->iv_bss);

	ATH_LOCK(sc);
	ath_power_set_power_state(sc, HAL_PM_AWAKE);
	ATH_UNLOCK(sc);

	/* Always clear the quiet IE timers; let the next update program them */
	ath_hal_set_quiet(ah, 0, 0, 0, HAL_QUIET_DISABLE);
	memset(&avp->quiet_ie, 0, sizeof(avp->quiet_ie));

	/* extract tstamp from last beacon and convert to TU */
	nexttbtt = TSF_TO_TU(le32dec(ni->ni_tstamp.data + 4),
			     le32dec(ni->ni_tstamp.data));

	tsf_beacon = ((uint64_t) le32dec(ni->ni_tstamp.data + 4)) << 32;
	tsf_beacon |= le32dec(ni->ni_tstamp.data);

	if (ic->ic_opmode == IEEE80211_M_HOSTAP ||
	    ic->ic_opmode == IEEE80211_M_MBSS) {
		/*
		 * For multi-bss ap/mesh support beacons are either staggered
		 * evenly over N slots or burst together.  For the former
		 * arrange for the SWBA to be delivered for each slot.
		 * Slots that are not occupied will generate nothing.
		 */
		/* NB: the beacon interval is kept internally in TU's */
		intval = ni->ni_intval & HAL_BEACON_PERIOD;
		if (sc->sc_stagbeacons)
			intval /= ATH_BCBUF;
	} else {
		/* NB: the beacon interval is kept internally in TU's */
		intval = ni->ni_intval & HAL_BEACON_PERIOD;
	}

	/*
	 * Note: rounding up to the next intval can cause problems with
	 * bad APs when we're in powersave mode.
	 *
	 * In STA mode with powersave enabled, beacons are only received
	 * whenever the beacon timer fires to wake up the hardware.
	 * Now, if this is rounded up to the next intval, it assumes
	 * that the AP has started transmitting beacons at TSF values that
	 * are multiples of intval, versus say being 25 TU off.
	 *
	 * The specification (802.11-2012 10.1.3.2 - Beacon Generation in
	 * Infrastructure Networks) requires APs be beaconing at a
	 * mutiple of intval.  So, if bintval=100, then we shouldn't
	 * get beacons at intervals other than around multiples of 100.
	 */
	if (nexttbtt == 0)		/* e.g. for ap mode */
		nexttbtt = intval;
	else
		nexttbtt = roundup(nexttbtt, intval);

	DPRINTF(sc, ATH_DEBUG_BEACON, "%s: nexttbtt %u intval %u (%u)\n",
		__func__, nexttbtt, intval, ni->ni_intval);
	if (ic->ic_opmode == IEEE80211_M_STA && !sc->sc_swbmiss) {
		HAL_BEACON_STATE bs;
		int dtimperiod, dtimcount;
		int cfpperiod, cfpcount;

		/*
		 * Setup dtim and cfp parameters according to
		 * last beacon we received (which may be none).
		 */
		dtimperiod = ni->ni_dtim_period;
		if (dtimperiod <= 0)		/* NB: 0 if not known */
			dtimperiod = 1;
		dtimcount = ni->ni_dtim_count;
		if (dtimcount >= dtimperiod)	/* NB: sanity check */
			dtimcount = 0;		/* XXX? */
		cfpperiod = 1;			/* NB: no PCF support yet */
		cfpcount = 0;
		/*
		 * Pull nexttbtt forward to reflect the current
		 * TSF and calculate dtim+cfp state for the result.
		 */
		tsf = ath_hal_gettsf64(ah);
		tsftu = TSF_TO_TU(tsf>>32, tsf) + FUDGE;

		DPRINTF(sc, ATH_DEBUG_BEACON,
		    "%s: beacon tsf=%llu, hw tsf=%llu, nexttbtt=%u, tsftu=%u\n",
		    __func__,
		    (unsigned long long) tsf_beacon,
		    (unsigned long long) tsf,
		    nexttbtt,
		    tsftu);
		DPRINTF(sc, ATH_DEBUG_BEACON,
		    "%s: beacon tsf=%llu, hw tsf=%llu, tsf delta=%lld\n",
		    __func__,
		    (unsigned long long) tsf_beacon,
		    (unsigned long long) tsf,
		    (long long) tsf -
		    (long long) tsf_beacon);

		DPRINTF(sc, ATH_DEBUG_BEACON,
		    "%s: nexttbtt=%llu, beacon tsf delta=%lld\n",
		    __func__,
		    (unsigned long long) nexttbtt,
		    (long long) ((long long) nexttbtt * 1024LL) - (long long) tsf_beacon);

		/* XXX cfpcount? */

		if (nexttbtt > tsftu) {
			uint32_t countdiff, oldtbtt, remainder;

			oldtbtt = nexttbtt;
			remainder = (nexttbtt - tsftu) % intval;
			nexttbtt = tsftu + remainder;

			countdiff = (oldtbtt - nexttbtt) / intval % dtimperiod;
			if (dtimcount > countdiff) {
				dtimcount -= countdiff;
			} else {
				dtimcount += dtimperiod - countdiff;
			}
		} else { //nexttbtt <= tsftu
			uint32_t countdiff, oldtbtt, remainder;

			oldtbtt = nexttbtt;
			remainder = (tsftu - nexttbtt) % intval;
			nexttbtt = tsftu - remainder + intval;
			countdiff = (nexttbtt - oldtbtt) / intval % dtimperiod;
			if (dtimcount > countdiff) {
				dtimcount -= countdiff;
			} else {
				dtimcount += dtimperiod - countdiff;
			}
		}

		DPRINTF(sc, ATH_DEBUG_BEACON,
		    "%s: adj nexttbtt=%llu, rx tsf delta=%lld\n",
		    __func__,
		    (unsigned long long) nexttbtt,
		    (long long) ((long long)nexttbtt * 1024LL) - (long long)tsf);

		memset(&bs, 0, sizeof(bs));
		bs.bs_intval = intval;
		bs.bs_nexttbtt = nexttbtt;
		bs.bs_dtimperiod = dtimperiod*intval;
		bs.bs_nextdtim = bs.bs_nexttbtt + dtimcount*intval;
		bs.bs_cfpperiod = cfpperiod*bs.bs_dtimperiod;
		bs.bs_cfpnext = bs.bs_nextdtim + cfpcount*bs.bs_dtimperiod;
		bs.bs_cfpmaxduration = 0;
#if 0
		/*
		 * The 802.11 layer records the offset to the DTIM
		 * bitmap while receiving beacons; use it here to
		 * enable h/w detection of our AID being marked in
		 * the bitmap vector (to indicate frames for us are
		 * pending at the AP).
		 * XXX do DTIM handling in s/w to WAR old h/w bugs
		 * XXX enable based on h/w rev for newer chips
		 */
		bs.bs_timoffset = ni->ni_timoff;
#endif
		/*
		 * Calculate the number of consecutive beacons to miss
		 * before taking a BMISS interrupt.
		 * Note that we clamp the result to at most 10 beacons.
		 */
		bs.bs_bmissthreshold = vap->iv_bmissthreshold;
		if (bs.bs_bmissthreshold > 10)
			bs.bs_bmissthreshold = 10;
		else if (bs.bs_bmissthreshold <= 0)
			bs.bs_bmissthreshold = 1;

		/*
		 * Calculate sleep duration.  The configuration is
		 * given in ms.  We insure a multiple of the beacon
		 * period is used.  Also, if the sleep duration is
		 * greater than the DTIM period then it makes senses
		 * to make it a multiple of that.
		 *
		 * XXX fixed at 100ms
		 */
		bs.bs_sleepduration =
			roundup(IEEE80211_MS_TO_TU(100), bs.bs_intval);
		if (bs.bs_sleepduration > bs.bs_dtimperiod)
			bs.bs_sleepduration = roundup(bs.bs_sleepduration, bs.bs_dtimperiod);

		DPRINTF(sc, ATH_DEBUG_BEACON,
			"%s: tsf %ju tsf:tu %u intval %u nexttbtt %u dtim %u "
			"nextdtim %u bmiss %u sleep %u cfp:period %u "
			"maxdur %u next %u timoffset %u\n"
			, __func__
			, tsf
			, tsftu
			, bs.bs_intval
			, bs.bs_nexttbtt
			, bs.bs_dtimperiod
			, bs.bs_nextdtim
			, bs.bs_bmissthreshold
			, bs.bs_sleepduration
			, bs.bs_cfpperiod
			, bs.bs_cfpmaxduration
			, bs.bs_cfpnext
			, bs.bs_timoffset
		);
		ath_hal_intrset(ah, 0);
		ath_hal_beacontimers(ah, &bs);
		sc->sc_imask |= HAL_INT_BMISS;
		ath_hal_intrset(ah, sc->sc_imask);
	} else {
		ath_hal_intrset(ah, 0);
		if (nexttbtt == intval)
			intval |= HAL_BEACON_RESET_TSF;
		if (ic->ic_opmode == IEEE80211_M_IBSS) {
			/*
			 * In IBSS mode enable the beacon timers but only
			 * enable SWBA interrupts if we need to manually
			 * prepare beacon frames.  Otherwise we use a
			 * self-linked tx descriptor and let the hardware
			 * deal with things.
			 */
			intval |= HAL_BEACON_ENA;
			if (!sc->sc_hasveol)
				sc->sc_imask |= HAL_INT_SWBA;
			if ((intval & HAL_BEACON_RESET_TSF) == 0) {
				/*
				 * Pull nexttbtt forward to reflect
				 * the current TSF.
				 */
				tsf = ath_hal_gettsf64(ah);
				tsftu = TSF_TO_TU(tsf>>32, tsf) + FUDGE;
				do {
					nexttbtt += intval;
				} while (nexttbtt < tsftu);
			}
			ath_beaconq_config(sc);
		} else if (ic->ic_opmode == IEEE80211_M_HOSTAP ||
		    ic->ic_opmode == IEEE80211_M_MBSS) {
			/*
			 * In AP/mesh mode we enable the beacon timers
			 * and SWBA interrupts to prepare beacon frames.
			 */
			intval |= HAL_BEACON_ENA;
			sc->sc_imask |= HAL_INT_SWBA;	/* beacon prepare */
			ath_beaconq_config(sc);
		}

		/*
		 * Now dirty things because for now, the EDMA HAL has
		 * nexttbtt and intval is TU/8.
		 */
		if (sc->sc_isedma) {
			nexttbtt_u8 = (nexttbtt << 3);
			intval_u8 = (intval << 3);
			if (intval & HAL_BEACON_ENA)
				intval_u8 |= HAL_BEACON_ENA;
			if (intval & HAL_BEACON_RESET_TSF)
				intval_u8 |= HAL_BEACON_RESET_TSF;
			ath_hal_beaconinit(ah, nexttbtt_u8, intval_u8);
		} else
			ath_hal_beaconinit(ah, nexttbtt, intval);
		sc->sc_bmisscount = 0;
		ath_hal_intrset(ah, sc->sc_imask);
		/*
		 * When using a self-linked beacon descriptor in
		 * ibss mode load it once here.
		 */
		if (ic->ic_opmode == IEEE80211_M_IBSS && sc->sc_hasveol)
			ath_beacon_start_adhoc(sc, vap);
	}
	ieee80211_free_node(ni);

	ATH_LOCK(sc);
	ath_power_restore_power_state(sc);
	ATH_UNLOCK(sc);
#undef FUDGE
#undef TSF_TO_TU
}
