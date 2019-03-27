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

#ifdef ATH_TX99_DIAG
#include <dev/ath/ath_tx99/ath_tx99.h>
#endif

#ifdef	ATH_DEBUG_ALQ
#include <dev/ath/if_ath_alq.h>
#endif

#ifdef IEEE80211_SUPPORT_TDMA
#include <dev/ath/if_ath_tdma.h>

static void	ath_tdma_settimers(struct ath_softc *sc, u_int32_t nexttbtt,
		    u_int32_t bintval);
static void	ath_tdma_bintvalsetup(struct ath_softc *sc,
		    const struct ieee80211_tdma_state *tdma);
#endif /* IEEE80211_SUPPORT_TDMA */

#ifdef IEEE80211_SUPPORT_TDMA
static void
ath_tdma_settimers(struct ath_softc *sc, u_int32_t nexttbtt, u_int32_t bintval)
{
	struct ath_hal *ah = sc->sc_ah;
	HAL_BEACON_TIMERS bt;

	bt.bt_intval = bintval | HAL_BEACON_ENA;
	bt.bt_nexttbtt = nexttbtt;
	bt.bt_nextdba = (nexttbtt<<3) - sc->sc_tdmadbaprep;
	bt.bt_nextswba = (nexttbtt<<3) - sc->sc_tdmaswbaprep;
	bt.bt_nextatim = nexttbtt+1;
	/* Enables TBTT, DBA, SWBA timers by default */
	bt.bt_flags = 0;
#if 0
	DPRINTF(sc, ATH_DEBUG_TDMA_TIMER,
	    "%s: intval=%d (0x%08x) nexttbtt=%u (0x%08x), nextdba=%u (0x%08x), nextswba=%u (0x%08x),nextatim=%u (0x%08x)\n",
	    __func__,
	    bt.bt_intval,
	    bt.bt_intval,
	    bt.bt_nexttbtt,
	    bt.bt_nexttbtt,
	    bt.bt_nextdba,
	    bt.bt_nextdba,
	    bt.bt_nextswba,
	    bt.bt_nextswba,
	    bt.bt_nextatim,
	    bt.bt_nextatim);
#endif

#ifdef	ATH_DEBUG_ALQ
	if (if_ath_alq_checkdebug(&sc->sc_alq, ATH_ALQ_TDMA_TIMER_SET)) {
		struct if_ath_alq_tdma_timer_set t;
		t.bt_intval = htobe32(bt.bt_intval);
		t.bt_nexttbtt = htobe32(bt.bt_nexttbtt);
		t.bt_nextdba = htobe32(bt.bt_nextdba);
		t.bt_nextswba = htobe32(bt.bt_nextswba);
		t.bt_nextatim = htobe32(bt.bt_nextatim);
		t.bt_flags = htobe32(bt.bt_flags);
		t.sc_tdmadbaprep = htobe32(sc->sc_tdmadbaprep);
		t.sc_tdmaswbaprep = htobe32(sc->sc_tdmaswbaprep);
		if_ath_alq_post(&sc->sc_alq, ATH_ALQ_TDMA_TIMER_SET,
		    sizeof(t), (char *) &t);
	}
#endif

	DPRINTF(sc, ATH_DEBUG_TDMA_TIMER,
	    "%s: nexttbtt=%u (0x%08x), nexttbtt tsf=%lld (0x%08llx)\n",
	    __func__,
	    bt.bt_nexttbtt,
	    bt.bt_nexttbtt,
	    (long long) ( ((u_int64_t) (bt.bt_nexttbtt)) << 10),
	    (long long) ( ((u_int64_t) (bt.bt_nexttbtt)) << 10));
	ath_hal_beaconsettimers(ah, &bt);
}

/*
 * Calculate the beacon interval.  This is periodic in the
 * superframe for the bss.  We assume each station is configured
 * identically wrt transmit rate so the guard time we calculate
 * above will be the same on all stations.  Note we need to
 * factor in the xmit time because the hardware will schedule
 * a frame for transmit if the start of the frame is within
 * the burst time.  When we get hardware that properly kills
 * frames in the PCU we can reduce/eliminate the guard time.
 *
 * Roundup to 1024 is so we have 1 TU buffer in the guard time
 * to deal with the granularity of the nexttbtt timer.  11n MAC's
 * with 1us timer granularity should allow us to reduce/eliminate
 * this.
 */
static void
ath_tdma_bintvalsetup(struct ath_softc *sc,
	const struct ieee80211_tdma_state *tdma)
{
	/* copy from vap state (XXX check all vaps have same value?) */
	sc->sc_tdmaslotlen = tdma->tdma_slotlen;

	sc->sc_tdmabintval = roundup((sc->sc_tdmaslotlen+sc->sc_tdmaguard) *
		tdma->tdma_slotcnt, 1024);
	sc->sc_tdmabintval >>= 10;		/* TSF -> TU */
	if (sc->sc_tdmabintval & 1)
		sc->sc_tdmabintval++;

	if (tdma->tdma_slot == 0) {
		/*
		 * Only slot 0 beacons; other slots respond.
		 */
		sc->sc_imask |= HAL_INT_SWBA;
		sc->sc_tdmaswba = 0;		/* beacon immediately */
	} else {
		/* XXX all vaps must be slot 0 or slot !0 */
		sc->sc_imask &= ~HAL_INT_SWBA;
	}
}

/*
 * Max 802.11 overhead.  This assumes no 4-address frames and
 * the encapsulation done by ieee80211_encap (llc).  We also
 * include potential crypto overhead.
 */
#define	IEEE80211_MAXOVERHEAD \
	(sizeof(struct ieee80211_qosframe) \
	 + sizeof(struct llc) \
	 + IEEE80211_ADDR_LEN \
	 + IEEE80211_WEP_IVLEN \
	 + IEEE80211_WEP_KIDLEN \
	 + IEEE80211_WEP_CRCLEN \
	 + IEEE80211_WEP_MICLEN \
	 + IEEE80211_CRC_LEN)

/*
 * Setup initially for tdma operation.  Start the beacon
 * timers and enable SWBA if we are slot 0.  Otherwise
 * we wait for slot 0 to arrive so we can sync up before
 * starting to transmit.
 */
void
ath_tdma_config(struct ath_softc *sc, struct ieee80211vap *vap)
{
	struct ath_hal *ah = sc->sc_ah;
	struct ieee80211com *ic = &sc->sc_ic;
	const struct ieee80211_txparam *tp;
	const struct ieee80211_tdma_state *tdma = NULL;
	int rix;

	if (vap == NULL) {
		vap = TAILQ_FIRST(&ic->ic_vaps);   /* XXX */
		if (vap == NULL) {
			device_printf(sc->sc_dev, "%s: no vaps?\n", __func__);
			return;
		}
	}
	/* XXX should take a locked ref to iv_bss */
	tp = vap->iv_bss->ni_txparms;
	/*
	 * Calculate the guard time for each slot.  This is the
	 * time to send a maximal-size frame according to the
	 * fixed/lowest transmit rate.  Note that the interface
	 * mtu does not include the 802.11 overhead so we must
	 * tack that on (ath_hal_computetxtime includes the
	 * preamble and plcp in its calculation).
	 */
	tdma = vap->iv_tdma;
	if (tp->ucastrate != IEEE80211_FIXED_RATE_NONE)
		rix = ath_tx_findrix(sc, tp->ucastrate);
	else
		rix = ath_tx_findrix(sc, tp->mcastrate);

	/*
	 * If the chip supports enforcing TxOP on transmission,
	 * we can just delete the guard window.  It isn't at all required.
	 */
	if (sc->sc_hasenforcetxop) {
		sc->sc_tdmaguard = 0;
	} else {
		/* XXX short preamble assumed */
		/* XXX non-11n rate assumed */
		sc->sc_tdmaguard = ath_hal_computetxtime(ah, sc->sc_currates,
		    vap->iv_ifp->if_mtu + IEEE80211_MAXOVERHEAD, rix, AH_TRUE,
		    AH_TRUE);
	}

	ath_hal_intrset(ah, 0);

	ath_beaconq_config(sc);			/* setup h/w beacon q */
	if (sc->sc_setcca)
		ath_hal_setcca(ah, AH_FALSE);	/* disable CCA */
	ath_tdma_bintvalsetup(sc, tdma);	/* calculate beacon interval */
	ath_tdma_settimers(sc, sc->sc_tdmabintval,
		sc->sc_tdmabintval | HAL_BEACON_RESET_TSF);
	sc->sc_syncbeacon = 0;

	sc->sc_avgtsfdeltap = TDMA_DUMMY_MARKER;
	sc->sc_avgtsfdeltam = TDMA_DUMMY_MARKER;

	ath_hal_intrset(ah, sc->sc_imask);

	DPRINTF(sc, ATH_DEBUG_TDMA, "%s: slot %u len %uus cnt %u "
	    "bsched %u guard %uus bintval %u TU dba prep %u\n", __func__,
	    tdma->tdma_slot, tdma->tdma_slotlen, tdma->tdma_slotcnt,
	    tdma->tdma_bintval, sc->sc_tdmaguard, sc->sc_tdmabintval,
	    sc->sc_tdmadbaprep);

#ifdef	ATH_DEBUG_ALQ
	if (if_ath_alq_checkdebug(&sc->sc_alq, ATH_ALQ_TDMA_TIMER_CONFIG)) {
		struct if_ath_alq_tdma_timer_config t;

		t.tdma_slot = htobe32(tdma->tdma_slot);
		t.tdma_slotlen = htobe32(tdma->tdma_slotlen);
		t.tdma_slotcnt = htobe32(tdma->tdma_slotcnt);
		t.tdma_bintval = htobe32(tdma->tdma_bintval);
		t.tdma_guard = htobe32(sc->sc_tdmaguard);
		t.tdma_scbintval = htobe32(sc->sc_tdmabintval);
		t.tdma_dbaprep = htobe32(sc->sc_tdmadbaprep);

		if_ath_alq_post(&sc->sc_alq, ATH_ALQ_TDMA_TIMER_CONFIG,
		    sizeof(t), (char *) &t);
	}
#endif	/* ATH_DEBUG_ALQ */
}

/*
 * Update tdma operation.  Called from the 802.11 layer
 * when a beacon is received from the TDMA station operating
 * in the slot immediately preceding us in the bss.  Use
 * the rx timestamp for the beacon frame to update our
 * beacon timers so we follow their schedule.  Note that
 * by using the rx timestamp we implicitly include the
 * propagation delay in our schedule.
 *
 * XXX TODO: since the changes for the AR5416 and later chips
 * involved changing the TSF/TU calculations, we need to make
 * sure that various calculations wrap consistently.
 *
 * A lot of the problems stemmed from the calculations wrapping
 * at 65,535 TU.  Since a lot of the math is still being done in
 * TU, please audit it to ensure that when the TU values programmed
 * into the timers wrap at (2^31)-1 TSF, all the various terms
 * wrap consistently.
 */
void
ath_tdma_update(struct ieee80211_node *ni,
	const struct ieee80211_tdma_param *tdma, int changed)
{
#define	TSF_TO_TU(_h,_l) \
	((((u_int32_t)(_h)) << 22) | (((u_int32_t)(_l)) >> 10))
#define	TU_TO_TSF(_tu)	(((u_int64_t)(_tu)) << 10)
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211com *ic = ni->ni_ic;
	struct ath_softc *sc = ic->ic_softc;
	struct ath_hal *ah = sc->sc_ah;
	const HAL_RATE_TABLE *rt = sc->sc_currates;
	u_int64_t tsf, rstamp, nextslot, nexttbtt, nexttbtt_full;
	u_int32_t txtime, nextslottu;
	int32_t tudelta, tsfdelta;
	const struct ath_rx_status *rs;
	int rix;

	sc->sc_stats.ast_tdma_update++;

	/*
	 * Check for and adopt configuration changes.
	 */
	if (changed != 0) {
		const struct ieee80211_tdma_state *ts = vap->iv_tdma;

		ath_tdma_bintvalsetup(sc, ts);
		if (changed & TDMA_UPDATE_SLOTLEN)
			ath_wme_update(ic);

		DPRINTF(sc, ATH_DEBUG_TDMA,
		    "%s: adopt slot %u slotcnt %u slotlen %u us "
		    "bintval %u TU\n", __func__,
		    ts->tdma_slot, ts->tdma_slotcnt, ts->tdma_slotlen,
		    sc->sc_tdmabintval);

		/* XXX right? */
		ath_hal_intrset(ah, sc->sc_imask);
		/* NB: beacon timers programmed below */
	}

	/* extend rx timestamp to 64 bits */
	rs = sc->sc_lastrs;
	tsf = ath_hal_gettsf64(ah);
	rstamp = ath_extend_tsf(sc, rs->rs_tstamp, tsf);
	/*
	 * The rx timestamp is set by the hardware on completing
	 * reception (at the point where the rx descriptor is DMA'd
	 * to the host).  To find the start of our next slot we
	 * must adjust this time by the time required to send
	 * the packet just received.
	 */
	rix = rt->rateCodeToIndex[rs->rs_rate];

	/*
	 * To calculate the packet duration for legacy rates, we
	 * only need the rix and preamble.
	 *
	 * For 11n non-aggregate frames, we also need the channel
	 * width and short/long guard interval.
	 *
	 * For 11n aggregate frames, the required hacks are a little
	 * more subtle.  You need to figure out the frame duration
	 * for each frame, including the delimiters.  However, when
	 * a frame isn't received successfully, we won't hear it
	 * (unless you enable reception of CRC errored frames), so
	 * your duration calculation is going to be off.
	 *
	 * However, we can assume that the beacon frames won't be
	 * transmitted as aggregate frames, so we should be okay.
	 * Just add a check to ensure that we aren't handed something
	 * bad.
	 *
	 * For ath_hal_pkt_txtime() - for 11n rates, shortPreamble is
	 * actually short guard interval. For legacy rates,
	 * it's short preamble.
	 */
	txtime = ath_hal_pkt_txtime(ah, rt, rs->rs_datalen,
	    rix,
	    !! (rs->rs_flags & HAL_RX_2040),
	    (rix & 0x80) ?
	      (! (rs->rs_flags & HAL_RX_GI)) : rt->info[rix].shortPreamble,
	    AH_TRUE);
	/* NB: << 9 is to cvt to TU and /2 */
	nextslot = (rstamp - txtime) + (sc->sc_tdmabintval << 9);

	/*
	 * For 802.11n chips: nextslottu needs to be the full TSF space,
	 * not just 0..65535 TU.
	 */
	nextslottu = TSF_TO_TU(nextslot>>32, nextslot);
	/*
	 * Retrieve the hardware NextTBTT in usecs
	 * and calculate the difference between what the
	 * other station thinks and what we have programmed.  This
	 * lets us figure how to adjust our timers to match.  The
	 * adjustments are done by pulling the TSF forward and possibly
	 * rewriting the beacon timers.
	 */
	/*
	 * The logic here assumes the nexttbtt counter is in TSF
	 * but the prr-11n NICs are in TU.  The HAL shifts them
	 * to TSF but there's two important differences:
	 *
	 * + The TU->TSF values have 0's for the low 9 bits, and
	 * + The counter wraps at TU_TO_TSF(HAL_BEACON_PERIOD + 1) for
	 *   the pre-11n NICs, but not for the 11n NICs.
	 *
	 * So for now, just make sure the nexttbtt value we get
	 * matches the second issue or once nexttbtt exceeds this
	 * value, tsfdelta ends up becoming very negative and all
	 * of the adjustments get very messed up.
	 */

	/*
	 * We need to track the full nexttbtt rather than having it
	 * truncated at HAL_BEACON_PERIOD, as programming the
	 * nexttbtt (and related) registers for the 11n chips is
	 * actually going to take the full 32 bit space, rather than
	 * just 0..65535 TU.
	 */
	nexttbtt_full = ath_hal_getnexttbtt(ah);
	nexttbtt = nexttbtt_full % (TU_TO_TSF(HAL_BEACON_PERIOD + 1));
	tsfdelta = (int32_t)((nextslot % TU_TO_TSF(HAL_BEACON_PERIOD + 1)) - nexttbtt);

	DPRINTF(sc, ATH_DEBUG_TDMA_TIMER,
	    "rs->rstamp %llu rstamp %llu tsf %llu txtime %d, nextslot %llu, "
	    "nextslottu %d, nextslottume %d\n",
	    (unsigned long long) rs->rs_tstamp,
	    (unsigned long long) rstamp,
	    (unsigned long long) tsf, txtime,
	    (unsigned long long) nextslot,
	    nextslottu, TSF_TO_TU(nextslot >> 32, nextslot));
	DPRINTF(sc, ATH_DEBUG_TDMA,
	    "  beacon tstamp: %llu (0x%016llx)\n",
	    (unsigned long long) le64toh(ni->ni_tstamp.tsf),
	    (unsigned long long) le64toh(ni->ni_tstamp.tsf));

	DPRINTF(sc, ATH_DEBUG_TDMA_TIMER,
	    "nexttbtt %llu (0x%08llx) tsfdelta %d avg +%d/-%d\n",
	    (unsigned long long) nexttbtt,
	    (long long) nexttbtt,
	    tsfdelta,
	    TDMA_AVG(sc->sc_avgtsfdeltap), TDMA_AVG(sc->sc_avgtsfdeltam));

	if (tsfdelta < 0) {
		TDMA_SAMPLE(sc->sc_avgtsfdeltap, 0);
		TDMA_SAMPLE(sc->sc_avgtsfdeltam, -tsfdelta);
		tsfdelta = -tsfdelta % 1024;
		nextslottu++;
	} else if (tsfdelta > 0) {
		TDMA_SAMPLE(sc->sc_avgtsfdeltap, tsfdelta);
		TDMA_SAMPLE(sc->sc_avgtsfdeltam, 0);
		tsfdelta = 1024 - (tsfdelta % 1024);
		nextslottu++;
	} else {
		TDMA_SAMPLE(sc->sc_avgtsfdeltap, 0);
		TDMA_SAMPLE(sc->sc_avgtsfdeltam, 0);
	}
	tudelta = nextslottu - TSF_TO_TU(nexttbtt_full >> 32, nexttbtt_full);

#ifdef	ATH_DEBUG_ALQ
	if (if_ath_alq_checkdebug(&sc->sc_alq, ATH_ALQ_TDMA_BEACON_STATE)) {
		struct if_ath_alq_tdma_beacon_state t;
		t.rx_tsf = htobe64(rstamp);
		t.beacon_tsf = htobe64(le64toh(ni->ni_tstamp.tsf));
		t.tsf64 = htobe64(tsf);
		t.nextslot_tsf = htobe64(nextslot);
		t.nextslot_tu = htobe32(nextslottu);
		t.txtime = htobe32(txtime);
		if_ath_alq_post(&sc->sc_alq, ATH_ALQ_TDMA_BEACON_STATE,
		    sizeof(t), (char *) &t);
	}

	if (if_ath_alq_checkdebug(&sc->sc_alq, ATH_ALQ_TDMA_SLOT_CALC)) {
		struct if_ath_alq_tdma_slot_calc t;

		t.nexttbtt = htobe64(nexttbtt_full);
		t.next_slot = htobe64(nextslot);
		t.tsfdelta = htobe32(tsfdelta);
		t.avg_plus = htobe32(TDMA_AVG(sc->sc_avgtsfdeltap));
		t.avg_minus = htobe32(TDMA_AVG(sc->sc_avgtsfdeltam));

		if_ath_alq_post(&sc->sc_alq, ATH_ALQ_TDMA_SLOT_CALC,
		    sizeof(t), (char *) &t);
	}
#endif

	/*
	 * Copy sender's timetstamp into tdma ie so they can
	 * calculate roundtrip time.  We submit a beacon frame
	 * below after any timer adjustment.  The frame goes out
	 * at the next TBTT so the sender can calculate the
	 * roundtrip by inspecting the tdma ie in our beacon frame.
	 *
	 * NB: This tstamp is subtlely preserved when
	 *     IEEE80211_BEACON_TDMA is marked (e.g. when the
	 *     slot position changes) because ieee80211_add_tdma
	 *     skips over the data.
	 */
	memcpy(vap->iv_bcn_off.bo_tdma +
		__offsetof(struct ieee80211_tdma_param, tdma_tstamp),
		&ni->ni_tstamp.data, 8);
#if 0
	DPRINTF(sc, ATH_DEBUG_TDMA_TIMER,
	    "tsf %llu nextslot %llu (%d, %d) nextslottu %u nexttbtt %llu (%d)\n",
	    (unsigned long long) tsf, (unsigned long long) nextslot,
	    (int)(nextslot - tsf), tsfdelta, nextslottu, nexttbtt, tudelta);
#endif
	/*
	 * Adjust the beacon timers only when pulling them forward
	 * or when going back by less than the beacon interval.
	 * Negative jumps larger than the beacon interval seem to
	 * cause the timers to stop and generally cause instability.
	 * This basically filters out jumps due to missed beacons.
	 */
	if (tudelta != 0 && (tudelta > 0 || -tudelta < sc->sc_tdmabintval)) {
		DPRINTF(sc, ATH_DEBUG_TDMA_TIMER,
		    "%s: calling ath_tdma_settimers; nextslottu=%d, bintval=%d\n",
		    __func__,
		    nextslottu,
		    sc->sc_tdmabintval);
		ath_tdma_settimers(sc, nextslottu, sc->sc_tdmabintval);
		sc->sc_stats.ast_tdma_timers++;
	}
	if (tsfdelta > 0) {
		uint64_t tsf;

		/* XXX should just teach ath_hal_adjusttsf() to do this */
		tsf = ath_hal_gettsf64(ah);
		ath_hal_settsf64(ah, tsf + tsfdelta);
		DPRINTF(sc, ATH_DEBUG_TDMA_TIMER,
		    "%s: calling ath_hal_adjusttsf: TSF=%llu, tsfdelta=%d\n",
		    __func__,
		    (unsigned long long) tsf,
		    tsfdelta);

#ifdef	ATH_DEBUG_ALQ
		if (if_ath_alq_checkdebug(&sc->sc_alq,
		    ATH_ALQ_TDMA_TSF_ADJUST)) {
			struct if_ath_alq_tdma_tsf_adjust t;

			t.tsfdelta = htobe32(tsfdelta);
			t.tsf64_old = htobe64(tsf);
			t.tsf64_new = htobe64(tsf + tsfdelta);
			if_ath_alq_post(&sc->sc_alq, ATH_ALQ_TDMA_TSF_ADJUST,
			    sizeof(t), (char *) &t);
		}
#endif	/* ATH_DEBUG_ALQ */
		sc->sc_stats.ast_tdma_tsf++;
	}
	ath_tdma_beacon_send(sc, vap);		/* prepare response */
#undef TU_TO_TSF
#undef TSF_TO_TU
}

/*
 * Transmit a beacon frame at SWBA.  Dynamic updates
 * to the frame contents are done as needed.
 */
void
ath_tdma_beacon_send(struct ath_softc *sc, struct ieee80211vap *vap)
{
	struct ath_hal *ah = sc->sc_ah;
	struct ath_buf *bf;
	int otherant;

	/*
	 * Check if the previous beacon has gone out.  If
	 * not don't try to post another, skip this period
	 * and wait for the next.  Missed beacons indicate
	 * a problem and should not occur.  If we miss too
	 * many consecutive beacons reset the device.
	 */
	if (ath_hal_numtxpending(ah, sc->sc_bhalq) != 0) {
		sc->sc_bmisscount++;
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
	}

	/*
	 * Check recent per-antenna transmit statistics and flip
	 * the default antenna if noticeably more frames went out
	 * on the non-default antenna.
	 * XXX assumes 2 anntenae
	 */
	if (!sc->sc_diversity) {
		otherant = sc->sc_defant & 1 ? 2 : 1;
		if (sc->sc_ant_tx[otherant] > sc->sc_ant_tx[sc->sc_defant] + 2)
			ath_setdefantenna(sc, otherant);
		sc->sc_ant_tx[1] = sc->sc_ant_tx[2] = 0;
	}

	bf = ath_beacon_generate(sc, vap);
	/* XXX We don't do cabq traffic, but just for completeness .. */
	ATH_TXQ_LOCK(sc->sc_cabq);
	ath_beacon_cabq_start(sc);
	ATH_TXQ_UNLOCK(sc->sc_cabq);

	if (bf != NULL) {
		/*
		 * Stop any current dma and put the new frame on the queue.
		 * This should never fail since we check above that no frames
		 * are still pending on the queue.
		 */
		if ((! sc->sc_isedma) &&
		    (! ath_hal_stoptxdma(ah, sc->sc_bhalq))) {
			DPRINTF(sc, ATH_DEBUG_ANY,
				"%s: beacon queue %u did not stop?\n",
				__func__, sc->sc_bhalq);
			/* NB: the HAL still stops DMA, so proceed */
		}
		ath_hal_puttxbuf(ah, sc->sc_bhalq, bf->bf_daddr);
		ath_hal_txstart(ah, sc->sc_bhalq);

		sc->sc_stats.ast_be_xmit++;		/* XXX per-vap? */

		/*
		 * Record local TSF for our last send for use
		 * in arbitrating slot collisions.
		 */
		/* XXX should take a locked ref to iv_bss */
		vap->iv_bss->ni_tstamp.tsf = ath_hal_gettsf64(ah);
	}
}
#endif /* IEEE80211_SUPPORT_TDMA */
