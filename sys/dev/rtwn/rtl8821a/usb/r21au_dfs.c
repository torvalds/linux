/*-
 * Copyright (c) 2016 Andriy Voskoboinyk <avos@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/taskqueue.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/linker.h>

#include <net/if.h>
#include <net/ethernet.h>
#include <net/if_media.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_radiotap.h>

#include <dev/rtwn/if_rtwnvar.h>
#include <dev/rtwn/if_rtwn_debug.h>

#include <dev/rtwn/usb/rtwn_usb_var.h>

#include <dev/rtwn/rtl8812a/r12a_var.h>

#include <dev/rtwn/rtl8821a/usb/r21au.h>
#include <dev/rtwn/rtl8821a/usb/r21au_reg.h>


#define R21AU_RADAR_CHECK_PERIOD	(2 * hz)

static void
r21au_dfs_radar_disable(struct rtwn_softc *sc)
{
	rtwn_bb_setbits(sc, 0x924, 0x00008000, 0);
}

static int
r21au_dfs_radar_is_enabled(struct rtwn_softc *sc)
{
	return !!(rtwn_bb_read(sc, 0x924) & 0x00008000);
}

static int
r21au_dfs_radar_reset(struct rtwn_softc *sc)
{
	int error;

	error = rtwn_bb_setbits(sc, 0x924, 0x00008000, 0);
	if (error != 0)
		return (error);

	return (rtwn_bb_setbits(sc, 0x924, 0, 0x00008000));
}

static int
r21au_dfs_radar_enable(struct rtwn_softc *sc)
{
#define RTWN_CHK(res) do {	\
	if (res != 0)		\
		return (EIO);	\
} while(0)

	RTWN_ASSERT_LOCKED(sc);

	RTWN_CHK(rtwn_bb_setbits(sc, 0x814, 0x3fffffff, 0x04cc4d10));
	RTWN_CHK(rtwn_bb_setbits(sc, R12A_BW_INDICATION, 0xff, 0x06));
	RTWN_CHK(rtwn_bb_write(sc, 0x918, 0x1c16ecdf));
	RTWN_CHK(rtwn_bb_write(sc, 0x924, 0x0152a400));
	RTWN_CHK(rtwn_bb_write(sc, 0x91c, 0x0fa21a20));
	RTWN_CHK(rtwn_bb_write(sc, 0x920, 0xe0f57204));

	return (r21au_dfs_radar_reset(sc));

#undef RTWN_CHK
}

static int
r21au_dfs_radar_is_detected(struct rtwn_softc *sc)
{
	return !!(rtwn_bb_read(sc, 0xf98) & 0x00020000);
}

void
r21au_chan_check(void *arg, int npending __unused)
{
	struct rtwn_softc *sc = arg;
	struct r12a_softc *rs = sc->sc_priv;
	struct ieee80211com *ic = &sc->sc_ic;

	RTWN_LOCK(sc);
#ifdef DIAGNOSTIC
	RTWN_DPRINTF(sc, RTWN_DEBUG_STATE,
	    "%s: periodical radar detection task\n", __func__);
#endif

	if (!r21au_dfs_radar_is_enabled(sc)) {
		if (rs->rs_flags & R12A_RADAR_ENABLED) {
			/* should not happen */
			device_printf(sc->sc_dev,
			    "%s: radar detection was turned off "
			    "unexpectedly, resetting...\n", __func__);

			/* XXX something more appropriate? */
			ieee80211_restart_all(ic);
		}
		RTWN_UNLOCK(sc);
		return;
	}

	if (r21au_dfs_radar_is_detected(sc)) {
		r21au_dfs_radar_reset(sc);

		RTWN_DPRINTF(sc, RTWN_DEBUG_RADAR, "%s: got radar event\n",
		    __func__);

		RTWN_UNLOCK(sc);
		IEEE80211_LOCK(ic);

		ieee80211_dfs_notify_radar(ic, ic->ic_curchan);

		IEEE80211_UNLOCK(ic);
		RTWN_LOCK(sc);
	}

	if (rs->rs_flags & R12A_RADAR_ENABLED) {
		taskqueue_enqueue_timeout(taskqueue_thread,
		    &rs->rs_chan_check, R21AU_RADAR_CHECK_PERIOD);
	}
	RTWN_UNLOCK(sc);
}

int
r21au_newstate(struct ieee80211vap *vap, enum ieee80211_state nstate, int arg)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct rtwn_softc *sc = ic->ic_softc;
	struct rtwn_vap *rvp = RTWN_VAP(vap);
	struct r12a_softc *rs = sc->sc_priv;
	int error;

	KASSERT(rvp->id == 0 || rvp->id == 1,
	    ("%s: unexpected vap id %d\n", __func__, rvp->id));

	IEEE80211_UNLOCK(ic);
	RTWN_LOCK(sc);

	error = 0;
	if (nstate == IEEE80211_S_CAC &&
	    !(rs->rs_flags & R12A_RADAR_ENABLED)) {
		error = r21au_dfs_radar_enable(sc);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "%s: cannot enable radar detection\n", __func__);
			goto fail;
		}
		rs->rs_flags |= R12A_RADAR_ENABLED;

		RTWN_DPRINTF(sc, RTWN_DEBUG_RADAR,
		    "%s: radar detection was enabled\n", __func__);

		taskqueue_enqueue_timeout(taskqueue_thread,
		    &rs->rs_chan_check, R21AU_RADAR_CHECK_PERIOD);
	}

	if ((nstate < IEEE80211_S_CAC || nstate == IEEE80211_S_CSA) &&
	    (rs->rs_flags & R12A_RADAR_ENABLED) &&
	    (sc->vaps[!rvp->id] == NULL ||
	    sc->vaps[!rvp->id]->vap.iv_state < IEEE80211_S_CAC ||
	    sc->vaps[!rvp->id]->vap.iv_state == IEEE80211_S_CSA)) {
		taskqueue_cancel_timeout(taskqueue_thread, &rs->rs_chan_check,
		    NULL);

		rs->rs_flags &= ~R12A_RADAR_ENABLED;
		r21au_dfs_radar_disable(sc);

		RTWN_DPRINTF(sc, RTWN_DEBUG_RADAR,
		    "%s: radar detection was disabled\n", __func__);
	}

fail:
	RTWN_UNLOCK(sc);
	IEEE80211_LOCK(ic);

	if (error != 0)
		return (error);

	return (rs->rs_newstate[rvp->id](vap, nstate, arg));
}

void
r21au_scan_start(struct ieee80211com *ic)
{
	struct rtwn_softc *sc = ic->ic_softc;
	struct r12a_softc *rs = sc->sc_priv;

	RTWN_LOCK(sc);
	if (rs->rs_flags & R12A_RADAR_ENABLED) {
		RTWN_UNLOCK(sc);
		while (taskqueue_cancel_timeout(taskqueue_thread,
		    &rs->rs_chan_check, NULL) != 0) {
			taskqueue_drain_timeout(taskqueue_thread,
			    &rs->rs_chan_check);
		}
		RTWN_LOCK(sc);

		r21au_dfs_radar_disable(sc);
		RTWN_DPRINTF(sc, RTWN_DEBUG_RADAR,
		    "%s: radar detection was (temporarily) disabled\n",
		    __func__);
	}
	RTWN_UNLOCK(sc);

	rs->rs_scan_start(ic);
}

void
r21au_scan_end(struct ieee80211com *ic)
{
	struct rtwn_softc *sc = ic->ic_softc;
	struct r12a_softc *rs = sc->sc_priv;
	int error;

	RTWN_LOCK(sc);
	if (rs->rs_flags & R12A_RADAR_ENABLED) {
		error = r21au_dfs_radar_enable(sc);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "%s: cannot re-enable radar detection\n",
			    __func__);

			/* XXX */
			ieee80211_restart_all(ic);
			RTWN_UNLOCK(sc);
			return;
		}
		RTWN_DPRINTF(sc, RTWN_DEBUG_RADAR,
		    "%s: radar detection was re-enabled\n", __func__);

		taskqueue_enqueue_timeout(taskqueue_thread,
		    &rs->rs_chan_check, R21AU_RADAR_CHECK_PERIOD);
	}
	RTWN_UNLOCK(sc);

	rs->rs_scan_end(ic);
}
