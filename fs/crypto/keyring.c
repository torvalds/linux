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
 * - FS_IOC_REMOVE_ENCRYPTION_KEY_ALL_USERS
 * - FS_IOC_GET_ENCRYPTION_KEY_STATUS
 *
 * See the "User API" section of Documentation/filesystems/fscrypt.rst for more
 * information about these ioctls.
 */

#include <crypto/skcipher.h>
#include <linux/key-type.h>
#include <linux/seq_file.h>

#include "fscrypt_private.h"

static void wipe_master_key_secret(struct fscrypt_master_key_secret *secret)
{
	fscrypt_destroy_hkdf(&secret->hkdf);
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
	size_t i;

	wipe_master_key_secret(&mk->mk_secret);

	for (i = 0; i <= __FSCRYPT_MODE_MAX; i++) {
		crypto_free_skcipher(mk->mk_direct_tfms[i]);
		crypto_free_skcipher(mk->mk_iv_ino_lblk_64_tfms[i]);
	}

	key_put(mk->mk_users);
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

static int fscrypt_user_key_instantiate(struct key *key,
					struct key_preparsed_payload *prep)
{
	/*
	 * We just charge FSCRYPT_MAX_KEY_SIZE bytes to the user's key quota for
	 * each key, regardless of the exact key size.  The amount of memory
	 * actually used is greater than the size of the raw key anyway.
	 */
	return key_payload_reserve(key, FSCRYPT_MAX_KEY_SIZE);
}

static void fscrypt_user_key_describe(const struct key *key, struct seq_file *m)
{
	seq_puts(m, key->description);
}

/*
 * Type of key in ->mk_users.  Each key of this type represents a particular
 * user who has added a particular master key.
 *
 * Note that the name of this key type really should be something like
 * ".fscrypt-user" instead of simply ".fscrypt".  But the shorter name is chosen
 * mainly for simplicity of presentation in /proc/keys when read by a non-root
 * user.  And it is expected to be rare that a key is actually added by multiple
 * users, since users should keep their encryption keys confidential.
 */
static struct key_type key_type_fscrypt_user = {
	.name			= ".fscrypt",
	.instantiate		= fscrypt_user_key_instantiate,
	.describe		= fscrypt_user_key_describe,
};

/* Search ->s_master_keys or ->mk_users */
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
	(CONST_STRLEN("fscrypt-") + sizeof_field(struct super_block, s_id))

#define FSCRYPT_MK_DESCRIPTION_SIZE	(2 * FSCRYPT_KEY_IDENTIFIER_SIZE + 1)

#define FSCRYPT_MK_USERS_DESCRIPTION_SIZE	\
	(CONST_STRLEN("fscrypt-") + 2 * FSCRYPT_KEY_IDENTIFIER_SIZE + \
	 CONST_STRLEN("-users") + 1)

#define FSCRYPT_MK_USER_DESCRIPTION_SIZE	\
	(2 * FSCRYPT_KEY_IDENTIFIER_SIZE + CONST_STRLEN(".uid.") + 10 + 1)

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

static void format_mk_users_keyring_description(
			char description[FSCRYPT_MK_USERS_DESCRIPTION_SIZE],
			const u8 mk_identifier[FSCRYPT_KEY_IDENTIFIER_SIZE])
{
	sprintf(description, "fscrypt-%*phN-users",
		FSCRYPT_KEY_IDENTIFIER_SIZE, mk_identifier);
}

static void format_mk_user_description(
			char description[FSCRYPT_MK_USER_DESCRIPTION_SIZE],
			const u8 mk_identifier[FSCRYPT_KEY_IDENTIFIER_SIZE])
{

	sprintf(description, "%*phN.uid.%u", FSCRYPT_KEY_IDENTIFIER_SIZE,
		mk_identifier, __kuid_val(current_fsuid()));
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

static int allocate_master_key_users_keyring(struct fscrypt_master_key *mk)
{
	char description[FSCRYPT_MK_USERS_DESCRIPTION_SIZE];
	struct key *keyring;

	format_mk_users_keyring_description(description,
					    mk->mk_spec.u.identifier);
	keyring = keyring_alloc(description, GLOBAL_ROOT_UID, GLOBAL_ROOT_GID,
				current_cred(), KEY_POS_SEARCH |
				  KEY_USR_SEARCH | KEY_USR_READ | KEY_USR_VIEW,
				KEY_ALLOC_NOT_IN_QUOTA, NULL, NULL);
	if (IS_ERR(keyring))
		return PTR_ERR(keyring);

	mk->mk_users = keyring;
	return 0;
}

/*
 * Find the current user's "key" in the master key's ->mk_users.
 * Returns ERR_PTR(-ENOKEY) if not found.
 */
static struct key *find_master_key_user(struct fscrypt_master_key *mk)
{
	char description[FSCRYPT_MK_USER_DESCRIPTION_SIZE];

	format_mk_user_description(description, mk->mk_spec.u.identifier);
	return search_fscrypt_keyring(mk->mk_users, &key_type_fscrypt_user,
				      description);
}

/*
 * Give the current user a "key" in ->mk_users.  This charges the user's quota
 * and marks the master key as added by the current user, so that it cannot be
 * removed by another user with the key.  Either the master key's key->sem must
 * be held for write, or the master key must be still undergoing initialization.
 */
static int add_master_key_user(struct fscrypt_master_key *mk)
{
	char description[FSCRYPT_MK_USER_DESCRIPTION_SIZE];
	struct key *mk_user;
	int err;

	format_mk_user_description(description, mk->mk_spec.u.identifier);
	mk_user = key_alloc(&key_type_fscrypt_user, description,
			    current_fsuid(), current_gid(), current_cred(),
			    KEY_POS_SEARCH | KEY_USR_VIEW, 0, NULL);
	if (IS_ERR(mk_user))
		return PTR_ERR(mk_user);

	err = key_instantiate_and_link(mk_user, NULL, 0, mk->mk_users, NULL);
	key_put(mk_user);
	return err;
}

/*
 * Remove the current user's "key" from ->mk_users.
 * The master key's key->sem must be held for write.
 *
 * Returns 0 if removed, -ENOKEY if not found, or another -errno code.
 */
static int remove_master_key_user(struct fscrypt_master_key *mk)
{
	struct key *mk_user;
	int err;

	mk_user = find_master_key_user(mk);
	if (IS_ERR(mk_user))
		return PTR_ERR(mk_user);
	err = key_unlink(mk->mk_users, mk_user);
	key_put(mk_user);
	return err;
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
	init_rwsem(&mk->mk_secret_sem);

	refcount_set(&mk->mk_refcount, 1); /* secret is present */
	INIT_LIST_HEAD(&mk->mk_decrypted_inodes);
	spin_lock_init(&mk->mk_decrypted_inodes_lock);

	if (mk_spec->type == FSCRYPT_KEY_SPEC_TYPE_IDENTIFIER) {
		err = allocate_master_key_users_keyring(mk);
		if (err)
			goto out_free_mk;
		err = add_master_key_user(mk);
		if (err)
			goto out_free_mk;
	}

	/*
	 * Note that we don't charge this key to anyone's quota, since when
	 * ->mk_users is in use those keys are charged instead, and otherwise
	 * (when ->mk_users isn't in use) only root can add these keys.
	 */
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
	struct key *mk_user;
	bool rekey;
	int err;

	/*
	 * If the current user is already in ->mk_users, then there's nothing to
	 * do.  (Not applicable for v1 policy keys, which have NULL ->mk_users.)
	 */
	if (mk->mk_users) {
		mk_user = find_master_key_user(mk);
		if (mk_user != ERR_PTR(-ENOKEY)) {
			if (IS_ERR(mk_user))
				return PTR_ERR(mk_user);
			key_put(mk_user);
			return 0;
		}
	}

	/* If we'll be re-adding ->mk_secret, try to take the reference. */
	rekey = !is_master_key_secret_present(&mk->mk_secret);
	if (rekey && !refcount_inc_not_zero(&mk->mk_refcount))
		return KEY_DEAD;

	/* Add the current user to ->mk_users, if applicable. */
	if (mk->mk_users) {
		err = add_master_key_user(mk);
		if (err) {
			if (rekey && refcount_dec_and_test(&mk->mk_refcount))
				return KEY_DEAD;
			return err;
		}
	}

	/* Re-add the secret if needed. */
	if (rekey) {
		down_write(&mk->mk_secret_sem);
		move_master_key_secret(&mk->mk_secret, secret);
		up_write(&mk->mk_secret_sem);
	}
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
		 * needed, and add the user to ->mk_users if needed.
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
 * When adding a key for use by v1 encryption policies, this ioctl is
 * privileged, and userspace must provide the 'key_descriptor'.
 *
 * When adding a key for use by v2+ encryption policies, this ioctl is
 * unprivileged.  This is needed, in general, to allow non-root users to use
 * encryption without encountering the visibility problems of process-subscribed
 * keyrings and the inability to properly remove keys.  This works by having
 * each key identified by its cryptographically secure hash --- the
 * 'key_identifier'.  The cryptographic hash ensures that a malicious user
 * cannot add the wrong key for a given identifier.  Furthermore, each added key
 * is charged to the appropriate user's quota for the keyrings service, which
 * prevents a malicious user from adding too many keys.  Finally, we forbid a
 * user from removing a key while other users have added it too, which prevents
 * a user who knows another user's key from causing a denial-of-service by
 * removing it at an inopportune time.  (We tolerate that a user who knows a key
 * can prevent other users from removing it.)
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

	switch (arg.key_spec.type) {
	case FSCRYPT_KEY_SPEC_TYPE_DESCRIPTOR:
		/*
		 * Only root can add keys that are identified by an arbitrary
		 * descriptor rather than by a cryptographic hash --- since
		 * otherwise a malicious user could add the wrong key.
		 */
		err = -EACCES;
		if (!capable(CAP_SYS_ADMIN))
			goto out_wipe_secret;
		break;
	case FSCRYPT_KEY_SPEC_TYPE_IDENTIFIER:
		err = fscrypt_init_hkdf(&secret.hkdf, secret.raw, secret.size);
		if (err)
			goto out_wipe_secret;

		/*
		 * Now that the HKDF context is initialized, the raw key is no
		 * longer needed.
		 */
		memzero_explicit(secret.raw, secret.size);

		/* Calculate the key identifier and return it to userspace. */
		err = fscrypt_hkdf_expand(&secret.hkdf,
					  HKDF_CONTEXT_KEY_IDENTIFIER,
					  NULL, 0, arg.key_spec.u.identifier,
					  FSCRYPT_KEY_IDENTIFIER_SIZE);
		if (err)
			goto out_wipe_secret;
		err = -EFAULT;
		if (copy_to_user(uarg->key_spec.u.identifier,
				 arg.key_spec.u.identifier,
				 FSCRYPT_KEY_IDENTIFIER_SIZE))
			goto out_wipe_secret;
		break;
	default:
		WARN_ON(1);
		err = -EINVAL;
		goto out_wipe_secret;
	}

	err = add_master_key(sb, &secret, &arg.key_spec);
out_wipe_secret:
	wipe_master_key_secret(&secret);
	return err;
}
EXPORT_SYMBOL_GPL(fscrypt_ioctl_add_key);

/*
 * Verify that the current user has added a master key with the given identifier
 * (returns -ENOKEY if not).  This is needed to prevent a user from encrypting
 * their files using some other user's key which they don't actually know.
 * Cryptographically this isn't much of a problem, but the semantics of this
 * would be a bit weird, so it's best to just forbid it.
 *
 * The system administrator (CAP_FOWNER) can override this, which should be
 * enough for any use cases where encryption policies are being set using keys
 * that were chosen ahead of time but aren't available at the moment.
 *
 * Note that the key may have already removed by the time this returns, but
 * that's okay; we just care whether the key was there at some point.
 *
 * Return: 0 if the key is added, -ENOKEY if it isn't, or another -errno code
 */
int fscrypt_verify_key_added(struct super_block *sb,
			     const u8 identifier[FSCRYPT_KEY_IDENTIFIER_SIZE])
{
	struct fscrypt_key_specifier mk_spec;
	struct key *key, *mk_user;
	struct fscrypt_master_key *mk;
	int err;

	mk_spec.type = FSCRYPT_KEY_SPEC_TYPE_IDENTIFIER;
	memcpy(mk_spec.u.identifier, identifier, FSCRYPT_KEY_IDENTIFIER_SIZE);

	key = fscrypt_find_master_key(sb, &mk_spec);
	if (IS_ERR(key)) {
		err = PTR_ERR(key);
		goto out;
	}
	mk = key->payload.data[0];
	mk_user = find_master_key_user(mk);
	if (IS_ERR(mk_user)) {
		err = PTR_ERR(mk_user);
	} else {
		key_put(mk_user);
		err = 0;
	}
	key_put(key);
out:
	if (err == -ENOKEY && capable(CAP_FOWNER))
		err = 0;
	return err;
}

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
 * FS_IOC_REMOVE_ENCRYPTION_KEY (all_users=false) removes the current user's
 * claim to the key, then removes the key itself if no other users have claims.
 * FS_IOC_REMOVE_ENCRYPTION_KEY_ALL_USERS (all_users=true) always removes the
 * key itself.
 *
 * To "remove the key itself", first we wipe the actual master key secret, so
 * that no more inodes can be unlocked with it.  Then we try to evict all cached
 * inodes that had been unlocked with the key.
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
static int do_remove_key(struct file *filp, void __user *_uarg, bool all_users)
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

	/*
	 * Only root can add and remove keys that are identified by an arbitrary
	 * descriptor rather than by a cryptographic hash.
	 */
	if (arg.key_spec.type == FSCRYPT_KEY_SPEC_TYPE_DESCRIPTOR &&
	    !capable(CAP_SYS_ADMIN))
		return -EACCES;

	/* Find the key being removed. */
	key = fscrypt_find_master_key(sb, &arg.key_spec);
	if (IS_ERR(key))
		return PTR_ERR(key);
	mk = key->payload.data[0];

	down_write(&key->sem);

	/* If relevant, remove current user's (or all users) claim to the key */
	if (mk->mk_users && mk->mk_users->keys.nr_leaves_on_tree != 0) {
		if (all_users)
			err = keyring_clear(mk->mk_users);
		else
			err = remove_master_key_user(mk);
		if (err) {
			up_write(&key->sem);
			goto out_put_key;
		}
		if (mk->mk_users->keys.nr_leaves_on_tree != 0) {
			/*
			 * Other users have still added the key too.  We removed
			 * the current user's claim to the key, but we still
			 * can't remove the key itself.
			 */
			status_flags |=
				FSCRYPT_KEY_REMOVAL_STATUS_FLAG_OTHER_USERS;
			err = 0;
			up_write(&key->sem);
			goto out_put_key;
		}
	}

	/* No user claims remaining.  Go ahead and wipe the secret. */
	dead = false;
	if (is_master_key_secret_present(&mk->mk_secret)) {
		down_write(&mk->mk_secret_sem);
		wipe_master_key_secret(&mk->mk_secret);
		dead = refcount_dec_and_test(&mk->mk_refcount);
		up_write(&mk->mk_secret_sem);
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
	 * We return 0 if we successfully did something: removed a claim to the
	 * key, wiped the secret, or tried locking the files again.  Users need
	 * to check the informational status flags if they care whether the key
	 * has been fully removed including all files locked.
	 */
out_put_key:
	key_put(key);
	if (err == 0)
		err = put_user(status_flags, &uarg->removal_status_flags);
	return err;
}

int fscrypt_ioctl_remove_key(struct file *filp, void __user *uarg)
{
	return do_remove_key(filp, uarg, false);
}
EXPORT_SYMBOL_GPL(fscrypt_ioctl_remove_key);

int fscrypt_ioctl_remove_key_all_users(struct file *filp, void __user *uarg)
{
	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;
	return do_remove_key(filp, uarg, true);
}
EXPORT_SYMBOL_GPL(fscrypt_ioctl_remove_key_all_users);

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
 * In addition, for v2 policy keys we allow applications to determine, via
 * ->status_flags and ->user_count, whether the key has been added by the
 * current user, by other users, or by both.  Most applications should not need
 * this, since ordinarily only one user should know a given key.  However, if a
 * secret key is shared by multiple users, applications may wish to add an
 * already-present key to prevent other users from removing it.  This ioctl can
 * be used to check whether that really is the case before the work is done to
 * add the key --- which might e.g. require prompting the user for a passphrase.
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

	arg.status_flags = 0;
	arg.user_count = 0;
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
	if (mk->mk_users) {
		struct key *mk_user;

		arg.user_count = mk->mk_users->keys.nr_leaves_on_tree;
		mk_user = find_master_key_user(mk);
		if (!IS_ERR(mk_user)) {
			arg.status_flags |=
				FSCRYPT_KEY_STATUS_FLAG_ADDED_BY_SELF;
			key_put(mk_user);
		} else if (mk_user != ERR_PTR(-ENOKEY)) {
			err = PTR_ERR(mk_user);
			goto out_release_key;
		}
	}
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
	int err;

	err = register_key_type(&key_type_fscrypt);
	if (err)
		return err;

	err = register_key_type(&key_type_fscrypt_user);
	if (err)
		goto err_unregister_fscrypt;

	return 0;

err_unregister_fscrypt:
	unregister_key_type(&key_type_fscrypt);
	return err;
}
