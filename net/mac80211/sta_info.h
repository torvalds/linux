/*
 * Copyright 2002-2005, Devicescape Software, Inc.
 * Copyright 2013-2014  Intel Mobile Communications GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef STA_INFO_H
#define STA_INFO_H

#include <linux/list.h>
#include <linux/types.h>
#include <linux/if_ether.h>
#include <linux/workqueue.h>
#include <linux/average.h>
#include <linux/etherdevice.h>
#include <linux/rhashtable.h>
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
	WLAN_STA_UAPSD,
	WLAN_STA_SP,
	WLAN_STA_4ADDR_EVENT,
	WLAN_STA_INSERTED,
	WLAN_STA_RATE_CONTROL,
	WLAN_STA_TOFFSET_KNOWN,
	WLAN_STA_MPSP_OWNER,
	WLAN_STA_MPSP_RECIPIENT,
	WLAN_STA_PS_DELIVER,
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

enum ieee80211_agg_stop_reason {
	AGG_STOP_DECLINED,
	AGG_STOP_LOCAL_REQUEST,
	AGG_STOP_PEER_REQUEST,
	AGG_STOP_DESTROY_STA,
};

/**
 * struct tid_ampdu_tx - TID aggregation information (Tx).
 *
 * @rcu_head: rcu head for freeing structure
 * @session_timer: check if we keep Tx-ing on the TID (by timeout value)
 * @addba_resp_timer: timer for peer's response to addba request
 * @pending: pending frames queue -- use sta's spinlock to protect
 * @dialog_token: dialog token for aggregation session
 * @timeout: session timeout value to be filled in ADDBA requests
 * @state: session state (see above)
 * @last_tx: jiffies of last tx activity
 * @stop_initiator: initiator of a session stop
 * @tx_stop: TX DelBA frame when stopping
 * @buf_size: reorder buffer size at receiver
 * @failed_bar_ssn: ssn of the last failed BAR tx attempt
 * @bar_pending: BAR needs to be re-sent
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
	unsigned long state;
	unsigned long last_tx;
	u16 timeout;
	u8 dialog_token;
	u8 stop_initiator;
	bool tx_stop;
	u8 buf_size;

	u16 failed_bar_ssn;
	bool bar_pending;
};

/**
 * struct tid_ampdu_rx - TID aggregation information (Rx).
 *
 * @reorder_buf: buffer to reorder incoming aggregated MPDUs. An MPDU may be an
 *	A-MSDU with individually reported subframes.
 * @reorder_time: jiffies when skb was added
 * @session_timer: check if peer keeps Tx-ing on the TID (by timeout value)
 * @reorder_timer: releases expired frames from the reorder buffer.
 * @last_rx: jiffies of last rx activity
 * @head_seq_num: head sequence number in reordering buffer.
 * @stored_mpdu_num: number of MPDUs in reordering buffer
 * @ssn: Starting Sequence Number expected to be aggregated.
 * @buf_size: buffer size for incoming A-MPDUs
 * @timeout: reset timer value (in TUs).
 * @dialog_token: dialog token for aggregation session
 * @rcu_head: RCU head used for freeing this struct
 * @reorder_lock: serializes access to reorder buffer, see below.
 * @auto_seq: used for offloaded BA sessions to automatically pick head_seq_and
 *	and ssn.
 * @removed: this session is removed (but might have been found due to RCU)
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
	struct sk_buff_head *reorder_buf;
	unsigned long *reorder_time;
	struct timer_list session_timer;
	struct timer_list reorder_timer;
	unsigned long last_rx;
	u16 head_seq_num;
	u16 stored_mpdu_num;
	u16 ssn;
	u16 buf_size;
	u16 timeout;
	u8 dialog_token;
	bool auto_seq;
	bool removed;
};

/**
 * struct sta_ampdu_mlme - STA aggregation information.
 *
 * @tid_rx: aggregation info for Rx per TID -- RCU protected
 * @tid_tx: aggregation info for Tx per TID
 * @tid_start_tx: sessions where start was requested
 * @addba_req_num: number of times addBA request has been sent.
 * @last_addba_req_time: timestamp of the last addBA request.
 * @dialog_token_allocator: dialog token enumerator for each new session;
 * @work: work struct for starting/stopping aggregation
 * @tid_rx_timer_expired: bitmap indicating on which TIDs the
 *	RX timer expired until the work for it runs
 * @tid_rx_stop_requested:  bitmap indicating which BA sessions per TID the
 *	driver requested to close until the work for it runs
 * @mtx: mutex to protect all TX data (except non-NULL assignments
 *	to tid_tx[idx], which are protected by the sta spinlock)
 *	tid_start_tx is also protected by sta->lock.
 */
struct sta_ampdu_mlme {
	struct mutex mtx;
	/* rx */
	struct tid_ampdu_rx __rcu *tid_rx[IEEE80211_NUM_TIDS];
	unsigned long tid_rx_timer_expired[BITS_TO_LONGS(IEEE80211_NUM_TIDS)];
	unsigned long tid_rx_stop_requested[BITS_TO_LONGS(IEEE80211_NUM_TIDS)];
	/* tx */
	struct work_struct work;
	struct tid_ampdu_tx __rcu *tid_tx[IEEE80211_NUM_TIDS];
	struct tid_ampdu_tx *tid_start_tx[IEEE80211_NUM_TIDS];
	unsigned long last_addba_req_time[IEEE80211_NUM_TIDS];
	u8 addba_req_num[IEEE80211_NUM_TIDS];
	u8 dialog_token_allocator;
};


/* Value to indicate no TID reservation */
#define IEEE80211_TID_UNRESERVED	0xff

/**
 * struct sta_info - STA information
 *
 * This structure collects information about a station that
 * mac80211 is communicating with.
 *
 * @list: global linked list entry
 * @free_list: list entry for keeping track of stations to free
 * @hash_node: hash node for rhashtable
 * @local: pointer to the global information
 * @sdata: virtual interface this station belongs to
 * @ptk: peer keys negotiated with this station, if any
 * @ptk_idx: last installed peer key index
 * @gtk: group keys negotiated with this station, if any
 * @gtk_idx: last installed group key index
 * @rate_ctrl: rate control algorithm reference
 * @rate_ctrl_priv: rate control private per-STA pointer
 * @last_tx_rate: rate used for last transmit, to report to userspace as
 *	"the" transmit rate
 * @last_rx_rate_idx: rx status rate index of the last data packet
 * @last_rx_rate_flag: rx status flag of the last data packet
 * @last_rx_rate_vht_flag: rx status vht flag of the last data packet
 * @last_rx_rate_vht_nss: rx status nss of last data packet
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
 * @rx_packets: Number of MSDUs received from this STA
 * @rx_bytes: Number of bytes received from this STA
 * @last_rx: time (in jiffies) when last frame was received from this STA
 * @last_connected: time (in seconds) when a station got connected
 * @num_duplicates: number of duplicate frames received from this STA
 * @rx_fragments: number of received MPDUs
 * @rx_dropped: number of dropped MPDUs from this STA
 * @last_signal: signal of last received frame from this STA
 * @avg_signal: moving average of signal of received frames from this STA
 * @last_ack_signal: signal of last received Ack frame from this STA
 * @last_seq_ctrl: last received seq/frag number from this STA (per RX queue)
 * @tx_filtered_count: number of frames the hardware filtered for this STA
 * @tx_retry_failed: number of frames that failed retry
 * @tx_retry_count: total number of retries for frames to this STA
 * @fail_avg: moving percentage of failed MSDUs
 * @tx_packets: number of RX/TX MSDUs
 * @tx_bytes: number of bytes transmitted to this STA
 * @tx_fragments: number of transmitted MPDUs
 * @tid_seq: per-TID sequence numbers for sending to this STA
 * @ampdu_mlme: A-MPDU state machine state
 * @timer_to_tid: identity mapping to ID timers
 * @llid: Local link ID
 * @plid: Peer link ID
 * @reason: Cancel reason on PLINK_HOLDING state
 * @plink_retries: Retries in establishment
 * @plink_state: peer link state
 * @plink_timeout: timeout of peer link
 * @plink_timer: peer link watch timer
 * @t_offset: timing offset relative to this host
 * @t_offset_setpoint: reference timing offset of this sta to be used when
 * 	calculating clockdrift
 * @local_pm: local link-specific power save mode
 * @peer_pm: peer-specific power save mode towards local STA
 * @nonpeer_pm: STA power save mode towards non-peer neighbors
 * @debugfs: debug filesystem info
 * @dead: set to true when sta is unlinked
 * @uploaded: set to true when sta is uploaded to the driver
 * @lost_packets: number of consecutive lost packets
 * @sta: station information we share with the driver
 * @sta_state: duplicates information about station state (for debug)
 * @beacon_loss_count: number of times beacon loss has triggered
 * @rcu_head: RCU head used for freeing this station struct
 * @cur_max_bandwidth: maximum bandwidth to use for TX to the station,
 *	taken from HT/VHT capabilities or VHT operating mode notification
 * @chains: chains ever used for RX from this station
 * @chain_signal_last: last signal (per chain)
 * @chain_signal_avg: signal average (per chain)
 * @known_smps_mode: the smps_mode the client thinks we are in. Relevant for
 *	AP only.
 * @cipher_scheme: optional cipher scheme for this station
 * @last_tdls_pkt_time: holds the time in jiffies of last TDLS pkt ACKed
 * @reserved_tid: reserved TID (if any, otherwise IEEE80211_TID_UNRESERVED)
 * @tx_msdu: MSDUs transmitted to this station, using IEEE80211_NUM_TID
 *	entry for non-QoS frames
 * @tx_msdu_retries: MSDU retries for transmissions to to this station,
 *	using IEEE80211_NUM_TID entry for non-QoS frames
 * @tx_msdu_failed: MSDU failures for transmissions to to this station,
 *	using IEEE80211_NUM_TID entry for non-QoS frames
 * @rx_msdu: MSDUs received from this station, using IEEE80211_NUM_TID
 *	entry for non-QoS frames
 */
struct sta_info {
	/* General information, mostly static */
	struct list_head list, free_list;
	struct rcu_head rcu_head;
	struct rhash_head hash_node;
	struct ieee80211_local *local;
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_key __rcu *gtk[NUM_DEFAULT_KEYS + NUM_DEFAULT_MGMT_KEYS];
	struct ieee80211_key __rcu *ptk[NUM_DEFAULT_KEYS];
	u8 gtk_idx;
	u8 ptk_idx;
	struct rate_control_ref *rate_ctrl;
	void *rate_ctrl_priv;
	spinlock_t lock;

	struct work_struct drv_deliver_wk;

	u16 listen_interval;

	bool dead;

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

	/* Updated from RX path only, no locking requirements */
	unsigned long rx_packets;
	u64 rx_bytes;
	unsigned long last_rx;
	long last_connected;
	unsigned long num_duplicates;
	unsigned long rx_fragments;
	unsigned long rx_dropped;
	int last_signal;
	struct ewma avg_signal;
	int last_ack_signal;

	u8 chains;
	s8 chain_signal_last[IEEE80211_MAX_CHAINS];
	struct ewma chain_signal_avg[IEEE80211_MAX_CHAINS];

	/* Plus 1 for non-QoS frames */
	__le16 last_seq_ctrl[IEEE80211_NUM_TIDS + 1];

	/* Updated from TX status path only, no locking requirements */
	unsigned long tx_filtered_count;
	unsigned long tx_retry_failed, tx_retry_count;
	/* moving percentage of failed MSDUs */
	unsigned int fail_avg;

	/* Updated from TX path only, no locking requirements */
	u32 tx_fragments;
	u64 tx_packets[IEEE80211_NUM_ACS];
	u64 tx_bytes[IEEE80211_NUM_ACS];
	struct ieee80211_tx_rate last_tx_rate;
	int last_rx_rate_idx;
	u32 last_rx_rate_flag;
	u32 last_rx_rate_vht_flag;
	u8 last_rx_rate_vht_nss;
	u16 tid_seq[IEEE80211_QOS_CTL_TID_MASK + 1];
	u64 tx_msdu[IEEE80211_NUM_TIDS + 1];
	u64 tx_msdu_retries[IEEE80211_NUM_TIDS + 1];
	u64 tx_msdu_failed[IEEE80211_NUM_TIDS + 1];
	u64 rx_msdu[IEEE80211_NUM_TIDS + 1];

	/*
	 * Aggregation information, locked with lock.
	 */
	struct sta_ampdu_mlme ampdu_mlme;
	u8 timer_to_tid[IEEE80211_NUM_TIDS];

#ifdef CONFIG_MAC80211_MESH
	/*
	 * Mesh peer link attributes
	 * TODO: move to a sub-structure that is referenced with pointer?
	 */
	u16 llid;
	u16 plid;
	u16 reason;
	u8 plink_retries;
	enum nl80211_plink_state plink_state;
	u32 plink_timeout;
	struct timer_list plink_timer;
	s64 t_offset;
	s64 t_offset_setpoint;
	/* mesh power save */
	enum nl80211_mesh_power_mode local_pm;
	enum nl80211_mesh_power_mode peer_pm;
	enum nl80211_mesh_power_mode nonpeer_pm;
#endif

#ifdef CONFIG_MAC80211_DEBUGFS
	struct sta_info_debugfsdentries {
		struct dentry *dir;
		bool add_has_run;
	} debugfs;
#endif

	enum ieee80211_sta_rx_bandwidth cur_max_bandwidth;

	unsigned int lost_packets;
	unsigned int beacon_loss_count;

	enum ieee80211_smps_mode known_smps_mode;
	const struct ieee80211_cipher_scheme *cipher_scheme;

	/* TDLS timeout data */
	unsigned long last_tdls_pkt_time;

	u8 reserved_tid;

	/* keep last! */
	struct ieee80211_sta sta;
};

static inline enum nl80211_plink_state sta_plink_state(struct sta_info *sta)
{
#ifdef CONFIG_MAC80211_MESH
	return sta->plink_state;
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

static inline struct tid_ampdu_tx *
rcu_dereference_protected_tid_tx(struct sta_info *sta, int tid)
{
	return rcu_dereference_protected(sta->ampdu_mlme.tid_tx[tid],
					 lockdep_is_held(&sta->lock) ||
					 lockdep_is_held(&sta->ampdu_mlme.mtx));
}

/* Maximum number of frames to buffer per power saving station per AC */
#define STA_MAX_TX_BUFFER	64

/* Minimum buffered frame expiry time. If STA uses listen interval that is
 * smaller than this value, the minimum value here is used instead. */
#define STA_TX_BUFFER_EXPIRE (10 * HZ)

/* How often station data is cleaned up (e.g., expiration of buffered frames)
 */
#define STA_INFO_CLEANUP_INTERVAL (10 * HZ)

/*
 * Get a STA info, must be under RCU read lock.
 */
struct sta_info *sta_info_get(struct ieee80211_sub_if_data *sdata,
			      const u8 *addr);

struct sta_info *sta_info_get_bss(struct ieee80211_sub_if_data *sdata,
				  const u8 *addr);

u32 sta_addr_hash(const void *key, u32 length, u32 seed);

#define _sta_bucket_idx(_tbl, _a)					\
	rht_bucket_index(_tbl, sta_addr_hash(_a, ETH_ALEN, (_tbl)->hash_rnd))

#define for_each_sta_info(local, tbl, _addr, _sta, _tmp)		\
	rht_for_each_entry_rcu(_sta, _tmp, tbl, 			\
			       _sta_bucket_idx(tbl, _addr),		\
			       hash_node)				\
	/* compare address and run code only if it matches */		\
	if (ether_addr_equal(_sta->sta.addr, (_addr)))

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
 * sta_info_flush - flush matching STA entries from the STA table
 *
 * Returns the number of removed STA entries.
 *
 * @sdata: sdata to remove all stations from
 * @vlans: if the given interface is an AP interface, also flush VLANs
 */
int __sta_info_flush(struct ieee80211_sub_if_data *sdata, bool vlans);

static inline int sta_info_flush(struct ieee80211_sub_if_data *sdata)
{
	return __sta_info_flush(sdata, false);
}

void sta_set_rate_info_tx(struct sta_info *sta,
			  const struct ieee80211_tx_rate *rate,
			  struct rate_info *rinfo);
void sta_set_rate_info_rx(struct sta_info *sta,
			  struct rate_info *rinfo);
void sta_set_sinfo(struct sta_info *sta, struct station_info *sinfo);

void ieee80211_sta_expire(struct ieee80211_sub_if_data *sdata,
			  unsigned long exp_time);
u8 sta_info_tx_streams(struct sta_info *sta);

void ieee80211_sta_ps_deliver_wakeup(struct sta_info *sta);
void ieee80211_sta_ps_deliver_poll_response(struct sta_info *sta);
void ieee80211_sta_ps_deliver_uapsd(struct sta_info *sta);

#endif /* STA_INFO_H */
