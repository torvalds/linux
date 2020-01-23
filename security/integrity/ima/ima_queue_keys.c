// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2019 Microsoft Corporation
 *
 * Author: Lakshmi Ramasubramanian (nramas@linux.microsoft.com)
 *
 * File: ima_queue_keys.c
 *       Enables deferred processing of keys
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

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


	list_for_each_entry_safe(entry, tmp, &ima_keys, list) {
		process_buffer_measurement(entry->payload,
					   entry->payload_len,
					   entry->keyring_name,
					   KEY_CHECK, 0,
					   entry->keyring_name);
		list_del(&entry->list);
		ima_free_key_entry(entry);
	}
}

inline bool ima_should_queue_key(void)
{
	return !ima_process_keys;
}
