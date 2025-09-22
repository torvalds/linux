/*	$OpenBSD: ieee80211_pae_input.c,v 1.37 2020/11/19 20:03:33 krw Exp $	*/

/*-
 * Copyright (c) 2007,2008 Damien Bergamini <damien.bergamini@free.fr>
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

/*
 * This code implements the 4-Way Handshake and Group Key Handshake protocols
 * (both Supplicant and Authenticator Key Receive state machines) defined in
 * IEEE Std 802.11-2007 section 8.5.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/errno.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_priv.h>

void	ieee80211_recv_4way_msg1(struct ieee80211com *,
	    struct ieee80211_eapol_key *, struct ieee80211_node *);
#ifndef IEEE80211_STA_ONLY
void	ieee80211_recv_4way_msg2(struct ieee80211com *,
	    struct ieee80211_eapol_key *, struct ieee80211_node *,
	    const u_int8_t *);
#endif
int	ieee80211_must_update_group_key(struct ieee80211_key *, const uint8_t *,
	    int);
void	ieee80211_recv_4way_msg3(struct ieee80211com *,
	    struct ieee80211_eapol_key *, struct ieee80211_node *);
#ifndef IEEE80211_STA_ONLY
void	ieee80211_recv_4way_msg4(struct ieee80211com *,
	    struct ieee80211_eapol_key *, struct ieee80211_node *);
void	ieee80211_recv_4way_msg2or4(struct ieee80211com *,
	    struct ieee80211_eapol_key *, struct ieee80211_node *);
#endif
void	ieee80211_recv_rsn_group_msg1(struct ieee80211com *,
	    struct ieee80211_eapol_key *, struct ieee80211_node *);
void	ieee80211_recv_wpa_group_msg1(struct ieee80211com *,
	    struct ieee80211_eapol_key *, struct ieee80211_node *);
#ifndef IEEE80211_STA_ONLY
void	ieee80211_recv_group_msg2(struct ieee80211com *,
	    struct ieee80211_eapol_key *, struct ieee80211_node *);
void	ieee80211_recv_eapol_key_req(struct ieee80211com *,
	    struct ieee80211_eapol_key *, struct ieee80211_node *);
#endif

/*
 * Process an incoming EAPOL frame.  Notice that we are only interested in
 * EAPOL-Key frames with an IEEE 802.11 or WPA descriptor type.
 */
void
ieee80211_eapol_key_input(struct ieee80211com *ic, struct mbuf *m,
    struct ieee80211_node *ni)
{
	struct ifnet *ifp = &ic->ic_if;
	struct ether_header *eh;
	struct ieee80211_eapol_key *key;
	u_int16_t info, desc;
	int totlen, bodylen, paylen;

	ifp->if_ibytes += m->m_pkthdr.len;

	eh = mtod(m, struct ether_header *);
	if (IEEE80211_IS_MULTICAST(eh->ether_dhost)) {
		ifp->if_imcasts++;
		goto done;
	}
	m_adj(m, sizeof(*eh));

	if (m->m_pkthdr.len < sizeof(*key))
		goto done;
	if (m->m_len < sizeof(*key) &&
	    (m = m_pullup(m, sizeof(*key))) == NULL) {
		ic->ic_stats.is_rx_nombuf++;
		goto done;
	}
	key = mtod(m, struct ieee80211_eapol_key *);

	if (key->type != EAPOL_KEY)
		goto done;
	ic->ic_stats.is_rx_eapol_key++;

	if ((ni->ni_rsnprotos == IEEE80211_PROTO_RSN &&
	     key->desc != EAPOL_KEY_DESC_IEEE80211) ||
	    (ni->ni_rsnprotos == IEEE80211_PROTO_WPA &&
	     key->desc != EAPOL_KEY_DESC_WPA))
		goto done;

	/* check packet body length */
	bodylen = BE_READ_2(key->len);
	totlen = 4 + bodylen;
	if (m->m_pkthdr.len < totlen || totlen > MCLBYTES)
		goto done;

	/* check key data length */
	paylen = BE_READ_2(key->paylen);
	if (paylen > totlen - sizeof(*key))
		goto done;

	info = BE_READ_2(key->info);

	/* discard EAPOL-Key frames with an unknown descriptor version */
	desc = info & EAPOL_KEY_VERSION_MASK;
	if (desc < EAPOL_KEY_DESC_V1 || desc > EAPOL_KEY_DESC_V3)
		goto done;

	if (ieee80211_is_sha256_akm(ni->ni_rsnakms)) {
		if (desc != EAPOL_KEY_DESC_V3)
			goto done;
	} else if (ni->ni_rsncipher == IEEE80211_CIPHER_CCMP ||
	     ni->ni_rsngroupcipher == IEEE80211_CIPHER_CCMP) {
		if (desc != EAPOL_KEY_DESC_V2)
			goto done;
	}

	/* make sure the key data field is contiguous */
	if (m->m_len < totlen && (m = m_pullup(m, totlen)) == NULL) {
		ic->ic_stats.is_rx_nombuf++;
		goto done;
	}
	key = mtod(m, struct ieee80211_eapol_key *);

	/* determine message type (see 8.5.3.7) */
	if (info & EAPOL_KEY_REQUEST) {
#ifndef IEEE80211_STA_ONLY
		/* EAPOL-Key Request frame */
		ieee80211_recv_eapol_key_req(ic, key, ni);
#endif
	} else if (info & EAPOL_KEY_PAIRWISE) {
		/* 4-Way Handshake */
		if (info & EAPOL_KEY_KEYMIC) {
			if (info & EAPOL_KEY_KEYACK)
				ieee80211_recv_4way_msg3(ic, key, ni);
#ifndef IEEE80211_STA_ONLY
			else
				ieee80211_recv_4way_msg2or4(ic, key, ni);
#endif
		} else if (info & EAPOL_KEY_KEYACK)
			ieee80211_recv_4way_msg1(ic, key, ni);
	} else {
		/* Group Key Handshake */
		if (!(info & EAPOL_KEY_KEYMIC))
			goto done;
		if (info & EAPOL_KEY_KEYACK) {
			if (key->desc == EAPOL_KEY_DESC_WPA)
				ieee80211_recv_wpa_group_msg1(ic, key, ni);
			else
				ieee80211_recv_rsn_group_msg1(ic, key, ni);
		}
#ifndef IEEE80211_STA_ONLY
		else
			ieee80211_recv_group_msg2(ic, key, ni);
#endif
	}
 done:
	m_freem(m);
}

/*
 * Process Message 1 of the 4-Way Handshake (sent by Authenticator).
 */
void
ieee80211_recv_4way_msg1(struct ieee80211com *ic,
    struct ieee80211_eapol_key *key, struct ieee80211_node *ni)
{
	struct ieee80211_ptk tptk;
	struct ieee80211_pmk *pmk;
	const u_int8_t *frm, *efrm;
	const u_int8_t *pmkid;

#ifndef IEEE80211_STA_ONLY
	if (ic->ic_opmode != IEEE80211_M_STA &&
	    ic->ic_opmode != IEEE80211_M_IBSS)
		return;
#endif
	/* 
	 * Message 1 is always expected while RSN is active since some
	 * APs will rekey the PTK by sending Msg1/4 after some time.
	 */
	if (ni->ni_rsn_supp_state == RSNA_SUPP_INITIALIZE) {
		DPRINTF(("unexpected in state: %d\n", ni->ni_rsn_supp_state));
		return;
	}
	/* enforce monotonicity of key request replay counter */
	if (ni->ni_replaycnt_ok &&
	    BE_READ_8(key->replaycnt) <= ni->ni_replaycnt) {
		ic->ic_stats.is_rx_eapol_replay++;
		return;
	}

	/* parse key data field (may contain an encapsulated PMKID) */
	frm = (const u_int8_t *)&key[1];
	efrm = frm + BE_READ_2(key->paylen);

	pmkid = NULL;
	while (frm + 2 <= efrm) {
		if (frm + 2 + frm[1] > efrm)
			break;
		switch (frm[0]) {
		case IEEE80211_ELEMID_VENDOR:
			if (frm[1] < 4)
				break;
			if (memcmp(&frm[2], IEEE80211_OUI, 3) == 0) {
				switch (frm[5]) {
				case IEEE80211_KDE_PMKID:
					pmkid = frm;
					break;
				}
			}
			break;
		}
		frm += 2 + frm[1];
	}
	/* check that the PMKID KDE is valid (if present) */
	if (pmkid != NULL && pmkid[1] != 4 + 16)
		return;

	if (ieee80211_is_8021x_akm(ni->ni_rsnakms)) {
		/* retrieve the PMK for this (AP,PMKID) */
		pmk = ieee80211_pmksa_find(ic, ni,
		    (pmkid != NULL) ? &pmkid[6] : NULL);
		if (pmk == NULL) {
			DPRINTF(("no PMK available for %s\n",
			    ether_sprintf(ni->ni_macaddr)));
			return;
		}
		memcpy(ni->ni_pmk, pmk->pmk_key, IEEE80211_PMK_LEN);
	} else	/* use pre-shared key */
		memcpy(ni->ni_pmk, ic->ic_psk, IEEE80211_PMK_LEN);
	ni->ni_flags |= IEEE80211_NODE_PMK;

	/* save authenticator's nonce (ANonce) */
	memcpy(ni->ni_nonce, key->nonce, EAPOL_KEY_NONCE_LEN);

	/* generate supplicant's nonce (SNonce) */
	arc4random_buf(ic->ic_nonce, EAPOL_KEY_NONCE_LEN);

	/* TPTK = CalcPTK(PMK, ANonce, SNonce) */
	ieee80211_derive_ptk(ni->ni_rsnakms, ni->ni_pmk, ni->ni_macaddr,
	    ic->ic_myaddr, ni->ni_nonce, ic->ic_nonce, &tptk);

	/* We are now expecting a new pairwise key. */
	ni->ni_flags |= IEEE80211_NODE_RSN_NEW_PTK;

	if (ic->ic_if.if_flags & IFF_DEBUG)
		printf("%s: received msg %d/%d of the %s handshake from %s\n",
		    ic->ic_if.if_xname, 1, 4, "4-way",
		    ether_sprintf(ni->ni_macaddr));

	/* send message 2 to authenticator using TPTK */
	(void)ieee80211_send_4way_msg2(ic, ni, key->replaycnt, &tptk);
}

#ifndef IEEE80211_STA_ONLY
/*
 * Process Message 2 of the 4-Way Handshake (sent by Supplicant).
 */
void
ieee80211_recv_4way_msg2(struct ieee80211com *ic,
    struct ieee80211_eapol_key *key, struct ieee80211_node *ni,
    const u_int8_t *rsnie)
{
	struct ieee80211_ptk tptk;

	if (ic->ic_opmode != IEEE80211_M_HOSTAP &&
	    ic->ic_opmode != IEEE80211_M_IBSS)
		return;

	/* discard if we're not expecting this message */
	if (ni->ni_rsn_state != RSNA_PTKSTART &&
	    ni->ni_rsn_state != RSNA_PTKCALCNEGOTIATING) {
		DPRINTF(("unexpected in state: %d\n", ni->ni_rsn_state));
		return;
	}
	ni->ni_rsn_state = RSNA_PTKCALCNEGOTIATING;

	/* NB: replay counter has already been verified by caller */

	/* PTK = CalcPTK(ANonce, SNonce) */
	ieee80211_derive_ptk(ni->ni_rsnakms, ni->ni_pmk, ic->ic_myaddr,
	    ni->ni_macaddr, ni->ni_nonce, key->nonce, &tptk);

	/* check Key MIC field using KCK */
	if (ieee80211_eapol_key_check_mic(key, tptk.kck) != 0) {
		DPRINTF(("key MIC failed\n"));
		ic->ic_stats.is_rx_eapol_badmic++;
		return;	/* will timeout.. */
	}

	timeout_del(&ni->ni_eapol_to);
	ni->ni_rsn_state = RSNA_PTKCALCNEGOTIATING_2;
	ni->ni_rsn_retries = 0;

	/* install TPTK as PTK now that MIC is verified */
	memcpy(&ni->ni_ptk, &tptk, sizeof(tptk));

	/*
	 * The RSN IE must match bit-wise with what the STA included in its
	 * (Re)Association Request.
	 */
	if (ni->ni_rsnie == NULL || rsnie[1] != ni->ni_rsnie[1] ||
	    memcmp(rsnie, ni->ni_rsnie, 2 + rsnie[1]) != 0) {
		IEEE80211_SEND_MGMT(ic, ni, IEEE80211_FC0_SUBTYPE_DEAUTH,
		    IEEE80211_REASON_RSN_DIFFERENT_IE);
		ieee80211_node_leave(ic, ni);
		return;
	}

	if (ic->ic_if.if_flags & IFF_DEBUG)
		printf("%s: received msg %d/%d of the %s handshake from %s\n",
		    ic->ic_if.if_xname, 2, 4, "4-way",
		    ether_sprintf(ni->ni_macaddr));

	/* send message 3 to supplicant */
	(void)ieee80211_send_4way_msg3(ic, ni);
}
#endif	/* IEEE80211_STA_ONLY */

/* 
 * Check if a group key must be updated with a new GTK from an EAPOL frame.
 * Manipulated group key handshake messages could trick clients into
 * reinstalling an already used group key and hence lower or reset the
 * associated replay counter. This check prevents such attacks.
 */
int
ieee80211_must_update_group_key(struct ieee80211_key *k, const uint8_t *gtk,
    int len)
{
	return (k->k_cipher == IEEE80211_CIPHER_NONE || k->k_len != len ||
	    memcmp(k->k_key, gtk, len) != 0);
}

/*
 * Process Message 3 of the 4-Way Handshake (sent by Authenticator).
 */
void
ieee80211_recv_4way_msg3(struct ieee80211com *ic,
    struct ieee80211_eapol_key *key, struct ieee80211_node *ni)
{
	struct ieee80211_ptk tptk;
	struct ieee80211_key *k;
	const u_int8_t *frm, *efrm;
	const u_int8_t *rsnie1, *rsnie2, *gtk, *igtk;
	u_int16_t info, reason = 0;
	int keylen, deferlink = 0;

#ifndef IEEE80211_STA_ONLY
	if (ic->ic_opmode != IEEE80211_M_STA &&
	    ic->ic_opmode != IEEE80211_M_IBSS)
		return;
#endif
	/* discard if we're not expecting this message */
	if (ni->ni_rsn_supp_state != RSNA_SUPP_PTKNEGOTIATING &&
	    ni->ni_rsn_supp_state != RSNA_SUPP_PTKDONE) {
		DPRINTF(("unexpected in state: %d\n", ni->ni_rsn_supp_state));
		return;
	}
	/* enforce monotonicity of key request replay counter */
	if (ni->ni_replaycnt_ok &&
	    BE_READ_8(key->replaycnt) <= ni->ni_replaycnt) {
		ic->ic_stats.is_rx_eapol_replay++;
		return;
	}
	/* make sure that a PMK has been selected */
	if (!(ni->ni_flags & IEEE80211_NODE_PMK)) {
		DPRINTF(("no PMK found for %s\n",
		    ether_sprintf(ni->ni_macaddr)));
		return;
	}
	/* check that ANonce matches that of Message 1 */
	if (memcmp(key->nonce, ni->ni_nonce, EAPOL_KEY_NONCE_LEN) != 0) {
		DPRINTF(("ANonce does not match msg 1/4\n"));
		return;
	}
	/* TPTK = CalcPTK(PMK, ANonce, SNonce) */
	ieee80211_derive_ptk(ni->ni_rsnakms, ni->ni_pmk, ni->ni_macaddr,
	    ic->ic_myaddr, key->nonce, ic->ic_nonce, &tptk);

	info = BE_READ_2(key->info);

	/* check Key MIC field using KCK */
	if (ieee80211_eapol_key_check_mic(key, tptk.kck) != 0) {
		DPRINTF(("key MIC failed\n"));
		ic->ic_stats.is_rx_eapol_badmic++;
		return;
	}
	/* install TPTK as PTK now that MIC is verified */
	memcpy(&ni->ni_ptk, &tptk, sizeof(tptk));

	/* if encrypted, decrypt Key Data field using KEK */
	if ((info & EAPOL_KEY_ENCRYPTED) &&
	    ieee80211_eapol_key_decrypt(key, ni->ni_ptk.kek) != 0) {
		DPRINTF(("decryption failed\n"));
		return;
	}

	/* parse key data field */
	frm = (const u_int8_t *)&key[1];
	efrm = frm + BE_READ_2(key->paylen);

	/*
	 * Some WPA1+WPA2 APs (like hostapd) appear to include both WPA and
	 * RSN IEs in message 3/4.  We only take into account the IE of the
	 * version of the protocol we negotiated at association time.
	 */
	rsnie1 = rsnie2 = gtk = igtk = NULL;
	while (frm + 2 <= efrm) {
		if (frm + 2 + frm[1] > efrm)
			break;
		switch (frm[0]) {
		case IEEE80211_ELEMID_RSN:
			if (ni->ni_rsnprotos != IEEE80211_PROTO_RSN)
				break;
			if (rsnie1 == NULL)
				rsnie1 = frm;
			else if (rsnie2 == NULL)
				rsnie2 = frm;
			/* ignore others if more than two RSN IEs */
			break;
		case IEEE80211_ELEMID_VENDOR:
			if (frm[1] < 4)
				break;
			if (memcmp(&frm[2], IEEE80211_OUI, 3) == 0) {
				switch (frm[5]) {
				case IEEE80211_KDE_GTK:
					gtk = frm;
					break;
				case IEEE80211_KDE_IGTK:
					if (ni->ni_flags & IEEE80211_NODE_MFP)
						igtk = frm;
					break;
				}
			} else if (memcmp(&frm[2], MICROSOFT_OUI, 3) == 0) {
				switch (frm[5]) {
				case 1:	/* WPA */
					if (ni->ni_rsnprotos !=
					    IEEE80211_PROTO_WPA)
						break;
					rsnie1 = frm;
					break;
				}
			}
			break;
		}
		frm += 2 + frm[1];
	}
	/* first WPA/RSN IE is mandatory */
	if (rsnie1 == NULL) {
		DPRINTF(("missing RSN IE\n"));
		return;
	}
	/* key data must be encrypted if GTK is included */
	if (gtk != NULL && !(info & EAPOL_KEY_ENCRYPTED)) {
		DPRINTF(("GTK not encrypted\n"));
		return;
	}
	/* GTK KDE must be included if IGTK KDE is present */
	if (igtk != NULL && gtk == NULL) {
		DPRINTF(("IGTK KDE found but GTK KDE missing\n"));
		return;
	}
	/* check that the Install bit is set if using pairwise keys */
	if (ni->ni_rsncipher != IEEE80211_CIPHER_USEGROUP &&
	    !(info & EAPOL_KEY_INSTALL)) {
		DPRINTF(("pairwise cipher but !Install\n"));
		return;
	}

	/*
	 * Check that first WPA/RSN IE is identical to the one received in
	 * the beacon or probe response frame.
	 */
	if (ni->ni_rsnie == NULL || rsnie1[1] != ni->ni_rsnie[1] ||
	    memcmp(rsnie1, ni->ni_rsnie, 2 + rsnie1[1]) != 0) {
		reason = IEEE80211_REASON_RSN_DIFFERENT_IE;
		goto deauth;
	}

	/*
	 * If a second RSN information element is present, use its pairwise
	 * cipher suite or deauthenticate.
	 */
	if (rsnie2 != NULL) {
		struct ieee80211_rsnparams rsn;

		if (ieee80211_parse_rsn(ic, rsnie2, &rsn) == 0) {
			if (rsn.rsn_akms != ni->ni_rsnakms ||
			    rsn.rsn_groupcipher != ni->ni_rsngroupcipher ||
			    rsn.rsn_nciphers != 1 ||
			    !(rsn.rsn_ciphers & ic->ic_rsnciphers)) {
				reason = IEEE80211_REASON_BAD_PAIRWISE_CIPHER;
				goto deauth;
			}
			/* use pairwise cipher suite of second RSN IE */
			ni->ni_rsnciphers = rsn.rsn_ciphers;
			ni->ni_rsncipher = ni->ni_rsnciphers;
		}
	}

	/* update the last seen value of the key replay counter field */
	ni->ni_replaycnt = BE_READ_8(key->replaycnt);
	ni->ni_replaycnt_ok = 1;

	if (ic->ic_if.if_flags & IFF_DEBUG)
		printf("%s: received msg %d/%d of the %s handshake from %s\n",
		    ic->ic_if.if_xname, 3, 4, "4-way",
		    ether_sprintf(ni->ni_macaddr));

	/* send message 4 to authenticator */
	if (ieee80211_send_4way_msg4(ic, ni) != 0)
		return;	/* ..authenticator will retry */

	/* 
	 * Only install a new pairwise key if we are still expecting a new key,
	 * as indicated by the NODE_RSN_NEW_PTK flag. An adversary could be
	 * sending manipulated retransmissions of message 3 of the 4-way
	 * handshake in an attempt to trick us into reinstalling an already
	 * used pairwise key. If this attack succeeded, the incremental nonce
	 * and replay counter associated with the key would be reset.
	 * Against CCMP, the adversary could abuse this to replay and decrypt
	 * packets. Against TKIP, it would become possible to replay, decrypt,
	 * and forge packets.
	 */
	if (ni->ni_rsncipher != IEEE80211_CIPHER_USEGROUP &&
	    (ni->ni_flags & IEEE80211_NODE_RSN_NEW_PTK)) {
		u_int64_t prsc;

		/* check that key length matches that of pairwise cipher */
		keylen = ieee80211_cipher_keylen(ni->ni_rsncipher);
		if (BE_READ_2(key->keylen) != keylen) {
			reason = IEEE80211_REASON_AUTH_LEAVE;
			goto deauth;
		}
		prsc = (gtk == NULL) ? LE_READ_6(key->rsc) : 0;

		/* map PTK to 802.11 key */
		k = &ni->ni_pairwise_key;
		memset(k, 0, sizeof(*k));
		k->k_cipher = ni->ni_rsncipher;
		k->k_rsc[0] = prsc;
		k->k_len = keylen;
		memcpy(k->k_key, ni->ni_ptk.tk, k->k_len);
		/* install the PTK */
		switch ((*ic->ic_set_key)(ic, ni, k)) {
		case 0:
			break;
		case EBUSY:
			deferlink = 1;
			break;
		default:
			reason = IEEE80211_REASON_AUTH_LEAVE;
			goto deauth;
		}
		ni->ni_flags &= ~IEEE80211_NODE_RSN_NEW_PTK;
		ni->ni_flags &= ~IEEE80211_NODE_TXRXPROT;
		ni->ni_flags |= IEEE80211_NODE_RXPROT;
	} else if (ni->ni_rsncipher != IEEE80211_CIPHER_USEGROUP)
		printf("%s: unexpected pairwise key update received from %s\n",
		    ic->ic_if.if_xname, ether_sprintf(ni->ni_macaddr));

	if (gtk != NULL) {
		u_int8_t kid;

		/* check that key length matches that of group cipher */
		keylen = ieee80211_cipher_keylen(ni->ni_rsngroupcipher);
		if (gtk[1] != 6 + keylen) {
			reason = IEEE80211_REASON_AUTH_LEAVE;
			goto deauth;
		}
		/* map GTK to 802.11 key */
		kid = gtk[6] & 3;
		k = &ic->ic_nw_keys[kid];
		if (ieee80211_must_update_group_key(k, &gtk[8], keylen)) {
			memset(k, 0, sizeof(*k));
			k->k_id = kid;	/* 0-3 */
			k->k_cipher = ni->ni_rsngroupcipher;
			k->k_flags = IEEE80211_KEY_GROUP;
			if (gtk[6] & (1 << 2))
				k->k_flags |= IEEE80211_KEY_TX;
			k->k_rsc[0] = LE_READ_6(key->rsc);
			k->k_len = keylen;
			memcpy(k->k_key, &gtk[8], k->k_len);
			/* install the GTK */
			switch ((*ic->ic_set_key)(ic, ni, k)) {
			case 0:
				break;
			case EBUSY:
				deferlink = 1;
				break;
			default:
				reason = IEEE80211_REASON_AUTH_LEAVE;
				goto deauth;
			}
		}
	}
	if (igtk != NULL) {	/* implies MFP && gtk != NULL */
		u_int16_t kid;

		/* check that the IGTK KDE is valid */
		if (igtk[1] != 4 + 24) {
			reason = IEEE80211_REASON_AUTH_LEAVE;
			goto deauth;
		}
		kid = LE_READ_2(&igtk[6]);
		if (kid != 4 && kid != 5) {
			DPRINTF(("unsupported IGTK id %u\n", kid));
			reason = IEEE80211_REASON_AUTH_LEAVE;
			goto deauth;
		}
		/* map IGTK to 802.11 key */
		k = &ic->ic_nw_keys[kid];
		if (ieee80211_must_update_group_key(k, &igtk[14], 16)) {
			memset(k, 0, sizeof(*k));
			k->k_id = kid;	/* either 4 or 5 */
			k->k_cipher = ni->ni_rsngroupmgmtcipher;
			k->k_flags = IEEE80211_KEY_IGTK;
			k->k_mgmt_rsc = LE_READ_6(&igtk[8]);	/* IPN */
			k->k_len = 16;
			memcpy(k->k_key, &igtk[14], k->k_len);
			/* install the IGTK */
			switch ((*ic->ic_set_key)(ic, ni, k)) {
			case 0:
				break;
			case EBUSY:
				deferlink = 1;
				break;
			default:
				reason = IEEE80211_REASON_AUTH_LEAVE;
				goto deauth;
			}
		}
	}
	if (info & EAPOL_KEY_INSTALL)
		ni->ni_flags |= IEEE80211_NODE_TXRXPROT;

	if (info & EAPOL_KEY_SECURE) {
		ni->ni_flags |= IEEE80211_NODE_TXRXPROT;
#ifndef IEEE80211_STA_ONLY
		if (ic->ic_opmode != IEEE80211_M_IBSS ||
		    ++ni->ni_key_count == 2)
#endif
		{
			if (deferlink == 0) {
				DPRINTF(("marking port %s valid\n",
				    ether_sprintf(ni->ni_macaddr)));
				ni->ni_port_valid = 1;
				ieee80211_set_link_state(ic, LINK_STATE_UP);
			}
			ni->ni_assoc_fail = 0;
			if (ic->ic_opmode == IEEE80211_M_STA)
				ic->ic_rsngroupcipher = ni->ni_rsngroupcipher;
		}
	}
 deauth:
	if (reason != 0) {
		IEEE80211_SEND_MGMT(ic, ni, IEEE80211_FC0_SUBTYPE_DEAUTH,
		    reason);
		ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
	}
}

#ifndef IEEE80211_STA_ONLY
/*
 * Process Message 4 of the 4-Way Handshake (sent by Supplicant).
 */
void
ieee80211_recv_4way_msg4(struct ieee80211com *ic,
    struct ieee80211_eapol_key *key, struct ieee80211_node *ni)
{
	if (ic->ic_opmode != IEEE80211_M_HOSTAP &&
	    ic->ic_opmode != IEEE80211_M_IBSS)
		return;

	/* discard if we're not expecting this message */
	if (ni->ni_rsn_state != RSNA_PTKINITNEGOTIATING) {
		DPRINTF(("unexpected in state: %d\n", ni->ni_rsn_state));
		return;
	}

	/* NB: replay counter has already been verified by caller */

	/* check Key MIC field using KCK */
	if (ieee80211_eapol_key_check_mic(key, ni->ni_ptk.kck) != 0) {
		DPRINTF(("key MIC failed\n"));
		ic->ic_stats.is_rx_eapol_badmic++;
		return;	/* will timeout.. */
	}

	timeout_del(&ni->ni_eapol_to);
	ni->ni_rsn_state = RSNA_PTKINITDONE;
	ni->ni_rsn_retries = 0;

	if (ni->ni_rsncipher != IEEE80211_CIPHER_USEGROUP) {
		struct ieee80211_key *k;

		/* map PTK to 802.11 key */
		k = &ni->ni_pairwise_key;
		memset(k, 0, sizeof(*k));
		k->k_cipher = ni->ni_rsncipher;
		k->k_len = ieee80211_cipher_keylen(k->k_cipher);
		memcpy(k->k_key, ni->ni_ptk.tk, k->k_len);
		/* install the PTK */
		switch ((*ic->ic_set_key)(ic, ni, k)) {
		case 0:
		case EBUSY:
			break;
		default:
			IEEE80211_SEND_MGMT(ic, ni,
			    IEEE80211_FC0_SUBTYPE_DEAUTH,
			    IEEE80211_REASON_ASSOC_TOOMANY);
			ieee80211_node_leave(ic, ni);
			return;
		}
		ni->ni_flags |= IEEE80211_NODE_TXRXPROT;
	}
	if (ic->ic_opmode != IEEE80211_M_IBSS || ++ni->ni_key_count == 2) {
		DPRINTF(("marking port %s valid\n",
		    ether_sprintf(ni->ni_macaddr)));
		ni->ni_port_valid = 1;
	}

	if (ic->ic_if.if_flags & IFF_DEBUG)
		printf("%s: received msg %d/%d of the %s handshake from %s\n",
		    ic->ic_if.if_xname, 4, 4, "4-way",
		    ether_sprintf(ni->ni_macaddr));

	/* initiate a group key handshake for WPA */
	if (ni->ni_rsnprotos == IEEE80211_PROTO_WPA)
		(void)ieee80211_send_group_msg1(ic, ni);
	else
		ni->ni_rsn_gstate = RSNA_IDLE;
}

/*
 * Differentiate Message 2 from Message 4 of the 4-Way Handshake based on
 * the presence of an RSN or WPA Information Element.
 */
void
ieee80211_recv_4way_msg2or4(struct ieee80211com *ic,
    struct ieee80211_eapol_key *key, struct ieee80211_node *ni)
{
	const u_int8_t *frm, *efrm;
	const u_int8_t *rsnie;

	if (BE_READ_8(key->replaycnt) != ni->ni_replaycnt) {
		ic->ic_stats.is_rx_eapol_replay++;
		return;
	}

	/* parse key data field (check if an RSN IE is present) */
	frm = (const u_int8_t *)&key[1];
	efrm = frm + BE_READ_2(key->paylen);

	rsnie = NULL;
	while (frm + 2 <= efrm) {
		if (frm + 2 + frm[1] > efrm)
			break;
		switch (frm[0]) {
		case IEEE80211_ELEMID_RSN:
			rsnie = frm;
			break;
		case IEEE80211_ELEMID_VENDOR:
			if (frm[1] < 4)
				break;
			if (memcmp(&frm[2], MICROSOFT_OUI, 3) == 0) {
				switch (frm[5]) {
				case 1:	/* WPA */
					rsnie = frm;
					break;
				}
			}
		}
		frm += 2 + frm[1];
	}
	if (rsnie != NULL)
		ieee80211_recv_4way_msg2(ic, key, ni, rsnie);
	else
		ieee80211_recv_4way_msg4(ic, key, ni);
}
#endif	/* IEEE80211_STA_ONLY */

/*
 * Process Message 1 of the RSN Group Key Handshake (sent by Authenticator).
 */
void
ieee80211_recv_rsn_group_msg1(struct ieee80211com *ic,
    struct ieee80211_eapol_key *key, struct ieee80211_node *ni)
{
	struct ieee80211_key *k;
	const u_int8_t *frm, *efrm;
	const u_int8_t *gtk, *igtk;
	u_int16_t info, kid, reason = 0;
	int keylen;

#ifndef IEEE80211_STA_ONLY
	if (ic->ic_opmode != IEEE80211_M_STA &&
	    ic->ic_opmode != IEEE80211_M_IBSS)
		return;
#endif
	/* discard if we're not expecting this message */
	if (ni->ni_rsn_supp_state != RSNA_SUPP_PTKDONE) {
		DPRINTF(("unexpected in state: %d\n", ni->ni_rsn_supp_state));
		return;
	}
	/* enforce monotonicity of key request replay counter */
	if (BE_READ_8(key->replaycnt) <= ni->ni_replaycnt) {
		ic->ic_stats.is_rx_eapol_replay++;
		return;
	}
	/* check Key MIC field using KCK */
	if (ieee80211_eapol_key_check_mic(key, ni->ni_ptk.kck) != 0) {
		DPRINTF(("key MIC failed\n"));
		ic->ic_stats.is_rx_eapol_badmic++;
		return;
	}
	info = BE_READ_2(key->info);

	/* check that encrypted and decrypt Key Data field using KEK */
	if (!(info & EAPOL_KEY_ENCRYPTED) ||
	    ieee80211_eapol_key_decrypt(key, ni->ni_ptk.kek) != 0) {
		DPRINTF(("decryption failed\n"));
		return;
	}

	/* parse key data field (shall contain a GTK KDE) */
	frm = (const u_int8_t *)&key[1];
	efrm = frm + BE_READ_2(key->paylen);

	gtk = igtk = NULL;
	while (frm + 2 <= efrm) {
		if (frm + 2 + frm[1] > efrm)
			break;
		switch (frm[0]) {
		case IEEE80211_ELEMID_VENDOR:
			if (frm[1] < 4)
				break;
			if (memcmp(&frm[2], IEEE80211_OUI, 3) == 0) {
				switch (frm[5]) {
				case IEEE80211_KDE_GTK:
					gtk = frm;
					break;
				case IEEE80211_KDE_IGTK:
					if (ni->ni_flags & IEEE80211_NODE_MFP)
						igtk = frm;
					break;
				}
			}
			break;
		}
		frm += 2 + frm[1];
	}
	/* check that the GTK KDE is present */
	if (gtk == NULL) {
		DPRINTF(("GTK KDE missing\n"));
		return;
	}

	/* check that key length matches that of group cipher */
	keylen = ieee80211_cipher_keylen(ni->ni_rsngroupcipher);
	if (gtk[1] != 6 + keylen)
		return;

	/* map GTK to 802.11 key */
	kid = gtk[6] & 3;
	k = &ic->ic_nw_keys[kid];
	if (ieee80211_must_update_group_key(k, &gtk[8], keylen)) {
		memset(k, 0, sizeof(*k));
		k->k_id = kid;	/* 0-3 */
		k->k_cipher = ni->ni_rsngroupcipher;
		k->k_flags = IEEE80211_KEY_GROUP;
		if (gtk[6] & (1 << 2))
			k->k_flags |= IEEE80211_KEY_TX;
		k->k_rsc[0] = LE_READ_6(key->rsc);
		k->k_len = keylen;
		memcpy(k->k_key, &gtk[8], k->k_len);
		/* install the GTK */
		switch ((*ic->ic_set_key)(ic, ni, k)) {
		case 0:
		case EBUSY:
			break;
		default:
			reason = IEEE80211_REASON_AUTH_LEAVE;
			goto deauth;
		}
	}
	if (igtk != NULL) {	/* implies MFP */
		/* check that the IGTK KDE is valid */
		if (igtk[1] != 4 + 24) {
			reason = IEEE80211_REASON_AUTH_LEAVE;
			goto deauth;
		}
		kid = LE_READ_2(&igtk[6]);
		if (kid != 4 && kid != 5) {
			DPRINTF(("unsupported IGTK id %u\n", kid));
			reason = IEEE80211_REASON_AUTH_LEAVE;
			goto deauth;
		}
		/* map IGTK to 802.11 key */
		k = &ic->ic_nw_keys[kid];
		if (ieee80211_must_update_group_key(k, &igtk[14], 16)) {
			memset(k, 0, sizeof(*k));
			k->k_id = kid;	/* either 4 or 5 */
			k->k_cipher = ni->ni_rsngroupmgmtcipher;
			k->k_flags = IEEE80211_KEY_IGTK;
			k->k_mgmt_rsc = LE_READ_6(&igtk[8]);	/* IPN */
			k->k_len = 16;
			memcpy(k->k_key, &igtk[14], k->k_len);
			/* install the IGTK */
			switch ((*ic->ic_set_key)(ic, ni, k)) {
			case 0:
			case EBUSY:
				break;
			default:
				reason = IEEE80211_REASON_AUTH_LEAVE;
				goto deauth;
			}
		}
	}
	if (info & EAPOL_KEY_SECURE) {
#ifndef IEEE80211_STA_ONLY
		if (ic->ic_opmode != IEEE80211_M_IBSS ||
		    ++ni->ni_key_count == 2)
#endif
		{
			DPRINTF(("marking port %s valid\n",
			    ether_sprintf(ni->ni_macaddr)));
			ni->ni_port_valid = 1;
			ieee80211_set_link_state(ic, LINK_STATE_UP);
			ni->ni_assoc_fail = 0;
		}
	}
	/* update the last seen value of the key replay counter field */
	ni->ni_replaycnt = BE_READ_8(key->replaycnt);

	if (ic->ic_if.if_flags & IFF_DEBUG)
		printf("%s: received msg %d/%d of the %s handshake from %s\n",
		    ic->ic_if.if_xname, 1, 2, "group key",
		    ether_sprintf(ni->ni_macaddr));

	/* send message 2 to authenticator */
	(void)ieee80211_send_group_msg2(ic, ni, NULL);
	return;
 deauth:
	IEEE80211_SEND_MGMT(ic, ni, IEEE80211_FC0_SUBTYPE_DEAUTH, reason);
	ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
}

/*
 * Process Message 1 of the WPA Group Key Handshake (sent by Authenticator).
 */
void
ieee80211_recv_wpa_group_msg1(struct ieee80211com *ic,
    struct ieee80211_eapol_key *key, struct ieee80211_node *ni)
{
	struct ieee80211_key *k;
	u_int16_t info;
	u_int8_t kid;
	int keylen;
	const uint8_t *gtk;

#ifndef IEEE80211_STA_ONLY
	if (ic->ic_opmode != IEEE80211_M_STA &&
	    ic->ic_opmode != IEEE80211_M_IBSS)
		return;
#endif
	/* discard if we're not expecting this message */
	if (ni->ni_rsn_supp_state != RSNA_SUPP_PTKDONE) {
		DPRINTF(("unexpected in state: %d\n", ni->ni_rsn_supp_state));
		return;
	}
	/* enforce monotonicity of key request replay counter */
	if (BE_READ_8(key->replaycnt) <= ni->ni_replaycnt) {
		ic->ic_stats.is_rx_eapol_replay++;
		return;
	}
	/* check Key MIC field using KCK */
	if (ieee80211_eapol_key_check_mic(key, ni->ni_ptk.kck) != 0) {
		DPRINTF(("key MIC failed\n"));
		ic->ic_stats.is_rx_eapol_badmic++;
		return;
	}
	/*
	 * EAPOL-Key data field is encrypted even though WPA doesn't set
	 * the ENCRYPTED bit in the info field.
	 */
	if (ieee80211_eapol_key_decrypt(key, ni->ni_ptk.kek) != 0) {
		DPRINTF(("decryption failed\n"));
		return;
	}

	/* check that key length matches that of group cipher */
	keylen = ieee80211_cipher_keylen(ni->ni_rsngroupcipher);
	if (BE_READ_2(key->keylen) != keylen)
		return;

	/* check that the data length is large enough to hold the key */
	if (BE_READ_2(key->paylen) < keylen)
		return;

	info = BE_READ_2(key->info);

	/* map GTK to 802.11 key */
	kid = (info >> EAPOL_KEY_WPA_KID_SHIFT) & 3;
	k = &ic->ic_nw_keys[kid];
	gtk = (const uint8_t *)&key[1]; /* key data field contains the GTK */
	if (ieee80211_must_update_group_key(k, gtk, keylen)) {
		memset(k, 0, sizeof(*k));
		k->k_id = kid;	/* 0-3 */
		k->k_cipher = ni->ni_rsngroupcipher;
		k->k_flags = IEEE80211_KEY_GROUP;
		if (info & EAPOL_KEY_WPA_TX)
			k->k_flags |= IEEE80211_KEY_TX;
		k->k_rsc[0] = LE_READ_6(key->rsc);
		k->k_len = keylen;
		memcpy(k->k_key, gtk, k->k_len);
		/* install the GTK */
		switch ((*ic->ic_set_key)(ic, ni, k)) {
		case 0:
		case EBUSY:
			break;
		default:
			IEEE80211_SEND_MGMT(ic, ni, IEEE80211_FC0_SUBTYPE_DEAUTH,
			    IEEE80211_REASON_AUTH_LEAVE);
			ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
			return;
		}
	}
	if (info & EAPOL_KEY_SECURE) {
#ifndef IEEE80211_STA_ONLY
		if (ic->ic_opmode != IEEE80211_M_IBSS ||
		    ++ni->ni_key_count == 2)
#endif
		{
			DPRINTF(("marking port %s valid\n",
			    ether_sprintf(ni->ni_macaddr)));
			ni->ni_port_valid = 1;
			ieee80211_set_link_state(ic, LINK_STATE_UP);
			ni->ni_assoc_fail = 0;
		}
	}
	/* update the last seen value of the key replay counter field */
	ni->ni_replaycnt = BE_READ_8(key->replaycnt);

	if (ic->ic_if.if_flags & IFF_DEBUG)
		printf("%s: received msg %d/%d of the %s handshake from %s\n",
		    ic->ic_if.if_xname, 1, 2, "group key",
		    ether_sprintf(ni->ni_macaddr));

	/* send message 2 to authenticator */
	(void)ieee80211_send_group_msg2(ic, ni, k);
}

#ifndef IEEE80211_STA_ONLY
/*
 * Process Message 2 of the Group Key Handshake (sent by Supplicant).
 */
void
ieee80211_recv_group_msg2(struct ieee80211com *ic,
    struct ieee80211_eapol_key *key, struct ieee80211_node *ni)
{
	if (ic->ic_opmode != IEEE80211_M_HOSTAP &&
	    ic->ic_opmode != IEEE80211_M_IBSS)
		return;

	/* discard if we're not expecting this message */
	if (ni->ni_rsn_gstate != RSNA_REKEYNEGOTIATING) {
		DPRINTF(("%s: unexpected in state: %d\n", ic->ic_if.if_xname,
		     ni->ni_rsn_gstate));
		return;
	}
	/* enforce monotonicity of key request replay counter */
	if (BE_READ_8(key->replaycnt) != ni->ni_replaycnt) {
		ic->ic_stats.is_rx_eapol_replay++;
		return;
	}
	/* check Key MIC field using KCK */
	if (ieee80211_eapol_key_check_mic(key, ni->ni_ptk.kck) != 0) {
		DPRINTF(("key MIC failed\n"));
		ic->ic_stats.is_rx_eapol_badmic++;
		return;
	}

	timeout_del(&ni->ni_eapol_to);
	ni->ni_rsn_gstate = RSNA_REKEYESTABLISHED;

	if (ni->ni_flags & IEEE80211_NODE_REKEY) {
		int rekeysta = 0;
		ni->ni_flags &= ~IEEE80211_NODE_REKEY;
		ieee80211_iterate_nodes(ic,
		    ieee80211_count_rekeysta, &rekeysta);
		if (rekeysta == 0)
			ieee80211_setkeysdone(ic);
	}
	ni->ni_flags |= IEEE80211_NODE_TXRXPROT;

	ni->ni_rsn_gstate = RSNA_IDLE;
	ni->ni_rsn_retries = 0;

	if (ic->ic_if.if_flags & IFF_DEBUG)
		printf("%s: received msg %d/%d of the %s handshake from %s\n",
		    ic->ic_if.if_xname, 2, 2, "group key",
		    ether_sprintf(ni->ni_macaddr));
}

/*
 * EAPOL-Key Request frames are sent by the supplicant to request that the
 * authenticator initiates either a 4-Way Handshake or Group Key Handshake,
 * or to report a MIC failure in a TKIP MSDU.
 */
void
ieee80211_recv_eapol_key_req(struct ieee80211com *ic,
    struct ieee80211_eapol_key *key, struct ieee80211_node *ni)
{
	u_int16_t info;

	if (ic->ic_opmode != IEEE80211_M_HOSTAP &&
	    ic->ic_opmode != IEEE80211_M_IBSS)
		return;

	/* discard if we're not expecting this message */
	if (ni->ni_rsn_state != RSNA_PTKINITDONE) {
		DPRINTF(("unexpected in state: %d\n", ni->ni_rsn_state));
		return;
	}
	/* enforce monotonicity of key request replay counter */
	if (ni->ni_reqreplaycnt_ok &&
	    BE_READ_8(key->replaycnt) <= ni->ni_reqreplaycnt) {
		ic->ic_stats.is_rx_eapol_replay++;
		return;
	}
	info = BE_READ_2(key->info);

	if (!(info & EAPOL_KEY_KEYMIC) ||
	    ieee80211_eapol_key_check_mic(key, ni->ni_ptk.kck) != 0) {
		DPRINTF(("key request MIC failed\n"));
		ic->ic_stats.is_rx_eapol_badmic++;
		return;
	}
	/* update key request replay counter now that MIC is verified */
	ni->ni_reqreplaycnt = BE_READ_8(key->replaycnt);
	ni->ni_reqreplaycnt_ok = 1;

	if (info & EAPOL_KEY_ERROR) {	/* TKIP MIC failure */
		/* ignore reports from STAs not using TKIP */
		if (ic->ic_bss->ni_rsngroupcipher != IEEE80211_CIPHER_TKIP &&
		    ni->ni_rsncipher != IEEE80211_CIPHER_TKIP) {
			DPRINTF(("MIC failure report from !TKIP STA: %s\n",
			    ether_sprintf(ni->ni_macaddr)));
			return;
		}
		ic->ic_stats.is_rx_remmicfail++;
		ieee80211_michael_mic_failure(ic, LE_READ_6(key->rsc));

	} else if (info & EAPOL_KEY_PAIRWISE) {
		/* initiate a 4-Way Handshake */

	} else {
		/*
		 * Should change the GTK, initiate the 4-Way Handshake and
		 * then execute a Group Key Handshake with all supplicants.
		 */
	}
}
#endif	/* IEEE80211_STA_ONLY */
