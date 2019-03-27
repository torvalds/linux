/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002-2008 Sam Leffler, Errno Consulting
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
__FBSDID("$FreeBSD$");

/*
 * IEEE 802.11 scanning support.
 */
#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/systm.h> 
#include <sys/proc.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/condvar.h>
 
#include <sys/socket.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_media.h>
#include <net/ethernet.h>

#include <net80211/ieee80211_var.h>

/* XXX until it's implemented as attach ops */
#include <net80211/ieee80211_scan_sw.h>

#include <net/bpf.h>

/*
 * Roaming-related defaults.  RSSI thresholds are as returned by the
 * driver (.5dBm).  Transmit rate thresholds are IEEE rate codes (i.e
 * .5M units) or MCS.
 */
/* rssi thresholds */
#define	ROAM_RSSI_11A_DEFAULT		14	/* 11a bss */
#define	ROAM_RSSI_11B_DEFAULT		14	/* 11b bss */
#define	ROAM_RSSI_11BONLY_DEFAULT	14	/* 11b-only bss */
/* transmit rate thresholds */
#define	ROAM_RATE_11A_DEFAULT		2*12	/* 11a bss */
#define	ROAM_RATE_11B_DEFAULT		2*5	/* 11b bss */
#define	ROAM_RATE_11BONLY_DEFAULT	2*1	/* 11b-only bss */
#define	ROAM_RATE_HALF_DEFAULT		2*6	/* half-width 11a/g bss */
#define	ROAM_RATE_QUARTER_DEFAULT	2*3	/* quarter-width 11a/g bss */
#define	ROAM_MCS_11N_DEFAULT		(1 | IEEE80211_RATE_MCS) /* 11n bss */
#define	ROAM_MCS_11AC_DEFAULT		(1 | IEEE80211_RATE_MCS) /* 11ac bss; XXX not used yet */

void
ieee80211_scan_attach(struct ieee80211com *ic)
{
	/*
	 * If there's no scan method pointer, attach the
	 * swscan set as a default.
	 */
	if (ic->ic_scan_methods == NULL)
		ieee80211_swscan_attach(ic);
	else
		ic->ic_scan_methods->sc_attach(ic);
}

void
ieee80211_scan_detach(struct ieee80211com *ic)
{

	/*
	 * Ideally we'd do the ss_ops detach call here;
	 * but then sc_detach() would need to be split in two.
	 *
	 * I'll do that later.
	 */
	ic->ic_scan_methods->sc_detach(ic);
}

static const struct ieee80211_roamparam defroam[IEEE80211_MODE_MAX] = {
	[IEEE80211_MODE_11A]	= { .rssi = ROAM_RSSI_11A_DEFAULT,
				    .rate = ROAM_RATE_11A_DEFAULT },
	[IEEE80211_MODE_11G]	= { .rssi = ROAM_RSSI_11B_DEFAULT,
				    .rate = ROAM_RATE_11B_DEFAULT },
	[IEEE80211_MODE_11B]	= { .rssi = ROAM_RSSI_11BONLY_DEFAULT,
				    .rate = ROAM_RATE_11BONLY_DEFAULT },
	[IEEE80211_MODE_TURBO_A]= { .rssi = ROAM_RSSI_11A_DEFAULT,
				    .rate = ROAM_RATE_11A_DEFAULT },
	[IEEE80211_MODE_TURBO_G]= { .rssi = ROAM_RSSI_11A_DEFAULT,
				    .rate = ROAM_RATE_11A_DEFAULT },
	[IEEE80211_MODE_STURBO_A]={ .rssi = ROAM_RSSI_11A_DEFAULT,
				    .rate = ROAM_RATE_11A_DEFAULT },
	[IEEE80211_MODE_HALF]	= { .rssi = ROAM_RSSI_11A_DEFAULT,
				    .rate = ROAM_RATE_HALF_DEFAULT },
	[IEEE80211_MODE_QUARTER]= { .rssi = ROAM_RSSI_11A_DEFAULT,
				    .rate = ROAM_RATE_QUARTER_DEFAULT },
	[IEEE80211_MODE_11NA]	= { .rssi = ROAM_RSSI_11A_DEFAULT,
				    .rate = ROAM_MCS_11N_DEFAULT },
	[IEEE80211_MODE_11NG]	= { .rssi = ROAM_RSSI_11B_DEFAULT,
				    .rate = ROAM_MCS_11N_DEFAULT },
	[IEEE80211_MODE_VHT_2GHZ]	= { .rssi = ROAM_RSSI_11B_DEFAULT,
				    .rate = ROAM_MCS_11AC_DEFAULT },
	[IEEE80211_MODE_VHT_5GHZ]	= { .rssi = ROAM_RSSI_11A_DEFAULT,
				    .rate = ROAM_MCS_11AC_DEFAULT },

};

void
ieee80211_scan_vattach(struct ieee80211vap *vap)
{
	struct ieee80211com *ic = vap->iv_ic;
	int m;

	vap->iv_bgscanidle = (IEEE80211_BGSCAN_IDLE_DEFAULT*1000)/hz;
	vap->iv_bgscanintvl = IEEE80211_BGSCAN_INTVAL_DEFAULT*hz;
	vap->iv_scanvalid = IEEE80211_SCAN_VALID_DEFAULT*hz;

	vap->iv_roaming = IEEE80211_ROAMING_AUTO;

	memset(vap->iv_roamparms, 0, sizeof(vap->iv_roamparms));
	for (m = IEEE80211_MODE_AUTO + 1; m < IEEE80211_MODE_MAX; m++) {
		if (isclr(ic->ic_modecaps, m))
			continue;

		memcpy(&vap->iv_roamparms[m], &defroam[m], sizeof(defroam[m]));
	}

	ic->ic_scan_methods->sc_vattach(vap);
}

void
ieee80211_scan_vdetach(struct ieee80211vap *vap)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_scan_state *ss;

	IEEE80211_LOCK(ic);
	ss = ic->ic_scan;

	ic->ic_scan_methods->sc_vdetach(vap);

	if (ss != NULL && ss->ss_vap == vap) {
		if (ss->ss_ops != NULL) {
			ss->ss_ops->scan_detach(ss);
			ss->ss_ops = NULL;
		}
		ss->ss_vap = NULL;
	}
	IEEE80211_UNLOCK(ic);
}

/*
 * Simple-minded scanner module support.
 */
static const char *scan_modnames[IEEE80211_OPMODE_MAX] = {
	"wlan_scan_sta",	/* IEEE80211_M_IBSS */
	"wlan_scan_sta",	/* IEEE80211_M_STA */
	"wlan_scan_wds",	/* IEEE80211_M_WDS */
	"wlan_scan_sta",	/* IEEE80211_M_AHDEMO */
	"wlan_scan_ap",		/* IEEE80211_M_HOSTAP */
	"wlan_scan_monitor",	/* IEEE80211_M_MONITOR */
	"wlan_scan_sta",	/* IEEE80211_M_MBSS */
};
static const struct ieee80211_scanner *scanners[IEEE80211_OPMODE_MAX];

const struct ieee80211_scanner *
ieee80211_scanner_get(enum ieee80211_opmode mode)
{
	if (mode >= IEEE80211_OPMODE_MAX)
		return NULL;
	if (scanners[mode] == NULL)
		ieee80211_load_module(scan_modnames[mode]);
	return scanners[mode];
}

void
ieee80211_scanner_register(enum ieee80211_opmode mode,
	const struct ieee80211_scanner *scan)
{
	if (mode >= IEEE80211_OPMODE_MAX)
		return;
	scanners[mode] = scan;
}

void
ieee80211_scanner_unregister(enum ieee80211_opmode mode,
	const struct ieee80211_scanner *scan)
{
	if (mode >= IEEE80211_OPMODE_MAX)
		return;
	if (scanners[mode] == scan)
		scanners[mode] = NULL;
}

void
ieee80211_scanner_unregister_all(const struct ieee80211_scanner *scan)
{
	int m;

	for (m = 0; m < IEEE80211_OPMODE_MAX; m++)
		if (scanners[m] == scan)
			scanners[m] = NULL;
}

/*
 * Update common scanner state to reflect the current
 * operating mode.  This is called when the state machine
 * is transitioned to RUN state w/o scanning--e.g. when
 * operating in monitor mode.  The purpose of this is to
 * ensure later callbacks find ss_ops set to properly
 * reflect current operating mode.
 */
void
ieee80211_scan_update_locked(struct ieee80211vap *vap,
	const struct ieee80211_scanner *scan)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_scan_state *ss = ic->ic_scan;

	IEEE80211_LOCK_ASSERT(ic);

#ifdef IEEE80211_DEBUG
	if (ss->ss_vap != vap || ss->ss_ops != scan) {
		IEEE80211_DPRINTF(vap, IEEE80211_MSG_SCAN,
		    "%s: current scanner is <%s:%s>, switch to <%s:%s>\n",
		    __func__,
		    ss->ss_vap != NULL ?
			ss->ss_vap->iv_ifp->if_xname : "none",
		    ss->ss_vap != NULL ?
			ieee80211_opmode_name[ss->ss_vap->iv_opmode] : "none",
		    vap->iv_ifp->if_xname,
		    ieee80211_opmode_name[vap->iv_opmode]);
	}
#endif
	ss->ss_vap = vap;
	if (ss->ss_ops != scan) {
		/*
		 * Switch scanners; detach old, attach new.  Special
		 * case where a single scan module implements multiple
		 * policies by using different scan ops but a common
		 * core.  We assume if the old and new attach methods
		 * are identical then it's ok to just change ss_ops
		 * and not flush the internal state of the module.
		 */
		if (scan == NULL || ss->ss_ops == NULL ||
		    ss->ss_ops->scan_attach != scan->scan_attach) {
			if (ss->ss_ops != NULL)
				ss->ss_ops->scan_detach(ss);
			if (scan != NULL && !scan->scan_attach(ss)) {
				/* XXX attach failure */
				/* XXX stat+msg */
				scan = NULL;
			}
		}
		ss->ss_ops = scan;
	}
}

void
ieee80211_scan_dump_channels(const struct ieee80211_scan_state *ss)
{
	struct ieee80211com *ic = ss->ss_ic;
	const char *sep;
	int i;

	sep = "";
	for (i = ss->ss_next; i < ss->ss_last; i++) {
		const struct ieee80211_channel *c = ss->ss_chans[i];

		printf("%s%u%c", sep, ieee80211_chan2ieee(ic, c),
		    ieee80211_channel_type_char(c));
		sep = ", ";
	}
}

#ifdef IEEE80211_DEBUG
void
ieee80211_scan_dump(struct ieee80211_scan_state *ss)
{
	struct ieee80211vap *vap = ss->ss_vap;

	if_printf(vap->iv_ifp, "scan set ");
	ieee80211_scan_dump_channels(ss);
	printf(" dwell min %ums max %ums\n",
	    ticks_to_msecs(ss->ss_mindwell), ticks_to_msecs(ss->ss_maxdwell));
}
#endif /* IEEE80211_DEBUG */

void
ieee80211_scan_copy_ssid(struct ieee80211vap *vap, struct ieee80211_scan_state *ss,
	int nssid, const struct ieee80211_scan_ssid ssids[])
{
	if (nssid > IEEE80211_SCAN_MAX_SSID) {
		/* XXX printf */
		IEEE80211_DPRINTF(vap, IEEE80211_MSG_SCAN,
		    "%s: too many ssid %d, ignoring all of them\n",
		    __func__, nssid);
		return;
	}
	memcpy(ss->ss_ssid, ssids, nssid * sizeof(ssids[0]));
	ss->ss_nssid = nssid;
}

/*
 * Start a scan unless one is already going.
 */
int
ieee80211_start_scan(struct ieee80211vap *vap, int flags,
	u_int duration, u_int mindwell, u_int maxdwell,
	u_int nssid, const struct ieee80211_scan_ssid ssids[])
{
	const struct ieee80211_scanner *scan;
	struct ieee80211com *ic = vap->iv_ic;

	scan = ieee80211_scanner_get(vap->iv_opmode);
	if (scan == NULL) {
		IEEE80211_DPRINTF(vap, IEEE80211_MSG_SCAN,
		    "%s: no scanner support for %s mode\n",
		    __func__, ieee80211_opmode_name[vap->iv_opmode]);
		/* XXX stat */
		return 0;
	}

	return ic->ic_scan_methods->sc_start_scan(scan, vap, flags, duration,
	    mindwell, maxdwell, nssid, ssids);
}

/*
 * Check the scan cache for an ap/channel to use; if that
 * fails then kick off a new scan.
 */
int
ieee80211_check_scan(struct ieee80211vap *vap, int flags,
	u_int duration, u_int mindwell, u_int maxdwell,
	u_int nssid, const struct ieee80211_scan_ssid ssids[])
{
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_scan_state *ss = ic->ic_scan;
	const struct ieee80211_scanner *scan;
	int result;

	scan = ieee80211_scanner_get(vap->iv_opmode);
	if (scan == NULL) {
		IEEE80211_DPRINTF(vap, IEEE80211_MSG_SCAN,
		    "%s: no scanner support for %s mode\n",
		    __func__, vap->iv_opmode);
		/* XXX stat */
		return 0;
	}

	/*
	 * Check if there's a list of scan candidates already.
	 * XXX want more than the ap we're currently associated with
	 */

	IEEE80211_LOCK(ic);
	IEEE80211_DPRINTF(vap, IEEE80211_MSG_SCAN,
	    "%s: %s scan, %s%s%s%s%s\n"
	    , __func__
	    , flags & IEEE80211_SCAN_ACTIVE ? "active" : "passive"
	    , flags & IEEE80211_SCAN_FLUSH ? "flush" : "append"
	    , flags & IEEE80211_SCAN_NOPICK ? ", nopick" : ""
	    , flags & IEEE80211_SCAN_NOJOIN ? ", nojoin" : ""
	    , flags & IEEE80211_SCAN_PICK1ST ? ", pick1st" : ""
	    , flags & IEEE80211_SCAN_ONCE ? ", once" : ""
	);

	if (ss->ss_ops != scan) {
		/* XXX re-use cache contents? e.g. adhoc<->sta */
		flags |= IEEE80211_SCAN_FLUSH;
	}

	/*
	 * XXX TODO: separate things out a bit better.
	 */
	ieee80211_scan_update_locked(vap, scan);

	result = ic->ic_scan_methods->sc_check_scan(scan, vap, flags, duration,
	    mindwell, maxdwell, nssid, ssids);

	IEEE80211_UNLOCK(ic);

	return (result);
}

/*
 * Check the scan cache for an ap/channel to use; if that fails
 * then kick off a scan using the current settings.
 */
int
ieee80211_check_scan_current(struct ieee80211vap *vap)
{
	return ieee80211_check_scan(vap,
	    IEEE80211_SCAN_ACTIVE,
	    IEEE80211_SCAN_FOREVER, 0, 0,
	    vap->iv_des_nssid, vap->iv_des_ssid);
}

/*
 * Restart a previous scan.  If the previous scan completed
 * then we start again using the existing channel list.
 */
int
ieee80211_bg_scan(struct ieee80211vap *vap, int flags)
{
	struct ieee80211com *ic = vap->iv_ic;
	const struct ieee80211_scanner *scan;

	// IEEE80211_UNLOCK_ASSERT(sc);

	scan = ieee80211_scanner_get(vap->iv_opmode);
	if (scan == NULL) {
		IEEE80211_DPRINTF(vap, IEEE80211_MSG_SCAN,
		    "%s: no scanner support for %s mode\n",
		    __func__, vap->iv_opmode);
		/* XXX stat */
		return 0;
	}

	/*
	 * XXX TODO: pull apart the bgscan logic into whatever
	 * belongs here and whatever belongs in the software
	 * scanner.
	 */
	return (ic->ic_scan_methods->sc_bg_scan(scan, vap, flags));
}

/*
 * Cancel any scan currently going on for the specified vap.
 */
void
ieee80211_cancel_scan(struct ieee80211vap *vap)
{
	struct ieee80211com *ic = vap->iv_ic;

	ic->ic_scan_methods->sc_cancel_scan(vap);
}

/*
 * Cancel any scan currently going on.
 *
 * This is called during normal 802.11 data path to cancel
 * a scan so a newly arrived normal data packet can be sent.
 */
void
ieee80211_cancel_anyscan(struct ieee80211vap *vap)
{
	struct ieee80211com *ic = vap->iv_ic;

	ic->ic_scan_methods->sc_cancel_anyscan(vap);
}

/*
 * Manually switch to the next channel in the channel list.
 * Provided for drivers that manage scanning themselves
 * (e.g. for firmware-based devices).
 */
void
ieee80211_scan_next(struct ieee80211vap *vap)
{
	struct ieee80211com *ic = vap->iv_ic;

	ic->ic_scan_methods->sc_scan_next(vap);
}

/*
 * Manually stop a scan that is currently running.
 * Provided for drivers that are not able to scan single channels
 * (e.g. for firmware-based devices).
 */
void
ieee80211_scan_done(struct ieee80211vap *vap)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_scan_state *ss;

	IEEE80211_DPRINTF(vap, IEEE80211_MSG_SCAN, "%s: called\n", __func__);

	IEEE80211_LOCK(ic);
	ss = ic->ic_scan;
	ss->ss_next = ss->ss_last; /* all channels are complete */

	ic->ic_scan_methods->sc_scan_done(vap);

	IEEE80211_UNLOCK(ic);
}

/*
 * Probe the current channel, if allowed, while scanning.
 * If the channel is not marked passive-only then send
 * a probe request immediately.  Otherwise mark state and
 * listen for beacons on the channel; if we receive something
 * then we'll transmit a probe request.
 */
void
ieee80211_probe_curchan(struct ieee80211vap *vap, int force)
{
	struct ieee80211com *ic = vap->iv_ic;

	if ((ic->ic_curchan->ic_flags & IEEE80211_CHAN_PASSIVE) && !force) {
		ic->ic_flags_ext |= IEEE80211_FEXT_PROBECHAN;
		return;
	}

	ic->ic_scan_methods->sc_scan_probe_curchan(vap, force);
}

#ifdef IEEE80211_DEBUG
static void
dump_country(const uint8_t *ie)
{
	const struct ieee80211_country_ie *cie =
	   (const struct ieee80211_country_ie *) ie;
	int i, nbands, schan, nchan;

	if (cie->len < 3) {
		printf(" <bogus country ie, len %d>", cie->len);
		return;
	}
	printf(" country [%c%c%c", cie->cc[0], cie->cc[1], cie->cc[2]);
	nbands = (cie->len - 3) / sizeof(cie->band[0]);
	for (i = 0; i < nbands; i++) {
		schan = cie->band[i].schan;
		nchan = cie->band[i].nchan;
		if (nchan != 1)
			printf(" %u-%u,%u", schan, schan + nchan-1,
			    cie->band[i].maxtxpwr);
		else
			printf(" %u,%u", schan, cie->band[i].maxtxpwr);
	}
	printf("]");
}

void
ieee80211_scan_dump_probe_beacon(uint8_t subtype, int isnew,
	const uint8_t mac[IEEE80211_ADDR_LEN],
	const struct ieee80211_scanparams *sp, int rssi)
{

	printf("[%s] %s%s on chan %u (bss chan %u) ",
	    ether_sprintf(mac), isnew ? "new " : "",
	    ieee80211_mgt_subtype_name(subtype), sp->chan, sp->bchan);
	ieee80211_print_essid(sp->ssid + 2, sp->ssid[1]);
	printf(" rssi %d\n", rssi);

	if (isnew) {
		printf("[%s] caps 0x%x bintval %u erp 0x%x", 
			ether_sprintf(mac), sp->capinfo, sp->bintval, sp->erp);
		if (sp->country != NULL)
			dump_country(sp->country);
		printf("\n");
	}
}
#endif /* IEEE80211_DEBUG */

/*
 * Process a beacon or probe response frame.
 */
void
ieee80211_add_scan(struct ieee80211vap *vap,
	struct ieee80211_channel *curchan,
	const struct ieee80211_scanparams *sp,
	const struct ieee80211_frame *wh,
	int subtype, int rssi, int noise)
{
	struct ieee80211com *ic = vap->iv_ic;

	return (ic->ic_scan_methods->sc_add_scan(vap, curchan, sp, wh, subtype,
	    rssi, noise));
}

/*
 * Timeout/age scan cache entries; called from sta timeout
 * timer (XXX should be self-contained).
 */
void
ieee80211_scan_timeout(struct ieee80211com *ic)
{
	struct ieee80211_scan_state *ss = ic->ic_scan;

	if (ss->ss_ops != NULL)
		ss->ss_ops->scan_age(ss);
}

/*
 * Mark a scan cache entry after a successful associate.
 */
void
ieee80211_scan_assoc_success(struct ieee80211vap *vap, const uint8_t mac[])
{
	struct ieee80211_scan_state *ss = vap->iv_ic->ic_scan;

	if (ss->ss_ops != NULL) {
		IEEE80211_NOTE_MAC(vap, IEEE80211_MSG_SCAN,
			mac, "%s",  __func__);
		ss->ss_ops->scan_assoc_success(ss, mac);
	}
}

/*
 * Demerit a scan cache entry after failing to associate.
 */
void
ieee80211_scan_assoc_fail(struct ieee80211vap *vap,
	const uint8_t mac[], int reason)
{
	struct ieee80211_scan_state *ss = vap->iv_ic->ic_scan;

	if (ss->ss_ops != NULL) {
		IEEE80211_NOTE_MAC(vap, IEEE80211_MSG_SCAN, mac,
			"%s: reason %u", __func__, reason);
		ss->ss_ops->scan_assoc_fail(ss, mac, reason);
	}
}

/*
 * Iterate over the contents of the scan cache.
 */
void
ieee80211_scan_iterate(struct ieee80211vap *vap,
	ieee80211_scan_iter_func *f, void *arg)
{
	struct ieee80211_scan_state *ss = vap->iv_ic->ic_scan;

	if (ss->ss_ops != NULL)
		ss->ss_ops->scan_iterate(ss, f, arg);
}

/*
 * Flush the contents of the scan cache.
 */
void
ieee80211_scan_flush(struct ieee80211vap *vap)
{
	struct ieee80211_scan_state *ss = vap->iv_ic->ic_scan;

	if (ss->ss_ops != NULL && ss->ss_vap == vap) {
		IEEE80211_DPRINTF(vap, IEEE80211_MSG_SCAN, "%s\n",  __func__);
		ss->ss_ops->scan_flush(ss);
	}
}

/*
 * Check the scan cache for an ap/channel to use; if that
 * fails then kick off a new scan.
 */
struct ieee80211_channel *
ieee80211_scan_pickchannel(struct ieee80211com *ic, int flags)
{
	struct ieee80211_scan_state *ss = ic->ic_scan;

	IEEE80211_LOCK_ASSERT(ic);

	if (ss == NULL || ss->ss_ops == NULL || ss->ss_vap == NULL) {
		/* XXX printf? */
		return NULL;
	}
	if (ss->ss_ops->scan_pickchan == NULL) {
		IEEE80211_DPRINTF(ss->ss_vap, IEEE80211_MSG_SCAN,
		    "%s: scan module does not support picking a channel, "
		    "opmode %s\n", __func__, ss->ss_vap->iv_opmode);
		return NULL;
	}
	return ss->ss_ops->scan_pickchan(ss, flags);
}
