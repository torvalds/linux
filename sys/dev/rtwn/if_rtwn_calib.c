/*	$OpenBSD: if_urtwn.c,v 1.16 2011/02/10 17:26:40 jakemsr Exp $	*/

/*-
 * Copyright (c) 2010 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2014 Kevin Lo <kevlo@FreeBSD.org>
 * Copyright (c) 2015-2016 Andriy Voskoboinyk <avos@FreeBSD.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
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

#include <net/if.h>
#include <net/ethernet.h>
#include <net/if_media.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_radiotap.h>

#include <dev/rtwn/if_rtwnvar.h>

#include <dev/rtwn/if_rtwn_calib.h>
#include <dev/rtwn/if_rtwn_debug.h>
#include <dev/rtwn/if_rtwn_task.h>


static void
rtwn_temp_calib(struct rtwn_softc *sc)
{
	uint8_t temp;

	RTWN_ASSERT_LOCKED(sc);

	if (!(sc->sc_flags & RTWN_TEMP_MEASURED)) {
		/* Start measuring temperature. */
		RTWN_DPRINTF(sc, RTWN_DEBUG_TEMP,
		    "%s: start measuring temperature\n", __func__);
		rtwn_temp_measure(sc);
		sc->sc_flags |= RTWN_TEMP_MEASURED;
		return;
	}
	sc->sc_flags &= ~RTWN_TEMP_MEASURED;

	/* Read measured temperature. */
	temp = rtwn_temp_read(sc);
	if (temp == 0) {	/* Read failed, skip. */
		RTWN_DPRINTF(sc, RTWN_DEBUG_TEMP,
		    "%s: temperature read failed, skipping\n", __func__);
		return;
	}

	RTWN_DPRINTF(sc, RTWN_DEBUG_TEMP,
	    "temperature: previous %u, current %u\n",
	    sc->thcal_temp, temp);

	/*
	 * Redo LC/IQ calibration if temperature changed significantly since
	 * last calibration.
	 */
	if (sc->thcal_temp == 0xff) {
		/* efuse value is absent; do LCK at initial status. */
		rtwn_lc_calib(sc);

		sc->thcal_temp = temp;
	} else if (abs(temp - sc->thcal_temp) > sc->temp_delta) {
		RTWN_DPRINTF(sc, RTWN_DEBUG_TEMP,
		    "%s: LC/IQ calib triggered by temp: %u -> %u\n",
		    __func__, sc->thcal_temp, temp);

		rtwn_lc_calib(sc);
		rtwn_iq_calib(sc);

		/* Record temperature of last calibration. */
		sc->thcal_temp = temp;
	}
}

static void
rtwn_calib_cb(struct rtwn_softc *sc, union sec_param *data)
{
	/* Do temperature compensation. */
	rtwn_temp_calib(sc);

#ifndef RTWN_WITHOUT_UCODE
	if (sc->sc_ratectl == RTWN_RATECTL_FW) {
		/* Refresh per-node RSSI. */
		rtwn_set_rssi(sc);
	}
#endif

	if (sc->vaps_running > sc->monvaps_running)
		callout_reset(&sc->sc_calib_to, 2*hz, rtwn_calib_to, sc);
}

void
rtwn_calib_to(void *arg)
{
	struct rtwn_softc *sc = arg;

	/* Do it in a process context. */
	rtwn_cmd_sleepable(sc, NULL, 0, rtwn_calib_cb);
}
