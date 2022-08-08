// SPDX-License-Identifier: GPL-2.0-or-later
/* Large capacity key type
 *
 * Copyright (C) 2017-2020 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 * Copyright (C) 2013 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#define pr_fmt(fmt) "big_key: "fmt
#include <linux/init.h>
#include <linux/seq_file.h>
#include <linux/file.h>
#include <linux/shmem_fs.h>
#include <linux/err.h>
#include <linux/random.h>
#include <keys/user-type.h>
#include <keys/big_key-type.h>
#include <crypto/chacha20poly1305.h>

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
	.update			= big_key_update,
};

/*
 * Preparse a big key
 */
int big_key_preparse(struct key_preparsed_payload *prep)
{
	struct path *path = (struct path *)&prep->payload.data[big_key_path];
	struct file *file;
	u8 *buf, *enckey;
	ssize_t written;
	size_t datalen = prep->datalen;
	size_t enclen = datalen + CHACHA20POLY1305_AUTHTAG_SIZE;
	int ret;

	if (datalen <= 0 || datalen > 1024 * 1024 || !prep->data)
		return -EINVAL;

	/* Set an arbitrary quota */
	prep->quotalen = 16;

	prep->payload.data[big_key_len] = (void *)(unsigned long)datalen;

	if (datalen > BIG_KEY_FILE_THRESHOLD) {
		/* Create a shmem file to store the data in.  This will permit the data
		 * to be swapped out if needed.
		 *
		 * File content is stored encrypted with randomly generated key.
		 * Since the key is random for each file, we can set the nonce
		 * to zero, provided we never define a ->update() call.
		 */
		loff_t pos = 0;

		buf = kvmalloc(enclen, GFP_KERNEL);
		if (!buf)
			return -ENOMEM;

		/* generate random key */
		enckey = kmalloc(CHACHA20POLY1305_KEY_SIZE, GFP_KERNEL);
		if (!enckey) {
			ret = -ENOMEM;
			goto error;
		}
		ret = get_random_bytes_wait(enckey, CHACHA20POLY1305_KEY_SIZE);
		if (unlikely(ret))
			goto err_enckey;

		/* encrypt data */
		chacha20poly1305_encrypt(buf, prep->data, datalen, NULL, 0,
					 0, enckey);

		/* save aligned data to file */
		file = shmem_kernel_file_setup("", enclen, 0);
		if (IS_ERR(file)) {
			ret = PTR_ERR(file);
			goto err_enckey;
		}

		written = kernel_write(file, buf, enclen, &pos);
		if (written != enclen) {
			ret = written;
			if (written >= 0)
				ret = -EIO;
			goto err_fput;
		}

		/* Pin the mount and dentry to the key so that we can open it again
		 * later
		 */
		prep->payload.data[big_key_data] = enckey;
		*path = file->f_path;
		path_get(path);
		fput(file);
		kvfree_sensitive(buf, enclen);
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
err_enckey:
	kfree_sensitive(enckey);
error:
	kvfree_sensitive(buf, enclen);
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
	}
	kfree_sensitive(prep->payload.data[big_key_data]);
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
	if (key_is_positive(key) &&
	    (size_t)key->payload.data[big_key_len] > BIG_KEY_FILE_THRESHOLD)
		vfs_truncate(path, 0);
}

/*
 * dispose of the data dangling from the corpse of a big_key key
 */
void big_key_destroy(struct key *key)
{
	size_t datalen = (size_t)key->payload.data[big_key_len];

	if (datalen > BIG_KEY_FILE_THRESHOLD) {
		struct path *path = (struct path *)&key->payload.data[big_key_path];

		path_put(path);
		path->mnt = NULL;
		path->dentry = NULL;
	}
	kfree_sensitive(key->payload.data[big_key_data]);
	key->payload.data[big_key_data] = NULL;
}

/*
 * Update a big key
 */
int big_key_update(struct key *key, struct key_preparsed_payload *prep)
{
	int ret;

	ret = key_payload_reserve(key, prep->datalen);
	if (ret < 0)
		return ret;

	if (key_is_positive(key))
		big_key_destroy(key);

	return generic_key_instantiate(key, prep);
}

/*
 * describe the big_key key
 */
void big_key_describe(const struct key *key, struct seq_file *m)
{
	size_t datalen = (size_t)key->payload.data[big_key_len];

	seq_puts(m, key->description);

	if (key_is_positive(key))
		seq_printf(m, ": %zu [%s]",
			   datalen,
			   datalen > BIG_KEY_FILE_THRESHOLD ? "file" : "buff");
}

/*
 * read the key data
 * - the key's semaphore is read-locked
 */
long big_key_read(const struct key *key, char *buffer, size_t buflen)
{
	size_t datalen = (size_t)key->payload.data[big_key_len];
	long ret;

	if (!buffer || buflen < datalen)
		return datalen;

	if (datalen > BIG_KEY_FILE_THRESHOLD) {
		struct path *path = (struct path *)&key->payload.data[big_key_path];
		struct file *file;
		u8 *buf, *enckey = (u8 *)key->payload.data[big_key_data];
		size_t enclen = datalen + CHACHA20POLY1305_AUTHTAG_SIZE;
		loff_t pos = 0;

		buf = kvmalloc(enclen, GFP_KERNEL);
		if (!buf)
			return -ENOMEM;

		file = dentry_open(path, O_RDONLY, current_cred());
		if (IS_ERR(file)) {
			ret = PTR_ERR(file);
			goto error;
		}

		/* read file to kernel and decrypt */
		ret = kernel_read(file, buf, enclen, &pos);
		if (ret != enclen) {
			if (ret >= 0)
				ret = -EIO;
			goto err_fput;
		}

		ret = chacha20poly1305_decrypt(buf, buf, enclen, NULL, 0, 0,
					       enckey) ? 0 : -EBADMSG;
		if (unlikely(ret))
			goto err_fput;

		ret = datalen;

		/* copy out decrypted data */
		memcpy(buffer, buf, datalen);

err_fput:
		fput(file);
error:
		kvfree_sensitive(buf, enclen);
	} else {
		ret = datalen;
		memcpy(buffer, key->payload.data[big_key_data], datalen);
	}

	return ret;
}

/*
 * Register key type
 */
static int __init big_key_init(void)
{
	return register_key_type(&key_type_big_key);
}

late_initcall(big_key_init);
