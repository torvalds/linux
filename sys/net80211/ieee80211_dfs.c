/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007-2008 Sam Leffler, Errno Consulting
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
 * IEEE 802.11 DFS/Radar support.
 */
#include "opt_inet.h"
#include "opt_wlan.h"

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
#include <net/if_var.h>
#include <net/if_media.h>
#include <net/ethernet.h>

#include <net80211/ieee80211_var.h>

static MALLOC_DEFINE(M_80211_DFS, "80211dfs", "802.11 DFS state");

static	int ieee80211_nol_timeout = 30*60;		/* 30 minutes */
SYSCTL_INT(_net_wlan, OID_AUTO, nol_timeout, CTLFLAG_RW,
	&ieee80211_nol_timeout, 0, "NOL timeout (secs)");
#define	NOL_TIMEOUT	msecs_to_ticks(ieee80211_nol_timeout*1000)

static	int ieee80211_cac_timeout = 60;		/* 60 seconds */
SYSCTL_INT(_net_wlan, OID_AUTO, cac_timeout, CTLFLAG_RW,
	&ieee80211_cac_timeout, 0, "CAC timeout (secs)");
#define	CAC_TIMEOUT	msecs_to_ticks(ieee80211_cac_timeout*1000)

/*
 DFS* In order to facilitate  debugging, a couple of operating
 * modes aside from the default are needed.
 *
 * 0 - default CAC/NOL behaviour - ie, start CAC, place
 *     channel on NOL list.
 * 1 - send CAC, but don't change channel or add the channel
 *     to the NOL list.
 * 2 - just match on radar, don't send CAC or place channel in
 *     the NOL list.
 */
static	int ieee80211_dfs_debug = DFS_DBG_NONE;

/*
 * This option must not be included in the default kernel
 * as it allows users to plainly disable CAC/NOL handling.
 */
#ifdef	IEEE80211_DFS_DEBUG
SYSCTL_INT(_net_wlan, OID_AUTO, dfs_debug, CTLFLAG_RW,
	&ieee80211_dfs_debug, 0, "DFS debug behaviour");
#endif

static int
null_set_quiet(struct ieee80211_node *ni, u_int8_t *quiet_elm)
{
	return ENOSYS;
}

void
ieee80211_dfs_attach(struct ieee80211com *ic)
{
	struct ieee80211_dfs_state *dfs = &ic->ic_dfs;

	callout_init_mtx(&dfs->nol_timer, IEEE80211_LOCK_OBJ(ic), 0);
	callout_init_mtx(&dfs->cac_timer, IEEE80211_LOCK_OBJ(ic), 0);

	ic->ic_set_quiet = null_set_quiet;
}

void
ieee80211_dfs_detach(struct ieee80211com *ic)
{
	/* NB: we assume no locking is needed */
	ieee80211_dfs_reset(ic);
}

void
ieee80211_dfs_reset(struct ieee80211com *ic)
{
	struct ieee80211_dfs_state *dfs = &ic->ic_dfs;
	int i;

	/* NB: we assume no locking is needed */
	/* NB: cac_timer should be cleared by the state machine */
	callout_drain(&dfs->nol_timer);
	for (i = 0; i < ic->ic_nchans; i++)
		ic->ic_channels[i].ic_state = 0;
	dfs->lastchan = NULL;
}

static void
cac_timeout(void *arg)
{
	struct ieee80211vap *vap = arg;
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_dfs_state *dfs = &ic->ic_dfs;
	int i;

	IEEE80211_LOCK_ASSERT(ic);

	if (vap->iv_state != IEEE80211_S_CAC)	/* NB: just in case */
		return;
	/*
	 * When radar is detected during a CAC we are woken
	 * up prematurely to switch to a new channel.
	 * Check the channel to decide how to act.
	 */
	if (IEEE80211_IS_CHAN_RADAR(ic->ic_curchan)) {
		ieee80211_notify_cac(ic, ic->ic_curchan,
		    IEEE80211_NOTIFY_CAC_RADAR);

		if_printf(vap->iv_ifp,
		    "CAC timer on channel %u (%u MHz) stopped due to radar\n",
		    ic->ic_curchan->ic_ieee, ic->ic_curchan->ic_freq);

		/* XXX clobbers any existing desired channel */
		/* NB: dfs->newchan may be NULL, that's ok */
		vap->iv_des_chan = dfs->newchan;
		ieee80211_new_state_locked(vap, IEEE80211_S_SCAN, 0);
	} else {
		if_printf(vap->iv_ifp,
		    "CAC timer on channel %u (%u MHz) expired; "
		    "no radar detected\n",
		    ic->ic_curchan->ic_ieee, ic->ic_curchan->ic_freq);
		/*
		 * Mark all channels with the current frequency
		 * as having completed CAC; this keeps us from
		 * doing it again until we change channels.
		 */
		for (i = 0; i < ic->ic_nchans; i++) {
			struct ieee80211_channel *c = &ic->ic_channels[i];
			if (c->ic_freq == ic->ic_curchan->ic_freq)
				c->ic_state |= IEEE80211_CHANSTATE_CACDONE;
		}
		ieee80211_notify_cac(ic, ic->ic_curchan,
		    IEEE80211_NOTIFY_CAC_EXPIRE);
		ieee80211_cac_completeswitch(vap);
	}
}

/*
 * Initiate the CAC timer.  The driver is responsible
 * for setting up the hardware to scan for radar on the
 * channnel, we just handle timing things out.
 */
void
ieee80211_dfs_cac_start(struct ieee80211vap *vap)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_dfs_state *dfs = &ic->ic_dfs;

	IEEE80211_LOCK_ASSERT(ic);

	callout_reset(&dfs->cac_timer, CAC_TIMEOUT, cac_timeout, vap);
	if_printf(vap->iv_ifp, "start %d second CAC timer on channel %u (%u MHz)\n",
	    ticks_to_secs(CAC_TIMEOUT),
	    ic->ic_curchan->ic_ieee, ic->ic_curchan->ic_freq);
	ieee80211_notify_cac(ic, ic->ic_curchan, IEEE80211_NOTIFY_CAC_START);
}

/*
 * Clear the CAC timer.
 */
void
ieee80211_dfs_cac_stop(struct ieee80211vap *vap)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_dfs_state *dfs = &ic->ic_dfs;

	IEEE80211_LOCK_ASSERT(ic);

	/* NB: racey but not important */
	if (callout_pending(&dfs->cac_timer)) {
		if_printf(vap->iv_ifp, "stop CAC timer on channel %u (%u MHz)\n",
		    ic->ic_curchan->ic_ieee, ic->ic_curchan->ic_freq);
		ieee80211_notify_cac(ic, ic->ic_curchan,
		    IEEE80211_NOTIFY_CAC_STOP);
	}
	callout_stop(&dfs->cac_timer);
}

void
ieee80211_dfs_cac_clear(struct ieee80211com *ic,
	const struct ieee80211_channel *chan)
{
	int i;

	for (i = 0; i < ic->ic_nchans; i++) {
		struct ieee80211_channel *c = &ic->ic_channels[i];
		if (c->ic_freq == chan->ic_freq)
			c->ic_state &= ~IEEE80211_CHANSTATE_CACDONE;
	}
}

static void
dfs_timeout(void *arg)
{
	struct ieee80211com *ic = arg;
	struct ieee80211_dfs_state *dfs = &ic->ic_dfs;
	struct ieee80211_channel *c;
	int i, oldest, now;

	IEEE80211_LOCK_ASSERT(ic);

	now = oldest = ticks;
	for (i = 0; i < ic->ic_nchans; i++) {
		c = &ic->ic_channels[i];
		if (IEEE80211_IS_CHAN_RADAR(c)) {
			if (ieee80211_time_after_eq(now, dfs->nol_event[i]+NOL_TIMEOUT)) {
				c->ic_state &= ~IEEE80211_CHANSTATE_RADAR;
				if (c->ic_state & IEEE80211_CHANSTATE_NORADAR) {
					/*
					 * NB: do this here so we get only one
					 * msg instead of one for every channel
					 * table entry.
					 */
					ic_printf(ic, "radar on channel %u "
					    "(%u MHz) cleared after timeout\n",
					    c->ic_ieee, c->ic_freq);
					/* notify user space */
					c->ic_state &=
					    ~IEEE80211_CHANSTATE_NORADAR;
					ieee80211_notify_radar(ic, c);
				}
			} else if (dfs->nol_event[i] < oldest)
				oldest = dfs->nol_event[i];
		}
	}
	if (oldest != now) {
		/* arrange to process next channel up for a status change */
		callout_schedule(&dfs->nol_timer, oldest + NOL_TIMEOUT - now);
	}
}

static void
announce_radar(struct ieee80211com *ic, const struct ieee80211_channel *curchan,
	const struct ieee80211_channel *newchan)
{
	if (newchan == NULL)
		ic_printf(ic, "radar detected on channel %u (%u MHz)\n",
		    curchan->ic_ieee, curchan->ic_freq);
	else
		ic_printf(ic, "radar detected on channel %u (%u MHz), "
		    "moving to channel %u (%u MHz)\n",
		    curchan->ic_ieee, curchan->ic_freq,
		    newchan->ic_ieee, newchan->ic_freq);
}

/*
 * Handle a radar detection event on a channel. The channel is
 * added to the NOL list and we record the time of the event.
 * Entries are aged out after NOL_TIMEOUT.  If radar was
 * detected while doing CAC we force a state/channel change.
 * Otherwise radar triggers a channel switch using the CSA
 * mechanism (when the channel is the bss channel).
 */
void
ieee80211_dfs_notify_radar(struct ieee80211com *ic, struct ieee80211_channel *chan)
{
	struct ieee80211_dfs_state *dfs = &ic->ic_dfs;
	int i, now;

	IEEE80211_LOCK_ASSERT(ic);

	/*
	 * If doing DFS debugging (mode 2), don't bother
	 * running the rest of this function.
	 *
	 * Simply announce the presence of the radar and continue
	 * along merrily.
	 */
	if (ieee80211_dfs_debug == DFS_DBG_NOCSANOL) {
		announce_radar(ic, chan, chan);
		ieee80211_notify_radar(ic, chan);
		return;
	}

	/*
	 * Don't mark the channel and don't put it into NOL
	 * if we're doing DFS debugging.
	 */
	if (ieee80211_dfs_debug == DFS_DBG_NONE) {
		/*
		 * Mark all entries with this frequency.  Notify user
		 * space and arrange for notification when the radar
		 * indication is cleared.  Then kick the NOL processing
		 * thread if not already running.
		 */
		now = ticks;
		for (i = 0; i < ic->ic_nchans; i++) {
			struct ieee80211_channel *c = &ic->ic_channels[i];
			if (c->ic_freq == chan->ic_freq) {
				c->ic_state &= ~IEEE80211_CHANSTATE_CACDONE;
				c->ic_state |= IEEE80211_CHANSTATE_RADAR;
				dfs->nol_event[i] = now;
			}
		}
		ieee80211_notify_radar(ic, chan);
		chan->ic_state |= IEEE80211_CHANSTATE_NORADAR;
		if (!callout_pending(&dfs->nol_timer))
			callout_reset(&dfs->nol_timer, NOL_TIMEOUT,
			    dfs_timeout, ic);
	}

	/*
	 * If radar is detected on the bss channel while
	 * doing CAC; force a state change by scheduling the
	 * callout to be dispatched asap.  Otherwise, if this
	 * event is for the bss channel then we must quiet
	 * traffic and schedule a channel switch.
	 *
	 * Note this allows us to receive notification about
	 * channels other than the bss channel; not sure
	 * that can/will happen but it's simple to support.
	 */
	if (chan == ic->ic_bsschan) {
		/* XXX need a way to defer to user app */

		/*
		 * Don't flip over to a new channel if
		 * we are currently doing DFS debugging.
		 */
		if (ieee80211_dfs_debug == DFS_DBG_NONE)
			dfs->newchan = ieee80211_dfs_pickchannel(ic);
		else
			dfs->newchan = chan;

		announce_radar(ic, chan, dfs->newchan);

		if (callout_pending(&dfs->cac_timer))
			callout_schedule(&dfs->cac_timer, 0);
		else if (dfs->newchan != NULL) {
			/* XXX mode 1, switch count 2 */
			/* XXX calculate switch count based on max
			  switch time and beacon interval? */
			ieee80211_csa_startswitch(ic, dfs->newchan, 1, 2);
		} else {
			/*
			 * Spec says to stop all transmissions and
			 * wait on the current channel for an entry
			 * on the NOL to expire.
			 */
			/*XXX*/
			ic_printf(ic, "%s: No free channels; waiting for entry "
			    "on NOL to expire\n", __func__);
		}
	} else {
		/*
		 * Issue rate-limited console msgs.
		 */
		if (dfs->lastchan != chan) {
			dfs->lastchan = chan;
			dfs->cureps = 0;
			announce_radar(ic, chan, NULL);
		} else if (ppsratecheck(&dfs->lastevent, &dfs->cureps, 1)) {
			announce_radar(ic, chan, NULL);
		}
	}
}

struct ieee80211_channel *
ieee80211_dfs_pickchannel(struct ieee80211com *ic)
{
	struct ieee80211_channel *c;
	int i, flags;
	uint16_t v;

	/*
	 * Consult the scan cache first.
	 */
	flags = ic->ic_curchan->ic_flags & IEEE80211_CHAN_ALL;
	/*
	 * XXX if curchan is HT this will never find a channel
	 * XXX 'cuz we scan only legacy channels
	 */
	c = ieee80211_scan_pickchannel(ic, flags);
	if (c != NULL)
		return c;
	/*
	 * No channel found in scan cache; select a compatible
	 * one at random (skipping channels where radar has
	 * been detected).
	 */
	get_random_bytes(&v, sizeof(v));
	v %= ic->ic_nchans;
	for (i = v; i < ic->ic_nchans; i++) {
		c = &ic->ic_channels[i];
		if (!IEEE80211_IS_CHAN_RADAR(c) &&
		   (c->ic_flags & flags) == flags)
			return c;
	}
	for (i = 0; i < v; i++) {
		c = &ic->ic_channels[i];
		if (!IEEE80211_IS_CHAN_RADAR(c) &&
		   (c->ic_flags & flags) == flags)
			return c;
	}
	ic_printf(ic, "HELP, no channel located to switch to!\n");
	return NULL;
}
