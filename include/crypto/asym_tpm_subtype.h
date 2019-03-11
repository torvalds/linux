// SPDX-License-Identifier: GPL-2.0
#ifndef _LINUX_ASYM_TPM_SUBTYPE_H
#define _LINUX_ASYM_TPM_SUBTYPE_H

#include <linux/keyctl.h>

struct tpm_key {
	void *blob;
	u32 blob_len;
	uint16_t key_len; /* Size in bits of the key */
	const void *pub_key; /* pointer inside blob to the public key bytes */
	uint16_t pub_key_len; /* length of the public key */
};

struct tpm_key *tpm_key_create(const void *blob, uint32_t blob_len);

extern struct asymmetric_key_subtype asym_tpm_subtype;

#endif /* _LINUX_ASYM_TPM_SUBTYPE_H */
