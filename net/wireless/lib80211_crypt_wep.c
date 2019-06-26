/*
 * lib80211 crypt: host-based WEP encryption implementation for lib80211
 *
 * Copyright (c) 2002-2004, Jouni Malinen <j@w1.fi>
 * Copyright (c) 2008, John W. Linville <linville@tuxdriver.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation. See README and COPYING for
 * more details.
 */

#include <linux/err.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/scatterlist.h>
#include <linux/skbuff.h>
#include <linux/mm.h>
#include <asm/string.h>

#include <net/lib80211.h>

#include <linux/crypto.h>
#include <linux/crc32.h>

MODULE_AUTHOR("Jouni Malinen");
MODULE_DESCRIPTION("lib80211 crypt: WEP");
MODULE_LICENSE("GPL");

struct lib80211_wep_data {
	u32 iv;
#define WEP_KEY_LEN 13
	u8 key[WEP_KEY_LEN + 1];
	u8 key_len;
	u8 key_idx;
	struct crypto_cipher *tx_tfm;
	struct crypto_cipher *rx_tfm;
};

static void *lib80211_wep_init(int keyidx)
{
	struct lib80211_wep_data *priv;

	priv = kzalloc(sizeof(*priv), GFP_ATOMIC);
	if (priv == NULL)
		goto fail;
	priv->key_idx = keyidx;

	priv->tx_tfm = crypto_alloc_cipher("arc4", 0, 0);
	if (IS_ERR(priv->tx_tfm)) {
		priv->tx_tfm = NULL;
		goto fail;
	}

	priv->rx_tfm = crypto_alloc_cipher("arc4", 0, 0);
	if (IS_ERR(priv->rx_tfm)) {
		priv->rx_tfm = NULL;
		goto fail;
	}
	/* start WEP IV from a random value */
	get_random_bytes(&priv->iv, 4);

	return priv;

      fail:
	if (priv) {
		crypto_free_cipher(priv->tx_tfm);
		crypto_free_cipher(priv->rx_tfm);
		kfree(priv);
	}
	return NULL;
}

static void lib80211_wep_deinit(void *priv)
{
	struct lib80211_wep_data *_priv = priv;
	if (_priv) {
		crypto_free_cipher(_priv->tx_tfm);
		crypto_free_cipher(_priv->rx_tfm);
	}
	kfree(priv);
}

/* Add WEP IV/key info to a frame that has at least 4 bytes of headroom */
static int lib80211_wep_build_iv(struct sk_buff *skb, int hdr_len,
			       u8 *key, int keylen, void *priv)
{
	struct lib80211_wep_data *wep = priv;
	u32 klen;
	u8 *pos;

	if (skb_headroom(skb) < 4 || skb->len < hdr_len)
		return -1;

	pos = skb_push(skb, 4);
	memmove(pos, pos + 4, hdr_len);
	pos += hdr_len;

	klen = 3 + wep->key_len;

	wep->iv++;

	/* Fluhrer, Mantin, and Shamir have reported weaknesses in the key
	 * scheduling algorithm of RC4. At least IVs (KeyByte + 3, 0xff, N)
	 * can be used to speedup attacks, so avoid using them. */
	if ((wep->iv & 0xff00) == 0xff00) {
		u8 B = (wep->iv >> 16) & 0xff;
		if (B >= 3 && B < klen)
			wep->iv += 0x0100;
	}

	/* Prepend 24-bit IV to RC4 key and TX frame */
	*pos++ = (wep->iv >> 16) & 0xff;
	*pos++ = (wep->iv >> 8) & 0xff;
	*pos++ = wep->iv & 0xff;
	*pos++ = wep->key_idx << 6;

	return 0;
}

/* Perform WEP encryption on given skb that has at least 4 bytes of headroom
 * for IV and 4 bytes of tailroom for ICV. Both IV and ICV will be transmitted,
 * so the payload length increases with 8 bytes.
 *
 * WEP frame payload: IV + TX key idx, RC4(data), ICV = RC4(CRC32(data))
 */
static int lib80211_wep_encrypt(struct sk_buff *skb, int hdr_len, void *priv)
{
	struct lib80211_wep_data *wep = priv;
	u32 crc, klen, len;
	u8 *pos, *icv;
	u8 key[WEP_KEY_LEN + 3];
	int i;

	/* other checks are in lib80211_wep_build_iv */
	if (skb_tailroom(skb) < 4)
		return -1;

	/* add the IV to the frame */
	if (lib80211_wep_build_iv(skb, hdr_len, NULL, 0, priv))
		return -1;

	/* Copy the IV into the first 3 bytes of the key */
	skb_copy_from_linear_data_offset(skb, hdr_len, key, 3);

	/* Copy rest of the WEP key (the secret part) */
	memcpy(key + 3, wep->key, wep->key_len);

	len = skb->len - hdr_len - 4;
	pos = skb->data + hdr_len + 4;
	klen = 3 + wep->key_len;

	/* Append little-endian CRC32 over only the data and encrypt it to produce ICV */
	crc = ~crc32_le(~0, pos, len);
	icv = skb_put(skb, 4);
	icv[0] = crc;
	icv[1] = crc >> 8;
	icv[2] = crc >> 16;
	icv[3] = crc >> 24;

	crypto_cipher_setkey(wep->tx_tfm, key, klen);

	for (i = 0; i < len + 4; i++)
		crypto_cipher_encrypt_one(wep->tx_tfm, pos + i, pos + i);

	return 0;
}

/* Perform WEP decryption on given buffer. Buffer includes whole WEP part of
 * the frame: IV (4 bytes), encrypted payload (including SNAP header),
 * ICV (4 bytes). len includes both IV and ICV.
 *
 * Returns 0 if frame was decrypted successfully and ICV was correct and -1 on
 * failure. If frame is OK, IV and ICV will be removed.
 */
static int lib80211_wep_decrypt(struct sk_buff *skb, int hdr_len, void *priv)
{
	struct lib80211_wep_data *wep = priv;
	u32 crc, klen, plen;
	u8 key[WEP_KEY_LEN + 3];
	u8 keyidx, *pos, icv[4];
	int i;

	if (skb->len < hdr_len + 8)
		return -1;

	pos = skb->data + hdr_len;
	key[0] = *pos++;
	key[1] = *pos++;
	key[2] = *pos++;
	keyidx = *pos++ >> 6;
	if (keyidx != wep->key_idx)
		return -1;

	klen = 3 + wep->key_len;

	/* Copy rest of the WEP key (the secret part) */
	memcpy(key + 3, wep->key, wep->key_len);

	/* Apply RC4 to data and compute CRC32 over decrypted data */
	plen = skb->len - hdr_len - 8;

	crypto_cipher_setkey(wep->rx_tfm, key, klen);
	for (i = 0; i < plen + 4; i++)
		crypto_cipher_decrypt_one(wep->rx_tfm, pos + i, pos + i);

	crc = ~crc32_le(~0, pos, plen);
	icv[0] = crc;
	icv[1] = crc >> 8;
	icv[2] = crc >> 16;
	icv[3] = crc >> 24;
	if (memcmp(icv, pos + plen, 4) != 0) {
		/* ICV mismatch - drop frame */
		return -2;
	}

	/* Remove IV and ICV */
	memmove(skb->data + 4, skb->data, hdr_len);
	skb_pull(skb, 4);
	skb_trim(skb, skb->len - 4);

	return 0;
}

static int lib80211_wep_set_key(void *key, int len, u8 * seq, void *priv)
{
	struct lib80211_wep_data *wep = priv;

	if (len < 0 || len > WEP_KEY_LEN)
		return -1;

	memcpy(wep->key, key, len);
	wep->key_len = len;

	return 0;
}

static int lib80211_wep_get_key(void *key, int len, u8 * seq, void *priv)
{
	struct lib80211_wep_data *wep = priv;

	if (len < wep->key_len)
		return -1;

	memcpy(key, wep->key, wep->key_len);

	return wep->key_len;
}

static void lib80211_wep_print_stats(struct seq_file *m, void *priv)
{
	struct lib80211_wep_data *wep = priv;
	seq_printf(m, "key[%d] alg=WEP len=%d\n", wep->key_idx, wep->key_len);
}

static struct lib80211_crypto_ops lib80211_crypt_wep = {
	.name = "WEP",
	.init = lib80211_wep_init,
	.deinit = lib80211_wep_deinit,
	.encrypt_mpdu = lib80211_wep_encrypt,
	.decrypt_mpdu = lib80211_wep_decrypt,
	.encrypt_msdu = NULL,
	.decrypt_msdu = NULL,
	.set_key = lib80211_wep_set_key,
	.get_key = lib80211_wep_get_key,
	.print_stats = lib80211_wep_print_stats,
	.extra_mpdu_prefix_len = 4,	/* IV */
	.extra_mpdu_postfix_len = 4,	/* ICV */
	.owner = THIS_MODULE,
};

static int __init lib80211_crypto_wep_init(void)
{
	return lib80211_register_crypto_ops(&lib80211_crypt_wep);
}

static void __exit lib80211_crypto_wep_exit(void)
{
	lib80211_unregister_crypto_ops(&lib80211_crypt_wep);
}

module_init(lib80211_crypto_wep_init);
module_exit(lib80211_crypto_wep_exit);
