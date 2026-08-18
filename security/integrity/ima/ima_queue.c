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
#include <linux/reboot.h>
#include <linux/slab.h>
#include "ima.h"

#define AUDIT_CAUSE_LEN_MAX 32

bool ima_flush_htable;

static int __init ima_flush_htable_setup(char *str)
{
	if (IS_ENABLED(CONFIG_IMA_DISABLE_HTABLE)) {
		pr_warn("Hash table not enabled, ignoring request to flush\n");
		return 1;
	}

	ima_flush_htable = true;
	return 1;
}
__setup("ima_flush_htable", ima_flush_htable_setup);

/* pre-allocated array of tpm_digest structures to extend a PCR */
static struct tpm_digest *digests;

LIST_HEAD(ima_measurements);	/* list of all measurements */
LIST_HEAD(ima_measurements_staged); /* list of staged measurements */
#ifdef CONFIG_IMA_KEXEC
static unsigned long binary_runtime_size[BINARY__LAST];
#else
static unsigned long binary_runtime_size[BINARY__LAST] = {
	[0 ... BINARY__LAST - 1] = ULONG_MAX
};
#endif

atomic_long_t ima_num_records[BINARY__LAST] = {
	[0 ... BINARY__LAST - 1] = ATOMIC_LONG_INIT(0)
};
atomic_long_t ima_num_violations = ATOMIC_LONG_INIT(0);

/* key: inode (before secure-hashing a file) */
struct hlist_head __rcu *ima_htable;

/* mutex protects atomicity of extending and staging measurement list
 * and extending the TPM PCR aggregate. Since tpm_extend can take
 * long (and the tpm driver uses a mutex), we can't use the spinlock.
 */
static DEFINE_MUTEX(ima_extend_list_mutex);

/*
 * Used internally by the kernel to suspend measurements.
 * Protected by ima_extend_list_mutex.
 */
static bool ima_measurements_suspended;

/* Callers must call synchronize_rcu() and free the hash table. */
static struct hlist_head *ima_alloc_replace_htable(void)
{
	struct hlist_head *old_htable, *new_htable;

	/* Initializing to zeros is equivalent to call HLIST_HEAD_INIT. */
	new_htable = kcalloc(IMA_MEASURE_HTABLE_SIZE, sizeof(struct hlist_head),
			     GFP_KERNEL);
	if (!new_htable)
		return ERR_PTR(-ENOMEM);

	old_htable = rcu_replace_pointer(ima_htable, new_htable,
				lockdep_is_held(&ima_extend_list_mutex));

	return old_htable;
}

int __init ima_init_htable(void)
{
	struct hlist_head *old_htable;

	mutex_lock(&ima_extend_list_mutex);
	old_htable = ima_alloc_replace_htable();
	mutex_unlock(&ima_extend_list_mutex);

	if (IS_ERR(old_htable))
		return PTR_ERR(old_htable);

	/* Synchronize_rcu() and kfree() not necessary, only for robustness. */
	synchronize_rcu();
	kfree(old_htable);
	return 0;
}

/* lookup up the digest value in the hash table, and return the entry */
static struct ima_queue_entry *ima_lookup_digest_entry(u8 *digest_value,
						       int pcr)
{
	struct ima_queue_entry *qe, *ret = NULL;
	struct hlist_head *htable;
	unsigned int key;
	int rc;

	key = ima_hash_key(digest_value);
	rcu_read_lock();
	htable = rcu_dereference(ima_htable);
	hlist_for_each_entry_rcu(qe, &htable[key], hnext) {
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

static void ima_update_binary_runtime_size(struct ima_template_entry *entry,
					   enum binary_lists binary_list)
{
	int size;

	if (binary_runtime_size[binary_list] == ULONG_MAX)
		return;

	size = get_binary_runtime_size(entry);
	binary_runtime_size[binary_list] =
		(binary_runtime_size[binary_list] < ULONG_MAX - size) ?
		binary_runtime_size[binary_list] + size : ULONG_MAX;
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
	struct hlist_head *htable;
	unsigned int key;

	qe = kmalloc_obj(*qe);
	if (qe == NULL) {
		pr_err("OUT OF MEMORY ERROR creating queue entry\n");
		return -ENOMEM;
	}
	qe->entry = entry;

	INIT_LIST_HEAD(&qe->later);
	list_add_tail_rcu(&qe->later, &ima_measurements);

	htable = rcu_dereference_protected(ima_htable,
				lockdep_is_held(&ima_extend_list_mutex));

	atomic_long_inc(&ima_num_records[BINARY]);
	atomic_long_inc(&ima_num_records[BINARY_FULL]);

	if (update_htable) {
		key = ima_hash_key(entry->digests[ima_hash_algo_idx].digest);
		hlist_add_head_rcu(&qe->hnext, &htable[key]);
	}

	ima_update_binary_runtime_size(entry, BINARY);
	ima_update_binary_runtime_size(entry, BINARY_FULL);

	return 0;
}

/*
 * Return the amount of memory required for serializing the
 * entire binary_runtime_measurement list, including the ima_kexec_hdr
 * structure.
 */
unsigned long ima_get_binary_runtime_size(enum binary_lists binary_list)
{
	unsigned long val;

	mutex_lock(&ima_extend_list_mutex);
	val = binary_runtime_size[binary_list];
	mutex_unlock(&ima_extend_list_mutex);

	if (val >= (ULONG_MAX - sizeof(struct ima_kexec_hdr)))
		return ULONG_MAX;
	else
		return val + sizeof(struct ima_kexec_hdr);
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

	/*
	 * Avoid appending to the measurement log when the TPM subsystem has
	 * been shut down while preparing for system reboot.
	 */
	if (ima_measurements_suspended) {
		audit_cause = "measurements_suspended";
		audit_info = 0;
		result = -ENODEV;
		goto out;
	}

	if (!violation && !IS_ENABLED(CONFIG_IMA_DISABLE_HTABLE)) {
		if (ima_lookup_digest_entry(digest, entry->pcr)) {
			audit_cause = "hash_exists";
			result = -EEXIST;
			goto out;
		}
	}

	result = ima_add_digest_entry(entry,
				      !IS_ENABLED(CONFIG_IMA_DISABLE_HTABLE));
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

/**
 * ima_queue_stage - Stage all measurements
 *
 * If the staged measurements list is empty, the current measurements list is
 * not empty, and measurement is not suspended, move the measurements from the
 * current list to the staged one, and update the number of records and binary
 * run-time size accordingly.
 *
 * Do not allow staging after measurement is suspended, so that dumping
 * measurements can be done in a lockless way.
 *
 * Return: Zero on success, a negative value otherwise.
 */
int ima_queue_stage(void)
{
	int ret = 0;

	mutex_lock(&ima_extend_list_mutex);
	if (!list_empty(&ima_measurements_staged)) {
		ret = -EEXIST;
		goto out_unlock;
	}

	if (list_empty(&ima_measurements)) {
		ret = -ENOENT;
		goto out_unlock;
	}

	if (ima_measurements_suspended) {
		ret = -EACCES;
		goto out_unlock;
	}

	list_replace(&ima_measurements, &ima_measurements_staged);
	INIT_LIST_HEAD(&ima_measurements);

	atomic_long_set(&ima_num_records[BINARY_STAGED],
			atomic_long_read(&ima_num_records[BINARY]));
	atomic_long_set(&ima_num_records[BINARY], 0);

	if (IS_ENABLED(CONFIG_IMA_KEXEC)) {
		binary_runtime_size[BINARY_STAGED] =
					binary_runtime_size[BINARY];
		binary_runtime_size[BINARY] = 0;
	}
out_unlock:
	mutex_unlock(&ima_extend_list_mutex);
	return ret;
}

static void ima_queue_delete(struct list_head *head, bool flush_htable);

/**
 * ima_queue_staged_delete_all - Delete staged measurements
 *
 * Move staged measurements to a temporary list, ima_measurements_trim, update
 * the number of records and the binary run-time size accordingly. Finally,
 * delete measurements in the temporary list.
 *
 * Refuse to delete staged measurements if measurement is suspended, so that
 * dump can be done in a lockless way and user space is notified about staged
 * measurements being carried over to the secondary kernel, so that it does not
 * save them twice.
 *
 * Return: Zero on success, a negative value otherwise.
 */
int ima_queue_staged_delete_all(void)
{
	struct hlist_head *old_queue = NULL;
	LIST_HEAD(ima_measurements_trim);

	mutex_lock(&ima_extend_list_mutex);
	if (list_empty(&ima_measurements_staged)) {
		mutex_unlock(&ima_extend_list_mutex);
		return -ENOENT;
	}

	if (ima_measurements_suspended) {
		mutex_unlock(&ima_extend_list_mutex);
		return -ESTALE;
	}

	list_replace(&ima_measurements_staged, &ima_measurements_trim);
	INIT_LIST_HEAD(&ima_measurements_staged);

	atomic_long_set(&ima_num_records[BINARY_STAGED], 0);

	if (IS_ENABLED(CONFIG_IMA_KEXEC))
		binary_runtime_size[BINARY_STAGED] = 0;

	if (ima_flush_htable) {
		old_queue = ima_alloc_replace_htable();
		if (IS_ERR(old_queue)) {
			mutex_unlock(&ima_extend_list_mutex);
			return PTR_ERR(old_queue);
		}
	}

	mutex_unlock(&ima_extend_list_mutex);

	if (ima_flush_htable) {
		synchronize_rcu();
		kfree(old_queue);
	}

	ima_queue_delete(&ima_measurements_trim, ima_flush_htable);
	return 0;
}

/**
 * ima_queue_delete_partial - Delete current measurements
 * @req_value: Number of measurements to delete
 *
 * Delete the requested number of measurements from the current measurements
 * list, and update the number of records and the binary run-time size
 * accordingly.
 *
 * Refuse to delete current measurements if measurement is suspended, so that
 * dump can be done in a lockless way and user space is notified about current
 * measurements being carried over to the secondary kernel, so that it does not
 * save them twice.
 *
 * Return: Zero on success, a negative value otherwise.
 */
int ima_queue_delete_partial(unsigned long req_value)
{
	unsigned long req_value_copy = req_value;
	unsigned long size_to_remove = 0, num_to_remove = 0;
	LIST_HEAD(ima_measurements_trim);
	struct ima_queue_entry *qe;
	int ret = 0;

	/*
	 * list_for_each_entry_rcu() without rcu_read_lock() is fine because
	 * only list append can happen concurrently. No list replace due to the
	 * staging/delete writers mutual exclusion.
	 */
	list_for_each_entry_rcu(qe, &ima_measurements, later, true) {
		size_to_remove += get_binary_runtime_size(qe->entry);
		num_to_remove++;

		if (--req_value_copy == 0)
			break;
	}

	/* Not enough records to delete. */
	if (req_value_copy > 0)
		return -ENOENT;

	mutex_lock(&ima_extend_list_mutex);
	if (ima_measurements_suspended) {
		mutex_unlock(&ima_extend_list_mutex);
		return -ESTALE;
	}

	/*
	 * qe remains valid because ima_fs.c enforces single-writer exclusion.
	 */
	__list_cut_position(&ima_measurements_trim, &ima_measurements,
			    &qe->later);

	atomic_long_sub(num_to_remove, &ima_num_records[BINARY]);

	if (IS_ENABLED(CONFIG_IMA_KEXEC))
		binary_runtime_size[BINARY] -= size_to_remove;

	mutex_unlock(&ima_extend_list_mutex);

	ima_queue_delete(&ima_measurements_trim, false);
	return ret;
}

/**
 * ima_queue_delete - Delete measurements
 * @head: List head measurements are deleted from
 * @flush_htable: Whether or not the hash table is being flushed
 *
 * Delete the measurements from the passed list head completely if the
 * hash table is not enabled or is being flushed, or partially (only the
 * template data), if the hash table is used.
 */
static void ima_queue_delete(struct list_head *head, bool flush_htable)
{
	struct ima_queue_entry *qe, *qe_tmp;
	unsigned int i;

	list_for_each_entry_safe(qe, qe_tmp, head, later) {
		/*
		 * Safe to free template_data here without synchronize_rcu()
		 * because the only htable reader, ima_lookup_digest_entry(),
		 * accesses only entry->digests, not template_data. If new
		 * htable readers are added that access template_data, a
		 * synchronize_rcu() is required here.
		 */
		for (i = 0; i < qe->entry->template_desc->num_fields; i++) {
			kfree(qe->entry->template_data[i].data);
			qe->entry->template_data[i].data = NULL;
			qe->entry->template_data[i].len = 0;
		}

		list_del(&qe->later);

		/* No leak if condition is false, referenced by ima_htable. */
		if (IS_ENABLED(CONFIG_IMA_DISABLE_HTABLE) || flush_htable) {
			kfree(qe->entry->digests);
			kfree(qe->entry);
			kfree(qe);
		}
	}
}

int ima_restore_measurement_entry(struct ima_template_entry *entry)
{
	int result = 0;

	mutex_lock(&ima_extend_list_mutex);
	result = ima_add_digest_entry(entry, 0);
	mutex_unlock(&ima_extend_list_mutex);
	return result;
}

static void ima_measurements_suspend(void)
{
	mutex_lock(&ima_extend_list_mutex);
	ima_measurements_suspended = true;
	mutex_unlock(&ima_extend_list_mutex);
}

static int ima_reboot_notifier(struct notifier_block *nb,
			       unsigned long action,
			       void *data)
{
#ifdef CONFIG_IMA_KEXEC
	if (action == SYS_RESTART && data && !strcmp(data, "kexec reboot"))
		ima_measure_kexec_event("kexec_execute");
#endif

	ima_measurements_suspend();

	return NOTIFY_DONE;
}

static struct notifier_block ima_reboot_nb = {
	.notifier_call = ima_reboot_notifier,
};

void __init ima_init_reboot_notifier(void)
{
	register_reboot_notifier(&ima_reboot_nb);
}

int __init ima_init_digests(void)
{
	u16 digest_size;
	u16 crypto_id;
	int i;

	if (!ima_tpm_chip)
		return 0;

	digests = kzalloc_objs(*digests, ima_tpm_chip->nr_allocated_banks,
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
