/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
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

#ifdef	IEEE80211_SUPPORT_SUPERG

#include <sys/param.h>
#include <sys/systm.h> 
#include <sys/mbuf.h>   
#include <sys/kernel.h>
#include <sys/endian.h>

#include <sys/socket.h>
 
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_llc.h>
#include <net/if_media.h>
#include <net/bpf.h>
#include <net/ethernet.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_input.h>
#include <net80211/ieee80211_phy.h>
#include <net80211/ieee80211_superg.h>

/*
 * Atheros fast-frame encapsulation format.
 * FF max payload:
 * 802.2 + FFHDR + HPAD + 802.3 + 802.2 + 1500 + SPAD + 802.3 + 802.2 + 1500:
 *   8   +   4   +  4   +   14  +   8   + 1500 +  6   +   14  +   8   + 1500
 * = 3066
 */
/* fast frame header is 32-bits */
#define	ATH_FF_PROTO	0x0000003f	/* protocol */
#define	ATH_FF_PROTO_S	0
#define	ATH_FF_FTYPE	0x000000c0	/* frame type */
#define	ATH_FF_FTYPE_S	6
#define	ATH_FF_HLEN32	0x00000300	/* optional hdr length */
#define	ATH_FF_HLEN32_S	8
#define	ATH_FF_SEQNUM	0x001ffc00	/* sequence number */
#define	ATH_FF_SEQNUM_S	10
#define	ATH_FF_OFFSET	0xffe00000	/* offset to 2nd payload */
#define	ATH_FF_OFFSET_S	21

#define	ATH_FF_MAX_HDR_PAD	4
#define	ATH_FF_MAX_SEP_PAD	6
#define	ATH_FF_MAX_HDR		30

#define	ATH_FF_PROTO_L2TUNNEL	0	/* L2 tunnel protocol */
#define	ATH_FF_ETH_TYPE		0x88bd	/* Ether type for encapsulated frames */
#define	ATH_FF_SNAP_ORGCODE_0	0x00
#define	ATH_FF_SNAP_ORGCODE_1	0x03
#define	ATH_FF_SNAP_ORGCODE_2	0x7f

#define	ATH_FF_TXQMIN	2		/* min txq depth for staging */
#define	ATH_FF_TXQMAX	50		/* maximum # of queued frames allowed */
#define	ATH_FF_STAGEMAX	5		/* max waiting period for staged frame*/

#define	ETHER_HEADER_COPY(dst, src) \
	memcpy(dst, src, sizeof(struct ether_header))

static	int ieee80211_ffppsmin = 2;	/* pps threshold for ff aggregation */
SYSCTL_INT(_net_wlan, OID_AUTO, ffppsmin, CTLFLAG_RW,
	&ieee80211_ffppsmin, 0, "min packet rate before fast-frame staging");
static	int ieee80211_ffagemax = -1;	/* max time frames held on stage q */
SYSCTL_PROC(_net_wlan, OID_AUTO, ffagemax, CTLTYPE_INT | CTLFLAG_RW,
	&ieee80211_ffagemax, 0, ieee80211_sysctl_msecs_ticks, "I",
	"max hold time for fast-frame staging (ms)");

static void
ff_age_all(void *arg, int npending)
{
	struct ieee80211com *ic = arg;

	/* XXX cache timer value somewhere (racy) */
	ieee80211_ff_age_all(ic, ieee80211_ffagemax + 1);
}

void
ieee80211_superg_attach(struct ieee80211com *ic)
{
	struct ieee80211_superg *sg;

	IEEE80211_FF_LOCK_INIT(ic, ic->ic_name);

	sg = (struct ieee80211_superg *) IEEE80211_MALLOC(
	     sizeof(struct ieee80211_superg), M_80211_VAP,
	     IEEE80211_M_NOWAIT | IEEE80211_M_ZERO);
	if (sg == NULL) {
		printf("%s: cannot allocate SuperG state block\n",
		    __func__);
		return;
	}
	TIMEOUT_TASK_INIT(ic->ic_tq, &sg->ff_qtimer, 0, ff_age_all, ic);
	ic->ic_superg = sg;

	/*
	 * Default to not being so aggressive for FF/AMSDU
	 * aging, otherwise we may hold a frame around
	 * for way too long before we expire it out.
	 */
	ieee80211_ffagemax = msecs_to_ticks(2);
}

void
ieee80211_superg_detach(struct ieee80211com *ic)
{

	if (ic->ic_superg != NULL) {
		struct timeout_task *qtask = &ic->ic_superg->ff_qtimer;

		while (taskqueue_cancel_timeout(ic->ic_tq, qtask, NULL) != 0)
			taskqueue_drain_timeout(ic->ic_tq, qtask);
		IEEE80211_FREE(ic->ic_superg, M_80211_VAP);
		ic->ic_superg = NULL;
	}
	IEEE80211_FF_LOCK_DESTROY(ic);
}

void
ieee80211_superg_vattach(struct ieee80211vap *vap)
{
	struct ieee80211com *ic = vap->iv_ic;

	if (ic->ic_superg == NULL)	/* NB: can't do fast-frames w/o state */
		vap->iv_caps &= ~IEEE80211_C_FF;
	if (vap->iv_caps & IEEE80211_C_FF)
		vap->iv_flags |= IEEE80211_F_FF;
	/* NB: we only implement sta mode */
	if (vap->iv_opmode == IEEE80211_M_STA &&
	    (vap->iv_caps & IEEE80211_C_TURBOP))
		vap->iv_flags |= IEEE80211_F_TURBOP;
}

void
ieee80211_superg_vdetach(struct ieee80211vap *vap)
{
}

#define	ATH_OUI_BYTES		0x00, 0x03, 0x7f
/*
 * Add a WME information element to a frame.
 */
uint8_t *
ieee80211_add_ath(uint8_t *frm, uint8_t caps, ieee80211_keyix defkeyix)
{
	static const struct ieee80211_ath_ie info = {
		.ath_id		= IEEE80211_ELEMID_VENDOR,
		.ath_len	= sizeof(struct ieee80211_ath_ie) - 2,
		.ath_oui	= { ATH_OUI_BYTES },
		.ath_oui_type	= ATH_OUI_TYPE,
		.ath_oui_subtype= ATH_OUI_SUBTYPE,
		.ath_version	= ATH_OUI_VERSION,
	};
	struct ieee80211_ath_ie *ath = (struct ieee80211_ath_ie *) frm;

	memcpy(frm, &info, sizeof(info));
	ath->ath_capability = caps;
	if (defkeyix != IEEE80211_KEYIX_NONE) {
		ath->ath_defkeyix[0] = (defkeyix & 0xff);
		ath->ath_defkeyix[1] = ((defkeyix >> 8) & 0xff);
	} else {
		ath->ath_defkeyix[0] = 0xff;
		ath->ath_defkeyix[1] = 0x7f;
	}
	return frm + sizeof(info); 
}
#undef ATH_OUI_BYTES

uint8_t *
ieee80211_add_athcaps(uint8_t *frm, const struct ieee80211_node *bss)
{
	const struct ieee80211vap *vap = bss->ni_vap;

	return ieee80211_add_ath(frm,
	    vap->iv_flags & IEEE80211_F_ATHEROS,
	    ((vap->iv_flags & IEEE80211_F_WPA) == 0 &&
	    bss->ni_authmode != IEEE80211_AUTH_8021X) ?
	    vap->iv_def_txkey : IEEE80211_KEYIX_NONE);
}

void
ieee80211_parse_ath(struct ieee80211_node *ni, uint8_t *ie)
{
	const struct ieee80211_ath_ie *ath =
		(const struct ieee80211_ath_ie *) ie;

	ni->ni_ath_flags = ath->ath_capability;
	ni->ni_ath_defkeyix = le16dec(&ath->ath_defkeyix);
}

int
ieee80211_parse_athparams(struct ieee80211_node *ni, uint8_t *frm,
	const struct ieee80211_frame *wh)
{
	struct ieee80211vap *vap = ni->ni_vap;
	const struct ieee80211_ath_ie *ath;
	u_int len = frm[1];
	int capschanged;
	uint16_t defkeyix;

	if (len < sizeof(struct ieee80211_ath_ie)-2) {
		IEEE80211_DISCARD_IE(vap,
		    IEEE80211_MSG_ELEMID | IEEE80211_MSG_SUPERG,
		    wh, "Atheros", "too short, len %u", len);
		return -1;
	}
	ath = (const struct ieee80211_ath_ie *)frm;
	capschanged = (ni->ni_ath_flags != ath->ath_capability);
	defkeyix = le16dec(ath->ath_defkeyix);
	if (capschanged || defkeyix != ni->ni_ath_defkeyix) {
		ni->ni_ath_flags = ath->ath_capability;
		ni->ni_ath_defkeyix = defkeyix;
		IEEE80211_NOTE(vap, IEEE80211_MSG_SUPERG, ni,
		    "ath ie change: new caps 0x%x defkeyix 0x%x",
		    ni->ni_ath_flags, ni->ni_ath_defkeyix);
	}
	if (IEEE80211_ATH_CAP(vap, ni, ATHEROS_CAP_TURBO_PRIME)) {
		uint16_t curflags, newflags;

		/*
		 * Check for turbo mode switch.  Calculate flags
		 * for the new mode and effect the switch.
		 */
		newflags = curflags = vap->iv_ic->ic_bsschan->ic_flags;
		/* NB: BOOST is not in ic_flags, so get it from the ie */
		if (ath->ath_capability & ATHEROS_CAP_BOOST) 
			newflags |= IEEE80211_CHAN_TURBO;
		else
			newflags &= ~IEEE80211_CHAN_TURBO;
		if (newflags != curflags)
			ieee80211_dturbo_switch(vap, newflags);
	}
	return capschanged;
}

/*
 * Decap the encapsulated frame pair and dispatch the first
 * for delivery.  The second frame is returned for delivery
 * via the normal path.
 */
struct mbuf *
ieee80211_ff_decap(struct ieee80211_node *ni, struct mbuf *m)
{
#define	FF_LLC_SIZE	(sizeof(struct ether_header) + sizeof(struct llc))
#define	MS(x,f)	(((x) & f) >> f##_S)
	struct ieee80211vap *vap = ni->ni_vap;
	struct llc *llc;
	uint32_t ath;
	struct mbuf *n;
	int framelen;

	/* NB: we assume caller does this check for us */
	KASSERT(IEEE80211_ATH_CAP(vap, ni, IEEE80211_NODE_FF),
	    ("ff not negotiated"));
	/*
	 * Check for fast-frame tunnel encapsulation.
	 */
	if (m->m_pkthdr.len < 3*FF_LLC_SIZE)
		return m;
	if (m->m_len < FF_LLC_SIZE &&
	    (m = m_pullup(m, FF_LLC_SIZE)) == NULL) {
		IEEE80211_DISCARD_MAC(vap, IEEE80211_MSG_ANY,
		    ni->ni_macaddr, "fast-frame",
		    "%s", "m_pullup(llc) failed");
		vap->iv_stats.is_rx_tooshort++;
		return NULL;
	}
	llc = (struct llc *)(mtod(m, uint8_t *) +
	    sizeof(struct ether_header));
	if (llc->llc_snap.ether_type != htons(ATH_FF_ETH_TYPE))
		return m;
	m_adj(m, FF_LLC_SIZE);
	m_copydata(m, 0, sizeof(uint32_t), (caddr_t) &ath);
	if (MS(ath, ATH_FF_PROTO) != ATH_FF_PROTO_L2TUNNEL) {
		IEEE80211_DISCARD_MAC(vap, IEEE80211_MSG_ANY,
		    ni->ni_macaddr, "fast-frame",
		    "unsupport tunnel protocol, header 0x%x", ath);
		vap->iv_stats.is_ff_badhdr++;
		m_freem(m);
		return NULL;
	}
	/* NB: skip header and alignment padding */
	m_adj(m, roundup(sizeof(uint32_t) - 2, 4) + 2);

	vap->iv_stats.is_ff_decap++;

	/*
	 * Decap the first frame, bust it apart from the
	 * second and deliver; then decap the second frame
	 * and return it to the caller for normal delivery.
	 */
	m = ieee80211_decap1(m, &framelen);
	if (m == NULL) {
		IEEE80211_DISCARD_MAC(vap, IEEE80211_MSG_ANY,
		    ni->ni_macaddr, "fast-frame", "%s", "first decap failed");
		vap->iv_stats.is_ff_tooshort++;
		return NULL;
	}
	n = m_split(m, framelen, M_NOWAIT);
	if (n == NULL) {
		IEEE80211_DISCARD_MAC(vap, IEEE80211_MSG_ANY,
		    ni->ni_macaddr, "fast-frame",
		    "%s", "unable to split encapsulated frames");
		vap->iv_stats.is_ff_split++;
		m_freem(m);			/* NB: must reclaim */
		return NULL;
	}
	/* XXX not right for WDS */
	vap->iv_deliver_data(vap, ni, m);	/* 1st of pair */

	/*
	 * Decap second frame.
	 */
	m_adj(n, roundup2(framelen, 4) - framelen);	/* padding */
	n = ieee80211_decap1(n, &framelen);
	if (n == NULL) {
		IEEE80211_DISCARD_MAC(vap, IEEE80211_MSG_ANY,
		    ni->ni_macaddr, "fast-frame", "%s", "second decap failed");
		vap->iv_stats.is_ff_tooshort++;
	}
	/* XXX verify framelen against mbuf contents */
	return n;				/* 2nd delivered by caller */
#undef MS
#undef FF_LLC_SIZE
}

/*
 * Fast frame encapsulation.  There must be two packets
 * chained with m_nextpkt.  We do header adjustment for
 * each, add the tunnel encapsulation, and then concatenate
 * the mbuf chains to form a single frame for transmission.
 */
struct mbuf *
ieee80211_ff_encap(struct ieee80211vap *vap, struct mbuf *m1, int hdrspace,
	struct ieee80211_key *key)
{
	struct mbuf *m2;
	struct ether_header eh1, eh2;
	struct llc *llc;
	struct mbuf *m;
	int pad;

	m2 = m1->m_nextpkt;
	if (m2 == NULL) {
		IEEE80211_DPRINTF(vap, IEEE80211_MSG_SUPERG,
		    "%s: only one frame\n", __func__);
		goto bad;
	}
	m1->m_nextpkt = NULL;

	/*
	 * Adjust to include 802.11 header requirement.
	 */
	KASSERT(m1->m_len >= sizeof(eh1), ("no ethernet header!"));
	ETHER_HEADER_COPY(&eh1, mtod(m1, caddr_t));
	m1 = ieee80211_mbuf_adjust(vap, hdrspace, key, m1);
	if (m1 == NULL) {
		printf("%s: failed initial mbuf_adjust\n", __func__);
		/* NB: ieee80211_mbuf_adjust handles msgs+statistics */
		m_freem(m2);
		goto bad;
	}

	/*
	 * Copy second frame's Ethernet header out of line
	 * and adjust for possible padding in case there isn't room
	 * at the end of first frame.
	 */
	KASSERT(m2->m_len >= sizeof(eh2), ("no ethernet header!"));
	ETHER_HEADER_COPY(&eh2, mtod(m2, caddr_t));
	m2 = ieee80211_mbuf_adjust(vap, 4, NULL, m2);
	if (m2 == NULL) {
		/* NB: ieee80211_mbuf_adjust handles msgs+statistics */
		printf("%s: failed second \n", __func__);
		goto bad;
	}

	/*
	 * Now do tunnel encapsulation.  First, each
	 * frame gets a standard encapsulation.
	 */
	m1 = ieee80211_ff_encap1(vap, m1, &eh1);
	if (m1 == NULL)
		goto bad;
	m2 = ieee80211_ff_encap1(vap, m2, &eh2);
	if (m2 == NULL)
		goto bad;

	/*
	 * Pad leading frame to a 4-byte boundary.  If there
	 * is space at the end of the first frame, put it
	 * there; otherwise prepend to the front of the second
	 * frame.  We know doing the second will always work
	 * because we reserve space above.  We prefer appending
	 * as this typically has better DMA alignment properties.
	 */
	for (m = m1; m->m_next != NULL; m = m->m_next)
		;
	pad = roundup2(m1->m_pkthdr.len, 4) - m1->m_pkthdr.len;
	if (pad) {
		if (M_TRAILINGSPACE(m) < pad) {		/* prepend to second */
			m2->m_data -= pad;
			m2->m_len += pad;
			m2->m_pkthdr.len += pad;
		} else {				/* append to first */
			m->m_len += pad;
			m1->m_pkthdr.len += pad;
		}
	}

	/*
	 * A-MSDU's are just appended; the "I'm A-MSDU!" bit is in the
	 * QoS header.
	 *
	 * XXX optimize by prepending together
	 */
	m->m_next = m2;			/* NB: last mbuf from above */
	m1->m_pkthdr.len += m2->m_pkthdr.len;
	M_PREPEND(m1, sizeof(uint32_t)+2, M_NOWAIT);
	if (m1 == NULL) {		/* XXX cannot happen */
		IEEE80211_DPRINTF(vap, IEEE80211_MSG_SUPERG,
		    "%s: no space for tunnel header\n", __func__);
		vap->iv_stats.is_tx_nobuf++;
		return NULL;
	}
	memset(mtod(m1, void *), 0, sizeof(uint32_t)+2);

	M_PREPEND(m1, sizeof(struct llc), M_NOWAIT);
	if (m1 == NULL) {		/* XXX cannot happen */
		IEEE80211_DPRINTF(vap, IEEE80211_MSG_SUPERG,
		    "%s: no space for llc header\n", __func__);
		vap->iv_stats.is_tx_nobuf++;
		return NULL;
	}
	llc = mtod(m1, struct llc *);
	llc->llc_dsap = llc->llc_ssap = LLC_SNAP_LSAP;
	llc->llc_control = LLC_UI;
	llc->llc_snap.org_code[0] = ATH_FF_SNAP_ORGCODE_0;
	llc->llc_snap.org_code[1] = ATH_FF_SNAP_ORGCODE_1;
	llc->llc_snap.org_code[2] = ATH_FF_SNAP_ORGCODE_2;
	llc->llc_snap.ether_type = htons(ATH_FF_ETH_TYPE);

	vap->iv_stats.is_ff_encap++;

	return m1;
bad:
	vap->iv_stats.is_ff_encapfail++;
	if (m1 != NULL)
		m_freem(m1);
	if (m2 != NULL)
		m_freem(m2);
	return NULL;
}

/*
 * A-MSDU encapsulation.
 *
 * This assumes just two frames for now, since we're borrowing the
 * same queuing code and infrastructure as fast-frames.
 *
 * There must be two packets chained with m_nextpkt.
 * We do header adjustment for each, and then concatenate the mbuf chains
 * to form a single frame for transmission.
 */
struct mbuf *
ieee80211_amsdu_encap(struct ieee80211vap *vap, struct mbuf *m1, int hdrspace,
	struct ieee80211_key *key)
{
	struct mbuf *m2;
	struct ether_header eh1, eh2;
	struct mbuf *m;
	int pad;

	m2 = m1->m_nextpkt;
	if (m2 == NULL) {
		IEEE80211_DPRINTF(vap, IEEE80211_MSG_SUPERG,
		    "%s: only one frame\n", __func__);
		goto bad;
	}
	m1->m_nextpkt = NULL;

	/*
	 * Include A-MSDU header in adjusting header layout.
	 */
	KASSERT(m1->m_len >= sizeof(eh1), ("no ethernet header!"));
	ETHER_HEADER_COPY(&eh1, mtod(m1, caddr_t));
	m1 = ieee80211_mbuf_adjust(vap,
		hdrspace + sizeof(struct llc) + sizeof(uint32_t) +
		    sizeof(struct ether_header),
		key, m1);
	if (m1 == NULL) {
		/* NB: ieee80211_mbuf_adjust handles msgs+statistics */
		m_freem(m2);
		goto bad;
	}

	/*
	 * Copy second frame's Ethernet header out of line
	 * and adjust for encapsulation headers.  Note that
	 * we make room for padding in case there isn't room
	 * at the end of first frame.
	 */
	KASSERT(m2->m_len >= sizeof(eh2), ("no ethernet header!"));
	ETHER_HEADER_COPY(&eh2, mtod(m2, caddr_t));
	m2 = ieee80211_mbuf_adjust(vap, 4, NULL, m2);
	if (m2 == NULL) {
		/* NB: ieee80211_mbuf_adjust handles msgs+statistics */
		goto bad;
	}

	/*
	 * Now do tunnel encapsulation.  First, each
	 * frame gets a standard encapsulation.
	 */
	m1 = ieee80211_ff_encap1(vap, m1, &eh1);
	if (m1 == NULL)
		goto bad;
	m2 = ieee80211_ff_encap1(vap, m2, &eh2);
	if (m2 == NULL)
		goto bad;

	/*
	 * Pad leading frame to a 4-byte boundary.  If there
	 * is space at the end of the first frame, put it
	 * there; otherwise prepend to the front of the second
	 * frame.  We know doing the second will always work
	 * because we reserve space above.  We prefer appending
	 * as this typically has better DMA alignment properties.
	 */
	for (m = m1; m->m_next != NULL; m = m->m_next)
		;
	pad = roundup2(m1->m_pkthdr.len, 4) - m1->m_pkthdr.len;
	if (pad) {
		if (M_TRAILINGSPACE(m) < pad) {		/* prepend to second */
			m2->m_data -= pad;
			m2->m_len += pad;
			m2->m_pkthdr.len += pad;
		} else {				/* append to first */
			m->m_len += pad;
			m1->m_pkthdr.len += pad;
		}
	}

	/*
	 * Now, stick 'em together.
	 */
	m->m_next = m2;			/* NB: last mbuf from above */
	m1->m_pkthdr.len += m2->m_pkthdr.len;

	vap->iv_stats.is_amsdu_encap++;

	return m1;
bad:
	vap->iv_stats.is_amsdu_encapfail++;
	if (m1 != NULL)
		m_freem(m1);
	if (m2 != NULL)
		m_freem(m2);
	return NULL;
}


static void
ff_transmit(struct ieee80211_node *ni, struct mbuf *m)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211com *ic = ni->ni_ic;

	IEEE80211_TX_LOCK_ASSERT(ic);

	/* encap and xmit */
	m = ieee80211_encap(vap, ni, m);
	if (m != NULL)
		(void) ieee80211_parent_xmitpkt(ic, m);
	else
		ieee80211_free_node(ni);
}

/*
 * Flush frames to device; note we re-use the linked list
 * the frames were stored on and use the sentinel (unchanged)
 * which may be non-NULL.
 */
static void
ff_flush(struct mbuf *head, struct mbuf *last)
{
	struct mbuf *m, *next;
	struct ieee80211_node *ni;
	struct ieee80211vap *vap;

	for (m = head; m != last; m = next) {
		next = m->m_nextpkt;
		m->m_nextpkt = NULL;

		ni = (struct ieee80211_node *) m->m_pkthdr.rcvif;
		vap = ni->ni_vap;

		IEEE80211_NOTE(vap, IEEE80211_MSG_SUPERG, ni,
		    "%s: flush frame, age %u", __func__, M_AGE_GET(m));
		vap->iv_stats.is_ff_flush++;

		ff_transmit(ni, m);
	}
}

/*
 * Age frames on the staging queue.
 */
void
ieee80211_ff_age(struct ieee80211com *ic, struct ieee80211_stageq *sq,
    int quanta)
{
	struct mbuf *m, *head;
	struct ieee80211_node *ni;

	IEEE80211_FF_LOCK(ic);
	if (sq->depth == 0) {
		IEEE80211_FF_UNLOCK(ic);
		return;		/* nothing to do */
	}

	KASSERT(sq->head != NULL, ("stageq empty"));

	head = sq->head;
	while ((m = sq->head) != NULL && M_AGE_GET(m) < quanta) {
		int tid = WME_AC_TO_TID(M_WME_GETAC(m));

		/* clear staging ref to frame */
		ni = (struct ieee80211_node *) m->m_pkthdr.rcvif;
		KASSERT(ni->ni_tx_superg[tid] == m, ("staging queue empty"));
		ni->ni_tx_superg[tid] = NULL;

		sq->head = m->m_nextpkt;
		sq->depth--;
	}
	if (m == NULL)
		sq->tail = NULL;
	else
		M_AGE_SUB(m, quanta);
	IEEE80211_FF_UNLOCK(ic);

	IEEE80211_TX_LOCK(ic);
	ff_flush(head, m);
	IEEE80211_TX_UNLOCK(ic);
}

static void
stageq_add(struct ieee80211com *ic, struct ieee80211_stageq *sq, struct mbuf *m)
{
	int age = ieee80211_ffagemax;

	IEEE80211_FF_LOCK_ASSERT(ic);

	if (sq->tail != NULL) {
		sq->tail->m_nextpkt = m;
		age -= M_AGE_GET(sq->head);
	} else {
		sq->head = m;

		struct timeout_task *qtask = &ic->ic_superg->ff_qtimer;
		taskqueue_enqueue_timeout(ic->ic_tq, qtask, age);
	}
	KASSERT(age >= 0, ("age %d", age));
	M_AGE_SET(m, age);
	m->m_nextpkt = NULL;
	sq->tail = m;
	sq->depth++;
}

static void
stageq_remove(struct ieee80211com *ic, struct ieee80211_stageq *sq, struct mbuf *mstaged)
{
	struct mbuf *m, *mprev;

	IEEE80211_FF_LOCK_ASSERT(ic);

	mprev = NULL;
	for (m = sq->head; m != NULL; m = m->m_nextpkt) {
		if (m == mstaged) {
			if (mprev == NULL)
				sq->head = m->m_nextpkt;
			else
				mprev->m_nextpkt = m->m_nextpkt;
			if (sq->tail == m)
				sq->tail = mprev;
			sq->depth--;
			return;
		}
		mprev = m;
	}
	printf("%s: packet not found\n", __func__);
}

static uint32_t
ff_approx_txtime(struct ieee80211_node *ni,
	const struct mbuf *m1, const struct mbuf *m2)
{
	struct ieee80211com *ic = ni->ni_ic;
	struct ieee80211vap *vap = ni->ni_vap;
	uint32_t framelen;
	uint32_t frame_time;

	/*
	 * Approximate the frame length to be transmitted. A swag to add
	 * the following maximal values to the skb payload:
	 *   - 32: 802.11 encap + CRC
	 *   - 24: encryption overhead (if wep bit)
	 *   - 4 + 6: fast-frame header and padding
	 *   - 16: 2 LLC FF tunnel headers
	 *   - 14: 1 802.3 FF tunnel header (mbuf already accounts for 2nd)
	 */
	framelen = m1->m_pkthdr.len + 32 +
	    ATH_FF_MAX_HDR_PAD + ATH_FF_MAX_SEP_PAD + ATH_FF_MAX_HDR;
	if (vap->iv_flags & IEEE80211_F_PRIVACY)
		framelen += 24;
	if (m2 != NULL)
		framelen += m2->m_pkthdr.len;

	/*
	 * For now, we assume non-shortgi, 20MHz, just because I want to
	 * at least test 802.11n.
	 */
	if (ni->ni_txrate & IEEE80211_RATE_MCS)
		frame_time = ieee80211_compute_duration_ht(framelen,
		    ni->ni_txrate,
		    IEEE80211_HT_RC_2_STREAMS(ni->ni_txrate),
		    0, /* isht40 */
		    0); /* isshortgi */
	else
		frame_time = ieee80211_compute_duration(ic->ic_rt, framelen,
			    ni->ni_txrate, 0);
	return (frame_time);
}

/*
 * Check if the supplied frame can be partnered with an existing
 * or pending frame.  Return a reference to any frame that should be
 * sent on return; otherwise return NULL.
 */
struct mbuf *
ieee80211_ff_check(struct ieee80211_node *ni, struct mbuf *m)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211com *ic = ni->ni_ic;
	struct ieee80211_superg *sg = ic->ic_superg;
	const int pri = M_WME_GETAC(m);
	struct ieee80211_stageq *sq;
	struct ieee80211_tx_ampdu *tap;
	struct mbuf *mstaged;
	uint32_t txtime, limit;

	IEEE80211_TX_UNLOCK_ASSERT(ic);

	IEEE80211_LOCK(ic);
	limit = IEEE80211_TXOP_TO_US(
	    ic->ic_wme.wme_chanParams.cap_wmeParams[pri].wmep_txopLimit);
	IEEE80211_UNLOCK(ic);

	/*
	 * Check if the supplied frame can be aggregated.
	 *
	 * NB: we allow EAPOL frames to be aggregated with other ucast traffic.
	 *     Do 802.1x EAPOL frames proceed in the clear? Then they couldn't
	 *     be aggregated with other types of frames when encryption is on?
	 */
	IEEE80211_FF_LOCK(ic);
	tap = &ni->ni_tx_ampdu[WME_AC_TO_TID(pri)];
	mstaged = ni->ni_tx_superg[WME_AC_TO_TID(pri)];
	/* XXX NOTE: reusing packet counter state from A-MPDU */
	/*
	 * XXX NOTE: this means we're double-counting; it should just
	 * be done in ieee80211_output.c once for both superg and A-MPDU.
	 */
	ieee80211_txampdu_count_packet(tap);

	/*
	 * When not in station mode never aggregate a multicast
	 * frame; this insures, for example, that a combined frame
	 * does not require multiple encryption keys.
	 */
	if (vap->iv_opmode != IEEE80211_M_STA &&
	    ETHER_IS_MULTICAST(mtod(m, struct ether_header *)->ether_dhost)) {
		/* XXX flush staged frame? */
		IEEE80211_FF_UNLOCK(ic);
		return m;
	}
	/*
	 * If there is no frame to combine with and the pps is
	 * too low; then do not attempt to aggregate this frame.
	 */
	if (mstaged == NULL &&
	    ieee80211_txampdu_getpps(tap) < ieee80211_ffppsmin) {
		IEEE80211_FF_UNLOCK(ic);
		return m;
	}
	sq = &sg->ff_stageq[pri];
	/*
	 * Check the txop limit to insure the aggregate fits.
	 */
	if (limit != 0 &&
	    (txtime = ff_approx_txtime(ni, m, mstaged)) > limit) {
		/*
		 * Aggregate too long, return to the caller for direct
		 * transmission.  In addition, flush any pending frame
		 * before sending this one.
		 */
		IEEE80211_DPRINTF(vap, IEEE80211_MSG_SUPERG,
		    "%s: txtime %u exceeds txop limit %u\n",
		    __func__, txtime, limit);

		ni->ni_tx_superg[WME_AC_TO_TID(pri)] = NULL;
		if (mstaged != NULL)
			stageq_remove(ic, sq, mstaged);
		IEEE80211_FF_UNLOCK(ic);

		if (mstaged != NULL) {
			IEEE80211_TX_LOCK(ic);
			IEEE80211_NOTE(vap, IEEE80211_MSG_SUPERG, ni,
			    "%s: flush staged frame", __func__);
			/* encap and xmit */
			ff_transmit(ni, mstaged);
			IEEE80211_TX_UNLOCK(ic);
		}
		return m;		/* NB: original frame */
	}
	/*
	 * An aggregation candidate.  If there's a frame to partner
	 * with then combine and return for processing.  Otherwise
	 * save this frame and wait for a partner to show up (or
	 * the frame to be flushed).  Note that staged frames also
	 * hold their node reference.
	 */
	if (mstaged != NULL) {
		ni->ni_tx_superg[WME_AC_TO_TID(pri)] = NULL;
		stageq_remove(ic, sq, mstaged);
		IEEE80211_FF_UNLOCK(ic);

		IEEE80211_NOTE(vap, IEEE80211_MSG_SUPERG, ni,
		    "%s: aggregate fast-frame", __func__);
		/*
		 * Release the node reference; we only need
		 * the one already in mstaged.
		 */
		KASSERT(mstaged->m_pkthdr.rcvif == (void *)ni,
		    ("rcvif %p ni %p", mstaged->m_pkthdr.rcvif, ni));
		ieee80211_free_node(ni);

		m->m_nextpkt = NULL;
		mstaged->m_nextpkt = m;
		mstaged->m_flags |= M_FF; /* NB: mark for encap work */
	} else {
		KASSERT(ni->ni_tx_superg[WME_AC_TO_TID(pri)] == NULL,
		    ("ni_tx_superg[]: %p",
		    ni->ni_tx_superg[WME_AC_TO_TID(pri)]));
		ni->ni_tx_superg[WME_AC_TO_TID(pri)] = m;

		stageq_add(ic, sq, m);
		IEEE80211_FF_UNLOCK(ic);

		IEEE80211_NOTE(vap, IEEE80211_MSG_SUPERG, ni,
		    "%s: stage frame, %u queued", __func__, sq->depth);
		/* NB: mstaged is NULL */
	}
	return mstaged;
}

struct mbuf *
ieee80211_amsdu_check(struct ieee80211_node *ni, struct mbuf *m)
{
	/*
	 * XXX TODO: actually enforce the node support
	 * and HTCAP requirements for the maximum A-MSDU
	 * size.
	 */

	/* First: software A-MSDU transmit? */
	if (! ieee80211_amsdu_tx_ok(ni))
		return (m);

	/* Next - EAPOL? Nope, don't aggregate; we don't QoS encap them */
	if (m->m_flags & (M_EAPOL | M_MCAST | M_BCAST))
		return (m);

	/* Next - needs to be a data frame, non-broadcast, etc */
	if (ETHER_IS_MULTICAST(mtod(m, struct ether_header *)->ether_dhost))
		return (m);

	return (ieee80211_ff_check(ni, m));
}

void
ieee80211_ff_node_init(struct ieee80211_node *ni)
{
	/*
	 * Clean FF state on re-associate.  This handles the case
	 * where a station leaves w/o notifying us and then returns
	 * before node is reaped for inactivity.
	 */
	ieee80211_ff_node_cleanup(ni);
}

void
ieee80211_ff_node_cleanup(struct ieee80211_node *ni)
{
	struct ieee80211com *ic = ni->ni_ic;
	struct ieee80211_superg *sg = ic->ic_superg;
	struct mbuf *m, *next_m, *head;
	int tid;

	IEEE80211_FF_LOCK(ic);
	head = NULL;
	for (tid = 0; tid < WME_NUM_TID; tid++) {
		int ac = TID_TO_WME_AC(tid);
		/*
		 * XXX Initialise the packet counter.
		 *
		 * This may be double-work for 11n stations;
		 * but without it we never setup things.
		 */
		ieee80211_txampdu_init_pps(&ni->ni_tx_ampdu[tid]);
		m = ni->ni_tx_superg[tid];
		if (m != NULL) {
			ni->ni_tx_superg[tid] = NULL;
			stageq_remove(ic, &sg->ff_stageq[ac], m);
			m->m_nextpkt = head;
			head = m;
		}
	}
	IEEE80211_FF_UNLOCK(ic);

	/*
	 * Free mbufs, taking care to not dereference the mbuf after
	 * we free it (hence grabbing m_nextpkt before we free it.)
	 */
	m = head;
	while (m != NULL) {
		next_m = m->m_nextpkt;
		m_freem(m);
		ieee80211_free_node(ni);
		m = next_m;
	}
}

/*
 * Switch between turbo and non-turbo operating modes.
 * Use the specified channel flags to locate the new
 * channel, update 802.11 state, and then call back into
 * the driver to effect the change.
 */
void
ieee80211_dturbo_switch(struct ieee80211vap *vap, int newflags)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_channel *chan;

	chan = ieee80211_find_channel(ic, ic->ic_bsschan->ic_freq, newflags);
	if (chan == NULL) {		/* XXX should not happen */
		IEEE80211_DPRINTF(vap, IEEE80211_MSG_SUPERG,
		    "%s: no channel with freq %u flags 0x%x\n",
		    __func__, ic->ic_bsschan->ic_freq, newflags);
		return;
	}

	IEEE80211_DPRINTF(vap, IEEE80211_MSG_SUPERG,
	    "%s: %s -> %s (freq %u flags 0x%x)\n", __func__,
	    ieee80211_phymode_name[ieee80211_chan2mode(ic->ic_bsschan)],
	    ieee80211_phymode_name[ieee80211_chan2mode(chan)],
	    chan->ic_freq, chan->ic_flags);

	ic->ic_bsschan = chan;
	ic->ic_prevchan = ic->ic_curchan;
	ic->ic_curchan = chan;
	ic->ic_rt = ieee80211_get_ratetable(chan);
	ic->ic_set_channel(ic);
	ieee80211_radiotap_chan_change(ic);
	/* NB: do not need to reset ERP state 'cuz we're in sta mode */
}

/*
 * Return the current ``state'' of an Atheros capbility.
 * If associated in station mode report the negotiated
 * setting. Otherwise report the current setting.
 */
static int
getathcap(struct ieee80211vap *vap, int cap)
{
	if (vap->iv_opmode == IEEE80211_M_STA &&
	    vap->iv_state == IEEE80211_S_RUN)
		return IEEE80211_ATH_CAP(vap, vap->iv_bss, cap) != 0;
	else
		return (vap->iv_flags & cap) != 0;
}

static int
superg_ioctl_get80211(struct ieee80211vap *vap, struct ieee80211req *ireq)
{
	switch (ireq->i_type) {
	case IEEE80211_IOC_FF:
		ireq->i_val = getathcap(vap, IEEE80211_F_FF);
		break;
	case IEEE80211_IOC_TURBOP:
		ireq->i_val = getathcap(vap, IEEE80211_F_TURBOP);
		break;
	default:
		return ENOSYS;
	}
	return 0;
}
IEEE80211_IOCTL_GET(superg, superg_ioctl_get80211);

static int
superg_ioctl_set80211(struct ieee80211vap *vap, struct ieee80211req *ireq)
{
	switch (ireq->i_type) {
	case IEEE80211_IOC_FF:
		if (ireq->i_val) {
			if ((vap->iv_caps & IEEE80211_C_FF) == 0)
				return EOPNOTSUPP;
			vap->iv_flags |= IEEE80211_F_FF;
		} else
			vap->iv_flags &= ~IEEE80211_F_FF;
		return ENETRESET;
	case IEEE80211_IOC_TURBOP:
		if (ireq->i_val) {
			if ((vap->iv_caps & IEEE80211_C_TURBOP) == 0)
				return EOPNOTSUPP;
			vap->iv_flags |= IEEE80211_F_TURBOP;
		} else
			vap->iv_flags &= ~IEEE80211_F_TURBOP;
		return ENETRESET;
	default:
		return ENOSYS;
	}
}
IEEE80211_IOCTL_SET(superg, superg_ioctl_set80211);

#endif	/* IEEE80211_SUPPORT_SUPERG */
