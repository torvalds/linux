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
#include <dev/rtwn/if_rtwn_tx.h>

#include <dev/rtwn/rtl8192c/r92c.h>
#include <dev/rtwn/rtl8192c/r92c_var.h>
#include <dev/rtwn/rtl8192c/r92c_tx_desc.h>


static int
r92c_tx_get_sco(struct rtwn_softc *sc, struct ieee80211_channel *c)
{
	if (IEEE80211_IS_CHAN_HT40U(c))
		return (R92C_TXDW4_SCO_SCA);
	else
		return (R92C_TXDW4_SCO_SCB);
}

static void
r92c_tx_set_ht40(struct rtwn_softc *sc, void *buf, struct ieee80211_node *ni)
{
	struct r92c_tx_desc *txd = (struct r92c_tx_desc *)buf;

	if (ni->ni_chan != IEEE80211_CHAN_ANYC &&
	    IEEE80211_IS_CHAN_HT40(ni->ni_chan)) {
		int extc_offset;

		extc_offset = r92c_tx_get_sco(sc, ni->ni_chan);
		txd->txdw4 |= htole32(R92C_TXDW4_DATA_BW40);
		txd->txdw4 |= htole32(SM(R92C_TXDW4_DATA_SCO, extc_offset));
	}
}

static void
r92c_tx_protection(struct rtwn_softc *sc, struct r92c_tx_desc *txd,
    enum ieee80211_protmode mode, uint8_t ridx)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint8_t rate;

	switch (mode) {
	case IEEE80211_PROT_CTSONLY:
		txd->txdw4 |= htole32(R92C_TXDW4_CTS2SELF);
		break;
	case IEEE80211_PROT_RTSCTS:
		txd->txdw4 |= htole32(R92C_TXDW4_RTSEN);
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

		txd->txdw4 |= htole32(SM(R92C_TXDW4_RTSRATE, ridx));
		/* RTS rate fallback limit (max). */
		txd->txdw5 |= htole32(SM(R92C_TXDW5_RTSRATE_FB_LMT, 0xf));

		if (RTWN_RATE_IS_CCK(ridx) && ridx != RTWN_RIDX_CCK1 &&
		    (ic->ic_flags & IEEE80211_F_SHPREAMBLE))
			txd->txdw4 |= htole32(R92C_TXDW4_RTS_SHORT);
	}
}

static void
r92c_tx_raid(struct rtwn_softc *sc, struct r92c_tx_desc *txd,
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
		case IEEE80211_MODE_11B:
		case IEEE80211_MODE_11G:
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
	case IEEE80211_MODE_11B:
		raid = R92C_RAID_11B;
		break;
	case IEEE80211_MODE_11G:
		if (vap->iv_flags & IEEE80211_F_PUREG)
			raid = R92C_RAID_11G;
		else
			raid = R92C_RAID_11BG;
		break;
	case IEEE80211_MODE_11NG:
		if (vap->iv_flags_ht & IEEE80211_FHT_PUREN)
			raid = R92C_RAID_11N;
		else
			raid = R92C_RAID_11BGN;
		break;
	default:
		device_printf(sc->sc_dev, "unknown mode(2) %d!\n", mode);
		return;
	}

	txd->txdw1 |= htole32(SM(R92C_TXDW1_RAID, raid));
}

/* XXX move to device-independent layer */
static void
r92c_tx_set_sgi(struct rtwn_softc *sc, void *buf, struct ieee80211_node *ni)
{
	struct r92c_tx_desc *txd = (struct r92c_tx_desc *)buf;
	struct ieee80211vap *vap = ni->ni_vap;

	if ((vap->iv_flags_ht & IEEE80211_FHT_SHORTGI20) &&	/* HT20 */
	    (ni->ni_htcap & IEEE80211_HTCAP_SHORTGI20))
		txd->txdw5 |= htole32(R92C_TXDW5_SGI);
	else if (ni->ni_chan != IEEE80211_CHAN_ANYC &&		/* HT40 */
	    IEEE80211_IS_CHAN_HT40(ni->ni_chan) &&
	    (ni->ni_htcap & IEEE80211_HTCAP_SHORTGI40) &&
	    (vap->iv_flags_ht & IEEE80211_FHT_SHORTGI40))
		txd->txdw5 |= htole32(R92C_TXDW5_SGI);
}

void
r92c_tx_enable_ampdu(void *buf, int enable)
{
	struct r92c_tx_desc *txd = (struct r92c_tx_desc *)buf;

	if (enable)
		txd->txdw1 |= htole32(R92C_TXDW1_AGGEN);
	else
		txd->txdw1 |= htole32(R92C_TXDW1_AGGBK);
}

void
r92c_tx_setup_hwseq(void *buf)
{
	struct r92c_tx_desc *txd = (struct r92c_tx_desc *)buf;

	txd->txdw4 |= htole32(R92C_TXDW4_HWSEQ_EN);
}

void
r92c_tx_setup_macid(void *buf, int id)
{
	struct r92c_tx_desc *txd = (struct r92c_tx_desc *)buf;

	txd->txdw1 |= htole32(SM(R92C_TXDW1_MACID, id));

	/* XXX does not belong here */
	/* XXX temporary (I hope) */
	/* Force CCK1 for RTS / CTS frames (driver bug) */
	txd->txdw4 &= ~htole32(SM(R92C_TXDW4_RTSRATE, R92C_TXDW4_RTSRATE_M));
	txd->txdw4 &= ~htole32(R92C_TXDW4_RTS_SHORT);
}

void
r92c_fill_tx_desc(struct rtwn_softc *sc, struct ieee80211_node *ni,
    struct mbuf *m, void *buf, uint8_t ridx, int maxretry)
{
#ifndef RTWN_WITHOUT_UCODE
	struct r92c_softc *rs = sc->sc_priv;
#endif
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = ni->ni_vap;
	struct rtwn_vap *uvp = RTWN_VAP(vap);
	struct ieee80211_frame *wh;
	struct r92c_tx_desc *txd;
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
	txd = (struct r92c_tx_desc *)buf;
	txd->flags0 |= R92C_FLAGS0_LSG | R92C_FLAGS0_FSG;
	if (ismcast)
		txd->flags0 |= R92C_FLAGS0_BMCAST;

	if (!ismcast) {
		/* Unicast frame, check if an ACK is expected. */
		if (!qos || (qos & IEEE80211_QOS_ACKPOLICY) !=
		    IEEE80211_QOS_ACKPOLICY_NOACK) {
			txd->txdw5 |= htole32(R92C_TXDW5_RTY_LMT_ENA);
			txd->txdw5 |= htole32(SM(R92C_TXDW5_RTY_LMT,
			    maxretry));
		}

		struct rtwn_node *un = RTWN_NODE(ni);
		macid = un->id;

		if (type == IEEE80211_FC0_TYPE_DATA) {
			qsel = tid % RTWN_MAX_TID;

			rtwn_r92c_tx_enable_ampdu(sc, buf,
			    (m->m_flags & M_AMPDU_MPDU) != 0);
			if (m->m_flags & M_AMPDU_MPDU) {
				txd->txdw2 |= htole32(SM(R92C_TXDW2_AMPDU_DEN,
				    vap->iv_ampdu_density));
				txd->txdw6 |= htole32(SM(R92C_TXDW6_MAX_AGG,
				    0x1f));	/* XXX */
			}
			if (sc->sc_ratectl == RTWN_RATECTL_NET80211) {
				txd->txdw2 |= htole32(R92C_TXDW2_CCX_RPT);
				sc->sc_tx_n_active++;
#ifndef RTWN_WITHOUT_UCODE
				rs->rs_c2h_pending++;
#endif
			}

			if (RTWN_RATE_IS_CCK(ridx) && ridx != RTWN_RIDX_CCK1 &&
			    (ic->ic_flags & IEEE80211_F_SHPREAMBLE))
				txd->txdw4 |= htole32(R92C_TXDW4_DATA_SHPRE);

			prot = IEEE80211_PROT_NONE;
			if (ridx >= RTWN_RIDX_HT_MCS(0)) {
				r92c_tx_set_ht40(sc, txd, ni);
				r92c_tx_set_sgi(sc, txd, ni);
				prot = ic->ic_htprotmode;
			} else if (ic->ic_flags & IEEE80211_F_USEPROT)
				prot = ic->ic_protmode;

			/* XXX fix last comparison for A-MSDU (in net80211) */
			/* XXX A-MPDU? */
			if (m->m_pkthdr.len + IEEE80211_CRC_LEN >
			    vap->iv_rtsthreshold &&
			    vap->iv_rtsthreshold != IEEE80211_RTS_MAX)
				prot = IEEE80211_PROT_RTSCTS;

			/* NB: checks for ht40 / short bits (set above). */
			if (prot != IEEE80211_PROT_NONE)
				r92c_tx_protection(sc, txd, prot, ridx);
		} else	/* IEEE80211_FC0_TYPE_MGT */
			qsel = R92C_TXDW1_QSEL_MGNT;
	} else {
		macid = RTWN_MACID_BC;
		qsel = R92C_TXDW1_QSEL_MGNT;
	}

	txd->txdw1 |= htole32(SM(R92C_TXDW1_QSEL, qsel));

	rtwn_r92c_tx_setup_macid(sc, txd, macid);
	txd->txdw5 |= htole32(SM(R92C_TXDW5_DATARATE, ridx));
	/* Data rate fallback limit (max). */
	txd->txdw5 |= htole32(SM(R92C_TXDW5_DATARATE_FB_LMT, 0x1f));
	txd->txdw4 |= htole32(SM(R92C_TXDW4_PORT_ID, uvp->id));
	r92c_tx_raid(sc, txd, ni, ismcast);

	/* Force this rate if needed. */
	if (sc->sc_ratectl != RTWN_RATECTL_FW)
		txd->txdw4 |= htole32(R92C_TXDW4_DRVRATE);

	if (!hasqos) {
		/* Use HW sequence numbering for non-QoS frames. */
		rtwn_r92c_tx_setup_hwseq(sc, txd);
		txd->txdw4 |= htole32(SM(R92C_TXDW4_SEQ_SEL, uvp->id));
	} else {
		uint16_t seqno;

		if (m->m_flags & M_AMPDU_MPDU) {
			seqno = ni->ni_txseqs[tid];
			ni->ni_txseqs[tid]++;
		} else
			seqno = M_SEQNO_GET(m) % IEEE80211_SEQ_RANGE;

		/* Set sequence number. */
		txd->txdseq = htole16(seqno);
	}
}

void
r92c_fill_tx_desc_raw(struct rtwn_softc *sc, struct ieee80211_node *ni,
    struct mbuf *m, void *buf, const struct ieee80211_bpf_params *params)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct rtwn_vap *uvp = RTWN_VAP(vap);
	struct ieee80211_frame *wh;
	struct r92c_tx_desc *txd;
	uint8_t ridx;
	int ismcast;

	/* XXX TODO: 11n checks, matching r92c_fill_tx_desc() */

	wh = mtod(m, struct ieee80211_frame *);
	ismcast = IEEE80211_IS_MULTICAST(wh->i_addr1);
	ridx = rate2ridx(params->ibp_rate0);

	/* Fill Tx descriptor. */
	txd = (struct r92c_tx_desc *)buf;
	txd->flags0 |= R92C_FLAGS0_LSG | R92C_FLAGS0_FSG;
	if (ismcast)
		txd->flags0 |= R92C_FLAGS0_BMCAST;

	if ((params->ibp_flags & IEEE80211_BPF_NOACK) == 0) {
		txd->txdw5 |= htole32(R92C_TXDW5_RTY_LMT_ENA);
		txd->txdw5 |= htole32(SM(R92C_TXDW5_RTY_LMT,
		    params->ibp_try0));
	}
	if (params->ibp_flags & IEEE80211_BPF_RTS)
		r92c_tx_protection(sc, txd, IEEE80211_PROT_RTSCTS, ridx);
	if (params->ibp_flags & IEEE80211_BPF_CTS)
		r92c_tx_protection(sc, txd, IEEE80211_PROT_CTSONLY, ridx);

	rtwn_r92c_tx_setup_macid(sc, txd, RTWN_MACID_BC);
	txd->txdw1 |= htole32(SM(R92C_TXDW1_QSEL, R92C_TXDW1_QSEL_MGNT));

	/* Set TX rate index. */
	txd->txdw5 |= htole32(SM(R92C_TXDW5_DATARATE, ridx));
	txd->txdw5 |= htole32(SM(R92C_TXDW5_DATARATE_FB_LMT, 0x1f));
	txd->txdw4 |= htole32(SM(R92C_TXDW4_PORT_ID, uvp->id));
	txd->txdw4 |= htole32(R92C_TXDW4_DRVRATE);
	r92c_tx_raid(sc, txd, ni, ismcast);

	if (!IEEE80211_QOS_HAS_SEQ(wh)) {
		/* Use HW sequence numbering for non-QoS frames. */
		rtwn_r92c_tx_setup_hwseq(sc, txd);
		txd->txdw4 |= htole32(SM(R92C_TXDW4_SEQ_SEL, uvp->id));
	} else {
		/* Set sequence number. */
		txd->txdseq |= htole16(M_SEQNO_GET(m) % IEEE80211_SEQ_RANGE);
	}
}

void
r92c_fill_tx_desc_null(struct rtwn_softc *sc, void *buf, int is11b,
    int qos, int id)
{
	struct r92c_tx_desc *txd = (struct r92c_tx_desc *)buf;

	txd->flags0 = R92C_FLAGS0_FSG | R92C_FLAGS0_LSG | R92C_FLAGS0_OWN;
	txd->txdw1 = htole32(
	    SM(R92C_TXDW1_QSEL, R92C_TXDW1_QSEL_MGNT));

	txd->txdw4 = htole32(R92C_TXDW4_DRVRATE);
	txd->txdw4 |= htole32(SM(R92C_TXDW4_PORT_ID, id));
	if (is11b) {
		txd->txdw5 = htole32(SM(R92C_TXDW5_DATARATE,
		    RTWN_RIDX_CCK1));
	} else {
		txd->txdw5 = htole32(SM(R92C_TXDW5_DATARATE,
		    RTWN_RIDX_OFDM6));
	}

	if (!qos) {
		rtwn_r92c_tx_setup_hwseq(sc, txd);
		txd->txdw4 |= htole32(SM(R92C_TXDW4_SEQ_SEL, id));
	}
}

uint8_t
r92c_tx_radiotap_flags(const void *buf)
{
	const struct r92c_tx_desc *txd = buf;
	uint8_t flags;

	flags = 0;
	if (txd->txdw4 & htole32(R92C_TXDW4_DATA_SHPRE))
		flags |= IEEE80211_RADIOTAP_F_SHORTPRE;
	if (txd->txdw5 & htole32(R92C_TXDW5_SGI))
		flags |= IEEE80211_RADIOTAP_F_SHORTGI;
	return (flags);
}
