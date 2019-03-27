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
#include <net80211/ieee80211_ratectl.h>

#include <dev/rtwn/if_rtwnreg.h>
#include <dev/rtwn/if_rtwnvar.h>

#include <dev/rtwn/if_rtwn_debug.h>
#include <dev/rtwn/if_rtwn_ridx.h>

#include <dev/rtwn/rtl8192c/r92c.h>
#include <dev/rtwn/rtl8188e/r88e.h>
#include <dev/rtwn/rtl8188e/r88e_rx_desc.h>


int
r88e_classify_intr(struct rtwn_softc *sc, void *buf, int len)
{
	struct r92c_rx_stat *stat = buf;
	int report_sel = MS(le32toh(stat->rxdw3), R88E_RXDW3_RPT);

	switch (report_sel) {
	case R88E_RXDW3_RPT_RX:
		return (RTWN_RX_DATA);
	case R88E_RXDW3_RPT_TX1:	/* per-packet Tx report */
	case R88E_RXDW3_RPT_TX2:	/* periodical Tx report */
		return (RTWN_RX_TX_REPORT);
	case R88E_RXDW3_RPT_HIS:
		return (RTWN_RX_OTHER);
	default:			/* shut up the compiler */
		return (RTWN_RX_DATA);
	}
}

void
r88e_ratectl_tx_complete(struct rtwn_softc *sc, uint8_t *buf, int len)
{
#if __FreeBSD_version >= 1200012
	struct ieee80211_ratectl_tx_status txs;
#endif
	struct r88e_tx_rpt_ccx *rpt;
	struct ieee80211_node *ni;
	uint8_t macid;
	int ntries;

	/* Skip Rx descriptor. */
	buf += sizeof(struct r92c_rx_stat);
	len -= sizeof(struct r92c_rx_stat);

	rpt = (struct r88e_tx_rpt_ccx *)buf;
	if (len != sizeof(*rpt)) {
		RTWN_DPRINTF(sc, RTWN_DEBUG_INTR,
		    "%s: wrong report size (%d, must be %zu)\n",
		    __func__, len, sizeof(*rpt));
		return;
	}

	RTWN_DPRINTF(sc, RTWN_DEBUG_INTR,
	    "%s: ccx report dump: 0: %02X, 1: %02X, 2: %02X, queue time: "
	    "low %02X, high %02X, final ridx: %02X, 6: %02X, 7: %02X\n",
	    __func__, rpt->rptb0, rpt->rptb1, rpt->rptb2, rpt->queue_time_low,
	    rpt->queue_time_high, rpt->final_rate, rpt->rptb6, rpt->rptb7);

	macid = MS(rpt->rptb1, R88E_RPTB1_MACID);
	if (macid > sc->macid_limit) {
		device_printf(sc->sc_dev,
		    "macid %u is too big; increase MACID_MAX limit\n",
		    macid);
		return;
	}

	ntries = MS(rpt->rptb2, R88E_RPTB2_RETRY_CNT);

	ni = sc->node_list[macid];
	if (ni != NULL) {
		RTWN_DPRINTF(sc, RTWN_DEBUG_INTR, "%s: frame for macid %u was"
		    "%s sent (%d retries)\n", __func__, macid,
		    (rpt->rptb1 & R88E_RPTB1_PKT_OK) ? "" : " not",
		    ntries);

#if __FreeBSD_version >= 1200012
		txs.flags = IEEE80211_RATECTL_STATUS_LONG_RETRY |
			    IEEE80211_RATECTL_STATUS_FINAL_RATE;
		txs.long_retries = ntries;
		if (rpt->final_rate > RTWN_RIDX_OFDM54) {	/* MCS */
			txs.final_rate =
			    rpt->final_rate - RTWN_RIDX_HT_MCS_SHIFT;
			txs.final_rate |= IEEE80211_RATE_MCS;
		} else
			txs.final_rate = ridx2rate[rpt->final_rate];
		if (rpt->rptb1 & R88E_RPTB1_PKT_OK)
			txs.status = IEEE80211_RATECTL_TX_SUCCESS;
		else if (rpt->rptb2 & R88E_RPTB2_RETRY_OVER)
			txs.status = IEEE80211_RATECTL_TX_FAIL_LONG;
		else if (rpt->rptb2 & R88E_RPTB2_LIFE_EXPIRE)
			txs.status = IEEE80211_RATECTL_TX_FAIL_EXPIRED;
		else
			txs.status = IEEE80211_RATECTL_TX_FAIL_UNSPECIFIED;
		ieee80211_ratectl_tx_complete(ni, &txs);
#else
		struct ieee80211vap *vap = ni->ni_vap;
		if (rpt->rptb1 & R88E_RPTB1_PKT_OK) {
			ieee80211_ratectl_tx_complete(vap, ni,
			    IEEE80211_RATECTL_TX_SUCCESS, &ntries, NULL);
		} else {
			ieee80211_ratectl_tx_complete(vap, ni,
			    IEEE80211_RATECTL_TX_FAILURE, &ntries, NULL);
		}
#endif
	} else {
		RTWN_DPRINTF(sc, RTWN_DEBUG_INTR, "%s: macid %u, ni is NULL\n",
		    __func__, macid);
	}
}

void
r88e_handle_c2h_report(struct rtwn_softc *sc, uint8_t *buf, int len)
{

	/* Skip Rx descriptor. */
	buf += sizeof(struct r92c_rx_stat);
	len -= sizeof(struct r92c_rx_stat);

	if (len != R88E_INTR_MSG_LEN) {
		RTWN_DPRINTF(sc, RTWN_DEBUG_INTR,
		    "%s: wrong interrupt message size (%d, must be %d)\n",
		    __func__, len, R88E_INTR_MSG_LEN);
		return;
	}

	/* XXX TODO */
}

int8_t
r88e_get_rssi_cck(struct rtwn_softc *sc, void *physt)
{
	struct r88e_rx_phystat *phy = (struct r88e_rx_phystat *)physt;
	int8_t lna_idx, vga_idx, rssi;

	lna_idx = (phy->agc_rpt & 0xe0) >> 5;
	vga_idx = (phy->agc_rpt & 0x1f);
	rssi = 6 - 2 * vga_idx;

	switch (lna_idx) {
	case 7:
		if (vga_idx > 27)
			rssi = -100 + 6;
		else
			rssi += -100 + 2 * 27;
		break;
	case 6:
		rssi += -48 + 2 * 2;
		break;
	case 5:
		rssi += -42 + 2 * 7;
		break;
	case 4:
		rssi += -36 + 2 * 7;
		break;
	case 3:
		rssi += -24 + 2 * 7;
		break;
	case 2:
		rssi += -6 + 2 * 5;
		if (sc->sc_flags & RTWN_FLAG_CCK_HIPWR)
			rssi -= 6;
		break;
	case 1:
		rssi += 8;
		break;
	case 0:
		rssi += 14;
		break;
	}

	return (rssi);
}

int8_t
r88e_get_rssi_ofdm(struct rtwn_softc *sc, void *physt)
{
	struct r88e_rx_phystat *phy = (struct r88e_rx_phystat *)physt;
	int rssi;

	/* Get average RSSI. */
	rssi = ((phy->sig_qual >> 1) & 0x7f) - 110;

	return (rssi);
}

void
r88e_get_rx_stats(struct rtwn_softc *sc, struct ieee80211_rx_stats *rxs,
    const void *desc, const void *physt_ptr)
{
	const struct r88e_rx_phystat *physt = physt_ptr;

	r92c_get_rx_stats(sc, rxs, desc, physt_ptr);

	if (!sc->sc_ht40) {	/* XXX center channel */
		rxs->r_flags |= IEEE80211_R_IEEE | IEEE80211_R_FREQ;
		rxs->c_ieee = physt->chan;
		rxs->c_freq = ieee80211_ieee2mhz(rxs->c_ieee,
		    IEEE80211_CHAN_2GHZ);
	}
}
