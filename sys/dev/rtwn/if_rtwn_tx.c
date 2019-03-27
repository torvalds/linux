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
#include <net/if_media.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_radiotap.h>
#include <net80211/ieee80211_ratectl.h>
#ifdef	IEEE80211_SUPPORT_SUPERG
#include <net80211/ieee80211_superg.h>
#endif

#include <dev/rtwn/if_rtwnreg.h>
#include <dev/rtwn/if_rtwnvar.h>

#include <dev/rtwn/if_rtwn_beacon.h>
#include <dev/rtwn/if_rtwn_debug.h>
#include <dev/rtwn/if_rtwn_ridx.h>
#include <dev/rtwn/if_rtwn_tx.h>


void
rtwn_drain_mbufq(struct rtwn_softc *sc)
{
	struct mbuf *m;
	struct ieee80211_node *ni;
	RTWN_ASSERT_LOCKED(sc);
	while ((m = mbufq_dequeue(&sc->sc_snd)) != NULL) {
		ni = (struct ieee80211_node *)m->m_pkthdr.rcvif;
		m->m_pkthdr.rcvif = NULL;
		ieee80211_free_node(ni);
		m_freem(m);
	}
}

#ifdef IEEE80211_SUPPORT_SUPERG
void
rtwn_ff_flush_all(struct rtwn_softc *sc, union sec_param *data)
{
	struct ieee80211com *ic = &sc->sc_ic;

	RTWN_UNLOCK(sc);
	ieee80211_ff_flush_all(ic);
	RTWN_LOCK(sc);
}
#endif

static uint8_t
rtwn_get_cipher(u_int ic_cipher)
{
	uint8_t cipher;

	switch (ic_cipher) {
	case IEEE80211_CIPHER_NONE:
		cipher = RTWN_TXDW1_CIPHER_NONE;
		break;
	case IEEE80211_CIPHER_WEP:
	case IEEE80211_CIPHER_TKIP:
		cipher = RTWN_TXDW1_CIPHER_RC4;
		break;
	case IEEE80211_CIPHER_AES_CCM:
		cipher = RTWN_TXDW1_CIPHER_AES;
		break;
	default:
		KASSERT(0, ("%s: unknown cipher %d\n", __func__,
		    ic_cipher));
		return (RTWN_TXDW1_CIPHER_SM4);
	}

	return (cipher);
}

static int
rtwn_tx_data(struct rtwn_softc *sc, struct ieee80211_node *ni,
    struct mbuf *m)
{
	const struct ieee80211_txparam *tp = ni->ni_txparms;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211_key *k = NULL;
	struct ieee80211_frame *wh;
	struct rtwn_tx_desc_common *txd;
	struct rtwn_tx_buf buf;
	uint8_t rate, ridx, type;
	u_int cipher;
	int ismcast;

	RTWN_ASSERT_LOCKED(sc);

	wh = mtod(m, struct ieee80211_frame *);
	type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;
	ismcast = IEEE80211_IS_MULTICAST(wh->i_addr1);

	/* Choose a TX rate index. */
	if (type == IEEE80211_FC0_TYPE_MGT ||
	    type == IEEE80211_FC0_TYPE_CTL ||
	    (m->m_flags & M_EAPOL) != 0)
		rate = tp->mgmtrate;
	else if (ismcast)
		rate = tp->mcastrate;
	else if (tp->ucastrate != IEEE80211_FIXED_RATE_NONE)
		rate = tp->ucastrate;
	else {
		if (sc->sc_ratectl == RTWN_RATECTL_NET80211) {
			/* XXX pass pktlen */
			(void) ieee80211_ratectl_rate(ni, NULL, 0);
			rate = ni->ni_txrate;
		} else {
			if (ni->ni_flags & IEEE80211_NODE_HT)
				rate = IEEE80211_RATE_MCS | 0x4; /* MCS4 */
			else if (ic->ic_curmode != IEEE80211_MODE_11B)
				rate = ridx2rate[RTWN_RIDX_OFDM36];
			else
				rate = ridx2rate[RTWN_RIDX_CCK55];
		}
	}

	ridx = rate2ridx(rate);

	cipher = IEEE80211_CIPHER_NONE;
	if (wh->i_fc[1] & IEEE80211_FC1_PROTECTED) {
		k = ieee80211_crypto_encap(ni, m);
		if (k == NULL) {
			device_printf(sc->sc_dev,
			    "ieee80211_crypto_encap returns NULL.\n");
			return (ENOBUFS);
		}
		if (!(k->wk_flags & IEEE80211_KEY_SWCRYPT))
			cipher = k->wk_cipher->ic_cipher;

		/* in case packet header moved, reset pointer */
		wh = mtod(m, struct ieee80211_frame *);
	}

	/* Fill Tx descriptor. */
	txd = (struct rtwn_tx_desc_common *)&buf;
	memset(txd, 0, sc->txdesc_len);
	txd->txdw1 = htole32(SM(RTWN_TXDW1_CIPHER, rtwn_get_cipher(cipher)));

	rtwn_fill_tx_desc(sc, ni, m, txd, ridx, tp->maxretry);

	if (ieee80211_radiotap_active_vap(vap)) {
		struct rtwn_tx_radiotap_header *tap = &sc->sc_txtap;

		tap->wt_flags = rtwn_tx_radiotap_flags(sc, txd);
		if (k != NULL)
			tap->wt_flags |= IEEE80211_RADIOTAP_F_WEP;
		ieee80211_radiotap_tx(vap, m);
	}

	return (rtwn_tx_start(sc, ni, m, (uint8_t *)txd, type, 0));
}

static int
rtwn_tx_raw(struct rtwn_softc *sc, struct ieee80211_node *ni,
    struct mbuf *m, const struct ieee80211_bpf_params *params)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211_key *k = NULL;
	struct ieee80211_frame *wh;
	struct rtwn_tx_desc_common *txd;
	struct rtwn_tx_buf buf;
	uint8_t type;
	u_int cipher;

	/* Encrypt the frame if need be. */
	cipher = IEEE80211_CIPHER_NONE;
	if (params->ibp_flags & IEEE80211_BPF_CRYPTO) {
		/* Retrieve key for TX. */
		k = ieee80211_crypto_encap(ni, m);
		if (k == NULL) {
			device_printf(sc->sc_dev,
			    "ieee80211_crypto_encap returns NULL.\n");
			return (ENOBUFS);
		}
		if (!(k->wk_flags & IEEE80211_KEY_SWCRYPT))
			cipher = k->wk_cipher->ic_cipher;
	}

	wh = mtod(m, struct ieee80211_frame *);
	type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;

	/* Fill Tx descriptor. */
	txd = (struct rtwn_tx_desc_common *)&buf;
	memset(txd, 0, sc->txdesc_len);
	txd->txdw1 = htole32(SM(RTWN_TXDW1_CIPHER, rtwn_get_cipher(cipher)));

	rtwn_fill_tx_desc_raw(sc, ni, m, txd, params);

	if (ieee80211_radiotap_active_vap(vap)) {
		struct rtwn_tx_radiotap_header *tap = &sc->sc_txtap;

		tap->wt_flags = rtwn_tx_radiotap_flags(sc, txd);
		if (k != NULL)
			tap->wt_flags |= IEEE80211_RADIOTAP_F_WEP;
		ieee80211_radiotap_tx(vap, m);
	}

	return (rtwn_tx_start(sc, ni, m, (uint8_t *)txd, type, 0));
}

int
rtwn_transmit(struct ieee80211com *ic, struct mbuf *m)
{
	struct rtwn_softc *sc = ic->ic_softc;
	int error;

	RTWN_LOCK(sc);
	if ((sc->sc_flags & RTWN_RUNNING) == 0) {
		RTWN_UNLOCK(sc);
		return (ENXIO);
	}
	error = mbufq_enqueue(&sc->sc_snd, m);
	if (error) {
		RTWN_UNLOCK(sc);
		return (error);
	}
	rtwn_start(sc);
	RTWN_UNLOCK(sc);

	return (0);
}

void
rtwn_start(struct rtwn_softc *sc)
{
	struct ieee80211_node *ni;
	struct mbuf *m;

	RTWN_ASSERT_LOCKED(sc);
	while ((m = mbufq_dequeue(&sc->sc_snd)) != NULL) {
		if (sc->qfullmsk != 0) {
			mbufq_prepend(&sc->sc_snd, m);
			break;
		}
		ni = (struct ieee80211_node *)m->m_pkthdr.rcvif;
		m->m_pkthdr.rcvif = NULL;

		RTWN_DPRINTF(sc, RTWN_DEBUG_XMIT,
		    "%s: called; m %p, ni %p\n", __func__, m, ni);

		if (rtwn_tx_data(sc, ni, m) != 0) {
			if_inc_counter(ni->ni_vap->iv_ifp,
			    IFCOUNTER_OERRORS, 1);
			m_freem(m);
#ifdef D4054
			ieee80211_tx_watchdog_refresh(ni->ni_ic, -1, 0);
#endif
			ieee80211_free_node(ni);
			break;
		}
	}
}

int
rtwn_raw_xmit(struct ieee80211_node *ni, struct mbuf *m,
    const struct ieee80211_bpf_params *params)
{
	struct ieee80211com *ic = ni->ni_ic;
	struct rtwn_softc *sc = ic->ic_softc;
	int error;

	RTWN_DPRINTF(sc, RTWN_DEBUG_XMIT, "%s: called; m %p, ni %p\n",
	    __func__, m, ni);

	/* prevent management frames from being sent if we're not ready */
	RTWN_LOCK(sc);
	if (!(sc->sc_flags & RTWN_RUNNING)) {
		error = ENETDOWN;
		goto end;
	}

	if (sc->qfullmsk != 0) {
		error = ENOBUFS;
		goto end;
	}

	if (params == NULL) {
		/*
		 * Legacy path; interpret frame contents to decide
		 * precisely how to send the frame.
		 */
		error = rtwn_tx_data(sc, ni, m);
	} else {
		/*
		 * Caller supplied explicit parameters to use in
		 * sending the frame.
		 */
		error = rtwn_tx_raw(sc, ni, m, params);
	}

end:
	if (error != 0) {
		if (m->m_flags & M_TXCB)
			ieee80211_process_callback(ni, m, 1);
		m_freem(m);
	}

	RTWN_UNLOCK(sc);

	return (error);
}
