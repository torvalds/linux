/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007-2009 Sam Leffler, Errno Consulting
 * Copyright (c) 2007-2009 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#ifdef __FreeBSD__
__FBSDID("$FreeBSD$");
#endif

/*
 * IEEE 802.11 TDMA mode support.
 */
#include "opt_inet.h"
#include "opt_tdma.h"
#include "opt_wlan.h"

#ifdef	IEEE80211_SUPPORT_TDMA

#include <sys/param.h>
#include <sys/systm.h> 
#include <sys/mbuf.h>   
#include <sys/malloc.h>
#include <sys/kernel.h>

#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/endian.h>
#include <sys/errno.h>
#include <sys/proc.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/if_media.h>
#include <net/if_llc.h>
#include <net/ethernet.h>

#include <net/bpf.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_tdma.h>
#include <net80211/ieee80211_input.h>

#ifndef TDMA_SLOTLEN_DEFAULT
#define	TDMA_SLOTLEN_DEFAULT	10*1000		/* 10ms */
#endif
#ifndef TDMA_SLOTCNT_DEFAULT
#define	TDMA_SLOTCNT_DEFAULT	2		/* 2x (pt-to-pt) */
#endif
#ifndef TDMA_BINTVAL_DEFAULT
#define	TDMA_BINTVAL_DEFAULT	5		/* 5x ~= 100TU beacon intvl */
#endif
#ifndef TDMA_TXRATE_11B_DEFAULT
#define	TDMA_TXRATE_11B_DEFAULT	2*11
#endif
#ifndef TDMA_TXRATE_11G_DEFAULT
#define	TDMA_TXRATE_11G_DEFAULT	2*24
#endif
#ifndef TDMA_TXRATE_11A_DEFAULT
#define	TDMA_TXRATE_11A_DEFAULT	2*24
#endif
#ifndef TDMA_TXRATE_TURBO_DEFAULT
#define	TDMA_TXRATE_TURBO_DEFAULT	2*24
#endif
#ifndef TDMA_TXRATE_HALF_DEFAULT
#define	TDMA_TXRATE_HALF_DEFAULT	2*12
#endif
#ifndef TDMA_TXRATE_QUARTER_DEFAULT
#define	TDMA_TXRATE_QUARTER_DEFAULT	2*6
#endif
#ifndef TDMA_TXRATE_11NA_DEFAULT
#define	TDMA_TXRATE_11NA_DEFAULT	(4 | IEEE80211_RATE_MCS)
#endif
#ifndef TDMA_TXRATE_11NG_DEFAULT
#define	TDMA_TXRATE_11NG_DEFAULT	(4 | IEEE80211_RATE_MCS)
#endif

#define	TDMA_VERSION_VALID(_version) \
	(TDMA_VERSION_V2 <= (_version) && (_version) <= TDMA_VERSION)
#define	TDMA_SLOTCNT_VALID(_slotcnt) \
	(2 <= (_slotcnt) && (_slotcnt) <= TDMA_MAXSLOTS)
/* XXX magic constants */
#define	TDMA_SLOTLEN_VALID(_slotlen) \
	(2*100 <= (_slotlen) && (unsigned)(_slotlen) <= 0xfffff)
/* XXX probably should set a max */
#define	TDMA_BINTVAL_VALID(_bintval)	(1 <= (_bintval))

/*
 * This code is not prepared to handle more than 2 slots.
 */
CTASSERT(TDMA_MAXSLOTS == 2);

static void tdma_vdetach(struct ieee80211vap *vap);
static int tdma_newstate(struct ieee80211vap *, enum ieee80211_state, int);
static void tdma_beacon_miss(struct ieee80211vap *vap);
static void tdma_recv_mgmt(struct ieee80211_node *, struct mbuf *,
	int subtype, const struct ieee80211_rx_stats *rxs, int rssi, int nf);
static int tdma_update(struct ieee80211vap *vap,
	const struct ieee80211_tdma_param *tdma, struct ieee80211_node *ni,
	int pickslot);
static int tdma_process_params(struct ieee80211_node *ni,
	const u_int8_t *ie, int rssi, int nf, const struct ieee80211_frame *wh);

static void
settxparms(struct ieee80211vap *vap, enum ieee80211_phymode mode, int rate)
{
	if (isclr(vap->iv_ic->ic_modecaps, mode))
		return;

	vap->iv_txparms[mode].ucastrate = rate;
	vap->iv_txparms[mode].mcastrate = rate;
}

static void
setackpolicy(struct ieee80211com *ic, int noack)
{
	struct ieee80211_wme_state *wme = &ic->ic_wme;
	int ac;

	for (ac = 0; ac < WME_NUM_AC; ac++) {
		wme->wme_chanParams.cap_wmeParams[ac].wmep_noackPolicy = noack;
		wme->wme_wmeChanParams.cap_wmeParams[ac].wmep_noackPolicy = noack;
	}
}

void
ieee80211_tdma_vattach(struct ieee80211vap *vap)
{
	struct ieee80211_tdma_state *ts;

	KASSERT(vap->iv_caps & IEEE80211_C_TDMA,
	     ("not a tdma vap, caps 0x%x", vap->iv_caps));

	ts = (struct ieee80211_tdma_state *) IEEE80211_MALLOC(
	     sizeof(struct ieee80211_tdma_state), M_80211_VAP,
	     IEEE80211_M_NOWAIT | IEEE80211_M_ZERO);
	if (ts == NULL) {
		printf("%s: cannot allocate TDMA state block\n", __func__);
		/* NB: fall back to adhdemo mode */
		vap->iv_caps &= ~IEEE80211_C_TDMA;
		return;
	}
	/* NB: default configuration is passive so no beacons */
	ts->tdma_version = TDMA_VERSION;
	ts->tdma_slotlen = TDMA_SLOTLEN_DEFAULT;
	ts->tdma_slotcnt = TDMA_SLOTCNT_DEFAULT;
	ts->tdma_bintval = TDMA_BINTVAL_DEFAULT;
	ts->tdma_slot = 1;			/* passive operation */

	/* setup default fixed rates */
	settxparms(vap, IEEE80211_MODE_11A, TDMA_TXRATE_11A_DEFAULT);
	settxparms(vap, IEEE80211_MODE_11B, TDMA_TXRATE_11B_DEFAULT);
	settxparms(vap, IEEE80211_MODE_11G, TDMA_TXRATE_11G_DEFAULT);
	settxparms(vap, IEEE80211_MODE_TURBO_A, TDMA_TXRATE_TURBO_DEFAULT);
	settxparms(vap, IEEE80211_MODE_TURBO_G, TDMA_TXRATE_TURBO_DEFAULT);
	settxparms(vap, IEEE80211_MODE_STURBO_A, TDMA_TXRATE_TURBO_DEFAULT);
	settxparms(vap, IEEE80211_MODE_11NA, TDMA_TXRATE_11NA_DEFAULT);
	settxparms(vap, IEEE80211_MODE_11NG, TDMA_TXRATE_11NG_DEFAULT);
	settxparms(vap, IEEE80211_MODE_HALF, TDMA_TXRATE_HALF_DEFAULT);
	settxparms(vap, IEEE80211_MODE_QUARTER, TDMA_TXRATE_QUARTER_DEFAULT);
	settxparms(vap, IEEE80211_MODE_VHT_2GHZ, TDMA_TXRATE_11NG_DEFAULT);
	settxparms(vap, IEEE80211_MODE_VHT_5GHZ, TDMA_TXRATE_11NA_DEFAULT);

	setackpolicy(vap->iv_ic, 1);	/* disable ACK's */

	ts->tdma_opdetach = vap->iv_opdetach;
	vap->iv_opdetach = tdma_vdetach;
	ts->tdma_newstate = vap->iv_newstate;
	vap->iv_newstate = tdma_newstate;
	vap->iv_bmiss = tdma_beacon_miss;
	ts->tdma_recv_mgmt = vap->iv_recv_mgmt;
	vap->iv_recv_mgmt = tdma_recv_mgmt;

	vap->iv_tdma = ts;
}

static void
tdma_vdetach(struct ieee80211vap *vap)
{
	struct ieee80211_tdma_state *ts = vap->iv_tdma;

	if (ts == NULL) {
		/* NB: should not have touched any ic state */
		return;
	}
	ts->tdma_opdetach(vap);
	IEEE80211_FREE(vap->iv_tdma, M_80211_VAP);
	vap->iv_tdma = NULL;

	setackpolicy(vap->iv_ic, 0);	/* enable ACK's */
}

static void
sta_leave(void *arg, struct ieee80211_node *ni)
{
	struct ieee80211vap *vap = ni->ni_vap;

	if (ni != vap->iv_bss)
		ieee80211_node_leave(ni);
}

/*
 * TDMA state machine handler.
 */
static int
tdma_newstate(struct ieee80211vap *vap, enum ieee80211_state nstate, int arg)
{
	struct ieee80211_tdma_state *ts = vap->iv_tdma;
	struct ieee80211com *ic = vap->iv_ic;
	enum ieee80211_state ostate;
	int status;

	IEEE80211_LOCK_ASSERT(ic);

	ostate = vap->iv_state;
	IEEE80211_DPRINTF(vap, IEEE80211_MSG_STATE, "%s: %s -> %s (%d)\n",
	    __func__, ieee80211_state_name[ostate],
	    ieee80211_state_name[nstate], arg);

	if (vap->iv_flags_ext & IEEE80211_FEXT_SWBMISS)
		callout_stop(&vap->iv_swbmiss);
	if (nstate == IEEE80211_S_SCAN &&
	    (ostate == IEEE80211_S_INIT || ostate == IEEE80211_S_RUN) &&
	    ts->tdma_slot != 0) {
		/*
		 * Override adhoc behaviour when operating as a slave;
		 * we need to scan even if the channel is locked.
		 */
		vap->iv_state = nstate;			/* state transition */
		ieee80211_cancel_scan(vap);		/* background scan */
		if (ostate == IEEE80211_S_RUN) {
			/* purge station table; entries are stale */
			ieee80211_iterate_nodes_vap(&ic->ic_sta, vap,
			    sta_leave, NULL);
		}
		if (vap->iv_flags_ext & IEEE80211_FEXT_SCANREQ) {
			ieee80211_check_scan(vap,
			    vap->iv_scanreq_flags,
			    vap->iv_scanreq_duration,
			    vap->iv_scanreq_mindwell,
			    vap->iv_scanreq_maxdwell,
			    vap->iv_scanreq_nssid, vap->iv_scanreq_ssid);
			vap->iv_flags_ext &= ~IEEE80211_FEXT_SCANREQ;
		} else
			ieee80211_check_scan_current(vap);
		status = 0;
	} else {
		status = ts->tdma_newstate(vap, nstate, arg);
	}
	if (status == 0 && 
	    nstate == IEEE80211_S_RUN && ostate != IEEE80211_S_RUN &&
	    (vap->iv_flags_ext & IEEE80211_FEXT_SWBMISS) &&
	    ts->tdma_slot != 0 &&
	    vap->iv_des_chan == IEEE80211_CHAN_ANYC) {
		/*
		 * Start s/w beacon miss timer for slave devices w/o
		 * hardware support.  Note we do this only if we're
		 * not locked to a channel (i.e. roam to follow the
		 * master). The 2x is a fudge for our doing this in
		 * software.
		 */
		vap->iv_swbmiss_period = IEEE80211_TU_TO_TICKS(
		    2 * vap->iv_bmissthreshold * ts->tdma_bintval *
		    ((ts->tdma_slotcnt * ts->tdma_slotlen) / 1024));
		vap->iv_swbmiss_count = 0;
		callout_reset(&vap->iv_swbmiss, vap->iv_swbmiss_period,
			ieee80211_swbmiss, vap);
	}
	return status;
}

static void
tdma_beacon_miss(struct ieee80211vap *vap)
{
	struct ieee80211_tdma_state *ts = vap->iv_tdma;

	IEEE80211_LOCK_ASSERT(vap->iv_ic);

	KASSERT((vap->iv_ic->ic_flags & IEEE80211_F_SCAN) == 0, ("scanning"));
	KASSERT(vap->iv_state == IEEE80211_S_RUN,
	    ("wrong state %d", vap->iv_state));

	IEEE80211_DPRINTF(vap,
		IEEE80211_MSG_STATE | IEEE80211_MSG_TDMA | IEEE80211_MSG_DEBUG,
		"beacon miss, mode %u state %s\n",
		vap->iv_opmode, ieee80211_state_name[vap->iv_state]);

	callout_stop(&vap->iv_swbmiss);

	if (ts->tdma_peer != NULL) {	/* XXX? can this be null? */
		ieee80211_notify_node_leave(vap->iv_bss);
		ts->tdma_peer = NULL;
		/*
		 * Treat beacon miss like an associate failure wrt the
		 * scan policy; this forces the entry in the scan cache
		 * to be ignored after several tries.
		 */
		ieee80211_scan_assoc_fail(vap, vap->iv_bss->ni_macaddr,
		    IEEE80211_STATUS_TIMEOUT);
	}
#if 0
	ts->tdma_inuse = 0;		/* clear slot usage */
#endif
	ieee80211_new_state(vap, IEEE80211_S_SCAN, 0);
}

static void
tdma_recv_mgmt(struct ieee80211_node *ni, struct mbuf *m0,
	int subtype, const struct ieee80211_rx_stats *rxs, int rssi, int nf)
{
	struct ieee80211com *ic = ni->ni_ic;
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211_tdma_state *ts = vap->iv_tdma;

	if (subtype == IEEE80211_FC0_SUBTYPE_BEACON &&
	    (ic->ic_flags & IEEE80211_F_SCAN) == 0) {
		struct ieee80211_frame *wh = mtod(m0, struct ieee80211_frame *);
		struct ieee80211_scanparams scan;

		/* XXX TODO: use rxstatus to determine off-channel beacons */
		if (ieee80211_parse_beacon(ni, m0, ic->ic_curchan, &scan) != 0)
			return;
		if (scan.tdma == NULL) {
			/*
			 * TDMA stations must beacon a TDMA ie; ignore
			 * any other station.
			 * XXX detect overlapping bss and change channel
			 */
			IEEE80211_DISCARD(vap,
			    IEEE80211_MSG_ELEMID | IEEE80211_MSG_INPUT,
			    wh, ieee80211_mgt_subtype_name(subtype),
			    "%s", "no TDMA ie");
			vap->iv_stats.is_rx_mgtdiscard++;
			return;
		}
		if (ni == vap->iv_bss &&
		    !IEEE80211_ADDR_EQ(wh->i_addr2, ni->ni_macaddr)) {
			/*
			 * Fake up a node for this newly
			 * discovered member of the IBSS.
			 */
			ni = ieee80211_add_neighbor(vap, wh, &scan);
			if (ni == NULL) {
				/* NB: stat kept for alloc failure */
				return;
			}
		}
		/*
		 * Check for state updates.
		 */
		if (IEEE80211_ADDR_EQ(wh->i_addr3, ni->ni_bssid)) {
			/*
			 * Count frame now that we know it's to be processed.
			 */
			vap->iv_stats.is_rx_beacon++;
			IEEE80211_NODE_STAT(ni, rx_beacons);
			/*
			 * Record tsf of last beacon.  NB: this must be
			 * done before calling tdma_process_params
			 * as deeper routines reference it.
			 */
			memcpy(&ni->ni_tstamp.data, scan.tstamp,
				sizeof(ni->ni_tstamp.data));
			/*
			 * Count beacon frame for s/w bmiss handling.
			 */
			vap->iv_swbmiss_count++;
			/*
			 * Process tdma ie.  The contents are used to sync
			 * the slot timing, reconfigure the bss, etc.
			 */
			(void) tdma_process_params(ni, scan.tdma, rssi, nf, wh);
			return;
		}
		/*
		 * NB: defer remaining work to the adhoc code; this causes
		 *     2x parsing of the frame but should happen infrequently
		 */
	}
	ts->tdma_recv_mgmt(ni, m0, subtype, rxs, rssi, nf);
}

/*
 * Update TDMA state on receipt of a beacon frame with
 * a TDMA information element.  The sender's identity
 * is provided so we can track who our peer is.  If pickslot
 * is non-zero we scan the slot allocation state in the ie
 * to locate a free slot for our use.
 */
static int
tdma_update(struct ieee80211vap *vap, const struct ieee80211_tdma_param *tdma,
	struct ieee80211_node *ni, int pickslot)
{
	struct ieee80211_tdma_state *ts = vap->iv_tdma;
	int slot, slotlen, update;

	KASSERT(vap->iv_caps & IEEE80211_C_TDMA,
	     ("not a tdma vap, caps 0x%x", vap->iv_caps));

	update = 0;
	if (tdma->tdma_slotcnt != ts->tdma_slotcnt) {
		if (!TDMA_SLOTCNT_VALID(tdma->tdma_slotcnt)) {
			if (ppsratecheck(&ts->tdma_lastprint, &ts->tdma_fails, 1))
				printf("%s: bad slot cnt %u\n",
				    __func__, tdma->tdma_slotcnt);
			return 0;
		}
		update |= TDMA_UPDATE_SLOTCNT;
 	}
	slotlen = le16toh(tdma->tdma_slotlen) * 100;
	if (slotlen != ts->tdma_slotlen) {
		if (!TDMA_SLOTLEN_VALID(slotlen)) {
			if (ppsratecheck(&ts->tdma_lastprint, &ts->tdma_fails, 1))
				printf("%s: bad slot len %u\n",
				    __func__, slotlen);
			return 0;
		}
		update |= TDMA_UPDATE_SLOTLEN;
 	}
	if (tdma->tdma_bintval != ts->tdma_bintval) {
		if (!TDMA_BINTVAL_VALID(tdma->tdma_bintval)) {
			if (ppsratecheck(&ts->tdma_lastprint, &ts->tdma_fails, 1))
				printf("%s: bad beacon interval %u\n",
				    __func__, tdma->tdma_bintval);
			return 0;
		}
		update |= TDMA_UPDATE_BINTVAL;
 	}
	slot = ts->tdma_slot;
	if (pickslot) {
		/*
		 * Pick unoccupied slot.  Note we never choose slot 0.
		 */
		for (slot = tdma->tdma_slotcnt-1; slot > 0; slot--)
			if (isclr(tdma->tdma_inuse, slot))
				break;
		if (slot <= 0) {
			printf("%s: no free slot, slotcnt %u inuse: 0x%x\n",
				__func__, tdma->tdma_slotcnt,
				tdma->tdma_inuse[0]);
			/* XXX need to do something better */
			return 0;
		}
		if (slot != ts->tdma_slot)
			update |= TDMA_UPDATE_SLOT;
	}
	if (ni != ts->tdma_peer) {
		/* update everything */
		update = TDMA_UPDATE_SLOT
		       | TDMA_UPDATE_SLOTCNT
		       | TDMA_UPDATE_SLOTLEN
		       | TDMA_UPDATE_BINTVAL;
	}

	if (update) {
		/*
		 * New/changed parameters; update runtime state.
		 */
		/* XXX overwrites user parameters */
		if (update & TDMA_UPDATE_SLOTCNT)
			ts->tdma_slotcnt = tdma->tdma_slotcnt;
		if (update & TDMA_UPDATE_SLOTLEN)
			ts->tdma_slotlen = slotlen;
		if (update & TDMA_UPDATE_SLOT)
			ts->tdma_slot = slot;
		if (update & TDMA_UPDATE_BINTVAL)
			ts->tdma_bintval = tdma->tdma_bintval;
		/* mark beacon to be updated before next xmit */
		ieee80211_beacon_notify(vap, IEEE80211_BEACON_TDMA);

		IEEE80211_DPRINTF(vap, IEEE80211_MSG_TDMA,
		    "%s: slot %u slotcnt %u slotlen %u us bintval %u\n",
		    __func__, ts->tdma_slot, ts->tdma_slotcnt,
		    ts->tdma_slotlen, ts->tdma_bintval);
	}
	/*
	 * Notify driver.  Note we can be called before
	 * entering RUN state if we scanned and are
	 * joining an existing bss.  In that case do not
	 * call the driver because not all necessary state
	 * has been setup.  The next beacon will dtrt.
	 */
	if (vap->iv_state == IEEE80211_S_RUN)
		vap->iv_ic->ic_tdma_update(ni, tdma, update);
	/*
	 * Dispatch join event on first beacon from new master.
	 */
	if (ts->tdma_peer != ni) {
		if (ts->tdma_peer != NULL)
			ieee80211_notify_node_leave(vap->iv_bss);
		ieee80211_notify_node_join(ni, 1);
		/* NB: no reference, we just use the address */
		ts->tdma_peer = ni;
	}
	return 1;
}

/*
 * Process received TDMA parameters.
 */
static int
tdma_process_params(struct ieee80211_node *ni, const u_int8_t *ie,
	int rssi, int nf, const struct ieee80211_frame *wh)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211_tdma_state *ts = vap->iv_tdma;
	const struct ieee80211_tdma_param *tdma = 
		(const struct ieee80211_tdma_param *) ie;
	u_int len = ie[1];

	KASSERT(vap->iv_caps & IEEE80211_C_TDMA,
	     ("not a tdma vap, caps 0x%x", vap->iv_caps));

	if (len < sizeof(*tdma) - 2) {
		IEEE80211_DISCARD_IE(vap,
		    IEEE80211_MSG_ELEMID | IEEE80211_MSG_TDMA,
		    wh, "tdma", "too short, len %u", len);
		return IEEE80211_REASON_IE_INVALID;
	}
	if (tdma->tdma_version != ts->tdma_version) {
		IEEE80211_DISCARD_IE(vap,
		    IEEE80211_MSG_ELEMID | IEEE80211_MSG_TDMA,
		    wh, "tdma", "bad version %u (ours %u)",
		    tdma->tdma_version, ts->tdma_version);
		return IEEE80211_REASON_IE_INVALID;
	}
 	/*
	 * NB: ideally we'd check against tdma_slotcnt, but that
	 * would require extra effort so do this easy check that
	 * covers the work below; more stringent checks are done
	 * before we make more extensive use of the ie contents.
	 */
	if (tdma->tdma_slot >= TDMA_MAXSLOTS) {
		IEEE80211_DISCARD_IE(vap,
		    IEEE80211_MSG_ELEMID | IEEE80211_MSG_TDMA,
		    wh, "tdma", "invalid slot %u", tdma->tdma_slot);
		return IEEE80211_REASON_IE_INVALID;
	}
	/*
	 * Can reach here while scanning, update
	 * operational state only in RUN state.
	 */
	if (vap->iv_state == IEEE80211_S_RUN) {
		if (tdma->tdma_slot != ts->tdma_slot &&
		    isclr(ts->tdma_inuse, tdma->tdma_slot)) {
			IEEE80211_NOTE(vap, IEEE80211_MSG_TDMA, ni,
			    "discovered in slot %u", tdma->tdma_slot);
			setbit(ts->tdma_inuse, tdma->tdma_slot);
			/* XXX dispatch event only when operating as master */
			if (ts->tdma_slot == 0)
				ieee80211_notify_node_join(ni, 1);
		}
		setbit(ts->tdma_active, tdma->tdma_slot);
		if (tdma->tdma_slot == ts->tdma_slot-1) {
			/*
			 * Slave tsf synchronization to station
			 * just before us in the schedule. The driver
			 * is responsible for copying the timestamp
			 * of the received beacon into our beacon
			 * frame so the sender can calculate round
			 * trip time.  We cannot do that here because
			 * we don't know how to update our beacon frame.
			 */
			(void) tdma_update(vap, tdma, ni, 0);
			/* XXX reschedule swbmiss timer on parameter change */
		} else if (tdma->tdma_slot == ts->tdma_slot+1) {
			uint64_t tstamp;
#if 0
			uint32_t rstamp = (uint32_t) le64toh(rs->tsf);
			int32_t rtt;
#endif
			/*
			 * Use returned timstamp to calculate the
			 * roundtrip time.
			 */
			memcpy(&tstamp, tdma->tdma_tstamp, 8);
#if 0
			/* XXX use only 15 bits of rstamp */
			rtt = rstamp - (le64toh(tstamp) & 0x7fff);
			if (rtt < 0)
				rtt += 0x7fff;
			/* XXX hack to quiet normal use */
			IEEE80211_DPRINTF(vap, IEEE80211_MSG_DOT1X,
			    "tdma rtt %5u [rstamp %5u tstamp %llu]\n",
			    rtt, rstamp,
			    (unsigned long long) le64toh(tstamp));
#endif
		} else if (tdma->tdma_slot == ts->tdma_slot &&
		    le64toh(ni->ni_tstamp.tsf) > vap->iv_bss->ni_tstamp.tsf) {
			/*
			 * Station using the same slot as us and has
			 * been around longer than us; we must move.
			 * Note this can happen if stations do not
			 * see each other while scanning.
			 */
			IEEE80211_DPRINTF(vap, IEEE80211_MSG_TDMA,
			    "slot %u collision rxtsf %llu tsf %llu\n",
			    tdma->tdma_slot,
			    (unsigned long long) le64toh(ni->ni_tstamp.tsf),
			    vap->iv_bss->ni_tstamp.tsf);
			setbit(ts->tdma_inuse, tdma->tdma_slot);

			(void) tdma_update(vap, tdma, ni, 1);
		}
	}
	return 0;
}

int
ieee80211_tdma_getslot(struct ieee80211vap *vap)
{
	struct ieee80211_tdma_state *ts = vap->iv_tdma;

	KASSERT(vap->iv_caps & IEEE80211_C_TDMA,
	     ("not a tdma vap, caps 0x%x", vap->iv_caps));
	return ts->tdma_slot;
}

/*
 * Parse a TDMA ie on station join and use it to setup node state.
 */
void
ieee80211_parse_tdma(struct ieee80211_node *ni, const uint8_t *ie)
{
	struct ieee80211vap *vap = ni->ni_vap;

	if (vap->iv_caps & IEEE80211_C_TDMA) {
		const struct ieee80211_tdma_param *tdma =
		    (const struct ieee80211_tdma_param *)ie;
		struct ieee80211_tdma_state *ts = vap->iv_tdma;
		/*
		 * Adopt TDMA configuration when joining an
		 * existing network.
		 */
		setbit(ts->tdma_inuse, tdma->tdma_slot);
		(void) tdma_update(vap, tdma, ni, 1);
		/*
		 * Propagate capabilities based on the local
		 * configuration and the remote station's advertised
		 * capabilities. In particular this permits us to
		 * enable use of QoS to disable ACK's.
		 */
		if ((vap->iv_flags & IEEE80211_F_WME) &&
		    ni->ni_ies.wme_ie != NULL)
			ni->ni_flags |= IEEE80211_NODE_QOS;
	}
}

#define	TDMA_OUI_BYTES		0x00, 0x03, 0x7f
/*
 * Add a TDMA parameters element to a frame.
 */
uint8_t *
ieee80211_add_tdma(uint8_t *frm, struct ieee80211vap *vap)
{
#define	ADDSHORT(frm, v) do {			\
	frm[0] = (v) & 0xff;			\
	frm[1] = (v) >> 8;			\
	frm += 2;				\
} while (0)
	static const struct ieee80211_tdma_param param = {
		.tdma_id	= IEEE80211_ELEMID_VENDOR,
		.tdma_len	= sizeof(struct ieee80211_tdma_param) - 2,
		.tdma_oui	= { TDMA_OUI_BYTES },
		.tdma_type	= TDMA_OUI_TYPE,
		.tdma_subtype	= TDMA_SUBTYPE_PARAM,
		.tdma_version	= TDMA_VERSION,
	};
	const struct ieee80211_tdma_state *ts = vap->iv_tdma;
	uint16_t slotlen;

	KASSERT(vap->iv_caps & IEEE80211_C_TDMA,
	     ("not a tdma vap, caps 0x%x", vap->iv_caps));

	memcpy(frm, &param, sizeof(param));
	frm += __offsetof(struct ieee80211_tdma_param, tdma_slot);
	*frm++ = ts->tdma_slot;
	*frm++ = ts->tdma_slotcnt;
	/* NB: convert units to fit in 16-bits */
	slotlen = ts->tdma_slotlen / 100;	/* 100us units */
	ADDSHORT(frm, slotlen);
	*frm++ = ts->tdma_bintval;
	*frm++ = ts->tdma_inuse[0];
	frm += 10;				/* pad+timestamp */
	return frm; 
#undef ADDSHORT
}
#undef TDMA_OUI_BYTES

/*
 * Update TDMA state at TBTT.
 */
void
ieee80211_tdma_update_beacon(struct ieee80211vap *vap,
	struct ieee80211_beacon_offsets *bo)
{
	struct ieee80211_tdma_state *ts = vap->iv_tdma;

	KASSERT(vap->iv_caps & IEEE80211_C_TDMA,
	     ("not a tdma vap, caps 0x%x", vap->iv_caps));

	if (isset(bo->bo_flags,  IEEE80211_BEACON_TDMA)) {
		(void) ieee80211_add_tdma(bo->bo_tdma, vap);
		clrbit(bo->bo_flags, IEEE80211_BEACON_TDMA);
	}
	if (ts->tdma_slot != 0)		/* only on master */
		return;
	if (ts->tdma_count <= 0) {
		/*
		 * Time to update the mask of active/inuse stations.
		 * We track stations that we've received a beacon
		 * frame from and update this mask periodically.
		 * This allows us to miss a few beacons before marking
		 * a slot free for re-use.
		 */
		ts->tdma_inuse[0] = ts->tdma_active[0];
		ts->tdma_active[0] = 0x01;
		/* update next time 'round */
		/* XXX use notify framework */
		setbit(bo->bo_flags, IEEE80211_BEACON_TDMA);
		/* NB: use s/w beacon miss threshold; may be too high */
		ts->tdma_count = vap->iv_bmissthreshold-1;
	} else
		ts->tdma_count--;
}

static int
tdma_ioctl_get80211(struct ieee80211vap *vap, struct ieee80211req *ireq)
{
	struct ieee80211_tdma_state *ts = vap->iv_tdma;

	if ((vap->iv_caps & IEEE80211_C_TDMA) == 0)
		return ENOSYS;

	switch (ireq->i_type) {
	case IEEE80211_IOC_TDMA_SLOT:
		ireq->i_val = ts->tdma_slot;
		break;
	case IEEE80211_IOC_TDMA_SLOTCNT:
		ireq->i_val = ts->tdma_slotcnt;
		break;
	case IEEE80211_IOC_TDMA_SLOTLEN:
		ireq->i_val = ts->tdma_slotlen;
		break;
	case IEEE80211_IOC_TDMA_BINTERVAL:
		ireq->i_val = ts->tdma_bintval;
		break;
	default:
		return ENOSYS;
	}
	return 0;
}
IEEE80211_IOCTL_GET(tdma, tdma_ioctl_get80211);

static int
tdma_ioctl_set80211(struct ieee80211vap *vap, struct ieee80211req *ireq)
{
	struct ieee80211_tdma_state *ts = vap->iv_tdma;

	if ((vap->iv_caps & IEEE80211_C_TDMA) == 0)
		return ENOSYS;

	switch (ireq->i_type) {
	case IEEE80211_IOC_TDMA_SLOT:
		if (!(0 <= ireq->i_val && ireq->i_val <= ts->tdma_slotcnt))
			return EINVAL;
		if (ireq->i_val != ts->tdma_slot) {
			ts->tdma_slot = ireq->i_val;
			goto restart;
		}
		break;
	case IEEE80211_IOC_TDMA_SLOTCNT:
		if (!TDMA_SLOTCNT_VALID(ireq->i_val))
			return EINVAL;
		if (ireq->i_val != ts->tdma_slotcnt) {
			ts->tdma_slotcnt = ireq->i_val;
			goto restart;
		}
		break;
	case IEEE80211_IOC_TDMA_SLOTLEN:
		/*
		 * XXX
		 * 150 insures at least 1/8 TU
		 * 0xfffff is the max duration for bursting
		 * (implict by way of 16-bit data type for i_val)
		 */
		if (!TDMA_SLOTLEN_VALID(ireq->i_val))
			return EINVAL;
		if (ireq->i_val != ts->tdma_slotlen) {
			ts->tdma_slotlen = ireq->i_val;
			goto restart;
		}
		break;
	case IEEE80211_IOC_TDMA_BINTERVAL:
		if (!TDMA_BINTVAL_VALID(ireq->i_val))
			return EINVAL;
		if (ireq->i_val != ts->tdma_bintval) {
			ts->tdma_bintval = ireq->i_val;
			goto restart;
		}
		break;
	default:
		return ENOSYS;
	}
	return 0;
restart:
	ieee80211_beacon_notify(vap, IEEE80211_BEACON_TDMA);
	return ERESTART;
}
IEEE80211_IOCTL_SET(tdma, tdma_ioctl_set80211);

#endif	/* IEEE80211_SUPPORT_TDMA */
