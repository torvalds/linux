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

#include <dev/rtwn/if_rtwnreg.h>
#include <dev/rtwn/if_rtwnvar.h>

#include <dev/rtwn/if_rtwn_ridx.h>

#include <dev/rtwn/rtl8812a/r12a.h>
#include <dev/rtwn/rtl8812a/r12a_tx_desc.h>


static int
r12a_get_primary_channel(struct rtwn_softc *sc, struct ieee80211_channel *c)
{
	/* XXX 80 MHz */
	if (IEEE80211_IS_CHAN_HT40U(c))
		return (R12A_TXDW5_PRIM_CHAN_20_80_2);
	else
		return (R12A_TXDW5_PRIM_CHAN_20_80_3);
}

static void
r12a_tx_set_ht40(struct rtwn_softc *sc, void *buf, struct ieee80211_node *ni)
{
	struct r12a_tx_desc *txd = (struct r12a_tx_desc *)buf;

	/* XXX 80 Mhz */
	if (ni->ni_chan != IEEE80211_CHAN_ANYC &&
	    IEEE80211_IS_CHAN_HT40(ni->ni_chan)) {
		int prim_chan;

		prim_chan = r12a_get_primary_channel(sc, ni->ni_chan);
		txd->txdw5 |= htole32(SM(R12A_TXDW5_DATA_BW,
		    R12A_TXDW5_DATA_BW40));
		txd->txdw5 |= htole32(SM(R12A_TXDW5_DATA_PRIM_CHAN,
		    prim_chan));
	}
}

static void
r12a_tx_protection(struct rtwn_softc *sc, struct r12a_tx_desc *txd,
    enum ieee80211_protmode mode, uint8_t ridx)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint8_t rate;

	switch (mode) {
	case IEEE80211_PROT_CTSONLY:
		txd->txdw3 |= htole32(R12A_TXDW3_CTS2SELF);
		break;
	case IEEE80211_PROT_RTSCTS:
		txd->txdw3 |= htole32(R12A_TXDW3_RTSEN);
		break;
	default:
		break;
	}

	if (mode == IEEE80211_PROT_CTSONLY ||
	    mode == IEEE80211_PROT_RTSCTS) {
		if (ridx >= RTWN_RIDX_HT_MCS(0))
			rate = rtwn_ctl_mcsrate(ic->ic_rt, ridx);
		else
			rate = ieee80211_ctl_rate(ic->ic_rt, ridx2rate[ridx]);
		ridx = rate2ridx(IEEE80211_RV(rate));

		txd->txdw4 |= htole32(SM(R12A_TXDW4_RTSRATE, ridx));
		/* RTS rate fallback limit (max). */
		txd->txdw4 |= htole32(SM(R12A_TXDW4_RTSRATE_FB_LMT, 0xf));

		if (RTWN_RATE_IS_CCK(ridx) && ridx != RTWN_RIDX_CCK1 &&
		    (ic->ic_flags & IEEE80211_F_SHPREAMBLE))
			txd->txdw5 |= htole32(R12A_TXDW5_RTS_SHORT);
	}
}

static void
r12a_tx_raid(struct rtwn_softc *sc, struct r12a_tx_desc *txd,
    struct ieee80211_node *ni, int ismcast)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211_channel *chan;
	enum ieee80211_phymode mode;
	uint8_t raid;

	chan = (ni->ni_chan != IEEE80211_CHAN_ANYC) ?
		ni->ni_chan : ic->ic_curchan;
	mode = ieee80211_chan2mode(chan);

	/* NB: group addressed frames are done at 11bg rates for now */
	if (ismcast || !(ni->ni_flags & IEEE80211_NODE_HT)) {
		switch (mode) {
		case IEEE80211_MODE_11A:
		case IEEE80211_MODE_11B:
		case IEEE80211_MODE_11G:
			break;
		case IEEE80211_MODE_11NA:
			mode = IEEE80211_MODE_11A;
			break;
		case IEEE80211_MODE_11NG:
			mode = IEEE80211_MODE_11G;
			break;
		default:
			device_printf(sc->sc_dev, "unknown mode(1) %d!\n",
			    ic->ic_curmode);
			return;
		}
	}

	switch (mode) {
	case IEEE80211_MODE_11A:
		raid = R12A_RAID_11G;
		break;
	case IEEE80211_MODE_11B:
		raid = R12A_RAID_11B;
		break;
	case IEEE80211_MODE_11G:
		if (vap->iv_flags & IEEE80211_F_PUREG)
			raid = R12A_RAID_11G;
		else
			raid = R12A_RAID_11BG;
		break;
	case IEEE80211_MODE_11NA:
		if (sc->ntxchains == 1)
			raid = R12A_RAID_11GN_1;
		else
			raid = R12A_RAID_11GN_2;
		break;
	case IEEE80211_MODE_11NG:
		if (sc->ntxchains == 1) {
			if (IEEE80211_IS_CHAN_HT40(chan))
				raid = R12A_RAID_11BGN_1_40;
			else
				raid = R12A_RAID_11BGN_1;
		} else {
			if (IEEE80211_IS_CHAN_HT40(chan))
				raid = R12A_RAID_11BGN_2_40;
			else
				raid = R12A_RAID_11BGN_2;
		}
		break;
	default:
		/* TODO: 80 MHz / 11ac */
		device_printf(sc->sc_dev, "unknown mode(2) %d!\n", mode);
		return;
	}

	txd->txdw1 |= htole32(SM(R12A_TXDW1_RAID, raid));
}

static void
r12a_tx_set_sgi(struct rtwn_softc *sc, void *buf, struct ieee80211_node *ni)
{
	struct r12a_tx_desc *txd = (struct r12a_tx_desc *)buf;
	struct ieee80211vap *vap = ni->ni_vap;

	if ((vap->iv_flags_ht & IEEE80211_FHT_SHORTGI20) &&	/* HT20 */
	    (ni->ni_htcap & IEEE80211_HTCAP_SHORTGI20))
		txd->txdw5 |= htole32(R12A_TXDW5_DATA_SHORT);
	else if (ni->ni_chan != IEEE80211_CHAN_ANYC &&		/* HT40 */
	    IEEE80211_IS_CHAN_HT40(ni->ni_chan) &&
	    (ni->ni_htcap & IEEE80211_HTCAP_SHORTGI40) &&
	    (vap->iv_flags_ht & IEEE80211_FHT_SHORTGI40))
		txd->txdw5 |= htole32(R12A_TXDW5_DATA_SHORT);
}

static void
r12a_tx_set_ldpc(struct rtwn_softc *sc, struct r12a_tx_desc *txd,
    struct ieee80211_node *ni)
{
	struct ieee80211vap *vap = ni->ni_vap;

	if ((vap->iv_flags_ht & IEEE80211_FHT_LDPC_TX) &&
	    (ni->ni_htcap & IEEE80211_HTCAP_LDPC))
		txd->txdw5 |= htole32(R12A_TXDW5_DATA_LDPC);
}

void
r12a_fill_tx_desc(struct rtwn_softc *sc, struct ieee80211_node *ni,
    struct mbuf *m, void *buf, uint8_t ridx, int maxretry)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = ni->ni_vap;
	struct rtwn_vap *uvp = RTWN_VAP(vap);
	struct ieee80211_frame *wh;
	struct r12a_tx_desc *txd;
	enum ieee80211_protmode prot;
	uint8_t type, tid, qos, qsel;
	int hasqos, ismcast, macid;

	wh = mtod(m, struct ieee80211_frame *);
	type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;
	hasqos = IEEE80211_QOS_HAS_SEQ(wh);
	ismcast = IEEE80211_IS_MULTICAST(wh->i_addr1);

	/* Select TX ring for this frame. */
	if (hasqos) {
		qos = ((const struct ieee80211_qosframe *)wh)->i_qos[0];
		tid = qos & IEEE80211_QOS_TID;
	} else {
		qos = 0;
		tid = 0;
	}

	/* Fill Tx descriptor. */
	txd = (struct r12a_tx_desc *)buf;
	txd->flags0 |= R12A_FLAGS0_LSG | R12A_FLAGS0_FSG;
	if (ismcast)
		txd->flags0 |= R12A_FLAGS0_BMCAST;

	if (!ismcast) {
		/* Unicast frame, check if an ACK is expected. */
		if (!qos || (qos & IEEE80211_QOS_ACKPOLICY) !=
		    IEEE80211_QOS_ACKPOLICY_NOACK) {
			txd->txdw4 = htole32(R12A_TXDW4_RETRY_LMT_ENA);
			txd->txdw4 |= htole32(SM(R12A_TXDW4_RETRY_LMT,
			    maxretry));
		}

		struct rtwn_node *un = RTWN_NODE(ni);
		macid = un->id;

		if (type == IEEE80211_FC0_TYPE_DATA) {
			qsel = tid % RTWN_MAX_TID;

			if (m->m_flags & M_AMPDU_MPDU) {
				txd->txdw2 |= htole32(R12A_TXDW2_AGGEN);
				txd->txdw2 |= htole32(SM(R12A_TXDW2_AMPDU_DEN,
				    vap->iv_ampdu_density));
				txd->txdw3 |= htole32(SM(R12A_TXDW3_MAX_AGG,
				    0x1f));	/* XXX */
			} else
				txd->txdw2 |= htole32(R12A_TXDW2_AGGBK);

			if (sc->sc_ratectl == RTWN_RATECTL_NET80211) {
				txd->txdw2 |= htole32(R12A_TXDW2_SPE_RPT);
				sc->sc_tx_n_active++;
			}

			if (RTWN_RATE_IS_CCK(ridx) && ridx != RTWN_RIDX_CCK1 &&
			    (ic->ic_flags & IEEE80211_F_SHPREAMBLE))
				txd->txdw5 |= htole32(R12A_TXDW5_DATA_SHORT);

			prot = IEEE80211_PROT_NONE;
			if (ridx >= RTWN_RIDX_HT_MCS(0)) {
				r12a_tx_set_ht40(sc, txd, ni);
				r12a_tx_set_sgi(sc, txd, ni);
				r12a_tx_set_ldpc(sc, txd, ni);
				prot = ic->ic_htprotmode;
			} else if (ic->ic_flags & IEEE80211_F_USEPROT)
				prot = ic->ic_protmode;

			/* XXX fix last comparison for A-MSDU (in net80211) */
			/* XXX A-MPDU? */
			if (m->m_pkthdr.len + IEEE80211_CRC_LEN >
			    vap->iv_rtsthreshold &&
			    vap->iv_rtsthreshold != IEEE80211_RTS_MAX)
				prot = IEEE80211_PROT_RTSCTS;

			if (prot != IEEE80211_PROT_NONE)
				r12a_tx_protection(sc, txd, prot, ridx);
		} else	/* IEEE80211_FC0_TYPE_MGT */
			qsel = R12A_TXDW1_QSEL_MGNT;
	} else {
		macid = RTWN_MACID_BC;
		qsel = R12A_TXDW1_QSEL_MGNT;
	}

	txd->txdw1 |= htole32(SM(R12A_TXDW1_QSEL, qsel));
	txd->txdw1 |= htole32(SM(R12A_TXDW1_MACID, macid));
	txd->txdw4 |= htole32(SM(R12A_TXDW4_DATARATE, ridx));
	/* Data rate fallback limit (max). */
	txd->txdw4 |= htole32(SM(R12A_TXDW4_DATARATE_FB_LMT, 0x1f));
	/* XXX recheck for non-21au */
	txd->txdw6 |= htole32(SM(R21A_TXDW6_MBSSID, uvp->id));
	r12a_tx_raid(sc, txd, ni, ismcast);

	/* Force this rate if needed. */
	if (sc->sc_ratectl != RTWN_RATECTL_FW)
		txd->txdw3 |= htole32(R12A_TXDW3_DRVRATE);

	if (!hasqos) {
		/* Use HW sequence numbering for non-QoS frames. */
		txd->txdw8 |= htole32(R12A_TXDW8_HWSEQ_EN);
		txd->txdw3 |= htole32(SM(R12A_TXDW3_SEQ_SEL, uvp->id));
	} else {
		uint16_t seqno;

		if (m->m_flags & M_AMPDU_MPDU) {
			seqno = ni->ni_txseqs[tid];
			ni->ni_txseqs[tid]++;
		} else
			seqno = M_SEQNO_GET(m) % IEEE80211_SEQ_RANGE;

		/* Set sequence number. */
		txd->txdw9 |= htole32(SM(R12A_TXDW9_SEQ, seqno));
	}
}

void
r12a_fill_tx_desc_raw(struct rtwn_softc *sc, struct ieee80211_node *ni,
    struct mbuf *m, void *buf, const struct ieee80211_bpf_params *params)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct rtwn_vap *uvp = RTWN_VAP(vap);
	struct ieee80211_frame *wh;
	struct r12a_tx_desc *txd;
	uint8_t ridx;
	int ismcast;

	/* XXX TODO: 11n checks, matching rtwn_fill_tx_desc() */

	wh = mtod(m, struct ieee80211_frame *);
	ismcast = IEEE80211_IS_MULTICAST(wh->i_addr1);
	ridx = rate2ridx(params->ibp_rate0);

	/* Fill Tx descriptor. */
	txd = (struct r12a_tx_desc *)buf;
	txd->flags0 |= R12A_FLAGS0_LSG | R12A_FLAGS0_FSG;
	if (ismcast)
		txd->flags0 |= R12A_FLAGS0_BMCAST;

	if ((params->ibp_flags & IEEE80211_BPF_NOACK) == 0) {
		txd->txdw4 = htole32(R12A_TXDW4_RETRY_LMT_ENA);
		txd->txdw4 |= htole32(SM(R12A_TXDW4_RETRY_LMT,
		    params->ibp_try0));
	}
	if (params->ibp_flags & IEEE80211_BPF_RTS)
		r12a_tx_protection(sc, txd, IEEE80211_PROT_RTSCTS, ridx);
	if (params->ibp_flags & IEEE80211_BPF_CTS)
		r12a_tx_protection(sc, txd, IEEE80211_PROT_CTSONLY, ridx);

	txd->txdw1 |= htole32(SM(R12A_TXDW1_MACID, RTWN_MACID_BC));
	txd->txdw1 |= htole32(SM(R12A_TXDW1_QSEL, R12A_TXDW1_QSEL_MGNT));

	/* Set TX rate index. */
	txd->txdw4 |= htole32(SM(R12A_TXDW4_DATARATE, ridx));
	txd->txdw4 |= htole32(SM(R12A_TXDW4_DATARATE_FB_LMT, 0x1f));
	txd->txdw6 |= htole32(SM(R21A_TXDW6_MBSSID, uvp->id));
	txd->txdw3 |= htole32(R12A_TXDW3_DRVRATE);
	r12a_tx_raid(sc, txd, ni, ismcast);

	if (!IEEE80211_QOS_HAS_SEQ(wh)) {
		/* Use HW sequence numbering for non-QoS frames. */
		txd->txdw8 |= htole32(R12A_TXDW8_HWSEQ_EN);
		txd->txdw3 |= htole32(SM(R12A_TXDW3_SEQ_SEL, uvp->id));
	} else {
		/* Set sequence number. */
		txd->txdw9 |= htole32(SM(R12A_TXDW9_SEQ,
		    M_SEQNO_GET(m) % IEEE80211_SEQ_RANGE));
	}
}

void
r12a_fill_tx_desc_null(struct rtwn_softc *sc, void *buf, int is11b, int qos,
    int id)
{
	struct r12a_tx_desc *txd = (struct r12a_tx_desc *)buf;

	txd->flags0 = R12A_FLAGS0_FSG | R12A_FLAGS0_LSG | R12A_FLAGS0_OWN;
	txd->txdw1 = htole32(
	    SM(R12A_TXDW1_QSEL, R12A_TXDW1_QSEL_MGNT));

	txd->txdw3 = htole32(R12A_TXDW3_DRVRATE);
	txd->txdw6 = htole32(SM(R21A_TXDW6_MBSSID, id));
	if (is11b) {
		txd->txdw4 = htole32(SM(R12A_TXDW4_DATARATE,
		    RTWN_RIDX_CCK1));
	} else {
		txd->txdw4 = htole32(SM(R12A_TXDW4_DATARATE,
		    RTWN_RIDX_OFDM6));
	}

	if (!qos) {
		txd->txdw8 = htole32(R12A_TXDW8_HWSEQ_EN);
		txd->txdw3 |= htole32(SM(R12A_TXDW3_SEQ_SEL, id));

	}
}

uint8_t
r12a_tx_radiotap_flags(const void *buf)
{
	const struct r12a_tx_desc *txd = buf;
	uint8_t flags, rate;

	if (!(txd->txdw5 & htole32(R12A_TXDW5_DATA_SHORT)))
		return (0);

	rate = MS(le32toh(txd->txdw4), R12A_TXDW4_DATARATE);
	if (RTWN_RATE_IS_CCK(rate))
		flags = IEEE80211_RADIOTAP_F_SHORTPRE;
	else
		flags = IEEE80211_RADIOTAP_F_SHORTGI;
	return (flags);
}
