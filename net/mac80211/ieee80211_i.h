/*
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2005, Devicescape Software, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 * Copyright 2007-2010	Johannes Berg <johannes@sipsolutions.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef IEEE80211_I_H
#define IEEE80211_I_H

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/if_ether.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/workqueue.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/etherdevice.h>
#include <linux/leds.h>
#include <linux/idr.h>
#include <net/ieee80211_radiotap.h>
#include <net/cfg80211.h>
#include <net/mac80211.h>
#include "key.h"
#include "sta_info.h"
#include "debug.h"

struct ieee80211_local;

/* Maximum number of broadcast/multicast frames to buffer when some of the
 * associated stations are using power saving. */
#define AP_MAX_BC_BUFFER 128

/* Maximum number of frames buffered to all STAs, including multicast frames.
 * Note: increasing this limit increases the potential memory requirement. Each
 * frame can be up to about 2 kB long. */
#define TOTAL_MAX_TX_BUFFER 512

/* Required encryption head and tailroom */
#define IEEE80211_ENCRYPT_HEADROOM 8
#define IEEE80211_ENCRYPT_TAILROOM 18

/* IEEE 802.11 (Ch. 9.5 Defragmentation) requires support for concurrent
 * reception of at least three fragmented frames. This limit can be increased
 * by changing this define, at the cost of slower frame reassembly and
 * increased memory use (about 2 kB of RAM per entry). */
#define IEEE80211_FRAGMENT_MAX 4

/* power level hasn't been configured (or set to automatic) */
#define IEEE80211_UNSET_POWER_LEVEL	INT_MIN

/*
 * Some APs experience problems when working with U-APSD. Decrease the
 * probability of that happening by using legacy mode for all ACs but VO.
 * The AP that caused us trouble was a Cisco 4410N. It ignores our
 * setting, and always treats non-VO ACs as legacy.
 */
#define IEEE80211_DEFAULT_UAPSD_QUEUES \
	IEEE80211_WMM_IE_STA_QOSINFO_AC_VO

#define IEEE80211_DEFAULT_MAX_SP_LEN		\
	IEEE80211_WMM_IE_STA_QOSINFO_SP_ALL

#define IEEE80211_DEAUTH_FRAME_LEN	(24 /* hdr */ + 2 /* reason */)

struct ieee80211_fragment_entry {
	unsigned long first_frag_time;
	unsigned int seq;
	unsigned int rx_queue;
	unsigned int last_frag;
	unsigned int extra_len;
	struct sk_buff_head skb_list;
	int ccmp; /* Whether fragments were encrypted with CCMP */
	u8 last_pn[6]; /* PN of the last fragment if CCMP was used */
};


struct ieee80211_bss {
	u32 device_ts_beacon, device_ts_presp;

	bool wmm_used;
	bool uapsd_supported;

#define IEEE80211_MAX_SUPP_RATES 32
	u8 supp_rates[IEEE80211_MAX_SUPP_RATES];
	size_t supp_rates_len;
	struct ieee80211_rate *beacon_rate;

	/*
	 * During association, we save an ERP value from a probe response so
	 * that we can feed ERP info to the driver when handling the
	 * association completes. these fields probably won't be up-to-date
	 * otherwise, you probably don't want to use them.
	 */
	bool has_erp_value;
	u8 erp_value;

	/* Keep track of the corruption of the last beacon/probe response. */
	u8 corrupt_data;

	/* Keep track of what bits of information we have valid info for. */
	u8 valid_data;
};

/**
 * enum ieee80211_corrupt_data_flags - BSS data corruption flags
 * @IEEE80211_BSS_CORRUPT_BEACON: last beacon frame received was corrupted
 * @IEEE80211_BSS_CORRUPT_PROBE_RESP: last probe response received was corrupted
 *
 * These are bss flags that are attached to a bss in the
 * @corrupt_data field of &struct ieee80211_bss.
 */
enum ieee80211_bss_corrupt_data_flags {
	IEEE80211_BSS_CORRUPT_BEACON		= BIT(0),
	IEEE80211_BSS_CORRUPT_PROBE_RESP	= BIT(1)
};

/**
 * enum ieee80211_valid_data_flags - BSS valid data flags
 * @IEEE80211_BSS_VALID_WMM: WMM/UAPSD data was gathered from non-corrupt IE
 * @IEEE80211_BSS_VALID_RATES: Supported rates were gathered from non-corrupt IE
 * @IEEE80211_BSS_VALID_ERP: ERP flag was gathered from non-corrupt IE
 *
 * These are bss flags that are attached to a bss in the
 * @valid_data field of &struct ieee80211_bss.  They show which parts
 * of the data structure were recieved as a result of an un-corrupted
 * beacon/probe response.
 */
enum ieee80211_bss_valid_data_flags {
	IEEE80211_BSS_VALID_WMM			= BIT(1),
	IEEE80211_BSS_VALID_RATES		= BIT(2),
	IEEE80211_BSS_VALID_ERP			= BIT(3)
};

typedef unsigned __bitwise__ ieee80211_tx_result;
#define TX_CONTINUE	((__force ieee80211_tx_result) 0u)
#define TX_DROP		((__force ieee80211_tx_result) 1u)
#define TX_QUEUED	((__force ieee80211_tx_result) 2u)

#define IEEE80211_TX_UNICAST		BIT(1)
#define IEEE80211_TX_PS_BUFFERED	BIT(2)

struct ieee80211_tx_data {
	struct sk_buff *skb;
	struct sk_buff_head skbs;
	struct ieee80211_local *local;
	struct ieee80211_sub_if_data *sdata;
	struct sta_info *sta;
	struct ieee80211_key *key;
	struct ieee80211_tx_rate rate;

	unsigned int flags;
};


typedef unsigned __bitwise__ ieee80211_rx_result;
#define RX_CONTINUE		((__force ieee80211_rx_result) 0u)
#define RX_DROP_UNUSABLE	((__force ieee80211_rx_result) 1u)
#define RX_DROP_MONITOR		((__force ieee80211_rx_result) 2u)
#define RX_QUEUED		((__force ieee80211_rx_result) 3u)

/**
 * enum ieee80211_packet_rx_flags - packet RX flags
 * @IEEE80211_RX_RA_MATCH: frame is destined to interface currently processed
 *	(incl. multicast frames)
 * @IEEE80211_RX_FRAGMENTED: fragmented frame
 * @IEEE80211_RX_AMSDU: a-MSDU packet
 * @IEEE80211_RX_MALFORMED_ACTION_FRM: action frame is malformed
 * @IEEE80211_RX_DEFERRED_RELEASE: frame was subjected to receive reordering
 *
 * These are per-frame flags that are attached to a frame in the
 * @rx_flags field of &struct ieee80211_rx_status.
 */
enum ieee80211_packet_rx_flags {
	IEEE80211_RX_RA_MATCH			= BIT(1),
	IEEE80211_RX_FRAGMENTED			= BIT(2),
	IEEE80211_RX_AMSDU			= BIT(3),
	IEEE80211_RX_MALFORMED_ACTION_FRM	= BIT(4),
	IEEE80211_RX_DEFERRED_RELEASE		= BIT(5),
};

/**
 * enum ieee80211_rx_flags - RX data flags
 *
 * @IEEE80211_RX_CMNTR: received on cooked monitor already
 * @IEEE80211_RX_BEACON_REPORTED: This frame was already reported
 *	to cfg80211_report_obss_beacon().
 *
 * These flags are used across handling multiple interfaces
 * for a single frame.
 */
enum ieee80211_rx_flags {
	IEEE80211_RX_CMNTR		= BIT(0),
	IEEE80211_RX_BEACON_REPORTED	= BIT(1),
};

struct ieee80211_rx_data {
	struct sk_buff *skb;
	struct ieee80211_local *local;
	struct ieee80211_sub_if_data *sdata;
	struct sta_info *sta;
	struct ieee80211_key *key;

	unsigned int flags;

	/*
	 * Index into sequence numbers array, 0..16
	 * since the last (16) is used for non-QoS,
	 * will be 16 on non-QoS frames.
	 */
	int seqno_idx;

	/*
	 * Index into the security IV/PN arrays, 0..16
	 * since the last (16) is used for CCMP-encrypted
	 * management frames, will be set to 16 on mgmt
	 * frames and 0 on non-QoS frames.
	 */
	int security_idx;

	u32 tkip_iv32;
	u16 tkip_iv16;
};

struct ieee80211_csa_settings {
	const u16 *counter_offsets_beacon;
	const u16 *counter_offsets_presp;

	int n_counter_offsets_beacon;
	int n_counter_offsets_presp;

	u8 count;
};

struct beacon_data {
	u8 *head, *tail;
	int head_len, tail_len;
	struct ieee80211_meshconf_ie *meshconf;
	u16 csa_counter_offsets[IEEE80211_MAX_CSA_COUNTERS_NUM];
	u8 csa_current_counter;
	struct rcu_head rcu_head;
};

struct probe_resp {
	struct rcu_head rcu_head;
	int len;
	u16 csa_counter_offsets[IEEE80211_MAX_CSA_COUNTERS_NUM];
	u8 data[0];
};

struct ps_data {
	/* yes, this looks ugly, but guarantees that we can later use
	 * bitmap_empty :)
	 * NB: don't touch this bitmap, use sta_info_{set,clear}_tim_bit */
	u8 tim[sizeof(unsigned long) * BITS_TO_LONGS(IEEE80211_MAX_AID + 1)]
			__aligned(__alignof__(unsigned long));
	struct sk_buff_head bc_buf;
	atomic_t num_sta_ps; /* number of stations in PS mode */
	int dtim_count;
	bool dtim_bc_mc;
};

struct ieee80211_if_ap {
	struct beacon_data __rcu *beacon;
	struct probe_resp __rcu *probe_resp;

	/* to be used after channel switch. */
	struct cfg80211_beacon_data *next_beacon;
	struct list_head vlans; /* write-protected with RTNL and local->mtx */

	struct ps_data ps;
	atomic_t num_mcast_sta; /* number of stations receiving multicast */
	enum ieee80211_smps_mode req_smps, /* requested smps mode */
			 driver_smps_mode; /* smps mode request */

	struct work_struct request_smps_work;
};

struct ieee80211_if_wds {
	struct sta_info *sta;
	u8 remote_addr[ETH_ALEN];
};

struct ieee80211_if_vlan {
	struct list_head list; /* write-protected with RTNL and local->mtx */

	/* used for all tx if the VLAN is configured to 4-addr mode */
	struct sta_info __rcu *sta;
};

struct mesh_stats {
	__u32 fwded_mcast;		/* Mesh forwarded multicast frames */
	__u32 fwded_unicast;		/* Mesh forwarded unicast frames */
	__u32 fwded_frames;		/* Mesh total forwarded frames */
	__u32 dropped_frames_ttl;	/* Not transmitted since mesh_ttl == 0*/
	__u32 dropped_frames_no_route;	/* Not transmitted, no route found */
	__u32 dropped_frames_congestion;/* Not forwarded due to congestion */
};

#define PREQ_Q_F_START		0x1
#define PREQ_Q_F_REFRESH	0x2
struct mesh_preq_queue {
	struct list_head list;
	u8 dst[ETH_ALEN];
	u8 flags;
};

#if HZ/100 == 0
#define IEEE80211_ROC_MIN_LEFT	1
#else
#define IEEE80211_ROC_MIN_LEFT	(HZ/100)
#endif

struct ieee80211_roc_work {
	struct list_head list;
	struct list_head dependents;

	struct delayed_work work;

	struct ieee80211_sub_if_data *sdata;

	struct ieee80211_channel *chan;

	bool started, abort, hw_begun, notified;
	bool to_be_freed;
	bool on_channel;

	unsigned long hw_start_time;

	u32 duration, req_duration;
	struct sk_buff *frame;
	u64 cookie, mgmt_tx_cookie;
	enum ieee80211_roc_type type;
};

/* flags used in struct ieee80211_if_managed.flags */
enum ieee80211_sta_flags {
	IEEE80211_STA_CONNECTION_POLL	= BIT(1),
	IEEE80211_STA_CONTROL_PORT	= BIT(2),
	IEEE80211_STA_DISABLE_HT	= BIT(4),
	IEEE80211_STA_MFP_ENABLED	= BIT(6),
	IEEE80211_STA_UAPSD_ENABLED	= BIT(7),
	IEEE80211_STA_NULLFUNC_ACKED	= BIT(8),
	IEEE80211_STA_RESET_SIGNAL_AVE	= BIT(9),
	IEEE80211_STA_DISABLE_40MHZ	= BIT(10),
	IEEE80211_STA_DISABLE_VHT	= BIT(11),
	IEEE80211_STA_DISABLE_80P80MHZ	= BIT(12),
	IEEE80211_STA_DISABLE_160MHZ	= BIT(13),
	IEEE80211_STA_DISABLE_WMM	= BIT(14),
};

struct ieee80211_mgd_auth_data {
	struct cfg80211_bss *bss;
	unsigned long timeout;
	int tries;
	u16 algorithm, expected_transaction;

	u8 key[WLAN_KEY_LEN_WEP104];
	u8 key_len, key_idx;
	bool done;
	bool timeout_started;

	u16 sae_trans, sae_status;
	size_t data_len;
	u8 data[];
};

struct ieee80211_mgd_assoc_data {
	struct cfg80211_bss *bss;
	const u8 *supp_rates;

	unsigned long timeout;
	int tries;

	u16 capability;
	u8 prev_bssid[ETH_ALEN];
	u8 ssid[IEEE80211_MAX_SSID_LEN];
	u8 ssid_len;
	u8 supp_rates_len;
	bool wmm, uapsd;
	bool need_beacon;
	bool synced;
	bool timeout_started;

	u8 ap_ht_param;

	struct ieee80211_vht_cap ap_vht_cap;

	size_t ie_len;
	u8 ie[];
};

struct ieee80211_if_managed {
	struct timer_list timer;
	struct timer_list conn_mon_timer;
	struct timer_list bcn_mon_timer;
	struct timer_list chswitch_timer;
	struct work_struct monitor_work;
	struct work_struct chswitch_work;
	struct work_struct beacon_connection_loss_work;
	struct work_struct csa_connection_drop_work;

	unsigned long beacon_timeout;
	unsigned long probe_timeout;
	int probe_send_count;
	bool nullfunc_failed;
	bool connection_loss;

	struct cfg80211_bss *associated;
	struct ieee80211_mgd_auth_data *auth_data;
	struct ieee80211_mgd_assoc_data *assoc_data;

	u8 bssid[ETH_ALEN];

	u16 aid;

	bool powersave; /* powersave requested for this iface */
	bool broken_ap; /* AP is broken -- turn off powersave */
	bool have_beacon;
	u8 dtim_period;
	enum ieee80211_smps_mode req_smps, /* requested smps mode */
				 driver_smps_mode; /* smps mode request */

	struct work_struct request_smps_work;

	unsigned int flags;

	bool beacon_crc_valid;
	u32 beacon_crc;

	bool status_acked;
	bool status_received;
	__le16 status_fc;

	enum {
		IEEE80211_MFP_DISABLED,
		IEEE80211_MFP_OPTIONAL,
		IEEE80211_MFP_REQUIRED
	} mfp; /* management frame protection */

	/*
	 * Bitmask of enabled u-apsd queues,
	 * IEEE80211_WMM_IE_STA_QOSINFO_AC_BE & co. Needs a new association
	 * to take effect.
	 */
	unsigned int uapsd_queues;

	/*
	 * Maximum number of buffered frames AP can deliver during a
	 * service period, IEEE80211_WMM_IE_STA_QOSINFO_SP_ALL or similar.
	 * Needs a new association to take effect.
	 */
	unsigned int uapsd_max_sp_len;

	int wmm_last_param_set;

	u8 use_4addr;

	s16 p2p_noa_index;

	/* Signal strength from the last Beacon frame in the current BSS. */
	int last_beacon_signal;

	/*
	 * Weighted average of the signal strength from Beacon frames in the
	 * current BSS. This is in units of 1/16 of the signal unit to maintain
	 * accuracy and to speed up calculations, i.e., the value need to be
	 * divided by 16 to get the actual value.
	 */
	int ave_beacon_signal;

	/*
	 * Number of Beacon frames used in ave_beacon_signal. This can be used
	 * to avoid generating less reliable cqm events that would be based
	 * only on couple of received frames.
	 */
	unsigned int count_beacon_signal;

	/*
	 * Last Beacon frame signal strength average (ave_beacon_signal / 16)
	 * that triggered a cqm event. 0 indicates that no event has been
	 * generated for the current association.
	 */
	int last_cqm_event_signal;

	/*
	 * State variables for keeping track of RSSI of the AP currently
	 * connected to and informing driver when RSSI has gone
	 * below/above a certain threshold.
	 */
	int rssi_min_thold, rssi_max_thold;
	int last_ave_beacon_signal;

	struct ieee80211_ht_cap ht_capa; /* configured ht-cap over-rides */
	struct ieee80211_ht_cap ht_capa_mask; /* Valid parts of ht_capa */
	struct ieee80211_vht_cap vht_capa; /* configured VHT overrides */
	struct ieee80211_vht_cap vht_capa_mask; /* Valid parts of vht_capa */

	u8 tdls_peer[ETH_ALEN] __aligned(2);
	struct delayed_work tdls_peer_del_work;
};

struct ieee80211_if_ibss {
	struct timer_list timer;
	struct work_struct csa_connection_drop_work;

	unsigned long last_scan_completed;

	u32 basic_rates;

	bool fixed_bssid;
	bool fixed_channel;
	bool privacy;

	bool control_port;
	bool userspace_handles_dfs;

	u8 bssid[ETH_ALEN] __aligned(2);
	u8 ssid[IEEE80211_MAX_SSID_LEN];
	u8 ssid_len, ie_len;
	u8 *ie;
	struct cfg80211_chan_def chandef;

	unsigned long ibss_join_req;
	/* probe response/beacon for IBSS */
	struct beacon_data __rcu *presp;

	struct ieee80211_ht_cap ht_capa; /* configured ht-cap over-rides */
	struct ieee80211_ht_cap ht_capa_mask; /* Valid parts of ht_capa */

	spinlock_t incomplete_lock;
	struct list_head incomplete_stations;

	enum {
		IEEE80211_IBSS_MLME_SEARCH,
		IEEE80211_IBSS_MLME_JOINED,
	} state;
};

/**
 * struct ieee80211_mesh_sync_ops - Extensible synchronization framework interface
 *
 * these declarations define the interface, which enables
 * vendor-specific mesh synchronization
 *
 */
struct ieee802_11_elems;
struct ieee80211_mesh_sync_ops {
	void (*rx_bcn_presp)(struct ieee80211_sub_if_data *sdata,
			     u16 stype,
			     struct ieee80211_mgmt *mgmt,
			     struct ieee802_11_elems *elems,
			     struct ieee80211_rx_status *rx_status);

	/* should be called with beacon_data under RCU read lock */
	void (*adjust_tbtt)(struct ieee80211_sub_if_data *sdata,
			    struct beacon_data *beacon);
	/* add other framework functions here */
};

struct mesh_csa_settings {
	struct rcu_head rcu_head;
	struct cfg80211_csa_settings settings;
};

struct ieee80211_if_mesh {
	struct timer_list housekeeping_timer;
	struct timer_list mesh_path_timer;
	struct timer_list mesh_path_root_timer;

	unsigned long wrkq_flags;
	unsigned long mbss_changed;

	u8 mesh_id[IEEE80211_MAX_MESH_ID_LEN];
	size_t mesh_id_len;
	/* Active Path Selection Protocol Identifier */
	u8 mesh_pp_id;
	/* Active Path Selection Metric Identifier */
	u8 mesh_pm_id;
	/* Congestion Control Mode Identifier */
	u8 mesh_cc_id;
	/* Synchronization Protocol Identifier */
	u8 mesh_sp_id;
	/* Authentication Protocol Identifier */
	u8 mesh_auth_id;
	/* Local mesh Sequence Number */
	u32 sn;
	/* Last used PREQ ID */
	u32 preq_id;
	atomic_t mpaths;
	/* Timestamp of last SN update */
	unsigned long last_sn_update;
	/* Time when it's ok to send next PERR */
	unsigned long next_perr;
	/* Timestamp of last PREQ sent */
	unsigned long last_preq;
	struct mesh_rmc *rmc;
	spinlock_t mesh_preq_queue_lock;
	struct mesh_preq_queue preq_queue;
	int preq_queue_len;
	struct mesh_stats mshstats;
	struct mesh_config mshcfg;
	atomic_t estab_plinks;
	u32 mesh_seqnum;
	bool accepting_plinks;
	int num_gates;
	struct beacon_data __rcu *beacon;
	const u8 *ie;
	u8 ie_len;
	enum {
		IEEE80211_MESH_SEC_NONE = 0x0,
		IEEE80211_MESH_SEC_AUTHED = 0x1,
		IEEE80211_MESH_SEC_SECURED = 0x2,
	} security;
	bool user_mpm;
	/* Extensible Synchronization Framework */
	const struct ieee80211_mesh_sync_ops *sync_ops;
	s64 sync_offset_clockdrift_max;
	spinlock_t sync_offset_lock;
	bool adjusting_tbtt;
	/* mesh power save */
	enum nl80211_mesh_power_mode nonpeer_pm;
	int ps_peers_light_sleep;
	int ps_peers_deep_sleep;
	struct ps_data ps;
	/* Channel Switching Support */
	struct mesh_csa_settings __rcu *csa;
	enum {
		IEEE80211_MESH_CSA_ROLE_NONE,
		IEEE80211_MESH_CSA_ROLE_INIT,
		IEEE80211_MESH_CSA_ROLE_REPEATER,
	} csa_role;
	u8 chsw_ttl;
	u16 pre_value;

	/* offset from skb->data while building IE */
	int meshconf_offset;
};

#ifdef CONFIG_MAC80211_MESH
#define IEEE80211_IFSTA_MESH_CTR_INC(msh, name)	\
	do { (msh)->mshstats.name++; } while (0)
#else
#define IEEE80211_IFSTA_MESH_CTR_INC(msh, name) \
	do { } while (0)
#endif

/**
 * enum ieee80211_sub_if_data_flags - virtual interface flags
 *
 * @IEEE80211_SDATA_ALLMULTI: interface wants all multicast packets
 * @IEEE80211_SDATA_PROMISC: interface is promisc
 * @IEEE80211_SDATA_OPERATING_GMODE: operating in G-only mode
 * @IEEE80211_SDATA_DONT_BRIDGE_PACKETS: bridge packets between
 *	associated stations and deliver multicast frames both
 *	back to wireless media and to the local net stack.
 * @IEEE80211_SDATA_DISCONNECT_RESUME: Disconnect after resume.
 * @IEEE80211_SDATA_IN_DRIVER: indicates interface was added to driver
 */
enum ieee80211_sub_if_data_flags {
	IEEE80211_SDATA_ALLMULTI		= BIT(0),
	IEEE80211_SDATA_PROMISC			= BIT(1),
	IEEE80211_SDATA_OPERATING_GMODE		= BIT(2),
	IEEE80211_SDATA_DONT_BRIDGE_PACKETS	= BIT(3),
	IEEE80211_SDATA_DISCONNECT_RESUME	= BIT(4),
	IEEE80211_SDATA_IN_DRIVER		= BIT(5),
};

/**
 * enum ieee80211_sdata_state_bits - virtual interface state bits
 * @SDATA_STATE_RUNNING: virtual interface is up & running; this
 *	mirrors netif_running() but is separate for interface type
 *	change handling while the interface is up
 * @SDATA_STATE_OFFCHANNEL: This interface is currently in offchannel
 *	mode, so queues are stopped
 * @SDATA_STATE_OFFCHANNEL_BEACON_STOPPED: Beaconing was stopped due
 *	to offchannel, reset when offchannel returns
 */
enum ieee80211_sdata_state_bits {
	SDATA_STATE_RUNNING,
	SDATA_STATE_OFFCHANNEL,
	SDATA_STATE_OFFCHANNEL_BEACON_STOPPED,
};

/**
 * enum ieee80211_chanctx_mode - channel context configuration mode
 *
 * @IEEE80211_CHANCTX_SHARED: channel context may be used by
 *	multiple interfaces
 * @IEEE80211_CHANCTX_EXCLUSIVE: channel context can be used
 *	only by a single interface. This can be used for example for
 *	non-fixed channel IBSS.
 */
enum ieee80211_chanctx_mode {
	IEEE80211_CHANCTX_SHARED,
	IEEE80211_CHANCTX_EXCLUSIVE
};

/**
 * enum ieee80211_chanctx_replace_state - channel context replacement state
 *
 * This is used for channel context in-place reservations that require channel
 * context switch/swap.
 *
 * @IEEE80211_CHANCTX_REPLACE_NONE: no replacement is taking place
 * @IEEE80211_CHANCTX_WILL_BE_REPLACED: this channel context will be replaced
 *	by a (not yet registered) channel context pointed by %replace_ctx.
 * @IEEE80211_CHANCTX_REPLACES_OTHER: this (not yet registered) channel context
 *	replaces an existing channel context pointed to by %replace_ctx.
 */
enum ieee80211_chanctx_replace_state {
	IEEE80211_CHANCTX_REPLACE_NONE,
	IEEE80211_CHANCTX_WILL_BE_REPLACED,
	IEEE80211_CHANCTX_REPLACES_OTHER,
};

struct ieee80211_chanctx {
	struct list_head list;
	struct rcu_head rcu_head;

	struct list_head assigned_vifs;
	struct list_head reserved_vifs;

	enum ieee80211_chanctx_replace_state replace_state;
	struct ieee80211_chanctx *replace_ctx;

	enum ieee80211_chanctx_mode mode;
	bool driver_present;

	struct ieee80211_chanctx_conf conf;
};

struct mac80211_qos_map {
	struct cfg80211_qos_map qos_map;
	struct rcu_head rcu_head;
};

struct ieee80211_sub_if_data {
	struct list_head list;

	struct wireless_dev wdev;

	/* keys */
	struct list_head key_list;

	/* count for keys needing tailroom space allocation */
	int crypto_tx_tailroom_needed_cnt;
	int crypto_tx_tailroom_pending_dec;
	struct delayed_work dec_tailroom_needed_wk;

	struct net_device *dev;
	struct ieee80211_local *local;

	unsigned int flags;

	unsigned long state;

	int drop_unencrypted;

	char name[IFNAMSIZ];

	/* Fragment table for host-based reassembly */
	struct ieee80211_fragment_entry	fragments[IEEE80211_FRAGMENT_MAX];
	unsigned int fragment_next;

	/* TID bitmap for NoAck policy */
	u16 noack_map;

	/* bit field of ACM bits (BIT(802.1D tag)) */
	u8 wmm_acm;

	struct ieee80211_key __rcu *keys[NUM_DEFAULT_KEYS + NUM_DEFAULT_MGMT_KEYS];
	struct ieee80211_key __rcu *default_unicast_key;
	struct ieee80211_key __rcu *default_multicast_key;
	struct ieee80211_key __rcu *default_mgmt_key;

	u16 sequence_number;
	__be16 control_port_protocol;
	bool control_port_no_encrypt;
	int encrypt_headroom;

	struct ieee80211_tx_queue_params tx_conf[IEEE80211_NUM_ACS];
	struct mac80211_qos_map __rcu *qos_map;

	struct work_struct csa_finalize_work;
	bool csa_block_tx; /* write-protected by sdata_lock and local->mtx */
	struct cfg80211_chan_def csa_chandef;

	struct list_head assigned_chanctx_list; /* protected by chanctx_mtx */
	struct list_head reserved_chanctx_list; /* protected by chanctx_mtx */

	/* context reservation -- protected with chanctx_mtx */
	struct ieee80211_chanctx *reserved_chanctx;
	struct cfg80211_chan_def reserved_chandef;
	bool reserved_radar_required;
	bool reserved_ready;

	/* used to reconfigure hardware SM PS */
	struct work_struct recalc_smps;

	struct work_struct work;
	struct sk_buff_head skb_queue;

	u8 needed_rx_chains;
	enum ieee80211_smps_mode smps_mode;

	int user_power_level; /* in dBm */
	int ap_power_level; /* in dBm */

	bool radar_required;
	struct delayed_work dfs_cac_timer_work;

	/*
	 * AP this belongs to: self in AP mode and
	 * corresponding AP in VLAN mode, NULL for
	 * all others (might be needed later in IBSS)
	 */
	struct ieee80211_if_ap *bss;

	/* bitmap of allowed (non-MCS) rate indexes for rate control */
	u32 rc_rateidx_mask[IEEE80211_NUM_BANDS];

	bool rc_has_mcs_mask[IEEE80211_NUM_BANDS];
	u8  rc_rateidx_mcs_mask[IEEE80211_NUM_BANDS][IEEE80211_HT_MCS_MASK_LEN];

	union {
		struct ieee80211_if_ap ap;
		struct ieee80211_if_wds wds;
		struct ieee80211_if_vlan vlan;
		struct ieee80211_if_managed mgd;
		struct ieee80211_if_ibss ibss;
		struct ieee80211_if_mesh mesh;
		u32 mntr_flags;
	} u;

#ifdef CONFIG_MAC80211_DEBUGFS
	struct {
		struct dentry *subdir_stations;
		struct dentry *default_unicast_key;
		struct dentry *default_multicast_key;
		struct dentry *default_mgmt_key;
	} debugfs;
#endif

	/* must be last, dynamically sized area in this! */
	struct ieee80211_vif vif;
};

static inline
struct ieee80211_sub_if_data *vif_to_sdata(struct ieee80211_vif *p)
{
	return container_of(p, struct ieee80211_sub_if_data, vif);
}

static inline void sdata_lock(struct ieee80211_sub_if_data *sdata)
	__acquires(&sdata->wdev.mtx)
{
	mutex_lock(&sdata->wdev.mtx);
	__acquire(&sdata->wdev.mtx);
}

static inline void sdata_unlock(struct ieee80211_sub_if_data *sdata)
	__releases(&sdata->wdev.mtx)
{
	mutex_unlock(&sdata->wdev.mtx);
	__release(&sdata->wdev.mtx);
}

#define sdata_dereference(p, sdata) \
	rcu_dereference_protected(p, lockdep_is_held(&sdata->wdev.mtx))

static inline void
sdata_assert_lock(struct ieee80211_sub_if_data *sdata)
{
	lockdep_assert_held(&sdata->wdev.mtx);
}

static inline enum ieee80211_band
ieee80211_get_sdata_band(struct ieee80211_sub_if_data *sdata)
{
	enum ieee80211_band band = IEEE80211_BAND_2GHZ;
	struct ieee80211_chanctx_conf *chanctx_conf;

	rcu_read_lock();
	chanctx_conf = rcu_dereference(sdata->vif.chanctx_conf);
	if (!WARN_ON(!chanctx_conf))
		band = chanctx_conf->def.chan->band;
	rcu_read_unlock();

	return band;
}

static inline int
ieee80211_chandef_get_shift(struct cfg80211_chan_def *chandef)
{
	switch (chandef->width) {
	case NL80211_CHAN_WIDTH_5:
		return 2;
	case NL80211_CHAN_WIDTH_10:
		return 1;
	default:
		return 0;
	}
}

static inline int
ieee80211_vif_get_shift(struct ieee80211_vif *vif)
{
	struct ieee80211_chanctx_conf *chanctx_conf;
	int shift = 0;

	rcu_read_lock();
	chanctx_conf = rcu_dereference(vif->chanctx_conf);
	if (chanctx_conf)
		shift = ieee80211_chandef_get_shift(&chanctx_conf->def);
	rcu_read_unlock();

	return shift;
}

struct ieee80211_rx_agg {
	u8 addr[ETH_ALEN];
	u16 tid;
};

enum sdata_queue_type {
	IEEE80211_SDATA_QUEUE_TYPE_FRAME	= 0,
	IEEE80211_SDATA_QUEUE_AGG_START		= 1,
	IEEE80211_SDATA_QUEUE_AGG_STOP		= 2,
	IEEE80211_SDATA_QUEUE_RX_AGG_START	= 3,
	IEEE80211_SDATA_QUEUE_RX_AGG_STOP	= 4,
};

enum {
	IEEE80211_RX_MSG	= 1,
	IEEE80211_TX_STATUS_MSG	= 2,
};

enum queue_stop_reason {
	IEEE80211_QUEUE_STOP_REASON_DRIVER,
	IEEE80211_QUEUE_STOP_REASON_PS,
	IEEE80211_QUEUE_STOP_REASON_CSA,
	IEEE80211_QUEUE_STOP_REASON_AGGREGATION,
	IEEE80211_QUEUE_STOP_REASON_SUSPEND,
	IEEE80211_QUEUE_STOP_REASON_SKB_ADD,
	IEEE80211_QUEUE_STOP_REASON_OFFCHANNEL,
	IEEE80211_QUEUE_STOP_REASON_FLUSH,
	IEEE80211_QUEUE_STOP_REASON_TDLS_TEARDOWN,

	IEEE80211_QUEUE_STOP_REASONS,
};

#ifdef CONFIG_MAC80211_LEDS
struct tpt_led_trigger {
	struct led_trigger trig;
	char name[32];
	const struct ieee80211_tpt_blink *blink_table;
	unsigned int blink_table_len;
	struct timer_list timer;
	unsigned long prev_traffic;
	unsigned long tx_bytes, rx_bytes;
	unsigned int active, want;
	bool running;
};
#endif

/*
 * struct ieee80211_tx_latency_bin_ranges - Tx latency statistics bins ranges
 *
 * Measuring Tx latency statistics. Counts how many Tx frames transmitted in a
 * certain latency range (in Milliseconds). Each station that uses these
 * ranges will have bins to count the amount of frames received in that range.
 * The user can configure the ranges via debugfs.
 * If ranges is NULL then Tx latency statistics bins are disabled for all
 * stations.
 *
 * @n_ranges: number of ranges that are taken in account
 * @ranges: the ranges that the user requested or NULL if disabled.
 */
struct ieee80211_tx_latency_bin_ranges {
	int n_ranges;
	u32 ranges[];
};

/**
 * mac80211 scan flags - currently active scan mode
 *
 * @SCAN_SW_SCANNING: We're currently in the process of scanning but may as
 *	well be on the operating channel
 * @SCAN_HW_SCANNING: The hardware is scanning for us, we have no way to
 *	determine if we are on the operating channel or not
 * @SCAN_ONCHANNEL_SCANNING:  Do a software scan on only the current operating
 *	channel. This should not interrupt normal traffic.
 * @SCAN_COMPLETED: Set for our scan work function when the driver reported
 *	that the scan completed.
 * @SCAN_ABORTED: Set for our scan work function when the driver reported
 *	a scan complete for an aborted scan.
 * @SCAN_HW_CANCELLED: Set for our scan work function when the scan is being
 *	cancelled.
 */
enum {
	SCAN_SW_SCANNING,
	SCAN_HW_SCANNING,
	SCAN_ONCHANNEL_SCANNING,
	SCAN_COMPLETED,
	SCAN_ABORTED,
	SCAN_HW_CANCELLED,
};

/**
 * enum mac80211_scan_state - scan state machine states
 *
 * @SCAN_DECISION: Main entry point to the scan state machine, this state
 *	determines if we should keep on scanning or switch back to the
 *	operating channel
 * @SCAN_SET_CHANNEL: Set the next channel to be scanned
 * @SCAN_SEND_PROBE: Send probe requests and wait for probe responses
 * @SCAN_SUSPEND: Suspend the scan and go back to operating channel to
 *	send out data
 * @SCAN_RESUME: Resume the scan and scan the next channel
 * @SCAN_ABORT: Abort the scan and go back to operating channel
 */
enum mac80211_scan_state {
	SCAN_DECISION,
	SCAN_SET_CHANNEL,
	SCAN_SEND_PROBE,
	SCAN_SUSPEND,
	SCAN_RESUME,
	SCAN_ABORT,
};

struct ieee80211_local {
	/* embed the driver visible part.
	 * don't cast (use the static inlines below), but we keep
	 * it first anyway so they become a no-op */
	struct ieee80211_hw hw;

	const struct ieee80211_ops *ops;

	/*
	 * private workqueue to mac80211. mac80211 makes this accessible
	 * via ieee80211_queue_work()
	 */
	struct workqueue_struct *workqueue;

	unsigned long queue_stop_reasons[IEEE80211_MAX_QUEUES];
	int q_stop_reasons[IEEE80211_MAX_QUEUES][IEEE80211_QUEUE_STOP_REASONS];
	/* also used to protect ampdu_ac_queue and amdpu_ac_stop_refcnt */
	spinlock_t queue_stop_reason_lock;

	int open_count;
	int monitors, cooked_mntrs;
	/* number of interfaces with corresponding FIF_ flags */
	int fif_fcsfail, fif_plcpfail, fif_control, fif_other_bss, fif_pspoll,
	    fif_probe_req;
	int probe_req_reg;
	unsigned int filter_flags; /* FIF_* */

	bool wiphy_ciphers_allocated;

	bool use_chanctx;

	/* protects the aggregated multicast list and filter calls */
	spinlock_t filter_lock;

	/* used for uploading changed mc list */
	struct work_struct reconfig_filter;

	/* aggregated multicast list */
	struct netdev_hw_addr_list mc_list;

	bool tim_in_locked_section; /* see ieee80211_beacon_get() */

	/*
	 * suspended is true if we finished all the suspend _and_ we have
	 * not yet come up from resume. This is to be used by mac80211
	 * to ensure driver sanity during suspend and mac80211's own
	 * sanity. It can eventually be used for WoW as well.
	 */
	bool suspended;

	/*
	 * Resuming is true while suspended, but when we're reprogramming the
	 * hardware -- at that time it's allowed to use ieee80211_queue_work()
	 * again even though some other parts of the stack are still suspended
	 * and we still drop received frames to avoid waking the stack.
	 */
	bool resuming;

	/*
	 * quiescing is true during the suspend process _only_ to
	 * ease timer cancelling etc.
	 */
	bool quiescing;

	/* device is started */
	bool started;

	/* device is during a HW reconfig */
	bool in_reconfig;

	/* wowlan is enabled -- don't reconfig on resume */
	bool wowlan;

	/* DFS/radar detection is enabled */
	bool radar_detect_enabled;
	struct work_struct radar_detected_work;

	/* number of RX chains the hardware has */
	u8 rx_chains;

	int tx_headroom; /* required headroom for hardware/radiotap */

	/* Tasklet and skb queue to process calls from IRQ mode. All frames
	 * added to skb_queue will be processed, but frames in
	 * skb_queue_unreliable may be dropped if the total length of these
	 * queues increases over the limit. */
#define IEEE80211_IRQSAFE_QUEUE_LIMIT 128
	struct tasklet_struct tasklet;
	struct sk_buff_head skb_queue;
	struct sk_buff_head skb_queue_unreliable;

	spinlock_t rx_path_lock;

	/* Station data */
	/*
	 * The mutex only protects the list, hash table and
	 * counter, reads are done with RCU.
	 */
	struct mutex sta_mtx;
	spinlock_t tim_lock;
	unsigned long num_sta;
	struct list_head sta_list;
	struct sta_info __rcu *sta_hash[STA_HASH_SIZE];
	struct timer_list sta_cleanup;
	int sta_generation;

	/*
	 * Tx latency statistics parameters for all stations.
	 * Can enable via debugfs (NULL when disabled).
	 */
	struct ieee80211_tx_latency_bin_ranges __rcu *tx_latency;

	struct sk_buff_head pending[IEEE80211_MAX_QUEUES];
	struct tasklet_struct tx_pending_tasklet;

	atomic_t agg_queue_stop[IEEE80211_MAX_QUEUES];

	/* number of interfaces with corresponding IFF_ flags */
	atomic_t iff_allmultis, iff_promiscs;

	struct rate_control_ref *rate_ctrl;

	struct crypto_cipher *wep_tx_tfm;
	struct crypto_cipher *wep_rx_tfm;
	u32 wep_iv;

	/* see iface.c */
	struct list_head interfaces;
	struct mutex iflist_mtx;

	/*
	 * Key mutex, protects sdata's key_list and sta_info's
	 * key pointers (write access, they're RCU.)
	 */
	struct mutex key_mtx;

	/* mutex for scan and work locking */
	struct mutex mtx;

	/* Scanning and BSS list */
	unsigned long scanning;
	struct cfg80211_ssid scan_ssid;
	struct cfg80211_scan_request *int_scan_req;
	struct cfg80211_scan_request *scan_req;
	struct ieee80211_scan_request *hw_scan_req;
	struct cfg80211_chan_def scan_chandef;
	enum ieee80211_band hw_scan_band;
	int scan_channel_idx;
	int scan_ies_len;
	int hw_scan_ies_bufsize;

	struct work_struct sched_scan_stopped_work;
	struct ieee80211_sub_if_data __rcu *sched_scan_sdata;
	struct cfg80211_sched_scan_request *sched_scan_req;

	unsigned long leave_oper_channel_time;
	enum mac80211_scan_state next_scan_state;
	struct delayed_work scan_work;
	struct ieee80211_sub_if_data __rcu *scan_sdata;
	/* For backward compatibility only -- do not use */
	struct cfg80211_chan_def _oper_chandef;

	/* Temporary remain-on-channel for off-channel operations */
	struct ieee80211_channel *tmp_channel;

	/* channel contexts */
	struct list_head chanctx_list;
	struct mutex chanctx_mtx;

	/* SNMP counters */
	/* dot11CountersTable */
	u32 dot11TransmittedFragmentCount;
	u32 dot11MulticastTransmittedFrameCount;
	u32 dot11FailedCount;
	u32 dot11RetryCount;
	u32 dot11MultipleRetryCount;
	u32 dot11FrameDuplicateCount;
	u32 dot11ReceivedFragmentCount;
	u32 dot11MulticastReceivedFrameCount;
	u32 dot11TransmittedFrameCount;

#ifdef CONFIG_MAC80211_LEDS
	struct led_trigger *tx_led, *rx_led, *assoc_led, *radio_led;
	struct tpt_led_trigger *tpt_led_trigger;
	char tx_led_name[32], rx_led_name[32],
	     assoc_led_name[32], radio_led_name[32];
#endif

#ifdef CONFIG_MAC80211_DEBUG_COUNTERS
	/* TX/RX handler statistics */
	unsigned int tx_handlers_drop;
	unsigned int tx_handlers_queued;
	unsigned int tx_handlers_drop_unencrypted;
	unsigned int tx_handlers_drop_fragment;
	unsigned int tx_handlers_drop_wep;
	unsigned int tx_handlers_drop_not_assoc;
	unsigned int tx_handlers_drop_unauth_port;
	unsigned int rx_handlers_drop;
	unsigned int rx_handlers_queued;
	unsigned int rx_handlers_drop_nullfunc;
	unsigned int rx_handlers_drop_defrag;
	unsigned int rx_handlers_drop_short;
	unsigned int tx_expand_skb_head;
	unsigned int tx_expand_skb_head_cloned;
	unsigned int rx_expand_skb_head;
	unsigned int rx_expand_skb_head2;
	unsigned int rx_handlers_fragments;
	unsigned int tx_status_drop;
#define I802_DEBUG_INC(c) (c)++
#else /* CONFIG_MAC80211_DEBUG_COUNTERS */
#define I802_DEBUG_INC(c) do { } while (0)
#endif /* CONFIG_MAC80211_DEBUG_COUNTERS */


	int total_ps_buffered; /* total number of all buffered unicast and
				* multicast packets for power saving stations
				*/

	bool pspolling;
	bool offchannel_ps_enabled;
	/*
	 * PS can only be enabled when we have exactly one managed
	 * interface (and monitors) in PS, this then points there.
	 */
	struct ieee80211_sub_if_data *ps_sdata;
	struct work_struct dynamic_ps_enable_work;
	struct work_struct dynamic_ps_disable_work;
	struct timer_list dynamic_ps_timer;
	struct notifier_block network_latency_notifier;
	struct notifier_block ifa_notifier;
	struct notifier_block ifa6_notifier;

	/*
	 * The dynamic ps timeout configured from user space via WEXT -
	 * this will override whatever chosen by mac80211 internally.
	 */
	int dynamic_ps_forced_timeout;

	int user_power_level; /* in dBm, for all interfaces */

	enum ieee80211_smps_mode smps_mode;

	struct work_struct restart_work;

#ifdef CONFIG_MAC80211_DEBUGFS
	struct local_debugfsdentries {
		struct dentry *rcdir;
		struct dentry *keys;
	} debugfs;
#endif

	/*
	 * Remain-on-channel support
	 */
	struct list_head roc_list;
	struct work_struct hw_roc_start, hw_roc_done;
	unsigned long hw_roc_start_time;
	u64 roc_cookie_counter;

	struct idr ack_status_frames;
	spinlock_t ack_status_lock;

	struct ieee80211_sub_if_data __rcu *p2p_sdata;

	struct napi_struct *napi;

	/* virtual monitor interface */
	struct ieee80211_sub_if_data __rcu *monitor_sdata;
	struct cfg80211_chan_def monitor_chandef;
};

static inline struct ieee80211_sub_if_data *
IEEE80211_DEV_TO_SUB_IF(struct net_device *dev)
{
	return netdev_priv(dev);
}

static inline struct ieee80211_sub_if_data *
IEEE80211_WDEV_TO_SUB_IF(struct wireless_dev *wdev)
{
	return container_of(wdev, struct ieee80211_sub_if_data, wdev);
}

/* this struct represents 802.11n's RA/TID combination */
struct ieee80211_ra_tid {
	u8 ra[ETH_ALEN];
	u16 tid;
};

/* this struct holds the value parsing from channel switch IE  */
struct ieee80211_csa_ie {
	struct cfg80211_chan_def chandef;
	u8 mode;
	u8 count;
	u8 ttl;
	u16 pre_value;
};

/* Parsed Information Elements */
struct ieee802_11_elems {
	const u8 *ie_start;
	size_t total_len;

	/* pointers to IEs */
	const u8 *ssid;
	const u8 *supp_rates;
	const u8 *ds_params;
	const struct ieee80211_tim_ie *tim;
	const u8 *challenge;
	const u8 *rsn;
	const u8 *erp_info;
	const u8 *ext_supp_rates;
	const u8 *wmm_info;
	const u8 *wmm_param;
	const struct ieee80211_ht_cap *ht_cap_elem;
	const struct ieee80211_ht_operation *ht_operation;
	const struct ieee80211_vht_cap *vht_cap_elem;
	const struct ieee80211_vht_operation *vht_operation;
	const struct ieee80211_meshconf_ie *mesh_config;
	const u8 *mesh_id;
	const u8 *peering;
	const __le16 *awake_window;
	const u8 *preq;
	const u8 *prep;
	const u8 *perr;
	const struct ieee80211_rann_ie *rann;
	const struct ieee80211_channel_sw_ie *ch_switch_ie;
	const struct ieee80211_ext_chansw_ie *ext_chansw_ie;
	const struct ieee80211_wide_bw_chansw_ie *wide_bw_chansw_ie;
	const u8 *country_elem;
	const u8 *pwr_constr_elem;
	const struct ieee80211_timeout_interval_ie *timeout_int;
	const u8 *opmode_notif;
	const struct ieee80211_sec_chan_offs_ie *sec_chan_offs;
	const struct ieee80211_mesh_chansw_params_ie *mesh_chansw_params_ie;

	/* length of them, respectively */
	u8 ssid_len;
	u8 supp_rates_len;
	u8 tim_len;
	u8 challenge_len;
	u8 rsn_len;
	u8 ext_supp_rates_len;
	u8 wmm_info_len;
	u8 wmm_param_len;
	u8 mesh_id_len;
	u8 peering_len;
	u8 preq_len;
	u8 prep_len;
	u8 perr_len;
	u8 country_elem_len;

	/* whether a parse error occurred while retrieving these elements */
	bool parse_error;
};

static inline struct ieee80211_local *hw_to_local(
	struct ieee80211_hw *hw)
{
	return container_of(hw, struct ieee80211_local, hw);
}


static inline int ieee80211_bssid_match(const u8 *raddr, const u8 *addr)
{
	return ether_addr_equal(raddr, addr) ||
	       is_broadcast_ether_addr(raddr);
}

static inline bool
ieee80211_have_rx_timestamp(struct ieee80211_rx_status *status)
{
	WARN_ON_ONCE(status->flag & RX_FLAG_MACTIME_START &&
		     status->flag & RX_FLAG_MACTIME_END);
	return status->flag & (RX_FLAG_MACTIME_START | RX_FLAG_MACTIME_END);
}

u64 ieee80211_calculate_rx_timestamp(struct ieee80211_local *local,
				     struct ieee80211_rx_status *status,
				     unsigned int mpdu_len,
				     unsigned int mpdu_offset);
int ieee80211_hw_config(struct ieee80211_local *local, u32 changed);
void ieee80211_tx_set_protected(struct ieee80211_tx_data *tx);
void ieee80211_bss_info_change_notify(struct ieee80211_sub_if_data *sdata,
				      u32 changed);
void ieee80211_configure_filter(struct ieee80211_local *local);
u32 ieee80211_reset_erp_info(struct ieee80211_sub_if_data *sdata);

/* STA code */
void ieee80211_sta_setup_sdata(struct ieee80211_sub_if_data *sdata);
int ieee80211_mgd_auth(struct ieee80211_sub_if_data *sdata,
		       struct cfg80211_auth_request *req);
int ieee80211_mgd_assoc(struct ieee80211_sub_if_data *sdata,
			struct cfg80211_assoc_request *req);
int ieee80211_mgd_deauth(struct ieee80211_sub_if_data *sdata,
			 struct cfg80211_deauth_request *req);
int ieee80211_mgd_disassoc(struct ieee80211_sub_if_data *sdata,
			   struct cfg80211_disassoc_request *req);
void ieee80211_send_pspoll(struct ieee80211_local *local,
			   struct ieee80211_sub_if_data *sdata);
void ieee80211_recalc_ps(struct ieee80211_local *local, s32 latency);
void ieee80211_recalc_ps_vif(struct ieee80211_sub_if_data *sdata);
int ieee80211_max_network_latency(struct notifier_block *nb,
				  unsigned long data, void *dummy);
int ieee80211_set_arp_filter(struct ieee80211_sub_if_data *sdata);
void ieee80211_sta_work(struct ieee80211_sub_if_data *sdata);
void ieee80211_sta_rx_queued_mgmt(struct ieee80211_sub_if_data *sdata,
				  struct sk_buff *skb);
void ieee80211_sta_reset_beacon_monitor(struct ieee80211_sub_if_data *sdata);
void ieee80211_sta_reset_conn_monitor(struct ieee80211_sub_if_data *sdata);
void ieee80211_mgd_stop(struct ieee80211_sub_if_data *sdata);
void ieee80211_mgd_conn_tx_status(struct ieee80211_sub_if_data *sdata,
				  __le16 fc, bool acked);
void ieee80211_mgd_quiesce(struct ieee80211_sub_if_data *sdata);
void ieee80211_sta_restart(struct ieee80211_sub_if_data *sdata);

/* IBSS code */
void ieee80211_ibss_notify_scan_completed(struct ieee80211_local *local);
void ieee80211_ibss_setup_sdata(struct ieee80211_sub_if_data *sdata);
void ieee80211_ibss_rx_no_sta(struct ieee80211_sub_if_data *sdata,
			      const u8 *bssid, const u8 *addr, u32 supp_rates);
int ieee80211_ibss_join(struct ieee80211_sub_if_data *sdata,
			struct cfg80211_ibss_params *params);
int ieee80211_ibss_leave(struct ieee80211_sub_if_data *sdata);
void ieee80211_ibss_work(struct ieee80211_sub_if_data *sdata);
void ieee80211_ibss_rx_queued_mgmt(struct ieee80211_sub_if_data *sdata,
				   struct sk_buff *skb);
int ieee80211_ibss_csa_beacon(struct ieee80211_sub_if_data *sdata,
			      struct cfg80211_csa_settings *csa_settings);
int ieee80211_ibss_finish_csa(struct ieee80211_sub_if_data *sdata);
void ieee80211_ibss_stop(struct ieee80211_sub_if_data *sdata);

/* mesh code */
void ieee80211_mesh_work(struct ieee80211_sub_if_data *sdata);
void ieee80211_mesh_rx_queued_mgmt(struct ieee80211_sub_if_data *sdata,
				   struct sk_buff *skb);
int ieee80211_mesh_csa_beacon(struct ieee80211_sub_if_data *sdata,
			      struct cfg80211_csa_settings *csa_settings);
int ieee80211_mesh_finish_csa(struct ieee80211_sub_if_data *sdata);

/* scan/BSS handling */
void ieee80211_scan_work(struct work_struct *work);
int ieee80211_request_ibss_scan(struct ieee80211_sub_if_data *sdata,
				const u8 *ssid, u8 ssid_len,
				struct ieee80211_channel *chan,
				enum nl80211_bss_scan_width scan_width);
int ieee80211_request_scan(struct ieee80211_sub_if_data *sdata,
			   struct cfg80211_scan_request *req);
void ieee80211_scan_cancel(struct ieee80211_local *local);
void ieee80211_run_deferred_scan(struct ieee80211_local *local);
void ieee80211_scan_rx(struct ieee80211_local *local, struct sk_buff *skb);

void ieee80211_mlme_notify_scan_completed(struct ieee80211_local *local);
struct ieee80211_bss *
ieee80211_bss_info_update(struct ieee80211_local *local,
			  struct ieee80211_rx_status *rx_status,
			  struct ieee80211_mgmt *mgmt,
			  size_t len,
			  struct ieee802_11_elems *elems,
			  struct ieee80211_channel *channel);
void ieee80211_rx_bss_put(struct ieee80211_local *local,
			  struct ieee80211_bss *bss);

/* scheduled scan handling */
int
__ieee80211_request_sched_scan_start(struct ieee80211_sub_if_data *sdata,
				     struct cfg80211_sched_scan_request *req);
int ieee80211_request_sched_scan_start(struct ieee80211_sub_if_data *sdata,
				       struct cfg80211_sched_scan_request *req);
int ieee80211_request_sched_scan_stop(struct ieee80211_sub_if_data *sdata);
void ieee80211_sched_scan_end(struct ieee80211_local *local);
void ieee80211_sched_scan_stopped_work(struct work_struct *work);

/* off-channel helpers */
void ieee80211_offchannel_stop_vifs(struct ieee80211_local *local);
void ieee80211_offchannel_return(struct ieee80211_local *local);
void ieee80211_roc_setup(struct ieee80211_local *local);
void ieee80211_start_next_roc(struct ieee80211_local *local);
void ieee80211_roc_purge(struct ieee80211_local *local,
			 struct ieee80211_sub_if_data *sdata);
void ieee80211_roc_notify_destroy(struct ieee80211_roc_work *roc, bool free);
void ieee80211_sw_roc_work(struct work_struct *work);
void ieee80211_handle_roc_started(struct ieee80211_roc_work *roc);

/* channel switch handling */
void ieee80211_csa_finalize_work(struct work_struct *work);
int ieee80211_channel_switch(struct wiphy *wiphy, struct net_device *dev,
			     struct cfg80211_csa_settings *params);

/* interface handling */
int ieee80211_iface_init(void);
void ieee80211_iface_exit(void);
int ieee80211_if_add(struct ieee80211_local *local, const char *name,
		     struct wireless_dev **new_wdev, enum nl80211_iftype type,
		     struct vif_params *params);
int ieee80211_if_change_type(struct ieee80211_sub_if_data *sdata,
			     enum nl80211_iftype type);
void ieee80211_if_remove(struct ieee80211_sub_if_data *sdata);
void ieee80211_remove_interfaces(struct ieee80211_local *local);
u32 ieee80211_idle_off(struct ieee80211_local *local);
void ieee80211_recalc_idle(struct ieee80211_local *local);
void ieee80211_adjust_monitor_flags(struct ieee80211_sub_if_data *sdata,
				    const int offset);
int ieee80211_do_open(struct wireless_dev *wdev, bool coming_up);
void ieee80211_sdata_stop(struct ieee80211_sub_if_data *sdata);
int ieee80211_add_virtual_monitor(struct ieee80211_local *local);
void ieee80211_del_virtual_monitor(struct ieee80211_local *local);

bool __ieee80211_recalc_txpower(struct ieee80211_sub_if_data *sdata);
void ieee80211_recalc_txpower(struct ieee80211_sub_if_data *sdata);

static inline bool ieee80211_sdata_running(struct ieee80211_sub_if_data *sdata)
{
	return test_bit(SDATA_STATE_RUNNING, &sdata->state);
}

/* tx handling */
void ieee80211_clear_tx_pending(struct ieee80211_local *local);
void ieee80211_tx_pending(unsigned long data);
netdev_tx_t ieee80211_monitor_start_xmit(struct sk_buff *skb,
					 struct net_device *dev);
netdev_tx_t ieee80211_subif_start_xmit(struct sk_buff *skb,
				       struct net_device *dev);
void ieee80211_purge_tx_queue(struct ieee80211_hw *hw,
			      struct sk_buff_head *skbs);

/* HT */
void ieee80211_apply_htcap_overrides(struct ieee80211_sub_if_data *sdata,
				     struct ieee80211_sta_ht_cap *ht_cap);
bool ieee80211_ht_cap_ie_to_sta_ht_cap(struct ieee80211_sub_if_data *sdata,
				       struct ieee80211_supported_band *sband,
				       const struct ieee80211_ht_cap *ht_cap_ie,
				       struct sta_info *sta);
void ieee80211_send_delba(struct ieee80211_sub_if_data *sdata,
			  const u8 *da, u16 tid,
			  u16 initiator, u16 reason_code);
int ieee80211_send_smps_action(struct ieee80211_sub_if_data *sdata,
			       enum ieee80211_smps_mode smps, const u8 *da,
			       const u8 *bssid);
void ieee80211_request_smps_ap_work(struct work_struct *work);
void ieee80211_request_smps_mgd_work(struct work_struct *work);
bool ieee80211_smps_is_restrictive(enum ieee80211_smps_mode smps_mode_old,
				   enum ieee80211_smps_mode smps_mode_new);

void ___ieee80211_stop_rx_ba_session(struct sta_info *sta, u16 tid,
				     u16 initiator, u16 reason, bool stop);
void __ieee80211_stop_rx_ba_session(struct sta_info *sta, u16 tid,
				    u16 initiator, u16 reason, bool stop);
void __ieee80211_start_rx_ba_session(struct sta_info *sta,
				     u8 dialog_token, u16 timeout,
				     u16 start_seq_num, u16 ba_policy, u16 tid,
				     u16 buf_size, bool tx);
void ieee80211_sta_tear_down_BA_sessions(struct sta_info *sta,
					 enum ieee80211_agg_stop_reason reason);
void ieee80211_process_delba(struct ieee80211_sub_if_data *sdata,
			     struct sta_info *sta,
			     struct ieee80211_mgmt *mgmt, size_t len);
void ieee80211_process_addba_resp(struct ieee80211_local *local,
				  struct sta_info *sta,
				  struct ieee80211_mgmt *mgmt,
				  size_t len);
void ieee80211_process_addba_request(struct ieee80211_local *local,
				     struct sta_info *sta,
				     struct ieee80211_mgmt *mgmt,
				     size_t len);

int __ieee80211_stop_tx_ba_session(struct sta_info *sta, u16 tid,
				   enum ieee80211_agg_stop_reason reason);
int ___ieee80211_stop_tx_ba_session(struct sta_info *sta, u16 tid,
				    enum ieee80211_agg_stop_reason reason);
void ieee80211_start_tx_ba_cb(struct ieee80211_vif *vif, u8 *ra, u16 tid);
void ieee80211_stop_tx_ba_cb(struct ieee80211_vif *vif, u8 *ra, u8 tid);
void ieee80211_ba_session_work(struct work_struct *work);
void ieee80211_tx_ba_session_handle_start(struct sta_info *sta, int tid);
void ieee80211_release_reorder_timeout(struct sta_info *sta, int tid);

u8 ieee80211_mcs_to_chains(const struct ieee80211_mcs_info *mcs);

/* VHT */
void
ieee80211_vht_cap_ie_to_sta_vht_cap(struct ieee80211_sub_if_data *sdata,
				    struct ieee80211_supported_band *sband,
				    const struct ieee80211_vht_cap *vht_cap_ie,
				    struct sta_info *sta);
enum ieee80211_sta_rx_bandwidth ieee80211_sta_cur_vht_bw(struct sta_info *sta);
void ieee80211_sta_set_rx_nss(struct sta_info *sta);
u32 __ieee80211_vht_handle_opmode(struct ieee80211_sub_if_data *sdata,
                                  struct sta_info *sta, u8 opmode,
                                  enum ieee80211_band band, bool nss_only);
void ieee80211_vht_handle_opmode(struct ieee80211_sub_if_data *sdata,
				 struct sta_info *sta, u8 opmode,
				 enum ieee80211_band band, bool nss_only);
void ieee80211_apply_vhtcap_overrides(struct ieee80211_sub_if_data *sdata,
				      struct ieee80211_sta_vht_cap *vht_cap);

/* Spectrum management */
void ieee80211_process_measurement_req(struct ieee80211_sub_if_data *sdata,
				       struct ieee80211_mgmt *mgmt,
				       size_t len);
/**
 * ieee80211_parse_ch_switch_ie - parses channel switch IEs
 * @sdata: the sdata of the interface which has received the frame
 * @elems: parsed 802.11 elements received with the frame
 * @beacon: indicates if the frame was a beacon or probe response
 * @current_band: indicates the current band
 * @sta_flags: contains information about own capabilities and restrictions
 *	to decide which channel switch announcements can be accepted. Only the
 *	following subset of &enum ieee80211_sta_flags are evaluated:
 *	%IEEE80211_STA_DISABLE_HT, %IEEE80211_STA_DISABLE_VHT,
 *	%IEEE80211_STA_DISABLE_40MHZ, %IEEE80211_STA_DISABLE_80P80MHZ,
 *	%IEEE80211_STA_DISABLE_160MHZ.
 * @bssid: the currently connected bssid (for reporting)
 * @csa_ie: parsed 802.11 csa elements on count, mode, chandef and mesh ttl.
	All of them will be filled with if success only.
 * Return: 0 on success, <0 on error and >0 if there is nothing to parse.
 */
int ieee80211_parse_ch_switch_ie(struct ieee80211_sub_if_data *sdata,
				 struct ieee802_11_elems *elems, bool beacon,
				 enum ieee80211_band current_band,
				 u32 sta_flags, u8 *bssid,
				 struct ieee80211_csa_ie *csa_ie);

/* Suspend/resume and hw reconfiguration */
int ieee80211_reconfig(struct ieee80211_local *local);
void ieee80211_stop_device(struct ieee80211_local *local);

int __ieee80211_suspend(struct ieee80211_hw *hw,
			struct cfg80211_wowlan *wowlan);

static inline int __ieee80211_resume(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);

	WARN(test_bit(SCAN_HW_SCANNING, &local->scanning),
		"%s: resume with hardware scan still in progress\n",
		wiphy_name(hw->wiphy));

	return ieee80211_reconfig(hw_to_local(hw));
}

/* utility functions/constants */
extern const void *const mac80211_wiphy_privid; /* for wiphy privid */
u8 *ieee80211_get_bssid(struct ieee80211_hdr *hdr, size_t len,
			enum nl80211_iftype type);
int ieee80211_frame_duration(enum ieee80211_band band, size_t len,
			     int rate, int erp, int short_preamble,
			     int shift);
void mac80211_ev_michael_mic_failure(struct ieee80211_sub_if_data *sdata, int keyidx,
				     struct ieee80211_hdr *hdr, const u8 *tsc,
				     gfp_t gfp);
void ieee80211_set_wmm_default(struct ieee80211_sub_if_data *sdata,
			       bool bss_notify);
void ieee80211_xmit(struct ieee80211_sub_if_data *sdata, struct sk_buff *skb,
		    enum ieee80211_band band);

void __ieee80211_tx_skb_tid_band(struct ieee80211_sub_if_data *sdata,
				 struct sk_buff *skb, int tid,
				 enum ieee80211_band band);

static inline void
ieee80211_tx_skb_tid_band(struct ieee80211_sub_if_data *sdata,
			  struct sk_buff *skb, int tid,
			  enum ieee80211_band band)
{
	rcu_read_lock();
	__ieee80211_tx_skb_tid_band(sdata, skb, tid, band);
	rcu_read_unlock();
}

static inline void ieee80211_tx_skb_tid(struct ieee80211_sub_if_data *sdata,
					struct sk_buff *skb, int tid)
{
	struct ieee80211_chanctx_conf *chanctx_conf;

	rcu_read_lock();
	chanctx_conf = rcu_dereference(sdata->vif.chanctx_conf);
	if (WARN_ON(!chanctx_conf)) {
		rcu_read_unlock();
		kfree_skb(skb);
		return;
	}

	__ieee80211_tx_skb_tid_band(sdata, skb, tid,
				    chanctx_conf->def.chan->band);
	rcu_read_unlock();
}

static inline void ieee80211_tx_skb(struct ieee80211_sub_if_data *sdata,
				    struct sk_buff *skb)
{
	/* Send all internal mgmt frames on VO. Accordingly set TID to 7. */
	ieee80211_tx_skb_tid(sdata, skb, 7);
}

u32 ieee802_11_parse_elems_crc(const u8 *start, size_t len, bool action,
			       struct ieee802_11_elems *elems,
			       u64 filter, u32 crc);
static inline void ieee802_11_parse_elems(const u8 *start, size_t len,
					  bool action,
					  struct ieee802_11_elems *elems)
{
	ieee802_11_parse_elems_crc(start, len, action, elems, 0, 0);
}

static inline bool ieee80211_rx_reorder_ready(struct sk_buff_head *frames)
{
	struct sk_buff *tail = skb_peek_tail(frames);
	struct ieee80211_rx_status *status;

	if (!tail)
		return false;

	status = IEEE80211_SKB_RXCB(tail);
	if (status->flag & RX_FLAG_AMSDU_MORE)
		return false;

	return true;
}

void ieee80211_dynamic_ps_enable_work(struct work_struct *work);
void ieee80211_dynamic_ps_disable_work(struct work_struct *work);
void ieee80211_dynamic_ps_timer(unsigned long data);
void ieee80211_send_nullfunc(struct ieee80211_local *local,
			     struct ieee80211_sub_if_data *sdata,
			     int powersave);
void ieee80211_sta_rx_notify(struct ieee80211_sub_if_data *sdata,
			     struct ieee80211_hdr *hdr);
void ieee80211_sta_tx_notify(struct ieee80211_sub_if_data *sdata,
			     struct ieee80211_hdr *hdr, bool ack);

void ieee80211_wake_queues_by_reason(struct ieee80211_hw *hw,
				     unsigned long queues,
				     enum queue_stop_reason reason,
				     bool refcounted);
void ieee80211_stop_vif_queues(struct ieee80211_local *local,
			       struct ieee80211_sub_if_data *sdata,
			       enum queue_stop_reason reason);
void ieee80211_wake_vif_queues(struct ieee80211_local *local,
			       struct ieee80211_sub_if_data *sdata,
			       enum queue_stop_reason reason);
void ieee80211_stop_queues_by_reason(struct ieee80211_hw *hw,
				     unsigned long queues,
				     enum queue_stop_reason reason,
				     bool refcounted);
void ieee80211_wake_queue_by_reason(struct ieee80211_hw *hw, int queue,
				    enum queue_stop_reason reason,
				    bool refcounted);
void ieee80211_stop_queue_by_reason(struct ieee80211_hw *hw, int queue,
				    enum queue_stop_reason reason,
				    bool refcounted);
void ieee80211_propagate_queue_wake(struct ieee80211_local *local, int queue);
void ieee80211_add_pending_skb(struct ieee80211_local *local,
			       struct sk_buff *skb);
void ieee80211_add_pending_skbs(struct ieee80211_local *local,
				struct sk_buff_head *skbs);
void ieee80211_flush_queues(struct ieee80211_local *local,
			    struct ieee80211_sub_if_data *sdata);

void ieee80211_send_auth(struct ieee80211_sub_if_data *sdata,
			 u16 transaction, u16 auth_alg, u16 status,
			 const u8 *extra, size_t extra_len, const u8 *bssid,
			 const u8 *da, const u8 *key, u8 key_len, u8 key_idx,
			 u32 tx_flags);
void ieee80211_send_deauth_disassoc(struct ieee80211_sub_if_data *sdata,
				    const u8 *bssid, u16 stype, u16 reason,
				    bool send_frame, u8 *frame_buf);
int ieee80211_build_preq_ies(struct ieee80211_local *local, u8 *buffer,
			     size_t buffer_len,
			     struct ieee80211_scan_ies *ie_desc,
			     const u8 *ie, size_t ie_len,
			     u8 bands_used, u32 *rate_masks,
			     struct cfg80211_chan_def *chandef);
struct sk_buff *ieee80211_build_probe_req(struct ieee80211_sub_if_data *sdata,
					  u8 *dst, u32 ratemask,
					  struct ieee80211_channel *chan,
					  const u8 *ssid, size_t ssid_len,
					  const u8 *ie, size_t ie_len,
					  bool directed);
void ieee80211_send_probe_req(struct ieee80211_sub_if_data *sdata, u8 *dst,
			      const u8 *ssid, size_t ssid_len,
			      const u8 *ie, size_t ie_len,
			      u32 ratemask, bool directed, u32 tx_flags,
			      struct ieee80211_channel *channel, bool scan);

u32 ieee80211_sta_get_rates(struct ieee80211_sub_if_data *sdata,
			    struct ieee802_11_elems *elems,
			    enum ieee80211_band band, u32 *basic_rates);
int __ieee80211_request_smps_mgd(struct ieee80211_sub_if_data *sdata,
				 enum ieee80211_smps_mode smps_mode);
int __ieee80211_request_smps_ap(struct ieee80211_sub_if_data *sdata,
				enum ieee80211_smps_mode smps_mode);
void ieee80211_recalc_smps(struct ieee80211_sub_if_data *sdata);
void ieee80211_recalc_min_chandef(struct ieee80211_sub_if_data *sdata);

size_t ieee80211_ie_split(const u8 *ies, size_t ielen,
			  const u8 *ids, int n_ids, size_t offset);
size_t ieee80211_ie_split_vendor(const u8 *ies, size_t ielen, size_t offset);
u8 *ieee80211_ie_build_ht_cap(u8 *pos, struct ieee80211_sta_ht_cap *ht_cap,
			      u16 cap);
u8 *ieee80211_ie_build_ht_oper(u8 *pos, struct ieee80211_sta_ht_cap *ht_cap,
			       const struct cfg80211_chan_def *chandef,
			       u16 prot_mode);
u8 *ieee80211_ie_build_vht_cap(u8 *pos, struct ieee80211_sta_vht_cap *vht_cap,
			       u32 cap);
int ieee80211_parse_bitrates(struct cfg80211_chan_def *chandef,
			     const struct ieee80211_supported_band *sband,
			     const u8 *srates, int srates_len, u32 *rates);
int ieee80211_add_srates_ie(struct ieee80211_sub_if_data *sdata,
			    struct sk_buff *skb, bool need_basic,
			    enum ieee80211_band band);
int ieee80211_add_ext_srates_ie(struct ieee80211_sub_if_data *sdata,
				struct sk_buff *skb, bool need_basic,
				enum ieee80211_band band);
u8 *ieee80211_add_wmm_info_ie(u8 *buf, u8 qosinfo);

/* channel management */
void ieee80211_ht_oper_to_chandef(struct ieee80211_channel *control_chan,
				  const struct ieee80211_ht_operation *ht_oper,
				  struct cfg80211_chan_def *chandef);
u32 ieee80211_chandef_downgrade(struct cfg80211_chan_def *c);

int __must_check
ieee80211_vif_use_channel(struct ieee80211_sub_if_data *sdata,
			  const struct cfg80211_chan_def *chandef,
			  enum ieee80211_chanctx_mode mode);
int __must_check
ieee80211_vif_reserve_chanctx(struct ieee80211_sub_if_data *sdata,
			      const struct cfg80211_chan_def *chandef,
			      enum ieee80211_chanctx_mode mode,
			      bool radar_required);
int __must_check
ieee80211_vif_use_reserved_context(struct ieee80211_sub_if_data *sdata);
int ieee80211_vif_unreserve_chanctx(struct ieee80211_sub_if_data *sdata);
int ieee80211_vif_use_reserved_switch(struct ieee80211_local *local);

int __must_check
ieee80211_vif_change_bandwidth(struct ieee80211_sub_if_data *sdata,
			       const struct cfg80211_chan_def *chandef,
			       u32 *changed);
void ieee80211_vif_release_channel(struct ieee80211_sub_if_data *sdata);
void ieee80211_vif_vlan_copy_chanctx(struct ieee80211_sub_if_data *sdata);
void ieee80211_vif_copy_chanctx_to_vlans(struct ieee80211_sub_if_data *sdata,
					 bool clear);
int ieee80211_chanctx_refcount(struct ieee80211_local *local,
			       struct ieee80211_chanctx *ctx);

void ieee80211_recalc_smps_chanctx(struct ieee80211_local *local,
				   struct ieee80211_chanctx *chanctx);
void ieee80211_recalc_chanctx_min_def(struct ieee80211_local *local,
				      struct ieee80211_chanctx *ctx);

void ieee80211_dfs_cac_timer(unsigned long data);
void ieee80211_dfs_cac_timer_work(struct work_struct *work);
void ieee80211_dfs_cac_cancel(struct ieee80211_local *local);
void ieee80211_dfs_radar_detected_work(struct work_struct *work);
int ieee80211_send_action_csa(struct ieee80211_sub_if_data *sdata,
			      struct cfg80211_csa_settings *csa_settings);

bool ieee80211_cs_valid(const struct ieee80211_cipher_scheme *cs);
bool ieee80211_cs_list_valid(const struct ieee80211_cipher_scheme *cs, int n);
const struct ieee80211_cipher_scheme *
ieee80211_cs_get(struct ieee80211_local *local, u32 cipher,
		 enum nl80211_iftype iftype);
int ieee80211_cs_headroom(struct ieee80211_local *local,
			  struct cfg80211_crypto_settings *crypto,
			  enum nl80211_iftype iftype);
void ieee80211_recalc_dtim(struct ieee80211_local *local,
			   struct ieee80211_sub_if_data *sdata);
int ieee80211_check_combinations(struct ieee80211_sub_if_data *sdata,
				 const struct cfg80211_chan_def *chandef,
				 enum ieee80211_chanctx_mode chanmode,
				 u8 radar_detect);
int ieee80211_max_num_channels(struct ieee80211_local *local);

/* TDLS */
int ieee80211_tdls_mgmt(struct wiphy *wiphy, struct net_device *dev,
			const u8 *peer, u8 action_code, u8 dialog_token,
			u16 status_code, u32 peer_capability,
			bool initiator, const u8 *extra_ies,
			size_t extra_ies_len);
int ieee80211_tdls_oper(struct wiphy *wiphy, struct net_device *dev,
			const u8 *peer, enum nl80211_tdls_operation oper);


extern const struct ethtool_ops ieee80211_ethtool_ops;

#ifdef CONFIG_MAC80211_NOINLINE
#define debug_noinline noinline
#else
#define debug_noinline
#endif

#endif /* IEEE80211_I_H */
void ieee80211_tdls_peer_del_work(struct work_struct *wk);
