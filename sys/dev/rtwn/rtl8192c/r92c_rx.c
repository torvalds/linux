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
#include <sys/linker.h>

#include <net/if.h>
#include <net/ethernet.h>
#include <net/if_media.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_radiotap.h>

#include <dev/rtwn/if_rtwnreg.h>
#include <dev/rtwn/if_rtwnvar.h>
#include <dev/rtwn/if_rtwn_ridx.h>

#include <dev/rtwn/rtl8192c/r92c.h>
#include <dev/rtwn/rtl8192c/r92c_rx_desc.h>


int
r92c_classify_intr(struct rtwn_softc *sc, void *buf, int len)
{
	/* NB: reports are fetched from C2H_MSG register. */
	return (RTWN_RX_DATA);
}

int8_t
r92c_get_rssi_cck(struct rtwn_softc *sc, void *physt)
{
	static const int8_t cckoff[] = { 16, -12, -26, -46 };
	struct r92c_rx_cck *cck = (struct r92c_rx_cck *)physt;
	uint8_t rpt;
	int8_t rssi;

	if (sc->sc_flags & RTWN_FLAG_CCK_HIPWR) {
		rpt = (cck->agc_rpt >> 5) & 0x03;
		rssi = (cck->agc_rpt & 0x1f) << 1;
	} else {
		rpt = (cck->agc_rpt >> 6) & 0x03;
		rssi = cck->agc_rpt & 0x3e;
	}
	rssi = cckoff[rpt] - rssi;

	return (rssi);
}

int8_t
r92c_get_rssi_ofdm(struct rtwn_softc *sc, void *physt)
{
	struct r92c_rx_phystat *phy = (struct r92c_rx_phystat *)physt;
	int rssi;

	/* Get average RSSI. */
	rssi = ((phy->pwdb_all >> 1) & 0x7f) - 110;

	return (rssi);
}

uint8_t
r92c_rx_radiotap_flags(const void *buf)
{
	const struct r92c_rx_stat *stat = buf;
	uint8_t flags, rate;

	if (!(stat->rxdw3 & htole32(R92C_RXDW3_SPLCP)))
		return (0);

	rate = MS(le32toh(stat->rxdw3), R92C_RXDW3_RATE);
	if (RTWN_RATE_IS_CCK(rate))
		flags = IEEE80211_RADIOTAP_F_SHORTPRE;
	else
		flags = IEEE80211_RADIOTAP_F_SHORTGI;
	return (flags);
}

void
r92c_get_rx_stats(struct rtwn_softc *sc, struct ieee80211_rx_stats *rxs,
    const void *desc, const void *physt_ptr)
{
	const struct r92c_rx_stat *stat = desc;
	uint32_t rxdw1, rxdw3;
	uint8_t rate;

	rxdw1 = le32toh(stat->rxdw1);
	rxdw3 = le32toh(stat->rxdw3);
	rate = MS(rxdw3, R92C_RXDW3_RATE);

	if (rxdw1 & R92C_RXDW1_AMPDU)
		rxs->c_pktflags |= IEEE80211_RX_F_AMPDU;
	else if (rxdw1 & R92C_RXDW1_AMPDU_MORE)
		rxs->c_pktflags |= IEEE80211_RX_F_AMPDU_MORE;
	if ((rxdw3 & R92C_RXDW3_SPLCP) && rate >= RTWN_RIDX_HT_MCS(0))
		rxs->c_pktflags |= IEEE80211_RX_F_SHORTGI;

	if (rxdw3 & R92C_RXDW3_HT40)
		rxs->c_width = IEEE80211_RX_FW_40MHZ;
	else
		rxs->c_width = IEEE80211_RX_FW_20MHZ;

	if (RTWN_RATE_IS_CCK(rate))
		rxs->c_phytype = IEEE80211_RX_FP_11B;
	else if (rate < RTWN_RIDX_HT_MCS(0))
		rxs->c_phytype = IEEE80211_RX_FP_11G;
	else
		rxs->c_phytype = IEEE80211_RX_FP_11NG;

	/* Map HW rate index to 802.11 rate. */
	if (rate < RTWN_RIDX_HT_MCS(0)) {
		rxs->c_rate = ridx2rate[rate];
		if (RTWN_RATE_IS_CCK(rate))
			rxs->c_pktflags |= IEEE80211_RX_F_CCK;
		else
			rxs->c_pktflags |= IEEE80211_RX_F_OFDM;
	} else {	/* MCS0~15. */
		rxs->c_rate =
		    IEEE80211_RATE_MCS | (rate - RTWN_RIDX_HT_MCS_SHIFT);
		rxs->c_pktflags |= IEEE80211_RX_F_HT;
	}
}
