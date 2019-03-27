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
#include <net80211/ieee80211_ratectl.h>

#include <dev/rtwn/if_rtwnreg.h>
#include <dev/rtwn/if_rtwnvar.h>

#include <dev/rtwn/if_rtwn_debug.h>
#include <dev/rtwn/if_rtwn_ridx.h>

#include <dev/rtwn/rtl8812a/r12a.h>
#include <dev/rtwn/rtl8812a/r12a_var.h>
#include <dev/rtwn/rtl8812a/r12a_fw_cmd.h>
#include <dev/rtwn/rtl8812a/r12a_rx_desc.h>


#ifndef RTWN_WITHOUT_UCODE
void
r12a_ratectl_tx_complete(struct rtwn_softc *sc, uint8_t *buf, int len)
{
#if __FreeBSD_version >= 1200012
	struct ieee80211_ratectl_tx_status txs;
#endif
	struct r12a_c2h_tx_rpt *rpt;
	struct ieee80211_node *ni;
	int ntries;

	/* Skip Rx descriptor / report id / sequence fields. */
	buf += sizeof(struct r92c_rx_stat) + 2;
	len -= sizeof(struct r92c_rx_stat) + 2;

	rpt = (struct r12a_c2h_tx_rpt *)buf;
	if (len != sizeof(*rpt)) {
		device_printf(sc->sc_dev,
		    "%s: wrong report size (%d, must be %zu)\n",
		    __func__, len, sizeof(*rpt));
		return;
	}

	RTWN_DPRINTF(sc, RTWN_DEBUG_INTR,
	    "%s: ccx report dump: 0: %02X, id: %02X, 2: %02X, queue time: "
	    "low %02X, high %02X, final ridx: %02X, rsvd: %04X\n",
	    __func__, rpt->txrptb0, rpt->macid, rpt->txrptb2,
	    rpt->queue_time_low, rpt->queue_time_high, rpt->final_rate,
	    rpt->reserved);

	if (rpt->macid > sc->macid_limit) {
		device_printf(sc->sc_dev,
		    "macid %u is too big; increase MACID_MAX limit\n",
		    rpt->macid);
		return;
	}

	ntries = MS(rpt->txrptb2, R12A_TXRPTB2_RETRY_CNT);

	ni = sc->node_list[rpt->macid];
	if (ni != NULL) {
		RTWN_DPRINTF(sc, RTWN_DEBUG_INTR, "%s: frame for macid %u was"
		    "%s sent (%d retries)\n", __func__, rpt->macid,
		    (rpt->txrptb0 & (R12A_TXRPTB0_RETRY_OVER |
		    R12A_TXRPTB0_LIFE_EXPIRE)) ? " not" : "", ntries);

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
		if (rpt->txrptb0 & R12A_TXRPTB0_RETRY_OVER)
			txs.status = IEEE80211_RATECTL_TX_FAIL_LONG;
		else if (rpt->txrptb0 & R12A_TXRPTB0_LIFE_EXPIRE)
			txs.status = IEEE80211_RATECTL_TX_FAIL_EXPIRED;
		else
			txs.status = IEEE80211_RATECTL_TX_SUCCESS;
		ieee80211_ratectl_tx_complete(ni, &txs);
#else
		struct ieee80211vap *vap = ni->ni_vap;
		if (rpt->txrptb0 & R12A_TXRPTB0_RETRY_OVER) {
			ieee80211_ratectl_tx_complete(vap, ni,
			    IEEE80211_RATECTL_TX_FAILURE, &ntries, NULL);
		} else {
			ieee80211_ratectl_tx_complete(vap, ni,
			    IEEE80211_RATECTL_TX_SUCCESS, &ntries, NULL);
		}
#endif
	} else {
		RTWN_DPRINTF(sc, RTWN_DEBUG_INTR,
		    "%s: macid %u, ni is NULL\n", __func__, rpt->macid);
	}
}

void
r12a_handle_c2h_report(struct rtwn_softc *sc, uint8_t *buf, int len)
{
	struct r12a_softc *rs = sc->sc_priv;

	/* Skip Rx descriptor. */
	buf += sizeof(struct r92c_rx_stat);
	len -= sizeof(struct r92c_rx_stat);

	if (len < 2) {
		device_printf(sc->sc_dev, "C2H report too short (len %d)\n",
		    len);
		return;
	}
	len -= 2;

	switch (buf[0]) {	/* command id */
	case R12A_C2H_TX_REPORT:
		/* NOTREACHED */
		KASSERT(0, ("use handle_tx_report() instead of %s\n",
		    __func__));
		break;
	case R12A_C2H_IQK_FINISHED:
		RTWN_DPRINTF(sc, RTWN_DEBUG_CALIB,
		    "FW IQ calibration finished\n");
		rs->rs_flags &= ~R12A_IQK_RUNNING;
		break;
	default:
		device_printf(sc->sc_dev,
		    "%s: C2H report %u was not handled\n",
		    __func__, buf[0]);
	}
}
#else
void
r12a_ratectl_tx_complete(struct rtwn_softc *sc, uint8_t *buf, int len)
{
	/* Should not happen. */
	device_printf(sc->sc_dev, "%s: called\n", __func__);
}

void
r12a_handle_c2h_report(struct rtwn_softc *sc, uint8_t *buf, int len)
{
	/* Should not happen. */
	device_printf(sc->sc_dev, "%s: called\n", __func__);
}
#endif

int
r12a_check_frame_checksum(struct rtwn_softc *sc, struct mbuf *m)
{
	struct r12a_softc *rs = sc->sc_priv;
	struct r92c_rx_stat *stat;
	uint32_t rxdw1;

	stat = mtod(m, struct r92c_rx_stat *);
	rxdw1 = le32toh(stat->rxdw1);
	if (rxdw1 & R12A_RXDW1_CKSUM) {
		RTWN_DPRINTF(sc, RTWN_DEBUG_RECV,
		    "%s: %s/%s checksum is %s\n", __func__,
		    (rxdw1 & R12A_RXDW1_UDP) ? "UDP" : "TCP",
		    (rxdw1 & R12A_RXDW1_IPV6) ? "IPv6" : "IP",
		    (rxdw1 & R12A_RXDW1_CKSUM_ERR) ? "invalid" : "valid");

		if (rxdw1 & R12A_RXDW1_CKSUM_ERR)
			return (-1);

		if ((rxdw1 & R12A_RXDW1_IPV6) ?
		    (rs->rs_flags & R12A_RXCKSUM6_EN) :
		    (rs->rs_flags & R12A_RXCKSUM_EN)) {
			m->m_pkthdr.csum_flags = CSUM_IP_CHECKED |
			    CSUM_IP_VALID | CSUM_DATA_VALID | CSUM_PSEUDO_HDR;
			m->m_pkthdr.csum_data = 0xffff;
		}
	}

	return (0);
}

uint8_t
r12a_rx_radiotap_flags(const void *buf)
{
	const struct r92c_rx_stat *stat = buf;
	uint8_t flags, rate;

	if (!(stat->rxdw4 & htole32(R12A_RXDW4_SPLCP)))
		return (0);
	rate = MS(le32toh(stat->rxdw3), R12A_RXDW3_RATE);
	if (RTWN_RATE_IS_CCK(rate))
		flags = IEEE80211_RADIOTAP_F_SHORTPRE;
	else
		flags = IEEE80211_RADIOTAP_F_SHORTGI;
	return (flags);
}

void
r12a_get_rx_stats(struct rtwn_softc *sc, struct ieee80211_rx_stats *rxs,
    const void *desc, const void *physt_ptr)
{
	const struct r92c_rx_stat *stat = desc;
	const struct r12a_rx_phystat *physt = physt_ptr;
	uint32_t rxdw0, rxdw1, rxdw3, rxdw4;
	uint8_t rate;

	rxdw0 = le32toh(stat->rxdw0);
	rxdw1 = le32toh(stat->rxdw1);
	rxdw3 = le32toh(stat->rxdw3);
	rxdw4 = le32toh(stat->rxdw4);
	rate = MS(rxdw3, R12A_RXDW3_RATE);

	/* TODO: STBC */
	if (rxdw4 & R12A_RXDW4_LDPC)
		rxs->c_pktflags |= IEEE80211_RX_F_LDPC;
	if (rxdw1 & R12A_RXDW1_AMPDU) {
		if (rxdw0 & R92C_RXDW0_PHYST)
			rxs->c_pktflags |= IEEE80211_RX_F_AMPDU;
		else
			rxs->c_pktflags |= IEEE80211_RX_F_AMPDU_MORE;
	}

	if ((rxdw4 & R12A_RXDW4_SPLCP) && rate >= RTWN_RIDX_HT_MCS(0))
		rxs->c_pktflags |= IEEE80211_RX_F_SHORTGI;

	switch (MS(rxdw4, R12A_RXDW4_BW)) {
	case R12A_RXDW4_BW20:
		rxs->c_width = IEEE80211_RX_FW_20MHZ;
		break;
	case R12A_RXDW4_BW40:
		rxs->c_width = IEEE80211_RX_FW_40MHZ;
		break;
	case R12A_RXDW4_BW80:
		rxs->c_width = IEEE80211_RX_FW_80MHZ;
		break;
	default:
		break;
	}

	if (RTWN_RATE_IS_CCK(rate))
		rxs->c_phytype = IEEE80211_RX_FP_11B;
	else {
		int is5ghz;

		/* XXX magic */
		/* XXX check with RTL8812AU */
		is5ghz = (physt->cfosho[2] != 0x01);

		if (rate < RTWN_RIDX_HT_MCS(0)) {
			if (is5ghz)
				rxs->c_phytype = IEEE80211_RX_FP_11A;
			else
				rxs->c_phytype = IEEE80211_RX_FP_11G;
		} else {
			if (is5ghz)
				rxs->c_phytype = IEEE80211_RX_FP_11NA;
			else
				rxs->c_phytype = IEEE80211_RX_FP_11NG;
		}
	}

	/* Map HW rate index to 802.11 rate. */
	if (rate < RTWN_RIDX_HT_MCS(0)) {
		rxs->c_rate = ridx2rate[rate];
		if (RTWN_RATE_IS_CCK(rate))
			rxs->c_pktflags |= IEEE80211_RX_F_CCK;
		else
			rxs->c_pktflags |= IEEE80211_RX_F_OFDM;
	} else {	/* MCS0~15. */
		/* TODO: VHT rates */
		rxs->c_rate =
		    IEEE80211_RATE_MCS | (rate - RTWN_RIDX_HT_MCS_SHIFT);
		rxs->c_pktflags |= IEEE80211_RX_F_HT;
	}

	/*
	 * XXX always zero for RTL8821AU
	 * (vendor driver does not check this field)
	 */
#if 0
	rxs->r_flags |= IEEE80211_R_IEEE | IEEE80211_R_FREQ;
	rxs->c_ieee = MS(le16toh(physt->phyw1), R12A_PHYW1_CHAN);
	rxs->c_freq = ieee80211_ieee2mhz(rxs->c_ieee,
	    (rxs->c_ieee < 36) ? IEEE80211_CHAN_2GHZ : IEEE80211_CHAN_5GHZ);
#endif
}
