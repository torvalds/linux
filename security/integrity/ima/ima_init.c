// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2005,2006,2007,2008 IBM Corporation
 *
 * Authors:
 * Reiner Sailer      <sailer@watson.ibm.com>
 * Leendert van Doorn <leendert@watson.ibm.com>
 * Mimi Zohar         <zohar@us.ibm.com>
 *
 * File: ima_init.c
 *             initialization and cleanup functions
 */

#include <linux/init.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/ima.h>
#include <generated/utsrelease.h>

#include "ima.h"

/* name for boot aggregate entry */
const char boot_aggregate_name[] = "boot_aggregate";
struct tpm_chip *ima_tpm_chip;

/* Add the boot aggregate to the IMA measurement list and extend
 * the PCR register.
 *
 * Calculate the boot aggregate, a hash over tpm registers 0-7,
 * assuming a TPM chip exists, and zeroes if the TPM chip does not
 * exist.  Add the boot aggregate measurement to the measurement
 * list and extend the PCR register.
 *
 * If a tpm chip does not exist, indicate the core root of trust is
 * not hardware based by invalidating the aggregate PCR value.
 * (The aggregate PCR value is invalidated by adding one value to
 * the measurement list and extending the aggregate PCR value with
 * a different value.) Violations add a zero entry to the measurement
 * list and extend the aggregate PCR value with ff...ff's.
 */
static int __init ima_add_boot_aggregate(void)
{
	static const char op[] = "add_boot_aggregate";
	const char *audit_cause = "ENOMEM";
	struct ima_template_entry *entry;
	struct ima_iint_cache tmp_iint, *iint = &tmp_iint;
	struct ima_event_data event_data = { .iint = iint,
					     .filename = boot_aggregate_name };
	struct ima_max_digest_data hash;
	struct ima_digest_data *hash_hdr = container_of(&hash.hdr,
						struct ima_digest_data, hdr);
	int result = -ENOMEM;
	int violation = 0;

	memset(iint, 0, sizeof(*iint));
	memset(&hash, 0, sizeof(hash));
	iint->ima_hash = hash_hdr;
	iint->ima_hash->algo = ima_hash_algo;
	iint->ima_hash->length = hash_digest_size[ima_hash_algo];

	/*
	 * With TPM 2.0 hash agility, TPM chips could support multiple TPM
	 * PCR banks, allowing firmware to configure and enable different
	 * banks.  The SHA1 bank is not necessarily enabled.
	 *
	 * Use the same hash algorithm for reading the TPM PCRs as for
	 * calculating the boot aggregate digest.  Preference is given to
	 * the configured IMA default hash algorithm.  Otherwise, use the
	 * TCG required banks - SHA256 for TPM 2.0, SHA1 for TPM 1.2.
	 * Ultimately select SHA1 also for TPM 2.0 if the SHA256 PCR bank
	 * is not found.
	 */
	if (ima_tpm_chip) {
		result = ima_calc_boot_aggregate(hash_hdr);
		if (result < 0) {
			audit_cause = "hashing_error";
			goto err_out;
		}
	}

	result = ima_alloc_init_template(&event_data, &entry, NULL);
	if (result < 0) {
		audit_cause = "alloc_entry";
		goto err_out;
	}

	result = ima_store_template(entry, violation, NULL,
				    boot_aggregate_name,
				    CONFIG_IMA_MEASURE_PCR_IDX);
	if (result < 0) {
		ima_free_template_entry(entry);
		audit_cause = "store_entry";
		goto err_out;
	}
	return 0;
err_out:
	integrity_audit_msg(AUDIT_INTEGRITY_PCR, NULL, boot_aggregate_name, op,
			    audit_cause, result, 0);
	return result;
}

#ifdef CONFIG_IMA_LOAD_X509
void __init ima_load_x509(void)
{
	int unset_flags = ima_policy_flag & IMA_APPRAISE;

	ima_policy_flag &= ~unset_flags;
	integrity_load_x509(INTEGRITY_KEYRING_IMA, CONFIG_IMA_X509_PATH);

	/* load also EVM key to avoid appraisal */
	evm_load_x509();

	ima_policy_flag |= unset_flags;
}
#endif

int __init ima_init(void)
{
	int rc;

	ima_tpm_chip = tpm_default_chip();
	if (!ima_tpm_chip)
		pr_info("No TPM chip found, activating TPM-bypass!\n");

	rc = integrity_init_keyring(INTEGRITY_KEYRING_IMA);
	if (rc)
		return rc;

	rc = ima_init_crypto();
	if (rc)
		return rc;
	rc = ima_init_template();
	if (rc != 0)
		return rc;

	/* It can be called before ima_init_digests(), it does not use TPM. */
	ima_load_kexec_buffer();

	rc = ima_init_digests();
	if (rc != 0)
		return rc;
	rc = ima_add_boot_aggregate();	/* boot aggregate must be first entry */
	if (rc != 0)
		return rc;

	ima_init_policy();

	rc = ima_fs_init();
	if (rc != 0)
		return rc;

	ima_init_key_queue();

	ima_init_reboot_notifier();

	ima_measure_critical_data("kernel_info", "kernel_version",
				  UTS_RELEASE, strlen(UTS_RELEASE), false,
				  NULL, 0);

	return rc;
}
