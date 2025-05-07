/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2002-2005, Devicescape Software, Inc.
 * Copyright 2013-2014  Intel Mobile Communications GmbH
 * Copyright(c) 2015-2017 Intel Deutschland GmbH
 * Copyright(c) 2020-2024 Intel Corporation
 */

#ifndef STA_INFO_H
#define STA_INFO_H

#include <linux/list.h>
#include <linux/types.h>
#include <linux/if_ether.h>
#include <linux/workqueue.h>
#include <linux/average.h>
#include <linux/bitfield.h>
#include <linux/etherdevice.h>
#include <linux/rhashtable.h>
#include <linux/u64_stats_sync.h>
#include "key.h"

/**
 * enum ieee80211_sta_info_flags - Stations flags
 *
 * These flags are used with &struct sta_info's @flags member, but
 * only indirectly with set_sta_flag() and friends.
 *
 * @WLAN_STA_AUTH: Station is authenticated.
 * @WLAN_STA_ASSOC: Station is associated.
 * @WLAN_STA_PS_STA: Station is in power-save mode
 * @WLAN_STA_AUTHORIZED: Station is authorized to send/receive traffic.
 *	This bit is always checked so needs to be enabled for all stations
 *	when virtual port control is not in use.
 * @WLAN_STA_SHORT_PREAMBLE: Station is capable of receiving short-preamble
 *	frames.
 * @WLAN_STA_WDS: Station is one of our WDS peers.
 * @WLAN_STA_CLEAR_PS_FILT: Clear PS filter in hardware (using the
 *	IEEE80211_TX_CTL_CLEAR_PS_FILT control flag) when the next
 *	frame to this station is transmitted.
 * @WLAN_STA_MFP: Management frame protection is used with this STA.
 * @WLAN_STA_BLOCK_BA: Used to deny ADDBA requests (both TX and RX)
 *	during suspend/resume and station removal.
 * @WLAN_STA_PS_DRIVER: driver requires keeping this station in
 *	power-save mode logically to flush frames that might still
 *	be in the queues
 * @WLAN_STA_PSPOLL: Station sent PS-poll while driver was keeping
 *	station in power-save mode, reply when the driver unblocks.
 * @WLAN_STA_TDLS_PEER: Station is a TDLS peer.
 * @WLAN_STA_TDLS_PEER_AUTH: This TDLS peer is authorized to send direct
 *	packets. This means the link is enabled.
 * @WLAN_STA_TDLS_INITIATOR: We are the initiator of the TDLS link with this
 *	station.
 * @WLAN_STA_TDLS_CHAN_SWITCH: This TDLS peer supports TDLS channel-switching
 * @WLAN_STA_TDLS_OFF_CHANNEL: The local STA is currently off-channel with this
 *	TDLS peer
 * @WLAN_STA_TDLS_WIDER_BW: This TDLS peer supports working on a wider bw on
 *	the BSS base channel.
 * @WLAN_STA_UAPSD: Station requested unscheduled SP while driver was
 *	keeping station in power-save mode, reply when the driver
 *	unblocks the station.
 * @WLAN_STA_SP: Station is in a service period, so don't try to
 *	reply to other uAPSD trigger frames or PS-Poll.
 * @WLAN_STA_4ADDR_EVENT: 4-addr event was already sent for this frame.
 * @WLAN_STA_INSERTED: This station is inserted into the hash table.
 * @WLAN_STA_RATE_CONTROL: rate control was initialized for this station.
 * @WLAN_STA_TOFFSET_KNOWN: toffset calculated for this station is valid.
 * @WLAN_STA_MPSP_OWNER: local STA is owner of a mesh Peer Service Period.
 * @WLAN_STA_MPSP_RECIPIENT: local STA is recipient of a MPSP.
 * @WLAN_STA_PS_DELIVER: station woke up, but we're still blocking TX
 *	until pending frames are delivered
 * @WLAN_STA_USES_ENCRYPTION: This station was configured for encryption,
 *	so drop all packets without a key later.
 * @WLAN_STA_DECAP_OFFLOAD: This station uses rx decap offload
 *
 * @NUM_WLAN_STA_FLAGS: number of defined flags
 */
enum ieee80211_sta_info_flags {
	WLAN_STA_AUTH,
	WLAN_STA_ASSOC,
	WLAN_STA_PS_STA,
	WLAN_STA_AUTHORIZED,
	WLAN_STA_SHORT_PREAMBLE,
	WLAN_STA_WDS,
	WLAN_STA_CLEAR_PS_FILT,
	WLAN_STA_MFP,
	WLAN_STA_BLOCK_BA,
	WLAN_STA_PS_DRIVER,
	WLAN_STA_PSPOLL,
	WLAN_STA_TDLS_PEER,
	WLAN_STA_TDLS_PEER_AUTH,
	WLAN_STA_TDLS_INITIATOR,
	WLAN_STA_TDLS_CHAN_SWITCH,
	WLAN_STA_TDLS_OFF_CHANNEL,
	WLAN_STA_TDLS_WIDER_BW,
	WLAN_STA_UAPSD,
	WLAN_STA_SP,
	WLAN_STA_4ADDR_EVENT,
	WLAN_STA_INSERTED,
	WLAN_STA_RATE_CONTROL,
	WLAN_STA_TOFFSET_KNOWN,
	WLAN_STA_MPSP_OWNER,
	WLAN_STA_MPSP_RECIPIENT,
	WLAN_STA_PS_DELIVER,
	WLAN_STA_USES_ENCRYPTION,
	WLAN_STA_DECAP_OFFLOAD,

	NUM_WLAN_STA_FLAGS,
};

#define ADDBA_RESP_INTERVAL HZ
#define HT_AGG_MAX_RETRIES		15
#define HT_AGG_BURST_RETRIES		3
#define HT_AGG_RETRIES_PERIOD		(15 * HZ)

#define HT_AGG_STATE_DRV_READY		0
#define HT_AGG_STATE_RESPONSE_RECEIVED	1
#define HT_AGG_STATE_OPERATIONAL	2
#define HT_AGG_STATE_STOPPING		3
#define HT_AGG_STATE_WANT_START		4
#define HT_AGG_STATE_WANT_STOP		5
#define HT_AGG_STATE_START_CB		6
#define HT_AGG_STATE_STOP_CB		7
#define HT_AGG_STATE_SENT_ADDBA		8

DECLARE_EWMA(avg_signal, 10, 8)
enum ieee80211_agg_stop_reason {
	AGG_STOP_DECLINED,
	AGG_STOP_LOCAL_REQUEST,
	AGG_STOP_PEER_REQUEST,
	AGG_STOP_DESTROY_STA,
};

/* Debugfs flags to enable/disable use of RX/TX airtime in scheduler */
#define AIRTIME_USE_TX		BIT(0)
#define AIRTIME_USE_RX		BIT(1)

struct airtime_info {
	u64 rx_airtime;
	u64 tx_airtime;
	unsigned long last_active;
	s32 deficit;
	atomic_t aql_tx_pending; /* Estimated airtime for frames pending */
	u32 aql_limit_low;
	u32 aql_limit_high;
};

void ieee80211_sta_update_pending_airtime(struct ieee80211_local *local,
					  struct sta_info *sta, u8 ac,
					  u16 tx_airtime, bool tx_completed);

struct sta_info;

/**
 * struct tid_ampdu_tx - TID aggregation information (Tx).
 *
 * @rcu_head: rcu head for freeing structure
 * @session_timer: check if we keep Tx-ing on the TID (by timeout value)
 * @addba_resp_timer: timer for peer's response to addba request
 * @pending: pending frames queue -- use sta's spinlock to protect
 * @sta: station we are attached to
 * @dialog_token: dialog token for aggregation session
 * @timeout: session timeout value to be filled in ADDBA requests
 * @tid: TID number
 * @state: session state (see above)
 * @last_tx: jiffies of last tx activity
 * @stop_initiator: initiator of a session stop
 * @tx_stop: TX DelBA frame when stopping
 * @buf_size: reorder buffer size at receiver
 * @failed_bar_ssn: ssn of the last failed BAR tx attempt
 * @bar_pending: BAR needs to be re-sent
 * @amsdu: support A-MSDU within A-MDPU
 * @ssn: starting sequence number of the session
 *
 * This structure's lifetime is managed by RCU, assignments to
 * the array holding it must hold the aggregation mutex.
 *
 * The TX path can access it under RCU lock-free if, and
 * only if, the state has the flag %HT_AGG_STATE_OPERATIONAL
 * set. Otherwise, the TX path must also acquire the spinlock
 * and re-check the state, see comments in the tx code
 * touching it.
 */
struct tid_ampdu_tx {
	struct rcu_head rcu_head;
	struct timer_list session_timer;
	struct timer_list addba_resp_timer;
	struct sk_buff_head pending;
	struct sta_info *sta;
	unsigned long state;
	unsigned long last_tx;
	u16 timeout;
	u8 dialog_token;
	u8 stop_initiator;
	bool tx_stop;
	u16 buf_size;
	u16 ssn;

	u16 failed_bar_ssn;
	bool bar_pending;
	bool amsdu;
	u8 tid;
};

/**
 * struct tid_ampdu_rx - TID aggregation information (Rx).
 *
 * @reorder_buf: buffer to reorder incoming aggregated MPDUs. An MPDU may be an
 *	A-MSDU with individually reported subframes.
 * @reorder_buf_filtered: bitmap indicating where there are filtered frames in
 *	the reorder buffer that should be ignored when releasing frames
 * @reorder_time: jiffies when skb was added
 * @session_timer: check if peer keeps Tx-ing on the TID (by timeout value)
 * @reorder_timer: releases expired frames from the reorder buffer.
 * @sta: station we are attached to
 * @last_rx: jiffies of last rx activity
 * @head_seq_num: head sequence number in reordering buffer.
 * @stored_mpdu_num: number of MPDUs in reordering buffer
 * @ssn: Starting Sequence Number expected to be aggregated.
 * @buf_size: buffer size for incoming A-MPDUs
 * @timeout: reset timer value (in TUs).
 * @tid: TID number
 * @rcu_head: RCU head used for freeing this struct
 * @reorder_lock: serializes access to reorder buffer, see below.
 * @auto_seq: used for offloaded BA sessions to automatically pick head_seq_and
 *	and ssn.
 * @removed: this session is removed (but might have been found due to RCU)
 * @started: this session has started (head ssn or higher was received)
 *
 * This structure's lifetime is managed by RCU, assignments to
 * the array holding it must hold the aggregation mutex.
 *
 * The @reorder_lock is used to protect the members of this
 * struct, except for @timeout, @buf_size and @dialog_token,
 * which are constant across the lifetime of the struct (the
 * dialog token being used only for debugging).
 */
struct tid_ampdu_rx {
	struct rcu_head rcu_head;
	spinlock_t reorder_lock;
	u64 reorder_buf_filtered;
	struct sk_buff_head *reorder_buf;
	unsigned long *reorder_time;
	struct sta_info *sta;
	struct timer_list session_timer;
	struct timer_list reorder_timer;
	unsigned long last_rx;
	u16 head_seq_num;
	u16 stored_mpdu_num;
	u16 ssn;
	u16 buf_size;
	u16 timeout;
	u8 tid;
	u8 auto_seq:1,
	   removed:1,
	   started:1;
};

/**
 * struct sta_ampdu_mlme - STA aggregation information.
 *
 * @tid_rx: aggregation info for Rx per TID -- RCU protected
 * @tid_rx_token: dialog tokens for valid aggregation sessions
 * @tid_rx_timer_expired: bitmap indicating on which TIDs the
 *	RX timer expired until the work for it runs
 * @tid_rx_stop_requested:  bitmap indicating which BA sessions per TID the
 *	driver requested to close until the work for it runs
 * @tid_rx_manage_offl: bitmap indicating which BA sessions were requested
 *	to be treated as started/stopped due to offloading
 * @agg_session_valid: bitmap indicating which TID has a rx BA session open on
 * @unexpected_agg: bitmap indicating which TID already sent a delBA due to
 *	unexpected aggregation related frames outside a session
 * @work: work struct for starting/stopping aggregation
 * @tid_tx: aggregation info for Tx per TID
 * @tid_start_tx: sessions where start was requested, not just protected
 *	by wiphy mutex but also sta->lock
 * @last_addba_req_time: timestamp of the last addBA request.
 * @addba_req_num: number of times addBA request has been sent.
 * @dialog_token_allocator: dialog token enumerator for each new session;
 */
struct sta_ampdu_mlme {
	/* rx */
	struct tid_ampdu_rx __rcu *tid_rx[IEEE80211_NUM_TIDS];
	u8 tid_rx_token[IEEE80211_NUM_TIDS];
	unsigned long tid_rx_timer_expired[BITS_TO_LONGS(IEEE80211_NUM_TIDS)];
	unsigned long tid_rx_stop_requested[BITS_TO_LONGS(IEEE80211_NUM_TIDS)];
	unsigned long tid_rx_manage_offl[BITS_TO_LONGS(2 * IEEE80211_NUM_TIDS)];
	unsigned long agg_session_valid[BITS_TO_LONGS(IEEE80211_NUM_TIDS)];
	unsigned long unexpected_agg[BITS_TO_LONGS(IEEE80211_NUM_TIDS)];
	/* tx */
	struct wiphy_work work;
	struct tid_ampdu_tx __rcu *tid_tx[IEEE80211_NUM_TIDS];
	struct tid_ampdu_tx *tid_start_tx[IEEE80211_NUM_TIDS];
	unsigned long last_addba_req_time[IEEE80211_NUM_TIDS];
	u8 addba_req_num[IEEE80211_NUM_TIDS];
	u8 dialog_token_allocator;
};


/* Value to indicate no TID reservation */
#define IEEE80211_TID_UNRESERVED	0xff

#define IEEE80211_FAST_XMIT_MAX_IV	18

/**
 * struct ieee80211_fast_tx - TX fastpath information
 * @key: key to use for hw crypto
 * @hdr: the 802.11 header to put with the frame
 * @hdr_len: actual 802.11 header length
 * @sa_offs: offset of the SA
 * @da_offs: offset of the DA
 * @pn_offs: offset where to put PN for crypto (or 0 if not needed)
 * @band: band this will be transmitted on, for tx_info
 * @rcu_head: RCU head to free this struct
 *
 * This struct is small enough so that the common case (maximum crypto
 * header length of 8 like for CCMP/GCMP) fits into a single 64-byte
 * cache line.
 */
struct ieee80211_fast_tx {
	struct ieee80211_key *key;
	u8 hdr_len;
	u8 sa_offs, da_offs, pn_offs;
	u8 band;
	u8 hdr[30 + 2 + IEEE80211_FAST_XMIT_MAX_IV +
	       sizeof(rfc1042_header)] __aligned(2);

	struct rcu_head rcu_head;
};

/**
 * struct ieee80211_fast_rx - RX fastpath information
 * @dev: netdevice for reporting the SKB
 * @vif_type: (P2P-less) interface type of the original sdata (sdata->vif.type)
 * @vif_addr: interface address
 * @rfc1042_hdr: copy of the RFC 1042 SNAP header (to have in cache)
 * @control_port_protocol: control port protocol copied from sdata
 * @expected_ds_bits: from/to DS bits expected
 * @icv_len: length of the MIC if present
 * @key: bool indicating encryption is expected (key is set)
 * @internal_forward: forward froms internally on AP/VLAN type interfaces
 * @uses_rss: copy of USES_RSS hw flag
 * @da_offs: offset of the DA in the header (for header conversion)
 * @sa_offs: offset of the SA in the header (for header conversion)
 * @rcu_head: RCU head for freeing this structure
 */
struct ieee80211_fast_rx {
	struct net_device *dev;
	enum nl80211_iftype vif_type;
	u8 vif_addr[ETH_ALEN] __aligned(2);
	u8 rfc1042_hdr[6] __aligned(2);
	__be16 control_port_protocol;
	__le16 expected_ds_bits;
	u8 icv_len;
	u8 key:1,
	   internal_forward:1,
	   uses_rss:1;
	u8 da_offs, sa_offs;

	struct rcu_head rcu_head;
};

/* we use only values in the range 0-100, so pick a large precision */
DECLARE_EWMA(mesh_fail_avg, 20, 8)
DECLARE_EWMA(mesh_tx_rate_avg, 8, 16)

/**
 * struct mesh_sta - mesh STA information
 * @plink_lock: serialize access to plink fields
 * @llid: Local link ID
 * @plid: Peer link ID
 * @aid: local aid supplied by peer
 * @reason: Cancel reason on PLINK_HOLDING state
 * @plink_retries: Retries in establishment
 * @plink_state: peer link state
 * @plink_timeout: timeout of peer link
 * @plink_timer: peer link watch timer
 * @plink_sta: peer link watch timer's sta_info
 * @t_offset: timing offset relative to this host
 * @t_offset_setpoint: reference timing offset of this sta to be used when
 * 	calculating clockdrift
 * @local_pm: local link-specific power save mode
 * @peer_pm: peer-specific power save mode towards local STA
 * @nonpeer_pm: STA power save mode towards non-peer neighbors
 * @processed_beacon: set to true after peer rates and capabilities are
 *	processed
 * @connected_to_gate: true if mesh STA has a path to a mesh gate
 * @connected_to_as: true if mesh STA has a path to a authentication server
 * @fail_avg: moving percentage of failed MSDUs
 * @tx_rate_avg: moving average of tx bitrate
 */
struct mesh_sta {
	struct timer_list plink_timer;
	struct sta_info *plink_sta;

	s64 t_offset;
	s64 t_offset_setpoint;

	spinlock_t plink_lock;
	u16 llid;
	u16 plid;
	u16 aid;
	u16 reason;
	u8 plink_retries;

	bool processed_beacon;
	bool connected_to_gate;
	bool connected_to_as;

	enum nl80211_plink_state plink_state;
	u32 plink_timeout;

	/* mesh power save */
	enum nl80211_mesh_power_mode local_pm;
	enum nl80211_mesh_power_mode peer_pm;
	enum nl80211_mesh_power_mode nonpeer_pm;

	/* moving percentage of failed MSDUs */
	struct ewma_mesh_fail_avg fail_avg;
	/* moving average of tx bitrate */
	struct ewma_mesh_tx_rate_avg tx_rate_avg;
};

DECLARE_EWMA(signal, 10, 8)

struct ieee80211_sta_rx_stats {
	unsigned long packets;
	unsigned long last_rx;
	unsigned long num_duplicates;
	unsigned long fragments;
	unsigned long dropped;
	int last_signal;
	u8 chains;
	s8 chain_signal_last[IEEE80211_MAX_CHAINS];
	u32 last_rate;
	struct u64_stats_sync syncp;
	u64 bytes;
	u64 msdu[IEEE80211_NUM_TIDS + 1];
};

/*
 * IEEE 802.11-2016 (10.6 "Defragmentation") recommends support for "concurrent
 * reception of at least one MSDU per access category per associated STA"
 * on APs, or "at least one MSDU per access category" on other interface types.
 *
 * This limit can be increased by changing this define, at the cost of slower
 * frame reassembly and increased memory use while fragments are pending.
 */
#define IEEE80211_FRAGMENT_MAX 4

struct ieee80211_fragment_entry {
	struct sk_buff_head skb_list;
	unsigned long first_frag_time;
	u16 seq;
	u16 extra_len;
	u16 last_frag;
	u8 rx_queue;
	u8 check_sequential_pn:1, /* needed for CCMP/GCMP */
	   is_protected:1;
	u8 last_pn[6]; /* PN of the last fragment if CCMP was used */
	unsigned int key_color;
};

struct ieee80211_fragment_cache {
	struct ieee80211_fragment_entry	entries[IEEE80211_FRAGMENT_MAX];
	unsigned int next;
};

/**
 * struct link_sta_info - Link STA information
 * All link specific sta info are stored here for reference. This can be
 * a single entry for non-MLD STA or multiple entries for MLD STA
 * @addr: Link MAC address - Can be same as MLD STA mac address and is always
 *	same for non-MLD STA. This is used as key for searching link STA
 * @link_id: Link ID uniquely identifying the link STA. This is 0 for non-MLD
 *	and set to the corresponding vif LinkId for MLD STA
 * @op_mode_nss: NSS limit as set by operating mode notification, or 0
 * @capa_nss: NSS limit as determined by local and peer capabilities
 * @link_hash_node: hash node for rhashtable
 * @sta: Points to the STA info
 * @gtk: group keys negotiated with this station, if any
 * @tx_stats: TX statistics
 * @tx_stats.packets: # of packets transmitted
 * @tx_stats.bytes: # of bytes in all packets transmitted
 * @tx_stats.last_rate: last TX rate
 * @tx_stats.msdu: # of transmitted MSDUs per TID
 * @rx_stats: RX statistics
 * @rx_stats_avg: averaged RX statistics
 * @rx_stats_avg.signal: averaged signal
 * @rx_stats_avg.chain_signal: averaged per-chain signal
 * @pcpu_rx_stats: per-CPU RX statistics, assigned only if the driver needs
 *	this (by advertising the USES_RSS hw flag)
 * @status_stats: TX status statistics
 * @status_stats.filtered: # of filtered frames
 * @status_stats.retry_failed: # of frames that failed after retry
 * @status_stats.retry_count: # of retries attempted
 * @status_stats.lost_packets: # of lost packets
 * @status_stats.last_pkt_time: timestamp of last ACKed packet
 * @status_stats.msdu_retries: # of MSDU retries
 * @status_stats.msdu_failed: # of failed MSDUs
 * @status_stats.last_ack: last ack timestamp (jiffies)
 * @status_stats.last_ack_signal: last ACK signal
 * @status_stats.ack_signal_filled: last ACK signal validity
 * @status_stats.avg_ack_signal: average ACK signal
 * @cur_max_bandwidth: maximum bandwidth to use for TX to the station,
 *	taken from HT/VHT capabilities or VHT operating mode notification
 * @rx_omi_bw_rx: RX OMI bandwidth restriction to apply for RX
 * @rx_omi_bw_tx: RX OMI bandwidth restriction to apply for TX
 * @rx_omi_bw_staging: RX OMI bandwidth restriction to apply later
 *	during finalize
 * @debugfs_dir: debug filesystem directory dentry
 * @pub: public (driver visible) link STA data
 * TODO Move other link params from sta_info as required for MLD operation
 */
struct link_sta_info {
	u8 addr[ETH_ALEN];
	u8 link_id;

	u8 op_mode_nss, capa_nss;

	struct rhlist_head link_hash_node;

	struct sta_info *sta;
	struct ieee80211_key __rcu *gtk[NUM_DEFAULT_KEYS +
					NUM_DEFAULT_MGMT_KEYS +
					NUM_DEFAULT_BEACON_KEYS];
	struct ieee80211_sta_rx_stats __percpu *pcpu_rx_stats;

	/* Updated from RX path only, no locking requirements */
	struct ieee80211_sta_rx_stats rx_stats;
	struct {
		struct ewma_signal signal;
		struct ewma_signal chain_signal[IEEE80211_MAX_CHAINS];
	} rx_stats_avg;

	/* Updated from TX status path only, no locking requirements */
	struct {
		unsigned long filtered;
		unsigned long retry_failed, retry_count;
		unsigned int lost_packets;
		unsigned long last_pkt_time;
		u64 msdu_retries[IEEE80211_NUM_TIDS + 1];
		u64 msdu_failed[IEEE80211_NUM_TIDS + 1];
		unsigned long last_ack;
		s8 last_ack_signal;
		bool ack_signal_filled;
		struct ewma_avg_signal avg_ack_signal;
	} status_stats;

	/* Updated from TX path only, no locking requirements */
	struct {
		u64 packets[IEEE80211_NUM_ACS];
		u64 bytes[IEEE80211_NUM_ACS];
		struct ieee80211_tx_rate last_rate;
		struct rate_info last_rate_info;
		u64 msdu[IEEE80211_NUM_TIDS + 1];
	} tx_stats;

	enum ieee80211_sta_rx_bandwidth cur_max_bandwidth;
	enum ieee80211_sta_rx_bandwidth rx_omi_bw_rx,
					rx_omi_bw_tx,
					rx_omi_bw_staging;

#ifdef CONFIG_MAC80211_DEBUGFS
	struct dentry *debugfs_dir;
#endif

	struct ieee80211_link_sta *pub;
};

/**
 * struct sta_info - STA information
 *
 * This structure collects information about a station that
 * mac80211 is communicating with.
 *
 * @list: global linked list entry
 * @free_list: list entry for keeping track of stations to free
 * @hash_node: hash node for rhashtable
 * @addr: station's MAC address - duplicated from public part to
 *	let the hash table work with just a single cacheline
 * @local: pointer to the global information
 * @sdata: virtual interface this station belongs to
 * @ptk: peer keys negotiated with this station, if any
 * @ptk_idx: last installed peer key index
 * @rate_ctrl: rate control algorithm reference
 * @rate_ctrl_lock: spinlock used to protect rate control data
 *	(data inside the algorithm, so serializes calls there)
 * @rate_ctrl_priv: rate control private per-STA pointer
 * @lock: used for locking all fields that require locking, see comments
 *	in the header file.
 * @drv_deliver_wk: used for delivering frames after driver PS unblocking
 * @listen_interval: listen interval of this station, when we're acting as AP
 * @_flags: STA flags, see &enum ieee80211_sta_info_flags, do not use directly
 * @ps_lock: used for powersave (when mac80211 is the AP) related locking
 * @ps_tx_buf: buffers (per AC) of frames to transmit to this station
 *	when it leaves power saving state or polls
 * @tx_filtered: buffers (per AC) of frames we already tried to
 *	transmit but were filtered by hardware due to STA having
 *	entered power saving state, these are also delivered to
 *	the station when it leaves powersave or polls for frames
 * @driver_buffered_tids: bitmap of TIDs the driver has data buffered on
 * @txq_buffered_tids: bitmap of TIDs that mac80211 has txq data buffered on
 * @assoc_at: clock boottime (in ns) of last association
 * @last_connected: time (in seconds) when a station got connected
 * @last_seq_ctrl: last received seq/frag number from this STA (per TID
 *	plus one for non-QoS frames)
 * @tid_seq: per-TID sequence numbers for sending to this STA
 * @airtime: per-AC struct airtime_info describing airtime statistics for this
 *	station
 * @airtime_weight: station weight for airtime fairness calculation purposes
 * @ampdu_mlme: A-MPDU state machine state
 * @mesh: mesh STA information
 * @debugfs_dir: debug filesystem directory dentry
 * @dead: set to true when sta is unlinked
 * @removed: set to true when sta is being removed from sta_list
 * @uploaded: set to true when sta is uploaded to the driver
 * @sta: station information we share with the driver
 * @sta_state: duplicates information about station state (for debug)
 * @rcu_head: RCU head used for freeing this station struct
 * @reserved_tid: reserved TID (if any, otherwise IEEE80211_TID_UNRESERVED)
 * @amsdu_mesh_control: track the mesh A-MSDU format used by the peer:
 *
 *	  * -1: not yet known
 *	  * 0: non-mesh A-MSDU length field
 *	  * 1: big-endian mesh A-MSDU length field
 *	  * 2: little-endian mesh A-MSDU length field
 *
 * @fast_tx: TX fastpath information
 * @fast_rx: RX fastpath information
 * @tdls_chandef: a TDLS peer can have a wider chandef that is compatible to
 *	the BSS one.
 * @frags: fragment cache
 * @cur: storage for aggregation data
 *	&struct ieee80211_sta points either here or to deflink.agg.
 * @deflink: This is the default link STA information, for non MLO STA all link
 *	specific STA information is accessed through @deflink or through
 *	link[0] which points to address of @deflink. For MLO Link STA
 *	the first added link STA will point to deflink.
 * @link: reference to Link Sta entries. For Non MLO STA, except 1st link,
 *	i.e link[0] all links would be assigned to NULL by default and
 *	would access link information via @deflink or link[0]. For MLO
 *	STA, first link STA being added will point its link pointer to
 *	@deflink address and remaining would be allocated and the address
 *	would be assigned to link[link_id] where link_id is the id assigned
 *	by the AP.
 */
struct sta_info {
	/* General information, mostly static */
	struct list_head list, free_list;
	struct rcu_head rcu_head;
	struct rhlist_head hash_node;
	u8 addr[ETH_ALEN];
	struct ieee80211_local *local;
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_key __rcu *ptk[NUM_DEFAULT_KEYS];
	u8 ptk_idx;
	struct rate_control_ref *rate_ctrl;
	void *rate_ctrl_priv;
	spinlock_t rate_ctrl_lock;
	spinlock_t lock;

	struct ieee80211_fast_tx __rcu *fast_tx;
	struct ieee80211_fast_rx __rcu *fast_rx;

#ifdef CONFIG_MAC80211_MESH
	struct mesh_sta *mesh;
#endif

	struct work_struct drv_deliver_wk;

	u16 listen_interval;

	bool dead;
	bool removed;

	bool uploaded;

	enum ieee80211_sta_state sta_state;

	/* use the accessors defined below */
	unsigned long _flags;

	/* STA powersave lock and frame queues */
	spinlock_t ps_lock;
	struct sk_buff_head ps_tx_buf[IEEE80211_NUM_ACS];
	struct sk_buff_head tx_filtered[IEEE80211_NUM_ACS];
	unsigned long driver_buffered_tids;
	unsigned long txq_buffered_tids;

	u64 assoc_at;
	long last_connected;

	/* Plus 1 for non-QoS frames */
	__le16 last_seq_ctrl[IEEE80211_NUM_TIDS + 1];

	u16 tid_seq[IEEE80211_QOS_CTL_TID_MASK + 1];

	struct airtime_info airtime[IEEE80211_NUM_ACS];
	u16 airtime_weight;

	/*
	 * Aggregation information, locked with lock.
	 */
	struct sta_ampdu_mlme ampdu_mlme;

#ifdef CONFIG_MAC80211_DEBUGFS
	struct dentry *debugfs_dir;
#endif

	u8 reserved_tid;
	s8 amsdu_mesh_control;

	struct cfg80211_chan_def tdls_chandef;

	struct ieee80211_fragment_cache frags;

	struct ieee80211_sta_aggregates cur;
	struct link_sta_info deflink;
	struct link_sta_info __rcu *link[IEEE80211_MLD_MAX_NUM_LINKS];

	/* keep last! */
	struct ieee80211_sta sta;
};

static inline int ieee80211_tdls_sta_link_id(struct sta_info *sta)
{
	/* TDLS STA can only have a single link */
	return sta->sta.valid_links ? __ffs(sta->sta.valid_links) : 0;
}

static inline enum nl80211_plink_state sta_plink_state(struct sta_info *sta)
{
#ifdef CONFIG_MAC80211_MESH
	return sta->mesh->plink_state;
#endif
	return NL80211_PLINK_LISTEN;
}

static inline void set_sta_flag(struct sta_info *sta,
				enum ieee80211_sta_info_flags flag)
{
	WARN_ON(flag == WLAN_STA_AUTH ||
		flag == WLAN_STA_ASSOC ||
		flag == WLAN_STA_AUTHORIZED);
	set_bit(flag, &sta->_flags);
}

static inline void clear_sta_flag(struct sta_info *sta,
				  enum ieee80211_sta_info_flags flag)
{
	WARN_ON(flag == WLAN_STA_AUTH ||
		flag == WLAN_STA_ASSOC ||
		flag == WLAN_STA_AUTHORIZED);
	clear_bit(flag, &sta->_flags);
}

static inline int test_sta_flag(struct sta_info *sta,
				enum ieee80211_sta_info_flags flag)
{
	return test_bit(flag, &sta->_flags);
}

static inline int test_and_clear_sta_flag(struct sta_info *sta,
					  enum ieee80211_sta_info_flags flag)
{
	WARN_ON(flag == WLAN_STA_AUTH ||
		flag == WLAN_STA_ASSOC ||
		flag == WLAN_STA_AUTHORIZED);
	return test_and_clear_bit(flag, &sta->_flags);
}

static inline int test_and_set_sta_flag(struct sta_info *sta,
					enum ieee80211_sta_info_flags flag)
{
	WARN_ON(flag == WLAN_STA_AUTH ||
		flag == WLAN_STA_ASSOC ||
		flag == WLAN_STA_AUTHORIZED);
	return test_and_set_bit(flag, &sta->_flags);
}

int sta_info_move_state(struct sta_info *sta,
			enum ieee80211_sta_state new_state);

static inline void sta_info_pre_move_state(struct sta_info *sta,
					   enum ieee80211_sta_state new_state)
{
	int ret;

	WARN_ON_ONCE(test_sta_flag(sta, WLAN_STA_INSERTED));

	ret = sta_info_move_state(sta, new_state);
	WARN_ON_ONCE(ret);
}


void ieee80211_assign_tid_tx(struct sta_info *sta, int tid,
			     struct tid_ampdu_tx *tid_tx);

#define rcu_dereference_protected_tid_tx(sta, tid)			\
	rcu_dereference_protected((sta)->ampdu_mlme.tid_tx[tid],	\
				  lockdep_is_held(&(sta)->lock) ||	\
				  lockdep_is_held(&(sta)->local->hw.wiphy->mtx));

/* Maximum number of frames to buffer per power saving station per AC */
#define STA_MAX_TX_BUFFER	64

/* Minimum buffered frame expiry time. If STA uses listen interval that is
 * smaller than this value, the minimum value here is used instead. */
#define STA_TX_BUFFER_EXPIRE (10 * HZ)

/* How often station data is cleaned up (e.g., expiration of buffered frames)
 */
#define STA_INFO_CLEANUP_INTERVAL (10 * HZ)

struct rhlist_head *sta_info_hash_lookup(struct ieee80211_local *local,
					 const u8 *addr);

/*
 * Get a STA info, must be under RCU read lock.
 */
struct sta_info *sta_info_get(struct ieee80211_sub_if_data *sdata,
			      const u8 *addr);

struct sta_info *sta_info_get_bss(struct ieee80211_sub_if_data *sdata,
				  const u8 *addr);

/* user must hold wiphy mutex or be in RCU critical section */
struct sta_info *sta_info_get_by_addrs(struct ieee80211_local *local,
				       const u8 *sta_addr, const u8 *vif_addr);

#define for_each_sta_info(local, _addr, _sta, _tmp)			\
	rhl_for_each_entry_rcu(_sta, _tmp,				\
			       sta_info_hash_lookup(local, _addr), hash_node)

struct rhlist_head *link_sta_info_hash_lookup(struct ieee80211_local *local,
					      const u8 *addr);

#define for_each_link_sta_info(local, _addr, _sta, _tmp)		\
	rhl_for_each_entry_rcu(_sta, _tmp,				\
			       link_sta_info_hash_lookup(local, _addr),	\
			       link_hash_node)

struct link_sta_info *
link_sta_info_get_bss(struct ieee80211_sub_if_data *sdata, const u8 *addr);

/*
 * Get STA info by index, BROKEN!
 */
struct sta_info *sta_info_get_by_idx(struct ieee80211_sub_if_data *sdata,
				     int idx);
/*
 * Create a new STA info, caller owns returned structure
 * until sta_info_insert().
 */
struct sta_info *sta_info_alloc(struct ieee80211_sub_if_data *sdata,
				const u8 *addr, gfp_t gfp);
struct sta_info *sta_info_alloc_with_link(struct ieee80211_sub_if_data *sdata,
					  const u8 *mld_addr,
					  unsigned int link_id,
					  const u8 *link_addr,
					  gfp_t gfp);

void sta_info_free(struct ieee80211_local *local, struct sta_info *sta);

/*
 * Insert STA info into hash table/list, returns zero or a
 * -EEXIST if (if the same MAC address is already present).
 *
 * Calling the non-rcu version makes the caller relinquish,
 * the _rcu version calls read_lock_rcu() and must be called
 * without it held.
 */
int sta_info_insert(struct sta_info *sta);
int sta_info_insert_rcu(struct sta_info *sta) __acquires(RCU);

int __must_check __sta_info_destroy(struct sta_info *sta);
int sta_info_destroy_addr(struct ieee80211_sub_if_data *sdata,
			  const u8 *addr);
int sta_info_destroy_addr_bss(struct ieee80211_sub_if_data *sdata,
			      const u8 *addr);

void sta_info_recalc_tim(struct sta_info *sta);

int sta_info_init(struct ieee80211_local *local);
void sta_info_stop(struct ieee80211_local *local);

/**
 * __sta_info_flush - flush matching STA entries from the STA table
 *
 * Return: the number of removed STA entries.
 *
 * @sdata: sdata to remove all stations from
 * @vlans: if the given interface is an AP interface, also flush VLANs
 * @link_id: if given (>=0), all those STA entries using @link_id only
 *	     will be removed. If -1 is passed, all STA entries will be
 *	     removed.
 * @do_not_flush_sta: a station that shouldn't be flushed.
 */
int __sta_info_flush(struct ieee80211_sub_if_data *sdata, bool vlans,
		     int link_id, struct sta_info *do_not_flush_sta);

/**
 * sta_info_flush - flush matching STA entries from the STA table
 *
 * Return: the number of removed STA entries.
 *
 * @sdata: sdata to remove all stations from
 * @link_id: if given (>=0), all those STA entries using @link_id only
 *	     will be removed. If -1 is passed, all STA entries will be
 *	     removed.
 */
static inline int sta_info_flush(struct ieee80211_sub_if_data *sdata,
				 int link_id)
{
	return __sta_info_flush(sdata, false, link_id, NULL);
}

void sta_set_rate_info_tx(struct sta_info *sta,
			  const struct ieee80211_tx_rate *rate,
			  struct rate_info *rinfo);
void sta_set_sinfo(struct sta_info *sta, struct station_info *sinfo,
		   bool tidstats);

u32 sta_get_expected_throughput(struct sta_info *sta);

void ieee80211_sta_expire(struct ieee80211_sub_if_data *sdata,
			  unsigned long exp_time);

int ieee80211_sta_allocate_link(struct sta_info *sta, unsigned int link_id);
void ieee80211_sta_free_link(struct sta_info *sta, unsigned int link_id);
int ieee80211_sta_activate_link(struct sta_info *sta, unsigned int link_id);
void ieee80211_sta_remove_link(struct sta_info *sta, unsigned int link_id);

void ieee80211_sta_ps_deliver_wakeup(struct sta_info *sta);
void ieee80211_sta_ps_deliver_poll_response(struct sta_info *sta);
void ieee80211_sta_ps_deliver_uapsd(struct sta_info *sta);

unsigned long ieee80211_sta_last_active(struct sta_info *sta);

void ieee80211_sta_set_max_amsdu_subframes(struct sta_info *sta,
					   const u8 *ext_capab,
					   unsigned int ext_capab_len);

void __ieee80211_sta_recalc_aggregates(struct sta_info *sta, u16 active_links);

enum sta_stats_type {
	STA_STATS_RATE_TYPE_INVALID = 0,
	STA_STATS_RATE_TYPE_LEGACY,
	STA_STATS_RATE_TYPE_HT,
	STA_STATS_RATE_TYPE_VHT,
	STA_STATS_RATE_TYPE_HE,
	STA_STATS_RATE_TYPE_S1G,
	STA_STATS_RATE_TYPE_EHT,
};

#define STA_STATS_FIELD_HT_MCS		GENMASK( 7,  0)
#define STA_STATS_FIELD_LEGACY_IDX	GENMASK( 3,  0)
#define STA_STATS_FIELD_LEGACY_BAND	GENMASK( 7,  4)
#define STA_STATS_FIELD_VHT_MCS		GENMASK( 3,  0)
#define STA_STATS_FIELD_VHT_NSS		GENMASK( 7,  4)
#define STA_STATS_FIELD_HE_MCS		GENMASK( 3,  0)
#define STA_STATS_FIELD_HE_NSS		GENMASK( 7,  4)
#define STA_STATS_FIELD_EHT_MCS		GENMASK( 3,  0)
#define STA_STATS_FIELD_EHT_NSS		GENMASK( 7,  4)
#define STA_STATS_FIELD_BW		GENMASK(12,  8)
#define STA_STATS_FIELD_SGI		GENMASK(13, 13)
#define STA_STATS_FIELD_TYPE		GENMASK(16, 14)
#define STA_STATS_FIELD_HE_RU		GENMASK(19, 17)
#define STA_STATS_FIELD_HE_GI		GENMASK(21, 20)
#define STA_STATS_FIELD_HE_DCM		GENMASK(22, 22)
#define STA_STATS_FIELD_EHT_RU		GENMASK(20, 17)
#define STA_STATS_FIELD_EHT_GI		GENMASK(22, 21)

#define STA_STATS_FIELD(_n, _v)		FIELD_PREP(STA_STATS_FIELD_ ## _n, _v)
#define STA_STATS_GET(_n, _v)		FIELD_GET(STA_STATS_FIELD_ ## _n, _v)

#define STA_STATS_RATE_INVALID		0

static inline u32 sta_stats_encode_rate(struct ieee80211_rx_status *s)
{
	u32 r;

	r = STA_STATS_FIELD(BW, s->bw);

	if (s->enc_flags & RX_ENC_FLAG_SHORT_GI)
		r |= STA_STATS_FIELD(SGI, 1);

	switch (s->encoding) {
	case RX_ENC_VHT:
		r |= STA_STATS_FIELD(TYPE, STA_STATS_RATE_TYPE_VHT);
		r |= STA_STATS_FIELD(VHT_NSS, s->nss);
		r |= STA_STATS_FIELD(VHT_MCS, s->rate_idx);
		break;
	case RX_ENC_HT:
		r |= STA_STATS_FIELD(TYPE, STA_STATS_RATE_TYPE_HT);
		r |= STA_STATS_FIELD(HT_MCS, s->rate_idx);
		break;
	case RX_ENC_LEGACY:
		r |= STA_STATS_FIELD(TYPE, STA_STATS_RATE_TYPE_LEGACY);
		r |= STA_STATS_FIELD(LEGACY_BAND, s->band);
		r |= STA_STATS_FIELD(LEGACY_IDX, s->rate_idx);
		break;
	case RX_ENC_HE:
		r |= STA_STATS_FIELD(TYPE, STA_STATS_RATE_TYPE_HE);
		r |= STA_STATS_FIELD(HE_NSS, s->nss);
		r |= STA_STATS_FIELD(HE_MCS, s->rate_idx);
		r |= STA_STATS_FIELD(HE_GI, s->he_gi);
		r |= STA_STATS_FIELD(HE_RU, s->he_ru);
		r |= STA_STATS_FIELD(HE_DCM, s->he_dcm);
		break;
	case RX_ENC_EHT:
		r |= STA_STATS_FIELD(TYPE, STA_STATS_RATE_TYPE_EHT);
		r |= STA_STATS_FIELD(EHT_NSS, s->nss);
		r |= STA_STATS_FIELD(EHT_MCS, s->rate_idx);
		r |= STA_STATS_FIELD(EHT_GI, s->eht.gi);
		r |= STA_STATS_FIELD(EHT_RU, s->eht.ru);
		break;
	default:
		WARN_ON(1);
		return STA_STATS_RATE_INVALID;
	}

	return r;
}

#endif /* STA_INFO_H */
