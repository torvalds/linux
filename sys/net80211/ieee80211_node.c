/*	$OpenBSD: ieee80211_node.c,v 1.203 2025/08/01 20:39:26 stsp Exp $	*/
/*	$NetBSD: ieee80211_node.c,v 1.14 2004/05/09 09:18:47 dyoung Exp $	*/

/*-
 * Copyright (c) 2001 Atsushi Onoe
 * Copyright (c) 2002, 2003 Sam Leffler, Errno Consulting
 * Copyright (c) 2008 Damien Bergamini
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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

#include "bridge.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/endian.h>
#include <sys/errno.h>
#include <sys/sysctl.h>
#include <sys/tree.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#if NBRIDGE > 0
#include <net/if_bridge.h>
#endif

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_priv.h>

struct ieee80211_node *ieee80211_node_alloc(struct ieee80211com *);
void ieee80211_node_free(struct ieee80211com *, struct ieee80211_node *);
void ieee80211_node_copy(struct ieee80211com *, struct ieee80211_node *,
    const struct ieee80211_node *);
void ieee80211_choose_rsnparams(struct ieee80211com *);
u_int8_t ieee80211_node_getrssi(struct ieee80211com *,
    const struct ieee80211_node *);
int ieee80211_node_checkrssi(struct ieee80211com *,
    const struct ieee80211_node *);
int ieee80211_ess_is_better(struct ieee80211com *ic, struct ieee80211_node *,
    struct ieee80211_node *);
void ieee80211_node_set_timeouts(struct ieee80211_node *);
void ieee80211_setup_node(struct ieee80211com *, struct ieee80211_node *,
    const u_int8_t *);
struct ieee80211_node *ieee80211_alloc_node_helper(struct ieee80211com *);
void ieee80211_node_free_unref_cb(struct ieee80211_node *);
void ieee80211_node_tx_flushed(struct ieee80211com *, struct ieee80211_node *);
void ieee80211_node_switch_bss(struct ieee80211com *, struct ieee80211_node *);
void ieee80211_node_addba_request(struct ieee80211_node *, int);
void ieee80211_node_addba_request_ac_be_to(void *);
void ieee80211_node_addba_request_ac_bk_to(void *);
void ieee80211_node_addba_request_ac_vi_to(void *);
void ieee80211_node_addba_request_ac_vo_to(void *);
void ieee80211_needs_auth(struct ieee80211com *, struct ieee80211_node *);
#ifndef IEEE80211_STA_ONLY
void ieee80211_node_join_ht(struct ieee80211com *, struct ieee80211_node *);
void ieee80211_node_join_rsn(struct ieee80211com *, struct ieee80211_node *);
void ieee80211_node_join_11g(struct ieee80211com *, struct ieee80211_node *);
void ieee80211_node_leave_ht(struct ieee80211com *, struct ieee80211_node *);
void ieee80211_node_leave_vht(struct ieee80211com *, struct ieee80211_node *);
void ieee80211_node_leave_rsn(struct ieee80211com *, struct ieee80211_node *);
void ieee80211_node_leave_11g(struct ieee80211com *, struct ieee80211_node *);
void ieee80211_node_leave_pwrsave(struct ieee80211com *,
    struct ieee80211_node *);
void ieee80211_inact_timeout(void *);
void ieee80211_node_cache_timeout(void *);
#endif
void ieee80211_clean_inactive_nodes(struct ieee80211com *, int);

#ifndef IEEE80211_STA_ONLY
void
ieee80211_inact_timeout(void *arg)
{
	struct ieee80211com *ic = arg;
	struct ieee80211_node *ni, *next_ni;
	int s;

	s = splnet();
	for (ni = RBT_MIN(ieee80211_tree, &ic->ic_tree);
	    ni != NULL; ni = next_ni) {
		next_ni = RBT_NEXT(ieee80211_tree, ni);
		if (ni->ni_refcnt > 0)
			continue;
		if (ni->ni_inact < IEEE80211_INACT_MAX)
			ni->ni_inact++;
	}
	splx(s);

	timeout_add_sec(&ic->ic_inact_timeout, IEEE80211_INACT_WAIT);
}

void
ieee80211_node_cache_timeout(void *arg)
{
	struct ieee80211com *ic = arg;

	ieee80211_clean_nodes(ic, 1);
	timeout_add_sec(&ic->ic_node_cache_timeout, IEEE80211_CACHE_WAIT);
}
#endif

/*
 * For debug purposes
 */
void
ieee80211_print_ess(struct ieee80211_ess *ess)
{
	ieee80211_print_essid(ess->essid, ess->esslen);
	if (ess->flags & IEEE80211_F_RSNON) {
		printf(" wpa");
		if (ess->rsnprotos & IEEE80211_PROTO_RSN)
			printf(",wpa2");
		if (ess->rsnprotos & IEEE80211_PROTO_WPA)
			printf(",wpa1");

		if (ess->rsnakms & IEEE80211_AKM_8021X ||
		    ess->rsnakms & IEEE80211_AKM_SHA256_8021X)
			printf(",802.1x");
		printf(" ");

		if (ess->rsnciphers & IEEE80211_CIPHER_USEGROUP)
			printf(" usegroup");
		if (ess->rsnciphers & IEEE80211_CIPHER_WEP40)
			printf(" wep40");
		if (ess->rsnciphers & IEEE80211_CIPHER_WEP104)
			printf(" wep104");
		if (ess->rsnciphers & IEEE80211_CIPHER_TKIP)
			printf(" tkip");
		if (ess->rsnciphers & IEEE80211_CIPHER_CCMP)
			printf(" ccmp");
	}
	if (ess->flags & IEEE80211_F_WEPON) {
		int i = ess->def_txkey;

		printf(" wep,");
		if (ess->nw_keys[i].k_cipher & IEEE80211_CIPHER_WEP40)
			printf("wep40");
		if (ess->nw_keys[i].k_cipher & IEEE80211_CIPHER_WEP104)
			printf("wep104");
	}
	if (ess->flags == 0)
		printf(" clear");
	printf("\n");
}

void
ieee80211_print_ess_list(struct ieee80211com *ic)
{
	struct ifnet		*ifp = &ic->ic_if;
	struct ieee80211_ess	*ess;

	printf("%s: known networks\n", ifp->if_xname);
	TAILQ_FOREACH(ess, &ic->ic_ess, ess_next) {
		ieee80211_print_ess(ess);
	}
}

struct ieee80211_ess *
ieee80211_get_ess(struct ieee80211com *ic, const char *nwid, int len)
{
	struct ieee80211_ess	*ess;

	TAILQ_FOREACH(ess, &ic->ic_ess, ess_next) {
		if (len == ess->esslen &&
		    memcmp(ess->essid, nwid, ess->esslen) == 0)
			return ess;
	}

	return NULL;
}

void
ieee80211_del_ess(struct ieee80211com *ic, char *nwid, int len, int all)
{
	struct ieee80211_ess *ess, *next;

	TAILQ_FOREACH_SAFE(ess, &ic->ic_ess, ess_next, next) {
		if (all == 1 || (ess->esslen == len &&
		    memcmp(ess->essid, nwid, len) == 0)) {
			TAILQ_REMOVE(&ic->ic_ess, ess, ess_next);
			explicit_bzero(ess, sizeof(*ess));
			free(ess, M_DEVBUF, sizeof(*ess));
			if (TAILQ_EMPTY(&ic->ic_ess))
				ic->ic_flags &= ~IEEE80211_F_AUTO_JOIN;
			if (all != 1)
				return;
		}
	}
}

/* Keep in sync with ieee80211_ioctl.c:ieee80211_ioctl_setnwkeys() */
static int
ieee80211_ess_setnwkeys(struct ieee80211_ess *ess,
    const struct ieee80211_nwkey *nwkey)
{
	struct ieee80211_key *k;
	int error, i;

	if (nwkey->i_wepon == IEEE80211_NWKEY_OPEN) {
		if (!(ess->flags & IEEE80211_F_WEPON))
			return 0;
		ess->flags &= ~IEEE80211_F_WEPON;
		return ENETRESET;
	}
	if (nwkey->i_defkid < 1 || nwkey->i_defkid > IEEE80211_WEP_NKID)
		return EINVAL;

	for (i = 0; i < IEEE80211_WEP_NKID; i++) {
		if (nwkey->i_key[i].i_keylen == 0 ||
		    nwkey->i_key[i].i_keydat == NULL)
			continue;	/* entry not set */
		if (nwkey->i_key[i].i_keylen > IEEE80211_KEYBUF_SIZE)
			return EINVAL;

		/* map wep key to ieee80211_key */
		k = &ess->nw_keys[i];
		memset(k, 0, sizeof(*k));
		if (nwkey->i_key[i].i_keylen <= 5)
			k->k_cipher = IEEE80211_CIPHER_WEP40;
		else
			k->k_cipher = IEEE80211_CIPHER_WEP104;
		k->k_len = ieee80211_cipher_keylen(k->k_cipher);
		k->k_flags = IEEE80211_KEY_GROUP | IEEE80211_KEY_TX;
		error = copyin(nwkey->i_key[i].i_keydat, k->k_key, k->k_len);
		if (error != 0)
			return error;
	}
	ess->def_txkey = nwkey->i_defkid - 1;
	ess->flags |= IEEE80211_F_WEPON;

	return ENETRESET;
}


/* Keep in sync with ieee80211_ioctl.c:ieee80211_ioctl_setwpaparms() */
static int
ieee80211_ess_setwpaparms(struct ieee80211_ess *ess,
    const struct ieee80211_wpaparams *wpa)
{
	if (!wpa->i_enabled) {
		if (!(ess->flags & IEEE80211_F_RSNON))
			return 0;
		ess->flags &= ~IEEE80211_F_RSNON;
		ess->rsnprotos = 0;
		ess->rsnakms = 0;
		ess->rsngroupcipher = 0;
		ess->rsnciphers = 0;
		return ENETRESET;
	}

	ess->rsnprotos = 0;
	if (wpa->i_protos & IEEE80211_WPA_PROTO_WPA1)
		ess->rsnprotos |= IEEE80211_PROTO_WPA;
	if (wpa->i_protos & IEEE80211_WPA_PROTO_WPA2)
		ess->rsnprotos |= IEEE80211_PROTO_RSN;
	if (ess->rsnprotos == 0)	/* set to default (RSN) */
		ess->rsnprotos = IEEE80211_PROTO_RSN;

	ess->rsnakms = 0;
	if (wpa->i_akms & IEEE80211_WPA_AKM_PSK)
		ess->rsnakms |= IEEE80211_AKM_PSK;
	if (wpa->i_akms & IEEE80211_WPA_AKM_SHA256_PSK)
		ess->rsnakms |= IEEE80211_AKM_SHA256_PSK;
	if (wpa->i_akms & IEEE80211_WPA_AKM_8021X)
		ess->rsnakms |= IEEE80211_AKM_8021X;
	if (wpa->i_akms & IEEE80211_WPA_AKM_SHA256_8021X)
		ess->rsnakms |= IEEE80211_AKM_SHA256_8021X;
	if (wpa->i_akms & IEEE80211_WPA_AKM_SAE)
		ess->rsnakms |= IEEE80211_AKM_SAE;
	if (ess->rsnakms == 0)	/* set to default (PSK) */
		ess->rsnakms = IEEE80211_AKM_PSK;

	if (wpa->i_groupcipher == IEEE80211_WPA_CIPHER_WEP40)
		ess->rsngroupcipher = IEEE80211_CIPHER_WEP40;
	else if (wpa->i_groupcipher == IEEE80211_WPA_CIPHER_TKIP)
		ess->rsngroupcipher = IEEE80211_CIPHER_TKIP;
	else if (wpa->i_groupcipher == IEEE80211_WPA_CIPHER_CCMP)
		ess->rsngroupcipher = IEEE80211_CIPHER_CCMP;
	else if (wpa->i_groupcipher == IEEE80211_WPA_CIPHER_WEP104)
		ess->rsngroupcipher = IEEE80211_CIPHER_WEP104;
	else  {	/* set to default */
		if (ess->rsnprotos & IEEE80211_PROTO_WPA)
			ess->rsngroupcipher = IEEE80211_CIPHER_TKIP;
		else
			ess->rsngroupcipher = IEEE80211_CIPHER_CCMP;
	}

	ess->rsnciphers = 0;
	if (wpa->i_ciphers & IEEE80211_WPA_CIPHER_TKIP)
		ess->rsnciphers |= IEEE80211_CIPHER_TKIP;
	if (wpa->i_ciphers & IEEE80211_WPA_CIPHER_CCMP)
		ess->rsnciphers |= IEEE80211_CIPHER_CCMP;
	if (wpa->i_ciphers & IEEE80211_WPA_CIPHER_USEGROUP)
		ess->rsnciphers = IEEE80211_CIPHER_USEGROUP;
	if (ess->rsnciphers == 0) { /* set to default (CCMP, TKIP if WPA1) */
		ess->rsnciphers = IEEE80211_CIPHER_CCMP;
		if (ess->rsnprotos & IEEE80211_PROTO_WPA)
			ess->rsnciphers |= IEEE80211_CIPHER_TKIP;
	}

	ess->flags |= IEEE80211_F_RSNON;

	if (ess->rsnakms &
	    (IEEE80211_AKM_8021X|IEEE80211_WPA_AKM_SHA256_8021X))
		ess->flags |= IEEE80211_JOIN_8021X;

	return ENETRESET;
}

static void
ieee80211_ess_clear_wep(struct ieee80211_ess *ess)
{
	int i;

	/* Disable WEP */
	for (i = 0; i < IEEE80211_WEP_NKID; i++) {
		explicit_bzero(&ess->nw_keys[i], sizeof(ess->nw_keys[0]));
	}
	ess->def_txkey = 0;
	ess->flags &= ~IEEE80211_F_WEPON;
}

static void
ieee80211_ess_clear_wpa(struct ieee80211_ess *ess)
{
	/* Disable WPA */
	ess->rsnprotos = ess->rsnakms = ess->rsngroupcipher =
	    ess->rsnciphers = 0;
	explicit_bzero(ess->psk, sizeof(ess->psk));
	ess->flags &= ~(IEEE80211_F_PSK | IEEE80211_F_RSNON);
}

int
ieee80211_add_ess(struct ieee80211com *ic, struct ieee80211_join *join)
{
	struct ieee80211_ess *ess;
	int new = 0, ness = 0;

	/* only valid for station (aka, client) mode */
	if (ic->ic_opmode != IEEE80211_M_STA)
		return (0);

	TAILQ_FOREACH(ess, &ic->ic_ess, ess_next) {
		if (ess->esslen == join->i_len &&
		    memcmp(ess->essid, join->i_nwid, ess->esslen) == 0)
			break;
		ness++;
	}

	if (ess == NULL) {
		/* if not found, and wpa/wep are set, then return */
		if ((join->i_flags & IEEE80211_JOIN_WPA) &&
		    (join->i_flags & IEEE80211_JOIN_NWKEY)) {
			return (EINVAL);
		}
		if (ness > IEEE80211_CACHE_SIZE)
			return (ERANGE);
		new = 1;
		ess = malloc(sizeof(*ess), M_DEVBUF, M_NOWAIT|M_ZERO);
		if (ess == NULL)
			return (ENOMEM);
		memcpy(ess->essid, join->i_nwid, join->i_len);
		ess->esslen = join->i_len;
	}

	if (join->i_flags & IEEE80211_JOIN_WPA) {
		if (join->i_wpaparams.i_enabled) {
			if (!(ic->ic_caps & IEEE80211_C_RSN)) {
				free(ess, M_DEVBUF, sizeof(*ess));
				return ENODEV;
			}
			ieee80211_ess_setwpaparms(ess,
			    &join->i_wpaparams);
			if (join->i_flags & IEEE80211_JOIN_WPAPSK) {
				ess->flags |= IEEE80211_F_PSK;
				explicit_bzero(ess->psk, sizeof(ess->psk));
				memcpy(ess->psk, &join->i_wpapsk.i_psk,
				    sizeof(ess->psk));
			}
			ieee80211_ess_clear_wep(ess);
		} else {
			ieee80211_ess_clear_wpa(ess);
		}
	} else if (join->i_flags & IEEE80211_JOIN_NWKEY) {
		if (join->i_nwkey.i_wepon) {
			if (!(ic->ic_caps & IEEE80211_C_WEP)) {
				free(ess, M_DEVBUF, sizeof(*ess));
				return ENODEV;
			}
			ieee80211_ess_setnwkeys(ess, &join->i_nwkey);
			ieee80211_ess_clear_wpa(ess);
		} else {
			ieee80211_ess_clear_wep(ess);
		}
	}

	if (new)
		TAILQ_INSERT_TAIL(&ic->ic_ess, ess, ess_next);

	return (0);
}

uint8_t
ieee80211_ess_adjust_rssi(struct ieee80211com *ic, struct ieee80211_node *ni)
{
	uint8_t rssi = ni->ni_rssi;

	/*
	 * Slightly punish 2 GHz RSSI values since they are usually
	 * stronger than 5 GHz RSSI values.
	 */
	if (IEEE80211_IS_CHAN_2GHZ(ni->ni_chan)) {
		if (ic->ic_max_rssi) {
			uint8_t p = (5 * ic->ic_max_rssi) / 100;
	 		if (rssi >= p)
				rssi -= p; /* punish by 5% */
		} else  {
			if (rssi >= 8)
				rssi -= 8; /* punish by 8 dBm */
		}
	}

	return rssi;
}

int
ieee80211_ess_calculate_score(struct ieee80211com *ic,
    struct ieee80211_node *ni)
{
	int score = 0;
	uint8_t	min_5ghz_rssi;

	if (ic->ic_max_rssi)
		min_5ghz_rssi = IEEE80211_RSSI_THRES_RATIO_5GHZ;
	else
		min_5ghz_rssi = (uint8_t)IEEE80211_RSSI_THRES_5GHZ;

	/* not using join any */
	if (ieee80211_get_ess(ic, ni->ni_essid, ni->ni_esslen))
		score += 32;

	/* Calculate the crypto score */
	if (ni->ni_rsnprotos & IEEE80211_PROTO_RSN)
		score += 16;
	if (ni->ni_rsnprotos & IEEE80211_PROTO_WPA)
		score += 8;
	if (ni->ni_capinfo & IEEE80211_CAPINFO_PRIVACY)
		score += 4;

	/* 5GHz with a good signal */
	if (IEEE80211_IS_CHAN_5GHZ(ni->ni_chan) &&
	    ni->ni_rssi > min_5ghz_rssi)
		score += 2;

	/* HT/VHT available */
	if (ieee80211_node_supports_ht(ni))
		score++;
	if (ieee80211_node_supports_vht(ni))
		score++;

	/* Boost this AP if it had no auth/assoc failures in the past. */
	if (ni->ni_fails == 0)
		score += 21;

	return score;
}

/*
 * Given two APs, determine the "better" one of the two.
 * We compute a score based on the following attributes:
 *
 *  crypto: wpa2 > wpa1 > wep > open
 *  band: 5 GHz > 2 GHz provided 5 GHz rssi is above threshold
 *  supported standard revisions: 11ac > 11n > 11a/b/g
 *  rssi: rssi1 > rssi2 as a numeric comparison with a slight
 *         disadvantage for 2 GHz APs
 *
 * Crypto carries most weight, followed by band, followed by rssi.
 */
int
ieee80211_ess_is_better(struct ieee80211com *ic,
    struct ieee80211_node *nicur, struct ieee80211_node *nican)
{
	struct ifnet		*ifp = &ic->ic_if;
	int			 score_cur = 0, score_can = 0;
	int			 cur_rssi, can_rssi;

	score_cur = ieee80211_ess_calculate_score(ic, nicur);
	score_can = ieee80211_ess_calculate_score(ic, nican);

	cur_rssi = ieee80211_ess_adjust_rssi(ic, nicur);
	can_rssi = ieee80211_ess_adjust_rssi(ic, nican);

	if (can_rssi > cur_rssi)
		score_can++;

	if ((ifp->if_flags & IFF_DEBUG) && (score_can <= score_cur)) {
		printf("%s: AP %s ", ifp->if_xname,
		    ether_sprintf(nican->ni_bssid));
		ieee80211_print_essid(nican->ni_essid, nican->ni_esslen);
		printf(" score %d\n", score_can);
	}

	return score_can > score_cur;
}

/* Determine whether a candidate AP belongs to a given ESS. */
int
ieee80211_match_ess(struct ieee80211_ess *ess, struct ieee80211_node *ni)
{
	if (ess->esslen != 0 &&
	    (ess->esslen != ni->ni_esslen ||
	    memcmp(ess->essid, ni->ni_essid, ess->esslen) != 0)) {
		ni->ni_assoc_fail |= IEEE80211_NODE_ASSOCFAIL_ESSID;
		return 0;
	}

	if (ess->flags & (IEEE80211_F_PSK | IEEE80211_F_RSNON)) {
		/* Ensure same WPA version. */
		if ((ni->ni_rsnprotos & IEEE80211_PROTO_RSN) &&
		    (ess->rsnprotos & IEEE80211_PROTO_RSN) == 0) {
			ni->ni_assoc_fail |= IEEE80211_NODE_ASSOCFAIL_WPA_PROTO;
			return 0;
		}
		if ((ni->ni_rsnprotos & IEEE80211_PROTO_WPA) &&
		    (ess->rsnprotos & IEEE80211_PROTO_WPA) == 0) {
			ni->ni_assoc_fail |= IEEE80211_NODE_ASSOCFAIL_WPA_PROTO;
			return 0;
		}
	} else if (ess->flags & IEEE80211_F_WEPON) {
		if ((ni->ni_capinfo & IEEE80211_CAPINFO_PRIVACY) == 0) {
			ni->ni_assoc_fail |= IEEE80211_NODE_ASSOCFAIL_PRIVACY;
			return 0;
		}
	} else {
		if ((ni->ni_capinfo & IEEE80211_CAPINFO_PRIVACY) != 0) {
			ni->ni_assoc_fail |= IEEE80211_NODE_ASSOCFAIL_PRIVACY;
			return 0;
		}
	}

	if (ess->esslen == 0 &&
	    (ni->ni_capinfo & IEEE80211_CAPINFO_PRIVACY) != 0) {
		ni->ni_assoc_fail |= IEEE80211_NODE_ASSOCFAIL_PRIVACY;
		return 0;
	}

	return 1;
}

void
ieee80211_switch_ess(struct ieee80211com *ic)
{
	struct ifnet		*ifp = &ic->ic_if;
	struct ieee80211_ess	*ess, *seless = NULL;
	struct ieee80211_node	*ni, *selni = NULL;

	if (!ISSET(ifp->if_flags, IFF_RUNNING))
		return;

	/* Find the best AP matching an entry on our ESS join list. */
	RBT_FOREACH(ni, ieee80211_tree, &ic->ic_tree) {
		if ((ic->ic_flags & IEEE80211_F_DESBSSID) &&
		    !IEEE80211_ADDR_EQ(ic->ic_des_bssid, ni->ni_bssid))
			continue;

		TAILQ_FOREACH(ess, &ic->ic_ess, ess_next) {
			if (ieee80211_match_ess(ess, ni))
				break;
		}
		if (ess == NULL)
			continue;

		/*
		 * Operate only on ic_des_essid if auto-join is disabled.
		 * We might have a password stored for this network.
		 */
		if (!ISSET(ic->ic_flags, IEEE80211_F_AUTO_JOIN)) {
			if (ic->ic_des_esslen == ni->ni_esslen &&
			    memcmp(ic->ic_des_essid, ni->ni_essid,
			    ni->ni_esslen) == 0) {
				ieee80211_set_ess(ic, ess, ni);
				return;
			}
			continue;
		}

		if (selni == NULL) {
			seless = ess;
			selni = ni;
			continue;
		}

		if (ieee80211_ess_is_better(ic, selni, ni)) {
			seless = ess;
			selni = ni;
		}
	}

	if (selni && seless && !(selni->ni_esslen == ic->ic_des_esslen &&
	    (memcmp(ic->ic_des_essid, selni->ni_essid,
	     IEEE80211_NWID_LEN) == 0))) {
		if (ifp->if_flags & IFF_DEBUG) {
			printf("%s: best AP %s ", ifp->if_xname,
			    ether_sprintf(selni->ni_bssid));
			ieee80211_print_essid(selni->ni_essid,
			    selni->ni_esslen);
			printf(" score %d\n",
			    ieee80211_ess_calculate_score(ic, selni));
			printf("%s: switching to network ", ifp->if_xname);
			ieee80211_print_essid(selni->ni_essid,
			    selni->ni_esslen);
			if (seless->esslen == 0)
				printf(" via join any");
			printf("\n");

		}
		ieee80211_set_ess(ic, seless, selni);
	}
}

void
ieee80211_set_ess(struct ieee80211com *ic, struct ieee80211_ess *ess, 
    struct ieee80211_node *ni)
{
	memset(ic->ic_des_essid, 0, IEEE80211_NWID_LEN);
	ic->ic_des_esslen = ni->ni_esslen;
	memcpy(ic->ic_des_essid, ni->ni_essid, ic->ic_des_esslen);

	ieee80211_disable_wep(ic);
	ieee80211_disable_rsn(ic);

	if (ess->flags & IEEE80211_F_RSNON) {
		explicit_bzero(ic->ic_psk, sizeof(ic->ic_psk));
		memcpy(ic->ic_psk, ess->psk, sizeof(ic->ic_psk));

		ic->ic_rsnprotos = ess->rsnprotos;
		ic->ic_rsnakms = ess->rsnakms;
		ic->ic_rsngroupcipher = ess->rsngroupcipher;
		ic->ic_rsnciphers = ess->rsnciphers;
		ic->ic_flags |= IEEE80211_F_RSNON;
		if (ess->flags & IEEE80211_F_PSK)
			ic->ic_flags |= IEEE80211_F_PSK;
	} else if (ess->flags & IEEE80211_F_WEPON) {
		struct ieee80211_key	*k;
		int			 i;

		for (i = 0; i < IEEE80211_WEP_NKID; i++) {
			k = &ic->ic_nw_keys[i];
			if (k->k_cipher != IEEE80211_CIPHER_NONE)
				(*ic->ic_delete_key)(ic, NULL, k);
			memcpy(&ic->ic_nw_keys[i], &ess->nw_keys[i],
			    sizeof(struct ieee80211_key));
			if (k->k_cipher != IEEE80211_CIPHER_NONE)
				(*ic->ic_set_key)(ic, NULL, k);
		}
		ic->ic_def_txkey = ess->def_txkey;
		ic->ic_flags |= IEEE80211_F_WEPON;
	}
}

void
ieee80211_deselect_ess(struct ieee80211com *ic)
{
	memset(ic->ic_des_essid, 0, IEEE80211_NWID_LEN);
	ic->ic_des_esslen = 0;
	ieee80211_disable_wep(ic);
	ieee80211_disable_rsn(ic);
}

void
ieee80211_node_attach(struct ifnet *ifp)
{
	struct ieee80211com *ic = (void *)ifp;
#ifndef IEEE80211_STA_ONLY
	int size;
#endif

	RBT_INIT(ieee80211_tree, &ic->ic_tree);
	ic->ic_node_alloc = ieee80211_node_alloc;
	ic->ic_node_free = ieee80211_node_free;
	ic->ic_node_copy = ieee80211_node_copy;
	ic->ic_node_getrssi = ieee80211_node_getrssi;
	ic->ic_node_checkrssi = ieee80211_node_checkrssi;
	ic->ic_scangen = 1;
	ic->ic_max_nnodes = ieee80211_cache_size;

	if (ic->ic_max_aid == 0)
		ic->ic_max_aid = IEEE80211_AID_DEF;
	else if (ic->ic_max_aid > IEEE80211_AID_MAX)
		ic->ic_max_aid = IEEE80211_AID_MAX;
#ifndef IEEE80211_STA_ONLY
	size = howmany(ic->ic_max_aid, 32) * sizeof(u_int32_t);
	ic->ic_aid_bitmap = malloc(size, M_DEVBUF, M_NOWAIT | M_ZERO);
	if (ic->ic_aid_bitmap == NULL) {
		/* XXX no way to recover */
		printf("%s: no memory for AID bitmap!\n", __func__);
		ic->ic_max_aid = 0;
	}
	if (ic->ic_caps & (IEEE80211_C_HOSTAP | IEEE80211_C_IBSS)) {
		ic->ic_tim_len = howmany(ic->ic_max_aid, 8);
		ic->ic_tim_bitmap = malloc(ic->ic_tim_len, M_DEVBUF,
		    M_NOWAIT | M_ZERO);
		if (ic->ic_tim_bitmap == NULL) {
			printf("%s: no memory for TIM bitmap!\n", __func__);
			ic->ic_tim_len = 0;
		} else
			ic->ic_set_tim = ieee80211_set_tim;
		timeout_set(&ic->ic_rsn_timeout,
		    ieee80211_gtk_rekey_timeout, ic);
		timeout_set(&ic->ic_inact_timeout,
		    ieee80211_inact_timeout, ic);
		timeout_set(&ic->ic_node_cache_timeout,
		    ieee80211_node_cache_timeout, ic);
	}
#endif
	TAILQ_INIT(&ic->ic_ess);
}

struct ieee80211_node *
ieee80211_alloc_node_helper(struct ieee80211com *ic)
{
	struct ieee80211_node *ni;
	if (ic->ic_nnodes >= ic->ic_max_nnodes)
		ieee80211_clean_nodes(ic, 0);
	if (ic->ic_nnodes >= ic->ic_max_nnodes)
		return NULL;
	ni = (*ic->ic_node_alloc)(ic);
	return ni;
}

void
ieee80211_node_lateattach(struct ifnet *ifp)
{
	struct ieee80211com *ic = (void *)ifp;
	struct ieee80211_node *ni;

	ni = ieee80211_alloc_node_helper(ic);
	if (ni == NULL)
		panic("unable to setup initial BSS node");
	ni->ni_chan = IEEE80211_CHAN_ANYC;
	ic->ic_bss = ieee80211_ref_node(ni);
	ic->ic_txpower = IEEE80211_TXPOWER_MAX;
#ifndef IEEE80211_STA_ONLY
	mq_init(&ni->ni_savedq, IEEE80211_PS_MAX_QUEUE, IPL_NET);
#endif
}

void
ieee80211_node_detach(struct ifnet *ifp)
{
	struct ieee80211com *ic = (void *)ifp;

	if (ic->ic_bss != NULL) {
		(*ic->ic_node_free)(ic, ic->ic_bss);
		ic->ic_bss = NULL;
	}
	ieee80211_del_ess(ic, NULL, 0, 1);
	ieee80211_free_allnodes(ic, 1);
#ifndef IEEE80211_STA_ONLY
	free(ic->ic_aid_bitmap, M_DEVBUF,
	    howmany(ic->ic_max_aid, 32) * sizeof(u_int32_t));
	free(ic->ic_tim_bitmap, M_DEVBUF, ic->ic_tim_len);
	timeout_del(&ic->ic_inact_timeout);
	timeout_del(&ic->ic_node_cache_timeout);
	timeout_del(&ic->ic_tkip_micfail_timeout);
#endif
	timeout_del(&ic->ic_rsn_timeout);
}

/*
 * AP scanning support.
 */

/*
 * Initialize the active channel set based on the set
 * of available channels and the current PHY mode.
 */
void
ieee80211_reset_scan(struct ifnet *ifp)
{
	struct ieee80211com *ic = (void *)ifp;

	memcpy(ic->ic_chan_scan, ic->ic_chan_active,
		sizeof(ic->ic_chan_active));
	/* NB: hack, setup so next_scan starts with the first channel */
	if (ic->ic_bss != NULL && ic->ic_bss->ni_chan == IEEE80211_CHAN_ANYC)
		ic->ic_bss->ni_chan = &ic->ic_channels[IEEE80211_CHAN_MAX];
}

/*
 * Increase a node's inactivity counter.
 * This counter get reset to zero if a frame is received.
 * This function is intended for station mode only.
 * See ieee80211_node_cache_timeout() for hostap mode.
 */
void
ieee80211_node_raise_inact(void *arg, struct ieee80211_node *ni)
{
	if (ni->ni_refcnt == 0 && ni->ni_inact < IEEE80211_INACT_SCAN)
		ni->ni_inact++;
}

/*
 * Begin an active scan.
 */
void
ieee80211_begin_scan(struct ifnet *ifp)
{
	struct ieee80211com *ic = (void *)ifp;

	/*
	 * In all but hostap mode scanning starts off in
	 * an active mode before switching to passive.
	 */
#ifndef IEEE80211_STA_ONLY
	if (ic->ic_opmode != IEEE80211_M_HOSTAP)
#endif
	{
		ic->ic_flags |= IEEE80211_F_ASCAN;
		ic->ic_stats.is_scan_active++;
	}
#ifndef IEEE80211_STA_ONLY
	else
		ic->ic_stats.is_scan_passive++;
#endif
	if (ifp->if_flags & IFF_DEBUG)
		printf("%s: begin %s scan\n", ifp->if_xname,
			(ic->ic_flags & IEEE80211_F_ASCAN) ?
				"active" : "passive");


	if (ic->ic_opmode == IEEE80211_M_STA) {
		ieee80211_node_cleanup(ic, ic->ic_bss);
		ieee80211_iterate_nodes(ic, ieee80211_node_raise_inact, NULL);
	}

	/*
	 * Reset the current mode. Setting the current mode will also
	 * reset scan state.
	 */
	if (IFM_MODE(ic->ic_media.ifm_cur->ifm_media) == IFM_AUTO)
		ic->ic_curmode = IEEE80211_MODE_AUTO;
	ieee80211_setmode(ic, ic->ic_curmode);

	ic->ic_scan_count = 0;

	/* Scan the next channel. */
	ieee80211_next_scan(ifp);
}

/*
 * Switch to the next channel marked for scanning.
 */
void
ieee80211_next_scan(struct ifnet *ifp)
{
	struct ieee80211com *ic = (void *)ifp;
	struct ieee80211_channel *chan;

	chan = ic->ic_bss->ni_chan;
	for (;;) {
		if (++chan > &ic->ic_channels[IEEE80211_CHAN_MAX])
			chan = &ic->ic_channels[0];
		if (isset(ic->ic_chan_scan, ieee80211_chan2ieee(ic, chan))) {
			/*
			 * Ignore channels marked passive-only
			 * during an active scan.
			 */
			if ((ic->ic_flags & IEEE80211_F_ASCAN) == 0 ||
			    (chan->ic_flags & IEEE80211_CHAN_PASSIVE) == 0)
				break;
		}
		if (chan == ic->ic_bss->ni_chan) {
			ieee80211_end_scan(ifp);
			return;
		}
	}
	clrbit(ic->ic_chan_scan, ieee80211_chan2ieee(ic, chan));
	DPRINTF(("chan %d->%d\n",
	    ieee80211_chan2ieee(ic, ic->ic_bss->ni_chan),
	    ieee80211_chan2ieee(ic, chan)));
	ic->ic_bss->ni_chan = chan;
	ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
}

#ifndef IEEE80211_STA_ONLY
void
ieee80211_create_ibss(struct ieee80211com* ic, struct ieee80211_channel *chan)
{
	enum ieee80211_phymode mode;
	struct ieee80211_node *ni;
	struct ifnet *ifp = &ic->ic_if;

	ni = ic->ic_bss;
	if (ifp->if_flags & IFF_DEBUG)
		printf("%s: creating ibss\n", ifp->if_xname);
	ic->ic_flags |= IEEE80211_F_SIBSS;
	ni->ni_chan = chan;
	if ((ic->ic_flags & IEEE80211_F_VHTON) && IEEE80211_IS_CHAN_5GHZ(chan))
		mode = IEEE80211_MODE_11AC;
	else if (ic->ic_flags & IEEE80211_F_HTON)
		mode = IEEE80211_MODE_11N;
	else {
		/* Was a specific 11a/b/g phy mode set by ifconfig? */
		switch (IFM_MODE(ic->ic_media.ifm_cur->ifm_media)) {
		case IFM_IEEE80211_11A:
			mode = IEEE80211_MODE_11A;
			break;
		case IFM_IEEE80211_11G:
			mode = IEEE80211_MODE_11G;
			break;
		case IFM_IEEE80211_11B:
			mode = IEEE80211_MODE_11B;
			break;
		default: /* If we get here, our phy mode is MODE_AUTO. */
			if (IEEE80211_IS_CHAN_5GHZ(ni->ni_chan))
				mode = IEEE80211_MODE_11A;
			else if ((ni->ni_chan->ic_flags &
			    (IEEE80211_CHAN_OFDM | IEEE80211_CHAN_DYN)) != 0)
				mode = IEEE80211_MODE_11G;
			else
				mode = IEEE80211_MODE_11B;
			break;
		}
	}
	ieee80211_setmode(ic, mode);
	/* Pick an appropriate mode for supported legacy rates. */
	if (ic->ic_curmode == IEEE80211_MODE_11AC) {
		mode = IEEE80211_MODE_11A;
	} else if (ic->ic_curmode == IEEE80211_MODE_11N) {
		if (IEEE80211_IS_CHAN_5GHZ(chan))
			mode = IEEE80211_MODE_11A;
		else
			mode = IEEE80211_MODE_11G;
	} else {
		mode = ic->ic_curmode;
	}
	ni->ni_rates = ic->ic_sup_rates[mode];
	ni->ni_txrate = 0;
	IEEE80211_ADDR_COPY(ni->ni_macaddr, ic->ic_myaddr);
	IEEE80211_ADDR_COPY(ni->ni_bssid, ic->ic_myaddr);
	if (ic->ic_opmode == IEEE80211_M_IBSS) {
		if ((ic->ic_flags & IEEE80211_F_DESBSSID) != 0)
			IEEE80211_ADDR_COPY(ni->ni_bssid, ic->ic_des_bssid);
		else
			ni->ni_bssid[0] |= 0x02;	/* local bit for IBSS */
	}
	ni->ni_esslen = ic->ic_des_esslen;
	memcpy(ni->ni_essid, ic->ic_des_essid, ni->ni_esslen);
	ni->ni_rssi = 0;
	ni->ni_rstamp = 0;
	memset(ni->ni_tstamp, 0, sizeof(ni->ni_tstamp));
	ni->ni_intval = ic->ic_lintval;
	ni->ni_capinfo = IEEE80211_CAPINFO_IBSS;
	if (ic->ic_flags & IEEE80211_F_WEPON)
		ni->ni_capinfo |= IEEE80211_CAPINFO_PRIVACY;
	if (ic->ic_flags & IEEE80211_F_HTON) {
		const struct ieee80211_edca_ac_params *ac_qap;
		struct ieee80211_edca_ac_params *ac;
		int aci;

		/* 
		 * Configure HT protection. This will be updated later
		 * based on the number of non-HT nodes in the node cache.
		 */
		ic->ic_protmode = IEEE80211_PROT_NONE;
		ni->ni_htop1 = IEEE80211_HTPROT_NONE;
		/* Disallow Greenfield mode. None of our drivers support it. */
		ni->ni_htop1 |= IEEE80211_HTOP1_NONGF_STA;
		if (ic->ic_updateprot)
			ic->ic_updateprot(ic);

		/* Configure QoS EDCA parameters. */
		for (aci = 0; aci < EDCA_NUM_AC; aci++) {
			ac = &ic->ic_edca_ac[aci];
			ac_qap = &ieee80211_qap_edca_table[ic->ic_curmode][aci];
			ac->ac_acm       = ac_qap->ac_acm;
			ac->ac_aifsn     = ac_qap->ac_aifsn;
			ac->ac_ecwmin    = ac_qap->ac_ecwmin;
			ac->ac_ecwmax    = ac_qap->ac_ecwmax;
			ac->ac_txoplimit = ac_qap->ac_txoplimit;
		}
		if (ic->ic_updateedca)
			(*ic->ic_updateedca)(ic);
	}
	if (ic->ic_flags & IEEE80211_F_RSNON) {
		struct ieee80211_key *k;

		/* initialize 256-bit global key counter to a random value */
		arc4random_buf(ic->ic_globalcnt, EAPOL_KEY_NONCE_LEN);

		ni->ni_rsnprotos = ic->ic_rsnprotos;
		ni->ni_rsnakms = ic->ic_rsnakms;
		ni->ni_rsnciphers = ic->ic_rsnciphers;
		ni->ni_rsngroupcipher = ic->ic_rsngroupcipher;
		ni->ni_rsngroupmgmtcipher = ic->ic_rsngroupmgmtcipher;
		ni->ni_rsncaps = 0;
		if (ic->ic_caps & IEEE80211_C_MFP) {
			ni->ni_rsncaps |= IEEE80211_RSNCAP_MFPC;
			if (ic->ic_flags & IEEE80211_F_MFPR)
				ni->ni_rsncaps |= IEEE80211_RSNCAP_MFPR;
		}

		ic->ic_def_txkey = 1;
		ic->ic_flags &= ~IEEE80211_F_COUNTERM;
		k = &ic->ic_nw_keys[ic->ic_def_txkey];
		memset(k, 0, sizeof(*k));
		k->k_id = ic->ic_def_txkey;
		k->k_cipher = ni->ni_rsngroupcipher;
		k->k_flags = IEEE80211_KEY_GROUP | IEEE80211_KEY_TX;
		k->k_len = ieee80211_cipher_keylen(k->k_cipher);
		arc4random_buf(k->k_key, k->k_len);
		(*ic->ic_set_key)(ic, ni, k);	/* XXX */

		if (ic->ic_caps & IEEE80211_C_MFP) {
			ic->ic_igtk_kid = 4;
			k = &ic->ic_nw_keys[ic->ic_igtk_kid];
			memset(k, 0, sizeof(*k));
			k->k_id = ic->ic_igtk_kid;
			k->k_cipher = ni->ni_rsngroupmgmtcipher;
			k->k_flags = IEEE80211_KEY_IGTK | IEEE80211_KEY_TX;
			k->k_len = 16;
			arc4random_buf(k->k_key, k->k_len);
			(*ic->ic_set_key)(ic, ni, k);	/* XXX */
		}
		/*
		 * In HostAP mode, multicast traffic is sent using ic_bss
		 * as the Tx node, so mark our node as valid so we can send
		 * multicast frames using the group key we've just configured.
		 */
		ni->ni_port_valid = 1;
		ni->ni_flags |= IEEE80211_NODE_TXPROT;

		/* schedule a GTK/IGTK rekeying after 3600s */
		timeout_add_sec(&ic->ic_rsn_timeout, 3600);
	}
	timeout_add_sec(&ic->ic_inact_timeout, IEEE80211_INACT_WAIT);
	timeout_add_sec(&ic->ic_node_cache_timeout, IEEE80211_CACHE_WAIT);
	ieee80211_new_state(ic, IEEE80211_S_RUN, -1);
}
#endif	/* IEEE80211_STA_ONLY */

int
ieee80211_match_bss(struct ieee80211com *ic, struct ieee80211_node *ni,
    int bgscan)
{
	u_int8_t rate;
	int fail;

	fail = 0;
	if ((ic->ic_flags & IEEE80211_F_BGSCAN) == 0 &&
	    isclr(ic->ic_chan_active, ieee80211_chan2ieee(ic, ni->ni_chan)))
		fail |= IEEE80211_NODE_ASSOCFAIL_CHAN;
	if (ic->ic_des_chan != IEEE80211_CHAN_ANYC &&
	    ni->ni_chan != ic->ic_des_chan)
		fail |= IEEE80211_NODE_ASSOCFAIL_CHAN;
#ifndef IEEE80211_STA_ONLY
	if (ic->ic_opmode == IEEE80211_M_IBSS) {
		if ((ni->ni_capinfo & IEEE80211_CAPINFO_IBSS) == 0)
			fail |= IEEE80211_NODE_ASSOCFAIL_IBSS;
	} else
#endif
	{
		if ((ni->ni_capinfo & IEEE80211_CAPINFO_ESS) == 0)
			fail |= IEEE80211_NODE_ASSOCFAIL_IBSS;
	}
	if (ic->ic_flags & (IEEE80211_F_WEPON | IEEE80211_F_RSNON)) {
		if ((ni->ni_capinfo & IEEE80211_CAPINFO_PRIVACY) == 0)
			fail |= IEEE80211_NODE_ASSOCFAIL_PRIVACY;
	} else {
		if (ni->ni_capinfo & IEEE80211_CAPINFO_PRIVACY)
			fail |= IEEE80211_NODE_ASSOCFAIL_PRIVACY;
	}

	rate = ieee80211_fix_rate(ic, ni, IEEE80211_F_DONEGO);
	if (rate & IEEE80211_RATE_BASIC)
		fail |= IEEE80211_NODE_ASSOCFAIL_BASIC_RATE;
	if (ic->ic_des_esslen == 0)
		fail |= IEEE80211_NODE_ASSOCFAIL_ESSID;
	if (ic->ic_des_esslen != 0 &&
	    (ni->ni_esslen != ic->ic_des_esslen ||
	     memcmp(ni->ni_essid, ic->ic_des_essid, ic->ic_des_esslen) != 0))
		fail |= IEEE80211_NODE_ASSOCFAIL_ESSID;
	if ((ic->ic_flags & IEEE80211_F_DESBSSID) &&
	    !IEEE80211_ADDR_EQ(ic->ic_des_bssid, ni->ni_bssid))
		fail |= IEEE80211_NODE_ASSOCFAIL_BSSID;

	if (ic->ic_flags & IEEE80211_F_RSNON) {
		/*
		 * If at least one RSN IE field from the AP's RSN IE fails
		 * to overlap with any value the STA supports, the STA shall
		 * decline to associate with that AP.
		 */
		if ((ni->ni_rsnprotos & ic->ic_rsnprotos) == 0)
			fail |= IEEE80211_NODE_ASSOCFAIL_WPA_PROTO;
		if ((ni->ni_rsnakms & ic->ic_rsnakms) == 0)
			fail |= IEEE80211_NODE_ASSOCFAIL_WPA_PROTO;
		if ((ni->ni_rsnakms & ic->ic_rsnakms &
		     ~(IEEE80211_AKM_PSK | IEEE80211_AKM_SHA256_PSK)) == 0) {
			/* AP only supports PSK AKMPs */
			if (!(ic->ic_flags & IEEE80211_F_PSK))
				fail |= IEEE80211_NODE_ASSOCFAIL_WPA_PROTO;
		}
		if (ni->ni_rsngroupcipher != IEEE80211_CIPHER_WEP40 &&
		    ni->ni_rsngroupcipher != IEEE80211_CIPHER_TKIP &&
		    ni->ni_rsngroupcipher != IEEE80211_CIPHER_CCMP &&
		    ni->ni_rsngroupcipher != IEEE80211_CIPHER_WEP104)
			fail |= IEEE80211_NODE_ASSOCFAIL_WPA_PROTO;
		if ((ni->ni_rsnciphers & ic->ic_rsnciphers) == 0)
			fail |= IEEE80211_NODE_ASSOCFAIL_WPA_PROTO;

		/* we only support BIP as the IGTK cipher */
		if ((ni->ni_rsncaps & IEEE80211_RSNCAP_MFPC) &&
		    ni->ni_rsngroupmgmtcipher != IEEE80211_CIPHER_BIP)
			fail |= IEEE80211_NODE_ASSOCFAIL_WPA_PROTO;

		/* we do not support MFP but AP requires it */
		if (!(ic->ic_caps & IEEE80211_C_MFP) &&
		    (ni->ni_rsncaps & IEEE80211_RSNCAP_MFPR))
			fail |= IEEE80211_NODE_ASSOCFAIL_WPA_PROTO;

		/* we require MFP but AP does not support it */
		if ((ic->ic_caps & IEEE80211_C_MFP) &&
		    (ic->ic_flags & IEEE80211_F_MFPR) &&
		    !(ni->ni_rsncaps & IEEE80211_RSNCAP_MFPC))
			fail |= IEEE80211_NODE_ASSOCFAIL_WPA_PROTO;
	}

	if (ic->ic_if.if_flags & IFF_DEBUG) {
		printf("%s: %c %s%c", ic->ic_if.if_xname, fail ? '-' : '+',
		    ether_sprintf(ni->ni_bssid),
		    fail & IEEE80211_NODE_ASSOCFAIL_BSSID ? '!' : ' ');
		printf(" %3d%c", ieee80211_chan2ieee(ic, ni->ni_chan),
			fail & IEEE80211_NODE_ASSOCFAIL_CHAN ? '!' : ' ');
		printf(" %+4d", ni->ni_rssi);
		printf(" %2dM%c", (rate & IEEE80211_RATE_VAL) / 2,
		    fail & IEEE80211_NODE_ASSOCFAIL_BASIC_RATE ? '!' : ' ');
		printf(" %4s%c",
		    (ni->ni_capinfo & IEEE80211_CAPINFO_ESS) ? "ess" :
		    (ni->ni_capinfo & IEEE80211_CAPINFO_IBSS) ? "ibss" :
		    "????",
		    fail & IEEE80211_NODE_ASSOCFAIL_IBSS ? '!' : ' ');
		printf(" %7s%c ",
		    (ni->ni_capinfo & IEEE80211_CAPINFO_PRIVACY) ?
		    "privacy" : "no",
		    fail & IEEE80211_NODE_ASSOCFAIL_PRIVACY ? '!' : ' ');
		printf(" %3s%c ",
		    (ic->ic_flags & IEEE80211_F_RSNON) ?
		    "rsn" : "no",
		    fail & IEEE80211_NODE_ASSOCFAIL_WPA_PROTO ? '!' : ' ');
		ieee80211_print_essid(ni->ni_essid, ni->ni_esslen);
		printf("%s\n",
		    fail & IEEE80211_NODE_ASSOCFAIL_ESSID ? "!" : "");
	}

	/* We don't care about unrelated networks during background scans. */
	if (bgscan) {
		if ((fail & IEEE80211_NODE_ASSOCFAIL_ESSID) == 0)
			ni->ni_assoc_fail = fail;
	} else
		ni->ni_assoc_fail = fail;
	if ((fail & IEEE80211_NODE_ASSOCFAIL_ESSID) == 0)
		ic->ic_bss->ni_assoc_fail = ni->ni_assoc_fail;

	return fail;
}

struct ieee80211_node_switch_bss_arg {
	u_int8_t cur_macaddr[IEEE80211_ADDR_LEN];
	u_int8_t sel_macaddr[IEEE80211_ADDR_LEN];
};

void
ieee80211_node_free_unref_cb(struct ieee80211_node *ni)
{
	free(ni->ni_unref_arg, M_DEVBUF, ni->ni_unref_arg_size);

	/* Guard against accidental reuse. */
	ni->ni_unref_cb = NULL;
	ni->ni_unref_arg = NULL;
	ni->ni_unref_arg_size = 0;
}

/* Implements ni->ni_unref_cb(). */
void
ieee80211_node_tx_stopped(struct ieee80211com *ic,
    struct ieee80211_node *ni)
{
	splassert(IPL_NET);

	if ((ic->ic_flags & IEEE80211_F_BGSCAN) == 0)
		return;

	/* 
	 * Install a callback which will switch us to the new AP once
	 * the de-auth frame has been processed by hardware.
	 * Pass on the existing ni->ni_unref_arg argument.
	 */
	ic->ic_bss->ni_unref_cb = ieee80211_node_switch_bss;

	/* 
	 * All data frames queued to hardware have been flushed and
	 * A-MPDU Tx has been stopped. We are now going to switch APs.
	 * Queue a de-auth frame addressed at our current AP.
	 */
	if (IEEE80211_SEND_MGMT(ic, ic->ic_bss,
	    IEEE80211_FC0_SUBTYPE_DEAUTH,
	    IEEE80211_REASON_AUTH_LEAVE) != 0) {
		ic->ic_flags &= ~IEEE80211_F_BGSCAN;
		ieee80211_node_free_unref_cb(ni);
		ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
		return;
	}

	/* F_BGSCAN flag gets cleared in ieee80211_node_join_bss(). */
}

/* Implements ni->ni_unref_cb(). */
void
ieee80211_node_tx_flushed(struct ieee80211com *ic, struct ieee80211_node *ni)
{
	splassert(IPL_NET);

	if ((ic->ic_flags & IEEE80211_F_BGSCAN) == 0)
		return;

	/* All data frames queued to hardware have been flushed. */
	if (ic->ic_caps & IEEE80211_C_TX_AMPDU) {
		/* 
		 * Install a callback which will switch us to the
		 * new AP once Tx agg sessions have been stopped,
		 * which involves sending a DELBA frame.
		 * Pass on the existing ni->ni_unref_arg argument.
		 */
		ic->ic_bss->ni_unref_cb = ieee80211_node_tx_stopped;
		ieee80211_stop_ampdu_tx(ic, ic->ic_bss,
		    IEEE80211_FC0_SUBTYPE_DEAUTH);
	} else
		ieee80211_node_tx_stopped(ic, ni);
}

/* Implements ni->ni_unref_cb(). */
void
ieee80211_node_switch_bss(struct ieee80211com *ic, struct ieee80211_node *ni)
{
	struct ifnet *ifp = &ic->ic_if;
	struct ieee80211_node_switch_bss_arg *sba = ni->ni_unref_arg;
	struct ieee80211_node *curbs, *selbs;

	splassert(IPL_NET);

	if ((ic->ic_flags & IEEE80211_F_BGSCAN) == 0)
		return;

	ic->ic_xflags &= ~IEEE80211_F_TX_MGMT_ONLY;

	selbs = ieee80211_find_node(ic, sba->sel_macaddr);
	if (selbs == NULL) {
		ieee80211_node_free_unref_cb(ni);
		ic->ic_flags &= ~IEEE80211_F_BGSCAN;
		ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
		return;
	}

	curbs = ieee80211_find_node(ic, sba->cur_macaddr);
	if (curbs == NULL) {
		ieee80211_node_free_unref_cb(ni);
		ic->ic_flags &= ~IEEE80211_F_BGSCAN;
		ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
		return;
	}

	if (ifp->if_flags & IFF_DEBUG) {
		printf("%s: roaming from %s chan %d ",
		    ifp->if_xname, ether_sprintf(curbs->ni_macaddr),
		    ieee80211_chan2ieee(ic, curbs->ni_chan));
		printf("to %s chan %d\n", ether_sprintf(selbs->ni_macaddr),
		    ieee80211_chan2ieee(ic, selbs->ni_chan));
	}
	ieee80211_node_newstate(curbs, IEEE80211_STA_CACHE);
	/*
	 * ieee80211_node_join_bss() frees arg and ic->ic_bss via
	 * ic->ic_node_copy() in ieee80211_node_cleanup().
	 */
	ieee80211_node_join_bss(ic, selbs);
}

void
ieee80211_node_join_bss(struct ieee80211com *ic, struct ieee80211_node *selbs)
{
	enum ieee80211_phymode mode;
	struct ieee80211_node *ni;
	uint32_t assoc_fail = 0;

	/* Reinitialize media mode and channels if needed. */
	mode = ieee80211_node_abg_mode(ic, selbs);
	if (mode != ic->ic_curmode)
		ieee80211_setmode(ic, mode);

	/* Keep recorded association failures for this BSS/ESS intact. */
	if (IEEE80211_ADDR_EQ(ic->ic_bss->ni_macaddr, selbs->ni_macaddr) ||
	    (ic->ic_des_esslen > 0 && ic->ic_des_esslen == selbs->ni_esslen &&
	    memcmp(ic->ic_des_essid, selbs->ni_essid, selbs->ni_esslen) == 0))
		assoc_fail = ic->ic_bss->ni_assoc_fail;

	(*ic->ic_node_copy)(ic, ic->ic_bss, selbs);
	ni = ic->ic_bss;
	ni->ni_assoc_fail |= assoc_fail;

	/* Make sure we send valid rates in an association request. */
	if (ic->ic_opmode == IEEE80211_M_STA)
		ieee80211_fix_rate(ic, ni,
		    IEEE80211_F_DOSORT | IEEE80211_F_DOFRATE |
		    IEEE80211_F_DONEGO | IEEE80211_F_DODEL);

	if (ic->ic_flags & IEEE80211_F_RSNON)
		ieee80211_choose_rsnparams(ic);
	else if (ic->ic_flags & IEEE80211_F_WEPON)
		ni->ni_rsncipher = IEEE80211_CIPHER_USEGROUP;

	ieee80211_node_newstate(selbs, IEEE80211_STA_BSS);
#ifndef IEEE80211_STA_ONLY
	if (ic->ic_opmode == IEEE80211_M_IBSS) {
		ieee80211_fix_rate(ic, ni, IEEE80211_F_DOFRATE |
		    IEEE80211_F_DONEGO | IEEE80211_F_DODEL);
		if (ni->ni_rates.rs_nrates == 0) {
			ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
			return;
		}
		ieee80211_new_state(ic, IEEE80211_S_RUN, -1);
	} else
#endif
	{
		int bgscan = ((ic->ic_flags & IEEE80211_F_BGSCAN) &&
		    ic->ic_opmode == IEEE80211_M_STA &&
		    ic->ic_state == IEEE80211_S_RUN);
		int auth_next = (ic->ic_opmode == IEEE80211_M_STA &&
		    ic->ic_state == IEEE80211_S_AUTH);
		int mgt = -1;

		timeout_del(&ic->ic_bgscan_timeout);
		ic->ic_flags &= ~IEEE80211_F_BGSCAN;

		/* 
		 * After a background scan, we have now switched APs.
		 * Pretend we were just de-authed, which makes
		 * ieee80211_new_state() try to re-auth and thus send
		 * an AUTH frame to our newly selected AP.
		 */
		if (bgscan)
			mgt = IEEE80211_FC0_SUBTYPE_DEAUTH;
		/*
		 * If we are trying another AP after the previous one
		 * failed (state transition AUTH->AUTH), ensure that
		 * ieee80211_new_state() tries to send another auth frame.
		 */
		else if (auth_next)
			mgt = IEEE80211_FC0_SUBTYPE_AUTH;

		ieee80211_new_state(ic, IEEE80211_S_AUTH, mgt);
	}
}

struct ieee80211_node *
ieee80211_node_choose_bss(struct ieee80211com *ic, int bgscan,
    struct ieee80211_node **curbs)
{
	struct ieee80211_node *ni, *nextbs, *selbs = NULL, 
	    *selbs2 = NULL, *selbs5 = NULL;
	uint8_t min_5ghz_rssi;

	ni = RBT_MIN(ieee80211_tree, &ic->ic_tree);

	for (; ni != NULL; ni = nextbs) {
		nextbs = RBT_NEXT(ieee80211_tree, ni);
		if (ni->ni_fails) {
			/*
			 * The configuration of the access points may change
			 * during my scan.  So delete the entry for the AP
			 * and retry to associate if there is another beacon.
			 */
			if (ni->ni_fails++ > 2)
				ieee80211_free_node(ic, ni);
			continue;
		}

		if (curbs && ieee80211_node_cmp(ic->ic_bss, ni) == 0)
			*curbs = ni;

		if (ieee80211_match_bss(ic, ni, bgscan) != 0)
			continue;

		if (ic->ic_caps & IEEE80211_C_SCANALLBAND) {
			if (IEEE80211_IS_CHAN_2GHZ(ni->ni_chan) &&
			    (selbs2 == NULL || ni->ni_rssi > selbs2->ni_rssi))
				selbs2 = ni;
			else if (IEEE80211_IS_CHAN_5GHZ(ni->ni_chan) &&
			    (selbs5 == NULL || ni->ni_rssi > selbs5->ni_rssi))
				selbs5 = ni;
		} else if (selbs == NULL || ni->ni_rssi > selbs->ni_rssi)
			selbs = ni;
	}

	if (ic->ic_max_rssi)
		min_5ghz_rssi = IEEE80211_RSSI_THRES_RATIO_5GHZ;
	else
		min_5ghz_rssi = (uint8_t)IEEE80211_RSSI_THRES_5GHZ;

	/*
	 * Prefer a 5Ghz AP even if its RSSI is weaker than the best 2Ghz AP
	 * (as long as it meets the minimum RSSI threshold) since the 5Ghz band
	 * is usually less saturated.
	 */
	if (selbs5 && (*ic->ic_node_checkrssi)(ic, selbs5))
		selbs = selbs5;
	else if (selbs5 && selbs2)
		selbs = (selbs5->ni_rssi >= selbs2->ni_rssi ? selbs5 : selbs2);
	else if (selbs2)
		selbs = selbs2;
	else if (selbs5)
		selbs = selbs5;

	return selbs;
}

/*
 * Complete a scan of potential channels.
 */
void
ieee80211_end_scan(struct ifnet *ifp)
{
	struct ieee80211com *ic = (void *)ifp;
	struct ieee80211_node *ni, *selbs = NULL, *curbs = NULL;
	int bgscan = ((ic->ic_flags & IEEE80211_F_BGSCAN) &&
	    ic->ic_opmode == IEEE80211_M_STA &&
	    ic->ic_state == IEEE80211_S_RUN);

	if (ifp->if_flags & IFF_DEBUG)
		printf("%s: end %s scan\n", ifp->if_xname,
		    bgscan ? "background" :
		    ((ic->ic_flags & IEEE80211_F_ASCAN) ?
		    "active" : "passive"));

	if (ic->ic_scan_count)
		ic->ic_flags &= ~IEEE80211_F_ASCAN;

	if (ic->ic_opmode == IEEE80211_M_STA)
		ieee80211_clean_inactive_nodes(ic, IEEE80211_INACT_SCAN);

	ni = RBT_MIN(ieee80211_tree, &ic->ic_tree);

#ifndef IEEE80211_STA_ONLY
	if (ic->ic_opmode == IEEE80211_M_HOSTAP) {
		/* XXX off stack? */
		u_char occupied[howmany(IEEE80211_CHAN_MAX, NBBY)];
		int i, fail;

		/*
		 * The passive scan to look for existing AP's completed,
		 * select a channel to camp on.  Identify the channels
		 * that already have one or more AP's and try to locate
		 * an unoccupied one.  If that fails, pick a random
		 * channel from the active set.
		 */
		memset(occupied, 0, sizeof(occupied));
		RBT_FOREACH(ni, ieee80211_tree, &ic->ic_tree)
			setbit(occupied, ieee80211_chan2ieee(ic, ni->ni_chan));
		for (i = 0; i < IEEE80211_CHAN_MAX; i++)
			if (isset(ic->ic_chan_active, i) && isclr(occupied, i))
				break;
		if (i == IEEE80211_CHAN_MAX) {
			fail = arc4random() & 3;	/* random 0-3 */
			for (i = 0; i < IEEE80211_CHAN_MAX; i++)
				if (isset(ic->ic_chan_active, i) && fail-- == 0)
					break;
		}
		ieee80211_create_ibss(ic, &ic->ic_channels[i]);
		return;
	}
#endif
	if (ni == NULL) {
		DPRINTF(("no scan candidate\n"));
 notfound:

#ifndef IEEE80211_STA_ONLY
		if (ic->ic_opmode == IEEE80211_M_IBSS &&
		    (ic->ic_flags & IEEE80211_F_IBSSON) &&
		    ic->ic_des_esslen != 0) {
			ieee80211_create_ibss(ic, ic->ic_ibss_chan);
			return;
		}
#endif
		/*
		 * Reset the list of channels to scan and scan the next mode
		 * if nothing has been found.
		 * If the device scans all bands in one fell swoop, return
		 * current scan results to userspace regardless of mode.
		 * This will loop forever until an access point is found.
		 */
		ieee80211_reset_scan(ifp);
		if (ieee80211_next_mode(ifp) == IEEE80211_MODE_AUTO ||
		    (ic->ic_caps & IEEE80211_C_SCANALLBAND))
			ic->ic_scan_count++;

		ieee80211_next_scan(ifp);
		return;
	}

	/* Possibly switch which ssid we are associated with */
	if (!bgscan && ic->ic_opmode == IEEE80211_M_STA)
		ieee80211_switch_ess(ic);

	selbs = ieee80211_node_choose_bss(ic, bgscan, &curbs);
	if (bgscan) {
		struct ieee80211_node_switch_bss_arg *arg;

		/* AP disappeared? Should not happen. */
		if (selbs == NULL || curbs == NULL) {
			ic->ic_flags &= ~IEEE80211_F_BGSCAN;
			goto notfound;
		}

		/* 
		 * After a background scan we might end up choosing the
		 * same AP again. Or the newly selected AP's RSSI level
		 * might be low enough to trigger another background scan.
		 * Do not change ic->ic_bss in these cases and make
		 * background scans less frequent.
		 */
		if (selbs == curbs || !(*ic->ic_node_checkrssi)(ic, selbs)) {
			if (ic->ic_bgscan_fail < IEEE80211_BGSCAN_FAIL_MAX) {
				if (ic->ic_bgscan_fail <= 0)
					ic->ic_bgscan_fail = 1;
				else
					ic->ic_bgscan_fail *= 2;
			}
			ic->ic_flags &= ~IEEE80211_F_BGSCAN;

			/*
			 * HT is negotiated during association so we must use
			 * ic_bss to check HT. The nodes tree was re-populated
			 * during background scan and therefore selbs and curbs
			 * may not carry HT information.
			 */
			ni = ic->ic_bss;
			if (ni->ni_flags & IEEE80211_NODE_VHT)
				ieee80211_setmode(ic, IEEE80211_MODE_11AC);
			else if (ni->ni_flags & IEEE80211_NODE_HT)
				ieee80211_setmode(ic, IEEE80211_MODE_11N);
			else
				ieee80211_setmode(ic,
				    ieee80211_node_abg_mode(ic, ni));
			return;
		}
	
		arg = malloc(sizeof(*arg), M_DEVBUF, M_NOWAIT | M_ZERO);
		if (arg == NULL) {
			ic->ic_flags &= ~IEEE80211_F_BGSCAN;
			return;
		}

		ic->ic_bgscan_fail = 0;

		/* Prevent dispatch of additional data frames to hardware. */
		ic->ic_xflags |= IEEE80211_F_TX_MGMT_ONLY;

		IEEE80211_ADDR_COPY(arg->cur_macaddr, curbs->ni_macaddr);
		IEEE80211_ADDR_COPY(arg->sel_macaddr, selbs->ni_macaddr);

		if (ic->ic_bgscan_done) {
			/*
			 * The driver will flush its queues and allow roaming
			 * to proceed once queues have been flushed.
			 * On failure the driver will move back to SCAN state.
			 */
			ic->ic_bgscan_done(ic, arg, sizeof(*arg));
			return;
		}

		/* 
		 * Install a callback which will switch us to the new AP once
		 * all dispatched frames have been processed by hardware.
		 */
		ic->ic_bss->ni_unref_arg = arg;
		ic->ic_bss->ni_unref_arg_size = sizeof(*arg);
		if (ic->ic_bss->ni_refcnt > 0)
			ic->ic_bss->ni_unref_cb = ieee80211_node_tx_flushed;
		else
			ieee80211_node_tx_flushed(ic, ni);
		/* F_BGSCAN flag gets cleared in ieee80211_node_join_bss(). */
		return;
	} else if (selbs == NULL)
		goto notfound;

	ieee80211_node_join_bss(ic, selbs);
}

/*
 * Autoselect the best RSN parameters (protocol, AKMP, pairwise cipher...)
 * that are supported by both peers (STA mode only).
 */
void
ieee80211_choose_rsnparams(struct ieee80211com *ic)
{
	struct ieee80211_node *ni = ic->ic_bss;
	struct ieee80211_pmk *pmk;

	/* filter out unsupported protocol versions */
	ni->ni_rsnprotos &= ic->ic_rsnprotos;
	/* prefer RSN (aka WPA2) over WPA */
	if (ni->ni_rsnprotos & IEEE80211_PROTO_RSN)
		ni->ni_rsnprotos = IEEE80211_PROTO_RSN;
	else
		ni->ni_rsnprotos = IEEE80211_PROTO_WPA;

	/* filter out unsupported AKMPs */
	ni->ni_rsnakms &= ic->ic_rsnakms;
	/* prefer SHA-256 based AKMPs */
	if ((ic->ic_flags & IEEE80211_F_PSK) && (ni->ni_rsnakms &
	    (IEEE80211_AKM_PSK | IEEE80211_AKM_SHA256_PSK))) {
		/* AP supports PSK AKMP and a PSK is configured */
		if (ni->ni_rsnakms & IEEE80211_AKM_SHA256_PSK)
			ni->ni_rsnakms = IEEE80211_AKM_SHA256_PSK;
		else
			ni->ni_rsnakms = IEEE80211_AKM_PSK;
	} else {
		if (ni->ni_rsnakms & IEEE80211_AKM_SHA256_8021X)
			ni->ni_rsnakms = IEEE80211_AKM_SHA256_8021X;
		else
			ni->ni_rsnakms = IEEE80211_AKM_8021X;
		/* check if we have a cached PMK for this AP */
		if (ni->ni_rsnprotos == IEEE80211_PROTO_RSN &&
		    (pmk = ieee80211_pmksa_find(ic, ni, NULL)) != NULL) {
			memcpy(ni->ni_pmkid, pmk->pmk_pmkid,
			    IEEE80211_PMKID_LEN);
			ni->ni_flags |= IEEE80211_NODE_PMKID;
		}
	}

	/* filter out unsupported pairwise ciphers */
	ni->ni_rsnciphers &= ic->ic_rsnciphers;
	/* prefer CCMP over TKIP */
	if (ni->ni_rsnciphers & IEEE80211_CIPHER_CCMP)
		ni->ni_rsnciphers = IEEE80211_CIPHER_CCMP;
	else
		ni->ni_rsnciphers = IEEE80211_CIPHER_TKIP;
	ni->ni_rsncipher = ni->ni_rsnciphers;

	/* use MFP if we both support it */
	if ((ic->ic_caps & IEEE80211_C_MFP) &&
	    (ni->ni_rsncaps & IEEE80211_RSNCAP_MFPC))
		ni->ni_flags |= IEEE80211_NODE_MFP;
}

int
ieee80211_get_rate(struct ieee80211com *ic)
{
	u_int8_t (*rates)[IEEE80211_RATE_MAXSIZE];
	int rate;

	rates = &ic->ic_bss->ni_rates.rs_rates;

	if (ic->ic_fixed_rate != -1)
		rate = (*rates)[ic->ic_fixed_rate];
	else if (ic->ic_state == IEEE80211_S_RUN)
		rate = (*rates)[ic->ic_bss->ni_txrate];
	else
		rate = 0;

	return rate & IEEE80211_RATE_VAL;
}

struct ieee80211_node *
ieee80211_node_alloc(struct ieee80211com *ic)
{
	return malloc(sizeof(struct ieee80211_node), M_DEVBUF,
	    M_NOWAIT | M_ZERO);
}

void
ieee80211_node_cleanup(struct ieee80211com *ic, struct ieee80211_node *ni)
{
	if (ni->ni_rsnie != NULL) {
		free(ni->ni_rsnie, M_DEVBUF, 2 + ni->ni_rsnie[1]);
		ni->ni_rsnie = NULL;
	}
	ieee80211_ba_del(ni);
#ifndef IEEE80211_STA_ONLY
	mq_purge(&ni->ni_savedq);
#endif
	ieee80211_node_free_unref_cb(ni);
}

void
ieee80211_node_free(struct ieee80211com *ic, struct ieee80211_node *ni)
{
	ieee80211_node_cleanup(ic, ni);
	free(ni, M_DEVBUF, 0);
}

void
ieee80211_node_copy(struct ieee80211com *ic,
	struct ieee80211_node *dst, const struct ieee80211_node *src)
{
	ieee80211_node_cleanup(ic, dst);
	*dst = *src;
	dst->ni_rsnie = NULL;
	if (src->ni_rsnie != NULL)
		ieee80211_save_ie(src->ni_rsnie, &dst->ni_rsnie);
	ieee80211_node_set_timeouts(dst);
#ifndef IEEE80211_STA_ONLY
	mq_init(&dst->ni_savedq, IEEE80211_PS_MAX_QUEUE, IPL_NET);
#endif
}

u_int8_t
ieee80211_node_getrssi(struct ieee80211com *ic,
    const struct ieee80211_node *ni)
{
	return ni->ni_rssi;
}

int
ieee80211_node_checkrssi(struct ieee80211com *ic,
    const struct ieee80211_node *ni)
{
	uint8_t thres;

	if (ni->ni_chan == IEEE80211_CHAN_ANYC)
		return 0;

	if (ic->ic_max_rssi) {
		thres = (IEEE80211_IS_CHAN_2GHZ(ni->ni_chan)) ?
		    IEEE80211_RSSI_THRES_RATIO_2GHZ :
		    IEEE80211_RSSI_THRES_RATIO_5GHZ;
		return ((ni->ni_rssi * 100) / ic->ic_max_rssi >= thres);
	}

	thres = (IEEE80211_IS_CHAN_2GHZ(ni->ni_chan)) ?
	    IEEE80211_RSSI_THRES_2GHZ :
	    IEEE80211_RSSI_THRES_5GHZ;
	return (ni->ni_rssi >= (u_int8_t)thres);
}

void
ieee80211_node_set_timeouts(struct ieee80211_node *ni)
{
	int i;

#ifndef IEEE80211_STA_ONLY
	timeout_set(&ni->ni_eapol_to, ieee80211_eapol_timeout, ni);
	timeout_set(&ni->ni_sa_query_to, ieee80211_sa_query_timeout, ni);
#endif
	timeout_set(&ni->ni_addba_req_to[EDCA_AC_BE],
	    ieee80211_node_addba_request_ac_be_to, ni);
	timeout_set(&ni->ni_addba_req_to[EDCA_AC_BK],
	    ieee80211_node_addba_request_ac_bk_to, ni);
	timeout_set(&ni->ni_addba_req_to[EDCA_AC_VI],
	    ieee80211_node_addba_request_ac_vi_to, ni);
	timeout_set(&ni->ni_addba_req_to[EDCA_AC_VO],
	    ieee80211_node_addba_request_ac_vo_to, ni);
	for (i = 0; i < nitems(ni->ni_addba_req_intval); i++)
		ni->ni_addba_req_intval[i] = 1;
}

void
ieee80211_setup_node(struct ieee80211com *ic,
	struct ieee80211_node *ni, const u_int8_t *macaddr)
{
	int i, s;

	DPRINTF(("%s\n", ether_sprintf((u_int8_t *)macaddr)));
	IEEE80211_ADDR_COPY(ni->ni_macaddr, macaddr);
	ieee80211_node_newstate(ni, IEEE80211_STA_CACHE);

	ni->ni_ic = ic;	/* back-pointer */
	/* Initialize cached last sequence numbers with invalid values. */
	ni->ni_rxseq = 0xffffU;
	for (i=0; i < IEEE80211_NUM_TID; ++i)
		ni->ni_qos_rxseqs[i] = 0xffffU;
#ifndef IEEE80211_STA_ONLY
	mq_init(&ni->ni_savedq, IEEE80211_PS_MAX_QUEUE, IPL_NET);
#endif
	ieee80211_node_set_timeouts(ni);

	s = splnet();
	RBT_INSERT(ieee80211_tree, &ic->ic_tree, ni);
	ic->ic_nnodes++;
	splx(s);
}

struct ieee80211_node *
ieee80211_alloc_node(struct ieee80211com *ic, const u_int8_t *macaddr)
{
	struct ieee80211_node *ni = ieee80211_alloc_node_helper(ic);
	if (ni != NULL)
		ieee80211_setup_node(ic, ni, macaddr);
	else
		ic->ic_stats.is_rx_nodealloc++;
	return ni;
}

struct ieee80211_node *
ieee80211_dup_bss(struct ieee80211com *ic, const u_int8_t *macaddr)
{
	struct ieee80211_node *ni = ieee80211_alloc_node_helper(ic);
	if (ni != NULL) {
		ieee80211_setup_node(ic, ni, macaddr);
		/*
		 * Inherit from ic_bss.
		 */
		IEEE80211_ADDR_COPY(ni->ni_bssid, ic->ic_bss->ni_bssid);
		ni->ni_chan = ic->ic_bss->ni_chan;
	} else
		ic->ic_stats.is_rx_nodealloc++;
	return ni;
}

struct ieee80211_node *
ieee80211_find_node(struct ieee80211com *ic, const u_int8_t *macaddr)
{
	struct ieee80211_node *ni;
	int cmp;

	/* similar to RBT_FIND except we compare keys, not nodes */
	ni = RBT_ROOT(ieee80211_tree, &ic->ic_tree);
	while (ni != NULL) {
		cmp = memcmp(macaddr, ni->ni_macaddr, IEEE80211_ADDR_LEN);
		if (cmp < 0)
			ni = RBT_LEFT(ieee80211_tree, ni);
		else if (cmp > 0)
			ni = RBT_RIGHT(ieee80211_tree, ni);
		else
			break;
	}
	return ni;
}

/*
 * Return a reference to the appropriate node for sending
 * a data frame.  This handles node discovery in adhoc networks.
 *
 * Drivers will call this, so increase the reference count before
 * returning the node.
 */
struct ieee80211_node *
ieee80211_find_txnode(struct ieee80211com *ic, const u_int8_t *macaddr)
{
#ifndef IEEE80211_STA_ONLY
	struct ieee80211_node *ni;
	int s;
#endif

	/*
	 * The destination address should be in the node table
	 * unless we are operating in station mode or this is a
	 * multicast/broadcast frame.
	 */
	if (ic->ic_opmode == IEEE80211_M_STA || IEEE80211_IS_MULTICAST(macaddr))
		return ieee80211_ref_node(ic->ic_bss);

#ifndef IEEE80211_STA_ONLY
	s = splnet();
	ni = ieee80211_find_node(ic, macaddr);
	splx(s);
	if (ni == NULL) {
		if (ic->ic_opmode != IEEE80211_M_IBSS &&
		    ic->ic_opmode != IEEE80211_M_AHDEMO)
			return NULL;

		/*
		 * Fake up a node; this handles node discovery in
		 * adhoc mode.  Note that for the driver's benefit
		 * we treat this like an association so the driver
		 * has an opportunity to setup its private state.
		 *
		 * XXX need better way to handle this; issue probe
		 *     request so we can deduce rate set, etc.
		 */
		if ((ni = ieee80211_dup_bss(ic, macaddr)) == NULL)
			return NULL;
		/* XXX no rate negotiation; just dup */
		ni->ni_rates = ic->ic_bss->ni_rates;
		ni->ni_txrate = 0;
		if (ic->ic_newassoc)
			(*ic->ic_newassoc)(ic, ni, 1);
	}
	return ieee80211_ref_node(ni);
#else
	return NULL;	/* can't get there */
#endif	/* IEEE80211_STA_ONLY */
}

/*
 * It is usually desirable to process a Rx packet using its sender's
 * node-record instead of the BSS record.
 *
 * - AP mode: keep a node-record for every authenticated/associated
 *   station *in the BSS*. For future use, we also track neighboring
 *   APs, since they might belong to the same ESS.  APs in the same
 *   ESS may bridge packets to each other, forming a Wireless
 *   Distribution System (WDS).
 *
 * - IBSS mode: keep a node-record for every station *in the BSS*.
 *   Also track neighboring stations by their beacons/probe responses.
 *
 * - monitor mode: keep a node-record for every sender, regardless
 *   of BSS.
 *
 * - STA mode: the only available node-record is the BSS record,
 *   ic->ic_bss.
 *
 * Of all the 802.11 Control packets, only the node-records for
 * RTS packets node-record can be looked up.
 *
 * Return non-zero if the packet's node-record is kept, zero
 * otherwise.
 */
static __inline int
ieee80211_needs_rxnode(struct ieee80211com *ic,
    const struct ieee80211_frame *wh, const u_int8_t **bssid)
{
	int monitor, rc = 0;

	monitor = (ic->ic_opmode == IEEE80211_M_MONITOR);

	*bssid = NULL;

	switch (wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) {
	case IEEE80211_FC0_TYPE_CTL:
		if (!monitor)
			break;
		return (wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) ==
		    IEEE80211_FC0_SUBTYPE_RTS;
	case IEEE80211_FC0_TYPE_MGT:
		*bssid = wh->i_addr3;
		switch (wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) {
		case IEEE80211_FC0_SUBTYPE_BEACON:
		case IEEE80211_FC0_SUBTYPE_PROBE_RESP:
			break;
		default:
#ifndef IEEE80211_STA_ONLY
			if (ic->ic_opmode == IEEE80211_M_STA)
				break;
			rc = IEEE80211_ADDR_EQ(*bssid, ic->ic_bss->ni_bssid) ||
			     IEEE80211_ADDR_EQ(*bssid, etherbroadcastaddr);
#endif
			break;
		}
		break;
	case IEEE80211_FC0_TYPE_DATA:
		switch (wh->i_fc[1] & IEEE80211_FC1_DIR_MASK) {
		case IEEE80211_FC1_DIR_NODS:
			*bssid = wh->i_addr3;
#ifndef IEEE80211_STA_ONLY
			if (ic->ic_opmode == IEEE80211_M_IBSS ||
			    ic->ic_opmode == IEEE80211_M_AHDEMO)
				rc = IEEE80211_ADDR_EQ(*bssid,
				    ic->ic_bss->ni_bssid);
#endif
			break;
		case IEEE80211_FC1_DIR_TODS:
			*bssid = wh->i_addr1;
#ifndef IEEE80211_STA_ONLY
			if (ic->ic_opmode == IEEE80211_M_HOSTAP)
				rc = IEEE80211_ADDR_EQ(*bssid,
				    ic->ic_bss->ni_bssid);
#endif
			break;
		case IEEE80211_FC1_DIR_FROMDS:
		case IEEE80211_FC1_DIR_DSTODS:
			*bssid = wh->i_addr2;
#ifndef IEEE80211_STA_ONLY
			rc = (ic->ic_opmode == IEEE80211_M_HOSTAP);
#endif
			break;
		}
		break;
	}
	return monitor || rc;
}

/* 
 * Drivers call this, so increase the reference count before returning
 * the node.
 */
struct ieee80211_node *
ieee80211_find_rxnode(struct ieee80211com *ic,
    const struct ieee80211_frame *wh)
{
	static const u_int8_t zero[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	struct ieee80211_node *ni;
	const u_int8_t *bssid;
	int s;

	if (!ieee80211_needs_rxnode(ic, wh, &bssid))
		return ieee80211_ref_node(ic->ic_bss);

	s = splnet();
	ni = ieee80211_find_node(ic, wh->i_addr2);
	splx(s);

	if (ni != NULL)
		return ieee80211_ref_node(ni);
#ifndef IEEE80211_STA_ONLY
	if (ic->ic_opmode == IEEE80211_M_HOSTAP)
		return ieee80211_ref_node(ic->ic_bss);
#endif
	/* XXX see remarks in ieee80211_find_txnode */
	/* XXX no rate negotiation; just dup */
	if ((ni = ieee80211_dup_bss(ic, wh->i_addr2)) == NULL)
		return ieee80211_ref_node(ic->ic_bss);

	IEEE80211_ADDR_COPY(ni->ni_bssid, (bssid != NULL) ? bssid : zero);

	ni->ni_rates = ic->ic_bss->ni_rates;
	ni->ni_txrate = 0;
	if (ic->ic_newassoc)
		(*ic->ic_newassoc)(ic, ni, 1);

	DPRINTF(("faked-up node %p for %s\n", ni,
	    ether_sprintf((u_int8_t *)wh->i_addr2)));

	return ieee80211_ref_node(ni);
}

void
ieee80211_node_tx_ba_clear(struct ieee80211_node *ni, int tid)
{
	struct ieee80211_tx_ba *ba = &ni->ni_tx_ba[tid];

	if (ba->ba_state != IEEE80211_BA_INIT) {
		if (timeout_pending(&ba->ba_to))
			timeout_del(&ba->ba_to);
		ba->ba_state = IEEE80211_BA_INIT;
	}
}

void
ieee80211_ba_del(struct ieee80211_node *ni)
{
	int tid;

	for (tid = 0; tid < nitems(ni->ni_rx_ba); tid++) {
		struct ieee80211_rx_ba *ba = &ni->ni_rx_ba[tid];
		if (ba->ba_state != IEEE80211_BA_INIT) {
			if (timeout_pending(&ba->ba_to))
				timeout_del(&ba->ba_to);
			if (timeout_pending(&ba->ba_gap_to))
				timeout_del(&ba->ba_gap_to);
			ba->ba_state = IEEE80211_BA_INIT;
		}
	}

	for (tid = 0; tid < nitems(ni->ni_tx_ba); tid++)
		ieee80211_node_tx_ba_clear(ni, tid);

	timeout_del(&ni->ni_addba_req_to[EDCA_AC_BE]);
	timeout_del(&ni->ni_addba_req_to[EDCA_AC_BK]);
	timeout_del(&ni->ni_addba_req_to[EDCA_AC_VI]);
	timeout_del(&ni->ni_addba_req_to[EDCA_AC_VO]);
}

void
ieee80211_free_node(struct ieee80211com *ic, struct ieee80211_node *ni)
{
	if (ni == ic->ic_bss)
		panic("freeing bss node");

	splassert(IPL_NET);

	DPRINTF(("%s\n", ether_sprintf(ni->ni_macaddr)));
#ifndef IEEE80211_STA_ONLY
	timeout_del(&ni->ni_eapol_to);
	timeout_del(&ni->ni_sa_query_to);
	IEEE80211_AID_CLR(ni->ni_associd, ic->ic_aid_bitmap);
#endif
	ieee80211_ba_del(ni);
	RBT_REMOVE(ieee80211_tree, &ic->ic_tree, ni);
	ic->ic_nnodes--;
#ifndef IEEE80211_STA_ONLY
	if (mq_purge(&ni->ni_savedq) > 0) {
		if (ic->ic_set_tim != NULL)
			(*ic->ic_set_tim)(ic, ni->ni_associd, 0);
	}
#endif
	(*ic->ic_node_free)(ic, ni);
	/* TBD indicate to drivers that a new node can be allocated */
}

void
ieee80211_release_node(struct ieee80211com *ic, struct ieee80211_node *ni)
{
	int s;
	void (*ni_unref_cb)(struct ieee80211com *, struct ieee80211_node *);

	DPRINTF(("%s refcnt %u\n", ether_sprintf(ni->ni_macaddr),
	    ni->ni_refcnt));
	s = splnet();
	if (ieee80211_node_decref(ni) == 0) {
		if (ni->ni_unref_cb) {
			/* The callback may set ni->ni_unref_cb again. */
			ni_unref_cb = ni->ni_unref_cb;
			ni->ni_unref_cb = NULL;
 			/* Freed by callback if necessary: */
			(*ni_unref_cb)(ic, ni);
		}
	    	if (ni->ni_state == IEEE80211_STA_COLLECT)
			ieee80211_free_node(ic, ni);
	}
	splx(s);
}

void
ieee80211_free_allnodes(struct ieee80211com *ic, int clear_ic_bss)
{
	struct ieee80211_node *ni;
	int s;

	DPRINTF(("freeing all nodes\n"));
	s = splnet();
	while ((ni = RBT_MIN(ieee80211_tree, &ic->ic_tree)) != NULL)
		ieee80211_free_node(ic, ni);
	splx(s);

	if (clear_ic_bss && ic->ic_bss != NULL)
		ieee80211_node_cleanup(ic, ic->ic_bss);
}

void
ieee80211_clean_cached(struct ieee80211com *ic)
{
	struct ieee80211_node *ni, *next_ni;
	int s;

	s = splnet();
	for (ni = RBT_MIN(ieee80211_tree, &ic->ic_tree);
	    ni != NULL; ni = next_ni) {
		next_ni = RBT_NEXT(ieee80211_tree, ni);
		if (ni->ni_state == IEEE80211_STA_CACHE)
			ieee80211_free_node(ic, ni);
	}
	splx(s);
}
/*
 * Timeout inactive nodes.
 *
 * If called because of a cache timeout, which happens only in hostap and ibss
 * modes, clean all inactive cached or authenticated nodes but don't de-auth
 * any associated nodes. Also update HT protection settings.
 *
 * Else, this function is called because a new node must be allocated but the
 * node cache is full. In this case, return as soon as a free slot was made
 * available. If acting as hostap, clean cached nodes regardless of their
 * recent activity and also allow de-authing of authenticated nodes older
 * than one cache wait interval, and de-authing of inactive associated nodes.
 */
void
ieee80211_clean_nodes(struct ieee80211com *ic, int cache_timeout)
{
	struct ieee80211_node *ni, *next_ni;
	u_int gen = ic->ic_scangen++;		/* NB: ok 'cuz single-threaded*/
	int s;
#ifndef IEEE80211_STA_ONLY
	int nnodes = 0, nonht = 0, nonhtassoc = 0;
	struct ifnet *ifp = &ic->ic_if;
	enum ieee80211_htprot htprot = IEEE80211_HTPROT_NONE;
	enum ieee80211_protmode protmode = IEEE80211_PROT_NONE;
#endif

	s = splnet();
	for (ni = RBT_MIN(ieee80211_tree, &ic->ic_tree);
	    ni != NULL; ni = next_ni) {
		next_ni = RBT_NEXT(ieee80211_tree, ni);
		if (!cache_timeout && ic->ic_nnodes < ic->ic_max_nnodes)
			break;
		if (ni->ni_scangen == gen)	/* previously handled */
			continue;
#ifndef IEEE80211_STA_ONLY
		nnodes++;
		if ((ic->ic_flags & IEEE80211_F_HTON) && cache_timeout) {
			/*
			 * Check if node supports 802.11n.
			 * Only require HT capabilities IE for this check.
			 * Nodes might never reveal their supported MCS to us
			 * unless they go through a full association sequence.
			 * ieee80211_node_supports_ht() could misclassify them.
			 */
			if ((ni->ni_flags & IEEE80211_NODE_HTCAP) == 0) {
				nonht++;
				if (ni->ni_state == IEEE80211_STA_ASSOC)
					nonhtassoc++;
			}
		}
#endif
		ni->ni_scangen = gen;
		if (ni->ni_refcnt > 0)
			continue;
#ifndef IEEE80211_STA_ONLY
		if ((ic->ic_opmode == IEEE80211_M_HOSTAP ||
		    ic->ic_opmode == IEEE80211_M_IBSS) &&
		    ic->ic_state == IEEE80211_S_RUN) {
			if (cache_timeout) {
				if (ni->ni_state != IEEE80211_STA_COLLECT &&
				    (ni->ni_state == IEEE80211_STA_ASSOC ||
				    ni->ni_inact < IEEE80211_INACT_MAX))
					continue;
			} else {
				if (ic->ic_opmode == IEEE80211_M_HOSTAP &&
				    ((ni->ni_state == IEEE80211_STA_ASSOC &&
				    ni->ni_inact < IEEE80211_INACT_MAX) ||
				    (ni->ni_state == IEEE80211_STA_AUTH &&
				     ni->ni_inact == 0)))
				    	continue;

				if (ic->ic_opmode == IEEE80211_M_IBSS &&
				    ni->ni_state != IEEE80211_STA_COLLECT &&
				    ni->ni_state != IEEE80211_STA_CACHE &&
				    ni->ni_inact < IEEE80211_INACT_MAX)
					continue;
			}
		}
		if (ifp->if_flags & IFF_DEBUG)
			printf("%s: station %s purged from node cache\n",
			    ifp->if_xname, ether_sprintf(ni->ni_macaddr));
#endif
		/*
		 * If we're hostap and the node is authenticated, send
		 * a deauthentication frame. The node will be freed when
		 * the driver calls ieee80211_release_node().
		 */
#ifndef IEEE80211_STA_ONLY
		nnodes--;
		if ((ic->ic_flags & IEEE80211_F_HTON) && cache_timeout) {
			if ((ni->ni_flags & IEEE80211_NODE_HTCAP) == 0) {
				nonht--;
				if (ni->ni_state == IEEE80211_STA_ASSOC)
					nonhtassoc--;
			}
		}
		if (ic->ic_opmode == IEEE80211_M_HOSTAP &&
		    ni->ni_state >= IEEE80211_STA_AUTH &&
		    ni->ni_state != IEEE80211_STA_COLLECT) {
			IEEE80211_SEND_MGMT(ic, ni,
			    IEEE80211_FC0_SUBTYPE_DEAUTH,
			    IEEE80211_REASON_AUTH_EXPIRE);
			ieee80211_node_leave(ic, ni);
		} else
#endif
			ieee80211_free_node(ic, ni);
		ic->ic_stats.is_node_timeout++;
	}

#ifndef IEEE80211_STA_ONLY
	if ((ic->ic_flags & IEEE80211_F_HTON) && cache_timeout) {
		uint16_t htop1 = ic->ic_bss->ni_htop1;

		/* Update HT protection settings. */
		if (nonht) {
			protmode = IEEE80211_PROT_CTSONLY;
			if (nonhtassoc)
				htprot = IEEE80211_HTPROT_NONHT_MIXED;
			else
				htprot = IEEE80211_HTPROT_NONMEMBER;
		}
		if ((htop1 & IEEE80211_HTOP1_PROT_MASK) != htprot) {
			htop1 &= ~IEEE80211_HTOP1_PROT_MASK;
			htop1 |= htprot;
			ic->ic_bss->ni_htop1 = htop1;
			ic->ic_protmode = protmode;
			if (ic->ic_updateprot)
				ic->ic_updateprot(ic);
		}
	}

	/* 
	 * During a cache timeout we iterate over all nodes.
	 * Check for node leaks by comparing the actual number of cached
	 * nodes with the ic_nnodes count, which is maintained while adding
	 * and removing nodes from the cache.
	 */
	if ((ifp->if_flags & IFF_DEBUG) && cache_timeout &&
	    nnodes != ic->ic_nnodes)
		printf("%s: number of cached nodes is %d, expected %d,"
		    "possible nodes leak\n", ifp->if_xname, nnodes,
		    ic->ic_nnodes);
#endif
	splx(s);
}

void
ieee80211_clean_inactive_nodes(struct ieee80211com *ic, int inact_max)
{
	struct ieee80211_node *ni, *next_ni;
	u_int gen = ic->ic_scangen++;	/* NB: ok 'cuz single-threaded*/
	int s;

	s = splnet();
	for (ni = RBT_MIN(ieee80211_tree, &ic->ic_tree);
	    ni != NULL; ni = next_ni) {
		next_ni = RBT_NEXT(ieee80211_tree, ni);
		if (ni->ni_scangen == gen)	/* previously handled */
			continue;
		ni->ni_scangen = gen;
		if (ni->ni_refcnt > 0 || ni->ni_inact < inact_max)
			continue;
		ieee80211_free_node(ic, ni);
		ic->ic_stats.is_node_timeout++;
	}

	splx(s);
}

void
ieee80211_iterate_nodes(struct ieee80211com *ic, ieee80211_iter_func *f,
    void *arg)
{
	struct ieee80211_node *ni;
	int s;

	s = splnet();
	RBT_FOREACH(ni, ieee80211_tree, &ic->ic_tree)
		(*f)(arg, ni);
	splx(s);
}


/*
 * Install received HT caps information in the node's state block.
 */
void
ieee80211_setup_htcaps(struct ieee80211_node *ni, const uint8_t *data,
    uint8_t len)
{
	uint16_t rxrate;

	if (len != 26)
		return;

	ni->ni_htcaps = (data[0] | (data[1] << 8));
	ni->ni_ampdu_param = data[2];

	memcpy(ni->ni_rxmcs, &data[3], sizeof(ni->ni_rxmcs));
	/* clear reserved bits */
	clrbit(ni->ni_rxmcs, 77);
	clrbit(ni->ni_rxmcs, 78);
	clrbit(ni->ni_rxmcs, 79);

	/* Max MCS Rx rate in 1Mb/s units (0 means "not specified"). */
	rxrate = ((data[13] | (data[14]) << 8) & IEEE80211_MCS_RX_RATE_HIGH);
	if (rxrate < 1024)
		ni->ni_max_rxrate = rxrate;

	ni->ni_tx_mcs_set = data[15];
	ni->ni_htxcaps = (data[19] | (data[20] << 8));
	ni->ni_txbfcaps = (data[21] | (data[22] << 8) | (data[23] << 16) |
		(data[24] << 24));
	ni->ni_aselcaps = data[25];

	ni->ni_flags |= IEEE80211_NODE_HTCAP;
}

#ifndef IEEE80211_STA_ONLY
/* 
 * Handle nodes switching from 11n into legacy modes.
 */
void
ieee80211_clear_htcaps(struct ieee80211_node *ni)
{
	ni->ni_htcaps = 0;
	ni->ni_ampdu_param = 0;
	memset(ni->ni_rxmcs, 0, sizeof(ni->ni_rxmcs));
	ni->ni_max_rxrate = 0;
	ni->ni_tx_mcs_set = 0;
	ni->ni_htxcaps = 0;
	ni->ni_txbfcaps = 0;
	ni->ni_aselcaps = 0;

	ni->ni_flags &= ~(IEEE80211_NODE_HT | IEEE80211_NODE_HT_SGI20 |
	    IEEE80211_NODE_HT_SGI40 | IEEE80211_NODE_HTCAP);

}
#endif

int
ieee80211_40mhz_valid_secondary_above(uint8_t primary_chan)
{
	static const uint8_t valid_secondary_chan[] = {
		5, 6, 7, 8, 9, 10, 11, 12, 13,
		40, 48, 56, 64, 104, 112, 120, 128, 136, 144, 153, 161
	};
	uint8_t secondary_chan;
	int i;

	if ((primary_chan >= 1 && primary_chan <= 9) ||
	    (primary_chan >= 36 && primary_chan <= 157))
		secondary_chan = primary_chan + 4;
	else
		return 0;

	for (i = 0; i < nitems(valid_secondary_chan); i++) {
		if (secondary_chan == valid_secondary_chan[i])
			return 1;
	}

	return 0;
}

int
ieee80211_40mhz_valid_secondary_below(uint8_t primary_chan)
{
	static const uint8_t valid_secondary_chan[] = {
		1, 2, 3, 4, 5, 6, 7, 8, 9,
		36, 44, 52, 60, 100, 108, 116, 124, 132, 140, 149, 157
	};
	int8_t secondary_chan;
	int i;

	if ((primary_chan >= 5 && primary_chan <= 13) ||
	    (primary_chan >= 40 && primary_chan <= 161))
		secondary_chan = primary_chan - 4;
	else
		return 0;

	for (i = 0; i < nitems(valid_secondary_chan); i++) {
		if (secondary_chan == valid_secondary_chan[i])
			return 1;
	}

	return 0;
}

/*
 * Only accept 40 MHz channel configurations that conform to
 * regulatory operating classes as defined by the 802.11ac spec.
 * Passing other configurations down to firmware can result in
 * regulatory assertions being triggered, such as fatal firmware
 * error 14FD in iwm(4).
 *
 * See 802.11ac 2013, page 380, Tables E-1 to E-5.
 */
int
ieee80211_40mhz_center_freq_valid(uint8_t primary_chan, uint8_t htop0)
{
	uint8_t sco;

	sco = ((htop0 & IEEE80211_HTOP0_SCO_MASK) >> IEEE80211_HTOP0_SCO_SHIFT);
	switch (sco) {
	case IEEE80211_HTOP0_SCO_SCN:
		return 1;
	case IEEE80211_HTOP0_SCO_SCA:
		return ieee80211_40mhz_valid_secondary_above(primary_chan);
	case IEEE80211_HTOP0_SCO_SCB:
		return ieee80211_40mhz_valid_secondary_below(primary_chan);
	}

	return 0;
}

/*
 * Install received HT op information in the node's state block.
 */
int
ieee80211_setup_htop(struct ieee80211_node *ni, const uint8_t *data,
    uint8_t len, int isprobe)
{
	if (len != 22)
		return 0;

	ni->ni_primary_chan = data[0]; /* corresponds to ni_chan */
	ni->ni_htop0 = data[1];
	if (!ieee80211_40mhz_center_freq_valid(data[0], data[1]))
		ni->ni_htop0 &= ~IEEE80211_HTOP0_SCO_MASK;
	ni->ni_htop1 = (data[2] | (data[3] << 8));
	ni->ni_htop2 = (data[3] | (data[4] << 8));

	/*
	 * According to 802.11-2012 Table 8-130 the Basic MCS set is
	 * only "present in Beacon, Probe Response, Mesh Peering Open
	 * and Mesh Peering Confirm frames. Otherwise reserved."
	 */
	if (isprobe)
		memcpy(ni->ni_basic_mcs, &data[6], sizeof(ni->ni_basic_mcs));

	return 1;
}

/*
 * Install received VHT caps information in the node's state block.
 */
void
ieee80211_setup_vhtcaps(struct ieee80211_node *ni, const uint8_t *data,
    uint8_t len)
{
	if (len != 12)
		return;

	ni->ni_vhtcaps = (data[0] | (data[1] << 8) | data[2] << 16 |
	    data[3] << 24);
	ni->ni_vht_rxmcs = (data[4] | (data[5] << 8));
	ni->ni_vht_rx_max_lgi_mbit_s = ((data[6] | (data[7] << 8)) &
	    IEEE80211_VHT_MAX_LGI_MBIT_S_MASK);
	ni->ni_vht_txmcs = (data[8] | (data[9] << 8));
	ni->ni_vht_tx_max_lgi_mbit_s = ((data[10] | (data[11] << 8)) &
	    IEEE80211_VHT_MAX_LGI_MBIT_S_MASK);

	ni->ni_flags |= IEEE80211_NODE_VHTCAP;
}

/*
 * Only accept 80 MHz channel configurations that conform to
 * regulatory operating classes as defined by the 802.11ac spec.
 * Passing other configurations down to firmware can result in
 * regulatory assertions being triggered, such as fatal firmware
 * error 14FD in iwm(4).
 *
 * See 802.11ac 2013, page 380, Tables E-1 to E-5.
 */
int
ieee80211_80mhz_center_freq_valid(const uint8_t chanidx)
{
	static const uint8_t valid_center_chanidx[] = {
		42, 50, 58, 106, 112, 114, 138, 155
	};
	int i;

	for (i = 0; i < nitems(valid_center_chanidx); i++) {
		if (chanidx == valid_center_chanidx[i])
			return 1;
	}

	return 0;
}

/*
 * Install received VHT op information in the node's state block.
 */
int
ieee80211_setup_vhtop(struct ieee80211_node *ni, const uint8_t *data,
    uint8_t len, int isprobe)
{
	uint8_t sco;
	int have_40mhz;

	if (len != 5)
		return 0;

	if (data[0] != IEEE80211_VHTOP0_CHAN_WIDTH_HT &&
	    data[0] != IEEE80211_VHTOP0_CHAN_WIDTH_80 &&
	    data[0] != IEEE80211_VHTOP0_CHAN_WIDTH_160 &&
	    data[0] != IEEE80211_VHTOP0_CHAN_WIDTH_8080)
		return 0;

	sco = ((ni->ni_htop0 & IEEE80211_HTOP0_SCO_MASK) >>
	    IEEE80211_HTOP0_SCO_SHIFT);
	have_40mhz = (sco == IEEE80211_HTOP0_SCO_SCA ||
	    sco == IEEE80211_HTOP0_SCO_SCB);

	if (have_40mhz && ieee80211_80mhz_center_freq_valid(data[1])) {
		ni->ni_vht_chan_width = data[0];
		ni->ni_vht_chan_center_freq_idx0 = data[1];

		/* Only used in non-consecutive 80-80 160MHz configs. */
		if (data[2] && ieee80211_80mhz_center_freq_valid(data[2]))
			ni->ni_vht_chan_center_freq_idx1 = data[2];
		else
			ni->ni_vht_chan_center_freq_idx1 = 0;
	} else {
		ni->ni_vht_chan_width = IEEE80211_VHTOP0_CHAN_WIDTH_HT;
		ni->ni_vht_chan_center_freq_idx0 = 0;
		ni->ni_vht_chan_center_freq_idx1 = 0;
	}

	ni->ni_vht_basic_mcs = (data[3] | data[4] << 8);
	return 1;
}

#ifndef IEEE80211_STA_ONLY
/* 
 * Handle nodes switching from 11ac into legacy modes.
 */
void
ieee80211_clear_vhtcaps(struct ieee80211_node *ni)
{
	ni->ni_vhtcaps = 0;
	ni->ni_vht_rxmcs = 0;
	ni->ni_vht_rx_max_lgi_mbit_s = 0;
	ni->ni_vht_txmcs = 0;
	ni->ni_vht_tx_max_lgi_mbit_s = 0;

	ni->ni_flags &= ~(IEEE80211_NODE_VHT | IEEE80211_NODE_VHT_SGI80 |
	    IEEE80211_NODE_VHT_SGI160 | IEEE80211_NODE_VHTCAP);

}
#endif

/*
 * Install received rate set information in the node's state block.
 */
int
ieee80211_setup_rates(struct ieee80211com *ic, struct ieee80211_node *ni,
    const u_int8_t *rates, const u_int8_t *xrates, int flags)
{
	struct ieee80211_rateset *rs = &ni->ni_rates;

	memset(rs, 0, sizeof(*rs));
	rs->rs_nrates = rates[1];
	memcpy(rs->rs_rates, rates + 2, rs->rs_nrates);
	if (xrates != NULL) {
		u_int8_t nxrates;
		/*
		 * Tack on 11g extended supported rate element.
		 */
		nxrates = xrates[1];
		if (rs->rs_nrates + nxrates > IEEE80211_RATE_MAXSIZE) {
			nxrates = IEEE80211_RATE_MAXSIZE - rs->rs_nrates;
			DPRINTF(("extended rate set too large; "
			    "only using %u of %u rates\n",
			    nxrates, xrates[1]));
			ic->ic_stats.is_rx_rstoobig++;
		}
		memcpy(rs->rs_rates + rs->rs_nrates, xrates+2, nxrates);
		rs->rs_nrates += nxrates;

		/* 11g support implies ERP support */
		if (nxrates > 0 && IEEE80211_IS_CHAN_2GHZ(ni->ni_chan))
			ni->ni_flags |= IEEE80211_NODE_ERP;
	}
	return ieee80211_fix_rate(ic, ni, flags);
}

/* 
 * Return the 11a/b/g mode mutually supported for the given node.
 * ni->ni_chan must be set before calling this, and ieee80211_setup_rates()
 * should be called beforehand to properly differentiate 11b and 11g.
 */
enum ieee80211_phymode
ieee80211_node_abg_mode(struct ieee80211com *ic, struct ieee80211_node *ni)
{
	/* Handle the case where our own phy mode was fixed by ifconfig. */
	switch (IFM_MODE(ic->ic_media.ifm_cur->ifm_media)) {
	case IFM_IEEE80211_11A:
		return IEEE80211_MODE_11A; /* Peer uses 11a. */
	case IFM_IEEE80211_11B:
		return IEEE80211_MODE_11B; /* Peer uses 11b. */
	case IFM_IEEE80211_11G:
		/* Peer could be using either 11g or 11b, check below. */
		break;
	default:
		break;
	}

	/* Our own phy mode is either 11G or AUTO. */

	if (IEEE80211_IS_CHAN_5GHZ(ni->ni_chan))
		return IEEE80211_MODE_11A;

	if ((ni->ni_flags & IEEE80211_NODE_ERP) &&
	    (ni->ni_chan->ic_flags &
	    (IEEE80211_CHAN_OFDM | IEEE80211_CHAN_DYN)) != 0)
		return IEEE80211_MODE_11G;

	return IEEE80211_MODE_11B;
}

void
ieee80211_node_trigger_addba_req(struct ieee80211_node *ni, int tid)
{
	if (ni->ni_tx_ba[tid].ba_state == IEEE80211_BA_INIT &&
	    !timeout_pending(&ni->ni_addba_req_to[tid])) {
		timeout_add_sec(&ni->ni_addba_req_to[tid],
		    ni->ni_addba_req_intval[tid]);
	}
}

void
ieee80211_node_addba_request(struct ieee80211_node *ni, int tid)
{
	struct ieee80211com *ic = ni->ni_ic;
	uint16_t ssn = ni->ni_qos_txseqs[tid];

	ieee80211_addba_request(ic, ni, ssn, tid);
}

void
ieee80211_node_addba_request_ac_be_to(void *arg)
{
	struct ieee80211_node *ni = arg;
	ieee80211_node_addba_request(ni, EDCA_AC_BE);
}

void
ieee80211_node_addba_request_ac_bk_to(void *arg)
{
	struct ieee80211_node *ni = arg;
	ieee80211_node_addba_request(ni, EDCA_AC_BK);
}

void
ieee80211_node_addba_request_ac_vi_to(void *arg)
{
	struct ieee80211_node *ni = arg;
	ieee80211_node_addba_request(ni, EDCA_AC_VI);
}

void
ieee80211_node_addba_request_ac_vo_to(void *arg)
{
	struct ieee80211_node *ni = arg;
	ieee80211_node_addba_request(ni, EDCA_AC_VO);
}

#ifndef IEEE80211_STA_ONLY
/*
 * This function is called to notify the 802.1X PACP machine that a new
 * 802.1X port is enabled and must be authenticated. For 802.11, a port
 * becomes enabled whenever a STA successfully completes Open System
 * authentication with an AP.
 */
void
ieee80211_needs_auth(struct ieee80211com *ic, struct ieee80211_node *ni)
{
	/*
	 * XXX this could be done via the route socket of via a dedicated
	 * EAP socket or another kernel->userland notification mechanism.
	 * The notification should include the MAC address (ni_macaddr).
	 */
}

/*
 * Handle an HT STA joining an HT network.
 */
void
ieee80211_node_join_ht(struct ieee80211com *ic, struct ieee80211_node *ni)
{
	enum ieee80211_htprot;

	/* Update HT protection setting. */
	if ((ni->ni_flags & IEEE80211_NODE_HT) == 0) {
		uint16_t htop1 = ic->ic_bss->ni_htop1;
		htop1 &= ~IEEE80211_HTOP1_PROT_MASK;
		htop1 |= IEEE80211_HTPROT_NONHT_MIXED;
		ic->ic_bss->ni_htop1 = htop1;
		if (ic->ic_updateprot)
			ic->ic_updateprot(ic);
	}
}

/*
 * Handle a station joining an RSN network.
 */
void
ieee80211_node_join_rsn(struct ieee80211com *ic, struct ieee80211_node *ni)
{
	DPRINTF(("station %s associated using proto %d akm 0x%x "
	    "cipher 0x%x groupcipher 0x%x\n", ether_sprintf(ni->ni_macaddr),
	    ni->ni_rsnprotos, ni->ni_rsnakms, ni->ni_rsnciphers,
	    ni->ni_rsngroupcipher));

	ni->ni_rsn_state = RSNA_AUTHENTICATION;

	ni->ni_key_count = 0;
	ni->ni_port_valid = 0;
	ni->ni_flags &= ~IEEE80211_NODE_TXRXPROT;
	ni->ni_flags &= ~IEEE80211_NODE_RSN_NEW_PTK;
	ni->ni_replaycnt = -1;	/* XXX */
	ni->ni_rsn_retries = 0;
	ni->ni_rsncipher = ni->ni_rsnciphers;

	ni->ni_rsn_state = RSNA_AUTHENTICATION_2;

	/* generate a new authenticator nonce (ANonce) */
	arc4random_buf(ni->ni_nonce, EAPOL_KEY_NONCE_LEN);

	if (!ieee80211_is_8021x_akm(ni->ni_rsnakms)) {
		memcpy(ni->ni_pmk, ic->ic_psk, IEEE80211_PMK_LEN);
		ni->ni_flags |= IEEE80211_NODE_PMK;
		(void)ieee80211_send_4way_msg1(ic, ni);
	} else if (ni->ni_flags & IEEE80211_NODE_PMK) {
		/* skip 802.1X auth if a cached PMK was found */
		(void)ieee80211_send_4way_msg1(ic, ni);
	} else {
		/* no cached PMK found, needs full 802.1X auth */
		ieee80211_needs_auth(ic, ni);
	}
}

void
ieee80211_count_longslotsta(void *arg, struct ieee80211_node *ni)
{
	int *longslotsta = arg;

	if (ni->ni_associd == 0 || ni->ni_state == IEEE80211_STA_COLLECT)
		return;

	if (!(ni->ni_capinfo & IEEE80211_CAPINFO_SHORT_SLOTTIME))
		(*longslotsta)++;
}

void
ieee80211_count_nonerpsta(void *arg, struct ieee80211_node *ni)
{
	int *nonerpsta = arg;

	if (ni->ni_associd == 0 || ni->ni_state == IEEE80211_STA_COLLECT)
		return;

	if ((ni->ni_flags & IEEE80211_NODE_ERP) == 0)
		(*nonerpsta)++;
}

void
ieee80211_count_pssta(void *arg, struct ieee80211_node *ni)
{
	int *pssta = arg;

	if (ni->ni_associd == 0 || ni->ni_state == IEEE80211_STA_COLLECT)
		return;

	if (ni->ni_pwrsave == IEEE80211_PS_DOZE)
		(*pssta)++;
}

void
ieee80211_count_rekeysta(void *arg, struct ieee80211_node *ni)
{
	int *rekeysta = arg;

	if (ni->ni_associd == 0 || ni->ni_state == IEEE80211_STA_COLLECT)
		return;

	if (ni->ni_flags & IEEE80211_NODE_REKEY)
		(*rekeysta)++;
}

/*
 * Handle a station joining an 11g network.
 */
void
ieee80211_node_join_11g(struct ieee80211com *ic, struct ieee80211_node *ni)
{
	int longslotsta = 0, nonerpsta = 0;

	if (!(ni->ni_capinfo & IEEE80211_CAPINFO_SHORT_SLOTTIME)) {
		/*
		 * Joining STA doesn't support short slot time.  We must
		 * disable the use of short slot time for all other associated
		 * STAs and give the driver a chance to reconfigure the
		 * hardware.
		 */
		ieee80211_iterate_nodes(ic,
		    ieee80211_count_longslotsta, &longslotsta);
		if (longslotsta == 1) {
			if (ic->ic_caps & IEEE80211_C_SHSLOT)
				ieee80211_set_shortslottime(ic, 0);
		}
		DPRINTF(("[%s] station needs long slot time, count %d\n",
		    ether_sprintf(ni->ni_macaddr), longslotsta));
	}

	if ((ni->ni_flags & IEEE80211_NODE_ERP) == 0) {
		/*
		 * Joining STA is non-ERP.
		 */
		ieee80211_iterate_nodes(ic,
		    ieee80211_count_nonerpsta, &nonerpsta);
		DPRINTF(("[%s] station is non-ERP, %d non-ERP "
		    "stations associated\n", ether_sprintf(ni->ni_macaddr),
		    nonerpsta));
		/* must enable the use of protection */
		if (ic->ic_protmode != IEEE80211_PROT_NONE) {
			DPRINTF(("enable use of protection\n"));
			ic->ic_flags |= IEEE80211_F_USEPROT;
		}

		if (!(ni->ni_capinfo & IEEE80211_CAPINFO_SHORT_PREAMBLE))
			ic->ic_flags &= ~IEEE80211_F_SHPREAMBLE;
	}
}

void
ieee80211_node_join(struct ieee80211com *ic, struct ieee80211_node *ni,
    int resp)
{
	int newassoc = (ni->ni_state != IEEE80211_STA_ASSOC);

	if (ni->ni_associd == 0) {
		u_int16_t aid;

		/*
		 * It would be clever to search the bitmap
		 * more efficiently, but this will do for now.
		 */
		for (aid = 1; aid < ic->ic_max_aid; aid++) {
			if (!IEEE80211_AID_ISSET(aid,
			    ic->ic_aid_bitmap))
				break;
		}
		if (aid >= ic->ic_max_aid) {
			IEEE80211_SEND_MGMT(ic, ni, resp,
			    IEEE80211_REASON_ASSOC_TOOMANY);
			ieee80211_node_leave(ic, ni);
			return;
		}
		ni->ni_associd = aid | 0xc000;
		IEEE80211_AID_SET(ni->ni_associd, ic->ic_aid_bitmap);
		if (ic->ic_curmode == IEEE80211_MODE_11G ||
		    (ic->ic_curmode == IEEE80211_MODE_11N &&
		    IEEE80211_IS_CHAN_2GHZ(ic->ic_bss->ni_chan)))
			ieee80211_node_join_11g(ic, ni);
	}

	DPRINTF(("station %s %s associated at aid %d\n",
	    ether_sprintf(ni->ni_macaddr), newassoc ? "newly" : "already",
	    ni->ni_associd & ~0xc000));

	ieee80211_ht_negotiate(ic, ni);
	if (ic->ic_flags & IEEE80211_F_HTON)
		ieee80211_node_join_ht(ic, ni);

	/* give driver a chance to setup state like ni_txrate */
	if (ic->ic_newassoc)
		(*ic->ic_newassoc)(ic, ni, newassoc);
	IEEE80211_SEND_MGMT(ic, ni, resp, IEEE80211_STATUS_SUCCESS);
	ieee80211_node_newstate(ni, IEEE80211_STA_ASSOC);

	if (!(ic->ic_flags & IEEE80211_F_RSNON)) {
		ni->ni_port_valid = 1;
		ni->ni_rsncipher = IEEE80211_CIPHER_USEGROUP;
	} else
		ieee80211_node_join_rsn(ic, ni);

#if NBRIDGE > 0
	/*
	 * If the parent interface is a bridge port, learn
	 * the node's address dynamically on this interface.
	 */
	if (ic->ic_if.if_bridgeidx != 0)
		bridge_update(&ic->ic_if,
		    (struct ether_addr *)ni->ni_macaddr, 0);
#endif
}

/*
 * Handle an HT STA leaving an HT network.
 */
void
ieee80211_node_leave_ht(struct ieee80211com *ic, struct ieee80211_node *ni)
{
	struct ieee80211_rx_ba *ba;
	u_int8_t tid;
	int i;

	/* free all Block Ack records */
	ieee80211_ba_del(ni);
	for (tid = 0; tid < IEEE80211_NUM_TID; tid++) {
		ba = &ni->ni_rx_ba[tid];
		if (ba->ba_buf != NULL) {
			for (i = 0; i < IEEE80211_BA_MAX_WINSZ; i++)
				m_freem(ba->ba_buf[i].m);
			free(ba->ba_buf, M_DEVBUF,
			    IEEE80211_BA_MAX_WINSZ * sizeof(*ba->ba_buf));
			ba->ba_buf = NULL;
		}
	}

	ieee80211_clear_htcaps(ni);
}

/*
 * Handle a VHT STA leaving a VHT network.
 */
void
ieee80211_node_leave_vht(struct ieee80211com *ic, struct ieee80211_node *ni)
{
	ieee80211_clear_vhtcaps(ni);
}

/*
 * Handle a station leaving an RSN network.
 */
void
ieee80211_node_leave_rsn(struct ieee80211com *ic, struct ieee80211_node *ni)
{
	int rekeysta = 0;

	ni->ni_rsn_state = RSNA_INITIALIZE;
	if (ni->ni_flags & IEEE80211_NODE_REKEY) {
		ni->ni_flags &= ~IEEE80211_NODE_REKEY;
		ieee80211_iterate_nodes(ic,
		    ieee80211_count_rekeysta, &rekeysta);
		if (rekeysta == 0)
			ieee80211_setkeysdone(ic);
	}
	ni->ni_flags &= ~IEEE80211_NODE_PMK;
	ni->ni_rsn_gstate = RSNA_IDLE;

	timeout_del(&ni->ni_eapol_to);
	timeout_del(&ni->ni_sa_query_to);

	ni->ni_rsn_retries = 0;
	ni->ni_flags &= ~IEEE80211_NODE_TXRXPROT;
	ni->ni_port_valid = 0;
	(*ic->ic_delete_key)(ic, ni, &ni->ni_pairwise_key);
}

/*
 * Handle a station leaving an 11g network.
 */
void
ieee80211_node_leave_11g(struct ieee80211com *ic, struct ieee80211_node *ni)
{
	int longslotsta = 0, nonerpsta = 0;

	if (!(ni->ni_capinfo & IEEE80211_CAPINFO_SHORT_SLOTTIME)) {
		/* leaving STA did not support short slot time */
		ieee80211_iterate_nodes(ic,
		    ieee80211_count_longslotsta, &longslotsta);
		if (longslotsta == 1) {
			/*
			 * All associated STAs now support short slot time, so
			 * enable this feature and give the driver a chance to
			 * reconfigure the hardware. Notice that IBSS always
			 * use a long slot time.
			 */
			if ((ic->ic_caps & IEEE80211_C_SHSLOT) &&
			    ic->ic_opmode != IEEE80211_M_IBSS)
				ieee80211_set_shortslottime(ic, 1);
		}
		DPRINTF(("[%s] long slot time station leaves, count %d\n",
		    ether_sprintf(ni->ni_macaddr), longslotsta));
	}

	if (!(ni->ni_flags & IEEE80211_NODE_ERP)) {
		/* leaving STA was non-ERP */
		ieee80211_iterate_nodes(ic,
		    ieee80211_count_nonerpsta, &nonerpsta);
		if (nonerpsta == 1) {
			/*
			 * All associated STAs are now ERP capable, disable use
			 * of protection and re-enable short preamble support.
			 */
			ic->ic_flags &= ~IEEE80211_F_USEPROT;
			if (ic->ic_caps & IEEE80211_C_SHPREAMBLE)
				ic->ic_flags |= IEEE80211_F_SHPREAMBLE;
		}
		DPRINTF(("[%s] non-ERP station leaves, count %d\n",
		    ether_sprintf(ni->ni_macaddr), nonerpsta));
	}
}

void
ieee80211_node_leave_pwrsave(struct ieee80211com *ic,
    struct ieee80211_node *ni)
{
	struct mbuf_queue keep = MBUF_QUEUE_INITIALIZER(IFQ_MAXLEN, IPL_NET);
	struct mbuf *m;

	if (ni->ni_pwrsave == IEEE80211_PS_DOZE)
		ni->ni_pwrsave = IEEE80211_PS_AWAKE;

	if (mq_len(&ni->ni_savedq) > 0) {
		if (ic->ic_set_tim != NULL)
			(*ic->ic_set_tim)(ic, ni->ni_associd, 0);
	}
	while ((m = mq_dequeue(&ni->ni_savedq)) != NULL) {
		if (ni->ni_refcnt > 0)
			ieee80211_node_decref(ni);
		m_freem(m);
	}

	/* Purge frames queued for transmission during DTIM. */
	while ((m = mq_dequeue(&ic->ic_pwrsaveq)) != NULL) {
		if (m->m_pkthdr.ph_cookie == ni) {
			if (ni->ni_refcnt > 0)
				ieee80211_node_decref(ni);
			m_freem(m);
		} else
			mq_enqueue(&keep, m);
	}
	while ((m = mq_dequeue(&keep)) != NULL)
		mq_enqueue(&ic->ic_pwrsaveq, m);
}

/*
 * Handle bookkeeping for station deauthentication/disassociation
 * when operating as an ap.
 */
void
ieee80211_node_leave(struct ieee80211com *ic, struct ieee80211_node *ni)
{
	if (ic->ic_opmode != IEEE80211_M_HOSTAP)
		panic("not in ap mode, mode %u", ic->ic_opmode);

	if (ni->ni_state == IEEE80211_STA_COLLECT)
		return;
	/*
	 * If node wasn't previously associated all we need to do is
	 * reclaim the reference.
	 */
	if (ni->ni_associd == 0) {
		ieee80211_node_newstate(ni, IEEE80211_STA_COLLECT);
		return;
	}

	ieee80211_node_leave_pwrsave(ic, ni);

	if (ic->ic_flags & IEEE80211_F_RSNON)
		ieee80211_node_leave_rsn(ic, ni);

	if (ic->ic_curmode == IEEE80211_MODE_11G ||
	    (ic->ic_curmode == IEEE80211_MODE_11N &&
	    IEEE80211_IS_CHAN_2GHZ(ic->ic_bss->ni_chan)))
		ieee80211_node_leave_11g(ic, ni);

	if (ni->ni_flags & IEEE80211_NODE_HT)
		ieee80211_node_leave_ht(ic, ni);
	if (ni->ni_flags & IEEE80211_NODE_VHT)
		ieee80211_node_leave_vht(ic, ni);

	if (ic->ic_node_leave != NULL)
		(*ic->ic_node_leave)(ic, ni);

	ieee80211_node_newstate(ni, IEEE80211_STA_COLLECT);

#if NBRIDGE > 0
	/*
	 * If the parent interface is a bridge port, delete
	 * any dynamically learned address for this node.
	 */
	if (ic->ic_if.if_bridgeidx != 0)
		bridge_update(&ic->ic_if,
		    (struct ether_addr *)ni->ni_macaddr, 1);
#endif
}

static int
ieee80211_do_slow_print(struct ieee80211com *ic, int *did_print)
{
	static const struct timeval merge_print_intvl = {
		.tv_sec = 1, .tv_usec = 0
	};
	if ((ic->ic_if.if_flags & IFF_LINK0) == 0)
		return 0;
	if (!*did_print && (ic->ic_if.if_flags & IFF_DEBUG) == 0 &&
	    !ratecheck(&ic->ic_last_merge_print, &merge_print_intvl))
		return 0;

	*did_print = 1;
	return 1;
}

/* ieee80211_ibss_merge helps merge 802.11 ad hoc networks.  The
 * convention, set by the Wireless Ethernet Compatibility Alliance
 * (WECA), is that an 802.11 station will change its BSSID to match
 * the "oldest" 802.11 ad hoc network, on the same channel, that
 * has the station's desired SSID.  The "oldest" 802.11 network
 * sends beacons with the greatest TSF timestamp.
 *
 * Return ENETRESET if the BSSID changed, 0 otherwise.
 *
 * XXX Perhaps we should compensate for the time that elapses
 * between the MAC receiving the beacon and the host processing it
 * in ieee80211_ibss_merge.
 */
int
ieee80211_ibss_merge(struct ieee80211com *ic, struct ieee80211_node *ni,
    u_int64_t local_tsft)
{
	u_int64_t beacon_tsft;
	int did_print = 0, sign;
	union {
		u_int64_t	word;
		u_int8_t	tstamp[8];
	} u;

	/* ensure alignment */
	(void)memcpy(&u, &ni->ni_tstamp[0], sizeof(u));
	beacon_tsft = letoh64(u.word);

	/* we are faster, let the other guy catch up */
	if (beacon_tsft < local_tsft)
		sign = -1;
	else
		sign = 1;

	if (IEEE80211_ADDR_EQ(ni->ni_bssid, ic->ic_bss->ni_bssid)) {
		if (!ieee80211_do_slow_print(ic, &did_print))
			return 0;
		printf("%s: tsft offset %s%llu\n", ic->ic_if.if_xname,
		    (sign < 0) ? "-" : "",
		    (sign < 0)
			? (local_tsft - beacon_tsft)
			: (beacon_tsft - local_tsft));
		return 0;
	}

	if (sign < 0)
		return 0;

	if (ieee80211_match_bss(ic, ni, 0) != 0)
		return 0;

	if (ieee80211_do_slow_print(ic, &did_print)) {
		printf("%s: ieee80211_ibss_merge: bssid mismatch %s\n",
		    ic->ic_if.if_xname, ether_sprintf(ni->ni_bssid));
		printf("%s: my tsft %llu beacon tsft %llu\n",
		    ic->ic_if.if_xname, local_tsft, beacon_tsft);
		printf("%s: sync TSF with %s\n",
		    ic->ic_if.if_xname, ether_sprintf(ni->ni_macaddr));
	}

	ic->ic_flags &= ~IEEE80211_F_SIBSS;

	/* negotiate rates with new IBSS */
	ieee80211_fix_rate(ic, ni, IEEE80211_F_DOFRATE |
	    IEEE80211_F_DONEGO | IEEE80211_F_DODEL);
	if (ni->ni_rates.rs_nrates == 0) {
		if (ieee80211_do_slow_print(ic, &did_print)) {
			printf("%s: rates mismatch, BSSID %s\n",
			    ic->ic_if.if_xname, ether_sprintf(ni->ni_bssid));
		}
		return 0;
	}

	if (ieee80211_do_slow_print(ic, &did_print)) {
		printf("%s: sync BSSID %s -> ",
		    ic->ic_if.if_xname, ether_sprintf(ic->ic_bss->ni_bssid));
		printf("%s ", ether_sprintf(ni->ni_bssid));
		printf("(from %s)\n", ether_sprintf(ni->ni_macaddr));
	}

	ieee80211_node_newstate(ni, IEEE80211_STA_BSS);
	(*ic->ic_node_copy)(ic, ic->ic_bss, ni);

	return ENETRESET;
}

void
ieee80211_set_tim(struct ieee80211com *ic, int aid, int set)
{
	if (set)
		setbit(ic->ic_tim_bitmap, aid & ~0xc000);
	else
		clrbit(ic->ic_tim_bitmap, aid & ~0xc000);
}

/*
 * This function shall be called by drivers immediately after every DTIM.
 * Transmit all group addressed MSDUs buffered at the AP.
 */
void
ieee80211_notify_dtim(struct ieee80211com *ic)
{
	/* NB: group addressed MSDUs are buffered in ic_bss */
	struct ieee80211_node *ni = ic->ic_bss;
	struct ifnet *ifp = &ic->ic_if;
	struct ieee80211_frame *wh;
	struct mbuf *m;

	KASSERT(ic->ic_opmode == IEEE80211_M_HOSTAP);

	while ((m = mq_dequeue(&ni->ni_savedq)) != NULL) {
		if (!mq_empty(&ni->ni_savedq)) {
			/* more queued frames, set the more data bit */
			wh = mtod(m, struct ieee80211_frame *);
			wh->i_fc[1] |= IEEE80211_FC1_MORE_DATA;
		}
		mq_enqueue(&ic->ic_pwrsaveq, m);
		if_start(ifp);
	}
	/* XXX assumes everything has been sent */
	ic->ic_tim_mcast_pending = 0;
}
#endif	/* IEEE80211_STA_ONLY */

/*
 * Compare nodes in the tree by lladdr
 */
int
ieee80211_node_cmp(const struct ieee80211_node *b1,
    const struct ieee80211_node *b2)
{
	return (memcmp(b1->ni_macaddr, b2->ni_macaddr, IEEE80211_ADDR_LEN));
}

/*
 * Compare nodes in the tree by essid
 */
int
ieee80211_ess_cmp(const struct ieee80211_ess_rbt *b1,
    const struct ieee80211_ess_rbt *b2)
{
	return (memcmp(b1->essid, b2->essid, IEEE80211_NWID_LEN));
}

/*
 * Generate red-black tree function logic
 */
RBT_GENERATE(ieee80211_tree, ieee80211_node, ni_node, ieee80211_node_cmp);
RBT_GENERATE(ieee80211_ess_tree, ieee80211_ess_rbt, ess_rbt, ieee80211_ess_cmp);
