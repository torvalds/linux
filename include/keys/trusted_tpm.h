/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __TRUSTED_TPM_H
#define __TRUSTED_TPM_H

#include <keys/trusted-type.h>

extern struct trusted_key_ops trusted_key_tpm_ops;

struct trusted_key_tpm {
	u32 keyhandle;
	unsigned char keyauth[TPM_DIGEST_SIZE];
	u32 blobauth_len;
	unsigned char blobauth[TPM_DIGEST_SIZE];
	u32 pcrinfo_len;
	unsigned char pcrinfo[MAX_PCRINFO_SIZE];
	int pcrlock;
	u32 hash;
	u32 policydigest_len;
	unsigned char policydigest[MAX_DIGEST_SIZE];
	u32 policyhandle;
};

int tpm2_seal_trusted(struct tpm_chip *chip,
		      struct trusted_key_payload *payload,
		      struct trusted_key_options *options);
int tpm2_unseal_trusted(struct tpm_chip *chip,
			struct trusted_key_payload *payload,
			struct trusted_key_options *options);

#endif
