/* Key garbage collector
 *
 * Copyright (C) 2009-2011 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/security.h>
#include <keys/keyring-type.h>
#include "internal.h"

/*
 * Delay between key revocation/expiry in seconds
 */
unsigned key_gc_delay = 5 * 60;

/*
 * Reaper for unused keys.
 */
static void key_garbage_collector(struct work_struct *work);
DECLARE_WORK(key_gc_work, key_garbage_collector);

/*
 * Reaper for links from keyrings to dead keys.
 */
static void key_gc_timer_func(unsigned long);
static DEFINE_TIMER(key_gc_timer, key_gc_timer_func, 0, 0);

static time_t key_gc_next_run = LONG_MAX;
static struct key_type *key_gc_dead_keytype;

static unsigned long key_gc_flags;
#define KEY_GC_KEY_EXPIRED	0	/* A key expired and needs unlinking */
#define KEY_GC_REAP_KEYTYPE	1	/* A keytype is being unregistered */
#define KEY_GC_REAPING_KEYTYPE	2	/* Cleared when keytype reaped */


/*
 * Any key whose type gets unregistered will be re-typed to this if it can't be
 * immediately unlinked.
 */
struct key_type key_type_dead = {
	.name = ".dead",
};

/*
 * Schedule a garbage collection run.
 * - time precision isn't particularly important
 */
void key_schedule_gc(time_t gc_at)
{
	unsigned long expires;
	time_t now = current_kernel_time().tv_sec;

	kenter("%ld", gc_at - now);

	if (gc_at <= now || test_bit(KEY_GC_REAP_KEYTYPE, &key_gc_flags)) {
		kdebug("IMMEDIATE");
		schedule_work(&key_gc_work);
	} else if (gc_at < key_gc_next_run) {
		kdebug("DEFERRED");
		key_gc_next_run = gc_at;
		expires = jiffies + (gc_at - now) * HZ;
		mod_timer(&key_gc_timer, expires);
	}
}

/*
 * Schedule a dead links collection run.
 */
void key_schedule_gc_links(void)
{
	set_bit(KEY_GC_KEY_EXPIRED, &key_gc_flags);
	schedule_work(&key_gc_work);
}

/*
 * Some key's cleanup time was met after it expired, so we need to get the
 * reaper to go through a cycle finding expired keys.
 */
static void key_gc_timer_func(unsigned long data)
{
	kenter("");
	key_gc_next_run = LONG_MAX;
	key_schedule_gc_links();
}

/*
 * Reap keys of dead type.
 *
 * We use three flags to make sure we see three complete cycles of the garbage
 * collector: the first to mark keys of that type as being dead, the second to
 * collect dead links and the third to clean up the dead keys.  We have to be
 * careful as there may already be a cycle in progress.
 *
 * The caller must be holding key_types_sem.
 */
void key_gc_keytype(struct key_type *ktype)
{
	kenter("%s", ktype->name);

	key_gc_dead_keytype = ktype;
	set_bit(KEY_GC_REAPING_KEYTYPE, &key_gc_flags);
	smp_mb();
	set_bit(KEY_GC_REAP_KEYTYPE, &key_gc_flags);

	kdebug("schedule");
	schedule_work(&key_gc_work);

	kdebug("sleep");
	wait_on_bit(&key_gc_flags, KEY_GC_REAPING_KEYTYPE,
		    TASK_UNINTERRUPTIBLE);

	key_gc_dead_keytype = NULL;
	kleave("");
}

/*
 * Garbage collect a list of unreferenced, detached keys
 */
static noinline void key_gc_unused_keys(struct list_head *keys)
{
	while (!list_empty(keys)) {
		struct key *key =
			list_entry(keys->next, struct key, graveyard_link);
		list_del(&key->graveyard_link);

		kdebug("- %u", key->serial);
		key_check(key);

		/* Throw away the key data if the key is instantiated */
		if (test_bit(KEY_FLAG_INSTANTIATED, &key->flags) &&
		    !test_bit(KEY_FLAG_NEGATIVE, &key->flags) &&
		    key->type->destroy)
			key->type->destroy(key);

		security_key_free(key);

		/* deal with the user's key tracking and quota */
		if (test_bit(KEY_FLAG_IN_QUOTA, &key->flags)) {
			spin_lock(&key->user->lock);
			key->user->qnkeys--;
			key->user->qnbytes -= key->quotalen;
			spin_unlock(&key->user->lock);
		}

		atomic_dec(&key->user->nkeys);
		if (test_bit(KEY_FLAG_INSTANTIATED, &key->flags))
			atomic_dec(&key->user->nikeys);

		key_user_put(key->user);

		kfree(key->description);

#ifdef KEY_DEBUGGING
		key->magic = KEY_DEBUG_MAGIC_X;
#endif
		kmem_cache_free(key_jar, key);
	}
}

/*
 * Garbage collector for unused keys.
 *
 * This is done in process context so that we don't have to disable interrupts
 * all over the place.  key_put() schedules this rather than trying to do the
 * cleanup itself, which means key_put() doesn't have to sleep.
 */
static void key_garbage_collector(struct work_struct *work)
{
	static LIST_HEAD(graveyard);
	static u8 gc_state;		/* Internal persistent state */
#define KEY_GC_REAP_AGAIN	0x01	/* - Need another cycle */
#define KEY_GC_REAPING_LINKS	0x02	/* - We need to reap links */
#define KEY_GC_SET_TIMER	0x04	/* - We need to restart the timer */
#define KEY_GC_REAPING_DEAD_1	0x10	/* - We need to mark dead keys */
#define KEY_GC_REAPING_DEAD_2	0x20	/* - We need to reap dead key links */
#define KEY_GC_REAPING_DEAD_3	0x40	/* - We need to reap dead keys */
#define KEY_GC_FOUND_DEAD_KEY	0x80	/* - We found at least one dead key */

	struct rb_node *cursor;
	struct key *key;
	time_t new_timer, limit;

	kenter("[%lx,%x]", key_gc_flags, gc_state);

	limit = current_kernel_time().tv_sec;
	if (limit > key_gc_delay)
		limit -= key_gc_delay;
	else
		limit = key_gc_delay;

	/* Work out what we're going to be doing in this pass */
	gc_state &= KEY_GC_REAPING_DEAD_1 | KEY_GC_REAPING_DEAD_2;
	gc_state <<= 1;
	if (test_and_clear_bit(KEY_GC_KEY_EXPIRED, &key_gc_flags))
		gc_state |= KEY_GC_REAPING_LINKS | KEY_GC_SET_TIMER;

	if (test_and_clear_bit(KEY_GC_REAP_KEYTYPE, &key_gc_flags))
		gc_state |= KEY_GC_REAPING_DEAD_1;
	kdebug("new pass %x", gc_state);

	new_timer = LONG_MAX;

	/* As only this function is permitted to remove things from the key
	 * serial tree, if cursor is non-NULL then it will always point to a
	 * valid node in the tree - even if lock got dropped.
	 */
	spin_lock(&key_serial_lock);
	cursor = rb_first(&key_serial_tree);

continue_scanning:
	while (cursor) {
		key = rb_entry(cursor, struct key, serial_node);
		cursor = rb_next(cursor);

		if (atomic_read(&key->usage) == 0)
			goto found_unreferenced_key;

		if (unlikely(gc_state & KEY_GC_REAPING_DEAD_1)) {
			if (key->type == key_gc_dead_keytype) {
				gc_state |= KEY_GC_FOUND_DEAD_KEY;
				set_bit(KEY_FLAG_DEAD, &key->flags);
				key->perm = 0;
				goto skip_dead_key;
			}
		}

		if (gc_state & KEY_GC_SET_TIMER) {
			if (key->expiry > limit && key->expiry < new_timer) {
				kdebug("will expire %x in %ld",
				       key_serial(key), key->expiry - limit);
				new_timer = key->expiry;
			}
		}

		if (unlikely(gc_state & KEY_GC_REAPING_DEAD_2))
			if (key->type == key_gc_dead_keytype)
				gc_state |= KEY_GC_FOUND_DEAD_KEY;

		if ((gc_state & KEY_GC_REAPING_LINKS) ||
		    unlikely(gc_state & KEY_GC_REAPING_DEAD_2)) {
			if (key->type == &key_type_keyring)
				goto found_keyring;
		}

		if (unlikely(gc_state & KEY_GC_REAPING_DEAD_3))
			if (key->type == key_gc_dead_keytype)
				goto destroy_dead_key;

	skip_dead_key:
		if (spin_is_contended(&key_serial_lock) || need_resched())
			goto contended;
	}

contended:
	spin_unlock(&key_serial_lock);

maybe_resched:
	if (cursor) {
		cond_resched();
		spin_lock(&key_serial_lock);
		goto continue_scanning;
	}

	/* We've completed the pass.  Set the timer if we need to and queue a
	 * new cycle if necessary.  We keep executing cycles until we find one
	 * where we didn't reap any keys.
	 */
	kdebug("pass complete");

	if (gc_state & KEY_GC_SET_TIMER && new_timer != (time_t)LONG_MAX) {
		new_timer += key_gc_delay;
		key_schedule_gc(new_timer);
	}

	if (unlikely(gc_state & KEY_GC_REAPING_DEAD_2) ||
	    !list_empty(&graveyard)) {
		/* Make sure that all pending keyring payload destructions are
		 * fulfilled and that people aren't now looking at dead or
		 * dying keys that they don't have a reference upon or a link
		 * to.
		 */
		kdebug("gc sync");
		synchronize_rcu();
	}

	if (!list_empty(&graveyard)) {
		kdebug("gc keys");
		key_gc_unused_keys(&graveyard);
	}

	if (unlikely(gc_state & (KEY_GC_REAPING_DEAD_1 |
				 KEY_GC_REAPING_DEAD_2))) {
		if (!(gc_state & KEY_GC_FOUND_DEAD_KEY)) {
			/* No remaining dead keys: short circuit the remaining
			 * keytype reap cycles.
			 */
			kdebug("dead short");
			gc_state &= ~(KEY_GC_REAPING_DEAD_1 | KEY_GC_REAPING_DEAD_2);
			gc_state |= KEY_GC_REAPING_DEAD_3;
		} else {
			gc_state |= KEY_GC_REAP_AGAIN;
		}
	}

	if (unlikely(gc_state & KEY_GC_REAPING_DEAD_3)) {
		kdebug("dead wake");
		smp_mb();
		clear_bit(KEY_GC_REAPING_KEYTYPE, &key_gc_flags);
		wake_up_bit(&key_gc_flags, KEY_GC_REAPING_KEYTYPE);
	}

	if (gc_state & KEY_GC_REAP_AGAIN)
		schedule_work(&key_gc_work);
	kleave(" [end %x]", gc_state);
	return;

	/* We found an unreferenced key - once we've removed it from the tree,
	 * we can safely drop the lock.
	 */
found_unreferenced_key:
	kdebug("unrefd key %d", key->serial);
	rb_erase(&key->serial_node, &key_serial_tree);
	spin_unlock(&key_serial_lock);

	list_add_tail(&key->graveyard_link, &graveyard);
	gc_state |= KEY_GC_REAP_AGAIN;
	goto maybe_resched;

	/* We found a keyring and we need to check the payload for links to
	 * dead or expired keys.  We don't flag another reap immediately as we
	 * have to wait for the old payload to be destroyed by RCU before we
	 * can reap the keys to which it refers.
	 */
found_keyring:
	spin_unlock(&key_serial_lock);
	keyring_gc(key, limit);
	goto maybe_resched;

	/* We found a dead key that is still referenced.  Reset its type and
	 * destroy its payload with its semaphore held.
	 */
destroy_dead_key:
	spin_unlock(&key_serial_lock);
	kdebug("destroy key %d", key->serial);
	down_write(&key->sem);
	key->type = &key_type_dead;
	if (key_gc_dead_keytype->destroy)
		key_gc_dead_keytype->destroy(key);
	memset(&key->payload, KEY_DESTROY, sizeof(key->payload));
	up_write(&key->sem);
	goto maybe_resched;
}
