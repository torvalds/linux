/* Keyring handling
 *
 * Copyright (C) 2004-2005, 2008 Red Hat, Inc. All Rights Reserved.
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
#include <linux/security.h>
#include <linux/seq_file.h>
#include <linux/err.h>
#include <keys/keyring-type.h>
#include <linux/uaccess.h>
#include "internal.h"

#define rcu_dereference_locked_keyring(keyring)				\
	(rcu_dereference_protected(					\
		(keyring)->payload.subscriptions,			\
		rwsem_is_locked((struct rw_semaphore *)&(keyring)->sem)))

#define rcu_deref_link_locked(klist, index, keyring)			\
	(rcu_dereference_protected(					\
		(klist)->keys[index],					\
		rwsem_is_locked((struct rw_semaphore *)&(keyring)->sem)))

#define MAX_KEYRING_LINKS						\
	min_t(size_t, USHRT_MAX - 1,					\
	      ((PAGE_SIZE - sizeof(struct keyring_list)) / sizeof(struct key *)))

#define KEY_LINK_FIXQUOTA 1UL

/*
 * When plumbing the depths of the key tree, this sets a hard limit
 * set on how deep we're willing to go.
 */
#define KEYRING_SEARCH_MAX_DEPTH 6

/*
 * We keep all named keyrings in a hash to speed looking them up.
 */
#define KEYRING_NAME_HASH_SIZE	(1 << 5)

static struct list_head	keyring_name_hash[KEYRING_NAME_HASH_SIZE];
static DEFINE_RWLOCK(keyring_name_lock);

static inline unsigned keyring_hash(const char *desc)
{
	unsigned bucket = 0;

	for (; *desc; desc++)
		bucket += (unsigned char)*desc;

	return bucket & (KEYRING_NAME_HASH_SIZE - 1);
}

/*
 * The keyring key type definition.  Keyrings are simply keys of this type and
 * can be treated as ordinary keys in addition to having their own special
 * operations.
 */
static int keyring_instantiate(struct key *keyring,
			       struct key_preparsed_payload *prep);
static int keyring_match(const struct key *keyring, const void *criterion);
static void keyring_revoke(struct key *keyring);
static void keyring_destroy(struct key *keyring);
static void keyring_describe(const struct key *keyring, struct seq_file *m);
static long keyring_read(const struct key *keyring,
			 char __user *buffer, size_t buflen);

struct key_type key_type_keyring = {
	.name		= "keyring",
	.def_datalen	= sizeof(struct keyring_list),
	.instantiate	= keyring_instantiate,
	.match		= keyring_match,
	.revoke		= keyring_revoke,
	.destroy	= keyring_destroy,
	.describe	= keyring_describe,
	.read		= keyring_read,
};
EXPORT_SYMBOL(key_type_keyring);

/*
 * Semaphore to serialise link/link calls to prevent two link calls in parallel
 * introducing a cycle.
 */
static DECLARE_RWSEM(keyring_serialise_link_sem);

/*
 * Publish the name of a keyring so that it can be found by name (if it has
 * one).
 */
static void keyring_publish_name(struct key *keyring)
{
	int bucket;

	if (keyring->description) {
		bucket = keyring_hash(keyring->description);

		write_lock(&keyring_name_lock);

		if (!keyring_name_hash[bucket].next)
			INIT_LIST_HEAD(&keyring_name_hash[bucket]);

		list_add_tail(&keyring->type_data.link,
			      &keyring_name_hash[bucket]);

		write_unlock(&keyring_name_lock);
	}
}

/*
 * Initialise a keyring.
 *
 * Returns 0 on success, -EINVAL if given any data.
 */
static int keyring_instantiate(struct key *keyring,
			       struct key_preparsed_payload *prep)
{
	int ret;

	ret = -EINVAL;
	if (prep->datalen == 0) {
		/* make the keyring available by name if it has one */
		keyring_publish_name(keyring);
		ret = 0;
	}

	return ret;
}

/*
 * Match keyrings on their name
 */
static int keyring_match(const struct key *keyring, const void *description)
{
	return keyring->description &&
		strcmp(keyring->description, description) == 0;
}

/*
 * Clean up a keyring when it is destroyed.  Unpublish its name if it had one
 * and dispose of its data.
 *
 * The garbage collector detects the final key_put(), removes the keyring from
 * the serial number tree and then does RCU synchronisation before coming here,
 * so we shouldn't need to worry about code poking around here with the RCU
 * readlock held by this time.
 */
static void keyring_destroy(struct key *keyring)
{
	struct keyring_list *klist;
	int loop;

	if (keyring->description) {
		write_lock(&keyring_name_lock);

		if (keyring->type_data.link.next != NULL &&
		    !list_empty(&keyring->type_data.link))
			list_del(&keyring->type_data.link);

		write_unlock(&keyring_name_lock);
	}

	klist = rcu_access_pointer(keyring->payload.subscriptions);
	if (klist) {
		for (loop = klist->nkeys - 1; loop >= 0; loop--)
			key_put(rcu_access_pointer(klist->keys[loop]));
		kfree(klist);
	}
}

/*
 * Describe a keyring for /proc.
 */
static void keyring_describe(const struct key *keyring, struct seq_file *m)
{
	struct keyring_list *klist;

	if (keyring->description)
		seq_puts(m, keyring->description);
	else
		seq_puts(m, "[anon]");

	if (key_is_instantiated(keyring)) {
		rcu_read_lock();
		klist = rcu_dereference(keyring->payload.subscriptions);
		if (klist)
			seq_printf(m, ": %u/%u", klist->nkeys, klist->maxkeys);
		else
			seq_puts(m, ": empty");
		rcu_read_unlock();
	}
}

/*
 * Read a list of key IDs from the keyring's contents in binary form
 *
 * The keyring's semaphore is read-locked by the caller.
 */
static long keyring_read(const struct key *keyring,
			 char __user *buffer, size_t buflen)
{
	struct keyring_list *klist;
	struct key *key;
	size_t qty, tmp;
	int loop, ret;

	ret = 0;
	klist = rcu_dereference_locked_keyring(keyring);
	if (klist) {
		/* calculate how much data we could return */
		qty = klist->nkeys * sizeof(key_serial_t);

		if (buffer && buflen > 0) {
			if (buflen > qty)
				buflen = qty;

			/* copy the IDs of the subscribed keys into the
			 * buffer */
			ret = -EFAULT;

			for (loop = 0; loop < klist->nkeys; loop++) {
				key = rcu_deref_link_locked(klist, loop,
							    keyring);

				tmp = sizeof(key_serial_t);
				if (tmp > buflen)
					tmp = buflen;

				if (copy_to_user(buffer,
						 &key->serial,
						 tmp) != 0)
					goto error;

				buflen -= tmp;
				if (buflen == 0)
					break;
				buffer += tmp;
			}
		}

		ret = qty;
	}

error:
	return ret;
}

/*
 * Allocate a keyring and link into the destination keyring.
 */
struct key *keyring_alloc(const char *description, kuid_t uid, kgid_t gid,
			  const struct cred *cred, unsigned long flags,
			  struct key *dest)
{
	struct key *keyring;
	int ret;

	keyring = key_alloc(&key_type_keyring, description,
			    uid, gid, cred,
			    (KEY_POS_ALL & ~KEY_POS_SETATTR) | KEY_USR_ALL,
			    flags);

	if (!IS_ERR(keyring)) {
		ret = key_instantiate_and_link(keyring, NULL, 0, dest, NULL);
		if (ret < 0) {
			key_put(keyring);
			keyring = ERR_PTR(ret);
		}
	}

	return keyring;
}

/**
 * keyring_search_aux - Search a keyring tree for a key matching some criteria
 * @keyring_ref: A pointer to the keyring with possession indicator.
 * @cred: The credentials to use for permissions checks.
 * @type: The type of key to search for.
 * @description: Parameter for @match.
 * @match: Function to rule on whether or not a key is the one required.
 * @no_state_check: Don't check if a matching key is bad
 *
 * Search the supplied keyring tree for a key that matches the criteria given.
 * The root keyring and any linked keyrings must grant Search permission to the
 * caller to be searchable and keys can only be found if they too grant Search
 * to the caller. The possession flag on the root keyring pointer controls use
 * of the possessor bits in permissions checking of the entire tree.  In
 * addition, the LSM gets to forbid keyring searches and key matches.
 *
 * The search is performed as a breadth-then-depth search up to the prescribed
 * limit (KEYRING_SEARCH_MAX_DEPTH).
 *
 * Keys are matched to the type provided and are then filtered by the match
 * function, which is given the description to use in any way it sees fit.  The
 * match function may use any attributes of a key that it wishes to to
 * determine the match.  Normally the match function from the key type would be
 * used.
 *
 * RCU is used to prevent the keyring key lists from disappearing without the
 * need to take lots of locks.
 *
 * Returns a pointer to the found key and increments the key usage count if
 * successful; -EAGAIN if no matching keys were found, or if expired or revoked
 * keys were found; -ENOKEY if only negative keys were found; -ENOTDIR if the
 * specified keyring wasn't a keyring.
 *
 * In the case of a successful return, the possession attribute from
 * @keyring_ref is propagated to the returned key reference.
 */
key_ref_t keyring_search_aux(key_ref_t keyring_ref,
			     const struct cred *cred,
			     struct key_type *type,
			     const void *description,
			     key_match_func_t match,
			     bool no_state_check)
{
	struct {
		/* Need a separate keylist pointer for RCU purposes */
		struct key *keyring;
		struct keyring_list *keylist;
		int kix;
	} stack[KEYRING_SEARCH_MAX_DEPTH];

	struct keyring_list *keylist;
	struct timespec now;
	unsigned long possessed, kflags;
	struct key *keyring, *key;
	key_ref_t key_ref;
	long err;
	int sp, nkeys, kix;

	keyring = key_ref_to_ptr(keyring_ref);
	possessed = is_key_possessed(keyring_ref);
	key_check(keyring);

	/* top keyring must have search permission to begin the search */
	err = key_task_permission(keyring_ref, cred, KEY_SEARCH);
	if (err < 0) {
		key_ref = ERR_PTR(err);
		goto error;
	}

	key_ref = ERR_PTR(-ENOTDIR);
	if (keyring->type != &key_type_keyring)
		goto error;

	rcu_read_lock();

	now = current_kernel_time();
	err = -EAGAIN;
	sp = 0;

	/* firstly we should check to see if this top-level keyring is what we
	 * are looking for */
	key_ref = ERR_PTR(-EAGAIN);
	kflags = keyring->flags;
	if (keyring->type == type && match(keyring, description)) {
		key = keyring;
		if (no_state_check)
			goto found;

		/* check it isn't negative and hasn't expired or been
		 * revoked */
		if (kflags & (1 << KEY_FLAG_REVOKED))
			goto error_2;
		if (key->expiry && now.tv_sec >= key->expiry)
			goto error_2;
		key_ref = ERR_PTR(key->type_data.reject_error);
		if (kflags & (1 << KEY_FLAG_NEGATIVE))
			goto error_2;
		goto found;
	}

	/* otherwise, the top keyring must not be revoked, expired, or
	 * negatively instantiated if we are to search it */
	key_ref = ERR_PTR(-EAGAIN);
	if (kflags & ((1 << KEY_FLAG_INVALIDATED) |
		      (1 << KEY_FLAG_REVOKED) |
		      (1 << KEY_FLAG_NEGATIVE)) ||
	    (keyring->expiry && now.tv_sec >= keyring->expiry))
		goto error_2;

	/* start processing a new keyring */
descend:
	kflags = keyring->flags;
	if (kflags & ((1 << KEY_FLAG_INVALIDATED) |
		      (1 << KEY_FLAG_REVOKED)))
		goto not_this_keyring;

	keylist = rcu_dereference(keyring->payload.subscriptions);
	if (!keylist)
		goto not_this_keyring;

	/* iterate through the keys in this keyring first */
	nkeys = keylist->nkeys;
	smp_rmb();
	for (kix = 0; kix < nkeys; kix++) {
		key = rcu_dereference(keylist->keys[kix]);
		kflags = key->flags;

		/* ignore keys not of this type */
		if (key->type != type)
			continue;

		/* skip invalidated, revoked and expired keys */
		if (!no_state_check) {
			if (kflags & ((1 << KEY_FLAG_INVALIDATED) |
				      (1 << KEY_FLAG_REVOKED)))
				continue;

			if (key->expiry && now.tv_sec >= key->expiry)
				continue;
		}

		/* keys that don't match */
		if (!match(key, description))
			continue;

		/* key must have search permissions */
		if (key_task_permission(make_key_ref(key, possessed),
					cred, KEY_SEARCH) < 0)
			continue;

		if (no_state_check)
			goto found;

		/* we set a different error code if we pass a negative key */
		if (kflags & (1 << KEY_FLAG_NEGATIVE)) {
			err = key->type_data.reject_error;
			continue;
		}

		goto found;
	}

	/* search through the keyrings nested in this one */
	kix = 0;
ascend:
	nkeys = keylist->nkeys;
	smp_rmb();
	for (; kix < nkeys; kix++) {
		key = rcu_dereference(keylist->keys[kix]);
		if (key->type != &key_type_keyring)
			continue;

		/* recursively search nested keyrings
		 * - only search keyrings for which we have search permission
		 */
		if (sp >= KEYRING_SEARCH_MAX_DEPTH)
			continue;

		if (key_task_permission(make_key_ref(key, possessed),
					cred, KEY_SEARCH) < 0)
			continue;

		/* stack the current position */
		stack[sp].keyring = keyring;
		stack[sp].keylist = keylist;
		stack[sp].kix = kix;
		sp++;

		/* begin again with the new keyring */
		keyring = key;
		goto descend;
	}

	/* the keyring we're looking at was disqualified or didn't contain a
	 * matching key */
not_this_keyring:
	if (sp > 0) {
		/* resume the processing of a keyring higher up in the tree */
		sp--;
		keyring = stack[sp].keyring;
		keylist = stack[sp].keylist;
		kix = stack[sp].kix + 1;
		goto ascend;
	}

	key_ref = ERR_PTR(err);
	goto error_2;

	/* we found a viable match */
found:
	atomic_inc(&key->usage);
	key->last_used_at = now.tv_sec;
	keyring->last_used_at = now.tv_sec;
	while (sp > 0)
		stack[--sp].keyring->last_used_at = now.tv_sec;
	key_check(key);
	key_ref = make_key_ref(key, possessed);
error_2:
	rcu_read_unlock();
error:
	return key_ref;
}

/**
 * keyring_search - Search the supplied keyring tree for a matching key
 * @keyring: The root of the keyring tree to be searched.
 * @type: The type of keyring we want to find.
 * @description: The name of the keyring we want to find.
 *
 * As keyring_search_aux() above, but using the current task's credentials and
 * type's default matching function.
 */
key_ref_t keyring_search(key_ref_t keyring,
			 struct key_type *type,
			 const char *description)
{
	if (!type->match)
		return ERR_PTR(-ENOKEY);

	return keyring_search_aux(keyring, current->cred,
				  type, description, type->match, false);
}
EXPORT_SYMBOL(keyring_search);

/*
 * Search the given keyring only (no recursion).
 *
 * The caller must guarantee that the keyring is a keyring and that the
 * permission is granted to search the keyring as no check is made here.
 *
 * RCU is used to make it unnecessary to lock the keyring key list here.
 *
 * Returns a pointer to the found key with usage count incremented if
 * successful and returns -ENOKEY if not found.  Revoked keys and keys not
 * providing the requested permission are skipped over.
 *
 * If successful, the possession indicator is propagated from the keyring ref
 * to the returned key reference.
 */
key_ref_t __keyring_search_one(key_ref_t keyring_ref,
			       const struct key_type *ktype,
			       const char *description,
			       key_perm_t perm)
{
	struct keyring_list *klist;
	unsigned long possessed;
	struct key *keyring, *key;
	int nkeys, loop;

	keyring = key_ref_to_ptr(keyring_ref);
	possessed = is_key_possessed(keyring_ref);

	rcu_read_lock();

	klist = rcu_dereference(keyring->payload.subscriptions);
	if (klist) {
		nkeys = klist->nkeys;
		smp_rmb();
		for (loop = 0; loop < nkeys ; loop++) {
			key = rcu_dereference(klist->keys[loop]);
			if (key->type == ktype &&
			    (!key->type->match ||
			     key->type->match(key, description)) &&
			    key_permission(make_key_ref(key, possessed),
					   perm) == 0 &&
			    !(key->flags & ((1 << KEY_FLAG_INVALIDATED) |
					    (1 << KEY_FLAG_REVOKED)))
			    )
				goto found;
		}
	}

	rcu_read_unlock();
	return ERR_PTR(-ENOKEY);

found:
	atomic_inc(&key->usage);
	keyring->last_used_at = key->last_used_at =
		current_kernel_time().tv_sec;
	rcu_read_unlock();
	return make_key_ref(key, possessed);
}

/*
 * Find a keyring with the specified name.
 *
 * All named keyrings in the current user namespace are searched, provided they
 * grant Search permission directly to the caller (unless this check is
 * skipped).  Keyrings whose usage points have reached zero or who have been
 * revoked are skipped.
 *
 * Returns a pointer to the keyring with the keyring's refcount having being
 * incremented on success.  -ENOKEY is returned if a key could not be found.
 */
struct key *find_keyring_by_name(const char *name, bool skip_perm_check)
{
	struct key *keyring;
	int bucket;

	if (!name)
		return ERR_PTR(-EINVAL);

	bucket = keyring_hash(name);

	read_lock(&keyring_name_lock);

	if (keyring_name_hash[bucket].next) {
		/* search this hash bucket for a keyring with a matching name
		 * that's readable and that hasn't been revoked */
		list_for_each_entry(keyring,
				    &keyring_name_hash[bucket],
				    type_data.link
				    ) {
			if (!kuid_has_mapping(current_user_ns(), keyring->user->uid))
				continue;

			if (test_bit(KEY_FLAG_REVOKED, &keyring->flags))
				continue;

			if (strcmp(keyring->description, name) != 0)
				continue;

			if (!skip_perm_check &&
			    key_permission(make_key_ref(keyring, 0),
					   KEY_SEARCH) < 0)
				continue;

			/* we've got a match but we might end up racing with
			 * key_cleanup() if the keyring is currently 'dead'
			 * (ie. it has a zero usage count) */
			if (!atomic_inc_not_zero(&keyring->usage))
				continue;
			keyring->last_used_at = current_kernel_time().tv_sec;
			goto out;
		}
	}

	keyring = ERR_PTR(-ENOKEY);
out:
	read_unlock(&keyring_name_lock);
	return keyring;
}

/*
 * See if a cycle will will be created by inserting acyclic tree B in acyclic
 * tree A at the topmost level (ie: as a direct child of A).
 *
 * Since we are adding B to A at the top level, checking for cycles should just
 * be a matter of seeing if node A is somewhere in tree B.
 */
static int keyring_detect_cycle(struct key *A, struct key *B)
{
	struct {
		struct keyring_list *keylist;
		int kix;
	} stack[KEYRING_SEARCH_MAX_DEPTH];

	struct keyring_list *keylist;
	struct key *subtree, *key;
	int sp, nkeys, kix, ret;

	rcu_read_lock();

	ret = -EDEADLK;
	if (A == B)
		goto cycle_detected;

	subtree = B;
	sp = 0;

	/* start processing a new keyring */
descend:
	if (test_bit(KEY_FLAG_REVOKED, &subtree->flags))
		goto not_this_keyring;

	keylist = rcu_dereference(subtree->payload.subscriptions);
	if (!keylist)
		goto not_this_keyring;
	kix = 0;

ascend:
	/* iterate through the remaining keys in this keyring */
	nkeys = keylist->nkeys;
	smp_rmb();
	for (; kix < nkeys; kix++) {
		key = rcu_dereference(keylist->keys[kix]);

		if (key == A)
			goto cycle_detected;

		/* recursively check nested keyrings */
		if (key->type == &key_type_keyring) {
			if (sp >= KEYRING_SEARCH_MAX_DEPTH)
				goto too_deep;

			/* stack the current position */
			stack[sp].keylist = keylist;
			stack[sp].kix = kix;
			sp++;

			/* begin again with the new keyring */
			subtree = key;
			goto descend;
		}
	}

	/* the keyring we're looking at was disqualified or didn't contain a
	 * matching key */
not_this_keyring:
	if (sp > 0) {
		/* resume the checking of a keyring higher up in the tree */
		sp--;
		keylist = stack[sp].keylist;
		kix = stack[sp].kix + 1;
		goto ascend;
	}

	ret = 0; /* no cycles detected */

error:
	rcu_read_unlock();
	return ret;

too_deep:
	ret = -ELOOP;
	goto error;

cycle_detected:
	ret = -EDEADLK;
	goto error;
}

/*
 * Dispose of a keyring list after the RCU grace period, freeing the unlinked
 * key
 */
static void keyring_unlink_rcu_disposal(struct rcu_head *rcu)
{
	struct keyring_list *klist =
		container_of(rcu, struct keyring_list, rcu);

	if (klist->delkey != USHRT_MAX)
		key_put(rcu_access_pointer(klist->keys[klist->delkey]));
	kfree(klist);
}

/*
 * Preallocate memory so that a key can be linked into to a keyring.
 */
int __key_link_begin(struct key *keyring, const struct key_type *type,
		     const char *description, unsigned long *_prealloc)
	__acquires(&keyring->sem)
	__acquires(&keyring_serialise_link_sem)
{
	struct keyring_list *klist, *nklist;
	unsigned long prealloc;
	unsigned max;
	time_t lowest_lru;
	size_t size;
	int loop, lru, ret;

	kenter("%d,%s,%s,", key_serial(keyring), type->name, description);

	if (keyring->type != &key_type_keyring)
		return -ENOTDIR;

	down_write(&keyring->sem);

	ret = -EKEYREVOKED;
	if (test_bit(KEY_FLAG_REVOKED, &keyring->flags))
		goto error_krsem;

	/* serialise link/link calls to prevent parallel calls causing a cycle
	 * when linking two keyring in opposite orders */
	if (type == &key_type_keyring)
		down_write(&keyring_serialise_link_sem);

	klist = rcu_dereference_locked_keyring(keyring);

	/* see if there's a matching key we can displace */
	lru = -1;
	if (klist && klist->nkeys > 0) {
		lowest_lru = TIME_T_MAX;
		for (loop = klist->nkeys - 1; loop >= 0; loop--) {
			struct key *key = rcu_deref_link_locked(klist, loop,
								keyring);
			if (key->type == type &&
			    strcmp(key->description, description) == 0) {
				/* Found a match - we'll replace the link with
				 * one to the new key.  We record the slot
				 * position.
				 */
				klist->delkey = loop;
				prealloc = 0;
				goto done;
			}
			if (key->last_used_at < lowest_lru) {
				lowest_lru = key->last_used_at;
				lru = loop;
			}
		}
	}

	/* If the keyring is full then do an LRU discard */
	if (klist &&
	    klist->nkeys == klist->maxkeys &&
	    klist->maxkeys >= MAX_KEYRING_LINKS) {
		kdebug("LRU discard %d\n", lru);
		klist->delkey = lru;
		prealloc = 0;
		goto done;
	}

	/* check that we aren't going to overrun the user's quota */
	ret = key_payload_reserve(keyring,
				  keyring->datalen + KEYQUOTA_LINK_BYTES);
	if (ret < 0)
		goto error_sem;

	if (klist && klist->nkeys < klist->maxkeys) {
		/* there's sufficient slack space to append directly */
		klist->delkey = klist->nkeys;
		prealloc = KEY_LINK_FIXQUOTA;
	} else {
		/* grow the key list */
		max = 4;
		if (klist) {
			max += klist->maxkeys;
			if (max > MAX_KEYRING_LINKS)
				max = MAX_KEYRING_LINKS;
			BUG_ON(max <= klist->maxkeys);
		}

		size = sizeof(*klist) + sizeof(struct key *) * max;

		ret = -ENOMEM;
		nklist = kmalloc(size, GFP_KERNEL);
		if (!nklist)
			goto error_quota;

		nklist->maxkeys = max;
		if (klist) {
			memcpy(nklist->keys, klist->keys,
			       sizeof(struct key *) * klist->nkeys);
			nklist->delkey = klist->nkeys;
			nklist->nkeys = klist->nkeys + 1;
			klist->delkey = USHRT_MAX;
		} else {
			nklist->nkeys = 1;
			nklist->delkey = 0;
		}

		/* add the key into the new space */
		RCU_INIT_POINTER(nklist->keys[nklist->delkey], NULL);
		prealloc = (unsigned long)nklist | KEY_LINK_FIXQUOTA;
	}

done:
	*_prealloc = prealloc;
	kleave(" = 0");
	return 0;

error_quota:
	/* undo the quota changes */
	key_payload_reserve(keyring,
			    keyring->datalen - KEYQUOTA_LINK_BYTES);
error_sem:
	if (type == &key_type_keyring)
		up_write(&keyring_serialise_link_sem);
error_krsem:
	up_write(&keyring->sem);
	kleave(" = %d", ret);
	return ret;
}

/*
 * Check already instantiated keys aren't going to be a problem.
 *
 * The caller must have called __key_link_begin(). Don't need to call this for
 * keys that were created since __key_link_begin() was called.
 */
int __key_link_check_live_key(struct key *keyring, struct key *key)
{
	if (key->type == &key_type_keyring)
		/* check that we aren't going to create a cycle by linking one
		 * keyring to another */
		return keyring_detect_cycle(keyring, key);
	return 0;
}

/*
 * Link a key into to a keyring.
 *
 * Must be called with __key_link_begin() having being called.  Discards any
 * already extant link to matching key if there is one, so that each keyring
 * holds at most one link to any given key of a particular type+description
 * combination.
 */
void __key_link(struct key *keyring, struct key *key,
		unsigned long *_prealloc)
{
	struct keyring_list *klist, *nklist;
	struct key *discard;

	nklist = (struct keyring_list *)(*_prealloc & ~KEY_LINK_FIXQUOTA);
	*_prealloc = 0;

	kenter("%d,%d,%p", keyring->serial, key->serial, nklist);

	klist = rcu_dereference_locked_keyring(keyring);

	atomic_inc(&key->usage);
	keyring->last_used_at = key->last_used_at =
		current_kernel_time().tv_sec;

	/* there's a matching key we can displace or an empty slot in a newly
	 * allocated list we can fill */
	if (nklist) {
		kdebug("reissue %hu/%hu/%hu",
		       nklist->delkey, nklist->nkeys, nklist->maxkeys);

		RCU_INIT_POINTER(nklist->keys[nklist->delkey], key);

		rcu_assign_pointer(keyring->payload.subscriptions, nklist);

		/* dispose of the old keyring list and, if there was one, the
		 * displaced key */
		if (klist) {
			kdebug("dispose %hu/%hu/%hu",
			       klist->delkey, klist->nkeys, klist->maxkeys);
			call_rcu(&klist->rcu, keyring_unlink_rcu_disposal);
		}
	} else if (klist->delkey < klist->nkeys) {
		kdebug("replace %hu/%hu/%hu",
		       klist->delkey, klist->nkeys, klist->maxkeys);

		discard = rcu_dereference_protected(
			klist->keys[klist->delkey],
			rwsem_is_locked(&keyring->sem));
		rcu_assign_pointer(klist->keys[klist->delkey], key);
		/* The garbage collector will take care of RCU
		 * synchronisation */
		key_put(discard);
	} else {
		/* there's sufficient slack space to append directly */
		kdebug("append %hu/%hu/%hu",
		       klist->delkey, klist->nkeys, klist->maxkeys);

		RCU_INIT_POINTER(klist->keys[klist->delkey], key);
		smp_wmb();
		klist->nkeys++;
	}
}

/*
 * Finish linking a key into to a keyring.
 *
 * Must be called with __key_link_begin() having being called.
 */
void __key_link_end(struct key *keyring, struct key_type *type,
		    unsigned long prealloc)
	__releases(&keyring->sem)
	__releases(&keyring_serialise_link_sem)
{
	BUG_ON(type == NULL);
	BUG_ON(type->name == NULL);
	kenter("%d,%s,%lx", keyring->serial, type->name, prealloc);

	if (type == &key_type_keyring)
		up_write(&keyring_serialise_link_sem);

	if (prealloc) {
		if (prealloc & KEY_LINK_FIXQUOTA)
			key_payload_reserve(keyring,
					    keyring->datalen -
					    KEYQUOTA_LINK_BYTES);
		kfree((struct keyring_list *)(prealloc & ~KEY_LINK_FIXQUOTA));
	}
	up_write(&keyring->sem);
}

/**
 * key_link - Link a key to a keyring
 * @keyring: The keyring to make the link in.
 * @key: The key to link to.
 *
 * Make a link in a keyring to a key, such that the keyring holds a reference
 * on that key and the key can potentially be found by searching that keyring.
 *
 * This function will write-lock the keyring's semaphore and will consume some
 * of the user's key data quota to hold the link.
 *
 * Returns 0 if successful, -ENOTDIR if the keyring isn't a keyring,
 * -EKEYREVOKED if the keyring has been revoked, -ENFILE if the keyring is
 * full, -EDQUOT if there is insufficient key data quota remaining to add
 * another link or -ENOMEM if there's insufficient memory.
 *
 * It is assumed that the caller has checked that it is permitted for a link to
 * be made (the keyring should have Write permission and the key Link
 * permission).
 */
int key_link(struct key *keyring, struct key *key)
{
	unsigned long prealloc;
	int ret;

	key_check(keyring);
	key_check(key);

	ret = __key_link_begin(keyring, key->type, key->description, &prealloc);
	if (ret == 0) {
		ret = __key_link_check_live_key(keyring, key);
		if (ret == 0)
			__key_link(keyring, key, &prealloc);
		__key_link_end(keyring, key->type, prealloc);
	}

	return ret;
}
EXPORT_SYMBOL(key_link);

/**
 * key_unlink - Unlink the first link to a key from a keyring.
 * @keyring: The keyring to remove the link from.
 * @key: The key the link is to.
 *
 * Remove a link from a keyring to a key.
 *
 * This function will write-lock the keyring's semaphore.
 *
 * Returns 0 if successful, -ENOTDIR if the keyring isn't a keyring, -ENOENT if
 * the key isn't linked to by the keyring or -ENOMEM if there's insufficient
 * memory.
 *
 * It is assumed that the caller has checked that it is permitted for a link to
 * be removed (the keyring should have Write permission; no permissions are
 * required on the key).
 */
int key_unlink(struct key *keyring, struct key *key)
{
	struct keyring_list *klist, *nklist;
	int loop, ret;

	key_check(keyring);
	key_check(key);

	ret = -ENOTDIR;
	if (keyring->type != &key_type_keyring)
		goto error;

	down_write(&keyring->sem);

	klist = rcu_dereference_locked_keyring(keyring);
	if (klist) {
		/* search the keyring for the key */
		for (loop = 0; loop < klist->nkeys; loop++)
			if (rcu_access_pointer(klist->keys[loop]) == key)
				goto key_is_present;
	}

	up_write(&keyring->sem);
	ret = -ENOENT;
	goto error;

key_is_present:
	/* we need to copy the key list for RCU purposes */
	nklist = kmalloc(sizeof(*klist) +
			 sizeof(struct key *) * klist->maxkeys,
			 GFP_KERNEL);
	if (!nklist)
		goto nomem;
	nklist->maxkeys = klist->maxkeys;
	nklist->nkeys = klist->nkeys - 1;

	if (loop > 0)
		memcpy(&nklist->keys[0],
		       &klist->keys[0],
		       loop * sizeof(struct key *));

	if (loop < nklist->nkeys)
		memcpy(&nklist->keys[loop],
		       &klist->keys[loop + 1],
		       (nklist->nkeys - loop) * sizeof(struct key *));

	/* adjust the user's quota */
	key_payload_reserve(keyring,
			    keyring->datalen - KEYQUOTA_LINK_BYTES);

	rcu_assign_pointer(keyring->payload.subscriptions, nklist);

	up_write(&keyring->sem);

	/* schedule for later cleanup */
	klist->delkey = loop;
	call_rcu(&klist->rcu, keyring_unlink_rcu_disposal);

	ret = 0;

error:
	return ret;
nomem:
	ret = -ENOMEM;
	up_write(&keyring->sem);
	goto error;
}
EXPORT_SYMBOL(key_unlink);

/*
 * Dispose of a keyring list after the RCU grace period, releasing the keys it
 * links to.
 */
static void keyring_clear_rcu_disposal(struct rcu_head *rcu)
{
	struct keyring_list *klist;
	int loop;

	klist = container_of(rcu, struct keyring_list, rcu);

	for (loop = klist->nkeys - 1; loop >= 0; loop--)
		key_put(rcu_access_pointer(klist->keys[loop]));

	kfree(klist);
}

/**
 * keyring_clear - Clear a keyring
 * @keyring: The keyring to clear.
 *
 * Clear the contents of the specified keyring.
 *
 * Returns 0 if successful or -ENOTDIR if the keyring isn't a keyring.
 */
int keyring_clear(struct key *keyring)
{
	struct keyring_list *klist;
	int ret;

	ret = -ENOTDIR;
	if (keyring->type == &key_type_keyring) {
		/* detach the pointer block with the locks held */
		down_write(&keyring->sem);

		klist = rcu_dereference_locked_keyring(keyring);
		if (klist) {
			/* adjust the quota */
			key_payload_reserve(keyring,
					    sizeof(struct keyring_list));

			rcu_assign_pointer(keyring->payload.subscriptions,
					   NULL);
		}

		up_write(&keyring->sem);

		/* free the keys after the locks have been dropped */
		if (klist)
			call_rcu(&klist->rcu, keyring_clear_rcu_disposal);

		ret = 0;
	}

	return ret;
}
EXPORT_SYMBOL(keyring_clear);

/*
 * Dispose of the links from a revoked keyring.
 *
 * This is called with the key sem write-locked.
 */
static void keyring_revoke(struct key *keyring)
{
	struct keyring_list *klist;

	klist = rcu_dereference_locked_keyring(keyring);

	/* adjust the quota */
	key_payload_reserve(keyring, 0);

	if (klist) {
		rcu_assign_pointer(keyring->payload.subscriptions, NULL);
		call_rcu(&klist->rcu, keyring_clear_rcu_disposal);
	}
}

/*
 * Collect garbage from the contents of a keyring, replacing the old list with
 * a new one with the pointers all shuffled down.
 *
 * Dead keys are classed as oned that are flagged as being dead or are revoked,
 * expired or negative keys that were revoked or expired before the specified
 * limit.
 */
void keyring_gc(struct key *keyring, time_t limit)
{
	struct keyring_list *klist, *new;
	struct key *key;
	int loop, keep, max;

	kenter("{%x,%s}", key_serial(keyring), keyring->description);

	down_write(&keyring->sem);

	klist = rcu_dereference_locked_keyring(keyring);
	if (!klist)
		goto no_klist;

	/* work out how many subscriptions we're keeping */
	keep = 0;
	for (loop = klist->nkeys - 1; loop >= 0; loop--)
		if (!key_is_dead(rcu_deref_link_locked(klist, loop, keyring),
				 limit))
			keep++;

	if (keep == klist->nkeys)
		goto just_return;

	/* allocate a new keyring payload */
	max = roundup(keep, 4);
	new = kmalloc(sizeof(struct keyring_list) + max * sizeof(struct key *),
		      GFP_KERNEL);
	if (!new)
		goto nomem;
	new->maxkeys = max;
	new->nkeys = 0;
	new->delkey = 0;

	/* install the live keys
	 * - must take care as expired keys may be updated back to life
	 */
	keep = 0;
	for (loop = klist->nkeys - 1; loop >= 0; loop--) {
		key = rcu_deref_link_locked(klist, loop, keyring);
		if (!key_is_dead(key, limit)) {
			if (keep >= max)
				goto discard_new;
			RCU_INIT_POINTER(new->keys[keep++], key_get(key));
		}
	}
	new->nkeys = keep;

	/* adjust the quota */
	key_payload_reserve(keyring,
			    sizeof(struct keyring_list) +
			    KEYQUOTA_LINK_BYTES * keep);

	if (keep == 0) {
		rcu_assign_pointer(keyring->payload.subscriptions, NULL);
		kfree(new);
	} else {
		rcu_assign_pointer(keyring->payload.subscriptions, new);
	}

	up_write(&keyring->sem);

	call_rcu(&klist->rcu, keyring_clear_rcu_disposal);
	kleave(" [yes]");
	return;

discard_new:
	new->nkeys = keep;
	keyring_clear_rcu_disposal(&new->rcu);
	up_write(&keyring->sem);
	kleave(" [discard]");
	return;

just_return:
	up_write(&keyring->sem);
	kleave(" [no dead]");
	return;

no_klist:
	up_write(&keyring->sem);
	kleave(" [no_klist]");
	return;

nomem:
	up_write(&keyring->sem);
	kleave(" [oom]");
}
