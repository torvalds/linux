/* Large capacity key type
 *
 * Copyright (C) 2013 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/seq_file.h>
#include <linux/file.h>
#include <linux/shmem_fs.h>
#include <linux/err.h>
#include <keys/user-type.h>
#include <keys/big_key-type.h>

MODULE_LICENSE("GPL");

/*
 * Layout of key payload words.
 */
enum {
	big_key_data,
	big_key_path,
	big_key_path_2nd_part,
	big_key_len,
};

/*
 * If the data is under this limit, there's no point creating a shm file to
 * hold it as the permanently resident metadata for the shmem fs will be at
 * least as large as the data.
 */
#define BIG_KEY_FILE_THRESHOLD (sizeof(struct inode) + sizeof(struct dentry))

/*
 * big_key defined keys take an arbitrary string as the description and an
 * arbitrary blob of data as the payload
 */
struct key_type key_type_big_key = {
	.name			= "big_key",
	.preparse		= big_key_preparse,
	.free_preparse		= big_key_free_preparse,
	.instantiate		= generic_key_instantiate,
	.revoke			= big_key_revoke,
	.destroy		= big_key_destroy,
	.describe		= big_key_describe,
	.read			= big_key_read,
};

/*
 * Preparse a big key
 */
int big_key_preparse(struct key_preparsed_payload *prep)
{
	struct path *path = (struct path *)&prep->payload.data[big_key_path];
	struct file *file;
	ssize_t written;
	size_t datalen = prep->datalen;
	int ret;

	ret = -EINVAL;
	if (datalen <= 0 || datalen > 1024 * 1024 || !prep->data)
		goto error;

	/* Set an arbitrary quota */
	prep->quotalen = 16;

	prep->payload.data[big_key_len] = (void *)(unsigned long)datalen;

	if (datalen > BIG_KEY_FILE_THRESHOLD) {
		/* Create a shmem file to store the data in.  This will permit the data
		 * to be swapped out if needed.
		 *
		 * TODO: Encrypt the stored data with a temporary key.
		 */
		file = shmem_kernel_file_setup("", datalen, 0);
		if (IS_ERR(file)) {
			ret = PTR_ERR(file);
			goto error;
		}

		written = kernel_write(file, prep->data, prep->datalen, 0);
		if (written != datalen) {
			ret = written;
			if (written >= 0)
				ret = -ENOMEM;
			goto err_fput;
		}

		/* Pin the mount and dentry to the key so that we can open it again
		 * later
		 */
		*path = file->f_path;
		path_get(path);
		fput(file);
	} else {
		/* Just store the data in a buffer */
		void *data = kmalloc(datalen, GFP_KERNEL);
		if (!data)
			return -ENOMEM;

		prep->payload.data[big_key_data] = data;
		memcpy(data, prep->data, prep->datalen);
	}
	return 0;

err_fput:
	fput(file);
error:
	return ret;
}

/*
 * Clear preparsement.
 */
void big_key_free_preparse(struct key_preparsed_payload *prep)
{
	if (prep->datalen > BIG_KEY_FILE_THRESHOLD) {
		struct path *path = (struct path *)&prep->payload.data[big_key_path];
		path_put(path);
	} else {
		kfree(prep->payload.data[big_key_data]);
	}
}

/*
 * dispose of the links from a revoked keyring
 * - called with the key sem write-locked
 */
void big_key_revoke(struct key *key)
{
	struct path *path = (struct path *)&key->payload.data[big_key_path];

	/* clear the quota */
	key_payload_reserve(key, 0);
	if (key_is_instantiated(key) &&
	    (size_t)key->payload.data[big_key_len] > BIG_KEY_FILE_THRESHOLD)
		vfs_truncate(path, 0);
}

/*
 * dispose of the data dangling from the corpse of a big_key key
 */
void big_key_destroy(struct key *key)
{
	size_t datalen = (size_t)key->payload.data[big_key_len];

	if (datalen) {
		struct path *path = (struct path *)&key->payload.data[big_key_path];
		path_put(path);
		path->mnt = NULL;
		path->dentry = NULL;
	} else {
		kfree(key->payload.data[big_key_data]);
		key->payload.data[big_key_data] = NULL;
	}
}

/*
 * describe the big_key key
 */
void big_key_describe(const struct key *key, struct seq_file *m)
{
	size_t datalen = (size_t)key->payload.data[big_key_len];

	seq_puts(m, key->description);

	if (key_is_instantiated(key))
		seq_printf(m, ": %zu [%s]",
			   datalen,
			   datalen > BIG_KEY_FILE_THRESHOLD ? "file" : "buff");
}

/*
 * read the key data
 * - the key's semaphore is read-locked
 */
long big_key_read(const struct key *key, char __user *buffer, size_t buflen)
{
	size_t datalen = (size_t)key->payload.data[big_key_len];
	long ret;

	if (!buffer || buflen < datalen)
		return datalen;

	if (datalen > BIG_KEY_FILE_THRESHOLD) {
		struct path *path = (struct path *)&key->payload.data[big_key_path];
		struct file *file;
		loff_t pos;

		file = dentry_open(path, O_RDONLY, current_cred());
		if (IS_ERR(file))
			return PTR_ERR(file);

		pos = 0;
		ret = vfs_read(file, buffer, datalen, &pos);
		fput(file);
		if (ret >= 0 && ret != datalen)
			ret = -EIO;
	} else {
		ret = datalen;
		if (copy_to_user(buffer, key->payload.data[big_key_data],
				 datalen) != 0)
			ret = -EFAULT;
	}

	return ret;
}

/*
 * Module stuff
 */
static int __init big_key_init(void)
{
	return register_key_type(&key_type_big_key);
}

static void __exit big_key_cleanup(void)
{
	unregister_key_type(&key_type_big_key);
}

module_init(big_key_init);
module_exit(big_key_cleanup);
