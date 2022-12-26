#ifndef _CRYPTO_GCM_H
#define _CRYPTO_GCM_H

#include <linux/errno.h>

#include <crypto/aes.h>
#include <crypto/gf128mul.h>

#define GCM_AES_IV_SIZE 12
#define GCM_RFC4106_IV_SIZE 8
#define GCM_RFC4543_IV_SIZE 8

/*
 * validate authentication tag for GCM
 */
static inline int crypto_gcm_check_authsize(unsigned int authsize)
{
	switch (authsize) {
	case 4:
	case 8:
	case 12:
	case 13:
	case 14:
	case 15:
	case 16:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/*
 * validate authentication tag for RFC4106
 */
static inline int crypto_rfc4106_check_authsize(unsigned int authsize)
{
	switch (authsize) {
	case 8:
	case 12:
	case 16:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/*
 * validate assoclen for RFC4106/RFC4543
 */
static inline int crypto_ipsec_check_assoclen(unsigned int assoclen)
{
	switch (assoclen) {
	case 16:
	case 20:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

struct aesgcm_ctx {
	be128			ghash_key;
	struct crypto_aes_ctx	aes_ctx;
	unsigned int		authsize;
};

int aesgcm_expandkey(struct aesgcm_ctx *ctx, const u8 *key,
		     unsigned int keysize, unsigned int authsize);

void aesgcm_encrypt(const struct aesgcm_ctx *ctx, u8 *dst, const u8 *src,
		    int crypt_len, const u8 *assoc, int assoc_len,
		    const u8 iv[GCM_AES_IV_SIZE], u8 *authtag);

bool __must_check aesgcm_decrypt(const struct aesgcm_ctx *ctx, u8 *dst,
				 const u8 *src, int crypt_len, const u8 *assoc,
				 int assoc_len, const u8 iv[GCM_AES_IV_SIZE],
				 const u8 *authtag);

#endif
