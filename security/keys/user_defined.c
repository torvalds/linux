/* user_defined.c: user defined key type
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/err.h>
#include <keys/user-type.h>
#include <linux/uaccess.h>
#include "internal.h"

static int logon_vet_description(const char *desc);

/*
 * user defined keys take an arbitrary string as the description and an
 * arbitrary blob of data as the payload
 */
struct key_type key_type_user = {
	.name			= "user",
	.preparse		= user_preparse,
	.free_preparse		= user_free_preparse,
	.instantiate		= generic_key_instantiate,
	.update			= user_update,
	.revoke			= user_revoke,
	.destroy		= user_destroy,
	.describe		= user_describe,
	.read			= user_read,
};

EXPORT_SYMBOL_GPL(key_type_user);

/*
 * This key type is essentially the same as key_type_user, but it does
 * not define a .read op. This is suitable for storing username and
 * password pairs in the keyring that you do not want to be readable
 * from userspace.
 */
struct key_type key_type_logon = {
	.name			= "logon",
	.preparse		= user_preparse,
	.free_preparse		= user_free_preparse,
	.instantiate		= generic_key_instantiate,
	.update			= user_update,
	.revoke			= user_revoke,
	.destroy		= user_destroy,
	.describe		= user_describe,
	.vet_description	= logon_vet_description,
};
EXPORT_SYMBOL_GPL(key_type_logon);

/*
 * Preparse a user defined key payload
 */
int user_preparse(struct key_preparsed_payload *prep)
{
	struct user_key_payload *upayload;
	size_t datalen = prep->datalen;

	if (datalen <= 0 || datalen > 32767 || !prep->data)
		return -EINVAL;

	upayload = kmalloc(sizeof(*upayload) + datalen, GFP_KERNEL);
	if (!upayload)
		return -ENOMEM;

	/* attach the data */
	prep->quotalen = datalen;
	prep->payload.data[0] = upayload;
	upayload->datalen = datalen;
	memcpy(upayload->data, prep->data, datalen);
	return 0;
}
EXPORT_SYMBOL_GPL(user_preparse);

/*
 * Free a preparse of a user defined key payload
 */
void user_free_preparse(struct key_preparsed_payload *prep)
{
	kzfree(prep->payload.data[0]);
}
EXPORT_SYMBOL_GPL(user_free_preparse);

static void user_free_payload_rcu(struct rcu_head *head)
{
	struct user_key_payload *payload;

	payload = container_of(head, struct user_key_payload, rcu);
	kzfree(payload);
}

/*
 * update a user defined key
 * - the key's semaphore is write-locked
 */
int user_update(struct key *key, struct key_preparsed_payload *prep)
{
	struct user_key_payload *zap = NULL;
	int ret;

	/* check the quota and attach the new data */
	ret = key_payload_reserve(key, prep->datalen);
	if (ret < 0)
		return ret;

	/* attach the new data, displacing the old */
	key->expiry = prep->expiry;
	if (key_is_positive(key))
		zap = dereference_key_locked(key);
	rcu_assign_keypointer(key, prep->payload.data[0]);
	prep->payload.data[0] = NULL;

	if (zap)
		call_rcu(&zap->rcu, user_free_payload_rcu);
	return ret;
}
EXPORT_SYMBOL_GPL(user_update);

/*
 * dispose of the links from a revoked keyring
 * - called with the key sem write-locked
 */
void user_revoke(struct key *key)
{
	struct user_key_payload *upayload = user_key_payload_locked(key);

	/* clear the quota */
	key_payload_reserve(key, 0);

	if (upayload) {
		rcu_assign_keypointer(key, NULL);
		call_rcu(&upayload->rcu, user_free_payload_rcu);
	}
}

EXPORT_SYMBOL(user_revoke);

/*
 * dispose of the data dangling from the corpse of a user key
 */
void user_destroy(struct key *key)
{
	struct user_key_payload *upayload = key->payload.data[0];

	kzfree(upayload);
}

EXPORT_SYMBOL_GPL(user_destroy);

/*
 * describe the user key
 */
void user_describe(const struct key *key, struct seq_file *m)
{
	seq_puts(m, key->description);
	if (key_is_positive(key))
		seq_printf(m, ": %u", key->datalen);
}

EXPORT_SYMBOL_GPL(user_describe);

/*
 * read the key data
 * - the key's semaphore is read-locked
 */
long user_read(const struct key *key, char *buffer, size_t buflen)
{
	const struct user_key_payload *upayload;
	long ret;

	upayload = user_key_payload_locked(key);
	ret = upayload->datalen;

	/* we can return the data as is */
	if (buffer && buflen > 0) {
		if (buflen > upayload->datalen)
			buflen = upayload->datalen;

		memcpy(buffer, upayload->data, buflen);
	}

	return ret;
}

EXPORT_SYMBOL_GPL(user_read);

/* Vet the description for a "logon" key */
static int logon_vet_description(const char *desc)
{
	char *p;

	/* require a "qualified" description string */
	p = strchr(desc, ':');
	if (!p)
		return -EINVAL;

	/* also reject description with ':' as first char */
	if (p == desc)
		return -EINVAL;

	return 0;
}
