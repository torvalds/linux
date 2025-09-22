/*	$OpenBSD: ieee80211_pae_output.c,v 1.33 2022/01/05 05:18:25 dlg Exp $	*/

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
 * (both Supplicant and Authenticator Key Transmit state machines) defined in
 * IEEE Std 802.11-2007 section 8.5.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/endian.h>
#include <sys/errno.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_llc.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_priv.h>

int		ieee80211_send_eapol_key(struct ieee80211com *, struct mbuf *,
		    struct ieee80211_node *, const struct ieee80211_ptk *);
#ifndef IEEE80211_STA_ONLY
u_int8_t	*ieee80211_add_gtk_kde(u_int8_t *, struct ieee80211_node *,
		    const struct ieee80211_key *);
u_int8_t	*ieee80211_add_pmkid_kde(u_int8_t *, const u_int8_t *);
u_int8_t	*ieee80211_add_igtk_kde(u_int8_t *,
		    const struct ieee80211_key *);
#endif
struct mbuf 	*ieee80211_get_eapol_key(int, int, u_int);

/*
 * Send an EAPOL-Key frame to node `ni'.  If MIC or encryption is required,
 * the PTK must be passed (otherwise it can be set to NULL.)
 */
int
ieee80211_send_eapol_key(struct ieee80211com *ic, struct mbuf *m,
    struct ieee80211_node *ni, const struct ieee80211_ptk *ptk)
{
	struct ifnet *ifp = &ic->ic_if;
	struct ether_header *eh;
	struct ieee80211_eapol_key *key;
	u_int16_t info;
	int len, error;

	M_PREPEND(m, sizeof(struct ether_header), M_DONTWAIT);
	if (m == NULL)
		return ENOMEM;
	/* no need to m_pullup here (ok by construction) */
	eh = mtod(m, struct ether_header *);
	eh->ether_type = htons(ETHERTYPE_EAPOL);
	IEEE80211_ADDR_COPY(eh->ether_shost, ic->ic_myaddr);
	IEEE80211_ADDR_COPY(eh->ether_dhost, ni->ni_macaddr);

	key = (struct ieee80211_eapol_key *)&eh[1];
	key->version = EAPOL_VERSION;
	key->type = EAPOL_KEY;
	key->desc = (ni->ni_rsnprotos == IEEE80211_PROTO_RSN) ?
	    EAPOL_KEY_DESC_IEEE80211 : EAPOL_KEY_DESC_WPA;

	info = BE_READ_2(key->info);
	/* use V3 descriptor if KDF is SHA256-based */
	if (ieee80211_is_sha256_akm(ni->ni_rsnakms))
		info |= EAPOL_KEY_DESC_V3;
	/* use V2 descriptor if pairwise or group cipher is CCMP */
	else if (ni->ni_rsncipher == IEEE80211_CIPHER_CCMP ||
	    ni->ni_rsngroupcipher == IEEE80211_CIPHER_CCMP)
		info |= EAPOL_KEY_DESC_V2;
	else
		info |= EAPOL_KEY_DESC_V1;
	BE_WRITE_2(key->info, info);

	len = m->m_len - sizeof(struct ether_header);
	BE_WRITE_2(key->paylen, len - sizeof(*key));
	BE_WRITE_2(key->len, len - 4);

#ifndef IEEE80211_STA_ONLY
	if (info & EAPOL_KEY_ENCRYPTED) {
		if (ni->ni_rsnprotos == IEEE80211_PROTO_WPA) {
			/* clear "Encrypted" bit for WPA */
			info &= ~EAPOL_KEY_ENCRYPTED;
			BE_WRITE_2(key->info, info);
		}
		ieee80211_eapol_key_encrypt(ic, key, ptk->kek);

		if ((info & EAPOL_KEY_VERSION_MASK) != EAPOL_KEY_DESC_V1) {
			/* AES Key Wrap adds 8 bytes + padding */
			m->m_pkthdr.len = m->m_len =
			    sizeof(*eh) + 4 + BE_READ_2(key->len);
		}
	}
#endif
	if (info & EAPOL_KEY_KEYMIC)
		ieee80211_eapol_key_mic(key, ptk->kck);

#ifndef IEEE80211_STA_ONLY
	/* start a 100ms timeout if an answer is expected from supplicant */
	if (info & EAPOL_KEY_KEYACK)
		timeout_add_msec(&ni->ni_eapol_to, 100);
#endif

	error = ifq_enqueue(&ifp->if_snd, m);
	if (error)
		return (error);
	if_start(ifp);
	return 0;
}

#ifndef IEEE80211_STA_ONLY
/*
 * Handle EAPOL-Key timeouts (no answer from supplicant).
 */
void
ieee80211_eapol_timeout(void *arg)
{
	struct ieee80211_node *ni = arg;
	struct ieee80211com *ic = ni->ni_ic;
	int s;

	DPRINTF(("no answer from station %s in state %d\n",
	    ether_sprintf(ni->ni_macaddr), ni->ni_rsn_state));

	s = splnet();

	switch (ni->ni_rsn_state) {
	case RSNA_PTKSTART:
	case RSNA_PTKCALCNEGOTIATING:
		(void)ieee80211_send_4way_msg1(ic, ni);
		break;
	case RSNA_PTKINITNEGOTIATING:
		(void)ieee80211_send_4way_msg3(ic, ni);
		break;
	}

	switch (ni->ni_rsn_gstate) {
	case RSNA_REKEYNEGOTIATING:
		(void)ieee80211_send_group_msg1(ic, ni);
		break;
	}

	splx(s);
}

/*
 * Add a GTK KDE to an EAPOL-Key frame (see Figure 144).
 */
u_int8_t *
ieee80211_add_gtk_kde(u_int8_t *frm, struct ieee80211_node *ni,
    const struct ieee80211_key *k)
{
	KASSERT(k->k_flags & IEEE80211_KEY_GROUP);

	*frm++ = IEEE80211_ELEMID_VENDOR;
	*frm++ = 6 + k->k_len;
	memcpy(frm, IEEE80211_OUI, 3); frm += 3;
	*frm++ = IEEE80211_KDE_GTK;
	*frm = k->k_id & 3;
	/*
	 * The TxRx flag for sending a GTK is always the opposite of whether
	 * the pairwise key is used for data encryption/integrity or not.
	 */
	if (ni->ni_rsncipher == IEEE80211_CIPHER_USEGROUP)
		*frm |= 1 << 2;	/* set the Tx bit */
	frm++;
	*frm++ = 0;	/* reserved */
	memcpy(frm, k->k_key, k->k_len);
	return frm + k->k_len;
}

/*
 * Add a PMKID KDE to an EAPOL-Key frame (see Figure 146).
 */
u_int8_t *
ieee80211_add_pmkid_kde(u_int8_t *frm, const u_int8_t *pmkid)
{
	*frm++ = IEEE80211_ELEMID_VENDOR;
	*frm++ = 20;
	memcpy(frm, IEEE80211_OUI, 3); frm += 3;
	*frm++ = IEEE80211_KDE_PMKID;
	memcpy(frm, pmkid, IEEE80211_PMKID_LEN);
	return frm + IEEE80211_PMKID_LEN;
}

/*
 * Add an IGTK KDE to an EAPOL-Key frame (see Figure 8-32a).
 */
u_int8_t *
ieee80211_add_igtk_kde(u_int8_t *frm, const struct ieee80211_key *k)
{
	KASSERT(k->k_flags & IEEE80211_KEY_IGTK);

	*frm++ = IEEE80211_ELEMID_VENDOR;
	*frm++ = 4 + 24;
	memcpy(frm, IEEE80211_OUI, 3); frm += 3;
	*frm++ = IEEE80211_KDE_IGTK;
	LE_WRITE_2(frm, k->k_id); frm += 2;
	LE_WRITE_6(frm, k->k_tsc); frm += 6;	/* IPN */
	memcpy(frm, k->k_key, 16);
	return frm + 16;
}
#endif	/* IEEE80211_STA_ONLY */

struct mbuf *
ieee80211_get_eapol_key(int flags, int type, u_int pktlen)
{
	struct mbuf *m;

	/* reserve space for 802.11 encapsulation and EAPOL-Key header */
	pktlen += sizeof(struct ieee80211_frame) + LLC_SNAPFRAMELEN +
	    sizeof(struct ieee80211_eapol_key);

	if (pktlen > MCLBYTES)
		panic("EAPOL-Key frame too large: %u", pktlen);
	MGETHDR(m, flags, type);
	if (m == NULL)
		return NULL;
	if (pktlen > MHLEN) {
		MCLGET(m, flags);
		if (!(m->m_flags & M_EXT))
			return m_free(m);
	}
	m->m_data += sizeof(struct ieee80211_frame) + LLC_SNAPFRAMELEN;
	return m;
}

#ifndef IEEE80211_STA_ONLY
/*
 * Send 4-Way Handshake Message 1 to the supplicant.
 */
int
ieee80211_send_4way_msg1(struct ieee80211com *ic, struct ieee80211_node *ni)
{
	struct ieee80211_eapol_key *key;
	struct mbuf *m;
	u_int16_t info, keylen;
	u_int8_t *frm;

	ni->ni_rsn_state = RSNA_PTKSTART;
	if (++ni->ni_rsn_retries > 3) {
		IEEE80211_SEND_MGMT(ic, ni, IEEE80211_FC0_SUBTYPE_DEAUTH,
		    IEEE80211_REASON_4WAY_TIMEOUT);
		ieee80211_node_leave(ic, ni);
		return 0;
	}
	m = ieee80211_get_eapol_key(M_DONTWAIT, MT_DATA,
	    (ni->ni_rsnprotos == IEEE80211_PROTO_RSN) ? 2 + 20 : 0);
	if (m == NULL)
		return ENOMEM;
	key = mtod(m, struct ieee80211_eapol_key *);
	memset(key, 0, sizeof(*key));

	info = EAPOL_KEY_PAIRWISE | EAPOL_KEY_KEYACK;
	BE_WRITE_2(key->info, info);

	/* copy the authenticator's nonce (ANonce) */
	memcpy(key->nonce, ni->ni_nonce, EAPOL_KEY_NONCE_LEN);

	keylen = ieee80211_cipher_keylen(ni->ni_rsncipher);
	BE_WRITE_2(key->keylen, keylen);

	frm = (u_int8_t *)&key[1];
	/* NB: WPA does not have PMKID KDE */
	if (ni->ni_rsnprotos == IEEE80211_PROTO_RSN &&
	    ieee80211_is_8021x_akm(ni->ni_rsnakms))
		frm = ieee80211_add_pmkid_kde(frm, ni->ni_pmkid);

	m->m_pkthdr.len = m->m_len = frm - (u_int8_t *)key;

	if (ic->ic_if.if_flags & IFF_DEBUG)
		printf("%s: sending msg %d/%d of the %s handshake to %s\n",
		    ic->ic_if.if_xname, 1, 4, "4-way",
		    ether_sprintf(ni->ni_macaddr));

	ni->ni_replaycnt++;
	BE_WRITE_8(key->replaycnt, ni->ni_replaycnt);

	return ieee80211_send_eapol_key(ic, m, ni, NULL);
}
#endif	/* IEEE80211_STA_ONLY */

/*
 * Send 4-Way Handshake Message 2 to the authenticator.
 */
int
ieee80211_send_4way_msg2(struct ieee80211com *ic, struct ieee80211_node *ni,
    const u_int8_t *replaycnt, const struct ieee80211_ptk *tptk)
{
	struct ieee80211_eapol_key *key;
	struct mbuf *m;
	u_int16_t info;
	u_int8_t *frm;

	ni->ni_rsn_supp_state = RSNA_SUPP_PTKNEGOTIATING;
	m = ieee80211_get_eapol_key(M_DONTWAIT, MT_DATA,
	    (ni->ni_rsnprotos == IEEE80211_PROTO_WPA) ?
		2 + IEEE80211_WPAIE_MAXLEN :
		2 + IEEE80211_RSNIE_MAXLEN);
	if (m == NULL)
		return ENOMEM;
	key = mtod(m, struct ieee80211_eapol_key *);
	memset(key, 0, sizeof(*key));

	info = EAPOL_KEY_PAIRWISE | EAPOL_KEY_KEYMIC;
	BE_WRITE_2(key->info, info);

	/* copy key replay counter from Message 1/4 */
	memcpy(key->replaycnt, replaycnt, 8);

	/* copy the supplicant's nonce (SNonce) */
	memcpy(key->nonce, ic->ic_nonce, EAPOL_KEY_NONCE_LEN);

	frm = (u_int8_t *)&key[1];
	/* add the WPA/RSN IE used in the (Re)Association Request */
	if (ni->ni_rsnprotos == IEEE80211_PROTO_WPA) {
		int keylen;
		frm = ieee80211_add_wpa(frm, ic, ni);
		/* WPA sets the key length field here */
		keylen = ieee80211_cipher_keylen(ni->ni_rsncipher);
		BE_WRITE_2(key->keylen, keylen);
	} else	/* RSN */
		frm = ieee80211_add_rsn(frm, ic, ni);

	m->m_pkthdr.len = m->m_len = frm - (u_int8_t *)key;

	if (ic->ic_if.if_flags & IFF_DEBUG)
		printf("%s: sending msg %d/%d of the %s handshake to %s\n",
		    ic->ic_if.if_xname, 2, 4, "4-way",
		    ether_sprintf(ni->ni_macaddr));

	return ieee80211_send_eapol_key(ic, m, ni, tptk);
}

#ifndef IEEE80211_STA_ONLY
/*
 * Send 4-Way Handshake Message 3 to the supplicant.
 */
int
ieee80211_send_4way_msg3(struct ieee80211com *ic, struct ieee80211_node *ni)
{
	struct ieee80211_eapol_key *key;
	struct ieee80211_key *k = NULL;
	struct mbuf *m;
	u_int16_t info, keylen;
	u_int8_t *frm;

	ni->ni_rsn_state = RSNA_PTKINITNEGOTIATING;
	if (++ni->ni_rsn_retries > 3) {
		IEEE80211_SEND_MGMT(ic, ni, IEEE80211_FC0_SUBTYPE_DEAUTH,
		    IEEE80211_REASON_4WAY_TIMEOUT);
		ieee80211_node_leave(ic, ni);
		return 0;
	}
	if (ni->ni_rsnprotos == IEEE80211_PROTO_RSN) {
		k = &ic->ic_nw_keys[ic->ic_def_txkey];
		m = ieee80211_get_eapol_key(M_DONTWAIT, MT_DATA,
		    2 + IEEE80211_RSNIE_MAXLEN + 2 + 6 + k->k_len + 15 +
		    ((ni->ni_flags & IEEE80211_NODE_MFP) ? 2 + 28 : 0));
	} else { /* WPA */
		m = ieee80211_get_eapol_key(M_DONTWAIT, MT_DATA,
		    2 + IEEE80211_WPAIE_MAXLEN +
		    ((ni->ni_flags & IEEE80211_NODE_MFP) ? 2 + 28 : 0));
	}
	if (m == NULL)
		return ENOMEM;
	key = mtod(m, struct ieee80211_eapol_key *);
	memset(key, 0, sizeof(*key));

	info = EAPOL_KEY_PAIRWISE | EAPOL_KEY_KEYACK | EAPOL_KEY_KEYMIC;
	if (ni->ni_rsncipher != IEEE80211_CIPHER_USEGROUP)
		info |= EAPOL_KEY_INSTALL;

	/* use same nonce as in Message 1 */
	memcpy(key->nonce, ni->ni_nonce, EAPOL_KEY_NONCE_LEN);

	ni->ni_replaycnt++;
	BE_WRITE_8(key->replaycnt, ni->ni_replaycnt);

	keylen = ieee80211_cipher_keylen(ni->ni_rsncipher);
	BE_WRITE_2(key->keylen, keylen);

	frm = (u_int8_t *)&key[1];
	/* add the WPA/RSN IE included in Beacon/Probe Response */
	if (ni->ni_rsnprotos == IEEE80211_PROTO_RSN) {
		frm = ieee80211_add_rsn(frm, ic, ic->ic_bss);
		/* encapsulate the GTK */
		frm = ieee80211_add_gtk_kde(frm, ni, k);
		LE_WRITE_6(key->rsc, k->k_tsc);
		/* encapsulate the IGTK if MFP was negotiated */
		if (ni->ni_flags & IEEE80211_NODE_MFP) {
			frm = ieee80211_add_igtk_kde(frm,
			    &ic->ic_nw_keys[ic->ic_igtk_kid]);
		}
		/* ask that the EAPOL-Key frame be encrypted */
		info |= EAPOL_KEY_ENCRYPTED | EAPOL_KEY_SECURE;
	} else	/* WPA */
		frm = ieee80211_add_wpa(frm, ic, ic->ic_bss);

	/* write the key info field */
	BE_WRITE_2(key->info, info);

	m->m_pkthdr.len = m->m_len = frm - (u_int8_t *)key;

	if (ic->ic_if.if_flags & IFF_DEBUG)
		printf("%s: sending msg %d/%d of the %s handshake to %s\n",
		    ic->ic_if.if_xname, 3, 4, "4-way",
		    ether_sprintf(ni->ni_macaddr));

	return ieee80211_send_eapol_key(ic, m, ni, &ni->ni_ptk);
}
#endif	/* IEEE80211_STA_ONLY */

/*
 * Send 4-Way Handshake Message 4 to the authenticator.
 */
int
ieee80211_send_4way_msg4(struct ieee80211com *ic, struct ieee80211_node *ni)
{
	struct ieee80211_eapol_key *key;
	struct mbuf *m;
	u_int16_t info;

	ni->ni_rsn_supp_state = RSNA_SUPP_PTKDONE;
	m = ieee80211_get_eapol_key(M_DONTWAIT, MT_DATA, 0);
	if (m == NULL)
		return ENOMEM;
	key = mtod(m, struct ieee80211_eapol_key *);
	memset(key, 0, sizeof(*key));

	info = EAPOL_KEY_PAIRWISE | EAPOL_KEY_KEYMIC;

	/* copy key replay counter from authenticator */
	BE_WRITE_8(key->replaycnt, ni->ni_replaycnt);

	if (ni->ni_rsnprotos == IEEE80211_PROTO_WPA) {
		int keylen;
		/* WPA sets the key length field here */
		keylen = ieee80211_cipher_keylen(ni->ni_rsncipher);
		BE_WRITE_2(key->keylen, keylen);
	} else
		info |= EAPOL_KEY_SECURE;

	/* write the key info field */
	BE_WRITE_2(key->info, info);

	/* empty key data field */
	m->m_pkthdr.len = m->m_len = sizeof(*key);

	if (ic->ic_if.if_flags & IFF_DEBUG)
		printf("%s: sending msg %d/%d of the %s handshake to %s\n",
		    ic->ic_if.if_xname, 4, 4, "4-way",
		    ether_sprintf(ni->ni_macaddr));

	return ieee80211_send_eapol_key(ic, m, ni, &ni->ni_ptk);
}

#ifndef IEEE80211_STA_ONLY
/*
 * Send Group Key Handshake Message 1 to the supplicant.
 */
int
ieee80211_send_group_msg1(struct ieee80211com *ic, struct ieee80211_node *ni)
{
	struct ieee80211_eapol_key *key;
	const struct ieee80211_key *k;
	struct mbuf *m;
	u_int16_t info;
	u_int8_t *frm;
	u_int8_t kid;

	ni->ni_rsn_gstate = RSNA_REKEYNEGOTIATING;
	if (++ni->ni_rsn_retries > 3) {
		IEEE80211_SEND_MGMT(ic, ni, IEEE80211_FC0_SUBTYPE_DEAUTH,
		    IEEE80211_REASON_GROUP_TIMEOUT);
		ieee80211_node_leave(ic, ni);
		return 0;
	}
	if (ni->ni_flags & IEEE80211_NODE_REKEY)
		kid = (ic->ic_def_txkey == 1) ? 2 : 1;
	else
		kid = ic->ic_def_txkey;
	k = &ic->ic_nw_keys[kid];

	m = ieee80211_get_eapol_key(M_DONTWAIT, MT_DATA,
	    ((ni->ni_rsnprotos == IEEE80211_PROTO_WPA) ?
		k->k_len : 2 + 6 + k->k_len) +
	    ((ni->ni_flags & IEEE80211_NODE_MFP) ? 2 + 28 : 0) +
	    15);
	if (m == NULL)
		return ENOMEM;
	key = mtod(m, struct ieee80211_eapol_key *);
	memset(key, 0, sizeof(*key));

	info = EAPOL_KEY_KEYACK | EAPOL_KEY_KEYMIC | EAPOL_KEY_SECURE |
	    EAPOL_KEY_ENCRYPTED;

	ni->ni_replaycnt++;
	BE_WRITE_8(key->replaycnt, ni->ni_replaycnt);

	frm = (u_int8_t *)&key[1];
	if (ni->ni_rsnprotos == IEEE80211_PROTO_WPA) {
		/* WPA does not have GTK KDE */
		BE_WRITE_2(key->keylen, k->k_len);
		memcpy(frm, k->k_key, k->k_len);
		frm += k->k_len;
		info |= (k->k_id & 0x3) << EAPOL_KEY_WPA_KID_SHIFT;
		if (ni->ni_rsncipher == IEEE80211_CIPHER_USEGROUP)
			info |= EAPOL_KEY_WPA_TX;
	} else {	/* RSN */
		frm = ieee80211_add_gtk_kde(frm, ni, k);
		if (ni->ni_flags & IEEE80211_NODE_MFP) {
			if (ni->ni_flags & IEEE80211_NODE_REKEY)
				kid = (ic->ic_igtk_kid == 4) ? 5 : 4;
			else
				kid = ic->ic_igtk_kid;
			frm = ieee80211_add_igtk_kde(frm,
			    &ic->ic_nw_keys[kid]);
		}
	}
	/* RSC = last transmit sequence number for the GTK */
	LE_WRITE_6(key->rsc, k->k_tsc);

	/* write the key info field */
	BE_WRITE_2(key->info, info);

	m->m_pkthdr.len = m->m_len = frm - (u_int8_t *)key;

	if (ic->ic_if.if_flags & IFF_DEBUG)
		printf("%s: sending msg %d/%d of the %s handshake to %s\n",
		    ic->ic_if.if_xname, 1, 2, "group key",
		    ether_sprintf(ni->ni_macaddr));

	return ieee80211_send_eapol_key(ic, m, ni, &ni->ni_ptk);
}
#endif	/* IEEE80211_STA_ONLY */

/*
 * Send Group Key Handshake Message 2 to the authenticator.
 */
int
ieee80211_send_group_msg2(struct ieee80211com *ic, struct ieee80211_node *ni,
    const struct ieee80211_key *k)
{
	struct ieee80211_eapol_key *key;
	u_int16_t info;
	struct mbuf *m;

	m = ieee80211_get_eapol_key(M_DONTWAIT, MT_DATA, 0);
	if (m == NULL)
		return ENOMEM;
	key = mtod(m, struct ieee80211_eapol_key *);
	memset(key, 0, sizeof(*key));

	info = EAPOL_KEY_KEYMIC | EAPOL_KEY_SECURE;

	/* copy key replay counter from authenticator */
	BE_WRITE_8(key->replaycnt, ni->ni_replaycnt);

	if (ni->ni_rsnprotos == IEEE80211_PROTO_WPA) {
		/* WPA sets the key length and key id fields here */
		BE_WRITE_2(key->keylen, k->k_len);
		info |= (k->k_id & 3) << EAPOL_KEY_WPA_KID_SHIFT;
	}

	/* write the key info field */
	BE_WRITE_2(key->info, info);

	/* empty key data field */
	m->m_pkthdr.len = m->m_len = sizeof(*key);

	if (ic->ic_if.if_flags & IFF_DEBUG)
		printf("%s: sending msg %d/%d of the %s handshake to %s\n",
		    ic->ic_if.if_xname, 2, 2, "group key",
		    ether_sprintf(ni->ni_macaddr));

	return ieee80211_send_eapol_key(ic, m, ni, &ni->ni_ptk);
}

/*
 * EAPOL-Key Request frames are sent by the supplicant to request that the
 * authenticator initiates either a 4-Way Handshake or Group Key Handshake,
 * or to report a MIC failure in a TKIP MSDU.
 */
int
ieee80211_send_eapol_key_req(struct ieee80211com *ic,
    struct ieee80211_node *ni, u_int16_t info, u_int64_t tsc)
{
	struct ieee80211_eapol_key *key;
	struct mbuf *m;

	m = ieee80211_get_eapol_key(M_DONTWAIT, MT_DATA, 0);
	if (m == NULL)
		return ENOMEM;
	key = mtod(m, struct ieee80211_eapol_key *);
	memset(key, 0, sizeof(*key));

	info |= EAPOL_KEY_REQUEST;
	BE_WRITE_2(key->info, info);

	/* in case of TKIP MIC failure, fill the RSC field */
	if (info & EAPOL_KEY_ERROR)
		LE_WRITE_6(key->rsc, tsc);

	/* use our separate key replay counter for key requests */
	BE_WRITE_8(key->replaycnt, ni->ni_reqreplaycnt);
	ni->ni_reqreplaycnt++;

	/* empty key data field */
	m->m_pkthdr.len = m->m_len = sizeof(*key);

	if (ic->ic_if.if_flags & IFF_DEBUG)
		printf("%s: sending EAPOL-Key request to %s\n",
		    ic->ic_if.if_xname, ether_sprintf(ni->ni_macaddr));

	return ieee80211_send_eapol_key(ic, m, ni, &ni->ni_ptk);
}
