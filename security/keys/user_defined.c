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
#include <asm/uaccess.h>
#include "internal.h"

static int user_instantiate(struct key *key, const void *data, size_t datalen);
static int user_duplicate(struct key *key, const struct key *source);
static int user_update(struct key *key, const void *data, size_t datalen);
static int user_match(const struct key *key, const void *criterion);
static void user_destroy(struct key *key);
static void user_describe(const struct key *user, struct seq_file *m);
static long user_read(const struct key *key,
		      char __user *buffer, size_t buflen);

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

/*****************************************************************************/
/*
 * instantiate a user defined key
 */
static int user_instantiate(struct key *key, const void *data, size_t datalen)
{
	int ret;

	ret = -EINVAL;
	if (datalen <= 0 || datalen > 32767 || !data)
		goto error;

	ret = key_payload_reserve(key, datalen);
	if (ret < 0)
		goto error;

	/* attach the data */
	ret = -ENOMEM;
	key->payload.data = kmalloc(datalen, GFP_KERNEL);
	if (!key->payload.data)
		goto error;

	memcpy(key->payload.data, data, datalen);
	ret = 0;

 error:
	return ret;

} /* end user_instantiate() */

/*****************************************************************************/
/*
 * duplicate a user defined key
 */
static int user_duplicate(struct key *key, const struct key *source)
{
	int ret;

	/* just copy the payload */
	ret = -ENOMEM;
	key->payload.data = kmalloc(source->datalen, GFP_KERNEL);

	if (key->payload.data) {
		key->datalen = source->datalen;
		memcpy(key->payload.data, source->payload.data, source->datalen);
		ret = 0;
	}

	return ret;

} /* end user_duplicate() */

/*****************************************************************************/
/*
 * update a user defined key
 */
static int user_update(struct key *key, const void *data, size_t datalen)
{
	void *new, *zap;
	int ret;

	ret = -EINVAL;
	if (datalen <= 0 || datalen > 32767 || !data)
		goto error;

	/* copy the data */
	ret = -ENOMEM;
	new = kmalloc(datalen, GFP_KERNEL);
	if (!new)
		goto error;

	memcpy(new, data, datalen);

	/* check the quota and attach the new data */
	zap = new;
	write_lock(&key->lock);

	ret = key_payload_reserve(key, datalen);

	if (ret == 0) {
		/* attach the new data, displacing the old */
		zap = key->payload.data;
		key->payload.data = new;
		key->expiry = 0;
	}

	write_unlock(&key->lock);
	kfree(zap);

 error:
	return ret;

} /* end user_update() */

/*****************************************************************************/
/*
 * match users on their name
 */
static int user_match(const struct key *key, const void *description)
{
	return strcmp(key->description, description) == 0;

} /* end user_match() */

/*****************************************************************************/
/*
 * dispose of the data dangling from the corpse of a user
 */
static void user_destroy(struct key *key)
{
	kfree(key->payload.data);

} /* end user_destroy() */

/*****************************************************************************/
/*
 * describe the user
 */
static void user_describe(const struct key *key, struct seq_file *m)
{
	seq_puts(m, key->description);

	seq_printf(m, ": %u", key->datalen);

} /* end user_describe() */

/*****************************************************************************/
/*
 * read the key data
 */
static long user_read(const struct key *key,
		      char __user *buffer, size_t buflen)
{
	long ret = key->datalen;

	/* we can return the data as is */
	if (buffer && buflen > 0) {
		if (buflen > key->datalen)
			buflen = key->datalen;

		if (copy_to_user(buffer, key->payload.data, buflen) != 0)
			ret = -EFAULT;
	}

	return ret;

} /* end user_read() */
