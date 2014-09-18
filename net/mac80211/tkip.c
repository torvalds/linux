/*
 * Copyright 2002-2004, Instant802 Networks, Inc.
 * Copyright 2005, Devicescape Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/types.h>
#include <linux/netdevice.h>
#include <linux/export.h>
#include <asm/unaligned.h>

#include <net/mac80211.h>
#include "driver-ops.h"
#include "key.h"
#include "tkip.h"
#include "wep.h"

#define PHASE1_LOOP_COUNT 8

/*
 * 2-byte by 2-byte subset of the full AES S-box table; second part of this
 * table is identical to first part but byte-swapped
 */
static const u16 tkip_sbox[256] =
{
	0xC6A5, 0xF884, 0xEE99, 0xF68D, 0xFF0D, 0xD6BD, 0xDEB1, 0x9154,
	0x6050, 0x0203, 0xCEA9, 0x567D, 0xE719, 0xB562, 0x4DE6, 0xEC9A,
	0x8F45, 0x1F9D, 0x8940, 0xFA87, 0xEF15, 0xB2EB, 0x8EC9, 0xFB0B,
	0x41EC, 0xB367, 0x5FFD, 0x45EA, 0x23BF, 0x53F7, 0xE496, 0x9B5B,
	0x75C2, 0xE11C, 0x3DAE, 0x4C6A, 0x6C5A, 0x7E41, 0xF502, 0x834F,
	0x685C, 0x51F4, 0xD134, 0xF908, 0xE293, 0xAB73, 0x6253, 0x2A3F,
	0x080C, 0x9552, 0x4665, 0x9D5E, 0x3028, 0x37A1, 0x0A0F, 0x2FB5,
	0x0E09, 0x2436, 0x1B9B, 0xDF3D, 0xCD26, 0x4E69, 0x7FCD, 0xEA9F,
	0x121B, 0x1D9E, 0x5874, 0x342E, 0x362D, 0xDCB2, 0xB4EE, 0x5BFB,
	0xA4F6, 0x764D, 0xB761, 0x7DCE, 0x527B, 0xDD3E, 0x5E71, 0x1397,
	0xA6F5, 0xB968, 0x0000, 0xC12C, 0x4060, 0xE31F, 0x79C8, 0xB6ED,
	0xD4BE, 0x8D46, 0x67D9, 0x724B, 0x94DE, 0x98D4, 0xB0E8, 0x854A,
	0xBB6B, 0xC52A, 0x4FE5, 0xED16, 0x86C5, 0x9AD7, 0x6655, 0x1194,
	0x8ACF, 0xE910, 0x0406, 0xFE81, 0xA0F0, 0x7844, 0x25BA, 0x4BE3,
	0xA2F3, 0x5DFE, 0x80C0, 0x058A, 0x3FAD, 0x21BC, 0x7048, 0xF104,
	0x63DF, 0x77C1, 0xAF75, 0x4263, 0x2030, 0xE51A, 0xFD0E, 0xBF6D,
	0x814C, 0x1814, 0x2635, 0xC32F, 0xBEE1, 0x35A2, 0x88CC, 0x2E39,
	0x9357, 0x55F2, 0xFC82, 0x7A47, 0xC8AC, 0xBAE7, 0x322B, 0xE695,
	0xC0A0, 0x1998, 0x9ED1, 0xA37F, 0x4466, 0x547E, 0x3BAB, 0x0B83,
	0x8CCA, 0xC729, 0x6BD3, 0x283C, 0xA779, 0xBCE2, 0x161D, 0xAD76,
	0xDB3B, 0x6456, 0x744E, 0x141E, 0x92DB, 0x0C0A, 0x486C, 0xB8E4,
	0x9F5D, 0xBD6E, 0x43EF, 0xC4A6, 0x39A8, 0x31A4, 0xD337, 0xF28B,
	0xD532, 0x8B43, 0x6E59, 0xDAB7, 0x018C, 0xB164, 0x9CD2, 0x49E0,
	0xD8B4, 0xACFA, 0xF307, 0xCF25, 0xCAAF, 0xF48E, 0x47E9, 0x1018,
	0x6FD5, 0xF088, 0x4A6F, 0x5C72, 0x3824, 0x57F1, 0x73C7, 0x9751,
	0xCB23, 0xA17C, 0xE89C, 0x3E21, 0x96DD, 0x61DC, 0x0D86, 0x0F85,
	0xE090, 0x7C42, 0x71C4, 0xCCAA, 0x90D8, 0x0605, 0xF701, 0x1C12,
	0xC2A3, 0x6A5F, 0xAEF9, 0x69D0, 0x1791, 0x9958, 0x3A27, 0x27B9,
	0xD938, 0xEB13, 0x2BB3, 0x2233, 0xD2BB, 0xA970, 0x0789, 0x33A7,
	0x2DB6, 0x3C22, 0x1592, 0xC920, 0x8749, 0xAAFF, 0x5078, 0xA57A,
	0x038F, 0x59F8, 0x0980, 0x1A17, 0x65DA, 0xD731, 0x84C6, 0xD0B8,
	0x82C3, 0x29B0, 0x5A77, 0x1E11, 0x7BCB, 0xA8FC, 0x6DD6, 0x2C3A,
};

static u16 tkipS(u16 val)
{
	return tkip_sbox[val & 0xff] ^ swab16(tkip_sbox[val >> 8]);
}

static u8 *write_tkip_iv(u8 *pos, u16 iv16)
{
	*pos++ = iv16 >> 8;
	*pos++ = ((iv16 >> 8) | 0x20) & 0x7f;
	*pos++ = iv16 & 0xFF;
	return pos;
}

/*
 * P1K := Phase1(TA, TK, TSC)
 * TA = transmitter address (48 bits)
 * TK = dot11DefaultKeyValue or dot11KeyMappingValue (128 bits)
 * TSC = TKIP sequence counter (48 bits, only 32 msb bits used)
 * P1K: 80 bits
 */
static void tkip_mixing_phase1(const u8 *tk, struct tkip_ctx *ctx,
			       const u8 *ta, u32 tsc_IV32)
{
	int i, j;
	u16 *p1k = ctx->p1k;

	p1k[0] = tsc_IV32 & 0xFFFF;
	p1k[1] = tsc_IV32 >> 16;
	p1k[2] = get_unaligned_le16(ta + 0);
	p1k[3] = get_unaligned_le16(ta + 2);
	p1k[4] = get_unaligned_le16(ta + 4);

	for (i = 0; i < PHASE1_LOOP_COUNT; i++) {
		j = 2 * (i & 1);
		p1k[0] += tkipS(p1k[4] ^ get_unaligned_le16(tk + 0 + j));
		p1k[1] += tkipS(p1k[0] ^ get_unaligned_le16(tk + 4 + j));
		p1k[2] += tkipS(p1k[1] ^ get_unaligned_le16(tk + 8 + j));
		p1k[3] += tkipS(p1k[2] ^ get_unaligned_le16(tk + 12 + j));
		p1k[4] += tkipS(p1k[3] ^ get_unaligned_le16(tk + 0 + j)) + i;
	}
	ctx->state = TKIP_STATE_PHASE1_DONE;
	ctx->p1k_iv32 = tsc_IV32;
}

static void tkip_mixing_phase2(const u8 *tk, struct tkip_ctx *ctx,
			       u16 tsc_IV16, u8 *rc4key)
{
	u16 ppk[6];
	const u16 *p1k = ctx->p1k;
	int i;

	ppk[0] = p1k[0];
	ppk[1] = p1k[1];
	ppk[2] = p1k[2];
	ppk[3] = p1k[3];
	ppk[4] = p1k[4];
	ppk[5] = p1k[4] + tsc_IV16;

	ppk[0] += tkipS(ppk[5] ^ get_unaligned_le16(tk + 0));
	ppk[1] += tkipS(ppk[0] ^ get_unaligned_le16(tk + 2));
	ppk[2] += tkipS(ppk[1] ^ get_unaligned_le16(tk + 4));
	ppk[3] += tkipS(ppk[2] ^ get_unaligned_le16(tk + 6));
	ppk[4] += tkipS(ppk[3] ^ get_unaligned_le16(tk + 8));
	ppk[5] += tkipS(ppk[4] ^ get_unaligned_le16(tk + 10));
	ppk[0] += ror16(ppk[5] ^ get_unaligned_le16(tk + 12), 1);
	ppk[1] += ror16(ppk[0] ^ get_unaligned_le16(tk + 14), 1);
	ppk[2] += ror16(ppk[1], 1);
	ppk[3] += ror16(ppk[2], 1);
	ppk[4] += ror16(ppk[3], 1);
	ppk[5] += ror16(ppk[4], 1);

	rc4key = write_tkip_iv(rc4key, tsc_IV16);
	*rc4key++ = ((ppk[5] ^ get_unaligned_le16(tk)) >> 1) & 0xFF;

	for (i = 0; i < 6; i++)
		put_unaligned_le16(ppk[i], rc4key + 2 * i);
}

/* Add TKIP IV and Ext. IV at @pos. @iv0, @iv1, and @iv2 are the first octets
 * of the IV. Returns pointer to the octet following IVs (i.e., beginning of
 * the packet payload). */
u8 *ieee80211_tkip_add_iv(u8 *pos, struct ieee80211_key *key)
{
	lockdep_assert_held(&key->u.tkip.txlock);

	pos = write_tkip_iv(pos, key->u.tkip.tx.iv16);
	*pos++ = (key->conf.keyidx << 6) | (1 << 5) /* Ext IV */;
	put_unaligned_le32(key->u.tkip.tx.iv32, pos);
	return pos + 4;
}

static void ieee80211_compute_tkip_p1k(struct ieee80211_key *key, u32 iv32)
{
	struct ieee80211_sub_if_data *sdata = key->sdata;
	struct tkip_ctx *ctx = &key->u.tkip.tx;
	const u8 *tk = &key->conf.key[NL80211_TKIP_DATA_OFFSET_ENCR_KEY];

	lockdep_assert_held(&key->u.tkip.txlock);

	/*
	 * Update the P1K when the IV32 is different from the value it
	 * had when we last computed it (or when not initialised yet).
	 * This might flip-flop back and forth if packets are processed
	 * out-of-order due to the different ACs, but then we have to
	 * just compute the P1K more often.
	 */
	if (ctx->p1k_iv32 != iv32 || ctx->state == TKIP_STATE_NOT_INIT)
		tkip_mixing_phase1(tk, ctx, sdata->vif.addr, iv32);
}

void ieee80211_get_tkip_p1k_iv(struct ieee80211_key_conf *keyconf,
			       u32 iv32, u16 *p1k)
{
	struct ieee80211_key *key = (struct ieee80211_key *)
			container_of(keyconf, struct ieee80211_key, conf);
	struct tkip_ctx *ctx = &key->u.tkip.tx;

	spin_lock_bh(&key->u.tkip.txlock);
	ieee80211_compute_tkip_p1k(key, iv32);
	memcpy(p1k, ctx->p1k, sizeof(ctx->p1k));
	spin_unlock_bh(&key->u.tkip.txlock);
}
EXPORT_SYMBOL(ieee80211_get_tkip_p1k_iv);

void ieee80211_get_tkip_rx_p1k(struct ieee80211_key_conf *keyconf,
			       const u8 *ta, u32 iv32, u16 *p1k)
{
	const u8 *tk = &keyconf->key[NL80211_TKIP_DATA_OFFSET_ENCR_KEY];
	struct tkip_ctx ctx;

	tkip_mixing_phase1(tk, &ctx, ta, iv32);
	memcpy(p1k, ctx.p1k, sizeof(ctx.p1k));
}
EXPORT_SYMBOL(ieee80211_get_tkip_rx_p1k);

void ieee80211_get_tkip_p2k(struct ieee80211_key_conf *keyconf,
			    struct sk_buff *skb, u8 *p2k)
{
	struct ieee80211_key *key = (struct ieee80211_key *)
			container_of(keyconf, struct ieee80211_key, conf);
	const u8 *tk = &key->conf.key[NL80211_TKIP_DATA_OFFSET_ENCR_KEY];
	struct tkip_ctx *ctx = &key->u.tkip.tx;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	const u8 *data = (u8 *)hdr + ieee80211_hdrlen(hdr->frame_control);
	u32 iv32 = get_unaligned_le32(&data[4]);
	u16 iv16 = data[2] | (data[0] << 8);

	spin_lock(&key->u.tkip.txlock);
	ieee80211_compute_tkip_p1k(key, iv32);
	tkip_mixing_phase2(tk, ctx, iv16, p2k);
	spin_unlock(&key->u.tkip.txlock);
}
EXPORT_SYMBOL(ieee80211_get_tkip_p2k);

/*
 * Encrypt packet payload with TKIP using @key. @pos is a pointer to the
 * beginning of the buffer containing payload. This payload must include
 * the IV/Ext.IV and space for (taildroom) four octets for ICV.
 * @payload_len is the length of payload (_not_ including IV/ICV length).
 * @ta is the transmitter addresses.
 */
int ieee80211_tkip_encrypt_data(struct crypto_cipher *tfm,
				struct ieee80211_key *key,
				struct sk_buff *skb,
				u8 *payload, size_t payload_len)
{
	u8 rc4key[16];

	ieee80211_get_tkip_p2k(&key->conf, skb, rc4key);

	return ieee80211_wep_encrypt_data(tfm, rc4key, 16,
					  payload, payload_len);
}

/* Decrypt packet payload with TKIP using @key. @pos is a pointer to the
 * beginning of the buffer containing IEEE 802.11 header payload, i.e.,
 * including IV, Ext. IV, real data, Michael MIC, ICV. @payload_len is the
 * length of payload, including IV, Ext. IV, MIC, ICV.  */
int ieee80211_tkip_decrypt_data(struct crypto_cipher *tfm,
				struct ieee80211_key *key,
				u8 *payload, size_t payload_len, u8 *ta,
				u8 *ra, int only_iv, int queue,
				u32 *out_iv32, u16 *out_iv16)
{
	u32 iv32;
	u32 iv16;
	u8 rc4key[16], keyid, *pos = payload;
	int res;
	const u8 *tk = &key->conf.key[NL80211_TKIP_DATA_OFFSET_ENCR_KEY];

	if (payload_len < 12)
		return -1;

	iv16 = (pos[0] << 8) | pos[2];
	keyid = pos[3];
	iv32 = get_unaligned_le32(pos + 4);
	pos += 8;

	if (!(keyid & (1 << 5)))
		return TKIP_DECRYPT_NO_EXT_IV;

	if ((keyid >> 6) != key->conf.keyidx)
		return TKIP_DECRYPT_INVALID_KEYIDX;

	if (key->u.tkip.rx[queue].state != TKIP_STATE_NOT_INIT &&
	    (iv32 < key->u.tkip.rx[queue].iv32 ||
	     (iv32 == key->u.tkip.rx[queue].iv32 &&
	      iv16 <= key->u.tkip.rx[queue].iv16)))
		return TKIP_DECRYPT_REPLAY;

	if (only_iv) {
		res = TKIP_DECRYPT_OK;
		key->u.tkip.rx[queue].state = TKIP_STATE_PHASE1_HW_UPLOADED;
		goto done;
	}

	if (key->u.tkip.rx[queue].state == TKIP_STATE_NOT_INIT ||
	    key->u.tkip.rx[queue].iv32 != iv32) {
		/* IV16 wrapped around - perform TKIP phase 1 */
		tkip_mixing_phase1(tk, &key->u.tkip.rx[queue], ta, iv32);
	}
	if (key->local->ops->update_tkip_key &&
	    key->flags & KEY_FLAG_UPLOADED_TO_HARDWARE &&
	    key->u.tkip.rx[queue].state != TKIP_STATE_PHASE1_HW_UPLOADED) {
		struct ieee80211_sub_if_data *sdata = key->sdata;

		if (sdata->vif.type == NL80211_IFTYPE_AP_VLAN)
			sdata = container_of(key->sdata->bss,
					struct ieee80211_sub_if_data, u.ap);
		drv_update_tkip_key(key->local, sdata, &key->conf, key->sta,
				iv32, key->u.tkip.rx[queue].p1k);
		key->u.tkip.rx[queue].state = TKIP_STATE_PHASE1_HW_UPLOADED;
	}

	tkip_mixing_phase2(tk, &key->u.tkip.rx[queue], iv16, rc4key);

	res = ieee80211_wep_decrypt_data(tfm, rc4key, 16, pos, payload_len - 12);
 done:
	if (res == TKIP_DECRYPT_OK) {
		/*
		 * Record previously received IV, will be copied into the
		 * key information after MIC verification. It is possible
		 * that we don't catch replays of fragments but that's ok
		 * because the Michael MIC verication will then fail.
		 */
		*out_iv32 = iv32;
		*out_iv16 = iv16;
	}

	return res;
}
