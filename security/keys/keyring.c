/* keyring.c: keyring handling
 *
 * Copyright (C) 2004-5 Red Hat, Inc. All Rights Reserved.
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
static int keyring_match(const struct key *keyring, const void *criterion);
static void keyring_destroy(struct key *keyring);
static void keyring_describe(const struct key *keyring, struct seq_file *m);
static long keyring_read(const struct key *keyring,
			 char __user *buffer, size_t buflen);

struct key_type key_type_keyring = {
	.name		= "keyring",
	.def_datalen	= sizeof(struct keyring_list),
	.instantiate	= keyring_instantiate,
	.match		= keyring_match,
	.destroy	= keyring_destroy,
	.describe	= keyring_describe,
	.read		= keyring_read,
};

/*
 * semaphore to serialise link/link calls to prevent two link calls in parallel
 * introducing a cycle
 */
static DECLARE_RWSEM(keyring_serialise_link_sem);

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

		if (keyring->type_data.link.next != NULL &&
		    !list_empty(&keyring->type_data.link))
			list_del(&keyring->type_data.link);

		write_unlock(&keyring_name_lock);
	}

	klist = rcu_dereference(keyring->payload.subscriptions);
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

	rcu_read_lock();
	klist = rcu_dereference(keyring->payload.subscriptions);
	if (klist)
		seq_printf(m, ": %u/%u", klist->nkeys, klist->maxkeys);
	else
		seq_puts(m, ": empty");
	rcu_read_unlock();

} /* end keyring_describe() */

/*****************************************************************************/
/*
 * read a list of key IDs from the keyring's contents
 * - the keyring's semaphore is read-locked
 */
static long keyring_read(const struct key *keyring,
			 char __user *buffer, size_t buflen)
{
	struct keyring_list *klist;
	struct key *key;
	size_t qty, tmp;
	int loop, ret;

	ret = 0;
	klist = rcu_dereference(keyring->payload.subscriptions);

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
			  struct task_struct *ctx, int not_in_quota,
			  struct key *dest)
{
	struct key *keyring;
	int ret;

	keyring = key_alloc(&key_type_keyring, description,
			    uid, gid, ctx,
			    (KEY_POS_ALL & ~KEY_POS_SETATTR) | KEY_USR_ALL,
			    not_in_quota);

	if (!IS_ERR(keyring)) {
		ret = key_instantiate_and_link(keyring, NULL, 0, dest, NULL);
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
 * - we rely on RCU to prevent the keyring lists from disappearing on us
 * - we return -EAGAIN if we didn't find any matching key
 * - we return -ENOKEY if we only found negative matching keys
 * - we propagate the possession attribute from the keyring ref to the key ref
 */
key_ref_t keyring_search_aux(key_ref_t keyring_ref,
			     struct task_struct *context,
			     struct key_type *type,
			     const void *description,
			     key_match_func_t match)
{
	struct {
		struct keyring_list *keylist;
		int kix;
	} stack[KEYRING_SEARCH_MAX_DEPTH];

	struct keyring_list *keylist;
	struct timespec now;
	unsigned long possessed;
	struct key *keyring, *key;
	key_ref_t key_ref;
	long err;
	int sp, kix;

	keyring = key_ref_to_ptr(keyring_ref);
	possessed = is_key_possessed(keyring_ref);
	key_check(keyring);

	/* top keyring must have search permission to begin the search */
        err = key_task_permission(keyring_ref, context, KEY_SEARCH);
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

	/* start processing a new keyring */
descend:
	if (test_bit(KEY_FLAG_REVOKED, &keyring->flags))
		goto not_this_keyring;

	keylist = rcu_dereference(keyring->payload.subscriptions);
	if (!keylist)
		goto not_this_keyring;

	/* iterate through the keys in this keyring first */
	for (kix = 0; kix < keylist->nkeys; kix++) {
		key = keylist->keys[kix];

		/* ignore keys not of this type */
		if (key->type != type)
			continue;

		/* skip revoked keys and expired keys */
		if (test_bit(KEY_FLAG_REVOKED, &key->flags))
			continue;

		if (key->expiry && now.tv_sec >= key->expiry)
			continue;

		/* keys that don't match */
		if (!match(key, description))
			continue;

		/* key must have search permissions */
		if (key_task_permission(make_key_ref(key, possessed),
					context, KEY_SEARCH) < 0)
			continue;

		/* we set a different error code if we find a negative key */
		if (test_bit(KEY_FLAG_NEGATIVE, &key->flags)) {
			err = -ENOKEY;
			continue;
		}

		goto found;
	}

	/* search through the keyrings nested in this one */
	kix = 0;
ascend:
	for (; kix < keylist->nkeys; kix++) {
		key = keylist->keys[kix];
		if (key->type != &key_type_keyring)
			continue;

		/* recursively search nested keyrings
		 * - only search keyrings for which we have search permission
		 */
		if (sp >= KEYRING_SEARCH_MAX_DEPTH)
			continue;

		if (key_task_permission(make_key_ref(key, possessed),
					context, KEY_SEARCH) < 0)
			continue;

		/* stack the current position */
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
		keylist = stack[sp].keylist;
		kix = stack[sp].kix + 1;
		goto ascend;
	}

	key_ref = ERR_PTR(err);
	goto error_2;

	/* we found a viable match */
found:
	atomic_inc(&key->usage);
	key_check(key);
	key_ref = make_key_ref(key, possessed);
error_2:
	rcu_read_unlock();
error:
	return key_ref;

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
key_ref_t keyring_search(key_ref_t keyring,
			 struct key_type *type,
			 const char *description)
{
	if (!type->match)
		return ERR_PTR(-ENOKEY);

	return keyring_search_aux(keyring, current,
				  type, description, type->match);

} /* end keyring_search() */

EXPORT_SYMBOL(keyring_search);

/*****************************************************************************/
/*
 * search the given keyring only (no recursion)
 * - keyring must be locked by caller
 * - caller must guarantee that the keyring is a keyring
 */
key_ref_t __keyring_search_one(key_ref_t keyring_ref,
			       const struct key_type *ktype,
			       const char *description,
			       key_perm_t perm)
{
	struct keyring_list *klist;
	unsigned long possessed;
	struct key *keyring, *key;
	int loop;

	keyring = key_ref_to_ptr(keyring_ref);
	possessed = is_key_possessed(keyring_ref);

	rcu_read_lock();

	klist = rcu_dereference(keyring->payload.subscriptions);
	if (klist) {
		for (loop = 0; loop < klist->nkeys; loop++) {
			key = klist->keys[loop];

			if (key->type == ktype &&
			    (!key->type->match ||
			     key->type->match(key, description)) &&
			    key_permission(make_key_ref(key, possessed),
					   perm) == 0 &&
			    !test_bit(KEY_FLAG_REVOKED, &key->flags)
			    )
				goto found;
		}
	}

	rcu_read_unlock();
	return ERR_PTR(-ENOKEY);

 found:
	atomic_inc(&key->usage);
	rcu_read_unlock();
	return make_key_ref(key, possessed);

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
			if (test_bit(KEY_FLAG_REVOKED, &keyring->flags))
				continue;

			if (strcmp(keyring->description, name) != 0)
				continue;

			if (key_permission(make_key_ref(keyring, 0),
					   KEY_SEARCH) < 0)
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
		struct keyring_list *keylist;
		int kix;
	} stack[KEYRING_SEARCH_MAX_DEPTH];

	struct keyring_list *keylist;
	struct key *subtree, *key;
	int sp, kix, ret;

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
	for (; kix < keylist->nkeys; kix++) {
		key = keylist->keys[kix];

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

} /* end keyring_detect_cycle() */

/*****************************************************************************/
/*
 * dispose of a keyring list after the RCU grace period
 */
static void keyring_link_rcu_disposal(struct rcu_head *rcu)
{
	struct keyring_list *klist =
		container_of(rcu, struct keyring_list, rcu);

	kfree(klist);

} /* end keyring_link_rcu_disposal() */

/*****************************************************************************/
/*
 * dispose of a keyring list after the RCU grace period, freeing the unlinked
 * key
 */
static void keyring_unlink_rcu_disposal(struct rcu_head *rcu)
{
	struct keyring_list *klist =
		container_of(rcu, struct keyring_list, rcu);

	key_put(klist->keys[klist->delkey]);
	kfree(klist);

} /* end keyring_unlink_rcu_disposal() */

/*****************************************************************************/
/*
 * link a key into to a keyring
 * - must be called with the keyring's semaphore write-locked
 * - discard already extant link to matching key if there is one
 */
int __key_link(struct key *keyring, struct key *key)
{
	struct keyring_list *klist, *nklist;
	unsigned max;
	size_t size;
	int loop, ret;

	ret = -EKEYREVOKED;
	if (test_bit(KEY_FLAG_REVOKED, &keyring->flags))
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

	/* see if there's a matching key we can displace */
	klist = keyring->payload.subscriptions;

	if (klist && klist->nkeys > 0) {
		struct key_type *type = key->type;

		for (loop = klist->nkeys - 1; loop >= 0; loop--) {
			if (klist->keys[loop]->type == type &&
			    strcmp(klist->keys[loop]->description,
				   key->description) == 0
			    ) {
				/* found a match - replace with new key */
				size = sizeof(struct key *) * klist->maxkeys;
				size += sizeof(*klist);
				BUG_ON(size > PAGE_SIZE);

				ret = -ENOMEM;
				nklist = kmalloc(size, GFP_KERNEL);
				if (!nklist)
					goto error2;

				memcpy(nklist, klist, size);

				/* replace matched key */
				atomic_inc(&key->usage);
				nklist->keys[loop] = key;

				rcu_assign_pointer(
					keyring->payload.subscriptions,
					nklist);

				/* dispose of the old keyring list and the
				 * displaced key */
				klist->delkey = loop;
				call_rcu(&klist->rcu,
					 keyring_unlink_rcu_disposal);

				goto done;
			}
		}
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

		klist->keys[klist->nkeys] = key;
		smp_wmb();
		klist->nkeys++;
		smp_wmb();
	}
	else {
		/* grow the key list */
		max = 4;
		if (klist)
			max += klist->maxkeys;

		ret = -ENFILE;
		if (max > 65535)
			goto error3;
		size = sizeof(*klist) + sizeof(struct key *) * max;
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
		nklist->keys[nklist->nkeys++] = key;

		rcu_assign_pointer(keyring->payload.subscriptions, nklist);

		/* dispose of the old keyring list */
		if (klist)
			call_rcu(&klist->rcu, keyring_link_rcu_disposal);
	}

done:
	ret = 0;
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
	struct keyring_list *klist, *nklist;
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

} /* end key_unlink() */

EXPORT_SYMBOL(key_unlink);

/*****************************************************************************/
/*
 * dispose of a keyring list after the RCU grace period, releasing the keys it
 * links to
 */
static void keyring_clear_rcu_disposal(struct rcu_head *rcu)
{
	struct keyring_list *klist;
	int loop;

	klist = container_of(rcu, struct keyring_list, rcu);

	for (loop = klist->nkeys - 1; loop >= 0; loop--)
		key_put(klist->keys[loop]);

	kfree(klist);

} /* end keyring_clear_rcu_disposal() */

/*****************************************************************************/
/*
 * clear the specified process keyring
 * - implements keyctl(KEYCTL_CLEAR)
 */
int keyring_clear(struct key *keyring)
{
	struct keyring_list *klist;
	int ret;

	ret = -ENOTDIR;
	if (keyring->type == &key_type_keyring) {
		/* detach the pointer block with the locks held */
		down_write(&keyring->sem);

		klist = keyring->payload.subscriptions;
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

} /* end keyring_clear() */

EXPORT_SYMBOL(keyring_clear);
