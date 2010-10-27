/* Key garbage collector
 *
 * Copyright (C) 2009 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#include <linux/module.h>
#include <keys/keyring-type.h>
#include "internal.h"

/*
 * Delay between key revocation/expiry in seconds
 */
unsigned key_gc_delay = 5 * 60;

/*
 * Reaper
 */
static void key_gc_timer_func(unsigned long);
static void key_garbage_collector(struct work_struct *);
static DEFINE_TIMER(key_gc_timer, key_gc_timer_func, 0, 0);
static DECLARE_WORK(key_gc_work, key_garbage_collector);
static key_serial_t key_gc_cursor; /* the last key the gc considered */
static bool key_gc_again;
static unsigned long key_gc_executing;
static time_t key_gc_next_run = LONG_MAX;
static time_t key_gc_new_timer;

/*
 * Schedule a garbage collection run
 * - precision isn't particularly important
 */
void key_schedule_gc(time_t gc_at)
{
	unsigned long expires;
	time_t now = current_kernel_time().tv_sec;

	kenter("%ld", gc_at - now);

	if (gc_at <= now) {
		schedule_work(&key_gc_work);
	} else if (gc_at < key_gc_next_run) {
		expires = jiffies + (gc_at - now) * HZ;
		mod_timer(&key_gc_timer, expires);
	}
}

/*
 * The garbage collector timer kicked off
 */
static void key_gc_timer_func(unsigned long data)
{
	kenter("");
	key_gc_next_run = LONG_MAX;
	schedule_work(&key_gc_work);
}

/*
 * Garbage collect pointers from a keyring
 * - return true if we altered the keyring
 */
static bool key_gc_keyring(struct key *keyring, time_t limit)
	__releases(key_serial_lock)
{
	struct keyring_list *klist;
	struct key *key;
	int loop;

	kenter("%x", key_serial(keyring));

	if (test_bit(KEY_FLAG_REVOKED, &keyring->flags))
		goto dont_gc;

	/* scan the keyring looking for dead keys */
	rcu_read_lock();
	klist = rcu_dereference(keyring->payload.subscriptions);
	if (!klist)
		goto unlock_dont_gc;

	for (loop = klist->nkeys - 1; loop >= 0; loop--) {
		key = klist->keys[loop];
		if (test_bit(KEY_FLAG_DEAD, &key->flags) ||
		    (key->expiry > 0 && key->expiry <= limit))
			goto do_gc;
	}

unlock_dont_gc:
	rcu_read_unlock();
dont_gc:
	kleave(" = false");
	return false;

do_gc:
	rcu_read_unlock();
	key_gc_cursor = keyring->serial;
	key_get(keyring);
	spin_unlock(&key_serial_lock);
	keyring_gc(keyring, limit);
	key_put(keyring);
	kleave(" = true");
	return true;
}

/*
 * Garbage collector for keys
 * - this involves scanning the keyrings for dead, expired and revoked keys
 *   that have overstayed their welcome
 */
static void key_garbage_collector(struct work_struct *work)
{
	struct rb_node *rb;
	key_serial_t cursor;
	struct key *key, *xkey;
	time_t new_timer = LONG_MAX, limit, now;

	now = current_kernel_time().tv_sec;
	kenter("[%x,%ld]", key_gc_cursor, key_gc_new_timer - now);

	if (test_and_set_bit(0, &key_gc_executing)) {
		key_schedule_gc(current_kernel_time().tv_sec + 1);
		kleave(" [busy; deferring]");
		return;
	}

	limit = now;
	if (limit > key_gc_delay)
		limit -= key_gc_delay;
	else
		limit = key_gc_delay;

	spin_lock(&key_serial_lock);

	if (unlikely(RB_EMPTY_ROOT(&key_serial_tree))) {
		spin_unlock(&key_serial_lock);
		clear_bit(0, &key_gc_executing);
		return;
	}

	cursor = key_gc_cursor;
	if (cursor < 0)
		cursor = 0;
	if (cursor > 0)
		new_timer = key_gc_new_timer;
	else
		key_gc_again = false;

	/* find the first key above the cursor */
	key = NULL;
	rb = key_serial_tree.rb_node;
	while (rb) {
		xkey = rb_entry(rb, struct key, serial_node);
		if (cursor < xkey->serial) {
			key = xkey;
			rb = rb->rb_left;
		} else if (cursor > xkey->serial) {
			rb = rb->rb_right;
		} else {
			rb = rb_next(rb);
			if (!rb)
				goto reached_the_end;
			key = rb_entry(rb, struct key, serial_node);
			break;
		}
	}

	if (!key)
		goto reached_the_end;

	/* trawl through the keys looking for keyrings */
	for (;;) {
		if (key->expiry > limit && key->expiry < new_timer) {
			kdebug("will expire %x in %ld",
			       key_serial(key), key->expiry - limit);
			new_timer = key->expiry;
		}

		if (key->type == &key_type_keyring &&
		    key_gc_keyring(key, limit))
			/* the gc had to release our lock so that the keyring
			 * could be modified, so we have to get it again */
			goto gc_released_our_lock;

		rb = rb_next(&key->serial_node);
		if (!rb)
			goto reached_the_end;
		key = rb_entry(rb, struct key, serial_node);
	}

gc_released_our_lock:
	kdebug("gc_released_our_lock");
	key_gc_new_timer = new_timer;
	key_gc_again = true;
	clear_bit(0, &key_gc_executing);
	schedule_work(&key_gc_work);
	kleave(" [continue]");
	return;

	/* when we reach the end of the run, we set the timer for the next one */
reached_the_end:
	kdebug("reached_the_end");
	spin_unlock(&key_serial_lock);
	key_gc_new_timer = new_timer;
	key_gc_cursor = 0;
	clear_bit(0, &key_gc_executing);

	if (key_gc_again) {
		/* there may have been a key that expired whilst we were
		 * scanning, so if we discarded any links we should do another
		 * scan */
		new_timer = now + 1;
		key_schedule_gc(new_timer);
	} else if (new_timer < LONG_MAX) {
		new_timer += key_gc_delay;
		key_schedule_gc(new_timer);
	}
	kleave(" [end]");
}
