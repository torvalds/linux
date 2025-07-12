/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _FS_CEPH_CRYPTO_H
#define _FS_CEPH_CRYPTO_H

#include <crypto/sha2.h>
#include <linux/ceph/types.h>
#include <linux/ceph/buffer.h>

#define CEPH_MAX_KEY_LEN		32
#define CEPH_MAX_CON_SECRET_LEN		64

/*
 * cryptographic secret
 */
struct ceph_crypto_key {
	int type;
	struct ceph_timespec created;
	int len;
	void *key;

	union {
		struct crypto_sync_skcipher *aes_tfm;
		struct {
			struct hmac_sha256_key hmac_key;
			const struct krb5_enctype *krb5_type;
			struct crypto_aead *krb5_tfms[3];
		};
	};
};

int ceph_crypto_key_prepare(struct ceph_crypto_key *key,
			    const u32 *key_usages, int key_usage_cnt);
int ceph_crypto_key_clone(struct ceph_crypto_key *dst,
			  const struct ceph_crypto_key *src);
int ceph_crypto_key_decode(struct ceph_crypto_key *key, void **p, void *end);
int ceph_crypto_key_unarmor(struct ceph_crypto_key *key, const char *in);
void ceph_crypto_key_destroy(struct ceph_crypto_key *key);

/* crypto.c */
int ceph_crypt(const struct ceph_crypto_key *key, int usage_slot, bool encrypt,
	       void *buf, int buf_len, int in_len, int *pout_len);
int ceph_crypt_data_offset(const struct ceph_crypto_key *key);
int ceph_crypt_buflen(const struct ceph_crypto_key *key, int data_len);
void ceph_hmac_sha256(const struct ceph_crypto_key *key, const void *buf,
		      int buf_len, u8 hmac[SHA256_DIGEST_SIZE]);
int ceph_crypto_init(void);
void ceph_crypto_shutdown(void);

/* armor.c */
int ceph_armor(char *dst, const char *src, const char *end);
int ceph_unarmor(char *dst, const char *src, const char *end);

#endif
