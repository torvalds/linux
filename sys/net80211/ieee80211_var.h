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
#ifndef _NET80211_IEEE80211_VAR_H_
#define _NET80211_IEEE80211_VAR_H_

/*
 * Definitions for IEEE 802.11 drivers.
 */
/* NB: portability glue must go first */
#if defined(__NetBSD__)
#include <net80211/ieee80211_netbsd.h>
#elif defined(__FreeBSD__)
#include <net80211/ieee80211_freebsd.h>
#elif defined(__linux__)
#include <net80211/ieee80211_linux.h>
#else
#error	"No support for your operating system!"
#endif

#include <net80211/_ieee80211.h>
#include <net80211/ieee80211.h>
#include <net80211/ieee80211_ageq.h>
#include <net80211/ieee80211_crypto.h>
#include <net80211/ieee80211_dfs.h>
#include <net80211/ieee80211_ioctl.h>		/* for ieee80211_stats */
#include <net80211/ieee80211_phy.h>
#include <net80211/ieee80211_power.h>
#include <net80211/ieee80211_node.h>
#include <net80211/ieee80211_proto.h>
#include <net80211/ieee80211_radiotap.h>
#include <net80211/ieee80211_scan.h>

#define	IEEE80211_TXPOWER_MAX	100	/* .5 dBm (XXX units?) */
#define	IEEE80211_TXPOWER_MIN	0	/* kill radio */

#define	IEEE80211_DTIM_DEFAULT	1	/* default DTIM period */
#define	IEEE80211_BINTVAL_DEFAULT 100	/* default beacon interval (TU's) */

#define	IEEE80211_BMISS_MAX	2	/* maximum consecutive bmiss allowed */
#define	IEEE80211_HWBMISS_DEFAULT 7	/* h/w bmiss threshold (beacons) */

#define	IEEE80211_BGSCAN_INTVAL_MIN	15	/* min bg scan intvl (secs) */
#define	IEEE80211_BGSCAN_INTVAL_DEFAULT	(5*60)	/* default bg scan intvl */

#define	IEEE80211_BGSCAN_IDLE_MIN	100	/* min idle time (ms) */
#define	IEEE80211_BGSCAN_IDLE_DEFAULT	250	/* default idle time (ms) */

#define	IEEE80211_SCAN_VALID_MIN	10	/* min scan valid time (secs) */
#define	IEEE80211_SCAN_VALID_DEFAULT	60	/* default scan valid time */

#define	IEEE80211_PS_SLEEP	0x1	/* STA is in power saving mode */
#define	IEEE80211_PS_MAX_QUEUE	50	/* maximum saved packets */

#define	IEEE80211_FIXED_RATE_NONE	0xff
#define	IEEE80211_TXMAX_DEFAULT		6	/* default ucast max retries */

#define	IEEE80211_RTS_DEFAULT		IEEE80211_RTS_MAX
#define	IEEE80211_FRAG_DEFAULT		IEEE80211_FRAG_MAX

#define	IEEE80211_MS_TO_TU(x)	(((x) * 1000) / 1024)
#define	IEEE80211_TU_TO_MS(x)	(((x) * 1024) / 1000)
/* XXX TODO: cap this at 1, in case hz is not 1000 */
#define	IEEE80211_TU_TO_TICKS(x)(((uint64_t)(x) * 1024 * hz) / (1000 * 1000))

/*
 * Technically, vhtflags may be 0 /and/ 11ac is enabled.
 * At some point ic should just grow a flag somewhere that
 * says that VHT is supported - and then this macro can be
 * changed.
 */
#define	IEEE80211_CONF_VHT(ic)			\
	    ((ic)->ic_flags_ext & IEEE80211_FEXT_VHT)

#define	IEEE80211_CONF_SEQNO_OFFLOAD(ic)	\
	    ((ic)->ic_flags_ext & IEEE80211_FEXT_SEQNO_OFFLOAD)
#define	IEEE80211_CONF_FRAG_OFFLOAD(ic)	\
	    ((ic)->ic_flags_ext & IEEE80211_FEXT_FRAG_OFFLOAD)

/*
 * 802.11 control state is split into a common portion that maps
 * 1-1 to a physical device and one or more "Virtual AP's" (VAP)
 * that are bound to an ieee80211com instance and share a single
 * underlying device.  Each VAP has a corresponding OS device
 * entity through which traffic flows and that applications use
 * for issuing ioctls, etc.
 */

/*
 * Data common to one or more virtual AP's.  State shared by
 * the underlying device and the net80211 layer is exposed here;
 * e.g. device-specific callbacks.
 */
struct ieee80211vap;
typedef void (*ieee80211vap_attach)(struct ieee80211vap *);

struct ieee80211_appie {
	uint16_t		ie_len;		/* size of ie_data */
	uint8_t			ie_data[];	/* user-specified IE's */
};

struct ieee80211_tdma_param;
struct ieee80211_rate_table;
struct ieee80211_tx_ampdu;
struct ieee80211_rx_ampdu;
struct ieee80211_superg;
struct ieee80211_frame;

struct ieee80211com {
	void			*ic_softc;	/* driver softc */
	const char		*ic_name;	/* usually device name */
	ieee80211_com_lock_t	ic_comlock;	/* state update lock */
	ieee80211_tx_lock_t	ic_txlock;	/* ic/vap TX lock */
	ieee80211_ff_lock_t	ic_fflock;	/* stageq/ni_tx_superg lock */
	LIST_ENTRY(ieee80211com)   ic_next;	/* on global list */
	TAILQ_HEAD(, ieee80211vap) ic_vaps;	/* list of vap instances */
	int			ic_headroom;	/* driver tx headroom needs */
	enum ieee80211_phytype	ic_phytype;	/* XXX wrong for multi-mode */
	enum ieee80211_opmode	ic_opmode;	/* operation mode */
	struct callout		ic_inact;	/* inactivity processing */
	struct taskqueue	*ic_tq;		/* deferred state thread */
	struct task		ic_parent_task;	/* deferred parent processing */
	struct task		ic_promisc_task;/* deferred promisc update */
	struct task		ic_mcast_task;	/* deferred mcast update */
	struct task		ic_chan_task;	/* deferred channel change */
	struct task		ic_bmiss_task;	/* deferred beacon miss hndlr */
	struct task		ic_chw_task;	/* deferred HT CHW update */
	struct task		ic_restart_task; /* deferred device restart */

	counter_u64_t		ic_ierrors;	/* input errors */
	counter_u64_t		ic_oerrors;	/* output errors */

	uint32_t		ic_flags;	/* state flags */
	uint32_t		ic_flags_ext;	/* extended state flags */
	uint32_t		ic_flags_ht;	/* HT state flags */
	uint32_t		ic_flags_ven;	/* vendor state flags */
	uint32_t		ic_caps;	/* capabilities */
	uint32_t		ic_htcaps;	/* HT capabilities */
	uint32_t		ic_htextcaps;	/* HT extended capabilities */
	uint32_t		ic_cryptocaps;	/* crypto capabilities */
						/* set of mode capabilities */
	uint8_t			ic_modecaps[IEEE80211_MODE_BYTES];
	uint8_t			ic_promisc;	/* vap's needing promisc mode */
	uint8_t			ic_allmulti;	/* vap's needing all multicast*/
	uint8_t			ic_nrunning;	/* vap's marked running */
	uint8_t			ic_curmode;	/* current mode */
	uint8_t			ic_macaddr[IEEE80211_ADDR_LEN];
	uint16_t		ic_bintval;	/* beacon interval */
	uint16_t		ic_lintval;	/* listen interval */
	uint16_t		ic_holdover;	/* PM hold over duration */
	uint16_t		ic_txpowlimit;	/* global tx power limit */
	struct ieee80211_rateset ic_sup_rates[IEEE80211_MODE_MAX];
	struct ieee80211_htrateset ic_sup_htrates;

	/*
	 * Channel state:
	 *
	 * ic_channels is the set of available channels for the device;
	 *    it is setup by the driver
	 * ic_nchans is the number of valid entries in ic_channels
	 * ic_chan_avail is a bit vector of these channels used to check
	 *    whether a channel is available w/o searching the channel table.
	 * ic_chan_active is a (potentially) constrained subset of
	 *    ic_chan_avail that reflects any mode setting or user-specified
	 *    limit on the set of channels to use/scan
	 * ic_curchan is the current channel the device is set to; it may
	 *    be different from ic_bsschan when we are off-channel scanning
	 *    or otherwise doing background work
	 * ic_bsschan is the channel selected for operation; it may
	 *    be undefined (IEEE80211_CHAN_ANYC)
	 * ic_prevchan is a cached ``previous channel'' used to optimize
	 *    lookups when switching back+forth between two channels
	 *    (e.g. for dynamic turbo)
	 */
	int			ic_nchans;	/* # entries in ic_channels */
	struct ieee80211_channel ic_channels[IEEE80211_CHAN_MAX];
	uint8_t			ic_chan_avail[IEEE80211_CHAN_BYTES];
	uint8_t			ic_chan_active[IEEE80211_CHAN_BYTES];
	uint8_t			ic_chan_scan[IEEE80211_CHAN_BYTES];
	struct ieee80211_channel *ic_curchan;	/* current channel */
	const struct ieee80211_rate_table *ic_rt; /* table for ic_curchan */
	struct ieee80211_channel *ic_bsschan;	/* bss channel */
	struct ieee80211_channel *ic_prevchan;	/* previous channel */
	struct ieee80211_regdomain ic_regdomain;/* regulatory data */
	struct ieee80211_appie	*ic_countryie;	/* calculated country ie */
	struct ieee80211_channel *ic_countryie_chan;

	/* 802.11h/DFS state */
	struct ieee80211_channel *ic_csa_newchan;/* channel for doing CSA */
	short			ic_csa_mode;	/* mode for doing CSA */
	short			ic_csa_count;	/* count for doing CSA */
	struct ieee80211_dfs_state ic_dfs;	/* DFS state */

	struct ieee80211_scan_state *ic_scan;	/* scan state */
	struct ieee80211_scan_methods *ic_scan_methods;	/* scan methods */
	int			ic_lastdata;	/* time of last data frame */
	int			ic_lastscan;	/* time last scan completed */

	/* NB: this is the union of all vap stations/neighbors */
	int			ic_max_keyix;	/* max h/w key index */
	struct ieee80211_node_table ic_sta;	/* stations/neighbors */
	struct ieee80211_ageq	ic_stageq;	/* frame staging queue */
	uint32_t		ic_hash_key;	/* random key for mac hash */

	/* XXX multi-bss: split out common/vap parts */
	struct ieee80211_wme_state ic_wme;	/* WME/WMM state */

	/* XXX multi-bss: can per-vap be done/make sense? */
	enum ieee80211_protmode	ic_protmode;	/* 802.11g protection mode */
	uint16_t		ic_nonerpsta;	/* # non-ERP stations */
	uint16_t		ic_longslotsta;	/* # long slot time stations */
	uint16_t		ic_sta_assoc;	/* stations associated */
	uint16_t		ic_ht_sta_assoc;/* HT stations associated */
	uint16_t		ic_ht40_sta_assoc;/* HT40 stations associated */
	uint8_t			ic_curhtprotmode;/* HTINFO bss state */
	enum ieee80211_protmode	ic_htprotmode;	/* HT protection mode */
	int			ic_lastnonerp;	/* last time non-ERP sta noted*/
	int			ic_lastnonht;	/* last time non-HT sta noted */
	uint8_t			ic_rxstream;    /* # RX streams */
	uint8_t			ic_txstream;    /* # TX streams */

	/* VHT information */
	uint32_t		ic_vhtcaps;	/* VHT capabilities */
	uint32_t		ic_vhtextcaps;	/* VHT extended capabilities (TODO) */
	struct ieee80211_vht_mcs_info	ic_vht_mcsinfo; /* Support TX/RX VHT MCS */
	uint32_t		ic_flags_vht;	/* VHT state flags */
	uint32_t		ic_vht_spare[3];

	/* optional state for Atheros SuperG protocol extensions */
	struct ieee80211_superg	*ic_superg;

	/* radiotap handling */
	struct ieee80211_radiotap_header *ic_th;/* tx radiotap headers */
	void			*ic_txchan;	/* channel state in ic_th */
	struct ieee80211_radiotap_header *ic_rh;/* rx radiotap headers */
	void			*ic_rxchan;	/* channel state in ic_rh */
	int			ic_montaps;	/* active monitor mode taps */

	/* virtual ap create/delete */
	struct ieee80211vap*	(*ic_vap_create)(struct ieee80211com *,
				    const char [IFNAMSIZ], int,
				    enum ieee80211_opmode, int,
				    const uint8_t [IEEE80211_ADDR_LEN],
				    const uint8_t [IEEE80211_ADDR_LEN]);
	void			(*ic_vap_delete)(struct ieee80211vap *);
	/* device specific ioctls */
	int			(*ic_ioctl)(struct ieee80211com *,
				    u_long, void *);
	/* start/stop device */
	void			(*ic_parent)(struct ieee80211com *);
	/* operating mode attachment */
	ieee80211vap_attach	ic_vattach[IEEE80211_OPMODE_MAX];
	/* return hardware/radio capabilities */
	void			(*ic_getradiocaps)(struct ieee80211com *,
				    int, int *, struct ieee80211_channel []);
	/* check and/or prepare regdomain state change */
	int			(*ic_setregdomain)(struct ieee80211com *,
				    struct ieee80211_regdomain *,
				    int, struct ieee80211_channel []);

	int			(*ic_set_quiet)(struct ieee80211_node *,
				    u_int8_t *quiet_elm);

	/* regular transmit */
	int			(*ic_transmit)(struct ieee80211com *,
				    struct mbuf *);
	/* send/recv 802.11 management frame */
	int			(*ic_send_mgmt)(struct ieee80211_node *,
				     int, int);
	/* send raw 802.11 frame */
	int			(*ic_raw_xmit)(struct ieee80211_node *,
				    struct mbuf *,
				    const struct ieee80211_bpf_params *);
	/* update device state for 802.11 slot time change */
	void			(*ic_updateslot)(struct ieee80211com *);
	/* handle multicast state changes */
	void			(*ic_update_mcast)(struct ieee80211com *);
	/* handle promiscuous mode changes */
	void			(*ic_update_promisc)(struct ieee80211com *);
	/* new station association callback/notification */
	void			(*ic_newassoc)(struct ieee80211_node *, int);
	/* TDMA update notification */
	void			(*ic_tdma_update)(struct ieee80211_node *,
				    const struct ieee80211_tdma_param *, int);
	/* node state management */
	struct ieee80211_node*	(*ic_node_alloc)(struct ieee80211vap *,
				    const uint8_t [IEEE80211_ADDR_LEN]);
	void			(*ic_node_free)(struct ieee80211_node *);
	void			(*ic_node_cleanup)(struct ieee80211_node *);
	void			(*ic_node_age)(struct ieee80211_node *);
	void			(*ic_node_drain)(struct ieee80211_node *);
	int8_t			(*ic_node_getrssi)(const struct ieee80211_node*);
	void			(*ic_node_getsignal)(const struct ieee80211_node*,
				    int8_t *, int8_t *);
	void			(*ic_node_getmimoinfo)(
				    const struct ieee80211_node*,
				    struct ieee80211_mimo_info *);
	/* scanning support */
	void			(*ic_scan_start)(struct ieee80211com *);
	void			(*ic_scan_end)(struct ieee80211com *);
	void			(*ic_set_channel)(struct ieee80211com *);
	void			(*ic_scan_curchan)(struct ieee80211_scan_state *,
				    unsigned long);
	void			(*ic_scan_mindwell)(struct ieee80211_scan_state *);

	/*
	 * 802.11n ADDBA support.  A simple/generic implementation
	 * of A-MPDU tx aggregation is provided; the driver may
	 * override these methods to provide their own support.
	 * A-MPDU rx re-ordering happens automatically if the
	 * driver passes out-of-order frames to ieee80211_input
	 * from an assocated HT station.
	 */
	int			(*ic_recv_action)(struct ieee80211_node *,
				    const struct ieee80211_frame *,
				    const uint8_t *frm, const uint8_t *efrm);
	int			(*ic_send_action)(struct ieee80211_node *,
				    int category, int action, void *);
	/* check if A-MPDU should be enabled this station+ac */
	int			(*ic_ampdu_enable)(struct ieee80211_node *,
				    struct ieee80211_tx_ampdu *);
	/* start/stop doing A-MPDU tx aggregation for a station */
	int			(*ic_addba_request)(struct ieee80211_node *,
				    struct ieee80211_tx_ampdu *,
				    int dialogtoken, int baparamset,
				    int batimeout);
	int			(*ic_addba_response)(struct ieee80211_node *,
				    struct ieee80211_tx_ampdu *,
				    int status, int baparamset, int batimeout);
	void			(*ic_addba_stop)(struct ieee80211_node *,
				    struct ieee80211_tx_ampdu *);
	void			(*ic_addba_response_timeout)(struct ieee80211_node *,
				    struct ieee80211_tx_ampdu *);
	/* BAR response received */
	void			(*ic_bar_response)(struct ieee80211_node *,
				    struct ieee80211_tx_ampdu *, int status);
	/* start/stop doing A-MPDU rx processing for a station */
	int			(*ic_ampdu_rx_start)(struct ieee80211_node *,
				    struct ieee80211_rx_ampdu *, int baparamset,
				    int batimeout, int baseqctl);
	void			(*ic_ampdu_rx_stop)(struct ieee80211_node *,
				    struct ieee80211_rx_ampdu *);

	/* The channel width has changed (20<->2040) */
	void			(*ic_update_chw)(struct ieee80211com *);

	uint64_t		ic_spare[7];
};

struct ieee80211_aclator;
struct ieee80211_tdma_state;
struct ieee80211_mesh_state;
struct ieee80211_hwmp_state;

struct ieee80211vap {
	struct ifmedia		iv_media;	/* interface media config */
	struct ifnet		*iv_ifp;	/* associated device */
	struct bpf_if		*iv_rawbpf;	/* packet filter structure */
	struct sysctl_ctx_list	*iv_sysctl;	/* dynamic sysctl context */
	struct sysctl_oid	*iv_oid;	/* net.wlan.X sysctl oid */

	TAILQ_ENTRY(ieee80211vap) iv_next;	/* list of vap instances */
	struct ieee80211com	*iv_ic;		/* back ptr to common state */
	/* MAC address: ifp or ic */
	uint8_t			iv_myaddr[IEEE80211_ADDR_LEN];
	uint32_t		iv_debug;	/* debug msg flags */
	struct ieee80211_stats	iv_stats;	/* statistics */

	uint32_t		iv_flags;	/* state flags */
	uint32_t		iv_flags_ext;	/* extended state flags */
	uint32_t		iv_flags_ht;	/* HT state flags */
	uint32_t		iv_flags_ven;	/* vendor state flags */
	uint32_t		iv_ifflags;	/* ifnet flags */
	uint32_t		iv_caps;	/* capabilities */
	uint32_t		iv_htcaps;	/* HT capabilities */
	uint32_t		iv_htextcaps;	/* HT extended capabilities */
	uint32_t		iv_com_state;	/* com usage / detached flag */
	enum ieee80211_opmode	iv_opmode;	/* operation mode */
	enum ieee80211_state	iv_state;	/* state machine state */
	enum ieee80211_state	iv_nstate;	/* pending state */
	int			iv_nstate_arg;	/* pending state arg */
	struct task		iv_nstate_task;	/* deferred state processing */
	struct task		iv_swbmiss_task;/* deferred iv_bmiss call */
	struct callout		iv_mgtsend;	/* mgmt frame response timer */
						/* inactivity timer settings */
	int			iv_inact_init;	/* setting for new station */
	int			iv_inact_auth;	/* auth but not assoc setting */
	int			iv_inact_run;	/* authorized setting */
	int			iv_inact_probe;	/* inactive probe time */

	/* VHT flags */
	uint32_t		iv_flags_vht;	/* VHT state flags */
	uint32_t		iv_vhtcaps;	/* VHT capabilities */
	uint32_t		iv_vhtextcaps;	/* VHT extended capabilities (TODO) */
	struct ieee80211_vht_mcs_info	iv_vht_mcsinfo;
	uint32_t		iv_vht_spare[4];

	int			iv_des_nssid;	/* # desired ssids */
	struct ieee80211_scan_ssid iv_des_ssid[1];/* desired ssid table */
	uint8_t			iv_des_bssid[IEEE80211_ADDR_LEN];
	struct ieee80211_channel *iv_des_chan;	/* desired channel */
	uint16_t		iv_des_mode;	/* desired mode */
	int			iv_nicknamelen;	/* XXX junk */
	uint8_t			iv_nickname[IEEE80211_NWID_LEN];
	u_int			iv_bgscanidle;	/* bg scan idle threshold */
	u_int			iv_bgscanintvl;	/* bg scan min interval */
	u_int			iv_scanvalid;	/* scan cache valid threshold */
	u_int			iv_scanreq_duration;
	u_int			iv_scanreq_mindwell;
	u_int			iv_scanreq_maxdwell;
	uint16_t		iv_scanreq_flags;/* held scan request params */
	uint8_t			iv_scanreq_nssid;
	struct ieee80211_scan_ssid iv_scanreq_ssid[IEEE80211_SCAN_MAX_SSID];
	/* sta-mode roaming state */
	enum ieee80211_roamingmode iv_roaming;	/* roaming mode */
	struct ieee80211_roamparam iv_roamparms[IEEE80211_MODE_MAX];

	uint8_t			iv_bmissthreshold;
	uint8_t			iv_bmiss_count;	/* current beacon miss count */
	int			iv_bmiss_max;	/* max bmiss before scan */
	uint16_t		iv_swbmiss_count;/* beacons in last period */
	uint16_t		iv_swbmiss_period;/* s/w bmiss period */
	struct callout		iv_swbmiss;	/* s/w beacon miss timer */

	int			iv_ampdu_rxmax;	/* A-MPDU rx limit (bytes) */
	int			iv_ampdu_density;/* A-MPDU density */
	int			iv_ampdu_limit;	/* A-MPDU tx limit (bytes) */
	int			iv_amsdu_limit;	/* A-MSDU tx limit (bytes) */
	u_int			iv_ampdu_mintraffic[WME_NUM_AC];

	struct ieee80211_beacon_offsets iv_bcn_off;
	uint32_t		*iv_aid_bitmap;	/* association id map */
	uint16_t		iv_max_aid;
	uint16_t		iv_sta_assoc;	/* stations associated */
	uint16_t		iv_ps_sta;	/* stations in power save */
	uint16_t		iv_ps_pending;	/* ps sta's w/ pending frames */
	uint16_t		iv_txseq;	/* mcast xmit seq# space */
	uint16_t		iv_tim_len;	/* ic_tim_bitmap size (bytes) */
	uint8_t			*iv_tim_bitmap;	/* power-save stations w/ data*/
	uint8_t			iv_dtim_period;	/* DTIM period */
	uint8_t			iv_dtim_count;	/* DTIM count from last bcn */
						/* set/unset aid pwrsav state */
	uint8_t			iv_quiet;	/* Quiet Element */
	uint8_t			iv_quiet_count;	/* constant count for Quiet Element */
	uint8_t			iv_quiet_count_value;	/* variable count for Quiet Element */
	uint8_t			iv_quiet_period;	/* period for Quiet Element */
	uint16_t		iv_quiet_duration;	/* duration for Quiet Element */
	uint16_t		iv_quiet_offset;	/* offset for Quiet Element */
	int			iv_csa_count;	/* count for doing CSA */

	struct ieee80211_node	*iv_bss;	/* information for this node */
	struct ieee80211_txparam iv_txparms[IEEE80211_MODE_MAX];
	uint16_t		iv_rtsthreshold;
	uint16_t		iv_fragthreshold;
	int			iv_inact_timer;	/* inactivity timer wait */
	/* application-specified IE's to attach to mgt frames */
	struct ieee80211_appie	*iv_appie_beacon;
	struct ieee80211_appie	*iv_appie_probereq;
	struct ieee80211_appie	*iv_appie_proberesp;
	struct ieee80211_appie	*iv_appie_assocreq;
	struct ieee80211_appie	*iv_appie_assocresp;
	struct ieee80211_appie	*iv_appie_wpa;
	uint8_t			*iv_wpa_ie;
	uint8_t			*iv_rsn_ie;

	/* Key management */
	uint16_t		iv_max_keyix;	/* max h/w key index */
	ieee80211_keyix		iv_def_txkey;	/* default/group tx key index */
	struct ieee80211_key	iv_nw_keys[IEEE80211_WEP_NKID];
	int			(*iv_key_alloc)(struct ieee80211vap *,
				    struct ieee80211_key *,
				    ieee80211_keyix *, ieee80211_keyix *);
	int			(*iv_key_delete)(struct ieee80211vap *, 
				    const struct ieee80211_key *);
	int			(*iv_key_set)(struct ieee80211vap *,
				    const struct ieee80211_key *);
	void			(*iv_key_update_begin)(struct ieee80211vap *);
	void			(*iv_key_update_end)(struct ieee80211vap *);
	void			(*iv_update_deftxkey)(struct ieee80211vap *,
				    ieee80211_keyix deftxkey);

	const struct ieee80211_authenticator *iv_auth; /* authenticator glue */
	void			*iv_ec;		/* private auth state */

	const struct ieee80211_aclator *iv_acl;	/* acl glue */
	void			*iv_as;		/* private aclator state */

	const struct ieee80211_ratectl *iv_rate;
	void			*iv_rs;		/* private ratectl state */

	struct ieee80211_tdma_state *iv_tdma;	/* tdma state */
	struct ieee80211_mesh_state *iv_mesh;	/* MBSS state */
	struct ieee80211_hwmp_state *iv_hwmp;	/* HWMP state */

	/* operate-mode detach hook */
	void			(*iv_opdetach)(struct ieee80211vap *);
	/* receive processing */
	int			(*iv_input)(struct ieee80211_node *,
				    struct mbuf *,
				    const struct ieee80211_rx_stats *,
				    int, int);
	void			(*iv_recv_mgmt)(struct ieee80211_node *,
				    struct mbuf *, int,
				    const struct ieee80211_rx_stats *,
				    int, int);
	void			(*iv_recv_ctl)(struct ieee80211_node *,
				    struct mbuf *, int);
	void			(*iv_deliver_data)(struct ieee80211vap *,
				    struct ieee80211_node *, struct mbuf *);
#if 0
	/* send processing */
	int			(*iv_send_mgmt)(struct ieee80211_node *,
				     int, int);
#endif
	/* beacon miss processing */
	void			(*iv_bmiss)(struct ieee80211vap *);
	/* reset device state after 802.11 parameter/state change */
	int			(*iv_reset)(struct ieee80211vap *, u_long);
	/* [schedule] beacon frame update */
	void			(*iv_update_beacon)(struct ieee80211vap *, int);
	/* power save handling */
	void			(*iv_update_ps)(struct ieee80211vap *, int);
	int			(*iv_set_tim)(struct ieee80211_node *, int);
	void			(*iv_node_ps)(struct ieee80211_node *, int);
	void			(*iv_sta_ps)(struct ieee80211vap *, int);
	void			(*iv_recv_pspoll)(struct ieee80211_node *,
				    struct mbuf *);

	/* state machine processing */
	int			(*iv_newstate)(struct ieee80211vap *,
				    enum ieee80211_state, int);
	/* 802.3 output method for raw frame xmit */
	int			(*iv_output)(struct ifnet *, struct mbuf *,
				    const struct sockaddr *, struct route *);

	int			(*iv_wme_update)(struct ieee80211vap *,
				    const struct wmeParams *wme_params);
	struct task		iv_wme_task;	/* deferred VAP WME update */

	uint64_t		iv_spare[6];
};
MALLOC_DECLARE(M_80211_VAP);

#define	IEEE80211_ADDR_EQ(a1,a2)	(memcmp(a1,a2,IEEE80211_ADDR_LEN) == 0)
#define	IEEE80211_ADDR_COPY(dst,src)	memcpy(dst,src,IEEE80211_ADDR_LEN)

/* ic_flags/iv_flags */
#define	IEEE80211_F_TURBOP	0x00000001	/* CONF: ATH Turbo enabled*/
#define	IEEE80211_F_COMP	0x00000002	/* CONF: ATH comp enabled */
#define	IEEE80211_F_FF		0x00000004	/* CONF: ATH FF enabled */
#define	IEEE80211_F_BURST	0x00000008	/* CONF: bursting enabled */
/* NB: this is intentionally setup to be IEEE80211_CAPINFO_PRIVACY */
#define	IEEE80211_F_PRIVACY	0x00000010	/* CONF: privacy enabled */
#define	IEEE80211_F_PUREG	0x00000020	/* CONF: 11g w/o 11b sta's */
#define	IEEE80211_F_SCAN	0x00000080	/* STATUS: scanning */
/* 0x00000300 reserved */
/* NB: this is intentionally setup to be IEEE80211_CAPINFO_SHORT_SLOTTIME */
#define	IEEE80211_F_SHSLOT	0x00000400	/* STATUS: use short slot time*/
#define	IEEE80211_F_PMGTON	0x00000800	/* CONF: Power mgmt enable */
#define	IEEE80211_F_DESBSSID	0x00001000	/* CONF: des_bssid is set */
#define	IEEE80211_F_WME		0x00002000	/* CONF: enable WME use */
#define	IEEE80211_F_BGSCAN	0x00004000	/* CONF: bg scan enabled (???)*/
#define	IEEE80211_F_SWRETRY	0x00008000	/* CONF: sw tx retry enabled */
/* 0x00030000 reserved */
#define	IEEE80211_F_SHPREAMBLE	0x00040000	/* STATUS: use short preamble */
#define	IEEE80211_F_DATAPAD	0x00080000	/* CONF: do alignment pad */
#define	IEEE80211_F_USEPROT	0x00100000	/* STATUS: protection enabled */
#define	IEEE80211_F_USEBARKER	0x00200000	/* STATUS: use barker preamble*/
#define	IEEE80211_F_CSAPENDING	0x00400000	/* STATUS: chan switch pending*/
#define	IEEE80211_F_WPA1	0x00800000	/* CONF: WPA enabled */
#define	IEEE80211_F_WPA2	0x01000000	/* CONF: WPA2 enabled */
#define	IEEE80211_F_WPA		0x01800000	/* CONF: WPA/WPA2 enabled */
#define	IEEE80211_F_DROPUNENC	0x02000000	/* CONF: drop unencrypted */
#define	IEEE80211_F_COUNTERM	0x04000000	/* CONF: TKIP countermeasures */
#define	IEEE80211_F_HIDESSID	0x08000000	/* CONF: hide SSID in beacon */
#define	IEEE80211_F_NOBRIDGE	0x10000000	/* CONF: dis. internal bridge */
#define	IEEE80211_F_PCF		0x20000000	/* CONF: PCF enabled */
#define	IEEE80211_F_DOTH	0x40000000	/* CONF: 11h enabled */
#define	IEEE80211_F_DWDS	0x80000000	/* CONF: Dynamic WDS enabled */

#define	IEEE80211_F_BITS \
	"\20\1TURBOP\2COMP\3FF\4BURST\5PRIVACY\6PUREG\10SCAN" \
	"\13SHSLOT\14PMGTON\15DESBSSID\16WME\17BGSCAN\20SWRETRY" \
	"\23SHPREAMBLE\24DATAPAD\25USEPROT\26USERBARKER\27CSAPENDING" \
	"\30WPA1\31WPA2\32DROPUNENC\33COUNTERM\34HIDESSID\35NOBRIDG\36PCF" \
	"\37DOTH\40DWDS"

/* Atheros protocol-specific flags */
#define	IEEE80211_F_ATHEROS \
	(IEEE80211_F_FF | IEEE80211_F_COMP | IEEE80211_F_TURBOP)
/* Check if an Atheros capability was negotiated for use */
#define	IEEE80211_ATH_CAP(vap, ni, bit) \
	((vap)->iv_flags & (ni)->ni_ath_flags & (bit))

/* ic_flags_ext/iv_flags_ext */
#define	IEEE80211_FEXT_INACT	 0x00000002	/* CONF: sta inact handling */
#define	IEEE80211_FEXT_SCANWAIT	 0x00000004	/* STATUS: awaiting scan */
/* 0x00000006 reserved */
#define	IEEE80211_FEXT_BGSCAN	 0x00000008	/* STATUS: complete bgscan */
#define	IEEE80211_FEXT_WPS	 0x00000010	/* CONF: WPS enabled */
#define	IEEE80211_FEXT_TSN 	 0x00000020	/* CONF: TSN enabled */
#define	IEEE80211_FEXT_SCANREQ	 0x00000040	/* STATUS: scan req params */
#define	IEEE80211_FEXT_RESUME	 0x00000080	/* STATUS: start on resume */
#define	IEEE80211_FEXT_4ADDR	 0x00000100	/* CONF: apply 4-addr encap */
#define	IEEE80211_FEXT_NONERP_PR 0x00000200	/* STATUS: non-ERP sta present*/
#define	IEEE80211_FEXT_SWBMISS	 0x00000400	/* CONF: do bmiss in s/w */
#define	IEEE80211_FEXT_DFS	 0x00000800	/* CONF: DFS enabled */
#define	IEEE80211_FEXT_DOTD	 0x00001000	/* CONF: 11d enabled */
#define	IEEE80211_FEXT_STATEWAIT 0x00002000	/* STATUS: awaiting state chg */
#define	IEEE80211_FEXT_REINIT	 0x00004000	/* STATUS: INIT state first */
#define	IEEE80211_FEXT_BPF	 0x00008000	/* STATUS: BPF tap present */
/* NB: immutable: should be set only when creating a vap */
#define	IEEE80211_FEXT_WDSLEGACY 0x00010000	/* CONF: legacy WDS operation */
#define	IEEE80211_FEXT_PROBECHAN 0x00020000	/* CONF: probe passive channel*/
#define	IEEE80211_FEXT_UNIQMAC	 0x00040000	/* CONF: user or computed mac */
#define	IEEE80211_FEXT_SCAN_OFFLOAD	0x00080000	/* CONF: scan is fully offloaded */
#define	IEEE80211_FEXT_SEQNO_OFFLOAD	0x00100000	/* CONF: driver does seqno insertion/allocation */
#define	IEEE80211_FEXT_FRAG_OFFLOAD	0x00200000	/* CONF: hardware does 802.11 fragmentation + assignment */
#define	IEEE80211_FEXT_VHT	0x00400000	/* CONF: VHT support */
#define	IEEE80211_FEXT_QUIET_IE	0x00800000	/* STATUS: quiet IE in a beacon has been added */

#define	IEEE80211_FEXT_BITS \
	"\20\2INACT\3SCANWAIT\4BGSCAN\5WPS\6TSN\7SCANREQ\10RESUME" \
	"\0114ADDR\12NONEPR_PR\13SWBMISS\14DFS\15DOTD\16STATEWAIT\17REINIT" \
	"\20BPF\21WDSLEGACY\22PROBECHAN\23UNIQMAC\24SCAN_OFFLOAD\25SEQNO_OFFLOAD" \
	"\26VHT\27QUIET_IE"

/* ic_flags_ht/iv_flags_ht */
#define	IEEE80211_FHT_NONHT_PR	 0x00000001	/* STATUS: non-HT sta present */
#define	IEEE80211_FHT_LDPC_TX	 0x00010000	/* CONF: LDPC tx enabled */
#define	IEEE80211_FHT_LDPC_RX	 0x00020000	/* CONF: LDPC rx enabled */
#define	IEEE80211_FHT_GF  	 0x00040000	/* CONF: Greenfield enabled */
#define	IEEE80211_FHT_HT	 0x00080000	/* CONF: HT supported */
#define	IEEE80211_FHT_AMPDU_TX	 0x00100000	/* CONF: A-MPDU tx supported */
#define	IEEE80211_FHT_AMPDU_RX	 0x00200000	/* CONF: A-MPDU rx supported */
#define	IEEE80211_FHT_AMSDU_TX	 0x00400000	/* CONF: A-MSDU tx supported */
#define	IEEE80211_FHT_AMSDU_RX	 0x00800000	/* CONF: A-MSDU rx supported */
#define	IEEE80211_FHT_USEHT40	 0x01000000	/* CONF: 20/40 use enabled */
#define	IEEE80211_FHT_PUREN	 0x02000000	/* CONF: 11n w/o legacy sta's */
#define	IEEE80211_FHT_SHORTGI20	 0x04000000	/* CONF: short GI in HT20 */
#define	IEEE80211_FHT_SHORTGI40	 0x08000000	/* CONF: short GI in HT40 */
#define	IEEE80211_FHT_HTCOMPAT 	 0x10000000	/* CONF: HT vendor OUI's */
#define	IEEE80211_FHT_RIFS  	 0x20000000	/* CONF: RIFS enabled */
#define	IEEE80211_FHT_STBC_TX 	 0x40000000	/* CONF: STBC tx enabled */
#define	IEEE80211_FHT_STBC_RX 	 0x80000000	/* CONF: STBC rx enabled */

#define	IEEE80211_FHT_BITS \
	"\20\1NONHT_PR" \
	"\23GF\24HT\25AMPDU_TX\26AMPDU_TX" \
	"\27AMSDU_TX\30AMSDU_RX\31USEHT40\32PUREN\33SHORTGI20\34SHORTGI40" \
	"\35HTCOMPAT\36RIFS\37STBC_TX\40STBC_RX"

#define	IEEE80211_FVEN_BITS	"\20"

#define	IEEE80211_FVHT_VHT	0x000000001	/* CONF: VHT supported */
#define	IEEE80211_FVHT_USEVHT40	0x000000002	/* CONF: Use VHT40 */
#define	IEEE80211_FVHT_USEVHT80	0x000000004	/* CONF: Use VHT80 */
#define	IEEE80211_FVHT_USEVHT80P80	0x000000008	/* CONF: Use VHT 80+80 */
#define	IEEE80211_FVHT_USEVHT160	0x000000010	/* CONF: Use VHT160 */
#define	IEEE80211_VFHT_BITS \
	"\20\1VHT\2VHT40\3VHT80\4VHT80P80\5VHT160"

#define	IEEE80211_COM_DETACHED	0x00000001	/* ieee80211_ifdetach called */
#define	IEEE80211_COM_REF_ADD	0x00000002	/* add / remove reference */
#define	IEEE80211_COM_REF_M	0xfffffffe	/* reference counter bits */
#define	IEEE80211_COM_REF_S	1
#define	IEEE80211_COM_REF_MAX	(IEEE80211_COM_REF_M >> IEEE80211_COM_REF_S)

int	ic_printf(struct ieee80211com *, const char *, ...) __printflike(2, 3);
void	ieee80211_ifattach(struct ieee80211com *);
void	ieee80211_ifdetach(struct ieee80211com *);
int	ieee80211_vap_setup(struct ieee80211com *, struct ieee80211vap *,
		const char name[IFNAMSIZ], int unit,
		enum ieee80211_opmode opmode, int flags,
		const uint8_t bssid[IEEE80211_ADDR_LEN]);
int	ieee80211_vap_attach(struct ieee80211vap *,
		ifm_change_cb_t, ifm_stat_cb_t,
		const uint8_t macaddr[IEEE80211_ADDR_LEN]);
void	ieee80211_vap_detach(struct ieee80211vap *);
const struct ieee80211_rateset *ieee80211_get_suprates(struct ieee80211com *ic,
		const struct ieee80211_channel *);
const struct ieee80211_htrateset *ieee80211_get_suphtrates(
		struct ieee80211com *, const struct ieee80211_channel *);
void	ieee80211_announce(struct ieee80211com *);
void	ieee80211_announce_channels(struct ieee80211com *);
void	ieee80211_drain(struct ieee80211com *);
void	ieee80211_chan_init(struct ieee80211com *);
struct ieee80211com *ieee80211_find_vap(const uint8_t mac[IEEE80211_ADDR_LEN]);
struct ieee80211com *ieee80211_find_com(const char *name);
typedef void ieee80211_com_iter_func(void *, struct ieee80211com *);
void	ieee80211_iterate_coms(ieee80211_com_iter_func *, void *);
int	ieee80211_media_change(struct ifnet *);
void	ieee80211_media_status(struct ifnet *, struct ifmediareq *);
int	ieee80211_ioctl(struct ifnet *, u_long, caddr_t);
int	ieee80211_rate2media(struct ieee80211com *, int,
		enum ieee80211_phymode);
int	ieee80211_media2rate(int);
int	ieee80211_mhz2ieee(u_int, u_int);
int	ieee80211_chan2ieee(struct ieee80211com *,
		const struct ieee80211_channel *);
u_int	ieee80211_ieee2mhz(u_int, u_int);
int	ieee80211_add_channel(struct ieee80211_channel[], int, int *,
	    uint8_t, uint16_t, int8_t, uint32_t, const uint8_t[]);
int	ieee80211_add_channel_ht40(struct ieee80211_channel[], int, int *,
	    uint8_t, int8_t, uint32_t);
uint32_t ieee80211_get_channel_center_freq(const struct ieee80211_channel *);
uint32_t ieee80211_get_channel_center_freq1(const struct ieee80211_channel *);
uint32_t ieee80211_get_channel_center_freq2(const struct ieee80211_channel *);
int	ieee80211_add_channel_list_2ghz(struct ieee80211_channel[], int, int *,
	    const uint8_t[], int, const uint8_t[], int);
int	ieee80211_add_channels_default_2ghz(struct ieee80211_channel[], int,
	    int *, const uint8_t[], int);
int	ieee80211_add_channel_list_5ghz(struct ieee80211_channel[], int, int *,
	    const uint8_t[], int, const uint8_t[], int);
struct ieee80211_channel *ieee80211_find_channel(struct ieee80211com *,
		int freq, int flags);
struct ieee80211_channel *ieee80211_find_channel_byieee(struct ieee80211com *,
		int ieee, int flags);
struct ieee80211_channel *ieee80211_lookup_channel_rxstatus(struct ieee80211vap *,
		const struct ieee80211_rx_stats *);
int	ieee80211_setmode(struct ieee80211com *, enum ieee80211_phymode);
enum ieee80211_phymode ieee80211_chan2mode(const struct ieee80211_channel *);
uint32_t ieee80211_mac_hash(const struct ieee80211com *,
		const uint8_t addr[IEEE80211_ADDR_LEN]);
char	ieee80211_channel_type_char(const struct ieee80211_channel *c);

#define	ieee80211_get_current_channel(_ic)	((_ic)->ic_curchan)
#define	ieee80211_get_home_channel(_ic)		((_ic)->ic_bsschan)
#define	ieee80211_get_vap_desired_channel(_iv)	((_iv)->iv_des_chan)

void	ieee80211_radiotap_attach(struct ieee80211com *,
	    struct ieee80211_radiotap_header *th, int tlen,
		uint32_t tx_radiotap,
	    struct ieee80211_radiotap_header *rh, int rlen,
		uint32_t rx_radiotap);
void	ieee80211_radiotap_attachv(struct ieee80211com *,
	    struct ieee80211_radiotap_header *th,
	    int tlen, int n_tx_v, uint32_t tx_radiotap,
	    struct ieee80211_radiotap_header *rh,
	    int rlen, int n_rx_v, uint32_t rx_radiotap);
void	ieee80211_radiotap_detach(struct ieee80211com *);
void	ieee80211_radiotap_vattach(struct ieee80211vap *);
void	ieee80211_radiotap_vdetach(struct ieee80211vap *);
void	ieee80211_radiotap_chan_change(struct ieee80211com *);
void	ieee80211_radiotap_tx(struct ieee80211vap *, struct mbuf *);
void	ieee80211_radiotap_rx(struct ieee80211vap *, struct mbuf *);
void	ieee80211_radiotap_rx_all(struct ieee80211com *, struct mbuf *);

static __inline int
ieee80211_radiotap_active(const struct ieee80211com *ic)
{
	return (ic->ic_flags_ext & IEEE80211_FEXT_BPF) != 0;
}

static __inline int
ieee80211_radiotap_active_vap(const struct ieee80211vap *vap)
{
	return (vap->iv_flags_ext & IEEE80211_FEXT_BPF) ||
	    vap->iv_ic->ic_montaps != 0;
}

/*
 * Enqueue a task on the state thread.
 */
static __inline void
ieee80211_runtask(struct ieee80211com *ic, struct task *task)
{
	taskqueue_enqueue(ic->ic_tq, task);
}

/*
 * Wait for a queued task to complete.
 */
static __inline void
ieee80211_draintask(struct ieee80211com *ic, struct task *task)
{
	taskqueue_drain(ic->ic_tq, task);
}

/* 
 * Key update synchronization methods.  XXX should not be visible.
 */
static __inline void
ieee80211_key_update_begin(struct ieee80211vap *vap)
{
	vap->iv_key_update_begin(vap);
}
static __inline void
ieee80211_key_update_end(struct ieee80211vap *vap)
{
	vap->iv_key_update_end(vap);
}

/*
 * XXX these need to be here for IEEE80211_F_DATAPAD
 */

/*
 * Return the space occupied by the 802.11 header and any
 * padding required by the driver.  This works for a
 * management or data frame.
 */
static __inline int
ieee80211_hdrspace(struct ieee80211com *ic, const void *data)
{
	int size = ieee80211_hdrsize(data);
	if (ic->ic_flags & IEEE80211_F_DATAPAD)
		size = roundup(size, sizeof(uint32_t));
	return size;
}

/*
 * Like ieee80211_hdrspace, but handles any type of frame.
 */
static __inline int
ieee80211_anyhdrspace(struct ieee80211com *ic, const void *data)
{
	int size = ieee80211_anyhdrsize(data);
	if (ic->ic_flags & IEEE80211_F_DATAPAD)
		size = roundup(size, sizeof(uint32_t));
	return size;
}

/*
 * Notify a vap that beacon state has been updated.
 */
static __inline void
ieee80211_beacon_notify(struct ieee80211vap *vap, int what)
{
	if (vap->iv_state == IEEE80211_S_RUN)
		vap->iv_update_beacon(vap, what);
}

/*
 * Calculate HT channel promotion flags for a channel.
 * XXX belongs in ieee80211_ht.h but needs IEEE80211_FHT_*
 */
static __inline int
ieee80211_htchanflags(const struct ieee80211_channel *c)
{
	return IEEE80211_IS_CHAN_HT40(c) ?
	    IEEE80211_FHT_HT | IEEE80211_FHT_USEHT40 :
	    IEEE80211_IS_CHAN_HT(c) ?  IEEE80211_FHT_HT : 0;
}

/*
 * Calculate VHT channel promotion flags for a channel.
 * XXX belongs in ieee80211_vht.h but needs IEEE80211_FVHT_*
 */
static __inline int
ieee80211_vhtchanflags(const struct ieee80211_channel *c)
{

	if (IEEE80211_IS_CHAN_VHT160(c))
		return IEEE80211_FVHT_USEVHT160;
	if (IEEE80211_IS_CHAN_VHT80_80(c))
		return IEEE80211_FVHT_USEVHT80P80;
	if (IEEE80211_IS_CHAN_VHT80(c))
		return IEEE80211_FVHT_USEVHT80;
	if (IEEE80211_IS_CHAN_VHT40(c))
		return IEEE80211_FVHT_USEVHT40;
	if (IEEE80211_IS_CHAN_VHT(c))
		return IEEE80211_FVHT_VHT;
	return (0);
}

/*
 * Fetch the current TX power (cap) for the given node.
 *
 * This includes the node and ic/vap TX power limit as needed,
 * but it doesn't take into account any per-rate limit.
 */
static __inline uint16_t
ieee80211_get_node_txpower(struct ieee80211_node *ni)
{
	struct ieee80211com *ic = ni->ni_ic;
	uint16_t txpower;

	txpower = ni->ni_txpower;
	txpower = MIN(txpower, ic->ic_txpowlimit);
	if (ic->ic_curchan != NULL) {
		txpower = MIN(txpower, 2 * ic->ic_curchan->ic_maxregpower);
		txpower = MIN(txpower, ic->ic_curchan->ic_maxpower);
	}

	return (txpower);
}

/*
 * Debugging facilities compiled in when IEEE80211_DEBUG is defined.
 *
 * The intent is that any problem in the net80211 layer can be
 * diagnosed by inspecting the statistics (dumped by the wlanstats
 * program) and/or the msgs generated by net80211.  Messages are
 * broken into functional classes and can be controlled with the
 * wlandebug program.  Certain of these msg groups are for facilities
 * that are no longer part of net80211 (e.g. IEEE80211_MSG_DOT1XSM).
 */
#define	IEEE80211_MSG_11N	0x80000000	/* 11n mode debug */
#define	IEEE80211_MSG_DEBUG	0x40000000	/* IFF_DEBUG equivalent */
#define	IEEE80211_MSG_DUMPPKTS	0x20000000	/* IFF_LINK2 equivalant */
#define	IEEE80211_MSG_CRYPTO	0x10000000	/* crypto work */
#define	IEEE80211_MSG_INPUT	0x08000000	/* input handling */
#define	IEEE80211_MSG_XRATE	0x04000000	/* rate set handling */
#define	IEEE80211_MSG_ELEMID	0x02000000	/* element id parsing */
#define	IEEE80211_MSG_NODE	0x01000000	/* node handling */
#define	IEEE80211_MSG_ASSOC	0x00800000	/* association handling */
#define	IEEE80211_MSG_AUTH	0x00400000	/* authentication handling */
#define	IEEE80211_MSG_SCAN	0x00200000	/* scanning */
#define	IEEE80211_MSG_OUTPUT	0x00100000	/* output handling */
#define	IEEE80211_MSG_STATE	0x00080000	/* state machine */
#define	IEEE80211_MSG_POWER	0x00040000	/* power save handling */
#define	IEEE80211_MSG_HWMP	0x00020000	/* hybrid mesh protocol */
#define	IEEE80211_MSG_DOT1XSM	0x00010000	/* 802.1x state machine */
#define	IEEE80211_MSG_RADIUS	0x00008000	/* 802.1x radius client */
#define	IEEE80211_MSG_RADDUMP	0x00004000	/* dump 802.1x radius packets */
#define	IEEE80211_MSG_MESH	0x00002000	/* mesh networking */
#define	IEEE80211_MSG_WPA	0x00001000	/* WPA/RSN protocol */
#define	IEEE80211_MSG_ACL	0x00000800	/* ACL handling */
#define	IEEE80211_MSG_WME	0x00000400	/* WME protocol */
#define	IEEE80211_MSG_SUPERG	0x00000200	/* Atheros SuperG protocol */
#define	IEEE80211_MSG_DOTH	0x00000100	/* 802.11h support */
#define	IEEE80211_MSG_INACT	0x00000080	/* inactivity handling */
#define	IEEE80211_MSG_ROAM	0x00000040	/* sta-mode roaming */
#define	IEEE80211_MSG_RATECTL	0x00000020	/* tx rate control */
#define	IEEE80211_MSG_ACTION	0x00000010	/* action frame handling */
#define	IEEE80211_MSG_WDS	0x00000008	/* WDS handling */
#define	IEEE80211_MSG_IOCTL	0x00000004	/* ioctl handling */
#define	IEEE80211_MSG_TDMA	0x00000002	/* TDMA handling */

#define	IEEE80211_MSG_ANY	0xffffffff	/* anything */

#define	IEEE80211_MSG_BITS \
	"\20\2TDMA\3IOCTL\4WDS\5ACTION\6RATECTL\7ROAM\10INACT\11DOTH\12SUPERG" \
	"\13WME\14ACL\15WPA\16RADKEYS\17RADDUMP\20RADIUS\21DOT1XSM\22HWMP" \
	"\23POWER\24STATE\25OUTPUT\26SCAN\27AUTH\30ASSOC\31NODE\32ELEMID" \
	"\33XRATE\34INPUT\35CRYPTO\36DUPMPKTS\37DEBUG\04011N"

#ifdef IEEE80211_DEBUG
#define	ieee80211_msg(_vap, _m)	((_vap)->iv_debug & (_m))
#define	IEEE80211_DPRINTF(_vap, _m, _fmt, ...) do {			\
	if (ieee80211_msg(_vap, _m))					\
		ieee80211_note(_vap, _fmt, __VA_ARGS__);		\
} while (0)
#define	IEEE80211_NOTE(_vap, _m, _ni, _fmt, ...) do {			\
	if (ieee80211_msg(_vap, _m))					\
		ieee80211_note_mac(_vap, (_ni)->ni_macaddr, _fmt, __VA_ARGS__);\
} while (0)
#define	IEEE80211_NOTE_MAC(_vap, _m, _mac, _fmt, ...) do {		\
	if (ieee80211_msg(_vap, _m))					\
		ieee80211_note_mac(_vap, _mac, _fmt, __VA_ARGS__);	\
} while (0)
#define	IEEE80211_NOTE_FRAME(_vap, _m, _wh, _fmt, ...) do {		\
	if (ieee80211_msg(_vap, _m))					\
		ieee80211_note_frame(_vap, _wh, _fmt, __VA_ARGS__);	\
} while (0)
void	ieee80211_note(const struct ieee80211vap *, const char *, ...);
void	ieee80211_note_mac(const struct ieee80211vap *,
		const uint8_t mac[IEEE80211_ADDR_LEN], const char *, ...);
void	ieee80211_note_frame(const struct ieee80211vap *,
		const struct ieee80211_frame *, const char *, ...);
#define	ieee80211_msg_debug(_vap) \
	((_vap)->iv_debug & IEEE80211_MSG_DEBUG)
#define	ieee80211_msg_dumppkts(_vap) \
	((_vap)->iv_debug & IEEE80211_MSG_DUMPPKTS)
#define	ieee80211_msg_input(_vap) \
	((_vap)->iv_debug & IEEE80211_MSG_INPUT)
#define	ieee80211_msg_radius(_vap) \
	((_vap)->iv_debug & IEEE80211_MSG_RADIUS)
#define	ieee80211_msg_dumpradius(_vap) \
	((_vap)->iv_debug & IEEE80211_MSG_RADDUMP)
#define	ieee80211_msg_dumpradkeys(_vap) \
	((_vap)->iv_debug & IEEE80211_MSG_RADKEYS)
#define	ieee80211_msg_scan(_vap) \
	((_vap)->iv_debug & IEEE80211_MSG_SCAN)
#define	ieee80211_msg_assoc(_vap) \
	((_vap)->iv_debug & IEEE80211_MSG_ASSOC)

/*
 * Emit a debug message about discarding a frame or information
 * element.  One format is for extracting the mac address from
 * the frame header; the other is for when a header is not
 * available or otherwise appropriate.
 */
#define	IEEE80211_DISCARD(_vap, _m, _wh, _type, _fmt, ...) do {		\
	if ((_vap)->iv_debug & (_m))					\
		ieee80211_discard_frame(_vap, _wh, _type, _fmt, __VA_ARGS__);\
} while (0)
#define	IEEE80211_DISCARD_IE(_vap, _m, _wh, _type, _fmt, ...) do {	\
	if ((_vap)->iv_debug & (_m))					\
		ieee80211_discard_ie(_vap, _wh, _type, _fmt, __VA_ARGS__);\
} while (0)
#define	IEEE80211_DISCARD_MAC(_vap, _m, _mac, _type, _fmt, ...) do {	\
	if ((_vap)->iv_debug & (_m))					\
		ieee80211_discard_mac(_vap, _mac, _type, _fmt, __VA_ARGS__);\
} while (0)

void ieee80211_discard_frame(const struct ieee80211vap *,
	const struct ieee80211_frame *, const char *type, const char *fmt, ...);
void ieee80211_discard_ie(const struct ieee80211vap *,
	const struct ieee80211_frame *, const char *type, const char *fmt, ...);
void ieee80211_discard_mac(const struct ieee80211vap *,
	const uint8_t mac[IEEE80211_ADDR_LEN], const char *type,
	const char *fmt, ...);
#else
#define	IEEE80211_DPRINTF(_vap, _m, _fmt, ...)
#define	IEEE80211_NOTE(_vap, _m, _ni, _fmt, ...)
#define	IEEE80211_NOTE_FRAME(_vap, _m, _wh, _fmt, ...)
#define	IEEE80211_NOTE_MAC(_vap, _m, _mac, _fmt, ...)
#define	ieee80211_msg_dumppkts(_vap)	0
#define	ieee80211_msg(_vap, _m)		0

#define	IEEE80211_DISCARD(_vap, _m, _wh, _type, _fmt, ...)
#define	IEEE80211_DISCARD_IE(_vap, _m, _wh, _type, _fmt, ...)
#define	IEEE80211_DISCARD_MAC(_vap, _m, _mac, _type, _fmt, ...)
#endif

#endif /* _NET80211_IEEE80211_VAR_H_ */
