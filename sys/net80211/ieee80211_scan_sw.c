/*-
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

#include <net80211/ieee80211_scan_sw.h>

#include <net/bpf.h>

struct scan_state {
	struct ieee80211_scan_state base;	/* public state */

	u_int			ss_iflags;	/* flags used internally */
#define	ISCAN_MINDWELL 		0x0001		/* min dwell time reached */
#define	ISCAN_DISCARD		0x0002		/* discard rx'd frames */
#define ISCAN_INTERRUPT		0x0004		/* interrupt current scan */
#define	ISCAN_CANCEL		0x0008		/* cancel current scan */
#define ISCAN_PAUSE		(ISCAN_INTERRUPT | ISCAN_CANCEL)
#define	ISCAN_ABORT		0x0010		/* end the scan immediately */
#define	ISCAN_RUNNING		0x0020		/* scan was started */

	unsigned long		ss_chanmindwell;  /* min dwell on curchan */
	unsigned long		ss_scanend;	/* time scan must stop */
	u_int			ss_duration;	/* duration for next scan */
	struct task		ss_scan_start;	/* scan start */
	struct timeout_task	ss_scan_curchan;  /* scan execution */
};
#define	SCAN_PRIVATE(ss)	((struct scan_state *) ss)

/*
 * Amount of time to go off-channel during a background
 * scan.  This value should be large enough to catch most
 * ap's but short enough that we can return on-channel
 * before our listen interval expires.
 *
 * XXX tunable
 * XXX check against configured listen interval
 */
#define	IEEE80211_SCAN_OFFCHANNEL	msecs_to_ticks(150)

static	void scan_curchan(struct ieee80211_scan_state *, unsigned long);
static	void scan_mindwell(struct ieee80211_scan_state *);
static	void scan_signal(struct ieee80211_scan_state *, int);
static	void scan_signal_locked(struct ieee80211_scan_state *, int);
static	void scan_start(void *, int);
static	void scan_curchan_task(void *, int);
static	void scan_end(struct ieee80211_scan_state *, int);
static	void scan_done(struct ieee80211_scan_state *, int);

MALLOC_DEFINE(M_80211_SCAN, "80211scan", "802.11 scan state");

static void
ieee80211_swscan_detach(struct ieee80211com *ic)
{
	struct ieee80211_scan_state *ss = ic->ic_scan;

	if (ss != NULL) {
		scan_signal(ss, ISCAN_ABORT);
		ieee80211_draintask(ic, &SCAN_PRIVATE(ss)->ss_scan_start);
		taskqueue_drain_timeout(ic->ic_tq,
		    &SCAN_PRIVATE(ss)->ss_scan_curchan);
		KASSERT((ic->ic_flags & IEEE80211_F_SCAN) == 0,
		    ("scan still running"));

		/*
		 * For now, do the ss_ops detach here rather
		 * than ieee80211_scan_detach().
		 *
		 * I'll figure out how to cleanly split things up
		 * at a later date.
		 */
		if (ss->ss_ops != NULL) {
			ss->ss_ops->scan_detach(ss);
			ss->ss_ops = NULL;
		}
		ic->ic_scan = NULL;
		IEEE80211_FREE(SCAN_PRIVATE(ss), M_80211_SCAN);
	}
}

static void
ieee80211_swscan_vattach(struct ieee80211vap *vap)
{
	/* nothing to do for now */
	/*
	 * TODO: all of the vap scan calls should be methods!
	 */

}

static void
ieee80211_swscan_vdetach(struct ieee80211vap *vap)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_scan_state *ss = ic->ic_scan;

	IEEE80211_LOCK_ASSERT(ic);

	if (ss != NULL && ss->ss_vap == vap &&
	    (ic->ic_flags & IEEE80211_F_SCAN))
		scan_signal_locked(ss, ISCAN_ABORT);
}

static void
ieee80211_swscan_set_scan_duration(struct ieee80211vap *vap, u_int duration)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_scan_state *ss = ic->ic_scan;

	IEEE80211_LOCK_ASSERT(ic);

	/* NB: flush frames rx'd before 1st channel change */
	SCAN_PRIVATE(ss)->ss_iflags |= ISCAN_DISCARD;
	SCAN_PRIVATE(ss)->ss_duration = duration;
}

/*
 * Start a scan unless one is already going.
 */
static int
ieee80211_swscan_start_scan_locked(const struct ieee80211_scanner *scan,
	struct ieee80211vap *vap, int flags, u_int duration,
	u_int mindwell, u_int maxdwell,
	u_int nssid, const struct ieee80211_scan_ssid ssids[])
{
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_scan_state *ss = ic->ic_scan;

	IEEE80211_LOCK_ASSERT(ic);

	if (ic->ic_flags & IEEE80211_F_CSAPENDING) {
		IEEE80211_DPRINTF(vap, IEEE80211_MSG_SCAN,
		    "%s: scan inhibited by pending channel change\n", __func__);
	} else if ((ic->ic_flags & IEEE80211_F_SCAN) == 0) {
		IEEE80211_DPRINTF(vap, IEEE80211_MSG_SCAN,
		    "%s: %s scan, duration %u mindwell %u maxdwell %u, desired mode %s, %s%s%s%s%s%s\n"
		    , __func__
		    , flags & IEEE80211_SCAN_ACTIVE ? "active" : "passive"
		    , duration, mindwell, maxdwell
		    , ieee80211_phymode_name[vap->iv_des_mode]
		    , flags & IEEE80211_SCAN_FLUSH ? "flush" : "append"
		    , flags & IEEE80211_SCAN_NOPICK ? ", nopick" : ""
		    , flags & IEEE80211_SCAN_NOJOIN ? ", nojoin" : ""
		    , flags & IEEE80211_SCAN_NOBCAST ? ", nobcast" : ""
		    , flags & IEEE80211_SCAN_PICK1ST ? ", pick1st" : ""
		    , flags & IEEE80211_SCAN_ONCE ? ", once" : ""
		);

		ieee80211_scan_update_locked(vap, scan);
		if (ss->ss_ops != NULL) {
			if ((flags & IEEE80211_SCAN_NOSSID) == 0)
				ieee80211_scan_copy_ssid(vap, ss, nssid, ssids);

			/* NB: top 4 bits for internal use */
			ss->ss_flags = flags & 0xfff;
			if (ss->ss_flags & IEEE80211_SCAN_ACTIVE)
				vap->iv_stats.is_scan_active++;
			else
				vap->iv_stats.is_scan_passive++;
			if (flags & IEEE80211_SCAN_FLUSH)
				ss->ss_ops->scan_flush(ss);
			if (flags & IEEE80211_SCAN_BGSCAN)
				ic->ic_flags_ext |= IEEE80211_FEXT_BGSCAN;

			/* Set duration for this particular scan */
			ieee80211_swscan_set_scan_duration(vap, duration);

			ss->ss_next = 0;
			ss->ss_mindwell = mindwell;
			ss->ss_maxdwell = maxdwell;
			/* NB: scan_start must be before the scan runtask */
			ss->ss_ops->scan_start(ss, vap);
#ifdef IEEE80211_DEBUG
			if (ieee80211_msg_scan(vap))
				ieee80211_scan_dump(ss);
#endif /* IEEE80211_DEBUG */
			ic->ic_flags |= IEEE80211_F_SCAN;

			/* Start scan task */
			ieee80211_runtask(ic, &SCAN_PRIVATE(ss)->ss_scan_start);
		}
		return 1;
	} else {
		IEEE80211_DPRINTF(vap, IEEE80211_MSG_SCAN,
		    "%s: %s scan already in progress\n", __func__,
		    ss->ss_flags & IEEE80211_SCAN_ACTIVE ? "active" : "passive");
	}
	return 0;
}


/*
 * Start a scan unless one is already going.
 *
 * Called without the comlock held; grab the comlock as appropriate.
 */
static int
ieee80211_swscan_start_scan(const struct ieee80211_scanner *scan,
    struct ieee80211vap *vap, int flags,
    u_int duration, u_int mindwell, u_int maxdwell,
    u_int nssid, const struct ieee80211_scan_ssid ssids[])
{
	struct ieee80211com *ic = vap->iv_ic;
	int result;

	IEEE80211_UNLOCK_ASSERT(ic);

	IEEE80211_LOCK(ic);
	result = ieee80211_swscan_start_scan_locked(scan, vap, flags, duration,
	    mindwell, maxdwell, nssid, ssids);
	IEEE80211_UNLOCK(ic);

	return result;
}

/*
 * Check the scan cache for an ap/channel to use; if that
 * fails then kick off a new scan.
 *
 * Called with the comlock held.
 *
 * XXX TODO: split out!
 */
static int
ieee80211_swscan_check_scan(const struct ieee80211_scanner *scan,
    struct ieee80211vap *vap, int flags,
    u_int duration, u_int mindwell, u_int maxdwell,
    u_int nssid, const struct ieee80211_scan_ssid ssids[])
{
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_scan_state *ss = ic->ic_scan;
	int result;

	IEEE80211_LOCK_ASSERT(ic);

	if (ss->ss_ops != NULL) {
		/* XXX verify ss_ops matches vap->iv_opmode */
		if ((flags & IEEE80211_SCAN_NOSSID) == 0) {
			/*
			 * Update the ssid list and mark flags so if
			 * we call start_scan it doesn't duplicate work.
			 */
			ieee80211_scan_copy_ssid(vap, ss, nssid, ssids);
			flags |= IEEE80211_SCAN_NOSSID;
		}
		if ((ic->ic_flags & IEEE80211_F_SCAN) == 0 &&
		    (flags & IEEE80211_SCAN_FLUSH) == 0 &&
		    ieee80211_time_before(ticks, ic->ic_lastscan + vap->iv_scanvalid)) {
			/*
			 * We're not currently scanning and the cache is
			 * deemed hot enough to consult.  Lock out others
			 * by marking IEEE80211_F_SCAN while we decide if
			 * something is already in the scan cache we can
			 * use.  Also discard any frames that might come
			 * in while temporarily marked as scanning.
			 */
			SCAN_PRIVATE(ss)->ss_iflags |= ISCAN_DISCARD;
			ic->ic_flags |= IEEE80211_F_SCAN;

			/* NB: need to use supplied flags in check */
			ss->ss_flags = flags & 0xff;
			result = ss->ss_ops->scan_end(ss, vap);

			ic->ic_flags &= ~IEEE80211_F_SCAN;
			SCAN_PRIVATE(ss)->ss_iflags &= ~ISCAN_DISCARD;
			if (result) {
				ieee80211_notify_scan_done(vap);
				return 1;
			}
		}
	}
	result = ieee80211_swscan_start_scan_locked(scan, vap, flags, duration,
	    mindwell, maxdwell, nssid, ssids);

	return result;
}

/*
 * Restart a previous scan.  If the previous scan completed
 * then we start again using the existing channel list.
 */
static int
ieee80211_swscan_bg_scan(const struct ieee80211_scanner *scan,
    struct ieee80211vap *vap, int flags)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_scan_state *ss = ic->ic_scan;

	/* XXX assert unlocked? */
	// IEEE80211_UNLOCK_ASSERT(ic);

	IEEE80211_LOCK(ic);
	if ((ic->ic_flags & IEEE80211_F_SCAN) == 0) {
		u_int duration;
		/*
		 * Go off-channel for a fixed interval that is large
		 * enough to catch most ap's but short enough that
		 * we can return on-channel before our listen interval
		 * expires.
		 */
		duration = IEEE80211_SCAN_OFFCHANNEL;

		IEEE80211_DPRINTF(vap, IEEE80211_MSG_SCAN,
		    "%s: %s scan, ticks %u duration %u\n", __func__,
		    ss->ss_flags & IEEE80211_SCAN_ACTIVE ? "active" : "passive",
		    ticks, duration);

		ieee80211_scan_update_locked(vap, scan);
		if (ss->ss_ops != NULL) {
			ss->ss_vap = vap;
			/*
			 * A background scan does not select a new sta; it
			 * just refreshes the scan cache.  Also, indicate
			 * the scan logic should follow the beacon schedule:
			 * we go off-channel and scan for a while, then
			 * return to the bss channel to receive a beacon,
			 * then go off-channel again.  All during this time
			 * we notify the ap we're in power save mode.  When
			 * the scan is complete we leave power save mode.
			 * If any beacon indicates there are frames pending
			 * for us then we drop out of power save mode
			 * (and background scan) automatically by way of the
			 * usual sta power save logic.
			 */
			ss->ss_flags |= IEEE80211_SCAN_NOPICK
				     |  IEEE80211_SCAN_BGSCAN
				     |  flags
				     ;
			/* if previous scan completed, restart */
			if (ss->ss_next >= ss->ss_last) {
				if (ss->ss_flags & IEEE80211_SCAN_ACTIVE)
					vap->iv_stats.is_scan_active++;
				else
					vap->iv_stats.is_scan_passive++;
				/*
				 * NB: beware of the scan cache being flushed;
				 *     if the channel list is empty use the
				 *     scan_start method to populate it.
				 */
				ss->ss_next = 0;
				if (ss->ss_last != 0)
					ss->ss_ops->scan_restart(ss, vap);
				else {
					ss->ss_ops->scan_start(ss, vap);
#ifdef IEEE80211_DEBUG
					if (ieee80211_msg_scan(vap))
						ieee80211_scan_dump(ss);
#endif /* IEEE80211_DEBUG */
				}
			}
			ieee80211_swscan_set_scan_duration(vap, duration);
			ss->ss_maxdwell = duration;
			ic->ic_flags |= IEEE80211_F_SCAN;
			ic->ic_flags_ext |= IEEE80211_FEXT_BGSCAN;
			ieee80211_runtask(ic,
			    &SCAN_PRIVATE(ss)->ss_scan_start);
		} else {
			/* XXX msg+stat */
		}
	} else {
		IEEE80211_DPRINTF(vap, IEEE80211_MSG_SCAN,
		    "%s: %s scan already in progress\n", __func__,
		    ss->ss_flags & IEEE80211_SCAN_ACTIVE ? "active" : "passive");
	}
	IEEE80211_UNLOCK(ic);

	/* NB: racey, does it matter? */
	return (ic->ic_flags & IEEE80211_F_SCAN);
}

/*
 * Taskqueue work to cancel a scan.
 *
 * Note: for offload scan devices, we may want to call into the
 * driver to try and cancel scanning, however it may not be cancelable.
 */
static void
cancel_scan(struct ieee80211vap *vap, int any, const char *func)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_scan_state *ss = ic->ic_scan;
	struct scan_state *ss_priv = SCAN_PRIVATE(ss);
	int signal;

	IEEE80211_LOCK(ic);
	signal = any ? ISCAN_PAUSE : ISCAN_CANCEL;
	if ((ic->ic_flags & IEEE80211_F_SCAN) &&
	    (any || ss->ss_vap == vap) &&
	    (ss_priv->ss_iflags & signal) == 0) {
		IEEE80211_DPRINTF(vap, IEEE80211_MSG_SCAN,
		    "%s: %s %s scan\n", func,
		    any ? "pause" : "cancel",
		    ss->ss_flags & IEEE80211_SCAN_ACTIVE ?
			"active" : "passive");

		/* clear bg scan NOPICK */
		ss->ss_flags &= ~IEEE80211_SCAN_NOPICK;
		/* mark request and wake up the scan task */
		scan_signal_locked(ss, signal);
	} else {
		IEEE80211_DPRINTF(vap, IEEE80211_MSG_SCAN,
		    "%s: called; F_SCAN=%d, vap=%s, signal=%d\n",
			func,
			!! (ic->ic_flags & IEEE80211_F_SCAN),
			(ss->ss_vap == vap ? "match" : "nomatch"),
			!! (ss_priv->ss_iflags & signal));
	}
	IEEE80211_UNLOCK(ic);
}

/*
 * Cancel any scan currently going on for the specified vap.
 */
static void
ieee80211_swscan_cancel_scan(struct ieee80211vap *vap)
{
	cancel_scan(vap, 0, __func__);
}

/*
 * Cancel any scan currently going on.
 */
static void
ieee80211_swscan_cancel_anyscan(struct ieee80211vap *vap)
{

	/* XXX for now - just don't do this per packet. */
	if (vap->iv_flags_ext & IEEE80211_FEXT_SCAN_OFFLOAD)
		return;

	cancel_scan(vap, 1, __func__);
}

/*
 * Manually switch to the next channel in the channel list.
 * Provided for drivers that manage scanning themselves
 * (e.g. for firmware-based devices).
 */
static void
ieee80211_swscan_scan_next(struct ieee80211vap *vap)
{
	struct ieee80211_scan_state *ss = vap->iv_ic->ic_scan;

	IEEE80211_DPRINTF(vap, IEEE80211_MSG_SCAN, "%s: called\n", __func__);

	/* wake up the scan task */
	scan_signal(ss, 0);
}

/*
 * Manually stop a scan that is currently running.
 * Provided for drivers that are not able to scan single channels
 * (e.g. for firmware-based devices).
 */
static void
ieee80211_swscan_scan_done(struct ieee80211vap *vap)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_scan_state *ss = ic->ic_scan;

	IEEE80211_LOCK_ASSERT(ic);

	scan_signal_locked(ss, 0);
}

/*
 * Probe the current channel, if allowed, while scanning.
 * If the channel is not marked passive-only then send
 * a probe request immediately.  Otherwise mark state and
 * listen for beacons on the channel; if we receive something
 * then we'll transmit a probe request.
 */
static void
ieee80211_swscan_probe_curchan(struct ieee80211vap *vap, int force)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_scan_state *ss = ic->ic_scan;
	struct ifnet *ifp = vap->iv_ifp;
	int i;

	/*
	 * Full-offload scan devices don't require this.
	 */
	if (vap->iv_flags_ext & IEEE80211_FEXT_SCAN_OFFLOAD)
		return;

	/*
	 * Send directed probe requests followed by any
	 * broadcast probe request.
	 * XXX remove dependence on ic/vap->iv_bss
	 */
	for (i = 0; i < ss->ss_nssid; i++)
		ieee80211_send_probereq(vap->iv_bss,
			vap->iv_myaddr, ifp->if_broadcastaddr,
			ifp->if_broadcastaddr,
			ss->ss_ssid[i].ssid, ss->ss_ssid[i].len);
	if ((ss->ss_flags & IEEE80211_SCAN_NOBCAST) == 0)
		ieee80211_send_probereq(vap->iv_bss,
			vap->iv_myaddr, ifp->if_broadcastaddr,
			ifp->if_broadcastaddr,
			"", 0);
}

/*
 * Scan curchan.  If this is an active scan and the channel
 * is not marked passive then send probe request frame(s).
 * Arrange for the channel change after maxdwell ticks.
 */
static void
scan_curchan(struct ieee80211_scan_state *ss, unsigned long maxdwell)
{
	struct ieee80211vap *vap  = ss->ss_vap;
	struct ieee80211com *ic = ss->ss_ic;

	IEEE80211_DPRINTF(vap, IEEE80211_MSG_SCAN,
	    "%s: calling; maxdwell=%lu\n",
	    __func__,
	    maxdwell);
	IEEE80211_LOCK(ic);
	if (ss->ss_flags & IEEE80211_SCAN_ACTIVE)
		ieee80211_probe_curchan(vap, 0);
	taskqueue_enqueue_timeout(ic->ic_tq,
	    &SCAN_PRIVATE(ss)->ss_scan_curchan, maxdwell);
	IEEE80211_UNLOCK(ic);
}

static void
scan_signal(struct ieee80211_scan_state *ss, int iflags)
{
	struct ieee80211com *ic = ss->ss_ic;

	IEEE80211_UNLOCK_ASSERT(ic);

	IEEE80211_LOCK(ic);
	scan_signal_locked(ss, iflags);
	IEEE80211_UNLOCK(ic);
}

static void
scan_signal_locked(struct ieee80211_scan_state *ss, int iflags)
{
	struct scan_state *ss_priv = SCAN_PRIVATE(ss);
	struct timeout_task *scan_task = &ss_priv->ss_scan_curchan;
	struct ieee80211com *ic = ss->ss_ic;

	IEEE80211_LOCK_ASSERT(ic);

	ss_priv->ss_iflags |= iflags;
	if (ss_priv->ss_iflags & ISCAN_RUNNING) {
		if (taskqueue_cancel_timeout(ic->ic_tq, scan_task, NULL) == 0)
			taskqueue_enqueue_timeout(ic->ic_tq, scan_task, 0);
	}
}

/*
 * Handle mindwell requirements completed; initiate a channel
 * change to the next channel asap.
 */
static void
scan_mindwell(struct ieee80211_scan_state *ss)
{

	IEEE80211_DPRINTF(ss->ss_vap, IEEE80211_MSG_SCAN, "%s: called\n",
	    __func__);

	scan_signal(ss, 0);
}

static void
scan_start(void *arg, int pending)
{
#define	ISCAN_REP	(ISCAN_MINDWELL | ISCAN_DISCARD)
	struct ieee80211_scan_state *ss = (struct ieee80211_scan_state *) arg;
	struct scan_state *ss_priv = SCAN_PRIVATE(ss);
	struct ieee80211vap *vap = ss->ss_vap;
	struct ieee80211com *ic = ss->ss_ic;

	IEEE80211_LOCK(ic);
	if (vap == NULL || (ic->ic_flags & IEEE80211_F_SCAN) == 0 ||
	    (ss_priv->ss_iflags & ISCAN_ABORT)) {
		/* Cancelled before we started */
		scan_done(ss, 0);
		return;
	}

	if (ss->ss_next == ss->ss_last) {
		IEEE80211_DPRINTF(vap, IEEE80211_MSG_SCAN,
			"%s: no channels to scan\n", __func__);
		scan_done(ss, 1);
		return;
	}

	/*
	 * Put the station into power save mode.
	 *
	 * This is only required if we're not a full-offload devices;
	 * those devices manage scan/traffic differently.
	 */
	if (((vap->iv_flags_ext & IEEE80211_FEXT_SCAN_OFFLOAD) == 0) &&
	    vap->iv_opmode == IEEE80211_M_STA &&
	    vap->iv_state == IEEE80211_S_RUN) {
		if ((vap->iv_bss->ni_flags & IEEE80211_NODE_PWR_MGT) == 0) {
			/* Enable station power save mode */
			vap->iv_sta_ps(vap, 1);
			/* Wait until null data frame will be ACK'ed */
			mtx_sleep(vap, IEEE80211_LOCK_OBJ(ic), PCATCH,
			    "sta_ps", msecs_to_ticks(10));
			if (ss_priv->ss_iflags & ISCAN_ABORT) {
				scan_done(ss, 0);
				return;
			}
		}
	}

	ss_priv->ss_scanend = ticks + ss_priv->ss_duration;

	/* XXX scan state can change! Re-validate scan state! */

	IEEE80211_UNLOCK(ic);

	ic->ic_scan_start(ic);		/* notify driver */

	scan_curchan_task(ss, 0);
}

static void
scan_curchan_task(void *arg, int pending)
{
	struct ieee80211_scan_state *ss = arg;
	struct scan_state *ss_priv = SCAN_PRIVATE(ss);
	struct ieee80211com *ic = ss->ss_ic;
	struct ieee80211_channel *chan;
	unsigned long maxdwell;
	int scandone;

	IEEE80211_LOCK(ic);
end:
	scandone = (ss->ss_next >= ss->ss_last) ||
	    (ss_priv->ss_iflags & ISCAN_CANCEL) != 0;

	IEEE80211_DPRINTF(ss->ss_vap, IEEE80211_MSG_SCAN,
	    "%s: loop start; scandone=%d\n",
	    __func__,
	    scandone);

	if (scandone || (ss->ss_flags & IEEE80211_SCAN_GOTPICK) ||
	    (ss_priv->ss_iflags & ISCAN_ABORT) ||
	     ieee80211_time_after(ticks + ss->ss_mindwell, ss_priv->ss_scanend)) {
		ss_priv->ss_iflags &= ~ISCAN_RUNNING;
		scan_end(ss, scandone);
		return;
	} else
		ss_priv->ss_iflags |= ISCAN_RUNNING;

	chan = ss->ss_chans[ss->ss_next++];

	/*
	 * Watch for truncation due to the scan end time.
	 */
	if (ieee80211_time_after(ticks + ss->ss_maxdwell, ss_priv->ss_scanend))
		maxdwell = ss_priv->ss_scanend - ticks;
	else
		maxdwell = ss->ss_maxdwell;

	IEEE80211_DPRINTF(ss->ss_vap, IEEE80211_MSG_SCAN,
	    "%s: chan %3d%c -> %3d%c [%s, dwell min %lums max %lums]\n",
	    __func__,
	    ieee80211_chan2ieee(ic, ic->ic_curchan),
	    ieee80211_channel_type_char(ic->ic_curchan),
	    ieee80211_chan2ieee(ic, chan),
	    ieee80211_channel_type_char(chan),
	    (ss->ss_flags & IEEE80211_SCAN_ACTIVE) &&
		(chan->ic_flags & IEEE80211_CHAN_PASSIVE) == 0 ?
		"active" : "passive",
	    ticks_to_msecs(ss->ss_mindwell), ticks_to_msecs(maxdwell));

	/*
	 * Potentially change channel and phy mode.
	 */
	ic->ic_curchan = chan;
	ic->ic_rt = ieee80211_get_ratetable(chan);
	IEEE80211_UNLOCK(ic);
	/*
	 * Perform the channel change and scan unlocked so the driver
	 * may sleep. Once set_channel returns the hardware has
	 * completed the channel change.
	 */
	ic->ic_set_channel(ic);
	ieee80211_radiotap_chan_change(ic);

	/*
	 * Scan curchan.  Drivers for "intelligent hardware"
	 * override ic_scan_curchan to tell the device to do
	 * the work.  Otherwise we manage the work ourselves;
	 * sending a probe request (as needed), and arming the
	 * timeout to switch channels after maxdwell ticks.
	 *
	 * scan_curchan should only pause for the time required to
	 * prepare/initiate the hardware for the scan (if at all).
	 */
	ic->ic_scan_curchan(ss, maxdwell);
	IEEE80211_LOCK(ic);

	/* XXX scan state can change! Re-validate scan state! */

	ss_priv->ss_chanmindwell = ticks + ss->ss_mindwell;
	/* clear mindwell lock and initial channel change flush */
	ss_priv->ss_iflags &= ~ISCAN_REP;

	if (ss_priv->ss_iflags & (ISCAN_CANCEL|ISCAN_ABORT)) {
		taskqueue_cancel_timeout(ic->ic_tq, &ss_priv->ss_scan_curchan,
		    NULL);
		goto end;
	}

	IEEE80211_DPRINTF(ss->ss_vap, IEEE80211_MSG_SCAN, "%s: waiting\n",
	    __func__);
	IEEE80211_UNLOCK(ic);
}

static void
scan_end(struct ieee80211_scan_state *ss, int scandone)
{
	struct scan_state *ss_priv = SCAN_PRIVATE(ss);
	struct ieee80211vap *vap = ss->ss_vap;
	struct ieee80211com *ic = ss->ss_ic;

	IEEE80211_LOCK_ASSERT(ic);

	IEEE80211_DPRINTF(vap, IEEE80211_MSG_SCAN, "%s: out\n", __func__);

	if (ss_priv->ss_iflags & ISCAN_ABORT) {
		scan_done(ss, scandone);
		return;
	}

	IEEE80211_UNLOCK(ic);
	ic->ic_scan_end(ic);		/* notify driver */
	IEEE80211_LOCK(ic);
	/* XXX scan state can change! Re-validate scan state! */

	/*
	 * Since a cancellation may have occurred during one of the
	 * driver calls (whilst unlocked), update scandone.
	 */
	if (scandone == 0 && (ss_priv->ss_iflags & ISCAN_CANCEL) != 0) {
		/* XXX printf? */
		if_printf(vap->iv_ifp,
		    "%s: OOPS! scan cancelled during driver call (1)!\n",
		    __func__);
		scandone = 1;
	}

	/*
	 * Record scan complete time.  Note that we also do
	 * this when canceled so any background scan will
	 * not be restarted for a while.
	 */
	if (scandone)
		ic->ic_lastscan = ticks;
	/* return to the bss channel */
	if (ic->ic_bsschan != IEEE80211_CHAN_ANYC &&
	    ic->ic_curchan != ic->ic_bsschan) {
		ieee80211_setupcurchan(ic, ic->ic_bsschan);
		IEEE80211_UNLOCK(ic);
		ic->ic_set_channel(ic);
		ieee80211_radiotap_chan_change(ic);
		IEEE80211_LOCK(ic);
	}
	/* clear internal flags and any indication of a pick */
	ss_priv->ss_iflags &= ~ISCAN_REP;
	ss->ss_flags &= ~IEEE80211_SCAN_GOTPICK;

	/*
	 * If not canceled and scan completed, do post-processing.
	 * If the callback function returns 0, then it wants to
	 * continue/restart scanning.  Unfortunately we needed to
	 * notify the driver to end the scan above to avoid having
	 * rx frames alter the scan candidate list.
	 */
	if ((ss_priv->ss_iflags & ISCAN_CANCEL) == 0 &&
	    !ss->ss_ops->scan_end(ss, vap) &&
	    (ss->ss_flags & IEEE80211_SCAN_ONCE) == 0 &&
	    ieee80211_time_before(ticks + ss->ss_mindwell, ss_priv->ss_scanend)) {
		IEEE80211_DPRINTF(vap, IEEE80211_MSG_SCAN,
		    "%s: done, restart "
		    "[ticks %u, dwell min %lu scanend %lu]\n",
		    __func__,
		    ticks, ss->ss_mindwell, ss_priv->ss_scanend);
		ss->ss_next = 0;	/* reset to beginning */
		if (ss->ss_flags & IEEE80211_SCAN_ACTIVE)
			vap->iv_stats.is_scan_active++;
		else
			vap->iv_stats.is_scan_passive++;

		ss->ss_ops->scan_restart(ss, vap);	/* XXX? */
		ieee80211_runtask(ic, &ss_priv->ss_scan_start);
		IEEE80211_UNLOCK(ic);
		return;
	}

	/* past here, scandone is ``true'' if not in bg mode */
	if ((ss->ss_flags & IEEE80211_SCAN_BGSCAN) == 0)
		scandone = 1;

	IEEE80211_DPRINTF(vap, IEEE80211_MSG_SCAN,
	    "%s: %s, [ticks %u, dwell min %lu scanend %lu]\n",
	    __func__, scandone ? "done" : "stopped",
	    ticks, ss->ss_mindwell, ss_priv->ss_scanend);

	/*
	 * Since a cancellation may have occurred during one of the
	 * driver calls (whilst unlocked), update scandone.
	 */
	if (scandone == 0 && (ss_priv->ss_iflags & ISCAN_CANCEL) != 0) {
		/* XXX printf? */
		if_printf(vap->iv_ifp,
		    "%s: OOPS! scan cancelled during driver call (2)!\n",
		    __func__);
		scandone = 1;
	}

	scan_done(ss, scandone);
}

static void
scan_done(struct ieee80211_scan_state *ss, int scandone)
{
	struct scan_state *ss_priv = SCAN_PRIVATE(ss);
	struct ieee80211com *ic = ss->ss_ic;
	struct ieee80211vap *vap = ss->ss_vap;

	IEEE80211_LOCK_ASSERT(ic);

	/*
	 * Clear the SCAN bit first in case frames are
	 * pending on the station power save queue.  If
	 * we defer this then the dispatch of the frames
	 * may generate a request to cancel scanning.
	 */
	ic->ic_flags &= ~IEEE80211_F_SCAN;

	/*
	 * Drop out of power save mode when a scan has
	 * completed.  If this scan was prematurely terminated
	 * because it is a background scan then don't notify
	 * the ap; we'll either return to scanning after we
	 * receive the beacon frame or we'll drop out of power
	 * save mode because the beacon indicates we have frames
	 * waiting for us.
	 */
	if (scandone) {
		/*
		 * If we're not a scan offload device, come back out of
		 * station powersave.  Offload devices handle this themselves.
		 */
		if ((vap->iv_flags_ext & IEEE80211_FEXT_SCAN_OFFLOAD) == 0)
			vap->iv_sta_ps(vap, 0);
		if (ss->ss_next >= ss->ss_last)
			ic->ic_flags_ext &= ~IEEE80211_FEXT_BGSCAN;

		/* send 'scan done' event if not interrupted due to traffic. */
		if (!(ss_priv->ss_iflags & ISCAN_INTERRUPT))
			ieee80211_notify_scan_done(vap);
	}
	ss_priv->ss_iflags &= ~(ISCAN_PAUSE | ISCAN_ABORT);
	ss_priv->ss_scanend = 0;
	ss->ss_flags &= ~(IEEE80211_SCAN_ONCE | IEEE80211_SCAN_PICK1ST);
	IEEE80211_UNLOCK(ic);
#undef ISCAN_REP
}

/*
 * Process a beacon or probe response frame.
 */
static void
ieee80211_swscan_add_scan(struct ieee80211vap *vap,
	struct ieee80211_channel *curchan,
	const struct ieee80211_scanparams *sp,
	const struct ieee80211_frame *wh,
	int subtype, int rssi, int noise)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_scan_state *ss = ic->ic_scan;

	/* XXX locking */
	/*
	 * Frames received during startup are discarded to avoid
	 * using scan state setup on the initial entry to the timer
	 * callback.  This can occur because the device may enable
	 * rx prior to our doing the initial channel change in the
	 * timer routine.
	 */
	if (SCAN_PRIVATE(ss)->ss_iflags & ISCAN_DISCARD)
		return;
#ifdef IEEE80211_DEBUG
	if (ieee80211_msg_scan(vap) && (ic->ic_flags & IEEE80211_F_SCAN))
		ieee80211_scan_dump_probe_beacon(subtype, 1, wh->i_addr2, sp, rssi);
#endif
	if (ss->ss_ops != NULL &&
	    ss->ss_ops->scan_add(ss, curchan, sp, wh, subtype, rssi, noise)) {
		/*
		 * If we've reached the min dwell time terminate
		 * the timer so we'll switch to the next channel.
		 */
		if ((SCAN_PRIVATE(ss)->ss_iflags & ISCAN_MINDWELL) == 0 &&
		    ieee80211_time_after_eq(ticks, SCAN_PRIVATE(ss)->ss_chanmindwell)) {
			IEEE80211_DPRINTF(vap, IEEE80211_MSG_SCAN,
			    "%s: chan %3d%c min dwell met (%u > %lu)\n",
			    __func__,
			    ieee80211_chan2ieee(ic, ic->ic_curchan),
			    ieee80211_channel_type_char(ic->ic_curchan),
			    ticks, SCAN_PRIVATE(ss)->ss_chanmindwell);
			SCAN_PRIVATE(ss)->ss_iflags |= ISCAN_MINDWELL;
			/*
			 * NB: trigger at next clock tick or wait for the
			 * hardware.
			 */
			ic->ic_scan_mindwell(ss);
		}
	}
}

static struct ieee80211_scan_methods swscan_methods = {
	.sc_attach = ieee80211_swscan_attach,
	.sc_detach = ieee80211_swscan_detach,
	.sc_vattach = ieee80211_swscan_vattach,
	.sc_vdetach = ieee80211_swscan_vdetach,
	.sc_set_scan_duration = ieee80211_swscan_set_scan_duration,
	.sc_start_scan = ieee80211_swscan_start_scan,
	.sc_check_scan = ieee80211_swscan_check_scan,
	.sc_bg_scan = ieee80211_swscan_bg_scan,
	.sc_cancel_scan = ieee80211_swscan_cancel_scan,
	.sc_cancel_anyscan = ieee80211_swscan_cancel_anyscan,
	.sc_scan_next = ieee80211_swscan_scan_next,
	.sc_scan_done = ieee80211_swscan_scan_done,
	.sc_scan_probe_curchan = ieee80211_swscan_probe_curchan,
	.sc_add_scan = ieee80211_swscan_add_scan
};

/*
 * Default scan attach method.
 */
void
ieee80211_swscan_attach(struct ieee80211com *ic)
{
	struct scan_state *ss;

	/*
	 * Setup the default methods
	 */
	ic->ic_scan_methods = &swscan_methods;

	/* Allocate initial scan state */
	ss = (struct scan_state *) IEEE80211_MALLOC(sizeof(struct scan_state),
		M_80211_SCAN, IEEE80211_M_NOWAIT | IEEE80211_M_ZERO);
	if (ss == NULL) {
		ic->ic_scan = NULL;
		return;
	}
	TASK_INIT(&ss->ss_scan_start, 0, scan_start, ss);
	TIMEOUT_TASK_INIT(ic->ic_tq, &ss->ss_scan_curchan, 0,
	    scan_curchan_task, ss);

	ic->ic_scan = &ss->base;
	ss->base.ss_ic = ic;

	ic->ic_scan_curchan = scan_curchan;
	ic->ic_scan_mindwell = scan_mindwell;
}
