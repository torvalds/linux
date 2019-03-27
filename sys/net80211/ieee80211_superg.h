/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Sam Leffler, Errno Consulting
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
 *
 * $FreeBSD$
 */
#ifndef _NET80211_IEEE80211_SUPERG_H_
#define _NET80211_IEEE80211_SUPERG_H_

/*
 * Atheros' 802.11 SuperG protocol support.
 */

/*
 * Atheros advanced capability information element.
 */
struct ieee80211_ath_ie {
	uint8_t		ath_id;			/* IEEE80211_ELEMID_VENDOR */
	uint8_t		ath_len;		/* length in bytes */
	uint8_t		ath_oui[3];		/* ATH_OUI */
	uint8_t		ath_oui_type;		/* ATH_OUI_TYPE */
	uint8_t		ath_oui_subtype;	/* ATH_OUI_SUBTYPE */
	uint8_t		ath_version;		/* spec revision */
	uint8_t		ath_capability;		/* capability info */
#define	ATHEROS_CAP_TURBO_PRIME		0x01	/* dynamic turbo--aka Turbo' */
#define	ATHEROS_CAP_COMPRESSION		0x02	/* data compression */
#define	ATHEROS_CAP_FAST_FRAME		0x04	/* fast (jumbo) frames */
#define	ATHEROS_CAP_XR			0x08	/* Xtended Range support */
#define	ATHEROS_CAP_AR			0x10	/* Advanded Radar support */
#define	ATHEROS_CAP_BURST		0x20	/* Bursting - not negotiated */
#define	ATHEROS_CAP_WME			0x40	/* CWMin tuning */
#define	ATHEROS_CAP_BOOST		0x80	/* use turbo/!turbo mode */
	uint8_t		ath_defkeyix[2];
} __packed;

#define	ATH_OUI_VERSION		0x00
#define	ATH_OUI_SUBTYPE		0x01

#ifdef _KERNEL
struct ieee80211_stageq {
	struct mbuf		*head;		/* frames linked w/ m_nextpkt */
	struct mbuf		*tail;		/* last frame in queue */
	int			depth;		/* # items on head */
};

struct ieee80211_superg {
	/* fast-frames staging q */
	struct ieee80211_stageq	ff_stageq[WME_NUM_AC];
	/* flush queues automatically */
	struct timeout_task	ff_qtimer;
};

void	ieee80211_superg_attach(struct ieee80211com *);
void	ieee80211_superg_detach(struct ieee80211com *);
void	ieee80211_superg_vattach(struct ieee80211vap *);
void	ieee80211_superg_vdetach(struct ieee80211vap *);

uint8_t *ieee80211_add_ath(uint8_t *, uint8_t, ieee80211_keyix);
uint8_t *ieee80211_add_athcaps(uint8_t *, const struct ieee80211_node *);
void	ieee80211_parse_ath(struct ieee80211_node *, uint8_t *);
int	ieee80211_parse_athparams(struct ieee80211_node *, uint8_t *,
	    const struct ieee80211_frame *);

void	ieee80211_ff_node_init(struct ieee80211_node *);
void	ieee80211_ff_node_cleanup(struct ieee80211_node *);

static inline int
ieee80211_amsdu_tx_ok(struct ieee80211_node *ni)
{

	/* First: software A-MSDU transmit? */
	if ((ni->ni_ic->ic_caps & IEEE80211_C_SWAMSDUTX) == 0)
		return (0);

	/* Next: does the VAP have AMSDU TX enabled? */
	if ((ni->ni_vap->iv_flags_ht & IEEE80211_FHT_AMSDU_TX) == 0)
		return (0);

	/* Next: 11n node? (assumed that A-MSDU TX to HT nodes is ok */
	if ((ni->ni_flags & IEEE80211_NODE_HT) == 0)
		return (0);

	/* ok, we can at least /do/ AMSDU to this node */
	return (1);
}

struct mbuf * ieee80211_amsdu_check(struct ieee80211_node *ni, struct mbuf *m);
struct mbuf *ieee80211_ff_check(struct ieee80211_node *, struct mbuf *);
void	ieee80211_ff_age(struct ieee80211com *, struct ieee80211_stageq *,
	     int quanta);

static __inline void
ieee80211_ff_age_all(struct ieee80211com *ic, int quanta)
{
	struct ieee80211_superg *sg = ic->ic_superg;

	if (sg != NULL) {
		ieee80211_ff_age(ic, &sg->ff_stageq[WME_AC_VO], quanta);
		ieee80211_ff_age(ic, &sg->ff_stageq[WME_AC_VI], quanta);
		ieee80211_ff_age(ic, &sg->ff_stageq[WME_AC_BE], quanta);
		ieee80211_ff_age(ic, &sg->ff_stageq[WME_AC_BK], quanta);
	}
}

static __inline void
ieee80211_ff_flush(struct ieee80211com *ic, int ac)
{
	struct ieee80211_superg *sg = ic->ic_superg;

	if (sg != NULL)
		ieee80211_ff_age(ic, &sg->ff_stageq[ac], 0x7fffffff);
}

static __inline void
ieee80211_ff_flush_all(struct ieee80211com *ic)
{
	ieee80211_ff_age_all(ic, 0x7fffffff);
}

struct mbuf *ieee80211_ff_encap(struct ieee80211vap *, struct mbuf *,
	    int, struct ieee80211_key *);
struct mbuf * ieee80211_amsdu_encap(struct ieee80211vap *vap, struct mbuf *m1,
	    int hdrspace, struct ieee80211_key *key);

struct mbuf *ieee80211_ff_decap(struct ieee80211_node *, struct mbuf *);

static __inline struct mbuf *
ieee80211_decap_fastframe(struct ieee80211vap *vap, struct ieee80211_node *ni,
    struct mbuf *m)
{
	return IEEE80211_ATH_CAP(vap, ni, IEEE80211_NODE_FF) ?
	    ieee80211_ff_decap(ni, m) : m;
}
#endif /* _KERNEL */
#endif /* _NET80211_IEEE80211_SUPERG_H_ */
