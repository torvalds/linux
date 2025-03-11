/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _FS_CEPH_CRYPTO_H
#define _FS_CEPH_CRYPTO_H

#include <linux/ceph/types.h>
#include <linux/ceph/buffer.h>

#define CEPH_KEY_LEN			16
#define CEPH_MAX_CON_SECRET_LEN		64

/*
 * cryptographic secret
 */
struct ceph_crypto_key {
	int type;
	struct ceph_timespec created;
	int len;
	void *key;
	struct crypto_sync_skcipher *tfm;
};

int ceph_crypto_key_clone(struct ceph_crypto_key *dst,
			  const struct ceph_crypto_key *src);
int ceph_crypto_key_decode(struct ceph_crypto_key *key, void **p, void *end);
int ceph_crypto_key_unarmor(struct ceph_crypto_key *key, const char *in);
void ceph_crypto_key_destroy(struct ceph_crypto_key *key);

/* crypto.c */
int ceph_crypt(const struct ceph_crypto_key *key, bool encrypt,
	       void *buf, int buf_len, int in_len, int *pout_len);
int ceph_crypto_init(void);
void ceph_crypto_shutdown(void);

/* armor.c */
int ceph_armor(char *dst, const char *src, const char *end);
int ceph_unarmor(char *dst, const char *src, const char *end);

#endif
