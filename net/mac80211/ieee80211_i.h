/*
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2005, Devicescape Software, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
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
#include <net/wireless.h>
#include "ieee80211_key.h"
#include "sta_info.h"

/* ieee80211.o internal definitions, etc. These are not included into
 * low-level drivers. */

#ifndef ETH_P_PAE
#define ETH_P_PAE 0x888E /* Port Access Entity (IEEE 802.1X) */
#endif /* ETH_P_PAE */

#define WLAN_FC_DATA_PRESENT(fc) (((fc) & 0x4c) == 0x08)

struct ieee80211_local;

#define IEEE80211_ALIGN32_PAD(a) ((4 - ((a) & 3)) & 3)

/* Maximum number of broadcast/multicast frames to buffer when some of the
 * associated stations are using power saving. */
#define AP_MAX_BC_BUFFER 128

/* Maximum number of frames buffered to all STAs, including multicast frames.
 * Note: increasing this limit increases the potential memory requirement. Each
 * frame can be up to about 2 kB long. */
#define TOTAL_MAX_TX_BUFFER 512

/* Required encryption head and tailroom */
#define IEEE80211_ENCRYPT_HEADROOM 8
#define IEEE80211_ENCRYPT_TAILROOM 12

/* IEEE 802.11 (Ch. 9.5 Defragmentation) requires support for concurrent
 * reception of at least three fragmented frames. This limit can be increased
 * by changing this define, at the cost of slower frame reassembly and
 * increased memory use (about 2 kB of RAM per entry). */
#define IEEE80211_FRAGMENT_MAX 4

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


struct ieee80211_sta_bss {
	struct list_head list;
	struct ieee80211_sta_bss *hnext;
	atomic_t users;

	u8 bssid[ETH_ALEN];
	u8 ssid[IEEE80211_MAX_SSID_LEN];
	size_t ssid_len;
	u16 capability; /* host byte order */
	int hw_mode;
	int channel;
	int freq;
	int rssi, signal, noise;
	u8 *wpa_ie;
	size_t wpa_ie_len;
	u8 *rsn_ie;
	size_t rsn_ie_len;
	u8 *wmm_ie;
	size_t wmm_ie_len;
#define IEEE80211_MAX_SUPP_RATES 32
	u8 supp_rates[IEEE80211_MAX_SUPP_RATES];
	size_t supp_rates_len;
	int beacon_int;
	u64 timestamp;

	int probe_resp;
	unsigned long last_update;

	/* during assocation, we save an ERP value from a probe response so
	 * that we can feed ERP info to the driver when handling the
	 * association completes. these fields probably won't be up-to-date
	 * otherwise, you probably don't want to use them. */
	int has_erp_value;
	u8 erp_value;
};


typedef enum {
	TXRX_CONTINUE, TXRX_DROP, TXRX_QUEUED
} ieee80211_txrx_result;

/* flags used in struct ieee80211_txrx_data.flags */
/* whether the MSDU was fragmented */
#define IEEE80211_TXRXD_FRAGMENTED		BIT(0)
#define IEEE80211_TXRXD_TXUNICAST		BIT(1)
#define IEEE80211_TXRXD_TXPS_BUFFERED		BIT(2)
#define IEEE80211_TXRXD_TXPROBE_LAST_FRAG	BIT(3)
#define IEEE80211_TXRXD_RXIN_SCAN		BIT(4)
/* frame is destined to interface currently processed (incl. multicast frames) */
#define IEEE80211_TXRXD_RXRA_MATCH		BIT(5)
#define IEEE80211_TXRXD_TX_INJECTED		BIT(6)
struct ieee80211_txrx_data {
	struct sk_buff *skb;
	struct net_device *dev;
	struct ieee80211_local *local;
	struct ieee80211_sub_if_data *sdata;
	struct sta_info *sta;
	u16 fc, ethertype;
	struct ieee80211_key *key;
	unsigned int flags;
	union {
		struct {
			struct ieee80211_tx_control *control;
			struct ieee80211_hw_mode *mode;
			struct ieee80211_rate *rate;
			/* use this rate (if set) for last fragment; rate can
			 * be set to lower rate for the first fragments, e.g.,
			 * when using CTS protection with IEEE 802.11g. */
			struct ieee80211_rate *last_frag_rate;
			int last_frag_hwrate;

			/* Extra fragments (in addition to the first fragment
			 * in skb) */
			int num_extra_frag;
			struct sk_buff **extra_frag;
		} tx;
		struct {
			struct ieee80211_rx_status *status;
			int sent_ps_buffered;
			int queue;
			int load;
			u32 tkip_iv32;
			u16 tkip_iv16;
		} rx;
	} u;
};

/* flags used in struct ieee80211_tx_packet_data.flags */
#define IEEE80211_TXPD_REQ_TX_STATUS	BIT(0)
#define IEEE80211_TXPD_DO_NOT_ENCRYPT	BIT(1)
#define IEEE80211_TXPD_REQUEUE		BIT(2)
/* Stored in sk_buff->cb */
struct ieee80211_tx_packet_data {
	int ifindex;
	unsigned long jiffies;
	unsigned int flags;
	u8 queue;
};

struct ieee80211_tx_stored_packet {
	struct ieee80211_tx_control control;
	struct sk_buff *skb;
	int num_extra_frag;
	struct sk_buff **extra_frag;
	int last_frag_rateidx;
	int last_frag_hwrate;
	struct ieee80211_rate *last_frag_rate;
	unsigned int last_frag_rate_ctrl_probe;
};

typedef ieee80211_txrx_result (*ieee80211_tx_handler)
(struct ieee80211_txrx_data *tx);

typedef ieee80211_txrx_result (*ieee80211_rx_handler)
(struct ieee80211_txrx_data *rx);

struct ieee80211_if_ap {
	u8 *beacon_head, *beacon_tail;
	int beacon_head_len, beacon_tail_len;

	struct list_head vlans;

	u8 ssid[IEEE80211_MAX_SSID_LEN];
	size_t ssid_len;

	/* yes, this looks ugly, but guarantees that we can later use
	 * bitmap_empty :)
	 * NB: don't ever use set_bit, use bss_tim_set/bss_tim_clear! */
	u8 tim[sizeof(unsigned long) * BITS_TO_LONGS(IEEE80211_MAX_AID + 1)];
	atomic_t num_sta_ps; /* number of stations in PS mode */
	struct sk_buff_head ps_bc_buf;
	int dtim_period, dtim_count;
	int force_unicast_rateidx; /* forced TX rateidx for unicast frames */
	int max_ratectrl_rateidx; /* max TX rateidx for rate control */
	int num_beacons; /* number of TXed beacon frames for this BSS */
};

struct ieee80211_if_wds {
	u8 remote_addr[ETH_ALEN];
	struct sta_info *sta;
};

struct ieee80211_if_vlan {
	struct ieee80211_sub_if_data *ap;
	struct list_head list;
};

/* flags used in struct ieee80211_if_sta.flags */
#define IEEE80211_STA_SSID_SET		BIT(0)
#define IEEE80211_STA_BSSID_SET		BIT(1)
#define IEEE80211_STA_PREV_BSSID_SET	BIT(2)
#define IEEE80211_STA_AUTHENTICATED	BIT(3)
#define IEEE80211_STA_ASSOCIATED	BIT(4)
#define IEEE80211_STA_PROBEREQ_POLL	BIT(5)
#define IEEE80211_STA_CREATE_IBSS	BIT(6)
#define IEEE80211_STA_MIXED_CELL	BIT(7)
#define IEEE80211_STA_WMM_ENABLED	BIT(8)
#define IEEE80211_STA_AUTO_SSID_SEL	BIT(10)
#define IEEE80211_STA_AUTO_BSSID_SEL	BIT(11)
#define IEEE80211_STA_AUTO_CHANNEL_SEL	BIT(12)
#define IEEE80211_STA_PRIVACY_INVOKED	BIT(13)
struct ieee80211_if_sta {
	enum {
		IEEE80211_DISABLED, IEEE80211_AUTHENTICATE,
		IEEE80211_ASSOCIATE, IEEE80211_ASSOCIATED,
		IEEE80211_IBSS_SEARCH, IEEE80211_IBSS_JOINED
	} state;
	struct timer_list timer;
	struct work_struct work;
	u8 bssid[ETH_ALEN], prev_bssid[ETH_ALEN];
	u8 ssid[IEEE80211_MAX_SSID_LEN];
	size_t ssid_len;
	u16 aid;
	u16 ap_capab, capab;
	u8 *extra_ie; /* to be added to the end of AssocReq */
	size_t extra_ie_len;

	/* The last AssocReq/Resp IEs */
	u8 *assocreq_ies, *assocresp_ies;
	size_t assocreq_ies_len, assocresp_ies_len;

	int auth_tries, assoc_tries;

	unsigned int flags;
#define IEEE80211_STA_REQ_SCAN 0
#define IEEE80211_STA_REQ_AUTH 1
#define IEEE80211_STA_REQ_RUN  2
	unsigned long request;
	struct sk_buff_head skb_queue;

	unsigned long last_probe;

#define IEEE80211_AUTH_ALG_OPEN BIT(0)
#define IEEE80211_AUTH_ALG_SHARED_KEY BIT(1)
#define IEEE80211_AUTH_ALG_LEAP BIT(2)
	unsigned int auth_algs; /* bitfield of allowed auth algs */
	int auth_alg; /* currently used IEEE 802.11 authentication algorithm */
	int auth_transaction;

	unsigned long ibss_join_req;
	struct sk_buff *probe_resp; /* ProbeResp template for IBSS */
	u32 supp_rates_bits;

	int wmm_last_param_set;
};


/* flags used in struct ieee80211_sub_if_data.flags */
#define IEEE80211_SDATA_ALLMULTI	BIT(0)
#define IEEE80211_SDATA_PROMISC		BIT(1)
#define IEEE80211_SDATA_USE_PROTECTION	BIT(2) /* CTS protect ERP frames */
/* use short preamble with IEEE 802.11b: this flag is set when the AP or beacon
 * generator reports that there are no present stations that cannot support short
 * preambles */
#define IEEE80211_SDATA_SHORT_PREAMBLE	BIT(3)
#define IEEE80211_SDATA_USERSPACE_MLME	BIT(4)
struct ieee80211_sub_if_data {
	struct list_head list;
	enum ieee80211_if_types type;

	struct wireless_dev wdev;

	/* keys */
	struct list_head key_list;

	struct net_device *dev;
	struct ieee80211_local *local;

	unsigned int flags;

	int drop_unencrypted;
	int eapol; /* 0 = process EAPOL frames as normal data frames,
		    * 1 = send EAPOL frames through wlan#ap to hostapd
		    *     (default) */
	int ieee802_1x; /* IEEE 802.1X PAE - drop packet to/from unauthorized
			 * port */

	u16 sequence;

	/* Fragment table for host-based reassembly */
	struct ieee80211_fragment_entry	fragments[IEEE80211_FRAGMENT_MAX];
	unsigned int fragment_next;

#define NUM_DEFAULT_KEYS 4
	struct ieee80211_key *keys[NUM_DEFAULT_KEYS];
	struct ieee80211_key *default_key;

	struct ieee80211_if_ap *bss; /* BSS that this device belongs to */

	union {
		struct ieee80211_if_ap ap;
		struct ieee80211_if_wds wds;
		struct ieee80211_if_vlan vlan;
		struct ieee80211_if_sta sta;
	} u;
	int channel_use;
	int channel_use_raw;

#ifdef CONFIG_MAC80211_DEBUGFS
	struct dentry *debugfsdir;
	union {
		struct {
			struct dentry *channel_use;
			struct dentry *drop_unencrypted;
			struct dentry *eapol;
			struct dentry *ieee8021_x;
			struct dentry *state;
			struct dentry *bssid;
			struct dentry *prev_bssid;
			struct dentry *ssid_len;
			struct dentry *aid;
			struct dentry *ap_capab;
			struct dentry *capab;
			struct dentry *extra_ie_len;
			struct dentry *auth_tries;
			struct dentry *assoc_tries;
			struct dentry *auth_algs;
			struct dentry *auth_alg;
			struct dentry *auth_transaction;
			struct dentry *flags;
		} sta;
		struct {
			struct dentry *channel_use;
			struct dentry *drop_unencrypted;
			struct dentry *eapol;
			struct dentry *ieee8021_x;
			struct dentry *num_sta_ps;
			struct dentry *dtim_period;
			struct dentry *dtim_count;
			struct dentry *num_beacons;
			struct dentry *force_unicast_rateidx;
			struct dentry *max_ratectrl_rateidx;
			struct dentry *num_buffered_multicast;
			struct dentry *beacon_head_len;
			struct dentry *beacon_tail_len;
		} ap;
		struct {
			struct dentry *channel_use;
			struct dentry *drop_unencrypted;
			struct dentry *eapol;
			struct dentry *ieee8021_x;
			struct dentry *peer;
		} wds;
		struct {
			struct dentry *channel_use;
			struct dentry *drop_unencrypted;
			struct dentry *eapol;
			struct dentry *ieee8021_x;
		} vlan;
		struct {
			struct dentry *mode;
		} monitor;
		struct dentry *default_key;
	} debugfs;
#endif
};

#define IEEE80211_DEV_TO_SUB_IF(dev) netdev_priv(dev)

enum {
	IEEE80211_RX_MSG	= 1,
	IEEE80211_TX_STATUS_MSG	= 2,
};

struct ieee80211_local {
	/* embed the driver visible part.
	 * don't cast (use the static inlines below), but we keep
	 * it first anyway so they become a no-op */
	struct ieee80211_hw hw;

	const struct ieee80211_ops *ops;

	/* List of registered struct ieee80211_hw_mode */
	struct list_head modes_list;

	struct net_device *mdev; /* wmaster# - "master" 802.11 device */
	int open_count;
	int monitors;
	unsigned int filter_flags; /* FIF_* */
	struct iw_statistics wstats;
	u8 wstats_flags;
	int tx_headroom; /* required headroom for hardware/radiotap */

	enum {
		IEEE80211_DEV_UNINITIALIZED = 0,
		IEEE80211_DEV_REGISTERED,
		IEEE80211_DEV_UNREGISTERED,
	} reg_state;

	/* Tasklet and skb queue to process calls from IRQ mode. All frames
	 * added to skb_queue will be processed, but frames in
	 * skb_queue_unreliable may be dropped if the total length of these
	 * queues increases over the limit. */
#define IEEE80211_IRQSAFE_QUEUE_LIMIT 128
	struct tasklet_struct tasklet;
	struct sk_buff_head skb_queue;
	struct sk_buff_head skb_queue_unreliable;

	/* Station data structures */
	rwlock_t sta_lock; /* protects STA data structures */
	int num_sta; /* number of stations in sta_list */
	struct list_head sta_list;
	struct sta_info *sta_hash[STA_HASH_SIZE];
	struct timer_list sta_cleanup;

	unsigned long state[NUM_TX_DATA_QUEUES];
	struct ieee80211_tx_stored_packet pending_packet[NUM_TX_DATA_QUEUES];
	struct tasklet_struct tx_pending_tasklet;

	/* number of interfaces with corresponding IFF_ flags */
	atomic_t iff_allmultis, iff_promiscs;

	struct rate_control_ref *rate_ctrl;

	/* Supported and basic rate filters for different modes. These are
	 * pointers to -1 terminated lists and rates in 100 kbps units. */
	int *supp_rates[NUM_IEEE80211_MODES];
	int *basic_rates[NUM_IEEE80211_MODES];

	int rts_threshold;
	int fragmentation_threshold;
	int short_retry_limit; /* dot11ShortRetryLimit */
	int long_retry_limit; /* dot11LongRetryLimit */

	struct crypto_blkcipher *wep_tx_tfm;
	struct crypto_blkcipher *wep_rx_tfm;
	u32 wep_iv;

	int bridge_packets; /* bridge packets between associated stations and
			     * deliver multicast frames both back to wireless
			     * media and to the local net stack */

	ieee80211_rx_handler *rx_pre_handlers;
	ieee80211_rx_handler *rx_handlers;
	ieee80211_tx_handler *tx_handlers;

	struct list_head interfaces;

	int sta_scanning;
	int scan_channel_idx;
	enum { SCAN_SET_CHANNEL, SCAN_SEND_PROBE } scan_state;
	unsigned long last_scan_completed;
	struct delayed_work scan_work;
	struct net_device *scan_dev;
	struct ieee80211_channel *oper_channel, *scan_channel;
	struct ieee80211_hw_mode *oper_hw_mode, *scan_hw_mode;
	u8 scan_ssid[IEEE80211_MAX_SSID_LEN];
	size_t scan_ssid_len;
	struct list_head sta_bss_list;
	struct ieee80211_sta_bss *sta_bss_hash[STA_HASH_SIZE];
	spinlock_t sta_bss_lock;
#define IEEE80211_SCAN_MATCH_SSID BIT(0)
#define IEEE80211_SCAN_WPA_ONLY BIT(1)
#define IEEE80211_SCAN_EXTRA_INFO BIT(2)
	int scan_flags;

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
	u32 dot11WEPUndecryptableCount;

#ifdef CONFIG_MAC80211_LEDS
	int tx_led_counter, rx_led_counter;
	struct led_trigger *tx_led, *rx_led, *assoc_led;
	char tx_led_name[32], rx_led_name[32], assoc_led_name[32];
#endif

	u32 channel_use;
	u32 channel_use_raw;

#ifdef CONFIG_MAC80211_DEBUGFS
	struct work_struct sta_debugfs_add;
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
	unsigned int rx_handlers_drop_passive_scan;
	unsigned int tx_expand_skb_head;
	unsigned int tx_expand_skb_head_cloned;
	unsigned int rx_expand_skb_head;
	unsigned int rx_expand_skb_head2;
	unsigned int rx_handlers_fragments;
	unsigned int tx_status_drop;
	unsigned int wme_rx_queue[NUM_RX_DATA_QUEUES];
	unsigned int wme_tx_queue[NUM_RX_DATA_QUEUES];
#define I802_DEBUG_INC(c) (c)++
#else /* CONFIG_MAC80211_DEBUG_COUNTERS */
#define I802_DEBUG_INC(c) do { } while (0)
#endif /* CONFIG_MAC80211_DEBUG_COUNTERS */


	int total_ps_buffered; /* total number of all buffered unicast and
				* multicast packets for power saving stations
				*/
	int wifi_wme_noack_test;
	unsigned int wmm_acm; /* bit field of ACM bits (BIT(802.1D tag)) */

	unsigned int enabled_modes; /* bitfield of allowed modes;
				      * (1 << MODE_*) */
	unsigned int hw_modes; /* bitfield of supported hardware modes;
				* (1 << MODE_*) */

#ifdef CONFIG_MAC80211_DEBUGFS
	struct local_debugfsdentries {
		struct dentry *channel;
		struct dentry *frequency;
		struct dentry *antenna_sel_tx;
		struct dentry *antenna_sel_rx;
		struct dentry *bridge_packets;
		struct dentry *rts_threshold;
		struct dentry *fragmentation_threshold;
		struct dentry *short_retry_limit;
		struct dentry *long_retry_limit;
		struct dentry *total_ps_buffered;
		struct dentry *mode;
		struct dentry *wep_iv;
		struct dentry *modes;
		struct dentry *statistics;
		struct local_debugfsdentries_statsdentries {
			struct dentry *transmitted_fragment_count;
			struct dentry *multicast_transmitted_frame_count;
			struct dentry *failed_count;
			struct dentry *retry_count;
			struct dentry *multiple_retry_count;
			struct dentry *frame_duplicate_count;
			struct dentry *received_fragment_count;
			struct dentry *multicast_received_frame_count;
			struct dentry *transmitted_frame_count;
			struct dentry *wep_undecryptable_count;
			struct dentry *num_scans;
#ifdef CONFIG_MAC80211_DEBUG_COUNTERS
			struct dentry *tx_handlers_drop;
			struct dentry *tx_handlers_queued;
			struct dentry *tx_handlers_drop_unencrypted;
			struct dentry *tx_handlers_drop_fragment;
			struct dentry *tx_handlers_drop_wep;
			struct dentry *tx_handlers_drop_not_assoc;
			struct dentry *tx_handlers_drop_unauth_port;
			struct dentry *rx_handlers_drop;
			struct dentry *rx_handlers_queued;
			struct dentry *rx_handlers_drop_nullfunc;
			struct dentry *rx_handlers_drop_defrag;
			struct dentry *rx_handlers_drop_short;
			struct dentry *rx_handlers_drop_passive_scan;
			struct dentry *tx_expand_skb_head;
			struct dentry *tx_expand_skb_head_cloned;
			struct dentry *rx_expand_skb_head;
			struct dentry *rx_expand_skb_head2;
			struct dentry *rx_handlers_fragments;
			struct dentry *tx_status_drop;
			struct dentry *wme_tx_queue;
			struct dentry *wme_rx_queue;
#endif
			struct dentry *dot11ACKFailureCount;
			struct dentry *dot11RTSFailureCount;
			struct dentry *dot11FCSErrorCount;
			struct dentry *dot11RTSSuccessCount;
		} stats;
		struct dentry *stations;
		struct dentry *keys;
	} debugfs;
#endif
};

static inline struct ieee80211_local *hw_to_local(
	struct ieee80211_hw *hw)
{
	return container_of(hw, struct ieee80211_local, hw);
}

static inline struct ieee80211_hw *local_to_hw(
	struct ieee80211_local *local)
{
	return &local->hw;
}

enum ieee80211_link_state_t {
	IEEE80211_LINK_STATE_XOFF = 0,
	IEEE80211_LINK_STATE_PENDING,
};

struct sta_attribute {
	struct attribute attr;
	ssize_t (*show)(const struct sta_info *, char *buf);
	ssize_t (*store)(struct sta_info *, const char *buf, size_t count);
};

static inline void __bss_tim_set(struct ieee80211_if_ap *bss, u16 aid)
{
	/*
	 * This format has been mandated by the IEEE specifications,
	 * so this line may not be changed to use the __set_bit() format.
	 */
	bss->tim[aid / 8] |= (1 << (aid % 8));
}

static inline void bss_tim_set(struct ieee80211_local *local,
			       struct ieee80211_if_ap *bss, u16 aid)
{
	read_lock_bh(&local->sta_lock);
	__bss_tim_set(bss, aid);
	read_unlock_bh(&local->sta_lock);
}

static inline void __bss_tim_clear(struct ieee80211_if_ap *bss, u16 aid)
{
	/*
	 * This format has been mandated by the IEEE specifications,
	 * so this line may not be changed to use the __clear_bit() format.
	 */
	bss->tim[aid / 8] &= ~(1 << (aid % 8));
}

static inline void bss_tim_clear(struct ieee80211_local *local,
				 struct ieee80211_if_ap *bss, u16 aid)
{
	read_lock_bh(&local->sta_lock);
	__bss_tim_clear(bss, aid);
	read_unlock_bh(&local->sta_lock);
}

/**
 * ieee80211_is_erp_rate - Check if a rate is an ERP rate
 * @phymode: The PHY-mode for this rate (MODE_IEEE80211...)
 * @rate: Transmission rate to check, in 100 kbps
 *
 * Check if a given rate is an Extended Rate PHY (ERP) rate.
 */
static inline int ieee80211_is_erp_rate(int phymode, int rate)
{
	if (phymode == MODE_IEEE80211G) {
		if (rate != 10 && rate != 20 &&
		    rate != 55 && rate != 110)
			return 1;
	}
	return 0;
}

static inline int ieee80211_bssid_match(const u8 *raddr, const u8 *addr)
{
	return compare_ether_addr(raddr, addr) == 0 ||
	       is_broadcast_ether_addr(raddr);
}


/* ieee80211.c */
int ieee80211_hw_config(struct ieee80211_local *local);
int ieee80211_if_config(struct net_device *dev);
int ieee80211_if_config_beacon(struct net_device *dev);
void ieee80211_prepare_rates(struct ieee80211_local *local,
			     struct ieee80211_hw_mode *mode);
void ieee80211_tx_set_iswep(struct ieee80211_txrx_data *tx);
int ieee80211_if_update_wds(struct net_device *dev, u8 *remote_addr);
void ieee80211_if_setup(struct net_device *dev);
struct ieee80211_rate *ieee80211_get_rate(struct ieee80211_local *local,
					  int phymode, int hwrate);

/* ieee80211_ioctl.c */
extern const struct iw_handler_def ieee80211_iw_handler_def;


/* Least common multiple of the used rates (in 100 kbps). This is used to
 * calculate rate_inv values for each rate so that only integers are needed. */
#define CHAN_UTIL_RATE_LCM 95040
/* 1 usec is 1/8 * (95040/10) = 1188 */
#define CHAN_UTIL_PER_USEC 1188
/* Amount of bits to shift the result right to scale the total utilization
 * to values that will not wrap around 32-bit integers. */
#define CHAN_UTIL_SHIFT 9
/* Theoretical maximum of channel utilization counter in 10 ms (stat_time=1):
 * (CHAN_UTIL_PER_USEC * 10000) >> CHAN_UTIL_SHIFT = 23203. So dividing the
 * raw value with about 23 should give utilization in 10th of a percentage
 * (1/1000). However, utilization is only estimated and not all intervals
 * between frames etc. are calculated. 18 seems to give numbers that are closer
 * to the real maximum. */
#define CHAN_UTIL_PER_10MS 18
#define CHAN_UTIL_HDR_LONG (202 * CHAN_UTIL_PER_USEC)
#define CHAN_UTIL_HDR_SHORT (40 * CHAN_UTIL_PER_USEC)


/* ieee80211_ioctl.c */
int ieee80211_set_compression(struct ieee80211_local *local,
			      struct net_device *dev, struct sta_info *sta);
int ieee80211_set_channel(struct ieee80211_local *local, int channel, int freq);
/* ieee80211_sta.c */
void ieee80211_sta_timer(unsigned long data);
void ieee80211_sta_work(struct work_struct *work);
void ieee80211_sta_scan_work(struct work_struct *work);
void ieee80211_sta_rx_mgmt(struct net_device *dev, struct sk_buff *skb,
			   struct ieee80211_rx_status *rx_status);
int ieee80211_sta_set_ssid(struct net_device *dev, char *ssid, size_t len);
int ieee80211_sta_get_ssid(struct net_device *dev, char *ssid, size_t *len);
int ieee80211_sta_set_bssid(struct net_device *dev, u8 *bssid);
int ieee80211_sta_req_scan(struct net_device *dev, u8 *ssid, size_t ssid_len);
void ieee80211_sta_req_auth(struct net_device *dev,
			    struct ieee80211_if_sta *ifsta);
int ieee80211_sta_scan_results(struct net_device *dev, char *buf, size_t len);
void ieee80211_sta_rx_scan(struct net_device *dev, struct sk_buff *skb,
			   struct ieee80211_rx_status *rx_status);
void ieee80211_rx_bss_list_init(struct net_device *dev);
void ieee80211_rx_bss_list_deinit(struct net_device *dev);
int ieee80211_sta_set_extra_ie(struct net_device *dev, char *ie, size_t len);
struct sta_info * ieee80211_ibss_add_sta(struct net_device *dev,
					 struct sk_buff *skb, u8 *bssid,
					 u8 *addr);
int ieee80211_sta_deauthenticate(struct net_device *dev, u16 reason);
int ieee80211_sta_disassociate(struct net_device *dev, u16 reason);
void ieee80211_erp_info_change_notify(struct net_device *dev, u8 changes);
void ieee80211_reset_erp_info(struct net_device *dev);

/* ieee80211_iface.c */
int ieee80211_if_add(struct net_device *dev, const char *name,
		     struct net_device **new_dev, int type);
void ieee80211_if_set_type(struct net_device *dev, int type);
void ieee80211_if_reinit(struct net_device *dev);
void __ieee80211_if_del(struct ieee80211_local *local,
			struct ieee80211_sub_if_data *sdata);
int ieee80211_if_remove(struct net_device *dev, const char *name, int id);
void ieee80211_if_free(struct net_device *dev);
void ieee80211_if_sdata_init(struct ieee80211_sub_if_data *sdata);

/* regdomain.c */
void ieee80211_regdomain_init(void);
void ieee80211_set_default_regdomain(struct ieee80211_hw_mode *mode);

/* rx handling */
extern ieee80211_rx_handler ieee80211_rx_pre_handlers[];
extern ieee80211_rx_handler ieee80211_rx_handlers[];

/* tx handling */
extern ieee80211_tx_handler ieee80211_tx_handlers[];
void ieee80211_clear_tx_pending(struct ieee80211_local *local);
void ieee80211_tx_pending(unsigned long data);
int ieee80211_master_start_xmit(struct sk_buff *skb, struct net_device *dev);
int ieee80211_monitor_start_xmit(struct sk_buff *skb, struct net_device *dev);
int ieee80211_subif_start_xmit(struct sk_buff *skb, struct net_device *dev);

/* utility functions/constants */
extern void *mac80211_wiphy_privid; /* for wiphy privid */
extern const unsigned char rfc1042_header[6];
extern const unsigned char bridge_tunnel_header[6];
u8 *ieee80211_get_bssid(struct ieee80211_hdr *hdr, size_t len);
int ieee80211_is_eapol(const struct sk_buff *skb);
int ieee80211_frame_duration(struct ieee80211_local *local, size_t len,
			     int rate, int erp, int short_preamble);
void mac80211_ev_michael_mic_failure(struct net_device *dev, int keyidx,
				     struct ieee80211_hdr *hdr);

#endif /* IEEE80211_I_H */
