/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __TRUSTED_TPM_H
#define __TRUSTED_TPM_H

#include <keys/trusted-type.h>
#include <linux/tpm_command.h>

extern struct trusted_key_ops trusted_key_tpm_ops;

int tpm2_seal_trusted(struct tpm_chip *chip,
		      struct trusted_key_payload *payload,
		      struct trusted_key_options *options);
int tpm2_unseal_trusted(struct tpm_chip *chip,
			struct trusted_key_payload *payload,
			struct trusted_key_options *options);

#endif
