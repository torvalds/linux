/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Atsushi Onoe
 * Copyright (c) 2002-2009 Sam Leffler, Errno Consulting
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
__FBSDID("$FreeBSD$");

#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>   
#include <sys/malloc.h>
#include <sys/endian.h>
#include <sys/kernel.h>
 
#include <sys/socket.h>
 
#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_llc.h>
#include <net/if_media.h>
#include <net/if_vlan_var.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_input.h>
#ifdef IEEE80211_SUPPORT_MESH
#include <net80211/ieee80211_mesh.h>
#endif

#include <net/bpf.h>

#ifdef INET
#include <netinet/in.h>
#include <net/ethernet.h>
#endif

static void
ieee80211_process_mimo(struct ieee80211_node *ni, struct ieee80211_rx_stats *rx)
{
	int i;

	/* Verify the required MIMO bits are set */
	if ((rx->r_flags & (IEEE80211_R_C_CHAIN | IEEE80211_R_C_NF | IEEE80211_R_C_RSSI)) !=
	    (IEEE80211_R_C_CHAIN | IEEE80211_R_C_NF | IEEE80211_R_C_RSSI))
		return;

	/* XXX This assumes the MIMO radios have both ctl and ext chains */
	for (i = 0; i < MIN(rx->c_chain, IEEE80211_MAX_CHAINS); i++) {
		IEEE80211_RSSI_LPF(ni->ni_mimo_rssi_ctl[i], rx->c_rssi_ctl[i]);
		IEEE80211_RSSI_LPF(ni->ni_mimo_rssi_ext[i], rx->c_rssi_ext[i]);
	}

	/* XXX This also assumes the MIMO radios have both ctl and ext chains */
	for(i = 0; i < MIN(rx->c_chain, IEEE80211_MAX_CHAINS); i++) {
		ni->ni_mimo_noise_ctl[i] = rx->c_nf_ctl[i];
		ni->ni_mimo_noise_ext[i] = rx->c_nf_ext[i];
	}
	ni->ni_mimo_chains = rx->c_chain;
}

int
ieee80211_input_mimo(struct ieee80211_node *ni, struct mbuf *m)
{
	struct ieee80211_rx_stats rxs;

	/* try to read stats from mbuf */
	bzero(&rxs, sizeof(rxs));
	if (ieee80211_get_rx_params(m, &rxs) != 0)
		return (-1);

	/* XXX should assert IEEE80211_R_NF and IEEE80211_R_RSSI are set */
	ieee80211_process_mimo(ni, &rxs);

	//return ieee80211_input(ni, m, rx->rssi, rx->nf);
	return ni->ni_vap->iv_input(ni, m, &rxs, rxs.c_rssi, rxs.c_nf);
}

int
ieee80211_input_all(struct ieee80211com *ic, struct mbuf *m, int rssi, int nf)
{
	struct ieee80211_rx_stats rx;

	rx.r_flags = IEEE80211_R_NF | IEEE80211_R_RSSI;
	rx.c_nf = nf;
	rx.c_rssi = rssi;

	if (!ieee80211_add_rx_params(m, &rx))
		return (-1);

	return ieee80211_input_mimo_all(ic, m);
}

int
ieee80211_input_mimo_all(struct ieee80211com *ic, struct mbuf *m)
{
	struct ieee80211vap *vap;
	int type = -1;

	m->m_flags |= M_BCAST;		/* NB: mark for bpf tap'ing */

	/* XXX locking */
	TAILQ_FOREACH(vap, &ic->ic_vaps, iv_next) {
		struct ieee80211_node *ni;
		struct mbuf *mcopy;

		/* NB: could check for IFF_UP but this is cheaper */
		if (vap->iv_state == IEEE80211_S_INIT)
			continue;
		/*
		 * WDS vap's only receive directed traffic from the
		 * station at the ``far end''.  That traffic should
		 * be passed through the AP vap the station is associated
		 * to--so don't spam them with mcast frames.
		 */
		if (vap->iv_opmode == IEEE80211_M_WDS)
			continue;
		if (TAILQ_NEXT(vap, iv_next) != NULL) {
			/*
			 * Packet contents are changed by ieee80211_decap
			 * so do a deep copy of the packet.
			 * NB: tags are copied too.
			 */
			mcopy = m_dup(m, M_NOWAIT);
			if (mcopy == NULL) {
				/* XXX stat+msg */
				continue;
			}
		} else {
			mcopy = m;
			m = NULL;
		}
		ni = ieee80211_ref_node(vap->iv_bss);
		type = ieee80211_input_mimo(ni, mcopy);
		ieee80211_free_node(ni);
	}
	if (m != NULL)			/* no vaps, reclaim mbuf */
		m_freem(m);
	return type;
}

/*
 * This function reassembles fragments.
 *
 * XXX should handle 3 concurrent reassemblies per-spec.
 */
struct mbuf *
ieee80211_defrag(struct ieee80211_node *ni, struct mbuf *m, int hdrspace)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211_frame *wh = mtod(m, struct ieee80211_frame *);
	struct ieee80211_frame *lwh;
	uint16_t rxseq;
	uint8_t fragno;
	uint8_t more_frag = wh->i_fc[1] & IEEE80211_FC1_MORE_FRAG;
	struct mbuf *mfrag;

	KASSERT(!IEEE80211_IS_MULTICAST(wh->i_addr1), ("multicast fragm?"));

	rxseq = le16toh(*(uint16_t *)wh->i_seq);
	fragno = rxseq & IEEE80211_SEQ_FRAG_MASK;

	/* Quick way out, if there's nothing to defragment */
	if (!more_frag && fragno == 0 && ni->ni_rxfrag[0] == NULL)
		return m;

	/*
	 * Remove frag to insure it doesn't get reaped by timer.
	 */
	if (ni->ni_table == NULL) {
		/*
		 * Should never happen.  If the node is orphaned (not in
		 * the table) then input packets should not reach here.
		 * Otherwise, a concurrent request that yanks the table
		 * should be blocked by other interlocking and/or by first
		 * shutting the driver down.  Regardless, be defensive
		 * here and just bail
		 */
		/* XXX need msg+stat */
		m_freem(m);
		return NULL;
	}
	IEEE80211_NODE_LOCK(ni->ni_table);
	mfrag = ni->ni_rxfrag[0];
	ni->ni_rxfrag[0] = NULL;
	IEEE80211_NODE_UNLOCK(ni->ni_table);

	/*
	 * Validate new fragment is in order and
	 * related to the previous ones.
	 */
	if (mfrag != NULL) {
		uint16_t last_rxseq;

		lwh = mtod(mfrag, struct ieee80211_frame *);
		last_rxseq = le16toh(*(uint16_t *)lwh->i_seq);
		/* NB: check seq # and frag together */
		if (rxseq == last_rxseq+1 &&
		    IEEE80211_ADDR_EQ(wh->i_addr1, lwh->i_addr1) &&
		    IEEE80211_ADDR_EQ(wh->i_addr2, lwh->i_addr2)) {
			/* XXX clear MORE_FRAG bit? */
			/* track last seqnum and fragno */
			*(uint16_t *) lwh->i_seq = *(uint16_t *) wh->i_seq;

			m_adj(m, hdrspace);		/* strip header */
			m_catpkt(mfrag, m);		/* concatenate */
		} else {
			/*
			 * Unrelated fragment or no space for it,
			 * clear current fragments.
			 */
			m_freem(mfrag);
			mfrag = NULL;
		}
	}

 	if (mfrag == NULL) {
		if (fragno != 0) {		/* !first fragment, discard */
			vap->iv_stats.is_rx_defrag++;
			IEEE80211_NODE_STAT(ni, rx_defrag);
			m_freem(m);
			return NULL;
		}
		mfrag = m;
	}
	if (more_frag) {			/* more to come, save */
		ni->ni_rxfragstamp = ticks;
		ni->ni_rxfrag[0] = mfrag;
		mfrag = NULL;
	}
	return mfrag;
}

void
ieee80211_deliver_data(struct ieee80211vap *vap,
	struct ieee80211_node *ni, struct mbuf *m)
{
	struct ether_header *eh = mtod(m, struct ether_header *);
	struct ifnet *ifp = vap->iv_ifp;

	/* clear driver/net80211 flags before passing up */
	m->m_flags &= ~(M_MCAST | M_BCAST);
	m_clrprotoflags(m);

	/* NB: see hostap_deliver_data, this path doesn't handle hostap */
	KASSERT(vap->iv_opmode != IEEE80211_M_HOSTAP, ("gack, hostap"));
	/*
	 * Do accounting.
	 */
	if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
	IEEE80211_NODE_STAT(ni, rx_data);
	IEEE80211_NODE_STAT_ADD(ni, rx_bytes, m->m_pkthdr.len);
	if (ETHER_IS_MULTICAST(eh->ether_dhost)) {
		if (ETHER_IS_BROADCAST(eh->ether_dhost))
			m->m_flags |= M_BCAST;
		else
			m->m_flags |= M_MCAST;
		IEEE80211_NODE_STAT(ni, rx_mcast);
	} else
		IEEE80211_NODE_STAT(ni, rx_ucast);
	m->m_pkthdr.rcvif = ifp;

	if (ni->ni_vlan != 0) {
		/* attach vlan tag */
		m->m_pkthdr.ether_vtag = ni->ni_vlan;
		m->m_flags |= M_VLANTAG;
	}
	ifp->if_input(ifp, m);
}

struct mbuf *
ieee80211_decap(struct ieee80211vap *vap, struct mbuf *m, int hdrlen)
{
	struct ieee80211_qosframe_addr4 wh;
	struct ether_header *eh;
	struct llc *llc;

	KASSERT(hdrlen <= sizeof(wh),
	    ("hdrlen %d > max %zd", hdrlen, sizeof(wh)));

	if (m->m_len < hdrlen + sizeof(*llc) &&
	    (m = m_pullup(m, hdrlen + sizeof(*llc))) == NULL) {
		vap->iv_stats.is_rx_tooshort++;
		/* XXX msg */
		return NULL;
	}
	memcpy(&wh, mtod(m, caddr_t), hdrlen);
	llc = (struct llc *)(mtod(m, caddr_t) + hdrlen);
	if (llc->llc_dsap == LLC_SNAP_LSAP && llc->llc_ssap == LLC_SNAP_LSAP &&
	    llc->llc_control == LLC_UI && llc->llc_snap.org_code[0] == 0 &&
	    llc->llc_snap.org_code[1] == 0 && llc->llc_snap.org_code[2] == 0 &&
	    /* NB: preserve AppleTalk frames that have a native SNAP hdr */
	    !(llc->llc_snap.ether_type == htons(ETHERTYPE_AARP) ||
	      llc->llc_snap.ether_type == htons(ETHERTYPE_IPX))) {
		m_adj(m, hdrlen + sizeof(struct llc) - sizeof(*eh));
		llc = NULL;
	} else {
		m_adj(m, hdrlen - sizeof(*eh));
	}
	eh = mtod(m, struct ether_header *);
	switch (wh.i_fc[1] & IEEE80211_FC1_DIR_MASK) {
	case IEEE80211_FC1_DIR_NODS:
		IEEE80211_ADDR_COPY(eh->ether_dhost, wh.i_addr1);
		IEEE80211_ADDR_COPY(eh->ether_shost, wh.i_addr2);
		break;
	case IEEE80211_FC1_DIR_TODS:
		IEEE80211_ADDR_COPY(eh->ether_dhost, wh.i_addr3);
		IEEE80211_ADDR_COPY(eh->ether_shost, wh.i_addr2);
		break;
	case IEEE80211_FC1_DIR_FROMDS:
		IEEE80211_ADDR_COPY(eh->ether_dhost, wh.i_addr1);
		IEEE80211_ADDR_COPY(eh->ether_shost, wh.i_addr3);
		break;
	case IEEE80211_FC1_DIR_DSTODS:
		IEEE80211_ADDR_COPY(eh->ether_dhost, wh.i_addr3);
		IEEE80211_ADDR_COPY(eh->ether_shost, wh.i_addr4);
		break;
	}
#ifndef __NO_STRICT_ALIGNMENT
	if (!ALIGNED_POINTER(mtod(m, caddr_t) + sizeof(*eh), uint32_t)) {
		m = ieee80211_realign(vap, m, sizeof(*eh));
		if (m == NULL)
			return NULL;
	}
#endif /* !__NO_STRICT_ALIGNMENT */
	if (llc != NULL) {
		eh = mtod(m, struct ether_header *);
		eh->ether_type = htons(m->m_pkthdr.len - sizeof(*eh));
	}
	return m;
}

/*
 * Decap a frame encapsulated in a fast-frame/A-MSDU.
 */
struct mbuf *
ieee80211_decap1(struct mbuf *m, int *framelen)
{
#define	FF_LLC_SIZE	(sizeof(struct ether_header) + sizeof(struct llc))
	struct ether_header *eh;
	struct llc *llc;

	/*
	 * The frame has an 802.3 header followed by an 802.2
	 * LLC header.  The encapsulated frame length is in the
	 * first header type field; save that and overwrite it 
	 * with the true type field found in the second.  Then
	 * copy the 802.3 header up to where it belongs and
	 * adjust the mbuf contents to remove the void.
	 */
	if (m->m_len < FF_LLC_SIZE && (m = m_pullup(m, FF_LLC_SIZE)) == NULL)
		return NULL;
	eh = mtod(m, struct ether_header *);	/* 802.3 header is first */
	llc = (struct llc *)&eh[1];		/* 802.2 header follows */
	*framelen = ntohs(eh->ether_type)	/* encap'd frame size */
		  + sizeof(struct ether_header) - sizeof(struct llc);
	eh->ether_type = llc->llc_un.type_snap.ether_type;
	ovbcopy(eh, mtod(m, uint8_t *) + sizeof(struct llc),
		sizeof(struct ether_header));
	m_adj(m, sizeof(struct llc));
	return m;
#undef FF_LLC_SIZE
}

/*
 * Install received rate set information in the node's state block.
 */
int
ieee80211_setup_rates(struct ieee80211_node *ni,
	const uint8_t *rates, const uint8_t *xrates, int flags)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211_rateset *rs = &ni->ni_rates;

	memset(rs, 0, sizeof(*rs));
	rs->rs_nrates = rates[1];
	memcpy(rs->rs_rates, rates + 2, rs->rs_nrates);
	if (xrates != NULL) {
		uint8_t nxrates;
		/*
		 * Tack on 11g extended supported rate element.
		 */
		nxrates = xrates[1];
		if (rs->rs_nrates + nxrates > IEEE80211_RATE_MAXSIZE) {
			nxrates = IEEE80211_RATE_MAXSIZE - rs->rs_nrates;
			IEEE80211_NOTE(vap, IEEE80211_MSG_XRATE, ni,
			    "extended rate set too large; only using "
			    "%u of %u rates", nxrates, xrates[1]);
			vap->iv_stats.is_rx_rstoobig++;
		}
		memcpy(rs->rs_rates + rs->rs_nrates, xrates+2, nxrates);
		rs->rs_nrates += nxrates;
	}
	return ieee80211_fix_rate(ni, rs, flags);
}

/*
 * Send a management frame error response to the specified
 * station.  If ni is associated with the station then use
 * it; otherwise allocate a temporary node suitable for
 * transmitting the frame and then free the reference so
 * it will go away as soon as the frame has been transmitted.
 */
void
ieee80211_send_error(struct ieee80211_node *ni,
	const uint8_t mac[IEEE80211_ADDR_LEN], int subtype, int arg)
{
	struct ieee80211vap *vap = ni->ni_vap;
	int istmp;

	if (ni == vap->iv_bss) {
		if (vap->iv_state != IEEE80211_S_RUN) {
			/*
			 * XXX hack until we get rid of this routine.
			 * We can be called prior to the vap reaching
			 * run state under certain conditions in which
			 * case iv_bss->ni_chan will not be setup.
			 * Check for this explicitly and and just ignore
			 * the request.
			 */
			return;
		}
		ni = ieee80211_tmp_node(vap, mac);
		if (ni == NULL) {
			/* XXX msg */
			return;
		}
		istmp = 1;
	} else
		istmp = 0;
	IEEE80211_SEND_MGMT(ni, subtype, arg);
	if (istmp)
		ieee80211_free_node(ni);
}

int
ieee80211_alloc_challenge(struct ieee80211_node *ni)
{
	if (ni->ni_challenge == NULL)
		ni->ni_challenge = (uint32_t *)
		    IEEE80211_MALLOC(IEEE80211_CHALLENGE_LEN,
		      M_80211_NODE, IEEE80211_M_NOWAIT);
	if (ni->ni_challenge == NULL) {
		IEEE80211_NOTE(ni->ni_vap,
		    IEEE80211_MSG_DEBUG | IEEE80211_MSG_AUTH, ni,
		    "%s", "shared key challenge alloc failed");
		/* XXX statistic */
	}
	return (ni->ni_challenge != NULL);
}

/*
 * Parse a Beacon or ProbeResponse frame and return the
 * useful information in an ieee80211_scanparams structure.
 * Status is set to 0 if no problems were found; otherwise
 * a bitmask of IEEE80211_BPARSE_* items is returned that
 * describes the problems detected.
 */
int
ieee80211_parse_beacon(struct ieee80211_node *ni, struct mbuf *m,
	struct ieee80211_channel *rxchan, struct ieee80211_scanparams *scan)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211com *ic = ni->ni_ic;
	struct ieee80211_frame *wh;
	uint8_t *frm, *efrm;

	wh = mtod(m, struct ieee80211_frame *);
	frm = (uint8_t *)&wh[1];
	efrm = mtod(m, uint8_t *) + m->m_len;
	scan->status = 0;
	/*
	 * beacon/probe response frame format
	 *
	 * XXX Update from 802.11-2012 - eg where HT is
	 *	[8] time stamp
	 *	[2] beacon interval
	 *	[2] capability information
	 *	[tlv] ssid
	 *	[tlv] supported rates
	 *	[tlv] country information
	 *	[tlv] channel switch announcement (CSA)
	 *	[tlv] parameter set (FH/DS)
	 *	[tlv] erp information
	 *	[tlv] extended supported rates
	 *	[tlv] WME
	 *	[tlv] WPA or RSN
	 *	[tlv] HT capabilities
	 *	[tlv] HT information
	 *	[tlv] VHT capabilities
	 *	[tlv] VHT information
	 *	[tlv] Atheros capabilities
	 *	[tlv] Mesh ID
	 *	[tlv] Mesh Configuration
	 */
	IEEE80211_VERIFY_LENGTH(efrm - frm, 12,
	    return (scan->status = IEEE80211_BPARSE_BADIELEN));
	memset(scan, 0, sizeof(*scan));
	scan->tstamp  = frm;				frm += 8;
	scan->bintval = le16toh(*(uint16_t *)frm);	frm += 2;
	scan->capinfo = le16toh(*(uint16_t *)frm);	frm += 2;
	scan->bchan = ieee80211_chan2ieee(ic, rxchan);
	scan->chan = scan->bchan;
	scan->ies = frm;
	scan->ies_len = efrm - frm;

	while (efrm - frm > 1) {
		IEEE80211_VERIFY_LENGTH(efrm - frm, frm[1] + 2,
		    return (scan->status = IEEE80211_BPARSE_BADIELEN));
		switch (*frm) {
		case IEEE80211_ELEMID_SSID:
			scan->ssid = frm;
			break;
		case IEEE80211_ELEMID_RATES:
			scan->rates = frm;
			break;
		case IEEE80211_ELEMID_COUNTRY:
			scan->country = frm;
			break;
		case IEEE80211_ELEMID_CSA:
			scan->csa = frm;
			break;
		case IEEE80211_ELEMID_QUIET:
			scan->quiet = frm;
			break;
		case IEEE80211_ELEMID_FHPARMS:
			if (ic->ic_phytype == IEEE80211_T_FH) {
				scan->fhdwell = le16dec(&frm[2]);
				scan->chan = IEEE80211_FH_CHAN(frm[4], frm[5]);
				scan->fhindex = frm[6];
			}
			break;
		case IEEE80211_ELEMID_DSPARMS:
			/*
			 * XXX hack this since depending on phytype
			 * is problematic for multi-mode devices.
			 */
			if (ic->ic_phytype != IEEE80211_T_FH)
				scan->chan = frm[2];
			break;
		case IEEE80211_ELEMID_TIM:
			/* XXX ATIM? */
			scan->tim = frm;
			scan->timoff = frm - mtod(m, uint8_t *);
			break;
		case IEEE80211_ELEMID_IBSSPARMS:
		case IEEE80211_ELEMID_CFPARMS:
		case IEEE80211_ELEMID_PWRCNSTR:
		case IEEE80211_ELEMID_BSSLOAD:
		case IEEE80211_ELEMID_APCHANREP:
			/* NB: avoid debugging complaints */
			break;
		case IEEE80211_ELEMID_XRATES:
			scan->xrates = frm;
			break;
		case IEEE80211_ELEMID_ERP:
			if (frm[1] != 1) {
				IEEE80211_DISCARD_IE(vap,
				    IEEE80211_MSG_ELEMID, wh, "ERP",
				    "bad len %u", frm[1]);
				vap->iv_stats.is_rx_elem_toobig++;
				break;
			}
			scan->erp = frm[2] | 0x100;
			break;
		case IEEE80211_ELEMID_HTCAP:
			scan->htcap = frm;
			break;
		case IEEE80211_ELEMID_VHT_CAP:
			scan->vhtcap = frm;
			break;
		case IEEE80211_ELEMID_VHT_OPMODE:
			scan->vhtopmode = frm;
			break;
		case IEEE80211_ELEMID_RSN:
			scan->rsn = frm;
			break;
		case IEEE80211_ELEMID_HTINFO:
			scan->htinfo = frm;
			break;
#ifdef IEEE80211_SUPPORT_MESH
		case IEEE80211_ELEMID_MESHID:
			scan->meshid = frm;
			break;
		case IEEE80211_ELEMID_MESHCONF:
			scan->meshconf = frm;
			break;
#endif
		/* Extended capabilities; nothing handles it for now */
		case IEEE80211_ELEMID_EXTCAP:
			break;
		case IEEE80211_ELEMID_VENDOR:
			if (iswpaoui(frm))
				scan->wpa = frm;
			else if (iswmeparam(frm) || iswmeinfo(frm))
				scan->wme = frm;
#ifdef IEEE80211_SUPPORT_SUPERG
			else if (isatherosoui(frm))
				scan->ath = frm;
#endif
#ifdef IEEE80211_SUPPORT_TDMA
			else if (istdmaoui(frm))
				scan->tdma = frm;
#endif
			else if (vap->iv_flags_ht & IEEE80211_FHT_HTCOMPAT) {
				/*
				 * Accept pre-draft HT ie's if the
				 * standard ones have not been seen.
				 */
				if (ishtcapoui(frm)) {
					if (scan->htcap == NULL)
						scan->htcap = frm;
				} else if (ishtinfooui(frm)) {
					if (scan->htinfo == NULL)
						scan->htcap = frm;
				}
			}
			break;
		default:
			IEEE80211_DISCARD_IE(vap, IEEE80211_MSG_ELEMID,
			    wh, "unhandled",
			    "id %u, len %u", *frm, frm[1]);
			vap->iv_stats.is_rx_elem_unknown++;
			break;
		}
		frm += frm[1] + 2;
	}
	IEEE80211_VERIFY_ELEMENT(scan->rates, IEEE80211_RATE_MAXSIZE,
	    scan->status |= IEEE80211_BPARSE_RATES_INVALID);
	if (scan->rates != NULL && scan->xrates != NULL) {
		/*
		 * NB: don't process XRATES if RATES is missing.  This
		 * avoids a potential null ptr deref and should be ok
		 * as the return code will already note RATES is missing
		 * (so callers shouldn't otherwise process the frame).
		 */
		IEEE80211_VERIFY_ELEMENT(scan->xrates,
		    IEEE80211_RATE_MAXSIZE - scan->rates[1],
		    scan->status |= IEEE80211_BPARSE_XRATES_INVALID);
	}
	IEEE80211_VERIFY_ELEMENT(scan->ssid, IEEE80211_NWID_LEN,
	    scan->status |= IEEE80211_BPARSE_SSID_INVALID);
	if (scan->chan != scan->bchan && ic->ic_phytype != IEEE80211_T_FH) {
		/*
		 * Frame was received on a channel different from the
		 * one indicated in the DS params element id;
		 * silently discard it.
		 *
		 * NB: this can happen due to signal leakage.
		 *     But we should take it for FH phy because
		 *     the rssi value should be correct even for
		 *     different hop pattern in FH.
		 */
		IEEE80211_DISCARD(vap,
		    IEEE80211_MSG_ELEMID | IEEE80211_MSG_INPUT,
		    wh, NULL, "for off-channel %u (bchan=%u)",
		    scan->chan, scan->bchan);
		vap->iv_stats.is_rx_chanmismatch++;
		scan->status |= IEEE80211_BPARSE_OFFCHAN;
	}
	if (!(IEEE80211_BINTVAL_MIN <= scan->bintval &&
	      scan->bintval <= IEEE80211_BINTVAL_MAX)) {
		IEEE80211_DISCARD(vap,
		    IEEE80211_MSG_ELEMID | IEEE80211_MSG_INPUT,
		    wh, NULL, "bogus beacon interval (%d TU)",
		    (int) scan->bintval);
		vap->iv_stats.is_rx_badbintval++;
		scan->status |= IEEE80211_BPARSE_BINTVAL_INVALID;
	}
	if (scan->country != NULL) {
		/*
		 * Validate we have at least enough data to extract
		 * the country code.  Not sure if we should return an
		 * error instead of discarding the IE; consider this
		 * being lenient as we don't depend on the data for
		 * correct operation.
		 */
		IEEE80211_VERIFY_LENGTH(scan->country[1], 3 * sizeof(uint8_t),
		    scan->country = NULL);
	}
	if (scan->csa != NULL) {
		/*
		 * Validate Channel Switch Announcement; this must
		 * be the correct length or we toss the frame.
		 */
		IEEE80211_VERIFY_LENGTH(scan->csa[1], 3 * sizeof(uint8_t),
		    scan->status |= IEEE80211_BPARSE_CSA_INVALID);
	}
	/*
	 * Process HT ie's.  This is complicated by our
	 * accepting both the standard ie's and the pre-draft
	 * vendor OUI ie's that some vendors still use/require.
	 */
	if (scan->htcap != NULL) {
		IEEE80211_VERIFY_LENGTH(scan->htcap[1],
		     scan->htcap[0] == IEEE80211_ELEMID_VENDOR ?
			 4 + sizeof(struct ieee80211_ie_htcap)-2 :
			 sizeof(struct ieee80211_ie_htcap)-2,
		     scan->htcap = NULL);
	}
	if (scan->htinfo != NULL) {
		IEEE80211_VERIFY_LENGTH(scan->htinfo[1],
		     scan->htinfo[0] == IEEE80211_ELEMID_VENDOR ?
			 4 + sizeof(struct ieee80211_ie_htinfo)-2 :
			 sizeof(struct ieee80211_ie_htinfo)-2,
		     scan->htinfo = NULL);
	}

	/* Process VHT IEs */
	if (scan->vhtcap != NULL) {
		IEEE80211_VERIFY_LENGTH(scan->vhtcap[1],
		    sizeof(struct ieee80211_ie_vhtcap) - 2,
		    scan->vhtcap = NULL);
	}
	if (scan->vhtopmode != NULL) {
		IEEE80211_VERIFY_LENGTH(scan->vhtopmode[1],
		    sizeof(struct ieee80211_ie_vht_operation) - 2,
		    scan->vhtopmode = NULL);
	}

	return scan->status;
}

/*
 * Parse an Action frame.  Return 0 on success, non-zero on failure.
 */
int
ieee80211_parse_action(struct ieee80211_node *ni, struct mbuf *m)
{
	struct ieee80211vap *vap = ni->ni_vap;
	const struct ieee80211_action *ia;
	struct ieee80211_frame *wh;
	uint8_t *frm, *efrm;

	/*
	 * action frame format:
	 *	[1] category
	 *	[1] action
	 *	[tlv] parameters
	 */
	wh = mtod(m, struct ieee80211_frame *);
	frm = (u_int8_t *)&wh[1];
	efrm = mtod(m, u_int8_t *) + m->m_len;
	IEEE80211_VERIFY_LENGTH(efrm - frm,
		sizeof(struct ieee80211_action), return EINVAL);
	ia = (const struct ieee80211_action *) frm;

	vap->iv_stats.is_rx_action++;
	IEEE80211_NODE_STAT(ni, rx_action);

	/* verify frame payloads but defer processing */
	switch (ia->ia_category) {
	case IEEE80211_ACTION_CAT_BA:
		switch (ia->ia_action) {
		case IEEE80211_ACTION_BA_ADDBA_REQUEST:
			IEEE80211_VERIFY_LENGTH(efrm - frm,
			    sizeof(struct ieee80211_action_ba_addbarequest),
			    return EINVAL);
			break;
		case IEEE80211_ACTION_BA_ADDBA_RESPONSE:
			IEEE80211_VERIFY_LENGTH(efrm - frm,
			    sizeof(struct ieee80211_action_ba_addbaresponse),
			    return EINVAL);
			break;
		case IEEE80211_ACTION_BA_DELBA:
			IEEE80211_VERIFY_LENGTH(efrm - frm,
			    sizeof(struct ieee80211_action_ba_delba),
			    return EINVAL);
			break;
		}
		break;
	case IEEE80211_ACTION_CAT_HT:
		switch (ia->ia_action) {
		case IEEE80211_ACTION_HT_TXCHWIDTH:
			IEEE80211_VERIFY_LENGTH(efrm - frm,
			    sizeof(struct ieee80211_action_ht_txchwidth),
			    return EINVAL);
			break;
		case IEEE80211_ACTION_HT_MIMOPWRSAVE:
			IEEE80211_VERIFY_LENGTH(efrm - frm,
			    sizeof(struct ieee80211_action_ht_mimopowersave),
			    return EINVAL);
			break;
		}
		break;
#ifdef IEEE80211_SUPPORT_MESH
	case IEEE80211_ACTION_CAT_MESH:
		switch (ia->ia_action) {
		case IEEE80211_ACTION_MESH_LMETRIC:
			/*
			 * XXX: verification is true only if we are using
			 * Airtime link metric (default)
			 */
			IEEE80211_VERIFY_LENGTH(efrm - frm,
			    sizeof(struct ieee80211_meshlmetric_ie),
			    return EINVAL);
			break;
		case IEEE80211_ACTION_MESH_HWMP:
			/* verify something */
			break;
		case IEEE80211_ACTION_MESH_GANN:
			IEEE80211_VERIFY_LENGTH(efrm - frm,
			    sizeof(struct ieee80211_meshgann_ie),
			    return EINVAL);
			break;
		case IEEE80211_ACTION_MESH_CC:
		case IEEE80211_ACTION_MESH_MCCA_SREQ:
		case IEEE80211_ACTION_MESH_MCCA_SREP:
		case IEEE80211_ACTION_MESH_MCCA_AREQ:
		case IEEE80211_ACTION_MESH_MCCA_ADVER:
		case IEEE80211_ACTION_MESH_MCCA_TRDOWN:
		case IEEE80211_ACTION_MESH_TBTT_REQ:
		case IEEE80211_ACTION_MESH_TBTT_RES:
			/* reject these early on, not implemented */
			IEEE80211_DISCARD(vap,
			    IEEE80211_MSG_ELEMID | IEEE80211_MSG_INPUT,
			    wh, NULL, "not implemented yet, act=0x%02X",
			    ia->ia_action);
			return EINVAL;
		}
		break;
	case IEEE80211_ACTION_CAT_SELF_PROT:
		/* If TA or RA group address discard silently */
		if (IEEE80211_IS_MULTICAST(wh->i_addr1) ||
			IEEE80211_IS_MULTICAST(wh->i_addr2))
			return EINVAL;
		/*
		 * XXX: Should we verify complete length now or it is
		 * to varying in sizes?
		 */
		switch (ia->ia_action) {
		case IEEE80211_ACTION_MESHPEERING_CONFIRM:
		case IEEE80211_ACTION_MESHPEERING_CLOSE:
			/* is not a peering candidate (yet) */
			if (ni == vap->iv_bss)
				return EINVAL;
			break;
		}
		break;
#endif
	case IEEE80211_ACTION_CAT_VHT:
		printf("%s: TODO: VHT handling!\n", __func__);
		break;
	}
	return 0;
}

#ifdef IEEE80211_DEBUG
/*
 * Debugging support.
 */
void
ieee80211_ssid_mismatch(struct ieee80211vap *vap, const char *tag,
	uint8_t mac[IEEE80211_ADDR_LEN], uint8_t *ssid)
{
	printf("[%s] discard %s frame, ssid mismatch: ",
		ether_sprintf(mac), tag);
	ieee80211_print_essid(ssid + 2, ssid[1]);
	printf("\n");
}

/*
 * Return the bssid of a frame.
 */
static const uint8_t *
ieee80211_getbssid(const struct ieee80211vap *vap,
	const struct ieee80211_frame *wh)
{
	if (vap->iv_opmode == IEEE80211_M_STA)
		return wh->i_addr2;
	if ((wh->i_fc[1] & IEEE80211_FC1_DIR_MASK) != IEEE80211_FC1_DIR_NODS)
		return wh->i_addr1;
	if ((wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) == IEEE80211_FC0_SUBTYPE_PS_POLL)
		return wh->i_addr1;
	return wh->i_addr3;
}

#include <machine/stdarg.h>

void
ieee80211_note(const struct ieee80211vap *vap, const char *fmt, ...)
{
	char buf[128];		/* XXX */
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	if_printf(vap->iv_ifp, "%s", buf);	/* NB: no \n */
}

void
ieee80211_note_frame(const struct ieee80211vap *vap,
	const struct ieee80211_frame *wh,
	const char *fmt, ...)
{
	char buf[128];		/* XXX */
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if_printf(vap->iv_ifp, "[%s] %s\n",
		ether_sprintf(ieee80211_getbssid(vap, wh)), buf);
}

void
ieee80211_note_mac(const struct ieee80211vap *vap,
	const uint8_t mac[IEEE80211_ADDR_LEN],
	const char *fmt, ...)
{
	char buf[128];		/* XXX */
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if_printf(vap->iv_ifp, "[%s] %s\n", ether_sprintf(mac), buf);
}

void
ieee80211_discard_frame(const struct ieee80211vap *vap,
	const struct ieee80211_frame *wh,
	const char *type, const char *fmt, ...)
{
	va_list ap;

	if_printf(vap->iv_ifp, "[%s] discard ",
		ether_sprintf(ieee80211_getbssid(vap, wh)));
	printf("%s frame, ", type != NULL ? type :
	    ieee80211_mgt_subtype_name(wh->i_fc[0]));
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
}

void
ieee80211_discard_ie(const struct ieee80211vap *vap,
	const struct ieee80211_frame *wh,
	const char *type, const char *fmt, ...)
{
	va_list ap;

	if_printf(vap->iv_ifp, "[%s] discard ",
		ether_sprintf(ieee80211_getbssid(vap, wh)));
	if (type != NULL)
		printf("%s information element, ", type);
	else
		printf("information element, ");
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
}

void
ieee80211_discard_mac(const struct ieee80211vap *vap,
	const uint8_t mac[IEEE80211_ADDR_LEN],
	const char *type, const char *fmt, ...)
{
	va_list ap;

	if_printf(vap->iv_ifp, "[%s] discard ", ether_sprintf(mac));
	if (type != NULL)
		printf("%s frame, ", type);
	else
		printf("frame, ");
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
}
#endif /* IEEE80211_DEBUG */
