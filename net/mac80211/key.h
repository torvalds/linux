/*
 * Copyright 2002-2004, Instant802 Networks, Inc.
 * Copyright 2005, Devicescape Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef IEEE80211_KEY_H
#define IEEE80211_KEY_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/crypto.h>
#include <linux/rcupdate.h>
#include <net/mac80211.h>

/* ALG_TKIP
 * struct ieee80211_key::key is encoded as a 256-bit (32 byte) data block:
 * Temporal Encryption Key (128 bits)
 * Temporal Authenticator Tx MIC Key (64 bits)
 * Temporal Authenticator Rx MIC Key (64 bits)
 */

#define WEP_IV_LEN 4
#define WEP_ICV_LEN 4

#define ALG_TKIP_KEY_LEN 32
/* Starting offsets for each key */
#define ALG_TKIP_TEMP_ENCR_KEY 0
#define ALG_TKIP_TEMP_AUTH_TX_MIC_KEY 16
#define ALG_TKIP_TEMP_AUTH_RX_MIC_KEY 24
#define TKIP_IV_LEN 8
#define TKIP_ICV_LEN 4

#define ALG_CCMP_KEY_LEN 16
#define CCMP_HDR_LEN 8
#define CCMP_MIC_LEN 8
#define CCMP_TK_LEN 16
#define CCMP_PN_LEN 6

#define NUM_RX_DATA_QUEUES 17

struct ieee80211_local;
struct ieee80211_sub_if_data;
struct sta_info;

/**
 * enum ieee80211_internal_key_flags - internal key flags
 *
 * @KEY_FLAG_UPLOADED_TO_HARDWARE: Indicates that this key is present
 *	in the hardware for TX crypto hardware acceleration.
 * @KEY_FLAG_TODO_DELETE: Key is marked for deletion and will, after an
 *	RCU grace period, no longer be reachable other than from the
 *	todo list.
 * @KEY_FLAG_TODO_HWACCEL_ADD: Key needs to be added to hardware acceleration.
 * @KEY_FLAG_TODO_HWACCEL_REMOVE: Key needs to be removed from hardware
 *	acceleration.
 * @KEY_FLAG_TODO_DEFKEY: Key is default key and debugfs needs to be updated.
 * @KEY_FLAG_TODO_ADD_DEBUGFS: Key needs to be added to debugfs.
 */
enum ieee80211_internal_key_flags {
	KEY_FLAG_UPLOADED_TO_HARDWARE	= BIT(0),
	KEY_FLAG_TODO_DELETE		= BIT(1),
	KEY_FLAG_TODO_HWACCEL_ADD	= BIT(2),
	KEY_FLAG_TODO_HWACCEL_REMOVE	= BIT(3),
	KEY_FLAG_TODO_DEFKEY		= BIT(4),
	KEY_FLAG_TODO_ADD_DEBUGFS	= BIT(5),
};

struct ieee80211_key {
	struct ieee80211_local *local;
	struct ieee80211_sub_if_data *sdata;
	struct sta_info *sta;

	/* for sdata list */
	struct list_head list;
	/* for todo list */
	struct list_head todo;

	/* protected by todo lock! */
	unsigned int flags;

	union {
		struct {
			/* last used TSC */
			u32 iv32;
			u16 iv16;
			u16 p1k[5];
			int tx_initialized;

			/* last received RSC */
			u32 iv32_rx[NUM_RX_DATA_QUEUES];
			u16 iv16_rx[NUM_RX_DATA_QUEUES];
			u16 p1k_rx[NUM_RX_DATA_QUEUES][5];
			int rx_initialized[NUM_RX_DATA_QUEUES];
		} tkip;
		struct {
			u8 tx_pn[6];
			u8 rx_pn[NUM_RX_DATA_QUEUES][6];
			struct crypto_cipher *tfm;
			u32 replays; /* dot11RSNAStatsCCMPReplays */
			/* scratch buffers for virt_to_page() (crypto API) */
#ifndef AES_BLOCK_LEN
#define AES_BLOCK_LEN 16
#endif
			u8 tx_crypto_buf[6 * AES_BLOCK_LEN];
			u8 rx_crypto_buf[6 * AES_BLOCK_LEN];
		} ccmp;
	} u;

	/* number of times this key has been used */
	int tx_rx_count;

#ifdef CONFIG_MAC80211_DEBUGFS
	struct {
		struct dentry *stalink;
		struct dentry *dir;
		struct dentry *keylen;
		struct dentry *flags;
		struct dentry *keyidx;
		struct dentry *hw_key_idx;
		struct dentry *tx_rx_count;
		struct dentry *algorithm;
		struct dentry *tx_spec;
		struct dentry *rx_spec;
		struct dentry *replays;
		struct dentry *key;
		struct dentry *ifindex;
		int cnt;
	} debugfs;
#endif

	/*
	 * key config, must be last because it contains key
	 * material as variable length member
	 */
	struct ieee80211_key_conf conf;
};

struct ieee80211_key *ieee80211_key_alloc(enum ieee80211_key_alg alg,
					  int idx,
					  size_t key_len,
					  const u8 *key_data);
/*
 * Insert a key into data structures (sdata, sta if necessary)
 * to make it used, free old key.
 */
void ieee80211_key_link(struct ieee80211_key *key,
			struct ieee80211_sub_if_data *sdata,
			struct sta_info *sta);
void ieee80211_key_free(struct ieee80211_key *key);
void ieee80211_set_default_key(struct ieee80211_sub_if_data *sdata, int idx);
void ieee80211_free_keys(struct ieee80211_sub_if_data *sdata);
void ieee80211_enable_keys(struct ieee80211_sub_if_data *sdata);
void ieee80211_disable_keys(struct ieee80211_sub_if_data *sdata);

void ieee80211_key_todo(void);

#endif /* IEEE80211_KEY_H */
