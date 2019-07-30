// SPDX-License-Identifier: GPL-2.0-only
/*
 * lib80211 crypt: host-based CCMP encryption implementation for lib80211
 *
 * Copyright (c) 2003-2004, Jouni Malinen <j@w1.fi>
 * Copyright (c) 2008, John W. Linville <linville@tuxdriver.com>
 */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/if_ether.h>
#include <linux/if_arp.h>
#include <asm/string.h>
#include <linux/wireless.h>

#include <linux/ieee80211.h>

#include <linux/crypto.h>

#include <net/lib80211.h>

MODULE_AUTHOR("Jouni Malinen");
MODULE_DESCRIPTION("Host AP crypt: CCMP");
MODULE_LICENSE("GPL");

#define AES_BLOCK_LEN 16
#define CCMP_HDR_LEN 8
#define CCMP_MIC_LEN 8
#define CCMP_TK_LEN 16
#define CCMP_PN_LEN 6

struct lib80211_ccmp_data {
	u8 key[CCMP_TK_LEN];
	int key_set;

	u8 tx_pn[CCMP_PN_LEN];
	u8 rx_pn[CCMP_PN_LEN];

	u32 dot11RSNAStatsCCMPFormatErrors;
	u32 dot11RSNAStatsCCMPReplays;
	u32 dot11RSNAStatsCCMPDecryptErrors;

	int key_idx;

	struct crypto_cipher *tfm;

	/* scratch buffers for virt_to_page() (crypto API) */
	u8 tx_b0[AES_BLOCK_LEN], tx_b[AES_BLOCK_LEN],
	    tx_e[AES_BLOCK_LEN], tx_s0[AES_BLOCK_LEN];
	u8 rx_b0[AES_BLOCK_LEN], rx_b[AES_BLOCK_LEN], rx_a[AES_BLOCK_LEN];
};

static inline void lib80211_ccmp_aes_encrypt(struct crypto_cipher *tfm,
					      const u8 pt[16], u8 ct[16])
{
	crypto_cipher_encrypt_one(tfm, ct, pt);
}

static void *lib80211_ccmp_init(int key_idx)
{
	struct lib80211_ccmp_data *priv;

	priv = kzalloc(sizeof(*priv), GFP_ATOMIC);
	if (priv == NULL)
		goto fail;
	priv->key_idx = key_idx;

	priv->tfm = crypto_alloc_cipher("aes", 0, 0);
	if (IS_ERR(priv->tfm)) {
		priv->tfm = NULL;
		goto fail;
	}

	return priv;

      fail:
	if (priv) {
		if (priv->tfm)
			crypto_free_cipher(priv->tfm);
		kfree(priv);
	}

	return NULL;
}

static void lib80211_ccmp_deinit(void *priv)
{
	struct lib80211_ccmp_data *_priv = priv;
	if (_priv && _priv->tfm)
		crypto_free_cipher(_priv->tfm);
	kfree(priv);
}

static inline void xor_block(u8 * b, u8 * a, size_t len)
{
	int i;
	for (i = 0; i < len; i++)
		b[i] ^= a[i];
}

static void ccmp_init_blocks(struct crypto_cipher *tfm,
			     struct ieee80211_hdr *hdr,
			     u8 * pn, size_t dlen, u8 * b0, u8 * auth, u8 * s0)
{
	u8 *pos, qc = 0;
	size_t aad_len;
	int a4_included, qc_included;
	u8 aad[2 * AES_BLOCK_LEN];

	a4_included = ieee80211_has_a4(hdr->frame_control);
	qc_included = ieee80211_is_data_qos(hdr->frame_control);

	aad_len = 22;
	if (a4_included)
		aad_len += 6;
	if (qc_included) {
		pos = (u8 *) & hdr->addr4;
		if (a4_included)
			pos += 6;
		qc = *pos & 0x0f;
		aad_len += 2;
	}

	/* CCM Initial Block:
	 * Flag (Include authentication header, M=3 (8-octet MIC),
	 *       L=1 (2-octet Dlen))
	 * Nonce: 0x00 | A2 | PN
	 * Dlen */
	b0[0] = 0x59;
	b0[1] = qc;
	memcpy(b0 + 2, hdr->addr2, ETH_ALEN);
	memcpy(b0 + 8, pn, CCMP_PN_LEN);
	b0[14] = (dlen >> 8) & 0xff;
	b0[15] = dlen & 0xff;

	/* AAD:
	 * FC with bits 4..6 and 11..13 masked to zero; 14 is always one
	 * A1 | A2 | A3
	 * SC with bits 4..15 (seq#) masked to zero
	 * A4 (if present)
	 * QC (if present)
	 */
	pos = (u8 *) hdr;
	aad[0] = 0;		/* aad_len >> 8 */
	aad[1] = aad_len & 0xff;
	aad[2] = pos[0] & 0x8f;
	aad[3] = pos[1] & 0xc7;
	memcpy(aad + 4, hdr->addr1, 3 * ETH_ALEN);
	pos = (u8 *) & hdr->seq_ctrl;
	aad[22] = pos[0] & 0x0f;
	aad[23] = 0;		/* all bits masked */
	memset(aad + 24, 0, 8);
	if (a4_included)
		memcpy(aad + 24, hdr->addr4, ETH_ALEN);
	if (qc_included) {
		aad[a4_included ? 30 : 24] = qc;
		/* rest of QC masked */
	}

	/* Start with the first block and AAD */
	lib80211_ccmp_aes_encrypt(tfm, b0, auth);
	xor_block(auth, aad, AES_BLOCK_LEN);
	lib80211_ccmp_aes_encrypt(tfm, auth, auth);
	xor_block(auth, &aad[AES_BLOCK_LEN], AES_BLOCK_LEN);
	lib80211_ccmp_aes_encrypt(tfm, auth, auth);
	b0[0] &= 0x07;
	b0[14] = b0[15] = 0;
	lib80211_ccmp_aes_encrypt(tfm, b0, s0);
}

static int lib80211_ccmp_hdr(struct sk_buff *skb, int hdr_len,
			      u8 *aeskey, int keylen, void *priv)
{
	struct lib80211_ccmp_data *key = priv;
	int i;
	u8 *pos;

	if (skb_headroom(skb) < CCMP_HDR_LEN || skb->len < hdr_len)
		return -1;

	if (aeskey != NULL && keylen >= CCMP_TK_LEN)
		memcpy(aeskey, key->key, CCMP_TK_LEN);

	pos = skb_push(skb, CCMP_HDR_LEN);
	memmove(pos, pos + CCMP_HDR_LEN, hdr_len);
	pos += hdr_len;

	i = CCMP_PN_LEN - 1;
	while (i >= 0) {
		key->tx_pn[i]++;
		if (key->tx_pn[i] != 0)
			break;
		i--;
	}

	*pos++ = key->tx_pn[5];
	*pos++ = key->tx_pn[4];
	*pos++ = 0;
	*pos++ = (key->key_idx << 6) | (1 << 5) /* Ext IV included */ ;
	*pos++ = key->tx_pn[3];
	*pos++ = key->tx_pn[2];
	*pos++ = key->tx_pn[1];
	*pos++ = key->tx_pn[0];

	return CCMP_HDR_LEN;
}

static int lib80211_ccmp_encrypt(struct sk_buff *skb, int hdr_len, void *priv)
{
	struct lib80211_ccmp_data *key = priv;
	int data_len, i, blocks, last, len;
	u8 *pos, *mic;
	struct ieee80211_hdr *hdr;
	u8 *b0 = key->tx_b0;
	u8 *b = key->tx_b;
	u8 *e = key->tx_e;
	u8 *s0 = key->tx_s0;

	if (skb_tailroom(skb) < CCMP_MIC_LEN || skb->len < hdr_len)
		return -1;

	data_len = skb->len - hdr_len;
	len = lib80211_ccmp_hdr(skb, hdr_len, NULL, 0, priv);
	if (len < 0)
		return -1;

	pos = skb->data + hdr_len + CCMP_HDR_LEN;
	hdr = (struct ieee80211_hdr *)skb->data;
	ccmp_init_blocks(key->tfm, hdr, key->tx_pn, data_len, b0, b, s0);

	blocks = DIV_ROUND_UP(data_len, AES_BLOCK_LEN);
	last = data_len % AES_BLOCK_LEN;

	for (i = 1; i <= blocks; i++) {
		len = (i == blocks && last) ? last : AES_BLOCK_LEN;
		/* Authentication */
		xor_block(b, pos, len);
		lib80211_ccmp_aes_encrypt(key->tfm, b, b);
		/* Encryption, with counter */
		b0[14] = (i >> 8) & 0xff;
		b0[15] = i & 0xff;
		lib80211_ccmp_aes_encrypt(key->tfm, b0, e);
		xor_block(pos, e, len);
		pos += len;
	}

	mic = skb_put(skb, CCMP_MIC_LEN);
	for (i = 0; i < CCMP_MIC_LEN; i++)
		mic[i] = b[i] ^ s0[i];

	return 0;
}

/*
 * deal with seq counter wrapping correctly.
 * refer to timer_after() for jiffies wrapping handling
 */
static inline int ccmp_replay_check(u8 *pn_n, u8 *pn_o)
{
	u32 iv32_n, iv16_n;
	u32 iv32_o, iv16_o;

	iv32_n = (pn_n[0] << 24) | (pn_n[1] << 16) | (pn_n[2] << 8) | pn_n[3];
	iv16_n = (pn_n[4] << 8) | pn_n[5];

	iv32_o = (pn_o[0] << 24) | (pn_o[1] << 16) | (pn_o[2] << 8) | pn_o[3];
	iv16_o = (pn_o[4] << 8) | pn_o[5];

	if ((s32)iv32_n - (s32)iv32_o < 0 ||
	    (iv32_n == iv32_o && iv16_n <= iv16_o))
		return 1;
	return 0;
}

static int lib80211_ccmp_decrypt(struct sk_buff *skb, int hdr_len, void *priv)
{
	struct lib80211_ccmp_data *key = priv;
	u8 keyidx, *pos;
	struct ieee80211_hdr *hdr;
	u8 *b0 = key->rx_b0;
	u8 *b = key->rx_b;
	u8 *a = key->rx_a;
	u8 pn[6];
	int i, blocks, last, len;
	size_t data_len = skb->len - hdr_len - CCMP_HDR_LEN - CCMP_MIC_LEN;
	u8 *mic = skb->data + skb->len - CCMP_MIC_LEN;

	if (skb->len < hdr_len + CCMP_HDR_LEN + CCMP_MIC_LEN) {
		key->dot11RSNAStatsCCMPFormatErrors++;
		return -1;
	}

	hdr = (struct ieee80211_hdr *)skb->data;
	pos = skb->data + hdr_len;
	keyidx = pos[3];
	if (!(keyidx & (1 << 5))) {
		net_dbg_ratelimited("CCMP: received packet without ExtIV flag from %pM\n",
				    hdr->addr2);
		key->dot11RSNAStatsCCMPFormatErrors++;
		return -2;
	}
	keyidx >>= 6;
	if (key->key_idx != keyidx) {
		net_dbg_ratelimited("CCMP: RX tkey->key_idx=%d frame keyidx=%d\n",
				    key->key_idx, keyidx);
		return -6;
	}
	if (!key->key_set) {
		net_dbg_ratelimited("CCMP: received packet from %pM with keyid=%d that does not have a configured key\n",
				    hdr->addr2, keyidx);
		return -3;
	}

	pn[0] = pos[7];
	pn[1] = pos[6];
	pn[2] = pos[5];
	pn[3] = pos[4];
	pn[4] = pos[1];
	pn[5] = pos[0];
	pos += 8;

	if (ccmp_replay_check(pn, key->rx_pn)) {
#ifdef CONFIG_LIB80211_DEBUG
		net_dbg_ratelimited("CCMP: replay detected: STA=%pM previous PN %02x%02x%02x%02x%02x%02x received PN %02x%02x%02x%02x%02x%02x\n",
				    hdr->addr2,
				    key->rx_pn[0], key->rx_pn[1], key->rx_pn[2],
				    key->rx_pn[3], key->rx_pn[4], key->rx_pn[5],
				    pn[0], pn[1], pn[2], pn[3], pn[4], pn[5]);
#endif
		key->dot11RSNAStatsCCMPReplays++;
		return -4;
	}

	ccmp_init_blocks(key->tfm, hdr, pn, data_len, b0, a, b);
	xor_block(mic, b, CCMP_MIC_LEN);

	blocks = DIV_ROUND_UP(data_len, AES_BLOCK_LEN);
	last = data_len % AES_BLOCK_LEN;

	for (i = 1; i <= blocks; i++) {
		len = (i == blocks && last) ? last : AES_BLOCK_LEN;
		/* Decrypt, with counter */
		b0[14] = (i >> 8) & 0xff;
		b0[15] = i & 0xff;
		lib80211_ccmp_aes_encrypt(key->tfm, b0, b);
		xor_block(pos, b, len);
		/* Authentication */
		xor_block(a, pos, len);
		lib80211_ccmp_aes_encrypt(key->tfm, a, a);
		pos += len;
	}

	if (memcmp(mic, a, CCMP_MIC_LEN) != 0) {
		net_dbg_ratelimited("CCMP: decrypt failed: STA=%pM\n",
				    hdr->addr2);
		key->dot11RSNAStatsCCMPDecryptErrors++;
		return -5;
	}

	memcpy(key->rx_pn, pn, CCMP_PN_LEN);

	/* Remove hdr and MIC */
	memmove(skb->data + CCMP_HDR_LEN, skb->data, hdr_len);
	skb_pull(skb, CCMP_HDR_LEN);
	skb_trim(skb, skb->len - CCMP_MIC_LEN);

	return keyidx;
}

static int lib80211_ccmp_set_key(void *key, int len, u8 * seq, void *priv)
{
	struct lib80211_ccmp_data *data = priv;
	int keyidx;
	struct crypto_cipher *tfm = data->tfm;

	keyidx = data->key_idx;
	memset(data, 0, sizeof(*data));
	data->key_idx = keyidx;
	data->tfm = tfm;
	if (len == CCMP_TK_LEN) {
		memcpy(data->key, key, CCMP_TK_LEN);
		data->key_set = 1;
		if (seq) {
			data->rx_pn[0] = seq[5];
			data->rx_pn[1] = seq[4];
			data->rx_pn[2] = seq[3];
			data->rx_pn[3] = seq[2];
			data->rx_pn[4] = seq[1];
			data->rx_pn[5] = seq[0];
		}
		crypto_cipher_setkey(data->tfm, data->key, CCMP_TK_LEN);
	} else if (len == 0)
		data->key_set = 0;
	else
		return -1;

	return 0;
}

static int lib80211_ccmp_get_key(void *key, int len, u8 * seq, void *priv)
{
	struct lib80211_ccmp_data *data = priv;

	if (len < CCMP_TK_LEN)
		return -1;

	if (!data->key_set)
		return 0;
	memcpy(key, data->key, CCMP_TK_LEN);

	if (seq) {
		seq[0] = data->tx_pn[5];
		seq[1] = data->tx_pn[4];
		seq[2] = data->tx_pn[3];
		seq[3] = data->tx_pn[2];
		seq[4] = data->tx_pn[1];
		seq[5] = data->tx_pn[0];
	}

	return CCMP_TK_LEN;
}

static void lib80211_ccmp_print_stats(struct seq_file *m, void *priv)
{
	struct lib80211_ccmp_data *ccmp = priv;

	seq_printf(m,
		   "key[%d] alg=CCMP key_set=%d "
		   "tx_pn=%02x%02x%02x%02x%02x%02x "
		   "rx_pn=%02x%02x%02x%02x%02x%02x "
		   "format_errors=%d replays=%d decrypt_errors=%d\n",
		   ccmp->key_idx, ccmp->key_set,
		   ccmp->tx_pn[0], ccmp->tx_pn[1], ccmp->tx_pn[2],
		   ccmp->tx_pn[3], ccmp->tx_pn[4], ccmp->tx_pn[5],
		   ccmp->rx_pn[0], ccmp->rx_pn[1], ccmp->rx_pn[2],
		   ccmp->rx_pn[3], ccmp->rx_pn[4], ccmp->rx_pn[5],
		   ccmp->dot11RSNAStatsCCMPFormatErrors,
		   ccmp->dot11RSNAStatsCCMPReplays,
		   ccmp->dot11RSNAStatsCCMPDecryptErrors);
}

static struct lib80211_crypto_ops lib80211_crypt_ccmp = {
	.name = "CCMP",
	.init = lib80211_ccmp_init,
	.deinit = lib80211_ccmp_deinit,
	.encrypt_mpdu = lib80211_ccmp_encrypt,
	.decrypt_mpdu = lib80211_ccmp_decrypt,
	.encrypt_msdu = NULL,
	.decrypt_msdu = NULL,
	.set_key = lib80211_ccmp_set_key,
	.get_key = lib80211_ccmp_get_key,
	.print_stats = lib80211_ccmp_print_stats,
	.extra_mpdu_prefix_len = CCMP_HDR_LEN,
	.extra_mpdu_postfix_len = CCMP_MIC_LEN,
	.owner = THIS_MODULE,
};

static int __init lib80211_crypto_ccmp_init(void)
{
	return lib80211_register_crypto_ops(&lib80211_crypt_ccmp);
}

static void __exit lib80211_crypto_ccmp_exit(void)
{
	lib80211_unregister_crypto_ops(&lib80211_crypt_ccmp);
}

module_init(lib80211_crypto_ccmp_init);
module_exit(lib80211_crypto_ccmp_exit);
