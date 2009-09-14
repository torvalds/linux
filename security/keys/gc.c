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
static unsigned long key_gc_executing;
static time_t key_gc_next_run = LONG_MAX;

/*
 * Schedule a garbage collection run
 * - precision isn't particularly important
 */
void key_schedule_gc(time_t gc_at)
{
	unsigned long expires;
	time_t now = current_kernel_time().tv_sec;

	kenter("%ld", gc_at - now);

	gc_at += key_gc_delay;

	if (now >= gc_at) {
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
	klist = rcu_dereference(keyring->payload.subscriptions);
	if (!klist)
		goto dont_gc;

	for (loop = klist->nkeys - 1; loop >= 0; loop--) {
		key = klist->keys[loop];
		if (test_bit(KEY_FLAG_DEAD, &key->flags) ||
		    (key->expiry > 0 && key->expiry <= limit))
			goto do_gc;
	}

dont_gc:
	kleave(" = false");
	return false;

do_gc:
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
	time_t new_timer = LONG_MAX, limit;

	kenter("");

	if (test_and_set_bit(0, &key_gc_executing)) {
		key_schedule_gc(current_kernel_time().tv_sec);
		return;
	}

	limit = current_kernel_time().tv_sec;
	if (limit > key_gc_delay)
		limit -= key_gc_delay;
	else
		limit = key_gc_delay;

	spin_lock(&key_serial_lock);

	if (RB_EMPTY_ROOT(&key_serial_tree))
		goto reached_the_end;

	cursor = key_gc_cursor;
	if (cursor < 0)
		cursor = 0;

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
		if (key->expiry > 0 && key->expiry < new_timer)
			new_timer = key->expiry;

		if (key->type == &key_type_keyring &&
		    key_gc_keyring(key, limit)) {
			/* the gc ate our lock */
			schedule_work(&key_gc_work);
			goto no_unlock;
		}

		rb = rb_next(&key->serial_node);
		if (!rb) {
			key_gc_cursor = 0;
			break;
		}
		key = rb_entry(rb, struct key, serial_node);
	}

out:
	spin_unlock(&key_serial_lock);
no_unlock:
	clear_bit(0, &key_gc_executing);
	if (new_timer < LONG_MAX)
		key_schedule_gc(new_timer);

	kleave("");
	return;

reached_the_end:
	key_gc_cursor = 0;
	goto out;
}
