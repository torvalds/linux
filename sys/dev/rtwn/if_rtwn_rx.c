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
#include <net/if_var.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_radiotap.h>

#include <dev/rtwn/if_rtwnreg.h>
#include <dev/rtwn/if_rtwnvar.h>

#include <dev/rtwn/if_rtwn_debug.h>
#include <dev/rtwn/if_rtwn_ridx.h>
#include <dev/rtwn/if_rtwn_rx.h>

#include <dev/rtwn/rtl8192c/r92c_reg.h>


void
rtwn_get_rates(struct rtwn_softc *sc, const struct ieee80211_rateset *rs,
    const struct ieee80211_htrateset *rs_ht, uint32_t *rates_p,
    int *maxrate_p, int basic_rates)
{
	uint32_t rates;
	uint8_t ridx;
	int i, maxrate;

	/* Get rates mask. */
	rates = 0;
	maxrate = 0;

	/* This is for 11bg */
	for (i = 0; i < rs->rs_nrates; i++) {
		/* Convert 802.11 rate to HW rate index. */
		ridx = rate2ridx(IEEE80211_RV(rs->rs_rates[i]));
		if (ridx == RTWN_RIDX_UNKNOWN)	/* Unknown rate, skip. */
			continue;
		if (((rs->rs_rates[i] & IEEE80211_RATE_BASIC) != 0) ||
		    !basic_rates) {
			rates |= 1 << ridx;
			if (ridx > maxrate)
				maxrate = ridx;
		}
	}

	/* If we're doing 11n, enable 11n rates */
	if (rs_ht != NULL && !basic_rates) {
		for (i = 0; i < rs_ht->rs_nrates; i++) {
			if ((rs_ht->rs_rates[i] & 0x7f) > 0xf)
				continue;
			/* 11n rates start at index 12 */
			ridx = RTWN_RIDX_HT_MCS((rs_ht->rs_rates[i]) & 0xf);
			rates |= (1 << ridx);

			/* Guard against the rate table being oddly ordered */
			if (ridx > maxrate)
				maxrate = ridx;
		}
	}

	RTWN_DPRINTF(sc, RTWN_DEBUG_RA,
	    "%s: rates 0x%08X, maxrate %d\n", __func__, rates, maxrate);

	if (rates_p != NULL)
		*rates_p = rates;
	if (maxrate_p != NULL)
		*maxrate_p = maxrate;
}

void
rtwn_set_basicrates(struct rtwn_softc *sc, uint32_t rates)
{

	RTWN_DPRINTF(sc, RTWN_DEBUG_RA, "%s: rates 0x%08X\n", __func__, rates);

	rtwn_setbits_4(sc, R92C_RRSR, R92C_RRSR_RATE_BITMAP_M, rates);
}

static void
rtwn_update_avgrssi(struct rtwn_softc *sc, struct rtwn_node *un, int8_t rssi,
    int is_cck)
{
	int pwdb;

	/* Convert antenna signal to percentage. */
	if (rssi <= -100 || rssi >= 20)
		pwdb = 0;
	else if (rssi >= 0)
		pwdb = 100;
	else
		pwdb = 100 + rssi;
	if (is_cck) {
		/* CCK gain is smaller than OFDM/MCS gain. */
		pwdb += 6;
		if (pwdb > 100)
			pwdb = 100;
		if (pwdb <= 14)
			pwdb -= 4;
		else if (pwdb <= 26)
			pwdb -= 8;
		else if (pwdb <= 34)
			pwdb -= 6;
		else if (pwdb <= 42)
			pwdb -= 2;
	}

	if (un->avg_pwdb == -1)	/* Init. */
		un->avg_pwdb = pwdb;
	else if (un->avg_pwdb < pwdb)
		un->avg_pwdb = ((un->avg_pwdb * 19 + pwdb) / 20) + 1;
	else
		un->avg_pwdb = ((un->avg_pwdb * 19 + pwdb) / 20);

	RTWN_DPRINTF(sc, RTWN_DEBUG_RSSI,
	    "MACID %d, PWDB %d, EMA %d\n", un->id, pwdb, un->avg_pwdb);
}

static int8_t
rtwn_get_rssi(struct rtwn_softc *sc, void *physt, int is_cck)
{
	int8_t rssi;

	if (is_cck)
		rssi = rtwn_get_rssi_cck(sc, physt);
	else	/* OFDM/HT. */
		rssi = rtwn_get_rssi_ofdm(sc, physt);

	return (rssi);
}

static uint32_t
rtwn_get_tsf_low(struct rtwn_softc *sc, int id)
{
	return (rtwn_read_4(sc, R92C_TSFTR(id)));
}

static uint32_t
rtwn_get_tsf_high(struct rtwn_softc *sc, int id)
{
	return (rtwn_read_4(sc, R92C_TSFTR(id) + 4));
}

static void
rtwn_get_tsf(struct rtwn_softc *sc, uint64_t *buf, int id)
{
	/* NB: we cannot read it at once. */
	*buf = rtwn_get_tsf_high(sc, id);
	*buf <<= 32;
	*buf += rtwn_get_tsf_low(sc, id);
}

static uint64_t
rtwn_extend_rx_tsf(struct rtwn_softc *sc,
    const struct rtwn_rx_stat_common *stat)
{
	uint64_t tsft;
	uint32_t rxdw3, tsfl, tsfl_curr;
	int id;

	rxdw3 = le32toh(stat->rxdw3);
	tsfl = le32toh(stat->tsf_low);
	id = MS(rxdw3, RTWN_RXDW3_BSSID01_FIT);

	switch (id) {
	case 1:
	case 2:
		id >>= 1;
		tsfl_curr = rtwn_get_tsf_low(sc, id);
		break;
	default:
	{
		uint32_t tsfl0, tsfl1;

		tsfl0 = rtwn_get_tsf_low(sc, 0);
		tsfl1 = rtwn_get_tsf_low(sc, 1);

		if (abs(tsfl0 - tsfl) < abs(tsfl1 - tsfl)) {
			id = 0;
			tsfl_curr = tsfl0;
		} else {
			id = 1;
			tsfl_curr = tsfl1;
		}
		break;
	}
	}

	tsft = rtwn_get_tsf_high(sc, id);
	if (tsfl > tsfl_curr && tsfl > 0xffff0000)
		tsft--;
	tsft <<= 32;
	tsft += tsfl;

	return (tsft);
}

struct ieee80211_node *
rtwn_rx_common(struct rtwn_softc *sc, struct mbuf *m, void *desc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni;
	struct ieee80211_frame_min *wh;
	struct ieee80211_rx_stats rxs;
	struct rtwn_node *un;
	struct rtwn_rx_stat_common *stat;
	void *physt;
	uint32_t rxdw0;
	int8_t rssi;
	int cipher, infosz, is_cck, pktlen, shift;

	stat = desc;
	rxdw0 = le32toh(stat->rxdw0);

	cipher = MS(rxdw0, RTWN_RXDW0_CIPHER);
	infosz = MS(rxdw0, RTWN_RXDW0_INFOSZ) * 8;
	pktlen = MS(rxdw0, RTWN_RXDW0_PKTLEN);
	shift = MS(rxdw0, RTWN_RXDW0_SHIFT);

	wh = (struct ieee80211_frame_min *)(mtodo(m, shift + infosz));
	if ((wh->i_fc[1] & IEEE80211_FC1_PROTECTED) &&
	    cipher != R92C_CAM_ALGO_NONE)
		m->m_flags |= M_WEP;

	if (pktlen >= sizeof(*wh)) {
		ni = ieee80211_find_rxnode(ic, wh);
		if (ni != NULL && (ni->ni_flags & IEEE80211_NODE_HT))
			m->m_flags |= M_AMPDU;
	} else
		ni = NULL;
	un = RTWN_NODE(ni);

	if (infosz != 0 && (rxdw0 & RTWN_RXDW0_PHYST))
		physt = (void *)mtodo(m, shift);
	else
		physt = (un != NULL) ? &un->last_physt : &sc->last_physt;

	bzero(&rxs, sizeof(rxs));
	rtwn_get_rx_stats(sc, &rxs, desc, physt);
	if (rxs.c_pktflags & IEEE80211_RX_F_AMPDU) {
		/* Next MPDU will come without PHY info. */
		memcpy(&sc->last_physt, physt, sizeof(sc->last_physt));
		if (un != NULL)
			memcpy(&un->last_physt, physt, sizeof(sc->last_physt));
	}

	/* Add some common bits. */
	/* NB: should not happen. */
	if (rxdw0 & RTWN_RXDW0_CRCERR)
		rxs.c_pktflags |= IEEE80211_RX_F_FAIL_FCSCRC;

	rxs.r_flags |= IEEE80211_R_TSF_START;	/* XXX undocumented */
	rxs.r_flags |= IEEE80211_R_TSF64;
	rxs.c_rx_tsf = rtwn_extend_rx_tsf(sc, stat);

	/* Get RSSI from PHY status descriptor. */
	is_cck = (rxs.c_pktflags & IEEE80211_RX_F_CCK) != 0;
	rssi = rtwn_get_rssi(sc, physt, is_cck);

	/* XXX TODO: we really need a rate-to-string method */
	RTWN_DPRINTF(sc, RTWN_DEBUG_RSSI, "%s: rssi %d, rate %d\n",
	    __func__, rssi, rxs.c_rate);
	if (un != NULL && infosz != 0 && (rxdw0 & RTWN_RXDW0_PHYST)) {
		/* Update our average RSSI. */
		rtwn_update_avgrssi(sc, un, rssi, is_cck);
	}

	rxs.r_flags |= IEEE80211_R_NF | IEEE80211_R_RSSI;
	rxs.c_nf = RTWN_NOISE_FLOOR;
	rxs.c_rssi = rssi - rxs.c_nf;
	(void) ieee80211_add_rx_params(m, &rxs);

	if (ieee80211_radiotap_active(ic)) {
		struct rtwn_rx_radiotap_header *tap = &sc->sc_rxtap;

		tap->wr_flags = rtwn_rx_radiotap_flags(sc, desc);
		tap->wr_tsft = htole64(rxs.c_rx_tsf);
		tap->wr_rate = rxs.c_rate;
		tap->wr_dbm_antsignal = rssi;
		tap->wr_dbm_antnoise = rxs.c_nf;
	}

	/* Drop PHY descriptor. */
	m_adj(m, infosz + shift);

	return (ni);
}

void
rtwn_adhoc_recv_mgmt(struct ieee80211_node *ni, struct mbuf *m, int subtype,
    const struct ieee80211_rx_stats *rxs,
    int rssi, int nf)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct rtwn_softc *sc = vap->iv_ic->ic_softc;
	struct rtwn_vap *uvp = RTWN_VAP(vap);
	uint64_t ni_tstamp, curr_tstamp;

	uvp->recv_mgmt(ni, m, subtype, rxs, rssi, nf);

	if (vap->iv_state == IEEE80211_S_RUN &&
	    (subtype == IEEE80211_FC0_SUBTYPE_BEACON ||
	    subtype == IEEE80211_FC0_SUBTYPE_PROBE_RESP)) {
		ni_tstamp = le64toh(ni->ni_tstamp.tsf);
		RTWN_LOCK(sc);
		rtwn_get_tsf(sc, &curr_tstamp, uvp->id);
		RTWN_UNLOCK(sc);

		if (ni_tstamp >= curr_tstamp)
			(void) ieee80211_ibss_merge(ni);
	}
}

static uint8_t
rtwn_get_multi_pos(const uint8_t maddr[])
{
	uint64_t mask = 0x00004d101df481b4;
	uint8_t pos = 0x27;	/* initial value */
	int i, j;

	for (i = 0; i < IEEE80211_ADDR_LEN; i++)
		for (j = (i == 0) ? 1 : 0; j < 8; j++)
			if ((maddr[i] >> j) & 1)
				pos ^= (mask >> (i * 8 + j - 1));

	pos &= 0x3f;

	return (pos);
}

void
rtwn_set_multi(struct rtwn_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t mfilt[2];

	RTWN_ASSERT_LOCKED(sc);

	/* general structure was copied from ath(4). */
	if (ic->ic_allmulti == 0) {
		struct ieee80211vap *vap;
		struct ifnet *ifp;
		struct ifmultiaddr *ifma;

		/*
		 * Merge multicast addresses to form the hardware filter.
		 */
		mfilt[0] = mfilt[1] = 0;
		TAILQ_FOREACH(vap, &ic->ic_vaps, iv_next) {
			ifp = vap->iv_ifp;
			if_maddr_rlock(ifp);
			CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
				caddr_t dl;
				uint8_t pos;

				dl = LLADDR((struct sockaddr_dl *)
				    ifma->ifma_addr);
				pos = rtwn_get_multi_pos(dl);

				mfilt[pos / 32] |= (1 << (pos % 32));
			}
			if_maddr_runlock(ifp);
		}
	} else
		mfilt[0] = mfilt[1] = ~0;


	rtwn_write_4(sc, R92C_MAR + 0, mfilt[0]);
	rtwn_write_4(sc, R92C_MAR + 4, mfilt[1]);

	RTWN_DPRINTF(sc, RTWN_DEBUG_STATE, "%s: MC filter %08x:%08x\n",
	    __func__, mfilt[0], mfilt[1]);
}

static void
rtwn_rxfilter_update_mgt(struct rtwn_softc *sc)
{
	uint16_t filter;

	filter = 0x7f7f;
	if (sc->bcn_vaps == 0) {	/* STA and/or MONITOR mode vaps */
		filter &= ~(
		    R92C_RXFLTMAP_SUBTYPE(IEEE80211_FC0_SUBTYPE_ASSOC_REQ) |
		    R92C_RXFLTMAP_SUBTYPE(IEEE80211_FC0_SUBTYPE_REASSOC_REQ) |
		    R92C_RXFLTMAP_SUBTYPE(IEEE80211_FC0_SUBTYPE_PROBE_REQ));
	}
	if (sc->ap_vaps == sc->nvaps - sc->mon_vaps) {	/* AP vaps only */
		filter &= ~(
		    R92C_RXFLTMAP_SUBTYPE(IEEE80211_FC0_SUBTYPE_ASSOC_RESP) |
		    R92C_RXFLTMAP_SUBTYPE(IEEE80211_FC0_SUBTYPE_REASSOC_RESP));
	}
	rtwn_write_2(sc, R92C_RXFLTMAP0, filter);
}

void
rtwn_rxfilter_update(struct rtwn_softc *sc)
{

	RTWN_ASSERT_LOCKED(sc);

	/* Filter for management frames. */
	rtwn_rxfilter_update_mgt(sc);

	/* Update Rx filter. */
	rtwn_set_promisc(sc);
}

void
rtwn_rxfilter_init(struct rtwn_softc *sc)
{

	RTWN_ASSERT_LOCKED(sc);

	/* Setup multicast filter. */
	rtwn_set_multi(sc);

	/* Reject all control frames. */
	rtwn_write_2(sc, R92C_RXFLTMAP1, 0x0000);

	/* Reject all data frames. */
	rtwn_write_2(sc, R92C_RXFLTMAP2, 0x0000);

	/* Append generic Rx filter bits. */
	sc->rcr |= R92C_RCR_AM | R92C_RCR_AB | R92C_RCR_APM |
	    R92C_RCR_HTC_LOC_CTRL | R92C_RCR_APP_PHYSTS |
	    R92C_RCR_APP_ICV | R92C_RCR_APP_MIC;

	/* Update dynamic Rx filter parts. */
	rtwn_rxfilter_update(sc);
}

void
rtwn_rxfilter_set(struct rtwn_softc *sc)
{
	if (!(sc->sc_flags & RTWN_RCR_LOCKED))
		rtwn_write_4(sc, R92C_RCR, sc->rcr);
}

void
rtwn_set_rx_bssid_all(struct rtwn_softc *sc, int enable)
{

	if (enable)
		sc->rcr &= ~R92C_RCR_CBSSID_BCN;
	else
		sc->rcr |= R92C_RCR_CBSSID_BCN;
	rtwn_rxfilter_set(sc);
}

void
rtwn_set_promisc(struct rtwn_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t mask_all, mask_min;

	RTWN_ASSERT_LOCKED(sc);

	mask_all = R92C_RCR_ACF | R92C_RCR_ADF | R92C_RCR_AMF | R92C_RCR_AAP;
	mask_min = R92C_RCR_APM;

	if (sc->bcn_vaps == 0)
		mask_min |= R92C_RCR_CBSSID_BCN;
	if (sc->ap_vaps == 0)
		mask_min |= R92C_RCR_CBSSID_DATA;

	if (ic->ic_promisc == 0 && sc->mon_vaps == 0) {
		if (sc->bcn_vaps != 0)
			mask_all |= R92C_RCR_CBSSID_BCN;
		if (sc->ap_vaps != 0)	/* for Null data frames */
			mask_all |= R92C_RCR_CBSSID_DATA;

		sc->rcr &= ~mask_all;
		sc->rcr |= mask_min;
	} else {
		sc->rcr &= ~mask_min;
		sc->rcr |= mask_all;
	}
	rtwn_rxfilter_set(sc);
}
