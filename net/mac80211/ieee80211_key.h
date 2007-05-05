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
#include <linux/kref.h>
#include <linux/crypto.h>
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

struct ieee80211_key {
	struct kref kref;

	int hw_key_idx; /* filled and used by low-level driver */
	ieee80211_key_alg alg;
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
	int tx_rx_count; /* number of times this key has been used */
	int keylen;

	/* if the low level driver can provide hardware acceleration it should
	 * clear this flag */
	unsigned int force_sw_encrypt:1;
	unsigned int default_tx_key:1; /* This key is the new default TX key
					* (used only for broadcast keys). */
	s8 keyidx; /* WEP key index */

#ifdef CONFIG_MAC80211_DEBUGFS
	struct {
		struct dentry *stalink;
		struct dentry *dir;
		struct dentry *keylen;
		struct dentry *force_sw_encrypt;
		struct dentry *keyidx;
		struct dentry *hw_key_idx;
		struct dentry *tx_rx_count;
		struct dentry *algorithm;
		struct dentry *tx_spec;
		struct dentry *rx_spec;
		struct dentry *replays;
		struct dentry *key;
	} debugfs;
#endif

	u8 key[0];
};

#endif /* IEEE80211_KEY_H */
