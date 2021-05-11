/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2002-2004, Instant802 Networks, Inc.
 * Copyright 2005, Devicescape Software, Inc.
 * Copyright (C) 2019 Intel Corporation
 */

#ifndef IEEE80211_KEY_H
#define IEEE80211_KEY_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/crypto.h>
#include <linux/rcupdate.h>
#include <crypto/arc4.h>
#include <net/mac80211.h>

#define NUM_DEFAULT_KEYS 4
#define NUM_DEFAULT_MGMT_KEYS 2
#define NUM_DEFAULT_BEACON_KEYS 2
#define INVALID_PTK_KEYIDX 2 /* Keyidx always pointing to a NULL key for PTK */

struct ieee80211_local;
struct ieee80211_sub_if_data;
struct sta_info;

/**
 * enum ieee80211_internal_key_flags - internal key flags
 *
 * @KEY_FLAG_UPLOADED_TO_HARDWARE: Indicates that this key is present
 *	in the hardware for TX crypto hardware acceleration.
 * @KEY_FLAG_TAINTED: Key is tainted and packets should be dropped.
 * @KEY_FLAG_CIPHER_SCHEME: This key is for a hardware cipher scheme
 */
enum ieee80211_internal_key_flags {
	KEY_FLAG_UPLOADED_TO_HARDWARE	= BIT(0),
	KEY_FLAG_TAINTED		= BIT(1),
	KEY_FLAG_CIPHER_SCHEME		= BIT(2),
};

enum ieee80211_internal_tkip_state {
	TKIP_STATE_NOT_INIT,
	TKIP_STATE_PHASE1_DONE,
	TKIP_STATE_PHASE1_HW_UPLOADED,
};

struct tkip_ctx {
	u16 p1k[5];	/* p1k cache */
	u32 p1k_iv32;	/* iv32 for which p1k computed */
	enum ieee80211_internal_tkip_state state;
};

struct tkip_ctx_rx {
	struct tkip_ctx ctx;
	u32 iv32;	/* current iv32 */
	u16 iv16;	/* current iv16 */
};

struct ieee80211_key {
	struct ieee80211_local *local;
	struct ieee80211_sub_if_data *sdata;
	struct sta_info *sta;

	/* for sdata list */
	struct list_head list;

	/* protected by key mutex */
	unsigned int flags;

	union {
		struct {
			/* protects tx context */
			spinlock_t txlock;

			/* last used TSC */
			struct tkip_ctx tx;

			/* last received RSC */
			struct tkip_ctx_rx rx[IEEE80211_NUM_TIDS];

			/* number of mic failures */
			u32 mic_failures;
		} tkip;
		struct {
			/*
			 * Last received packet number. The first
			 * IEEE80211_NUM_TIDS counters are used with Data
			 * frames and the last counter is used with Robust
			 * Management frames.
			 */
			u8 rx_pn[IEEE80211_NUM_TIDS + 1][IEEE80211_CCMP_PN_LEN];
			struct crypto_aead *tfm;
			u32 replays; /* dot11RSNAStatsCCMPReplays */
		} ccmp;
		struct {
			u8 rx_pn[IEEE80211_CMAC_PN_LEN];
			struct crypto_shash *tfm;
			u32 replays; /* dot11RSNAStatsCMACReplays */
			u32 icverrors; /* dot11RSNAStatsCMACICVErrors */
		} aes_cmac;
		struct {
			u8 rx_pn[IEEE80211_GMAC_PN_LEN];
			struct crypto_aead *tfm;
			u32 replays; /* dot11RSNAStatsCMACReplays */
			u32 icverrors; /* dot11RSNAStatsCMACICVErrors */
		} aes_gmac;
		struct {
			/* Last received packet number. The first
			 * IEEE80211_NUM_TIDS counters are used with Data
			 * frames and the last counter is used with Robust
			 * Management frames.
			 */
			u8 rx_pn[IEEE80211_NUM_TIDS + 1][IEEE80211_GCMP_PN_LEN];
			struct crypto_aead *tfm;
			u32 replays; /* dot11RSNAStatsGCMPReplays */
		} gcmp;
		struct {
			/* generic cipher scheme */
			u8 rx_pn[IEEE80211_NUM_TIDS + 1][IEEE80211_MAX_PN_LEN];
		} gen;
	} u;

#ifdef CONFIG_MAC80211_DEBUGFS
	struct {
		struct dentry *stalink;
		struct dentry *dir;
		int cnt;
	} debugfs;
#endif

	unsigned int color;

	/*
	 * key config, must be last because it contains key
	 * material as variable length member
	 */
	struct ieee80211_key_conf conf;
};

struct ieee80211_key *
ieee80211_key_alloc(u32 cipher, int idx, size_t key_len,
		    const u8 *key_data,
		    size_t seq_len, const u8 *seq,
		    const struct ieee80211_cipher_scheme *cs);
/*
 * Insert a key into data structures (sdata, sta if necessary)
 * to make it used, free old key. On failure, also free the new key.
 */
int ieee80211_key_link(struct ieee80211_key *key,
		       struct ieee80211_sub_if_data *sdata,
		       struct sta_info *sta);
int ieee80211_set_tx_key(struct ieee80211_key *key);
void ieee80211_key_free(struct ieee80211_key *key, bool delay_tailroom);
void ieee80211_key_free_unused(struct ieee80211_key *key);
void ieee80211_set_default_key(struct ieee80211_sub_if_data *sdata, int idx,
			       bool uni, bool multi);
void ieee80211_set_default_mgmt_key(struct ieee80211_sub_if_data *sdata,
				    int idx);
void ieee80211_set_default_beacon_key(struct ieee80211_sub_if_data *sdata,
				      int idx);
void ieee80211_free_keys(struct ieee80211_sub_if_data *sdata,
			 bool force_synchronize);
void ieee80211_free_sta_keys(struct ieee80211_local *local,
			     struct sta_info *sta);
void ieee80211_reenable_keys(struct ieee80211_sub_if_data *sdata);

#define key_mtx_dereference(local, ref) \
	rcu_dereference_protected(ref, lockdep_is_held(&((local)->key_mtx)))

void ieee80211_delayed_tailroom_dec(struct work_struct *wk);

#endif /* IEEE80211_KEY_H */
