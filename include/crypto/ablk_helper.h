/*
 * Shared async block cipher helpers
 */

#ifndef _CRYPTO_ABLK_HELPER_H
#define _CRYPTO_ABLK_HELPER_H

#include <linux/crypto.h>
#include <linux/kernel.h>
#include <crypto/cryptd.h>

struct async_helper_ctx {
	struct cryptd_ablkcipher *cryptd_tfm;
};

extern int ablk_set_key(struct crypto_ablkcipher *tfm, const u8 *key,
			unsigned int key_len);

extern int __ablk_encrypt(struct ablkcipher_request *req);

extern int ablk_encrypt(struct ablkcipher_request *req);

extern int ablk_decrypt(struct ablkcipher_request *req);

extern void ablk_exit(struct crypto_tfm *tfm);

extern int ablk_init_common(struct crypto_tfm *tfm, const char *drv_name);

extern int ablk_init(struct crypto_tfm *tfm);

#endif /* _CRYPTO_ABLK_HELPER_H */
