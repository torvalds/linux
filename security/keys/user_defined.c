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
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/err.h>
#include <keys/user-type.h>
#include <asm/uaccess.h>
#include "internal.h"

/*
 * user defined keys take an arbitrary string as the description and an
 * arbitrary blob of data as the payload
 */
struct key_type key_type_user = {
	.name		= "user",
	.instantiate	= user_instantiate,
	.duplicate	= user_duplicate,
	.update		= user_update,
	.match		= user_match,
	.destroy	= user_destroy,
	.describe	= user_describe,
	.read		= user_read,
};

EXPORT_SYMBOL_GPL(key_type_user);

/*****************************************************************************/
/*
 * instantiate a user defined key
 */
int user_instantiate(struct key *key, const void *data, size_t datalen)
{
	struct user_key_payload *upayload;
	int ret;

	ret = -EINVAL;
	if (datalen <= 0 || datalen > 32767 || !data)
		goto error;

	ret = key_payload_reserve(key, datalen);
	if (ret < 0)
		goto error;

	ret = -ENOMEM;
	upayload = kmalloc(sizeof(*upayload) + datalen, GFP_KERNEL);
	if (!upayload)
		goto error;

	/* attach the data */
	upayload->datalen = datalen;
	memcpy(upayload->data, data, datalen);
	rcu_assign_pointer(key->payload.data, upayload);
	ret = 0;

error:
	return ret;

} /* end user_instantiate() */

EXPORT_SYMBOL_GPL(user_instantiate);

/*****************************************************************************/
/*
 * duplicate a user defined key
 * - both keys' semaphores are locked against further modification
 * - the new key cannot yet be accessed
 */
int user_duplicate(struct key *key, const struct key *source)
{
	struct user_key_payload *upayload, *spayload;
	int ret;

	/* just copy the payload */
	ret = -ENOMEM;
	upayload = kmalloc(sizeof(*upayload) + source->datalen, GFP_KERNEL);
	if (upayload) {
		spayload = rcu_dereference(source->payload.data);
		BUG_ON(source->datalen != spayload->datalen);

		upayload->datalen = key->datalen = spayload->datalen;
		memcpy(upayload->data, spayload->data, key->datalen);

		key->payload.data = upayload;
		ret = 0;
	}

	return ret;

} /* end user_duplicate() */

EXPORT_SYMBOL_GPL(user_duplicate);

/*****************************************************************************/
/*
 * dispose of the old data from an updated user defined key
 */
static void user_update_rcu_disposal(struct rcu_head *rcu)
{
	struct user_key_payload *upayload;

	upayload = container_of(rcu, struct user_key_payload, rcu);

	kfree(upayload);

} /* end user_update_rcu_disposal() */

/*****************************************************************************/
/*
 * update a user defined key
 * - the key's semaphore is write-locked
 */
int user_update(struct key *key, const void *data, size_t datalen)
{
	struct user_key_payload *upayload, *zap;
	int ret;

	ret = -EINVAL;
	if (datalen <= 0 || datalen > 32767 || !data)
		goto error;

	/* construct a replacement payload */
	ret = -ENOMEM;
	upayload = kmalloc(sizeof(*upayload) + datalen, GFP_KERNEL);
	if (!upayload)
		goto error;

	upayload->datalen = datalen;
	memcpy(upayload->data, data, datalen);

	/* check the quota and attach the new data */
	zap = upayload;

	ret = key_payload_reserve(key, datalen);

	if (ret == 0) {
		/* attach the new data, displacing the old */
		zap = key->payload.data;
		rcu_assign_pointer(key->payload.data, upayload);
		key->expiry = 0;
	}

	call_rcu(&zap->rcu, user_update_rcu_disposal);

error:
	return ret;

} /* end user_update() */

EXPORT_SYMBOL_GPL(user_update);

/*****************************************************************************/
/*
 * match users on their name
 */
int user_match(const struct key *key, const void *description)
{
	return strcmp(key->description, description) == 0;

} /* end user_match() */

EXPORT_SYMBOL_GPL(user_match);

/*****************************************************************************/
/*
 * dispose of the data dangling from the corpse of a user
 */
void user_destroy(struct key *key)
{
	struct user_key_payload *upayload = key->payload.data;

	kfree(upayload);

} /* end user_destroy() */

EXPORT_SYMBOL_GPL(user_destroy);

/*****************************************************************************/
/*
 * describe the user key
 */
void user_describe(const struct key *key, struct seq_file *m)
{
	seq_puts(m, key->description);

	seq_printf(m, ": %u", key->datalen);

} /* end user_describe() */

EXPORT_SYMBOL_GPL(user_describe);

/*****************************************************************************/
/*
 * read the key data
 * - the key's semaphore is read-locked
 */
long user_read(const struct key *key, char __user *buffer, size_t buflen)
{
	struct user_key_payload *upayload;
	long ret;

	upayload = rcu_dereference(key->payload.data);
	ret = upayload->datalen;

	/* we can return the data as is */
	if (buffer && buflen > 0) {
		if (buflen > upayload->datalen)
			buflen = upayload->datalen;

		if (copy_to_user(buffer, upayload->data, buflen) != 0)
			ret = -EFAULT;
	}

	return ret;

} /* end user_read() */

EXPORT_SYMBOL_GPL(user_read);
