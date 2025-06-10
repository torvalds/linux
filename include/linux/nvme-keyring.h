/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2023 Hannes Reinecke, SUSE Labs
 */

#ifndef _NVME_KEYRING_H
#define _NVME_KEYRING_H

#include <linux/key.h>

#if IS_ENABLED(CONFIG_NVME_KEYRING)

struct key *nvme_tls_psk_refresh(struct key *keyring,
		const char *hostnqn, const char *subnqn, u8 hmac_id,
		u8 *data, size_t data_len, const char *digest);
key_serial_t nvme_tls_psk_default(struct key *keyring,
		const char *hostnqn, const char *subnqn);

key_serial_t nvme_keyring_id(void);
struct key *nvme_tls_key_lookup(key_serial_t key_id);
#else
static inline struct key *nvme_tls_psk_refresh(struct key *keyring,
		const char *hostnqn, char *subnqn, u8 hmac_id,
		u8 *data, size_t data_len, const char *digest)
{
	return ERR_PTR(-ENOTSUPP);
}
static inline key_serial_t nvme_tls_psk_default(struct key *keyring,
		const char *hostnqn, const char *subnqn)
{
	return 0;
}
static inline key_serial_t nvme_keyring_id(void)
{
	return 0;
}
static inline struct key *nvme_tls_key_lookup(key_serial_t key_id)
{
	return ERR_PTR(-ENOTSUPP);
}
#endif /* !CONFIG_NVME_KEYRING */
#endif /* _NVME_KEYRING_H */
