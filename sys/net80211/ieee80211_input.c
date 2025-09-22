/*	$OpenBSD: ieee80211_input.c,v 1.256 2025/08/01 20:39:26 stsp Exp $	*/
/*	$NetBSD: ieee80211_input.c,v 1.24 2004/05/31 11:12:24 dyoung Exp $	*/

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
#include <sys/task.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_llc.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_priv.h>

struct	mbuf *ieee80211_input_hwdecrypt(struct ieee80211com *,
	    struct ieee80211_node *, struct mbuf *,
	    struct ieee80211_rxinfo *rxi);
struct	mbuf *ieee80211_defrag(struct ieee80211com *, struct mbuf *, int);
void	ieee80211_defrag_timeout(void *);
void	ieee80211_input_ba(struct ieee80211com *, struct mbuf *,
	    struct ieee80211_node *, int, struct ieee80211_rxinfo *,
	    struct mbuf_list *);
void	ieee80211_input_ba_flush(struct ieee80211com *, struct ieee80211_node *,
	    struct ieee80211_rx_ba *, struct mbuf_list *);
int	ieee80211_input_ba_gap_skip(struct ieee80211_rx_ba *);
void	ieee80211_input_ba_gap_timeout(void *arg);
void	ieee80211_ba_move_window(struct ieee80211com *,
	    struct ieee80211_node *, u_int8_t, u_int16_t, struct mbuf_list *);
void	ieee80211_input_ba_seq(struct ieee80211com *,
	    struct ieee80211_node *, uint8_t, uint16_t, struct mbuf_list *);
void	ieee80211_decap(struct ieee80211com *, struct mbuf *,
	    struct ieee80211_node *, int, struct mbuf_list *);
int	ieee80211_amsdu_decap_validate(struct ieee80211com *, struct mbuf *,
	    struct ieee80211_node *);
void	ieee80211_amsdu_decap(struct ieee80211com *, struct mbuf *,
	    struct ieee80211_node *, int, struct mbuf_list *);
void	ieee80211_enqueue_data(struct ieee80211com *, struct mbuf *,
	    struct ieee80211_node *, int, struct mbuf_list *);
int	ieee80211_parse_edca_params_body(struct ieee80211com *,
	    const u_int8_t *);
int	ieee80211_parse_edca_params(struct ieee80211com *, const u_int8_t *);
int	ieee80211_parse_wmm_params(struct ieee80211com *, const u_int8_t *);
enum	ieee80211_cipher ieee80211_parse_rsn_cipher(const u_int8_t *);
enum	ieee80211_akm ieee80211_parse_rsn_akm(const u_int8_t *);
int	ieee80211_parse_rsn_body(struct ieee80211com *, const u_int8_t *,
	    u_int, struct ieee80211_rsnparams *);
int	ieee80211_save_ie(const u_int8_t *, u_int8_t **);
void	ieee80211_recv_probe_resp(struct ieee80211com *, struct mbuf *,
	    struct ieee80211_node *, struct ieee80211_rxinfo *, int);
#ifndef IEEE80211_STA_ONLY
void	ieee80211_recv_probe_req(struct ieee80211com *, struct mbuf *,
	    struct ieee80211_node *, struct ieee80211_rxinfo *);
#endif
void	ieee80211_recv_auth(struct ieee80211com *, struct mbuf *,
	    struct ieee80211_node *, struct ieee80211_rxinfo *);
#ifndef IEEE80211_STA_ONLY
void	ieee80211_recv_assoc_req(struct ieee80211com *, struct mbuf *,
	    struct ieee80211_node *, struct ieee80211_rxinfo *, int);
#endif
void	ieee80211_recv_assoc_resp(struct ieee80211com *, struct mbuf *,
	    struct ieee80211_node *, int);
void	ieee80211_recv_deauth(struct ieee80211com *, struct mbuf *,
	    struct ieee80211_node *);
void	ieee80211_recv_disassoc(struct ieee80211com *, struct mbuf *,
	    struct ieee80211_node *);
void	ieee80211_recv_addba_req(struct ieee80211com *, struct mbuf *,
	    struct ieee80211_node *);
void	ieee80211_recv_addba_resp(struct ieee80211com *, struct mbuf *,
	    struct ieee80211_node *);
void	ieee80211_recv_delba(struct ieee80211com *, struct mbuf *,
	    struct ieee80211_node *);
void	ieee80211_recv_sa_query_req(struct ieee80211com *, struct mbuf *,
	    struct ieee80211_node *);
#ifndef IEEE80211_STA_ONLY
void	ieee80211_recv_sa_query_resp(struct ieee80211com *, struct mbuf *,
	    struct ieee80211_node *);
#endif
void	ieee80211_recv_action(struct ieee80211com *, struct mbuf *,
	    struct ieee80211_node *);
#ifndef IEEE80211_STA_ONLY
void	ieee80211_recv_pspoll(struct ieee80211com *, struct mbuf *,
	    struct ieee80211_node *);
#endif
void	ieee80211_recv_bar(struct ieee80211com *, struct mbuf *,
	    struct ieee80211_node *);
void	ieee80211_bar_tid(struct ieee80211com *, struct ieee80211_node *,
	    u_int8_t, u_int16_t);

/*
 * Retrieve the length in bytes of an 802.11 header.
 */
u_int
ieee80211_get_hdrlen(const struct ieee80211_frame *wh)
{
	u_int size = sizeof(*wh);

	/* NB: does not work with control frames */
	KASSERT(ieee80211_has_seq(wh));

	if (ieee80211_has_addr4(wh))
		size += IEEE80211_ADDR_LEN;	/* i_addr4 */
	if (ieee80211_has_qos(wh))
		size += sizeof(u_int16_t);	/* i_qos */
	if (ieee80211_has_htc(wh))
		size += sizeof(u_int32_t);	/* i_ht */
	return size;
}

/* Post-processing for drivers which perform decryption in hardware. */
struct mbuf *
ieee80211_input_hwdecrypt(struct ieee80211com *ic, struct ieee80211_node *ni,
    struct mbuf *m, struct ieee80211_rxinfo *rxi)
{
	struct ieee80211_key *k;
	struct ieee80211_frame *wh;
	uint64_t pn, *prsc;
	int hdrlen;

	k = ieee80211_get_rxkey(ic, m, ni);
	if (k == NULL)
		return NULL;
	
	wh = mtod(m, struct ieee80211_frame *);
	hdrlen = ieee80211_get_hdrlen(wh);

	/*
	 * Update the last-seen packet number (PN) for drivers using hardware
	 * crypto offloading. This cannot be done by drivers because A-MPDU
	 * reordering needs to occur before a valid lower bound can be
	 * determined for the PN. Drivers will read the PN we write here and
	 * are expected to discard replayed frames based on it.
	 * Drivers are expected to leave the IV of decrypted frames intact
	 * so we can update the last-seen PN and strip the IV here.
	 */
	switch (k->k_cipher) {
	case IEEE80211_CIPHER_CCMP:
		if (!(wh->i_fc[1] & IEEE80211_FC1_PROTECTED)) {
			/*
			 * If the protected bit is clear then hardware has
			 * stripped the IV and we must trust that it handles
			 * replay detection correctly.
			 */
			break;
		}
		if (ieee80211_ccmp_get_pn(&pn, &prsc, m, k) != 0) {
			ic->ic_stats.is_ccmp_dec_errs++;
			return NULL;
		}
		if (rxi->rxi_flags & IEEE80211_RXI_HWDEC_SAME_PN) {
			if (pn < *prsc) {
				ic->ic_stats.is_ccmp_replays++;
				return NULL;
			}
		} else if (pn <= *prsc) {
			ic->ic_stats.is_ccmp_replays++;
			return NULL;
		}

		/* Update last-seen packet number. */
		*prsc = pn;

		/* Clear Protected bit and strip IV. */
		wh->i_fc[1] &= ~IEEE80211_FC1_PROTECTED;
		memmove(mtod(m, caddr_t) + IEEE80211_CCMP_HDRLEN, wh, hdrlen);
		m_adj(m, IEEE80211_CCMP_HDRLEN);
		/* Drivers are expected to strip the MIC. */
		break;
	 case IEEE80211_CIPHER_TKIP:
		if (!(wh->i_fc[1] & IEEE80211_FC1_PROTECTED)) {
			/*
			 * If the protected bit is clear then hardware has
			 * stripped the IV and we must trust that it handles
			 * replay detection correctly.
			 */
			break;
		}
		if (ieee80211_tkip_get_tsc(&pn, &prsc, m, k) != 0)
			return NULL;
		if (rxi->rxi_flags & IEEE80211_RXI_HWDEC_SAME_PN) {
			if (pn < *prsc) {
				ic->ic_stats.is_tkip_replays++;
				return NULL;
			}
		} else if (pn <= *prsc) {
			ic->ic_stats.is_tkip_replays++;
			return NULL;
		}

		/* Update last-seen packet number. */
		*prsc = pn;

		/* Clear Protected bit and strip IV. */
		wh = mtod(m, struct ieee80211_frame *);
		wh->i_fc[1] &= ~IEEE80211_FC1_PROTECTED;
		memmove(mtod(m, caddr_t) + IEEE80211_TKIP_HDRLEN, wh, hdrlen);
		m_adj(m, IEEE80211_TKIP_HDRLEN);
		/* Drivers are expected to strip the MIC. */
		break;
	default:
		break;
	}

	return m;
}

/*
 * Process a received frame.  The node associated with the sender
 * should be supplied.  If nothing was found in the node table then
 * the caller is assumed to supply a reference to ic_bss instead.
 * The RSSI and a timestamp are also supplied.  The RSSI data is used
 * during AP scanning to select a AP to associate with; it can have
 * any units so long as values have consistent units and higher values
 * mean ``better signal''.  The receive timestamp is currently not used
 * by the 802.11 layer.
 *
 * This function acts on management frames immediately and queues data frames
 * on the specified mbuf list. Delivery of queued data frames to upper layers
 * must be triggered with if_input(). Drivers should call if_input() only once
 * per Rx interrupt to avoid triggering the input ifq pressure drop mechanism
 * unnecessarily.
 */
void
ieee80211_inputm(struct ifnet *ifp, struct mbuf *m, struct ieee80211_node *ni,
    struct ieee80211_rxinfo *rxi, struct mbuf_list *ml)
{
	struct ieee80211com *ic = (void *)ifp;
	struct ieee80211_frame *wh;
	u_int16_t *orxseq, nrxseq, qos;
	u_int8_t dir, type, subtype, tid;
	int hdrlen, hasqos;

	KASSERT(ni != NULL);

	/* in monitor mode, send everything directly to bpf */
	if (ic->ic_opmode == IEEE80211_M_MONITOR)
		goto out;

	/*
	 * Do not process frames without an Address 2 field any further.
	 * Only CTS and ACK control frames do not have this field.
	 */
	if (m->m_len < sizeof(struct ieee80211_frame_min)) {
		DPRINTF(("frame too short, len %u\n", m->m_len));
		ic->ic_stats.is_rx_tooshort++;
		goto out;
	}

	wh = mtod(m, struct ieee80211_frame *);
	if ((wh->i_fc[0] & IEEE80211_FC0_VERSION_MASK) !=
	    IEEE80211_FC0_VERSION_0) {
		DPRINTF(("frame with wrong version: %x\n", wh->i_fc[0]));
		ic->ic_stats.is_rx_badversion++;
		goto err;
	}

	dir = wh->i_fc[1] & IEEE80211_FC1_DIR_MASK;
	type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;
	subtype = wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;

	if (type != IEEE80211_FC0_TYPE_CTL) {
		hdrlen = ieee80211_get_hdrlen(wh);
		if (m->m_len < hdrlen) {
			DPRINTF(("frame too short, len %u\n", m->m_len));
			ic->ic_stats.is_rx_tooshort++;
			goto err;
		}
	} else
		hdrlen = 0;
	if ((hasqos = ieee80211_has_qos(wh))) {
		qos = ieee80211_get_qos(wh);
		tid = qos & IEEE80211_QOS_TID;
	} else {
		qos = 0;
		tid = 0;
	}

	if (ic->ic_state == IEEE80211_S_RUN &&
	    type == IEEE80211_FC0_TYPE_DATA && hasqos &&
	    (subtype & IEEE80211_FC0_SUBTYPE_NODATA) == 0 &&
	    !(rxi->rxi_flags & IEEE80211_RXI_AMPDU_DONE)
#ifndef IEEE80211_STA_ONLY
	    && (ic->ic_opmode == IEEE80211_M_STA || ni != ic->ic_bss)
#endif
	    ) {
		int ba_state = ni->ni_rx_ba[tid].ba_state;

#ifndef IEEE80211_STA_ONLY
		if (ic->ic_opmode == IEEE80211_M_HOSTAP) {
			if (!IEEE80211_ADDR_EQ(wh->i_addr1,
			    ic->ic_bss->ni_bssid)) {
				ic->ic_stats.is_rx_wrongbss++;
				goto err;
			}
			if (ni->ni_state != IEEE80211_S_ASSOC) {
				ic->ic_stats.is_rx_notassoc++;
				goto err;
			}
		}
#endif
		/* 
		 * If Block Ack was explicitly requested, check
		 * if we have a BA agreement for this RA/TID.
		 */
		if ((qos & IEEE80211_QOS_ACK_POLICY_MASK) ==
		    IEEE80211_QOS_ACK_POLICY_BA &&
		    ba_state != IEEE80211_BA_AGREED) {
			DPRINTF(("no BA agreement for %s, TID %d\n",
			    ether_sprintf(ni->ni_macaddr), tid));
			/* send a DELBA with reason code UNKNOWN-BA */
			IEEE80211_SEND_ACTION(ic, ni,
			    IEEE80211_CATEG_BA, IEEE80211_ACTION_DELBA,
			    IEEE80211_REASON_SETUP_REQUIRED << 16 | tid);
			goto err;
		}

		/* 
		 * Check if we have an explicit or implicit
		 * Block Ack Request for a valid BA agreement.
		 */
		if (ba_state == IEEE80211_BA_AGREED &&
		    ((qos & IEEE80211_QOS_ACK_POLICY_MASK) ==
		    IEEE80211_QOS_ACK_POLICY_BA ||
		    (qos & IEEE80211_QOS_ACK_POLICY_MASK) ==
		    IEEE80211_QOS_ACK_POLICY_NORMAL)) {
			/* go through A-MPDU reordering */
			ieee80211_input_ba(ic, m, ni, tid, rxi, ml);
			return;	/* don't free m! */
		} else if (ba_state == IEEE80211_BA_REQUESTED &&
		    (qos & IEEE80211_QOS_ACK_POLICY_MASK) ==
		    IEEE80211_QOS_ACK_POLICY_NORMAL) {
			/*
			 * Apparently, qos frames for a tid where a
			 * block ack agreement was requested but not
			 * yet confirmed by us should still contribute
			 * to the sequence number for this tid.
			 */
			ieee80211_input_ba(ic, m, ni, tid, rxi, ml);
			return;	/* don't free m! */
		}
	}

	/*
	 * We do not yet support fragments. Drop any fragmented packets.
	 * Counter-measure against attacks where an arbitrary packet is
	 * injected via a fragment with attacker-controlled content.
	 * See https://papers.mathyvanhoef.com/usenix2021.pdf
	 * Section 6.8 "Treating fragments as full frames"
	 */
	if (ieee80211_has_seq(wh)) {
		uint16_t rxseq = letoh16(*(const u_int16_t *)wh->i_seq);
		if ((wh->i_fc[1] & IEEE80211_FC1_MORE_FRAG) ||
		    (rxseq & IEEE80211_SEQ_FRAG_MASK))
			goto err;
	}

	/* duplicate detection (see 9.2.9) */
	if (ieee80211_has_seq(wh) &&
	    ic->ic_state != IEEE80211_S_SCAN) {
		nrxseq = letoh16(*(u_int16_t *)wh->i_seq) >>
		    IEEE80211_SEQ_SEQ_SHIFT;
		if (hasqos)
			orxseq = &ni->ni_qos_rxseqs[tid];
		else
			orxseq = &ni->ni_rxseq;
		if (rxi->rxi_flags & IEEE80211_RXI_SAME_SEQ) {
			if (nrxseq != *orxseq) {
				/* duplicate, silently discarded */
				ic->ic_stats.is_rx_dup++;
				goto out;
			}
		} else if ((wh->i_fc[1] & IEEE80211_FC1_RETRY) &&
		    nrxseq == *orxseq) {
			/* duplicate, silently discarded */
			ic->ic_stats.is_rx_dup++;
			goto out;
		}
		*orxseq = nrxseq;
	}
	if (ic->ic_state > IEEE80211_S_SCAN) {
		ni->ni_rssi = rxi->rxi_rssi;
		ni->ni_rstamp = rxi->rxi_tstamp;
		ni->ni_inact = 0;

		if (ic->ic_state == IEEE80211_S_RUN && ic->ic_bgscan_start) {
			/* Cancel or start background scan based on RSSI. */
			if ((*ic->ic_node_checkrssi)(ic, ni))
				timeout_del(&ic->ic_bgscan_timeout);
			else if (!timeout_pending(&ic->ic_bgscan_timeout) &&
			    (ic->ic_flags & IEEE80211_F_BGSCAN) == 0 &&
			    (ic->ic_flags & IEEE80211_F_DESBSSID) == 0)
				timeout_add_msec(&ic->ic_bgscan_timeout,
				    500 * (ic->ic_bgscan_fail + 1));
		}
	}

#ifndef IEEE80211_STA_ONLY
	if (ic->ic_opmode == IEEE80211_M_HOSTAP &&
	    (ic->ic_caps & IEEE80211_C_APPMGT) &&
	    ni->ni_state == IEEE80211_STA_ASSOC) {
		if (wh->i_fc[1] & IEEE80211_FC1_PWR_MGT) {
			if (ni->ni_pwrsave == IEEE80211_PS_AWAKE) {
				/* turn on PS mode */
				ni->ni_pwrsave = IEEE80211_PS_DOZE;
				DPRINTF(("PS mode on for %s\n",
				    ether_sprintf(wh->i_addr2)));
			}
		} else if (ni->ni_pwrsave == IEEE80211_PS_DOZE) {
			struct mbuf *m;

			/* turn off PS mode */
			ni->ni_pwrsave = IEEE80211_PS_AWAKE;
			DPRINTF(("PS mode off for %s\n",
			    ether_sprintf(wh->i_addr2)));

			(*ic->ic_set_tim)(ic, ni->ni_associd, 0);

			/* dequeue buffered unicast frames */
			while ((m = mq_dequeue(&ni->ni_savedq)) != NULL) {
				mq_enqueue(&ic->ic_pwrsaveq, m);
				if_start(ifp);
			}
		}
	}
#endif
	switch (type) {
	case IEEE80211_FC0_TYPE_DATA:
		switch (ic->ic_opmode) {
		case IEEE80211_M_STA:
			if (dir != IEEE80211_FC1_DIR_FROMDS) {
				ic->ic_stats.is_rx_wrongdir++;
				goto out;
			}
			if (ic->ic_state != IEEE80211_S_SCAN &&
			    !IEEE80211_ADDR_EQ(wh->i_addr2, ni->ni_bssid)) {
				/* Source address is not our BSS. */
				DPRINTF(("discard frame from SA %s\n",
				    ether_sprintf(wh->i_addr2)));
				ic->ic_stats.is_rx_wrongbss++;
				goto out;
			}
			if ((ifp->if_flags & IFF_SIMPLEX) &&
			    IEEE80211_IS_MULTICAST(wh->i_addr1) &&
			    IEEE80211_ADDR_EQ(wh->i_addr3, ic->ic_myaddr)) {
				/*
				 * In IEEE802.11 network, multicast frame
				 * sent from me is broadcasted from AP.
				 * It should be silently discarded for
				 * SIMPLEX interface.
				 */
				ic->ic_stats.is_rx_mcastecho++;
				goto out;
			}
			break;
#ifndef IEEE80211_STA_ONLY
		case IEEE80211_M_IBSS:
		case IEEE80211_M_AHDEMO:
			if (dir != IEEE80211_FC1_DIR_NODS) {
				ic->ic_stats.is_rx_wrongdir++;
				goto out;
			}
			if (ic->ic_state != IEEE80211_S_SCAN &&
			    !IEEE80211_ADDR_EQ(wh->i_addr3,
				ic->ic_bss->ni_bssid) &&
			    !IEEE80211_ADDR_EQ(wh->i_addr3,
				etherbroadcastaddr)) {
				/* Destination is not our BSS or broadcast. */
				DPRINTF(("discard data frame to DA %s\n",
				    ether_sprintf(wh->i_addr3)));
				ic->ic_stats.is_rx_wrongbss++;
				goto out;
			}
			break;
		case IEEE80211_M_HOSTAP:
			if (dir != IEEE80211_FC1_DIR_TODS) {
				ic->ic_stats.is_rx_wrongdir++;
				goto out;
			}
			if (ic->ic_state != IEEE80211_S_SCAN &&
			    !IEEE80211_ADDR_EQ(wh->i_addr1,
				ic->ic_bss->ni_bssid) &&
			    !IEEE80211_ADDR_EQ(wh->i_addr1,
				etherbroadcastaddr)) {
				/* BSS is not us or broadcast. */
				DPRINTF(("discard data frame to BSS %s\n",
				    ether_sprintf(wh->i_addr1)));
				ic->ic_stats.is_rx_wrongbss++;
				goto out;
			}
			/* check if source STA is associated */
			if (ni == ic->ic_bss) {
				DPRINTF(("data from unknown src %s\n",
				    ether_sprintf(wh->i_addr2)));
				/* NB: caller deals with reference */
				ni = ieee80211_find_node(ic, wh->i_addr2);
				if (ni == NULL)
					ni = ieee80211_dup_bss(ic, wh->i_addr2);
				if (ni != NULL) {
					IEEE80211_SEND_MGMT(ic, ni,
					    IEEE80211_FC0_SUBTYPE_DEAUTH,
					    IEEE80211_REASON_NOT_AUTHED);
				}
				ic->ic_stats.is_rx_notassoc++;
				goto err;
			}
			if (ni->ni_state != IEEE80211_STA_ASSOC) {
				DPRINTF(("data from unassoc src %s\n",
				    ether_sprintf(wh->i_addr2)));
				IEEE80211_SEND_MGMT(ic, ni,
				    IEEE80211_FC0_SUBTYPE_DISASSOC,
				    IEEE80211_REASON_NOT_ASSOCED);
				ic->ic_stats.is_rx_notassoc++;
				goto err;
			}
			break;
#endif	/* IEEE80211_STA_ONLY */
		default:
			/* can't get there */
			goto out;
		}

		/* Do not process "no data" frames any further. */
		if (subtype & IEEE80211_FC0_SUBTYPE_NODATA) {
#if NBPFILTER > 0
			if (ic->ic_rawbpf)
				bpf_mtap(ic->ic_rawbpf, m, BPF_DIRECTION_IN);
#endif
			goto out;
		}

		if ((ic->ic_flags & IEEE80211_F_WEPON) ||
		    ((ic->ic_flags & IEEE80211_F_RSNON) &&
		     (ni->ni_flags & IEEE80211_NODE_RXPROT))) {
			/* protection is on for Rx */
			if (!(rxi->rxi_flags & IEEE80211_RXI_HWDEC)) {
				if (!(wh->i_fc[1] & IEEE80211_FC1_PROTECTED)) {
					/* drop unencrypted */
					ic->ic_stats.is_rx_unencrypted++;
					goto err;
				}
				/* do software decryption */
				m = ieee80211_decrypt(ic, m, ni);
				if (m == NULL) {
					ic->ic_stats.is_rx_wepfail++;
					goto err;
				}
			} else {
				m = ieee80211_input_hwdecrypt(ic, ni, m, rxi);
				if (m == NULL)
					goto err;
			}
			wh = mtod(m, struct ieee80211_frame *);
		} else if ((wh->i_fc[1] & IEEE80211_FC1_PROTECTED) ||
		    (rxi->rxi_flags & IEEE80211_RXI_HWDEC)) {
			/* frame encrypted but protection off for Rx */
			ic->ic_stats.is_rx_nowep++;
			goto out;
		}

#if NBPFILTER > 0
		/* copy to listener after decrypt */
		if (ic->ic_rawbpf)
			bpf_mtap(ic->ic_rawbpf, m, BPF_DIRECTION_IN);
#endif

		if ((ni->ni_flags & IEEE80211_NODE_HT) &&
		    hasqos && (qos & IEEE80211_QOS_AMSDU))
			ieee80211_amsdu_decap(ic, m, ni, hdrlen, ml);
		else
			ieee80211_decap(ic, m, ni, hdrlen, ml);
		return;

	case IEEE80211_FC0_TYPE_MGT:
		if (dir != IEEE80211_FC1_DIR_NODS) {
			ic->ic_stats.is_rx_wrongdir++;
			goto err;
		}
#ifndef IEEE80211_STA_ONLY
		if (ic->ic_opmode == IEEE80211_M_AHDEMO) {
			ic->ic_stats.is_rx_ahdemo_mgt++;
			goto out;
		}
#endif
		/* drop frames without interest */
		if (ic->ic_state == IEEE80211_S_SCAN) {
			if (subtype != IEEE80211_FC0_SUBTYPE_BEACON &&
			    subtype != IEEE80211_FC0_SUBTYPE_PROBE_RESP) {
				ic->ic_stats.is_rx_mgtdiscard++;
				goto out;
			}
		}

		if (ni->ni_flags & IEEE80211_NODE_RXMGMTPROT) {
			/* MMPDU protection is on for Rx */
			if (subtype == IEEE80211_FC0_SUBTYPE_DISASSOC ||
			    subtype == IEEE80211_FC0_SUBTYPE_DEAUTH ||
			    subtype == IEEE80211_FC0_SUBTYPE_ACTION) {
				if (!IEEE80211_IS_MULTICAST(wh->i_addr1) &&
				    !(wh->i_fc[1] & IEEE80211_FC1_PROTECTED)) {
					/* unicast mgmt not encrypted */
					goto out;
				}
				/* do software decryption */
				m = ieee80211_decrypt(ic, m, ni);
				if (m == NULL) {
					/* XXX stats */
					goto out;
				}
				wh = mtod(m, struct ieee80211_frame *);
			}
		} else if ((ic->ic_flags & IEEE80211_F_RSNON) &&
		    (wh->i_fc[1] & IEEE80211_FC1_PROTECTED)) {
			/* encrypted but MMPDU Rx protection off for TA */
			goto out;
		}

#if NBPFILTER > 0
		if (bpf_mtap(ic->ic_rawbpf, m, BPF_DIRECTION_IN) != 0) {
			/*
			 * Drop mbuf if it was filtered by bpf. Normally,
			 * this is done in ether_input() but IEEE 802.11
			 * management frames are a special case.
			 */
			m_freem(m);
			return;
		}
#endif
		(*ic->ic_recv_mgmt)(ic, m, ni, rxi, subtype);
		m_freem(m);
		return;

	case IEEE80211_FC0_TYPE_CTL:
		switch (subtype) {
#ifndef IEEE80211_STA_ONLY
		case IEEE80211_FC0_SUBTYPE_PS_POLL:
			ieee80211_recv_pspoll(ic, m, ni);
			break;
#endif
		case IEEE80211_FC0_SUBTYPE_BAR:
			ieee80211_recv_bar(ic, m, ni);
			break;
		default:
			ic->ic_stats.is_rx_ctl++;
			break;
		}
		goto out;

	default:
		DPRINTF(("bad frame type %x\n", type));
		/* should not come here */
		break;
	}
 err:
	ifp->if_ierrors++;
 out:
	if (m != NULL) {
#if NBPFILTER > 0
		if (ic->ic_rawbpf)
			bpf_mtap(ic->ic_rawbpf, m, BPF_DIRECTION_IN);
#endif
		m_freem(m);
	}
}

/* Input handler for drivers which only receive one frame per interrupt. */
void
ieee80211_input(struct ifnet *ifp, struct mbuf *m, struct ieee80211_node *ni,
    struct ieee80211_rxinfo *rxi)
{
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();

	ieee80211_inputm(ifp, m, ni, rxi, &ml);
	if_input(ifp, &ml);
}

#ifdef notyet
/*
 * Handle defragmentation (see 9.5 and Annex C).  We support the concurrent
 * reception of fragments of three fragmented MSDUs or MMPDUs.
 */
struct mbuf *
ieee80211_defrag(struct ieee80211com *ic, struct mbuf *m, int hdrlen)
{
	const struct ieee80211_frame *owh, *wh;
	struct ieee80211_defrag *df;
	u_int16_t rxseq, seq;
	u_int8_t frag;
	int i;

	wh = mtod(m, struct ieee80211_frame *);
	rxseq = letoh16(*(const u_int16_t *)wh->i_seq);
	seq = rxseq >> IEEE80211_SEQ_SEQ_SHIFT;
	frag = rxseq & IEEE80211_SEQ_FRAG_MASK;

	if (frag == 0 && !(wh->i_fc[1] & IEEE80211_FC1_MORE_FRAG))
		return m;	/* not fragmented */

	if (frag == 0) {
		/* first fragment, setup entry in the fragment cache */
		if (++ic->ic_defrag_cur == IEEE80211_DEFRAG_SIZE)
			ic->ic_defrag_cur = 0;
		df = &ic->ic_defrag[ic->ic_defrag_cur];
		m_freem(df->df_m);	/* discard old entry */
		df->df_seq = seq;
		df->df_frag = 0;
		df->df_m = m;
		/* start receive MSDU timer of aMaxReceiveLifetime */
		timeout_add_sec(&df->df_to, 1);
		return NULL;	/* MSDU or MMPDU not yet complete */
	}

	/* find matching entry in the fragment cache */
	for (i = 0; i < IEEE80211_DEFRAG_SIZE; i++) {
		df = &ic->ic_defrag[i];
		if (df->df_m == NULL)
			continue;
		if (df->df_seq != seq || df->df_frag + 1 != frag)
			continue;
		owh = mtod(df->df_m, struct ieee80211_frame *);
		/* frame type, source and destination must match */
		if (((wh->i_fc[0] ^ owh->i_fc[0]) & IEEE80211_FC0_TYPE_MASK) ||
		    !IEEE80211_ADDR_EQ(wh->i_addr1, owh->i_addr1) ||
		    !IEEE80211_ADDR_EQ(wh->i_addr2, owh->i_addr2))
			continue;
		/* matching entry found */
		break;
	}
	if (i == IEEE80211_DEFRAG_SIZE) {
		/* no matching entry found, discard fragment */
		ic->ic_if.if_ierrors++;
		m_freem(m);
		return NULL;
	}

	df->df_frag = frag;
	/* strip 802.11 header and concatenate fragment */
	m_adj(m, hdrlen);
	m_cat(df->df_m, m);
	df->df_m->m_pkthdr.len += m->m_pkthdr.len;

	if (wh->i_fc[1] & IEEE80211_FC1_MORE_FRAG)
		return NULL;	/* MSDU or MMPDU not yet complete */

	/* MSDU or MMPDU complete */
	timeout_del(&df->df_to);
	m = df->df_m;
	df->df_m = NULL;
	return m;
}

/*
 * Receive MSDU defragmentation timer exceeds aMaxReceiveLifetime.
 */
void
ieee80211_defrag_timeout(void *arg)
{
	struct ieee80211_defrag *df = arg;
	int s = splnet();

	/* discard all received fragments */
	m_freem(df->df_m);
	df->df_m = NULL;

	splx(s);
}
#endif

/*
 * Process a received data MPDU related to a specific HT-immediate Block Ack
 * agreement (see 9.10.7.6).
 */
void
ieee80211_input_ba(struct ieee80211com *ic, struct mbuf *m,
    struct ieee80211_node *ni, int tid, struct ieee80211_rxinfo *rxi,
    struct mbuf_list *ml)
{
	struct ifnet *ifp = &ic->ic_if;
	struct ieee80211_rx_ba *ba = &ni->ni_rx_ba[tid];
	struct ieee80211_frame *wh;
	int idx, count;
	u_int16_t sn;

	wh = mtod(m, struct ieee80211_frame *);
	sn = letoh16(*(u_int16_t *)wh->i_seq) >> IEEE80211_SEQ_SEQ_SHIFT;

	/* reset Block Ack inactivity timer */
	if (ba->ba_timeout_val != 0)
		timeout_add_usec(&ba->ba_to, ba->ba_timeout_val);

	if (SEQ_LT(sn, ba->ba_winstart)) {	/* SN < WinStartB */
		ic->ic_stats.is_ht_rx_frame_below_ba_winstart++;
		m_freem(m);	/* discard the MPDU */
		return;
	}
	if (SEQ_LT(ba->ba_winend, sn)) {	/* WinEndB < SN */
		ic->ic_stats.is_ht_rx_frame_above_ba_winend++;
		count = (sn - ba->ba_winend) & 0xfff;
		if (count > ba->ba_winsize) {
			/* 
			 * Check whether we're consistently behind the window,
			 * and let the window move forward if necessary.
			 */
			if (ba->ba_winmiss < IEEE80211_BA_MAX_WINMISS) { 
				if (ba->ba_missedsn == ((sn - 1) & 0xfff))
					ba->ba_winmiss++;
				else
					ba->ba_winmiss = 0;
				ba->ba_missedsn = sn;
				ifp->if_ierrors++;
				m_freem(m);	/* discard the MPDU */
				return;
			}

			/* It appears the window has moved for real. */
			ic->ic_stats.is_ht_rx_ba_window_jump++;
			ba->ba_winmiss = 0;
			ba->ba_missedsn = 0;
			ieee80211_ba_move_window(ic, ni, tid, sn, ml);
		} else {
			ic->ic_stats.is_ht_rx_ba_window_slide++;
			ieee80211_input_ba_seq(ic, ni, tid,
			    (ba->ba_winstart + count) & 0xfff, ml);
			ieee80211_input_ba_flush(ic, ni, ba, ml);
		}
	}
	/* WinStartB <= SN <= WinEndB */

	ba->ba_winmiss = 0;
	ba->ba_missedsn = 0;
	idx = (sn - ba->ba_winstart) & 0xfff;
	idx = (ba->ba_head + idx) % IEEE80211_BA_MAX_WINSZ;
	/* store the received MPDU in the buffer */
	if (ba->ba_buf[idx].m != NULL) {
		ifp->if_ierrors++;
		ic->ic_stats.is_ht_rx_ba_no_buf++;
		m_freem(m);
		return;
	}
	ba->ba_buf[idx].m = m;
	/* store Rx meta-data too */
	rxi->rxi_flags |= IEEE80211_RXI_AMPDU_DONE;
	ba->ba_buf[idx].rxi = *rxi;
	ba->ba_gapwait++;

	if (ba->ba_buf[ba->ba_head].m == NULL && ba->ba_gapwait == 1)
		timeout_add_msec(&ba->ba_gap_to, IEEE80211_BA_GAP_TIMEOUT);

	ieee80211_input_ba_flush(ic, ni, ba, ml);
}

/* 
 * Forward buffered frames with sequence number lower than max_seq.
 * See 802.11-2012 9.21.7.6.2 b.
 */
void
ieee80211_input_ba_seq(struct ieee80211com *ic, struct ieee80211_node *ni,
    uint8_t tid, uint16_t max_seq, struct mbuf_list *ml)
{
	struct ifnet *ifp = &ic->ic_if;
	struct ieee80211_rx_ba *ba = &ni->ni_rx_ba[tid];
	struct ieee80211_frame *wh;
	uint16_t seq;
	int i = 0;

	while (i++ < ba->ba_winsize) {
		/* gaps may exist */
		if (ba->ba_buf[ba->ba_head].m != NULL) {
			wh = mtod(ba->ba_buf[ba->ba_head].m,
			    struct ieee80211_frame *);
			KASSERT(ieee80211_has_seq(wh));
			seq = letoh16(*(u_int16_t *)wh->i_seq) >>
			    IEEE80211_SEQ_SEQ_SHIFT;
			if (!SEQ_LT(seq, max_seq))
				break;
			ieee80211_inputm(ifp, ba->ba_buf[ba->ba_head].m,
			    ni, &ba->ba_buf[ba->ba_head].rxi, ml);
			ba->ba_buf[ba->ba_head].m = NULL;
			ba->ba_gapwait--;
		} else
			ic->ic_stats.is_ht_rx_ba_frame_lost++;
		ba->ba_head = (ba->ba_head + 1) % IEEE80211_BA_MAX_WINSZ;
		/* move window forward */
		ba->ba_winstart = (ba->ba_winstart + 1) & 0xfff;
	}
	ba->ba_winend = (ba->ba_winstart + ba->ba_winsize - 1) & 0xfff;
}

/* Flush a consecutive sequence of frames from the reorder buffer. */
void
ieee80211_input_ba_flush(struct ieee80211com *ic, struct ieee80211_node *ni,
    struct ieee80211_rx_ba *ba, struct mbuf_list *ml)

{
	struct ifnet *ifp = &ic->ic_if;

	/* Do not re-arm the gap timeout if we made no progress. */
	if (ba->ba_buf[ba->ba_head].m == NULL)
		return;

	/* pass reordered MPDUs up to the next MAC process */
	while (ba->ba_buf[ba->ba_head].m != NULL) {
		ieee80211_inputm(ifp, ba->ba_buf[ba->ba_head].m, ni,
		    &ba->ba_buf[ba->ba_head].rxi, ml);
		ba->ba_buf[ba->ba_head].m = NULL;
		ba->ba_gapwait--;

		ba->ba_head = (ba->ba_head + 1) % IEEE80211_BA_MAX_WINSZ;
		/* move window forward */
		ba->ba_winstart = (ba->ba_winstart + 1) & 0xfff;
	}
	ba->ba_winend = (ba->ba_winstart + ba->ba_winsize - 1) & 0xfff;

	if (timeout_pending(&ba->ba_gap_to))
		timeout_del(&ba->ba_gap_to);
	if (ba->ba_gapwait)
		timeout_add_msec(&ba->ba_gap_to, IEEE80211_BA_GAP_TIMEOUT);
}

/* 
 * Forcibly move the BA window forward to remove a leading gap which has
 * been causing frames to linger in the reordering buffer for too long.
 * A leading gap will occur if a particular A-MPDU subframe never arrives
 * or if a bug in the sender causes sequence numbers to jump forward by > 1.
 */
int
ieee80211_input_ba_gap_skip(struct ieee80211_rx_ba *ba)
{
	int skipped = 0;

	while (skipped < ba->ba_winsize && ba->ba_buf[ba->ba_head].m == NULL) {
		/* move window forward */
		ba->ba_head = (ba->ba_head + 1) % IEEE80211_BA_MAX_WINSZ;
		ba->ba_winstart = (ba->ba_winstart + 1) & 0xfff;
		skipped++;
	}
	if (skipped > 0)
		ba->ba_winend = (ba->ba_winstart + ba->ba_winsize - 1) & 0xfff;

	return skipped;
}

void
ieee80211_input_ba_gap_timeout(void *arg)
{
	struct ieee80211_rx_ba *ba = arg;
	struct ieee80211_node *ni = ba->ba_ni;
	struct ieee80211com *ic = ni->ni_ic;
	int s, skipped;

	ic->ic_stats.is_ht_rx_ba_window_gap_timeout++;

	s = splnet();

	skipped = ieee80211_input_ba_gap_skip(ba);
	ic->ic_stats.is_ht_rx_ba_frame_lost += skipped;
	if (skipped) {
		struct mbuf_list ml = MBUF_LIST_INITIALIZER();
		ieee80211_input_ba_flush(ic, ni, ba, &ml);
		if_input(&ic->ic_if, &ml);
	}

	splx(s);	
}


/*
 * Change the value of WinStartB (move window forward) upon reception of a
 * BlockAckReq frame or an ADDBA Request (PBAC).
 */
void
ieee80211_ba_move_window(struct ieee80211com *ic, struct ieee80211_node *ni,
    u_int8_t tid, u_int16_t ssn, struct mbuf_list *ml)
{
	struct ifnet *ifp = &ic->ic_if;
	struct ieee80211_rx_ba *ba = &ni->ni_rx_ba[tid];
	int count;

	/* assert(WinStartB <= SSN) */

	count = (ssn - ba->ba_winstart) & 0xfff;
	if (count > ba->ba_winsize)	/* no overlap */
		count = ba->ba_winsize;
	while (count-- > 0) {
		/* gaps may exist */
		if (ba->ba_buf[ba->ba_head].m != NULL) {
			ieee80211_inputm(ifp, ba->ba_buf[ba->ba_head].m, ni,
			    &ba->ba_buf[ba->ba_head].rxi, ml);
			ba->ba_buf[ba->ba_head].m = NULL;
			ba->ba_gapwait--;
		} else
			ic->ic_stats.is_ht_rx_ba_frame_lost++;
		ba->ba_head = (ba->ba_head + 1) % IEEE80211_BA_MAX_WINSZ;
	}
	/* move window forward */
	ba->ba_winstart = ssn;
	ba->ba_winend = (ba->ba_winstart + ba->ba_winsize - 1) & 0xfff;

	ieee80211_input_ba_flush(ic, ni, ba, ml);
}

void
ieee80211_enqueue_data(struct ieee80211com *ic, struct mbuf *m,
    struct ieee80211_node *ni, int mcast, struct mbuf_list *ml)
{
	struct ifnet *ifp = &ic->ic_if;
	struct ether_header *eh;
	struct mbuf *m1;

	eh = mtod(m, struct ether_header *);

	if ((ic->ic_flags & IEEE80211_F_RSNON) && !ni->ni_port_valid &&
	    eh->ether_type != htons(ETHERTYPE_EAPOL)) {
		DPRINTF(("port not valid: %s\n",
		    ether_sprintf(eh->ether_dhost)));
		ic->ic_stats.is_rx_unauth++;
		m_freem(m);
		return;
	}

	/*
	 * Perform as a bridge within the AP.  Notice that we do not
	 * bridge EAPOL frames as suggested in C.1.1 of IEEE Std 802.1X.
	 * And we do not forward unicast frames received on a multicast address.
	 */
	m1 = NULL;
#ifndef IEEE80211_STA_ONLY
	if (ic->ic_opmode == IEEE80211_M_HOSTAP &&
	    !(ic->ic_userflags & IEEE80211_F_NOBRIDGE) &&
	    eh->ether_type != htons(ETHERTYPE_EAPOL)) {
		struct ieee80211_node *ni1;

		if (ETHER_IS_MULTICAST(eh->ether_dhost)) {
			m1 = m_dup_pkt(m, ETHER_ALIGN, M_DONTWAIT);
			if (m1 == NULL)
				ifp->if_oerrors++;
			else
				m1->m_flags |= M_MCAST;
		} else if (!mcast) {
			ni1 = ieee80211_find_node(ic, eh->ether_dhost);
			if (ni1 != NULL &&
			    ni1->ni_state == IEEE80211_STA_ASSOC) {
				m1 = m;
				m = NULL;
			}
		}
		if (m1 != NULL) {
			if (if_enqueue(ifp, m1))
				 ifp->if_oerrors++;
		}
	}
#endif
	if (m != NULL) {
		if ((ic->ic_flags & IEEE80211_F_RSNON) &&
		    eh->ether_type == htons(ETHERTYPE_EAPOL)) {
			ifp->if_ipackets++;
#if NBPFILTER > 0
			/*
			 * If we forward frame into transmitter of the AP,
			 * we don't need to duplicate for DLT_EN10MB.
			 */
			if (ifp->if_bpf && m1 == NULL)
				bpf_mtap(ifp->if_bpf, m, BPF_DIRECTION_IN);
#endif
			ieee80211_eapol_key_input(ic, m, ni);
		} else {
			ml_enqueue(ml, m);
		}
	}
}

void
ieee80211_decap(struct ieee80211com *ic, struct mbuf *m,
    struct ieee80211_node *ni, int hdrlen, struct mbuf_list *ml)
{
	struct ether_header eh;
	struct ieee80211_frame *wh;
	struct llc *llc;
	int mcast;

	if (m->m_len < hdrlen + LLC_SNAPFRAMELEN &&
	    (m = m_pullup(m, hdrlen + LLC_SNAPFRAMELEN)) == NULL) {
		ic->ic_stats.is_rx_decap++;
		return;
	}
	wh = mtod(m, struct ieee80211_frame *);
	mcast = IEEE80211_IS_MULTICAST(wh->i_addr1);
	switch (wh->i_fc[1] & IEEE80211_FC1_DIR_MASK) {
	case IEEE80211_FC1_DIR_NODS:
		IEEE80211_ADDR_COPY(eh.ether_dhost, wh->i_addr1);
		IEEE80211_ADDR_COPY(eh.ether_shost, wh->i_addr2);
		break;
	case IEEE80211_FC1_DIR_TODS:
		IEEE80211_ADDR_COPY(eh.ether_dhost, wh->i_addr3);
		IEEE80211_ADDR_COPY(eh.ether_shost, wh->i_addr2);
		break;
	case IEEE80211_FC1_DIR_FROMDS:
		IEEE80211_ADDR_COPY(eh.ether_dhost, wh->i_addr1);
		IEEE80211_ADDR_COPY(eh.ether_shost, wh->i_addr3);
		break;
	case IEEE80211_FC1_DIR_DSTODS:
		IEEE80211_ADDR_COPY(eh.ether_dhost, wh->i_addr3);
		IEEE80211_ADDR_COPY(eh.ether_shost,
		    ((struct ieee80211_frame_addr4 *)wh)->i_addr4);
		break;
	}
	llc = (struct llc *)((caddr_t)wh + hdrlen);
	if (llc->llc_dsap == LLC_SNAP_LSAP &&
	    llc->llc_ssap == LLC_SNAP_LSAP &&
	    llc->llc_control == LLC_UI &&
	    llc->llc_snap.org_code[0] == 0 &&
	    llc->llc_snap.org_code[1] == 0 &&
	    llc->llc_snap.org_code[2] == 0) {
		eh.ether_type = llc->llc_snap.ether_type;
		m_adj(m, hdrlen + LLC_SNAPFRAMELEN - ETHER_HDR_LEN);
	} else {
		eh.ether_type = htons(m->m_pkthdr.len - hdrlen);
		m_adj(m, hdrlen - ETHER_HDR_LEN);
	}
	memcpy(mtod(m, caddr_t), &eh, ETHER_HDR_LEN);
	if (!ALIGNED_POINTER(mtod(m, caddr_t) + ETHER_HDR_LEN, u_int32_t)) {
		struct mbuf *m0 = m;
		m = m_dup_pkt(m0, ETHER_ALIGN, M_NOWAIT);
		m_freem(m0);
		if (m == NULL) {
			ic->ic_stats.is_rx_decap++;
			return;
		}
	}
	ieee80211_enqueue_data(ic, m, ni, mcast, ml);
}

int
ieee80211_amsdu_decap_validate(struct ieee80211com *ic, struct mbuf *m,
    struct ieee80211_node *ni)
{
	struct ether_header *eh = mtod(m, struct ether_header *);
	const uint8_t llc_hdr_mac[ETHER_ADDR_LEN] = {
		/* MAC address matching the 802.2 LLC header. */
		LLC_SNAP_LSAP, LLC_SNAP_LSAP, LLC_UI, 0, 0, 0
	}; 

	/*
	 * We are sorry, but this particular MAC address cannot be used.
	 * This mitigates an attack where a single 802.11 frame is interpreted
	 * as an A-MSDU because of a forged AMSDU-present bit in the 802.11
	 * QoS frame header: https://papers.mathyvanhoef.com/usenix2021.pdf
	 * See Section 7.2, 'Countermeasures for the design flaws'
	 */
	if (ETHER_IS_EQ(eh->ether_dhost, llc_hdr_mac))
		return 1;

	switch (ic->ic_opmode) {
#ifndef IEEE80211_STA_ONLY
	case IEEE80211_M_HOSTAP:
		/*
		 * Subframes must use the source address of the node which
		 * transmitted the A-MSDU. Prevents MAC spoofing.
		 */
		if (!ETHER_IS_EQ(ni->ni_macaddr, eh->ether_shost))
			return 1;
		break;
#endif
	case IEEE80211_M_STA:
		/* Subframes must be addressed to me. */
		if (!ETHER_IS_EQ(ic->ic_myaddr, eh->ether_dhost))
			return 1;
		break;
	default:
		/* Ignore MONITOR/IBSS modes for now. */
		break;
	}

	return 0;
}

/*
 * Decapsulate an Aggregate MSDU (see 7.2.2.2).
 */
void
ieee80211_amsdu_decap(struct ieee80211com *ic, struct mbuf *m,
    struct ieee80211_node *ni, int hdrlen, struct mbuf_list *ml)
{
	struct mbuf *n;
	struct ether_header *eh;
	struct llc *llc;
	int len, pad, mcast;
	struct ieee80211_frame *wh;
	struct mbuf_list subframes = MBUF_LIST_INITIALIZER();

	wh = mtod(m, struct ieee80211_frame *);
	mcast = IEEE80211_IS_MULTICAST(wh->i_addr1);

	/* strip 802.11 header */
	m_adj(m, hdrlen);

	while (m->m_pkthdr.len >= ETHER_HDR_LEN + LLC_SNAPFRAMELEN) {
		/* process an A-MSDU subframe */
		m = m_pullup(m, ETHER_HDR_LEN + LLC_SNAPFRAMELEN);
		if (m == NULL)
			break;
		eh = mtod(m, struct ether_header *);
		/* examine 802.3 header */
		len = ntohs(eh->ether_type);
		if (len < LLC_SNAPFRAMELEN) {
			DPRINTF(("A-MSDU subframe too short (%d)\n", len));
			/* stop processing A-MSDU subframes */
			ic->ic_stats.is_rx_decap++;
			ml_purge(&subframes);
			m_freem(m);
			return;
		}
		llc = (struct llc *)&eh[1];
		/* Examine the 802.2 LLC header after the A-MSDU header. */
		if (llc->llc_dsap == LLC_SNAP_LSAP &&
		    llc->llc_ssap == LLC_SNAP_LSAP &&
		    llc->llc_control == LLC_UI &&
		    llc->llc_snap.org_code[0] == 0 &&
		    llc->llc_snap.org_code[1] == 0 &&
		    llc->llc_snap.org_code[2] == 0) {
			/* convert to Ethernet II header */
			eh->ether_type = llc->llc_snap.ether_type;
			/* strip LLC+SNAP headers */
			memmove((u_int8_t *)eh + LLC_SNAPFRAMELEN, eh,
			    ETHER_HDR_LEN);
			m_adj(m, LLC_SNAPFRAMELEN);
			len -= LLC_SNAPFRAMELEN;
		}
		len += ETHER_HDR_LEN;
		if (len > m->m_pkthdr.len) {
			/* stop processing A-MSDU subframes */
			DPRINTF(("A-MSDU subframe too long (%d)\n", len));
			ic->ic_stats.is_rx_decap++;
			ml_purge(&subframes);
			m_freem(m);
			return;
		}

		/* "detach" our A-MSDU subframe from the others */
		n = m_split(m, len, M_NOWAIT);
		if (n == NULL) {
			/* stop processing A-MSDU subframes */
			ic->ic_stats.is_rx_decap++;
			ml_purge(&subframes);
			m_freem(m);
			return;
		}

		if (ieee80211_amsdu_decap_validate(ic, m, ni)) {
			/* stop processing A-MSDU subframes */
			ic->ic_stats.is_rx_decap++;
			ml_purge(&subframes);
			m_freem(m);
			return;
		}

		ml_enqueue(&subframes, m);
	
		m = n;
		/* remove padding */
		pad = ((len + 3) & ~3) - len;
		m_adj(m, pad);
	}
	
	while ((n = ml_dequeue(&subframes)) != NULL)
		ieee80211_enqueue_data(ic, n, ni, mcast, ml);

	m_freem(m);
}

/*
 * Parse an EDCA Parameter Set element (see 7.3.2.27).
 */
int
ieee80211_parse_edca_params_body(struct ieee80211com *ic, const u_int8_t *frm)
{
	u_int updtcount;
	int aci;

	/*
	 * Check if EDCA parameters have changed XXX if we miss more than
	 * 15 consecutive beacons, we might not detect changes to EDCA
	 * parameters due to wraparound of the 4-bit Update Count field.
	 */
	updtcount = frm[0] & 0xf;
	if (updtcount == ic->ic_edca_updtcount)
		return 0;	/* no changes to EDCA parameters, ignore */
	ic->ic_edca_updtcount = updtcount;

	frm += 2;	/* skip QoS Info & Reserved fields */

	/* parse AC Parameter Records */
	for (aci = 0; aci < EDCA_NUM_AC; aci++) {
		struct ieee80211_edca_ac_params *ac = &ic->ic_edca_ac[aci];

		ac->ac_acm       = (frm[0] >> 4) & 0x1;
		ac->ac_aifsn     = frm[0] & 0xf;
		ac->ac_ecwmin    = frm[1] & 0xf;
		ac->ac_ecwmax    = frm[1] >> 4;
		ac->ac_txoplimit = LE_READ_2(frm + 2);
		frm += 4;
	}
	/* give drivers a chance to update their settings */
	if ((ic->ic_flags & IEEE80211_F_QOS) && ic->ic_updateedca != NULL)
		(*ic->ic_updateedca)(ic);

	return 0;
}

int
ieee80211_parse_edca_params(struct ieee80211com *ic, const u_int8_t *frm)
{
	if (frm[1] < 18) {
		ic->ic_stats.is_rx_elem_toosmall++;
		return IEEE80211_REASON_IE_INVALID;
	}
	return ieee80211_parse_edca_params_body(ic, frm + 2);
}

int
ieee80211_parse_wmm_params(struct ieee80211com *ic, const u_int8_t *frm)
{
	if (frm[1] < 24) {
		ic->ic_stats.is_rx_elem_toosmall++;
		return IEEE80211_REASON_IE_INVALID;
	}
	return ieee80211_parse_edca_params_body(ic, frm + 8);
}

enum ieee80211_cipher
ieee80211_parse_rsn_cipher(const u_int8_t selector[4])
{
	if (memcmp(selector, MICROSOFT_OUI, 3) == 0) {	/* WPA */
		switch (selector[3]) {
		case 0:	/* use group data cipher suite */
			return IEEE80211_CIPHER_USEGROUP;
		case 1:	/* WEP-40 */
			return IEEE80211_CIPHER_WEP40;
		case 2:	/* TKIP */
			return IEEE80211_CIPHER_TKIP;
		case 4:	/* CCMP (RSNA default) */
			return IEEE80211_CIPHER_CCMP;
		case 5:	/* WEP-104 */
			return IEEE80211_CIPHER_WEP104;
		}
	} else if (memcmp(selector, IEEE80211_OUI, 3) == 0) {	/* RSN */
		/* see 802.11-2012 Table 8-99 */
		switch (selector[3]) {
		case 0:	/* use group data cipher suite */
			return IEEE80211_CIPHER_USEGROUP;
		case 1:	/* WEP-40 */
			return IEEE80211_CIPHER_WEP40;
		case 2:	/* TKIP */
			return IEEE80211_CIPHER_TKIP;
		case 4:	/* CCMP (RSNA default) */
			return IEEE80211_CIPHER_CCMP;
		case 5:	/* WEP-104 */
			return IEEE80211_CIPHER_WEP104;
		case 6:	/* BIP */
			return IEEE80211_CIPHER_BIP;
		}
	}
	return IEEE80211_CIPHER_NONE;	/* ignore unknown ciphers */
}

enum ieee80211_akm
ieee80211_parse_rsn_akm(const u_int8_t selector[4])
{
	if (memcmp(selector, MICROSOFT_OUI, 3) == 0) {	/* WPA */
		switch (selector[3]) {
		case 1:	/* IEEE 802.1X (RSNA default) */
			return IEEE80211_AKM_8021X;
		case 2:	/* PSK */
			return IEEE80211_AKM_PSK;
		}
	} else if (memcmp(selector, IEEE80211_OUI, 3) == 0) {	/* RSN */
		/* from IEEE Std 802.11i-2004 - Table 20dc */
		switch (selector[3]) {
		case 1:	/* IEEE 802.1X (RSNA default) */
			return IEEE80211_AKM_8021X;
		case 2:	/* PSK */
			return IEEE80211_AKM_PSK;
		case 5:	/* IEEE 802.1X with SHA256 KDF */
			return IEEE80211_AKM_SHA256_8021X;
		case 6:	/* PSK with SHA256 KDF */
			return IEEE80211_AKM_SHA256_PSK;
		case 8:	/* SAE */
			return IEEE80211_AKM_SAE;
		}
	}
	return IEEE80211_AKM_NONE;	/* ignore unknown AKMs */
}

/*
 * Parse an RSN element (see 802.11-2012 8.4.2.27)
 */
int
ieee80211_parse_rsn_body(struct ieee80211com *ic, const u_int8_t *frm,
    u_int len, struct ieee80211_rsnparams *rsn)
{
	const u_int8_t *efrm;
	u_int16_t m, n, s;

	efrm = frm + len;

	/* check Version field */
	if (LE_READ_2(frm) != 1)
		return IEEE80211_STATUS_RSN_IE_VER_UNSUP;
	frm += 2;

	/* all fields after the Version field are optional */

	/* if Cipher Suite missing, default to CCMP */
	rsn->rsn_groupcipher = IEEE80211_CIPHER_CCMP;
	rsn->rsn_nciphers = 1;
	rsn->rsn_ciphers = IEEE80211_CIPHER_CCMP;
	/* if Group Management Cipher Suite missing, default to BIP */
	rsn->rsn_groupmgmtcipher = IEEE80211_CIPHER_BIP;
	/* if AKM Suite missing, default to 802.1X */
	rsn->rsn_nakms = 1;
	rsn->rsn_akms = IEEE80211_AKM_8021X;
	/* if RSN capabilities missing, default to 0 */
	rsn->rsn_caps = 0;
	rsn->rsn_npmkids = 0;

	/* read Group Data Cipher Suite field */
	if (frm + 4 > efrm)
		return 0;
	rsn->rsn_groupcipher = ieee80211_parse_rsn_cipher(frm);
	if (rsn->rsn_groupcipher == IEEE80211_CIPHER_NONE ||
	    rsn->rsn_groupcipher == IEEE80211_CIPHER_USEGROUP ||
	    rsn->rsn_groupcipher == IEEE80211_CIPHER_BIP)
		return IEEE80211_STATUS_BAD_GROUP_CIPHER;
	frm += 4;

	/* read Pairwise Cipher Suite Count field */
	if (frm + 2 > efrm)
		return 0;
	m = rsn->rsn_nciphers = LE_READ_2(frm);
	frm += 2;

	/* read Pairwise Cipher Suite List */
	if (frm + m * 4 > efrm)
		return IEEE80211_STATUS_IE_INVALID;
	rsn->rsn_ciphers = IEEE80211_CIPHER_NONE;
	while (m-- > 0) {
		rsn->rsn_ciphers |= ieee80211_parse_rsn_cipher(frm);
		frm += 4;
	}
	if (rsn->rsn_ciphers & IEEE80211_CIPHER_USEGROUP) {
		if (rsn->rsn_ciphers != IEEE80211_CIPHER_USEGROUP)
			return IEEE80211_STATUS_BAD_PAIRWISE_CIPHER;
		if (rsn->rsn_groupcipher == IEEE80211_CIPHER_CCMP)
			return IEEE80211_STATUS_BAD_PAIRWISE_CIPHER;
	}

	/* read AKM Suite List Count field */
	if (frm + 2 > efrm)
		return 0;
	n = rsn->rsn_nakms = LE_READ_2(frm);
	frm += 2;

	/* read AKM Suite List */
	if (frm + n * 4 > efrm)
		return IEEE80211_STATUS_IE_INVALID;
	rsn->rsn_akms = IEEE80211_AKM_NONE;
	while (n-- > 0) {
		rsn->rsn_akms |= ieee80211_parse_rsn_akm(frm);
		frm += 4;
	}

	/* read RSN Capabilities field */
	if (frm + 2 > efrm)
		return 0;
	rsn->rsn_caps = LE_READ_2(frm);
	frm += 2;

	/* read PMKID Count field */
	if (frm + 2 > efrm)
		return 0;
	s = rsn->rsn_npmkids = LE_READ_2(frm);
	frm += 2;

	/* read PMKID List */
	if (frm + s * IEEE80211_PMKID_LEN > efrm)
		return IEEE80211_STATUS_IE_INVALID;
	if (s != 0) {
		rsn->rsn_pmkids = frm;
		frm += s * IEEE80211_PMKID_LEN;
	}

	/* read Group Management Cipher Suite field */
	if (frm + 4 > efrm)
		return 0;
	rsn->rsn_groupmgmtcipher = ieee80211_parse_rsn_cipher(frm);
	if (rsn->rsn_groupmgmtcipher != IEEE80211_CIPHER_BIP)
		return IEEE80211_STATUS_BAD_GROUP_CIPHER;

	return IEEE80211_STATUS_SUCCESS;
}

int
ieee80211_parse_rsn(struct ieee80211com *ic, const u_int8_t *frm,
    struct ieee80211_rsnparams *rsn)
{
	if (frm[1] < 2) {
		ic->ic_stats.is_rx_elem_toosmall++;
		return IEEE80211_STATUS_IE_INVALID;
	}
	return ieee80211_parse_rsn_body(ic, frm + 2, frm[1], rsn);
}

int
ieee80211_parse_wpa(struct ieee80211com *ic, const u_int8_t *frm,
    struct ieee80211_rsnparams *rsn)
{
	if (frm[1] < 6) {
		ic->ic_stats.is_rx_elem_toosmall++;
		return IEEE80211_STATUS_IE_INVALID;
	}
	return ieee80211_parse_rsn_body(ic, frm + 6, frm[1] - 4, rsn);
}

/*
 * Create (or update) a copy of an information element.
 */
int
ieee80211_save_ie(const u_int8_t *frm, u_int8_t **ie)
{
	int olen = *ie ? 2 + (*ie)[1] : 0;
	int len = 2 + frm[1];

	if (*ie == NULL || olen != len) {
		if (*ie != NULL)
			free(*ie, M_DEVBUF, olen);
		*ie = malloc(len, M_DEVBUF, M_NOWAIT);
		if (*ie == NULL)
			return ENOMEM;
	}
	memcpy(*ie, frm, len);
	return 0;
}

/*-
 * Beacon/Probe response frame format:
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
 * [tlv] QoS Capability (Beacon only, 802.11e)
 * [tlv] HT Capabilities (802.11n)
 * [tlv] HT Operation (802.11n)
 */
void
ieee80211_recv_probe_resp(struct ieee80211com *ic, struct mbuf *m,
    struct ieee80211_node *rni, struct ieee80211_rxinfo *rxi, int isprobe)
{
	struct ieee80211_node *ni;
	const struct ieee80211_frame *wh;
	const u_int8_t *frm, *efrm;
	const u_int8_t *tstamp, *ssid, *rates, *xrates, *edcaie, *wmmie, *tim;
	const u_int8_t *rsnie, *wpaie, *htcaps, *htop, *vhtcaps, *vhtop;
	u_int16_t capinfo, bintval;
	u_int8_t chan, bchan, erp;
	int is_new;

	/*
	 * We process beacon/probe response frames for:
	 *    o station mode: to collect state
	 *      updates such as 802.11g slot time and for passive
	 *      scanning of APs
	 *    o adhoc mode: to discover neighbors
	 *    o hostap mode: for passive scanning of neighbor APs
	 *    o when scanning
	 * In other words, in all modes other than monitor (which
	 * does not process incoming frames) and adhoc-demo (which
	 * does not use management frames at all).
	 */
#ifdef DIAGNOSTIC
	if (ic->ic_opmode != IEEE80211_M_STA &&
#ifndef IEEE80211_STA_ONLY
	    ic->ic_opmode != IEEE80211_M_IBSS &&
	    ic->ic_opmode != IEEE80211_M_HOSTAP &&
#endif
	    ic->ic_state != IEEE80211_S_SCAN) {
		panic("%s: impossible operating mode", __func__);
	}
#endif
	/* make sure all mandatory fixed fields are present */
	if (m->m_len < sizeof(*wh) + 12) {
		DPRINTF(("frame too short\n"));
		return;
	}
	wh = mtod(m, struct ieee80211_frame *);
	frm = (const u_int8_t *)&wh[1];
	efrm = mtod(m, u_int8_t *) + m->m_len;

	tstamp  = frm; frm += 8;
	bintval = LE_READ_2(frm); frm += 2;
	capinfo = LE_READ_2(frm); frm += 2;

	ssid = rates = xrates = edcaie = wmmie = rsnie = wpaie = tim = NULL;
	htcaps = htop = vhtcaps = vhtop = NULL;
	if (rxi->rxi_chan)
		bchan = rxi->rxi_chan;
	else
		bchan = ieee80211_chan2ieee(ic, ic->ic_bss->ni_chan);
	chan = bchan;
	erp = 0;
	while (frm + 2 <= efrm) {
		if (frm + 2 + frm[1] > efrm) {
			ic->ic_stats.is_rx_elem_toosmall++;
			break;
		}
		switch (frm[0]) {
		case IEEE80211_ELEMID_SSID:
			ssid = frm;
			break;
		case IEEE80211_ELEMID_RATES:
			rates = frm;
			break;
		case IEEE80211_ELEMID_DSPARMS:
			if (frm[1] < 1) {
				ic->ic_stats.is_rx_elem_toosmall++;
				break;
			}
			chan = frm[2];
			break;
		case IEEE80211_ELEMID_XRATES:
			xrates = frm;
			break;
		case IEEE80211_ELEMID_ERP:
			if (frm[1] < 1) {
				ic->ic_stats.is_rx_elem_toosmall++;
				break;
			}
			erp = frm[2];
			break;
		case IEEE80211_ELEMID_RSN:
			rsnie = frm;
			break;
		case IEEE80211_ELEMID_EDCAPARMS:
			edcaie = frm;
			break;
		case IEEE80211_ELEMID_HTCAPS:
			htcaps = frm;
			break;
		case IEEE80211_ELEMID_HTOP:
			if (frm[1] < 22) {
				ic->ic_stats.is_rx_elem_toosmall++;
				break;
			}
			htop = frm;
			chan = frm[2];
			break;
		case IEEE80211_ELEMID_VHTCAPS:
			vhtcaps = frm;
			break;
		case IEEE80211_ELEMID_VHTOP:
			vhtop = frm;
			break;
		case IEEE80211_ELEMID_TIM:
			if (frm[1] < 4) {
				ic->ic_stats.is_rx_elem_toosmall++;
				break;
			}
			tim = frm;
			break;
		case IEEE80211_ELEMID_VENDOR:
			if (frm[1] < 4) {
				ic->ic_stats.is_rx_elem_toosmall++;
				break;
			}
			if (memcmp(frm + 2, MICROSOFT_OUI, 3) == 0) {
				if (frm[5] == 1)
					wpaie = frm;
				else if (frm[1] >= 5 &&
				    frm[5] == 2 && frm[6] == 1)
					wmmie = frm;
			}
			break;
		}
		frm += 2 + frm[1];
	}
	/* supported rates element is mandatory */
	if (rates == NULL || rates[1] > IEEE80211_RATE_MAXSIZE) {
		DPRINTF(("invalid supported rates element\n"));
		return;
	}
	/* SSID element is mandatory */
	if (ssid == NULL || ssid[1] > IEEE80211_NWID_LEN) {
		DPRINTF(("invalid SSID element\n"));
		return;
	}

	if (
#if IEEE80211_CHAN_MAX < 255
	    chan > IEEE80211_CHAN_MAX ||
#endif
	    (isclr(ic->ic_chan_active, chan) &&
	     ((ic->ic_caps & IEEE80211_C_SCANALL) == 0 ||
	     (ic->ic_flags & IEEE80211_F_BGSCAN) == 0))) {
		DPRINTF(("ignore %s with invalid channel %u\n",
		    isprobe ? "probe response" : "beacon", chan));
		ic->ic_stats.is_rx_badchan++;
		return;
	}
	if ((rxi->rxi_chan != 0 && chan != rxi->rxi_chan) ||
	    ((ic->ic_state != IEEE80211_S_SCAN ||
	     !(ic->ic_caps & IEEE80211_C_SCANALL)) &&
	    chan != bchan)) {
		/*
		 * Frame was received on a channel different from the
		 * one indicated in the DS params element id;
		 * silently discard it.
		 *
		 * NB: this can happen due to signal leakage.
		 */
		DPRINTF(("ignore %s on channel %u marked for channel %u\n",
		    isprobe ? "probe response" : "beacon", bchan, chan));
		ic->ic_stats.is_rx_chanmismatch++;
		return;
	}

#ifdef IEEE80211_DEBUG
	if (ieee80211_debug > 1 &&
	    (ni == NULL || ic->ic_state == IEEE80211_S_SCAN ||
	    (ic->ic_flags & IEEE80211_F_BGSCAN))) {
		printf("%s: %s%s on chan %u (bss chan %u) ",
		    __func__, (ni == NULL ? "new " : ""),
		    isprobe ? "probe response" : "beacon",
		    chan, bchan);
		ieee80211_print_essid(ssid + 2, ssid[1]);
		printf(" from %s\n", ether_sprintf((u_int8_t *)wh->i_addr2));
		printf("%s: caps 0x%x bintval %u erp 0x%x\n",
			__func__, capinfo, bintval, erp);
	}
#endif

	if ((ni = ieee80211_find_node(ic, wh->i_addr2)) == NULL) {
		ni = ieee80211_alloc_node(ic, wh->i_addr2);
		if (ni == NULL)
			return;
		is_new = 1;
	} else
		is_new = 0;

	ni->ni_chan = &ic->ic_channels[chan];

	if (htcaps)
		ieee80211_setup_htcaps(ni, htcaps + 2, htcaps[1]);
	if (htop && !ieee80211_setup_htop(ni, htop + 2, htop[1], 1))
		htop = NULL; /* invalid HTOP */
	if (htcaps && vhtcaps && IEEE80211_IS_CHAN_5GHZ(ni->ni_chan)) {
		ieee80211_setup_vhtcaps(ni, vhtcaps + 2, vhtcaps[1]);
		if (vhtop && !ieee80211_setup_vhtop(ni, vhtop + 2, vhtop[1], 1))
			vhtop = NULL; /* invalid VHTOP */
	}

	if (tim) {
		ni->ni_dtimcount = tim[2];
		ni->ni_dtimperiod = tim[3];
	}

	/*
	 * When operating in station mode, check for state updates
	 * while we're associated.
	 */
	if (ic->ic_opmode == IEEE80211_M_STA &&
	    ic->ic_state == IEEE80211_S_RUN &&
	    ni->ni_state == IEEE80211_STA_BSS) {
		int updateprot = 0;
		/*
		 * Check if protection mode has changed since last beacon.
		 */
		if (ni->ni_erp != erp) {
			DPRINTF(("[%s] erp change: was 0x%x, now 0x%x\n",
			    ether_sprintf((u_int8_t *)wh->i_addr2),
			    ni->ni_erp, erp));
			if ((ic->ic_curmode == IEEE80211_MODE_11G ||
			    (ic->ic_curmode == IEEE80211_MODE_11N &&
			    IEEE80211_IS_CHAN_2GHZ(ni->ni_chan))) &&
			    (erp & IEEE80211_ERP_USE_PROTECTION))
				ic->ic_flags |= IEEE80211_F_USEPROT;
			else
				ic->ic_flags &= ~IEEE80211_F_USEPROT;
			ic->ic_bss->ni_erp = erp;
			updateprot = 1;
		}
		if (htop && (ic->ic_bss->ni_flags & IEEE80211_NODE_HT)) {
			enum ieee80211_htprot htprot_last, htprot;
			htprot_last =
			    ((ic->ic_bss->ni_htop1 & IEEE80211_HTOP1_PROT_MASK)
			    >> IEEE80211_HTOP1_PROT_SHIFT);
			htprot = ((ni->ni_htop1 & IEEE80211_HTOP1_PROT_MASK) >>
			    IEEE80211_HTOP1_PROT_SHIFT);
			if (htprot_last != htprot) {
				DPRINTF(("[%s] htprot change: was %d, now %d\n",
				    ether_sprintf((u_int8_t *)wh->i_addr2),
				    htprot_last, htprot));
				ic->ic_stats.is_ht_prot_change++;
				ic->ic_bss->ni_htop1 = ni->ni_htop1;
				updateprot = 1;
			}
		}
		if (updateprot && ic->ic_updateprot != NULL)
			ic->ic_updateprot(ic);

		/*
		 * Check if 40MHz channel mode has changed since last beacon.
		 */
		if (htop && (ic->ic_bss->ni_flags & IEEE80211_NODE_HT) &&
		    (ic->ic_htcaps & IEEE80211_HTCAP_CBW20_40)) {
			uint8_t chw_last, chw, sco_last, sco;
			chw_last = (ic->ic_bss->ni_htop0 & IEEE80211_HTOP0_CHW);
			chw = (ni->ni_htop0 & IEEE80211_HTOP0_CHW);
			sco_last =
			    ((ic->ic_bss->ni_htop0 & IEEE80211_HTOP0_SCO_MASK)
			    >> IEEE80211_HTOP0_SCO_SHIFT);
			sco = ((ni->ni_htop0 & IEEE80211_HTOP0_SCO_MASK) >>
			    IEEE80211_HTOP0_SCO_SHIFT);
			ic->ic_bss->ni_htop0 = ni->ni_htop0;
			if (chw_last != chw || sco_last != sco) {
				if (ic->ic_updatechan != NULL)
					ic->ic_updatechan(ic);
			}
		} else if (htop)
			ic->ic_bss->ni_htop0 = ni->ni_htop0;

		/*
		 * Check if AP short slot time setting has changed
		 * since last beacon and give the driver a chance to
		 * update the hardware.
		 */
		if ((ni->ni_capinfo ^ capinfo) &
		    IEEE80211_CAPINFO_SHORT_SLOTTIME) {
			ieee80211_set_shortslottime(ic,
			    ic->ic_curmode == IEEE80211_MODE_11A ||
			    (capinfo & IEEE80211_CAPINFO_SHORT_SLOTTIME));
		}

		if (tim && ic->ic_bss->ni_dtimperiod != ni->ni_dtimperiod) {
			ic->ic_bss->ni_dtimperiod = ni->ni_dtimperiod;
			ic->ic_bss->ni_dtimcount = ni->ni_dtimcount;

			if (ic->ic_updatedtim != NULL)
				ic->ic_updatedtim(ic);
		}

		/* 
		 * Reset management timer. If it is non-zero in RUN state, the
		 * driver sent a probe request after a missed beacon event.
		 * This probe response indicates the AP is still serving us
		 * so don't allow ieee80211_watchdog() to move us into SCAN.
		 */
		if ((ic->ic_flags & IEEE80211_F_BGSCAN) == 0)
		 	ic->ic_mgt_timer = 0;
	}
	/*
	 * We do not try to update EDCA parameters if QoS was not negotiated
	 * with the AP at association time.
	 */
	if (ni->ni_flags & IEEE80211_NODE_QOS) {
		/* always prefer EDCA IE over Wi-Fi Alliance WMM IE */
		if ((edcaie != NULL &&
		     ieee80211_parse_edca_params(ic, edcaie) == 0) ||
		    (wmmie != NULL &&
		     ieee80211_parse_wmm_params(ic, wmmie) == 0))
			ni->ni_flags |= IEEE80211_NODE_QOS;
		else
			ni->ni_flags &= ~IEEE80211_NODE_QOS;
	}

	if (ic->ic_state == IEEE80211_S_SCAN ||
	    (ic->ic_flags & IEEE80211_F_BGSCAN)) {
		struct ieee80211_rsnparams rsn, wpa;

		ni->ni_rsnprotos = IEEE80211_PROTO_NONE;
		ni->ni_supported_rsnprotos = IEEE80211_PROTO_NONE;
		ni->ni_rsnakms = 0;
		ni->ni_supported_rsnakms = 0;
		ni->ni_rsnciphers = 0;
		ni->ni_rsngroupcipher = 0;
		ni->ni_rsngroupmgmtcipher = 0;
		ni->ni_rsncaps = 0;

		if (rsnie != NULL &&
		    ieee80211_parse_rsn(ic, rsnie, &rsn) == 0) {
			ni->ni_supported_rsnprotos |= IEEE80211_PROTO_RSN;
			ni->ni_supported_rsnakms |= rsn.rsn_akms;
		}
		if (wpaie != NULL &&
		    ieee80211_parse_wpa(ic, wpaie, &wpa) == 0) {
			ni->ni_supported_rsnprotos |= IEEE80211_PROTO_WPA;
			ni->ni_supported_rsnakms |= wpa.rsn_akms;
		}

		/*
		 * If the AP advertises both WPA and RSN IEs (WPA1+WPA2),
		 * we only use the highest protocol version we support.
		 */
		if (rsnie != NULL &&
		    (ni->ni_supported_rsnprotos & IEEE80211_PROTO_RSN) &&
		    (ic->ic_caps & IEEE80211_C_RSN)) {
			if (ieee80211_save_ie(rsnie, &ni->ni_rsnie) == 0
#ifndef IEEE80211_STA_ONLY
	    		&& ic->ic_opmode != IEEE80211_M_HOSTAP
#endif
			) {
				ni->ni_rsnprotos = IEEE80211_PROTO_RSN;
				ni->ni_rsnakms = rsn.rsn_akms;
				ni->ni_rsnciphers = rsn.rsn_ciphers;
				ni->ni_rsngroupcipher = rsn.rsn_groupcipher;
				ni->ni_rsngroupmgmtcipher =
				    rsn.rsn_groupmgmtcipher;
				ni->ni_rsncaps = rsn.rsn_caps;
			}
		} else if (wpaie != NULL &&
		    (ni->ni_supported_rsnprotos & IEEE80211_PROTO_WPA) &&
		    (ic->ic_caps & IEEE80211_C_RSN)) {
			if (ieee80211_save_ie(wpaie, &ni->ni_rsnie) == 0
#ifndef IEEE80211_STA_ONLY
	    		&& ic->ic_opmode != IEEE80211_M_HOSTAP
#endif
			) {
				ni->ni_rsnprotos = IEEE80211_PROTO_WPA;
				ni->ni_rsnakms = wpa.rsn_akms;
				ni->ni_rsnciphers = wpa.rsn_ciphers;
				ni->ni_rsngroupcipher = wpa.rsn_groupcipher;
				ni->ni_rsngroupmgmtcipher =
				    wpa.rsn_groupmgmtcipher;
				ni->ni_rsncaps = wpa.rsn_caps;
			}
		}
	}

	/*
	 * Set our SSID if we do not know it yet.
	 * If we are doing a directed scan for an AP with a hidden SSID
	 * we must collect the SSID from a probe response to override
	 * a non-zero-length SSID filled with zeroes that we may have
	 * received earlier in a beacon.
	 */
	if (ssid[1] != 0 && ni->ni_essid[0] == '\0') {
		ni->ni_esslen = ssid[1];
		memset(ni->ni_essid, 0, sizeof(ni->ni_essid));
		/* we know that ssid[1] <= IEEE80211_NWID_LEN */
		memcpy(ni->ni_essid, &ssid[2], ssid[1]);
	}
	IEEE80211_ADDR_COPY(ni->ni_bssid, wh->i_addr3);
	if (ic->ic_state == IEEE80211_S_SCAN &&
	    IEEE80211_IS_CHAN_5GHZ(ni->ni_chan)) {
		/*
		 * During a scan on 5Ghz, prefer RSSI measured for probe
		 * response frames. i.e. don't allow beacons to lower the
		 * measured RSSI. Some 5GHz APs send beacons with much
		 * less Tx power than they use for probe responses.
		 */
		if (isprobe || ni->ni_rssi == 0)
			ni->ni_rssi = rxi->rxi_rssi;
		else if (ni->ni_rssi < rxi->rxi_rssi)
			ni->ni_rssi = rxi->rxi_rssi;
	} else
		ni->ni_rssi = rxi->rxi_rssi;
	ni->ni_rstamp = rxi->rxi_tstamp;
	memcpy(ni->ni_tstamp, tstamp, sizeof(ni->ni_tstamp));
	ni->ni_intval = bintval;
	ni->ni_capinfo = capinfo;
	ni->ni_erp = erp;
	/* NB: must be after ni_chan is setup */
	ieee80211_setup_rates(ic, ni, rates, xrates, IEEE80211_F_DOSORT);
#ifndef IEEE80211_STA_ONLY
	if (ic->ic_opmode == IEEE80211_M_IBSS && is_new && isprobe) {
		/*
		 * Fake an association so the driver can setup its
		 * private state.  The rate set has been setup above;
		 * there is no handshake as in ap/station operation.
		 */
		if (ic->ic_newassoc)
			(*ic->ic_newassoc)(ic, ni, 1);
	}
#endif
}

#ifndef IEEE80211_STA_ONLY
/*-
 * Probe request frame format:
 * [tlv] SSID
 * [tlv] Supported rates
 * [tlv] Extended Supported Rates (802.11g)
 * [tlv] HT Capabilities (802.11n)
 */
void
ieee80211_recv_probe_req(struct ieee80211com *ic, struct mbuf *m,
    struct ieee80211_node *ni, struct ieee80211_rxinfo *rxi)
{
	const struct ieee80211_frame *wh;
	const u_int8_t *frm, *efrm;
	const u_int8_t *ssid, *rates, *xrates, *htcaps, *vhtcaps;
	u_int8_t rate;

	if (ic->ic_opmode == IEEE80211_M_STA ||
	    ic->ic_state != IEEE80211_S_RUN)
		return;

	wh = mtod(m, struct ieee80211_frame *);
	frm = (const u_int8_t *)&wh[1];
	efrm = mtod(m, u_int8_t *) + m->m_len;

	ssid = rates = xrates = htcaps = vhtcaps = NULL;
	while (frm + 2 <= efrm) {
		if (frm + 2 + frm[1] > efrm) {
			ic->ic_stats.is_rx_elem_toosmall++;
			break;
		}
		switch (frm[0]) {
		case IEEE80211_ELEMID_SSID:
			ssid = frm;
			break;
		case IEEE80211_ELEMID_RATES:
			rates = frm;
			break;
		case IEEE80211_ELEMID_XRATES:
			xrates = frm;
			break;
		case IEEE80211_ELEMID_HTCAPS:
			htcaps = frm;
			break;
		case IEEE80211_ELEMID_VHTCAPS:
			vhtcaps = frm;
			break;
		}
		frm += 2 + frm[1];
	}
	/* supported rates element is mandatory */
	if (rates == NULL || rates[1] > IEEE80211_RATE_MAXSIZE) {
		DPRINTF(("invalid supported rates element\n"));
		return;
	}
	/* SSID element is mandatory */
	if (ssid == NULL || ssid[1] > IEEE80211_NWID_LEN) {
		DPRINTF(("invalid SSID element\n"));
		return;
	}
	/* check that the specified SSID (if not wildcard) matches ours */
	if (ssid[1] != 0 && (ssid[1] != ic->ic_bss->ni_esslen ||
	    memcmp(&ssid[2], ic->ic_bss->ni_essid, ic->ic_bss->ni_esslen))) {
		DPRINTF(("SSID mismatch\n"));
		ic->ic_stats.is_rx_ssidmismatch++;
		return;
	}
	/* refuse wildcard SSID if we're hiding our SSID in beacons */
	if (ssid[1] == 0 && (ic->ic_userflags & IEEE80211_F_HIDENWID)) {
		DPRINTF(("wildcard SSID rejected"));
		ic->ic_stats.is_rx_ssidmismatch++;
		return;
	}

	if (ni == ic->ic_bss) {
		ni = ieee80211_find_node(ic, wh->i_addr2);
		if (ni == NULL)
			ni = ieee80211_dup_bss(ic, wh->i_addr2);
		if (ni == NULL)
			return;
		DPRINTF(("new probe req from %s\n",
		    ether_sprintf((u_int8_t *)wh->i_addr2)));
	}
	ni->ni_rssi = rxi->rxi_rssi;
	ni->ni_rstamp = rxi->rxi_tstamp;
	rate = ieee80211_setup_rates(ic, ni, rates, xrates,
	    IEEE80211_F_DOSORT | IEEE80211_F_DOFRATE | IEEE80211_F_DONEGO |
	    IEEE80211_F_DODEL);
	if (rate & IEEE80211_RATE_BASIC) {
		DPRINTF(("rate mismatch for %s\n",
		    ether_sprintf((u_int8_t *)wh->i_addr2)));
		return;
	}
	if (htcaps)
		ieee80211_setup_htcaps(ni, htcaps + 2, htcaps[1]);
	else
		ieee80211_clear_htcaps(ni);
	if (htcaps && vhtcaps && IEEE80211_IS_CHAN_5GHZ(ic->ic_bss->ni_chan))
		ieee80211_setup_vhtcaps(ni, vhtcaps + 2, vhtcaps[1]);
	else
		ieee80211_clear_vhtcaps(ni);
	IEEE80211_SEND_MGMT(ic, ni, IEEE80211_FC0_SUBTYPE_PROBE_RESP, 0);
}
#endif	/* IEEE80211_STA_ONLY */

/*-
 * Authentication frame format:
 * [2] Authentication algorithm number
 * [2] Authentication transaction sequence number
 * [2] Status code
 */
void
ieee80211_recv_auth(struct ieee80211com *ic, struct mbuf *m,
    struct ieee80211_node *ni, struct ieee80211_rxinfo *rxi)
{
	const struct ieee80211_frame *wh;
	const u_int8_t *frm;
	u_int16_t algo, seq, status;

	/* make sure all mandatory fixed fields are present */
	if (m->m_len < sizeof(*wh) + 6) {
		DPRINTF(("frame too short\n"));
		return;
	}
	wh = mtod(m, struct ieee80211_frame *);
	frm = (const u_int8_t *)&wh[1];

	algo   = LE_READ_2(frm); frm += 2;
	seq    = LE_READ_2(frm); frm += 2;
	status = LE_READ_2(frm); frm += 2;
	DPRINTF(("auth %d seq %d from %s\n", algo, seq,
	    ether_sprintf((u_int8_t *)wh->i_addr2)));

	/* only "open" auth mode is supported */
	if (algo != IEEE80211_AUTH_ALG_OPEN) {
		DPRINTF(("unsupported auth algorithm %d from %s\n",
		    algo, ether_sprintf((u_int8_t *)wh->i_addr2)));
		ic->ic_stats.is_rx_auth_unsupported++;
#ifndef IEEE80211_STA_ONLY
		if (ic->ic_opmode == IEEE80211_M_HOSTAP) {
			/* XXX hack to workaround calling convention */
			IEEE80211_SEND_MGMT(ic, ni,
			    IEEE80211_FC0_SUBTYPE_AUTH,
			    IEEE80211_STATUS_ALG << 16 | ((seq + 1) & 0xfff));
		}
#endif
		return;
	}
	ieee80211_auth_open(ic, wh, ni, rxi, seq, status);
}

#ifndef IEEE80211_STA_ONLY
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
void
ieee80211_recv_assoc_req(struct ieee80211com *ic, struct mbuf *m,
    struct ieee80211_node *ni, struct ieee80211_rxinfo *rxi, int reassoc)
{
	const struct ieee80211_frame *wh;
	const u_int8_t *frm, *efrm;
	const u_int8_t *ssid, *rates, *xrates, *rsnie, *wpaie, *wmeie;
	const u_int8_t *htcaps, *vhtcaps;
	u_int16_t capinfo, bintval;
	int resp, status = 0;
	struct ieee80211_rsnparams rsn;
	u_int8_t rate;
	const u_int8_t *saveie = NULL;

	if (ic->ic_opmode != IEEE80211_M_HOSTAP ||
	    ic->ic_state != IEEE80211_S_RUN)
		return;

	/* make sure all mandatory fixed fields are present */
	if (m->m_len < sizeof(*wh) + (reassoc ? 10 : 4)) {
		DPRINTF(("frame too short\n"));
		return;
	}
	wh = mtod(m, struct ieee80211_frame *);
	frm = (const u_int8_t *)&wh[1];
	efrm = mtod(m, u_int8_t *) + m->m_len;

	if (!IEEE80211_ADDR_EQ(wh->i_addr3, ic->ic_bss->ni_bssid)) {
		DPRINTF(("ignore other bss from %s\n",
		    ether_sprintf((u_int8_t *)wh->i_addr2)));
		ic->ic_stats.is_rx_assoc_bss++;
		return;
	}
	capinfo = LE_READ_2(frm); frm += 2;
	bintval = LE_READ_2(frm); frm += 2;
	if (reassoc) {
		frm += IEEE80211_ADDR_LEN;	/* skip current AP address */
		resp = IEEE80211_FC0_SUBTYPE_REASSOC_RESP;
	} else
		resp = IEEE80211_FC0_SUBTYPE_ASSOC_RESP;

	ssid = rates = xrates = rsnie = wpaie = wmeie = htcaps = vhtcaps = NULL;
	while (frm + 2 <= efrm) {
		if (frm + 2 + frm[1] > efrm) {
			ic->ic_stats.is_rx_elem_toosmall++;
			break;
		}
		switch (frm[0]) {
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
			rsnie = frm;
			break;
		case IEEE80211_ELEMID_QOS_CAP:
			break;
		case IEEE80211_ELEMID_HTCAPS:
			htcaps = frm;
			break;
		case IEEE80211_ELEMID_VHTCAPS:
			vhtcaps = frm;
			break;
		case IEEE80211_ELEMID_VENDOR:
			if (frm[1] < 4) {
				ic->ic_stats.is_rx_elem_toosmall++;
				break;
			}
			if (memcmp(frm + 2, MICROSOFT_OUI, 3) == 0) {
				if (frm[5] == 1)
					wpaie = frm;
				/* WME info IE: len=7 type=2 subtype=0 */
				if (frm[1] == 7 && frm[5] == 2 && frm[6] == 0)
					wmeie = frm;
			}
			break;
		}
		frm += 2 + frm[1];
	}
	/* supported rates element is mandatory */
	if (rates == NULL || rates[1] > IEEE80211_RATE_MAXSIZE) {
		DPRINTF(("invalid supported rates element\n"));
		return;
	}
	/* SSID element is mandatory */
	if (ssid == NULL || ssid[1] > IEEE80211_NWID_LEN) {
		DPRINTF(("invalid SSID element\n"));
		return;
	}
	/* check that the specified SSID matches ours */
	if (ssid[1] != ic->ic_bss->ni_esslen ||
	    memcmp(&ssid[2], ic->ic_bss->ni_essid, ic->ic_bss->ni_esslen)) {
		DPRINTF(("SSID mismatch\n"));
		ic->ic_stats.is_rx_ssidmismatch++;
		return;
	}

	if (ni->ni_state != IEEE80211_STA_AUTH &&
	    ni->ni_state != IEEE80211_STA_ASSOC) {
		DPRINTF(("deny %sassoc from %s, not authenticated\n",
		    reassoc ? "re" : "",
		    ether_sprintf((u_int8_t *)wh->i_addr2)));
		ni = ieee80211_find_node(ic, wh->i_addr2);
		if (ni == NULL)
			ni = ieee80211_dup_bss(ic, wh->i_addr2);
		if (ni != NULL) {
			IEEE80211_SEND_MGMT(ic, ni,
			    IEEE80211_FC0_SUBTYPE_DEAUTH,
			    IEEE80211_REASON_ASSOC_NOT_AUTHED);
		}
		ic->ic_stats.is_rx_assoc_notauth++;
		return;
	}

	if (ni->ni_state == IEEE80211_STA_ASSOC &&
	    (ni->ni_flags & IEEE80211_NODE_MFP)) {
		if (ni->ni_flags & IEEE80211_NODE_SA_QUERY_FAILED) {
			/* send a protected Disassociate frame */
			IEEE80211_SEND_MGMT(ic, ni,
			    IEEE80211_FC0_SUBTYPE_DISASSOC,
			    IEEE80211_REASON_AUTH_EXPIRE);
			/* terminate the old SA */
			ieee80211_node_leave(ic, ni);
		} else {
			/* reject the (Re)Association Request temporarily */
			IEEE80211_SEND_MGMT(ic, ni, resp,
			    IEEE80211_STATUS_TRY_AGAIN_LATER);
			/* start SA Query procedure if not already engaged */
			if (!(ni->ni_flags & IEEE80211_NODE_SA_QUERY))
				ieee80211_sa_query_request(ic, ni);
			/* do not modify association state */
		}
		return;
	}

	if (!(capinfo & IEEE80211_CAPINFO_ESS)) {
		ic->ic_stats.is_rx_assoc_capmismatch++;
		status = IEEE80211_STATUS_CAPINFO;
		goto end;
	}
	rate = ieee80211_setup_rates(ic, ni, rates, xrates,
	    IEEE80211_F_DOSORT | IEEE80211_F_DOFRATE | IEEE80211_F_DONEGO |
	    IEEE80211_F_DODEL);
	if (rate & IEEE80211_RATE_BASIC) {
		ic->ic_stats.is_rx_assoc_norate++;
		status = IEEE80211_STATUS_BASIC_RATE;
		goto end;
	}

	ni->ni_rsnprotos = IEEE80211_PROTO_NONE;
	ni->ni_supported_rsnprotos = IEEE80211_PROTO_NONE;
	ni->ni_rsnakms = 0;
	ni->ni_supported_rsnakms = 0;
	ni->ni_rsnciphers = 0;
	ni->ni_rsngroupcipher = 0;
	ni->ni_rsngroupmgmtcipher = 0;
	ni->ni_rsncaps = 0;

	/*
	 * A station should never include both a WPA and an RSN IE
	 * in its (Re)Association Requests, but if it does, we only
	 * consider the IE of the highest version of the protocol
	 * that is allowed (ie RSN over WPA).
	 */
	if (rsnie != NULL) {
		status = ieee80211_parse_rsn(ic, rsnie, &rsn);
		if (status != 0)
			goto end;
		ni->ni_supported_rsnprotos = IEEE80211_PROTO_RSN;
		ni->ni_supported_rsnakms = rsn.rsn_akms;
		if ((ic->ic_flags & IEEE80211_F_RSNON) &&
		    (ic->ic_rsnprotos & IEEE80211_PROTO_RSN)) {
			ni->ni_rsnprotos = IEEE80211_PROTO_RSN;
			saveie = rsnie;
		}
	} else if (wpaie != NULL) {
		status = ieee80211_parse_wpa(ic, wpaie, &rsn);
		if (status != 0)
			goto end;
		ni->ni_supported_rsnprotos = IEEE80211_PROTO_WPA;
		ni->ni_supported_rsnakms = rsn.rsn_akms;
		if ((ic->ic_flags & IEEE80211_F_RSNON) &&
		    (ic->ic_rsnprotos & IEEE80211_PROTO_WPA)) {
			ni->ni_rsnprotos = IEEE80211_PROTO_WPA;
			saveie = wpaie;
		}
	}

	if (ic->ic_flags & IEEE80211_F_QOS) {
		if (wmeie != NULL)
			ni->ni_flags |= IEEE80211_NODE_QOS;
		else	/* for Reassociation */
			ni->ni_flags &= ~IEEE80211_NODE_QOS;
	}

	if (ic->ic_flags & IEEE80211_F_RSNON) {
		if (ni->ni_rsnprotos == IEEE80211_PROTO_NONE) {
			/*
			 * In an RSN, an AP shall not associate with STAs
			 * that fail to include the RSN IE in the
			 * (Re)Association Request.
			 */
			status = IEEE80211_STATUS_IE_INVALID;
			goto end;
		}
		/*
		 * The initiating STA's RSN IE shall include one authentication
		 * and pairwise cipher suite among those advertised by the
		 * targeted AP.  It shall also specify the group cipher suite
		 * specified by the targeted AP.
		 */
		if (rsn.rsn_nakms != 1 ||
		    !(rsn.rsn_akms & ic->ic_bss->ni_rsnakms)) {
			status = IEEE80211_STATUS_BAD_AKMP;
			ni->ni_rsnprotos = IEEE80211_PROTO_NONE;
			goto end;
		}
		if (rsn.rsn_nciphers != 1 ||
		    !(rsn.rsn_ciphers & ic->ic_bss->ni_rsnciphers)) {
			status = IEEE80211_STATUS_BAD_PAIRWISE_CIPHER;
			ni->ni_rsnprotos = IEEE80211_PROTO_NONE;
			goto end;
		}
		if (rsn.rsn_groupcipher != ic->ic_bss->ni_rsngroupcipher) {
			status = IEEE80211_STATUS_BAD_GROUP_CIPHER;
			ni->ni_rsnprotos = IEEE80211_PROTO_NONE;
			goto end;
		}

		if ((ic->ic_bss->ni_rsncaps & IEEE80211_RSNCAP_MFPR) &&
		    !(rsn.rsn_caps & IEEE80211_RSNCAP_MFPC)) {
			status = IEEE80211_STATUS_MFP_POLICY;
			ni->ni_rsnprotos = IEEE80211_PROTO_NONE;
			goto end;
		}
		if ((ic->ic_bss->ni_rsncaps & IEEE80211_RSNCAP_MFPC) &&
		    (rsn.rsn_caps & (IEEE80211_RSNCAP_MFPC |
		     IEEE80211_RSNCAP_MFPR)) == IEEE80211_RSNCAP_MFPR) {
			/* STA advertises an invalid setting */
			status = IEEE80211_STATUS_MFP_POLICY;
			ni->ni_rsnprotos = IEEE80211_PROTO_NONE;
			goto end;
		}
		/*
		 * A STA that has associated with Management Frame Protection
		 * enabled shall not use cipher suite pairwise selector WEP40,
		 * WEP104, TKIP, or "Use Group cipher suite".
		 */
		if ((rsn.rsn_caps & IEEE80211_RSNCAP_MFPC) &&
		    (rsn.rsn_ciphers != IEEE80211_CIPHER_CCMP ||
		     rsn.rsn_groupmgmtcipher !=
		     ic->ic_bss->ni_rsngroupmgmtcipher)) {
			status = IEEE80211_STATUS_MFP_POLICY;
			ni->ni_rsnprotos = IEEE80211_PROTO_NONE;
			goto end;
		}

		/*
		 * Disallow new associations using TKIP if countermeasures
		 * are active.
		 */
		if ((ic->ic_flags & IEEE80211_F_COUNTERM) &&
		    (rsn.rsn_ciphers == IEEE80211_CIPHER_TKIP ||
		     rsn.rsn_groupcipher == IEEE80211_CIPHER_TKIP)) {
			status = IEEE80211_STATUS_CIPHER_REJ_POLICY;
			ni->ni_rsnprotos = IEEE80211_PROTO_NONE;
			goto end;
		}

		/* everything looks fine, save IE and parameters */
		if (saveie == NULL ||
		    ieee80211_save_ie(saveie, &ni->ni_rsnie) != 0) {
			status = IEEE80211_STATUS_TOOMANY;
			ni->ni_rsnprotos = IEEE80211_PROTO_NONE;
			goto end;
		}
		ni->ni_rsnakms = rsn.rsn_akms;
		ni->ni_rsnciphers = rsn.rsn_ciphers;
		ni->ni_rsngroupcipher = ic->ic_bss->ni_rsngroupcipher;
		ni->ni_rsngroupmgmtcipher = ic->ic_bss->ni_rsngroupmgmtcipher;
		ni->ni_rsncaps = rsn.rsn_caps;

		if (ieee80211_is_8021x_akm(ni->ni_rsnakms)) {
			struct ieee80211_pmk *pmk = NULL;
			const u_int8_t *pmkid = rsn.rsn_pmkids;
			/*
			 * Check if we have a cached PMK entry matching one
			 * of the PMKIDs specified in the RSN IE.
			 */
			while (rsn.rsn_npmkids-- > 0) {
				pmk = ieee80211_pmksa_find(ic, ni, pmkid);
				if (pmk != NULL)
					break;
				pmkid += IEEE80211_PMKID_LEN;
			}
			if (pmk != NULL) {
				memcpy(ni->ni_pmk, pmk->pmk_key,
				    IEEE80211_PMK_LEN);
				memcpy(ni->ni_pmkid, pmk->pmk_pmkid,
				    IEEE80211_PMKID_LEN);
				ni->ni_flags |= IEEE80211_NODE_PMK;
			}
		}
	}

	ni->ni_rssi = rxi->rxi_rssi;
	ni->ni_rstamp = rxi->rxi_tstamp;
	ni->ni_intval = bintval;
	ni->ni_capinfo = capinfo;
	ni->ni_chan = ic->ic_bss->ni_chan;
	if (htcaps)
		ieee80211_setup_htcaps(ni, htcaps + 2, htcaps[1]);
	else
		ieee80211_clear_htcaps(ni);
	if (htcaps && vhtcaps && IEEE80211_IS_CHAN_5GHZ(ic->ic_bss->ni_chan))
		ieee80211_setup_vhtcaps(ni, vhtcaps + 2, vhtcaps[1]);
	else
		ieee80211_clear_vhtcaps(ni);
 end:
	if (status != 0) {
		IEEE80211_SEND_MGMT(ic, ni, resp, status);
		ieee80211_node_leave(ic, ni);
	} else
		ieee80211_node_join(ic, ni, resp);
}
#endif	/* IEEE80211_STA_ONLY */

/*-
 * (Re)Association response frame format:
 * [2]   Capability information
 * [2]   Status code
 * [2]   Association ID (AID)
 * [tlv] Supported rates
 * [tlv] Extended Supported Rates (802.11g)
 * [tlv] EDCA Parameter Set (802.11e)
 * [tlv] HT Capabilities (802.11n)
 * [tlv] HT Operation (802.11n)
 */
void
ieee80211_recv_assoc_resp(struct ieee80211com *ic, struct mbuf *m,
    struct ieee80211_node *ni, int reassoc)
{
	struct ifnet *ifp = &ic->ic_if;
	const struct ieee80211_frame *wh;
	const u_int8_t *frm, *efrm;
	const u_int8_t *rates, *xrates, *edcaie, *wmmie, *htcaps, *htop;
	const u_int8_t *vhtcaps, *vhtop;
	u_int16_t capinfo, status, associd;
	u_int8_t rate;

	if (ic->ic_opmode != IEEE80211_M_STA ||
	    ic->ic_state != IEEE80211_S_ASSOC) {
		ic->ic_stats.is_rx_mgtdiscard++;
		return;
	}

	/* make sure all mandatory fixed fields are present */
	if (m->m_len < sizeof(*wh) + 6) {
		DPRINTF(("frame too short\n"));
		return;
	}
	wh = mtod(m, struct ieee80211_frame *);
	frm = (const u_int8_t *)&wh[1];
	efrm = mtod(m, u_int8_t *) + m->m_len;

	capinfo = LE_READ_2(frm); frm += 2;
	status =  LE_READ_2(frm); frm += 2;
	if (status != IEEE80211_STATUS_SUCCESS) {
		if (ifp->if_flags & IFF_DEBUG)
			printf("%s: %sassociation failed (status %d)"
			    " for %s\n", ifp->if_xname,
			    reassoc ?  "re" : "",
			    status, ether_sprintf((u_int8_t *)wh->i_addr3));
		if (ni != ic->ic_bss)
			ni->ni_fails++;
		ic->ic_stats.is_rx_auth_fail++;
		return;
	}
	associd = LE_READ_2(frm); frm += 2;

	rates = xrates = edcaie = wmmie = htcaps = htop = NULL;
	vhtcaps = vhtop = NULL;
	while (frm + 2 <= efrm) {
		if (frm + 2 + frm[1] > efrm) {
			ic->ic_stats.is_rx_elem_toosmall++;
			break;
		}
		switch (frm[0]) {
		case IEEE80211_ELEMID_RATES:
			rates = frm;
			break;
		case IEEE80211_ELEMID_XRATES:
			xrates = frm;
			break;
		case IEEE80211_ELEMID_EDCAPARMS:
			edcaie = frm;
			break;
		case IEEE80211_ELEMID_HTCAPS:
			htcaps = frm;
			break;
		case IEEE80211_ELEMID_HTOP:
			htop = frm;
			break;
		case IEEE80211_ELEMID_VHTCAPS:
			vhtcaps = frm;
			break;
		case IEEE80211_ELEMID_VHTOP:
			vhtop = frm;
			break;
		case IEEE80211_ELEMID_VENDOR:
			if (frm[1] < 4) {
				ic->ic_stats.is_rx_elem_toosmall++;
				break;
			}
			if (memcmp(frm + 2, MICROSOFT_OUI, 3) == 0) {
				if (frm[1] >= 5 && frm[5] == 2 && frm[6] == 1)
					wmmie = frm;
			}
			break;
		}
		frm += 2 + frm[1];
	}
	/* supported rates element is mandatory */
	if (rates == NULL || rates[1] > IEEE80211_RATE_MAXSIZE) {
		DPRINTF(("invalid supported rates element\n"));
		return;
	}
	rate = ieee80211_setup_rates(ic, ni, rates, xrates,
	    IEEE80211_F_DOSORT | IEEE80211_F_DOFRATE | IEEE80211_F_DONEGO |
	    IEEE80211_F_DODEL);
	if (rate & IEEE80211_RATE_BASIC) {
		DPRINTF(("rate mismatch for %s\n",
		    ether_sprintf((u_int8_t *)wh->i_addr2)));
		ic->ic_stats.is_rx_assoc_norate++;
		return;
	}
	ni->ni_capinfo = capinfo;
	ni->ni_associd = associd;
	if (edcaie != NULL || wmmie != NULL) {
		/* force update of EDCA parameters */
		ic->ic_edca_updtcount = -1;

		if ((edcaie != NULL &&
		     ieee80211_parse_edca_params(ic, edcaie) == 0) ||
		    (wmmie != NULL &&
		     ieee80211_parse_wmm_params(ic, wmmie) == 0))
			ni->ni_flags |= IEEE80211_NODE_QOS;
		else	/* for Reassociation */
			ni->ni_flags &= ~IEEE80211_NODE_QOS;
	}
	if (htcaps)
		ieee80211_setup_htcaps(ni, htcaps + 2, htcaps[1]);
	if (htop)
		ieee80211_setup_htop(ni, htop + 2, htop[1], 0);
	ieee80211_ht_negotiate(ic, ni);

	if (htcaps && vhtcaps && IEEE80211_IS_CHAN_5GHZ(ni->ni_chan)) {
		ieee80211_setup_vhtcaps(ni, vhtcaps + 2, vhtcaps[1]);
		if (vhtop && !ieee80211_setup_vhtop(ni, vhtop + 2, vhtop[1], 1))
			vhtop = NULL; /* invalid VHTOP */
	}
	ieee80211_vht_negotiate(ic, ni);

	/* Hop into 11n/11ac modes after associating to a HT/VHT AP. */
	if (ni->ni_flags & IEEE80211_NODE_VHT)
		ieee80211_setmode(ic, IEEE80211_MODE_11AC);
	else if (ni->ni_flags & IEEE80211_NODE_HT)
		ieee80211_setmode(ic, IEEE80211_MODE_11N);
	else
		ieee80211_setmode(ic, ieee80211_node_abg_mode(ic, ni));
	/*
	 * Reset the erp state (mostly the slot time) now that
	 * our operating mode has been nailed down.
	 */
	ieee80211_reset_erp(ic);

	/*
	 * Configure state now that we are associated.
	 */
	if (ic->ic_curmode == IEEE80211_MODE_11A ||
	    (ni->ni_capinfo & IEEE80211_CAPINFO_SHORT_PREAMBLE))
		ic->ic_flags |= IEEE80211_F_SHPREAMBLE;
	else
		ic->ic_flags &= ~IEEE80211_F_SHPREAMBLE;

	ieee80211_set_shortslottime(ic,
	    ic->ic_curmode == IEEE80211_MODE_11A ||
	    (ni->ni_capinfo & IEEE80211_CAPINFO_SHORT_SLOTTIME));
	/*
	 * Honor ERP protection.
	 */
	if ((ic->ic_curmode == IEEE80211_MODE_11G ||
	    (ic->ic_curmode == IEEE80211_MODE_11N &&
	    IEEE80211_IS_CHAN_2GHZ(ni->ni_chan))) &&
	    (ni->ni_erp & IEEE80211_ERP_USE_PROTECTION))
		ic->ic_flags |= IEEE80211_F_USEPROT;
	else
		ic->ic_flags &= ~IEEE80211_F_USEPROT;
	/*
	 * If not an RSNA, mark the port as valid, otherwise wait for
	 * 802.1X authentication and 4-way handshake to complete..
	 */
	if (ic->ic_flags & IEEE80211_F_RSNON) {
		/* XXX ic->ic_mgt_timer = 5; */
		ni->ni_rsn_supp_state = RSNA_SUPP_PTKSTART;
	} else if (ic->ic_flags & IEEE80211_F_WEPON)
		ni->ni_flags |= IEEE80211_NODE_TXRXPROT;

	ieee80211_new_state(ic, IEEE80211_S_RUN,
	    IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
}

/*-
 * Deauthentication frame format:
 * [2] Reason code
 */
void
ieee80211_recv_deauth(struct ieee80211com *ic, struct mbuf *m,
    struct ieee80211_node *ni)
{
	const struct ieee80211_frame *wh;
	const u_int8_t *frm;
	u_int16_t reason;

	/* make sure all mandatory fixed fields are present */
	if (m->m_len < sizeof(*wh) + 2) {
		DPRINTF(("frame too short\n"));
		return;
	}
	wh = mtod(m, struct ieee80211_frame *);
	frm = (const u_int8_t *)&wh[1];

	reason = LE_READ_2(frm);

	ic->ic_stats.is_rx_deauth++;
	switch (ic->ic_opmode) {
	case IEEE80211_M_STA: {
		int bgscan = ((ic->ic_flags & IEEE80211_F_BGSCAN) &&
		    ic->ic_state == IEEE80211_S_RUN);
		int stay_auth = ((ic->ic_userflags & IEEE80211_F_STAYAUTH) &&
		    ic->ic_state >= IEEE80211_S_AUTH);
		if (!(bgscan || stay_auth))
			ieee80211_new_state(ic, IEEE80211_S_AUTH,
			    IEEE80211_FC0_SUBTYPE_DEAUTH);
		}
		break;
#ifndef IEEE80211_STA_ONLY
	case IEEE80211_M_HOSTAP:
		if (ni != ic->ic_bss) {
			int stay_auth =
			    ((ic->ic_userflags & IEEE80211_F_STAYAUTH) &&
			    (ni->ni_state == IEEE80211_STA_AUTH ||
			    ni->ni_state == IEEE80211_STA_ASSOC));
			if (ic->ic_if.if_flags & IFF_DEBUG)
				printf("%s: station %s deauthenticated "
				    "by peer (reason %d)\n",
				    ic->ic_if.if_xname,
				    ether_sprintf(ni->ni_macaddr),
				    reason);
			if (!stay_auth)
				ieee80211_node_leave(ic, ni);
		}
		break;
#endif
	default:
		break;
	}
}

/*-
 * Disassociation frame format:
 * [2] Reason code
 */
void
ieee80211_recv_disassoc(struct ieee80211com *ic, struct mbuf *m,
    struct ieee80211_node *ni)
{
	const struct ieee80211_frame *wh;
	const u_int8_t *frm;
	u_int16_t reason;

	/* make sure all mandatory fixed fields are present */
	if (m->m_len < sizeof(*wh) + 2) {
		DPRINTF(("frame too short\n"));
		return;
	}
	wh = mtod(m, struct ieee80211_frame *);
	frm = (const u_int8_t *)&wh[1];

	reason = LE_READ_2(frm);

	ic->ic_stats.is_rx_disassoc++;
	switch (ic->ic_opmode) {
	case IEEE80211_M_STA: {
		int bgscan = ((ic->ic_flags & IEEE80211_F_BGSCAN) &&
		    ic->ic_state == IEEE80211_S_RUN);
		if (!bgscan) /* ignore disassoc during bgscan */
			ieee80211_new_state(ic, IEEE80211_S_ASSOC,
			    IEEE80211_FC0_SUBTYPE_DISASSOC);
		}
		break;
#ifndef IEEE80211_STA_ONLY
	case IEEE80211_M_HOSTAP:
		if (ni != ic->ic_bss) {
			if (ic->ic_if.if_flags & IFF_DEBUG)
				printf("%s: station %s disassociated "
				    "by peer (reason %d)\n",
				    ic->ic_if.if_xname,
				    ether_sprintf(ni->ni_macaddr),
				    reason);
			ieee80211_node_leave(ic, ni);
		}
		break;
#endif
	default:
		break;
	}
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
void
ieee80211_recv_addba_req(struct ieee80211com *ic, struct mbuf *m,
    struct ieee80211_node *ni)
{
	const struct ieee80211_frame *wh;
	const u_int8_t *frm;
	struct ieee80211_rx_ba *ba;
	u_int16_t params, ssn, bufsz, timeout;
	u_int8_t token, tid;
	int err = 0;

	/* Ignore if we are not ready to receive data frames. */
	if (ic->ic_state != IEEE80211_S_RUN ||
	    ((ic->ic_flags & IEEE80211_F_RSNON) && !ni->ni_port_valid))
		return;

	if (!(ni->ni_flags & IEEE80211_NODE_HT)) {
		DPRINTF(("received ADDBA req from non-HT STA %s\n",
		    ether_sprintf(ni->ni_macaddr)));
		return;
	}
	if (m->m_len < sizeof(*wh) + 9) {
		DPRINTF(("frame too short\n"));
		return;
	}
	/* MLME-ADDBA.indication */
	wh = mtod(m, struct ieee80211_frame *);
	frm = (const u_int8_t *)&wh[1];

	token = frm[2];
	params = LE_READ_2(&frm[3]);
	tid = ((params & IEEE80211_ADDBA_TID_MASK) >>
	    IEEE80211_ADDBA_TID_SHIFT);
	bufsz = (params & IEEE80211_ADDBA_BUFSZ_MASK) >>
	    IEEE80211_ADDBA_BUFSZ_SHIFT;
	timeout = LE_READ_2(&frm[5]);
	ssn = LE_READ_2(&frm[7]) >> 4;

	ba = &ni->ni_rx_ba[tid];
	/* The driver is still processing an ADDBA request for this tid. */
	if (ba->ba_state == IEEE80211_BA_REQUESTED)
		return;
	/* If we are in the process of roaming between APs, ignore. */
	if ((ic->ic_flags & IEEE80211_F_BGSCAN) &&
	    (ic->ic_xflags & IEEE80211_F_TX_MGMT_ONLY))
		return;
	/* check if we already have a Block Ack agreement for this RA/TID */
	if (ba->ba_state == IEEE80211_BA_AGREED) {
		/* XXX should we update the timeout value? */
		/* reset Block Ack inactivity timer */
		if (ba->ba_timeout_val != 0)
			timeout_add_usec(&ba->ba_to, ba->ba_timeout_val);

		/* check if it's a Protected Block Ack agreement */
		if (!(ni->ni_flags & IEEE80211_NODE_MFP) ||
		    !(ni->ni_rsncaps & IEEE80211_RSNCAP_PBAC))
			return;	/* not a PBAC, ignore */

		/* PBAC: treat the ADDBA Request like a BlockAckReq */
		if (SEQ_LT(ba->ba_winstart, ssn)) {
			struct mbuf_list ml = MBUF_LIST_INITIALIZER();
			ieee80211_ba_move_window(ic, ni, tid, ssn, &ml);
			if_input(&ic->ic_if, &ml);
		}
		return;
	}

	/* if PBAC required but RA does not support it, refuse request */
	if ((ic->ic_flags & IEEE80211_F_PBAR) &&
	    (!(ni->ni_flags & IEEE80211_NODE_MFP) ||
	     !(ni->ni_rsncaps & IEEE80211_RSNCAP_PBAC)))
		goto refuse;
	/*
	 * If the TID for which the Block Ack agreement is requested is
	 * configured with a no-ACK policy, refuse the agreement.
	 */
	if (ic->ic_tid_noack & (1 << tid))
		goto refuse;

	/* check that we support the requested Block Ack Policy */
	if (!(ic->ic_htcaps & IEEE80211_HTCAP_DELAYEDBA) &&
	    !(params & IEEE80211_ADDBA_BA_POLICY))
		goto refuse;

	/* setup Block Ack agreement */
	ba->ba_state = IEEE80211_BA_REQUESTED;
	ba->ba_timeout_val = timeout * IEEE80211_DUR_TU;
	ba->ba_ni = ni;
	ba->ba_token = token;
	timeout_set(&ba->ba_to, ieee80211_rx_ba_timeout, ba);
	timeout_set(&ba->ba_gap_to, ieee80211_input_ba_gap_timeout, ba);
	ba->ba_gapwait = 0;
	ba->ba_winsize = bufsz;
	if (ba->ba_winsize == 0 || ba->ba_winsize > IEEE80211_BA_MAX_WINSZ)
		ba->ba_winsize = IEEE80211_BA_MAX_WINSZ;
	ba->ba_params = (params & IEEE80211_ADDBA_BA_POLICY);
	ba->ba_params |= ((ba->ba_winsize << IEEE80211_ADDBA_BUFSZ_SHIFT) |
	    (tid << IEEE80211_ADDBA_TID_SHIFT));
	ba->ba_params |= IEEE80211_ADDBA_AMSDU;
	ba->ba_winstart = ssn;
	ba->ba_winend = (ba->ba_winstart + ba->ba_winsize - 1) & 0xfff;
	/* allocate and setup our reordering buffer */
	ba->ba_buf = malloc(IEEE80211_BA_MAX_WINSZ * sizeof(*ba->ba_buf),
	    M_DEVBUF, M_NOWAIT | M_ZERO);
	if (ba->ba_buf == NULL)
		goto refuse;

	ba->ba_head = 0;

	/* notify drivers of this new Block Ack agreement */
	if (ic->ic_ampdu_rx_start != NULL)
		err = ic->ic_ampdu_rx_start(ic, ni, tid);
	if (err == EBUSY) {
		/* driver will accept or refuse agreement when done */
		return;
	} else if (err) {
		/* driver failed to setup, rollback */
		ieee80211_addba_req_refuse(ic, ni, tid);
	} else
		ieee80211_addba_req_accept(ic, ni, tid);
	return;

 refuse:
	/* MLME-ADDBA.response */
	IEEE80211_SEND_ACTION(ic, ni, IEEE80211_CATEG_BA,
	    IEEE80211_ACTION_ADDBA_RESP,
	    IEEE80211_STATUS_REFUSED << 16 | token << 8 | tid);
}

void
ieee80211_addba_req_accept(struct ieee80211com *ic, struct ieee80211_node *ni,
    uint8_t tid)
{
	struct ieee80211_rx_ba *ba = &ni->ni_rx_ba[tid];

	ba->ba_state = IEEE80211_BA_AGREED;
	ic->ic_stats.is_ht_rx_ba_agreements++;
	/* start Block Ack inactivity timer */
	if (ba->ba_timeout_val != 0)
		timeout_add_usec(&ba->ba_to, ba->ba_timeout_val);

	/* MLME-ADDBA.response */
	IEEE80211_SEND_ACTION(ic, ni, IEEE80211_CATEG_BA,
	    IEEE80211_ACTION_ADDBA_RESP,
	    IEEE80211_STATUS_SUCCESS << 16 | ba->ba_token << 8 | tid);
}

void
ieee80211_addba_req_refuse(struct ieee80211com *ic, struct ieee80211_node *ni,
    uint8_t tid)
{
	struct ieee80211_rx_ba *ba = &ni->ni_rx_ba[tid];

	free(ba->ba_buf, M_DEVBUF,
	    IEEE80211_BA_MAX_WINSZ * sizeof(*ba->ba_buf));
	ba->ba_buf = NULL;
	ba->ba_state = IEEE80211_BA_INIT;

	/* MLME-ADDBA.response */
	IEEE80211_SEND_ACTION(ic, ni, IEEE80211_CATEG_BA,
	    IEEE80211_ACTION_ADDBA_RESP,
	    IEEE80211_STATUS_REFUSED << 16 | ba->ba_token << 8 | tid);
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
void
ieee80211_recv_addba_resp(struct ieee80211com *ic, struct mbuf *m,
    struct ieee80211_node *ni)
{
	const struct ieee80211_frame *wh;
	const u_int8_t *frm;
	struct ieee80211_tx_ba *ba;
	u_int16_t status, params, bufsz, timeout;
	u_int8_t token, tid;
	int err = 0;

	if (m->m_len < sizeof(*wh) + 9) {
		DPRINTF(("frame too short\n"));
		return;
	}
	wh = mtod(m, struct ieee80211_frame *);
	frm = (const u_int8_t *)&wh[1];

	token = frm[2];
	status = LE_READ_2(&frm[3]);
	params = LE_READ_2(&frm[5]);
	tid = (params >> 2) & 0xf;
	bufsz = (params >> 6) & 0x3ff;
	timeout = LE_READ_2(&frm[7]);

	DPRINTF(("received ADDBA resp from %s, TID %d, status %d\n",
	    ether_sprintf(ni->ni_macaddr), tid, status));

	/*
	 * Ignore if no ADDBA request has been sent for this RA/TID or
	 * if we already have a Block Ack agreement.
	 */
	ba = &ni->ni_tx_ba[tid];
	if (ba->ba_state != IEEE80211_BA_REQUESTED) {
		DPRINTF(("no matching ADDBA req found\n"));
		return;
	}
	if (token != ba->ba_token) {
		DPRINTF(("ignoring ADDBA resp from %s: token %x!=%x\n",
		    ether_sprintf(ni->ni_macaddr), token, ba->ba_token));
		return;
	}
	/* we got an ADDBA Response matching our request, stop timeout */
	timeout_del(&ba->ba_to);

	if (status != IEEE80211_STATUS_SUCCESS) {
		if (ni->ni_addba_req_intval[tid] <
		    IEEE80211_ADDBA_REQ_INTVAL_MAX)
			ni->ni_addba_req_intval[tid]++;

		ieee80211_addba_resp_refuse(ic, ni, tid, status);

		/*
		 * In case the peer believes there is an existing
		 * block ack agreement with us, try to delete it.
		 */
		IEEE80211_SEND_ACTION(ic, ni, IEEE80211_CATEG_BA,
		    IEEE80211_ACTION_DELBA,
		    IEEE80211_REASON_SETUP_REQUIRED << 16 | 1 << 8 | tid);
		return;
	}

	/* notify drivers of this new Block Ack agreement */
	if (ic->ic_ampdu_tx_start != NULL)
		err = ic->ic_ampdu_tx_start(ic, ni, tid);

	if (err == EBUSY) {
		/* driver will accept or refuse agreement when done */
		return;
	} else if (err) {
		/* driver failed to setup, rollback */
		ieee80211_addba_resp_refuse(ic, ni, tid,
		    IEEE80211_STATUS_UNSPECIFIED);
	} else
		ieee80211_addba_resp_accept(ic, ni, tid);
}

void
ieee80211_addba_resp_accept(struct ieee80211com *ic,
    struct ieee80211_node *ni, uint8_t tid)
{
	struct ieee80211_tx_ba *ba = &ni->ni_tx_ba[tid];

	/* MLME-ADDBA.confirm(Success) */
	ba->ba_state = IEEE80211_BA_AGREED;
	ic->ic_stats.is_ht_tx_ba_agreements++;

	/* Reset ADDBA request interval. */
	ni->ni_addba_req_intval[tid] = 1;

	ni->ni_qos_txseqs[tid] = ba->ba_winstart;

	/* start Block Ack inactivity timeout */
	if (ba->ba_timeout_val != 0)
		timeout_add_usec(&ba->ba_to, ba->ba_timeout_val);
}

void
ieee80211_addba_resp_refuse(struct ieee80211com *ic,
    struct ieee80211_node *ni, uint8_t tid, uint16_t status)
{
	struct ieee80211_tx_ba *ba = &ni->ni_tx_ba[tid];

	/* MLME-ADDBA.confirm(Failure) */
	ba->ba_state = IEEE80211_BA_INIT;
}

/*-
 * DELBA frame format:
 * [1] Category
 * [1] Action
 * [2] DELBA Parameter Set
 * [2] Reason Code
 */
void
ieee80211_recv_delba(struct ieee80211com *ic, struct mbuf *m,
    struct ieee80211_node *ni)
{
	const struct ieee80211_frame *wh;
	const u_int8_t *frm;
	u_int16_t params, reason;
	u_int8_t tid;
	int i;

	if (m->m_len < sizeof(*wh) + 6) {
		DPRINTF(("frame too short\n"));
		return;
	}
	wh = mtod(m, struct ieee80211_frame *);
	frm = (const u_int8_t *)&wh[1];

	params = LE_READ_2(&frm[2]);
	reason = LE_READ_2(&frm[4]);
	tid = params >> 12;

	DPRINTF(("received DELBA from %s, TID %d, reason %d\n",
	    ether_sprintf(ni->ni_macaddr), tid, reason));

	if (params & IEEE80211_DELBA_INITIATOR) {
		/* MLME-DELBA.indication(Originator) */
		struct ieee80211_rx_ba *ba = &ni->ni_rx_ba[tid];

		if (ba->ba_state != IEEE80211_BA_AGREED) {
			DPRINTF(("no matching Block Ack agreement\n"));
			return;
		}
		/* notify drivers of the end of the Block Ack agreement */
		if (ic->ic_ampdu_rx_stop != NULL)
			ic->ic_ampdu_rx_stop(ic, ni, tid);

		ba->ba_state = IEEE80211_BA_INIT;
		/* stop Block Ack inactivity timer */
		timeout_del(&ba->ba_to);
		timeout_del(&ba->ba_gap_to);
		ba->ba_gapwait = 0;

		if (ba->ba_buf != NULL) {
			/* free all MSDUs stored in reordering buffer */
			for (i = 0; i < IEEE80211_BA_MAX_WINSZ; i++)
				m_freem(ba->ba_buf[i].m);
			/* free reordering buffer */
			free(ba->ba_buf, M_DEVBUF,
			    IEEE80211_BA_MAX_WINSZ * sizeof(*ba->ba_buf));
			ba->ba_buf = NULL;
		}
	} else {
		/* MLME-DELBA.indication(Recipient) */
		struct ieee80211_tx_ba *ba = &ni->ni_tx_ba[tid];

		if (ba->ba_state != IEEE80211_BA_AGREED) {
			DPRINTF(("no matching Block Ack agreement\n"));
			return;
		}
		/* notify drivers of the end of the Block Ack agreement */
		if (ic->ic_ampdu_tx_stop != NULL)
			ic->ic_ampdu_tx_stop(ic, ni, tid);

		ba->ba_state = IEEE80211_BA_INIT;
		/* stop Block Ack inactivity timer */
		timeout_del(&ba->ba_to);
	}
}

/*-
 * SA Query Request frame format:
 * [1] Category
 * [1] Action
 * [2] Transaction Identifier
 */
void
ieee80211_recv_sa_query_req(struct ieee80211com *ic, struct mbuf *m,
    struct ieee80211_node *ni)
{
	const struct ieee80211_frame *wh;
	const u_int8_t *frm;

	if (ic->ic_opmode != IEEE80211_M_STA ||
	    !(ni->ni_flags & IEEE80211_NODE_MFP)) {
		DPRINTF(("unexpected SA Query req from %s\n",
		    ether_sprintf(ni->ni_macaddr)));
		return;
	}
	if (m->m_len < sizeof(*wh) + 4) {
		DPRINTF(("frame too short\n"));
		return;
	}
	wh = mtod(m, struct ieee80211_frame *);
	frm = (const u_int8_t *)&wh[1];

	/* MLME-SAQuery.indication */

	/* save Transaction Identifier for SA Query Response */
	ni->ni_sa_query_trid = LE_READ_2(&frm[2]);

	/* MLME-SAQuery.response */
	IEEE80211_SEND_ACTION(ic, ni, IEEE80211_CATEG_SA_QUERY,
	    IEEE80211_ACTION_SA_QUERY_RESP, 0);
}

#ifndef IEEE80211_STA_ONLY
/*-
 * SA Query Response frame format:
 * [1] Category
 * [1] Action
 * [2] Transaction Identifier
 */
void
ieee80211_recv_sa_query_resp(struct ieee80211com *ic, struct mbuf *m,
    struct ieee80211_node *ni)
{
	const struct ieee80211_frame *wh;
	const u_int8_t *frm;

	/* ignore if we're not engaged in an SA Query with that STA */
	if (!(ni->ni_flags & IEEE80211_NODE_SA_QUERY)) {
		DPRINTF(("unexpected SA Query resp from %s\n",
		    ether_sprintf(ni->ni_macaddr)));
		return;
	}
	if (m->m_len < sizeof(*wh) + 4) {
		DPRINTF(("frame too short\n"));
		return;
	}
	wh = mtod(m, struct ieee80211_frame *);
	frm = (const u_int8_t *)&wh[1];

	/* check that Transaction Identifier matches */
	if (ni->ni_sa_query_trid != LE_READ_2(&frm[2])) {
		DPRINTF(("transaction identifier does not match\n"));
		return;
	}
	/* MLME-SAQuery.confirm */
	timeout_del(&ni->ni_sa_query_to);
	ni->ni_flags &= ~IEEE80211_NODE_SA_QUERY;
}
#endif

/*-
 * Action frame format:
 * [1] Category
 * [1] Action
 */
void
ieee80211_recv_action(struct ieee80211com *ic, struct mbuf *m,
    struct ieee80211_node *ni)
{
	const struct ieee80211_frame *wh;
	const u_int8_t *frm;

	if (m->m_len < sizeof(*wh) + 2) {
		DPRINTF(("frame too short\n"));
		return;
	}
	wh = mtod(m, struct ieee80211_frame *);
	frm = (const u_int8_t *)&wh[1];

	switch (frm[0]) {
	case IEEE80211_CATEG_BA:
		switch (frm[1]) {
		case IEEE80211_ACTION_ADDBA_REQ:
			ieee80211_recv_addba_req(ic, m, ni);
			break;
		case IEEE80211_ACTION_ADDBA_RESP:
			ieee80211_recv_addba_resp(ic, m, ni);
			break;
		case IEEE80211_ACTION_DELBA:
			ieee80211_recv_delba(ic, m, ni);
			break;
		}
		break;
	case IEEE80211_CATEG_SA_QUERY:
		switch (frm[1]) {
		case IEEE80211_ACTION_SA_QUERY_REQ:
			ieee80211_recv_sa_query_req(ic, m, ni);
			break;
#ifndef IEEE80211_STA_ONLY
		case IEEE80211_ACTION_SA_QUERY_RESP:
			ieee80211_recv_sa_query_resp(ic, m, ni);
			break;
#endif
		}
		break;
	default:
		DPRINTF(("action frame category %d not handled\n", frm[0]));
		break;
	}
}

void
ieee80211_recv_mgmt(struct ieee80211com *ic, struct mbuf *m,
    struct ieee80211_node *ni, struct ieee80211_rxinfo *rxi, int subtype)
{
	switch (subtype) {
	case IEEE80211_FC0_SUBTYPE_BEACON:
		ieee80211_recv_probe_resp(ic, m, ni, rxi, 0);
		break;
	case IEEE80211_FC0_SUBTYPE_PROBE_RESP:
		ieee80211_recv_probe_resp(ic, m, ni, rxi, 1);
		break;
#ifndef IEEE80211_STA_ONLY
	case IEEE80211_FC0_SUBTYPE_PROBE_REQ:
		ieee80211_recv_probe_req(ic, m, ni, rxi);
		break;
#endif
	case IEEE80211_FC0_SUBTYPE_AUTH:
		ieee80211_recv_auth(ic, m, ni, rxi);
		break;
#ifndef IEEE80211_STA_ONLY
	case IEEE80211_FC0_SUBTYPE_ASSOC_REQ:
		ieee80211_recv_assoc_req(ic, m, ni, rxi, 0);
		break;
	case IEEE80211_FC0_SUBTYPE_REASSOC_REQ:
		ieee80211_recv_assoc_req(ic, m, ni, rxi, 1);
		break;
#endif
	case IEEE80211_FC0_SUBTYPE_ASSOC_RESP:
		ieee80211_recv_assoc_resp(ic, m, ni, 0);
		break;
	case IEEE80211_FC0_SUBTYPE_REASSOC_RESP:
		ieee80211_recv_assoc_resp(ic, m, ni, 1);
		break;
	case IEEE80211_FC0_SUBTYPE_DEAUTH:
		ieee80211_recv_deauth(ic, m, ni);
		break;
	case IEEE80211_FC0_SUBTYPE_DISASSOC:
		ieee80211_recv_disassoc(ic, m, ni);
		break;
	case IEEE80211_FC0_SUBTYPE_ACTION:
		ieee80211_recv_action(ic, m, ni);
		break;
	default:
		DPRINTF(("mgmt frame with subtype 0x%x not handled\n",
		    subtype));
		ic->ic_stats.is_rx_badsubtype++;
		break;
	}
}

#ifndef IEEE80211_STA_ONLY
/*
 * Process an incoming PS-Poll control frame (see 11.2).
 */
void
ieee80211_recv_pspoll(struct ieee80211com *ic, struct mbuf *m,
    struct ieee80211_node *ni)
{
	struct ifnet *ifp = &ic->ic_if;
	struct ieee80211_frame_pspoll *psp;
	struct ieee80211_frame *wh;
	u_int16_t aid;

	if (ic->ic_opmode != IEEE80211_M_HOSTAP ||
	    !(ic->ic_caps & IEEE80211_C_APPMGT) ||
	    ni->ni_state != IEEE80211_STA_ASSOC)
		return;

	if (m->m_len < sizeof(*psp)) {
		DPRINTF(("frame too short, len %u\n", m->m_len));
		ic->ic_stats.is_rx_tooshort++;
		return;
	}
	psp = mtod(m, struct ieee80211_frame_pspoll *);
	if (!IEEE80211_ADDR_EQ(psp->i_bssid, ic->ic_bss->ni_bssid)) {
		DPRINTF(("discard pspoll frame to BSS %s\n",
		    ether_sprintf(psp->i_bssid)));
		ic->ic_stats.is_rx_wrongbss++;
		return;
	}
	aid = letoh16(*(u_int16_t *)psp->i_aid);
	if (aid != ni->ni_associd) {
		DPRINTF(("invalid pspoll aid %x from %s\n", aid,
		    ether_sprintf(psp->i_ta)));
		return;
	}

	/* take the first queued frame and put it out.. */
	m = mq_dequeue(&ni->ni_savedq);
	if (m == NULL)
		return;
	if (mq_empty(&ni->ni_savedq)) {
		/* last queued frame, turn off the TIM bit */
		(*ic->ic_set_tim)(ic, ni->ni_associd, 0);
	} else {
		/* more queued frames, set the more data bit */
		wh = mtod(m, struct ieee80211_frame *);
		wh->i_fc[1] |= IEEE80211_FC1_MORE_DATA;
	}
	mq_enqueue(&ic->ic_pwrsaveq, m);
	if_start(ifp);
}
#endif	/* IEEE80211_STA_ONLY */

/*
 * Process an incoming BlockAckReq control frame (see 7.2.1.7).
 */
void
ieee80211_recv_bar(struct ieee80211com *ic, struct mbuf *m,
    struct ieee80211_node *ni)
{
	const struct ieee80211_frame_min *wh;
	const u_int8_t *frm;
	u_int16_t ctl, ssn;
	u_int8_t tid, ntids;

	if (!(ni->ni_flags & IEEE80211_NODE_HT)) {
		DPRINTF(("received BlockAckReq from non-HT STA %s\n",
		    ether_sprintf(ni->ni_macaddr)));
		return;
	}
	if (m->m_len < sizeof(*wh) + 4) {
		DPRINTF(("frame too short\n"));
		return;
	}
	wh = mtod(m, struct ieee80211_frame_min *);
	frm = (const u_int8_t *)&wh[1];

	/* read BlockAckReq Control field */
	ctl = LE_READ_2(&frm[0]);
	tid = ctl >> 12;

	/* determine BlockAckReq frame variant */
	if (ctl & IEEE80211_BA_MULTI_TID) {
		/* Multi-TID BlockAckReq variant (PSMP only) */
		ntids = tid + 1;

		if (m->m_len < sizeof(*wh) + 2 + 4 * ntids) {
			DPRINTF(("MTBAR frame too short\n"));
			return;
		}
		frm += 2;	/* skip BlockAckReq Control field */
		while (ntids-- > 0) {
			/* read MTBAR Information field */
			tid = LE_READ_2(&frm[0]) >> 12;
			ssn = LE_READ_2(&frm[2]) >> 4;
			ieee80211_bar_tid(ic, ni, tid, ssn);
			frm += 4;
		}
	} else {
		/* Basic or Compressed BlockAckReq variants */
		ssn = LE_READ_2(&frm[2]) >> 4;
		ieee80211_bar_tid(ic, ni, tid, ssn);
	}
}

/*
 * Process a BlockAckReq for a specific TID (see 9.10.7.6.3).
 * This is the common back-end for all BlockAckReq frame variants.
 */
void
ieee80211_bar_tid(struct ieee80211com *ic, struct ieee80211_node *ni,
    u_int8_t tid, u_int16_t ssn)
{
	struct ieee80211_rx_ba *ba = &ni->ni_rx_ba[tid];

	/* check if we have a Block Ack agreement for RA/TID */
	if (ba->ba_state != IEEE80211_BA_AGREED) {
		/* XXX not sure in PBAC case */
		/* send a DELBA with reason code UNKNOWN-BA */
		IEEE80211_SEND_ACTION(ic, ni, IEEE80211_CATEG_BA,
		    IEEE80211_ACTION_DELBA,
		    IEEE80211_REASON_SETUP_REQUIRED << 16 | tid);
		return;
	}
	/* check if it is a Protected Block Ack agreement */
	if ((ni->ni_flags & IEEE80211_NODE_MFP) &&
	    (ni->ni_rsncaps & IEEE80211_RSNCAP_PBAC)) {
		/* ADDBA Requests must be used in PBAC case */
		if (SEQ_LT(ssn, ba->ba_winstart) ||
		    SEQ_LT(ba->ba_winend, ssn))
			ic->ic_stats.is_pbac_errs++;
		return;	/* PBAC, do not move window */
	}
	/* reset Block Ack inactivity timer */
	if (ba->ba_timeout_val != 0)
		timeout_add_usec(&ba->ba_to, ba->ba_timeout_val);

	if (SEQ_LT(ba->ba_winstart, ssn)) {
		struct mbuf_list ml = MBUF_LIST_INITIALIZER();
		ieee80211_ba_move_window(ic, ni, tid, ssn, &ml);
		if_input(&ic->ic_if, &ml);
	}
}
