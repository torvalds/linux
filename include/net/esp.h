#ifndef _NET_ESP_H
#define _NET_ESP_H

#include <linux/crypto.h>
#include <net/xfrm.h>
#include <asm/scatterlist.h>

#define ESP_NUM_FAST_SG		4

struct esp_data
{
	struct scatterlist		sgbuf[ESP_NUM_FAST_SG];

	/* Confidentiality */
	struct {
		u8			*key;		/* Key */
		int			key_len;	/* Key length */
		int			padlen;		/* 0..255 */
		/* ivlen is offset from enc_data, where encrypted data start.
		 * It is logically different of crypto_tfm_alg_ivsize(tfm).
		 * We assume that it is either zero (no ivec), or
		 * >= crypto_tfm_alg_ivsize(tfm). */
		int			ivlen;
		int			ivinitted;
		u8			*ivec;		/* ivec buffer */
		struct crypto_blkcipher	*tfm;		/* crypto handle */
	} conf;

	/* Integrity. It is active when icv_full_len != 0 */
	struct {
		u8			*key;		/* Key */
		int			key_len;	/* Length of the key */
		u8			*work_icv;
		int			icv_full_len;
		int			icv_trunc_len;
		void			(*icv)(struct esp_data*,
		                               struct sk_buff *skb,
		                               int offset, int len, u8 *icv);
		struct crypto_hash	*tfm;
	} auth;
};

extern void *pskb_put(struct sk_buff *skb, struct sk_buff *tail, int len);

static inline int esp_mac_digest(struct esp_data *esp, struct sk_buff *skb,
				 int offset, int len)
{
	struct hash_desc desc;
	int err;

	desc.tfm = esp->auth.tfm;
	desc.flags = 0;

	err = crypto_hash_init(&desc);
	if (unlikely(err))
		return err;
	err = skb_icv_walk(skb, &desc, offset, len, crypto_hash_update);
	if (unlikely(err))
		return err;
	return crypto_hash_final(&desc, esp->auth.work_icv);
}

#endif
