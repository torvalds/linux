/*
 * Software WEP encryption implementation
 * Copyright 2002, Jouni Malinen <jkmaline@cc.hut.fi>
 * Copyright 2003, Instant802 Networks, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/netdevice.h>
#include <linux/types.h>
#include <linux/random.h>
#include <linux/compiler.h>
#include <linux/crc32.h>
#include <linux/crypto.h>
#include <linux/err.h>
#include <linux/mm.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <asm/unaligned.h>

#include <net/mac80211.h>
#include "ieee80211_i.h"
#include "wep.h"


int ieee80211_wep_init(struct ieee80211_local *local)
{
	/* start WEP IV from a random value */
	get_random_bytes(&local->wep_iv, WEP_IV_LEN);

	local->wep_tx_tfm = crypto_alloc_blkcipher("ecb(arc4)", 0,
						CRYPTO_ALG_ASYNC);
	if (IS_ERR(local->wep_tx_tfm))
		return PTR_ERR(local->wep_tx_tfm);

	local->wep_rx_tfm = crypto_alloc_blkcipher("ecb(arc4)", 0,
						CRYPTO_ALG_ASYNC);
	if (IS_ERR(local->wep_rx_tfm)) {
		crypto_free_blkcipher(local->wep_tx_tfm);
		return PTR_ERR(local->wep_rx_tfm);
	}

	return 0;
}

void ieee80211_wep_free(struct ieee80211_local *local)
{
	crypto_free_blkcipher(local->wep_tx_tfm);
	crypto_free_blkcipher(local->wep_rx_tfm);
}

static inline bool ieee80211_wep_weak_iv(u32 iv, int keylen)
{
	/*
	 * Fluhrer, Mantin, and Shamir have reported weaknesses in the
	 * key scheduling algorithm of RC4. At least IVs (KeyByte + 3,
	 * 0xff, N) can be used to speedup attacks, so avoid using them.
	 */
	if ((iv & 0xff00) == 0xff00) {
		u8 B = (iv >> 16) & 0xff;
		if (B >= 3 && B < 3 + keylen)
			return true;
	}
	return false;
}


static void ieee80211_wep_get_iv(struct ieee80211_local *local,
				 int keylen, int keyidx, u8 *iv)
{
	local->wep_iv++;
	if (ieee80211_wep_weak_iv(local->wep_iv, keylen))
		local->wep_iv += 0x0100;

	if (!iv)
		return;

	*iv++ = (local->wep_iv >> 16) & 0xff;
	*iv++ = (local->wep_iv >> 8) & 0xff;
	*iv++ = local->wep_iv & 0xff;
	*iv++ = keyidx << 6;
}


static u8 *ieee80211_wep_add_iv(struct ieee80211_local *local,
				struct sk_buff *skb,
				int keylen, int keyidx)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	unsigned int hdrlen;
	u8 *newhdr;

	hdr->frame_control |= cpu_to_le16(IEEE80211_FCTL_PROTECTED);

	if (WARN_ON(skb_tailroom(skb) < WEP_ICV_LEN ||
		    skb_headroom(skb) < WEP_IV_LEN))
		return NULL;

	hdrlen = ieee80211_hdrlen(hdr->frame_control);
	newhdr = skb_push(skb, WEP_IV_LEN);
	memmove(newhdr, newhdr + WEP_IV_LEN, hdrlen);
	ieee80211_wep_get_iv(local, keylen, keyidx, newhdr + hdrlen);
	return newhdr + hdrlen;
}


static void ieee80211_wep_remove_iv(struct ieee80211_local *local,
				    struct sk_buff *skb,
				    struct ieee80211_key *key)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	unsigned int hdrlen;

	hdrlen = ieee80211_hdrlen(hdr->frame_control);
	memmove(skb->data + WEP_IV_LEN, skb->data, hdrlen);
	skb_pull(skb, WEP_IV_LEN);
}


/* Perform WEP encryption using given key. data buffer must have tailroom
 * for 4-byte ICV. data_len must not include this ICV. Note: this function
 * does _not_ add IV. data = RC4(data | CRC32(data)) */
void ieee80211_wep_encrypt_data(struct crypto_blkcipher *tfm, u8 *rc4key,
				size_t klen, u8 *data, size_t data_len)
{
	struct blkcipher_desc desc = { .tfm = tfm };
	struct scatterlist sg;
	__le32 icv;

	icv = cpu_to_le32(~crc32_le(~0, data, data_len));
	put_unaligned(icv, (__le32 *)(data + data_len));

	crypto_blkcipher_setkey(tfm, rc4key, klen);
	sg_init_one(&sg, data, data_len + WEP_ICV_LEN);
	crypto_blkcipher_encrypt(&desc, &sg, &sg, sg.length);
}


/* Perform WEP encryption on given skb. 4 bytes of extra space (IV) in the
 * beginning of the buffer 4 bytes of extra space (ICV) in the end of the
 * buffer will be added. Both IV and ICV will be transmitted, so the
 * payload length increases with 8 bytes.
 *
 * WEP frame payload: IV + TX key idx, RC4(data), ICV = RC4(CRC32(data))
 */
int ieee80211_wep_encrypt(struct ieee80211_local *local,
			  struct sk_buff *skb,
			  const u8 *key, int keylen, int keyidx)
{
	u8 *iv;
	size_t len;
	u8 rc4key[3 + WLAN_KEY_LEN_WEP104];

	iv = ieee80211_wep_add_iv(local, skb, keylen, keyidx);
	if (!iv)
		return -1;

	len = skb->len - (iv + WEP_IV_LEN - skb->data);

	/* Prepend 24-bit IV to RC4 key */
	memcpy(rc4key, iv, 3);

	/* Copy rest of the WEP key (the secret part) */
	memcpy(rc4key + 3, key, keylen);

	/* Add room for ICV */
	skb_put(skb, WEP_ICV_LEN);

	ieee80211_wep_encrypt_data(local->wep_tx_tfm, rc4key, keylen + 3,
				   iv + WEP_IV_LEN, len);

	return 0;
}


/* Perform WEP decryption using given key. data buffer includes encrypted
 * payload, including 4-byte ICV, but _not_ IV. data_len must not include ICV.
 * Return 0 on success and -1 on ICV mismatch. */
int ieee80211_wep_decrypt_data(struct crypto_blkcipher *tfm, u8 *rc4key,
			       size_t klen, u8 *data, size_t data_len)
{
	struct blkcipher_desc desc = { .tfm = tfm };
	struct scatterlist sg;
	__le32 crc;

	crypto_blkcipher_setkey(tfm, rc4key, klen);
	sg_init_one(&sg, data, data_len + WEP_ICV_LEN);
	crypto_blkcipher_decrypt(&desc, &sg, &sg, sg.length);

	crc = cpu_to_le32(~crc32_le(~0, data, data_len));
	if (memcmp(&crc, data + data_len, WEP_ICV_LEN) != 0)
		/* ICV mismatch */
		return -1;

	return 0;
}


/* Perform WEP decryption on given skb. Buffer includes whole WEP part of
 * the frame: IV (4 bytes), encrypted payload (including SNAP header),
 * ICV (4 bytes). skb->len includes both IV and ICV.
 *
 * Returns 0 if frame was decrypted successfully and ICV was correct and -1 on
 * failure. If frame is OK, IV and ICV will be removed, i.e., decrypted payload
 * is moved to the beginning of the skb and skb length will be reduced.
 */
static int ieee80211_wep_decrypt(struct ieee80211_local *local,
				 struct sk_buff *skb,
				 struct ieee80211_key *key)
{
	u32 klen;
	u8 *rc4key;
	u8 keyidx;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	unsigned int hdrlen;
	size_t len;
	int ret = 0;

	if (!ieee80211_has_protected(hdr->frame_control))
		return -1;

	hdrlen = ieee80211_hdrlen(hdr->frame_control);
	if (skb->len < hdrlen + WEP_IV_LEN + WEP_ICV_LEN)
		return -1;

	len = skb->len - hdrlen - WEP_IV_LEN - WEP_ICV_LEN;

	keyidx = skb->data[hdrlen + 3] >> 6;

	if (!key || keyidx != key->conf.keyidx || key->conf.alg != ALG_WEP)
		return -1;

	klen = 3 + key->conf.keylen;

	rc4key = kmalloc(klen, GFP_ATOMIC);
	if (!rc4key)
		return -1;

	/* Prepend 24-bit IV to RC4 key */
	memcpy(rc4key, skb->data + hdrlen, 3);

	/* Copy rest of the WEP key (the secret part) */
	memcpy(rc4key + 3, key->conf.key, key->conf.keylen);

	if (ieee80211_wep_decrypt_data(local->wep_rx_tfm, rc4key, klen,
				       skb->data + hdrlen + WEP_IV_LEN,
				       len))
		ret = -1;

	kfree(rc4key);

	/* Trim ICV */
	skb_trim(skb, skb->len - WEP_ICV_LEN);

	/* Remove IV */
	memmove(skb->data + WEP_IV_LEN, skb->data, hdrlen);
	skb_pull(skb, WEP_IV_LEN);

	return ret;
}


bool ieee80211_wep_is_weak_iv(struct sk_buff *skb, struct ieee80211_key *key)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	unsigned int hdrlen;
	u8 *ivpos;
	u32 iv;

	if (!ieee80211_has_protected(hdr->frame_control))
		return false;

	hdrlen = ieee80211_hdrlen(hdr->frame_control);
	ivpos = skb->data + hdrlen;
	iv = (ivpos[0] << 16) | (ivpos[1] << 8) | ivpos[2];

	return ieee80211_wep_weak_iv(iv, key->conf.keylen);
}

ieee80211_rx_result
ieee80211_crypto_wep_decrypt(struct ieee80211_rx_data *rx)
{
	struct sk_buff *skb = rx->skb;
	struct ieee80211_rx_status *status = IEEE80211_SKB_RXCB(skb);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;

	if (!ieee80211_is_data(hdr->frame_control) &&
	    !ieee80211_is_auth(hdr->frame_control))
		return RX_CONTINUE;

	if (!(status->flag & RX_FLAG_DECRYPTED)) {
		if (ieee80211_wep_decrypt(rx->local, rx->skb, rx->key))
			return RX_DROP_UNUSABLE;
	} else if (!(status->flag & RX_FLAG_IV_STRIPPED)) {
		ieee80211_wep_remove_iv(rx->local, rx->skb, rx->key);
		/* remove ICV */
		skb_trim(rx->skb, rx->skb->len - WEP_ICV_LEN);
	}

	return RX_CONTINUE;
}

static int wep_encrypt_skb(struct ieee80211_tx_data *tx, struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

	if (!info->control.hw_key) {
		if (ieee80211_wep_encrypt(tx->local, skb, tx->key->conf.key,
					  tx->key->conf.keylen,
					  tx->key->conf.keyidx))
			return -1;
	} else if (info->control.hw_key->flags &
			IEEE80211_KEY_FLAG_GENERATE_IV) {
		if (!ieee80211_wep_add_iv(tx->local, skb,
					  tx->key->conf.keylen,
					  tx->key->conf.keyidx))
			return -1;
	}

	return 0;
}

ieee80211_tx_result
ieee80211_crypto_wep_encrypt(struct ieee80211_tx_data *tx)
{
	struct sk_buff *skb;

	ieee80211_tx_set_protected(tx);

	skb = tx->skb;
	do {
		if (wep_encrypt_skb(tx, skb) < 0) {
			I802_DEBUG_INC(tx->local->tx_handlers_drop_wep);
			return TX_DROP;
		}
	} while ((skb = skb->next));

	return TX_CONTINUE;
}
