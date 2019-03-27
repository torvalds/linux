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
 * IEEE 802.11 WDS mode support.
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
#include <net80211/ieee80211_wds.h>
#include <net80211/ieee80211_input.h>
#ifdef IEEE80211_SUPPORT_SUPERG
#include <net80211/ieee80211_superg.h>
#endif

static void wds_vattach(struct ieee80211vap *);
static int wds_newstate(struct ieee80211vap *, enum ieee80211_state, int);
static	int wds_input(struct ieee80211_node *ni, struct mbuf *m,
	    const struct ieee80211_rx_stats *rxs, int, int);
static void wds_recv_mgmt(struct ieee80211_node *, struct mbuf *, int subtype,
	const struct ieee80211_rx_stats *, int, int);

void
ieee80211_wds_attach(struct ieee80211com *ic)
{
	ic->ic_vattach[IEEE80211_M_WDS] = wds_vattach;
}

void
ieee80211_wds_detach(struct ieee80211com *ic)
{
}

static void
wds_vdetach(struct ieee80211vap *vap)
{
	if (vap->iv_bss != NULL) {
		/* XXX locking? */
		if (vap->iv_bss->ni_wdsvap == vap)
			vap->iv_bss->ni_wdsvap = NULL;
	}
}

static void
wds_vattach(struct ieee80211vap *vap)
{
	vap->iv_newstate = wds_newstate;
	vap->iv_input = wds_input;
	vap->iv_recv_mgmt = wds_recv_mgmt;
	vap->iv_opdetach = wds_vdetach;
}

static void
wds_flush(struct ieee80211_node *ni)
{
	struct ieee80211com *ic = ni->ni_ic;
	struct mbuf *m, *next;
	int8_t rssi, nf;

	m = ieee80211_ageq_remove(&ic->ic_stageq,
	    (void *)(uintptr_t) ieee80211_mac_hash(ic, ni->ni_macaddr));
	if (m == NULL)
		return;

	IEEE80211_NOTE(ni->ni_vap, IEEE80211_MSG_WDS, ni,
	    "%s", "flush wds queue");
	ic->ic_node_getsignal(ni, &rssi, &nf);
	for (; m != NULL; m = next) {
		next = m->m_nextpkt;
		m->m_nextpkt = NULL;
		ieee80211_input(ni, m, rssi, nf);
	}
}

static int
ieee80211_create_wds(struct ieee80211vap *vap, struct ieee80211_channel *chan)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_node_table *nt = &ic->ic_sta;
	struct ieee80211_node *ni, *obss;

	IEEE80211_DPRINTF(vap, IEEE80211_MSG_WDS,
	     "%s: creating link to %s on channel %u\n", __func__,
	     ether_sprintf(vap->iv_des_bssid), ieee80211_chan2ieee(ic, chan));

	/* NB: vap create must specify the bssid for the link */
	KASSERT(vap->iv_flags & IEEE80211_F_DESBSSID, ("no bssid"));
	/* NB: we should only be called on RUN transition */
	KASSERT(vap->iv_state == IEEE80211_S_RUN, ("!RUN state"));

	if ((vap->iv_flags_ext & IEEE80211_FEXT_WDSLEGACY) == 0) {
		/*
		 * Dynamic/non-legacy WDS.  Reference the associated
		 * station specified by the desired bssid setup at vap
		 * create.  Point ni_wdsvap at the WDS vap so 4-address
		 * frames received through the associated AP vap will
		 * be dispatched upward (e.g. to a bridge) as though
		 * they arrived on the WDS vap.
		 */
		IEEE80211_NODE_LOCK(nt);
		obss = NULL;
		ni = ieee80211_find_node_locked(&ic->ic_sta, vap->iv_des_bssid);
		if (ni == NULL) {
			/*
			 * Node went away before we could hookup.  This
			 * should be ok; no traffic will flow and a leave
			 * event will be dispatched that should cause
			 * the vap to be destroyed.
			 */
			IEEE80211_DPRINTF(vap, IEEE80211_MSG_WDS,
			    "%s: station %s went away\n",
			    __func__, ether_sprintf(vap->iv_des_bssid));
			/* XXX stat? */
		} else if (ni->ni_wdsvap != NULL) {
			/*
			 * Node already setup with a WDS vap; we cannot
			 * allow multiple references so disallow.  If
			 * ni_wdsvap points at us that's ok; we should
			 * do nothing anyway.
			 */
			/* XXX printf instead? */
			IEEE80211_DPRINTF(vap, IEEE80211_MSG_WDS,
			    "%s: station %s in use with %s\n",
			    __func__, ether_sprintf(vap->iv_des_bssid),
			    ni->ni_wdsvap->iv_ifp->if_xname);
			/* XXX stat? */
		} else {
			/*
			 * Committed to new node, setup state.
			 */
			obss = vap->iv_bss;
			vap->iv_bss = ni;
			ni->ni_wdsvap = vap;
		}
		IEEE80211_NODE_UNLOCK(nt);
		if (obss != NULL) {
			/* NB: deferred to avoid recursive lock */
			ieee80211_free_node(obss);
		}
	} else {
		/*
		 * Legacy WDS vap setup.
		 */
		/*
		 * The far end does not associate so we just create
		 * create a new node and install it as the vap's
		 * bss node.  We must simulate an association and
		 * authorize the port for traffic to flow.
		 * XXX check if node already in sta table?
		 */
		ni = ieee80211_node_create_wds(vap, vap->iv_des_bssid, chan);
		if (ni != NULL) {
			obss = vap->iv_bss;
			vap->iv_bss = ieee80211_ref_node(ni);
			ni->ni_flags |= IEEE80211_NODE_AREF;
			if (obss != NULL)
				ieee80211_free_node(obss);
			/* give driver a chance to setup state like ni_txrate */
			if (ic->ic_newassoc != NULL)
				ic->ic_newassoc(ni, 1);
			/* tell the authenticator about new station */
			if (vap->iv_auth->ia_node_join != NULL)
				vap->iv_auth->ia_node_join(ni);
			if (ni->ni_authmode != IEEE80211_AUTH_8021X)
				ieee80211_node_authorize(ni);

			ieee80211_notify_node_join(ni, 1 /*newassoc*/);
			/* XXX inject l2uf frame */
		}
	}

	/*
	 * Flush any pending frames now that were setup.
	 */
	if (ni != NULL)
		wds_flush(ni);
	return (ni == NULL ? ENOENT : 0);
}

/*
 * Propagate multicast frames of an ap vap to all DWDS links.
 * The caller is assumed to have verified this frame is multicast.
 */
void
ieee80211_dwds_mcast(struct ieee80211vap *vap0, struct mbuf *m)
{
	struct ieee80211com *ic = vap0->iv_ic;
	const struct ether_header *eh = mtod(m, const struct ether_header *);
	struct ieee80211_node *ni;
	struct ieee80211vap *vap;
	struct ifnet *ifp;
	struct mbuf *mcopy;
	int err;

	KASSERT(ETHER_IS_MULTICAST(eh->ether_dhost),
	    ("%s not mcast", ether_sprintf(eh->ether_dhost)));

	/* XXX locking */
	TAILQ_FOREACH(vap, &ic->ic_vaps, iv_next) {
		/* only DWDS vaps are interesting */
		if (vap->iv_opmode != IEEE80211_M_WDS ||
		    (vap->iv_flags_ext & IEEE80211_FEXT_WDSLEGACY))
			continue;
		/* if it came in this interface, don't send it back out */
		ifp = vap->iv_ifp;
		if (ifp == m->m_pkthdr.rcvif)
			continue;
		/*
		 * Duplicate the frame and send it.
		 */
		mcopy = m_copypacket(m, M_NOWAIT);
		if (mcopy == NULL) {
			if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
			/* XXX stat + msg */
			continue;
		}
		ni = ieee80211_find_txnode(vap, eh->ether_dhost);
		if (ni == NULL) {
			/* NB: ieee80211_find_txnode does stat+msg */
			if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
			m_freem(mcopy);
			continue;
		}
		/* calculate priority so drivers can find the tx queue */
		if (ieee80211_classify(ni, mcopy)) {
			IEEE80211_DISCARD_MAC(vap,
			    IEEE80211_MSG_OUTPUT | IEEE80211_MSG_WDS,
			    eh->ether_dhost, NULL,
			    "%s", "classification failure");
			vap->iv_stats.is_tx_classify++;
			if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
			m_freem(mcopy);
			ieee80211_free_node(ni);
			continue;
		}

		BPF_MTAP(ifp, m);		/* 802.3 tx */

		/*
		 * Encapsulate the packet in prep for transmission.
		 */
		IEEE80211_TX_LOCK(ic);
		mcopy = ieee80211_encap(vap, ni, mcopy);
		if (mcopy == NULL) {
			/* NB: stat+msg handled in ieee80211_encap */
			IEEE80211_TX_UNLOCK(ic);
			ieee80211_free_node(ni);
			continue;
		}
		mcopy->m_flags |= M_MCAST;
		mcopy->m_pkthdr.rcvif = (void *) ni;

		err = ieee80211_parent_xmitpkt(ic, mcopy);
		IEEE80211_TX_UNLOCK(ic);
		if (!err) {
			if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
			if_inc_counter(ifp, IFCOUNTER_OMCASTS, 1);
			if_inc_counter(ifp, IFCOUNTER_OBYTES,
			    m->m_pkthdr.len);
		}
	}
}

/*
 * Handle DWDS discovery on receipt of a 4-address frame in
 * ap mode.  Queue the frame and post an event for someone
 * to plumb the necessary WDS vap for this station.  Frames
 * received prior to the vap set running will then be reprocessed
 * as if they were just received.
 */
void
ieee80211_dwds_discover(struct ieee80211_node *ni, struct mbuf *m)
{
	struct ieee80211com *ic = ni->ni_ic;

	/*
	 * Save the frame with an aging interval 4 times
	 * the listen interval specified by the station. 
	 * Frames that sit around too long are reclaimed
	 * using this information.
	 * XXX handle overflow?
	 * XXX per/vap beacon interval?
	 */
	m->m_pkthdr.rcvif = (void *)(uintptr_t)
	    ieee80211_mac_hash(ic, ni->ni_macaddr);
	(void) ieee80211_ageq_append(&ic->ic_stageq, m,
	    ((ni->ni_intval * ic->ic_lintval) << 2) / 1024);
	ieee80211_notify_wds_discover(ni);
}

/*
 * IEEE80211_M_WDS vap state machine handler.
 */
static int
wds_newstate(struct ieee80211vap *vap, enum ieee80211_state nstate, int arg)
{
	struct ieee80211com *ic = vap->iv_ic;
	enum ieee80211_state ostate;
	int error;

	IEEE80211_LOCK_ASSERT(ic);

	ostate = vap->iv_state;
	IEEE80211_DPRINTF(vap, IEEE80211_MSG_STATE, "%s: %s -> %s\n", __func__,
		ieee80211_state_name[ostate], ieee80211_state_name[nstate]);
	vap->iv_state = nstate;			/* state transition */
	callout_stop(&vap->iv_mgtsend);		/* XXX callout_drain */
	if (ostate != IEEE80211_S_SCAN)
		ieee80211_cancel_scan(vap);	/* background scan */
	error = 0;
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
		case IEEE80211_S_INIT:
			ieee80211_check_scan_current(vap);
			break;
		default:
			break;
		}
		break;
	case IEEE80211_S_RUN:
		if (ostate == IEEE80211_S_INIT) {
			/*
			 * Already have a channel; bypass the scan
			 * and startup immediately.
			 */
			error = ieee80211_create_wds(vap, ic->ic_curchan);
		}
		break;
	default:
		break;
	}
	return error;
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
wds_input(struct ieee80211_node *ni, struct mbuf *m,
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

	if (!IEEE80211_IS_MULTICAST(wh->i_addr1))
		ni->ni_inact = ni->ni_inact_reload;

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

	/* NB: WDS vap's do not scan */
	if (m->m_pkthdr.len < sizeof(struct ieee80211_frame_addr4)) {
		IEEE80211_DISCARD_MAC(vap,
		    IEEE80211_MSG_ANY, ni->ni_macaddr, NULL,
		    "too short (3): len %u", m->m_pkthdr.len);
		vap->iv_stats.is_rx_tooshort++;
		goto out;
	}
	/* NB: the TA is implicitly verified by finding the wds peer node */
	if (!IEEE80211_ADDR_EQ(wh->i_addr1, vap->iv_myaddr) &&
	    !IEEE80211_ADDR_EQ(wh->i_addr1, ifp->if_broadcastaddr)) {
		/* not interested in */
		IEEE80211_DISCARD_MAC(vap, IEEE80211_MSG_INPUT,
		    wh->i_addr1, NULL, "%s", "not to bss");
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
		if (! ieee80211_check_rxseq(ni, wh, wh->i_addr1, rxs))
			goto out;
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
		if (dir != IEEE80211_FC1_DIR_DSTODS) {
			IEEE80211_DISCARD(vap, IEEE80211_MSG_INPUT,
			    wh, "data", "incorrect dir 0x%x", dir);
			vap->iv_stats.is_rx_wrongdir++;
			goto out;
		}
		/*
		 * Only legacy WDS traffic should take this path.
		 */
		if ((vap->iv_flags_ext & IEEE80211_FEXT_WDSLEGACY) == 0) {
			IEEE80211_DISCARD(vap, IEEE80211_MSG_INPUT,
			    wh, "data", "%s", "not legacy wds");
			vap->iv_stats.is_rx_wrongdir++;/*XXX*/
			goto out;
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
		if (ieee80211_msg_debug(vap) || ieee80211_msg_dumppkts(vap)) {
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
wds_recv_mgmt(struct ieee80211_node *ni, struct mbuf *m0, int subtype,
    const struct ieee80211_rx_stats *rxs, int rssi, int nf)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211com *ic = ni->ni_ic;
	struct ieee80211_frame *wh;
	u_int8_t *frm, *efrm;

	wh = mtod(m0, struct ieee80211_frame *);
	frm = (u_int8_t *)&wh[1];
	efrm = mtod(m0, u_int8_t *) + m0->m_len;
	switch (subtype) {
	case IEEE80211_FC0_SUBTYPE_ACTION:
	case IEEE80211_FC0_SUBTYPE_ACTION_NOACK:
		if (ni == vap->iv_bss) {
			IEEE80211_DISCARD(vap, IEEE80211_MSG_INPUT,
			    wh, NULL, "%s", "unknown node");
			vap->iv_stats.is_rx_mgtdiscard++;
		} else if (!IEEE80211_ADDR_EQ(vap->iv_myaddr, wh->i_addr1)) {
			/* NB: not interested in multicast frames. */
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
	case IEEE80211_FC0_SUBTYPE_PROBE_REQ:
	case IEEE80211_FC0_SUBTYPE_PROBE_RESP:
	case IEEE80211_FC0_SUBTYPE_TIMING_ADV:
	case IEEE80211_FC0_SUBTYPE_BEACON:
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
