// SPDX-License-Identifier: GPL-2.0
/*
 * Filesystem-level keyring for fscrypt
 *
 * Copyright 2019 Google LLC
 */

/*
 * This file implements management of fscrypt master keys in the
 * filesystem-level keyring, including the ioctls:
 *
 * - FS_IOC_ADD_ENCRYPTION_KEY
 *
 * See the "User API" section of Documentation/filesystems/fscrypt.rst for more
 * information about these ioctls.
 */

#include <linux/key-type.h>
#include <linux/seq_file.h>

#include "fscrypt_private.h"

static void wipe_master_key_secret(struct fscrypt_master_key_secret *secret)
{
	memzero_explicit(secret, sizeof(*secret));
}

static void move_master_key_secret(struct fscrypt_master_key_secret *dst,
				   struct fscrypt_master_key_secret *src)
{
	memcpy(dst, src, sizeof(*dst));
	memzero_explicit(src, sizeof(*src));
}

static void free_master_key(struct fscrypt_master_key *mk)
{
	wipe_master_key_secret(&mk->mk_secret);
	kzfree(mk);
}

static inline bool valid_key_spec(const struct fscrypt_key_specifier *spec)
{
	if (spec->__reserved)
		return false;
	return master_key_spec_len(spec) != 0;
}

static int fscrypt_key_instantiate(struct key *key,
				   struct key_preparsed_payload *prep)
{
	key->payload.data[0] = (struct fscrypt_master_key *)prep->data;
	return 0;
}

static void fscrypt_key_destroy(struct key *key)
{
	free_master_key(key->payload.data[0]);
}

static void fscrypt_key_describe(const struct key *key, struct seq_file *m)
{
	seq_puts(m, key->description);
}

/*
 * Type of key in ->s_master_keys.  Each key of this type represents a master
 * key which has been added to the filesystem.  Its payload is a
 * 'struct fscrypt_master_key'.  The "." prefix in the key type name prevents
 * users from adding keys of this type via the keyrings syscalls rather than via
 * the intended method of FS_IOC_ADD_ENCRYPTION_KEY.
 */
static struct key_type key_type_fscrypt = {
	.name			= "._fscrypt",
	.instantiate		= fscrypt_key_instantiate,
	.destroy		= fscrypt_key_destroy,
	.describe		= fscrypt_key_describe,
};

/* Search ->s_master_keys */
static struct key *search_fscrypt_keyring(struct key *keyring,
					  struct key_type *type,
					  const char *description)
{
	/*
	 * We need to mark the keyring reference as "possessed" so that we
	 * acquire permission to search it, via the KEY_POS_SEARCH permission.
	 */
	key_ref_t keyref = make_key_ref(keyring, true /* possessed */);

	keyref = keyring_search(keyref, type, description, false);
	if (IS_ERR(keyref)) {
		if (PTR_ERR(keyref) == -EAGAIN || /* not found */
		    PTR_ERR(keyref) == -EKEYREVOKED) /* recently invalidated */
			keyref = ERR_PTR(-ENOKEY);
		return ERR_CAST(keyref);
	}
	return key_ref_to_ptr(keyref);
}

#define FSCRYPT_FS_KEYRING_DESCRIPTION_SIZE	\
	(CONST_STRLEN("fscrypt-") + FIELD_SIZEOF(struct super_block, s_id))

#define FSCRYPT_MK_DESCRIPTION_SIZE	(2 * FSCRYPT_KEY_DESCRIPTOR_SIZE + 1)

static void format_fs_keyring_description(
			char description[FSCRYPT_FS_KEYRING_DESCRIPTION_SIZE],
			const struct super_block *sb)
{
	sprintf(description, "fscrypt-%s", sb->s_id);
}

static void format_mk_description(
			char description[FSCRYPT_MK_DESCRIPTION_SIZE],
			const struct fscrypt_key_specifier *mk_spec)
{
	sprintf(description, "%*phN",
		master_key_spec_len(mk_spec), (u8 *)&mk_spec->u);
}

/* Create ->s_master_keys if needed.  Synchronized by fscrypt_add_key_mutex. */
static int allocate_filesystem_keyring(struct super_block *sb)
{
	char description[FSCRYPT_FS_KEYRING_DESCRIPTION_SIZE];
	struct key *keyring;

	if (sb->s_master_keys)
		return 0;

	format_fs_keyring_description(description, sb);
	keyring = keyring_alloc(description, GLOBAL_ROOT_UID, GLOBAL_ROOT_GID,
				current_cred(), KEY_POS_SEARCH |
				  KEY_USR_SEARCH | KEY_USR_READ | KEY_USR_VIEW,
				KEY_ALLOC_NOT_IN_QUOTA, NULL, NULL);
	if (IS_ERR(keyring))
		return PTR_ERR(keyring);

	/* Pairs with READ_ONCE() in fscrypt_find_master_key() */
	smp_store_release(&sb->s_master_keys, keyring);
	return 0;
}

void fscrypt_sb_free(struct super_block *sb)
{
	key_put(sb->s_master_keys);
	sb->s_master_keys = NULL;
}

/*
 * Find the specified master key in ->s_master_keys.
 * Returns ERR_PTR(-ENOKEY) if not found.
 */
struct key *fscrypt_find_master_key(struct super_block *sb,
				    const struct fscrypt_key_specifier *mk_spec)
{
	struct key *keyring;
	char description[FSCRYPT_MK_DESCRIPTION_SIZE];

	/* pairs with smp_store_release() in allocate_filesystem_keyring() */
	keyring = READ_ONCE(sb->s_master_keys);
	if (keyring == NULL)
		return ERR_PTR(-ENOKEY); /* No keyring yet, so no keys yet. */

	format_mk_description(description, mk_spec);
	return search_fscrypt_keyring(keyring, &key_type_fscrypt, description);
}

/*
 * Allocate a new fscrypt_master_key which contains the given secret, set it as
 * the payload of a new 'struct key' of type fscrypt, and link the 'struct key'
 * into the given keyring.  Synchronized by fscrypt_add_key_mutex.
 */
static int add_new_master_key(struct fscrypt_master_key_secret *secret,
			      const struct fscrypt_key_specifier *mk_spec,
			      struct key *keyring)
{
	struct fscrypt_master_key *mk;
	char description[FSCRYPT_MK_DESCRIPTION_SIZE];
	struct key *key;
	int err;

	mk = kzalloc(sizeof(*mk), GFP_KERNEL);
	if (!mk)
		return -ENOMEM;

	mk->mk_spec = *mk_spec;

	move_master_key_secret(&mk->mk_secret, secret);

	format_mk_description(description, mk_spec);
	key = key_alloc(&key_type_fscrypt, description,
			GLOBAL_ROOT_UID, GLOBAL_ROOT_GID, current_cred(),
			KEY_POS_SEARCH | KEY_USR_SEARCH | KEY_USR_VIEW,
			KEY_ALLOC_NOT_IN_QUOTA, NULL);
	if (IS_ERR(key)) {
		err = PTR_ERR(key);
		goto out_free_mk;
	}
	err = key_instantiate_and_link(key, mk, sizeof(*mk), keyring, NULL);
	key_put(key);
	if (err)
		goto out_free_mk;

	return 0;

out_free_mk:
	free_master_key(mk);
	return err;
}

static int add_master_key(struct super_block *sb,
			  struct fscrypt_master_key_secret *secret,
			  const struct fscrypt_key_specifier *mk_spec)
{
	static DEFINE_MUTEX(fscrypt_add_key_mutex);
	struct key *key;
	int err;

	mutex_lock(&fscrypt_add_key_mutex); /* serialize find + link */
	key = fscrypt_find_master_key(sb, mk_spec);
	if (IS_ERR(key)) {
		err = PTR_ERR(key);
		if (err != -ENOKEY)
			goto out_unlock;
		/* Didn't find the key in ->s_master_keys.  Add it. */
		err = allocate_filesystem_keyring(sb);
		if (err)
			goto out_unlock;
		err = add_new_master_key(secret, mk_spec, sb->s_master_keys);
	} else {
		key_put(key);
		err = 0;
	}
out_unlock:
	mutex_unlock(&fscrypt_add_key_mutex);
	return err;
}

/*
 * Add a master encryption key to the filesystem, causing all files which were
 * encrypted with it to appear "unlocked" (decrypted) when accessed.
 *
 * For more details, see the "FS_IOC_ADD_ENCRYPTION_KEY" section of
 * Documentation/filesystems/fscrypt.rst.
 */
int fscrypt_ioctl_add_key(struct file *filp, void __user *_uarg)
{
	struct super_block *sb = file_inode(filp)->i_sb;
	struct fscrypt_add_key_arg __user *uarg = _uarg;
	struct fscrypt_add_key_arg arg;
	struct fscrypt_master_key_secret secret;
	int err;

	if (copy_from_user(&arg, uarg, sizeof(arg)))
		return -EFAULT;

	if (!valid_key_spec(&arg.key_spec))
		return -EINVAL;

	if (arg.raw_size < FSCRYPT_MIN_KEY_SIZE ||
	    arg.raw_size > FSCRYPT_MAX_KEY_SIZE)
		return -EINVAL;

	if (memchr_inv(arg.__reserved, 0, sizeof(arg.__reserved)))
		return -EINVAL;

	memset(&secret, 0, sizeof(secret));
	secret.size = arg.raw_size;
	err = -EFAULT;
	if (copy_from_user(secret.raw, uarg->raw, secret.size))
		goto out_wipe_secret;

	err = -EACCES;
	if (!capable(CAP_SYS_ADMIN))
		goto out_wipe_secret;

	err = add_master_key(sb, &secret, &arg.key_spec);
out_wipe_secret:
	wipe_master_key_secret(&secret);
	return err;
}
EXPORT_SYMBOL_GPL(fscrypt_ioctl_add_key);

int __init fscrypt_init_keyring(void)
{
	return register_key_type(&key_type_fscrypt);
}
