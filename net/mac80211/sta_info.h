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
#include <linux/kref.h>
#include "ieee80211_key.h"

/**
 * enum ieee80211_sta_info_flags - Stations flags
 *
 * These flags are used with &struct sta_info's @flags member.
 *
 * @WLAN_STA_AUTH: Station is authenticated.
 * @WLAN_STA_ASSOC: Station is associated.
 * @WLAN_STA_PS: Station is in power-save mode
 * @WLAN_STA_AUTHORIZED: Station is authorized to send/receive traffic.
 *	This bit is always checked so needs to be enabled for all stations
 *	when virtual port control is not in use.
 * @WLAN_STA_SHORT_PREAMBLE: Station is capable of receiving short-preamble
 *	frames.
 * @WLAN_STA_ASSOC_AP: We're associated to that station, it is an AP.
 * @WLAN_STA_WME: Station is a QoS-STA.
 * @WLAN_STA_WDS: Station is one of our WDS peers.
 * @WLAN_STA_PSPOLL: Station has just PS-polled us.
 * @WLAN_STA_CLEAR_PS_FILT: Clear PS filter in hardware (using the
 *	IEEE80211_TXCTL_CLEAR_PS_FILT control flag) when the next
 *	frame to this station is transmitted.
 */
enum ieee80211_sta_info_flags {
	WLAN_STA_AUTH		= 1<<0,
	WLAN_STA_ASSOC		= 1<<1,
	WLAN_STA_PS		= 1<<2,
	WLAN_STA_AUTHORIZED	= 1<<3,
	WLAN_STA_SHORT_PREAMBLE	= 1<<4,
	WLAN_STA_ASSOC_AP	= 1<<5,
	WLAN_STA_WME		= 1<<6,
	WLAN_STA_WDS		= 1<<7,
	WLAN_STA_PSPOLL		= 1<<8,
	WLAN_STA_CLEAR_PS_FILT	= 1<<9,
};

#define STA_TID_NUM 16
#define ADDBA_RESP_INTERVAL HZ
#define HT_AGG_MAX_RETRIES		(0x3)

#define HT_AGG_STATE_INITIATOR_SHIFT	(4)

#define HT_ADDBA_REQUESTED_MSK		BIT(0)
#define HT_ADDBA_DRV_READY_MSK		BIT(1)
#define HT_ADDBA_RECEIVED_MSK		BIT(2)
#define HT_AGG_STATE_REQ_STOP_BA_MSK	BIT(3)
#define HT_AGG_STATE_INITIATOR_MSK      BIT(HT_AGG_STATE_INITIATOR_SHIFT)
#define HT_AGG_STATE_IDLE		(0x0)
#define HT_AGG_STATE_OPERATIONAL	(HT_ADDBA_REQUESTED_MSK |	\
					 HT_ADDBA_DRV_READY_MSK |	\
					 HT_ADDBA_RECEIVED_MSK)

/**
 * struct tid_ampdu_tx - TID aggregation information (Tx).
 *
 * @state: TID's state in session state machine.
 * @dialog_token: dialog token for aggregation session
 * @ssn: Starting Sequence Number expected to be aggregated.
 * @addba_resp_timer: timer for peer's response to addba request
 * @addba_req_num: number of times addBA request has been sent.
 */
struct tid_ampdu_tx {
	u8 state;
	u8 dialog_token;
	u16 ssn;
	struct timer_list addba_resp_timer;
	u8 addba_req_num;
};

/**
 * struct tid_ampdu_rx - TID aggregation information (Rx).
 *
 * @state: TID's state in session state machine.
 * @dialog_token: dialog token for aggregation session
 * @ssn: Starting Sequence Number expected to be aggregated.
 * @buf_size: buffer size for incoming A-MPDUs
 * @timeout: reset timer value.
 * @head_seq_num: head sequence number in reordering buffer.
 * @stored_mpdu_num: number of MPDUs in reordering buffer
 * @reorder_buf: buffer to reorder incoming aggregated MPDUs
 * @session_timer: check if peer keeps Tx-ing on the TID (by timeout value)
 */
struct tid_ampdu_rx {
	u8 state;
	u8 dialog_token;
	u16 ssn;
	u16 buf_size;
	u16 timeout;
	u16 head_seq_num;
	u16 stored_mpdu_num;
	struct sk_buff **reorder_buf;
	struct timer_list session_timer;
};

enum plink_state {
	LISTEN,
	OPN_SNT,
	OPN_RCVD,
	CNF_RCVD,
	ESTAB,
	HOLDING,
	BLOCKED
};

/**
 * struct sta_ampdu_mlme - STA aggregation information.
 *
 * @tid_rx: aggregation info for Rx per TID
 * @tid_tx: aggregation info for Tx per TID
 * @ampdu_rx: for locking sections in aggregation Rx flow
 * @ampdu_tx: for locking sectionsi in aggregation Tx flow
 * @dialog_token_allocator: dialog token enumerator for each new session;
 */
struct sta_ampdu_mlme {
	struct tid_ampdu_rx tid_rx[STA_TID_NUM];
	struct tid_ampdu_tx tid_tx[STA_TID_NUM];
	spinlock_t ampdu_rx;
	spinlock_t ampdu_tx;
	u8 dialog_token_allocator;
};

struct sta_info {
	struct kref kref;
	struct list_head list;
	struct sta_info *hnext; /* next entry in hash table list */

	struct ieee80211_local *local;

	u8 addr[ETH_ALEN];
	u16 aid; /* STA's unique AID (1..2007), 0 = not yet assigned */
	u32 flags; /* WLAN_STA_ */

	struct sk_buff_head ps_tx_buf; /* buffer of TX frames for station in
					* power saving state */
	struct sk_buff_head tx_filtered; /* buffer of TX frames that were
					  * already given to low-level driver,
					  * but were filtered */
	unsigned long rx_packets, tx_packets; /* number of RX/TX MSDUs */
	unsigned long rx_bytes, tx_bytes;
	unsigned long tx_retry_failed, tx_retry_count;
	unsigned long tx_filtered_count;
	/* moving percentage of failed MSDUs */
	unsigned int fail_avg;

	unsigned int wep_weak_iv_count; /* number of RX frames with weak IV */

	unsigned long last_rx;
	/* bitmap of supported rates per band */
	u64 supp_rates[IEEE80211_NUM_BANDS];
	int txrate_idx;
	/* last rates used to send a frame to this STA */
	int last_txrate_idx, last_nonerp_txrate_idx;

	struct net_device *dev; /* which net device is this station associated
				 * to */

	struct ieee80211_key *key;

	u32 tx_num_consecutive_failures;
	u32 tx_num_mpdu_ok;
	u32 tx_num_mpdu_fail;

	struct rate_control_ref *rate_ctrl;
	void *rate_ctrl_priv;

	/* last received seq/frag number from this STA (per RX queue) */
	__le16 last_seq_ctrl[NUM_RX_DATA_QUEUES];
	unsigned long num_duplicates; /* number of duplicate frames received
				       * from this STA */
	unsigned long tx_fragments; /* number of transmitted MPDUs */
	unsigned long rx_fragments; /* number of received MPDUs */
	unsigned long rx_dropped; /* number of dropped MPDUs from this STA */

	int last_rssi; /* RSSI of last received frame from this STA */
	int last_signal; /* signal of last received frame from this STA */
	int last_noise; /* noise of last received frame from this STA */
	int channel_use;
	int channel_use_raw;

#ifdef CONFIG_MAC80211_DEBUG_COUNTERS
	unsigned int wme_rx_queue[NUM_RX_DATA_QUEUES];
	unsigned int wme_tx_queue[NUM_RX_DATA_QUEUES];
#endif /* CONFIG_MAC80211_DEBUG_COUNTERS */

	u16 listen_interval;

	struct ieee80211_ht_info ht_info; /* 802.11n HT capabilities
					     of this STA */
	struct sta_ampdu_mlme ampdu_mlme;
	u8 timer_to_tid[STA_TID_NUM];	/* convert timer id to tid */
	u8 tid_to_tx_q[STA_TID_NUM];	/* map tid to tx queue */
#ifdef CONFIG_MAC80211_MESH
	/* mesh peer link attributes */
	__le16 llid;		/* Local link ID */
	__le16 plid;		/* Peer link ID */
	__le16 reason;		/* Buffer for cancel reason on HOLDING state */
	u8 plink_retries;	/* Retries in establishment */
	bool ignore_plink_timer;
	enum plink_state plink_state;
	u32 plink_timeout;
	struct timer_list plink_timer;
	spinlock_t plink_lock;	/* For peer_state reads / updates and other
				   updates in the structure. Ensures robust
				   transitions for the peerlink FSM */
#endif

#ifdef CONFIG_MAC80211_DEBUGFS
	struct sta_info_debugfsdentries {
		struct dentry *dir;
		struct dentry *flags;
		struct dentry *num_ps_buf_frames;
		struct dentry *inactive_ms;
		struct dentry *last_seq_ctrl;
#ifdef CONFIG_MAC80211_DEBUG_COUNTERS
		struct dentry *wme_rx_queue;
		struct dentry *wme_tx_queue;
#endif
		struct dentry *agg_status;
	} debugfs;
#endif
};

static inline enum plink_state sta_plink_state(struct sta_info *sta)
{
#ifdef CONFIG_MAC80211_MESH
	return sta->plink_state;
#endif
	return LISTEN;
}


/* Maximum number of concurrently registered stations */
#define MAX_STA_COUNT 2007

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

static inline void __sta_info_get(struct sta_info *sta)
{
	kref_get(&sta->kref);
}

struct sta_info * sta_info_get(struct ieee80211_local *local, u8 *addr);
struct sta_info *sta_info_get_by_idx(struct ieee80211_local *local, int idx,
				      struct net_device *dev);
void sta_info_put(struct sta_info *sta);
struct sta_info *sta_info_add(struct ieee80211_local *local,
			      struct net_device *dev, u8 *addr, gfp_t gfp);
void sta_info_remove(struct sta_info *sta);
void sta_info_free(struct sta_info *sta);
void sta_info_init(struct ieee80211_local *local);
int sta_info_start(struct ieee80211_local *local);
void sta_info_stop(struct ieee80211_local *local);
void sta_info_flush(struct ieee80211_local *local, struct net_device *dev);

void sta_info_set_tim_bit(struct sta_info *sta);
void sta_info_clear_tim_bit(struct sta_info *sta);

#endif /* STA_INFO_H */
