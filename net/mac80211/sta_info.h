/*
 * Copyright 2002-2005, Devicescape Software, Inc.
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
#include "key.h"

/**
 * enum ieee80211_sta_info_flags - Stations flags
 *
 * These flags are used with &struct sta_info's @flags member.
 *
 * @WLAN_STA_AUTH: Station is authenticated.
 * @WLAN_STA_ASSOC: Station is associated.
 * @WLAN_STA_PS_STA: Station is in power-save mode
 * @WLAN_STA_AUTHORIZED: Station is authorized to send/receive traffic.
 *	This bit is always checked so needs to be enabled for all stations
 *	when virtual port control is not in use.
 * @WLAN_STA_SHORT_PREAMBLE: Station is capable of receiving short-preamble
 *	frames.
 * @WLAN_STA_ASSOC_AP: We're associated to that station, it is an AP.
 * @WLAN_STA_WME: Station is a QoS-STA.
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
 */
enum ieee80211_sta_info_flags {
	WLAN_STA_AUTH		= 1<<0,
	WLAN_STA_ASSOC		= 1<<1,
	WLAN_STA_PS_STA		= 1<<2,
	WLAN_STA_AUTHORIZED	= 1<<3,
	WLAN_STA_SHORT_PREAMBLE	= 1<<4,
	WLAN_STA_ASSOC_AP	= 1<<5,
	WLAN_STA_WME		= 1<<6,
	WLAN_STA_WDS		= 1<<7,
	WLAN_STA_CLEAR_PS_FILT	= 1<<9,
	WLAN_STA_MFP		= 1<<10,
	WLAN_STA_BLOCK_BA	= 1<<11,
	WLAN_STA_PS_DRIVER	= 1<<12,
	WLAN_STA_PSPOLL		= 1<<13,
};

#define STA_TID_NUM 16
#define ADDBA_RESP_INTERVAL HZ
#define HT_AGG_MAX_RETRIES		0x3

#define HT_AGG_STATE_DRV_READY		0
#define HT_AGG_STATE_RESPONSE_RECEIVED	1
#define HT_AGG_STATE_OPERATIONAL	2
#define HT_AGG_STATE_STOPPING		3
#define HT_AGG_STATE_WANT_START		4
#define HT_AGG_STATE_WANT_STOP		5

/**
 * struct tid_ampdu_tx - TID aggregation information (Tx).
 *
 * @rcu_head: rcu head for freeing structure
 * @addba_resp_timer: timer for peer's response to addba request
 * @pending: pending frames queue -- use sta's spinlock to protect
 * @dialog_token: dialog token for aggregation session
 * @timeout: session timeout value to be filled in ADDBA requests
 * @state: session state (see above)
 * @stop_initiator: initiator of a session stop
 * @tx_stop: TX DelBA frame when stopping
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
	struct timer_list addba_resp_timer;
	struct sk_buff_head pending;
	unsigned long state;
	u16 timeout;
	u8 dialog_token;
	u8 stop_initiator;
	bool tx_stop;
};

/**
 * struct tid_ampdu_rx - TID aggregation information (Rx).
 *
 * @reorder_buf: buffer to reorder incoming aggregated MPDUs
 * @reorder_time: jiffies when skb was added
 * @session_timer: check if peer keeps Tx-ing on the TID (by timeout value)
 * @reorder_timer: releases expired frames from the reorder buffer.
 * @head_seq_num: head sequence number in reordering buffer.
 * @stored_mpdu_num: number of MPDUs in reordering buffer
 * @ssn: Starting Sequence Number expected to be aggregated.
 * @buf_size: buffer size for incoming A-MPDUs
 * @timeout: reset timer value (in TUs).
 * @dialog_token: dialog token for aggregation session
 * @rcu_head: RCU head used for freeing this struct
 * @reorder_lock: serializes access to reorder buffer, see below.
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
	struct sk_buff **reorder_buf;
	unsigned long *reorder_time;
	struct timer_list session_timer;
	struct timer_list reorder_timer;
	u16 head_seq_num;
	u16 stored_mpdu_num;
	u16 ssn;
	u16 buf_size;
	u16 timeout;
	u8 dialog_token;
};

/**
 * struct sta_ampdu_mlme - STA aggregation information.
 *
 * @tid_rx: aggregation info for Rx per TID -- RCU protected
 * @tid_tx: aggregation info for Tx per TID
 * @addba_req_num: number of times addBA request has been sent.
 * @dialog_token_allocator: dialog token enumerator for each new session;
 * @work: work struct for starting/stopping aggregation
 * @tid_rx_timer_expired: bitmap indicating on which TIDs the
 *	RX timer expired until the work for it runs
 * @mtx: mutex to protect all TX data (except non-NULL assignments
 *	to tid_tx[idx], which are protected by the sta spinlock)
 */
struct sta_ampdu_mlme {
	struct mutex mtx;
	/* rx */
	struct tid_ampdu_rx *tid_rx[STA_TID_NUM];
	unsigned long tid_rx_timer_expired[BITS_TO_LONGS(STA_TID_NUM)];
	/* tx */
	struct work_struct work;
	struct tid_ampdu_tx *tid_tx[STA_TID_NUM];
	u8 addba_req_num[STA_TID_NUM];
	u8 dialog_token_allocator;
};


/**
 * enum plink_state - state of a mesh peer link finite state machine
 *
 * @PLINK_LISTEN: initial state, considered the implicit state of non existant
 * 	mesh peer links
 * @PLINK_OPN_SNT: mesh plink open frame has been sent to this mesh peer
 * @PLINK_OPN_RCVD: mesh plink open frame has been received from this mesh peer
 * @PLINK_CNF_RCVD: mesh plink confirm frame has been received from this mesh
 * 	peer
 * @PLINK_ESTAB: mesh peer link is established
 * @PLINK_HOLDING: mesh peer link is being closed or cancelled
 * @PLINK_BLOCKED: all frames transmitted from this mesh plink are discarded
 */
enum plink_state {
	PLINK_LISTEN,
	PLINK_OPN_SNT,
	PLINK_OPN_RCVD,
	PLINK_CNF_RCVD,
	PLINK_ESTAB,
	PLINK_HOLDING,
	PLINK_BLOCKED
};

/**
 * struct sta_info - STA information
 *
 * This structure collects information about a station that
 * mac80211 is communicating with.
 *
 * @list: global linked list entry
 * @hnext: hash table linked list pointer
 * @local: pointer to the global information
 * @sdata: virtual interface this station belongs to
 * @ptk: peer key negotiated with this station, if any
 * @gtk: group keys negotiated with this station, if any
 * @rate_ctrl: rate control algorithm reference
 * @rate_ctrl_priv: rate control private per-STA pointer
 * @last_tx_rate: rate used for last transmit, to report to userspace as
 *	"the" transmit rate
 * @lock: used for locking all fields that require locking, see comments
 *	in the header file.
 * @flaglock: spinlock for flags accesses
 * @drv_unblock_wk: used for driver PS unblocking
 * @listen_interval: listen interval of this station, when we're acting as AP
 * @flags: STA flags, see &enum ieee80211_sta_info_flags
 * @ps_tx_buf: buffer of frames to transmit to this station
 *	when it leaves power saving state
 * @tx_filtered: buffer of frames we already tried to transmit
 *	but were filtered by hardware due to STA having entered
 *	power saving state
 * @rx_packets: Number of MSDUs received from this STA
 * @rx_bytes: Number of bytes received from this STA
 * @wep_weak_iv_count: number of weak WEP IVs received from this station
 * @last_rx: time (in jiffies) when last frame was received from this STA
 * @num_duplicates: number of duplicate frames received from this STA
 * @rx_fragments: number of received MPDUs
 * @rx_dropped: number of dropped MPDUs from this STA
 * @last_signal: signal of last received frame from this STA
 * @avg_signal: moving average of signal of received frames from this STA
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
 * @ignore_plink_timer: ignore the peer-link timer (used internally)
 * @plink_state: peer link state
 * @plink_timeout: timeout of peer link
 * @plink_timer: peer link watch timer
 * @plink_timer_was_running: used by suspend/resume to restore timers
 * @debugfs: debug filesystem info
 * @sta: station information we share with the driver
 * @dead: set to true when sta is unlinked
 * @uploaded: set to true when sta is uploaded to the driver
 * @lost_packets: number of consecutive lost packets
 */
struct sta_info {
	/* General information, mostly static */
	struct list_head list;
	struct sta_info *hnext;
	struct ieee80211_local *local;
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_key *gtk[NUM_DEFAULT_KEYS + NUM_DEFAULT_MGMT_KEYS];
	struct ieee80211_key *ptk;
	struct rate_control_ref *rate_ctrl;
	void *rate_ctrl_priv;
	spinlock_t lock;
	spinlock_t flaglock;

	struct work_struct drv_unblock_wk;

	u16 listen_interval;

	bool dead;

	bool uploaded;

	/*
	 * frequently updated, locked with own spinlock (flaglock),
	 * use the accessors defined below
	 */
	u32 flags;

	/*
	 * STA powersave frame queues, no more than the internal
	 * locking required.
	 */
	struct sk_buff_head ps_tx_buf;
	struct sk_buff_head tx_filtered;

	/* Updated from RX path only, no locking requirements */
	unsigned long rx_packets, rx_bytes;
	unsigned long wep_weak_iv_count;
	unsigned long last_rx;
	unsigned long num_duplicates;
	unsigned long rx_fragments;
	unsigned long rx_dropped;
	int last_signal;
	struct ewma avg_signal;
	__le16 last_seq_ctrl[NUM_RX_DATA_QUEUES];

	/* Updated from TX status path only, no locking requirements */
	unsigned long tx_filtered_count;
	unsigned long tx_retry_failed, tx_retry_count;
	/* moving percentage of failed MSDUs */
	unsigned int fail_avg;

	/* Updated from TX path only, no locking requirements */
	unsigned long tx_packets;
	unsigned long tx_bytes;
	unsigned long tx_fragments;
	struct ieee80211_tx_rate last_tx_rate;
	u16 tid_seq[IEEE80211_QOS_CTL_TID_MASK + 1];

	/*
	 * Aggregation information, locked with lock.
	 */
	struct sta_ampdu_mlme ampdu_mlme;
	u8 timer_to_tid[STA_TID_NUM];

#ifdef CONFIG_MAC80211_MESH
	/*
	 * Mesh peer link attributes
	 * TODO: move to a sub-structure that is referenced with pointer?
	 */
	__le16 llid;
	__le16 plid;
	__le16 reason;
	u8 plink_retries;
	bool ignore_plink_timer;
	bool plink_timer_was_running;
	enum plink_state plink_state;
	u32 plink_timeout;
	struct timer_list plink_timer;
#endif

#ifdef CONFIG_MAC80211_DEBUGFS
	struct sta_info_debugfsdentries {
		struct dentry *dir;
		bool add_has_run;
	} debugfs;
#endif

	unsigned int lost_packets;

	/* keep last! */
	struct ieee80211_sta sta;
};

static inline enum plink_state sta_plink_state(struct sta_info *sta)
{
#ifdef CONFIG_MAC80211_MESH
	return sta->plink_state;
#endif
	return PLINK_LISTEN;
}

static inline void set_sta_flags(struct sta_info *sta, const u32 flags)
{
	unsigned long irqfl;

	spin_lock_irqsave(&sta->flaglock, irqfl);
	sta->flags |= flags;
	spin_unlock_irqrestore(&sta->flaglock, irqfl);
}

static inline void clear_sta_flags(struct sta_info *sta, const u32 flags)
{
	unsigned long irqfl;

	spin_lock_irqsave(&sta->flaglock, irqfl);
	sta->flags &= ~flags;
	spin_unlock_irqrestore(&sta->flaglock, irqfl);
}

static inline u32 test_sta_flags(struct sta_info *sta, const u32 flags)
{
	u32 ret;
	unsigned long irqfl;

	spin_lock_irqsave(&sta->flaglock, irqfl);
	ret = sta->flags & flags;
	spin_unlock_irqrestore(&sta->flaglock, irqfl);

	return ret;
}

static inline u32 test_and_clear_sta_flags(struct sta_info *sta,
					   const u32 flags)
{
	u32 ret;
	unsigned long irqfl;

	spin_lock_irqsave(&sta->flaglock, irqfl);
	ret = sta->flags & flags;
	sta->flags &= ~flags;
	spin_unlock_irqrestore(&sta->flaglock, irqfl);

	return ret;
}

static inline u32 get_sta_flags(struct sta_info *sta)
{
	u32 ret;
	unsigned long irqfl;

	spin_lock_irqsave(&sta->flaglock, irqfl);
	ret = sta->flags;
	spin_unlock_irqrestore(&sta->flaglock, irqfl);

	return ret;
}



#define STA_HASH_SIZE 256
#define STA_HASH(sta) (sta[5])


/* Maximum number of frames to buffer per power saving station */
#define STA_MAX_TX_BUFFER 128

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

static inline
void for_each_sta_info_type_check(struct ieee80211_local *local,
				  const u8 *addr,
				  struct sta_info *sta,
				  struct sta_info *nxt)
{
}

#define for_each_sta_info(local, _addr, _sta, nxt) 			\
	for (	/* initialise loop */					\
		_sta = rcu_dereference(local->sta_hash[STA_HASH(_addr)]),\
		nxt = _sta ? rcu_dereference(_sta->hnext) : NULL;	\
		/* typecheck */						\
		for_each_sta_info_type_check(local, (_addr), _sta, nxt),\
		/* continue condition */				\
		_sta;							\
		/* advance loop */					\
		_sta = nxt,						\
		nxt = _sta ? rcu_dereference(_sta->hnext) : NULL	\
	     )								\
	/* compare address and run code only if it matches */		\
	if (memcmp(_sta->sta.addr, (_addr), ETH_ALEN) == 0)

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
				u8 *addr, gfp_t gfp);
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
int sta_info_insert_atomic(struct sta_info *sta);

int sta_info_destroy_addr(struct ieee80211_sub_if_data *sdata,
			  const u8 *addr);
int sta_info_destroy_addr_bss(struct ieee80211_sub_if_data *sdata,
			      const u8 *addr);

void sta_info_set_tim_bit(struct sta_info *sta);
void sta_info_clear_tim_bit(struct sta_info *sta);

void sta_info_init(struct ieee80211_local *local);
int sta_info_start(struct ieee80211_local *local);
void sta_info_stop(struct ieee80211_local *local);
int sta_info_flush(struct ieee80211_local *local,
		   struct ieee80211_sub_if_data *sdata);
void ieee80211_sta_expire(struct ieee80211_sub_if_data *sdata,
			  unsigned long exp_time);

void ieee80211_sta_ps_deliver_wakeup(struct sta_info *sta);
void ieee80211_sta_ps_deliver_poll_response(struct sta_info *sta);

#endif /* STA_INFO_H */
