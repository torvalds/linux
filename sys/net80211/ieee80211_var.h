/*	$OpenBSD: ieee80211_var.h,v 1.112 2025/08/01 20:39:26 stsp Exp $	*/
/*	$NetBSD: ieee80211_var.h,v 1.7 2004/05/06 03:07:10 dyoung Exp $	*/

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
 * $FreeBSD: src/sys/net80211/ieee80211_var.h,v 1.15 2004/04/05 22:10:26 sam Exp $
 */
#ifndef _NET80211_IEEE80211_VAR_H_
#define _NET80211_IEEE80211_VAR_H_

/*
 * Definitions for IEEE 802.11 drivers.
 */

#ifdef	SMALL_KERNEL
#define IEEE80211_STA_ONLY	1
#endif

#include <sys/timeout.h>

#include <net80211/ieee80211.h>
#include <net80211/ieee80211_crypto.h>
#include <net80211/ieee80211_ioctl.h>		/* for ieee80211_stats */
#include <net80211/ieee80211_node.h>
#include <net80211/ieee80211_proto.h>

#define	IEEE80211_CHAN_MAX	255
#define	IEEE80211_CHAN_ANY	0xffff		/* token for ``any channel'' */
#define	IEEE80211_CHAN_ANYC \
	((struct ieee80211_channel *) NULL)

#define	IEEE80211_TXPOWER_MAX	100	/* max power */
#define	IEEE80211_TXPOWER_MIN	-50	/* kill radio (if possible) */

#define IEEE80211_RSSI_THRES_2GHZ		(-60)	/* in dBm */
#define IEEE80211_RSSI_THRES_5GHZ		(-70)	/* in dBm */
#define IEEE80211_RSSI_THRES_RATIO_2GHZ		60	/* in percent */
#define IEEE80211_RSSI_THRES_RATIO_5GHZ		50	/* in percent */

#define IEEE80211_BGSCAN_FAIL_MAX		512	/* units of 500 msec */

/*
 * Missed beacon threshold: An access point has disappeared if this amount
 * of consecutive beacons have been missed.
 * This value needs to be high enough to avoid frequent re-connects to APs
 * which suffer from occasional packet loss, and low enough to avoid a long
 * delay before we start scanning when an AP has actually disappeared.
 *
 * The beacon interval is variable, but generally in the order of 100ms.
 * So 30 beacons implies a grace period of about 3 seconds before we start
 * searching for a new AP.
 */
#define IEEE80211_BEACON_MISS_THRES		30	/* units of beacons */

enum ieee80211_phytype {
	IEEE80211_T_DS,			/* direct sequence spread spectrum */
	IEEE80211_T_OFDM,		/* frequency division multiplexing */
	IEEE80211_T_XR		        /* extended range mode */
};
#define	IEEE80211_T_CCK	IEEE80211_T_DS	/* more common nomenclature */

/* XXX not really a mode; there are really multiple PHY's */
enum ieee80211_phymode {
	IEEE80211_MODE_AUTO	= 0,	/* autoselect */
	IEEE80211_MODE_11A	= 1,	/* 5GHz, OFDM */
	IEEE80211_MODE_11B	= 2,	/* 2GHz, CCK */
	IEEE80211_MODE_11G	= 3,	/* 2GHz, OFDM */
	IEEE80211_MODE_11N	= 4,	/* 2GHz/5GHz, OFDM/HT */
	IEEE80211_MODE_11AC	= 5,	/* 5GHz, OFDM/VHT */
};
#define	IEEE80211_MODE_MAX	(IEEE80211_MODE_11AC+1)

enum ieee80211_opmode {
	IEEE80211_M_STA		= 1,	/* infrastructure station */
#ifndef IEEE80211_STA_ONLY
	IEEE80211_M_IBSS	= 0,	/* IBSS (adhoc) station */
	IEEE80211_M_AHDEMO	= 3,	/* Old lucent compatible adhoc demo */
	IEEE80211_M_HOSTAP	= 6,	/* Software Access Point */
#endif
	IEEE80211_M_MONITOR	= 8	/* Monitor mode */
};

/*
 * 802.11g protection mode.
 */
enum ieee80211_protmode {
	IEEE80211_PROT_NONE	= 0,	/* no protection */
	IEEE80211_PROT_CTSONLY	= 1,	/* CTS to self */
	IEEE80211_PROT_RTSCTS	= 2	/* RTS-CTS */
};

/*
 * Channels are specified by frequency and attributes.
 */
struct ieee80211_channel {
	u_int16_t	ic_freq;	/* setting in MHz */
	u_int16_t	ic_flags;	/* see below */
	u_int32_t	ic_xflags;	/* extra flags; see below */
};

/*
 * Channel attributes (XXX must keep in sync with radiotap flags).
 */
#define IEEE80211_CHAN_CCK	0x0020	/* CCK channel */
#define IEEE80211_CHAN_OFDM	0x0040	/* OFDM channel */
#define IEEE80211_CHAN_2GHZ	0x0080	/* 2 GHz spectrum channel */
#define IEEE80211_CHAN_5GHZ	0x0100	/* 5 GHz spectrum channel */
#define IEEE80211_CHAN_PASSIVE	0x0200	/* Only passive scan allowed */
#define IEEE80211_CHAN_DYN	0x0400	/* Dynamic CCK-OFDM channel */
#define IEEE80211_CHAN_XR	0x1000	/* Extended range OFDM channel */
#define IEEE80211_CHAN_HT	0x2000	/* 11n/HT channel */
#define IEEE80211_CHAN_VHT	0x4000	/* 11ac/VHT channel */
#define IEEE80211_CHAN_40MHZ	0x8000	/* use of 40 MHz is allowed */

/*
 * Extra channel flags.
 */
#define IEEE80211_CHANX_80MHZ	0x00000001 /* use of 80 MHz is allowed */
#define IEEE80211_CHANX_160MHZ	0x00000002 /* use of 160 MHz is allowed */

/*
 * Useful combinations of channel characteristics.
 */
#define IEEE80211_CHAN_A \
	(IEEE80211_CHAN_5GHZ | IEEE80211_CHAN_OFDM)
#define IEEE80211_CHAN_B \
	(IEEE80211_CHAN_2GHZ | IEEE80211_CHAN_CCK)
#define IEEE80211_CHAN_PUREG \
	(IEEE80211_CHAN_2GHZ | IEEE80211_CHAN_OFDM)
#define IEEE80211_CHAN_G \
	(IEEE80211_CHAN_2GHZ | IEEE80211_CHAN_DYN)

#define	IEEE80211_IS_CHAN_A(_c) \
	(((_c)->ic_flags & IEEE80211_CHAN_A) == IEEE80211_CHAN_A)
#define	IEEE80211_IS_CHAN_B(_c) \
	(((_c)->ic_flags & IEEE80211_CHAN_B) == IEEE80211_CHAN_B)
#define	IEEE80211_IS_CHAN_PUREG(_c) \
	(((_c)->ic_flags & IEEE80211_CHAN_PUREG) == IEEE80211_CHAN_PUREG)
#define	IEEE80211_IS_CHAN_G(_c) \
	(((_c)->ic_flags & IEEE80211_CHAN_G) == IEEE80211_CHAN_G)
#define	IEEE80211_IS_CHAN_N(_c) \
	(((_c)->ic_flags & IEEE80211_CHAN_HT) == IEEE80211_CHAN_HT)
#define	IEEE80211_IS_CHAN_AC(_c) \
	(((_c)->ic_flags & IEEE80211_CHAN_VHT) == IEEE80211_CHAN_VHT)

#define	IEEE80211_IS_CHAN_2GHZ(_c) \
	(((_c)->ic_flags & IEEE80211_CHAN_2GHZ) != 0)
#define	IEEE80211_IS_CHAN_5GHZ(_c) \
	(((_c)->ic_flags & IEEE80211_CHAN_5GHZ) != 0)
#define	IEEE80211_IS_CHAN_OFDM(_c) \
	(((_c)->ic_flags & IEEE80211_CHAN_OFDM) != 0)
#define	IEEE80211_IS_CHAN_CCK(_c) \
	(((_c)->ic_flags & IEEE80211_CHAN_CCK) != 0)
#define	IEEE80211_IS_CHAN_XR(_c) \
	(((_c)->ic_flags & IEEE80211_CHAN_XR) != 0)

#define	IEEE80211_CHAN_40MHZ_ALLOWED(_c) \
	(((_c)->ic_flags & IEEE80211_CHAN_40MHZ) != 0)
#define	IEEE80211_CHAN_80MHZ_ALLOWED(_c) \
	(((_c)->ic_xflags & IEEE80211_CHANX_80MHZ) != 0)
#define	IEEE80211_CHAN_160MHZ_ALLOWED(_c) \
	(((_c)->ic_xflags & IEEE80211_CHANX_160MHZ) != 0)

/*
 * EDCA AC parameters.
 */
struct ieee80211_edca_ac_params {
	u_int8_t	ac_ecwmin;	/* CWmin = 2^ECWmin - 1 */
	u_int8_t	ac_ecwmax;	/* CWmax = 2^ECWmax - 1 */
	u_int8_t	ac_aifsn;
	u_int16_t	ac_txoplimit;	/* 32TU */
#define IEEE80211_TXOP_TO_US(txop)	((txop) * 32)

	u_int8_t	ac_acm;
};

extern const struct ieee80211_edca_ac_params
	    ieee80211_edca_table[IEEE80211_MODE_MAX][EDCA_NUM_AC];
extern const struct ieee80211_edca_ac_params
	    ieee80211_qap_edca_table[IEEE80211_MODE_MAX][EDCA_NUM_AC];

#define IEEE80211_DEFRAG_SIZE	3	/* must be >= 3 according to spec */
/*
 * Entry in the fragment cache.
 */
struct ieee80211_defrag {
	struct timeout	df_to;
	struct mbuf	*df_m;
	u_int16_t	df_seq;
	u_int8_t	df_frag;
};

#define IEEE80211_PROTO_NONE	0
#define IEEE80211_PROTO_RSN	(1 << 0)
#define IEEE80211_PROTO_WPA	(1 << 1)

#define	IEEE80211_SCAN_UNLOCKED	0x0
#define	IEEE80211_SCAN_LOCKED	0x1
#define	IEEE80211_SCAN_REQUEST	0x2
#define	IEEE80211_SCAN_RESUME	0x4

#define IEEE80211_GROUP_NKID	6

struct ieee80211_node_switch_bss_arg;

struct ieee80211com {
	struct arpcom		ic_ac;
	LIST_ENTRY(ieee80211com) ic_list;	/* chain of all ieee80211com */
	void			(*ic_recv_mgmt)(struct ieee80211com *,
				    struct mbuf *, struct ieee80211_node *,
				    struct ieee80211_rxinfo *, int);
	int			(*ic_send_mgmt)(struct ieee80211com *,
				    struct ieee80211_node *, int, int, int);
	int			(*ic_newstate)(struct ieee80211com *,
				    enum ieee80211_state, int);
	int			(*ic_newauth)(struct ieee80211com *,
				    struct ieee80211_node *, int, uint16_t);
	void			(*ic_newassoc)(struct ieee80211com *,
				    struct ieee80211_node *, int);
	void			(*ic_node_leave)(struct ieee80211com *,
				    struct ieee80211_node *);
	void			(*ic_updateslot)(struct ieee80211com *);
	void			(*ic_updateedca)(struct ieee80211com *);
	void			(*ic_set_tim)(struct ieee80211com *, int, int);
	int			(*ic_set_key)(struct ieee80211com *,
				    struct ieee80211_node *,
				    struct ieee80211_key *);
	void			(*ic_delete_key)(struct ieee80211com *,
				    struct ieee80211_node *,
				    struct ieee80211_key *);
	int			(*ic_ampdu_tx_start)(struct ieee80211com *,
				    struct ieee80211_node *, u_int8_t);
	void			(*ic_ampdu_tx_stop)(struct ieee80211com *,
				    struct ieee80211_node *, u_int8_t);
	int			(*ic_ampdu_rx_start)(struct ieee80211com *,
				    struct ieee80211_node *, u_int8_t);
	void			(*ic_ampdu_rx_stop)(struct ieee80211com *,
				    struct ieee80211_node *, u_int8_t);
	void			(*ic_updateprot)(struct ieee80211com *);
	void			(*ic_updatechan)(struct ieee80211com *);
	void			(*ic_updatedtim)(struct ieee80211com *);
	int			(*ic_bgscan_start)(struct ieee80211com *);
	void			(*ic_bgscan_done)(struct ieee80211com *,
				    struct ieee80211_node_switch_bss_arg *,
				    size_t);
	struct timeout		ic_bgscan_timeout;
	uint32_t		ic_bgscan_fail;
	u_int8_t		ic_myaddr[IEEE80211_ADDR_LEN];
	struct ieee80211_rateset ic_sup_rates[IEEE80211_MODE_MAX];
	struct ieee80211_channel ic_channels[IEEE80211_CHAN_MAX+1];
	u_char			ic_chan_avail[howmany(IEEE80211_CHAN_MAX,NBBY)];
	u_char			ic_chan_active[howmany(IEEE80211_CHAN_MAX, NBBY)];
	u_char			ic_chan_scan[howmany(IEEE80211_CHAN_MAX,NBBY)];
	struct mbuf_queue	ic_mgtq;
	struct mbuf_queue	ic_pwrsaveq;
	u_int8_t		ic_scan_count;	/* count scans */
	u_int32_t		ic_flags;	/* state flags */
	u_int32_t		ic_xflags;	/* more flags */
	u_int32_t		ic_userflags;	/* yet more flags */
	u_int32_t		ic_caps;	/* capabilities */
	u_int16_t		ic_modecaps;	/* set of mode capabilities */
	u_int16_t		ic_curmode;	/* current mode */
	enum ieee80211_phytype	ic_phytype;	/* XXX wrong for multi-mode */
	enum ieee80211_opmode	ic_opmode;	/* operation mode */
	enum ieee80211_state	ic_state;	/* 802.11 state */
	u_int32_t		*ic_aid_bitmap;
	u_int16_t		ic_max_aid;
	enum ieee80211_protmode	ic_protmode;	/* 802.11g/n protection mode */
	struct ifmedia		ic_media;	/* interface media config */
	caddr_t			ic_rawbpf;	/* packet filter structure */
	struct ieee80211_node	*ic_bss;	/* information for this node */
	struct ieee80211_channel *ic_ibss_chan;
	int			ic_fixed_rate;	/* index to ic_sup_rates[] */
	u_int16_t		ic_rtsthreshold;
	u_int16_t		ic_fragthreshold;
	u_int			ic_scangen;	/* gen# for timeout scan */
	struct ieee80211_node	*(*ic_node_alloc)(struct ieee80211com *);
	void			(*ic_node_free)(struct ieee80211com *,
					struct ieee80211_node *);
	void			(*ic_node_copy)(struct ieee80211com *,
					struct ieee80211_node *,
					const struct ieee80211_node *);
	u_int8_t		(*ic_node_getrssi)(struct ieee80211com *,
					const struct ieee80211_node *);
	int			(*ic_node_checkrssi)(struct ieee80211com *,
					const struct ieee80211_node *);
	u_int8_t		ic_max_rssi;
	struct ieee80211_tree	ic_tree;
	int			ic_nnodes;	/* length of ic_nnodes */
	int			ic_max_nnodes;	/* max length of ic_nnodes */
	u_int16_t		ic_lintval;	/* listen interval */
	int16_t			ic_txpower;	/* tx power setting (dBm) */
	int			ic_bmissthres;	/* beacon miss threshold */
	int			ic_mgt_timer;	/* mgmt timeout */
#ifndef IEEE80211_STA_ONLY
	struct timeout		ic_inact_timeout; /* node inactivity timeout */
	struct timeout		ic_node_cache_timeout;
#endif
	struct task		ic_rtm_80211info_task;
	int			ic_des_esslen;
	u_int8_t		ic_des_essid[IEEE80211_NWID_LEN];
	struct ieee80211_channel *ic_des_chan;	/* desired channel */
	u_int8_t		ic_des_bssid[IEEE80211_ADDR_LEN];
	struct ieee80211_key	ic_nw_keys[IEEE80211_GROUP_NKID];
	int			ic_def_txkey;	/* group data key index */
#define ic_wep_txkey	ic_def_txkey
	int			ic_igtk_kid;	/* IGTK key index */
	u_int32_t		ic_iv;		/* initial vector for wep */
	struct ieee80211_stats	ic_stats;	/* statistics */
	struct timeval		ic_last_merge_print;	/* for rate-limiting
							 * IBSS merge print-outs
							 */
	struct ieee80211_edca_ac_params ic_edca_ac[EDCA_NUM_AC];
	u_int			ic_edca_updtcount;
	u_int16_t		ic_tid_noack;
	u_int8_t		ic_globalcnt[EAPOL_KEY_NONCE_LEN];
	u_int8_t		ic_nonce[EAPOL_KEY_NONCE_LEN];
	u_int8_t		ic_psk[IEEE80211_PMK_LEN];
	struct timeout		ic_rsn_timeout;
	time_t			ic_tkip_micfail;
	u_int64_t		ic_tkip_micfail_last_tsc;
#ifndef IEEE80211_STA_ONLY
	struct timeout		ic_tkip_micfail_timeout;
#endif

	TAILQ_HEAD(, ieee80211_pmk) ic_pmksa;	/* PMKSA cache */
	u_int			ic_rsnprotos;
	u_int			ic_rsnakms;
	u_int			ic_rsnciphers;
	enum ieee80211_cipher	ic_rsngroupcipher;
	enum ieee80211_cipher	ic_rsngroupmgmtcipher;

#ifdef notyet
	struct ieee80211_defrag	ic_defrag[IEEE80211_DEFRAG_SIZE];
	int			ic_defrag_cur;
#endif

	u_int8_t		*ic_tim_bitmap;
	u_int			ic_tim_len;
	u_int			ic_tim_mcast_pending;
	u_int			ic_dtim_period;
	u_int			ic_dtim_count;

	u_int32_t		ic_txbfcaps;
	u_int16_t		ic_htcaps;
	u_int8_t		ic_ampdu_params;
	u_int8_t		ic_sup_mcs[howmany(80, NBBY)];
	u_int16_t		ic_max_rxrate;	/* in Mb/s, 0 <= rate <= 1023 */
	u_int8_t		ic_tx_mcs_set;
	u_int16_t		ic_htxcaps;
	u_int8_t		ic_aselcaps;
	u_int8_t		ic_dialog_token;
	int			ic_fixed_mcs;

	uint32_t		ic_vhtcaps;
	uint16_t		ic_vht_rxmcs;
	uint16_t		ic_vht_rx_max_lgi_mbit_s;
	uint16_t		ic_vht_txmcs;
	uint16_t		ic_vht_tx_max_lgi_mbit_s;

	TAILQ_HEAD(, ieee80211_ess)	 ic_ess;
};
#define	ic_if		ic_ac.ac_if
#define	ic_softc	ic_if.if_softc

/* list of APs we want to automatically use */
/* all data is copied from struct ieee80211com */
struct ieee80211_ess {
	/* nwid */
	int			esslen;
	u_int8_t		essid[IEEE80211_NWID_LEN];

	/* clear/wep/wpa */
	u_int32_t		flags;

	/* nwkey */
	struct ieee80211_key    nw_keys[IEEE80211_GROUP_NKID];
	int			def_txkey;

	/* wpakey */
	u_int8_t		psk[IEEE80211_PMK_LEN];
	u_int			rsnprotos;
	u_int			rsnakms;
	u_int			rsnciphers;
	enum ieee80211_cipher	rsngroupcipher;

	TAILQ_ENTRY(ieee80211_ess) ess_next;
};

#define	IEEE80211_ADDR_EQ(a1,a2)	(memcmp(a1,a2,IEEE80211_ADDR_LEN) == 0)
#define	IEEE80211_ADDR_COPY(dst,src)	memcpy(dst,src,IEEE80211_ADDR_LEN)

/* ic_flags */
#define	IEEE80211_F_ASCAN	0x00000001	/* STATUS: active scan */
#define	IEEE80211_F_SIBSS	0x00000002	/* STATUS: start IBSS */
#define	IEEE80211_F_WEPON	0x00000100	/* CONF: WEP enabled */
#define	IEEE80211_F_IBSSON	0x00000200	/* CONF: IBSS creation enable */
#define	IEEE80211_F_PMGTON	0x00000400	/* CONF: Power mgmt enable */
#define	IEEE80211_F_DESBSSID	0x00000800	/* CONF: des_bssid is set */
#define	IEEE80211_F_ROAMING	0x00002000	/* CONF: roaming enabled */
#define	IEEE80211_F_TXPMGT	0x00018000	/* STATUS: tx power */
#define IEEE80211_F_TXPOW_OFF	0x00000000	/* TX Power: radio disabled */
#define IEEE80211_F_TXPOW_FIXED	0x00008000	/* TX Power: fixed rate */
#define IEEE80211_F_TXPOW_AUTO	0x00010000	/* TX Power: undefined */
#define	IEEE80211_F_SHSLOT	0x00020000	/* STATUS: short slot time */
#define	IEEE80211_F_SHPREAMBLE	0x00040000	/* STATUS: short preamble */
#define IEEE80211_F_QOS		0x00080000	/* CONF: QoS enabled */
#define	IEEE80211_F_USEPROT	0x00100000	/* STATUS: protection enabled */
#define	IEEE80211_F_RSNON	0x00200000	/* CONF: RSN enabled */
#define	IEEE80211_F_PSK		0x00400000	/* CONF: pre-shared key set */
#define IEEE80211_F_COUNTERM	0x00800000	/* STATUS: countermeasures */
#define IEEE80211_F_MFPR	0x01000000	/* CONF: MFP required */
#define	IEEE80211_F_HTON	0x02000000	/* CONF: HT enabled */
#define	IEEE80211_F_PBAR	0x04000000	/* CONF: PBAC required */
#define	IEEE80211_F_BGSCAN	0x08000000	/* STATUS: background scan */
#define IEEE80211_F_AUTO_JOIN	0x10000000	/* CONF: auto-join active */
#define	IEEE80211_F_VHTON	0x20000000	/* CONF: VHT enabled */

/* ic_xflags */
#define	IEEE80211_F_TX_MGMT_ONLY 0x00000001	/* leave data frames on ifq */

/* ic_caps */
#define	IEEE80211_C_WEP		0x00000001	/* CAPABILITY: WEP available */
#define	IEEE80211_C_IBSS	0x00000002	/* CAPABILITY: IBSS available */
#define	IEEE80211_C_PMGT	0x00000004	/* CAPABILITY: Power mgmt */
#define	IEEE80211_C_HOSTAP	0x00000008	/* CAPABILITY: HOSTAP avail */
#define	IEEE80211_C_AHDEMO	0x00000010	/* CAPABILITY: Old Adhoc Demo */
#define	IEEE80211_C_APPMGT	0x00000020	/* CAPABILITY: AP power mgmt */
#define	IEEE80211_C_TXPMGT	0x00000040	/* CAPABILITY: tx power mgmt */
#define	IEEE80211_C_SHSLOT	0x00000080	/* CAPABILITY: short slottime */
#define	IEEE80211_C_SHPREAMBLE	0x00000100	/* CAPABILITY: short preamble */
#define	IEEE80211_C_MONITOR	0x00000200	/* CAPABILITY: monitor mode */
#define IEEE80211_C_SCANALL	0x00000400	/* CAPABILITY: scan all chan */
#define IEEE80211_C_QOS		0x00000800	/* CAPABILITY: QoS avail */
#define IEEE80211_C_RSN		0x00001000	/* CAPABILITY: RSN avail */
#define IEEE80211_C_MFP		0x00002000	/* CAPABILITY: MFP avail */
#define IEEE80211_C_RAWCTL	0x00004000	/* CAPABILITY: raw ctl */
#define IEEE80211_C_SCANALLBAND	0x00008000	/* CAPABILITY: scan all bands */
#define IEEE80211_C_TX_AMPDU	0x00010000	/* CAPABILITY: send A-MPDU */
#define IEEE80211_C_ADDBA_OFFLOAD 0x00020000	/* CAPABILITY: ADDBA offload */

/* flags for ieee80211_fix_rate() */
#define	IEEE80211_F_DOSORT	0x00000001	/* sort rate list */
#define	IEEE80211_F_DOFRATE	0x00000002	/* use fixed rate */
#define	IEEE80211_F_DONEGO	0x00000004	/* calc negotiated rate */
#define	IEEE80211_F_DODEL	0x00000008	/* delete ignore rate */

void	ieee80211_ifattach(struct ifnet *);
void	ieee80211_ifdetach(struct ifnet *);
void	ieee80211_channel_init(struct ifnet *);
void	ieee80211_media_init(struct ifnet *, ifm_change_cb_t, ifm_stat_cb_t);
int	ieee80211_media_change(struct ifnet *);
void	ieee80211_media_status(struct ifnet *, struct ifmediareq *);
int	ieee80211_ioctl(struct ifnet *, u_long, caddr_t);
int	ieee80211_get_rate(struct ieee80211com *);
void	ieee80211_watchdog(struct ifnet *);
int	ieee80211_fix_rate(struct ieee80211com *, struct ieee80211_node *, int);
uint64_t	ieee80211_rate2media(struct ieee80211com *, int,
		    enum ieee80211_phymode);
int	ieee80211_media2rate(uint64_t);
uint64_t	ieee80211_mcs2media(struct ieee80211com *, int,
		    enum ieee80211_phymode);
int	ieee80211_media2mcs(uint64_t);
u_int8_t ieee80211_rate2plcp(u_int8_t, enum ieee80211_phymode);
u_int8_t ieee80211_plcp2rate(u_int8_t, enum ieee80211_phymode);
u_int	ieee80211_mhz2ieee(u_int, u_int);
u_int	ieee80211_chan2ieee(struct ieee80211com *,
		const struct ieee80211_channel *);
u_int	ieee80211_ieee2mhz(u_int, u_int);
int	ieee80211_min_basic_rate(struct ieee80211com *);
int	ieee80211_max_basic_rate(struct ieee80211com *);
int	ieee80211_setmode(struct ieee80211com *, enum ieee80211_phymode);
enum ieee80211_phymode ieee80211_next_mode(struct ifnet *);
void	ieee80211_disable_wep(struct ieee80211com *); 
void	ieee80211_disable_rsn(struct ieee80211com *); 
int	ieee80211_add_ess(struct ieee80211com *, struct ieee80211_join *);
void	ieee80211_del_ess(struct ieee80211com *, char *, int, int);
void	ieee80211_set_ess(struct ieee80211com *, struct ieee80211_ess *,
	    struct ieee80211_node *);
void	ieee80211_deselect_ess(struct ieee80211com *);
struct ieee80211_ess *ieee80211_get_ess(struct ieee80211com *, const char *, int);
void	ieee80211_begin_bgscan(struct ifnet *);

extern	int ieee80211_cache_size;

#endif /* _NET80211_IEEE80211_VAR_H_ */
