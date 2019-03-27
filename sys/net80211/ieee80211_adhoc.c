/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007-2009 Sam Leffler, Errno Consulting
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
 * IEEE 802.11 IBSS mode support.
 */
#include "opt_inet.h"
#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/systm.h> 
#include <sys/mbuf.h>   
#include <sys/malloc.h>
#include <sys/kernel.h>

#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/endian.h>
#include <sys/errno.h>
#include <sys/proc.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_media.h>
#include <net/if_llc.h>
#include <net/ethernet.h>

#include <net/bpf.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_adhoc.h>
#include <net80211/ieee80211_input.h>
#ifdef IEEE80211_SUPPORT_SUPERG
#include <net80211/ieee80211_superg.h>
#endif
#ifdef IEEE80211_SUPPORT_TDMA
#include <net80211/ieee80211_tdma.h>
#endif
#include <net80211/ieee80211_sta.h>

#define	IEEE80211_RATE2MBS(r)	(((r) & IEEE80211_RATE_VAL) / 2)

static	void adhoc_vattach(struct ieee80211vap *);
static	int adhoc_newstate(struct ieee80211vap *, enum ieee80211_state, int);
static int adhoc_input(struct ieee80211_node *, struct mbuf *,
	    const struct ieee80211_rx_stats *, int, int);
static void adhoc_recv_mgmt(struct ieee80211_node *, struct mbuf *,
	int subtype, const struct ieee80211_rx_stats *, int, int);
static void ahdemo_recv_mgmt(struct ieee80211_node *, struct mbuf *,
	    int subtype, const struct ieee80211_rx_stats *rxs, int, int);
static void adhoc_recv_ctl(struct ieee80211_node *, struct mbuf *, int subtype);

void
ieee80211_adhoc_attach(struct ieee80211com *ic)
{
	ic->ic_vattach[IEEE80211_M_IBSS] = adhoc_vattach;
	ic->ic_vattach[IEEE80211_M_AHDEMO] = adhoc_vattach;
}

void
ieee80211_adhoc_detach(struct ieee80211com *ic)
{
}

static void
adhoc_vdetach(struct ieee80211vap *vap)
{
}

static void
adhoc_vattach(struct ieee80211vap *vap)
{
	vap->iv_newstate = adhoc_newstate;
	vap->iv_input = adhoc_input;
	if (vap->iv_opmode == IEEE80211_M_IBSS)
		vap->iv_recv_mgmt = adhoc_recv_mgmt;
	else
		vap->iv_recv_mgmt = ahdemo_recv_mgmt;
	vap->iv_recv_ctl = adhoc_recv_ctl;
	vap->iv_opdetach = adhoc_vdetach;
#ifdef IEEE80211_SUPPORT_TDMA
	/*
	 * Throw control to tdma support.  Note we do this
	 * after setting up our callbacks so it can piggyback
	 * on top of us.
	 */
	if (vap->iv_caps & IEEE80211_C_TDMA)
		ieee80211_tdma_vattach(vap);
#endif
}

static void
sta_leave(void *arg, struct ieee80211_node *ni)
{
	struct ieee80211vap *vap = ni->ni_vap;

	if (ni != vap->iv_bss)
		ieee80211_node_leave(ni);
}

/*
 * IEEE80211_M_IBSS+IEEE80211_M_AHDEMO vap state machine handler.
 */
static int
adhoc_newstate(struct ieee80211vap *vap, enum ieee80211_state nstate, int arg)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_node *ni;
	enum ieee80211_state ostate;

	IEEE80211_LOCK_ASSERT(vap->iv_ic);

	ostate = vap->iv_state;
	IEEE80211_DPRINTF(vap, IEEE80211_MSG_STATE, "%s: %s -> %s (%d)\n",
	    __func__, ieee80211_state_name[ostate],
	    ieee80211_state_name[nstate], arg);
	vap->iv_state = nstate;			/* state transition */
	if (ostate != IEEE80211_S_SCAN)
		ieee80211_cancel_scan(vap);	/* background scan */
	ni = vap->iv_bss;			/* NB: no reference held */
	switch (nstate) {
	case IEEE80211_S_INIT:
		switch (ostate) {
		case IEEE80211_S_SCAN:
			ieee80211_cancel_scan(vap);
			break;
		default:
			break;
		}
		if (ostate != IEEE80211_S_INIT) {
			/* NB: optimize INIT -> INIT case */
			ieee80211_reset_bss(vap);
		}
		break;
	case IEEE80211_S_SCAN:
		switch (ostate) {
		case IEEE80211_S_RUN:		/* beacon miss */
			/* purge station table; entries are stale */
			ieee80211_iterate_nodes_vap(&ic->ic_sta, vap,
			    sta_leave, NULL);
			/* fall thru... */
		case IEEE80211_S_INIT:
			if (vap->iv_des_chan != IEEE80211_CHAN_ANYC &&
			    !IEEE80211_IS_CHAN_RADAR(vap->iv_des_chan)) {
				/*
				 * Already have a channel; bypass the
				 * scan and startup immediately.
				 */
				ieee80211_create_ibss(vap,
				    ieee80211_ht_adjust_channel(ic,
				    vap->iv_des_chan, vap->iv_flags_ht));
				break;
			}
			/*
			 * Initiate a scan.  We can come here as a result
			 * of an IEEE80211_IOC_SCAN_REQ too in which case
			 * the vap will be marked with IEEE80211_FEXT_SCANREQ
			 * and the scan request parameters will be present
			 * in iv_scanreq.  Otherwise we do the default.
			 */
			if (vap->iv_flags_ext & IEEE80211_FEXT_SCANREQ) {
				ieee80211_check_scan(vap,
				    vap->iv_scanreq_flags,
				    vap->iv_scanreq_duration,
				    vap->iv_scanreq_mindwell,
				    vap->iv_scanreq_maxdwell,
				    vap->iv_scanreq_nssid, vap->iv_scanreq_ssid);
				vap->iv_flags_ext &= ~IEEE80211_FEXT_SCANREQ;
			} else
				ieee80211_check_scan_current(vap);
			break;
		case IEEE80211_S_SCAN:
			/*
			 * This can happen because of a change in state
			 * that requires a reset.  Trigger a new scan
			 * unless we're in manual roaming mode in which
			 * case an application must issue an explicit request.
			 */
			if (vap->iv_roaming == IEEE80211_ROAMING_AUTO)
				ieee80211_check_scan_current(vap);
			break;
		default:
			goto invalid;
		}
		break;
	case IEEE80211_S_RUN:
		if (vap->iv_flags & IEEE80211_F_WPA) {
			/* XXX validate prerequisites */
		}
		switch (ostate) {
		case IEEE80211_S_INIT:
			/*
			 * Already have a channel; bypass the
			 * scan and startup immediately.
			 * Note that ieee80211_create_ibss will call
			 * back to do a RUN->RUN state change.
			 */
			ieee80211_create_ibss(vap,
			    ieee80211_ht_adjust_channel(ic,
				ic->ic_curchan, vap->iv_flags_ht));
			/* NB: iv_bss is changed on return */
			ni = vap->iv_bss;
			break;
		case IEEE80211_S_SCAN:
#ifdef IEEE80211_DEBUG
			if (ieee80211_msg_debug(vap)) {
				ieee80211_note(vap,
				    "synchronized with %s ssid ",
				    ether_sprintf(ni->ni_bssid));
				ieee80211_print_essid(vap->iv_bss->ni_essid,
				    ni->ni_esslen);
				/* XXX MCS/HT */
				printf(" channel %d start %uMb\n",
				    ieee80211_chan2ieee(ic, ic->ic_curchan),
				    IEEE80211_RATE2MBS(ni->ni_txrate));
			}
#endif
			break;
		case IEEE80211_S_RUN:	/* IBSS merge */
			break;
		default:
			goto invalid;
		}
		/*
		 * When 802.1x is not in use mark the port authorized
		 * at this point so traffic can flow.
		 */
		if (ni->ni_authmode != IEEE80211_AUTH_8021X)
			ieee80211_node_authorize(ni);
		/*
		 * Fake association when joining an existing bss.
		 */
		if (!IEEE80211_ADDR_EQ(ni->ni_macaddr, vap->iv_myaddr) &&
		    ic->ic_newassoc != NULL)
			ic->ic_newassoc(ni, ostate != IEEE80211_S_RUN);
		break;
	case IEEE80211_S_SLEEP:
		vap->iv_sta_ps(vap, 0);
		break;
	default:
	invalid:
		IEEE80211_DPRINTF(vap, IEEE80211_MSG_STATE,
		    "%s: unexpected state transition %s -> %s\n", __func__,
		    ieee80211_state_name[ostate], ieee80211_state_name[nstate]);
		break;
	}
	return 0;
}

/*
 * Decide if a received management frame should be
 * printed when debugging is enabled.  This filters some
 * of the less interesting frames that come frequently
 * (e.g. beacons).
 */
static __inline int
doprint(struct ieee80211vap *vap, int subtype)
{
	switch (subtype) {
	case IEEE80211_FC0_SUBTYPE_BEACON:
		return (vap->iv_ic->ic_flags & IEEE80211_F_SCAN);
	case IEEE80211_FC0_SUBTYPE_PROBE_REQ:
		return 1;
	}
	return 1;
}

/*
 * Process a received frame.  The node associated with the sender
 * should be supplied.  If nothing was found in the node table then
 * the caller is assumed to supply a reference to iv_bss instead.
 * The RSSI and a timestamp are also supplied.  The RSSI data is used
 * during AP scanning to select a AP to associate with; it can have
 * any units so long as values have consistent units and higher values
 * mean ``better signal''.  The receive timestamp is currently not used
 * by the 802.11 layer.
 */
static int
adhoc_input(struct ieee80211_node *ni, struct mbuf *m,
    const struct ieee80211_rx_stats *rxs, int rssi, int nf)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211com *ic = ni->ni_ic;
	struct ifnet *ifp = vap->iv_ifp;
	struct ieee80211_frame *wh;
	struct ieee80211_key *key;
	struct ether_header *eh;
	int hdrspace, need_tap = 1;	/* mbuf need to be tapped. */	
	uint8_t dir, type, subtype, qos;
	uint8_t *bssid;
	int is_hw_decrypted = 0;
	int has_decrypted = 0;

	/*
	 * Some devices do hardware decryption all the way through
	 * to pretending the frame wasn't encrypted in the first place.
	 * So, tag it appropriately so it isn't discarded inappropriately.
	 */
	if ((rxs != NULL) && (rxs->c_pktflags & IEEE80211_RX_F_DECRYPTED))
		is_hw_decrypted = 1;

	if (m->m_flags & M_AMPDU_MPDU) {
		/*
		 * Fastpath for A-MPDU reorder q resubmission.  Frames
		 * w/ M_AMPDU_MPDU marked have already passed through
		 * here but were received out of order and been held on
		 * the reorder queue.  When resubmitted they are marked
		 * with the M_AMPDU_MPDU flag and we can bypass most of
		 * the normal processing.
		 */
		wh = mtod(m, struct ieee80211_frame *);
		type = IEEE80211_FC0_TYPE_DATA;
		dir = wh->i_fc[1] & IEEE80211_FC1_DIR_MASK;
		subtype = IEEE80211_FC0_SUBTYPE_QOS;
		hdrspace = ieee80211_hdrspace(ic, wh);	/* XXX optimize? */
		goto resubmit_ampdu;
	}

	KASSERT(ni != NULL, ("null node"));
	ni->ni_inact = ni->ni_inact_reload;

	type = -1;			/* undefined */

	if (m->m_pkthdr.len < sizeof(struct ieee80211_frame_min)) {
		IEEE80211_DISCARD_MAC(vap, IEEE80211_MSG_ANY,
		    ni->ni_macaddr, NULL,
		    "too short (1): len %u", m->m_pkthdr.len);
		vap->iv_stats.is_rx_tooshort++;
		goto out;
	}
	/*
	 * Bit of a cheat here, we use a pointer for a 3-address
	 * frame format but don't reference fields past outside
	 * ieee80211_frame_min w/o first validating the data is
	 * present.
	 */
	wh = mtod(m, struct ieee80211_frame *);

	if ((wh->i_fc[0] & IEEE80211_FC0_VERSION_MASK) !=
	    IEEE80211_FC0_VERSION_0) {
		IEEE80211_DISCARD_MAC(vap, IEEE80211_MSG_ANY,
		    ni->ni_macaddr, NULL, "wrong version, fc %02x:%02x",
		    wh->i_fc[0], wh->i_fc[1]);
		vap->iv_stats.is_rx_badversion++;
		goto err;
	}

	dir = wh->i_fc[1] & IEEE80211_FC1_DIR_MASK;
	type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;
	subtype = wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;
	if ((ic->ic_flags & IEEE80211_F_SCAN) == 0) {
		if (dir != IEEE80211_FC1_DIR_NODS)
			bssid = wh->i_addr1;
		else if (type == IEEE80211_FC0_TYPE_CTL)
			bssid = wh->i_addr1;
		else {
			if (m->m_pkthdr.len < sizeof(struct ieee80211_frame)) {
				IEEE80211_DISCARD_MAC(vap,
				    IEEE80211_MSG_ANY, ni->ni_macaddr,
				    NULL, "too short (2): len %u",
				    m->m_pkthdr.len);
				vap->iv_stats.is_rx_tooshort++;
				goto out;
			}
			bssid = wh->i_addr3;
		}
		/*
		 * Validate the bssid.
		 */
		if (!(type == IEEE80211_FC0_TYPE_MGT &&
		     (subtype == IEEE80211_FC0_SUBTYPE_BEACON ||
		      subtype == IEEE80211_FC0_SUBTYPE_PROBE_RESP)) &&
		    !IEEE80211_ADDR_EQ(bssid, vap->iv_bss->ni_bssid) &&
		    !IEEE80211_ADDR_EQ(bssid, ifp->if_broadcastaddr)) {
			/* not interested in */
			IEEE80211_DISCARD_MAC(vap, IEEE80211_MSG_INPUT,
			    bssid, NULL, "%s", "not to bss");
			vap->iv_stats.is_rx_wrongbss++;
			goto out;
		}
		/*
		 * Data frame, cons up a node when it doesn't
		 * exist. This should probably done after an ACL check.
		 */
		if (type == IEEE80211_FC0_TYPE_DATA &&
		    ni == vap->iv_bss &&
		    !IEEE80211_ADDR_EQ(wh->i_addr2, ni->ni_macaddr)) {
			/*
			 * Beware of frames that come in too early; we
			 * can receive broadcast frames and creating sta
			 * entries will blow up because there is no bss
			 * channel yet.
			 */
			if (vap->iv_state != IEEE80211_S_RUN) {
				IEEE80211_DISCARD(vap, IEEE80211_MSG_INPUT,
				    wh, "data", "not in RUN state (%s)",
				    ieee80211_state_name[vap->iv_state]);
				vap->iv_stats.is_rx_badstate++;
				goto err;
			}
			/*
			 * Fake up a node for this newly discovered member
			 * of the IBSS.
			 *
			 * Note: This doesn't "upgrade" the node to 11n;
			 * that will happen after a probe request/response
			 * exchange.
			 */
			ni = ieee80211_fakeup_adhoc_node(vap, wh->i_addr2);
			if (ni == NULL) {
				/* NB: stat kept for alloc failure */
				goto err;
			}
		}
		IEEE80211_RSSI_LPF(ni->ni_avgrssi, rssi);
		ni->ni_noise = nf;
		if (IEEE80211_HAS_SEQ(type, subtype) &&
		    IEEE80211_ADDR_EQ(wh->i_addr2, ni->ni_macaddr)) {
			uint8_t tid = ieee80211_gettid(wh);
			if (IEEE80211_QOS_HAS_SEQ(wh) &&
			    TID_TO_WME_AC(tid) >= WME_AC_VI)
				ic->ic_wme.wme_hipri_traffic++;
			if (! ieee80211_check_rxseq(ni, wh, bssid, rxs))
				goto out;
		}
	}

	switch (type) {
	case IEEE80211_FC0_TYPE_DATA:
		hdrspace = ieee80211_hdrspace(ic, wh);
		if (m->m_len < hdrspace &&
		    (m = m_pullup(m, hdrspace)) == NULL) {
			IEEE80211_DISCARD_MAC(vap, IEEE80211_MSG_ANY,
			    ni->ni_macaddr, NULL,
			    "data too short: expecting %u", hdrspace);
			vap->iv_stats.is_rx_tooshort++;
			goto out;		/* XXX */
		}
		if (dir != IEEE80211_FC1_DIR_NODS) {
			IEEE80211_DISCARD(vap, IEEE80211_MSG_INPUT,
			    wh, "data", "incorrect dir 0x%x", dir);
			vap->iv_stats.is_rx_wrongdir++;
			goto out;
		}
		/* XXX no power-save support */

		/*
		 * Handle A-MPDU re-ordering.  If the frame is to be
		 * processed directly then ieee80211_ampdu_reorder
		 * will return 0; otherwise it has consumed the mbuf
		 * and we should do nothing more with it.
		 */
		if ((m->m_flags & M_AMPDU) &&
		    ieee80211_ampdu_reorder(ni, m, rxs) != 0) {
			m = NULL;
			goto out;
		}
	resubmit_ampdu:

		/*
		 * Handle privacy requirements.  Note that we
		 * must not be preempted from here until after
		 * we (potentially) call ieee80211_crypto_demic;
		 * otherwise we may violate assumptions in the
		 * crypto cipher modules used to do delayed update
		 * of replay sequence numbers.
		 */
		if (is_hw_decrypted || wh->i_fc[1] & IEEE80211_FC1_PROTECTED) {
			if ((vap->iv_flags & IEEE80211_F_PRIVACY) == 0) {
				/*
				 * Discard encrypted frames when privacy is off.
				 */
				IEEE80211_DISCARD(vap, IEEE80211_MSG_INPUT,
				    wh, "WEP", "%s", "PRIVACY off");
				vap->iv_stats.is_rx_noprivacy++;
				IEEE80211_NODE_STAT(ni, rx_noprivacy);
				goto out;
			}
			if (ieee80211_crypto_decap(ni, m, hdrspace, &key) == 0) {
				/* NB: stats+msgs handled in crypto_decap */
				IEEE80211_NODE_STAT(ni, rx_wepfail);
				goto out;
			}
			wh = mtod(m, struct ieee80211_frame *);
			wh->i_fc[1] &= ~IEEE80211_FC1_PROTECTED;
			has_decrypted = 1;
		} else {
			/* XXX M_WEP and IEEE80211_F_PRIVACY */
			key = NULL;
		}

		/*
		 * Save QoS bits for use below--before we strip the header.
		 */
		if (subtype == IEEE80211_FC0_SUBTYPE_QOS)
			qos = ieee80211_getqos(wh)[0];
		else
			qos = 0;

		/*
		 * Next up, any fragmentation.
		 */
		if (!IEEE80211_IS_MULTICAST(wh->i_addr1)) {
			m = ieee80211_defrag(ni, m, hdrspace);
			if (m == NULL) {
				/* Fragment dropped or frame not complete yet */
				goto out;
			}
		}
		wh = NULL;		/* no longer valid, catch any uses */

		/*
		 * Next strip any MSDU crypto bits.
		 */
		if (!ieee80211_crypto_demic(vap, key, m, 0)) {
			IEEE80211_DISCARD_MAC(vap, IEEE80211_MSG_INPUT,
			    ni->ni_macaddr, "data", "%s", "demic error");
			vap->iv_stats.is_rx_demicfail++;
			IEEE80211_NODE_STAT(ni, rx_demicfail);
			goto out;
		}

		/* copy to listener after decrypt */
		if (ieee80211_radiotap_active_vap(vap))
			ieee80211_radiotap_rx(vap, m);
		need_tap = 0;

		/*
		 * Finally, strip the 802.11 header.
		 */
		m = ieee80211_decap(vap, m, hdrspace);
		if (m == NULL) {
			/* XXX mask bit to check for both */
			/* don't count Null data frames as errors */
			if (subtype == IEEE80211_FC0_SUBTYPE_NODATA ||
			    subtype == IEEE80211_FC0_SUBTYPE_QOS_NULL)
				goto out;
			IEEE80211_DISCARD_MAC(vap, IEEE80211_MSG_INPUT,
			    ni->ni_macaddr, "data", "%s", "decap error");
			vap->iv_stats.is_rx_decap++;
			IEEE80211_NODE_STAT(ni, rx_decap);
			goto err;
		}
		eh = mtod(m, struct ether_header *);
		if (!ieee80211_node_is_authorized(ni)) {
			/*
			 * Deny any non-PAE frames received prior to
			 * authorization.  For open/shared-key
			 * authentication the port is mark authorized
			 * after authentication completes.  For 802.1x
			 * the port is not marked authorized by the
			 * authenticator until the handshake has completed.
			 */
			if (eh->ether_type != htons(ETHERTYPE_PAE)) {
				IEEE80211_DISCARD_MAC(vap, IEEE80211_MSG_INPUT,
				    eh->ether_shost, "data",
				    "unauthorized port: ether type 0x%x len %u",
				    eh->ether_type, m->m_pkthdr.len);
				vap->iv_stats.is_rx_unauth++;
				IEEE80211_NODE_STAT(ni, rx_unauth);
				goto err;
			}
		} else {
			/*
			 * When denying unencrypted frames, discard
			 * any non-PAE frames received without encryption.
			 */
			if ((vap->iv_flags & IEEE80211_F_DROPUNENC) &&
			    ((has_decrypted == 0) && (m->m_flags & M_WEP) == 0) &&
			    (is_hw_decrypted == 0) &&
			    eh->ether_type != htons(ETHERTYPE_PAE)) {
				/*
				 * Drop unencrypted frames.
				 */
				vap->iv_stats.is_rx_unencrypted++;
				IEEE80211_NODE_STAT(ni, rx_unencrypted);
				goto out;
			}
		}
		/* XXX require HT? */
		if (qos & IEEE80211_QOS_AMSDU) {
			m = ieee80211_decap_amsdu(ni, m);
			if (m == NULL)
				return IEEE80211_FC0_TYPE_DATA;
		} else {
#ifdef IEEE80211_SUPPORT_SUPERG
			m = ieee80211_decap_fastframe(vap, ni, m);
			if (m == NULL)
				return IEEE80211_FC0_TYPE_DATA;
#endif
		}
		if (dir == IEEE80211_FC1_DIR_DSTODS && ni->ni_wdsvap != NULL)
			ieee80211_deliver_data(ni->ni_wdsvap, ni, m);
		else
			ieee80211_deliver_data(vap, ni, m);
		return IEEE80211_FC0_TYPE_DATA;

	case IEEE80211_FC0_TYPE_MGT:
		vap->iv_stats.is_rx_mgmt++;
		IEEE80211_NODE_STAT(ni, rx_mgmt);
		if (dir != IEEE80211_FC1_DIR_NODS) {
			IEEE80211_DISCARD(vap, IEEE80211_MSG_INPUT,
			    wh, "data", "incorrect dir 0x%x", dir);
			vap->iv_stats.is_rx_wrongdir++;
			goto err;
		}
		if (m->m_pkthdr.len < sizeof(struct ieee80211_frame)) {
			IEEE80211_DISCARD_MAC(vap, IEEE80211_MSG_ANY,
			    ni->ni_macaddr, "mgt", "too short: len %u",
			    m->m_pkthdr.len);
			vap->iv_stats.is_rx_tooshort++;
			goto out;
		}
#ifdef IEEE80211_DEBUG
		if ((ieee80211_msg_debug(vap) && doprint(vap, subtype)) ||
		    ieee80211_msg_dumppkts(vap)) {
			if_printf(ifp, "received %s from %s rssi %d\n",
			    ieee80211_mgt_subtype_name(subtype),
			    ether_sprintf(wh->i_addr2), rssi);
		}
#endif
		if (wh->i_fc[1] & IEEE80211_FC1_PROTECTED) {
			IEEE80211_DISCARD(vap, IEEE80211_MSG_INPUT,
			    wh, NULL, "%s", "WEP set but not permitted");
			vap->iv_stats.is_rx_mgtdiscard++; /* XXX */
			goto out;
		}
		vap->iv_recv_mgmt(ni, m, subtype, rxs, rssi, nf);
		goto out;

	case IEEE80211_FC0_TYPE_CTL:
		vap->iv_stats.is_rx_ctl++;
		IEEE80211_NODE_STAT(ni, rx_ctrl);
		vap->iv_recv_ctl(ni, m, subtype);
		goto out;

	default:
		IEEE80211_DISCARD(vap, IEEE80211_MSG_ANY,
		    wh, "bad", "frame type 0x%x", type);
		/* should not come here */
		break;
	}
err:
	if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
out:
	if (m != NULL) {
		if (need_tap && ieee80211_radiotap_active_vap(vap))
			ieee80211_radiotap_rx(vap, m);
		m_freem(m);
	}
	return type;
}

static int
is11bclient(const uint8_t *rates, const uint8_t *xrates)
{
	static const uint32_t brates = (1<<2*1)|(1<<2*2)|(1<<11)|(1<<2*11);
	int i;

	/* NB: the 11b clients we care about will not have xrates */
	if (xrates != NULL || rates == NULL)
		return 0;
	for (i = 0; i < rates[1]; i++) {
		int r = rates[2+i] & IEEE80211_RATE_VAL;
		if (r > 2*11 || ((1<<r) & brates) == 0)
			return 0;
	}
	return 1;
}

static void
adhoc_recv_mgmt(struct ieee80211_node *ni, struct mbuf *m0,
	int subtype, const struct ieee80211_rx_stats *rxs, int rssi, int nf)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211com *ic = ni->ni_ic;
	struct ieee80211_channel *rxchan = ic->ic_curchan;
	struct ieee80211_frame *wh;
	uint8_t *frm, *efrm;
	uint8_t *ssid, *rates, *xrates;
#if 0
	int ht_state_change = 0;
#endif

	wh = mtod(m0, struct ieee80211_frame *);
	frm = (uint8_t *)&wh[1];
	efrm = mtod(m0, uint8_t *) + m0->m_len;
	switch (subtype) {
	case IEEE80211_FC0_SUBTYPE_PROBE_RESP:
	case IEEE80211_FC0_SUBTYPE_BEACON: {
		struct ieee80211_scanparams scan;
		struct ieee80211_channel *c;
		/*
		 * We process beacon/probe response
		 * frames to discover neighbors.
		 */ 
		if (rxs != NULL) {
			c = ieee80211_lookup_channel_rxstatus(vap, rxs);
			if (c != NULL)
				rxchan = c;
		}
		if (ieee80211_parse_beacon(ni, m0, rxchan, &scan) != 0)
			return;
		/*
		 * Count frame now that we know it's to be processed.
		 */
		if (subtype == IEEE80211_FC0_SUBTYPE_BEACON) {
			vap->iv_stats.is_rx_beacon++;		/* XXX remove */
			IEEE80211_NODE_STAT(ni, rx_beacons);
		} else
			IEEE80211_NODE_STAT(ni, rx_proberesp);
		/*
		 * If scanning, just pass information to the scan module.
		 */
		if (ic->ic_flags & IEEE80211_F_SCAN) {
			if (ic->ic_flags_ext & IEEE80211_FEXT_PROBECHAN) {
				/*
				 * Actively scanning a channel marked passive;
				 * send a probe request now that we know there
				 * is 802.11 traffic present.
				 *
				 * XXX check if the beacon we recv'd gives
				 * us what we need and suppress the probe req
				 */
				ieee80211_probe_curchan(vap, 1);
				ic->ic_flags_ext &= ~IEEE80211_FEXT_PROBECHAN;
			}
			ieee80211_add_scan(vap, rxchan, &scan, wh,
			    subtype, rssi, nf);
			return;
		}
		if (scan.capinfo & IEEE80211_CAPINFO_IBSS) {
			if (!IEEE80211_ADDR_EQ(wh->i_addr2, ni->ni_macaddr)) {
				/*
				 * Create a new entry in the neighbor table.
				 *
				 * XXX TODO:
				 *
				 * Here we're not scanning; so if we have an
				 * SSID then make sure it matches our SSID.
				 * Otherwise this code will match on all IBSS
				 * beacons/probe requests for all SSIDs,
				 * filling the node table with nodes that
				 * aren't ours.
				 */
				if (ieee80211_ibss_node_check_new(ni, &scan)) {
					ni = ieee80211_add_neighbor(vap, wh, &scan);
					/*
					 * Send a probe request so we announce 11n
					 * capabilities.
					 */
					ieee80211_send_probereq(ni, /* node */
					    vap->iv_myaddr, /* SA */
					    ni->ni_macaddr, /* DA */
					    vap->iv_bss->ni_bssid, /* BSSID */
					    vap->iv_bss->ni_essid,
					    vap->iv_bss->ni_esslen); /* SSID */
				} else
					ni = NULL;

			} else if (ni->ni_capinfo == 0) {
				/*
				 * Update faked node created on transmit.
				 * Note this also updates the tsf.
				 */
				ieee80211_init_neighbor(ni, wh, &scan);

				/*
				 * Send a probe request so we announce 11n
				 * capabilities.
				 */
				ieee80211_send_probereq(ni, /* node */
					vap->iv_myaddr, /* SA */
					ni->ni_macaddr, /* DA */
					vap->iv_bss->ni_bssid, /* BSSID */
					vap->iv_bss->ni_essid,
					vap->iv_bss->ni_esslen); /* SSID */
			} else {
				/*
				 * Record tsf for potential resync.
				 */
				memcpy(ni->ni_tstamp.data, scan.tstamp,
					sizeof(ni->ni_tstamp));
			}
			/*
			 * This isn't enabled yet - otherwise it would
			 * update the HT parameters and channel width
			 * from any node, which could lead to lots of
			 * strange behaviour if the 11n nodes aren't
			 * exactly configured to match.
			 */
#if 0
			if (scan.htcap != NULL && scan.htinfo != NULL &&
			    (vap->iv_flags_ht & IEEE80211_FHT_HT)) {
				ieee80211_ht_updateparams(ni,
				    scan.htcap, scan.htinfo));
				if (ieee80211_ht_updateparams_final(ni,
				    scan.htcap, scan.htinfo))
					ht_state_change = 1;
			}

			/* XXX same for VHT? */
#endif
			if (ni != NULL) {
				IEEE80211_RSSI_LPF(ni->ni_avgrssi, rssi);
				ni->ni_noise = nf;
			}
			/*
			 * Same here - the channel width change should
			 * be applied to the specific peer node, not
			 * to the ic.  Ie, the interface configuration
			 * should stay in its current channel width;
			 * but it should change the rate control and
			 * any queued frames for the given node only.
			 *
			 * Since there's no (current) way to inform
			 * the driver that a channel width change has
			 * occurred for a single node, just stub this
			 * out.
			 */
#if 0
			if (ht_state_change)
				ieee80211_update_chw(ic);
#endif
		}
		break;
	}

	case IEEE80211_FC0_SUBTYPE_PROBE_REQ:
		if (vap->iv_state != IEEE80211_S_RUN) {
			IEEE80211_DISCARD(vap, IEEE80211_MSG_INPUT,
			    wh, NULL, "wrong state %s",
			    ieee80211_state_name[vap->iv_state]);
			vap->iv_stats.is_rx_mgtdiscard++;
			return;
		}
		if (IEEE80211_IS_MULTICAST(wh->i_addr2)) {
			/* frame must be directed */
			IEEE80211_DISCARD(vap, IEEE80211_MSG_INPUT,
			    wh, NULL, "%s", "not unicast");
			vap->iv_stats.is_rx_mgtdiscard++;	/* XXX stat */
			return;
		}

		/*
		 * prreq frame format
		 *	[tlv] ssid
		 *	[tlv] supported rates
		 *	[tlv] extended supported rates
		 */
		ssid = rates = xrates = NULL;
		while (efrm - frm > 1) {
			IEEE80211_VERIFY_LENGTH(efrm - frm, frm[1] + 2, return);
			switch (*frm) {
			case IEEE80211_ELEMID_SSID:
				ssid = frm;
				break;
			case IEEE80211_ELEMID_RATES:
				rates = frm;
				break;
			case IEEE80211_ELEMID_XRATES:
				xrates = frm;
				break;
			}
			frm += frm[1] + 2;
		}
		IEEE80211_VERIFY_ELEMENT(rates, IEEE80211_RATE_MAXSIZE, return);
		if (xrates != NULL)
			IEEE80211_VERIFY_ELEMENT(xrates,
				IEEE80211_RATE_MAXSIZE - rates[1], return);
		IEEE80211_VERIFY_ELEMENT(ssid, IEEE80211_NWID_LEN, return);
		IEEE80211_VERIFY_SSID(vap->iv_bss, ssid, return);
		if ((vap->iv_flags & IEEE80211_F_HIDESSID) && ssid[1] == 0) {
			IEEE80211_DISCARD(vap, IEEE80211_MSG_INPUT,
			    wh, NULL,
			    "%s", "no ssid with ssid suppression enabled");
			vap->iv_stats.is_rx_ssidmismatch++; /*XXX*/
			return;
		}

		/* XXX find a better class or define it's own */
		IEEE80211_NOTE_MAC(vap, IEEE80211_MSG_INPUT, wh->i_addr2,
		    "%s", "recv probe req");
		/*
		 * Some legacy 11b clients cannot hack a complete
		 * probe response frame.  When the request includes
		 * only a bare-bones rate set, communicate this to
		 * the transmit side.
		 */
		ieee80211_send_proberesp(vap, wh->i_addr2,
		    is11bclient(rates, xrates) ? IEEE80211_SEND_LEGACY_11B : 0);

		/*
		 * Note: we don't benefit from stashing the probe request
		 * IEs away to use for IBSS negotiation, because we
		 * typically don't get all of the IEs.
		 */
		break;

	case IEEE80211_FC0_SUBTYPE_ACTION:
	case IEEE80211_FC0_SUBTYPE_ACTION_NOACK:
		if ((ni == vap->iv_bss) &&
		    !IEEE80211_ADDR_EQ(wh->i_addr2, ni->ni_macaddr)) {
			IEEE80211_DISCARD(vap, IEEE80211_MSG_INPUT,
			    wh, NULL, "%s", "unknown node");
			vap->iv_stats.is_rx_mgtdiscard++;
		} else if (!IEEE80211_ADDR_EQ(vap->iv_myaddr, wh->i_addr1) &&
		    !IEEE80211_IS_MULTICAST(wh->i_addr1)) {
			IEEE80211_DISCARD(vap, IEEE80211_MSG_INPUT,
			    wh, NULL, "%s", "not for us");
			vap->iv_stats.is_rx_mgtdiscard++;
		} else if (vap->iv_state != IEEE80211_S_RUN) {
			IEEE80211_DISCARD(vap, IEEE80211_MSG_INPUT,
			    wh, NULL, "wrong state %s",
			    ieee80211_state_name[vap->iv_state]);
			vap->iv_stats.is_rx_mgtdiscard++;
		} else {
			if (ieee80211_parse_action(ni, m0) == 0)
				(void)ic->ic_recv_action(ni, wh, frm, efrm);
		}
		break;

	case IEEE80211_FC0_SUBTYPE_ASSOC_REQ:
	case IEEE80211_FC0_SUBTYPE_ASSOC_RESP:
	case IEEE80211_FC0_SUBTYPE_REASSOC_REQ:
	case IEEE80211_FC0_SUBTYPE_REASSOC_RESP:
	case IEEE80211_FC0_SUBTYPE_TIMING_ADV:
	case IEEE80211_FC0_SUBTYPE_ATIM:
	case IEEE80211_FC0_SUBTYPE_DISASSOC:
	case IEEE80211_FC0_SUBTYPE_AUTH:
	case IEEE80211_FC0_SUBTYPE_DEAUTH:
		IEEE80211_DISCARD(vap, IEEE80211_MSG_INPUT,
		    wh, NULL, "%s", "not handled");
		vap->iv_stats.is_rx_mgtdiscard++;
		break;

	default:
		IEEE80211_DISCARD(vap, IEEE80211_MSG_ANY,
		    wh, "mgt", "subtype 0x%x not handled", subtype);
		vap->iv_stats.is_rx_badsubtype++;
		break;
	}
}
#undef IEEE80211_VERIFY_LENGTH
#undef IEEE80211_VERIFY_ELEMENT

static void
ahdemo_recv_mgmt(struct ieee80211_node *ni, struct mbuf *m0,
	int subtype, const struct ieee80211_rx_stats *rxs, int rssi, int nf)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211com *ic = ni->ni_ic;
	struct ieee80211_frame *wh;

	/*
	 * Process management frames when scanning; useful for doing
	 * a site-survey.
	 */
	if (ic->ic_flags & IEEE80211_F_SCAN)
		adhoc_recv_mgmt(ni, m0, subtype, rxs, rssi, nf);
	else {
		wh = mtod(m0, struct ieee80211_frame *);
		switch (subtype) {
		case IEEE80211_FC0_SUBTYPE_ASSOC_REQ:
		case IEEE80211_FC0_SUBTYPE_ASSOC_RESP:
		case IEEE80211_FC0_SUBTYPE_REASSOC_REQ:
		case IEEE80211_FC0_SUBTYPE_REASSOC_RESP:
		case IEEE80211_FC0_SUBTYPE_PROBE_REQ:
		case IEEE80211_FC0_SUBTYPE_PROBE_RESP:
		case IEEE80211_FC0_SUBTYPE_TIMING_ADV:
		case IEEE80211_FC0_SUBTYPE_BEACON:
		case IEEE80211_FC0_SUBTYPE_ATIM:
		case IEEE80211_FC0_SUBTYPE_DISASSOC:
		case IEEE80211_FC0_SUBTYPE_AUTH:
		case IEEE80211_FC0_SUBTYPE_DEAUTH:
		case IEEE80211_FC0_SUBTYPE_ACTION:
		case IEEE80211_FC0_SUBTYPE_ACTION_NOACK:
			IEEE80211_DISCARD(vap, IEEE80211_MSG_INPUT,
			     wh, NULL, "%s", "not handled");
			vap->iv_stats.is_rx_mgtdiscard++;
			break;
		default:
			IEEE80211_DISCARD(vap, IEEE80211_MSG_ANY,
			     wh, "mgt", "subtype 0x%x not handled", subtype);
			vap->iv_stats.is_rx_badsubtype++;
			break;
		}
	}
}

static void
adhoc_recv_ctl(struct ieee80211_node *ni, struct mbuf *m, int subtype)
{

	switch (subtype) {
	case IEEE80211_FC0_SUBTYPE_BAR:
		ieee80211_recv_bar(ni, m);
		break;
	}
}
