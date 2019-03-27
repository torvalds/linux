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
 * IEEE 802.11n protocol support.
 */

#include "opt_inet.h"
#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/systm.h> 
#include <sys/endian.h>
 
#include <sys/socket.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_media.h>
#include <net/ethernet.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_action.h>
#include <net80211/ieee80211_input.h>

/* define here, used throughout file */
#define	MS(_v, _f)	(((_v) & _f) >> _f##_S)
#define	SM(_v, _f)	(((_v) << _f##_S) & _f)

const struct ieee80211_mcs_rates ieee80211_htrates[IEEE80211_HTRATE_MAXSIZE] = {
	{  13,  14,   27,   30 },	/* MCS 0 */
	{  26,  29,   54,   60 },	/* MCS 1 */
	{  39,  43,   81,   90 },	/* MCS 2 */
	{  52,  58,  108,  120 },	/* MCS 3 */
	{  78,  87,  162,  180 },	/* MCS 4 */
	{ 104, 116,  216,  240 },	/* MCS 5 */
	{ 117, 130,  243,  270 },	/* MCS 6 */
	{ 130, 144,  270,  300 },	/* MCS 7 */
	{  26,  29,   54,   60 },	/* MCS 8 */
	{  52,  58,  108,  120 },	/* MCS 9 */
	{  78,  87,  162,  180 },	/* MCS 10 */
	{ 104, 116,  216,  240 },	/* MCS 11 */
	{ 156, 173,  324,  360 },	/* MCS 12 */
	{ 208, 231,  432,  480 },	/* MCS 13 */
	{ 234, 260,  486,  540 },	/* MCS 14 */
	{ 260, 289,  540,  600 },	/* MCS 15 */
	{  39,  43,   81,   90 },	/* MCS 16 */
	{  78,  87,  162,  180 },	/* MCS 17 */
	{ 117, 130,  243,  270 },	/* MCS 18 */
	{ 156, 173,  324,  360 },	/* MCS 19 */
	{ 234, 260,  486,  540 },	/* MCS 20 */
	{ 312, 347,  648,  720 },	/* MCS 21 */
	{ 351, 390,  729,  810 },	/* MCS 22 */
	{ 390, 433,  810,  900 },	/* MCS 23 */
	{  52,  58,  108,  120 },	/* MCS 24 */
	{ 104, 116,  216,  240 },	/* MCS 25 */
	{ 156, 173,  324,  360 },	/* MCS 26 */
	{ 208, 231,  432,  480 },	/* MCS 27 */
	{ 312, 347,  648,  720 },	/* MCS 28 */
	{ 416, 462,  864,  960 },	/* MCS 29 */
	{ 468, 520,  972, 1080 },	/* MCS 30 */
	{ 520, 578, 1080, 1200 },	/* MCS 31 */
	{   0,   0,   12,   13 },	/* MCS 32 */
	{  78,  87,  162,  180 },	/* MCS 33 */
	{ 104, 116,  216,  240 },	/* MCS 34 */
	{ 130, 144,  270,  300 },	/* MCS 35 */
	{ 117, 130,  243,  270 },	/* MCS 36 */
	{ 156, 173,  324,  360 },	/* MCS 37 */
	{ 195, 217,  405,  450 },	/* MCS 38 */
	{ 104, 116,  216,  240 },	/* MCS 39 */
	{ 130, 144,  270,  300 },	/* MCS 40 */
	{ 130, 144,  270,  300 },	/* MCS 41 */
	{ 156, 173,  324,  360 },	/* MCS 42 */
	{ 182, 202,  378,  420 },	/* MCS 43 */
	{ 182, 202,  378,  420 },	/* MCS 44 */
	{ 208, 231,  432,  480 },	/* MCS 45 */
	{ 156, 173,  324,  360 },	/* MCS 46 */
	{ 195, 217,  405,  450 },	/* MCS 47 */
	{ 195, 217,  405,  450 },	/* MCS 48 */
	{ 234, 260,  486,  540 },	/* MCS 49 */
	{ 273, 303,  567,  630 },	/* MCS 50 */
	{ 273, 303,  567,  630 },	/* MCS 51 */
	{ 312, 347,  648,  720 },	/* MCS 52 */
	{ 130, 144,  270,  300 },	/* MCS 53 */
	{ 156, 173,  324,  360 },	/* MCS 54 */
	{ 182, 202,  378,  420 },	/* MCS 55 */
	{ 156, 173,  324,  360 },	/* MCS 56 */
	{ 182, 202,  378,  420 },	/* MCS 57 */
	{ 208, 231,  432,  480 },	/* MCS 58 */
	{ 234, 260,  486,  540 },	/* MCS 59 */
	{ 208, 231,  432,  480 },	/* MCS 60 */
	{ 234, 260,  486,  540 },	/* MCS 61 */
	{ 260, 289,  540,  600 },	/* MCS 62 */
	{ 260, 289,  540,  600 },	/* MCS 63 */
	{ 286, 318,  594,  660 },	/* MCS 64 */
	{ 195, 217,  405,  450 },	/* MCS 65 */
	{ 234, 260,  486,  540 },	/* MCS 66 */
	{ 273, 303,  567,  630 },	/* MCS 67 */
	{ 234, 260,  486,  540 },	/* MCS 68 */
	{ 273, 303,  567,  630 },	/* MCS 69 */
	{ 312, 347,  648,  720 },	/* MCS 70 */
	{ 351, 390,  729,  810 },	/* MCS 71 */
	{ 312, 347,  648,  720 },	/* MCS 72 */
	{ 351, 390,  729,  810 },	/* MCS 73 */
	{ 390, 433,  810,  900 },	/* MCS 74 */
	{ 390, 433,  810,  900 },	/* MCS 75 */
	{ 429, 477,  891,  990 },	/* MCS 76 */
};

static	int ieee80211_ampdu_age = -1;	/* threshold for ampdu reorder q (ms) */
SYSCTL_PROC(_net_wlan, OID_AUTO, ampdu_age, CTLTYPE_INT | CTLFLAG_RW,
	&ieee80211_ampdu_age, 0, ieee80211_sysctl_msecs_ticks, "I",
	"AMPDU max reorder age (ms)");

static	int ieee80211_recv_bar_ena = 1;
SYSCTL_INT(_net_wlan, OID_AUTO, recv_bar, CTLFLAG_RW, &ieee80211_recv_bar_ena,
	    0, "BAR frame processing (ena/dis)");

static	int ieee80211_addba_timeout = -1;/* timeout for ADDBA response */
SYSCTL_PROC(_net_wlan, OID_AUTO, addba_timeout, CTLTYPE_INT | CTLFLAG_RW,
	&ieee80211_addba_timeout, 0, ieee80211_sysctl_msecs_ticks, "I",
	"ADDBA request timeout (ms)");
static	int ieee80211_addba_backoff = -1;/* backoff after max ADDBA requests */
SYSCTL_PROC(_net_wlan, OID_AUTO, addba_backoff, CTLTYPE_INT | CTLFLAG_RW,
	&ieee80211_addba_backoff, 0, ieee80211_sysctl_msecs_ticks, "I",
	"ADDBA request backoff (ms)");
static	int ieee80211_addba_maxtries = 3;/* max ADDBA requests before backoff */
SYSCTL_INT(_net_wlan, OID_AUTO, addba_maxtries, CTLFLAG_RW,
	&ieee80211_addba_maxtries, 0, "max ADDBA requests sent before backoff");

static	int ieee80211_bar_timeout = -1;	/* timeout waiting for BAR response */
static	int ieee80211_bar_maxtries = 50;/* max BAR requests before DELBA */

static	ieee80211_recv_action_func ht_recv_action_ba_addba_request;
static	ieee80211_recv_action_func ht_recv_action_ba_addba_response;
static	ieee80211_recv_action_func ht_recv_action_ba_delba;
static	ieee80211_recv_action_func ht_recv_action_ht_mimopwrsave;
static	ieee80211_recv_action_func ht_recv_action_ht_txchwidth;

static	ieee80211_send_action_func ht_send_action_ba_addba;
static	ieee80211_send_action_func ht_send_action_ba_delba;
static	ieee80211_send_action_func ht_send_action_ht_txchwidth;

static void
ieee80211_ht_init(void)
{
	/*
	 * Setup HT parameters that depends on the clock frequency.
	 */
	ieee80211_ampdu_age = msecs_to_ticks(500);
	ieee80211_addba_timeout = msecs_to_ticks(250);
	ieee80211_addba_backoff = msecs_to_ticks(10*1000);
	ieee80211_bar_timeout = msecs_to_ticks(250);
	/*
	 * Register action frame handlers.
	 */
	ieee80211_recv_action_register(IEEE80211_ACTION_CAT_BA, 
	    IEEE80211_ACTION_BA_ADDBA_REQUEST, ht_recv_action_ba_addba_request);
	ieee80211_recv_action_register(IEEE80211_ACTION_CAT_BA, 
	    IEEE80211_ACTION_BA_ADDBA_RESPONSE, ht_recv_action_ba_addba_response);
	ieee80211_recv_action_register(IEEE80211_ACTION_CAT_BA, 
	    IEEE80211_ACTION_BA_DELBA, ht_recv_action_ba_delba);
	ieee80211_recv_action_register(IEEE80211_ACTION_CAT_HT, 
	    IEEE80211_ACTION_HT_MIMOPWRSAVE, ht_recv_action_ht_mimopwrsave);
	ieee80211_recv_action_register(IEEE80211_ACTION_CAT_HT, 
	    IEEE80211_ACTION_HT_TXCHWIDTH, ht_recv_action_ht_txchwidth);

	ieee80211_send_action_register(IEEE80211_ACTION_CAT_BA, 
	    IEEE80211_ACTION_BA_ADDBA_REQUEST, ht_send_action_ba_addba);
	ieee80211_send_action_register(IEEE80211_ACTION_CAT_BA, 
	    IEEE80211_ACTION_BA_ADDBA_RESPONSE, ht_send_action_ba_addba);
	ieee80211_send_action_register(IEEE80211_ACTION_CAT_BA, 
	    IEEE80211_ACTION_BA_DELBA, ht_send_action_ba_delba);
	ieee80211_send_action_register(IEEE80211_ACTION_CAT_HT, 
	    IEEE80211_ACTION_HT_TXCHWIDTH, ht_send_action_ht_txchwidth);
}
SYSINIT(wlan_ht, SI_SUB_DRIVERS, SI_ORDER_FIRST, ieee80211_ht_init, NULL);

static int ieee80211_ampdu_enable(struct ieee80211_node *ni,
	struct ieee80211_tx_ampdu *tap);
static int ieee80211_addba_request(struct ieee80211_node *ni,
	struct ieee80211_tx_ampdu *tap,
	int dialogtoken, int baparamset, int batimeout);
static int ieee80211_addba_response(struct ieee80211_node *ni,
	struct ieee80211_tx_ampdu *tap,
	int code, int baparamset, int batimeout);
static void ieee80211_addba_stop(struct ieee80211_node *ni,
	struct ieee80211_tx_ampdu *tap);
static void null_addba_response_timeout(struct ieee80211_node *ni,
	struct ieee80211_tx_ampdu *tap);

static void ieee80211_bar_response(struct ieee80211_node *ni,
	struct ieee80211_tx_ampdu *tap, int status);
static void ampdu_tx_stop(struct ieee80211_tx_ampdu *tap);
static void bar_stop_timer(struct ieee80211_tx_ampdu *tap);
static int ampdu_rx_start(struct ieee80211_node *, struct ieee80211_rx_ampdu *,
	int baparamset, int batimeout, int baseqctl);
static void ampdu_rx_stop(struct ieee80211_node *, struct ieee80211_rx_ampdu *);

void
ieee80211_ht_attach(struct ieee80211com *ic)
{
	/* setup default aggregation policy */
	ic->ic_recv_action = ieee80211_recv_action;
	ic->ic_send_action = ieee80211_send_action;
	ic->ic_ampdu_enable = ieee80211_ampdu_enable;
	ic->ic_addba_request = ieee80211_addba_request;
	ic->ic_addba_response = ieee80211_addba_response;
	ic->ic_addba_response_timeout = null_addba_response_timeout;
	ic->ic_addba_stop = ieee80211_addba_stop;
	ic->ic_bar_response = ieee80211_bar_response;
	ic->ic_ampdu_rx_start = ampdu_rx_start;
	ic->ic_ampdu_rx_stop = ampdu_rx_stop;

	ic->ic_htprotmode = IEEE80211_PROT_RTSCTS;
	ic->ic_curhtprotmode = IEEE80211_HTINFO_OPMODE_PURE;
}

void
ieee80211_ht_detach(struct ieee80211com *ic)
{
}

void
ieee80211_ht_vattach(struct ieee80211vap *vap)
{

	/* driver can override defaults */
	vap->iv_ampdu_rxmax = IEEE80211_HTCAP_MAXRXAMPDU_8K;
	vap->iv_ampdu_density = IEEE80211_HTCAP_MPDUDENSITY_NA;
	vap->iv_ampdu_limit = vap->iv_ampdu_rxmax;
	vap->iv_amsdu_limit = vap->iv_htcaps & IEEE80211_HTCAP_MAXAMSDU;
	/* tx aggregation traffic thresholds */
	vap->iv_ampdu_mintraffic[WME_AC_BK] = 128;
	vap->iv_ampdu_mintraffic[WME_AC_BE] = 64;
	vap->iv_ampdu_mintraffic[WME_AC_VO] = 32;
	vap->iv_ampdu_mintraffic[WME_AC_VI] = 32;

	if (vap->iv_htcaps & IEEE80211_HTC_HT) {
		/*
		 * Device is HT capable; enable all HT-related
		 * facilities by default.
		 * XXX these choices may be too aggressive.
		 */
		vap->iv_flags_ht |= IEEE80211_FHT_HT
				 |  IEEE80211_FHT_HTCOMPAT
				 ;
		if (vap->iv_htcaps & IEEE80211_HTCAP_SHORTGI20)
			vap->iv_flags_ht |= IEEE80211_FHT_SHORTGI20;
		/* XXX infer from channel list? */
		if (vap->iv_htcaps & IEEE80211_HTCAP_CHWIDTH40) {
			vap->iv_flags_ht |= IEEE80211_FHT_USEHT40;
			if (vap->iv_htcaps & IEEE80211_HTCAP_SHORTGI40)
				vap->iv_flags_ht |= IEEE80211_FHT_SHORTGI40;
		}
		/* enable RIFS if capable */
		if (vap->iv_htcaps & IEEE80211_HTC_RIFS)
			vap->iv_flags_ht |= IEEE80211_FHT_RIFS;

		/* NB: A-MPDU and A-MSDU rx are mandated, these are tx only */
		vap->iv_flags_ht |= IEEE80211_FHT_AMPDU_RX;
		if (vap->iv_htcaps & IEEE80211_HTC_AMPDU)
			vap->iv_flags_ht |= IEEE80211_FHT_AMPDU_TX;
		vap->iv_flags_ht |= IEEE80211_FHT_AMSDU_RX;
		if (vap->iv_htcaps & IEEE80211_HTC_AMSDU)
			vap->iv_flags_ht |= IEEE80211_FHT_AMSDU_TX;

		if (vap->iv_htcaps & IEEE80211_HTCAP_TXSTBC)
			vap->iv_flags_ht |= IEEE80211_FHT_STBC_TX;
		if (vap->iv_htcaps & IEEE80211_HTCAP_RXSTBC)
			vap->iv_flags_ht |= IEEE80211_FHT_STBC_RX;

		if (vap->iv_htcaps & IEEE80211_HTCAP_LDPC)
			vap->iv_flags_ht |= IEEE80211_FHT_LDPC_RX;
		if (vap->iv_htcaps & IEEE80211_HTC_TXLDPC)
			vap->iv_flags_ht |= IEEE80211_FHT_LDPC_TX;
	}
	/* NB: disable default legacy WDS, too many issues right now */
	if (vap->iv_flags_ext & IEEE80211_FEXT_WDSLEGACY)
		vap->iv_flags_ht &= ~IEEE80211_FHT_HT;
}

void
ieee80211_ht_vdetach(struct ieee80211vap *vap)
{
}

static int
ht_getrate(struct ieee80211com *ic, int index, enum ieee80211_phymode mode,
    int ratetype)
{
	int mword, rate;

	mword = ieee80211_rate2media(ic, index | IEEE80211_RATE_MCS, mode);
	if (IFM_SUBTYPE(mword) != IFM_IEEE80211_MCS)
		return (0);
	switch (ratetype) {
	case 0:
		rate = ieee80211_htrates[index].ht20_rate_800ns;
		break;
	case 1:
		rate = ieee80211_htrates[index].ht20_rate_400ns;
		break;
	case 2:
		rate = ieee80211_htrates[index].ht40_rate_800ns;
		break;
	default:
		rate = ieee80211_htrates[index].ht40_rate_400ns;
		break;
	}
	return (rate);
}

static struct printranges {
	int	minmcs;
	int	maxmcs;
	int	txstream;
	int	ratetype;
	int	htcapflags;
} ranges[] = {
	{  0,  7, 1, 0, 0 },
	{  8, 15, 2, 0, 0 },
	{ 16, 23, 3, 0, 0 },
	{ 24, 31, 4, 0, 0 },
	{ 32,  0, 1, 2, IEEE80211_HTC_TXMCS32 },
	{ 33, 38, 2, 0, IEEE80211_HTC_TXUNEQUAL },
	{ 39, 52, 3, 0, IEEE80211_HTC_TXUNEQUAL },
	{ 53, 76, 4, 0, IEEE80211_HTC_TXUNEQUAL },
	{  0,  0, 0, 0, 0 },
};

static void
ht_rateprint(struct ieee80211com *ic, enum ieee80211_phymode mode, int ratetype)
{
	int minrate, maxrate;
	struct printranges *range;

	for (range = ranges; range->txstream != 0; range++) {
		if (ic->ic_txstream < range->txstream)
			continue;
		if (range->htcapflags &&
		    (ic->ic_htcaps & range->htcapflags) == 0)
			continue;
		if (ratetype < range->ratetype)
			continue;
		minrate = ht_getrate(ic, range->minmcs, mode, ratetype);
		maxrate = ht_getrate(ic, range->maxmcs, mode, ratetype);
		if (range->maxmcs) {
			ic_printf(ic, "MCS %d-%d: %d%sMbps - %d%sMbps\n",
			    range->minmcs, range->maxmcs,
			    minrate/2, ((minrate & 0x1) != 0 ? ".5" : ""),
			    maxrate/2, ((maxrate & 0x1) != 0 ? ".5" : ""));
		} else {
			ic_printf(ic, "MCS %d: %d%sMbps\n", range->minmcs,
			    minrate/2, ((minrate & 0x1) != 0 ? ".5" : ""));
		}
	}
}

static void
ht_announce(struct ieee80211com *ic, enum ieee80211_phymode mode)
{
	const char *modestr = ieee80211_phymode_name[mode];

	ic_printf(ic, "%s MCS 20MHz\n", modestr);
	ht_rateprint(ic, mode, 0);
	if (ic->ic_htcaps & IEEE80211_HTCAP_SHORTGI20) {
		ic_printf(ic, "%s MCS 20MHz SGI\n", modestr);
		ht_rateprint(ic, mode, 1);
	}
	if (ic->ic_htcaps & IEEE80211_HTCAP_CHWIDTH40) {
		ic_printf(ic, "%s MCS 40MHz:\n", modestr);
		ht_rateprint(ic, mode, 2);
	}
	if ((ic->ic_htcaps & IEEE80211_HTCAP_CHWIDTH40) &&
	    (ic->ic_htcaps & IEEE80211_HTCAP_SHORTGI40)) {
		ic_printf(ic, "%s MCS 40MHz SGI:\n", modestr);
		ht_rateprint(ic, mode, 3);
	}
}

void
ieee80211_ht_announce(struct ieee80211com *ic)
{

	if (isset(ic->ic_modecaps, IEEE80211_MODE_11NA) ||
	    isset(ic->ic_modecaps, IEEE80211_MODE_11NG))
		ic_printf(ic, "%dT%dR\n", ic->ic_txstream, ic->ic_rxstream);
	if (isset(ic->ic_modecaps, IEEE80211_MODE_11NA))
		ht_announce(ic, IEEE80211_MODE_11NA);
	if (isset(ic->ic_modecaps, IEEE80211_MODE_11NG))
		ht_announce(ic, IEEE80211_MODE_11NG);
}

void
ieee80211_init_suphtrates(struct ieee80211com *ic)
{
#define	ADDRATE(x)	do {						\
	htrateset->rs_rates[htrateset->rs_nrates] = x;			\
	htrateset->rs_nrates++;						\
} while (0)
	struct ieee80211_htrateset *htrateset = &ic->ic_sup_htrates;
	int i;

	memset(htrateset, 0, sizeof(struct ieee80211_htrateset));
	for (i = 0; i < ic->ic_txstream * 8; i++)
		ADDRATE(i);
	if ((ic->ic_htcaps & IEEE80211_HTCAP_CHWIDTH40) &&
	    (ic->ic_htcaps & IEEE80211_HTC_TXMCS32))
		ADDRATE(32);
	if (ic->ic_htcaps & IEEE80211_HTC_TXUNEQUAL) {
		if (ic->ic_txstream >= 2) {
			 for (i = 33; i <= 38; i++)
				ADDRATE(i);
		}
		if (ic->ic_txstream >= 3) {
			for (i = 39; i <= 52; i++)
				ADDRATE(i);
		}
		if (ic->ic_txstream == 4) {
			for (i = 53; i <= 76; i++)
				ADDRATE(i);
		}
	}
#undef	ADDRATE
}

/*
 * Receive processing.
 */

/*
 * Decap the encapsulated A-MSDU frames and dispatch all but
 * the last for delivery.  The last frame is returned for 
 * delivery via the normal path.
 */
struct mbuf *
ieee80211_decap_amsdu(struct ieee80211_node *ni, struct mbuf *m)
{
	struct ieee80211vap *vap = ni->ni_vap;
	int framelen;
	struct mbuf *n;

	/* discard 802.3 header inserted by ieee80211_decap */
	m_adj(m, sizeof(struct ether_header));

	vap->iv_stats.is_amsdu_decap++;

	for (;;) {
		/*
		 * Decap the first frame, bust it apart from the
		 * remainder and deliver.  We leave the last frame
		 * delivery to the caller (for consistency with other
		 * code paths, could also do it here).
		 */
		m = ieee80211_decap1(m, &framelen);
		if (m == NULL) {
			IEEE80211_DISCARD_MAC(vap, IEEE80211_MSG_ANY,
			    ni->ni_macaddr, "a-msdu", "%s", "decap failed");
			vap->iv_stats.is_amsdu_tooshort++;
			return NULL;
		}
		if (m->m_pkthdr.len == framelen)
			break;
		n = m_split(m, framelen, M_NOWAIT);
		if (n == NULL) {
			IEEE80211_DISCARD_MAC(vap, IEEE80211_MSG_ANY,
			    ni->ni_macaddr, "a-msdu",
			    "%s", "unable to split encapsulated frames");
			vap->iv_stats.is_amsdu_split++;
			m_freem(m);			/* NB: must reclaim */
			return NULL;
		}
		vap->iv_deliver_data(vap, ni, m);

		/*
		 * Remove frame contents; each intermediate frame
		 * is required to be aligned to a 4-byte boundary.
		 */
		m = n;
		m_adj(m, roundup2(framelen, 4) - framelen);	/* padding */
	}
	return m;				/* last delivered by caller */
}

/*
 * Add the given frame to the current RX reorder slot.
 *
 * For future offloaded A-MSDU handling where multiple frames with
 * the same sequence number show up here, this routine will append
 * those frames as long as they're appropriately tagged.
 */
static int
ampdu_rx_add_slot(struct ieee80211_rx_ampdu *rap, int off, int tid,
    ieee80211_seq rxseq,
    struct ieee80211_node *ni,
    struct mbuf *m)
{
	struct ieee80211vap *vap = ni->ni_vap;

	if (rap->rxa_m[off] == NULL) {
		rap->rxa_m[off] = m;
		rap->rxa_qframes++;
		rap->rxa_qbytes += m->m_pkthdr.len;
		vap->iv_stats.is_ampdu_rx_reorder++;
		return (0);
	} else {
		IEEE80211_DISCARD_MAC(vap,
		    IEEE80211_MSG_INPUT | IEEE80211_MSG_11N,
		    ni->ni_macaddr, "a-mpdu duplicate",
		    "seqno %u tid %u BA win <%u:%u>",
		    rxseq, tid, rap->rxa_start,
		    IEEE80211_SEQ_ADD(rap->rxa_start, rap->rxa_wnd-1));
		vap->iv_stats.is_rx_dup++;
		IEEE80211_NODE_STAT(ni, rx_dup);
		m_freem(m);
		return (-1);
	}
}

static void
ampdu_rx_purge_slot(struct ieee80211_rx_ampdu *rap, int i)
{
	struct mbuf *m;

	m = rap->rxa_m[i];
	if (m == NULL)
		return;

	rap->rxa_m[i] = NULL;
	rap->rxa_qbytes -= m->m_pkthdr.len;
	rap->rxa_qframes--;
	m_freem(m);
}

/*
 * Purge all frames in the A-MPDU re-order queue.
 */
static void
ampdu_rx_purge(struct ieee80211_rx_ampdu *rap)
{
	int i;

	for (i = 0; i < rap->rxa_wnd; i++) {
		ampdu_rx_purge_slot(rap, i);
		if (rap->rxa_qframes == 0)
			break;
	}
	KASSERT(rap->rxa_qbytes == 0 && rap->rxa_qframes == 0,
	    ("lost %u data, %u frames on ampdu rx q",
	    rap->rxa_qbytes, rap->rxa_qframes));
}

/*
 * Start A-MPDU rx/re-order processing for the specified TID.
 */
static int
ampdu_rx_start(struct ieee80211_node *ni, struct ieee80211_rx_ampdu *rap,
	int baparamset, int batimeout, int baseqctl)
{
	int bufsiz = MS(baparamset, IEEE80211_BAPS_BUFSIZ);

	if (rap->rxa_flags & IEEE80211_AGGR_RUNNING) {
		/*
		 * AMPDU previously setup and not terminated with a DELBA,
		 * flush the reorder q's in case anything remains.
		 */
		ampdu_rx_purge(rap);
	}
	memset(rap, 0, sizeof(*rap));
	rap->rxa_wnd = (bufsiz == 0) ?
	    IEEE80211_AGGR_BAWMAX : min(bufsiz, IEEE80211_AGGR_BAWMAX);
	rap->rxa_start = MS(baseqctl, IEEE80211_BASEQ_START);
	rap->rxa_flags |=  IEEE80211_AGGR_RUNNING | IEEE80211_AGGR_XCHGPEND;

	return 0;
}

/*
 * Public function; manually setup the RX ampdu state.
 */
int
ieee80211_ampdu_rx_start_ext(struct ieee80211_node *ni, int tid, int seq, int baw)
{
	struct ieee80211_rx_ampdu *rap;

	/* XXX TODO: sanity check tid, seq, baw */

	rap = &ni->ni_rx_ampdu[tid];

	if (rap->rxa_flags & IEEE80211_AGGR_RUNNING) {
		/*
		 * AMPDU previously setup and not terminated with a DELBA,
		 * flush the reorder q's in case anything remains.
		 */
		ampdu_rx_purge(rap);
	}

	memset(rap, 0, sizeof(*rap));
	rap->rxa_wnd = (baw== 0) ?
	    IEEE80211_AGGR_BAWMAX : min(baw, IEEE80211_AGGR_BAWMAX);
	if (seq == -1) {
		/* Wait for the first RX frame, use that as BAW */
		rap->rxa_start = 0;
		rap->rxa_flags |= IEEE80211_AGGR_WAITRX;
	} else {
		rap->rxa_start = seq;
	}
	rap->rxa_flags |=  IEEE80211_AGGR_RUNNING | IEEE80211_AGGR_XCHGPEND;

	IEEE80211_NOTE(ni->ni_vap, IEEE80211_MSG_11N, ni,
	    "%s: tid=%d, start=%d, wnd=%d, flags=0x%08x",
	    __func__,
	    tid,
	    seq,
	    rap->rxa_wnd,
	    rap->rxa_flags);

	return 0;
}

/*
 * Public function; manually stop the RX AMPDU state.
 */
void
ieee80211_ampdu_rx_stop_ext(struct ieee80211_node *ni, int tid)
{
	struct ieee80211_rx_ampdu *rap;

	/* XXX TODO: sanity check tid, seq, baw */
	rap = &ni->ni_rx_ampdu[tid];
	ampdu_rx_stop(ni, rap);
}

/*
 * Stop A-MPDU rx processing for the specified TID.
 */
static void
ampdu_rx_stop(struct ieee80211_node *ni, struct ieee80211_rx_ampdu *rap)
{

	ampdu_rx_purge(rap);
	rap->rxa_flags &= ~(IEEE80211_AGGR_RUNNING
	    | IEEE80211_AGGR_XCHGPEND
	    | IEEE80211_AGGR_WAITRX);
}

/*
 * Dispatch a frame from the A-MPDU reorder queue.  The
 * frame is fed back into ieee80211_input marked with an
 * M_AMPDU_MPDU flag so it doesn't come back to us (it also
 * permits ieee80211_input to optimize re-processing).
 */
static __inline void
ampdu_dispatch(struct ieee80211_node *ni, struct mbuf *m)
{
	m->m_flags |= M_AMPDU_MPDU;	/* bypass normal processing */
	/* NB: rssi and noise are ignored w/ M_AMPDU_MPDU set */
	(void) ieee80211_input(ni, m, 0, 0);
}

static int
ampdu_dispatch_slot(struct ieee80211_rx_ampdu *rap, struct ieee80211_node *ni,
    int i)
{
	struct mbuf *m;

	if (rap->rxa_m[i] == NULL)
		return (0);

	m = rap->rxa_m[i];
	rap->rxa_m[i] = NULL;
	rap->rxa_qbytes -= m->m_pkthdr.len;
	rap->rxa_qframes--;

	ampdu_dispatch(ni, m);

	return (1);
}

static void
ampdu_rx_moveup(struct ieee80211_rx_ampdu *rap, struct ieee80211_node *ni,
    int i, int winstart)
{
	struct ieee80211vap *vap = ni->ni_vap;

	if (rap->rxa_qframes != 0) {
		int n = rap->rxa_qframes, j;

		if (winstart != -1) {
			/*
			 * NB: in window-sliding mode, loop assumes i > 0
			 * and/or rxa_m[0] is NULL
			 */
			KASSERT(rap->rxa_m[0] == NULL,
			    ("%s: BA window slot 0 occupied", __func__));
		}
		for (j = i+1; j < rap->rxa_wnd; j++) {
			if (rap->rxa_m[j] != NULL) {
				rap->rxa_m[j-i] = rap->rxa_m[j];
				rap->rxa_m[j] = NULL;
				if (--n == 0)
					break;
			}
		}
		KASSERT(n == 0, ("%s: lost %d frames, qframes %d off %d "
		    "BA win <%d:%d> winstart %d",
		    __func__, n, rap->rxa_qframes, i, rap->rxa_start,
		    IEEE80211_SEQ_ADD(rap->rxa_start, rap->rxa_wnd-1),
		    winstart));
		vap->iv_stats.is_ampdu_rx_copy += rap->rxa_qframes;
	}
}

/*
 * Dispatch as many frames as possible from the re-order queue.
 * Frames will always be "at the front"; we process all frames
 * up to the first empty slot in the window.  On completion we
 * cleanup state if there are still pending frames in the current
 * BA window.  We assume the frame at slot 0 is already handled
 * by the caller; we always start at slot 1.
 */
static void
ampdu_rx_dispatch(struct ieee80211_rx_ampdu *rap, struct ieee80211_node *ni)
{
	struct ieee80211vap *vap = ni->ni_vap;
	int i;

	/* flush run of frames */
	for (i = 1; i < rap->rxa_wnd; i++) {
		if (ampdu_dispatch_slot(rap, ni, i) == 0)
			break;
	}

	/*
	 * If frames remain, copy the mbuf pointers down so
	 * they correspond to the offsets in the new window.
	 */
	ampdu_rx_moveup(rap, ni, i, -1);

	/*
	 * Adjust the start of the BA window to
	 * reflect the frames just dispatched.
	 */
	rap->rxa_start = IEEE80211_SEQ_ADD(rap->rxa_start, i);
	vap->iv_stats.is_ampdu_rx_oor += i;
}

/*
 * Dispatch all frames in the A-MPDU re-order queue.
 */
static void
ampdu_rx_flush(struct ieee80211_node *ni, struct ieee80211_rx_ampdu *rap)
{
	struct ieee80211vap *vap = ni->ni_vap;
	int i, r;

	for (i = 0; i < rap->rxa_wnd; i++) {
		r = ampdu_dispatch_slot(rap, ni, i);
		if (r == 0)
			continue;
		vap->iv_stats.is_ampdu_rx_oor += r;

		if (rap->rxa_qframes == 0)
			break;
	}
}

/*
 * Dispatch all frames in the A-MPDU re-order queue
 * preceding the specified sequence number.  This logic
 * handles window moves due to a received MSDU or BAR.
 */
static void
ampdu_rx_flush_upto(struct ieee80211_node *ni,
	struct ieee80211_rx_ampdu *rap, ieee80211_seq winstart)
{
	struct ieee80211vap *vap = ni->ni_vap;
	ieee80211_seq seqno;
	int i, r;

	/*
	 * Flush any complete MSDU's with a sequence number lower
	 * than winstart.  Gaps may exist.  Note that we may actually
	 * dispatch frames past winstart if a run continues; this is
	 * an optimization that avoids having to do a separate pass
	 * to dispatch frames after moving the BA window start.
	 */
	seqno = rap->rxa_start;
	for (i = 0; i < rap->rxa_wnd; i++) {
		r = ampdu_dispatch_slot(rap, ni, i);
		if (r == 0) {
			if (!IEEE80211_SEQ_BA_BEFORE(seqno, winstart))
				break;
		}
		vap->iv_stats.is_ampdu_rx_oor += r;
		seqno = IEEE80211_SEQ_INC(seqno);
	}
	/*
	 * If frames remain, copy the mbuf pointers down so
	 * they correspond to the offsets in the new window.
	 */
	ampdu_rx_moveup(rap, ni, i, winstart);

	/*
	 * Move the start of the BA window; we use the
	 * sequence number of the last MSDU that was
	 * passed up the stack+1 or winstart if stopped on
	 * a gap in the reorder buffer.
	 */
	rap->rxa_start = seqno;
}

/*
 * Process a received QoS data frame for an HT station.  Handle
 * A-MPDU reordering: if this frame is received out of order
 * and falls within the BA window hold onto it.  Otherwise if
 * this frame completes a run, flush any pending frames.  We
 * return 1 if the frame is consumed.  A 0 is returned if
 * the frame should be processed normally by the caller.
 */
int
ieee80211_ampdu_reorder(struct ieee80211_node *ni, struct mbuf *m,
    const struct ieee80211_rx_stats *rxs)
{
#define	PROCESS		0	/* caller should process frame */
#define	CONSUMED	1	/* frame consumed, caller does nothing */
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211_qosframe *wh;
	struct ieee80211_rx_ampdu *rap;
	ieee80211_seq rxseq;
	uint8_t tid;
	int off;

	KASSERT((m->m_flags & (M_AMPDU | M_AMPDU_MPDU)) == M_AMPDU,
	    ("!a-mpdu or already re-ordered, flags 0x%x", m->m_flags));
	KASSERT(ni->ni_flags & IEEE80211_NODE_HT, ("not an HT sta"));

	/* NB: m_len known to be sufficient */
	wh = mtod(m, struct ieee80211_qosframe *);
	if (wh->i_fc[0] != IEEE80211_FC0_QOSDATA) {
		/*
		 * Not QoS data, shouldn't get here but just
		 * return it to the caller for processing.
		 */
		return PROCESS;
	}

	/*
	 * 802.11-2012 9.3.2.10 - Duplicate detection and recovery.
	 *
	 * Multicast QoS data frames are checked against a different
	 * counter, not the per-TID counter.
	 */
	if (IEEE80211_IS_MULTICAST(wh->i_addr1))
		return PROCESS;

	tid = ieee80211_getqos(wh)[0];
	tid &= IEEE80211_QOS_TID;
	rap = &ni->ni_rx_ampdu[tid];
	if ((rap->rxa_flags & IEEE80211_AGGR_XCHGPEND) == 0) {
		/*
		 * No ADDBA request yet, don't touch.
		 */
		return PROCESS;
	}
	rxseq = le16toh(*(uint16_t *)wh->i_seq);
	if ((rxseq & IEEE80211_SEQ_FRAG_MASK) != 0) {
		/*
		 * Fragments are not allowed; toss.
		 */
		IEEE80211_DISCARD_MAC(vap,
		    IEEE80211_MSG_INPUT | IEEE80211_MSG_11N, ni->ni_macaddr,
		    "A-MPDU", "fragment, rxseq 0x%x tid %u%s", rxseq, tid,
		    wh->i_fc[1] & IEEE80211_FC1_RETRY ? " (retransmit)" : "");
		vap->iv_stats.is_ampdu_rx_drop++;
		IEEE80211_NODE_STAT(ni, rx_drop);
		m_freem(m);
		return CONSUMED;
	}
	rxseq >>= IEEE80211_SEQ_SEQ_SHIFT;
	rap->rxa_nframes++;

	/*
	 * Handle waiting for the first frame to define the BAW.
	 * Some firmware doesn't provide the RX of the starting point
	 * of the BAW and we have to cope.
	 */
	if (rap->rxa_flags & IEEE80211_AGGR_WAITRX) {
		rap->rxa_flags &= ~IEEE80211_AGGR_WAITRX;
		rap->rxa_start = rxseq;
	}
again:
	if (rxseq == rap->rxa_start) {
		/*
		 * First frame in window.
		 */
		if (rap->rxa_qframes != 0) {
			/*
			 * Dispatch as many packets as we can.
			 */
			KASSERT(rap->rxa_m[0] == NULL, ("unexpected dup"));
			ampdu_dispatch(ni, m);
			ampdu_rx_dispatch(rap, ni);
			return CONSUMED;
		} else {
			/*
			 * In order; advance window and notify
			 * caller to dispatch directly.
			 */
			rap->rxa_start = IEEE80211_SEQ_INC(rxseq);
			return PROCESS;
		}
	}
	/*
	 * Frame is out of order; store if in the BA window.
	 */
	/* calculate offset in BA window */
	off = IEEE80211_SEQ_SUB(rxseq, rap->rxa_start);
	if (off < rap->rxa_wnd) {
		/*
		 * Common case (hopefully): in the BA window.
		 * Sec 9.10.7.6.2 a) (p.137)
		 */

		/* 
		 * Check for frames sitting too long in the reorder queue.
		 * This should only ever happen if frames are not delivered
		 * without the sender otherwise notifying us (e.g. with a
		 * BAR to move the window).  Typically this happens because
		 * of vendor bugs that cause the sequence number to jump.
		 * When this happens we get a gap in the reorder queue that
		 * leaves frame sitting on the queue until they get pushed
		 * out due to window moves.  When the vendor does not send
		 * BAR this move only happens due to explicit packet sends
		 *
		 * NB: we only track the time of the oldest frame in the
		 * reorder q; this means that if we flush we might push
		 * frames that still "new"; if this happens then subsequent
		 * frames will result in BA window moves which cost something
		 * but is still better than a big throughput dip.
		 */
		if (rap->rxa_qframes != 0) {
			/* XXX honor batimeout? */
			if (ticks - rap->rxa_age > ieee80211_ampdu_age) {
				/*
				 * Too long since we received the first
				 * frame; flush the reorder buffer.
				 */
				if (rap->rxa_qframes != 0) {
					vap->iv_stats.is_ampdu_rx_age +=
					    rap->rxa_qframes;
					ampdu_rx_flush(ni, rap);
				}
				rap->rxa_start = IEEE80211_SEQ_INC(rxseq);
				return PROCESS;
			}
		} else {
			/*
			 * First frame, start aging timer.
			 */
			rap->rxa_age = ticks;
		}

		/* save packet - this consumes, no matter what */
		ampdu_rx_add_slot(rap, off, tid, rxseq, ni, m);

		return CONSUMED;
	}
	if (off < IEEE80211_SEQ_BA_RANGE) {
		/*
		 * Outside the BA window, but within range;
		 * flush the reorder q and move the window.
		 * Sec 9.10.7.6.2 b) (p.138)
		 */
		IEEE80211_NOTE(vap, IEEE80211_MSG_11N, ni,
		    "move BA win <%u:%u> (%u frames) rxseq %u tid %u",
		    rap->rxa_start,
		    IEEE80211_SEQ_ADD(rap->rxa_start, rap->rxa_wnd-1),
		    rap->rxa_qframes, rxseq, tid);
		vap->iv_stats.is_ampdu_rx_move++;

		/*
		 * The spec says to flush frames up to but not including:
		 * 	WinStart_B = rxseq - rap->rxa_wnd + 1
		 * Then insert the frame or notify the caller to process
		 * it immediately.  We can safely do this by just starting
		 * over again because we know the frame will now be within
		 * the BA window.
		 */
		/* NB: rxa_wnd known to be >0 */
		ampdu_rx_flush_upto(ni, rap,
		    IEEE80211_SEQ_SUB(rxseq, rap->rxa_wnd-1));
		goto again;
	} else {
		/*
		 * Outside the BA window and out of range; toss.
		 * Sec 9.10.7.6.2 c) (p.138)
		 */
		IEEE80211_DISCARD_MAC(vap,
		    IEEE80211_MSG_INPUT | IEEE80211_MSG_11N, ni->ni_macaddr,
		    "MPDU", "BA win <%u:%u> (%u frames) rxseq %u tid %u%s",
		    rap->rxa_start,
		    IEEE80211_SEQ_ADD(rap->rxa_start, rap->rxa_wnd-1),
		    rap->rxa_qframes, rxseq, tid,
		    wh->i_fc[1] & IEEE80211_FC1_RETRY ? " (retransmit)" : "");
		vap->iv_stats.is_ampdu_rx_drop++;
		IEEE80211_NODE_STAT(ni, rx_drop);
		m_freem(m);
		return CONSUMED;
	}
#undef CONSUMED
#undef PROCESS
}

/*
 * Process a BAR ctl frame.  Dispatch all frames up to
 * the sequence number of the frame.  If this frame is
 * out of range it's discarded.
 */
void
ieee80211_recv_bar(struct ieee80211_node *ni, struct mbuf *m0)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211_frame_bar *wh;
	struct ieee80211_rx_ampdu *rap;
	ieee80211_seq rxseq;
	int tid, off;

	if (!ieee80211_recv_bar_ena) {
#if 0
		IEEE80211_DISCARD_MAC(vap, IEEE80211_MSG_11N,
		    ni->ni_macaddr, "BAR", "%s", "processing disabled");
#endif
		vap->iv_stats.is_ampdu_bar_bad++;
		return;
	}
	wh = mtod(m0, struct ieee80211_frame_bar *);
	/* XXX check basic BAR */
	tid = MS(le16toh(wh->i_ctl), IEEE80211_BAR_TID);
	rap = &ni->ni_rx_ampdu[tid];
	if ((rap->rxa_flags & IEEE80211_AGGR_XCHGPEND) == 0) {
		/*
		 * No ADDBA request yet, don't touch.
		 */
		IEEE80211_DISCARD_MAC(vap,
		    IEEE80211_MSG_INPUT | IEEE80211_MSG_11N,
		    ni->ni_macaddr, "BAR", "no BA stream, tid %u", tid);
		vap->iv_stats.is_ampdu_bar_bad++;
		return;
	}
	vap->iv_stats.is_ampdu_bar_rx++;
	rxseq = le16toh(wh->i_seq) >> IEEE80211_SEQ_SEQ_SHIFT;
	if (rxseq == rap->rxa_start)
		return;
	/* calculate offset in BA window */
	off = IEEE80211_SEQ_SUB(rxseq, rap->rxa_start);
	if (off < IEEE80211_SEQ_BA_RANGE) {
		/*
		 * Flush the reorder q up to rxseq and move the window.
		 * Sec 9.10.7.6.3 a) (p.138)
		 */
		IEEE80211_NOTE(vap, IEEE80211_MSG_11N, ni,
		    "BAR moves BA win <%u:%u> (%u frames) rxseq %u tid %u",
		    rap->rxa_start,
		    IEEE80211_SEQ_ADD(rap->rxa_start, rap->rxa_wnd-1),
		    rap->rxa_qframes, rxseq, tid);
		vap->iv_stats.is_ampdu_bar_move++;

		ampdu_rx_flush_upto(ni, rap, rxseq);
		if (off >= rap->rxa_wnd) {
			/*
			 * BAR specifies a window start to the right of BA
			 * window; we must move it explicitly since
			 * ampdu_rx_flush_upto will not.
			 */
			rap->rxa_start = rxseq;
		}
	} else {
		/*
		 * Out of range; toss.
		 * Sec 9.10.7.6.3 b) (p.138)
		 */
		IEEE80211_DISCARD_MAC(vap,
		    IEEE80211_MSG_INPUT | IEEE80211_MSG_11N, ni->ni_macaddr,
		    "BAR", "BA win <%u:%u> (%u frames) rxseq %u tid %u%s",
		    rap->rxa_start,
		    IEEE80211_SEQ_ADD(rap->rxa_start, rap->rxa_wnd-1),
		    rap->rxa_qframes, rxseq, tid,
		    wh->i_fc[1] & IEEE80211_FC1_RETRY ? " (retransmit)" : "");
		vap->iv_stats.is_ampdu_bar_oow++;
		IEEE80211_NODE_STAT(ni, rx_drop);
	}
}

/*
 * Setup HT-specific state in a node.  Called only
 * when HT use is negotiated so we don't do extra
 * work for temporary and/or legacy sta's.
 */
void
ieee80211_ht_node_init(struct ieee80211_node *ni)
{
	struct ieee80211_tx_ampdu *tap;
	int tid;

	IEEE80211_NOTE(ni->ni_vap, IEEE80211_MSG_11N,
	    ni,
	    "%s: called (%p)",
	    __func__,
	    ni);

	if (ni->ni_flags & IEEE80211_NODE_HT) {
		/*
		 * Clean AMPDU state on re-associate.  This handles the case
		 * where a station leaves w/o notifying us and then returns
		 * before node is reaped for inactivity.
		 */
		IEEE80211_NOTE(ni->ni_vap, IEEE80211_MSG_11N,
		    ni,
		    "%s: calling cleanup (%p)",
		    __func__, ni);
		ieee80211_ht_node_cleanup(ni);
	}
	for (tid = 0; tid < WME_NUM_TID; tid++) {
		tap = &ni->ni_tx_ampdu[tid];
		tap->txa_tid = tid;
		tap->txa_ni = ni;
		ieee80211_txampdu_init_pps(tap);
		/* NB: further initialization deferred */
	}
	ni->ni_flags |= IEEE80211_NODE_HT | IEEE80211_NODE_AMPDU;
}

/*
 * Cleanup HT-specific state in a node.  Called only
 * when HT use has been marked.
 */
void
ieee80211_ht_node_cleanup(struct ieee80211_node *ni)
{
	struct ieee80211com *ic = ni->ni_ic;
	int i;

	IEEE80211_NOTE(ni->ni_vap, IEEE80211_MSG_11N,
	    ni,
	    "%s: called (%p)",
	    __func__, ni);

	KASSERT(ni->ni_flags & IEEE80211_NODE_HT, ("not an HT node"));

	/* XXX optimize this */
	for (i = 0; i < WME_NUM_TID; i++) {
		struct ieee80211_tx_ampdu *tap = &ni->ni_tx_ampdu[i];
		if (tap->txa_flags & IEEE80211_AGGR_SETUP)
			ampdu_tx_stop(tap);
	}
	for (i = 0; i < WME_NUM_TID; i++)
		ic->ic_ampdu_rx_stop(ni, &ni->ni_rx_ampdu[i]);

	ni->ni_htcap = 0;
	ni->ni_flags &= ~IEEE80211_NODE_HT_ALL;
}

/*
 * Age out HT resources for a station.
 */
void
ieee80211_ht_node_age(struct ieee80211_node *ni)
{
	struct ieee80211vap *vap = ni->ni_vap;
	uint8_t tid;

	KASSERT(ni->ni_flags & IEEE80211_NODE_HT, ("not an HT sta"));

	for (tid = 0; tid < WME_NUM_TID; tid++) {
		struct ieee80211_rx_ampdu *rap;

		rap = &ni->ni_rx_ampdu[tid];
		if ((rap->rxa_flags & IEEE80211_AGGR_XCHGPEND) == 0)
			continue;
		if (rap->rxa_qframes == 0)
			continue;
		/* 
		 * Check for frames sitting too long in the reorder queue.
		 * See above for more details on what's happening here.
		 */
		/* XXX honor batimeout? */
		if (ticks - rap->rxa_age > ieee80211_ampdu_age) {
			/*
			 * Too long since we received the first
			 * frame; flush the reorder buffer.
			 */
			vap->iv_stats.is_ampdu_rx_age += rap->rxa_qframes;
			ampdu_rx_flush(ni, rap);
		}
	}
}

static struct ieee80211_channel *
findhtchan(struct ieee80211com *ic, struct ieee80211_channel *c, int htflags)
{
	return ieee80211_find_channel(ic, c->ic_freq,
	    (c->ic_flags &~ IEEE80211_CHAN_HT) | htflags);
}

/*
 * Adjust a channel to be HT/non-HT according to the vap's configuration.
 */
struct ieee80211_channel *
ieee80211_ht_adjust_channel(struct ieee80211com *ic,
	struct ieee80211_channel *chan, int flags)
{
	struct ieee80211_channel *c;

	if (flags & IEEE80211_FHT_HT) {
		/* promote to HT if possible */
		if (flags & IEEE80211_FHT_USEHT40) {
			if (!IEEE80211_IS_CHAN_HT40(chan)) {
				/* NB: arbitrarily pick ht40+ over ht40- */
				c = findhtchan(ic, chan, IEEE80211_CHAN_HT40U);
				if (c == NULL)
					c = findhtchan(ic, chan,
						IEEE80211_CHAN_HT40D);
				if (c == NULL)
					c = findhtchan(ic, chan,
						IEEE80211_CHAN_HT20);
				if (c != NULL)
					chan = c;
			}
		} else if (!IEEE80211_IS_CHAN_HT20(chan)) {
			c = findhtchan(ic, chan, IEEE80211_CHAN_HT20);
			if (c != NULL)
				chan = c;
		}
	} else if (IEEE80211_IS_CHAN_HT(chan)) {
		/* demote to legacy, HT use is disabled */
		c = ieee80211_find_channel(ic, chan->ic_freq,
		    chan->ic_flags &~ IEEE80211_CHAN_HT);
		if (c != NULL)
			chan = c;
	}
	return chan;
}

/*
 * Setup HT-specific state for a legacy WDS peer.
 */
void
ieee80211_ht_wds_init(struct ieee80211_node *ni)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211_tx_ampdu *tap;
	int tid;

	KASSERT(vap->iv_flags_ht & IEEE80211_FHT_HT, ("no HT requested"));

	/* XXX check scan cache in case peer has an ap and we have info */
	/*
	 * If setup with a legacy channel; locate an HT channel.
	 * Otherwise if the inherited channel (from a companion
	 * AP) is suitable use it so we use the same location
	 * for the extension channel).
	 */
	ni->ni_chan = ieee80211_ht_adjust_channel(ni->ni_ic,
	    ni->ni_chan, ieee80211_htchanflags(ni->ni_chan));

	ni->ni_htcap = 0;
	if (vap->iv_flags_ht & IEEE80211_FHT_SHORTGI20)
		ni->ni_htcap |= IEEE80211_HTCAP_SHORTGI20;
	if (IEEE80211_IS_CHAN_HT40(ni->ni_chan)) {
		ni->ni_htcap |= IEEE80211_HTCAP_CHWIDTH40;
		ni->ni_chw = 40;
		if (IEEE80211_IS_CHAN_HT40U(ni->ni_chan))
			ni->ni_ht2ndchan = IEEE80211_HTINFO_2NDCHAN_ABOVE;
		else if (IEEE80211_IS_CHAN_HT40D(ni->ni_chan))
			ni->ni_ht2ndchan = IEEE80211_HTINFO_2NDCHAN_BELOW;
		if (vap->iv_flags_ht & IEEE80211_FHT_SHORTGI40)
			ni->ni_htcap |= IEEE80211_HTCAP_SHORTGI40;
	} else {
		ni->ni_chw = 20;
		ni->ni_ht2ndchan = IEEE80211_HTINFO_2NDCHAN_NONE;
	}
	ni->ni_htctlchan = ni->ni_chan->ic_ieee;
	if (vap->iv_flags_ht & IEEE80211_FHT_RIFS)
		ni->ni_flags |= IEEE80211_NODE_RIFS;
	/* XXX does it make sense to enable SMPS? */

	ni->ni_htopmode = 0;		/* XXX need protection state */
	ni->ni_htstbc = 0;		/* XXX need info */

	for (tid = 0; tid < WME_NUM_TID; tid++) {
		tap = &ni->ni_tx_ampdu[tid];
		tap->txa_tid = tid;
		ieee80211_txampdu_init_pps(tap);
	}
	/* NB: AMPDU tx/rx governed by IEEE80211_FHT_AMPDU_{TX,RX} */
	ni->ni_flags |= IEEE80211_NODE_HT | IEEE80211_NODE_AMPDU;
}

/*
 * Notify hostap vaps of a change in the HTINFO ie.
 */
static void
htinfo_notify(struct ieee80211com *ic)
{
	struct ieee80211vap *vap;
	int first = 1;

	IEEE80211_LOCK_ASSERT(ic);

	TAILQ_FOREACH(vap, &ic->ic_vaps, iv_next) {
		if (vap->iv_opmode != IEEE80211_M_HOSTAP)
			continue;
		if (vap->iv_state != IEEE80211_S_RUN ||
		    !IEEE80211_IS_CHAN_HT(vap->iv_bss->ni_chan))
			continue;
		if (first) {
			IEEE80211_NOTE(vap,
			    IEEE80211_MSG_ASSOC | IEEE80211_MSG_11N,
			    vap->iv_bss,
			    "HT bss occupancy change: %d sta, %d ht, "
			    "%d ht40%s, HT protmode now 0x%x"
			    , ic->ic_sta_assoc
			    , ic->ic_ht_sta_assoc
			    , ic->ic_ht40_sta_assoc
			    , (ic->ic_flags_ht & IEEE80211_FHT_NONHT_PR) ?
				 ", non-HT sta present" : ""
			    , ic->ic_curhtprotmode);
			first = 0;
		}
		ieee80211_beacon_notify(vap, IEEE80211_BEACON_HTINFO);
	}
}

/*
 * Calculate HT protection mode from current
 * state and handle updates.
 */
static void
htinfo_update(struct ieee80211com *ic)
{
	uint8_t protmode;

	if (ic->ic_sta_assoc != ic->ic_ht_sta_assoc) {
		protmode = IEEE80211_HTINFO_OPMODE_MIXED
			 | IEEE80211_HTINFO_NONHT_PRESENT;
	} else if (ic->ic_flags_ht & IEEE80211_FHT_NONHT_PR) {
		protmode = IEEE80211_HTINFO_OPMODE_PROTOPT
			 | IEEE80211_HTINFO_NONHT_PRESENT;
	} else if (ic->ic_bsschan != IEEE80211_CHAN_ANYC &&
	    IEEE80211_IS_CHAN_HT40(ic->ic_bsschan) && 
	    ic->ic_sta_assoc != ic->ic_ht40_sta_assoc) {
		protmode = IEEE80211_HTINFO_OPMODE_HT20PR;
	} else {
		protmode = IEEE80211_HTINFO_OPMODE_PURE;
	}
	if (protmode != ic->ic_curhtprotmode) {
		ic->ic_curhtprotmode = protmode;
		htinfo_notify(ic);
	}
}

/*
 * Handle an HT station joining a BSS.
 */
void
ieee80211_ht_node_join(struct ieee80211_node *ni)
{
	struct ieee80211com *ic = ni->ni_ic;

	IEEE80211_LOCK_ASSERT(ic);

	if (ni->ni_flags & IEEE80211_NODE_HT) {
		ic->ic_ht_sta_assoc++;
		if (ni->ni_chw == 40)
			ic->ic_ht40_sta_assoc++;
	}
	htinfo_update(ic);
}

/*
 * Handle an HT station leaving a BSS.
 */
void
ieee80211_ht_node_leave(struct ieee80211_node *ni)
{
	struct ieee80211com *ic = ni->ni_ic;

	IEEE80211_LOCK_ASSERT(ic);

	if (ni->ni_flags & IEEE80211_NODE_HT) {
		ic->ic_ht_sta_assoc--;
		if (ni->ni_chw == 40)
			ic->ic_ht40_sta_assoc--;
	}
	htinfo_update(ic);
}

/*
 * Public version of htinfo_update; used for processing
 * beacon frames from overlapping bss.
 *
 * Caller can specify either IEEE80211_HTINFO_OPMODE_MIXED
 * (on receipt of a beacon that advertises MIXED) or
 * IEEE80211_HTINFO_OPMODE_PROTOPT (on receipt of a beacon
 * from an overlapping legacy bss).  We treat MIXED with
 * a higher precedence than PROTOPT (i.e. we will not change
 * change PROTOPT -> MIXED; only MIXED -> PROTOPT).  This
 * corresponds to how we handle things in htinfo_update.
 */
void
ieee80211_htprot_update(struct ieee80211com *ic, int protmode)
{
#define	OPMODE(x)	SM(x, IEEE80211_HTINFO_OPMODE)
	IEEE80211_LOCK(ic);

	/* track non-HT station presence */
	KASSERT(protmode & IEEE80211_HTINFO_NONHT_PRESENT,
	    ("protmode 0x%x", protmode));
	ic->ic_flags_ht |= IEEE80211_FHT_NONHT_PR;
	ic->ic_lastnonht = ticks;

	if (protmode != ic->ic_curhtprotmode &&
	    (OPMODE(ic->ic_curhtprotmode) != IEEE80211_HTINFO_OPMODE_MIXED ||
	     OPMODE(protmode) == IEEE80211_HTINFO_OPMODE_PROTOPT)) {
		/* push beacon update */
		ic->ic_curhtprotmode = protmode;
		htinfo_notify(ic);
	}
	IEEE80211_UNLOCK(ic);
#undef OPMODE
}

/*
 * Time out presence of an overlapping bss with non-HT
 * stations.  When operating in hostap mode we listen for
 * beacons from other stations and if we identify a non-HT
 * station is present we update the opmode field of the
 * HTINFO ie.  To identify when all non-HT stations are
 * gone we time out this condition.
 */
void
ieee80211_ht_timeout(struct ieee80211com *ic)
{
	IEEE80211_LOCK_ASSERT(ic);

	if ((ic->ic_flags_ht & IEEE80211_FHT_NONHT_PR) &&
	    ieee80211_time_after(ticks, ic->ic_lastnonht + IEEE80211_NONHT_PRESENT_AGE)) {
#if 0
		IEEE80211_NOTE(vap, IEEE80211_MSG_11N, ni,
		    "%s", "time out non-HT STA present on channel");
#endif
		ic->ic_flags_ht &= ~IEEE80211_FHT_NONHT_PR;
		htinfo_update(ic);
	}
}

/*
 * Process an 802.11n HT capabilities ie.
 */
void
ieee80211_parse_htcap(struct ieee80211_node *ni, const uint8_t *ie)
{
	if (ie[0] == IEEE80211_ELEMID_VENDOR) {
		/*
		 * Station used Vendor OUI ie to associate;
		 * mark the node so when we respond we'll use
		 * the Vendor OUI's and not the standard ie's.
		 */
		ni->ni_flags |= IEEE80211_NODE_HTCOMPAT;
		ie += 4;
	} else
		ni->ni_flags &= ~IEEE80211_NODE_HTCOMPAT;

	ni->ni_htcap = le16dec(ie +
		__offsetof(struct ieee80211_ie_htcap, hc_cap));
	ni->ni_htparam = ie[__offsetof(struct ieee80211_ie_htcap, hc_param)];
}

static void
htinfo_parse(struct ieee80211_node *ni,
	const struct ieee80211_ie_htinfo *htinfo)
{
	uint16_t w;

	ni->ni_htctlchan = htinfo->hi_ctrlchannel;
	ni->ni_ht2ndchan = SM(htinfo->hi_byte1, IEEE80211_HTINFO_2NDCHAN);
	w = le16dec(&htinfo->hi_byte2);
	ni->ni_htopmode = SM(w, IEEE80211_HTINFO_OPMODE);
	w = le16dec(&htinfo->hi_byte45);
	ni->ni_htstbc = SM(w, IEEE80211_HTINFO_BASIC_STBCMCS);
}

/*
 * Parse an 802.11n HT info ie and save useful information
 * to the node state.  Note this does not effect any state
 * changes such as for channel width change.
 */
void
ieee80211_parse_htinfo(struct ieee80211_node *ni, const uint8_t *ie)
{
	if (ie[0] == IEEE80211_ELEMID_VENDOR)
		ie += 4;
	htinfo_parse(ni, (const struct ieee80211_ie_htinfo *) ie);
}

/*
 * Handle 11n/11ac channel switch.
 *
 * Use the received HT/VHT ie's to identify the right channel to use.
 * If we cannot locate it in the channel table then fallback to
 * legacy operation.
 *
 * Note that we use this information to identify the node's
 * channel only; the caller is responsible for insuring any
 * required channel change is done (e.g. in sta mode when
 * parsing the contents of a beacon frame).
 */
static int
htinfo_update_chw(struct ieee80211_node *ni, int htflags, int vhtflags)
{
	struct ieee80211com *ic = ni->ni_ic;
	struct ieee80211_channel *c;
	int chanflags;
	int ret = 0;

	/*
	 * First step - do HT/VHT only channel lookup based on operating mode
	 * flags.  This involves masking out the VHT flags as well.
	 * Otherwise we end up doing the full channel walk each time
	 * we trigger this, which is expensive.
	 */
	chanflags = (ni->ni_chan->ic_flags &~
	    (IEEE80211_CHAN_HT | IEEE80211_CHAN_VHT)) | htflags | vhtflags;

	if (chanflags == ni->ni_chan->ic_flags)
		goto done;

	/*
	 * If HT /or/ VHT flags have changed then check both.
	 * We need to start by picking a HT channel anyway.
	 */

	c = NULL;
	chanflags = (ni->ni_chan->ic_flags &~
	    (IEEE80211_CHAN_HT | IEEE80211_CHAN_VHT)) | htflags;
	/* XXX not right for ht40- */
	c = ieee80211_find_channel(ic, ni->ni_chan->ic_freq, chanflags);
	if (c == NULL && (htflags & IEEE80211_CHAN_HT40)) {
		/*
		 * No HT40 channel entry in our table; fall back
		 * to HT20 operation.  This should not happen.
		 */
		c = findhtchan(ic, ni->ni_chan, IEEE80211_CHAN_HT20);
#if 0
		IEEE80211_NOTE(ni->ni_vap,
		    IEEE80211_MSG_ASSOC | IEEE80211_MSG_11N, ni,
		    "no HT40 channel (freq %u), falling back to HT20",
		    ni->ni_chan->ic_freq);
#endif
		/* XXX stat */
	}

	/* Nothing found - leave it alone; move onto VHT */
	if (c == NULL)
		c = ni->ni_chan;

	/*
	 * If it's non-HT, then bail out now.
	 */
	if (! IEEE80211_IS_CHAN_HT(c)) {
		IEEE80211_NOTE(ni->ni_vap,
		    IEEE80211_MSG_ASSOC | IEEE80211_MSG_11N, ni,
		    "not HT; skipping VHT check (%u/0x%x)",
		    c->ic_freq, c->ic_flags);
		goto done;
	}

	/*
	 * Next step - look at the current VHT flags and determine
	 * if we need to upgrade.  Mask out the VHT and HT flags since
	 * the vhtflags field will already have the correct HT
	 * flags to use.
	 */
	if (IEEE80211_CONF_VHT(ic) && ni->ni_vhtcap != 0 && vhtflags != 0) {
		chanflags = (c->ic_flags
		    &~ (IEEE80211_CHAN_HT | IEEE80211_CHAN_VHT))
		    | vhtflags;
		IEEE80211_NOTE(ni->ni_vap,
		    IEEE80211_MSG_ASSOC | IEEE80211_MSG_11N,
		    ni,
		    "%s: VHT; chanwidth=0x%02x; vhtflags=0x%08x",
		    __func__, ni->ni_vht_chanwidth, vhtflags);

		IEEE80211_NOTE(ni->ni_vap,
		    IEEE80211_MSG_ASSOC | IEEE80211_MSG_11N,
		    ni,
		    "%s: VHT; trying lookup for %d/0x%08x",
		    __func__, c->ic_freq, chanflags);
		c = ieee80211_find_channel(ic, c->ic_freq, chanflags);
	}

	/* Finally, if it's changed */
	if (c != NULL && c != ni->ni_chan) {
		IEEE80211_NOTE(ni->ni_vap,
		    IEEE80211_MSG_ASSOC | IEEE80211_MSG_11N, ni,
		    "switch station to %s%d channel %u/0x%x",
		    IEEE80211_IS_CHAN_VHT(c) ? "VHT" : "HT",
		    IEEE80211_IS_CHAN_VHT80(c) ? 80 :
		      (IEEE80211_IS_CHAN_HT40(c) ? 40 : 20),
		    c->ic_freq, c->ic_flags);
		ni->ni_chan = c;
		ret = 1;
	}
	/* NB: caller responsible for forcing any channel change */

done:
	/* update node's (11n) tx channel width */
	ni->ni_chw = IEEE80211_IS_CHAN_HT40(ni->ni_chan)? 40 : 20;
	return (ret);
}

/*
 * Update 11n MIMO PS state according to received htcap.
 */
static __inline int
htcap_update_mimo_ps(struct ieee80211_node *ni)
{
	uint16_t oflags = ni->ni_flags;

	switch (ni->ni_htcap & IEEE80211_HTCAP_SMPS) {
	case IEEE80211_HTCAP_SMPS_DYNAMIC:
		ni->ni_flags |= IEEE80211_NODE_MIMO_PS;
		ni->ni_flags |= IEEE80211_NODE_MIMO_RTS;
		break;
	case IEEE80211_HTCAP_SMPS_ENA:
		ni->ni_flags |= IEEE80211_NODE_MIMO_PS;
		ni->ni_flags &= ~IEEE80211_NODE_MIMO_RTS;
		break;
	case IEEE80211_HTCAP_SMPS_OFF:
	default:		/* disable on rx of reserved value */
		ni->ni_flags &= ~IEEE80211_NODE_MIMO_PS;
		ni->ni_flags &= ~IEEE80211_NODE_MIMO_RTS;
		break;
	}
	return (oflags ^ ni->ni_flags);
}

/*
 * Update short GI state according to received htcap
 * and local settings.
 */
static __inline void
htcap_update_shortgi(struct ieee80211_node *ni)
{
	struct ieee80211vap *vap = ni->ni_vap;

	ni->ni_flags &= ~(IEEE80211_NODE_SGI20|IEEE80211_NODE_SGI40);
	if ((ni->ni_htcap & IEEE80211_HTCAP_SHORTGI20) &&
	    (vap->iv_flags_ht & IEEE80211_FHT_SHORTGI20))
		ni->ni_flags |= IEEE80211_NODE_SGI20;
	if ((ni->ni_htcap & IEEE80211_HTCAP_SHORTGI40) &&
	    (vap->iv_flags_ht & IEEE80211_FHT_SHORTGI40))
		ni->ni_flags |= IEEE80211_NODE_SGI40;
}

/*
 * Update LDPC state according to received htcap
 * and local settings.
 */
static __inline void
htcap_update_ldpc(struct ieee80211_node *ni)
{
	struct ieee80211vap *vap = ni->ni_vap;

	if ((ni->ni_htcap & IEEE80211_HTCAP_LDPC) &&
	    (vap->iv_flags_ht & IEEE80211_FHT_LDPC_TX))
		ni->ni_flags |= IEEE80211_NODE_LDPC;
}

/*
 * Parse and update HT-related state extracted from
 * the HT cap and info ie's.
 *
 * This is called from the STA management path and
 * the ieee80211_node_join() path.  It will take into
 * account the IEs discovered during scanning and
 * adjust things accordingly.
 */
void
ieee80211_ht_updateparams(struct ieee80211_node *ni,
	const uint8_t *htcapie, const uint8_t *htinfoie)
{
	struct ieee80211vap *vap = ni->ni_vap;
	const struct ieee80211_ie_htinfo *htinfo;

	ieee80211_parse_htcap(ni, htcapie);
	if (vap->iv_htcaps & IEEE80211_HTC_SMPS)
		htcap_update_mimo_ps(ni);
	htcap_update_shortgi(ni);
	htcap_update_ldpc(ni);

	if (htinfoie[0] == IEEE80211_ELEMID_VENDOR)
		htinfoie += 4;
	htinfo = (const struct ieee80211_ie_htinfo *) htinfoie;
	htinfo_parse(ni, htinfo);

	/*
	 * Defer the node channel change; we need to now
	 * update VHT parameters before we do it.
	 */

	if ((htinfo->hi_byte1 & IEEE80211_HTINFO_RIFSMODE_PERM) &&
	    (vap->iv_flags_ht & IEEE80211_FHT_RIFS))
		ni->ni_flags |= IEEE80211_NODE_RIFS;
	else
		ni->ni_flags &= ~IEEE80211_NODE_RIFS;
}

static uint32_t
ieee80211_vht_get_vhtflags(struct ieee80211_node *ni, uint32_t htflags)
{
	struct ieee80211vap *vap = ni->ni_vap;
	uint32_t vhtflags = 0;

	vhtflags = 0;
	if (ni->ni_flags & IEEE80211_NODE_VHT && vap->iv_flags_vht & IEEE80211_FVHT_VHT) {
		if ((ni->ni_vht_chanwidth == IEEE80211_VHT_CHANWIDTH_160MHZ) &&
		    /* XXX 2 means "160MHz and 80+80MHz", 1 means "160MHz" */
		    (MS(vap->iv_vhtcaps,
		     IEEE80211_VHTCAP_SUPP_CHAN_WIDTH_MASK) >= 1) &&
		    (vap->iv_flags_vht & IEEE80211_FVHT_USEVHT160)) {
			vhtflags = IEEE80211_CHAN_VHT160;
			/* Mirror the HT40 flags */
			if (htflags == IEEE80211_CHAN_HT40U) {
				vhtflags |= IEEE80211_CHAN_HT40U;
			} else if (htflags == IEEE80211_CHAN_HT40D) {
				vhtflags |= IEEE80211_CHAN_HT40D;
			}
		} else if ((ni->ni_vht_chanwidth == IEEE80211_VHT_CHANWIDTH_80P80MHZ) &&
		    /* XXX 2 means "160MHz and 80+80MHz" */
		    (MS(vap->iv_vhtcaps,
		     IEEE80211_VHTCAP_SUPP_CHAN_WIDTH_MASK) == 2) &&
		    (vap->iv_flags_vht & IEEE80211_FVHT_USEVHT80P80)) {
			vhtflags = IEEE80211_CHAN_VHT80_80;
			/* Mirror the HT40 flags */
			if (htflags == IEEE80211_CHAN_HT40U) {
				vhtflags |= IEEE80211_CHAN_HT40U;
			} else if (htflags == IEEE80211_CHAN_HT40D) {
				vhtflags |= IEEE80211_CHAN_HT40D;
			}
		} else if ((ni->ni_vht_chanwidth == IEEE80211_VHT_CHANWIDTH_80MHZ) &&
		    (vap->iv_flags_vht & IEEE80211_FVHT_USEVHT80)) {
			vhtflags = IEEE80211_CHAN_VHT80;
			/* Mirror the HT40 flags */
			if (htflags == IEEE80211_CHAN_HT40U) {
				vhtflags |= IEEE80211_CHAN_HT40U;
			} else if (htflags == IEEE80211_CHAN_HT40D) {
				vhtflags |= IEEE80211_CHAN_HT40D;
			}
		} else if (ni->ni_vht_chanwidth == IEEE80211_VHT_CHANWIDTH_USE_HT) {
			/* Mirror the HT40 flags */
			/*
			 * XXX TODO: if ht40 is disabled, but vht40 isn't
			 * disabled then this logic will get very, very sad.
			 * It's quite possible the only sane thing to do is
			 * to not have vht40 as an option, and just obey
			 * 'ht40' as that flag.
			 */
			if ((htflags == IEEE80211_CHAN_HT40U) &&
			    (vap->iv_flags_vht & IEEE80211_FVHT_USEVHT40)) {
				vhtflags = IEEE80211_CHAN_VHT40U
				    | IEEE80211_CHAN_HT40U;
			} else if (htflags == IEEE80211_CHAN_HT40D &&
			    (vap->iv_flags_vht & IEEE80211_FVHT_USEVHT40)) {
				vhtflags = IEEE80211_CHAN_VHT40D
				    | IEEE80211_CHAN_HT40D;
			} else if (htflags == IEEE80211_CHAN_HT20) {
				vhtflags = IEEE80211_CHAN_VHT20
				    | IEEE80211_CHAN_HT20;
			}
		} else {
			vhtflags = IEEE80211_CHAN_VHT20;
		}
	}
	return (vhtflags);
}

/*
 * Final part of updating the HT parameters.
 *
 * This is called from the STA management path and
 * the ieee80211_node_join() path.  It will take into
 * account the IEs discovered during scanning and
 * adjust things accordingly.
 *
 * This is done after a call to ieee80211_ht_updateparams()
 * because it (and the upcoming VHT version of updateparams)
 * needs to ensure everything is parsed before htinfo_update_chw()
 * is called - which will change the channel config for the
 * node for us.
 */
int
ieee80211_ht_updateparams_final(struct ieee80211_node *ni,
	const uint8_t *htcapie, const uint8_t *htinfoie)
{
	struct ieee80211vap *vap = ni->ni_vap;
	const struct ieee80211_ie_htinfo *htinfo;
	int htflags, vhtflags;
	int ret = 0;

	htinfo = (const struct ieee80211_ie_htinfo *) htinfoie;

	htflags = (vap->iv_flags_ht & IEEE80211_FHT_HT) ?
	    IEEE80211_CHAN_HT20 : 0;

	/* NB: honor operating mode constraint */
	if ((htinfo->hi_byte1 & IEEE80211_HTINFO_TXWIDTH_2040) &&
	    (vap->iv_flags_ht & IEEE80211_FHT_USEHT40)) {
		if (ni->ni_ht2ndchan == IEEE80211_HTINFO_2NDCHAN_ABOVE)
			htflags = IEEE80211_CHAN_HT40U;
		else if (ni->ni_ht2ndchan == IEEE80211_HTINFO_2NDCHAN_BELOW)
			htflags = IEEE80211_CHAN_HT40D;
	}

	/*
	 * VHT flags - do much the same; check whether VHT is available
	 * and if so, what our ideal channel use would be based on our
	 * capabilities and the (pre-parsed) VHT info IE.
	 */
	vhtflags = ieee80211_vht_get_vhtflags(ni, htflags);

	if (htinfo_update_chw(ni, htflags, vhtflags))
		ret = 1;

	return (ret);
}

/*
 * Parse and update HT-related state extracted from the HT cap ie
 * for a station joining an HT BSS.
 *
 * This is called from the hostap path for each station.
 */
void
ieee80211_ht_updatehtcap(struct ieee80211_node *ni, const uint8_t *htcapie)
{
	struct ieee80211vap *vap = ni->ni_vap;

	ieee80211_parse_htcap(ni, htcapie);
	if (vap->iv_htcaps & IEEE80211_HTC_SMPS)
		htcap_update_mimo_ps(ni);
	htcap_update_shortgi(ni);
	htcap_update_ldpc(ni);
}

/*
 * Called once HT and VHT capabilities are parsed in hostap mode -
 * this will adjust the channel configuration of the given node
 * based on the configuration and capabilities.
 */
void
ieee80211_ht_updatehtcap_final(struct ieee80211_node *ni)
{
	struct ieee80211vap *vap = ni->ni_vap;
	int htflags;
	int vhtflags;

	/* NB: honor operating mode constraint */
	/* XXX 40 MHz intolerant */
	htflags = (vap->iv_flags_ht & IEEE80211_FHT_HT) ?
	    IEEE80211_CHAN_HT20 : 0;
	if ((ni->ni_htcap & IEEE80211_HTCAP_CHWIDTH40) &&
	    (vap->iv_flags_ht & IEEE80211_FHT_USEHT40)) {
		if (IEEE80211_IS_CHAN_HT40U(vap->iv_bss->ni_chan))
			htflags = IEEE80211_CHAN_HT40U;
		else if (IEEE80211_IS_CHAN_HT40D(vap->iv_bss->ni_chan))
			htflags = IEEE80211_CHAN_HT40D;
	}
	/*
	 * VHT flags - do much the same; check whether VHT is available
	 * and if so, what our ideal channel use would be based on our
	 * capabilities and the (pre-parsed) VHT info IE.
	 */
	vhtflags = ieee80211_vht_get_vhtflags(ni, htflags);

	(void) htinfo_update_chw(ni, htflags, vhtflags);
}

/*
 * Install received HT rate set by parsing the HT cap ie.
 */
int
ieee80211_setup_htrates(struct ieee80211_node *ni, const uint8_t *ie, int flags)
{
	struct ieee80211com *ic = ni->ni_ic;
	struct ieee80211vap *vap = ni->ni_vap;
	const struct ieee80211_ie_htcap *htcap;
	struct ieee80211_htrateset *rs;
	int i, maxequalmcs, maxunequalmcs;

	maxequalmcs = ic->ic_txstream * 8 - 1;
	maxunequalmcs = 0;
	if (ic->ic_htcaps & IEEE80211_HTC_TXUNEQUAL) {
		if (ic->ic_txstream >= 2)
			maxunequalmcs = 38;
		if (ic->ic_txstream >= 3)
			maxunequalmcs = 52;
		if (ic->ic_txstream >= 4)
			maxunequalmcs = 76;
	}

	rs = &ni->ni_htrates;
	memset(rs, 0, sizeof(*rs));
	if (ie != NULL) {
		if (ie[0] == IEEE80211_ELEMID_VENDOR)
			ie += 4;
		htcap = (const struct ieee80211_ie_htcap *) ie;
		for (i = 0; i < IEEE80211_HTRATE_MAXSIZE; i++) {
			if (isclr(htcap->hc_mcsset, i))
				continue;
			if (rs->rs_nrates == IEEE80211_HTRATE_MAXSIZE) {
				IEEE80211_NOTE(vap,
				    IEEE80211_MSG_XRATE | IEEE80211_MSG_11N, ni,
				    "WARNING, HT rate set too large; only "
				    "using %u rates", IEEE80211_HTRATE_MAXSIZE);
				vap->iv_stats.is_rx_rstoobig++;
				break;
			}
			if (i <= 31 && i > maxequalmcs)
				continue;
			if (i == 32 &&
			    (ic->ic_htcaps & IEEE80211_HTC_TXMCS32) == 0)
				continue;
			if (i > 32 && i > maxunequalmcs)
				continue;
			rs->rs_rates[rs->rs_nrates++] = i;
		}
	}
	return ieee80211_fix_rate(ni, (struct ieee80211_rateset *) rs, flags);
}

/*
 * Mark rates in a node's HT rate set as basic according
 * to the information in the supplied HT info ie.
 */
void
ieee80211_setup_basic_htrates(struct ieee80211_node *ni, const uint8_t *ie)
{
	const struct ieee80211_ie_htinfo *htinfo;
	struct ieee80211_htrateset *rs;
	int i, j;

	if (ie[0] == IEEE80211_ELEMID_VENDOR)
		ie += 4;
	htinfo = (const struct ieee80211_ie_htinfo *) ie;
	rs = &ni->ni_htrates;
	if (rs->rs_nrates == 0) {
		IEEE80211_NOTE(ni->ni_vap,
		    IEEE80211_MSG_XRATE | IEEE80211_MSG_11N, ni,
		    "%s", "WARNING, empty HT rate set");
		return;
	}
	for (i = 0; i < IEEE80211_HTRATE_MAXSIZE; i++) {
		if (isclr(htinfo->hi_basicmcsset, i))
			continue;
		for (j = 0; j < rs->rs_nrates; j++)
			if ((rs->rs_rates[j] & IEEE80211_RATE_VAL) == i)
				rs->rs_rates[j] |= IEEE80211_RATE_BASIC;
	}
}

static void
ampdu_tx_setup(struct ieee80211_tx_ampdu *tap)
{
	callout_init(&tap->txa_timer, 1);
	tap->txa_flags |= IEEE80211_AGGR_SETUP;
	tap->txa_lastsample = ticks;
}

static void
ampdu_tx_stop(struct ieee80211_tx_ampdu *tap)
{
	struct ieee80211_node *ni = tap->txa_ni;
	struct ieee80211com *ic = ni->ni_ic;

	IEEE80211_NOTE(tap->txa_ni->ni_vap, IEEE80211_MSG_11N,
	    tap->txa_ni,
	    "%s: called",
	    __func__);

	KASSERT(tap->txa_flags & IEEE80211_AGGR_SETUP,
	    ("txa_flags 0x%x tid %d ac %d", tap->txa_flags, tap->txa_tid,
	    TID_TO_WME_AC(tap->txa_tid)));

	/*
	 * Stop BA stream if setup so driver has a chance
	 * to reclaim any resources it might have allocated.
	 */
	ic->ic_addba_stop(ni, tap);
	/*
	 * Stop any pending BAR transmit.
	 */
	bar_stop_timer(tap);

	/*
	 * Reset packet estimate.
	 */
	ieee80211_txampdu_init_pps(tap);

	/* NB: clearing NAK means we may re-send ADDBA */ 
	tap->txa_flags &= ~(IEEE80211_AGGR_SETUP | IEEE80211_AGGR_NAK);
}

/*
 * ADDBA response timeout.
 *
 * If software aggregation and per-TID queue management was done here,
 * that queue would be unpaused after the ADDBA timeout occurs.
 */
static void
addba_timeout(void *arg)
{
	struct ieee80211_tx_ampdu *tap = arg;
	struct ieee80211_node *ni = tap->txa_ni;
	struct ieee80211com *ic = ni->ni_ic;

	/* XXX ? */
	tap->txa_flags &= ~IEEE80211_AGGR_XCHGPEND;
	tap->txa_attempts++;
	ic->ic_addba_response_timeout(ni, tap);
}

static void
addba_start_timeout(struct ieee80211_tx_ampdu *tap)
{
	/* XXX use CALLOUT_PENDING instead? */
	callout_reset(&tap->txa_timer, ieee80211_addba_timeout,
	    addba_timeout, tap);
	tap->txa_flags |= IEEE80211_AGGR_XCHGPEND;
	tap->txa_nextrequest = ticks + ieee80211_addba_timeout;
}

static void
addba_stop_timeout(struct ieee80211_tx_ampdu *tap)
{
	/* XXX use CALLOUT_PENDING instead? */
	if (tap->txa_flags & IEEE80211_AGGR_XCHGPEND) {
		callout_stop(&tap->txa_timer);
		tap->txa_flags &= ~IEEE80211_AGGR_XCHGPEND;
	}
}

static void
null_addba_response_timeout(struct ieee80211_node *ni,
    struct ieee80211_tx_ampdu *tap)
{
}

/*
 * Default method for requesting A-MPDU tx aggregation.
 * We setup the specified state block and start a timer
 * to wait for an ADDBA response frame.
 */
static int
ieee80211_addba_request(struct ieee80211_node *ni,
	struct ieee80211_tx_ampdu *tap,
	int dialogtoken, int baparamset, int batimeout)
{
	int bufsiz;

	/* XXX locking */
	tap->txa_token = dialogtoken;
	tap->txa_flags |= IEEE80211_AGGR_IMMEDIATE;
	bufsiz = MS(baparamset, IEEE80211_BAPS_BUFSIZ);
	tap->txa_wnd = (bufsiz == 0) ?
	    IEEE80211_AGGR_BAWMAX : min(bufsiz, IEEE80211_AGGR_BAWMAX);
	addba_start_timeout(tap);
	return 1;
}

/*
 * Called by drivers that wish to request an ADDBA session be
 * setup.  This brings it up and starts the request timer.
 */
int
ieee80211_ampdu_tx_request_ext(struct ieee80211_node *ni, int tid)
{
	struct ieee80211_tx_ampdu *tap;

	if (tid < 0 || tid > 15)
		return (0);
	tap = &ni->ni_tx_ampdu[tid];

	/* XXX locking */
	if ((tap->txa_flags & IEEE80211_AGGR_SETUP) == 0) {
		/* do deferred setup of state */
		ampdu_tx_setup(tap);
	}
	/* XXX hack for not doing proper locking */
	tap->txa_flags &= ~IEEE80211_AGGR_NAK;
	addba_start_timeout(tap);
	return (1);
}

/*
 * Called by drivers that have marked a session as active.
 */
int
ieee80211_ampdu_tx_request_active_ext(struct ieee80211_node *ni, int tid,
    int status)
{
	struct ieee80211_tx_ampdu *tap;

	if (tid < 0 || tid > 15)
		return (0);
	tap = &ni->ni_tx_ampdu[tid];

	/* XXX locking */
	addba_stop_timeout(tap);
	if (status == 1) {
		tap->txa_flags |= IEEE80211_AGGR_RUNNING;
		tap->txa_attempts = 0;
	} else {
		/* mark tid so we don't try again */
		tap->txa_flags |= IEEE80211_AGGR_NAK;
	}
	return (1);
}

/*
 * Default method for processing an A-MPDU tx aggregation
 * response.  We shutdown any pending timer and update the
 * state block according to the reply.
 */
static int
ieee80211_addba_response(struct ieee80211_node *ni,
	struct ieee80211_tx_ampdu *tap,
	int status, int baparamset, int batimeout)
{
	int bufsiz, tid;

	/* XXX locking */
	addba_stop_timeout(tap);
	if (status == IEEE80211_STATUS_SUCCESS) {
		bufsiz = MS(baparamset, IEEE80211_BAPS_BUFSIZ);
		/* XXX override our request? */
		tap->txa_wnd = (bufsiz == 0) ?
		    IEEE80211_AGGR_BAWMAX : min(bufsiz, IEEE80211_AGGR_BAWMAX);
		/* XXX AC/TID */
		tid = MS(baparamset, IEEE80211_BAPS_TID);
		tap->txa_flags |= IEEE80211_AGGR_RUNNING;
		tap->txa_attempts = 0;
	} else {
		/* mark tid so we don't try again */
		tap->txa_flags |= IEEE80211_AGGR_NAK;
	}
	return 1;
}

/*
 * Default method for stopping A-MPDU tx aggregation.
 * Any timer is cleared and we drain any pending frames.
 */
static void
ieee80211_addba_stop(struct ieee80211_node *ni, struct ieee80211_tx_ampdu *tap)
{
	/* XXX locking */
	addba_stop_timeout(tap);
	if (tap->txa_flags & IEEE80211_AGGR_RUNNING) {
		/* XXX clear aggregation queue */
		tap->txa_flags &= ~IEEE80211_AGGR_RUNNING;
	}
	tap->txa_attempts = 0;
}

/*
 * Process a received action frame using the default aggregation
 * policy.  We intercept ADDBA-related frames and use them to
 * update our aggregation state.  All other frames are passed up
 * for processing by ieee80211_recv_action.
 */
static int
ht_recv_action_ba_addba_request(struct ieee80211_node *ni,
	const struct ieee80211_frame *wh,
	const uint8_t *frm, const uint8_t *efrm)
{
	struct ieee80211com *ic = ni->ni_ic;
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211_rx_ampdu *rap;
	uint8_t dialogtoken;
	uint16_t baparamset, batimeout, baseqctl;
	uint16_t args[5];
	int tid;

	dialogtoken = frm[2];
	baparamset = le16dec(frm+3);
	batimeout = le16dec(frm+5);
	baseqctl = le16dec(frm+7);

	tid = MS(baparamset, IEEE80211_BAPS_TID);

	IEEE80211_NOTE(vap, IEEE80211_MSG_ACTION | IEEE80211_MSG_11N, ni,
	    "recv ADDBA request: dialogtoken %u baparamset 0x%x "
	    "(tid %d bufsiz %d) batimeout %d baseqctl %d:%d",
	    dialogtoken, baparamset,
	    tid, MS(baparamset, IEEE80211_BAPS_BUFSIZ),
	    batimeout,
	    MS(baseqctl, IEEE80211_BASEQ_START),
	    MS(baseqctl, IEEE80211_BASEQ_FRAG));

	rap = &ni->ni_rx_ampdu[tid];

	/* Send ADDBA response */
	args[0] = dialogtoken;
	/*
	 * NB: We ack only if the sta associated with HT and
	 * the ap is configured to do AMPDU rx (the latter
	 * violates the 11n spec and is mostly for testing).
	 */
	if ((ni->ni_flags & IEEE80211_NODE_AMPDU_RX) &&
	    (vap->iv_flags_ht & IEEE80211_FHT_AMPDU_RX)) {
		/* XXX handle ampdu_rx_start failure */
		ic->ic_ampdu_rx_start(ni, rap,
		    baparamset, batimeout, baseqctl);

		args[1] = IEEE80211_STATUS_SUCCESS;
	} else {
		IEEE80211_NOTE(vap, IEEE80211_MSG_ACTION | IEEE80211_MSG_11N,
		    ni, "reject ADDBA request: %s",
		    ni->ni_flags & IEEE80211_NODE_AMPDU_RX ?
		       "administratively disabled" :
		       "not negotiated for station");
		vap->iv_stats.is_addba_reject++;
		args[1] = IEEE80211_STATUS_UNSPECIFIED;
	}
	/* XXX honor rap flags? */
	args[2] = IEEE80211_BAPS_POLICY_IMMEDIATE
		| SM(tid, IEEE80211_BAPS_TID)
		| SM(rap->rxa_wnd, IEEE80211_BAPS_BUFSIZ)
		;
	args[3] = 0;
	args[4] = 0;
	ic->ic_send_action(ni, IEEE80211_ACTION_CAT_BA,
		IEEE80211_ACTION_BA_ADDBA_RESPONSE, args);
	return 0;
}

static int
ht_recv_action_ba_addba_response(struct ieee80211_node *ni,
	const struct ieee80211_frame *wh,
	const uint8_t *frm, const uint8_t *efrm)
{
	struct ieee80211com *ic = ni->ni_ic;
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211_tx_ampdu *tap;
	uint8_t dialogtoken, policy;
	uint16_t baparamset, batimeout, code;
	int tid, bufsiz;

	dialogtoken = frm[2];
	code = le16dec(frm+3);
	baparamset = le16dec(frm+5);
	tid = MS(baparamset, IEEE80211_BAPS_TID);
	bufsiz = MS(baparamset, IEEE80211_BAPS_BUFSIZ);
	policy = MS(baparamset, IEEE80211_BAPS_POLICY);
	batimeout = le16dec(frm+7);

	tap = &ni->ni_tx_ampdu[tid];
	if ((tap->txa_flags & IEEE80211_AGGR_XCHGPEND) == 0) {
		IEEE80211_DISCARD_MAC(vap,
		    IEEE80211_MSG_ACTION | IEEE80211_MSG_11N,
		    ni->ni_macaddr, "ADDBA response",
		    "no pending ADDBA, tid %d dialogtoken %u "
		    "code %d", tid, dialogtoken, code);
		vap->iv_stats.is_addba_norequest++;
		return 0;
	}
	if (dialogtoken != tap->txa_token) {
		IEEE80211_DISCARD_MAC(vap,
		    IEEE80211_MSG_ACTION | IEEE80211_MSG_11N,
		    ni->ni_macaddr, "ADDBA response",
		    "dialogtoken mismatch: waiting for %d, "
		    "received %d, tid %d code %d",
		    tap->txa_token, dialogtoken, tid, code);
		vap->iv_stats.is_addba_badtoken++;
		return 0;
	}
	/* NB: assumes IEEE80211_AGGR_IMMEDIATE is 1 */
	if (policy != (tap->txa_flags & IEEE80211_AGGR_IMMEDIATE)) {
		IEEE80211_DISCARD_MAC(vap,
		    IEEE80211_MSG_ACTION | IEEE80211_MSG_11N,
		    ni->ni_macaddr, "ADDBA response",
		    "policy mismatch: expecting %s, "
		    "received %s, tid %d code %d",
		    tap->txa_flags & IEEE80211_AGGR_IMMEDIATE,
		    policy, tid, code);
		vap->iv_stats.is_addba_badpolicy++;
		return 0;
	}
#if 0
	/* XXX we take MIN in ieee80211_addba_response */
	if (bufsiz > IEEE80211_AGGR_BAWMAX) {
		IEEE80211_DISCARD_MAC(vap,
		    IEEE80211_MSG_ACTION | IEEE80211_MSG_11N,
		    ni->ni_macaddr, "ADDBA response",
		    "BA window too large: max %d, "
		    "received %d, tid %d code %d",
		    bufsiz, IEEE80211_AGGR_BAWMAX, tid, code);
		vap->iv_stats.is_addba_badbawinsize++;
		return 0;
	}
#endif
	IEEE80211_NOTE(vap, IEEE80211_MSG_ACTION | IEEE80211_MSG_11N, ni,
	    "recv ADDBA response: dialogtoken %u code %d "
	    "baparamset 0x%x (tid %d bufsiz %d) batimeout %d",
	    dialogtoken, code, baparamset, tid, bufsiz,
	    batimeout);
	ic->ic_addba_response(ni, tap, code, baparamset, batimeout);
	return 0;
}

static int
ht_recv_action_ba_delba(struct ieee80211_node *ni,
	const struct ieee80211_frame *wh,
	const uint8_t *frm, const uint8_t *efrm)
{
	struct ieee80211com *ic = ni->ni_ic;
	struct ieee80211_rx_ampdu *rap;
	struct ieee80211_tx_ampdu *tap;
	uint16_t baparamset, code;
	int tid;

	baparamset = le16dec(frm+2);
	code = le16dec(frm+4);

	tid = MS(baparamset, IEEE80211_DELBAPS_TID);

	IEEE80211_NOTE(ni->ni_vap, IEEE80211_MSG_ACTION | IEEE80211_MSG_11N, ni,
	    "recv DELBA: baparamset 0x%x (tid %d initiator %d) "
	    "code %d", baparamset, tid,
	    MS(baparamset, IEEE80211_DELBAPS_INIT), code);

	if ((baparamset & IEEE80211_DELBAPS_INIT) == 0) {
		tap = &ni->ni_tx_ampdu[tid];
		ic->ic_addba_stop(ni, tap);
	} else {
		rap = &ni->ni_rx_ampdu[tid];
		ic->ic_ampdu_rx_stop(ni, rap);
	}
	return 0;
}

static int
ht_recv_action_ht_txchwidth(struct ieee80211_node *ni,
	const struct ieee80211_frame *wh,
	const uint8_t *frm, const uint8_t *efrm)
{
	int chw;

	chw = (frm[2] == IEEE80211_A_HT_TXCHWIDTH_2040) ? 40 : 20;

	IEEE80211_NOTE(ni->ni_vap, IEEE80211_MSG_ACTION | IEEE80211_MSG_11N, ni,
	    "%s: HT txchwidth, width %d%s",
	    __func__, chw, ni->ni_chw != chw ? "*" : "");
	if (chw != ni->ni_chw) {
		/* XXX does this need to change the ht40 station count? */
		ni->ni_chw = chw;
		/* XXX notify on change */
	}
	return 0;
}

static int
ht_recv_action_ht_mimopwrsave(struct ieee80211_node *ni,
	const struct ieee80211_frame *wh,
	const uint8_t *frm, const uint8_t *efrm)
{
	const struct ieee80211_action_ht_mimopowersave *mps =
	    (const struct ieee80211_action_ht_mimopowersave *) frm;

	/* XXX check iv_htcaps */
	if (mps->am_control & IEEE80211_A_HT_MIMOPWRSAVE_ENA)
		ni->ni_flags |= IEEE80211_NODE_MIMO_PS;
	else
		ni->ni_flags &= ~IEEE80211_NODE_MIMO_PS;
	if (mps->am_control & IEEE80211_A_HT_MIMOPWRSAVE_MODE)
		ni->ni_flags |= IEEE80211_NODE_MIMO_RTS;
	else
		ni->ni_flags &= ~IEEE80211_NODE_MIMO_RTS;
	/* XXX notify on change */
	IEEE80211_NOTE(ni->ni_vap, IEEE80211_MSG_ACTION | IEEE80211_MSG_11N, ni,
	    "%s: HT MIMO PS (%s%s)", __func__,
	    (ni->ni_flags & IEEE80211_NODE_MIMO_PS) ?  "on" : "off",
	    (ni->ni_flags & IEEE80211_NODE_MIMO_RTS) ?  "+rts" : ""
	);
	return 0;
}

/*
 * Transmit processing.
 */

/*
 * Check if A-MPDU should be requested/enabled for a stream.
 * We require a traffic rate above a per-AC threshold and we
 * also handle backoff from previous failed attempts.
 *
 * Drivers may override this method to bring in information
 * such as link state conditions in making the decision.
 */
static int
ieee80211_ampdu_enable(struct ieee80211_node *ni,
	struct ieee80211_tx_ampdu *tap)
{
	struct ieee80211vap *vap = ni->ni_vap;

	if (tap->txa_avgpps <
	    vap->iv_ampdu_mintraffic[TID_TO_WME_AC(tap->txa_tid)])
		return 0;
	/* XXX check rssi? */
	if (tap->txa_attempts >= ieee80211_addba_maxtries &&
	    ieee80211_time_after(ticks, tap->txa_nextrequest)) {
		/*
		 * Don't retry too often; txa_nextrequest is set
		 * to the minimum interval we'll retry after
		 * ieee80211_addba_maxtries failed attempts are made.
		 */
		return 0;
	}
	IEEE80211_NOTE(vap, IEEE80211_MSG_11N, ni,
	    "enable AMPDU on tid %d (%s), avgpps %d pkts %d attempt %d",
	    tap->txa_tid, ieee80211_wme_acnames[TID_TO_WME_AC(tap->txa_tid)],
	    tap->txa_avgpps, tap->txa_pkts, tap->txa_attempts);
	return 1;
}

/*
 * Request A-MPDU tx aggregation.  Setup local state and
 * issue an ADDBA request.  BA use will only happen after
 * the other end replies with ADDBA response.
 */
int
ieee80211_ampdu_request(struct ieee80211_node *ni,
	struct ieee80211_tx_ampdu *tap)
{
	struct ieee80211com *ic = ni->ni_ic;
	uint16_t args[5];
	int tid, dialogtoken;
	static int tokens = 0;	/* XXX */

	/* XXX locking */
	if ((tap->txa_flags & IEEE80211_AGGR_SETUP) == 0) {
		/* do deferred setup of state */
		ampdu_tx_setup(tap);
	}
	/* XXX hack for not doing proper locking */
	tap->txa_flags &= ~IEEE80211_AGGR_NAK;

	dialogtoken = (tokens+1) % 63;		/* XXX */
	tid = tap->txa_tid;

	/*
	 * XXX TODO: This is racy with any other parallel TX going on. :(
	 */
	tap->txa_start = ni->ni_txseqs[tid];

	args[0] = dialogtoken;
	args[1] = 0;	/* NB: status code not used */
	args[2]	= IEEE80211_BAPS_POLICY_IMMEDIATE
		| SM(tid, IEEE80211_BAPS_TID)
		| SM(IEEE80211_AGGR_BAWMAX, IEEE80211_BAPS_BUFSIZ)
		;
	args[3] = 0;	/* batimeout */
	/* NB: do first so there's no race against reply */
	if (!ic->ic_addba_request(ni, tap, dialogtoken, args[2], args[3])) {
		/* unable to setup state, don't make request */
		IEEE80211_NOTE(ni->ni_vap, IEEE80211_MSG_11N,
		    ni, "%s: could not setup BA stream for TID %d AC %d",
		    __func__, tap->txa_tid, TID_TO_WME_AC(tap->txa_tid));
		/* defer next try so we don't slam the driver with requests */
		tap->txa_attempts = ieee80211_addba_maxtries;
		/* NB: check in case driver wants to override */
		if (tap->txa_nextrequest <= ticks)
			tap->txa_nextrequest = ticks + ieee80211_addba_backoff;
		return 0;
	}
	tokens = dialogtoken;			/* allocate token */
	/* NB: after calling ic_addba_request so driver can set txa_start */
	args[4] = SM(tap->txa_start, IEEE80211_BASEQ_START)
		| SM(0, IEEE80211_BASEQ_FRAG)
		;
	return ic->ic_send_action(ni, IEEE80211_ACTION_CAT_BA,
		IEEE80211_ACTION_BA_ADDBA_REQUEST, args);
}

/*
 * Terminate an AMPDU tx stream.  State is reclaimed
 * and the peer notified with a DelBA Action frame.
 */
void
ieee80211_ampdu_stop(struct ieee80211_node *ni, struct ieee80211_tx_ampdu *tap,
	int reason)
{
	struct ieee80211com *ic = ni->ni_ic;
	struct ieee80211vap *vap = ni->ni_vap;
	uint16_t args[4];

	/* XXX locking */
	tap->txa_flags &= ~IEEE80211_AGGR_BARPEND;
	if (IEEE80211_AMPDU_RUNNING(tap)) {
		IEEE80211_NOTE(vap, IEEE80211_MSG_ACTION | IEEE80211_MSG_11N,
		    ni, "%s: stop BA stream for TID %d (reason: %d (%s))",
		    __func__, tap->txa_tid, reason,
		    ieee80211_reason_to_string(reason));
		vap->iv_stats.is_ampdu_stop++;

		ic->ic_addba_stop(ni, tap);
		args[0] = tap->txa_tid;
		args[1] = IEEE80211_DELBAPS_INIT;
		args[2] = reason;			/* XXX reason code */
		ic->ic_send_action(ni, IEEE80211_ACTION_CAT_BA,
			IEEE80211_ACTION_BA_DELBA, args);
	} else {
		IEEE80211_NOTE(vap, IEEE80211_MSG_ACTION | IEEE80211_MSG_11N,
		    ni, "%s: BA stream for TID %d not running "
		    "(reason: %d (%s))", __func__, tap->txa_tid, reason,
		    ieee80211_reason_to_string(reason));
		vap->iv_stats.is_ampdu_stop_failed++;
	}
}

/* XXX */
static void bar_start_timer(struct ieee80211_tx_ampdu *tap);

static void
bar_timeout(void *arg)
{
	struct ieee80211_tx_ampdu *tap = arg;
	struct ieee80211_node *ni = tap->txa_ni;

	KASSERT((tap->txa_flags & IEEE80211_AGGR_XCHGPEND) == 0,
	    ("bar/addba collision, flags 0x%x", tap->txa_flags));

	IEEE80211_NOTE(ni->ni_vap, IEEE80211_MSG_11N,
	    ni, "%s: tid %u flags 0x%x attempts %d", __func__,
	    tap->txa_tid, tap->txa_flags, tap->txa_attempts);

	/* guard against race with bar_tx_complete */
	if ((tap->txa_flags & IEEE80211_AGGR_BARPEND) == 0)
		return;
	/* XXX ? */
	if (tap->txa_attempts >= ieee80211_bar_maxtries) {
		struct ieee80211com *ic = ni->ni_ic;

		ni->ni_vap->iv_stats.is_ampdu_bar_tx_fail++;
		/*
		 * If (at least) the last BAR TX timeout was due to
		 * an ieee80211_send_bar() failures, then we need
		 * to make sure we notify the driver that a BAR
		 * TX did occur and fail.  This gives the driver
		 * a chance to undo any queue pause that may
		 * have occurred.
		 */
		ic->ic_bar_response(ni, tap, 1);
		ieee80211_ampdu_stop(ni, tap, IEEE80211_REASON_TIMEOUT);
	} else {
		ni->ni_vap->iv_stats.is_ampdu_bar_tx_retry++;
		if (ieee80211_send_bar(ni, tap, tap->txa_seqpending) != 0) {
			IEEE80211_NOTE(ni->ni_vap, IEEE80211_MSG_11N,
			    ni, "%s: failed to TX, starting timer\n",
			    __func__);
			/*
			 * If ieee80211_send_bar() fails here, the
			 * timer may have stopped and/or the pending
			 * flag may be clear.  Because of this,
			 * fake the BARPEND and reset the timer.
			 * A retransmission attempt will then occur
			 * during the next timeout.
			 */
			/* XXX locking */
			tap->txa_flags |= IEEE80211_AGGR_BARPEND;
			bar_start_timer(tap);
		}
	}
}

static void
bar_start_timer(struct ieee80211_tx_ampdu *tap)
{
	IEEE80211_NOTE(tap->txa_ni->ni_vap, IEEE80211_MSG_11N,
	    tap->txa_ni,
	    "%s: called",
	    __func__);
	callout_reset(&tap->txa_timer, ieee80211_bar_timeout, bar_timeout, tap);
}

static void
bar_stop_timer(struct ieee80211_tx_ampdu *tap)
{
	IEEE80211_NOTE(tap->txa_ni->ni_vap, IEEE80211_MSG_11N,
	    tap->txa_ni,
	    "%s: called",
	    __func__);
	callout_stop(&tap->txa_timer);
}

static void
bar_tx_complete(struct ieee80211_node *ni, void *arg, int status)
{
	struct ieee80211_tx_ampdu *tap = arg;

	IEEE80211_NOTE(ni->ni_vap, IEEE80211_MSG_11N,
	    ni, "%s: tid %u flags 0x%x pending %d status %d",
	    __func__, tap->txa_tid, tap->txa_flags,
	    callout_pending(&tap->txa_timer), status);

	ni->ni_vap->iv_stats.is_ampdu_bar_tx++;
	/* XXX locking */
	if ((tap->txa_flags & IEEE80211_AGGR_BARPEND) &&
	    callout_pending(&tap->txa_timer)) {
		struct ieee80211com *ic = ni->ni_ic;

		if (status == 0)		/* ACK'd */
			bar_stop_timer(tap);
		ic->ic_bar_response(ni, tap, status);
		/* NB: just let timer expire so we pace requests */
	}
}

static void
ieee80211_bar_response(struct ieee80211_node *ni,
	struct ieee80211_tx_ampdu *tap, int status)
{

	IEEE80211_NOTE(tap->txa_ni->ni_vap, IEEE80211_MSG_11N,
	    tap->txa_ni,
	    "%s: called",
	    __func__);
	if (status == 0) {		/* got ACK */
		IEEE80211_NOTE(ni->ni_vap, IEEE80211_MSG_11N,
		    ni, "BAR moves BA win <%u:%u> (%u frames) txseq %u tid %u",
		    tap->txa_start,
		    IEEE80211_SEQ_ADD(tap->txa_start, tap->txa_wnd-1),
		    tap->txa_qframes, tap->txa_seqpending,
		    tap->txa_tid);

		/* NB: timer already stopped in bar_tx_complete */
		tap->txa_start = tap->txa_seqpending;
		tap->txa_flags &= ~IEEE80211_AGGR_BARPEND;
	}
}

/*
 * Transmit a BAR frame to the specified node.  The
 * BAR contents are drawn from the supplied aggregation
 * state associated with the node.
 *
 * NB: we only handle immediate ACK w/ compressed bitmap.
 */
int
ieee80211_send_bar(struct ieee80211_node *ni,
	struct ieee80211_tx_ampdu *tap, ieee80211_seq seq)
{
#define	senderr(_x, _v)	do { vap->iv_stats._v++; ret = _x; goto bad; } while (0)
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211com *ic = ni->ni_ic;
	struct ieee80211_frame_bar *bar;
	struct mbuf *m;
	uint16_t barctl, barseqctl;
	uint8_t *frm;
	int tid, ret;


	IEEE80211_NOTE(tap->txa_ni->ni_vap, IEEE80211_MSG_11N,
	    tap->txa_ni,
	    "%s: called",
	    __func__);

	if ((tap->txa_flags & IEEE80211_AGGR_RUNNING) == 0) {
		/* no ADDBA response, should not happen */
		/* XXX stat+msg */
		return EINVAL;
	}
	/* XXX locking */
	bar_stop_timer(tap);

	ieee80211_ref_node(ni);

	m = ieee80211_getmgtframe(&frm, ic->ic_headroom, sizeof(*bar));
	if (m == NULL)
		senderr(ENOMEM, is_tx_nobuf);

	if (!ieee80211_add_callback(m, bar_tx_complete, tap)) {
		m_freem(m);
		senderr(ENOMEM, is_tx_nobuf);	/* XXX */
		/* NOTREACHED */
	}

	bar = mtod(m, struct ieee80211_frame_bar *);
	bar->i_fc[0] = IEEE80211_FC0_VERSION_0 |
		IEEE80211_FC0_TYPE_CTL | IEEE80211_FC0_SUBTYPE_BAR;
	bar->i_fc[1] = 0;
	IEEE80211_ADDR_COPY(bar->i_ra, ni->ni_macaddr);
	IEEE80211_ADDR_COPY(bar->i_ta, vap->iv_myaddr);

	tid = tap->txa_tid;
	barctl 	= (tap->txa_flags & IEEE80211_AGGR_IMMEDIATE ?
			0 : IEEE80211_BAR_NOACK)
		| IEEE80211_BAR_COMP
		| SM(tid, IEEE80211_BAR_TID)
		;
	barseqctl = SM(seq, IEEE80211_BAR_SEQ_START);
	/* NB: known to have proper alignment */
	bar->i_ctl = htole16(barctl);
	bar->i_seq = htole16(barseqctl);
	m->m_pkthdr.len = m->m_len = sizeof(struct ieee80211_frame_bar);

	M_WME_SETAC(m, WME_AC_VO);

	IEEE80211_NODE_STAT(ni, tx_mgmt);	/* XXX tx_ctl? */

	/* XXX locking */
	/* init/bump attempts counter */
	if ((tap->txa_flags & IEEE80211_AGGR_BARPEND) == 0)
		tap->txa_attempts = 1;
	else
		tap->txa_attempts++;
	tap->txa_seqpending = seq;
	tap->txa_flags |= IEEE80211_AGGR_BARPEND;

	IEEE80211_NOTE(vap, IEEE80211_MSG_DEBUG | IEEE80211_MSG_11N,
	    ni, "send BAR: tid %u ctl 0x%x start %u (attempt %d)",
	    tid, barctl, seq, tap->txa_attempts);

	/*
	 * ic_raw_xmit will free the node reference
	 * regardless of queue/TX success or failure.
	 */
	IEEE80211_TX_LOCK(ic);
	ret = ieee80211_raw_output(vap, ni, m, NULL);
	IEEE80211_TX_UNLOCK(ic);
	if (ret != 0) {
		IEEE80211_NOTE(vap, IEEE80211_MSG_DEBUG | IEEE80211_MSG_11N,
		    ni, "send BAR: failed: (ret = %d)\n",
		    ret);
		/* xmit failed, clear state flag */
		tap->txa_flags &= ~IEEE80211_AGGR_BARPEND;
		vap->iv_stats.is_ampdu_bar_tx_fail++;
		return ret;
	}
	/* XXX hack against tx complete happening before timer is started */
	if (tap->txa_flags & IEEE80211_AGGR_BARPEND)
		bar_start_timer(tap);
	return 0;
bad:
	IEEE80211_NOTE(tap->txa_ni->ni_vap, IEEE80211_MSG_11N,
	    tap->txa_ni,
	    "%s: bad! ret=%d",
	    __func__, ret);
	vap->iv_stats.is_ampdu_bar_tx_fail++;
	ieee80211_free_node(ni);
	return ret;
#undef senderr
}

static int
ht_action_output(struct ieee80211_node *ni, struct mbuf *m)
{
	struct ieee80211_bpf_params params;

	memset(&params, 0, sizeof(params));
	params.ibp_pri = WME_AC_VO;
	params.ibp_rate0 = ni->ni_txparms->mgmtrate;
	/* NB: we know all frames are unicast */
	params.ibp_try0 = ni->ni_txparms->maxretry;
	params.ibp_power = ni->ni_txpower;
	return ieee80211_mgmt_output(ni, m, IEEE80211_FC0_SUBTYPE_ACTION,
	     &params);
}

#define	ADDSHORT(frm, v) do {			\
	frm[0] = (v) & 0xff;			\
	frm[1] = (v) >> 8;			\
	frm += 2;				\
} while (0)

/*
 * Send an action management frame.  The arguments are stuff
 * into a frame without inspection; the caller is assumed to
 * prepare them carefully (e.g. based on the aggregation state).
 */
static int
ht_send_action_ba_addba(struct ieee80211_node *ni,
	int category, int action, void *arg0)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211com *ic = ni->ni_ic;
	uint16_t *args = arg0;
	struct mbuf *m;
	uint8_t *frm;

	IEEE80211_NOTE(vap, IEEE80211_MSG_ACTION | IEEE80211_MSG_11N, ni,
	    "send ADDBA %s: dialogtoken %d status %d "
	    "baparamset 0x%x (tid %d) batimeout 0x%x baseqctl 0x%x",
	    (action == IEEE80211_ACTION_BA_ADDBA_REQUEST) ?
		"request" : "response",
	    args[0], args[1], args[2], MS(args[2], IEEE80211_BAPS_TID),
	    args[3], args[4]);

	IEEE80211_DPRINTF(vap, IEEE80211_MSG_NODE,
	    "ieee80211_ref_node (%s:%u) %p<%s> refcnt %d\n", __func__, __LINE__,
	    ni, ether_sprintf(ni->ni_macaddr), ieee80211_node_refcnt(ni)+1);
	ieee80211_ref_node(ni);

	m = ieee80211_getmgtframe(&frm,
	    ic->ic_headroom + sizeof(struct ieee80211_frame),
	    sizeof(uint16_t)	/* action+category */
	    /* XXX may action payload */
	    + sizeof(struct ieee80211_action_ba_addbaresponse)
	);
	if (m != NULL) {
		*frm++ = category;
		*frm++ = action;
		*frm++ = args[0];		/* dialog token */
		if (action == IEEE80211_ACTION_BA_ADDBA_RESPONSE)
			ADDSHORT(frm, args[1]);	/* status code */
		ADDSHORT(frm, args[2]);		/* baparamset */
		ADDSHORT(frm, args[3]);		/* batimeout */
		if (action == IEEE80211_ACTION_BA_ADDBA_REQUEST)
			ADDSHORT(frm, args[4]);	/* baseqctl */
		m->m_pkthdr.len = m->m_len = frm - mtod(m, uint8_t *);
		return ht_action_output(ni, m);
	} else {
		vap->iv_stats.is_tx_nobuf++;
		ieee80211_free_node(ni);
		return ENOMEM;
	}
}

static int
ht_send_action_ba_delba(struct ieee80211_node *ni,
	int category, int action, void *arg0)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211com *ic = ni->ni_ic;
	uint16_t *args = arg0;
	struct mbuf *m;
	uint16_t baparamset;
	uint8_t *frm;

	baparamset = SM(args[0], IEEE80211_DELBAPS_TID)
		   | args[1]
		   ;
	IEEE80211_NOTE(vap, IEEE80211_MSG_ACTION | IEEE80211_MSG_11N, ni,
	    "send DELBA action: tid %d, initiator %d reason %d (%s)",
	    args[0], args[1], args[2], ieee80211_reason_to_string(args[2]));

	IEEE80211_DPRINTF(vap, IEEE80211_MSG_NODE,
	    "ieee80211_ref_node (%s:%u) %p<%s> refcnt %d\n", __func__, __LINE__,
	    ni, ether_sprintf(ni->ni_macaddr), ieee80211_node_refcnt(ni)+1);
	ieee80211_ref_node(ni);

	m = ieee80211_getmgtframe(&frm,
	    ic->ic_headroom + sizeof(struct ieee80211_frame),
	    sizeof(uint16_t)	/* action+category */
	    /* XXX may action payload */
	    + sizeof(struct ieee80211_action_ba_addbaresponse)
	);
	if (m != NULL) {
		*frm++ = category;
		*frm++ = action;
		ADDSHORT(frm, baparamset);
		ADDSHORT(frm, args[2]);		/* reason code */
		m->m_pkthdr.len = m->m_len = frm - mtod(m, uint8_t *);
		return ht_action_output(ni, m);
	} else {
		vap->iv_stats.is_tx_nobuf++;
		ieee80211_free_node(ni);
		return ENOMEM;
	}
}

static int
ht_send_action_ht_txchwidth(struct ieee80211_node *ni,
	int category, int action, void *arg0)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211com *ic = ni->ni_ic;
	struct mbuf *m;
	uint8_t *frm;

	IEEE80211_NOTE(vap, IEEE80211_MSG_ACTION | IEEE80211_MSG_11N, ni,
	    "send HT txchwidth: width %d",
	    IEEE80211_IS_CHAN_HT40(ni->ni_chan) ? 40 : 20);

	IEEE80211_DPRINTF(vap, IEEE80211_MSG_NODE,
	    "ieee80211_ref_node (%s:%u) %p<%s> refcnt %d\n", __func__, __LINE__,
	    ni, ether_sprintf(ni->ni_macaddr), ieee80211_node_refcnt(ni)+1);
	ieee80211_ref_node(ni);

	m = ieee80211_getmgtframe(&frm,
	    ic->ic_headroom + sizeof(struct ieee80211_frame),
	    sizeof(uint16_t)	/* action+category */
	    /* XXX may action payload */
	    + sizeof(struct ieee80211_action_ba_addbaresponse)
	);
	if (m != NULL) {
		*frm++ = category;
		*frm++ = action;
		*frm++ = IEEE80211_IS_CHAN_HT40(ni->ni_chan) ? 
			IEEE80211_A_HT_TXCHWIDTH_2040 :
			IEEE80211_A_HT_TXCHWIDTH_20;
		m->m_pkthdr.len = m->m_len = frm - mtod(m, uint8_t *);
		return ht_action_output(ni, m);
	} else {
		vap->iv_stats.is_tx_nobuf++;
		ieee80211_free_node(ni);
		return ENOMEM;
	}
}
#undef ADDSHORT

/*
 * Construct the MCS bit mask for inclusion in an HT capabilities
 * information element.
 */
static void
ieee80211_set_mcsset(struct ieee80211com *ic, uint8_t *frm)
{
	int i;
	uint8_t txparams;

	KASSERT((ic->ic_rxstream > 0 && ic->ic_rxstream <= 4),
	    ("ic_rxstream %d out of range", ic->ic_rxstream));
	KASSERT((ic->ic_txstream > 0 && ic->ic_txstream <= 4),
	    ("ic_txstream %d out of range", ic->ic_txstream));

	for (i = 0; i < ic->ic_rxstream * 8; i++)
		setbit(frm, i);
	if ((ic->ic_htcaps & IEEE80211_HTCAP_CHWIDTH40) &&
	    (ic->ic_htcaps & IEEE80211_HTC_RXMCS32))
		setbit(frm, 32);
	if (ic->ic_htcaps & IEEE80211_HTC_RXUNEQUAL) {
		if (ic->ic_rxstream >= 2) {
			for (i = 33; i <= 38; i++)
				setbit(frm, i);
		}
		if (ic->ic_rxstream >= 3) {
			for (i = 39; i <= 52; i++)
				setbit(frm, i);
		}
		if (ic->ic_txstream >= 4) {
			for (i = 53; i <= 76; i++)
				setbit(frm, i);
		}
	}

	if (ic->ic_rxstream != ic->ic_txstream) {
		txparams = 0x1;			/* TX MCS set defined */
		txparams |= 0x2;		/* TX RX MCS not equal */
		txparams |= (ic->ic_txstream - 1) << 2;	/* num TX streams */
		if (ic->ic_htcaps & IEEE80211_HTC_TXUNEQUAL)
			txparams |= 0x16;	/* TX unequal modulation sup */
	} else
		txparams = 0;
	frm[12] = txparams;
}

/*
 * Add body of an HTCAP information element.
 */
static uint8_t *
ieee80211_add_htcap_body(uint8_t *frm, struct ieee80211_node *ni)
{
#define	ADDSHORT(frm, v) do {			\
	frm[0] = (v) & 0xff;			\
	frm[1] = (v) >> 8;			\
	frm += 2;				\
} while (0)
	struct ieee80211com *ic = ni->ni_ic;
	struct ieee80211vap *vap = ni->ni_vap;
	uint16_t caps, extcaps;
	int rxmax, density;

	/* HT capabilities */
	caps = vap->iv_htcaps & 0xffff;
	/*
	 * Note channel width depends on whether we are operating as
	 * a sta or not.  When operating as a sta we are generating
	 * a request based on our desired configuration.  Otherwise
	 * we are operational and the channel attributes identify
	 * how we've been setup (which might be different if a fixed
	 * channel is specified).
	 */
	if (vap->iv_opmode == IEEE80211_M_STA) {
		/* override 20/40 use based on config */
		if (vap->iv_flags_ht & IEEE80211_FHT_USEHT40)
			caps |= IEEE80211_HTCAP_CHWIDTH40;
		else
			caps &= ~IEEE80211_HTCAP_CHWIDTH40;

		/* Start by using the advertised settings */
		rxmax = MS(ni->ni_htparam, IEEE80211_HTCAP_MAXRXAMPDU);
		density = MS(ni->ni_htparam, IEEE80211_HTCAP_MPDUDENSITY);

		IEEE80211_DPRINTF(vap, IEEE80211_MSG_11N,
		    "%s: advertised rxmax=%d, density=%d, vap rxmax=%d, density=%d\n",
		    __func__,
		    rxmax,
		    density,
		    vap->iv_ampdu_rxmax,
		    vap->iv_ampdu_density);

		/* Cap at VAP rxmax */
		if (rxmax > vap->iv_ampdu_rxmax)
			rxmax = vap->iv_ampdu_rxmax;

		/*
		 * If the VAP ampdu density value greater, use that.
		 *
		 * (Larger density value == larger minimum gap between A-MPDU
		 * subframes.)
		 */
		if (vap->iv_ampdu_density > density)
			density = vap->iv_ampdu_density;

		/*
		 * NB: Hardware might support HT40 on some but not all
		 * channels. We can't determine this earlier because only
		 * after association the channel is upgraded to HT based
		 * on the negotiated capabilities.
		 */
		if (ni->ni_chan != IEEE80211_CHAN_ANYC &&
		    findhtchan(ic, ni->ni_chan, IEEE80211_CHAN_HT40U) == NULL &&
		    findhtchan(ic, ni->ni_chan, IEEE80211_CHAN_HT40D) == NULL)
			caps &= ~IEEE80211_HTCAP_CHWIDTH40;
	} else {
		/* override 20/40 use based on current channel */
		if (IEEE80211_IS_CHAN_HT40(ni->ni_chan))
			caps |= IEEE80211_HTCAP_CHWIDTH40;
		else
			caps &= ~IEEE80211_HTCAP_CHWIDTH40;

		/* XXX TODO should it start by using advertised settings? */
		rxmax = vap->iv_ampdu_rxmax;
		density = vap->iv_ampdu_density;
	}

	/* adjust short GI based on channel and config */
	if ((vap->iv_flags_ht & IEEE80211_FHT_SHORTGI20) == 0)
		caps &= ~IEEE80211_HTCAP_SHORTGI20;
	if ((vap->iv_flags_ht & IEEE80211_FHT_SHORTGI40) == 0 ||
	    (caps & IEEE80211_HTCAP_CHWIDTH40) == 0)
		caps &= ~IEEE80211_HTCAP_SHORTGI40;

	/* adjust STBC based on receive capabilities */
	if ((vap->iv_flags_ht & IEEE80211_FHT_STBC_RX) == 0)
		caps &= ~IEEE80211_HTCAP_RXSTBC;

	/* adjust LDPC based on receive capabilites */
	if ((vap->iv_flags_ht & IEEE80211_FHT_LDPC_RX) == 0)
		caps &= ~IEEE80211_HTCAP_LDPC;

	ADDSHORT(frm, caps);

	/* HT parameters */
	*frm = SM(rxmax, IEEE80211_HTCAP_MAXRXAMPDU)
	     | SM(density, IEEE80211_HTCAP_MPDUDENSITY)
	     ;
	frm++;

	/* pre-zero remainder of ie */
	memset(frm, 0, sizeof(struct ieee80211_ie_htcap) - 
		__offsetof(struct ieee80211_ie_htcap, hc_mcsset));

	/* supported MCS set */
	/*
	 * XXX: For sta mode the rate set should be restricted based
	 * on the AP's capabilities, but ni_htrates isn't setup when
	 * we're called to form an AssocReq frame so for now we're
	 * restricted to the device capabilities.
	 */
	ieee80211_set_mcsset(ni->ni_ic, frm);

	frm += __offsetof(struct ieee80211_ie_htcap, hc_extcap) -
		__offsetof(struct ieee80211_ie_htcap, hc_mcsset);

	/* HT extended capabilities */
	extcaps = vap->iv_htextcaps & 0xffff;

	ADDSHORT(frm, extcaps);

	frm += sizeof(struct ieee80211_ie_htcap) -
		__offsetof(struct ieee80211_ie_htcap, hc_txbf);

	return frm;
#undef ADDSHORT
}

/*
 * Add 802.11n HT capabilities information element
 */
uint8_t *
ieee80211_add_htcap(uint8_t *frm, struct ieee80211_node *ni)
{
	frm[0] = IEEE80211_ELEMID_HTCAP;
	frm[1] = sizeof(struct ieee80211_ie_htcap) - 2;
	return ieee80211_add_htcap_body(frm + 2, ni);
}

/*
 * Non-associated probe request - add HT capabilities based on
 * the current channel configuration.
 */
static uint8_t *
ieee80211_add_htcap_body_ch(uint8_t *frm, struct ieee80211vap *vap,
    struct ieee80211_channel *c)
{
#define	ADDSHORT(frm, v) do {			\
	frm[0] = (v) & 0xff;			\
	frm[1] = (v) >> 8;			\
	frm += 2;				\
} while (0)
	struct ieee80211com *ic = vap->iv_ic;
	uint16_t caps, extcaps;
	int rxmax, density;

	/* HT capabilities */
	caps = vap->iv_htcaps & 0xffff;

	/*
	 * We don't use this in STA mode; only in IBSS mode.
	 * So in IBSS mode we base our HTCAP flags on the
	 * given channel.
	 */

	/* override 20/40 use based on current channel */
	if (IEEE80211_IS_CHAN_HT40(c))
		caps |= IEEE80211_HTCAP_CHWIDTH40;
	else
		caps &= ~IEEE80211_HTCAP_CHWIDTH40;

	/* Use the currently configured values */
	rxmax = vap->iv_ampdu_rxmax;
	density = vap->iv_ampdu_density;

	/* adjust short GI based on channel and config */
	if ((vap->iv_flags_ht & IEEE80211_FHT_SHORTGI20) == 0)
		caps &= ~IEEE80211_HTCAP_SHORTGI20;
	if ((vap->iv_flags_ht & IEEE80211_FHT_SHORTGI40) == 0 ||
	    (caps & IEEE80211_HTCAP_CHWIDTH40) == 0)
		caps &= ~IEEE80211_HTCAP_SHORTGI40;
	ADDSHORT(frm, caps);

	/* HT parameters */
	*frm = SM(rxmax, IEEE80211_HTCAP_MAXRXAMPDU)
	     | SM(density, IEEE80211_HTCAP_MPDUDENSITY)
	     ;
	frm++;

	/* pre-zero remainder of ie */
	memset(frm, 0, sizeof(struct ieee80211_ie_htcap) - 
		__offsetof(struct ieee80211_ie_htcap, hc_mcsset));

	/* supported MCS set */
	/*
	 * XXX: For sta mode the rate set should be restricted based
	 * on the AP's capabilities, but ni_htrates isn't setup when
	 * we're called to form an AssocReq frame so for now we're
	 * restricted to the device capabilities.
	 */
	ieee80211_set_mcsset(ic, frm);

	frm += __offsetof(struct ieee80211_ie_htcap, hc_extcap) -
		__offsetof(struct ieee80211_ie_htcap, hc_mcsset);

	/* HT extended capabilities */
	extcaps = vap->iv_htextcaps & 0xffff;

	ADDSHORT(frm, extcaps);

	frm += sizeof(struct ieee80211_ie_htcap) -
		__offsetof(struct ieee80211_ie_htcap, hc_txbf);

	return frm;
#undef ADDSHORT
}

/*
 * Add 802.11n HT capabilities information element
 */
uint8_t *
ieee80211_add_htcap_ch(uint8_t *frm, struct ieee80211vap *vap,
    struct ieee80211_channel *c)
{
	frm[0] = IEEE80211_ELEMID_HTCAP;
	frm[1] = sizeof(struct ieee80211_ie_htcap) - 2;
	return ieee80211_add_htcap_body_ch(frm + 2, vap, c);
}

/*
 * Add Broadcom OUI wrapped standard HTCAP ie; this is
 * used for compatibility w/ pre-draft implementations.
 */
uint8_t *
ieee80211_add_htcap_vendor(uint8_t *frm, struct ieee80211_node *ni)
{
	frm[0] = IEEE80211_ELEMID_VENDOR;
	frm[1] = 4 + sizeof(struct ieee80211_ie_htcap) - 2;
	frm[2] = (BCM_OUI >> 0) & 0xff;
	frm[3] = (BCM_OUI >> 8) & 0xff;
	frm[4] = (BCM_OUI >> 16) & 0xff;
	frm[5] = BCM_OUI_HTCAP;
	return ieee80211_add_htcap_body(frm + 6, ni);
}

/*
 * Construct the MCS bit mask of basic rates
 * for inclusion in an HT information element.
 */
static void
ieee80211_set_basic_htrates(uint8_t *frm, const struct ieee80211_htrateset *rs)
{
	int i;

	for (i = 0; i < rs->rs_nrates; i++) {
		int r = rs->rs_rates[i] & IEEE80211_RATE_VAL;
		if ((rs->rs_rates[i] & IEEE80211_RATE_BASIC) &&
		    r < IEEE80211_HTRATE_MAXSIZE) {
			/* NB: this assumes a particular implementation */
			setbit(frm, r);
		}
	}
}

/*
 * Update the HTINFO ie for a beacon frame.
 */
void
ieee80211_ht_update_beacon(struct ieee80211vap *vap,
	struct ieee80211_beacon_offsets *bo)
{
#define	PROTMODE	(IEEE80211_HTINFO_OPMODE|IEEE80211_HTINFO_NONHT_PRESENT)
	struct ieee80211_node *ni;
	const struct ieee80211_channel *bsschan;
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_ie_htinfo *ht =
	   (struct ieee80211_ie_htinfo *) bo->bo_htinfo;

	ni = ieee80211_ref_node(vap->iv_bss);
	bsschan = ni->ni_chan;

	/* XXX only update on channel change */
	ht->hi_ctrlchannel = ieee80211_chan2ieee(ic, bsschan);
	if (vap->iv_flags_ht & IEEE80211_FHT_RIFS)
		ht->hi_byte1 = IEEE80211_HTINFO_RIFSMODE_PERM;
	else
		ht->hi_byte1 = IEEE80211_HTINFO_RIFSMODE_PROH;
	if (IEEE80211_IS_CHAN_HT40U(bsschan))
		ht->hi_byte1 |= IEEE80211_HTINFO_2NDCHAN_ABOVE;
	else if (IEEE80211_IS_CHAN_HT40D(bsschan))
		ht->hi_byte1 |= IEEE80211_HTINFO_2NDCHAN_BELOW;
	else
		ht->hi_byte1 |= IEEE80211_HTINFO_2NDCHAN_NONE;
	if (IEEE80211_IS_CHAN_HT40(bsschan))
		ht->hi_byte1 |= IEEE80211_HTINFO_TXWIDTH_2040;

	/* protection mode */
	ht->hi_byte2 = (ht->hi_byte2 &~ PROTMODE) | ic->ic_curhtprotmode;

	ieee80211_free_node(ni);

	/* XXX propagate to vendor ie's */
#undef PROTMODE
}

/*
 * Add body of an HTINFO information element.
 *
 * NB: We don't use struct ieee80211_ie_htinfo because we can
 * be called to fillin both a standard ie and a compat ie that
 * has a vendor OUI at the front.
 */
static uint8_t *
ieee80211_add_htinfo_body(uint8_t *frm, struct ieee80211_node *ni)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211com *ic = ni->ni_ic;

	/* pre-zero remainder of ie */
	memset(frm, 0, sizeof(struct ieee80211_ie_htinfo) - 2);

	/* primary/control channel center */
	*frm++ = ieee80211_chan2ieee(ic, ni->ni_chan);

	if (vap->iv_flags_ht & IEEE80211_FHT_RIFS)
		frm[0] = IEEE80211_HTINFO_RIFSMODE_PERM;
	else
		frm[0] = IEEE80211_HTINFO_RIFSMODE_PROH;
	if (IEEE80211_IS_CHAN_HT40U(ni->ni_chan))
		frm[0] |= IEEE80211_HTINFO_2NDCHAN_ABOVE;
	else if (IEEE80211_IS_CHAN_HT40D(ni->ni_chan))
		frm[0] |= IEEE80211_HTINFO_2NDCHAN_BELOW;
	else
		frm[0] |= IEEE80211_HTINFO_2NDCHAN_NONE;
	if (IEEE80211_IS_CHAN_HT40(ni->ni_chan))
		frm[0] |= IEEE80211_HTINFO_TXWIDTH_2040;

	frm[1] = ic->ic_curhtprotmode;

	frm += 5;

	/* basic MCS set */
	ieee80211_set_basic_htrates(frm, &ni->ni_htrates);
	frm += sizeof(struct ieee80211_ie_htinfo) -
		__offsetof(struct ieee80211_ie_htinfo, hi_basicmcsset);
	return frm;
}

/*
 * Add 802.11n HT information element.
 */
uint8_t *
ieee80211_add_htinfo(uint8_t *frm, struct ieee80211_node *ni)
{
	frm[0] = IEEE80211_ELEMID_HTINFO;
	frm[1] = sizeof(struct ieee80211_ie_htinfo) - 2;
	return ieee80211_add_htinfo_body(frm + 2, ni);
}

/*
 * Add Broadcom OUI wrapped standard HTINFO ie; this is
 * used for compatibility w/ pre-draft implementations.
 */
uint8_t *
ieee80211_add_htinfo_vendor(uint8_t *frm, struct ieee80211_node *ni)
{
	frm[0] = IEEE80211_ELEMID_VENDOR;
	frm[1] = 4 + sizeof(struct ieee80211_ie_htinfo) - 2;
	frm[2] = (BCM_OUI >> 0) & 0xff;
	frm[3] = (BCM_OUI >> 8) & 0xff;
	frm[4] = (BCM_OUI >> 16) & 0xff;
	frm[5] = BCM_OUI_HTINFO;
	return ieee80211_add_htinfo_body(frm + 6, ni);
}
