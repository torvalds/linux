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
 *
 * $FreeBSD$
 */
#ifndef _NET80211_IEEE80211_NODE_H_
#define _NET80211_IEEE80211_NODE_H_

#include <net80211/ieee80211_ioctl.h>		/* for ieee80211_nodestats */
#include <net80211/ieee80211_ht.h>		/* for aggregation state */

/*
 * Each ieee80211com instance has a single timer that fires every
 * IEEE80211_INACT_WAIT seconds to handle "inactivity processing".
 * This is used to do node inactivity processing when operating
 * as an AP, adhoc or mesh mode.  For inactivity processing each node
 * has a timeout set in its ni_inact field that is decremented
 * on each timeout and the node is reclaimed when the counter goes
 * to zero.  We use different inactivity timeout values depending
 * on whether the node is associated and authorized (either by
 * 802.1x or open/shared key authentication) or associated but yet
 * to be authorized.  The latter timeout is shorter to more aggressively
 * reclaim nodes that leave part way through the 802.1x exchange.
 */
#define	IEEE80211_INACT_WAIT	15		/* inactivity interval (secs) */
#define	IEEE80211_INACT_INIT	(30/IEEE80211_INACT_WAIT)	/* initial */
#define	IEEE80211_INACT_AUTH	(180/IEEE80211_INACT_WAIT)	/* associated but not authorized */
#define	IEEE80211_INACT_RUN	(300/IEEE80211_INACT_WAIT)	/* authorized */
#define	IEEE80211_INACT_PROBE	(30/IEEE80211_INACT_WAIT)	/* probe */
#define	IEEE80211_INACT_SCAN	(300/IEEE80211_INACT_WAIT)	/* scanned */

#define	IEEE80211_TRANS_WAIT 	2		/* mgt frame tx timer (secs) */

/* threshold for aging overlapping non-ERP bss */
#define	IEEE80211_NONERP_PRESENT_AGE	msecs_to_ticks(60*1000)

#define	IEEE80211_NODE_HASHSIZE	32		/* NB: hash size must be pow2 */
/* simple hash is enough for variation of macaddr */
#define	IEEE80211_NODE_HASH(ic, addr)	\
	(((const uint8_t *)(addr))[IEEE80211_ADDR_LEN - 1] % \
		IEEE80211_NODE_HASHSIZE)

struct ieee80211_node_table;
struct ieee80211com;
struct ieee80211vap;
struct ieee80211_scanparams;

/*
 * Information element ``blob''.  We use this structure
 * to capture management frame payloads that need to be
 * retained.  Information elements within the payload that
 * we need to consult have references recorded.
 */
struct ieee80211_ies {
	/* the following are either NULL or point within data */
	uint8_t	*wpa_ie;	/* captured WPA ie */
	uint8_t	*rsn_ie;	/* captured RSN ie */
	uint8_t	*wme_ie;	/* captured WME ie */
	uint8_t	*ath_ie;	/* captured Atheros ie */
	uint8_t	*htcap_ie;	/* captured HTCAP ie */
	uint8_t	*htinfo_ie;	/* captured HTINFO ie */
	uint8_t	*tdma_ie;	/* captured TDMA ie */
	uint8_t *meshid_ie;	/* captured MESH ID ie */
	uint8_t	*vhtcap_ie;	/* captured VHTCAP ie */
	uint8_t	*vhtopmode_ie;	/* captured VHTOPMODE ie */
	uint8_t	*vhtpwrenv_ie;	/* captured VHTPWRENV ie */
	uint8_t	*apchanrep_ie;	/* captured APCHANREP ie */
	uint8_t	*bssload_ie;	/* captured BSSLOAD ie */
	uint8_t	*spare[4];
	/* NB: these must be the last members of this structure */
	uint8_t	*data;		/* frame data > 802.11 header */
	int	len;		/* data size in bytes */
};

/*
 * 802.11s (Mesh) Peer Link FSM state.
 */
enum ieee80211_mesh_mlstate {
	IEEE80211_NODE_MESH_IDLE	= 0,
	IEEE80211_NODE_MESH_OPENSNT	= 1,	/* open frame sent */
	IEEE80211_NODE_MESH_OPENRCV	= 2,	/* open frame received */
	IEEE80211_NODE_MESH_CONFIRMRCV	= 3,	/* confirm frame received */
	IEEE80211_NODE_MESH_ESTABLISHED	= 4,	/* link established */
	IEEE80211_NODE_MESH_HOLDING	= 5,	/* link closing */
};
#define	IEEE80211_MESH_MLSTATE_BITS \
	"\20\1IDLE\2OPENSNT\2OPENRCV\3CONFIRMRCV\4ESTABLISHED\5HOLDING"

/*
 * Node specific information.  Note that drivers are expected
 * to derive from this structure to add device-specific per-node
 * state.  This is done by overriding the ic_node_* methods in
 * the ieee80211com structure.
 */
struct ieee80211_node {
	struct ieee80211vap	*ni_vap;	/* associated vap */
	struct ieee80211com	*ni_ic;		/* copy from vap to save deref*/
	struct ieee80211_node_table *ni_table;	/* NB: may be NULL */
	TAILQ_ENTRY(ieee80211_node) ni_list;	/* list of all nodes */
	LIST_ENTRY(ieee80211_node) ni_hash;	/* hash collision list */
	u_int			ni_refcnt;	/* count of held references */
	u_int			ni_flags;
#define	IEEE80211_NODE_AUTH	0x000001	/* authorized for data */
#define	IEEE80211_NODE_QOS	0x000002	/* QoS enabled */
#define	IEEE80211_NODE_ERP	0x000004	/* ERP enabled */
/* NB: this must have the same value as IEEE80211_FC1_PWR_MGT */
#define	IEEE80211_NODE_PWR_MGT	0x000010	/* power save mode enabled */
#define	IEEE80211_NODE_AREF	0x000020	/* authentication ref held */
#define	IEEE80211_NODE_HT	0x000040	/* HT enabled */
#define	IEEE80211_NODE_HTCOMPAT	0x000080	/* HT setup w/ vendor OUI's */
#define	IEEE80211_NODE_WPS	0x000100	/* WPS association */
#define	IEEE80211_NODE_TSN	0x000200	/* TSN association */
#define	IEEE80211_NODE_AMPDU_RX	0x000400	/* AMPDU rx enabled */
#define	IEEE80211_NODE_AMPDU_TX	0x000800	/* AMPDU tx enabled */
#define	IEEE80211_NODE_MIMO_PS	0x001000	/* MIMO power save enabled */
#define	IEEE80211_NODE_MIMO_RTS	0x002000	/* send RTS in MIMO PS */
#define	IEEE80211_NODE_RIFS	0x004000	/* RIFS enabled */
#define	IEEE80211_NODE_SGI20	0x008000	/* Short GI in HT20 enabled */
#define	IEEE80211_NODE_SGI40	0x010000	/* Short GI in HT40 enabled */
#define	IEEE80211_NODE_ASSOCID	0x020000	/* xmit requires associd */
#define	IEEE80211_NODE_AMSDU_RX	0x040000	/* AMSDU rx enabled */
#define	IEEE80211_NODE_AMSDU_TX	0x080000	/* AMSDU tx enabled */
#define	IEEE80211_NODE_VHT	0x100000	/* VHT enabled */
#define	IEEE80211_NODE_LDPC	0x200000	/* LDPC enabled */
	uint16_t		ni_associd;	/* association ID */
	uint16_t		ni_vlan;	/* vlan tag */
	uint16_t		ni_txpower;	/* current transmit power */
	uint8_t			ni_authmode;	/* authentication algorithm */
	uint8_t			ni_ath_flags;	/* Atheros feature flags */
	/* NB: These must have the same values as IEEE80211_ATHC_* */
#define IEEE80211_NODE_TURBOP	0x0001		/* Turbo prime enable */
#define IEEE80211_NODE_COMP	0x0002		/* Compresssion enable */
#define IEEE80211_NODE_FF	0x0004          /* Fast Frame capable */
#define IEEE80211_NODE_XR	0x0008		/* Atheros WME enable */
#define IEEE80211_NODE_AR	0x0010		/* AR capable */
#define IEEE80211_NODE_BOOST	0x0080		/* Dynamic Turbo boosted */
	uint16_t		ni_ath_defkeyix;/* Atheros def key index */
	const struct ieee80211_txparam *ni_txparms;
	uint32_t		ni_jointime;	/* time of join (secs) */
	uint32_t		*ni_challenge;	/* shared-key challenge */
	struct ieee80211_ies	ni_ies;		/* captured ie's */
						/* tx seq per-tid */
	ieee80211_seq		ni_txseqs[IEEE80211_TID_SIZE];
						/* rx seq previous per-tid*/
	ieee80211_seq		ni_rxseqs[IEEE80211_TID_SIZE];
	uint32_t		ni_rxfragstamp;	/* time stamp of last rx frag */
	struct mbuf		*ni_rxfrag[3];	/* rx frag reassembly */
	struct ieee80211_key	ni_ucastkey;	/* unicast key */

	/* hardware */
	uint32_t		ni_avgrssi;	/* recv ssi state */
	int8_t			ni_noise;	/* noise floor */

	/* mimo statistics */
	uint32_t		ni_mimo_rssi_ctl[IEEE80211_MAX_CHAINS];
	uint32_t		ni_mimo_rssi_ext[IEEE80211_MAX_CHAINS];
	uint8_t			ni_mimo_noise_ctl[IEEE80211_MAX_CHAINS];
	uint8_t			ni_mimo_noise_ext[IEEE80211_MAX_CHAINS];
	uint8_t			ni_mimo_chains;

	/* header */
	uint8_t			ni_macaddr[IEEE80211_ADDR_LEN];
	uint8_t			ni_bssid[IEEE80211_ADDR_LEN];

	/* beacon, probe response */
	union {
		uint8_t		data[8];
		u_int64_t	tsf;
	} ni_tstamp;				/* from last rcv'd beacon */
	uint16_t		ni_intval;	/* beacon interval */
	uint16_t		ni_capinfo;	/* capabilities */
	uint8_t			ni_esslen;
	uint8_t			ni_essid[IEEE80211_NWID_LEN];
	struct ieee80211_rateset ni_rates;	/* negotiated rate set */
	struct ieee80211_channel *ni_chan;
	uint16_t		ni_fhdwell;	/* FH only */
	uint8_t			ni_fhindex;	/* FH only */
	uint16_t		ni_erp;		/* ERP from beacon/probe resp */
	uint16_t		ni_timoff;	/* byte offset to TIM ie */
	uint8_t			ni_dtim_period;	/* DTIM period */
	uint8_t			ni_dtim_count;	/* DTIM count for last bcn */

	/* 11s state */
	uint8_t			ni_meshidlen;
	uint8_t			ni_meshid[IEEE80211_MESHID_LEN];
	enum ieee80211_mesh_mlstate ni_mlstate;	/* peering management state */
	uint16_t		ni_mllid;	/* link local ID */
	uint16_t		ni_mlpid;	/* link peer ID */
	struct callout		ni_mltimer;	/* link mesh timer */
	uint8_t			ni_mlrcnt;	/* link mesh retry counter */
	uint8_t			ni_mltval;	/* link mesh timer value */
	struct callout		ni_mlhtimer;	/* link mesh backoff timer */
	uint8_t			ni_mlhcnt;	/* link mesh holding counter */

	/* 11n state */
	uint16_t		ni_htcap;	/* HT capabilities */
	uint8_t			ni_htparam;	/* HT params */
	uint8_t			ni_htctlchan;	/* HT control channel */
	uint8_t			ni_ht2ndchan;	/* HT 2nd channel */
	uint8_t			ni_htopmode;	/* HT operating mode */
	uint8_t			ni_htstbc;	/* HT */
	uint8_t			ni_chw;		/* negotiated channel width */
	struct ieee80211_htrateset ni_htrates;	/* negotiated ht rate set */
	struct ieee80211_tx_ampdu ni_tx_ampdu[WME_NUM_TID];
	struct ieee80211_rx_ampdu ni_rx_ampdu[WME_NUM_TID];

	/* VHT state */
	uint32_t		ni_vhtcap;
	uint16_t		ni_vht_basicmcs;
	uint16_t		ni_vht_pad2;
	struct ieee80211_vht_mcs_info	ni_vht_mcsinfo;
	uint8_t			ni_vht_chan1;	/* 20/40/80/160 - VHT chan1 */
	uint8_t			ni_vht_chan2;	/* 80+80 - VHT chan2 */
	uint8_t			ni_vht_chanwidth;	/* IEEE80211_VHT_CHANWIDTH_ */
	uint8_t			ni_vht_pad1;
	uint32_t		ni_vht_spare[8];

	/* fast-frames state */
	struct mbuf *		ni_tx_superg[WME_NUM_TID];

	/* others */
	short			ni_inact;	/* inactivity mark count */
	short			ni_inact_reload;/* inactivity reload value */
	int			ni_txrate;	/* legacy rate/MCS */
	struct ieee80211_psq	ni_psq;		/* power save queue */
	struct ieee80211_nodestats ni_stats;	/* per-node statistics */

	struct ieee80211vap	*ni_wdsvap;	/* associated WDS vap */
	void			*ni_rctls;	/* private ratectl state */

	/* quiet time IE state for the given node */
	uint32_t		ni_quiet_ie_set;	/* Quiet time IE was seen */
	struct			ieee80211_quiet_ie ni_quiet_ie;	/* last seen quiet IE */

	uint64_t		ni_spare[3];
};
MALLOC_DECLARE(M_80211_NODE);
MALLOC_DECLARE(M_80211_NODE_IE);

#define	IEEE80211_NODE_ATH	(IEEE80211_NODE_FF | IEEE80211_NODE_TURBOP)
#define	IEEE80211_NODE_AMPDU \
	(IEEE80211_NODE_AMPDU_RX | IEEE80211_NODE_AMPDU_TX)
#define	IEEE80211_NODE_AMSDU \
	(IEEE80211_NODE_AMSDU_RX | IEEE80211_NODE_AMSDU_TX)
#define	IEEE80211_NODE_HT_ALL \
	(IEEE80211_NODE_HT | IEEE80211_NODE_HTCOMPAT | \
	 IEEE80211_NODE_AMPDU | IEEE80211_NODE_AMSDU | \
	 IEEE80211_NODE_MIMO_PS | IEEE80211_NODE_MIMO_RTS | \
	 IEEE80211_NODE_RIFS | IEEE80211_NODE_SGI20 | IEEE80211_NODE_SGI40)

#define	IEEE80211_NODE_BITS \
	"\20\1AUTH\2QOS\3ERP\5PWR_MGT\6AREF\7HT\10HTCOMPAT\11WPS\12TSN" \
	"\13AMPDU_RX\14AMPDU_TX\15MIMO_PS\16MIMO_RTS\17RIFS\20SGI20\21SGI40" \
	"\22ASSOCID"

#define	IEEE80211_NODE_AID(ni)	IEEE80211_AID(ni->ni_associd)

#define	IEEE80211_NODE_STAT(ni,stat)	(ni->ni_stats.ns_##stat++)
#define	IEEE80211_NODE_STAT_ADD(ni,stat,v)	(ni->ni_stats.ns_##stat += v)
#define	IEEE80211_NODE_STAT_SET(ni,stat,v)	(ni->ni_stats.ns_##stat = v)

/*
 * Filtered rssi calculation support.  The receive rssi is maintained
 * as an average over the last 10 frames received using a low pass filter
 * (all frames for now, possibly need to be more selective).  Calculations
 * are designed such that a good compiler can optimize them.  The avg
 * rssi state should be initialized to IEEE80211_RSSI_DUMMY_MARKER and
 * each sample incorporated with IEEE80211_RSSI_LPF.  Use IEEE80211_RSSI_GET
 * to extract the current value.
 *
 * Note that we assume rssi data are in the range [-127..127] and we
 * discard values <-20.  This is consistent with assumptions throughout
 * net80211 that signal strength data are in .5 dBm units relative to
 * the current noise floor (linear, not log).
 */
#define IEEE80211_RSSI_LPF_LEN		10
#define	IEEE80211_RSSI_DUMMY_MARKER	127
/* NB: pow2 to optimize out * and / */
#define	IEEE80211_RSSI_EP_MULTIPLIER	(1<<7)
#define IEEE80211_RSSI_IN(x)		((x) * IEEE80211_RSSI_EP_MULTIPLIER)
#define _IEEE80211_RSSI_LPF(x, y, len) \
    (((x) != IEEE80211_RSSI_DUMMY_MARKER) ? (((x) * ((len) - 1) + (y)) / (len)) : (y))
#define IEEE80211_RSSI_LPF(x, y) do {					\
    if ((y) >= -20) {							\
    	x = _IEEE80211_RSSI_LPF((x), IEEE80211_RSSI_IN((y)), 		\
		IEEE80211_RSSI_LPF_LEN);				\
    }									\
} while (0)
#define	IEEE80211_RSSI_EP_RND(x, mul) \
	((((x) % (mul)) >= ((mul)/2)) ? ((x) + ((mul) - 1)) / (mul) : (x)/(mul))
#define	IEEE80211_RSSI_GET(x) \
	IEEE80211_RSSI_EP_RND(x, IEEE80211_RSSI_EP_MULTIPLIER)

static __inline struct ieee80211_node *
ieee80211_ref_node(struct ieee80211_node *ni)
{
	ieee80211_node_incref(ni);
	return ni;
}

static __inline void
ieee80211_unref_node(struct ieee80211_node **ni)
{
	ieee80211_node_decref(*ni);
	*ni = NULL;			/* guard against use */
}

void	ieee80211_node_attach(struct ieee80211com *);
void	ieee80211_node_lateattach(struct ieee80211com *);
void	ieee80211_node_detach(struct ieee80211com *);
void	ieee80211_node_vattach(struct ieee80211vap *);
void	ieee80211_node_latevattach(struct ieee80211vap *);
void	ieee80211_node_vdetach(struct ieee80211vap *);

static __inline int
ieee80211_node_is_authorized(const struct ieee80211_node *ni)
{
	return (ni->ni_flags & IEEE80211_NODE_AUTH);
}

void	ieee80211_node_authorize(struct ieee80211_node *);
void	ieee80211_node_unauthorize(struct ieee80211_node *);

void	ieee80211_node_setuptxparms(struct ieee80211_node *);
void	ieee80211_node_set_chan(struct ieee80211_node *,
		struct ieee80211_channel *);
void	ieee80211_create_ibss(struct ieee80211vap*, struct ieee80211_channel *);
void	ieee80211_reset_bss(struct ieee80211vap *);
void	ieee80211_sync_curchan(struct ieee80211com *);
void	ieee80211_setupcurchan(struct ieee80211com *,
	    struct ieee80211_channel *);
void	ieee80211_setcurchan(struct ieee80211com *, struct ieee80211_channel *);
void	ieee80211_update_chw(struct ieee80211com *);
int	ieee80211_ibss_merge_check(struct ieee80211_node *);
int	ieee80211_ibss_node_check_new(struct ieee80211_node *ni,
	    const struct ieee80211_scanparams *);
int	ieee80211_ibss_merge(struct ieee80211_node *);
struct ieee80211_scan_entry;
int	ieee80211_sta_join(struct ieee80211vap *, struct ieee80211_channel *,
		const struct ieee80211_scan_entry *);
void	ieee80211_sta_leave(struct ieee80211_node *);
void	ieee80211_node_deauth(struct ieee80211_node *, int);

int	ieee80211_ies_init(struct ieee80211_ies *, const uint8_t *, int);
void	ieee80211_ies_cleanup(struct ieee80211_ies *);
void	ieee80211_ies_expand(struct ieee80211_ies *);
#define	ieee80211_ies_setie(_ies, _ie, _off) do {		\
	(_ies)._ie = (_ies).data + (_off);			\
} while (0)

/*
 * Table of ieee80211_node instances.  Each ieee80211com
 * has one that holds association stations (when operating
 * as an ap) or neighbors (in ibss mode).
 *
 * XXX embed this in ieee80211com instead of indirect?
 */
struct ieee80211_node_table {
	struct ieee80211com	*nt_ic;		/* back reference */
	ieee80211_node_lock_t	nt_nodelock;	/* on node table */
	TAILQ_HEAD(, ieee80211_node) nt_node;	/* information of all nodes */
	LIST_HEAD(, ieee80211_node) nt_hash[IEEE80211_NODE_HASHSIZE];
	int			nt_count;	/* number of nodes */
	struct ieee80211_node	**nt_keyixmap;	/* key ix -> node map */
	int			nt_keyixmax;	/* keyixmap size */
	const char		*nt_name;	/* table name for debug msgs */
	int			nt_inact_init;	/* initial node inact setting */
};

struct ieee80211_node *ieee80211_alloc_node(struct ieee80211_node_table *,
		struct ieee80211vap *,
		const uint8_t macaddr[IEEE80211_ADDR_LEN]);
struct ieee80211_node *ieee80211_tmp_node(struct ieee80211vap *,
		const uint8_t macaddr[IEEE80211_ADDR_LEN]);
struct ieee80211_node *ieee80211_dup_bss(struct ieee80211vap *,
		const uint8_t macaddr[IEEE80211_ADDR_LEN]);
struct ieee80211_node *ieee80211_node_create_wds(struct ieee80211vap *,
		const uint8_t bssid[IEEE80211_ADDR_LEN],
		struct ieee80211_channel *);
#ifdef IEEE80211_DEBUG_REFCNT
void	ieee80211_free_node_debug(struct ieee80211_node *,
		const char *func, int line);
struct ieee80211_node *ieee80211_find_node_locked_debug(
		struct ieee80211_node_table *,
		const uint8_t macaddr[IEEE80211_ADDR_LEN],
		const char *func, int line);
struct ieee80211_node *ieee80211_find_node_debug(struct ieee80211_node_table *,
		const uint8_t macaddr[IEEE80211_ADDR_LEN],
		const char *func, int line);
struct ieee80211_node *ieee80211_find_vap_node_locked_debug(
		struct ieee80211_node_table *,
		const struct ieee80211vap *vap,
		const uint8_t macaddr[IEEE80211_ADDR_LEN],
		const char *func, int line);
struct ieee80211_node *ieee80211_find_vap_node_debug(
		struct ieee80211_node_table *,
		const struct ieee80211vap *vap,
		const uint8_t macaddr[IEEE80211_ADDR_LEN],
		const char *func, int line);
struct ieee80211_node * ieee80211_find_rxnode_debug(struct ieee80211com *,
		const struct ieee80211_frame_min *,
		const char *func, int line);
struct ieee80211_node * ieee80211_find_rxnode_withkey_debug(
		struct ieee80211com *,
		const struct ieee80211_frame_min *, uint16_t keyix,
		const char *func, int line);
struct ieee80211_node *ieee80211_find_txnode_debug(struct ieee80211vap *,
		const uint8_t *,
		const char *func, int line);
#define	ieee80211_free_node(ni) \
	ieee80211_free_node_debug(ni, __func__, __LINE__)
#define	ieee80211_find_node_locked(nt, mac) \
	ieee80211_find_node_locked_debug(nt, mac, __func__, __LINE__)
#define	ieee80211_find_node(nt, mac) \
	ieee80211_find_node_debug(nt, mac, __func__, __LINE__)
#define	ieee80211_find_vap_node_locked(nt, vap, mac) \
	ieee80211_find_vap_node_locked_debug(nt, vap, mac, __func__, __LINE__)
#define	ieee80211_find_vap_node(nt, vap, mac) \
	ieee80211_find_vap_node_debug(nt, vap, mac, __func__, __LINE__)
#define	ieee80211_find_rxnode(ic, wh) \
	ieee80211_find_rxnode_debug(ic, wh, __func__, __LINE__)
#define	ieee80211_find_rxnode_withkey(ic, wh, keyix) \
	ieee80211_find_rxnode_withkey_debug(ic, wh, keyix, __func__, __LINE__)
#define	ieee80211_find_txnode(vap, mac) \
	ieee80211_find_txnode_debug(vap, mac, __func__, __LINE__)
#else
void	ieee80211_free_node(struct ieee80211_node *);
struct ieee80211_node *ieee80211_find_node_locked(struct ieee80211_node_table *,
		const uint8_t macaddr[IEEE80211_ADDR_LEN]);
struct ieee80211_node *ieee80211_find_node(struct ieee80211_node_table *,
		const uint8_t macaddr[IEEE80211_ADDR_LEN]);
struct ieee80211_node *ieee80211_find_vap_node_locked(
		struct ieee80211_node_table *, const struct ieee80211vap *,
		const uint8_t macaddr[IEEE80211_ADDR_LEN]);
struct ieee80211_node *ieee80211_find_vap_node(
		struct ieee80211_node_table *, const struct ieee80211vap *,
		const uint8_t macaddr[IEEE80211_ADDR_LEN]);
struct ieee80211_node * ieee80211_find_rxnode(struct ieee80211com *,
		const struct ieee80211_frame_min *);
struct ieee80211_node * ieee80211_find_rxnode_withkey(struct ieee80211com *,
		const struct ieee80211_frame_min *, uint16_t keyix);
struct ieee80211_node *ieee80211_find_txnode(struct ieee80211vap *,
		const uint8_t macaddr[IEEE80211_ADDR_LEN]);
#endif
int	ieee80211_node_delucastkey(struct ieee80211_node *);
void	ieee80211_node_timeout(void *arg);

typedef void ieee80211_iter_func(void *, struct ieee80211_node *);
int	ieee80211_iterate_nodes_vap(struct ieee80211_node_table *,
		struct ieee80211vap *, ieee80211_iter_func *, void *);
void	ieee80211_iterate_nodes(struct ieee80211_node_table *,
		ieee80211_iter_func *, void *);

void	ieee80211_notify_erp(struct ieee80211com *);
void	ieee80211_dump_node(struct ieee80211_node_table *,
		struct ieee80211_node *);
void	ieee80211_dump_nodes(struct ieee80211_node_table *);

struct ieee80211_node *ieee80211_fakeup_adhoc_node(struct ieee80211vap *,
		const uint8_t macaddr[IEEE80211_ADDR_LEN]);
struct ieee80211_scanparams;
void	ieee80211_init_neighbor(struct ieee80211_node *,
		const struct ieee80211_frame *,
		const struct ieee80211_scanparams *);
struct ieee80211_node *ieee80211_add_neighbor(struct ieee80211vap *,
		const struct ieee80211_frame *,
		const struct ieee80211_scanparams *);
void	ieee80211_node_join(struct ieee80211_node *,int);
void	ieee80211_node_leave(struct ieee80211_node *);
int8_t	ieee80211_getrssi(struct ieee80211vap *);
void	ieee80211_getsignal(struct ieee80211vap *, int8_t *, int8_t *);
#endif /* _NET80211_IEEE80211_NODE_H_ */
