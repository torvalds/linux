/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 Hannes Reinecke, SUSE Software Solutions
 */

#ifndef _NVME_AUTH_H
#define _NVME_AUTH_H

#include <crypto/kpp.h>
#include <crypto/sha2.h>

struct nvme_dhchap_key {
	size_t len;
	u8 hash;
	u8 key[] __counted_by(len);
};

u32 nvme_auth_get_seqnum(void);
const char *nvme_auth_dhgroup_name(u8 dhgroup_id);
const char *nvme_auth_dhgroup_kpp(u8 dhgroup_id);
u8 nvme_auth_dhgroup_id(const char *dhgroup_name);

const char *nvme_auth_hmac_name(u8 hmac_id);
size_t nvme_auth_hmac_hash_len(u8 hmac_id);
u8 nvme_auth_hmac_id(const char *hmac_name);
struct nvme_auth_hmac_ctx {
	u8 hmac_id;
	union {
		struct hmac_sha256_ctx sha256;
		struct hmac_sha384_ctx sha384;
		struct hmac_sha512_ctx sha512;
	};
};
int nvme_auth_hmac_init(struct nvme_auth_hmac_ctx *hmac, u8 hmac_id,
			const u8 *key, size_t key_len);
void nvme_auth_hmac_update(struct nvme_auth_hmac_ctx *hmac, const u8 *data,
			   size_t data_len);
void nvme_auth_hmac_final(struct nvme_auth_hmac_ctx *hmac, u8 *out);

u32 nvme_auth_key_struct_size(u32 key_len);
struct nvme_dhchap_key *nvme_auth_extract_key(const char *secret, u8 key_hash);
void nvme_auth_free_key(struct nvme_dhchap_key *key);
struct nvme_dhchap_key *nvme_auth_alloc_key(u32 len, u8 hash);
struct nvme_dhchap_key *nvme_auth_transform_key(
		const struct nvme_dhchap_key *key, const char *nqn);
int nvme_auth_parse_key(const char *secret, struct nvme_dhchap_key **ret_key);
int nvme_auth_augmented_challenge(u8 hmac_id, const u8 *skey, size_t skey_len,
				  const u8 *challenge, u8 *aug, size_t hlen);
int nvme_auth_gen_privkey(struct crypto_kpp *dh_tfm, u8 dh_gid);
int nvme_auth_gen_pubkey(struct crypto_kpp *dh_tfm,
			 u8 *host_key, size_t host_key_len);
int nvme_auth_gen_session_key(struct crypto_kpp *dh_tfm,
			      const u8 *public_key, size_t public_key_len,
			      u8 *sess_key, size_t sess_key_len, u8 hash_id);
int nvme_auth_generate_psk(u8 hmac_id, const u8 *skey, size_t skey_len,
			   const u8 *c1, const u8 *c2, size_t hash_len,
			   u8 **ret_psk, size_t *ret_len);
int nvme_auth_generate_digest(u8 hmac_id, const u8 *psk, size_t psk_len,
			      const char *subsysnqn, const char *hostnqn,
			      char **ret_digest);
int nvme_auth_derive_tls_psk(int hmac_id, const u8 *psk, size_t psk_len,
			     const char *psk_digest, u8 **ret_psk);

#endif /* _NVME_AUTH_H */
