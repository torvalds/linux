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
 * IEEE 802.11 HOSTAP mode support.
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
#include <net80211/ieee80211_hostap.h>
#include <net80211/ieee80211_input.h>
#ifdef IEEE80211_SUPPORT_SUPERG
#include <net80211/ieee80211_superg.h>
#endif
#include <net80211/ieee80211_wds.h>
#include <net80211/ieee80211_vht.h>

#define	IEEE80211_RATE2MBS(r)	(((r) & IEEE80211_RATE_VAL) / 2)

static	void hostap_vattach(struct ieee80211vap *);
static	int hostap_newstate(struct ieee80211vap *, enum ieee80211_state, int);
static	int hostap_input(struct ieee80211_node *ni, struct mbuf *m,
	    const struct ieee80211_rx_stats *,
	    int rssi, int nf);
static void hostap_deliver_data(struct ieee80211vap *,
	    struct ieee80211_node *, struct mbuf *);
static void hostap_recv_mgmt(struct ieee80211_node *, struct mbuf *,
	    int subtype, const struct ieee80211_rx_stats *rxs, int rssi, int nf);
static void hostap_recv_ctl(struct ieee80211_node *, struct mbuf *, int);

void
ieee80211_hostap_attach(struct ieee80211com *ic)
{
	ic->ic_vattach[IEEE80211_M_HOSTAP] = hostap_vattach;
}

void
ieee80211_hostap_detach(struct ieee80211com *ic)
{
}

static void
hostap_vdetach(struct ieee80211vap *vap)
{
}

static void
hostap_vattach(struct ieee80211vap *vap)
{
	vap->iv_newstate = hostap_newstate;
	vap->iv_input = hostap_input;
	vap->iv_recv_mgmt = hostap_recv_mgmt;
	vap->iv_recv_ctl = hostap_recv_ctl;
	vap->iv_opdetach = hostap_vdetach;
	vap->iv_deliver_data = hostap_deliver_data;
	vap->iv_recv_pspoll = ieee80211_recv_pspoll;
}

static void
sta_disassoc(void *arg, struct ieee80211_node *ni)
{

	if (ni->ni_associd != 0) {
		IEEE80211_SEND_MGMT(ni, IEEE80211_FC0_SUBTYPE_DISASSOC,
			IEEE80211_REASON_ASSOC_LEAVE);
		ieee80211_node_leave(ni);
	}
}

static void
sta_csa(void *arg, struct ieee80211_node *ni)
{
	struct ieee80211vap *vap = ni->ni_vap;

	if (ni->ni_associd != 0)
		if (ni->ni_inact > vap->iv_inact_init) {
			ni->ni_inact = vap->iv_inact_init;
			IEEE80211_NOTE(vap, IEEE80211_MSG_INACT, ni,
			    "%s: inact %u", __func__, ni->ni_inact);
		}
}

static void
sta_drop(void *arg, struct ieee80211_node *ni)
{

	if (ni->ni_associd != 0)
		ieee80211_node_leave(ni);
}

/*
 * Does a channel change require associated stations to re-associate
 * so protocol state is correct.  This is used when doing CSA across
 * bands or similar (e.g. HT -> legacy).
 */
static int
isbandchange(struct ieee80211com *ic)
{
	return ((ic->ic_bsschan->ic_flags ^ ic->ic_csa_newchan->ic_flags) &
	    (IEEE80211_CHAN_2GHZ | IEEE80211_CHAN_5GHZ | IEEE80211_CHAN_HALF |
	     IEEE80211_CHAN_QUARTER | IEEE80211_CHAN_HT)) != 0;
}

/*
 * IEEE80211_M_HOSTAP vap state machine handler.
 */
static int
hostap_newstate(struct ieee80211vap *vap, enum ieee80211_state nstate, int arg)
{
	struct ieee80211com *ic = vap->iv_ic;
	enum ieee80211_state ostate;

	IEEE80211_LOCK_ASSERT(ic);

	ostate = vap->iv_state;
	IEEE80211_DPRINTF(vap, IEEE80211_MSG_STATE, "%s: %s -> %s (%d)\n",
	    __func__, ieee80211_state_name[ostate],
	    ieee80211_state_name[nstate], arg);
	vap->iv_state = nstate;			/* state transition */
	if (ostate != IEEE80211_S_SCAN)
		ieee80211_cancel_scan(vap);	/* background scan */
	switch (nstate) {
	case IEEE80211_S_INIT:
		switch (ostate) {
		case IEEE80211_S_SCAN:
			ieee80211_cancel_scan(vap);
			break;
		case IEEE80211_S_CAC:
			ieee80211_dfs_cac_stop(vap);
			break;
		case IEEE80211_S_RUN:
			ieee80211_iterate_nodes_vap(&ic->ic_sta, vap,
			    sta_disassoc, NULL);
			break;
		default:
			break;
		}
		if (ostate != IEEE80211_S_INIT) {
			/* NB: optimize INIT -> INIT case */
			ieee80211_reset_bss(vap);
		}
		if (vap->iv_auth->ia_detach != NULL)
			vap->iv_auth->ia_detach(vap);
		break;
	case IEEE80211_S_SCAN:
		switch (ostate) {
		case IEEE80211_S_CSA:
		case IEEE80211_S_RUN:
			ieee80211_iterate_nodes_vap(&ic->ic_sta, vap,
			    sta_disassoc, NULL);
			/*
			 * Clear overlapping BSS state; the beacon frame
			 * will be reconstructed on transition to the RUN
			 * state and the timeout routines check if the flag
			 * is set before doing anything so this is sufficient.
			 */
			ic->ic_flags_ext &= ~IEEE80211_FEXT_NONERP_PR;
			ic->ic_flags_ht &= ~IEEE80211_FHT_NONHT_PR;
			/* fall thru... */
		case IEEE80211_S_CAC:
			/*
			 * NB: We may get here because of a manual channel
			 *     change in which case we need to stop CAC
			 * XXX no need to stop if ostate RUN but it's ok
			 */
			ieee80211_dfs_cac_stop(vap);
			/* fall thru... */
		case IEEE80211_S_INIT:
			if (vap->iv_des_chan != IEEE80211_CHAN_ANYC &&
			    !IEEE80211_IS_CHAN_RADAR(vap->iv_des_chan)) {
				/*
				 * Already have a channel; bypass the
				 * scan and startup immediately.  
				 * ieee80211_create_ibss will call back to
				 * move us to RUN state.
				 */
				ieee80211_create_ibss(vap, vap->iv_des_chan);
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
			 * A state change requires a reset; scan.
			 */
			ieee80211_check_scan_current(vap);
			break;
		default:
			break;
		}
		break;
	case IEEE80211_S_CAC:
		/*
		 * Start CAC on a DFS channel.  We come here when starting
		 * a bss on a DFS channel (see ieee80211_create_ibss).
		 */
		ieee80211_dfs_cac_start(vap);
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
			break;
		case IEEE80211_S_CAC:
			/*
			 * NB: This is the normal state change when CAC
			 * expires and no radar was detected; no need to
			 * clear the CAC timer as it's already expired.
			 */
			/* fall thru... */
		case IEEE80211_S_CSA:
			/*
			 * Shorten inactivity timer of associated stations
			 * to weed out sta's that don't follow a CSA.
			 */
			ieee80211_iterate_nodes_vap(&ic->ic_sta, vap,
			    sta_csa, NULL);
			/*
			 * Update bss node channel to reflect where
			 * we landed after CSA.
			 */
			ieee80211_node_set_chan(vap->iv_bss,
			    ieee80211_ht_adjust_channel(ic, ic->ic_curchan,
				ieee80211_htchanflags(vap->iv_bss->ni_chan)));
			/* XXX bypass debug msgs */
			break;
		case IEEE80211_S_SCAN:
		case IEEE80211_S_RUN:
#ifdef IEEE80211_DEBUG
			if (ieee80211_msg_debug(vap)) {
				struct ieee80211_node *ni = vap->iv_bss;
				ieee80211_note(vap,
				    "synchronized with %s ssid ",
				    ether_sprintf(ni->ni_bssid));
				ieee80211_print_essid(ni->ni_essid,
				    ni->ni_esslen);
				/* XXX MCS/HT */
				printf(" channel %d start %uMb\n",
				    ieee80211_chan2ieee(ic, ic->ic_curchan),
				    IEEE80211_RATE2MBS(ni->ni_txrate));
			}
#endif
			break;
		default:
			break;
		}
		/*
		 * Start/stop the authenticator.  We delay until here
		 * to allow configuration to happen out of order.
		 */
		if (vap->iv_auth->ia_attach != NULL) {
			/* XXX check failure */
			vap->iv_auth->ia_attach(vap);
		} else if (vap->iv_auth->ia_detach != NULL) {
			vap->iv_auth->ia_detach(vap);
		}
		ieee80211_node_authorize(vap->iv_bss);
		break;
	case IEEE80211_S_CSA:
		if (ostate == IEEE80211_S_RUN && isbandchange(ic)) {
			/*
			 * On a ``band change'' silently drop associated
			 * stations as they must re-associate before they
			 * can pass traffic (as otherwise protocol state
			 * such as capabilities and the negotiated rate
			 * set may/will be wrong).
			 */
			ieee80211_iterate_nodes_vap(&ic->ic_sta, vap,
			    sta_drop, NULL);
		}
		break;
	default:
		break;
	}
	return 0;
}

static void
hostap_deliver_data(struct ieee80211vap *vap,
	struct ieee80211_node *ni, struct mbuf *m)
{
	struct ether_header *eh = mtod(m, struct ether_header *);
	struct ifnet *ifp = vap->iv_ifp;

	/* clear driver/net80211 flags before passing up */
	m->m_flags &= ~(M_MCAST | M_BCAST);
	m_clrprotoflags(m);

	KASSERT(vap->iv_opmode == IEEE80211_M_HOSTAP,
	    ("gack, opmode %d", vap->iv_opmode));
	/*
	 * Do accounting.
	 */
	if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
	IEEE80211_NODE_STAT(ni, rx_data);
	IEEE80211_NODE_STAT_ADD(ni, rx_bytes, m->m_pkthdr.len);
	if (ETHER_IS_MULTICAST(eh->ether_dhost)) {
		m->m_flags |= M_MCAST;		/* XXX M_BCAST? */
		IEEE80211_NODE_STAT(ni, rx_mcast);
	} else
		IEEE80211_NODE_STAT(ni, rx_ucast);

	/* perform as a bridge within the AP */
	if ((vap->iv_flags & IEEE80211_F_NOBRIDGE) == 0) {
		struct mbuf *mcopy = NULL;

		if (m->m_flags & M_MCAST) {
			mcopy = m_dup(m, M_NOWAIT);
			if (mcopy == NULL)
				if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
			else
				mcopy->m_flags |= M_MCAST;
		} else {
			/*
			 * Check if the destination is associated with the
			 * same vap and authorized to receive traffic.
			 * Beware of traffic destined for the vap itself;
			 * sending it will not work; just let it be delivered
			 * normally.
			 */
			struct ieee80211_node *sta = ieee80211_find_vap_node(
			     &vap->iv_ic->ic_sta, vap, eh->ether_dhost);
			if (sta != NULL) {
				if (ieee80211_node_is_authorized(sta)) {
					/*
					 * Beware of sending to ourself; this
					 * needs to happen via the normal
					 * input path.
					 */
					if (sta != vap->iv_bss) {
						mcopy = m;
						m = NULL;
					}
				} else {
					vap->iv_stats.is_rx_unauth++;
					IEEE80211_NODE_STAT(sta, rx_unauth);
				}
				ieee80211_free_node(sta);
			}
		}
		if (mcopy != NULL)
			(void) ieee80211_vap_xmitpkt(vap, mcopy);
	}
	if (m != NULL) {
		/*
		 * Mark frame as coming from vap's interface.
		 */
		m->m_pkthdr.rcvif = ifp;
		if (m->m_flags & M_MCAST) {
			/*
			 * Spam DWDS vap's w/ multicast traffic.
			 */
			/* XXX only if dwds in use? */
			ieee80211_dwds_mcast(vap, m);
		}
		if (ni->ni_vlan != 0) {
			/* attach vlan tag */
			m->m_pkthdr.ether_vtag = ni->ni_vlan;
			m->m_flags |= M_VLANTAG;
		}
		ifp->if_input(ifp, m);
	}
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
		return 0;
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
hostap_input(struct ieee80211_node *ni, struct mbuf *m,
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
		      subtype == IEEE80211_FC0_SUBTYPE_BEACON) &&
		    !IEEE80211_ADDR_EQ(bssid, vap->iv_bss->ni_bssid) &&
		    !IEEE80211_ADDR_EQ(bssid, ifp->if_broadcastaddr)) {
			/* not interested in */
			IEEE80211_DISCARD_MAC(vap, IEEE80211_MSG_INPUT,
			    bssid, NULL, "%s", "not to bss");
			vap->iv_stats.is_rx_wrongbss++;
			goto out;
		}

		IEEE80211_RSSI_LPF(ni->ni_avgrssi, rssi);
		ni->ni_noise = nf;
		if (IEEE80211_HAS_SEQ(type, subtype)) {
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
		if (!(dir == IEEE80211_FC1_DIR_TODS ||
		     (dir == IEEE80211_FC1_DIR_DSTODS &&
		      (vap->iv_flags & IEEE80211_F_DWDS)))) {
			if (dir != IEEE80211_FC1_DIR_DSTODS) {
				IEEE80211_DISCARD(vap,
				    IEEE80211_MSG_INPUT, wh, "data",
				    "incorrect dir 0x%x", dir);
			} else {
				IEEE80211_DISCARD(vap,
				    IEEE80211_MSG_INPUT |
				    IEEE80211_MSG_WDS, wh,
				    "4-address data",
				    "%s", "DWDS not enabled");
			}
			vap->iv_stats.is_rx_wrongdir++;
			goto out;
		}
		/* check if source STA is associated */
		if (ni == vap->iv_bss) {
			IEEE80211_DISCARD(vap, IEEE80211_MSG_INPUT,
			    wh, "data", "%s", "unknown src");
			ieee80211_send_error(ni, wh->i_addr2,
			    IEEE80211_FC0_SUBTYPE_DEAUTH,
			    IEEE80211_REASON_NOT_AUTHED);
			vap->iv_stats.is_rx_notassoc++;
			goto err;
		}
		if (ni->ni_associd == 0) {
			IEEE80211_DISCARD(vap, IEEE80211_MSG_INPUT,
			    wh, "data", "%s", "unassoc src");
			IEEE80211_SEND_MGMT(ni,
			    IEEE80211_FC0_SUBTYPE_DISASSOC,
			    IEEE80211_REASON_NOT_ASSOCED);
			vap->iv_stats.is_rx_notassoc++;
			goto err;
		}

		/*
		 * Check for power save state change.
		 * XXX out-of-order A-MPDU frames?
		 */
		if (((wh->i_fc[1] & IEEE80211_FC1_PWR_MGT) ^
		    (ni->ni_flags & IEEE80211_NODE_PWR_MGT)))
			vap->iv_node_ps(ni,
				wh->i_fc[1] & IEEE80211_FC1_PWR_MGT);
		/*
		 * For 4-address packets handle WDS discovery
		 * notifications.  Once a WDS link is setup frames
		 * are just delivered to the WDS vap (see below).
		 */
		if (dir == IEEE80211_FC1_DIR_DSTODS && ni->ni_wdsvap == NULL) {
			if (!ieee80211_node_is_authorized(ni)) {
				IEEE80211_DISCARD(vap,
				    IEEE80211_MSG_INPUT |
				    IEEE80211_MSG_WDS, wh,
				    "4-address data",
				    "%s", "unauthorized port");
				vap->iv_stats.is_rx_unauth++;
				IEEE80211_NODE_STAT(ni, rx_unauth);
				goto err;
			}
			ieee80211_dwds_discover(ni, m);
			return type;
		}

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
		if (key != NULL && !ieee80211_crypto_demic(vap, key, m, 0)) {
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
			hostap_deliver_data(vap, ni, m);
		return IEEE80211_FC0_TYPE_DATA;

	case IEEE80211_FC0_TYPE_MGT:
		vap->iv_stats.is_rx_mgmt++;
		IEEE80211_NODE_STAT(ni, rx_mgmt);
		if (dir != IEEE80211_FC1_DIR_NODS) {
			IEEE80211_DISCARD(vap, IEEE80211_MSG_INPUT,
			    wh, "mgt", "incorrect dir 0x%x", dir);
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
		if (IEEE80211_IS_MULTICAST(wh->i_addr2)) {
			/* ensure return frames are unicast */
			IEEE80211_DISCARD(vap, IEEE80211_MSG_ANY,
			    wh, NULL, "source is multicast: %s",
			    ether_sprintf(wh->i_addr2));
			vap->iv_stats.is_rx_mgtdiscard++;	/* XXX stat */
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
			if (subtype != IEEE80211_FC0_SUBTYPE_AUTH) {
				/*
				 * Only shared key auth frames with a challenge
				 * should be encrypted, discard all others.
				 */
				IEEE80211_DISCARD(vap, IEEE80211_MSG_INPUT,
				    wh, NULL,
				    "%s", "WEP set but not permitted");
				vap->iv_stats.is_rx_mgtdiscard++; /* XXX */
				goto out;
			}
			if ((vap->iv_flags & IEEE80211_F_PRIVACY) == 0) {
				/*
				 * Discard encrypted frames when privacy is off.
				 */
				IEEE80211_DISCARD(vap, IEEE80211_MSG_INPUT,
				    wh, NULL, "%s", "WEP set but PRIVACY off");
				vap->iv_stats.is_rx_noprivacy++;
				goto out;
			}
			hdrspace = ieee80211_hdrspace(ic, wh);
			if (ieee80211_crypto_decap(ni, m, hdrspace, &key) == 0) {
				/* NB: stats+msgs handled in crypto_decap */
				goto out;
			}
			wh = mtod(m, struct ieee80211_frame *);
			wh->i_fc[1] &= ~IEEE80211_FC1_PROTECTED;
			has_decrypted = 1;
		}
		/*
		 * Pass the packet to radiotap before calling iv_recv_mgmt().
		 * Otherwise iv_recv_mgmt() might pass another packet to
		 * radiotap, resulting in out of order packet captures.
		 */
		if (ieee80211_radiotap_active_vap(vap))
			ieee80211_radiotap_rx(vap, m);
		need_tap = 0;
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

static void
hostap_auth_open(struct ieee80211_node *ni, struct ieee80211_frame *wh,
    int rssi, int nf, uint16_t seq, uint16_t status)
{
	struct ieee80211vap *vap = ni->ni_vap;

	KASSERT(vap->iv_state == IEEE80211_S_RUN, ("state %d", vap->iv_state));

	if (ni->ni_authmode == IEEE80211_AUTH_SHARED) {
		IEEE80211_DISCARD_MAC(vap, IEEE80211_MSG_AUTH,
		    ni->ni_macaddr, "open auth",
		    "bad sta auth mode %u", ni->ni_authmode);
		vap->iv_stats.is_rx_bad_auth++;	/* XXX */
		/*
		 * Clear any challenge text that may be there if
		 * a previous shared key auth failed and then an
		 * open auth is attempted.
		 */
		if (ni->ni_challenge != NULL) {
			IEEE80211_FREE(ni->ni_challenge, M_80211_NODE);
			ni->ni_challenge = NULL;
		}
		/* XXX hack to workaround calling convention */
		ieee80211_send_error(ni, wh->i_addr2, 
		    IEEE80211_FC0_SUBTYPE_AUTH,
		    (seq + 1) | (IEEE80211_STATUS_ALG<<16));
		return;
	}
	if (seq != IEEE80211_AUTH_OPEN_REQUEST) {
		vap->iv_stats.is_rx_bad_auth++;
		return;
	}
	/* always accept open authentication requests */
	if (ni == vap->iv_bss) {
		ni = ieee80211_dup_bss(vap, wh->i_addr2);
		if (ni == NULL)
			return;
	} else if ((ni->ni_flags & IEEE80211_NODE_AREF) == 0)
		(void) ieee80211_ref_node(ni);
	/*
	 * Mark the node as referenced to reflect that it's
	 * reference count has been bumped to insure it remains
	 * after the transaction completes.
	 */
	ni->ni_flags |= IEEE80211_NODE_AREF;
	/*
	 * Mark the node as requiring a valid association id
	 * before outbound traffic is permitted.
	 */
	ni->ni_flags |= IEEE80211_NODE_ASSOCID;

	if (vap->iv_acl != NULL &&
	    vap->iv_acl->iac_getpolicy(vap) == IEEE80211_MACCMD_POLICY_RADIUS) {
		/*
		 * When the ACL policy is set to RADIUS we defer the
		 * authorization to a user agent.  Dispatch an event,
		 * a subsequent MLME call will decide the fate of the
		 * station.  If the user agent is not present then the
		 * node will be reclaimed due to inactivity.
		 */
		IEEE80211_NOTE_MAC(vap,
		    IEEE80211_MSG_AUTH | IEEE80211_MSG_ACL, ni->ni_macaddr,
		    "%s", "station authentication defered (radius acl)");
		ieee80211_notify_node_auth(ni);
	} else {
		IEEE80211_SEND_MGMT(ni, IEEE80211_FC0_SUBTYPE_AUTH, seq + 1);
		IEEE80211_NOTE_MAC(vap,
		    IEEE80211_MSG_DEBUG | IEEE80211_MSG_AUTH, ni->ni_macaddr,
		    "%s", "station authenticated (open)");
		/*
		 * When 802.1x is not in use mark the port
		 * authorized at this point so traffic can flow.
		 */
		if (ni->ni_authmode != IEEE80211_AUTH_8021X)
			ieee80211_node_authorize(ni);
	}
}

static void
hostap_auth_shared(struct ieee80211_node *ni, struct ieee80211_frame *wh,
    uint8_t *frm, uint8_t *efrm, int rssi, int nf,
    uint16_t seq, uint16_t status)
{
	struct ieee80211vap *vap = ni->ni_vap;
	uint8_t *challenge;
	int allocbs, estatus;

	KASSERT(vap->iv_state == IEEE80211_S_RUN, ("state %d", vap->iv_state));

	/*
	 * NB: this can happen as we allow pre-shared key
	 * authentication to be enabled w/o wep being turned
	 * on so that configuration of these can be done
	 * in any order.  It may be better to enforce the
	 * ordering in which case this check would just be
	 * for sanity/consistency.
	 */
	if ((vap->iv_flags & IEEE80211_F_PRIVACY) == 0) {
		IEEE80211_DISCARD_MAC(vap, IEEE80211_MSG_AUTH,
		    ni->ni_macaddr, "shared key auth",
		    "%s", " PRIVACY is disabled");
		estatus = IEEE80211_STATUS_ALG;
		goto bad;
	}
	/*
	 * Pre-shared key authentication is evil; accept
	 * it only if explicitly configured (it is supported
	 * mainly for compatibility with clients like Mac OS X).
	 */
	if (ni->ni_authmode != IEEE80211_AUTH_AUTO &&
	    ni->ni_authmode != IEEE80211_AUTH_SHARED) {
		IEEE80211_DISCARD_MAC(vap, IEEE80211_MSG_AUTH,
		    ni->ni_macaddr, "shared key auth",
		    "bad sta auth mode %u", ni->ni_authmode);
		vap->iv_stats.is_rx_bad_auth++;	/* XXX maybe a unique error? */
		estatus = IEEE80211_STATUS_ALG;
		goto bad;
	}

	challenge = NULL;
	if (frm + 1 < efrm) {
		if ((frm[1] + 2) > (efrm - frm)) {
			IEEE80211_DISCARD_MAC(vap, IEEE80211_MSG_AUTH,
			    ni->ni_macaddr, "shared key auth",
			    "ie %d/%d too long",
			    frm[0], (frm[1] + 2) - (efrm - frm));
			vap->iv_stats.is_rx_bad_auth++;
			estatus = IEEE80211_STATUS_CHALLENGE;
			goto bad;
		}
		if (*frm == IEEE80211_ELEMID_CHALLENGE)
			challenge = frm;
		frm += frm[1] + 2;
	}
	switch (seq) {
	case IEEE80211_AUTH_SHARED_CHALLENGE:
	case IEEE80211_AUTH_SHARED_RESPONSE:
		if (challenge == NULL) {
			IEEE80211_DISCARD_MAC(vap, IEEE80211_MSG_AUTH,
			    ni->ni_macaddr, "shared key auth",
			    "%s", "no challenge");
			vap->iv_stats.is_rx_bad_auth++;
			estatus = IEEE80211_STATUS_CHALLENGE;
			goto bad;
		}
		if (challenge[1] != IEEE80211_CHALLENGE_LEN) {
			IEEE80211_DISCARD_MAC(vap, IEEE80211_MSG_AUTH,
			    ni->ni_macaddr, "shared key auth",
			    "bad challenge len %d", challenge[1]);
			vap->iv_stats.is_rx_bad_auth++;
			estatus = IEEE80211_STATUS_CHALLENGE;
			goto bad;
		}
	default:
		break;
	}
	switch (seq) {
	case IEEE80211_AUTH_SHARED_REQUEST:
		if (ni == vap->iv_bss) {
			ni = ieee80211_dup_bss(vap, wh->i_addr2);
			if (ni == NULL) {
				/* NB: no way to return an error */
				return;
			}
			allocbs = 1;
		} else {
			if ((ni->ni_flags & IEEE80211_NODE_AREF) == 0)
				(void) ieee80211_ref_node(ni);
			allocbs = 0;
		}
		/*
		 * Mark the node as referenced to reflect that it's
		 * reference count has been bumped to insure it remains
		 * after the transaction completes.
		 */
		ni->ni_flags |= IEEE80211_NODE_AREF;
		/*
		 * Mark the node as requiring a valid association id
		 * before outbound traffic is permitted.
		 */
		ni->ni_flags |= IEEE80211_NODE_ASSOCID;
		IEEE80211_RSSI_LPF(ni->ni_avgrssi, rssi);
		ni->ni_noise = nf;
		if (!ieee80211_alloc_challenge(ni)) {
			/* NB: don't return error so they rexmit */
			return;
		}
		get_random_bytes(ni->ni_challenge,
			IEEE80211_CHALLENGE_LEN);
		IEEE80211_NOTE(vap, IEEE80211_MSG_DEBUG | IEEE80211_MSG_AUTH,
		    ni, "shared key %sauth request", allocbs ? "" : "re");
		/*
		 * When the ACL policy is set to RADIUS we defer the
		 * authorization to a user agent.  Dispatch an event,
		 * a subsequent MLME call will decide the fate of the
		 * station.  If the user agent is not present then the
		 * node will be reclaimed due to inactivity.
		 */
		if (vap->iv_acl != NULL &&
		    vap->iv_acl->iac_getpolicy(vap) == IEEE80211_MACCMD_POLICY_RADIUS) {
			IEEE80211_NOTE_MAC(vap,
			    IEEE80211_MSG_AUTH | IEEE80211_MSG_ACL,
			    ni->ni_macaddr,
			    "%s", "station authentication defered (radius acl)");
			ieee80211_notify_node_auth(ni);
			return;
		}
		break;
	case IEEE80211_AUTH_SHARED_RESPONSE:
		if (ni == vap->iv_bss) {
			IEEE80211_DISCARD_MAC(vap, IEEE80211_MSG_AUTH,
			    ni->ni_macaddr, "shared key response",
			    "%s", "unknown station");
			/* NB: don't send a response */
			return;
		}
		if (ni->ni_challenge == NULL) {
			IEEE80211_DISCARD_MAC(vap, IEEE80211_MSG_AUTH,
			    ni->ni_macaddr, "shared key response",
			    "%s", "no challenge recorded");
			vap->iv_stats.is_rx_bad_auth++;
			estatus = IEEE80211_STATUS_CHALLENGE;
			goto bad;
		}
		if (memcmp(ni->ni_challenge, &challenge[2],
			   challenge[1]) != 0) {
			IEEE80211_DISCARD_MAC(vap, IEEE80211_MSG_AUTH,
			    ni->ni_macaddr, "shared key response",
			    "%s", "challenge mismatch");
			vap->iv_stats.is_rx_auth_fail++;
			estatus = IEEE80211_STATUS_CHALLENGE;
			goto bad;
		}
		IEEE80211_NOTE(vap, IEEE80211_MSG_DEBUG | IEEE80211_MSG_AUTH,
		    ni, "%s", "station authenticated (shared key)");
		ieee80211_node_authorize(ni);
		break;
	default:
		IEEE80211_DISCARD_MAC(vap, IEEE80211_MSG_AUTH,
		    ni->ni_macaddr, "shared key auth",
		    "bad seq %d", seq);
		vap->iv_stats.is_rx_bad_auth++;
		estatus = IEEE80211_STATUS_SEQUENCE;
		goto bad;
	}
	IEEE80211_SEND_MGMT(ni, IEEE80211_FC0_SUBTYPE_AUTH, seq + 1);
	return;
bad:
	/*
	 * Send an error response; but only when operating as an AP.
	 */
	/* XXX hack to workaround calling convention */
	ieee80211_send_error(ni, wh->i_addr2,
	    IEEE80211_FC0_SUBTYPE_AUTH,
	    (seq + 1) | (estatus<<16));
}

/*
 * Convert a WPA cipher selector OUI to an internal
 * cipher algorithm.  Where appropriate we also
 * record any key length.
 */
static int
wpa_cipher(const uint8_t *sel, uint8_t *keylen, uint8_t *cipher)
{
#define	WPA_SEL(x)	(((x)<<24)|WPA_OUI)
	uint32_t w = le32dec(sel);

	switch (w) {
	case WPA_SEL(WPA_CSE_NULL):
		*cipher = IEEE80211_CIPHER_NONE;
		break;
	case WPA_SEL(WPA_CSE_WEP40):
		if (keylen)
			*keylen = 40 / NBBY;
		*cipher = IEEE80211_CIPHER_WEP;
		break;
	case WPA_SEL(WPA_CSE_WEP104):
		if (keylen)
			*keylen = 104 / NBBY;
		*cipher = IEEE80211_CIPHER_WEP;
		break;
	case WPA_SEL(WPA_CSE_TKIP):
		*cipher = IEEE80211_CIPHER_TKIP;
		break;
	case WPA_SEL(WPA_CSE_CCMP):
		*cipher = IEEE80211_CIPHER_AES_CCM;
		break;
	default:
		return (EINVAL);
	}

	return (0);
#undef WPA_SEL
}

/*
 * Convert a WPA key management/authentication algorithm
 * to an internal code.
 */
static int
wpa_keymgmt(const uint8_t *sel)
{
#define	WPA_SEL(x)	(((x)<<24)|WPA_OUI)
	uint32_t w = le32dec(sel);

	switch (w) {
	case WPA_SEL(WPA_ASE_8021X_UNSPEC):
		return WPA_ASE_8021X_UNSPEC;
	case WPA_SEL(WPA_ASE_8021X_PSK):
		return WPA_ASE_8021X_PSK;
	case WPA_SEL(WPA_ASE_NONE):
		return WPA_ASE_NONE;
	}
	return 0;		/* NB: so is discarded */
#undef WPA_SEL
}

/*
 * Parse a WPA information element to collect parameters.
 * Note that we do not validate security parameters; that
 * is handled by the authenticator; the parsing done here
 * is just for internal use in making operational decisions.
 */
static int
ieee80211_parse_wpa(struct ieee80211vap *vap, const uint8_t *frm,
	struct ieee80211_rsnparms *rsn, const struct ieee80211_frame *wh)
{
	uint8_t len = frm[1];
	uint32_t w;
	int error, n;

	/*
	 * Check the length once for fixed parts: OUI, type,
	 * version, mcast cipher, and 2 selector counts.
	 * Other, variable-length data, must be checked separately.
	 */
	if ((vap->iv_flags & IEEE80211_F_WPA1) == 0) {
		IEEE80211_DISCARD_IE(vap,
		    IEEE80211_MSG_ELEMID | IEEE80211_MSG_WPA,
		    wh, "WPA", "not WPA, flags 0x%x", vap->iv_flags);
		return IEEE80211_REASON_IE_INVALID;
	}
	if (len < 14) {
		IEEE80211_DISCARD_IE(vap,
		    IEEE80211_MSG_ELEMID | IEEE80211_MSG_WPA,
		    wh, "WPA", "too short, len %u", len);
		return IEEE80211_REASON_IE_INVALID;
	}
	frm += 6, len -= 4;		/* NB: len is payload only */
	/* NB: iswpaoui already validated the OUI and type */
	w = le16dec(frm);
	if (w != WPA_VERSION) {
		IEEE80211_DISCARD_IE(vap,
		    IEEE80211_MSG_ELEMID | IEEE80211_MSG_WPA,
		    wh, "WPA", "bad version %u", w);
		return IEEE80211_REASON_IE_INVALID;
	}
	frm += 2, len -= 2;

	memset(rsn, 0, sizeof(*rsn));

	/* multicast/group cipher */
	error = wpa_cipher(frm, &rsn->rsn_mcastkeylen, &rsn->rsn_mcastcipher);
	if (error != 0) {
		IEEE80211_DISCARD_IE(vap,
		    IEEE80211_MSG_ELEMID | IEEE80211_MSG_WPA,
		    wh, "WPA", "unknown mcast cipher suite %08X",
		    le32dec(frm));
		return IEEE80211_REASON_GROUP_CIPHER_INVALID;
	}
	frm += 4, len -= 4;

	/* unicast ciphers */
	n = le16dec(frm);
	frm += 2, len -= 2;
	if (len < n*4+2) {
		IEEE80211_DISCARD_IE(vap,
		    IEEE80211_MSG_ELEMID | IEEE80211_MSG_WPA,
		    wh, "WPA", "ucast cipher data too short; len %u, n %u",
		    len, n);
		return IEEE80211_REASON_IE_INVALID;
	}
	w = 0;
	for (; n > 0; n--) {
		uint8_t cipher;

		error = wpa_cipher(frm, &rsn->rsn_ucastkeylen, &cipher);
		if (error == 0)
			w |= 1 << cipher;

		frm += 4, len -= 4;
	}
	if (w == 0) {
		IEEE80211_DISCARD_IE(vap,
		    IEEE80211_MSG_ELEMID | IEEE80211_MSG_WPA,
		    wh, "WPA", "no usable pairwise cipher suite found (w=%d)",
		    w);
		return IEEE80211_REASON_PAIRWISE_CIPHER_INVALID;
	}
	/* XXX other? */
	if (w & (1 << IEEE80211_CIPHER_AES_CCM))
		rsn->rsn_ucastcipher = IEEE80211_CIPHER_AES_CCM;
	else
		rsn->rsn_ucastcipher = IEEE80211_CIPHER_TKIP;

	/* key management algorithms */
	n = le16dec(frm);
	frm += 2, len -= 2;
	if (len < n*4) {
		IEEE80211_DISCARD_IE(vap,
		    IEEE80211_MSG_ELEMID | IEEE80211_MSG_WPA,
		    wh, "WPA", "key mgmt alg data too short; len %u, n %u",
		    len, n);
		return IEEE80211_REASON_IE_INVALID;
	}
	w = 0;
	for (; n > 0; n--) {
		w |= wpa_keymgmt(frm);
		frm += 4, len -= 4;
	}
	if (w & WPA_ASE_8021X_UNSPEC)
		rsn->rsn_keymgmt = WPA_ASE_8021X_UNSPEC;
	else
		rsn->rsn_keymgmt = WPA_ASE_8021X_PSK;

	if (len > 2)		/* optional capabilities */
		rsn->rsn_caps = le16dec(frm);

	return 0;
}

/*
 * Convert an RSN cipher selector OUI to an internal
 * cipher algorithm.  Where appropriate we also
 * record any key length.
 */
static int
rsn_cipher(const uint8_t *sel, uint8_t *keylen, uint8_t *cipher)
{
#define	RSN_SEL(x)	(((x)<<24)|RSN_OUI)
	uint32_t w = le32dec(sel);

	switch (w) {
	case RSN_SEL(RSN_CSE_NULL):
		*cipher = IEEE80211_CIPHER_NONE;
		break;
	case RSN_SEL(RSN_CSE_WEP40):
		if (keylen)
			*keylen = 40 / NBBY;
		*cipher = IEEE80211_CIPHER_WEP;
		break;
	case RSN_SEL(RSN_CSE_WEP104):
		if (keylen)
			*keylen = 104 / NBBY;
		*cipher = IEEE80211_CIPHER_WEP;
		break;
	case RSN_SEL(RSN_CSE_TKIP):
		*cipher = IEEE80211_CIPHER_TKIP;
		break;
	case RSN_SEL(RSN_CSE_CCMP):
		*cipher = IEEE80211_CIPHER_AES_CCM;
		break;
	case RSN_SEL(RSN_CSE_WRAP):
		*cipher = IEEE80211_CIPHER_AES_OCB;
		break;
	default:
		return (EINVAL);
	}

	return (0);
#undef WPA_SEL
}

/*
 * Convert an RSN key management/authentication algorithm
 * to an internal code.
 */
static int
rsn_keymgmt(const uint8_t *sel)
{
#define	RSN_SEL(x)	(((x)<<24)|RSN_OUI)
	uint32_t w = le32dec(sel);

	switch (w) {
	case RSN_SEL(RSN_ASE_8021X_UNSPEC):
		return RSN_ASE_8021X_UNSPEC;
	case RSN_SEL(RSN_ASE_8021X_PSK):
		return RSN_ASE_8021X_PSK;
	case RSN_SEL(RSN_ASE_NONE):
		return RSN_ASE_NONE;
	}
	return 0;		/* NB: so is discarded */
#undef RSN_SEL
}

/*
 * Parse a WPA/RSN information element to collect parameters
 * and validate the parameters against what has been
 * configured for the system.
 */
static int
ieee80211_parse_rsn(struct ieee80211vap *vap, const uint8_t *frm,
	struct ieee80211_rsnparms *rsn, const struct ieee80211_frame *wh)
{
	uint8_t len = frm[1];
	uint32_t w;
	int error, n;

	/*
	 * Check the length once for fixed parts: 
	 * version, mcast cipher, and 2 selector counts.
	 * Other, variable-length data, must be checked separately.
	 */
	if ((vap->iv_flags & IEEE80211_F_WPA2) == 0) {
		IEEE80211_DISCARD_IE(vap,
		    IEEE80211_MSG_ELEMID | IEEE80211_MSG_WPA,
		    wh, "WPA", "not RSN, flags 0x%x", vap->iv_flags);
		return IEEE80211_REASON_IE_INVALID;
	}
	/* XXX may be shorter */
	if (len < 10) {
		IEEE80211_DISCARD_IE(vap,
		    IEEE80211_MSG_ELEMID | IEEE80211_MSG_WPA,
		    wh, "RSN", "too short, len %u", len);
		return IEEE80211_REASON_IE_INVALID;
	}
	frm += 2;
	w = le16dec(frm);
	if (w != RSN_VERSION) {
		IEEE80211_DISCARD_IE(vap,
		    IEEE80211_MSG_ELEMID | IEEE80211_MSG_WPA,
		    wh, "RSN", "bad version %u", w);
		return IEEE80211_REASON_UNSUPP_RSN_IE_VERSION;
	}
	frm += 2, len -= 2;

	memset(rsn, 0, sizeof(*rsn));

	/* multicast/group cipher */
	error = rsn_cipher(frm, &rsn->rsn_mcastkeylen, &rsn->rsn_mcastcipher);
	if (error != 0) {
		IEEE80211_DISCARD_IE(vap,
		    IEEE80211_MSG_ELEMID | IEEE80211_MSG_WPA,
		    wh, "RSN", "unknown mcast cipher suite %08X",
		    le32dec(frm));
		return IEEE80211_REASON_GROUP_CIPHER_INVALID;
	}
	if (rsn->rsn_mcastcipher == IEEE80211_CIPHER_NONE) {
		IEEE80211_DISCARD_IE(vap,
		    IEEE80211_MSG_ELEMID | IEEE80211_MSG_WPA,
		    wh, "RSN", "invalid mcast cipher suite %d",
		    rsn->rsn_mcastcipher);
		return IEEE80211_REASON_GROUP_CIPHER_INVALID;
	}
	frm += 4, len -= 4;

	/* unicast ciphers */
	n = le16dec(frm);
	frm += 2, len -= 2;
	if (len < n*4+2) {
		IEEE80211_DISCARD_IE(vap,
		    IEEE80211_MSG_ELEMID | IEEE80211_MSG_WPA,
		    wh, "RSN", "ucast cipher data too short; len %u, n %u",
		    len, n);
		return IEEE80211_REASON_IE_INVALID;
	}
	w = 0;

	for (; n > 0; n--) {
		uint8_t cipher;

		error = rsn_cipher(frm, &rsn->rsn_ucastkeylen, &cipher);
		if (error == 0)
			w |= 1 << cipher;

		frm += 4, len -= 4;
	}
        if (w & (1 << IEEE80211_CIPHER_AES_CCM))
                rsn->rsn_ucastcipher = IEEE80211_CIPHER_AES_CCM;
	else if (w & (1 << IEEE80211_CIPHER_AES_OCB))
		rsn->rsn_ucastcipher = IEEE80211_CIPHER_AES_OCB;
	else if (w & (1 << IEEE80211_CIPHER_TKIP))
		rsn->rsn_ucastcipher = IEEE80211_CIPHER_TKIP;
	else if ((w & (1 << IEEE80211_CIPHER_NONE)) &&
	    (rsn->rsn_mcastcipher == IEEE80211_CIPHER_WEP ||
	     rsn->rsn_mcastcipher == IEEE80211_CIPHER_TKIP))
		rsn->rsn_ucastcipher = IEEE80211_CIPHER_NONE;
	else {
		IEEE80211_DISCARD_IE(vap,
		    IEEE80211_MSG_ELEMID | IEEE80211_MSG_WPA,
		    wh, "RSN", "no usable pairwise cipher suite found (w=%d)",
		    w);
		return IEEE80211_REASON_PAIRWISE_CIPHER_INVALID;
	}

	/* key management algorithms */
	n = le16dec(frm);
	frm += 2, len -= 2;
	if (len < n*4) {
		IEEE80211_DISCARD_IE(vap,
		    IEEE80211_MSG_ELEMID | IEEE80211_MSG_WPA,
		    wh, "RSN", "key mgmt alg data too short; len %u, n %u",
		    len, n);
		return IEEE80211_REASON_IE_INVALID;
	}
	w = 0;
	for (; n > 0; n--) {
		w |= rsn_keymgmt(frm);
		frm += 4, len -= 4;
	}
	if (w & RSN_ASE_8021X_UNSPEC)
		rsn->rsn_keymgmt = RSN_ASE_8021X_UNSPEC;
	else
		rsn->rsn_keymgmt = RSN_ASE_8021X_PSK;

	/* optional RSN capabilities */
	if (len > 2)
		rsn->rsn_caps = le16dec(frm);
	/* XXXPMKID */

	return 0;
}

/*
 * WPA/802.11i association request processing.
 */
static int
wpa_assocreq(struct ieee80211_node *ni, struct ieee80211_rsnparms *rsnparms,
	const struct ieee80211_frame *wh, const uint8_t *wpa,
	const uint8_t *rsn, uint16_t capinfo)
{
	struct ieee80211vap *vap = ni->ni_vap;
	uint8_t reason;
	int badwparsn;

	ni->ni_flags &= ~(IEEE80211_NODE_WPS|IEEE80211_NODE_TSN);
	if (wpa == NULL && rsn == NULL) {
		if (vap->iv_flags_ext & IEEE80211_FEXT_WPS) {
			/*
			 * W-Fi Protected Setup (WPS) permits
			 * clients to associate and pass EAPOL frames
			 * to establish initial credentials.
			 */
			ni->ni_flags |= IEEE80211_NODE_WPS;
			return 1;
		}
		if ((vap->iv_flags_ext & IEEE80211_FEXT_TSN) &&
		    (capinfo & IEEE80211_CAPINFO_PRIVACY)) {
			/* 
			 * Transitional Security Network.  Permits clients
			 * to associate and use WEP while WPA is configured.
			 */
			ni->ni_flags |= IEEE80211_NODE_TSN;
			return 1;
		}
		IEEE80211_DISCARD(vap, IEEE80211_MSG_ASSOC | IEEE80211_MSG_WPA,
		    wh, NULL, "%s", "no WPA/RSN IE in association request");
		vap->iv_stats.is_rx_assoc_badwpaie++;
		reason = IEEE80211_REASON_IE_INVALID;
		goto bad;
	}
	/* assert right association security credentials */
	badwparsn = 0;			/* NB: to silence compiler */
	switch (vap->iv_flags & IEEE80211_F_WPA) {
	case IEEE80211_F_WPA1:
		badwparsn = (wpa == NULL);
		break;
	case IEEE80211_F_WPA2:
		badwparsn = (rsn == NULL);
		break;
	case IEEE80211_F_WPA1|IEEE80211_F_WPA2:
		badwparsn = (wpa == NULL && rsn == NULL);
		break;
	}
	if (badwparsn) {
		IEEE80211_DISCARD(vap, IEEE80211_MSG_ASSOC | IEEE80211_MSG_WPA,
		    wh, NULL,
		    "%s", "missing WPA/RSN IE in association request");
		vap->iv_stats.is_rx_assoc_badwpaie++;
		reason = IEEE80211_REASON_IE_INVALID;
		goto bad;
	}
	/*
	 * Parse WPA/RSN information element.
	 */
	if (wpa != NULL)
		reason = ieee80211_parse_wpa(vap, wpa, rsnparms, wh);
	else
		reason = ieee80211_parse_rsn(vap, rsn, rsnparms, wh);
	if (reason != 0) {
		/* XXX wpa->rsn fallback? */
		/* XXX distinguish WPA/RSN? */
		vap->iv_stats.is_rx_assoc_badwpaie++;
		goto bad;
	}
	IEEE80211_NOTE(vap, IEEE80211_MSG_ASSOC | IEEE80211_MSG_WPA, ni,
	    "%s ie: mc %u/%u uc %u/%u key %u caps 0x%x",
	    wpa != NULL ? "WPA" : "RSN",
	    rsnparms->rsn_mcastcipher, rsnparms->rsn_mcastkeylen,
	    rsnparms->rsn_ucastcipher, rsnparms->rsn_ucastkeylen,
	    rsnparms->rsn_keymgmt, rsnparms->rsn_caps);

	return 1;
bad:
	ieee80211_node_deauth(ni, reason);
	return 0;
}

/* XXX find a better place for definition */
struct l2_update_frame {
	struct ether_header eh;
	uint8_t dsap;
	uint8_t ssap;
	uint8_t control;
	uint8_t xid[3];
}  __packed;

/*
 * Deliver a TGf L2UF frame on behalf of a station.
 * This primes any bridge when the station is roaming
 * between ap's on the same wired network.
 */
static void
ieee80211_deliver_l2uf(struct ieee80211_node *ni)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ifnet *ifp = vap->iv_ifp;
	struct mbuf *m;
	struct l2_update_frame *l2uf;
	struct ether_header *eh;
	
	m = m_gethdr(M_NOWAIT, MT_DATA);
	if (m == NULL) {
		IEEE80211_NOTE(vap, IEEE80211_MSG_ASSOC, ni,
		    "%s", "no mbuf for l2uf frame");
		vap->iv_stats.is_rx_nobuf++;	/* XXX not right */
		return;
	}
	l2uf = mtod(m, struct l2_update_frame *);
	eh = &l2uf->eh;
	/* dst: Broadcast address */
	IEEE80211_ADDR_COPY(eh->ether_dhost, ifp->if_broadcastaddr);
	/* src: associated STA */
	IEEE80211_ADDR_COPY(eh->ether_shost, ni->ni_macaddr);
	eh->ether_type = htons(sizeof(*l2uf) - sizeof(*eh));
	
	l2uf->dsap = 0;
	l2uf->ssap = 0;
	l2uf->control = 0xf5;
	l2uf->xid[0] = 0x81;
	l2uf->xid[1] = 0x80;
	l2uf->xid[2] = 0x00;
	
	m->m_pkthdr.len = m->m_len = sizeof(*l2uf);
	hostap_deliver_data(vap, ni, m);
}

static void
ratesetmismatch(struct ieee80211_node *ni, const struct ieee80211_frame *wh,
	int reassoc, int resp, const char *tag, int rate)
{
	IEEE80211_NOTE_MAC(ni->ni_vap, IEEE80211_MSG_ANY, wh->i_addr2,
	    "deny %s request, %s rate set mismatch, rate/MCS %d",
	    reassoc ? "reassoc" : "assoc", tag, rate & IEEE80211_RATE_VAL);
	IEEE80211_SEND_MGMT(ni, resp, IEEE80211_STATUS_BASIC_RATE);
	ieee80211_node_leave(ni);
}

static void
capinfomismatch(struct ieee80211_node *ni, const struct ieee80211_frame *wh,
	int reassoc, int resp, const char *tag, int capinfo)
{
	struct ieee80211vap *vap = ni->ni_vap;

	IEEE80211_NOTE_MAC(vap, IEEE80211_MSG_ANY, wh->i_addr2,
	    "deny %s request, %s mismatch 0x%x",
	    reassoc ? "reassoc" : "assoc", tag, capinfo);
	IEEE80211_SEND_MGMT(ni, resp, IEEE80211_STATUS_CAPINFO);
	ieee80211_node_leave(ni);
	vap->iv_stats.is_rx_assoc_capmismatch++;
}

static void
htcapmismatch(struct ieee80211_node *ni, const struct ieee80211_frame *wh,
	int reassoc, int resp)
{
	IEEE80211_NOTE_MAC(ni->ni_vap, IEEE80211_MSG_ANY, wh->i_addr2,
	    "deny %s request, %s missing HT ie", reassoc ? "reassoc" : "assoc");
	/* XXX no better code */
	IEEE80211_SEND_MGMT(ni, resp, IEEE80211_STATUS_MISSING_HT_CAPS);
	ieee80211_node_leave(ni);
}

static void
authalgreject(struct ieee80211_node *ni, const struct ieee80211_frame *wh,
	int algo, int seq, int status)
{
	struct ieee80211vap *vap = ni->ni_vap;

	IEEE80211_DISCARD(vap, IEEE80211_MSG_ANY,
	    wh, NULL, "unsupported alg %d", algo);
	vap->iv_stats.is_rx_auth_unsupported++;
	ieee80211_send_error(ni, wh->i_addr2, IEEE80211_FC0_SUBTYPE_AUTH,
	    seq | (status << 16));
}

static __inline int
ishtmixed(const uint8_t *ie)
{
	const struct ieee80211_ie_htinfo *ht =
	    (const struct ieee80211_ie_htinfo *) ie;
	return (ht->hi_byte2 & IEEE80211_HTINFO_OPMODE) ==
	    IEEE80211_HTINFO_OPMODE_MIXED;
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
hostap_recv_mgmt(struct ieee80211_node *ni, struct mbuf *m0,
	int subtype, const struct ieee80211_rx_stats *rxs, int rssi, int nf)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211com *ic = ni->ni_ic;
	struct ieee80211_frame *wh;
	uint8_t *frm, *efrm, *sfrm;
	uint8_t *ssid, *rates, *xrates, *wpa, *rsn, *wme, *ath, *htcap;
	uint8_t *vhtcap, *vhtinfo;
	int reassoc, resp;
	uint8_t rate;

	wh = mtod(m0, struct ieee80211_frame *);
	frm = (uint8_t *)&wh[1];
	efrm = mtod(m0, uint8_t *) + m0->m_len;
	switch (subtype) {
	case IEEE80211_FC0_SUBTYPE_PROBE_RESP:
		/*
		 * We process beacon/probe response frames when scanning;
		 * otherwise we check beacon frames for overlapping non-ERP
		 * BSS in 11g and/or overlapping legacy BSS when in HT.
		 */
		if ((ic->ic_flags & IEEE80211_F_SCAN) == 0) {
			vap->iv_stats.is_rx_mgtdiscard++;
			return;
		}
		/* FALLTHROUGH */
	case IEEE80211_FC0_SUBTYPE_BEACON: {
		struct ieee80211_scanparams scan;

		/* NB: accept off-channel frames */
		/* XXX TODO: use rxstatus to determine off-channel details */
		if (ieee80211_parse_beacon(ni, m0, ic->ic_curchan, &scan) &~ IEEE80211_BPARSE_OFFCHAN)
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
			if (scan.status == 0 &&		/* NB: on channel */
			    (ic->ic_flags_ext & IEEE80211_FEXT_PROBECHAN)) {
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
			ieee80211_add_scan(vap, ic->ic_curchan, &scan, wh,
			    subtype, rssi, nf);
			return;
		}
		/*
		 * Check beacon for overlapping bss w/ non ERP stations.
		 * If we detect one and protection is configured but not
		 * enabled, enable it and start a timer that'll bring us
		 * out if we stop seeing the bss.
		 */
		if (IEEE80211_IS_CHAN_ANYG(ic->ic_curchan) &&
		    scan.status == 0 &&			/* NB: on-channel */
		    ((scan.erp & 0x100) == 0 ||		/* NB: no ERP, 11b sta*/
		     (scan.erp & IEEE80211_ERP_NON_ERP_PRESENT))) {
			ic->ic_lastnonerp = ticks;
			ic->ic_flags_ext |= IEEE80211_FEXT_NONERP_PR;
			if (ic->ic_protmode != IEEE80211_PROT_NONE &&
			    (ic->ic_flags & IEEE80211_F_USEPROT) == 0) {
				IEEE80211_NOTE_FRAME(vap,
				    IEEE80211_MSG_ASSOC, wh,
				    "non-ERP present on channel %d "
				    "(saw erp 0x%x from channel %d), "
				    "enable use of protection",
				    ic->ic_curchan->ic_ieee,
				    scan.erp, scan.chan);
				ic->ic_flags |= IEEE80211_F_USEPROT;
				ieee80211_notify_erp(ic);
			}
		}
		/* 
		 * Check beacon for non-HT station on HT channel
		 * and update HT BSS occupancy as appropriate.
		 */
		if (IEEE80211_IS_CHAN_HT(ic->ic_curchan)) {
			if (scan.status & IEEE80211_BPARSE_OFFCHAN) {
				/*
				 * Off control channel; only check frames
				 * that come in the extension channel when
				 * operating w/ HT40.
				 */
				if (!IEEE80211_IS_CHAN_HT40(ic->ic_curchan))
					break;
				if (scan.chan != ic->ic_curchan->ic_extieee)
					break;
			}
			if (scan.htinfo == NULL) {
				ieee80211_htprot_update(ic,
				    IEEE80211_HTINFO_OPMODE_PROTOPT |
				    IEEE80211_HTINFO_NONHT_PRESENT);
			} else if (ishtmixed(scan.htinfo)) {
				/* XXX? take NONHT_PRESENT from beacon? */
				ieee80211_htprot_update(ic,
				    IEEE80211_HTINFO_OPMODE_MIXED |
				    IEEE80211_HTINFO_NONHT_PRESENT);
			}
		}
		break;
	}

	case IEEE80211_FC0_SUBTYPE_PROBE_REQ:
		if (vap->iv_state != IEEE80211_S_RUN) {
			vap->iv_stats.is_rx_mgtdiscard++;
			return;
		}
		/*
		 * Consult the ACL policy module if setup.
		 */
		if (vap->iv_acl != NULL && !vap->iv_acl->iac_check(vap, wh)) {
			IEEE80211_DISCARD(vap, IEEE80211_MSG_ACL,
			    wh, NULL, "%s", "disallowed by ACL");
			vap->iv_stats.is_rx_acl++;
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
		break;

	case IEEE80211_FC0_SUBTYPE_AUTH: {
		uint16_t algo, seq, status;

		if (vap->iv_state != IEEE80211_S_RUN) {
			vap->iv_stats.is_rx_mgtdiscard++;
			return;
		}
		if (!IEEE80211_ADDR_EQ(wh->i_addr3, vap->iv_bss->ni_bssid)) {
			IEEE80211_DISCARD(vap, IEEE80211_MSG_ANY,
			    wh, NULL, "%s", "wrong bssid");
			vap->iv_stats.is_rx_wrongbss++;	/*XXX unique stat?*/
			return;
		}
		/*
		 * auth frame format
		 *	[2] algorithm
		 *	[2] sequence
		 *	[2] status
		 *	[tlv*] challenge
		 */
		IEEE80211_VERIFY_LENGTH(efrm - frm, 6, return);
		algo   = le16toh(*(uint16_t *)frm);
		seq    = le16toh(*(uint16_t *)(frm + 2));
		status = le16toh(*(uint16_t *)(frm + 4));
		IEEE80211_NOTE_MAC(vap, IEEE80211_MSG_AUTH, wh->i_addr2,
		    "recv auth frame with algorithm %d seq %d", algo, seq);
		/*
		 * Consult the ACL policy module if setup.
		 */
		if (vap->iv_acl != NULL && !vap->iv_acl->iac_check(vap, wh)) {
			IEEE80211_DISCARD(vap, IEEE80211_MSG_ACL,
			    wh, NULL, "%s", "disallowed by ACL");
			vap->iv_stats.is_rx_acl++;
			ieee80211_send_error(ni, wh->i_addr2,
			    IEEE80211_FC0_SUBTYPE_AUTH,
			    (seq+1) | (IEEE80211_STATUS_UNSPECIFIED<<16));
			return;
		}
		if (vap->iv_flags & IEEE80211_F_COUNTERM) {
			IEEE80211_DISCARD(vap,
			    IEEE80211_MSG_AUTH | IEEE80211_MSG_CRYPTO,
			    wh, NULL, "%s", "TKIP countermeasures enabled");
			vap->iv_stats.is_rx_auth_countermeasures++;
			ieee80211_send_error(ni, wh->i_addr2,
				IEEE80211_FC0_SUBTYPE_AUTH,
				IEEE80211_REASON_MIC_FAILURE);
			return;
		}
		if (algo == IEEE80211_AUTH_ALG_SHARED)
			hostap_auth_shared(ni, wh, frm + 6, efrm, rssi, nf,
			    seq, status);
		else if (algo == IEEE80211_AUTH_ALG_OPEN)
			hostap_auth_open(ni, wh, rssi, nf, seq, status);
		else if (algo == IEEE80211_AUTH_ALG_LEAP) {
			authalgreject(ni, wh, algo,
			    seq+1, IEEE80211_STATUS_ALG);
			return;
		} else {
			/*
			 * We assume that an unknown algorithm is the result
			 * of a decryption failure on a shared key auth frame;
			 * return a status code appropriate for that instead
			 * of IEEE80211_STATUS_ALG.
			 *
			 * NB: a seq# of 4 is intentional; the decrypted
			 *     frame likely has a bogus seq value.
			 */
			authalgreject(ni, wh, algo,
			    4, IEEE80211_STATUS_CHALLENGE);
			return;
		} 
		break;
	}

	case IEEE80211_FC0_SUBTYPE_ASSOC_REQ:
	case IEEE80211_FC0_SUBTYPE_REASSOC_REQ: {
		uint16_t capinfo, lintval;
		struct ieee80211_rsnparms rsnparms;

		if (vap->iv_state != IEEE80211_S_RUN) {
			vap->iv_stats.is_rx_mgtdiscard++;
			return;
		}
		if (!IEEE80211_ADDR_EQ(wh->i_addr3, vap->iv_bss->ni_bssid)) {
			IEEE80211_DISCARD(vap, IEEE80211_MSG_ANY,
			    wh, NULL, "%s", "wrong bssid");
			vap->iv_stats.is_rx_assoc_bss++;
			return;
		}
		if (subtype == IEEE80211_FC0_SUBTYPE_REASSOC_REQ) {
			reassoc = 1;
			resp = IEEE80211_FC0_SUBTYPE_REASSOC_RESP;
		} else {
			reassoc = 0;
			resp = IEEE80211_FC0_SUBTYPE_ASSOC_RESP;
		}
		if (ni == vap->iv_bss) {
			IEEE80211_NOTE_MAC(vap, IEEE80211_MSG_ANY, wh->i_addr2,
			    "deny %s request, sta not authenticated",
			    reassoc ? "reassoc" : "assoc");
			ieee80211_send_error(ni, wh->i_addr2,
			    IEEE80211_FC0_SUBTYPE_DEAUTH,
			    IEEE80211_REASON_ASSOC_NOT_AUTHED);
			vap->iv_stats.is_rx_assoc_notauth++;
			return;
		}

		/*
		 * asreq frame format
		 *	[2] capability information
		 *	[2] listen interval
		 *	[6*] current AP address (reassoc only)
		 *	[tlv] ssid
		 *	[tlv] supported rates
		 *	[tlv] extended supported rates
		 *	[tlv] WPA or RSN
		 *	[tlv] HT capabilities
		 *	[tlv] Atheros capabilities
		 */
		IEEE80211_VERIFY_LENGTH(efrm - frm, (reassoc ? 10 : 4), return);
		capinfo = le16toh(*(uint16_t *)frm);	frm += 2;
		lintval = le16toh(*(uint16_t *)frm);	frm += 2;
		if (reassoc)
			frm += 6;	/* ignore current AP info */
		ssid = rates = xrates = wpa = rsn = wme = ath = htcap = NULL;
		vhtcap = vhtinfo = NULL;
		sfrm = frm;
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
			case IEEE80211_ELEMID_RSN:
				rsn = frm;
				break;
			case IEEE80211_ELEMID_HTCAP:
				htcap = frm;
				break;
			case IEEE80211_ELEMID_VHT_CAP:
				vhtcap = frm;
				break;
			case IEEE80211_ELEMID_VHT_OPMODE:
				vhtinfo = frm;
				break;
			case IEEE80211_ELEMID_VENDOR:
				if (iswpaoui(frm))
					wpa = frm;
				else if (iswmeinfo(frm))
					wme = frm;
#ifdef IEEE80211_SUPPORT_SUPERG
				else if (isatherosoui(frm))
					ath = frm;
#endif
				else if (vap->iv_flags_ht & IEEE80211_FHT_HTCOMPAT) {
					if (ishtcapoui(frm) && htcap == NULL)
						htcap = frm;
				}
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
		if (htcap != NULL) {
			IEEE80211_VERIFY_LENGTH(htcap[1],
			     htcap[0] == IEEE80211_ELEMID_VENDOR ?
			         4 + sizeof(struct ieee80211_ie_htcap)-2 :
			         sizeof(struct ieee80211_ie_htcap)-2,
			     return);		/* XXX just NULL out? */
		}

		/* Validate VHT IEs */
		if (vhtcap != NULL) {
			IEEE80211_VERIFY_LENGTH(vhtcap[1],
			    sizeof(struct ieee80211_ie_vhtcap) - 2,
			    return);
		}
		if (vhtinfo != NULL) {
			IEEE80211_VERIFY_LENGTH(vhtinfo[1],
			    sizeof(struct ieee80211_ie_vht_operation) - 2,
			    return);
		}

		if ((vap->iv_flags & IEEE80211_F_WPA) &&
		    !wpa_assocreq(ni, &rsnparms, wh, wpa, rsn, capinfo))
			return;
		/* discard challenge after association */
		if (ni->ni_challenge != NULL) {
			IEEE80211_FREE(ni->ni_challenge, M_80211_NODE);
			ni->ni_challenge = NULL;
		}
		/* NB: 802.11 spec says to ignore station's privacy bit */
		if ((capinfo & IEEE80211_CAPINFO_ESS) == 0) {
			capinfomismatch(ni, wh, reassoc, resp,
			    "capability", capinfo);
			return;
		}
		/*
		 * Disallow re-associate w/ invalid slot time setting.
		 */
		if (ni->ni_associd != 0 &&
		    IEEE80211_IS_CHAN_ANYG(ic->ic_bsschan) &&
		    ((ni->ni_capinfo ^ capinfo) & IEEE80211_CAPINFO_SHORT_SLOTTIME)) {
			capinfomismatch(ni, wh, reassoc, resp,
			    "slot time", capinfo);
			return;
		}
		rate = ieee80211_setup_rates(ni, rates, xrates,
				IEEE80211_F_DOSORT | IEEE80211_F_DOFRATE |
				IEEE80211_F_DONEGO | IEEE80211_F_DODEL);
		if (rate & IEEE80211_RATE_BASIC) {
			ratesetmismatch(ni, wh, reassoc, resp, "legacy", rate);
			vap->iv_stats.is_rx_assoc_norate++;
			return;
		}
		/*
		 * If constrained to 11g-only stations reject an
		 * 11b-only station.  We cheat a bit here by looking
		 * at the max negotiated xmit rate and assuming anyone
		 * with a best rate <24Mb/s is an 11b station.
		 */
		if ((vap->iv_flags & IEEE80211_F_PUREG) && rate < 48) {
			ratesetmismatch(ni, wh, reassoc, resp, "11g", rate);
			vap->iv_stats.is_rx_assoc_norate++;
			return;
		}

		/*
		 * Do HT rate set handling and setup HT node state.
		 */
		ni->ni_chan = vap->iv_bss->ni_chan;

		/* VHT */
		if (IEEE80211_IS_CHAN_VHT(ni->ni_chan) &&
		    vhtcap != NULL &&
		    vhtinfo != NULL) {
			/* XXX TODO; see below */
			printf("%s: VHT TODO!\n", __func__);
			ieee80211_vht_node_init(ni);
			ieee80211_vht_update_cap(ni, vhtcap, vhtinfo);
		} else if (ni->ni_flags & IEEE80211_NODE_VHT)
			ieee80211_vht_node_cleanup(ni);

		/* HT */
		if (IEEE80211_IS_CHAN_HT(ni->ni_chan) && htcap != NULL) {
			rate = ieee80211_setup_htrates(ni, htcap,
				IEEE80211_F_DOFMCS | IEEE80211_F_DONEGO |
				IEEE80211_F_DOBRS);
			if (rate & IEEE80211_RATE_BASIC) {
				ratesetmismatch(ni, wh, reassoc, resp,
				    "HT", rate);
				vap->iv_stats.is_ht_assoc_norate++;
				return;
			}
			ieee80211_ht_node_init(ni);
			ieee80211_ht_updatehtcap(ni, htcap);
		} else if (ni->ni_flags & IEEE80211_NODE_HT)
			ieee80211_ht_node_cleanup(ni);

		/* Finally - this will use HT/VHT info to change node channel */
		if (IEEE80211_IS_CHAN_HT(ni->ni_chan) && htcap != NULL) {
			ieee80211_ht_updatehtcap_final(ni);
		}

#ifdef IEEE80211_SUPPORT_SUPERG
		/* Always do ff node cleanup; for A-MSDU */
		ieee80211_ff_node_cleanup(ni);
#endif
		/*
		 * Allow AMPDU operation only with unencrypted traffic
		 * or AES-CCM; the 11n spec only specifies these ciphers
		 * so permitting any others is undefined and can lead
		 * to interoperability problems.
		 */
		if ((ni->ni_flags & IEEE80211_NODE_HT) &&
		    (((vap->iv_flags & IEEE80211_F_WPA) &&
		      rsnparms.rsn_ucastcipher != IEEE80211_CIPHER_AES_CCM) ||
		     (vap->iv_flags & (IEEE80211_F_WPA|IEEE80211_F_PRIVACY)) == IEEE80211_F_PRIVACY)) {
			IEEE80211_NOTE(vap,
			    IEEE80211_MSG_ASSOC | IEEE80211_MSG_11N, ni,
			    "disallow HT use because WEP or TKIP requested, "
			    "capinfo 0x%x ucastcipher %d", capinfo,
			    rsnparms.rsn_ucastcipher);
			ieee80211_ht_node_cleanup(ni);
#ifdef IEEE80211_SUPPORT_SUPERG
			/* Always do ff node cleanup; for A-MSDU */
			ieee80211_ff_node_cleanup(ni);
#endif
			vap->iv_stats.is_ht_assoc_downgrade++;
		}
		/*
		 * If constrained to 11n-only stations reject legacy stations.
		 */
		if ((vap->iv_flags_ht & IEEE80211_FHT_PUREN) &&
		    (ni->ni_flags & IEEE80211_NODE_HT) == 0) {
			htcapmismatch(ni, wh, reassoc, resp);
			vap->iv_stats.is_ht_assoc_nohtcap++;
			return;
		}
		IEEE80211_RSSI_LPF(ni->ni_avgrssi, rssi);
		ni->ni_noise = nf;
		ni->ni_intval = lintval;
		ni->ni_capinfo = capinfo;
		ni->ni_fhdwell = vap->iv_bss->ni_fhdwell;
		ni->ni_fhindex = vap->iv_bss->ni_fhindex;
		/*
		 * Store the IEs.
		 * XXX maybe better to just expand
		 */
		if (ieee80211_ies_init(&ni->ni_ies, sfrm, efrm - sfrm)) {
#define	setie(_ie, _off)	ieee80211_ies_setie(ni->ni_ies, _ie, _off)
			if (wpa != NULL)
				setie(wpa_ie, wpa - sfrm);
			if (rsn != NULL)
				setie(rsn_ie, rsn - sfrm);
			if (htcap != NULL)
				setie(htcap_ie, htcap - sfrm);
			if (wme != NULL) {
				setie(wme_ie, wme - sfrm);
				/*
				 * Mark node as capable of QoS.
				 */
				ni->ni_flags |= IEEE80211_NODE_QOS;
			} else
				ni->ni_flags &= ~IEEE80211_NODE_QOS;
#ifdef IEEE80211_SUPPORT_SUPERG
			if (ath != NULL) {
				setie(ath_ie, ath - sfrm);
				/* 
				 * Parse ATH station parameters.
				 */
				ieee80211_parse_ath(ni, ni->ni_ies.ath_ie);
			} else
#endif
				ni->ni_ath_flags = 0;
#undef setie
		} else {
			ni->ni_flags &= ~IEEE80211_NODE_QOS;
			ni->ni_ath_flags = 0;
		}
		ieee80211_node_join(ni, resp);
		ieee80211_deliver_l2uf(ni);
		break;
	}

	case IEEE80211_FC0_SUBTYPE_DEAUTH:
	case IEEE80211_FC0_SUBTYPE_DISASSOC: {
		uint16_t reason;

		if (vap->iv_state != IEEE80211_S_RUN ||
		    /* NB: can happen when in promiscuous mode */
		    !IEEE80211_ADDR_EQ(wh->i_addr1, vap->iv_myaddr)) {
			vap->iv_stats.is_rx_mgtdiscard++;
			break;
		}
		/*
		 * deauth/disassoc frame format
		 *	[2] reason
		 */
		IEEE80211_VERIFY_LENGTH(efrm - frm, 2, return);
		reason = le16toh(*(uint16_t *)frm);
		if (subtype == IEEE80211_FC0_SUBTYPE_DEAUTH) {
			vap->iv_stats.is_rx_deauth++;
			IEEE80211_NODE_STAT(ni, rx_deauth);
		} else {
			vap->iv_stats.is_rx_disassoc++;
			IEEE80211_NODE_STAT(ni, rx_disassoc);
		}
		IEEE80211_NOTE(vap, IEEE80211_MSG_AUTH, ni,
		    "recv %s (reason: %d (%s))",
		    ieee80211_mgt_subtype_name(subtype),
		    reason, ieee80211_reason_to_string(reason));
		if (ni != vap->iv_bss)
			ieee80211_node_leave(ni);
		break;
	}

	case IEEE80211_FC0_SUBTYPE_ACTION:
	case IEEE80211_FC0_SUBTYPE_ACTION_NOACK:
		if (ni == vap->iv_bss) {
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

	case IEEE80211_FC0_SUBTYPE_ASSOC_RESP:
	case IEEE80211_FC0_SUBTYPE_REASSOC_RESP:
	case IEEE80211_FC0_SUBTYPE_TIMING_ADV:
	case IEEE80211_FC0_SUBTYPE_ATIM:
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

static void
hostap_recv_ctl(struct ieee80211_node *ni, struct mbuf *m, int subtype)
{
	switch (subtype) {
	case IEEE80211_FC0_SUBTYPE_PS_POLL:
		ni->ni_vap->iv_recv_pspoll(ni, m);
		break;
	case IEEE80211_FC0_SUBTYPE_BAR:
		ieee80211_recv_bar(ni, m);
		break;
	}
}

/*
 * Process a received ps-poll frame.
 */
void
ieee80211_recv_pspoll(struct ieee80211_node *ni, struct mbuf *m0)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_frame_min *wh;
	struct mbuf *m;
	uint16_t aid;
	int qlen;

	wh = mtod(m0, struct ieee80211_frame_min *);
	if (ni->ni_associd == 0) {
		IEEE80211_DISCARD(vap,
		    IEEE80211_MSG_POWER | IEEE80211_MSG_DEBUG,
		    (struct ieee80211_frame *) wh, NULL,
		    "%s", "unassociated station");
		vap->iv_stats.is_ps_unassoc++;
		IEEE80211_SEND_MGMT(ni, IEEE80211_FC0_SUBTYPE_DEAUTH,
			IEEE80211_REASON_NOT_ASSOCED);
		return;
	}

	aid = le16toh(*(uint16_t *)wh->i_dur);
	if (aid != ni->ni_associd) {
		IEEE80211_DISCARD(vap,
		    IEEE80211_MSG_POWER | IEEE80211_MSG_DEBUG,
		    (struct ieee80211_frame *) wh, NULL,
		    "aid mismatch: sta aid 0x%x poll aid 0x%x",
		    ni->ni_associd, aid);
		vap->iv_stats.is_ps_badaid++;
		/*
		 * NB: We used to deauth the station but it turns out
		 * the Blackberry Curve 8230 (and perhaps other devices) 
		 * sometimes send the wrong AID when WME is negotiated.
		 * Being more lenient here seems ok as we already check
		 * the station is associated and we only return frames
		 * queued for the station (i.e. we don't use the AID).
		 */
		return;
	}

	/* Okay, take the first queued packet and put it out... */
	m = ieee80211_node_psq_dequeue(ni, &qlen);
	if (m == NULL) {
		IEEE80211_NOTE_MAC(vap, IEEE80211_MSG_POWER, wh->i_addr2,
		    "%s", "recv ps-poll, but queue empty");
		ieee80211_send_nulldata(ieee80211_ref_node(ni));
		vap->iv_stats.is_ps_qempty++;	/* XXX node stat */
		if (vap->iv_set_tim != NULL)
			vap->iv_set_tim(ni, 0);	/* just in case */
		return;
	}
	/* 
	 * If there are more packets, set the more packets bit
	 * in the packet dispatched to the station; otherwise
	 * turn off the TIM bit.
	 */
	if (qlen != 0) {
		IEEE80211_NOTE(vap, IEEE80211_MSG_POWER, ni,
		    "recv ps-poll, send packet, %u still queued", qlen);
		m->m_flags |= M_MORE_DATA;
	} else {
		IEEE80211_NOTE(vap, IEEE80211_MSG_POWER, ni,
		    "%s", "recv ps-poll, send packet, queue empty");
		if (vap->iv_set_tim != NULL)
			vap->iv_set_tim(ni, 0);
	}
	m->m_flags |= M_PWR_SAV;		/* bypass PS handling */

	/*
	 * Do the right thing; if it's an encap'ed frame then
	 * call ieee80211_parent_xmitpkt() else
	 * call ieee80211_vap_xmitpkt().
	 */
	if (m->m_flags & M_ENCAP) {
		(void) ieee80211_parent_xmitpkt(ic, m);
	} else {
		(void) ieee80211_vap_xmitpkt(vap, m);
	}
}
