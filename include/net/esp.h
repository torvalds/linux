#ifndef _NET_ESP_H
#define _NET_ESP_H

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
		u8			*ivec;		/* ivec buffer */
		/* ivlen is offset from enc_data, where encrypted data start.
		 * It is logically different of crypto_tfm_alg_ivsize(tfm).
		 * We assume that it is either zero (no ivec), or
		 * >= crypto_tfm_alg_ivsize(tfm). */
		int			ivlen;
		int			padlen;		/* 0..255 */
		struct crypto_tfm	*tfm;		/* crypto handle */
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
		struct crypto_tfm	*tfm;
	} auth;
};

extern int skb_to_sgvec(struct sk_buff *skb, struct scatterlist *sg, int offset, int len);
extern int skb_cow_data(struct sk_buff *skb, int tailbits, struct sk_buff **trailer);
extern void *pskb_put(struct sk_buff *skb, struct sk_buff *tail, int len);

static inline void
esp_hmac_digest(struct esp_data *esp, struct sk_buff *skb, int offset,
                int len, u8 *auth_data)
{
	struct crypto_tfm *tfm = esp->auth.tfm;
	char *icv = esp->auth.work_icv;

	memset(auth_data, 0, esp->auth.icv_trunc_len);
	crypto_hmac_init(tfm, esp->auth.key, &esp->auth.key_len);
	skb_icv_walk(skb, tfm, offset, len, crypto_hmac_update);
	crypto_hmac_final(tfm, esp->auth.key, &esp->auth.key_len, icv);
	memcpy(auth_data, icv, esp->auth.icv_trunc_len);
}

#endif
