/*	$OpenBSD: ieee80211_proto.c,v 1.110 2025/08/01 20:39:26 stsp Exp $	*/
/*	$NetBSD: ieee80211_proto.c,v 1.8 2004/04/30 23:58:20 dyoung Exp $	*/

/*-
 * Copyright (c) 2001 Atsushi Onoe
 * Copyright (c) 2002, 2003 Sam Leffler, Errno Consulting
 * Copyright (c) 2008, 2009 Damien Bergamini
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

/*
 * IEEE 802.11 protocol support.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/endian.h>
#include <sys/errno.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_llc.h>
#include <net/route.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_priv.h>

const char * const ieee80211_mgt_subtype_name[] = {
	"assoc_req",	"assoc_resp",	"reassoc_req",	"reassoc_resp",
	"probe_req",	"probe_resp",	"reserved#6",	"reserved#7",
	"beacon",	"atim",		"disassoc",	"auth",
	"deauth",	"action",	"action_noack",	"reserved#15"
};
const char * const ieee80211_state_name[IEEE80211_S_MAX] = {
	"INIT",		/* IEEE80211_S_INIT */
	"SCAN",		/* IEEE80211_S_SCAN */
	"AUTH",		/* IEEE80211_S_AUTH */
	"ASSOC",	/* IEEE80211_S_ASSOC */
	"RUN"		/* IEEE80211_S_RUN */
};
const char * const ieee80211_phymode_name[] = {
	"auto",		/* IEEE80211_MODE_AUTO */
	"11a",		/* IEEE80211_MODE_11A */
	"11b",		/* IEEE80211_MODE_11B */
	"11g",		/* IEEE80211_MODE_11G */
	"11n",		/* IEEE80211_MODE_11N */
	"11ac",		/* IEEE80211_MODE_11AC */
};

void ieee80211_set_beacon_miss_threshold(struct ieee80211com *);
int ieee80211_newstate(struct ieee80211com *, enum ieee80211_state, int);

void
ieee80211_proto_attach(struct ifnet *ifp)
{
	struct ieee80211com *ic = (void *)ifp;

	mq_init(&ic->ic_mgtq, IFQ_MAXLEN, IPL_NET);
	mq_init(&ic->ic_pwrsaveq, IFQ_MAXLEN, IPL_NET);

	ifp->if_hdrlen = sizeof(struct ieee80211_frame);

	ic->ic_rtsthreshold = IEEE80211_RTS_MAX;
	ic->ic_fragthreshold = 2346;		/* XXX not used yet */
	ic->ic_fixed_rate = -1;			/* no fixed rate */
	ic->ic_fixed_mcs = -1;			/* no fixed mcs */
	ic->ic_protmode = IEEE80211_PROT_CTSONLY;

	/* protocol state change handler */
	ic->ic_newstate = ieee80211_newstate;

	/* initialize management frame handlers */
	ic->ic_recv_mgmt = ieee80211_recv_mgmt;
	ic->ic_send_mgmt = ieee80211_send_mgmt;
}

void
ieee80211_proto_detach(struct ifnet *ifp)
{
	struct ieee80211com *ic = (void *)ifp;

	mq_purge(&ic->ic_mgtq);
	mq_purge(&ic->ic_pwrsaveq);
}

void
ieee80211_print_essid(const u_int8_t *essid, int len)
{
	int i;
	const u_int8_t *p;

	if (len > IEEE80211_NWID_LEN)
		len = IEEE80211_NWID_LEN;
	/* determine printable or not */
	for (i = 0, p = essid; i < len; i++, p++) {
		if (*p < ' ' || *p > 0x7e)
			break;
	}
	if (i == len) {
		printf("\"");
		for (i = 0, p = essid; i < len; i++, p++)
			printf("%c", *p);
		printf("\"");
	} else {
		printf("0x");
		for (i = 0, p = essid; i < len; i++, p++)
			printf("%02x", *p);
	}
}

#ifdef IEEE80211_DEBUG
void
ieee80211_dump_pkt(const u_int8_t *buf, int len, int rate, int rssi)
{
	struct ieee80211_frame *wh;
	int i;

	wh = (struct ieee80211_frame *)buf;
	switch (wh->i_fc[1] & IEEE80211_FC1_DIR_MASK) {
	case IEEE80211_FC1_DIR_NODS:
		printf("NODS %s", ether_sprintf(wh->i_addr2));
		printf("->%s", ether_sprintf(wh->i_addr1));
		printf("(%s)", ether_sprintf(wh->i_addr3));
		break;
	case IEEE80211_FC1_DIR_TODS:
		printf("TODS %s", ether_sprintf(wh->i_addr2));
		printf("->%s", ether_sprintf(wh->i_addr3));
		printf("(%s)", ether_sprintf(wh->i_addr1));
		break;
	case IEEE80211_FC1_DIR_FROMDS:
		printf("FRDS %s", ether_sprintf(wh->i_addr3));
		printf("->%s", ether_sprintf(wh->i_addr1));
		printf("(%s)", ether_sprintf(wh->i_addr2));
		break;
	case IEEE80211_FC1_DIR_DSTODS:
		printf("DSDS %s", ether_sprintf((u_int8_t *)&wh[1]));
		printf("->%s", ether_sprintf(wh->i_addr3));
		printf("(%s", ether_sprintf(wh->i_addr2));
		printf("->%s)", ether_sprintf(wh->i_addr1));
		break;
	}
	switch (wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) {
	case IEEE80211_FC0_TYPE_DATA:
		printf(" data");
		break;
	case IEEE80211_FC0_TYPE_MGT:
		printf(" %s", ieee80211_mgt_subtype_name[
		    (wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK)
		    >> IEEE80211_FC0_SUBTYPE_SHIFT]);
		break;
	default:
		printf(" type#%d", wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK);
		break;
	}
	if (wh->i_fc[1] & IEEE80211_FC1_WEP)
		printf(" WEP");
	if (rate >= 0)
		printf(" %d%sM", rate / 2, (rate & 1) ? ".5" : "");
	if (rssi >= 0)
		printf(" +%d", rssi);
	printf("\n");
	if (len > 0) {
		for (i = 0; i < len; i++) {
			if ((i & 1) == 0)
				printf(" ");
			printf("%02x", buf[i]);
		}
		printf("\n");
	}
}
#endif

int
ieee80211_fix_rate(struct ieee80211com *ic, struct ieee80211_node *ni,
    int flags)
{
#define	RV(v)	((v) & IEEE80211_RATE_VAL)
	int i, j, ignore, error;
	int okrate, badrate, fixedrate;
	const struct ieee80211_rateset *srs;
	struct ieee80211_rateset *nrs;
	u_int8_t r;

	/*
	 * If the fixed rate check was requested but no fixed rate has been
	 * defined then just remove the check.
	 */
	if ((flags & IEEE80211_F_DOFRATE) && ic->ic_fixed_rate == -1)
		flags &= ~IEEE80211_F_DOFRATE;

	error = 0;
	okrate = badrate = fixedrate = 0;
	srs = &ic->ic_sup_rates[ieee80211_node_abg_mode(ic, ni)];
	nrs = &ni->ni_rates;
	for (i = 0; i < nrs->rs_nrates; ) {
		ignore = 0;
		if (flags & IEEE80211_F_DOSORT) {
			/*
			 * Sort rates.
			 */
			for (j = i + 1; j < nrs->rs_nrates; j++) {
				if (RV(nrs->rs_rates[i]) >
				    RV(nrs->rs_rates[j])) {
					r = nrs->rs_rates[i];
					nrs->rs_rates[i] = nrs->rs_rates[j];
					nrs->rs_rates[j] = r;
				}
			}
		}
		r = nrs->rs_rates[i] & IEEE80211_RATE_VAL;
		badrate = r;
		if (flags & IEEE80211_F_DOFRATE) {
			/*
			 * Check fixed rate is included.
			 */
			if (r == RV(srs->rs_rates[ic->ic_fixed_rate]))
				fixedrate = r;
		}
		if (flags & IEEE80211_F_DONEGO) {
			/*
			 * Check against supported rates.
			 */
			for (j = 0; j < srs->rs_nrates; j++) {
				if (r == RV(srs->rs_rates[j])) {
					/*
					 * Overwrite with the supported rate
					 * value so any basic rate bit is set.
					 * This insures that response we send
					 * to stations have the necessary basic
					 * rate bit set.
					 */
					nrs->rs_rates[i] = srs->rs_rates[j];
					break;
				}
			}
			if (j == srs->rs_nrates) {
				/*
				 * A rate in the node's rate set is not
				 * supported.  If this is a basic rate and we
				 * are operating as an AP then this is an error.
				 * Otherwise we just discard/ignore the rate.
				 * Note that this is important for 11b stations
				 * when they want to associate with an 11g AP.
				 */
#ifndef IEEE80211_STA_ONLY
				if (ic->ic_opmode == IEEE80211_M_HOSTAP &&
				    (nrs->rs_rates[i] & IEEE80211_RATE_BASIC))
					error++;
#endif
				ignore++;
			}
		}
		if (flags & IEEE80211_F_DODEL) {
			/*
			 * Delete unacceptable rates.
			 */
			if (ignore) {
				nrs->rs_nrates--;
				for (j = i; j < nrs->rs_nrates; j++)
					nrs->rs_rates[j] = nrs->rs_rates[j + 1];
				nrs->rs_rates[j] = 0;
				continue;
			}
		}
		if (!ignore)
			okrate = nrs->rs_rates[i];
		i++;
	}
	if (okrate == 0 || error != 0 ||
	    ((flags & IEEE80211_F_DOFRATE) && fixedrate == 0))
		return badrate | IEEE80211_RATE_BASIC;
	else
		return RV(okrate);
#undef RV
}

/*
 * Reset 11g-related state.
 */
void
ieee80211_reset_erp(struct ieee80211com *ic)
{
	ic->ic_flags &= ~IEEE80211_F_USEPROT;

	ieee80211_set_shortslottime(ic,
	    ic->ic_curmode == IEEE80211_MODE_11A ||
	    (ic->ic_curmode == IEEE80211_MODE_11N &&
	    IEEE80211_IS_CHAN_5GHZ(ic->ic_ibss_chan))
#ifndef IEEE80211_STA_ONLY
	    ||
	    ((ic->ic_curmode == IEEE80211_MODE_11G ||
	    (ic->ic_curmode == IEEE80211_MODE_11N &&
	    IEEE80211_IS_CHAN_2GHZ(ic->ic_ibss_chan))) &&
	     ic->ic_opmode == IEEE80211_M_HOSTAP &&
	     (ic->ic_caps & IEEE80211_C_SHSLOT))
#endif
	);

	if (ic->ic_curmode == IEEE80211_MODE_11A ||
	    (ic->ic_curmode == IEEE80211_MODE_11N &&
	    IEEE80211_IS_CHAN_5GHZ(ic->ic_ibss_chan)) ||
	    (ic->ic_caps & IEEE80211_C_SHPREAMBLE))
		ic->ic_flags |= IEEE80211_F_SHPREAMBLE;
	else
		ic->ic_flags &= ~IEEE80211_F_SHPREAMBLE;
}

/*
 * Set the short slot time state and notify the driver.
 */
void
ieee80211_set_shortslottime(struct ieee80211com *ic, int on)
{
	if (on)
		ic->ic_flags |= IEEE80211_F_SHSLOT;
	else
		ic->ic_flags &= ~IEEE80211_F_SHSLOT;

	/* notify the driver */
	if (ic->ic_updateslot != NULL)
		ic->ic_updateslot(ic);
}

/*
 * This function is called by the 802.1X PACP machine (via an ioctl) when
 * the transmit key machine (4-Way Handshake for 802.11) should run.
 */
int
ieee80211_keyrun(struct ieee80211com *ic, u_int8_t *macaddr)
{
	struct ieee80211_node *ni = ic->ic_bss;
#ifndef IEEE80211_STA_ONLY
	struct ieee80211_pmk *pmk;
#endif

	/* STA must be associated or AP must be ready */
	if (ic->ic_state != IEEE80211_S_RUN ||
	    !(ic->ic_flags & IEEE80211_F_RSNON))
		return ENETDOWN;

	ni->ni_rsn_supp_state = RSNA_SUPP_PTKSTART;
#ifndef IEEE80211_STA_ONLY
	if (ic->ic_opmode == IEEE80211_M_STA)
#endif
		return 0;	/* supplicant only, do nothing */

#ifndef IEEE80211_STA_ONLY
	/* find the STA with which we must start the key exchange */
	if ((ni = ieee80211_find_node(ic, macaddr)) == NULL) {
		DPRINTF(("no node found for %s\n", ether_sprintf(macaddr)));
		return EINVAL;
	}
	/* check that the STA is in the correct state */
	if (ni->ni_state != IEEE80211_STA_ASSOC ||
	    ni->ni_rsn_state != RSNA_AUTHENTICATION_2) {
		DPRINTF(("unexpected in state %d\n", ni->ni_rsn_state));
		return EINVAL;
	}
	ni->ni_rsn_state = RSNA_INITPMK;

	/* make sure a PMK is available for this STA, otherwise deauth it */
	if ((pmk = ieee80211_pmksa_find(ic, ni, NULL)) == NULL) {
		DPRINTF(("no PMK available for %s\n", ether_sprintf(macaddr)));
		IEEE80211_SEND_MGMT(ic, ni, IEEE80211_FC0_SUBTYPE_DEAUTH,
		    IEEE80211_REASON_AUTH_LEAVE);
		ieee80211_node_leave(ic, ni);
		return EINVAL;
	}
	memcpy(ni->ni_pmk, pmk->pmk_key, IEEE80211_PMK_LEN);
	memcpy(ni->ni_pmkid, pmk->pmk_pmkid, IEEE80211_PMKID_LEN);
	ni->ni_flags |= IEEE80211_NODE_PMK;

	/* initiate key exchange (4-Way Handshake) with STA */
	return ieee80211_send_4way_msg1(ic, ni);
#endif	/* IEEE80211_STA_ONLY */
}

#ifndef IEEE80211_STA_ONLY
/*
 * Initiate a group key handshake with a node.
 */
static void
ieee80211_node_gtk_rekey(void *arg, struct ieee80211_node *ni)
{
	struct ieee80211com *ic = arg;

	if (ni->ni_state != IEEE80211_STA_ASSOC ||
	    ni->ni_rsn_gstate != RSNA_IDLE)
		return;

	/* initiate a group key handshake with STA */
	ni->ni_flags |= IEEE80211_NODE_REKEY;
	if (ieee80211_send_group_msg1(ic, ni) != 0)
		ni->ni_flags &= ~IEEE80211_NODE_REKEY;
}

/*
 * This function is called in HostAP mode when the group key needs to be
 * changed.
 */
void
ieee80211_setkeys(struct ieee80211com *ic)
{
	struct ieee80211_key *k;
	u_int8_t kid;
	int rekeysta = 0;

	/* Swap(GM, GN) */
	kid = (ic->ic_def_txkey == 1) ? 2 : 1;
	k = &ic->ic_nw_keys[kid];
	memset(k, 0, sizeof(*k));
	k->k_id = kid;
	k->k_cipher = ic->ic_bss->ni_rsngroupcipher;
	k->k_flags = IEEE80211_KEY_GROUP | IEEE80211_KEY_TX;
	k->k_len = ieee80211_cipher_keylen(k->k_cipher);
	arc4random_buf(k->k_key, k->k_len);

	if (ic->ic_caps & IEEE80211_C_MFP) {
		/* Swap(GM_igtk, GN_igtk) */
		kid = (ic->ic_igtk_kid == 4) ? 5 : 4;
		k = &ic->ic_nw_keys[kid];
		memset(k, 0, sizeof(*k));
		k->k_id = kid;
		k->k_cipher = ic->ic_bss->ni_rsngroupmgmtcipher;
		k->k_flags = IEEE80211_KEY_IGTK | IEEE80211_KEY_TX;
		k->k_len = 16;
		arc4random_buf(k->k_key, k->k_len);
	}

	ieee80211_iterate_nodes(ic, ieee80211_node_gtk_rekey, ic);
	ieee80211_iterate_nodes(ic, ieee80211_count_rekeysta, &rekeysta);
	if (rekeysta == 0)
		ieee80211_setkeysdone(ic);
}

/*
 * The group key handshake has been completed with all associated stations.
 */
void
ieee80211_setkeysdone(struct ieee80211com *ic)
{
	u_int8_t kid;

	/* install GTK */
	kid = (ic->ic_def_txkey == 1) ? 2 : 1;
	switch ((*ic->ic_set_key)(ic, ic->ic_bss, &ic->ic_nw_keys[kid])) {
	case 0:
	case EBUSY:
		ic->ic_def_txkey = kid;
		break;
	default:
		break;
	}

	if (ic->ic_caps & IEEE80211_C_MFP) {
		/* install IGTK */
		kid = (ic->ic_igtk_kid == 4) ? 5 : 4;
		switch ((*ic->ic_set_key)(ic, ic->ic_bss, &ic->ic_nw_keys[kid])) {
		case 0:
		case EBUSY:
			ic->ic_igtk_kid = kid;
			break;
		default:
			break;
		}
	}
}

/*
 * Group key lifetime has expired, update it.
 */
void
ieee80211_gtk_rekey_timeout(void *arg)
{
	struct ieee80211com *ic = arg;
	int s;

	s = splnet();
	ieee80211_setkeys(ic);
	splx(s);

	/* re-schedule a GTK rekeying after 3600s */
	timeout_add_sec(&ic->ic_rsn_timeout, 3600);
}

void
ieee80211_sa_query_timeout(void *arg)
{
	struct ieee80211_node *ni = arg;
	struct ieee80211com *ic = ni->ni_ic;
	int s;

	s = splnet();
	if (++ni->ni_sa_query_count >= 3) {
		ni->ni_flags &= ~IEEE80211_NODE_SA_QUERY;
		ni->ni_flags |= IEEE80211_NODE_SA_QUERY_FAILED;
	} else	/* retry SA Query Request */
		ieee80211_sa_query_request(ic, ni);
	splx(s);
}

/*
 * Request that a SA Query Request frame be sent to a specified peer STA
 * to which the STA is associated.
 */
void
ieee80211_sa_query_request(struct ieee80211com *ic, struct ieee80211_node *ni)
{
	/* MLME-SAQuery.request */

	if (!(ni->ni_flags & IEEE80211_NODE_SA_QUERY)) {
		ni->ni_flags |= IEEE80211_NODE_SA_QUERY;
		ni->ni_flags &= ~IEEE80211_NODE_SA_QUERY_FAILED;
		ni->ni_sa_query_count = 0;
	}
	/* generate new Transaction Identifier */
	ni->ni_sa_query_trid++;

	/* send SA Query Request */
	IEEE80211_SEND_ACTION(ic, ni, IEEE80211_CATEG_SA_QUERY,
	    IEEE80211_ACTION_SA_QUERY_REQ, 0);
	timeout_add_msec(&ni->ni_sa_query_to, 10);
}
#endif	/* IEEE80211_STA_ONLY */

void
ieee80211_ht_negotiate(struct ieee80211com *ic, struct ieee80211_node *ni)
{
	int i;

	ni->ni_flags &= ~(IEEE80211_NODE_HT | IEEE80211_NODE_HT_SGI20 |
	    IEEE80211_NODE_HT_SGI40);

	/* Check if we support HT. */
	if ((ic->ic_modecaps & (1 << IEEE80211_MODE_11N)) == 0)
		return;

	/* Check if HT support has been explicitly disabled. */
	if ((ic->ic_flags & IEEE80211_F_HTON) == 0)
		return;

	/*
	 * Check if the peer supports HT.
	 * Require at least one of the mandatory MCS.
	 * MCS 0-7 are mandatory but some APs have particular MCS disabled.
	 */
	if (!ieee80211_node_supports_ht(ni)) {
		ic->ic_stats.is_ht_nego_no_mandatory_mcs++;
		return;
	}

	if (ic->ic_opmode == IEEE80211_M_STA) {
		/* We must support the AP's basic MCS set. */
		for (i = 0; i < IEEE80211_HT_NUM_MCS; i++) {
			if (isset(ni->ni_basic_mcs, i) &&
			    !isset(ic->ic_sup_mcs, i)) {
				ic->ic_stats.is_ht_nego_no_basic_mcs++;
				return;
			}
		}
	}

	/*
	 * Don't allow group cipher (includes WEP) or TKIP
	 * for pairwise encryption (see 802.11-2012 11.1.6).
	 */
	if (ic->ic_flags & IEEE80211_F_WEPON) {
		ic->ic_stats.is_ht_nego_bad_crypto++;
		return;
	}
	if ((ic->ic_flags & IEEE80211_F_RSNON) &&
	    (ni->ni_rsnciphers & IEEE80211_CIPHER_USEGROUP ||
	    ni->ni_rsnciphers & IEEE80211_CIPHER_TKIP)) {
		ic->ic_stats.is_ht_nego_bad_crypto++;
		return;
	}

	ni->ni_flags |= IEEE80211_NODE_HT;

	if (ieee80211_node_supports_ht_sgi20(ni) &&
	    (ic->ic_htcaps & IEEE80211_HTCAP_SGI20))
		ni->ni_flags |= IEEE80211_NODE_HT_SGI20;
	if (ieee80211_node_supports_ht_sgi40(ni) &&
	    (ic->ic_htcaps & IEEE80211_HTCAP_SGI40))
		ni->ni_flags |= IEEE80211_NODE_HT_SGI40;
}

void
ieee80211_vht_negotiate(struct ieee80211com *ic, struct ieee80211_node *ni)
{
	int n;

	ni->ni_flags &= ~(IEEE80211_NODE_VHT | IEEE80211_NODE_VHT_SGI80 |
	    IEEE80211_NODE_VHT_SGI160);

	/* Check if we support VHT. */
	if ((ic->ic_modecaps & (1 << IEEE80211_MODE_11AC)) == 0)
		return;

	/* Check if VHT support has been explicitly disabled. */
	if ((ic->ic_flags & IEEE80211_F_VHTON) == 0)
		return;

	/*
	 * Check if the peer supports VHT.
	 * MCS 0-7 for a single spatial stream are mandatory.
	 */
	if (!ieee80211_node_supports_vht(ni)) {
		ic->ic_stats.is_vht_nego_no_mandatory_mcs++;
		return;
	}

	if (ic->ic_opmode == IEEE80211_M_STA) {
		/* We must support the AP's basic MCS set. */
		for (n = 1; n <= IEEE80211_VHT_NUM_SS; n++) {
			uint16_t basic_mcs = (ni->ni_vht_basic_mcs &
			    IEEE80211_VHT_MCS_FOR_SS_MASK(n)) >>
			    IEEE80211_VHT_MCS_FOR_SS_SHIFT(n);
			uint16_t rx_mcs = (ic->ic_vht_rxmcs &
			    IEEE80211_VHT_MCS_FOR_SS_MASK(n)) >>
			    IEEE80211_VHT_MCS_FOR_SS_SHIFT(n);
			if (basic_mcs != IEEE80211_VHT_MCS_SS_NOT_SUPP &&
			    basic_mcs > rx_mcs) {
				ic->ic_stats.is_vht_nego_no_basic_mcs++;
				return;
			}
		}
	}

	ni->ni_flags |= IEEE80211_NODE_VHT;

	if ((ni->ni_vhtcaps & IEEE80211_VHTCAP_SGI80) &&
	    (ic->ic_vhtcaps & IEEE80211_VHTCAP_SGI80))
		ni->ni_flags |= IEEE80211_NODE_VHT_SGI80;
	if ((ni->ni_vhtcaps & IEEE80211_VHTCAP_SGI160) &&
	    (ic->ic_vhtcaps & IEEE80211_VHTCAP_SGI160))
		ni->ni_flags |= IEEE80211_NODE_VHT_SGI160;
}

void
ieee80211_tx_ba_timeout(void *arg)
{
	struct ieee80211_tx_ba *ba = arg;
	struct ieee80211_node *ni = ba->ba_ni;
	struct ieee80211com *ic = ni->ni_ic;
	u_int8_t tid;
	int s;

	s = splnet();
	tid = ((caddr_t)ba - (caddr_t)ni->ni_tx_ba) / sizeof(*ba);
	if (ba->ba_state == IEEE80211_BA_REQUESTED) {
		/* MLME-ADDBA.confirm(TIMEOUT) */
		ba->ba_state = IEEE80211_BA_INIT;
		if (ni->ni_addba_req_intval[tid] <
		    IEEE80211_ADDBA_REQ_INTVAL_MAX)
			ni->ni_addba_req_intval[tid]++;
		/*
		 * In case the peer believes there is an existing
		 * block ack agreement with us, try to delete it.
		 */
		IEEE80211_SEND_ACTION(ic, ni, IEEE80211_CATEG_BA,
		    IEEE80211_ACTION_DELBA,
		    IEEE80211_REASON_SETUP_REQUIRED << 16 | 1 << 8 | tid);
	} else if (ba->ba_state == IEEE80211_BA_AGREED) {
		/* Block Ack inactivity timeout */
		ic->ic_stats.is_ht_tx_ba_timeout++;
		ieee80211_delba_request(ic, ni, IEEE80211_REASON_TIMEOUT,
		    1, tid);
	}
	splx(s);
}

void
ieee80211_rx_ba_timeout(void *arg)
{
	struct ieee80211_rx_ba *ba = arg;
	struct ieee80211_node *ni = ba->ba_ni;
	struct ieee80211com *ic = ni->ni_ic;
	u_int8_t tid;
	int s;

	ic->ic_stats.is_ht_rx_ba_timeout++;

	s = splnet();

	/* Block Ack inactivity timeout */
	tid = ((caddr_t)ba - (caddr_t)ni->ni_rx_ba) / sizeof(*ba);
	ieee80211_delba_request(ic, ni, IEEE80211_REASON_TIMEOUT, 0, tid);

	splx(s);
}

/*
 * Request initiation of Block Ack with the specified peer.
 */
int
ieee80211_addba_request(struct ieee80211com *ic, struct ieee80211_node *ni,
    u_int16_t ssn, u_int8_t tid)
{
	struct ieee80211_tx_ba *ba = &ni->ni_tx_ba[tid];

	if (ba->ba_state != IEEE80211_BA_INIT)
		return EBUSY;

	/* MLME-ADDBA.request */

	/* setup Block Ack */
	ba->ba_ni = ni;
	ba->ba_state = IEEE80211_BA_REQUESTED;
	ba->ba_token = ic->ic_dialog_token++;
	ba->ba_timeout_val = 0;
	timeout_set(&ba->ba_to, ieee80211_tx_ba_timeout, ba);
	ba->ba_winsize = IEEE80211_BA_MAX_WINSZ;
	ba->ba_winstart = ssn;
	ba->ba_winend = (ba->ba_winstart + ba->ba_winsize - 1) & 0xfff;
	ba->ba_params =
	    (ba->ba_winsize << IEEE80211_ADDBA_BUFSZ_SHIFT) |
	    (tid << IEEE80211_ADDBA_TID_SHIFT);
	ba->ba_params |= IEEE80211_ADDBA_AMSDU;
	if ((ic->ic_htcaps & IEEE80211_HTCAP_DELAYEDBA) == 0)
		/* immediate BA */
		ba->ba_params |= IEEE80211_ADDBA_BA_POLICY;

	if ((ic->ic_caps & IEEE80211_C_ADDBA_OFFLOAD) &&
	    ic->ic_ampdu_tx_start != NULL) {
		int err = ic->ic_ampdu_tx_start(ic, ni, tid);
		if (err && err != EBUSY) {
			/* driver failed to setup, rollback */
			ieee80211_addba_resp_refuse(ic, ni, tid,
			    IEEE80211_STATUS_UNSPECIFIED);
		} else if (err == 0)
			ieee80211_addba_resp_accept(ic, ni, tid);
		return err; /* The device will send an ADDBA frame. */
	}

	timeout_add_sec(&ba->ba_to, 1);	/* dot11ADDBAResponseTimeout */
	IEEE80211_SEND_ACTION(ic, ni, IEEE80211_CATEG_BA,
	    IEEE80211_ACTION_ADDBA_REQ, tid);
	return 0;
}

/*
 * Request the deletion of Block Ack with a peer and notify driver.
 */
void
ieee80211_delba_request(struct ieee80211com *ic, struct ieee80211_node *ni,
    u_int16_t reason, u_int8_t dir, u_int8_t tid)
{
	/* MLME-DELBA.request */

	if (reason) {
		/* transmit a DELBA frame */
		IEEE80211_SEND_ACTION(ic, ni, IEEE80211_CATEG_BA,
		    IEEE80211_ACTION_DELBA, reason << 16 | dir << 8 | tid);
	}
	if (dir) {
		/* MLME-DELBA.confirm(Originator) */
		if (ic->ic_ampdu_tx_stop != NULL)
			ic->ic_ampdu_tx_stop(ic, ni, tid);
		ieee80211_node_tx_ba_clear(ni, tid);
	} else {
		/* MLME-DELBA.confirm(Recipient) */
		struct ieee80211_rx_ba *ba = &ni->ni_rx_ba[tid];
		int i;

		if (ic->ic_ampdu_rx_stop != NULL)
			ic->ic_ampdu_rx_stop(ic, ni, tid);

		ba->ba_state = IEEE80211_BA_INIT;
		/* stop Block Ack inactivity timer */
		timeout_del(&ba->ba_to);
		timeout_del(&ba->ba_gap_to);

		if (ba->ba_buf != NULL) {
			/* free all MSDUs stored in reordering buffer */
			for (i = 0; i < IEEE80211_BA_MAX_WINSZ; i++)
				m_freem(ba->ba_buf[i].m);
			/* free reordering buffer */
			free(ba->ba_buf, M_DEVBUF,
			    IEEE80211_BA_MAX_WINSZ * sizeof(*ba->ba_buf));
			ba->ba_buf = NULL;
		}
	}
}

#ifndef IEEE80211_STA_ONLY
void
ieee80211_auth_open_confirm(struct ieee80211com *ic,
    struct ieee80211_node *ni, uint16_t seq)
{
	struct ifnet *ifp = &ic->ic_if;

	IEEE80211_SEND_MGMT(ic, ni, IEEE80211_FC0_SUBTYPE_AUTH, seq + 1);
	if (ifp->if_flags & IFF_DEBUG)
		printf("%s: station %s %s authenticated (open)\n",
		    ifp->if_xname,
		    ether_sprintf((u_int8_t *)ni->ni_macaddr),
		    ni->ni_state != IEEE80211_STA_CACHE ?
		    "newly" : "already");
	ieee80211_node_newstate(ni, IEEE80211_STA_AUTH);
}
#endif

void
ieee80211_try_another_bss(struct ieee80211com *ic)
{
	struct ieee80211_node *curbs, *selbs;
	struct ifnet *ifp = &ic->ic_if;

	/* Don't select our current AP again. */
	curbs = ieee80211_find_node(ic, ic->ic_bss->ni_macaddr);
	if (curbs) {
		curbs->ni_fails++;
		ieee80211_node_newstate(curbs, IEEE80211_STA_CACHE);
	}

	/* Try a different AP from the same ESS if available. */
	if (ic->ic_caps & IEEE80211_C_SCANALLBAND) {
		/*
		 * Make sure we will consider APs on all bands during
		 * access point selection in ieee80211_node_choose_bss().
		 * During multi-band scans, our previous AP may be trying
		 * to steer us onto another band by denying authentication.
		 */
		ieee80211_setmode(ic, IEEE80211_MODE_AUTO);
	}
	selbs = ieee80211_node_choose_bss(ic, 0, NULL);
	if (selbs == NULL)
		return;

	/* Should not happen but seriously, don't try the same AP again. */
	if (memcmp(selbs->ni_macaddr, ic->ic_bss->ni_macaddr,
	    IEEE80211_ADDR_LEN) == 0)
		return;

	if (ifp->if_flags & IFF_DEBUG)
		printf("%s: trying AP %s on channel %d instead\n",
		    ifp->if_xname, ether_sprintf(selbs->ni_macaddr),
		    ieee80211_chan2ieee(ic, selbs->ni_chan));

	/* Triggers an AUTH->AUTH transition, avoiding another SCAN. */
	ieee80211_node_join_bss(ic, selbs);
}

void
ieee80211_auth_open(struct ieee80211com *ic, const struct ieee80211_frame *wh,
    struct ieee80211_node *ni, struct ieee80211_rxinfo *rxi, u_int16_t seq,
    u_int16_t status)
{
	struct ifnet *ifp = &ic->ic_if;
	switch (ic->ic_opmode) {
#ifndef IEEE80211_STA_ONLY
	case IEEE80211_M_IBSS:
		if (ic->ic_state != IEEE80211_S_RUN ||
		    seq != IEEE80211_AUTH_OPEN_REQUEST) {
			DPRINTF(("discard auth from %s; state %u, seq %u\n",
			    ether_sprintf((u_int8_t *)wh->i_addr2),
			    ic->ic_state, seq));
			ic->ic_stats.is_rx_bad_auth++;
			return;
		}
		ieee80211_new_state(ic, IEEE80211_S_AUTH,
		    wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK);

		/* In IBSS mode no (re)association frames are sent. */
		if (ic->ic_flags & IEEE80211_F_RSNON)
			ni->ni_rsn_supp_state = RSNA_SUPP_PTKSTART;
		break;

	case IEEE80211_M_AHDEMO:
		/* should not come here */
		break;

	case IEEE80211_M_HOSTAP:
		if (ic->ic_state != IEEE80211_S_RUN ||
		    seq != IEEE80211_AUTH_OPEN_REQUEST) {
			DPRINTF(("discard auth from %s; state %u, seq %u\n",
			    ether_sprintf((u_int8_t *)wh->i_addr2),
			    ic->ic_state, seq));
			ic->ic_stats.is_rx_bad_auth++;
			return;
		}
		if (ni == ic->ic_bss) {
			ni = ieee80211_find_node(ic, wh->i_addr2);
			if (ni == NULL)
				ni = ieee80211_alloc_node(ic, wh->i_addr2);
			if (ni == NULL) {
				return;
			}
			IEEE80211_ADDR_COPY(ni->ni_bssid, ic->ic_bss->ni_bssid);
			ni->ni_rssi = rxi->rxi_rssi;
			ni->ni_rstamp = rxi->rxi_tstamp;
			ni->ni_chan = ic->ic_bss->ni_chan;
		}

		/*
		 * Drivers may want to set up state before confirming.
		 * In which case this returns EBUSY and the driver will
		 * later call ieee80211_auth_open_confirm() by itself.
		 */
		if (ic->ic_newauth && ic->ic_newauth(ic, ni,
		    ni->ni_state != IEEE80211_STA_CACHE, seq) != 0)
			break;
		ieee80211_auth_open_confirm(ic, ni, seq);
		break;
#endif	/* IEEE80211_STA_ONLY */

	case IEEE80211_M_STA:
		if (ic->ic_state != IEEE80211_S_AUTH ||
		    seq != IEEE80211_AUTH_OPEN_RESPONSE) {
			ic->ic_stats.is_rx_bad_auth++;
			DPRINTF(("discard auth from %s; state %u, seq %u\n",
			    ether_sprintf((u_int8_t *)wh->i_addr2),
			    ic->ic_state, seq));
			return;
		}
		if (ic->ic_flags & IEEE80211_F_RSNON) {
			/* XXX not here! */
			ic->ic_bss->ni_flags &= ~IEEE80211_NODE_TXRXPROT;
			ic->ic_bss->ni_port_valid = 0;
			ic->ic_bss->ni_replaycnt_ok = 0;
			(*ic->ic_delete_key)(ic, ic->ic_bss,
			    &ic->ic_bss->ni_pairwise_key);
		}
		if (status != 0) {
			if (ifp->if_flags & IFF_DEBUG)
				printf("%s: open authentication failed "
				    "(status %d) for %s\n", ifp->if_xname,
				    status,
				    ether_sprintf((u_int8_t *)wh->i_addr3));
			if (ni != ic->ic_bss)
				ni->ni_fails++;
			else
				ieee80211_try_another_bss(ic);
			ic->ic_stats.is_rx_auth_fail++;
			return;
		}
		ieee80211_new_state(ic, IEEE80211_S_ASSOC,
		    wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK);
		break;
	default:
		break;
	}
}

void
ieee80211_set_beacon_miss_threshold(struct ieee80211com *ic)
{
	struct ifnet *ifp = &ic->ic_if;

	/*
	 * Scale the missed beacon counter threshold to the AP's actual
	 * beacon interval.
	 */
	int btimeout = MIN(IEEE80211_BEACON_MISS_THRES * ic->ic_bss->ni_intval,
	    IEEE80211_BEACON_MISS_THRES * (IEEE80211_DUR_TU / 10));
	/* Ensure that at least one beacon may be missed. */
	btimeout = MAX(btimeout, 2 * ic->ic_bss->ni_intval);
	if (ic->ic_bss->ni_intval > 0) /* don't crash if interval is bogus */
		ic->ic_bmissthres = btimeout / ic->ic_bss->ni_intval;

	if (ifp->if_flags & IFF_DEBUG)
		printf("%s: missed beacon threshold set to %d beacons, "
		    "beacon interval is %u TU\n", ifp->if_xname,
		    ic->ic_bmissthres, ic->ic_bss->ni_intval);
}

/* Tell our peer, and the driver, to stop A-MPDU Tx for all TIDs. */
void
ieee80211_stop_ampdu_tx(struct ieee80211com *ic, struct ieee80211_node *ni,
    int mgt)
{
	int tid;

	for (tid = 0; tid < nitems(ni->ni_tx_ba); tid++) {
		struct ieee80211_tx_ba *ba = &ni->ni_tx_ba[tid];
		if (ba->ba_state != IEEE80211_BA_AGREED)
			continue;

		if (ic->ic_caps & IEEE80211_C_ADDBA_OFFLOAD) {
			if (ic->ic_ampdu_tx_stop != NULL)
				ic->ic_ampdu_tx_stop(ic, ni, tid);
			continue; /* Don't change ba->ba_state! */
		}

		ieee80211_delba_request(ic, ni,
		    mgt == -1 ? 0 : IEEE80211_REASON_AUTH_LEAVE, 1, tid);
	}
}

void
ieee80211_check_wpa_supplicant_failure(struct ieee80211com *ic,
    struct ieee80211_node *ni)
{
	struct ieee80211_node *ni2;

	if (ic->ic_opmode != IEEE80211_M_STA
#ifndef IEEE80211_STA_ONLY
	    && ic->ic_opmode != IEEE80211_M_IBSS
#endif
	    )
		return;

	if (ni->ni_rsn_supp_state != RSNA_SUPP_PTKNEGOTIATING)
		return;

	ni->ni_assoc_fail |= IEEE80211_NODE_ASSOCFAIL_WPA_KEY;

	if (ni != ic->ic_bss)
		return;

	/* Also update the copy of our AP's node in the node cache. */
	ni2 = ieee80211_find_node(ic, ic->ic_bss->ni_macaddr);
	if (ni2)
		ni2->ni_assoc_fail |= ic->ic_bss->ni_assoc_fail;
}

int
ieee80211_newstate(struct ieee80211com *ic, enum ieee80211_state nstate,
    int mgt)
{
	struct ifnet *ifp = &ic->ic_if;
	struct ieee80211_node *ni;
	enum ieee80211_state ostate;
	u_int rate;
#ifndef IEEE80211_STA_ONLY
	int s;
#endif

	ostate = ic->ic_state;
	if (ifp->if_flags & IFF_DEBUG)
		printf("%s: %s -> %s\n", ifp->if_xname,
		    ieee80211_state_name[ostate], ieee80211_state_name[nstate]);
	ic->ic_state = nstate;			/* state transition */
	ni = ic->ic_bss;			/* NB: no reference held */
	ieee80211_set_link_state(ic, LINK_STATE_DOWN);
	ic->ic_xflags &= ~IEEE80211_F_TX_MGMT_ONLY;
	switch (nstate) {
	case IEEE80211_S_INIT:
		/*
		 * If mgt = -1, driver is already partway down, so do
		 * not send management frames.
		 */
		switch (ostate) {
		case IEEE80211_S_INIT:
			break;
		case IEEE80211_S_RUN:
			if (mgt == -1)
				goto justcleanup;
			ieee80211_stop_ampdu_tx(ic, ni, mgt);
			ieee80211_ba_del(ni);
			switch (ic->ic_opmode) {
			case IEEE80211_M_STA:
				IEEE80211_SEND_MGMT(ic, ni,
				    IEEE80211_FC0_SUBTYPE_DISASSOC,
				    IEEE80211_REASON_ASSOC_LEAVE);
				break;
#ifndef IEEE80211_STA_ONLY
			case IEEE80211_M_HOSTAP:
				s = splnet();
				RBT_FOREACH(ni, ieee80211_tree, &ic->ic_tree) {
					if (ni->ni_state != IEEE80211_STA_ASSOC)
						continue;
					IEEE80211_SEND_MGMT(ic, ni,
					    IEEE80211_FC0_SUBTYPE_DISASSOC,
					    IEEE80211_REASON_ASSOC_LEAVE);
				}
				splx(s);
				break;
#endif
			default:
				break;
			}
			/* FALLTHROUGH */
		case IEEE80211_S_ASSOC:
			if (mgt == -1)
				goto justcleanup;
			switch (ic->ic_opmode) {
			case IEEE80211_M_STA:
				IEEE80211_SEND_MGMT(ic, ni,
				    IEEE80211_FC0_SUBTYPE_DEAUTH,
				    IEEE80211_REASON_AUTH_LEAVE);
				break;
#ifndef IEEE80211_STA_ONLY
			case IEEE80211_M_HOSTAP:
				s = splnet();
				RBT_FOREACH(ni, ieee80211_tree, &ic->ic_tree) {
					IEEE80211_SEND_MGMT(ic, ni,
					    IEEE80211_FC0_SUBTYPE_DEAUTH,
					    IEEE80211_REASON_AUTH_LEAVE);
				}
				splx(s);
				break;
#endif
			default:
				break;
			}
			/* FALLTHROUGH */
		case IEEE80211_S_AUTH:
		case IEEE80211_S_SCAN:
justcleanup:
#ifndef IEEE80211_STA_ONLY
			if (ic->ic_opmode == IEEE80211_M_HOSTAP)
				timeout_del(&ic->ic_rsn_timeout);
#endif
			ieee80211_ba_del(ni);
			timeout_del(&ic->ic_bgscan_timeout);
			ic->ic_bgscan_fail = 0;
			ic->ic_mgt_timer = 0;
			mq_purge(&ic->ic_mgtq);
			mq_purge(&ic->ic_pwrsaveq);
			ieee80211_free_allnodes(ic, 1);
			break;
		}
		ni->ni_rsn_supp_state = RSNA_SUPP_INITIALIZE;
		ni->ni_assoc_fail = 0;
		if (ic->ic_flags & IEEE80211_F_RSNON)
			ieee80211_crypto_clear_groupkeys(ic);
		break;
	case IEEE80211_S_SCAN:
		ic->ic_flags &= ~IEEE80211_F_SIBSS;
		/* initialize bss for probe request */
		IEEE80211_ADDR_COPY(ni->ni_macaddr, etherbroadcastaddr);
		IEEE80211_ADDR_COPY(ni->ni_bssid, etherbroadcastaddr);
		ni->ni_rates = ic->ic_sup_rates[
		    ieee80211_node_abg_mode(ic, ni)];
		ni->ni_associd = 0;
		ni->ni_rstamp = 0;
		ni->ni_rsn_supp_state = RSNA_SUPP_INITIALIZE;
		if (ic->ic_flags & IEEE80211_F_RSNON)
			ieee80211_crypto_clear_groupkeys(ic);
		switch (ostate) {
		case IEEE80211_S_INIT:
#ifndef IEEE80211_STA_ONLY
			if (ic->ic_opmode == IEEE80211_M_HOSTAP &&
			    ic->ic_des_chan != IEEE80211_CHAN_ANYC) {
				/*
				 * AP operation and we already have a channel;
				 * bypass the scan and startup immediately.
				 */
				ieee80211_create_ibss(ic, ic->ic_des_chan);
			} else
#endif
				ieee80211_begin_scan(ifp);
			break;
		case IEEE80211_S_SCAN:
			/* scan next */
			if (ic->ic_flags & IEEE80211_F_ASCAN) {
				IEEE80211_SEND_MGMT(ic, ni,
				    IEEE80211_FC0_SUBTYPE_PROBE_REQ, 0);
			}
			break;
		case IEEE80211_S_RUN:
			/* beacon miss */
			if (ifp->if_flags & IFF_DEBUG) {
				/* XXX bssid clobbered above */
				printf("%s: no recent beacons from %s;"
				    " rescanning\n", ifp->if_xname,
				    ether_sprintf(ic->ic_bss->ni_bssid));
			}
			timeout_del(&ic->ic_bgscan_timeout);
			ic->ic_bgscan_fail = 0;
			ieee80211_stop_ampdu_tx(ic, ni, mgt);
			ieee80211_free_allnodes(ic, 1);
			/* FALLTHROUGH */
		case IEEE80211_S_AUTH:
		case IEEE80211_S_ASSOC:
			/* timeout restart scan */
			ni = ieee80211_find_node(ic, ic->ic_bss->ni_macaddr);
			if (ni != NULL)
				ni->ni_fails++;
			ieee80211_begin_scan(ifp);
			break;
		}
		break;
	case IEEE80211_S_AUTH:
		if (ostate == IEEE80211_S_RUN)
			ieee80211_check_wpa_supplicant_failure(ic, ni);
		ni->ni_rsn_supp_state = RSNA_SUPP_INITIALIZE;
		if (ic->ic_flags & IEEE80211_F_RSNON)
			ieee80211_crypto_clear_groupkeys(ic);
		switch (ostate) {
		case IEEE80211_S_INIT:
			if (ifp->if_flags & IFF_DEBUG)
				printf("%s: invalid transition %s -> %s\n",
				    ifp->if_xname, ieee80211_state_name[ostate],
				    ieee80211_state_name[nstate]);
			break;
		case IEEE80211_S_SCAN:
			IEEE80211_SEND_MGMT(ic, ni,
			    IEEE80211_FC0_SUBTYPE_AUTH, 1);
			break;
		case IEEE80211_S_AUTH:
		case IEEE80211_S_ASSOC:
			switch (mgt) {
			case IEEE80211_FC0_SUBTYPE_AUTH:
				if (ic->ic_opmode == IEEE80211_M_STA) {
					IEEE80211_SEND_MGMT(ic, ni,
					    IEEE80211_FC0_SUBTYPE_AUTH,
					    IEEE80211_AUTH_OPEN_REQUEST);
				}
				break;
			case IEEE80211_FC0_SUBTYPE_DEAUTH:
				/* ignore and retry scan on timeout */
				break;
			}
			break;
		case IEEE80211_S_RUN:
			timeout_del(&ic->ic_bgscan_timeout);
			ic->ic_bgscan_fail = 0;
			ieee80211_stop_ampdu_tx(ic, ni, mgt);
			ieee80211_ba_del(ni);
			switch (mgt) {
			case IEEE80211_FC0_SUBTYPE_AUTH:
				IEEE80211_SEND_MGMT(ic, ni,
				    IEEE80211_FC0_SUBTYPE_AUTH, 2);
				ic->ic_state = ostate;	/* stay RUN */
				break;
			case IEEE80211_FC0_SUBTYPE_DEAUTH:
				/* try to reauth */
				IEEE80211_SEND_MGMT(ic, ni,
				    IEEE80211_FC0_SUBTYPE_AUTH, 1);
				break;
			}
			break;
		}
		break;
	case IEEE80211_S_ASSOC:
		switch (ostate) {
		case IEEE80211_S_INIT:
		case IEEE80211_S_SCAN:
		case IEEE80211_S_ASSOC:
			if (ifp->if_flags & IFF_DEBUG)
				printf("%s: invalid transition %s -> %s\n",
				    ifp->if_xname, ieee80211_state_name[ostate],
				    ieee80211_state_name[nstate]);
			break;
		case IEEE80211_S_AUTH:
			IEEE80211_SEND_MGMT(ic, ni,
			    IEEE80211_FC0_SUBTYPE_ASSOC_REQ, 0);
			break;
		case IEEE80211_S_RUN:
			ieee80211_stop_ampdu_tx(ic, ni, mgt);
			ieee80211_ba_del(ni);
			IEEE80211_SEND_MGMT(ic, ni,
			    IEEE80211_FC0_SUBTYPE_ASSOC_REQ, 1);
			break;
		}
		break;
	case IEEE80211_S_RUN:
		switch (ostate) {
		case IEEE80211_S_INIT:
			if (ic->ic_opmode == IEEE80211_M_MONITOR)
				break;
		case IEEE80211_S_AUTH:
		case IEEE80211_S_RUN:
			if (ifp->if_flags & IFF_DEBUG)
				printf("%s: invalid transition %s -> %s\n",
				    ifp->if_xname, ieee80211_state_name[ostate],
				    ieee80211_state_name[nstate]);
			break;
		case IEEE80211_S_SCAN:		/* adhoc/hostap mode */
		case IEEE80211_S_ASSOC:		/* infra mode */
			if (ni->ni_txrate >= ni->ni_rates.rs_nrates)
				panic("%s: bogus xmit rate %u setup",
				    __func__, ni->ni_txrate);
			if (ifp->if_flags & IFF_DEBUG) {
				printf("%s: %s with %s ssid ",
				    ifp->if_xname,
				    ic->ic_opmode == IEEE80211_M_STA ?
				    "associated" : "synchronized",
				    ether_sprintf(ni->ni_bssid));
				ieee80211_print_essid(ic->ic_bss->ni_essid,
				    ni->ni_esslen);
				rate = ni->ni_rates.rs_rates[ni->ni_txrate] &
				    IEEE80211_RATE_VAL;
				printf(" channel %d",
				    ieee80211_chan2ieee(ic, ni->ni_chan));
				if (ni->ni_flags & IEEE80211_NODE_HT)
					printf(" start MCS %u", ni->ni_txmcs);
				else
					printf(" start %u%sMb",
					    rate / 2, (rate & 1) ? ".5" : "");
				printf(" %s preamble %s slot time%s%s%s\n",
				    (ic->ic_flags & IEEE80211_F_SHPREAMBLE) ?
					"short" : "long",
				    (ic->ic_flags & IEEE80211_F_SHSLOT) ?
					"short" : "long",
				    (ic->ic_flags & IEEE80211_F_USEPROT) ?
					" protection enabled" : "",
				    (ni->ni_flags & IEEE80211_NODE_HT) ?
					" HT enabled" : "",
				    (ni->ni_flags & IEEE80211_NODE_VHT) ?
					" VHT enabled" : "");
			}
			if (!(ic->ic_flags & IEEE80211_F_RSNON)) {
				/*
				 * NB: When RSN is enabled, we defer setting
				 * the link up until the port is valid.
				 */
				ieee80211_set_link_state(ic, LINK_STATE_UP);
				ni->ni_assoc_fail = 0;
			}
			ic->ic_mgt_timer = 0;
			ieee80211_set_beacon_miss_threshold(ic);
			if_start(ifp);
			break;
		}
		break;
	}
	return 0;
}

void
ieee80211_rtm_80211info_task(void *arg)
{
	struct ieee80211com *ic = arg;
	struct ifnet *ifp = &ic->ic_if;
	struct if_ieee80211_data ifie;
	int s = splnet();

	if (LINK_STATE_IS_UP(ifp->if_link_state)) {
		memset(&ifie, 0, sizeof(ifie));
		ifie.ifie_nwid_len = ic->ic_bss->ni_esslen;
		memcpy(ifie.ifie_nwid, ic->ic_bss->ni_essid,
		    sizeof(ifie.ifie_nwid));
		memcpy(ifie.ifie_addr, ic->ic_bss->ni_bssid,
		    sizeof(ifie.ifie_addr));
		ifie.ifie_channel = ieee80211_chan2ieee(ic,
		    ic->ic_bss->ni_chan);
		ifie.ifie_flags = ic->ic_flags;
		ifie.ifie_xflags = ic->ic_xflags;
		rtm_80211info(&ic->ic_if, &ifie);
	}

	splx(s);
}

void
ieee80211_set_link_state(struct ieee80211com *ic, int nstate)
{
	struct ifnet *ifp = &ic->ic_if;

	switch (ic->ic_opmode) {
#ifndef IEEE80211_STA_ONLY
	case IEEE80211_M_IBSS:
	case IEEE80211_M_HOSTAP:
		nstate = LINK_STATE_UNKNOWN;
		break;
#endif
	case IEEE80211_M_MONITOR:
		nstate = LINK_STATE_DOWN;
		break;
	default:
		break;
	}
	if (nstate != ifp->if_link_state) {
		ifp->if_link_state = nstate;
		if (LINK_STATE_IS_UP(nstate))
			task_add(systq, &ic->ic_rtm_80211info_task);
		if_link_state_change(ifp);
	}
}
