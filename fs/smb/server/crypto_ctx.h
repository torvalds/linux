/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (C) 2019 Samsung Electronics Co., Ltd.
 */

#ifndef __CRYPTO_CTX_H__
#define __CRYPTO_CTX_H__

#include <crypto/hash.h>
#include <crypto/aead.h>

enum {
	CRYPTO_SHASH_CMACAES	= 0,
	CRYPTO_SHASH_MAX,
};

enum {
	CRYPTO_AEAD_AES_GCM = 16,
	CRYPTO_AEAD_AES_CCM,
	CRYPTO_AEAD_MAX,
};

enum {
	CRYPTO_BLK_ECBDES	= 32,
	CRYPTO_BLK_MAX,
};

struct ksmbd_crypto_ctx {
	struct list_head		list;

	struct shash_desc		*desc[CRYPTO_SHASH_MAX];
	struct crypto_aead		*ccmaes[CRYPTO_AEAD_MAX];
};

#define CRYPTO_CMACAES(c)	((c)->desc[CRYPTO_SHASH_CMACAES])

#define CRYPTO_CMACAES_TFM(c)	((c)->desc[CRYPTO_SHASH_CMACAES]->tfm)

#define CRYPTO_GCM(c)		((c)->ccmaes[CRYPTO_AEAD_AES_GCM])
#define CRYPTO_CCM(c)		((c)->ccmaes[CRYPTO_AEAD_AES_CCM])

void ksmbd_release_crypto_ctx(struct ksmbd_crypto_ctx *ctx);
struct ksmbd_crypto_ctx *ksmbd_crypto_ctx_find_cmacaes(void);
struct ksmbd_crypto_ctx *ksmbd_crypto_ctx_find_gcm(void);
struct ksmbd_crypto_ctx *ksmbd_crypto_ctx_find_ccm(void);
void ksmbd_crypto_destroy(void);
int ksmbd_crypto_create(void);

#endif /* __CRYPTO_CTX_H__ */
