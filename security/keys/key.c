/* Basic authentication token and access key management
 *
 * Copyright (C) 2004-2008 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/poison.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/security.h>
#include <linux/workqueue.h>
#include <linux/random.h>
#include <linux/err.h>
#include "internal.h"

struct kmem_cache *key_jar;
struct rb_root		key_serial_tree; /* tree of keys indexed by serial */
DEFINE_SPINLOCK(key_serial_lock);

struct rb_root	key_user_tree; /* tree of quota records indexed by UID */
DEFINE_SPINLOCK(key_user_lock);

unsigned int key_quota_root_maxkeys = 200;	/* root's key count quota */
unsigned int key_quota_root_maxbytes = 20000;	/* root's key space quota */
unsigned int key_quota_maxkeys = 200;		/* general key count quota */
unsigned int key_quota_maxbytes = 20000;	/* general key space quota */

static LIST_HEAD(key_types_list);
static DECLARE_RWSEM(key_types_sem);

/* We serialise key instantiation and link */
DEFINE_MUTEX(key_construction_mutex);

#ifdef KEY_DEBUGGING
void __key_check(const struct key *key)
{
	printk("__key_check: key %p {%08x} should be {%08x}\n",
	       key, key->magic, KEY_DEBUG_MAGIC);
	BUG();
}
#endif

/*
 * Get the key quota record for a user, allocating a new record if one doesn't
 * already exist.
 */
struct key_user *key_user_lookup(kuid_t uid)
{
	struct key_user *candidate = NULL, *user;
	struct rb_node *parent = NULL;
	struct rb_node **p;

try_again:
	p = &key_user_tree.rb_node;
	spin_lock(&key_user_lock);

	/* search the tree for a user record with a matching UID */
	while (*p) {
		parent = *p;
		user = rb_entry(parent, struct key_user, node);

		if (uid_lt(uid, user->uid))
			p = &(*p)->rb_left;
		else if (uid_gt(uid, user->uid))
			p = &(*p)->rb_right;
		else
			goto found;
	}

	/* if we get here, we failed to find a match in the tree */
	if (!candidate) {
		/* allocate a candidate user record if we don't already have
		 * one */
		spin_unlock(&key_user_lock);

		user = NULL;
		candidate = kmalloc(sizeof(struct key_user), GFP_KERNEL);
		if (unlikely(!candidate))
			goto out;

		/* the allocation may have scheduled, so we need to repeat the
		 * search lest someone else added the record whilst we were
		 * asleep */
		goto try_again;
	}

	/* if we get here, then the user record still hadn't appeared on the
	 * second pass - so we use the candidate record */
	atomic_set(&candidate->usage, 1);
	atomic_set(&candidate->nkeys, 0);
	atomic_set(&candidate->nikeys, 0);
	candidate->uid = uid;
	candidate->qnkeys = 0;
	candidate->qnbytes = 0;
	spin_lock_init(&candidate->lock);
	mutex_init(&candidate->cons_lock);

	rb_link_node(&candidate->node, parent, p);
	rb_insert_color(&candidate->node, &key_user_tree);
	spin_unlock(&key_user_lock);
	user = candidate;
	goto out;

	/* okay - we found a user record for this UID */
found:
	atomic_inc(&user->usage);
	spin_unlock(&key_user_lock);
	kfree(candidate);
out:
	return user;
}

/*
 * Dispose of a user structure
 */
void key_user_put(struct key_user *user)
{
	if (atomic_dec_and_lock(&user->usage, &key_user_lock)) {
		rb_erase(&user->node, &key_user_tree);
		spin_unlock(&key_user_lock);

		kfree(user);
	}
}

/*
 * Allocate a serial number for a key.  These are assigned randomly to avoid
 * security issues through covert channel problems.
 */
static inline void key_alloc_serial(struct key *key)
{
	struct rb_node *parent, **p;
	struct key *xkey;

	/* propose a random serial number and look for a hole for it in the
	 * serial number tree */
	do {
		get_random_bytes(&key->serial, sizeof(key->serial));

		key->serial >>= 1; /* negative numbers are not permitted */
	} while (key->serial < 3);

	spin_lock(&key_serial_lock);

attempt_insertion:
	parent = NULL;
	p = &key_serial_tree.rb_node;

	while (*p) {
		parent = *p;
		xkey = rb_entry(parent, struct key, serial_node);

		if (key->serial < xkey->serial)
			p = &(*p)->rb_left;
		else if (key->serial > xkey->serial)
			p = &(*p)->rb_right;
		else
			goto serial_exists;
	}

	/* we've found a suitable hole - arrange for this key to occupy it */
	rb_link_node(&key->serial_node, parent, p);
	rb_insert_color(&key->serial_node, &key_serial_tree);

	spin_unlock(&key_serial_lock);
	return;

	/* we found a key with the proposed serial number - walk the tree from
	 * that point looking for the next unused serial number */
serial_exists:
	for (;;) {
		key->serial++;
		if (key->serial < 3) {
			key->serial = 3;
			goto attempt_insertion;
		}

		parent = rb_next(parent);
		if (!parent)
			goto attempt_insertion;

		xkey = rb_entry(parent, struct key, serial_node);
		if (key->serial < xkey->serial)
			goto attempt_insertion;
	}
}

/**
 * key_alloc - Allocate a key of the specified type.
 * @type: The type of key to allocate.
 * @desc: The key description to allow the key to be searched out.
 * @uid: The owner of the new key.
 * @gid: The group ID for the new key's group permissions.
 * @cred: The credentials specifying UID namespace.
 * @perm: The permissions mask of the new key.
 * @flags: Flags specifying quota properties.
 *
 * Allocate a key of the specified type with the attributes given.  The key is
 * returned in an uninstantiated state and the caller needs to instantiate the
 * key before returning.
 *
 * The user's key count quota is updated to reflect the creation of the key and
 * the user's key data quota has the default for the key type reserved.  The
 * instantiation function should amend this as necessary.  If insufficient
 * quota is available, -EDQUOT will be returned.
 *
 * The LSM security modules can prevent a key being created, in which case
 * -EACCES will be returned.
 *
 * Returns a pointer to the new key if successful and an error code otherwise.
 *
 * Note that the caller needs to ensure the key type isn't uninstantiated.
 * Internally this can be done by locking key_types_sem.  Externally, this can
 * be done by either never unregistering the key type, or making sure
 * key_alloc() calls don't race with module unloading.
 */
struct key *key_alloc(struct key_type *type, const char *desc,
		      kuid_t uid, kgid_t gid, const struct cred *cred,
		      key_perm_t perm, unsigned long flags)
{
	struct key_user *user = NULL;
	struct key *key;
	size_t desclen, quotalen;
	int ret;

	key = ERR_PTR(-EINVAL);
	if (!desc || !*desc)
		goto error;

	if (type->vet_description) {
		ret = type->vet_description(desc);
		if (ret < 0) {
			key = ERR_PTR(ret);
			goto error;
		}
	}

	desclen = strlen(desc);
	quotalen = desclen + 1 + type->def_datalen;

	/* get hold of the key tracking for this user */
	user = key_user_lookup(uid);
	if (!user)
		goto no_memory_1;

	/* check that the user's quota permits allocation of another key and
	 * its description */
	if (!(flags & KEY_ALLOC_NOT_IN_QUOTA)) {
		unsigned maxkeys = uid_eq(uid, GLOBAL_ROOT_UID) ?
			key_quota_root_maxkeys : key_quota_maxkeys;
		unsigned maxbytes = uid_eq(uid, GLOBAL_ROOT_UID) ?
			key_quota_root_maxbytes : key_quota_maxbytes;

		spin_lock(&user->lock);
		if (!(flags & KEY_ALLOC_QUOTA_OVERRUN)) {
			if (user->qnkeys + 1 >= maxkeys ||
			    user->qnbytes + quotalen >= maxbytes ||
			    user->qnbytes + quotalen < user->qnbytes)
				goto no_quota;
		}

		user->qnkeys++;
		user->qnbytes += quotalen;
		spin_unlock(&user->lock);
	}

	/* allocate and initialise the key and its description */
	key = kmem_cache_zalloc(key_jar, GFP_KERNEL);
	if (!key)
		goto no_memory_2;

	if (desc) {
		key->index_key.desc_len = desclen;
		key->index_key.description = kmemdup(desc, desclen + 1, GFP_KERNEL);
		if (!key->description)
			goto no_memory_3;
	}

	atomic_set(&key->usage, 1);
	init_rwsem(&key->sem);
	lockdep_set_class(&key->sem, &type->lock_class);
	key->index_key.type = type;
	key->user = user;
	key->quotalen = quotalen;
	key->datalen = type->def_datalen;
	key->uid = uid;
	key->gid = gid;
	key->perm = perm;

	if (!(flags & KEY_ALLOC_NOT_IN_QUOTA))
		key->flags |= 1 << KEY_FLAG_IN_QUOTA;
	if (flags & KEY_ALLOC_TRUSTED)
		key->flags |= 1 << KEY_FLAG_TRUSTED;

#ifdef KEY_DEBUGGING
	key->magic = KEY_DEBUG_MAGIC;
#endif

	/* let the security module know about the key */
	ret = security_key_alloc(key, cred, flags);
	if (ret < 0)
		goto security_error;

	/* publish the key by giving it a serial number */
	atomic_inc(&user->nkeys);
	key_alloc_serial(key);

error:
	return key;

security_error:
	kfree(key->description);
	kmem_cache_free(key_jar, key);
	if (!(flags & KEY_ALLOC_NOT_IN_QUOTA)) {
		spin_lock(&user->lock);
		user->qnkeys--;
		user->qnbytes -= quotalen;
		spin_unlock(&user->lock);
	}
	key_user_put(user);
	key = ERR_PTR(ret);
	goto error;

no_memory_3:
	kmem_cache_free(key_jar, key);
no_memory_2:
	if (!(flags & KEY_ALLOC_NOT_IN_QUOTA)) {
		spin_lock(&user->lock);
		user->qnkeys--;
		user->qnbytes -= quotalen;
		spin_unlock(&user->lock);
	}
	key_user_put(user);
no_memory_1:
	key = ERR_PTR(-ENOMEM);
	goto error;

no_quota:
	spin_unlock(&user->lock);
	key_user_put(user);
	key = ERR_PTR(-EDQUOT);
	goto error;
}
EXPORT_SYMBOL(key_alloc);

/**
 * key_payload_reserve - Adjust data quota reservation for the key's payload
 * @key: The key to make the reservation for.
 * @datalen: The amount of data payload the caller now wants.
 *
 * Adjust the amount of the owning user's key data quota that a key reserves.
 * If the amount is increased, then -EDQUOT may be returned if there isn't
 * enough free quota available.
 *
 * If successful, 0 is returned.
 */
int key_payload_reserve(struct key *key, size_t datalen)
{
	int delta = (int)datalen - key->datalen;
	int ret = 0;

	key_check(key);

	/* contemplate the quota adjustment */
	if (delta != 0 && test_bit(KEY_FLAG_IN_QUOTA, &key->flags)) {
		unsigned maxbytes = uid_eq(key->user->uid, GLOBAL_ROOT_UID) ?
			key_quota_root_maxbytes : key_quota_maxbytes;

		spin_lock(&key->user->lock);

		if (delta > 0 &&
		    (key->user->qnbytes + delta >= maxbytes ||
		     key->user->qnbytes + delta < key->user->qnbytes)) {
			ret = -EDQUOT;
		}
		else {
			key->user->qnbytes += delta;
			key->quotalen += delta;
		}
		spin_unlock(&key->user->lock);
	}

	/* change the recorded data length if that didn't generate an error */
	if (ret == 0)
		key->datalen = datalen;

	return ret;
}
EXPORT_SYMBOL(key_payload_reserve);

/*
 * Instantiate a key and link it into the target keyring atomically.  Must be
 * called with the target keyring's semaphore writelocked.  The target key's
 * semaphore need not be locked as instantiation is serialised by
 * key_construction_mutex.
 */
static int __key_instantiate_and_link(struct key *key,
				      struct key_preparsed_payload *prep,
				      struct key *keyring,
				      struct key *authkey,
				      struct assoc_array_edit **_edit)
{
	int ret, awaken;

	key_check(key);
	key_check(keyring);

	awaken = 0;
	ret = -EBUSY;

	mutex_lock(&key_construction_mutex);

	/* can't instantiate twice */
	if (!test_bit(KEY_FLAG_INSTANTIATED, &key->flags)) {
		/* instantiate the key */
		ret = key->type->instantiate(key, prep);

		if (ret == 0) {
			/* mark the key as being instantiated */
			atomic_inc(&key->user->nikeys);
			set_bit(KEY_FLAG_INSTANTIATED, &key->flags);

			if (test_and_clear_bit(KEY_FLAG_USER_CONSTRUCT, &key->flags))
				awaken = 1;

			/* and link it into the destination keyring */
			if (keyring)
				__key_link(key, _edit);

			/* disable the authorisation key */
			if (authkey)
				key_revoke(authkey);
		}
	}

	mutex_unlock(&key_construction_mutex);

	/* wake up anyone waiting for a key to be constructed */
	if (awaken)
		wake_up_bit(&key->flags, KEY_FLAG_USER_CONSTRUCT);

	return ret;
}

/**
 * key_instantiate_and_link - Instantiate a key and link it into the keyring.
 * @key: The key to instantiate.
 * @data: The data to use to instantiate the keyring.
 * @datalen: The length of @data.
 * @keyring: Keyring to create a link in on success (or NULL).
 * @authkey: The authorisation token permitting instantiation.
 *
 * Instantiate a key that's in the uninstantiated state using the provided data
 * and, if successful, link it in to the destination keyring if one is
 * supplied.
 *
 * If successful, 0 is returned, the authorisation token is revoked and anyone
 * waiting for the key is woken up.  If the key was already instantiated,
 * -EBUSY will be returned.
 */
int key_instantiate_and_link(struct key *key,
			     const void *data,
			     size_t datalen,
			     struct key *keyring,
			     struct key *authkey)
{
	struct key_preparsed_payload prep;
	struct assoc_array_edit *edit;
	int ret;

	memset(&prep, 0, sizeof(prep));
	prep.data = data;
	prep.datalen = datalen;
	prep.quotalen = key->type->def_datalen;
	if (key->type->preparse) {
		ret = key->type->preparse(&prep);
		if (ret < 0)
			goto error;
	}

	if (keyring) {
		ret = __key_link_begin(keyring, &key->index_key, &edit);
		if (ret < 0)
			goto error_free_preparse;
	}

	ret = __key_instantiate_and_link(key, &prep, keyring, authkey, &edit);

	if (keyring)
		__key_link_end(keyring, &key->index_key, edit);

error_free_preparse:
	if (key->type->preparse)
		key->type->free_preparse(&prep);
error:
	return ret;
}

EXPORT_SYMBOL(key_instantiate_and_link);

/**
 * key_reject_and_link - Negatively instantiate a key and link it into the keyring.
 * @key: The key to instantiate.
 * @timeout: The timeout on the negative key.
 * @error: The error to return when the key is hit.
 * @keyring: Keyring to create a link in on success (or NULL).
 * @authkey: The authorisation token permitting instantiation.
 *
 * Negatively instantiate a key that's in the uninstantiated state and, if
 * successful, set its timeout and stored error and link it in to the
 * destination keyring if one is supplied.  The key and any links to the key
 * will be automatically garbage collected after the timeout expires.
 *
 * Negative keys are used to rate limit repeated request_key() calls by causing
 * them to return the stored error code (typically ENOKEY) until the negative
 * key expires.
 *
 * If successful, 0 is returned, the authorisation token is revoked and anyone
 * waiting for the key is woken up.  If the key was already instantiated,
 * -EBUSY will be returned.
 */
int key_reject_and_link(struct key *key,
			unsigned timeout,
			unsigned error,
			struct key *keyring,
			struct key *authkey)
{
	struct assoc_array_edit *edit;
	struct timespec now;
	int ret, awaken, link_ret = 0;

	key_check(key);
	key_check(keyring);

	awaken = 0;
	ret = -EBUSY;

	if (keyring)
		link_ret = __key_link_begin(keyring, &key->index_key, &edit);

	mutex_lock(&key_construction_mutex);

	/* can't instantiate twice */
	if (!test_bit(KEY_FLAG_INSTANTIATED, &key->flags)) {
		/* mark the key as being negatively instantiated */
		atomic_inc(&key->user->nikeys);
		key->type_data.reject_error = -error;
		smp_wmb();
		set_bit(KEY_FLAG_NEGATIVE, &key->flags);
		set_bit(KEY_FLAG_INSTANTIATED, &key->flags);
		now = current_kernel_time();
		key->expiry = now.tv_sec + timeout;
		key_schedule_gc(key->expiry + key_gc_delay);

		if (test_and_clear_bit(KEY_FLAG_USER_CONSTRUCT, &key->flags))
			awaken = 1;

		ret = 0;

		/* and link it into the destination keyring */
		if (keyring && link_ret == 0)
			__key_link(key, &edit);

		/* disable the authorisation key */
		if (authkey)
			key_revoke(authkey);
	}

	mutex_unlock(&key_construction_mutex);

	if (keyring)
		__key_link_end(keyring, &key->index_key, edit);

	/* wake up anyone waiting for a key to be constructed */
	if (awaken)
		wake_up_bit(&key->flags, KEY_FLAG_USER_CONSTRUCT);

	return ret == 0 ? link_ret : ret;
}
EXPORT_SYMBOL(key_reject_and_link);

/**
 * key_put - Discard a reference to a key.
 * @key: The key to discard a reference from.
 *
 * Discard a reference to a key, and when all the references are gone, we
 * schedule the cleanup task to come and pull it out of the tree in process
 * context at some later time.
 */
void key_put(struct key *key)
{
	if (key) {
		key_check(key);

		if (atomic_dec_and_test(&key->usage))
			schedule_work(&key_gc_work);
	}
}
EXPORT_SYMBOL(key_put);

/*
 * Find a key by its serial number.
 */
struct key *key_lookup(key_serial_t id)
{
	struct rb_node *n;
	struct key *key;

	spin_lock(&key_serial_lock);

	/* search the tree for the specified key */
	n = key_serial_tree.rb_node;
	while (n) {
		key = rb_entry(n, struct key, serial_node);

		if (id < key->serial)
			n = n->rb_left;
		else if (id > key->serial)
			n = n->rb_right;
		else
			goto found;
	}

not_found:
	key = ERR_PTR(-ENOKEY);
	goto error;

found:
	/* pretend it doesn't exist if it is awaiting deletion */
	if (atomic_read(&key->usage) == 0)
		goto not_found;

	/* this races with key_put(), but that doesn't matter since key_put()
	 * doesn't actually change the key
	 */
	__key_get(key);

error:
	spin_unlock(&key_serial_lock);
	return key;
}

/*
 * Find and lock the specified key type against removal.
 *
 * We return with the sem read-locked if successful.  If the type wasn't
 * available -ENOKEY is returned instead.
 */
struct key_type *key_type_lookup(const char *type)
{
	struct key_type *ktype;

	down_read(&key_types_sem);

	/* look up the key type to see if it's one of the registered kernel
	 * types */
	list_for_each_entry(ktype, &key_types_list, link) {
		if (strcmp(ktype->name, type) == 0)
			goto found_kernel_type;
	}

	up_read(&key_types_sem);
	ktype = ERR_PTR(-ENOKEY);

found_kernel_type:
	return ktype;
}

void key_set_timeout(struct key *key, unsigned timeout)
{
	struct timespec now;
	time_t expiry = 0;

	/* make the changes with the locks held to prevent races */
	down_write(&key->sem);

	if (timeout > 0) {
		now = current_kernel_time();
		expiry = now.tv_sec + timeout;
	}

	key->expiry = expiry;
	key_schedule_gc(key->expiry + key_gc_delay);

	up_write(&key->sem);
}
EXPORT_SYMBOL_GPL(key_set_timeout);

/*
 * Unlock a key type locked by key_type_lookup().
 */
void key_type_put(struct key_type *ktype)
{
	up_read(&key_types_sem);
}

/*
 * Attempt to update an existing key.
 *
 * The key is given to us with an incremented refcount that we need to discard
 * if we get an error.
 */
static inline key_ref_t __key_update(key_ref_t key_ref,
				     struct key_preparsed_payload *prep)
{
	struct key *key = key_ref_to_ptr(key_ref);
	int ret;

	/* need write permission on the key to update it */
	ret = key_permission(key_ref, KEY_NEED_WRITE);
	if (ret < 0)
		goto error;

	ret = -EEXIST;
	if (!key->type->update)
		goto error;

	down_write(&key->sem);

	ret = key->type->update(key, prep);
	if (ret == 0)
		/* updating a negative key instantiates it */
		clear_bit(KEY_FLAG_NEGATIVE, &key->flags);

	up_write(&key->sem);

	if (ret < 0)
		goto error;
out:
	return key_ref;

error:
	key_put(key);
	key_ref = ERR_PTR(ret);
	goto out;
}

/**
 * key_create_or_update - Update or create and instantiate a key.
 * @keyring_ref: A pointer to the destination keyring with possession flag.
 * @type: The type of key.
 * @description: The searchable description for the key.
 * @payload: The data to use to instantiate or update the key.
 * @plen: The length of @payload.
 * @perm: The permissions mask for a new key.
 * @flags: The quota flags for a new key.
 *
 * Search the destination keyring for a key of the same description and if one
 * is found, update it, otherwise create and instantiate a new one and create a
 * link to it from that keyring.
 *
 * If perm is KEY_PERM_UNDEF then an appropriate key permissions mask will be
 * concocted.
 *
 * Returns a pointer to the new key if successful, -ENODEV if the key type
 * wasn't available, -ENOTDIR if the keyring wasn't a keyring, -EACCES if the
 * caller isn't permitted to modify the keyring or the LSM did not permit
 * creation of the key.
 *
 * On success, the possession flag from the keyring ref will be tacked on to
 * the key ref before it is returned.
 */
key_ref_t key_create_or_update(key_ref_t keyring_ref,
			       const char *type,
			       const char *description,
			       const void *payload,
			       size_t plen,
			       key_perm_t perm,
			       unsigned long flags)
{
	struct keyring_index_key index_key = {
		.description	= description,
	};
	struct key_preparsed_payload prep;
	struct assoc_array_edit *edit;
	const struct cred *cred = current_cred();
	struct key *keyring, *key = NULL;
	key_ref_t key_ref;
	int ret;

	/* look up the key type to see if it's one of the registered kernel
	 * types */
	index_key.type = key_type_lookup(type);
	if (IS_ERR(index_key.type)) {
		key_ref = ERR_PTR(-ENODEV);
		goto error;
	}

	key_ref = ERR_PTR(-EINVAL);
	if (!index_key.type->match || !index_key.type->instantiate ||
	    (!index_key.description && !index_key.type->preparse))
		goto error_put_type;

	keyring = key_ref_to_ptr(keyring_ref);

	key_check(keyring);

	key_ref = ERR_PTR(-ENOTDIR);
	if (keyring->type != &key_type_keyring)
		goto error_put_type;

	memset(&prep, 0, sizeof(prep));
	prep.data = payload;
	prep.datalen = plen;
	prep.quotalen = index_key.type->def_datalen;
	prep.trusted = flags & KEY_ALLOC_TRUSTED;
	if (index_key.type->preparse) {
		ret = index_key.type->preparse(&prep);
		if (ret < 0) {
			key_ref = ERR_PTR(ret);
			goto error_put_type;
		}
		if (!index_key.description)
			index_key.description = prep.description;
		key_ref = ERR_PTR(-EINVAL);
		if (!index_key.description)
			goto error_free_prep;
	}
	index_key.desc_len = strlen(index_key.description);

	key_ref = ERR_PTR(-EPERM);
	if (!prep.trusted && test_bit(KEY_FLAG_TRUSTED_ONLY, &keyring->flags))
		goto error_free_prep;
	flags |= prep.trusted ? KEY_ALLOC_TRUSTED : 0;

	ret = __key_link_begin(keyring, &index_key, &edit);
	if (ret < 0) {
		key_ref = ERR_PTR(ret);
		goto error_free_prep;
	}

	/* if we're going to allocate a new key, we're going to have
	 * to modify the keyring */
	ret = key_permission(keyring_ref, KEY_NEED_WRITE);
	if (ret < 0) {
		key_ref = ERR_PTR(ret);
		goto error_link_end;
	}

	/* if it's possible to update this type of key, search for an existing
	 * key of the same type and description in the destination keyring and
	 * update that instead if possible
	 */
	if (index_key.type->update) {
		key_ref = find_key_to_update(keyring_ref, &index_key);
		if (key_ref)
			goto found_matching_key;
	}

	/* if the client doesn't provide, decide on the permissions we want */
	if (perm == KEY_PERM_UNDEF) {
		perm = KEY_POS_VIEW | KEY_POS_SEARCH | KEY_POS_LINK | KEY_POS_SETATTR;
		perm |= KEY_USR_VIEW;

		if (index_key.type->read)
			perm |= KEY_POS_READ;

		if (index_key.type == &key_type_keyring ||
		    index_key.type->update)
			perm |= KEY_POS_WRITE;
	}

	/* allocate a new key */
	key = key_alloc(index_key.type, index_key.description,
			cred->fsuid, cred->fsgid, cred, perm, flags);
	if (IS_ERR(key)) {
		key_ref = ERR_CAST(key);
		goto error_link_end;
	}

	/* instantiate it and link it into the target keyring */
	ret = __key_instantiate_and_link(key, &prep, keyring, NULL, &edit);
	if (ret < 0) {
		key_put(key);
		key_ref = ERR_PTR(ret);
		goto error_link_end;
	}

	key_ref = make_key_ref(key, is_key_possessed(keyring_ref));

error_link_end:
	__key_link_end(keyring, &index_key, edit);
error_free_prep:
	if (index_key.type->preparse)
		index_key.type->free_preparse(&prep);
error_put_type:
	key_type_put(index_key.type);
error:
	return key_ref;

 found_matching_key:
	/* we found a matching key, so we're going to try to update it
	 * - we can drop the locks first as we have the key pinned
	 */
	__key_link_end(keyring, &index_key, edit);

	key_ref = __key_update(key_ref, &prep);
	goto error_free_prep;
}
EXPORT_SYMBOL(key_create_or_update);

/**
 * key_update - Update a key's contents.
 * @key_ref: The pointer (plus possession flag) to the key.
 * @payload: The data to be used to update the key.
 * @plen: The length of @payload.
 *
 * Attempt to update the contents of a key with the given payload data.  The
 * caller must be granted Write permission on the key.  Negative keys can be
 * instantiated by this method.
 *
 * Returns 0 on success, -EACCES if not permitted and -EOPNOTSUPP if the key
 * type does not support updating.  The key type may return other errors.
 */
int key_update(key_ref_t key_ref, const void *payload, size_t plen)
{
	struct key_preparsed_payload prep;
	struct key *key = key_ref_to_ptr(key_ref);
	int ret;

	key_check(key);

	/* the key must be writable */
	ret = key_permission(key_ref, KEY_NEED_WRITE);
	if (ret < 0)
		goto error;

	/* attempt to update it if supported */
	ret = -EOPNOTSUPP;
	if (!key->type->update)
		goto error;

	memset(&prep, 0, sizeof(prep));
	prep.data = payload;
	prep.datalen = plen;
	prep.quotalen = key->type->def_datalen;
	if (key->type->preparse) {
		ret = key->type->preparse(&prep);
		if (ret < 0)
			goto error;
	}

	down_write(&key->sem);

	ret = key->type->update(key, &prep);
	if (ret == 0)
		/* updating a negative key instantiates it */
		clear_bit(KEY_FLAG_NEGATIVE, &key->flags);

	up_write(&key->sem);

	if (key->type->preparse)
		key->type->free_preparse(&prep);
error:
	return ret;
}
EXPORT_SYMBOL(key_update);

/**
 * key_revoke - Revoke a key.
 * @key: The key to be revoked.
 *
 * Mark a key as being revoked and ask the type to free up its resources.  The
 * revocation timeout is set and the key and all its links will be
 * automatically garbage collected after key_gc_delay amount of time if they
 * are not manually dealt with first.
 */
void key_revoke(struct key *key)
{
	struct timespec now;
	time_t time;

	key_check(key);

	/* make sure no one's trying to change or use the key when we mark it
	 * - we tell lockdep that we might nest because we might be revoking an
	 *   authorisation key whilst holding the sem on a key we've just
	 *   instantiated
	 */
	down_write_nested(&key->sem, 1);
	if (!test_and_set_bit(KEY_FLAG_REVOKED, &key->flags) &&
	    key->type->revoke)
		key->type->revoke(key);

	/* set the death time to no more than the expiry time */
	now = current_kernel_time();
	time = now.tv_sec;
	if (key->revoked_at == 0 || key->revoked_at > time) {
		key->revoked_at = time;
		key_schedule_gc(key->revoked_at + key_gc_delay);
	}

	up_write(&key->sem);
}
EXPORT_SYMBOL(key_revoke);

/**
 * key_invalidate - Invalidate a key.
 * @key: The key to be invalidated.
 *
 * Mark a key as being invalidated and have it cleaned up immediately.  The key
 * is ignored by all searches and other operations from this point.
 */
void key_invalidate(struct key *key)
{
	kenter("%d", key_serial(key));

	key_check(key);

	if (!test_bit(KEY_FLAG_INVALIDATED, &key->flags)) {
		down_write_nested(&key->sem, 1);
		if (!test_and_set_bit(KEY_FLAG_INVALIDATED, &key->flags))
			key_schedule_gc_links();
		up_write(&key->sem);
	}
}
EXPORT_SYMBOL(key_invalidate);

/**
 * generic_key_instantiate - Simple instantiation of a key from preparsed data
 * @key: The key to be instantiated
 * @prep: The preparsed data to load.
 *
 * Instantiate a key from preparsed data.  We assume we can just copy the data
 * in directly and clear the old pointers.
 *
 * This can be pointed to directly by the key type instantiate op pointer.
 */
int generic_key_instantiate(struct key *key, struct key_preparsed_payload *prep)
{
	int ret;

	pr_devel("==>%s()\n", __func__);

	ret = key_payload_reserve(key, prep->quotalen);
	if (ret == 0) {
		key->type_data.p[0] = prep->type_data[0];
		key->type_data.p[1] = prep->type_data[1];
		rcu_assign_keypointer(key, prep->payload);
		prep->type_data[0] = NULL;
		prep->type_data[1] = NULL;
		prep->payload = NULL;
	}
	pr_devel("<==%s() = %d\n", __func__, ret);
	return ret;
}
EXPORT_SYMBOL(generic_key_instantiate);

/**
 * register_key_type - Register a type of key.
 * @ktype: The new key type.
 *
 * Register a new key type.
 *
 * Returns 0 on success or -EEXIST if a type of this name already exists.
 */
int register_key_type(struct key_type *ktype)
{
	struct key_type *p;
	int ret;

	memset(&ktype->lock_class, 0, sizeof(ktype->lock_class));

	ret = -EEXIST;
	down_write(&key_types_sem);

	/* disallow key types with the same name */
	list_for_each_entry(p, &key_types_list, link) {
		if (strcmp(p->name, ktype->name) == 0)
			goto out;
	}

	/* store the type */
	list_add(&ktype->link, &key_types_list);

	pr_notice("Key type %s registered\n", ktype->name);
	ret = 0;

out:
	up_write(&key_types_sem);
	return ret;
}
EXPORT_SYMBOL(register_key_type);

/**
 * unregister_key_type - Unregister a type of key.
 * @ktype: The key type.
 *
 * Unregister a key type and mark all the extant keys of this type as dead.
 * Those keys of this type are then destroyed to get rid of their payloads and
 * they and their links will be garbage collected as soon as possible.
 */
void unregister_key_type(struct key_type *ktype)
{
	down_write(&key_types_sem);
	list_del_init(&ktype->link);
	downgrade_write(&key_types_sem);
	key_gc_keytype(ktype);
	pr_notice("Key type %s unregistered\n", ktype->name);
	up_read(&key_types_sem);
}
EXPORT_SYMBOL(unregister_key_type);

/*
 * Initialise the key management state.
 */
void __init key_init(void)
{
	/* allocate a slab in which we can store keys */
	key_jar = kmem_cache_create("key_jar", sizeof(struct key),
			0, SLAB_HWCACHE_ALIGN|SLAB_PANIC, NULL);

	/* add the special key types */
	list_add_tail(&key_type_keyring.link, &key_types_list);
	list_add_tail(&key_type_dead.link, &key_types_list);
	list_add_tail(&key_type_user.link, &key_types_list);
	list_add_tail(&key_type_logon.link, &key_types_list);

	/* record the root user tracking */
	rb_link_node(&root_key_user.node,
		     NULL,
		     &key_user_tree.rb_node);

	rb_insert_color(&root_key_user.node,
			&key_user_tree);
}
