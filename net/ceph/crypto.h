#ifndef _FS_CEPH_CRYPTO_H
#define _FS_CEPH_CRYPTO_H

#include <linux/ceph/types.h>
#include <linux/ceph/buffer.h>

/*
 * cryptographic secret
 */
struct ceph_crypto_key {
	int type;
	struct ceph_timespec created;
	int len;
	void *key;
};

static inline void ceph_crypto_key_destroy(struct ceph_crypto_key *key)
{
	if (key)
		kfree(key->key);
}

int ceph_crypto_key_clone(struct ceph_crypto_key *dst,
			  const struct ceph_crypto_key *src);
int ceph_crypto_key_encode(struct ceph_crypto_key *key, void **p, void *end);
int ceph_crypto_key_decode(struct ceph_crypto_key *key, void **p, void *end);
int ceph_crypto_key_unarmor(struct ceph_crypto_key *key, const char *in);

/* crypto.c */
int ceph_decrypt(struct ceph_crypto_key *secret,
		 void *dst, size_t *dst_len,
		 const void *src, size_t src_len);
int ceph_encrypt(struct ceph_crypto_key *secret,
		 void *dst, size_t *dst_len,
		 const void *src, size_t src_len);
int ceph_decrypt2(struct ceph_crypto_key *secret,
		  void *dst1, size_t *dst1_len,
		  void *dst2, size_t *dst2_len,
		  const void *src, size_t src_len);
int ceph_encrypt2(struct ceph_crypto_key *secret,
		  void *dst, size_t *dst_len,
		  const void *src1, size_t src1_len,
		  const void *src2, size_t src2_len);
int ceph_crypto_init(void);
void ceph_crypto_shutdown(void);

/* armor.c */
int ceph_armor(char *dst, const char *src, const char *end);
int ceph_unarmor(char *dst, const char *src, const char *end);

#endif
