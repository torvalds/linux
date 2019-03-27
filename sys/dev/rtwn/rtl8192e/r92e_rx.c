/*-
 * Copyright (c) 2010 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2014, 2017 Kevin Lo <kevlo@FreeBSD.org>
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
#include <sys/linker.h>

#include <net/if.h>
#include <net/ethernet.h>
#include <net/if_media.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_radiotap.h>
#include <net80211/ieee80211_ratectl.h>

#include <dev/rtwn/if_rtwnreg.h>
#include <dev/rtwn/if_rtwnvar.h>

#include <dev/rtwn/if_rtwn_debug.h>
#include <dev/rtwn/if_rtwn_ridx.h>

#include <dev/rtwn/rtl8188e/r88e.h>
#include <dev/rtwn/rtl8192e/r92e.h>

#include <dev/rtwn/rtl8192c/r92c_rx_desc.h>
#include <dev/rtwn/rtl8812a/r12a_fw_cmd.h>

#ifndef RTWN_WITHOUT_UCODE
void
r92e_handle_c2h_report(struct rtwn_softc *sc, uint8_t *buf, int len)
{

	/* Skip Rx descriptor. */
	buf += sizeof(struct r92c_rx_stat);
	len -= sizeof(struct r92c_rx_stat);

	if (len < 2) {
		device_printf(sc->sc_dev, "C2H report too short (len %d)\n",
		    len);
		return;
	}
	len -= 2;

	switch (buf[0]) {       /* command id */
	case R12A_C2H_TX_REPORT:
		/* NOTREACHED */
		KASSERT(0, ("use handle_tx_report() instead of %s\n",
		    __func__));
		break;
	}
}
#else
void
r92e_handle_c2h_report(struct rtwn_softc *sc, uint8_t *buf, int len)
{

	/* Should not happen. */
	device_printf(sc->sc_dev, "%s: called\n", __func__);
}
#endif

int8_t
r92e_get_rssi_cck(struct rtwn_softc *sc, void *physt)
{

	return (10 + r88e_get_rssi_cck(sc, physt));
}
