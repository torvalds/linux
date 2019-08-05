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
 * - FS_IOC_REMOVE_ENCRYPTION_KEY
 * - FS_IOC_GET_ENCRYPTION_KEY_STATUS
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

	if (key_is_positive(key)) {
		const struct fscrypt_master_key *mk = key->payload.data[0];

		if (!is_master_key_secret_present(&mk->mk_secret))
			seq_puts(m, ": secret removed");
	}
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

	keyref = keyring_search(keyref, type, description);
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

	refcount_set(&mk->mk_refcount, 1); /* secret is present */
	INIT_LIST_HEAD(&mk->mk_decrypted_inodes);
	spin_lock_init(&mk->mk_decrypted_inodes_lock);

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

#define KEY_DEAD	1

static int add_existing_master_key(struct fscrypt_master_key *mk,
				   struct fscrypt_master_key_secret *secret)
{
	if (is_master_key_secret_present(&mk->mk_secret))
		return 0;

	if (!refcount_inc_not_zero(&mk->mk_refcount))
		return KEY_DEAD;

	move_master_key_secret(&mk->mk_secret, secret);
	return 0;
}

static int add_master_key(struct super_block *sb,
			  struct fscrypt_master_key_secret *secret,
			  const struct fscrypt_key_specifier *mk_spec)
{
	static DEFINE_MUTEX(fscrypt_add_key_mutex);
	struct key *key;
	int err;

	mutex_lock(&fscrypt_add_key_mutex); /* serialize find + link */
retry:
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
		/*
		 * Found the key in ->s_master_keys.  Re-add the secret if
		 * needed.
		 */
		down_write(&key->sem);
		err = add_existing_master_key(key->payload.data[0], secret);
		up_write(&key->sem);
		if (err == KEY_DEAD) {
			/* Key being removed or needs to be removed */
			key_invalidate(key);
			key_put(key);
			goto retry;
		}
		key_put(key);
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

/*
 * Try to evict the inode's dentries from the dentry cache.  If the inode is a
 * directory, then it can have at most one dentry; however, that dentry may be
 * pinned by child dentries, so first try to evict the children too.
 */
static void shrink_dcache_inode(struct inode *inode)
{
	struct dentry *dentry;

	if (S_ISDIR(inode->i_mode)) {
		dentry = d_find_any_alias(inode);
		if (dentry) {
			shrink_dcache_parent(dentry);
			dput(dentry);
		}
	}
	d_prune_aliases(inode);
}

static void evict_dentries_for_decrypted_inodes(struct fscrypt_master_key *mk)
{
	struct fscrypt_info *ci;
	struct inode *inode;
	struct inode *toput_inode = NULL;

	spin_lock(&mk->mk_decrypted_inodes_lock);

	list_for_each_entry(ci, &mk->mk_decrypted_inodes, ci_master_key_link) {
		inode = ci->ci_inode;
		spin_lock(&inode->i_lock);
		if (inode->i_state & (I_FREEING | I_WILL_FREE | I_NEW)) {
			spin_unlock(&inode->i_lock);
			continue;
		}
		__iget(inode);
		spin_unlock(&inode->i_lock);
		spin_unlock(&mk->mk_decrypted_inodes_lock);

		shrink_dcache_inode(inode);
		iput(toput_inode);
		toput_inode = inode;

		spin_lock(&mk->mk_decrypted_inodes_lock);
	}

	spin_unlock(&mk->mk_decrypted_inodes_lock);
	iput(toput_inode);
}

static int check_for_busy_inodes(struct super_block *sb,
				 struct fscrypt_master_key *mk)
{
	struct list_head *pos;
	size_t busy_count = 0;
	unsigned long ino;
	struct dentry *dentry;
	char _path[256];
	char *path = NULL;

	spin_lock(&mk->mk_decrypted_inodes_lock);

	list_for_each(pos, &mk->mk_decrypted_inodes)
		busy_count++;

	if (busy_count == 0) {
		spin_unlock(&mk->mk_decrypted_inodes_lock);
		return 0;
	}

	{
		/* select an example file to show for debugging purposes */
		struct inode *inode =
			list_first_entry(&mk->mk_decrypted_inodes,
					 struct fscrypt_info,
					 ci_master_key_link)->ci_inode;
		ino = inode->i_ino;
		dentry = d_find_alias(inode);
	}
	spin_unlock(&mk->mk_decrypted_inodes_lock);

	if (dentry) {
		path = dentry_path(dentry, _path, sizeof(_path));
		dput(dentry);
	}
	if (IS_ERR_OR_NULL(path))
		path = "(unknown)";

	fscrypt_warn(NULL,
		     "%s: %zu inode(s) still busy after removing key with %s %*phN, including ino %lu (%s)",
		     sb->s_id, busy_count, master_key_spec_type(&mk->mk_spec),
		     master_key_spec_len(&mk->mk_spec), (u8 *)&mk->mk_spec.u,
		     ino, path);
	return -EBUSY;
}

static int try_to_lock_encrypted_files(struct super_block *sb,
				       struct fscrypt_master_key *mk)
{
	int err1;
	int err2;

	/*
	 * An inode can't be evicted while it is dirty or has dirty pages.
	 * Thus, we first have to clean the inodes in ->mk_decrypted_inodes.
	 *
	 * Just do it the easy way: call sync_filesystem().  It's overkill, but
	 * it works, and it's more important to minimize the amount of caches we
	 * drop than the amount of data we sync.  Also, unprivileged users can
	 * already call sync_filesystem() via sys_syncfs() or sys_sync().
	 */
	down_read(&sb->s_umount);
	err1 = sync_filesystem(sb);
	up_read(&sb->s_umount);
	/* If a sync error occurs, still try to evict as much as possible. */

	/*
	 * Inodes are pinned by their dentries, so we have to evict their
	 * dentries.  shrink_dcache_sb() would suffice, but would be overkill
	 * and inappropriate for use by unprivileged users.  So instead go
	 * through the inodes' alias lists and try to evict each dentry.
	 */
	evict_dentries_for_decrypted_inodes(mk);

	/*
	 * evict_dentries_for_decrypted_inodes() already iput() each inode in
	 * the list; any inodes for which that dropped the last reference will
	 * have been evicted due to fscrypt_drop_inode() detecting the key
	 * removal and telling the VFS to evict the inode.  So to finish, we
	 * just need to check whether any inodes couldn't be evicted.
	 */
	err2 = check_for_busy_inodes(sb, mk);

	return err1 ?: err2;
}

/*
 * Try to remove an fscrypt master encryption key.
 *
 * First we wipe the actual master key secret, so that no more inodes can be
 * unlocked with it.  Then we try to evict all cached inodes that had been
 * unlocked with the key.
 *
 * If all inodes were evicted, then we unlink the fscrypt_master_key from the
 * keyring.  Otherwise it remains in the keyring in the "incompletely removed"
 * state (without the actual secret key) where it tracks the list of remaining
 * inodes.  Userspace can execute the ioctl again later to retry eviction, or
 * alternatively can re-add the secret key again.
 *
 * For more details, see the "Removing keys" section of
 * Documentation/filesystems/fscrypt.rst.
 */
int fscrypt_ioctl_remove_key(struct file *filp, void __user *_uarg)
{
	struct super_block *sb = file_inode(filp)->i_sb;
	struct fscrypt_remove_key_arg __user *uarg = _uarg;
	struct fscrypt_remove_key_arg arg;
	struct key *key;
	struct fscrypt_master_key *mk;
	u32 status_flags = 0;
	int err;
	bool dead;

	if (copy_from_user(&arg, uarg, sizeof(arg)))
		return -EFAULT;

	if (!valid_key_spec(&arg.key_spec))
		return -EINVAL;

	if (memchr_inv(arg.__reserved, 0, sizeof(arg.__reserved)))
		return -EINVAL;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	/* Find the key being removed. */
	key = fscrypt_find_master_key(sb, &arg.key_spec);
	if (IS_ERR(key))
		return PTR_ERR(key);
	mk = key->payload.data[0];

	down_write(&key->sem);

	/* Wipe the secret. */
	dead = false;
	if (is_master_key_secret_present(&mk->mk_secret)) {
		wipe_master_key_secret(&mk->mk_secret);
		dead = refcount_dec_and_test(&mk->mk_refcount);
	}
	up_write(&key->sem);
	if (dead) {
		/*
		 * No inodes reference the key, and we wiped the secret, so the
		 * key object is free to be removed from the keyring.
		 */
		key_invalidate(key);
		err = 0;
	} else {
		/* Some inodes still reference this key; try to evict them. */
		err = try_to_lock_encrypted_files(sb, mk);
		if (err == -EBUSY) {
			status_flags |=
				FSCRYPT_KEY_REMOVAL_STATUS_FLAG_FILES_BUSY;
			err = 0;
		}
	}
	/*
	 * We return 0 if we successfully did something: wiped the secret, or
	 * tried locking the files again.  Users need to check the informational
	 * status flags if they care whether the key has been fully removed
	 * including all files locked.
	 */
	key_put(key);
	if (err == 0)
		err = put_user(status_flags, &uarg->removal_status_flags);
	return err;
}
EXPORT_SYMBOL_GPL(fscrypt_ioctl_remove_key);

/*
 * Retrieve the status of an fscrypt master encryption key.
 *
 * We set ->status to indicate whether the key is absent, present, or
 * incompletely removed.  "Incompletely removed" means that the master key
 * secret has been removed, but some files which had been unlocked with it are
 * still in use.  This field allows applications to easily determine the state
 * of an encrypted directory without using a hack such as trying to open a
 * regular file in it (which can confuse the "incompletely removed" state with
 * absent or present).
 *
 * For more details, see the "FS_IOC_GET_ENCRYPTION_KEY_STATUS" section of
 * Documentation/filesystems/fscrypt.rst.
 */
int fscrypt_ioctl_get_key_status(struct file *filp, void __user *uarg)
{
	struct super_block *sb = file_inode(filp)->i_sb;
	struct fscrypt_get_key_status_arg arg;
	struct key *key;
	struct fscrypt_master_key *mk;
	int err;

	if (copy_from_user(&arg, uarg, sizeof(arg)))
		return -EFAULT;

	if (!valid_key_spec(&arg.key_spec))
		return -EINVAL;

	if (memchr_inv(arg.__reserved, 0, sizeof(arg.__reserved)))
		return -EINVAL;

	memset(arg.__out_reserved, 0, sizeof(arg.__out_reserved));

	key = fscrypt_find_master_key(sb, &arg.key_spec);
	if (IS_ERR(key)) {
		if (key != ERR_PTR(-ENOKEY))
			return PTR_ERR(key);
		arg.status = FSCRYPT_KEY_STATUS_ABSENT;
		err = 0;
		goto out;
	}
	mk = key->payload.data[0];
	down_read(&key->sem);

	if (!is_master_key_secret_present(&mk->mk_secret)) {
		arg.status = FSCRYPT_KEY_STATUS_INCOMPLETELY_REMOVED;
		err = 0;
		goto out_release_key;
	}

	arg.status = FSCRYPT_KEY_STATUS_PRESENT;
	err = 0;
out_release_key:
	up_read(&key->sem);
	key_put(key);
out:
	if (!err && copy_to_user(uarg, &arg, sizeof(arg)))
		err = -EFAULT;
	return err;
}
EXPORT_SYMBOL_GPL(fscrypt_ioctl_get_key_status);

int __init fscrypt_init_keyring(void)
{
	return register_key_type(&key_type_fscrypt);
}
