/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (C) 2019 Samsung Electronics Co., Ltd.
 */

#ifndef __CRYPTO_CTX_H__
#define __CRYPTO_CTX_H__

#include <crypto/hash.h>
#include <crypto/aead.h>

enum {
	CRYPTO_SHASH_HMACMD5	= 0,
	CRYPTO_SHASH_HMACSHA256,
	CRYPTO_SHASH_CMACAES,
	CRYPTO_SHASH_SHA512,
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

#define CRYPTO_HMACMD5(c)	((c)->desc[CRYPTO_SHASH_HMACMD5])
#define CRYPTO_HMACSHA256(c)	((c)->desc[CRYPTO_SHASH_HMACSHA256])
#define CRYPTO_CMACAES(c)	((c)->desc[CRYPTO_SHASH_CMACAES])
#define CRYPTO_SHA512(c)	((c)->desc[CRYPTO_SHASH_SHA512])

#define CRYPTO_HMACMD5_TFM(c)	((c)->desc[CRYPTO_SHASH_HMACMD5]->tfm)
#define CRYPTO_HMACSHA256_TFM(c)\
				((c)->desc[CRYPTO_SHASH_HMACSHA256]->tfm)
#define CRYPTO_CMACAES_TFM(c)	((c)->desc[CRYPTO_SHASH_CMACAES]->tfm)
#define CRYPTO_SHA512_TFM(c)	((c)->desc[CRYPTO_SHASH_SHA512]->tfm)

#define CRYPTO_GCM(c)		((c)->ccmaes[CRYPTO_AEAD_AES_GCM])
#define CRYPTO_CCM(c)		((c)->ccmaes[CRYPTO_AEAD_AES_CCM])

void ksmbd_release_crypto_ctx(struct ksmbd_crypto_ctx *ctx);
struct ksmbd_crypto_ctx *ksmbd_crypto_ctx_find_hmacmd5(void);
struct ksmbd_crypto_ctx *ksmbd_crypto_ctx_find_hmacsha256(void);
struct ksmbd_crypto_ctx *ksmbd_crypto_ctx_find_cmacaes(void);
struct ksmbd_crypto_ctx *ksmbd_crypto_ctx_find_sha512(void);
struct ksmbd_crypto_ctx *ksmbd_crypto_ctx_find_gcm(void);
struct ksmbd_crypto_ctx *ksmbd_crypto_ctx_find_ccm(void);
void ksmbd_crypto_destroy(void);
int ksmbd_crypto_create(void);

#endif /* __CRYPTO_CTX_H__ */
