/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2023 Hannes Reinecke, SUSE Labs
 */

#ifndef _NVME_KEYRING_H
#define _NVME_KEYRING_H

#ifdef CONFIG_NVME_KEYRING

key_serial_t nvme_tls_psk_default(struct key *keyring,
		const char *hostnqn, const char *subnqn);

key_serial_t nvme_keyring_id(void);
int nvme_keyring_init(void);
void nvme_keyring_exit(void);

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
static inline int nvme_keyring_init(void)
{
	return 0;
}
static inline void nvme_keyring_exit(void) {}

#endif /* !CONFIG_NVME_KEYRING */
#endif /* _NVME_KEYRING_H */
