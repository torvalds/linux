// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2019 Microsoft Corporation
 *
 * Author: Lakshmi Ramasubramanian (nramas@linux.microsoft.com)
 *
 * File: ima_queue_keys.c
 *       Enables deferred processing of keys
 */

#include <linux/user_namespace.h>
#include <linux/workqueue.h>
#include <keys/asymmetric-type.h>
#include "ima.h"

/*
 * Flag to indicate whether a key can be processed
 * right away or should be queued for processing later.
 */
static bool ima_process_keys;

/*
 * To synchronize access to the list of keys that need to be measured
 */
static DEFINE_MUTEX(ima_keys_lock);
static LIST_HEAD(ima_keys);

/*
 * If custom IMA policy is not loaded then keys queued up
 * for measurement should be freed. This worker is used
 * for handling this scenario.
 */
static long ima_key_queue_timeout = 300000; /* 5 Minutes */
static void ima_keys_handler(struct work_struct *work);
static DECLARE_DELAYED_WORK(ima_keys_delayed_work, ima_keys_handler);
static bool timer_expired;

/*
 * This worker function frees keys that may still be
 * queued up in case custom IMA policy was not loaded.
 */
static void ima_keys_handler(struct work_struct *work)
{
	timer_expired = true;
	ima_process_queued_keys();
}

/*
 * This function sets up a worker to free queued keys in case
 * custom IMA policy was never loaded.
 */
void ima_init_key_queue(void)
{
	schedule_delayed_work(&ima_keys_delayed_work,
			      msecs_to_jiffies(ima_key_queue_timeout));
}

static void ima_free_key_entry(struct ima_key_entry *entry)
{
	if (entry) {
		kfree(entry->payload);
		kfree(entry->keyring_name);
		kfree(entry);
	}
}

static struct ima_key_entry *ima_alloc_key_entry(struct key *keyring,
						 const void *payload,
						 size_t payload_len)
{
	int rc = 0;
	const char *audit_cause = "ENOMEM";
	struct ima_key_entry *entry;

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (entry) {
		entry->payload = kmemdup(payload, payload_len, GFP_KERNEL);
		entry->keyring_name = kstrdup(keyring->description,
					      GFP_KERNEL);
		entry->payload_len = payload_len;
	}

	if ((entry == NULL) || (entry->payload == NULL) ||
	    (entry->keyring_name == NULL)) {
		rc = -ENOMEM;
		goto out;
	}

	INIT_LIST_HEAD(&entry->list);

out:
	if (rc) {
		integrity_audit_message(AUDIT_INTEGRITY_PCR, NULL,
					keyring->description,
					func_measure_str(KEY_CHECK),
					audit_cause, rc, 0, rc);
		ima_free_key_entry(entry);
		entry = NULL;
	}

	return entry;
}

bool ima_queue_key(struct key *keyring, const void *payload,
		   size_t payload_len)
{
	bool queued = false;
	struct ima_key_entry *entry;

	entry = ima_alloc_key_entry(keyring, payload, payload_len);
	if (!entry)
		return false;

	mutex_lock(&ima_keys_lock);
	if (!ima_process_keys) {
		list_add_tail(&entry->list, &ima_keys);
		queued = true;
	}
	mutex_unlock(&ima_keys_lock);

	if (!queued)
		ima_free_key_entry(entry);

	return queued;
}

/*
 * ima_process_queued_keys() - process keys queued for measurement
 *
 * This function sets ima_process_keys to true and processes queued keys.
 * From here on keys will be processed right away (not queued).
 */
void ima_process_queued_keys(void)
{
	struct ima_key_entry *entry, *tmp;
	bool process = false;

	if (ima_process_keys)
		return;

	/*
	 * Since ima_process_keys is set to true, any new key will be
	 * processed immediately and not be queued to ima_keys list.
	 * First one setting the ima_process_keys flag to true will
	 * process the queued keys.
	 */
	mutex_lock(&ima_keys_lock);
	if (!ima_process_keys) {
		ima_process_keys = true;
		process = true;
	}
	mutex_unlock(&ima_keys_lock);

	if (!process)
		return;

	if (!timer_expired)
		cancel_delayed_work_sync(&ima_keys_delayed_work);

	list_for_each_entry_safe(entry, tmp, &ima_keys, list) {
		if (!timer_expired)
			process_buffer_measurement(&init_user_ns, NULL,
						   entry->payload,
						   entry->payload_len,
						   entry->keyring_name,
						   KEY_CHECK, 0,
						   entry->keyring_name,
						   false, NULL, 0);
		list_del(&entry->list);
		ima_free_key_entry(entry);
	}
}

inline bool ima_should_queue_key(void)
{
	return !ima_process_keys;
}
