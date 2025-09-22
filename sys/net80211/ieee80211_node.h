/*	$OpenBSD: ieee80211_node.h,v 1.97 2025/08/01 20:39:26 stsp Exp $	*/
/*	$NetBSD: ieee80211_node.h,v 1.9 2004/04/30 22:57:32 dyoung Exp $	*/

/*-
 * Copyright (c) 2001 Atsushi Onoe
 * Copyright (c) 2002, 2003 Sam Leffler, Errno Consulting
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
 *
 * $FreeBSD: src/sys/net80211/ieee80211_node.h,v 1.10 2004/04/05 22:10:26 sam Exp $
 */
#ifndef _NET80211_IEEE80211_NODE_H_
#define _NET80211_IEEE80211_NODE_H_

#include <sys/tree.h>

#define	IEEE80211_PSCAN_WAIT	5		/* passive scan wait */
#define	IEEE80211_TRANS_WAIT	5		/* transition wait */
#define	IEEE80211_INACT_WAIT	5		/* inactivity timer interval */
#define	IEEE80211_INACT_MAX	(300/IEEE80211_INACT_WAIT)
#define	IEEE80211_CACHE_SIZE	512
#define	IEEE80211_CACHE_WAIT	30
#define	IEEE80211_INACT_SCAN	10		/* for station mode */

struct ieee80211_rateset {
	u_int8_t		rs_nrates;
	u_int8_t		rs_rates[IEEE80211_RATE_MAXSIZE];
};

extern const struct ieee80211_rateset ieee80211_std_rateset_11a;
extern const struct ieee80211_rateset ieee80211_std_rateset_11b;
extern const struct ieee80211_rateset ieee80211_std_rateset_11g;

/* Index into ieee80211_std_ratesets_11n[] array. */
#define IEEE80211_HT_RATESET_SISO	0
#define IEEE80211_HT_RATESET_SISO_SGI	1
#define IEEE80211_HT_RATESET_MIMO2	2
#define IEEE80211_HT_RATESET_MIMO2_SGI	3
#define IEEE80211_HT_RATESET_MIMO3	4
#define IEEE80211_HT_RATESET_MIMO3_SGI	5
#define IEEE80211_HT_RATESET_MIMO4	6
#define IEEE80211_HT_RATESET_MIMO4_SGI	7
#define IEEE80211_HT_RATESET_SISO_40	8
#define IEEE80211_HT_RATESET_SISO_SGI40 9
#define IEEE80211_HT_RATESET_MIMO2_40	10
#define IEEE80211_HT_RATESET_MIMO2_SGI40 11
#define IEEE80211_HT_RATESET_MIMO3_40	12
#define IEEE80211_HT_RATESET_MIMO3_SGI40 13
#define IEEE80211_HT_RATESET_MIMO4_40	14
#define IEEE80211_HT_RATESET_MIMO4_SGI40 15
#define IEEE80211_HT_NUM_RATESETS	16

/* Maximum number of rates in a HT rateset. */
#define IEEE80211_HT_RATESET_MAX_NRATES	8

/* Number of MCS indices represented by struct ieee80211_ht_rateset. */
#define IEEE80211_HT_RATESET_NUM_MCS 32

struct ieee80211_ht_rateset {
	uint32_t nrates;
	uint32_t rates[IEEE80211_HT_RATESET_MAX_NRATES]; /* 500 kbit/s units */

	/*
	 * This bitmask can only express MCS 0 - MCS 31.
	 * IEEE 802.11 defined 77 HT MCS in total but common hardware
	 * implementations tend to support MCS index 0 through 31 only.
	 */
	uint32_t mcs_mask;

	/* Range of MCS indices represented in this rateset. */
	int min_mcs;
	int max_mcs;

	int chan40;
	int sgi;
};

extern const struct ieee80211_ht_rateset ieee80211_std_ratesets_11n[];

/* Index into ieee80211_std_ratesets_11ac[] array. */
#define IEEE80211_VHT_RATESET_SISO		0
#define IEEE80211_VHT_RATESET_SISO_SGI		1
#define IEEE80211_VHT_RATESET_MIMO2		2
#define IEEE80211_VHT_RATESET_MIMO2_SGI		3
#define IEEE80211_VHT_RATESET_SISO_40		4
#define IEEE80211_VHT_RATESET_SISO_40_SGI	5
#define IEEE80211_VHT_RATESET_MIMO2_40		6
#define IEEE80211_VHT_RATESET_MIMO2_40_SGI	7
#define IEEE80211_VHT_RATESET_SISO_80		8
#define IEEE80211_VHT_RATESET_SISO_80_SGI	9
#define IEEE80211_VHT_RATESET_MIMO2_80		10
#define IEEE80211_VHT_RATESET_MIMO2_80_SGI	11
#define IEEE80211_VHT_NUM_RATESETS		12

/* Maximum number of rates in a VHT rateset. */
#define IEEE80211_VHT_RATESET_MAX_NRATES	10

struct ieee80211_vht_rateset {
	int idx; /* This rateset's index in ieee80211_std_ratesets_11ac[]. */

	uint32_t nrates;
	uint32_t rates[IEEE80211_VHT_RATESET_MAX_NRATES]; /* 500 kbit/s units */

	/* Number of spatial streams used by rates in this rateset. */
	int num_ss;

	int chan40;
	int chan80;
	int sgi;
};

extern const struct ieee80211_vht_rateset ieee80211_std_ratesets_11ac[];

enum ieee80211_node_state {
	IEEE80211_STA_CACHE,	/* cached node */
	IEEE80211_STA_BSS,	/* ic->ic_bss, the network we joined */
	IEEE80211_STA_AUTH,	/* successfully authenticated */
	IEEE80211_STA_ASSOC,	/* successfully associated */
	IEEE80211_STA_COLLECT	/* This node remains in the cache while
				 * the driver sends a de-auth message;
				 * afterward it should be freed to make room
				 * for a new node.
				 */
};

#define	ieee80211_node_newstate(__ni, __state)	\
	do {					\
		(__ni)->ni_state = (__state);	\
	} while (0)

enum ieee80211_node_psstate {
	IEEE80211_PS_AWAKE,
	IEEE80211_PS_DOZE
};

#define	IEEE80211_PS_MAX_QUEUE	50	/* maximum saved packets */

/* Authenticator state machine: 4-Way Handshake (see 8.5.6.1.1) */
enum {
	RSNA_INITIALIZE,
	RSNA_AUTHENTICATION,
	RSNA_AUTHENTICATION_2,
	RSNA_INITPMK,
	RSNA_INITPSK,
	RSNA_PTKSTART,
	RSNA_PTKCALCNEGOTIATING,
	RSNA_PTKCALCNEGOTIATING_2,
	RSNA_PTKINITNEGOTIATING,
	RSNA_PTKINITDONE,
	RSNA_DISCONNECT,
	RSNA_DISCONNECTED
};

/* Authenticator state machine: Group Key Handshake (see 8.5.6.1.2) */
enum {
	RSNA_IDLE,
	RSNA_REKEYNEGOTIATING,
	RSNA_REKEYESTABLISHED,
	RSNA_KEYERROR
};

/* Supplicant state machine: 4-Way Handshake (not documented in standard) */
enum {
	RSNA_SUPP_INITIALIZE,		/* not expecting any messages */
	RSNA_SUPP_PTKSTART,		/* awaiting handshake message 1 */
	RSNA_SUPP_PTKNEGOTIATING,	/* got message 1 and derived PTK */
	RSNA_SUPP_PTKDONE		/* got message 3 and authenticated AP */
};

struct ieee80211_rxinfo {
	u_int32_t		rxi_flags;
	u_int32_t		rxi_tstamp;
	int			rxi_rssi;
	uint8_t			rxi_chan;
};
#define IEEE80211_RXI_HWDEC		0x00000001
#define IEEE80211_RXI_AMPDU_DONE	0x00000002
#define IEEE80211_RXI_HWDEC_SAME_PN	0x00000004
#define IEEE80211_RXI_SAME_SEQ		0x00000008

/* Block Acknowledgement Record */
struct ieee80211_tx_ba {
	struct ieee80211_node	*ba_ni;	/* backpointer for callbacks */
	struct timeout		ba_to;
	int			ba_timeout_val;
	int			ba_state;
#define IEEE80211_BA_INIT	0
#define IEEE80211_BA_REQUESTED	1
#define IEEE80211_BA_AGREED	2

	/* ADDBA parameter set field for this BA agreement. */
	u_int16_t		ba_params;

	/* These values are IEEE802.11 frame sequence numbers (0x0-0xfff) */
	u_int16_t		ba_winstart;
	u_int16_t		ba_winend;

	/* Number of A-MPDU subframes in reorder buffer. */
	u_int16_t		ba_winsize;
#define IEEE80211_BA_MAX_WINSZ	64	/* corresponds to maximum ADDBA BUFSZ */

	u_int8_t		ba_token;

	/* Bitmap for ACK'd frames in the current BA window. */
	uint64_t		ba_bitmap;
};

struct ieee80211_rx_ba {
	struct ieee80211_node	*ba_ni;	/* backpointer for callbacks */
	struct {
		struct mbuf		*m;
		struct ieee80211_rxinfo	rxi;
	}			*ba_buf;
	struct timeout		ba_to;
	int			ba_timeout_val;
	int			ba_state;
	u_int16_t		ba_params;
	u_int16_t		ba_winstart;
	u_int16_t		ba_winend;
	u_int16_t		ba_winsize;
	u_int16_t		ba_head;
	struct timeout		ba_gap_to;
#define IEEE80211_BA_GAP_TIMEOUT	300 /* msec */

	/*
	 * Counter for frames forced to wait in the reordering buffer
	 * due to a leading gap caused by one or more missing frames.
	 */
	int			ba_gapwait;

	/* Counter for consecutive frames which missed the BA window. */
	int			ba_winmiss;
	/* Sequence number of previous frame which missed the BA window. */
	uint16_t		ba_missedsn;
	/* Window moves forward after this many frames have missed it. */
#define IEEE80211_BA_MAX_WINMISS	8

	uint8_t			ba_token;
};

/*
 * Node specific information.  Note that drivers are expected
 * to derive from this structure to add device-specific per-node
 * state.  This is done by overriding the ic_node_* methods in
 * the ieee80211com structure.
 */
struct ieee80211_node {
	RBT_ENTRY(ieee80211_node)	ni_node;

	struct ieee80211com	*ni_ic;		/* back-pointer */

	u_int			ni_refcnt;
	u_int			ni_scangen;	/* gen# for timeout scan */

	/* hardware */
	u_int32_t		ni_rstamp;	/* recv timestamp */
	u_int8_t		ni_rssi;	/* recv ssi */

	/* header */
	u_int8_t		ni_macaddr[IEEE80211_ADDR_LEN];
	u_int8_t		ni_bssid[IEEE80211_ADDR_LEN];

	/* beacon, probe response */
	u_int8_t		ni_tstamp[8];	/* from last rcv'd beacon */
	u_int16_t		ni_intval;	/* beacon interval */
	u_int16_t		ni_capinfo;	/* capabilities */
	u_int8_t		ni_esslen;
	u_int8_t		ni_essid[IEEE80211_NWID_LEN];
	struct ieee80211_rateset ni_rates;	/* negotiated rate set */
	u_int8_t		*ni_country;	/* country information XXX */
	struct ieee80211_channel *ni_chan;
	u_int8_t		ni_erp;		/* 11g only */

	/* DTIM and contention free period (CFP) */
	u_int8_t		ni_dtimcount;
	u_int8_t		ni_dtimperiod;
#ifdef notyet
	u_int8_t		ni_cfpperiod;	/* # of DTIMs between CFPs */
	u_int16_t		ni_cfpduremain;	/* remaining cfp duration */
	u_int16_t		ni_cfpmaxduration;/* max CFP duration in TU */
	u_int16_t		ni_nextdtim;	/* time to next DTIM */
	u_int16_t		ni_timoffset;
#endif

	/* power saving mode */
	u_int8_t		ni_pwrsave;
	struct mbuf_queue	ni_savedq;	/* packets queued for pspoll */

	/* RSN */
	struct timeout		ni_eapol_to;
	u_int			ni_rsn_state;
	u_int			ni_rsn_supp_state;
	u_int			ni_rsn_gstate;
	u_int			ni_rsn_retries;
	u_int			ni_supported_rsnprotos;
	u_int			ni_rsnprotos;
	u_int			ni_supported_rsnakms;
	u_int			ni_rsnakms;
	u_int			ni_rsnciphers;
	enum ieee80211_cipher	ni_rsngroupcipher;
	enum ieee80211_cipher	ni_rsngroupmgmtcipher;
	u_int16_t		ni_rsncaps;
	enum ieee80211_cipher	ni_rsncipher;
	u_int8_t		ni_nonce[EAPOL_KEY_NONCE_LEN];
	u_int8_t		ni_pmk[IEEE80211_PMK_LEN];
	u_int8_t		ni_pmkid[IEEE80211_PMKID_LEN];
	u_int64_t		ni_replaycnt;
	u_int8_t		ni_replaycnt_ok;
	u_int64_t		ni_reqreplaycnt;
	u_int8_t		ni_reqreplaycnt_ok;
	u_int8_t		*ni_rsnie;
	struct ieee80211_key	ni_pairwise_key;
	struct ieee80211_ptk	ni_ptk;
	u_int8_t		ni_key_count;
	int			ni_port_valid;

	/* SA Query */
	u_int16_t		ni_sa_query_trid;
	struct timeout		ni_sa_query_to;
	int			ni_sa_query_count;

	/* HT capabilities */
	uint16_t		ni_htcaps;
	uint8_t			ni_ampdu_param;
	uint8_t			ni_rxmcs[howmany(80,NBBY)];
	uint16_t		ni_max_rxrate;	/* in Mb/s, 0 <= rate <= 1023 */
	uint8_t			ni_tx_mcs_set;
	uint16_t		ni_htxcaps;
	uint32_t		ni_txbfcaps;
	uint8_t			ni_aselcaps;

	/* HT operation */
	uint8_t			ni_primary_chan; /* XXX corresponds to ni_chan */
	uint8_t			ni_htop0;
	uint16_t		ni_htop1;
	uint16_t		ni_htop2;
	uint8_t			ni_basic_mcs[howmany(128,NBBY)];

	/* VHT capabilities */
	uint32_t		ni_vhtcaps;
	uint16_t		ni_vht_rxmcs;
	uint16_t		ni_vht_rx_max_lgi_mbit_s;
	uint16_t		ni_vht_txmcs;
	uint16_t		ni_vht_tx_max_lgi_mbit_s;

	/* VHT operation */
	uint8_t			ni_vht_chan_width;
	uint8_t			ni_vht_chan_center_freq_idx0;
	uint8_t			ni_vht_chan_center_freq_idx1;
	uint16_t		ni_vht_basic_mcs;

	/* Timeout handlers which trigger Tx Block Ack negotiation. */
	struct timeout		ni_addba_req_to[IEEE80211_NUM_TID];
	int			ni_addba_req_intval[IEEE80211_NUM_TID];
#define IEEE80211_ADDBA_REQ_INTVAL_MAX 30	/* in seconds */

	/* Block Ack records */
	struct ieee80211_tx_ba	ni_tx_ba[IEEE80211_NUM_TID];
	struct ieee80211_rx_ba	ni_rx_ba[IEEE80211_NUM_TID];

	int			ni_txmcs;	/* current MCS used for TX */
	int			ni_vht_ss;	/* VHT # spatial streams */

	/* others */
	u_int16_t		ni_associd;	/* assoc response */
	u_int16_t		ni_txseq;	/* seq to be transmitted */
	u_int16_t		ni_rxseq;	/* seq previous received */
	u_int16_t		ni_qos_txseqs[IEEE80211_NUM_TID];
	u_int16_t		ni_qos_rxseqs[IEEE80211_NUM_TID];
	int			ni_fails;	/* failure count to associate */
	uint32_t		ni_assoc_fail;	/* assoc failure reasons */
#define IEEE80211_NODE_ASSOCFAIL_CHAN		0x01
#define IEEE80211_NODE_ASSOCFAIL_IBSS		0x02
#define IEEE80211_NODE_ASSOCFAIL_PRIVACY	0x04
#define IEEE80211_NODE_ASSOCFAIL_BASIC_RATE	0x08
#define IEEE80211_NODE_ASSOCFAIL_ESSID		0x10
#define IEEE80211_NODE_ASSOCFAIL_BSSID		0x20
#define IEEE80211_NODE_ASSOCFAIL_WPA_PROTO	0x40
#define IEEE80211_NODE_ASSOCFAIL_WPA_KEY	0x80

	int			ni_inact;	/* inactivity mark count */
	int			ni_txrate;	/* index to ni_rates[] */
	int			ni_state;

	u_int32_t		ni_flags;	/* special-purpose state */
#define IEEE80211_NODE_ERP		0x0001
#define IEEE80211_NODE_QOS		0x0002
#define IEEE80211_NODE_REKEY		0x0004	/* GTK rekeying in progress */
#define IEEE80211_NODE_RXPROT		0x0008	/* RX protection ON */
#define IEEE80211_NODE_TXPROT		0x0010	/* TX protection ON */
#define IEEE80211_NODE_TXRXPROT	\
	(IEEE80211_NODE_TXPROT | IEEE80211_NODE_RXPROT)
#define IEEE80211_NODE_RXMGMTPROT	0x0020	/* RX MMPDU protection ON */
#define IEEE80211_NODE_TXMGMTPROT	0x0040	/* TX MMPDU protection ON */
#define IEEE80211_NODE_MFP		0x0080	/* MFP negotiated */
#define IEEE80211_NODE_PMK		0x0100	/* ni_pmk set */
#define IEEE80211_NODE_PMKID		0x0200	/* ni_pmkid set */
#define IEEE80211_NODE_HT		0x0400	/* HT negotiated */
#define IEEE80211_NODE_SA_QUERY		0x0800	/* SA Query in progress */
#define IEEE80211_NODE_SA_QUERY_FAILED	0x1000	/* last SA Query failed */
#define IEEE80211_NODE_RSN_NEW_PTK	0x2000	/* expecting a new PTK */
#define IEEE80211_NODE_HT_SGI20		0x4000	/* SGI on 20 MHz negotiated */ 
#define IEEE80211_NODE_HT_SGI40		0x8000	/* SGI on 40 MHz negotiated */ 
#define IEEE80211_NODE_VHT		0x10000	/* VHT negotiated */
#define IEEE80211_NODE_HTCAP		0x20000	/* claims to support HT */
#define IEEE80211_NODE_VHTCAP		0x40000	/* claims to support VHT */
#define IEEE80211_NODE_VHT_SGI80	0x80000	/* SGI on 80 MHz negotiated */ 
#define IEEE80211_NODE_VHT_SGI160	0x100000 /* SGI on 160 MHz negotiated */ 

	/* If not NULL, this function gets called when ni_refcnt hits zero. */
	void			(*ni_unref_cb)(struct ieee80211com *,
					struct ieee80211_node *);
	void *			ni_unref_arg;
	size_t 			ni_unref_arg_size;
};

RBT_HEAD(ieee80211_tree, ieee80211_node);

struct ieee80211_ess_rbt {
	RBT_ENTRY(ieee80211_ess_rbt)	 ess_rbt;
	u_int8_t			 esslen;
	u_int8_t			 essid[IEEE80211_NWID_LEN];
	struct ieee80211_node		*ni2;
	struct ieee80211_node		*ni5;
	struct ieee80211_node		*ni;
};

RBT_HEAD(ieee80211_ess_tree, ieee80211_ess_rbt);

static inline void
ieee80211_node_incref(struct ieee80211_node *ni)
{
	int		s;

	s = splnet();
	ni->ni_refcnt++;
	splx(s);
}

static inline u_int
ieee80211_node_decref(struct ieee80211_node *ni)
{
	u_int		refcnt;
	int 		s;

	s = splnet();
	refcnt = --ni->ni_refcnt;
	splx(s);
	return refcnt;
}

static inline struct ieee80211_node *
ieee80211_ref_node(struct ieee80211_node *ni)
{
	ieee80211_node_incref(ni);
	return ni;
}

static inline void
ieee80211_unref_node(struct ieee80211_node **ni)
{
	ieee80211_node_decref(*ni);
	*ni = NULL;			/* guard against use */
}

/* 
 * Check if the peer supports HT.
 * Require a HT capabilities IE and at least one of the mandatory MCS.
 * MCS 0-7 are mandatory but some APs have particular MCS disabled.
 */
static inline int
ieee80211_node_supports_ht(struct ieee80211_node *ni)
{
	return ((ni->ni_flags & IEEE80211_NODE_HTCAP) &&
	    ni->ni_rxmcs[0] & 0xff);
}

/* Check if the peer supports HT short guard interval (SGI) on 20 MHz. */
static inline int
ieee80211_node_supports_ht_sgi20(struct ieee80211_node *ni)
{
	return ieee80211_node_supports_ht(ni) &&
	    (ni->ni_htcaps & IEEE80211_HTCAP_SGI20);
}

/* Check if the peer supports HT short guard interval (SGI) on 40 MHz. */
static inline int
ieee80211_node_supports_ht_sgi40(struct ieee80211_node *ni)
{
	return ieee80211_node_supports_ht(ni) &&
	    (ni->ni_htcaps & IEEE80211_HTCAP_SGI40);
}

/* Check if the peer can receive frames sent on a 40 MHz channel. */
static inline int
ieee80211_node_supports_ht_chan40(struct ieee80211_node *ni)
{
	return (ieee80211_node_supports_ht(ni) &&
	    (ni->ni_htcaps & IEEE80211_HTCAP_CBW20_40) &&
	    (ni->ni_htop0 & IEEE80211_HTOP0_CHW));
}

/* 
 * Check if the peer supports VHT.
 * Require a VHT capabilities IE and support for VHT MCS with a single
 * spatial stream.
 */
static inline int
ieee80211_node_supports_vht(struct ieee80211_node *ni)
{
	uint16_t rx_mcs;

	rx_mcs = (ni->ni_vht_rxmcs & IEEE80211_VHT_MCS_FOR_SS_MASK(1)) >>
	    IEEE80211_VHT_MCS_FOR_SS_SHIFT(1);

	return ((ni->ni_flags & IEEE80211_NODE_VHTCAP) &&
	    rx_mcs != IEEE80211_VHT_MCS_SS_NOT_SUPP);
}

/* Check if the peer supports VHT short guard interval (SGI) on 80 MHz. */
static inline int
ieee80211_node_supports_vht_sgi80(struct ieee80211_node *ni)
{
	return ieee80211_node_supports_vht(ni) &&
	    (ni->ni_vhtcaps & IEEE80211_VHTCAP_SGI80);
}

/* Check if the peer supports VHT short guard interval (SGI) on 160 MHz. */
static inline int
ieee80211_node_supports_vht_sgi160(struct ieee80211_node *ni)
{
	return ieee80211_node_supports_vht(ni) &&
	    (ni->ni_vhtcaps & IEEE80211_VHTCAP_SGI160);
}

/* Check if the peer can receive frames sent on an 80 MHz channel. */
static inline int
ieee80211_node_supports_vht_chan80(struct ieee80211_node *ni)
{
	uint8_t cap_chan_width, op_chan_width;

	if (!ieee80211_node_supports_vht(ni))
		return 0;

	cap_chan_width = (ni->ni_vhtcaps & IEEE80211_VHTCAP_CHAN_WIDTH_MASK) >>
	    IEEE80211_VHTCAP_CHAN_WIDTH_SHIFT;
	if (cap_chan_width != IEEE80211_VHTCAP_CHAN_WIDTH_80 &&	 
	    cap_chan_width != IEEE80211_VHTCAP_CHAN_WIDTH_160 &&	 
	    cap_chan_width != IEEE80211_VHTCAP_CHAN_WIDTH_160_8080)
		return 0;

	op_chan_width = (ni->ni_vht_chan_width &
	    IEEE80211_VHTOP0_CHAN_WIDTH_MASK) >>
	    IEEE80211_VHTOP0_CHAN_WIDTH_SHIFT;

	return (op_chan_width == IEEE80211_VHTOP0_CHAN_WIDTH_80 ||
	    op_chan_width == IEEE80211_VHTOP0_CHAN_WIDTH_160 ||
	    op_chan_width == IEEE80211_VHTOP0_CHAN_WIDTH_8080);
}

/* Check if the peer can receive frames sent on a 160 MHz channel. */
static inline int
ieee80211_node_supports_vht_chan160(struct ieee80211_node *ni)
{
	uint8_t cap_chan_width, op_chan_width;

	if (!ieee80211_node_supports_vht(ni))
		return 0;

	cap_chan_width = (ni->ni_vhtcaps & IEEE80211_VHTCAP_CHAN_WIDTH_MASK) >>
	    IEEE80211_VHTCAP_CHAN_WIDTH_SHIFT;
	if (cap_chan_width != IEEE80211_VHTCAP_CHAN_WIDTH_160)
		return 0;

	op_chan_width = (ni->ni_vht_chan_width &
	    IEEE80211_VHTOP0_CHAN_WIDTH_MASK) >>
	    IEEE80211_VHTOP0_CHAN_WIDTH_SHIFT;

	return (op_chan_width == IEEE80211_VHTOP0_CHAN_WIDTH_160);
}

struct ieee80211com;

typedef void ieee80211_iter_func(void *, struct ieee80211_node *);

void ieee80211_node_attach(struct ifnet *);
void ieee80211_node_lateattach(struct ifnet *);
void ieee80211_node_detach(struct ifnet *);

void ieee80211_begin_scan(struct ifnet *);
void ieee80211_next_scan(struct ifnet *);
void ieee80211_end_scan(struct ifnet *);
void ieee80211_reset_scan(struct ifnet *);
struct ieee80211_node *ieee80211_alloc_node(struct ieee80211com *,
		const u_int8_t *);
struct ieee80211_node *ieee80211_dup_bss(struct ieee80211com *,
		const u_int8_t *);
struct ieee80211_node *ieee80211_find_node(struct ieee80211com *,
		const u_int8_t *);
void ieee80211_node_tx_ba_clear(struct ieee80211_node *, int);
void ieee80211_ba_del(struct ieee80211_node *);
struct ieee80211_node *ieee80211_find_rxnode(struct ieee80211com *,
		const struct ieee80211_frame *);
struct ieee80211_node *ieee80211_find_txnode(struct ieee80211com *,
		const u_int8_t *);
void ieee80211_release_node(struct ieee80211com *,
		struct ieee80211_node *);
void ieee80211_node_cleanup(struct ieee80211com *, struct ieee80211_node *);
void ieee80211_free_allnodes(struct ieee80211com *, int);
void ieee80211_iterate_nodes(struct ieee80211com *,
		ieee80211_iter_func *, void *);
void ieee80211_clean_cached(struct ieee80211com *);
void ieee80211_clean_nodes(struct ieee80211com *, int);
void ieee80211_setup_htcaps(struct ieee80211_node *, const uint8_t *,
    uint8_t);
void ieee80211_clear_htcaps(struct ieee80211_node *);
int ieee80211_setup_htop(struct ieee80211_node *, const uint8_t *,
    uint8_t, int);
void ieee80211_setup_vhtcaps(struct ieee80211_node *, const uint8_t *,
    uint8_t);
void ieee80211_clear_vhtcaps(struct ieee80211_node *);
int ieee80211_setup_vhtop(struct ieee80211_node *, const uint8_t *,
    uint8_t, int);
int ieee80211_setup_rates(struct ieee80211com *,
	    struct ieee80211_node *, const u_int8_t *, const u_int8_t *, int);
enum ieee80211_phymode ieee80211_node_abg_mode(struct ieee80211com *,
	    struct ieee80211_node *);
void ieee80211_node_trigger_addba_req(struct ieee80211_node *, int);
void ieee80211_count_longslotsta(void *, struct ieee80211_node *);
void ieee80211_count_nonerpsta(void *, struct ieee80211_node *);
void ieee80211_count_pssta(void *, struct ieee80211_node *);
void ieee80211_count_rekeysta(void *, struct ieee80211_node *);
void ieee80211_node_join(struct ieee80211com *,
		struct ieee80211_node *, int);
void ieee80211_node_leave(struct ieee80211com *,
		struct ieee80211_node *);
int ieee80211_match_bss(struct ieee80211com *, struct ieee80211_node *, int);
void ieee80211_node_tx_stopped(struct ieee80211com *, struct ieee80211_node *);
struct ieee80211_node *ieee80211_node_choose_bss(struct ieee80211com *, int,
		struct ieee80211_node **);
void ieee80211_node_join_bss(struct ieee80211com *, struct ieee80211_node *);
void ieee80211_create_ibss(struct ieee80211com* ,
		struct ieee80211_channel *);
void ieee80211_notify_dtim(struct ieee80211com *);
void ieee80211_set_tim(struct ieee80211com *, int, int);
void ieee80211_free_node(struct ieee80211com *, struct ieee80211_node *);

int ieee80211_node_cmp(const struct ieee80211_node *,
		const struct ieee80211_node *);
int ieee80211_ess_cmp(const struct ieee80211_ess_rbt *,
		const struct ieee80211_ess_rbt *);
RBT_PROTOTYPE(ieee80211_tree, ieee80211_node, ni_node, ieee80211_node_cmp);
RBT_PROTOTYPE(ieee80211_ess_tree, ieee80211_ess_rbt, ess_rbt, ieee80211_ess_cmp);

#endif /* _NET80211_IEEE80211_NODE_H_ */
