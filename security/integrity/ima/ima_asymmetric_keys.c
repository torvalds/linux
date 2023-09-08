// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2019 Microsoft Corporation
 *
 * Author: Lakshmi Ramasubramanian (nramas@linux.microsoft.com)
 *
 * File: ima_asymmetric_keys.c
 *       Defines an IMA hook to measure asymmetric keys on key
 *       create or update.
 */

#include <keys/asymmetric-type.h>
#include <linux/user_namespace.h>
#include <linux/ima.h>
#include "ima.h"

/**
 * ima_post_key_create_or_update - measure asymmetric keys
 * @keyring: keyring to which the key is linked to
 * @key: created or updated key
 * @payload: The data used to instantiate or update the key.
 * @payload_len: The length of @payload.
 * @flags: key flags
 * @create: flag indicating whether the key was created or updated
 *
 * Keys can only be measured, not appraised.
 * The payload data used to instantiate or update the key is measured.
 */
void ima_post_key_create_or_update(struct key *keyring, struct key *key,
				   const void *payload, size_t payload_len,
				   unsigned long flags, bool create)
{
	bool queued = false;

	/* Only asymmetric keys are handled by this hook. */
	if (key->type != &key_type_asymmetric)
		return;

	if (!payload || (payload_len == 0))
		return;

	if (ima_should_queue_key())
		queued = ima_queue_key(keyring, payload, payload_len);

	if (queued)
		return;

	/*
	 * keyring->description points to the name of the keyring
	 * (such as ".builtin_trusted_keys", ".ima", etc.) to
	 * which the given key is linked to.
	 *
	 * The name of the keyring is passed in the "eventname"
	 * parameter to process_buffer_measurement() and is set
	 * in the "eventname" field in ima_event_data for
	 * the key measurement IMA event.
	 *
	 * The name of the keyring is also passed in the "keyring"
	 * parameter to process_buffer_measurement() to check
	 * if the IMA policy is configured to measure a key linked
	 * to the given keyring.
	 */
	process_buffer_measurement(&nop_mnt_idmap, NULL, payload, payload_len,
				   keyring->description, KEY_CHECK, 0,
				   keyring->description, false, NULL, 0);
}
