/* keyring.c: keyring handling
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

/*
 * when plumbing the depths of the key tree, this sets a hard limit set on how
 * deep we're willing to go
 */
#define KEYRING_SEARCH_MAX_DEPTH 6

/*
 * we keep all named keyrings in a hash to speed looking them up
 */
#define KEYRING_NAME_HASH_SIZE	(1 << 5)

static struct list_head	keyring_name_hash[KEYRING_NAME_HASH_SIZE];
static DEFINE_RWLOCK(keyring_name_lock);

static inline unsigned keyring_hash(const char *desc)
{
	unsigned bucket = 0;

	for (; *desc; desc++)
		bucket += (unsigned char) *desc;

	return bucket & (KEYRING_NAME_HASH_SIZE - 1);
}

/*
 * the keyring type definition
 */
static int keyring_instantiate(struct key *keyring,
			       const void *data, size_t datalen);
static int keyring_duplicate(struct key *keyring, const struct key *source);
static int keyring_match(const struct key *keyring, const void *criterion);
static void keyring_destroy(struct key *keyring);
static void keyring_describe(const struct key *keyring, struct seq_file *m);
static long keyring_read(const struct key *keyring,
			 char __user *buffer, size_t buflen);

struct key_type key_type_keyring = {
	.name		= "keyring",
	.def_datalen	= sizeof(struct keyring_list),
	.instantiate	= keyring_instantiate,
	.duplicate	= keyring_duplicate,
	.match		= keyring_match,
	.destroy	= keyring_destroy,
	.describe	= keyring_describe,
	.read		= keyring_read,
};

/*
 * semaphore to serialise link/link calls to prevent two link calls in parallel
 * introducing a cycle
 */
DECLARE_RWSEM(keyring_serialise_link_sem);

/*****************************************************************************/
/*
 * publish the name of a keyring so that it can be found by name (if it has
 * one)
 */
void keyring_publish_name(struct key *keyring)
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

} /* end keyring_publish_name() */

/*****************************************************************************/
/*
 * initialise a keyring
 * - we object if we were given any data
 */
static int keyring_instantiate(struct key *keyring,
			       const void *data, size_t datalen)
{
	int ret;

	ret = -EINVAL;
	if (datalen == 0) {
		/* make the keyring available by name if it has one */
		keyring_publish_name(keyring);
		ret = 0;
	}

	return ret;

} /* end keyring_instantiate() */

/*****************************************************************************/
/*
 * duplicate the list of subscribed keys from a source keyring into this one
 */
static int keyring_duplicate(struct key *keyring, const struct key *source)
{
	struct keyring_list *sklist, *klist;
	unsigned max;
	size_t size;
	int loop, ret;

	const unsigned limit =
		(PAGE_SIZE - sizeof(*klist)) / sizeof(struct key);

	ret = 0;
	sklist = source->payload.subscriptions;

	if (sklist && sklist->nkeys > 0) {
		max = sklist->nkeys;
		BUG_ON(max > limit);

		max = (max + 3) & ~3;
		if (max > limit)
			max = limit;

		ret = -ENOMEM;
		size = sizeof(*klist) + sizeof(struct key) * max;
		klist = kmalloc(size, GFP_KERNEL);
		if (!klist)
			goto error;

		klist->maxkeys = max;
		klist->nkeys = sklist->nkeys;
		memcpy(klist->keys,
		       sklist->keys,
		       sklist->nkeys * sizeof(struct key));

		for (loop = klist->nkeys - 1; loop >= 0; loop--)
			atomic_inc(&klist->keys[loop]->usage);

		keyring->payload.subscriptions = klist;
		ret = 0;
	}

 error:
	return ret;

} /* end keyring_duplicate() */

/*****************************************************************************/
/*
 * match keyrings on their name
 */
static int keyring_match(const struct key *keyring, const void *description)
{
	return keyring->description &&
		strcmp(keyring->description, description) == 0;

} /* end keyring_match() */

/*****************************************************************************/
/*
 * dispose of the data dangling from the corpse of a keyring
 */
static void keyring_destroy(struct key *keyring)
{
	struct keyring_list *klist;
	int loop;

	if (keyring->description) {
		write_lock(&keyring_name_lock);
		list_del(&keyring->type_data.link);
		write_unlock(&keyring_name_lock);
	}

	klist = keyring->payload.subscriptions;
	if (klist) {
		for (loop = klist->nkeys - 1; loop >= 0; loop--)
			key_put(klist->keys[loop]);
		kfree(klist);
	}

} /* end keyring_destroy() */

/*****************************************************************************/
/*
 * describe the keyring
 */
static void keyring_describe(const struct key *keyring, struct seq_file *m)
{
	struct keyring_list *klist;

	if (keyring->description) {
		seq_puts(m, keyring->description);
	}
	else {
		seq_puts(m, "[anon]");
	}

	klist = keyring->payload.subscriptions;
	if (klist)
		seq_printf(m, ": %u/%u", klist->nkeys, klist->maxkeys);
	else
		seq_puts(m, ": empty");

} /* end keyring_describe() */

/*****************************************************************************/
/*
 * read a list of key IDs from the keyring's contents
 */
static long keyring_read(const struct key *keyring,
			 char __user *buffer, size_t buflen)
{
	struct keyring_list *klist;
	struct key *key;
	size_t qty, tmp;
	int loop, ret;

	ret = 0;
	klist = keyring->payload.subscriptions;

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
				key = klist->keys[loop];

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

} /* end keyring_read() */

/*****************************************************************************/
/*
 * allocate a keyring and link into the destination keyring
 */
struct key *keyring_alloc(const char *description, uid_t uid, gid_t gid,
			  int not_in_quota, struct key *dest)
{
	struct key *keyring;
	int ret;

	keyring = key_alloc(&key_type_keyring, description,
			    uid, gid, KEY_USR_ALL, not_in_quota);

	if (!IS_ERR(keyring)) {
		ret = key_instantiate_and_link(keyring, NULL, 0, dest);
		if (ret < 0) {
			key_put(keyring);
			keyring = ERR_PTR(ret);
		}
	}

	return keyring;

} /* end keyring_alloc() */

/*****************************************************************************/
/*
 * search the supplied keyring tree for a key that matches the criterion
 * - perform a breadth-then-depth search up to the prescribed limit
 * - we only find keys on which we have search permission
 * - we use the supplied match function to see if the description (or other
 *   feature of interest) matches
 * - we readlock the keyrings as we search down the tree
 * - we return -EAGAIN if we didn't find any matching key
 * - we return -ENOKEY if we only found negative matching keys
 */
struct key *keyring_search_aux(struct key *keyring,
			       struct key_type *type,
			       const void *description,
			       key_match_func_t match)
{
	struct {
		struct key *keyring;
		int kix;
	} stack[KEYRING_SEARCH_MAX_DEPTH];

	struct keyring_list *keylist;
	struct timespec now;
	struct key *key;
	long err;
	int sp, psp, kix;

	key_check(keyring);

	/* top keyring must have search permission to begin the search */
	key = ERR_PTR(-EACCES);
	if (!key_permission(keyring, KEY_SEARCH))
		goto error;

	key = ERR_PTR(-ENOTDIR);
	if (keyring->type != &key_type_keyring)
		goto error;

	now = current_kernel_time();
	err = -EAGAIN;
	sp = 0;

	/* start processing a new keyring */
 descend:
	read_lock(&keyring->lock);
	if (keyring->flags & KEY_FLAG_REVOKED)
		goto not_this_keyring;

	keylist = keyring->payload.subscriptions;
	if (!keylist)
		goto not_this_keyring;

	/* iterate through the keys in this keyring first */
	for (kix = 0; kix < keylist->nkeys; kix++) {
		key = keylist->keys[kix];

		/* ignore keys not of this type */
		if (key->type != type)
			continue;

		/* skip revoked keys and expired keys */
		if (key->flags & KEY_FLAG_REVOKED)
			continue;

		if (key->expiry && now.tv_sec >= key->expiry)
			continue;

		/* keys that don't match */
		if (!match(key, description))
			continue;

		/* key must have search permissions */
		if (!key_permission(key, KEY_SEARCH))
			continue;

		/* we set a different error code if we find a negative key */
		if (key->flags & KEY_FLAG_NEGATIVE) {
			err = -ENOKEY;
			continue;
		}

		goto found;
	}

	/* search through the keyrings nested in this one */
	kix = 0;
 ascend:
	while (kix < keylist->nkeys) {
		key = keylist->keys[kix];
		if (key->type != &key_type_keyring)
			goto next;

		/* recursively search nested keyrings
		 * - only search keyrings for which we have search permission
		 */
		if (sp >= KEYRING_SEARCH_MAX_DEPTH)
			goto next;

		if (!key_permission(key, KEY_SEARCH))
			goto next;

		/* evade loops in the keyring tree */
		for (psp = 0; psp < sp; psp++)
			if (stack[psp].keyring == keyring)
				goto next;

		/* stack the current position */
		stack[sp].keyring = keyring;
		stack[sp].kix = kix;
		sp++;

		/* begin again with the new keyring */
		keyring = key;
		goto descend;

	next:
		kix++;
	}

	/* the keyring we're looking at was disqualified or didn't contain a
	 * matching key */
 not_this_keyring:
	read_unlock(&keyring->lock);

	if (sp > 0) {
		/* resume the processing of a keyring higher up in the tree */
		sp--;
		keyring = stack[sp].keyring;
		keylist = keyring->payload.subscriptions;
		kix = stack[sp].kix + 1;
		goto ascend;
	}

	key = ERR_PTR(err);
	goto error;

	/* we found a viable match */
 found:
	atomic_inc(&key->usage);
	read_unlock(&keyring->lock);

	/* unwind the keyring stack */
	while (sp > 0) {
		sp--;
		read_unlock(&stack[sp].keyring->lock);
	}

	key_check(key);
 error:
	return key;

} /* end keyring_search_aux() */

/*****************************************************************************/
/*
 * search the supplied keyring tree for a key that matches the criterion
 * - perform a breadth-then-depth search up to the prescribed limit
 * - we only find keys on which we have search permission
 * - we readlock the keyrings as we search down the tree
 * - we return -EAGAIN if we didn't find any matching key
 * - we return -ENOKEY if we only found negative matching keys
 */
struct key *keyring_search(struct key *keyring,
			   struct key_type *type,
			   const char *description)
{
	return keyring_search_aux(keyring, type, description, type->match);

} /* end keyring_search() */

EXPORT_SYMBOL(keyring_search);

/*****************************************************************************/
/*
 * search the given keyring only (no recursion)
 * - keyring must be locked by caller
 */
struct key *__keyring_search_one(struct key *keyring,
				 const struct key_type *ktype,
				 const char *description,
				 key_perm_t perm)
{
	struct keyring_list *klist;
	struct key *key;
	int loop;

	klist = keyring->payload.subscriptions;
	if (klist) {
		for (loop = 0; loop < klist->nkeys; loop++) {
			key = klist->keys[loop];

			if (key->type == ktype &&
			    key->type->match(key, description) &&
			    key_permission(key, perm) &&
			    !(key->flags & KEY_FLAG_REVOKED)
			    )
				goto found;
		}
	}

	key = ERR_PTR(-ENOKEY);
	goto error;

 found:
	atomic_inc(&key->usage);
 error:
	return key;

} /* end __keyring_search_one() */

/*****************************************************************************/
/*
 * find a keyring with the specified name
 * - all named keyrings are searched
 * - only find keyrings with search permission for the process
 * - only find keyrings with a serial number greater than the one specified
 */
struct key *find_keyring_by_name(const char *name, key_serial_t bound)
{
	struct key *keyring;
	int bucket;

	keyring = ERR_PTR(-EINVAL);
	if (!name)
		goto error;

	bucket = keyring_hash(name);

	read_lock(&keyring_name_lock);

	if (keyring_name_hash[bucket].next) {
		/* search this hash bucket for a keyring with a matching name
		 * that's readable and that hasn't been revoked */
		list_for_each_entry(keyring,
				    &keyring_name_hash[bucket],
				    type_data.link
				    ) {
			if (keyring->flags & KEY_FLAG_REVOKED)
				continue;

			if (strcmp(keyring->description, name) != 0)
				continue;

			if (!key_permission(keyring, KEY_SEARCH))
				continue;

			/* found a potential candidate, but we still need to
			 * check the serial number */
			if (keyring->serial <= bound)
				continue;

			/* we've got a match */
			atomic_inc(&keyring->usage);
			read_unlock(&keyring_name_lock);
			goto error;
		}
	}

	read_unlock(&keyring_name_lock);
	keyring = ERR_PTR(-ENOKEY);

 error:
	return keyring;

} /* end find_keyring_by_name() */

/*****************************************************************************/
/*
 * see if a cycle will will be created by inserting acyclic tree B in acyclic
 * tree A at the topmost level (ie: as a direct child of A)
 * - since we are adding B to A at the top level, checking for cycles should
 *   just be a matter of seeing if node A is somewhere in tree B
 */
static int keyring_detect_cycle(struct key *A, struct key *B)
{
	struct {
		struct key *subtree;
		int kix;
	} stack[KEYRING_SEARCH_MAX_DEPTH];

	struct keyring_list *keylist;
	struct key *subtree, *key;
	int sp, kix, ret;

	ret = -EDEADLK;
	if (A == B)
		goto error;

	subtree = B;
	sp = 0;

	/* start processing a new keyring */
 descend:
	read_lock(&subtree->lock);
	if (subtree->flags & KEY_FLAG_REVOKED)
		goto not_this_keyring;

	keylist = subtree->payload.subscriptions;
	if (!keylist)
		goto not_this_keyring;
	kix = 0;

 ascend:
	/* iterate through the remaining keys in this keyring */
	for (; kix < keylist->nkeys; kix++) {
		key = keylist->keys[kix];

		if (key == A)
			goto cycle_detected;

		/* recursively check nested keyrings */
		if (key->type == &key_type_keyring) {
			if (sp >= KEYRING_SEARCH_MAX_DEPTH)
				goto too_deep;

			/* stack the current position */
			stack[sp].subtree = subtree;
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
	read_unlock(&subtree->lock);

	if (sp > 0) {
		/* resume the checking of a keyring higher up in the tree */
		sp--;
		subtree = stack[sp].subtree;
		keylist = subtree->payload.subscriptions;
		kix = stack[sp].kix + 1;
		goto ascend;
	}

	ret = 0; /* no cycles detected */

 error:
	return ret;

 too_deep:
	ret = -ELOOP;
	goto error_unwind;
 cycle_detected:
	ret = -EDEADLK;
 error_unwind:
	read_unlock(&subtree->lock);

	/* unwind the keyring stack */
	while (sp > 0) {
		sp--;
		read_unlock(&stack[sp].subtree->lock);
	}

	goto error;

} /* end keyring_detect_cycle() */

/*****************************************************************************/
/*
 * link a key into to a keyring
 * - must be called with the keyring's semaphore held
 */
int __key_link(struct key *keyring, struct key *key)
{
	struct keyring_list *klist, *nklist;
	unsigned max;
	size_t size;
	int ret;

	ret = -EKEYREVOKED;
	if (keyring->flags & KEY_FLAG_REVOKED)
		goto error;

	ret = -ENOTDIR;
	if (keyring->type != &key_type_keyring)
		goto error;

	/* serialise link/link calls to prevent parallel calls causing a
	 * cycle when applied to two keyring in opposite orders */
	down_write(&keyring_serialise_link_sem);

	/* check that we aren't going to create a cycle adding one keyring to
	 * another */
	if (key->type == &key_type_keyring) {
		ret = keyring_detect_cycle(keyring, key);
		if (ret < 0)
			goto error2;
	}

	/* check that we aren't going to overrun the user's quota */
	ret = key_payload_reserve(keyring,
				  keyring->datalen + KEYQUOTA_LINK_BYTES);
	if (ret < 0)
		goto error2;

	klist = keyring->payload.subscriptions;

	if (klist && klist->nkeys < klist->maxkeys) {
		/* there's sufficient slack space to add directly */
		atomic_inc(&key->usage);

		write_lock(&keyring->lock);
		klist->keys[klist->nkeys++] = key;
		write_unlock(&keyring->lock);

		ret = 0;
	}
	else {
		/* grow the key list */
		max = 4;
		if (klist)
			max += klist->maxkeys;

		ret = -ENFILE;
		size = sizeof(*klist) + sizeof(*key) * max;
		if (size > PAGE_SIZE)
			goto error3;

		ret = -ENOMEM;
		nklist = kmalloc(size, GFP_KERNEL);
		if (!nklist)
			goto error3;
		nklist->maxkeys = max;
		nklist->nkeys = 0;

		if (klist) {
			nklist->nkeys = klist->nkeys;
			memcpy(nklist->keys,
			       klist->keys,
			       sizeof(struct key *) * klist->nkeys);
		}

		/* add the key into the new space */
		atomic_inc(&key->usage);

		write_lock(&keyring->lock);
		keyring->payload.subscriptions = nklist;
		nklist->keys[nklist->nkeys++] = key;
		write_unlock(&keyring->lock);

		/* dispose of the old keyring list */
		kfree(klist);

		ret = 0;
	}

 error2:
	up_write(&keyring_serialise_link_sem);
 error:
	return ret;

 error3:
	/* undo the quota changes */
	key_payload_reserve(keyring,
			    keyring->datalen - KEYQUOTA_LINK_BYTES);
	goto error2;

} /* end __key_link() */

/*****************************************************************************/
/*
 * link a key to a keyring
 */
int key_link(struct key *keyring, struct key *key)
{
	int ret;

	key_check(keyring);
	key_check(key);

	down_write(&keyring->sem);
	ret = __key_link(keyring, key);
	up_write(&keyring->sem);

	return ret;

} /* end key_link() */

EXPORT_SYMBOL(key_link);

/*****************************************************************************/
/*
 * unlink the first link to a key from a keyring
 */
int key_unlink(struct key *keyring, struct key *key)
{
	struct keyring_list *klist;
	int loop, ret;

	key_check(keyring);
	key_check(key);

	ret = -ENOTDIR;
	if (keyring->type != &key_type_keyring)
		goto error;

	down_write(&keyring->sem);

	klist = keyring->payload.subscriptions;
	if (klist) {
		/* search the keyring for the key */
		for (loop = 0; loop < klist->nkeys; loop++)
			if (klist->keys[loop] == key)
				goto key_is_present;
	}

	up_write(&keyring->sem);
	ret = -ENOENT;
	goto error;

 key_is_present:
	/* adjust the user's quota */
	key_payload_reserve(keyring,
			    keyring->datalen - KEYQUOTA_LINK_BYTES);

	/* shuffle down the key pointers
	 * - it might be worth shrinking the allocated memory, but that runs
	 *   the risk of ENOMEM as we would have to copy
	 */
	write_lock(&keyring->lock);

	klist->nkeys--;
	if (loop < klist->nkeys)
		memcpy(&klist->keys[loop],
		       &klist->keys[loop + 1],
		       (klist->nkeys - loop) * sizeof(struct key *));

	write_unlock(&keyring->lock);

	up_write(&keyring->sem);
	key_put(key);
	ret = 0;

 error:
	return ret;

} /* end key_unlink() */

EXPORT_SYMBOL(key_unlink);

/*****************************************************************************/
/*
 * clear the specified process keyring
 * - implements keyctl(KEYCTL_CLEAR)
 */
int keyring_clear(struct key *keyring)
{
	struct keyring_list *klist;
	int loop, ret;

	ret = -ENOTDIR;
	if (keyring->type == &key_type_keyring) {
		/* detach the pointer block with the locks held */
		down_write(&keyring->sem);

		klist = keyring->payload.subscriptions;
		if (klist) {
			/* adjust the quota */
			key_payload_reserve(keyring,
					    sizeof(struct keyring_list));

			write_lock(&keyring->lock);
			keyring->payload.subscriptions = NULL;
			write_unlock(&keyring->lock);
		}

		up_write(&keyring->sem);

		/* free the keys after the locks have been dropped */
		if (klist) {
			for (loop = klist->nkeys - 1; loop >= 0; loop--)
				key_put(klist->keys[loop]);

			kfree(klist);
		}

		ret = 0;
	}

	return ret;

} /* end keyring_clear() */

EXPORT_SYMBOL(keyring_clear);
