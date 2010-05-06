/*
 * Copyright (C) 2005,2006,2007,2008 IBM Corporation
 *
 * Authors:
 * Serge Hallyn <serue@us.ibm.com>
 * Reiner Sailer <sailer@watson.ibm.com>
 * Mimi Zohar <zohar@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
 * File: ima_queue.c
 *       Implements queues that store template measurements and
 *       maintains aggregate over the stored measurements
 *       in the pre-configured TPM PCR (if available).
 *       The measurement list is append-only. No entry is
 *       ever removed or changed during the boot-cycle.
 */
#include <linux/module.h>
#include <linux/rculist.h>
#include <linux/slab.h>
#include "ima.h"

LIST_HEAD(ima_measurements);	/* list of all measurements */

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
static struct ima_queue_entry *ima_lookup_digest_entry(u8 *digest_value)
{
	struct ima_queue_entry *qe, *ret = NULL;
	unsigned int key;
	struct hlist_node *pos;
	int rc;

	key = ima_hash_key(digest_value);
	rcu_read_lock();
	hlist_for_each_entry_rcu(qe, pos, &ima_htable.queue[key], hnext) {
		rc = memcmp(qe->entry->digest, digest_value, IMA_DIGEST_SIZE);
		if (rc == 0) {
			ret = qe;
			break;
		}
	}
	rcu_read_unlock();
	return ret;
}

/* ima_add_template_entry helper function:
 * - Add template entry to measurement list and hash table.
 *
 * (Called with ima_extend_list_mutex held.)
 */
static int ima_add_digest_entry(struct ima_template_entry *entry)
{
	struct ima_queue_entry *qe;
	unsigned int key;

	qe = kmalloc(sizeof(*qe), GFP_KERNEL);
	if (qe == NULL) {
		pr_err("IMA: OUT OF MEMORY ERROR creating queue entry.\n");
		return -ENOMEM;
	}
	qe->entry = entry;

	INIT_LIST_HEAD(&qe->later);
	list_add_tail_rcu(&qe->later, &ima_measurements);

	atomic_long_inc(&ima_htable.len);
	key = ima_hash_key(entry->digest);
	hlist_add_head_rcu(&qe->hnext, &ima_htable.queue[key]);
	return 0;
}

static int ima_pcr_extend(const u8 *hash)
{
	int result = 0;

	if (!ima_used_chip)
		return result;

	result = tpm_pcr_extend(TPM_ANY_NUM, CONFIG_IMA_MEASURE_PCR_IDX, hash);
	if (result != 0)
		pr_err("IMA: Error Communicating to TPM chip\n");
	return result;
}

/* Add template entry to the measurement list and hash table,
 * and extend the pcr.
 */
int ima_add_template_entry(struct ima_template_entry *entry, int violation,
			   const char *op, struct inode *inode)
{
	u8 digest[IMA_DIGEST_SIZE];
	const char *audit_cause = "hash_added";
	int audit_info = 1;
	int result = 0;

	mutex_lock(&ima_extend_list_mutex);
	if (!violation) {
		memcpy(digest, entry->digest, sizeof digest);
		if (ima_lookup_digest_entry(digest)) {
			audit_cause = "hash_exists";
			goto out;
		}
	}

	result = ima_add_digest_entry(entry);
	if (result < 0) {
		audit_cause = "ENOMEM";
		audit_info = 0;
		goto out;
	}

	if (violation)		/* invalidate pcr */
		memset(digest, 0xff, sizeof digest);

	result = ima_pcr_extend(digest);
	if (result != 0) {
		audit_cause = "TPM error";
		audit_info = 0;
	}
out:
	mutex_unlock(&ima_extend_list_mutex);
	integrity_audit_msg(AUDIT_INTEGRITY_PCR, inode,
			    entry->template.file_name,
			    op, audit_cause, result, audit_info);
	return result;
}
