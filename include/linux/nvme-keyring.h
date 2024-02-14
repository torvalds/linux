/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2023 Hannes Reinecke, SUSE Labs
 */

#ifndef _NVME_KEYRING_H
#define _NVME_KEYRING_H

#if IS_ENABLED(CONFIG_NVME_KEYRING)

key_serial_t nvme_tls_psk_default(struct key *keyring,
		const char *hostnqn, const char *subnqn);

key_serial_t nvme_keyring_id(void);

#else

static inline key_serial_t nvme_tls_psk_default(struct key *keyring,
		const char *hostnqn, const char *subnqn)
{
	return 0;
}
static inline key_serial_t nvme_keyring_id(void)
{
	return 0;
}
#endif /* !CONFIG_NVME_KEYRING */
#endif /* _NVME_KEYRING_H */
