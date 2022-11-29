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

#include <asm/unaligned.h>
#include <crypto/skcipher.h>
#include <linux/key-type.h>
#include <linux/random.h>
#include <linux/seq_file.h>

#include "fscrypt_private.h"

/* The master encryption keys for a filesystem (->s_master_keys) */
struct fscrypt_keyring {
	/*
	 * Lock that protects ->key_hashtable.  It does *not* protect the
	 * fscrypt_master_key structs themselves.
	 */
	spinlock_t lock;

	/* Hash table that maps fscrypt_key_specifier to fscrypt_master_key */
	struct hlist_head key_hashtable[128];
};

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

static void fscrypt_free_master_key(struct rcu_head *head)
{
	struct fscrypt_master_key *mk =
		container_of(head, struct fscrypt_master_key, mk_rcu_head);
	/*
	 * The master key secret and any embedded subkeys should have already
	 * been wiped when the last active reference to the fscrypt_master_key
	 * struct was dropped; doing it here would be unnecessarily late.
	 * Nevertheless, use kfree_sensitive() in case anything was missed.
	 */
	kfree_sensitive(mk);
}

void fscrypt_put_master_key(struct fscrypt_master_key *mk)
{
	if (!refcount_dec_and_test(&mk->mk_struct_refs))
		return;
	/*
	 * No structural references left, so free ->mk_users, and also free the
	 * fscrypt_master_key struct itself after an RCU grace period ensures
	 * that concurrent keyring lookups can no longer find it.
	 */
	WARN_ON(refcount_read(&mk->mk_active_refs) != 0);
	key_put(mk->mk_users);
	mk->mk_users = NULL;
	call_rcu(&mk->mk_rcu_head, fscrypt_free_master_key);
}

void fscrypt_put_master_key_activeref(struct fscrypt_master_key *mk)
{
	struct super_block *sb = mk->mk_sb;
	struct fscrypt_keyring *keyring = sb->s_master_keys;
	size_t i;

	if (!refcount_dec_and_test(&mk->mk_active_refs))
		return;
	/*
	 * No active references left, so complete the full removal of this
	 * fscrypt_master_key struct by removing it from the keyring and
	 * destroying any subkeys embedded in it.
	 */

	spin_lock(&keyring->lock);
	hlist_del_rcu(&mk->mk_node);
	spin_unlock(&keyring->lock);

	/*
	 * ->mk_active_refs == 0 implies that ->mk_secret is not present and
	 * that ->mk_decrypted_inodes is empty.
	 */
	WARN_ON(is_master_key_secret_present(&mk->mk_secret));
	WARN_ON(!list_empty(&mk->mk_decrypted_inodes));

	for (i = 0; i <= FSCRYPT_MODE_MAX; i++) {
		fscrypt_destroy_prepared_key(&mk->mk_direct_keys[i]);
		fscrypt_destroy_prepared_key(&mk->mk_iv_ino_lblk_64_keys[i]);
		fscrypt_destroy_prepared_key(&mk->mk_iv_ino_lblk_32_keys[i]);
	}
	memzero_explicit(&mk->mk_ino_hash_key,
			 sizeof(mk->mk_ino_hash_key));
	mk->mk_ino_hash_key_initialized = false;

	/* Drop the structural ref associated with the active refs. */
	fscrypt_put_master_key(mk);
}

static inline bool valid_key_spec(const struct fscrypt_key_specifier *spec)
{
	if (spec->__reserved)
		return false;
	return master_key_spec_len(spec) != 0;
}

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

#define FSCRYPT_MK_USERS_DESCRIPTION_SIZE	\
	(CONST_STRLEN("fscrypt-") + 2 * FSCRYPT_KEY_IDENTIFIER_SIZE + \
	 CONST_STRLEN("-users") + 1)

#define FSCRYPT_MK_USER_DESCRIPTION_SIZE	\
	(2 * FSCRYPT_KEY_IDENTIFIER_SIZE + CONST_STRLEN(".uid.") + 10 + 1)

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
	struct fscrypt_keyring *keyring;

	if (sb->s_master_keys)
		return 0;

	keyring = kzalloc(sizeof(*keyring), GFP_KERNEL);
	if (!keyring)
		return -ENOMEM;
	spin_lock_init(&keyring->lock);
	/*
	 * Pairs with the smp_load_acquire() in fscrypt_find_master_key().
	 * I.e., here we publish ->s_master_keys with a RELEASE barrier so that
	 * concurrent tasks can ACQUIRE it.
	 */
	smp_store_release(&sb->s_master_keys, keyring);
	return 0;
}

/*
 * Release all encryption keys that have been added to the filesystem, along
 * with the keyring that contains them.
 *
 * This is called at unmount time.  The filesystem's underlying block device(s)
 * are still available at this time; this is important because after user file
 * accesses have been allowed, this function may need to evict keys from the
 * keyslots of an inline crypto engine, which requires the block device(s).
 *
 * This is also called when the super_block is being freed.  This is needed to
 * avoid a memory leak if mounting fails after the "test_dummy_encryption"
 * option was processed, as in that case the unmount-time call isn't made.
 */
void fscrypt_destroy_keyring(struct super_block *sb)
{
	struct fscrypt_keyring *keyring = sb->s_master_keys;
	size_t i;

	if (!keyring)
		return;

	for (i = 0; i < ARRAY_SIZE(keyring->key_hashtable); i++) {
		struct hlist_head *bucket = &keyring->key_hashtable[i];
		struct fscrypt_master_key *mk;
		struct hlist_node *tmp;

		hlist_for_each_entry_safe(mk, tmp, bucket, mk_node) {
			/*
			 * Since all inodes were already evicted, every key
			 * remaining in the keyring should have an empty inode
			 * list, and should only still be in the keyring due to
			 * the single active ref associated with ->mk_secret.
			 * There should be no structural refs beyond the one
			 * associated with the active ref.
			 */
			WARN_ON(refcount_read(&mk->mk_active_refs) != 1);
			WARN_ON(refcount_read(&mk->mk_struct_refs) != 1);
			WARN_ON(!is_master_key_secret_present(&mk->mk_secret));
			wipe_master_key_secret(&mk->mk_secret);
			fscrypt_put_master_key_activeref(mk);
		}
	}
	kfree_sensitive(keyring);
	sb->s_master_keys = NULL;
}

static struct hlist_head *
fscrypt_mk_hash_bucket(struct fscrypt_keyring *keyring,
		       const struct fscrypt_key_specifier *mk_spec)
{
	/*
	 * Since key specifiers should be "random" values, it is sufficient to
	 * use a trivial hash function that just takes the first several bits of
	 * the key specifier.
	 */
	unsigned long i = get_unaligned((unsigned long *)&mk_spec->u);

	return &keyring->key_hashtable[i % ARRAY_SIZE(keyring->key_hashtable)];
}

/*
 * Find the specified master key struct in ->s_master_keys and take a structural
 * ref to it.  The structural ref guarantees that the key struct continues to
 * exist, but it does *not* guarantee that ->s_master_keys continues to contain
 * the key struct.  The structural ref needs to be dropped by
 * fscrypt_put_master_key().  Returns NULL if the key struct is not found.
 */
struct fscrypt_master_key *
fscrypt_find_master_key(struct super_block *sb,
			const struct fscrypt_key_specifier *mk_spec)
{
	struct fscrypt_keyring *keyring;
	struct hlist_head *bucket;
	struct fscrypt_master_key *mk;

	/*
	 * Pairs with the smp_store_release() in allocate_filesystem_keyring().
	 * I.e., another task can publish ->s_master_keys concurrently,
	 * executing a RELEASE barrier.  We need to use smp_load_acquire() here
	 * to safely ACQUIRE the memory the other task published.
	 */
	keyring = smp_load_acquire(&sb->s_master_keys);
	if (keyring == NULL)
		return NULL; /* No keyring yet, so no keys yet. */

	bucket = fscrypt_mk_hash_bucket(keyring, mk_spec);
	rcu_read_lock();
	switch (mk_spec->type) {
	case FSCRYPT_KEY_SPEC_TYPE_DESCRIPTOR:
		hlist_for_each_entry_rcu(mk, bucket, mk_node) {
			if (mk->mk_spec.type ==
				FSCRYPT_KEY_SPEC_TYPE_DESCRIPTOR &&
			    memcmp(mk->mk_spec.u.descriptor,
				   mk_spec->u.descriptor,
				   FSCRYPT_KEY_DESCRIPTOR_SIZE) == 0 &&
			    refcount_inc_not_zero(&mk->mk_struct_refs))
				goto out;
		}
		break;
	case FSCRYPT_KEY_SPEC_TYPE_IDENTIFIER:
		hlist_for_each_entry_rcu(mk, bucket, mk_node) {
			if (mk->mk_spec.type ==
				FSCRYPT_KEY_SPEC_TYPE_IDENTIFIER &&
			    memcmp(mk->mk_spec.u.identifier,
				   mk_spec->u.identifier,
				   FSCRYPT_KEY_IDENTIFIER_SIZE) == 0 &&
			    refcount_inc_not_zero(&mk->mk_struct_refs))
				goto out;
		}
		break;
	}
	mk = NULL;
out:
	rcu_read_unlock();
	return mk;
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
	key_ref_t keyref;

	format_mk_user_description(description, mk->mk_spec.u.identifier);

	/*
	 * We need to mark the keyring reference as "possessed" so that we
	 * acquire permission to search it, via the KEY_POS_SEARCH permission.
	 */
	keyref = keyring_search(make_key_ref(mk->mk_users, true /*possessed*/),
				&key_type_fscrypt_user, description, false);
	if (IS_ERR(keyref)) {
		if (PTR_ERR(keyref) == -EAGAIN || /* not found */
		    PTR_ERR(keyref) == -EKEYREVOKED) /* recently invalidated */
			keyref = ERR_PTR(-ENOKEY);
		return ERR_CAST(keyref);
	}
	return key_ref_to_ptr(keyref);
}

/*
 * Give the current user a "key" in ->mk_users.  This charges the user's quota
 * and marks the master key as added by the current user, so that it cannot be
 * removed by another user with the key.  Either ->mk_sem must be held for
 * write, or the master key must be still undergoing initialization.
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
 * ->mk_sem must be held for write.
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
 * Allocate a new fscrypt_master_key, transfer the given secret over to it, and
 * insert it into sb->s_master_keys.
 */
static int add_new_master_key(struct super_block *sb,
			      struct fscrypt_master_key_secret *secret,
			      const struct fscrypt_key_specifier *mk_spec)
{
	struct fscrypt_keyring *keyring = sb->s_master_keys;
	struct fscrypt_master_key *mk;
	int err;

	mk = kzalloc(sizeof(*mk), GFP_KERNEL);
	if (!mk)
		return -ENOMEM;

	mk->mk_sb = sb;
	init_rwsem(&mk->mk_sem);
	refcount_set(&mk->mk_struct_refs, 1);
	mk->mk_spec = *mk_spec;

	INIT_LIST_HEAD(&mk->mk_decrypted_inodes);
	spin_lock_init(&mk->mk_decrypted_inodes_lock);

	if (mk_spec->type == FSCRYPT_KEY_SPEC_TYPE_IDENTIFIER) {
		err = allocate_master_key_users_keyring(mk);
		if (err)
			goto out_put;
		err = add_master_key_user(mk);
		if (err)
			goto out_put;
	}

	move_master_key_secret(&mk->mk_secret, secret);
	refcount_set(&mk->mk_active_refs, 1); /* ->mk_secret is present */

	spin_lock(&keyring->lock);
	hlist_add_head_rcu(&mk->mk_node,
			   fscrypt_mk_hash_bucket(keyring, mk_spec));
	spin_unlock(&keyring->lock);
	return 0;

out_put:
	fscrypt_put_master_key(mk);
	return err;
}

#define KEY_DEAD	1

static int add_existing_master_key(struct fscrypt_master_key *mk,
				   struct fscrypt_master_key_secret *secret)
{
	int err;

	/*
	 * If the current user is already in ->mk_users, then there's nothing to
	 * do.  Otherwise, we need to add the user to ->mk_users.  (Neither is
	 * applicable for v1 policy keys, which have NULL ->mk_users.)
	 */
	if (mk->mk_users) {
		struct key *mk_user = find_master_key_user(mk);

		if (mk_user != ERR_PTR(-ENOKEY)) {
			if (IS_ERR(mk_user))
				return PTR_ERR(mk_user);
			key_put(mk_user);
			return 0;
		}
		err = add_master_key_user(mk);
		if (err)
			return err;
	}

	/* Re-add the secret if needed. */
	if (!is_master_key_secret_present(&mk->mk_secret)) {
		if (!refcount_inc_not_zero(&mk->mk_active_refs))
			return KEY_DEAD;
		move_master_key_secret(&mk->mk_secret, secret);
	}

	return 0;
}

static int do_add_master_key(struct super_block *sb,
			     struct fscrypt_master_key_secret *secret,
			     const struct fscrypt_key_specifier *mk_spec)
{
	static DEFINE_MUTEX(fscrypt_add_key_mutex);
	struct fscrypt_master_key *mk;
	int err;

	mutex_lock(&fscrypt_add_key_mutex); /* serialize find + link */

	mk = fscrypt_find_master_key(sb, mk_spec);
	if (!mk) {
		/* Didn't find the key in ->s_master_keys.  Add it. */
		err = allocate_filesystem_keyring(sb);
		if (!err)
			err = add_new_master_key(sb, secret, mk_spec);
	} else {
		/*
		 * Found the key in ->s_master_keys.  Re-add the secret if
		 * needed, and add the user to ->mk_users if needed.
		 */
		down_write(&mk->mk_sem);
		err = add_existing_master_key(mk, secret);
		up_write(&mk->mk_sem);
		if (err == KEY_DEAD) {
			/*
			 * We found a key struct, but it's already been fully
			 * removed.  Ignore the old struct and add a new one.
			 * fscrypt_add_key_mutex means we don't need to worry
			 * about concurrent adds.
			 */
			err = add_new_master_key(sb, secret, mk_spec);
		}
		fscrypt_put_master_key(mk);
	}
	mutex_unlock(&fscrypt_add_key_mutex);
	return err;
}

/* Size of software "secret" derived from hardware-wrapped key */
#define RAW_SECRET_SIZE 32

static int add_master_key(struct super_block *sb,
			  struct fscrypt_master_key_secret *secret,
			  struct fscrypt_key_specifier *key_spec)
{
	int err;

	if (key_spec->type == FSCRYPT_KEY_SPEC_TYPE_IDENTIFIER) {
		u8 _kdf_key[RAW_SECRET_SIZE];
		u8 *kdf_key = secret->raw;
		unsigned int kdf_key_size = secret->size;

		if (secret->is_hw_wrapped) {
			kdf_key = _kdf_key;
			kdf_key_size = RAW_SECRET_SIZE;
			err = fscrypt_derive_raw_secret(sb, secret->raw,
							secret->size,
							kdf_key, kdf_key_size);
			if (err)
				return err;
		}
		err = fscrypt_init_hkdf(&secret->hkdf, kdf_key, kdf_key_size);
		/*
		 * Now that the HKDF context is initialized, the raw HKDF key is
		 * no longer needed.
		 */
		memzero_explicit(kdf_key, kdf_key_size);
		if (err)
			return err;

		/* Calculate the key identifier */
		err = fscrypt_hkdf_expand(&secret->hkdf,
					  HKDF_CONTEXT_KEY_IDENTIFIER, NULL, 0,
					  key_spec->u.identifier,
					  FSCRYPT_KEY_IDENTIFIER_SIZE);
		if (err)
			return err;
	}
	return do_add_master_key(sb, secret, key_spec);
}

static int fscrypt_provisioning_key_preparse(struct key_preparsed_payload *prep)
{
	const struct fscrypt_provisioning_key_payload *payload = prep->data;

	BUILD_BUG_ON(FSCRYPT_MAX_HW_WRAPPED_KEY_SIZE < FSCRYPT_MAX_KEY_SIZE);

	if (prep->datalen < sizeof(*payload) + FSCRYPT_MIN_KEY_SIZE ||
	    prep->datalen > sizeof(*payload) + FSCRYPT_MAX_HW_WRAPPED_KEY_SIZE)
		return -EINVAL;

	if (payload->type != FSCRYPT_KEY_SPEC_TYPE_DESCRIPTOR &&
	    payload->type != FSCRYPT_KEY_SPEC_TYPE_IDENTIFIER)
		return -EINVAL;

	if (payload->__reserved)
		return -EINVAL;

	prep->payload.data[0] = kmemdup(payload, prep->datalen, GFP_KERNEL);
	if (!prep->payload.data[0])
		return -ENOMEM;

	prep->quotalen = prep->datalen;
	return 0;
}

static void fscrypt_provisioning_key_free_preparse(
					struct key_preparsed_payload *prep)
{
	kfree_sensitive(prep->payload.data[0]);
}

static void fscrypt_provisioning_key_describe(const struct key *key,
					      struct seq_file *m)
{
	seq_puts(m, key->description);
	if (key_is_positive(key)) {
		const struct fscrypt_provisioning_key_payload *payload =
			key->payload.data[0];

		seq_printf(m, ": %u [%u]", key->datalen, payload->type);
	}
}

static void fscrypt_provisioning_key_destroy(struct key *key)
{
	kfree_sensitive(key->payload.data[0]);
}

static struct key_type key_type_fscrypt_provisioning = {
	.name			= "fscrypt-provisioning",
	.preparse		= fscrypt_provisioning_key_preparse,
	.free_preparse		= fscrypt_provisioning_key_free_preparse,
	.instantiate		= generic_key_instantiate,
	.describe		= fscrypt_provisioning_key_describe,
	.destroy		= fscrypt_provisioning_key_destroy,
};

/*
 * Retrieve the raw key from the Linux keyring key specified by 'key_id', and
 * store it into 'secret'.
 *
 * The key must be of type "fscrypt-provisioning" and must have the field
 * fscrypt_provisioning_key_payload::type set to 'type', indicating that it's
 * only usable with fscrypt with the particular KDF version identified by
 * 'type'.  We don't use the "logon" key type because there's no way to
 * completely restrict the use of such keys; they can be used by any kernel API
 * that accepts "logon" keys and doesn't require a specific service prefix.
 *
 * The ability to specify the key via Linux keyring key is intended for cases
 * where userspace needs to re-add keys after the filesystem is unmounted and
 * re-mounted.  Most users should just provide the raw key directly instead.
 */
static int get_keyring_key(u32 key_id, u32 type,
			   struct fscrypt_master_key_secret *secret)
{
	key_ref_t ref;
	struct key *key;
	const struct fscrypt_provisioning_key_payload *payload;
	int err;

	ref = lookup_user_key(key_id, 0, KEY_NEED_SEARCH);
	if (IS_ERR(ref))
		return PTR_ERR(ref);
	key = key_ref_to_ptr(ref);

	if (key->type != &key_type_fscrypt_provisioning)
		goto bad_key;
	payload = key->payload.data[0];

	/* Don't allow fscrypt v1 keys to be used as v2 keys and vice versa. */
	if (payload->type != type)
		goto bad_key;

	secret->size = key->datalen - sizeof(*payload);
	memcpy(secret->raw, payload->raw, secret->size);
	err = 0;
	goto out_put;

bad_key:
	err = -EKEYREJECTED;
out_put:
	key_ref_put(ref);
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

	if (memchr_inv(arg.__reserved, 0, sizeof(arg.__reserved)))
		return -EINVAL;

	/*
	 * Only root can add keys that are identified by an arbitrary descriptor
	 * rather than by a cryptographic hash --- since otherwise a malicious
	 * user could add the wrong key.
	 */
	if (arg.key_spec.type == FSCRYPT_KEY_SPEC_TYPE_DESCRIPTOR &&
	    !capable(CAP_SYS_ADMIN))
		return -EACCES;

	memset(&secret, 0, sizeof(secret));

	if (arg.__flags) {
		if (arg.__flags & ~__FSCRYPT_ADD_KEY_FLAG_HW_WRAPPED)
			return -EINVAL;
		if (arg.key_spec.type != FSCRYPT_KEY_SPEC_TYPE_IDENTIFIER)
			return -EINVAL;
		secret.is_hw_wrapped = true;
	}

	if (arg.key_id) {
		if (arg.raw_size != 0)
			return -EINVAL;
		err = get_keyring_key(arg.key_id, arg.key_spec.type, &secret);
		if (err)
			goto out_wipe_secret;
		err = -EINVAL;
		if (secret.size > FSCRYPT_MAX_KEY_SIZE && !secret.is_hw_wrapped)
			goto out_wipe_secret;
	} else {
		if (arg.raw_size < FSCRYPT_MIN_KEY_SIZE ||
		    arg.raw_size > (secret.is_hw_wrapped ?
				    FSCRYPT_MAX_HW_WRAPPED_KEY_SIZE :
				    FSCRYPT_MAX_KEY_SIZE))
			return -EINVAL;
		secret.size = arg.raw_size;
		err = -EFAULT;
		if (copy_from_user(secret.raw, uarg->raw, secret.size))
			goto out_wipe_secret;
	}

	err = add_master_key(sb, &secret, &arg.key_spec);
	if (err)
		goto out_wipe_secret;

	/* Return the key identifier to userspace, if applicable */
	err = -EFAULT;
	if (arg.key_spec.type == FSCRYPT_KEY_SPEC_TYPE_IDENTIFIER &&
	    copy_to_user(uarg->key_spec.u.identifier, arg.key_spec.u.identifier,
			 FSCRYPT_KEY_IDENTIFIER_SIZE))
		goto out_wipe_secret;
	err = 0;
out_wipe_secret:
	wipe_master_key_secret(&secret);
	return err;
}
EXPORT_SYMBOL_GPL(fscrypt_ioctl_add_key);

/*
 * Add the key for '-o test_dummy_encryption' to the filesystem keyring.
 *
 * Use a per-boot random key to prevent people from misusing this option.
 */
int fscrypt_add_test_dummy_key(struct super_block *sb,
			       struct fscrypt_key_specifier *key_spec)
{
	static u8 test_key[FSCRYPT_MAX_KEY_SIZE];
	struct fscrypt_master_key_secret secret;
	int err;

	get_random_once(test_key, FSCRYPT_MAX_KEY_SIZE);

	memset(&secret, 0, sizeof(secret));
	secret.size = FSCRYPT_MAX_KEY_SIZE;
	memcpy(secret.raw, test_key, FSCRYPT_MAX_KEY_SIZE);

	err = add_master_key(sb, &secret, key_spec);
	wipe_master_key_secret(&secret);
	return err;
}

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
	struct fscrypt_master_key *mk;
	struct key *mk_user;
	int err;

	mk_spec.type = FSCRYPT_KEY_SPEC_TYPE_IDENTIFIER;
	memcpy(mk_spec.u.identifier, identifier, FSCRYPT_KEY_IDENTIFIER_SIZE);

	mk = fscrypt_find_master_key(sb, &mk_spec);
	if (!mk) {
		err = -ENOKEY;
		goto out;
	}
	down_read(&mk->mk_sem);
	mk_user = find_master_key_user(mk);
	if (IS_ERR(mk_user)) {
		err = PTR_ERR(mk_user);
	} else {
		key_put(mk_user);
		err = 0;
	}
	up_read(&mk->mk_sem);
	fscrypt_put_master_key(mk);
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
	char ino_str[50] = "";

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
	}
	spin_unlock(&mk->mk_decrypted_inodes_lock);

	/* If the inode is currently being created, ino may still be 0. */
	if (ino)
		snprintf(ino_str, sizeof(ino_str), ", including ino %lu", ino);

	fscrypt_warn(NULL,
		     "%s: %zu inode(s) still busy after removing key with %s %*phN%s",
		     sb->s_id, busy_count, master_key_spec_type(&mk->mk_spec),
		     master_key_spec_len(&mk->mk_spec), (u8 *)&mk->mk_spec.u,
		     ino_str);
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
	struct fscrypt_master_key *mk;
	u32 status_flags = 0;
	int err;
	bool inodes_remain;

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
	mk = fscrypt_find_master_key(sb, &arg.key_spec);
	if (!mk)
		return -ENOKEY;
	down_write(&mk->mk_sem);

	/* If relevant, remove current user's (or all users) claim to the key */
	if (mk->mk_users && mk->mk_users->keys.nr_leaves_on_tree != 0) {
		if (all_users)
			err = keyring_clear(mk->mk_users);
		else
			err = remove_master_key_user(mk);
		if (err) {
			up_write(&mk->mk_sem);
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
			up_write(&mk->mk_sem);
			goto out_put_key;
		}
	}

	/* No user claims remaining.  Go ahead and wipe the secret. */
	err = -ENOKEY;
	if (is_master_key_secret_present(&mk->mk_secret)) {
		wipe_master_key_secret(&mk->mk_secret);
		fscrypt_put_master_key_activeref(mk);
		err = 0;
	}
	inodes_remain = refcount_read(&mk->mk_active_refs) > 0;
	up_write(&mk->mk_sem);

	if (inodes_remain) {
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
	fscrypt_put_master_key(mk);
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

	mk = fscrypt_find_master_key(sb, &arg.key_spec);
	if (!mk) {
		arg.status = FSCRYPT_KEY_STATUS_ABSENT;
		err = 0;
		goto out;
	}
	down_read(&mk->mk_sem);

	if (!is_master_key_secret_present(&mk->mk_secret)) {
		arg.status = refcount_read(&mk->mk_active_refs) > 0 ?
			FSCRYPT_KEY_STATUS_INCOMPLETELY_REMOVED :
			FSCRYPT_KEY_STATUS_ABSENT /* raced with full removal */;
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
	up_read(&mk->mk_sem);
	fscrypt_put_master_key(mk);
out:
	if (!err && copy_to_user(uarg, &arg, sizeof(arg)))
		err = -EFAULT;
	return err;
}
EXPORT_SYMBOL_GPL(fscrypt_ioctl_get_key_status);

int __init fscrypt_init_keyring(void)
{
	int err;

	err = register_key_type(&key_type_fscrypt_user);
	if (err)
		return err;

	err = register_key_type(&key_type_fscrypt_provisioning);
	if (err)
		goto err_unregister_fscrypt_user;

	return 0;

err_unregister_fscrypt_user:
	unregister_key_type(&key_type_fscrypt_user);
	return err;
}
