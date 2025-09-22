/*	$OpenBSD: ieee80211_output.c,v 1.142 2025/08/01 20:39:26 stsp Exp $	*/
/*	$NetBSD: ieee80211_output.c,v 1.13 2004/05/31 11:02:55 dyoung Exp $	*/

/*-
 * Copyright (c) 2001 Atsushi Onoe
 * Copyright (c) 2002, 2003 Sam Leffler, Errno Consulting
 * Copyright (c) 2007-2009 Damien Bergamini
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

#include "bpfilter.h"
#include "vlan.h"

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
#include <net/bpf.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#ifdef INET6
#include <netinet/ip6.h>
#endif

#if NVLAN > 0
#include <net/if_vlan_var.h>
#endif

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_priv.h>

int	ieee80211_mgmt_output(struct ifnet *, struct ieee80211_node *,
	    struct mbuf *, int);
int	ieee80211_can_use_ampdu(struct ieee80211com *,
	    struct ieee80211_node *);
u_int8_t *ieee80211_add_rsn_body(u_int8_t *, struct ieee80211com *,
	    const struct ieee80211_node *, int);
struct	mbuf *ieee80211_getmgmt(int, int, u_int);
struct	mbuf *ieee80211_get_probe_req(struct ieee80211com *,
	    struct ieee80211_node *);
#ifndef IEEE80211_STA_ONLY
struct	mbuf *ieee80211_get_probe_resp(struct ieee80211com *);
#endif
struct	mbuf *ieee80211_get_auth(struct ieee80211com *,
	    struct ieee80211_node *, u_int16_t, u_int16_t);
struct	mbuf *ieee80211_get_deauth(struct ieee80211com *,
	    struct ieee80211_node *, u_int16_t);
struct	mbuf *ieee80211_get_assoc_req(struct ieee80211com *,
	    struct ieee80211_node *, int);
#ifndef IEEE80211_STA_ONLY
struct	mbuf *ieee80211_get_assoc_resp(struct ieee80211com *,
	    struct ieee80211_node *, u_int16_t);
#endif
struct	mbuf *ieee80211_get_disassoc(struct ieee80211com *,
	    struct ieee80211_node *, u_int16_t);
struct	mbuf *ieee80211_get_addba_req(struct ieee80211com *,
	    struct ieee80211_node *, u_int8_t);
struct	mbuf *ieee80211_get_addba_resp(struct ieee80211com *,
	    struct ieee80211_node *, u_int8_t, u_int8_t, u_int16_t);
struct	mbuf *ieee80211_get_delba(struct ieee80211com *,
	    struct ieee80211_node *, u_int8_t, u_int8_t, u_int16_t);
uint8_t *ieee80211_add_wme_info(uint8_t *, struct ieee80211com *);
#ifndef IEEE80211_STA_ONLY
uint8_t *ieee80211_add_wme_param(uint8_t *, struct ieee80211com *);
#endif
struct	mbuf *ieee80211_get_sa_query(struct ieee80211com *,
	    struct ieee80211_node *, u_int8_t);
struct	mbuf *ieee80211_get_action(struct ieee80211com *,
	    struct ieee80211_node *, u_int8_t, u_int8_t, int);

/*
 * IEEE 802.11 output routine. Normally this will directly call the
 * Ethernet output routine because 802.11 encapsulation is called
 * later by the driver. This function can be used to send raw frames
 * if the mbuf has been tagged with a 802.11 data link type.
 */
int
ieee80211_output(struct ifnet *ifp, struct mbuf *m, struct sockaddr *dst,
    struct rtentry *rt)
{
	struct ieee80211_frame *wh;
	struct m_tag *mtag;
	int error = 0;

	/* Interface has to be up and running */
	if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) !=
	    (IFF_UP | IFF_RUNNING)) {
		error = ENETDOWN;
		goto bad;
	}

	/* Try to get the DLT from a mbuf tag */
	if ((mtag = m_tag_find(m, PACKET_TAG_DLT, NULL)) != NULL) {
		struct ieee80211com *ic = (void *)ifp;
		u_int dlt = *(u_int *)(mtag + 1);

		/* Fallback to ethernet for non-802.11 linktypes */
		if (!(dlt == DLT_IEEE802_11 || dlt == DLT_IEEE802_11_RADIO))
			goto fallback;

		if (m->m_pkthdr.len < sizeof(struct ieee80211_frame_min))
			return (EINVAL);
		wh = mtod(m, struct ieee80211_frame *);
		if ((wh->i_fc[0] & IEEE80211_FC0_VERSION_MASK) !=
		    IEEE80211_FC0_VERSION_0)
			return (EINVAL);
		if (!(ic->ic_caps & IEEE80211_C_RAWCTL) &&
		    (wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) ==
		    IEEE80211_FC0_TYPE_CTL)
			return (EINVAL);

		return (if_enqueue(ifp, m));
	}

 fallback:
	return (ether_output(ifp, m, dst, rt));

 bad:
	m_freem(m);
	return (error);
}

const char *
ieee80211_action_name(struct ieee80211_frame *wh)
{
	const u_int8_t *frm = (const uint8_t *)&wh[1];
	const char *categ_ba_name[3] = { "addba_req", "addba_resp", "delba" };

	if (frm[0] == IEEE80211_CATEG_BA && frm[1] < nitems(categ_ba_name))
		return categ_ba_name[frm[1]];

	return "action";
}

/*
 * Send a management frame to the specified node.  The node pointer
 * must have a reference as the pointer will be passed to the driver
 * and potentially held for a long time.  If the frame is successfully
 * dispatched to the driver, then it is responsible for freeing the
 * reference (and potentially free'ing up any associated storage).
 */
int
ieee80211_mgmt_output(struct ifnet *ifp, struct ieee80211_node *ni,
    struct mbuf *m, int type)
{
	struct ieee80211com *ic = (void *)ifp;
	struct ieee80211_frame *wh;

	if (ni == NULL)
		panic("null node");
	ni->ni_inact = 0;

	/*
	 * We want to pass the node down to the driver's start
	 * routine.  We could stick this in an m_tag and tack that
	 * on to the mbuf.  However that's rather expensive to do
	 * for every frame so instead we stuff it in a special pkthdr
	 * field.
	 */
	M_PREPEND(m, sizeof(struct ieee80211_frame), M_DONTWAIT);
	if (m == NULL)
		return ENOMEM;
	m->m_pkthdr.ph_cookie = ni;

	wh = mtod(m, struct ieee80211_frame *);
	wh->i_fc[0] = IEEE80211_FC0_VERSION_0 | IEEE80211_FC0_TYPE_MGT | type;
	wh->i_fc[1] = IEEE80211_FC1_DIR_NODS;
	*(u_int16_t *)&wh->i_dur[0] = 0;
	*(u_int16_t *)&wh->i_seq[0] =
	    htole16(ni->ni_txseq << IEEE80211_SEQ_SEQ_SHIFT);
	ni->ni_txseq = (ni->ni_txseq + 1) & 0xfff;
	IEEE80211_ADDR_COPY(wh->i_addr1, ni->ni_macaddr);
	IEEE80211_ADDR_COPY(wh->i_addr2, ic->ic_myaddr);
	IEEE80211_ADDR_COPY(wh->i_addr3, ni->ni_bssid);

	/* check if protection is required for this mgmt frame */
	if ((ic->ic_caps & IEEE80211_C_MFP) &&
	    (type == IEEE80211_FC0_SUBTYPE_DISASSOC ||
	     type == IEEE80211_FC0_SUBTYPE_DEAUTH ||
	     type == IEEE80211_FC0_SUBTYPE_ACTION)) {
		/*
		 * Hack: we should not set the Protected bit in outgoing
		 * group management frames, however it is used as an
		 * indication to the drivers that they must encrypt the
		 * frame.  Drivers should clear this bit from group
		 * management frames (software crypto code will do it).
		 * XXX could use an mbuf flag..
		 */
		if (IEEE80211_IS_MULTICAST(wh->i_addr1) ||
		    (ni->ni_flags & IEEE80211_NODE_TXMGMTPROT))
			wh->i_fc[1] |= IEEE80211_FC1_PROTECTED;
	}

	if (ifp->if_flags & IFF_DEBUG) {
		/* avoid to print too many frames */
		if (
#ifndef IEEE80211_STA_ONLY
		    ic->ic_opmode == IEEE80211_M_IBSS ||
#endif
#ifdef IEEE80211_DEBUG
		    ieee80211_debug > 1 ||
#endif
		    (type & IEEE80211_FC0_SUBTYPE_MASK) !=
		    IEEE80211_FC0_SUBTYPE_PROBE_RESP) {
			const char *subtype_name;
			if ((type & IEEE80211_FC0_SUBTYPE_MASK) ==
			    IEEE80211_FC0_SUBTYPE_ACTION)
				subtype_name = ieee80211_action_name(wh);
			else
				subtype_name = ieee80211_mgt_subtype_name[
				    (type & IEEE80211_FC0_SUBTYPE_MASK) >>
				    IEEE80211_FC0_SUBTYPE_SHIFT];
			printf("%s: sending %s to %s on channel %u mode %s\n",
			    ifp->if_xname, subtype_name,
			    ether_sprintf(ni->ni_macaddr),
			    ieee80211_chan2ieee(ic, ni->ni_chan),
			    ieee80211_phymode_name[ic->ic_curmode]);
		}
	}

#ifndef IEEE80211_STA_ONLY
	if (ic->ic_opmode == IEEE80211_M_HOSTAP &&
	    ieee80211_pwrsave(ic, m, ni) != 0)
		return 0;
#endif
	mq_enqueue(&ic->ic_mgtq, m);
	ifp->if_timer = 1;
	if_start(ifp);
	return 0;
}

/*-
 * EDCA tables are computed using the following formulas:
 *
 * 1) EDCATable (non-AP QSTA)
 *
 * AC     CWmin 	   CWmax	   AIFSN  TXOP limit(ms)
 * -------------------------------------------------------------
 * AC_BK  aCWmin	   aCWmax	   7	  0
 * AC_BE  aCWmin	   aCWmax	   3	  0
 * AC_VI  (aCWmin+1)/2-1   aCWmin	   2	  agn=3.008 b=6.016 others=0
 * AC_VO  (aCWmin+1)/4-1   (aCWmin+1)/2-1  2	  agn=1.504 b=3.264 others=0
 *
 * 2) QAPEDCATable (QAP)
 *
 * AC     CWmin 	   CWmax	   AIFSN  TXOP limit(ms)
 * -------------------------------------------------------------
 * AC_BK  aCWmin	   aCWmax	   7	  0
 * AC_BE  aCWmin	   4*(aCWmin+1)-1  3	  0
 * AC_VI  (aCWmin+1)/2-1   aCWmin	   1	  agn=3.008 b=6.016 others=0
 * AC_VO  (aCWmin+1)/4-1   (aCWmin+1)/2-1  1	  agn=1.504 b=3.264 others=0
 *
 * and the following aCWmin/aCWmax values:
 *
 * PHY		aCWmin	aCWmax
 * ---------------------------
 * 11A		15	1023
 * 11B  	31	1023
 * 11G		15*	1023	(*) aCWmin(1)
 * 11N		15	1023
 */
const struct ieee80211_edca_ac_params
    ieee80211_edca_table[IEEE80211_MODE_MAX][EDCA_NUM_AC] = {
	[IEEE80211_MODE_11B] = {
		[EDCA_AC_BK] = { 5, 10, 7,   0 },
		[EDCA_AC_BE] = { 5, 10, 3,   0 },
		[EDCA_AC_VI] = { 4,  5, 2, 188 },
		[EDCA_AC_VO] = { 3,  4, 2, 102 }
	},
	[IEEE80211_MODE_11A] = {
		[EDCA_AC_BK] = { 4, 10, 7,   0 },
		[EDCA_AC_BE] = { 4, 10, 3,   0 },
		[EDCA_AC_VI] = { 3,  4, 2,  94 },
		[EDCA_AC_VO] = { 2,  3, 2,  47 }
	},
	[IEEE80211_MODE_11G] = {
		[EDCA_AC_BK] = { 4, 10, 7,   0 },
		[EDCA_AC_BE] = { 4, 10, 3,   0 },
		[EDCA_AC_VI] = { 3,  4, 2,  94 },
		[EDCA_AC_VO] = { 2,  3, 2,  47 }
	},
	[IEEE80211_MODE_11N] = {
		[EDCA_AC_BK] = { 4, 10, 7,   0 },
		[EDCA_AC_BE] = { 4, 10, 3,   0 },
		[EDCA_AC_VI] = { 3,  4, 2,  94 },
		[EDCA_AC_VO] = { 2,  3, 2,  47 }
	},
	[IEEE80211_MODE_11AC] = {
		[EDCA_AC_BK] = { 4, 10, 7,   0 },
		[EDCA_AC_BE] = { 4, 10, 3,   0 },
		[EDCA_AC_VI] = { 3,  4, 2,  94 },
		[EDCA_AC_VO] = { 2,  3, 2,  47 }
	},
};

#ifndef IEEE80211_STA_ONLY
const struct ieee80211_edca_ac_params
    ieee80211_qap_edca_table[IEEE80211_MODE_MAX][EDCA_NUM_AC] = {
	[IEEE80211_MODE_11B] = {
		[EDCA_AC_BK] = { 5, 10, 7,   0 },
		[EDCA_AC_BE] = { 5,  7, 3,   0 },
		[EDCA_AC_VI] = { 4,  5, 1, 188 },
		[EDCA_AC_VO] = { 3,  4, 1, 102 }
	},
	[IEEE80211_MODE_11A] = {
		[EDCA_AC_BK] = { 4, 10, 7,   0 },
		[EDCA_AC_BE] = { 4,  6, 3,   0 },
		[EDCA_AC_VI] = { 3,  4, 1,  94 },
		[EDCA_AC_VO] = { 2,  3, 1,  47 }
	},
	[IEEE80211_MODE_11G] = {
		[EDCA_AC_BK] = { 4, 10, 7,   0 },
		[EDCA_AC_BE] = { 4,  6, 3,   0 },
		[EDCA_AC_VI] = { 3,  4, 1,  94 },
		[EDCA_AC_VO] = { 2,  3, 1,  47 }
	},
	[IEEE80211_MODE_11N] = {
		[EDCA_AC_BK] = { 4, 10, 7,   0 },
		[EDCA_AC_BE] = { 4,  6, 3,   0 },
		[EDCA_AC_VI] = { 3,  4, 1,  94 },
		[EDCA_AC_VO] = { 2,  3, 1,  47 }
	},
	[IEEE80211_MODE_11AC] = {
		[EDCA_AC_BK] = { 4, 10, 7,   0 },
		[EDCA_AC_BE] = { 4,  6, 3,   0 },
		[EDCA_AC_VI] = { 3,  4, 1,  94 },
		[EDCA_AC_VO] = { 2,  3, 1,  47 }
	},
};
#endif	/* IEEE80211_STA_ONLY */

/*
 * Return the EDCA Access Category to be used for transmitting a frame with
 * user-priority `up'.
 */
enum ieee80211_edca_ac
ieee80211_up_to_ac(struct ieee80211com *ic, int up)
{
	/* see Table 9-1 */
	static const enum ieee80211_edca_ac up_to_ac[] = {
		EDCA_AC_BE,	/* BE */
		EDCA_AC_BK,	/* BK */
		EDCA_AC_BK,	/* -- */
		EDCA_AC_BE,	/* EE */
		EDCA_AC_VI,	/* CL */
		EDCA_AC_VI,	/* VI */
		EDCA_AC_VO,	/* VO */
		EDCA_AC_VO	/* NC */
	};
	enum ieee80211_edca_ac ac;

	ac = (up <= 7) ? up_to_ac[up] : EDCA_AC_BE;

#ifndef IEEE80211_STA_ONLY
	if (ic->ic_opmode == IEEE80211_M_HOSTAP)
		return ac;
#endif
	/*
	 * We do not support the admission control procedure defined in
	 * IEEE Std 802.11-2012 section 9.19.4.2.3. The spec says that
	 * non-AP QSTAs that don't support this procedure shall use EDCA
	 * parameters of a lower priority AC that does not require
	 * admission control.
	 */
	while (ac != EDCA_AC_BK && ic->ic_edca_ac[ac].ac_acm) {
		switch (ac) {
		case EDCA_AC_BK:
			/* can't get there */
			break;
		case EDCA_AC_BE:
			/* BE shouldn't require admission control */
			ac = EDCA_AC_BK;
			break;
		case EDCA_AC_VI:
			ac = EDCA_AC_BE;
			break;
		case EDCA_AC_VO:
			ac = EDCA_AC_VI;
			break;
		}
	}
	return ac;
}

/*
 * Get mbuf's user-priority: if mbuf is not VLAN tagged, select user-priority
 * based on the DSCP (Differentiated Services Codepoint) field.
 */
int
ieee80211_classify(struct ieee80211com *ic, struct mbuf *m)
{
	struct ether_header eh;
	u_int8_t ds_field;
#if NVLAN > 0
	if (m->m_flags & M_VLANTAG)	/* use VLAN 802.1D user-priority */
		return EVL_PRIOFTAG(m->m_pkthdr.ether_vtag);
#endif
	m_copydata(m, 0, sizeof(eh), (caddr_t)&eh);
	if (eh.ether_type == htons(ETHERTYPE_IP)) {
		struct ip ip;
		m_copydata(m, sizeof(eh), sizeof(ip), (caddr_t)&ip);
		if (ip.ip_v != 4)
			return 0;
		ds_field = ip.ip_tos;
	}
#ifdef INET6
	else if (eh.ether_type == htons(ETHERTYPE_IPV6)) {
		struct ip6_hdr ip6;
		u_int32_t flowlabel;
		m_copydata(m, sizeof(eh), sizeof(ip6), (caddr_t)&ip6);
		flowlabel = ntohl(ip6.ip6_flow);
		if ((flowlabel >> 28) != 6)
			return 0;
		ds_field = (flowlabel >> 20) & 0xff;
	}
#endif	/* INET6 */
	else	/* neither IPv4 nor IPv6 */
		return 0;

	/*
	 * Map Differentiated Services Codepoint field (see RFC2474).
	 * Preserves backward compatibility with IP Precedence field.
	 */
	switch (ds_field & 0xfc) {
	case IPTOS_PREC_PRIORITY:
		return EDCA_AC_VI;
	case IPTOS_PREC_IMMEDIATE:
		return EDCA_AC_BK;
	case IPTOS_PREC_FLASH:
	case IPTOS_PREC_FLASHOVERRIDE:
	case IPTOS_PREC_CRITIC_ECP:
	case IPTOS_PREC_INTERNETCONTROL:
	case IPTOS_PREC_NETCONTROL:
		return EDCA_AC_VO;
	default:
		return EDCA_AC_BE;
	}
}

int
ieee80211_can_use_ampdu(struct ieee80211com *ic, struct ieee80211_node *ni)
{
	return (ni->ni_flags & IEEE80211_NODE_HT) &&
	    (ic->ic_caps & IEEE80211_C_TX_AMPDU) &&
	    !(ic->ic_opmode == IEEE80211_M_STA && ni != ic->ic_bss) &&
	    /*
	     * Don't use A-MPDU on non-encrypted networks. There are devices
	     * with buggy firmware which allow an attacker to inject 802.11
	     * frames into a wifi network by embedding rogue A-MPDU subframes
	     * in an arbitrary data payload (e.g. PNG images) which may end
	     * up appearing as actual frames after de-aggregation by a buggy
	     * device; see https://github.com/rpp0/aggr-inject for details.
	     * WPA2 prevents this injection attack since the attacker would
	     * need to inject frames which get decrypted correctly.
	     */
	    ((ic->ic_flags & IEEE80211_F_RSNON) &&
	      (ni->ni_rsnprotos & IEEE80211_PROTO_RSN));
}

void
ieee80211_tx_compressed_bar(struct ieee80211com *ic, struct ieee80211_node *ni,
    int tid, uint16_t ssn)
{
	struct ifnet *ifp = &ic->ic_if;
	struct mbuf *m;

	m = ieee80211_get_compressed_bar(ic, ni, tid, ssn);
	if (m == NULL)
		return;

	ieee80211_ref_node(ni);
	if (mq_enqueue(&ic->ic_mgtq, m) == 0)
		if_start(ifp);
	else
		ieee80211_release_node(ic, ni);
}

/*
 * Encapsulate an outbound data frame.  The mbuf chain is updated and
 * a reference to the destination node is returned.  If an error is
 * encountered NULL is returned and the node reference will also be NULL.
 *
 * NB: The caller is responsible for free'ing a returned node reference.
 *     The convention is ic_bss is not reference counted; the caller must
 *     maintain that.
 */
struct mbuf *
ieee80211_encap(struct ifnet *ifp, struct mbuf *m, struct ieee80211_node **pni)
{
	struct ieee80211com *ic = (void *)ifp;
	struct ether_header eh;
	struct ieee80211_frame *wh;
	struct ieee80211_node *ni = NULL;
	struct llc *llc;
	struct m_tag *mtag;
	u_int8_t *addr;
	u_int dlt, hdrlen;
	int addqos, tid;

	/* Handle raw frames if mbuf is tagged as 802.11 */
	if ((mtag = m_tag_find(m, PACKET_TAG_DLT, NULL)) != NULL) {
		dlt = *(u_int *)(mtag + 1);

		if (!(dlt == DLT_IEEE802_11 || dlt == DLT_IEEE802_11_RADIO))
			goto fallback;

		wh = mtod(m, struct ieee80211_frame *);
		switch (wh->i_fc[1] & IEEE80211_FC1_DIR_MASK) {
		case IEEE80211_FC1_DIR_NODS:
		case IEEE80211_FC1_DIR_FROMDS:
			addr = wh->i_addr1;
			break;
		case IEEE80211_FC1_DIR_DSTODS:
		case IEEE80211_FC1_DIR_TODS:
			addr = wh->i_addr3;
			break;
		default:
			goto bad;
		}

		ni = ieee80211_find_txnode(ic, addr);
		if (ni == NULL)
			ni = ieee80211_ref_node(ic->ic_bss);
		if (ni == NULL) {
			printf("%s: no node for dst %s, "
			    "discard raw tx frame\n", ifp->if_xname,
			    ether_sprintf(addr));
			ic->ic_stats.is_tx_nonode++;
			goto bad;
		}
		ni->ni_inact = 0;

		*pni = ni;
		return (m);
	}

 fallback:
	if (ic->ic_opmode == IEEE80211_M_MONITOR)
		goto bad;

	if (m->m_len < sizeof(struct ether_header)) {
		m = m_pullup(m, sizeof(struct ether_header));
		if (m == NULL) {
			ic->ic_stats.is_tx_nombuf++;
			goto bad;
		}
	}
	memcpy(&eh, mtod(m, caddr_t), sizeof(struct ether_header));

	ni = ieee80211_find_txnode(ic, eh.ether_dhost);
	if (ni == NULL) {
		DPRINTF(("no node for dst %s, discard frame\n",
		    ether_sprintf(eh.ether_dhost)));
		ic->ic_stats.is_tx_nonode++;
		goto bad;
	}

#ifndef IEEE80211_STA_ONLY
	if (ic->ic_opmode == IEEE80211_M_HOSTAP && ni != ic->ic_bss &&
	    ni->ni_state != IEEE80211_STA_ASSOC) {
		ic->ic_stats.is_tx_nonode++;
		goto bad;
	}
#endif

	if ((ic->ic_flags & IEEE80211_F_RSNON) &&
	    !ni->ni_port_valid &&
	    eh.ether_type != htons(ETHERTYPE_EAPOL)) {
		DPRINTF(("port not valid: %s\n",
		    ether_sprintf(eh.ether_dhost)));
		ic->ic_stats.is_tx_noauth++;
		goto bad;
	}

	if ((ic->ic_flags & IEEE80211_F_COUNTERM) &&
	    ni->ni_rsncipher == IEEE80211_CIPHER_TKIP)
		/* XXX TKIP countermeasures! */;

	ni->ni_inact = 0;

	if ((ic->ic_flags & IEEE80211_F_QOS) &&
	    (ni->ni_flags & IEEE80211_NODE_QOS) &&
	    /* do not QoS-encapsulate EAPOL frames */
	    eh.ether_type != htons(ETHERTYPE_EAPOL)) {
		struct ieee80211_tx_ba *ba;
		tid = ieee80211_classify(ic, m);
		ba = &ni->ni_tx_ba[tid];
		/* We use QoS data frames for aggregation only. */
		if (ba->ba_state != IEEE80211_BA_AGREED) {
			hdrlen = sizeof(struct ieee80211_frame);
			addqos = 0;
			if (ieee80211_can_use_ampdu(ic, ni))
				ieee80211_node_trigger_addba_req(ni, tid);
		} else {
			hdrlen = sizeof(struct ieee80211_qosframe);
			addqos = 1;
		}
	} else {
		hdrlen = sizeof(struct ieee80211_frame);
		addqos = 0;
	}
	m_adj(m, sizeof(struct ether_header) - LLC_SNAPFRAMELEN);
	llc = mtod(m, struct llc *);
	llc->llc_dsap = llc->llc_ssap = LLC_SNAP_LSAP;
	llc->llc_control = LLC_UI;
	llc->llc_snap.org_code[0] = 0;
	llc->llc_snap.org_code[1] = 0;
	llc->llc_snap.org_code[2] = 0;
	llc->llc_snap.ether_type = eh.ether_type;
	M_PREPEND(m, hdrlen, M_DONTWAIT);
	if (m == NULL) {
		ic->ic_stats.is_tx_nombuf++;
		goto bad;
	}
	wh = mtod(m, struct ieee80211_frame *);
	wh->i_fc[0] = IEEE80211_FC0_VERSION_0 | IEEE80211_FC0_TYPE_DATA;
	*(u_int16_t *)&wh->i_dur[0] = 0;
	if (addqos) {
		struct ieee80211_qosframe *qwh =
		    (struct ieee80211_qosframe *)wh;
		u_int16_t qos = tid;

		if (ic->ic_tid_noack & (1 << tid))
			qos |= IEEE80211_QOS_ACK_POLICY_NOACK;
		else {
			/* Use HT immediate block-ack. */
			qos |= IEEE80211_QOS_ACK_POLICY_NORMAL;
		}
		qwh->i_fc[0] |= IEEE80211_FC0_SUBTYPE_QOS;
		*(u_int16_t *)qwh->i_qos = htole16(qos);
		*(u_int16_t *)qwh->i_seq =
		    htole16(ni->ni_qos_txseqs[tid] << IEEE80211_SEQ_SEQ_SHIFT);
		ni->ni_qos_txseqs[tid] = (ni->ni_qos_txseqs[tid] + 1) & 0xfff;
	} else {
		*(u_int16_t *)&wh->i_seq[0] =
		    htole16(ni->ni_txseq << IEEE80211_SEQ_SEQ_SHIFT);
		ni->ni_txseq = (ni->ni_txseq + 1) & 0xfff;
	}
	switch (ic->ic_opmode) {
	case IEEE80211_M_STA:
		wh->i_fc[1] = IEEE80211_FC1_DIR_TODS;
		IEEE80211_ADDR_COPY(wh->i_addr1, ni->ni_bssid);
		IEEE80211_ADDR_COPY(wh->i_addr2, eh.ether_shost);
		IEEE80211_ADDR_COPY(wh->i_addr3, eh.ether_dhost);
		break;
#ifndef IEEE80211_STA_ONLY
	case IEEE80211_M_IBSS:
	case IEEE80211_M_AHDEMO:
		wh->i_fc[1] = IEEE80211_FC1_DIR_NODS;
		IEEE80211_ADDR_COPY(wh->i_addr1, eh.ether_dhost);
		IEEE80211_ADDR_COPY(wh->i_addr2, eh.ether_shost);
		IEEE80211_ADDR_COPY(wh->i_addr3, ic->ic_bss->ni_bssid);
		break;
	case IEEE80211_M_HOSTAP:
		wh->i_fc[1] = IEEE80211_FC1_DIR_FROMDS;
		IEEE80211_ADDR_COPY(wh->i_addr1, eh.ether_dhost);
		IEEE80211_ADDR_COPY(wh->i_addr2, ni->ni_bssid);
		IEEE80211_ADDR_COPY(wh->i_addr3, eh.ether_shost);
		break;
#endif
	default:
		/* should not get there */
		goto bad;
	}

	if ((ic->ic_flags & IEEE80211_F_WEPON) ||
	    ((ic->ic_flags & IEEE80211_F_RSNON) &&
	     (ni->ni_flags & IEEE80211_NODE_TXPROT)))
		wh->i_fc[1] |= IEEE80211_FC1_PROTECTED;

#ifndef IEEE80211_STA_ONLY
	if (ic->ic_opmode == IEEE80211_M_HOSTAP &&
	    ieee80211_pwrsave(ic, m, ni) != 0) {
		*pni = NULL;
		return NULL;
	}
#endif
	*pni = ni;
	return m;
bad:
	m_freem(m);
	if (ni != NULL)
		ieee80211_release_node(ic, ni);
	*pni = NULL;
	return NULL;
}

/*
 * Add a Capability Information field to a frame (see 7.3.1.4).
 */
u_int8_t *
ieee80211_add_capinfo(u_int8_t *frm, struct ieee80211com *ic,
    const struct ieee80211_node *ni)
{
	u_int16_t capinfo;

#ifndef IEEE80211_STA_ONLY
	if (ic->ic_opmode == IEEE80211_M_IBSS)
		capinfo = IEEE80211_CAPINFO_IBSS;
	else if (ic->ic_opmode == IEEE80211_M_HOSTAP)
		capinfo = IEEE80211_CAPINFO_ESS;
	else
#endif
		capinfo = 0;
#ifndef IEEE80211_STA_ONLY
	if (ic->ic_opmode == IEEE80211_M_HOSTAP &&
	    (ic->ic_flags & (IEEE80211_F_WEPON | IEEE80211_F_RSNON)))
		capinfo |= IEEE80211_CAPINFO_PRIVACY;
#endif
	/* NB: some 11a AP's reject the request when short preamble is set */
	if ((ic->ic_flags & IEEE80211_F_SHPREAMBLE) &&
	    IEEE80211_IS_CHAN_2GHZ(ni->ni_chan))
		capinfo |= IEEE80211_CAPINFO_SHORT_PREAMBLE;
	if (ic->ic_flags & IEEE80211_F_SHSLOT)
		capinfo |= IEEE80211_CAPINFO_SHORT_SLOTTIME;
	LE_WRITE_2(frm, capinfo);
	return frm + 2;
}

/*
 * Add an SSID element to a frame (see 7.3.2.1).
 */
u_int8_t *
ieee80211_add_ssid(u_int8_t *frm, const u_int8_t *ssid, u_int len)
{
	*frm++ = IEEE80211_ELEMID_SSID;
	*frm++ = len;
	memcpy(frm, ssid, len);
	return frm + len;
}

/*
 * Add a supported rates element to a frame (see 7.3.2.2).
 */
u_int8_t *
ieee80211_add_rates(u_int8_t *frm, const struct ieee80211_rateset *rs)
{
	int nrates;

	*frm++ = IEEE80211_ELEMID_RATES;
	nrates = min(rs->rs_nrates, IEEE80211_RATE_SIZE);
	*frm++ = nrates;
	memcpy(frm, rs->rs_rates, nrates);
	return frm + nrates;
}

#ifndef IEEE80211_STA_ONLY
/*
 * Add a DS Parameter Set element to a frame (see 7.3.2.4).
 */
u_int8_t *
ieee80211_add_ds_params(u_int8_t *frm, struct ieee80211com *ic,
    const struct ieee80211_node *ni)
{
	*frm++ = IEEE80211_ELEMID_DSPARMS;
	*frm++ = 1;
	*frm++ = ieee80211_chan2ieee(ic, ni->ni_chan);
	return frm;
}

/*
 * Add a TIM element to a frame (see 7.3.2.6 and Annex L).
 */
u_int8_t *
ieee80211_add_tim(u_int8_t *frm, struct ieee80211com *ic)
{
	u_int i, offset = 0, len;

	/* find first non-zero octet in the virtual bit map */
	for (i = 0; i < ic->ic_tim_len && ic->ic_tim_bitmap[i] == 0; i++)
		;

	/* clear the lsb as it is reserved for the broadcast indication bit */
	if (i < ic->ic_tim_len)
		offset = i & ~1;

	/* find last non-zero octet in the virtual bit map */
	for (i = ic->ic_tim_len - 1; i > 0 && ic->ic_tim_bitmap[i] == 0; i--)
		;

	len = i - offset + 1;

	*frm++ = IEEE80211_ELEMID_TIM;
	*frm++ = len + 3;		/* length */
	*frm++ = ic->ic_dtim_count;	/* DTIM count */
	*frm++ = ic->ic_dtim_period;	/* DTIM period */

	/* Bitmap Control */
	*frm = offset;
	/* set broadcast/multicast indication bit if necessary */
	if (ic->ic_dtim_count == 0 && ic->ic_tim_mcast_pending)
		*frm |= 0x01;
	frm++;

	/* Partial Virtual Bitmap */
	memcpy(frm, &ic->ic_tim_bitmap[offset], len);
	return frm + len;
}

/*
 * Add an IBSS Parameter Set element to a frame (see 7.3.2.7).
 */
u_int8_t *
ieee80211_add_ibss_params(u_int8_t *frm, const struct ieee80211_node *ni)
{
	*frm++ = IEEE80211_ELEMID_IBSSPARMS;
	*frm++ = 2;
	LE_WRITE_2(frm, 0);	/* TODO: ATIM window */
	return frm + 2;
}

/*
 * Add an EDCA Parameter Set element to a frame (see 7.3.2.29).
 */
u_int8_t *
ieee80211_add_edca_params(u_int8_t *frm, struct ieee80211com *ic)
{
	const struct ieee80211_edca_ac_params *edca;
	int aci;

	*frm++ = IEEE80211_ELEMID_EDCAPARMS;
	*frm++ = 18;	/* length */
	*frm++ = 0;	/* QoS Info */
	*frm++ = 0;	/* reserved */

	/* setup AC Parameter Records */
	edca = ieee80211_edca_table[ic->ic_curmode];
	for (aci = 0; aci < EDCA_NUM_AC; aci++) {
		const struct ieee80211_edca_ac_params *ac = &edca[aci];

		*frm++ = (aci << 5) | ((ac->ac_acm & 0x1) << 4) |
			 (ac->ac_aifsn & 0xf);
		*frm++ = (ac->ac_ecwmax << 4) |
			 (ac->ac_ecwmin & 0xf);
		LE_WRITE_2(frm, ac->ac_txoplimit); frm += 2;
	}
	return frm;
}

/*
 * Add an ERP element to a frame (see 7.3.2.13).
 */
u_int8_t *
ieee80211_add_erp(u_int8_t *frm, struct ieee80211com *ic)
{
	u_int8_t erp;
	int nonerpsta = 0;

	*frm++ = IEEE80211_ELEMID_ERP;
	*frm++ = 1;
	erp = 0;
	/*
	 * The NonERP_Present bit shall be set to 1 when a NonERP STA
	 * is associated with the BSS.
	 */
	ieee80211_iterate_nodes(ic, ieee80211_count_nonerpsta, &nonerpsta);
	if (nonerpsta != 0)
		erp |= IEEE80211_ERP_NON_ERP_PRESENT;
	/*
	 * If one or more NonERP STAs are associated in the BSS, the
	 * Use_Protection bit shall be set to 1 in transmitted ERP
	 * Information Elements.
	 */
	if (ic->ic_flags & IEEE80211_F_USEPROT)
		erp |= IEEE80211_ERP_USE_PROTECTION;
	/*
	 * The Barker_Preamble_Mode bit shall be set to 1 by the ERP
	 * Information Element sender if one or more associated NonERP
	 * STAs are not short preamble capable.
	 */
	if (!(ic->ic_flags & IEEE80211_F_SHPREAMBLE))
		erp |= IEEE80211_ERP_BARKER_MODE;
	*frm++ = erp;
	return frm;
}
#endif	/* IEEE80211_STA_ONLY */

/*
 * Add a QoS Capability element to a frame (see 7.3.2.35).
 */
u_int8_t *
ieee80211_add_qos_capability(u_int8_t *frm, struct ieee80211com *ic)
{
	*frm++ = IEEE80211_ELEMID_QOS_CAP;
	*frm++ = 1;
	*frm++ = 0;	/* QoS Info */
	return frm;
}

/*
 * Add a Wifi-Alliance WME (aka WMM) info element to a frame.
 * WME is a requirement for Wifi-Alliance compliance and some
 * 11n APs will not negotiate HT if this element is missing.
 */
uint8_t *
ieee80211_add_wme_info(uint8_t *frm, struct ieee80211com *ic)
{
	*frm++ = IEEE80211_ELEMID_VENDOR;
	*frm++ = 7;
	memcpy(frm, MICROSOFT_OUI, 3); frm += 3;
	*frm++ = 2; /* OUI type */
	*frm++ = 0; /* OUI subtype */
	*frm++ = 1; /* version */
	*frm++ = 0; /* info */

	return frm;
}

#ifndef IEEE80211_STA_ONLY
/*
 * Add a Wifi-Alliance WMM (aka WME) parameter element to a frame.
 */
uint8_t *
ieee80211_add_wme_param(uint8_t *frm, struct ieee80211com *ic)
{
	const struct ieee80211_edca_ac_params *edca;
	int aci;

	*frm++ = IEEE80211_ELEMID_VENDOR;
	*frm++ = 24;
	memcpy(frm, MICROSOFT_OUI, 3); frm += 3;
	*frm++ = 2; /* OUI type */
	*frm++ = 1; /* OUI subtype */
	*frm++ = 1; /* version */
	*frm++ = 0; /* info */
	*frm++ = 0; /* reserved */

	/* setup AC Parameter Records */
	edca = ieee80211_edca_table[ic->ic_curmode];
	for (aci = 0; aci < EDCA_NUM_AC; aci++) {
		const struct ieee80211_edca_ac_params *ac = &edca[aci];

		*frm++ = (aci << 5) | ((ac->ac_acm & 0x1) << 4) |
			 (ac->ac_aifsn & 0xf);
		*frm++ = (ac->ac_ecwmax << 4) |
			 (ac->ac_ecwmin & 0xf);
		LE_WRITE_2(frm, ac->ac_txoplimit); frm += 2;
	}

	return frm;
}
#endif

/*
 * Add an RSN element to a frame (see 802.11-2012 8.4.2.27)
 */
u_int8_t *
ieee80211_add_rsn_body(u_int8_t *frm, struct ieee80211com *ic,
    const struct ieee80211_node *ni, int wpa)
{
	const u_int8_t *oui = wpa ? MICROSOFT_OUI : IEEE80211_OUI;
	u_int8_t *pcount;
	u_int16_t count, rsncaps;

	/* write Version field */
	LE_WRITE_2(frm, 1); frm += 2;

	/* write Group Data Cipher Suite field (see 802.11-2012 Table 8-99) */
	memcpy(frm, oui, 3); frm += 3;
	switch (ni->ni_rsngroupcipher) {
	case IEEE80211_CIPHER_WEP40:
		*frm++ = 1;
		break;
	case IEEE80211_CIPHER_TKIP:
		*frm++ = 2;
		break;
	case IEEE80211_CIPHER_CCMP:
		*frm++ = 4;
		break;
	case IEEE80211_CIPHER_WEP104:
		*frm++ = 5;
		break;
	default:
		/* can't get there */
		panic("invalid group data cipher!");
	}

	pcount = frm; frm += 2;
	count = 0;
	/* write Pairwise Cipher Suite List */
	if (ni->ni_rsnciphers & IEEE80211_CIPHER_USEGROUP) {
		memcpy(frm, oui, 3); frm += 3;
		*frm++ = 0;
		count++;
	}
	if (ni->ni_rsnciphers & IEEE80211_CIPHER_TKIP) {
		memcpy(frm, oui, 3); frm += 3;
		*frm++ = 2;
		count++;
	}
	if (ni->ni_rsnciphers & IEEE80211_CIPHER_CCMP) {
		memcpy(frm, oui, 3); frm += 3;
		*frm++ = 4;
		count++;
	}
	/* write Pairwise Cipher Suite Count field */
	LE_WRITE_2(pcount, count);

	pcount = frm; frm += 2;
	count = 0;
	/* write AKM Suite List (see Table 20dc) */
	if (ni->ni_rsnakms & IEEE80211_AKM_8021X) {
		memcpy(frm, oui, 3); frm += 3;
		*frm++ = 1;
		count++;
	}
	if (ni->ni_rsnakms & IEEE80211_AKM_PSK) {
		memcpy(frm, oui, 3); frm += 3;
		*frm++ = 2;
		count++;
	}
	if (!wpa && (ni->ni_rsnakms & IEEE80211_AKM_SHA256_8021X)) {
		memcpy(frm, oui, 3); frm += 3;
		*frm++ = 5;
		count++;
	}
	if (!wpa && (ni->ni_rsnakms & IEEE80211_AKM_SHA256_PSK)) {
		memcpy(frm, oui, 3); frm += 3;
		*frm++ = 6;
		count++;
	}
	/* write AKM Suite List Count field */
	LE_WRITE_2(pcount, count);

	if (wpa)
		return frm;

	/* write RSN Capabilities field */
	rsncaps = (ni->ni_rsncaps & (IEEE80211_RSNCAP_PTKSA_RCNT_MASK |
	    IEEE80211_RSNCAP_GTKSA_RCNT_MASK));
	if (ic->ic_caps & IEEE80211_C_MFP) {
		rsncaps |= IEEE80211_RSNCAP_MFPC;
		if (ic->ic_flags & IEEE80211_F_MFPR)
			rsncaps |= IEEE80211_RSNCAP_MFPR;
	}
	if (ic->ic_flags & IEEE80211_F_PBAR)
		rsncaps |= IEEE80211_RSNCAP_PBAC;
	LE_WRITE_2(frm, rsncaps); frm += 2;

	if (ni->ni_flags & IEEE80211_NODE_PMKID) {
		/* write PMKID Count field */
		LE_WRITE_2(frm, 1); frm += 2;
		/* write PMKID List (only 1) */
		memcpy(frm, ni->ni_pmkid, IEEE80211_PMKID_LEN);
		frm += IEEE80211_PMKID_LEN;
	}

	if (!(ic->ic_caps & IEEE80211_C_MFP))
		return frm;

	if ((ni->ni_flags & IEEE80211_NODE_PMKID) == 0) {
		/* no PMKID (PMKID Count=0) */
		LE_WRITE_2(frm, 0); frm += 2;
	}

	/* write Group Integrity Cipher Suite field */
	memcpy(frm, oui, 3); frm += 3;
	switch (ic->ic_rsngroupmgmtcipher) {
	case IEEE80211_CIPHER_BIP:
		*frm++ = 6;
		break;
	default:
		/* can't get there */
		panic("invalid integrity group cipher!");
	}
	return frm;
}

u_int8_t *
ieee80211_add_rsn(u_int8_t *frm, struct ieee80211com *ic,
    const struct ieee80211_node *ni)
{
	u_int8_t *plen;

	*frm++ = IEEE80211_ELEMID_RSN;
	plen = frm++;	/* length filled in later */
	frm = ieee80211_add_rsn_body(frm, ic, ni, 0);

	/* write length field */
	*plen = frm - plen - 1;
	return frm;
}

/*
 * Add a vendor-specific WPA element to a frame.
 * This is required for compatibility with Wi-Fi Alliance WPA.
 */
u_int8_t *
ieee80211_add_wpa(u_int8_t *frm, struct ieee80211com *ic,
    const struct ieee80211_node *ni)
{
	u_int8_t *plen;

	*frm++ = IEEE80211_ELEMID_VENDOR;
	plen = frm++;	/* length filled in later */
	memcpy(frm, MICROSOFT_OUI, 3); frm += 3;
	*frm++ = 1;	/* WPA */
	frm = ieee80211_add_rsn_body(frm, ic, ni, 1);

	/* write length field */
	*plen = frm - plen - 1;
	return frm;
}

/*
 * Add an extended supported rates element to a frame (see 7.3.2.14).
 */
u_int8_t *
ieee80211_add_xrates(u_int8_t *frm, const struct ieee80211_rateset *rs)
{
	int nrates;

	KASSERT(rs->rs_nrates > IEEE80211_RATE_SIZE);

	*frm++ = IEEE80211_ELEMID_XRATES;
	nrates = rs->rs_nrates - IEEE80211_RATE_SIZE;
	*frm++ = nrates;
	memcpy(frm, rs->rs_rates + IEEE80211_RATE_SIZE, nrates);
	return frm + nrates;
}

/*
 * Add an HT Capabilities element to a frame (see 7.3.2.57).
 */
u_int8_t *
ieee80211_add_htcaps(u_int8_t *frm, struct ieee80211com *ic)
{
	*frm++ = IEEE80211_ELEMID_HTCAPS;
	*frm++ = 26;
	LE_WRITE_2(frm, ic->ic_htcaps); frm += 2;
	*frm++ = ic->ic_ampdu_params;
	memcpy(frm, ic->ic_sup_mcs, 10); frm += 10;
	LE_WRITE_2(frm, (ic->ic_max_rxrate & IEEE80211_MCS_RX_RATE_HIGH));
	frm += 2;
	*frm++ = ic->ic_tx_mcs_set;
	*frm++ = 0; /* reserved */
	*frm++ = 0; /* reserved */
	*frm++ = 0; /* reserved */
	LE_WRITE_2(frm, ic->ic_htxcaps); frm += 2;
	LE_WRITE_4(frm, ic->ic_txbfcaps); frm += 4;
	*frm++ = ic->ic_aselcaps;
	return frm;
}

#ifndef IEEE80211_STA_ONLY
/*
 * Add an HT Operation element to a frame (see 7.3.2.58).
 */
u_int8_t *
ieee80211_add_htop(u_int8_t *frm, struct ieee80211com *ic)
{
	*frm++ = IEEE80211_ELEMID_HTOP;
	*frm++ = 22;
	*frm++ = ieee80211_chan2ieee(ic, ic->ic_bss->ni_chan);
	*frm++ = ic->ic_bss->ni_htop0;
	LE_WRITE_2(frm, ic->ic_bss->ni_htop1); frm += 2;
	LE_WRITE_2(frm, ic->ic_bss->ni_htop2); frm += 2;
	memset(frm, 0, 16); frm += 16;
	return frm;
}
#endif	/* !IEEE80211_STA_ONLY */

/*
 * Add a VHT Capabilities element to a frame (see 802.11ac-2013 8.4.2.160.2).
 */
u_int8_t *
ieee80211_add_vhtcaps(u_int8_t *frm, struct ieee80211com *ic)
{
	*frm++ = IEEE80211_ELEMID_VHTCAPS;
	*frm++ = 12;
	LE_WRITE_4(frm, ic->ic_vhtcaps); frm += 4;
	LE_WRITE_2(frm, ic->ic_vht_rxmcs); frm += 2;
	LE_WRITE_2(frm, ic->ic_vht_rx_max_lgi_mbit_s); frm += 2;
	LE_WRITE_2(frm, ic->ic_vht_txmcs); frm += 2;
	LE_WRITE_2(frm, ic->ic_vht_tx_max_lgi_mbit_s); frm += 2;
	return frm;
}

#ifndef IEEE80211_STA_ONLY
/*
 * Add a Timeout Interval element to a frame (see 7.3.2.49).
 */
u_int8_t *
ieee80211_add_tie(u_int8_t *frm, u_int8_t type, u_int32_t value)
{
	*frm++ = IEEE80211_ELEMID_TIE;
	*frm++ = 5;	/* length */
	*frm++ = type;	/* Timeout Interval type */
	LE_WRITE_4(frm, value);
	return frm + 4;
}
#endif

struct mbuf *
ieee80211_getmgmt(int flags, int type, u_int pktlen)
{
	struct mbuf *m;

	/* reserve space for 802.11 header */
	pktlen += sizeof(struct ieee80211_frame);

	if (pktlen > MCLBYTES)
		panic("management frame too large: %u", pktlen);
	MGETHDR(m, flags, type);
	if (m == NULL)
		return NULL;
	if (pktlen > MHLEN) {
		MCLGET(m, flags);
		if (!(m->m_flags & M_EXT))
			return m_free(m);
	}
	m->m_data += sizeof(struct ieee80211_frame);
	return m;
}

/*-
 * Probe request frame format:
 * [tlv] SSID
 * [tlv] Supported rates
 * [tlv] Extended Supported Rates (802.11g)
 * [tlv] HT Capabilities (802.11n)
 */
struct mbuf *
ieee80211_get_probe_req(struct ieee80211com *ic, struct ieee80211_node *ni)
{
	const struct ieee80211_rateset *rs =
	    &ic->ic_sup_rates[ieee80211_node_abg_mode(ic, ni)];
	struct mbuf *m;
	u_int8_t *frm;

	m = ieee80211_getmgmt(M_DONTWAIT, MT_DATA,
	    2 + ic->ic_des_esslen +
	    2 + min(rs->rs_nrates, IEEE80211_RATE_SIZE) +
	    ((rs->rs_nrates > IEEE80211_RATE_SIZE) ?
		2 + rs->rs_nrates - IEEE80211_RATE_SIZE : 0) +
	    ((ic->ic_flags & IEEE80211_F_HTON) ? 28 + 9 : 0) +
	    ((ic->ic_flags & IEEE80211_F_VHTON) ? 14 : 0));
	if (m == NULL)
		return NULL;

	frm = mtod(m, u_int8_t *);
	frm = ieee80211_add_ssid(frm, ic->ic_des_essid, ic->ic_des_esslen);
	frm = ieee80211_add_rates(frm, rs);
	if (rs->rs_nrates > IEEE80211_RATE_SIZE)
		frm = ieee80211_add_xrates(frm, rs);
	if (ic->ic_flags & IEEE80211_F_HTON) {
		frm = ieee80211_add_htcaps(frm, ic);
		frm = ieee80211_add_wme_info(frm, ic);
	}
	if (ic->ic_flags & IEEE80211_F_VHTON)
		frm = ieee80211_add_htcaps(frm, ic);

	m->m_pkthdr.len = m->m_len = frm - mtod(m, u_int8_t *);

	return m;
}

#ifndef IEEE80211_STA_ONLY
/*-
 * Probe response frame format:
 * [8]   Timestamp
 * [2]   Beacon interval
 * [2]   Capability
 * [tlv] Service Set Identifier (SSID)
 * [tlv] Supported rates
 * [tlv] DS Parameter Set (802.11g)
 * [tlv] ERP Information (802.11g)
 * [tlv] Extended Supported Rates (802.11g)
 * [tlv] RSN (802.11i)
 * [tlv] EDCA Parameter Set (802.11e)
 * [tlv] HT Capabilities (802.11n)
 * [tlv] HT Operation (802.11n)
 */
struct mbuf *
ieee80211_get_probe_resp(struct ieee80211com *ic)
{
	const struct ieee80211_rateset *rs = &ic->ic_bss->ni_rates;
	struct mbuf *m;
	u_int8_t *frm;

	m = ieee80211_getmgmt(M_DONTWAIT, MT_DATA,
	    8 + 2 + 2 +
	    2 + ic->ic_bss->ni_esslen +
	    2 + min(rs->rs_nrates, IEEE80211_RATE_SIZE) +
	    2 + 1 +
	    ((ic->ic_opmode == IEEE80211_M_IBSS) ? 2 + 2 : 0) +
	    ((ic->ic_curmode == IEEE80211_MODE_11G) ? 2 + 1 : 0) +
	    ((rs->rs_nrates > IEEE80211_RATE_SIZE) ?
		2 + rs->rs_nrates - IEEE80211_RATE_SIZE : 0) +
	    (((ic->ic_flags & IEEE80211_F_RSNON) &&
	      (ic->ic_bss->ni_rsnprotos & IEEE80211_PROTO_RSN)) ?
		2 + IEEE80211_RSNIE_MAXLEN : 0) +
	    ((ic->ic_flags & IEEE80211_F_QOS) ? 2 + 18 : 0) +
	    (((ic->ic_flags & IEEE80211_F_RSNON) &&
	      (ic->ic_bss->ni_rsnprotos & IEEE80211_PROTO_WPA)) ?
		2 + IEEE80211_WPAIE_MAXLEN : 0) +
	    ((ic->ic_flags & IEEE80211_F_HTON) ? 28 + 24 + 26 : 0));
	if (m == NULL)
		return NULL;

	frm = mtod(m, u_int8_t *);
	memset(frm, 0, 8); frm += 8;	/* timestamp is set by hardware */
	LE_WRITE_2(frm, ic->ic_bss->ni_intval); frm += 2;
	frm = ieee80211_add_capinfo(frm, ic, ic->ic_bss);
	frm = ieee80211_add_ssid(frm, ic->ic_bss->ni_essid,
	    ic->ic_bss->ni_esslen);
	frm = ieee80211_add_rates(frm, rs);
	frm = ieee80211_add_ds_params(frm, ic, ic->ic_bss);
	if (ic->ic_opmode == IEEE80211_M_IBSS)
		frm = ieee80211_add_ibss_params(frm, ic->ic_bss);
	if (ic->ic_curmode == IEEE80211_MODE_11G)
		frm = ieee80211_add_erp(frm, ic);
	if (rs->rs_nrates > IEEE80211_RATE_SIZE)
		frm = ieee80211_add_xrates(frm, rs);
	if ((ic->ic_flags & IEEE80211_F_RSNON) &&
	    (ic->ic_bss->ni_rsnprotos & IEEE80211_PROTO_RSN))
		frm = ieee80211_add_rsn(frm, ic, ic->ic_bss);
	if (ic->ic_flags & IEEE80211_F_QOS)
		frm = ieee80211_add_edca_params(frm, ic);
	if ((ic->ic_flags & IEEE80211_F_RSNON) &&
	    (ic->ic_bss->ni_rsnprotos & IEEE80211_PROTO_WPA))
		frm = ieee80211_add_wpa(frm, ic, ic->ic_bss);
	if (ic->ic_flags & IEEE80211_F_HTON) {
		frm = ieee80211_add_htcaps(frm, ic);
		frm = ieee80211_add_htop(frm, ic);
		frm = ieee80211_add_wme_param(frm, ic);
	}

	m->m_pkthdr.len = m->m_len = frm - mtod(m, u_int8_t *);

	return m;
}
#endif	/* IEEE80211_STA_ONLY */

/*-
 * Authentication frame format:
 * [2] Authentication algorithm number
 * [2] Authentication transaction sequence number
 * [2] Status code
 */
struct mbuf *
ieee80211_get_auth(struct ieee80211com *ic, struct ieee80211_node *ni,
    u_int16_t status, u_int16_t seq)
{
	struct mbuf *m;
	u_int8_t *frm;

	MGETHDR(m, M_DONTWAIT, MT_DATA);
	if (m == NULL)
		return NULL;
	m_align(m, 2 * 3);
	m->m_pkthdr.len = m->m_len = 2 * 3;

	frm = mtod(m, u_int8_t *);
	LE_WRITE_2(frm, IEEE80211_AUTH_ALG_OPEN); frm += 2;
	LE_WRITE_2(frm, seq); frm += 2;
	LE_WRITE_2(frm, status);

	return m;
}

/*-
 * Deauthentication frame format:
 * [2] Reason code
 */
struct mbuf *
ieee80211_get_deauth(struct ieee80211com *ic, struct ieee80211_node *ni,
    u_int16_t reason)
{
	struct mbuf *m;

	MGETHDR(m, M_DONTWAIT, MT_DATA);
	if (m == NULL)
		return NULL;
	m_align(m, 2);
	m->m_pkthdr.len = m->m_len = 2;

	*mtod(m, u_int16_t *) = htole16(reason);

	return m;
}

/*-
 * (Re)Association request frame format:
 * [2]   Capability information
 * [2]   Listen interval
 * [6*]  Current AP address (Reassociation only)
 * [tlv] SSID
 * [tlv] Supported rates
 * [tlv] Extended Supported Rates (802.11g)
 * [tlv] RSN (802.11i)
 * [tlv] QoS Capability (802.11e)
 * [tlv] HT Capabilities (802.11n)
 */
struct mbuf *
ieee80211_get_assoc_req(struct ieee80211com *ic, struct ieee80211_node *ni,
    int type)
{
	const struct ieee80211_rateset *rs = &ni->ni_rates;
	struct mbuf *m;
	u_int8_t *frm;
	u_int16_t capinfo;

	m = ieee80211_getmgmt(M_DONTWAIT, MT_DATA,
	    2 + 2 +
	    ((type == IEEE80211_FC0_SUBTYPE_REASSOC_REQ) ?
		IEEE80211_ADDR_LEN : 0) +
	    2 + ni->ni_esslen +
	    2 + min(rs->rs_nrates, IEEE80211_RATE_SIZE) +
	    ((rs->rs_nrates > IEEE80211_RATE_SIZE) ?
		2 + rs->rs_nrates - IEEE80211_RATE_SIZE : 0) +
	    (((ic->ic_flags & IEEE80211_F_RSNON) &&
	      (ni->ni_rsnprotos & IEEE80211_PROTO_RSN)) ?
		2 + IEEE80211_RSNIE_MAXLEN : 0) +
	    ((ni->ni_flags & IEEE80211_NODE_QOS) ? 2 + 1 : 0) +
	    (((ic->ic_flags & IEEE80211_F_RSNON) &&
	      (ni->ni_rsnprotos & IEEE80211_PROTO_WPA)) ?
		2 + IEEE80211_WPAIE_MAXLEN : 0) +
	    ((ic->ic_flags & IEEE80211_F_HTON) ? 28 + 9 : 0) +
	    ((ic->ic_flags & IEEE80211_F_VHTON) ? 14 : 0));
	if (m == NULL)
		return NULL;

	frm = mtod(m, u_int8_t *);
	capinfo = IEEE80211_CAPINFO_ESS;
	if (ic->ic_flags & IEEE80211_F_WEPON)
		capinfo |= IEEE80211_CAPINFO_PRIVACY;
	if ((ic->ic_flags & IEEE80211_F_SHPREAMBLE) &&
	    IEEE80211_IS_CHAN_2GHZ(ni->ni_chan))
		capinfo |= IEEE80211_CAPINFO_SHORT_PREAMBLE;
	if (ic->ic_caps & IEEE80211_C_SHSLOT)
		capinfo |= IEEE80211_CAPINFO_SHORT_SLOTTIME;
	LE_WRITE_2(frm, capinfo); frm += 2;
	LE_WRITE_2(frm, ic->ic_lintval); frm += 2;
	if (type == IEEE80211_FC0_SUBTYPE_REASSOC_REQ) {
		IEEE80211_ADDR_COPY(frm, ic->ic_bss->ni_bssid);
		frm += IEEE80211_ADDR_LEN;
	}
	frm = ieee80211_add_ssid(frm, ni->ni_essid, ni->ni_esslen);
	frm = ieee80211_add_rates(frm, rs);
	if (rs->rs_nrates > IEEE80211_RATE_SIZE)
		frm = ieee80211_add_xrates(frm, rs);
	if ((ic->ic_flags & IEEE80211_F_RSNON) &&
	    (ni->ni_rsnprotos & IEEE80211_PROTO_RSN))
		frm = ieee80211_add_rsn(frm, ic, ni);
	if (ni->ni_flags & IEEE80211_NODE_QOS)
		frm = ieee80211_add_qos_capability(frm, ic);
	if ((ic->ic_flags & IEEE80211_F_RSNON) &&
	    (ni->ni_rsnprotos & IEEE80211_PROTO_WPA))
		frm = ieee80211_add_wpa(frm, ic, ni);
	if (ic->ic_flags & IEEE80211_F_HTON) {
		frm = ieee80211_add_htcaps(frm, ic);
		frm = ieee80211_add_wme_info(frm, ic);
	}
	if (ic->ic_flags & IEEE80211_F_VHTON)
		frm = ieee80211_add_vhtcaps(frm, ic);

	m->m_pkthdr.len = m->m_len = frm - mtod(m, u_int8_t *);

	return m;
}

#ifndef IEEE80211_STA_ONLY
/*-
 * (Re)Association response frame format:
 * [2]   Capability information
 * [2]   Status code
 * [2]   Association ID (AID)
 * [tlv] Supported rates
 * [tlv] Extended Supported Rates (802.11g)
 * [tlv] EDCA Parameter Set (802.11e)
 * [tlv] Timeout Interval (802.11w)
 * [tlv] HT Capabilities (802.11n)
 * [tlv] HT Operation (802.11n)
 */
struct mbuf *
ieee80211_get_assoc_resp(struct ieee80211com *ic, struct ieee80211_node *ni,
    u_int16_t status)
{
	const struct ieee80211_rateset *rs = &ni->ni_rates;
	struct mbuf *m;
	u_int8_t *frm;

	m = ieee80211_getmgmt(M_DONTWAIT, MT_DATA,
	    2 + 2 + 2 +
	    2 + min(rs->rs_nrates, IEEE80211_RATE_SIZE) +
	    ((rs->rs_nrates > IEEE80211_RATE_SIZE) ?
		2 + rs->rs_nrates - IEEE80211_RATE_SIZE : 0) +
	    ((ni->ni_flags & IEEE80211_NODE_QOS) ? 2 + 18 : 0) +
	    ((status == IEEE80211_STATUS_TRY_AGAIN_LATER) ? 2 + 7 : 0) +
	    ((ic->ic_flags & IEEE80211_F_HTON) ? 28 + 24 + 26 : 0));
	if (m == NULL)
		return NULL;

	frm = mtod(m, u_int8_t *);
	frm = ieee80211_add_capinfo(frm, ic, ni);
	LE_WRITE_2(frm, status); frm += 2;
	if (status == IEEE80211_STATUS_SUCCESS)
		LE_WRITE_2(frm, ni->ni_associd);
	else
		LE_WRITE_2(frm, 0);
	frm += 2;
	frm = ieee80211_add_rates(frm, rs);
	if (rs->rs_nrates > IEEE80211_RATE_SIZE)
		frm = ieee80211_add_xrates(frm, rs);
	if (ni->ni_flags & IEEE80211_NODE_QOS)
		frm = ieee80211_add_edca_params(frm, ic);
	if ((ni->ni_flags & IEEE80211_NODE_MFP) &&
	    status == IEEE80211_STATUS_TRY_AGAIN_LATER) {
		/* Association Comeback Time */
		frm = ieee80211_add_tie(frm, 3, 1000 /* XXX */);
	}
	if (ic->ic_flags & IEEE80211_F_HTON) {
		frm = ieee80211_add_htcaps(frm, ic);
		frm = ieee80211_add_htop(frm, ic);
		frm = ieee80211_add_wme_param(frm, ic);
	}

	m->m_pkthdr.len = m->m_len = frm - mtod(m, u_int8_t *);

	return m;
}
#endif	/* IEEE80211_STA_ONLY */

/*-
 * Disassociation frame format:
 * [2] Reason code
 */
struct mbuf *
ieee80211_get_disassoc(struct ieee80211com *ic, struct ieee80211_node *ni,
    u_int16_t reason)
{
	struct mbuf *m;

	MGETHDR(m, M_DONTWAIT, MT_DATA);
	if (m == NULL)
		return NULL;
	m_align(m, 2);
	m->m_pkthdr.len = m->m_len = 2;

	*mtod(m, u_int16_t *) = htole16(reason);

	return m;
}

/*-
 * ADDBA Request frame format:
 * [1] Category
 * [1] Action
 * [1] Dialog Token
 * [2] Block Ack Parameter Set
 * [2] Block Ack Timeout Value
 * [2] Block Ack Starting Sequence Control
 */
struct mbuf *
ieee80211_get_addba_req(struct ieee80211com *ic, struct ieee80211_node *ni,
    u_int8_t tid)
{
	struct ieee80211_tx_ba *ba = &ni->ni_tx_ba[tid];
	struct mbuf *m;
	u_int8_t *frm;

	m = ieee80211_getmgmt(M_DONTWAIT, MT_DATA, 9);
	if (m == NULL)
		return m;

	frm = mtod(m, u_int8_t *);
	*frm++ = IEEE80211_CATEG_BA;
	*frm++ = IEEE80211_ACTION_ADDBA_REQ;
	*frm++ = ba->ba_token;
	LE_WRITE_2(frm, ba->ba_params); frm += 2;
	LE_WRITE_2(frm, ba->ba_timeout_val / IEEE80211_DUR_TU); frm += 2;
	LE_WRITE_2(frm, ba->ba_winstart << IEEE80211_SEQ_SEQ_SHIFT); frm += 2;

	m->m_pkthdr.len = m->m_len = frm - mtod(m, u_int8_t *);

	return m;
}

/* Move Tx BA window forward to the specified SSN. */
void
ieee80211_output_ba_move_window(struct ieee80211com *ic,
    struct ieee80211_node *ni, uint8_t tid, uint16_t ssn)
{
	struct ieee80211_tx_ba *ba = &ni->ni_tx_ba[tid];
	uint16_t s = ba->ba_winstart;

	while (SEQ_LT(s, ssn) && ba->ba_bitmap) {
		s = (s + 1) % 0xfff;
		ba->ba_bitmap >>= 1;
	}

	ba->ba_winstart = (ssn & 0xfff);
	ba->ba_winend = (ba->ba_winstart + ba->ba_winsize - 1) & 0xfff;
}

/*-
 * ADDBA Response frame format:
 * [1] Category
 * [1] Action
 * [1] Dialog Token
 * [2] Status Code
 * [2] Block Ack Parameter Set
 * [2] Block Ack Timeout Value
 */
struct mbuf *
ieee80211_get_addba_resp(struct ieee80211com *ic, struct ieee80211_node *ni,
    u_int8_t tid, u_int8_t token, u_int16_t status)
{
	struct ieee80211_rx_ba *ba = &ni->ni_rx_ba[tid];
	struct mbuf *m;
	u_int8_t *frm;
	u_int16_t params;

	m = ieee80211_getmgmt(M_DONTWAIT, MT_DATA, 9);
	if (m == NULL)
		return m;

	frm = mtod(m, u_int8_t *);
	*frm++ = IEEE80211_CATEG_BA;
	*frm++ = IEEE80211_ACTION_ADDBA_RESP;
	*frm++ = token;
	LE_WRITE_2(frm, status); frm += 2;
	if (status == 0)
		params = ba->ba_params;
	else
		params = tid << IEEE80211_ADDBA_TID_SHIFT;
	LE_WRITE_2(frm, params); frm += 2;
	if (status == 0)
		LE_WRITE_2(frm, ba->ba_timeout_val / IEEE80211_DUR_TU);
	else
		LE_WRITE_2(frm, 0);
	frm += 2;

	m->m_pkthdr.len = m->m_len = frm - mtod(m, u_int8_t *);

	return m;
}

/*-
 * DELBA frame format:
 * [1] Category
 * [1] Action
 * [2] DELBA Parameter Set
 * [2] Reason Code
 */
struct mbuf *
ieee80211_get_delba(struct ieee80211com *ic, struct ieee80211_node *ni,
    u_int8_t tid, u_int8_t dir, u_int16_t reason)
{
	struct mbuf *m;
	u_int8_t *frm;
	u_int16_t params;

	m = ieee80211_getmgmt(M_DONTWAIT, MT_DATA, 6);
	if (m == NULL)
		return m;

	frm = mtod(m, u_int8_t *);
	*frm++ = IEEE80211_CATEG_BA;
	*frm++ = IEEE80211_ACTION_DELBA;
	params = tid << 12;
	if (dir)
		params |= IEEE80211_DELBA_INITIATOR;
	LE_WRITE_2(frm, params); frm += 2;
	LE_WRITE_2(frm, reason); frm += 2;

	m->m_pkthdr.len = m->m_len = frm - mtod(m, u_int8_t *);

	return m;
}

/*-
 * SA Query Request/Response frame format:
 * [1]  Category
 * [1]  Action
 * [16] Transaction Identifier
 */
struct mbuf *
ieee80211_get_sa_query(struct ieee80211com *ic, struct ieee80211_node *ni,
    u_int8_t action)
{
	struct mbuf *m;
	u_int8_t *frm;

	m = ieee80211_getmgmt(M_DONTWAIT, MT_DATA, 4);
	if (m == NULL)
		return NULL;

	frm = mtod(m, u_int8_t *);
	*frm++ = IEEE80211_CATEG_SA_QUERY;
	*frm++ = action;	/* ACTION_SA_QUERY_REQ/RESP */
	LE_WRITE_2(frm, ni->ni_sa_query_trid); frm += 2;

	m->m_pkthdr.len = m->m_len = frm - mtod(m, u_int8_t *);

	return m;
}

struct mbuf *
ieee80211_get_action(struct ieee80211com *ic, struct ieee80211_node *ni,
    u_int8_t categ, u_int8_t action, int arg)
{
	struct mbuf *m = NULL;

	switch (categ) {
	case IEEE80211_CATEG_BA:
		switch (action) {
		case IEEE80211_ACTION_ADDBA_REQ:
			m = ieee80211_get_addba_req(ic, ni, arg & 0xffff);
			break;
		case IEEE80211_ACTION_ADDBA_RESP:
			m = ieee80211_get_addba_resp(ic, ni, arg & 0xff,
			    arg >> 8, arg >> 16);
			break;
		case IEEE80211_ACTION_DELBA:
			m = ieee80211_get_delba(ic, ni, arg & 0xff, arg >> 8,
			    arg >> 16);
			break;
		}
		break;
	case IEEE80211_CATEG_SA_QUERY:
		switch (action) {
#ifndef IEEE80211_STA_ONLY
		case IEEE80211_ACTION_SA_QUERY_REQ:
#endif
		case IEEE80211_ACTION_SA_QUERY_RESP:
			m = ieee80211_get_sa_query(ic, ni, action);
			break;
		}
		break;
	}
	return m;
}

/*
 * Send a management frame.  The node is for the destination (or ic_bss
 * when in station mode).  Nodes other than ic_bss have their reference
 * count bumped to reflect our use for an indeterminate time.
 */
int
ieee80211_send_mgmt(struct ieee80211com *ic, struct ieee80211_node *ni,
    int type, int arg1, int arg2)
{
#define	senderr(_x, _v)	do { ic->ic_stats._v++; ret = _x; goto bad; } while (0)
	struct ifnet *ifp = &ic->ic_if;
	struct mbuf *m;
	int ret, timer;

	if (ni == NULL)
		panic("null node");

	/*
	 * Hold a reference on the node so it doesn't go away until after
	 * the xmit is complete all the way in the driver.  On error we
	 * will remove our reference.
	 */
	ieee80211_ref_node(ni);
	timer = 0;
	switch (type) {
	case IEEE80211_FC0_SUBTYPE_PROBE_REQ:
		if ((m = ieee80211_get_probe_req(ic, ni)) == NULL)
			senderr(ENOMEM, is_tx_nombuf);

		timer = IEEE80211_TRANS_WAIT;
		break;
#ifndef IEEE80211_STA_ONLY
	case IEEE80211_FC0_SUBTYPE_PROBE_RESP:
		if ((m = ieee80211_get_probe_resp(ic)) == NULL)
			senderr(ENOMEM, is_tx_nombuf);
		break;
#endif
	case IEEE80211_FC0_SUBTYPE_AUTH:
		m = ieee80211_get_auth(ic, ni, arg1 >> 16, arg1 & 0xffff);
		if (m == NULL)
			senderr(ENOMEM, is_tx_nombuf);

		if (ic->ic_opmode == IEEE80211_M_STA)
			timer = IEEE80211_TRANS_WAIT;
		break;

	case IEEE80211_FC0_SUBTYPE_DEAUTH:
		if ((m = ieee80211_get_deauth(ic, ni, arg1)) == NULL)
			senderr(ENOMEM, is_tx_nombuf);
#ifndef IEEE80211_STA_ONLY
		if ((ifp->if_flags & IFF_DEBUG) &&
		    (ic->ic_opmode == IEEE80211_M_HOSTAP ||
		    ic->ic_opmode == IEEE80211_M_IBSS))
			printf("%s: station %s deauthenticate (reason %d)\n",
			    ifp->if_xname, ether_sprintf(ni->ni_macaddr),
			    arg1);
#endif
		break;

	case IEEE80211_FC0_SUBTYPE_ASSOC_REQ:
	case IEEE80211_FC0_SUBTYPE_REASSOC_REQ:
		if ((m = ieee80211_get_assoc_req(ic, ni, type)) == NULL)
			senderr(ENOMEM, is_tx_nombuf);

		timer = IEEE80211_TRANS_WAIT;
		break;
#ifndef IEEE80211_STA_ONLY
	case IEEE80211_FC0_SUBTYPE_ASSOC_RESP:
	case IEEE80211_FC0_SUBTYPE_REASSOC_RESP:
		if ((m = ieee80211_get_assoc_resp(ic, ni, arg1)) == NULL)
			senderr(ENOMEM, is_tx_nombuf);
		break;
#endif
	case IEEE80211_FC0_SUBTYPE_DISASSOC:
		if ((m = ieee80211_get_disassoc(ic, ni, arg1)) == NULL)
			senderr(ENOMEM, is_tx_nombuf);
#ifndef IEEE80211_STA_ONLY
		if ((ifp->if_flags & IFF_DEBUG) &&
		    (ic->ic_opmode == IEEE80211_M_HOSTAP ||
		    ic->ic_opmode == IEEE80211_M_IBSS))
			printf("%s: station %s disassociate (reason %d)\n",
			    ifp->if_xname, ether_sprintf(ni->ni_macaddr),
			    arg1);
#endif
		break;

	case IEEE80211_FC0_SUBTYPE_ACTION:
		m = ieee80211_get_action(ic, ni, arg1 >> 16, arg1 & 0xffff,
		    arg2);
		if (m == NULL)
			senderr(ENOMEM, is_tx_nombuf);
		break;

	default:
		DPRINTF(("invalid mgmt frame type %u\n", type));
		senderr(EINVAL, is_tx_unknownmgt);
		/* NOTREACHED */
	}

	ret = ieee80211_mgmt_output(ifp, ni, m, type);
	if (ret == 0) {
		if (timer)
			ic->ic_mgt_timer = timer;
	} else {
bad:
		ieee80211_release_node(ic, ni);
	}
	return ret;
#undef senderr
}

/*
 * Build a RTS (Request To Send) control frame (see 7.2.1.1).
 */
struct mbuf *
ieee80211_get_rts(struct ieee80211com *ic, const struct ieee80211_frame *wh,
    u_int16_t dur)
{
	struct ieee80211_frame_rts *rts;
	struct mbuf *m;

	MGETHDR(m, M_DONTWAIT, MT_DATA);
	if (m == NULL)
		return NULL;

	m->m_pkthdr.len = m->m_len = sizeof(struct ieee80211_frame_rts);

	rts = mtod(m, struct ieee80211_frame_rts *);
	rts->i_fc[0] = IEEE80211_FC0_VERSION_0 | IEEE80211_FC0_TYPE_CTL |
	    IEEE80211_FC0_SUBTYPE_RTS;
	rts->i_fc[1] = IEEE80211_FC1_DIR_NODS;
	*(u_int16_t *)rts->i_dur = htole16(dur);
	IEEE80211_ADDR_COPY(rts->i_ra, wh->i_addr1);
	IEEE80211_ADDR_COPY(rts->i_ta, wh->i_addr2);

	return m;
}

/*
 * Build a CTS-to-self (Clear To Send) control frame (see 7.2.1.2).
 */
struct mbuf *
ieee80211_get_cts_to_self(struct ieee80211com *ic, u_int16_t dur)
{
	struct ieee80211_frame_cts *cts;
	struct mbuf *m;

	MGETHDR(m, M_DONTWAIT, MT_DATA);
	if (m == NULL)
		return NULL;

	m->m_pkthdr.len = m->m_len = sizeof(struct ieee80211_frame_cts);

	cts = mtod(m, struct ieee80211_frame_cts *);
	cts->i_fc[0] = IEEE80211_FC0_VERSION_0 | IEEE80211_FC0_TYPE_CTL |
	    IEEE80211_FC0_SUBTYPE_CTS;
	cts->i_fc[1] = IEEE80211_FC1_DIR_NODS;
	*(u_int16_t *)cts->i_dur = htole16(dur);
	IEEE80211_ADDR_COPY(cts->i_ra, ic->ic_myaddr);

	return m;
}

/*
 * Build a compressed Block Ack Request control frame.
 */
struct mbuf *
ieee80211_get_compressed_bar(struct ieee80211com *ic,
    struct ieee80211_node *ni, int tid, uint16_t ssn)
{
	struct ieee80211_frame_min *wh;
	uint8_t *frm;
	uint16_t ctl;
	struct mbuf *m;

	MGETHDR(m, M_DONTWAIT, MT_DATA);
	if (m == NULL)
		return NULL;

	m->m_pkthdr.len = m->m_len = sizeof(struct ieee80211_frame_min) +
	    sizeof(ctl) + sizeof(ssn);

	wh = mtod(m, struct ieee80211_frame_min *);
	wh->i_fc[0] = IEEE80211_FC0_VERSION_0 | IEEE80211_FC0_TYPE_CTL |
	    IEEE80211_FC0_SUBTYPE_BAR;
	wh->i_fc[1] = IEEE80211_FC1_DIR_NODS;
	*(u_int16_t *)wh->i_dur = 0;
	IEEE80211_ADDR_COPY(wh->i_addr1, ni->ni_macaddr);
	IEEE80211_ADDR_COPY(wh->i_addr2, ic->ic_myaddr);
	frm = (uint8_t *)&wh[1];

	ctl = IEEE80211_BA_COMPRESSED | (tid << IEEE80211_BA_TID_INFO_SHIFT);
	LE_WRITE_2(frm, ctl);
	frm += 2;

	LE_WRITE_2(frm, ssn << IEEE80211_SEQ_SEQ_SHIFT);
	frm += 2;

	m->m_pkthdr.len = m->m_len = frm - mtod(m, u_int8_t *);
	m->m_pkthdr.ph_cookie = ni;

	return m;
}

#ifndef IEEE80211_STA_ONLY
/*-
 * Beacon frame format:
 * [8]   Timestamp
 * [2]   Beacon interval
 * [2]   Capability
 * [tlv] Service Set Identifier (SSID)
 * [tlv] Supported rates
 * [tlv] DS Parameter Set (802.11g)
 * [tlv] IBSS Parameter Set
 * [tlv] Traffic Indication Map (TIM)
 * [tlv] ERP Information (802.11g)
 * [tlv] Extended Supported Rates (802.11g)
 * [tlv] RSN (802.11i)
 * [tlv] EDCA Parameter Set (802.11e)
 * [tlv] HT Capabilities (802.11n)
 * [tlv] HT Operation (802.11n)
 */
struct mbuf *
ieee80211_beacon_alloc(struct ieee80211com *ic, struct ieee80211_node *ni)
{
	const struct ieee80211_rateset *rs = &ni->ni_rates;
	struct ieee80211_frame *wh;
	struct mbuf *m;
	u_int8_t *frm;

	m = ieee80211_getmgmt(M_DONTWAIT, MT_DATA,
	    8 + 2 + 2 +
	    2 + ((ic->ic_userflags & IEEE80211_F_HIDENWID) ?
	    0 : ni->ni_esslen) +
	    2 + min(rs->rs_nrates, IEEE80211_RATE_SIZE) +
	    2 + 1 +
	    2 + ((ic->ic_opmode == IEEE80211_M_IBSS) ? 2 : 254) +
	    ((ic->ic_curmode == IEEE80211_MODE_11G) ? 2 + 1 : 0) +
	    ((rs->rs_nrates > IEEE80211_RATE_SIZE) ?
		2 + rs->rs_nrates - IEEE80211_RATE_SIZE : 0) +
	    (((ic->ic_flags & IEEE80211_F_RSNON) &&
	      (ni->ni_rsnprotos & IEEE80211_PROTO_RSN)) ?
		2 + IEEE80211_RSNIE_MAXLEN : 0) +
	    ((ic->ic_flags & IEEE80211_F_QOS) ? 2 + 18 : 0) +
	    (((ic->ic_flags & IEEE80211_F_RSNON) &&
	      (ni->ni_rsnprotos & IEEE80211_PROTO_WPA)) ?
		2 + IEEE80211_WPAIE_MAXLEN : 0) +
	    ((ic->ic_flags & IEEE80211_F_HTON) ? 28 + 24 + 26 : 0));
	if (m == NULL)
		return NULL;

	M_PREPEND(m, sizeof(struct ieee80211_frame), M_DONTWAIT);
	if (m == NULL)
		return NULL;
	wh = mtod(m, struct ieee80211_frame *);
	wh->i_fc[0] = IEEE80211_FC0_VERSION_0 | IEEE80211_FC0_TYPE_MGT |
	    IEEE80211_FC0_SUBTYPE_BEACON;
	wh->i_fc[1] = IEEE80211_FC1_DIR_NODS;
	*(u_int16_t *)wh->i_dur = 0;
	IEEE80211_ADDR_COPY(wh->i_addr1, etherbroadcastaddr);
	IEEE80211_ADDR_COPY(wh->i_addr2, ic->ic_myaddr);
	IEEE80211_ADDR_COPY(wh->i_addr3, ni->ni_bssid);
	*(u_int16_t *)wh->i_seq = 0;

	frm = (u_int8_t *)&wh[1];
	memset(frm, 0, 8); frm += 8;	/* timestamp is set by hardware */
	LE_WRITE_2(frm, ni->ni_intval); frm += 2;
	frm = ieee80211_add_capinfo(frm, ic, ni);
	if (ic->ic_userflags & IEEE80211_F_HIDENWID)
		frm = ieee80211_add_ssid(frm, NULL, 0);
	else
		frm = ieee80211_add_ssid(frm, ni->ni_essid, ni->ni_esslen);
	frm = ieee80211_add_rates(frm, rs);
	frm = ieee80211_add_ds_params(frm, ic, ni);
	if (ic->ic_opmode == IEEE80211_M_IBSS)
		frm = ieee80211_add_ibss_params(frm, ni);
	else
		frm = ieee80211_add_tim(frm, ic);
	if (ic->ic_curmode == IEEE80211_MODE_11G)
		frm = ieee80211_add_erp(frm, ic);
	if (rs->rs_nrates > IEEE80211_RATE_SIZE)
		frm = ieee80211_add_xrates(frm, rs);
	if ((ic->ic_flags & IEEE80211_F_RSNON) &&
	    (ni->ni_rsnprotos & IEEE80211_PROTO_RSN))
		frm = ieee80211_add_rsn(frm, ic, ni);
	if (ic->ic_flags & IEEE80211_F_QOS)
		frm = ieee80211_add_edca_params(frm, ic);
	if ((ic->ic_flags & IEEE80211_F_RSNON) &&
	    (ni->ni_rsnprotos & IEEE80211_PROTO_WPA))
		frm = ieee80211_add_wpa(frm, ic, ni);
	if (ic->ic_flags & IEEE80211_F_HTON) {
		frm = ieee80211_add_htcaps(frm, ic);
		frm = ieee80211_add_htop(frm, ic);
		frm = ieee80211_add_wme_param(frm, ic);
	}

	m->m_pkthdr.len = m->m_len = frm - mtod(m, u_int8_t *);
	m->m_pkthdr.ph_cookie = ni;

	return m;
}

/*
 * Check if an outgoing MSDU or management frame should be buffered into
 * the AP for power management.  Return 1 if the frame was buffered into
 * the AP, or 0 if the frame shall be transmitted immediately.
 */
int
ieee80211_pwrsave(struct ieee80211com *ic, struct mbuf *m,
    struct ieee80211_node *ni)
{
	const struct ieee80211_frame *wh;
	int pssta = 0;

	KASSERT(ic->ic_opmode == IEEE80211_M_HOSTAP);
	if (!(ic->ic_caps & IEEE80211_C_APPMGT))
		return 0;

	wh = mtod(m, struct ieee80211_frame *);
	if (IEEE80211_IS_MULTICAST(wh->i_addr1)) {
		/*
		 * Buffer group addressed MSDUs with the Order bit clear
		 * if any associated STAs are in PS mode.
		 */
		ieee80211_iterate_nodes(ic, ieee80211_count_pssta, &pssta);
		if ((wh->i_fc[1] & IEEE80211_FC1_ORDER) || pssta == 0)
			return 0;
		ic->ic_tim_mcast_pending = 1;
	} else {
		/*
		 * Buffer MSDUs, A-MSDUs or management frames destined for
		 * PS STAs.
		 */
		if (ni->ni_pwrsave == IEEE80211_PS_AWAKE ||
		    (wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) ==
		    IEEE80211_FC0_TYPE_CTL)
			return 0;
		if (mq_empty(&ni->ni_savedq))
			(*ic->ic_set_tim)(ic, ni->ni_associd, 1);
	}
	/* NB: ni == ic->ic_bss for broadcast/multicast */
	/*
	 * Similar to ieee80211_mgmt_output, store the node in a
	 * special pkthdr field.
	 */
	m->m_pkthdr.ph_cookie = ni;
	mq_enqueue(&ni->ni_savedq, m);
	return 1;
}
#endif	/* IEEE80211_STA_ONLY */
