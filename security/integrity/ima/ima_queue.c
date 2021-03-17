// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2005,2006,2007,2008 IBM Corporation
 *
 * Authors:
 * Serge Hallyn <serue@us.ibm.com>
 * Reiner Sailer <sailer@watson.ibm.com>
 * Mimi Zohar <zohar@us.ibm.com>
 *
 * File: ima_queue.c
 *       Implements queues that store template measurements and
 *       maintains aggregate over the stored measurements
 *       in the pre-configured TPM PCR (if available).
 *       The measurement list is append-only. No entry is
 *       ever removed or changed during the boot-cycle.
 */

#include <linux/rculist.h>
#include <linux/slab.h>
#include "ima.h"

#define AUDIT_CAUSE_LEN_MAX 32

/* pre-allocated array of tpm_digest structures to extend a PCR */
static struct tpm_digest *digests;

LIST_HEAD(ima_measurements);	/* list of all measurements */
#ifdef CONFIG_IMA_KEXEC
static unsigned long binary_runtime_size;
#else
static unsigned long binary_runtime_size = ULONG_MAX;
#endif

/* key: inode (before secure-hashing a file) */
struct ima_h_table ima_htable = {
	.len = ATOMIC_LONG_INIT(0),
	.violations = ATOMIC_LONG_INIT(0),
	.queue[0 ... IMA_MEASURE_HTABLE_SIZE - 1] = HLIST_HEAD_INIT
};

/* mutex protects atomicity of extending measurement list
 * and extending the TPM PCR aggregate. Since tpm_extend can take
 * long (and the tpm driver uses a mutex), we can't use the spinlock.
 */
static DEFINE_MUTEX(ima_extend_list_mutex);

/* lookup up the digest value in the hash table, and return the entry */
static struct ima_queue_entry *ima_lookup_digest_entry(u8 *digest_value,
						       int pcr)
{
	struct ima_queue_entry *qe, *ret = NULL;
	unsigned int key;
	int rc;

	key = ima_hash_key(digest_value);
	rcu_read_lock();
	hlist_for_each_entry_rcu(qe, &ima_htable.queue[key], hnext) {
		rc = memcmp(qe->entry->digests[ima_hash_algo_idx].digest,
			    digest_value, hash_digest_size[ima_hash_algo]);
		if ((rc == 0) && (qe->entry->pcr == pcr)) {
			ret = qe;
			break;
		}
	}
	rcu_read_unlock();
	return ret;
}

/*
 * Calculate the memory required for serializing a single
 * binary_runtime_measurement list entry, which contains a
 * couple of variable length fields (e.g template name and data).
 */
static int get_binary_runtime_size(struct ima_template_entry *entry)
{
	int size = 0;

	size += sizeof(u32);	/* pcr */
	size += TPM_DIGEST_SIZE;
	size += sizeof(int);	/* template name size field */
	size += strlen(entry->template_desc->name);
	size += sizeof(entry->template_data_len);
	size += entry->template_data_len;
	return size;
}

/* ima_add_template_entry helper function:
 * - Add template entry to the measurement list and hash table, for
 *   all entries except those carried across kexec.
 *
 * (Called with ima_extend_list_mutex held.)
 */
static int ima_add_digest_entry(struct ima_template_entry *entry,
				bool update_htable)
{
	struct ima_queue_entry *qe;
	unsigned int key;

	qe = kmalloc(sizeof(*qe), GFP_KERNEL);
	if (qe == NULL) {
		pr_err("OUT OF MEMORY ERROR creating queue entry\n");
		return -ENOMEM;
	}
	qe->entry = entry;

	INIT_LIST_HEAD(&qe->later);
	list_add_tail_rcu(&qe->later, &ima_measurements);

	atomic_long_inc(&ima_htable.len);
	if (update_htable) {
		key = ima_hash_key(entry->digests[ima_hash_algo_idx].digest);
		hlist_add_head_rcu(&qe->hnext, &ima_htable.queue[key]);
	}

	if (binary_runtime_size != ULONG_MAX) {
		int size;

		size = get_binary_runtime_size(entry);
		binary_runtime_size = (binary_runtime_size < ULONG_MAX - size) ?
		     binary_runtime_size + size : ULONG_MAX;
	}
	return 0;
}

/*
 * Return the amount of memory required for serializing the
 * entire binary_runtime_measurement list, including the ima_kexec_hdr
 * structure.
 */
unsigned long ima_get_binary_runtime_size(void)
{
	if (binary_runtime_size >= (ULONG_MAX - sizeof(struct ima_kexec_hdr)))
		return ULONG_MAX;
	else
		return binary_runtime_size + sizeof(struct ima_kexec_hdr);
}

static int ima_pcr_extend(struct tpm_digest *digests_arg, int pcr)
{
	int result = 0;

	if (!ima_tpm_chip)
		return result;

	result = tpm_pcr_extend(ima_tpm_chip, pcr, digests_arg);
	if (result != 0)
		pr_err("Error Communicating to TPM chip, result: %d\n", result);
	return result;
}

/*
 * Add template entry to the measurement list and hash table, and
 * extend the pcr.
 *
 * On systems which support carrying the IMA measurement list across
 * kexec, maintain the total memory size required for serializing the
 * binary_runtime_measurements.
 */
int ima_add_template_entry(struct ima_template_entry *entry, int violation,
			   const char *op, struct inode *inode,
			   const unsigned char *filename)
{
	u8 *digest = entry->digests[ima_hash_algo_idx].digest;
	struct tpm_digest *digests_arg = entry->digests;
	const char *audit_cause = "hash_added";
	char tpm_audit_cause[AUDIT_CAUSE_LEN_MAX];
	int audit_info = 1;
	int result = 0, tpmresult = 0;

	mutex_lock(&ima_extend_list_mutex);
	if (!violation) {
		if (ima_lookup_digest_entry(digest, entry->pcr)) {
			audit_cause = "hash_exists";
			result = -EEXIST;
			goto out;
		}
	}

	result = ima_add_digest_entry(entry, 1);
	if (result < 0) {
		audit_cause = "ENOMEM";
		audit_info = 0;
		goto out;
	}

	if (violation)		/* invalidate pcr */
		digests_arg = digests;

	tpmresult = ima_pcr_extend(digests_arg, entry->pcr);
	if (tpmresult != 0) {
		snprintf(tpm_audit_cause, AUDIT_CAUSE_LEN_MAX, "TPM_error(%d)",
			 tpmresult);
		audit_cause = tpm_audit_cause;
		audit_info = 0;
	}
out:
	mutex_unlock(&ima_extend_list_mutex);
	integrity_audit_msg(AUDIT_INTEGRITY_PCR, inode, filename,
			    op, audit_cause, result, audit_info);
	return result;
}

int ima_restore_measurement_entry(struct ima_template_entry *entry)
{
	int result = 0;

	mutex_lock(&ima_extend_list_mutex);
	result = ima_add_digest_entry(entry, 0);
	mutex_unlock(&ima_extend_list_mutex);
	return result;
}

int __init ima_init_digests(void)
{
	u16 digest_size;
	u16 crypto_id;
	int i;

	if (!ima_tpm_chip)
		return 0;

	digests = kcalloc(ima_tpm_chip->nr_allocated_banks, sizeof(*digests),
			  GFP_NOFS);
	if (!digests)
		return -ENOMEM;

	for (i = 0; i < ima_tpm_chip->nr_allocated_banks; i++) {
		digests[i].alg_id = ima_tpm_chip->allocated_banks[i].alg_id;
		digest_size = ima_tpm_chip->allocated_banks[i].digest_size;
		crypto_id = ima_tpm_chip->allocated_banks[i].crypto_id;

		/* for unmapped TPM algorithms digest is still a padded SHA1 */
		if (crypto_id == HASH_ALGO__LAST)
			digest_size = SHA1_DIGEST_SIZE;

		memset(digests[i].digest, 0xff, digest_size);
	}

	return 0;
}
