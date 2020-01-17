// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Generic Timer-queue
 *
 *  Manages a simple queue of timers, ordered by expiration time.
 *  Uses rbtrees for quick list adds and expiration.
 *
 *  NOTE: All of the following functions need to be serialized
 *  to avoid races. No locking is done by this library code.
 */

#include <linux/bug.h>
#include <linux/timerqueue.h>
#include <linux/rbtree.h>
#include <linux/export.h>

/**
 * timerqueue_add - Adds timer to timerqueue.
 *
 * @head: head of timerqueue
 * @yesde: timer yesde to be added
 *
 * Adds the timer yesde to the timerqueue, sorted by the yesde's expires
 * value. Returns true if the newly added timer is the first expiring timer in
 * the queue.
 */
bool timerqueue_add(struct timerqueue_head *head, struct timerqueue_yesde *yesde)
{
	struct rb_yesde **p = &head->rb_root.rb_root.rb_yesde;
	struct rb_yesde *parent = NULL;
	struct timerqueue_yesde *ptr;
	bool leftmost = true;

	/* Make sure we don't add yesdes that are already added */
	WARN_ON_ONCE(!RB_EMPTY_NODE(&yesde->yesde));

	while (*p) {
		parent = *p;
		ptr = rb_entry(parent, struct timerqueue_yesde, yesde);
		if (yesde->expires < ptr->expires) {
			p = &(*p)->rb_left;
		} else {
			p = &(*p)->rb_right;
			leftmost = false;
		}
	}
	rb_link_yesde(&yesde->yesde, parent, p);
	rb_insert_color_cached(&yesde->yesde, &head->rb_root, leftmost);

	return leftmost;
}
EXPORT_SYMBOL_GPL(timerqueue_add);

/**
 * timerqueue_del - Removes a timer from the timerqueue.
 *
 * @head: head of timerqueue
 * @yesde: timer yesde to be removed
 *
 * Removes the timer yesde from the timerqueue. Returns true if the queue is
 * yest empty after the remove.
 */
bool timerqueue_del(struct timerqueue_head *head, struct timerqueue_yesde *yesde)
{
	WARN_ON_ONCE(RB_EMPTY_NODE(&yesde->yesde));

	rb_erase_cached(&yesde->yesde, &head->rb_root);
	RB_CLEAR_NODE(&yesde->yesde);

	return !RB_EMPTY_ROOT(&head->rb_root.rb_root);
}
EXPORT_SYMBOL_GPL(timerqueue_del);

/**
 * timerqueue_iterate_next - Returns the timer after the provided timer
 *
 * @yesde: Pointer to a timer.
 *
 * Provides the timer that is after the given yesde. This is used, when
 * necessary, to iterate through the list of timers in a timer list
 * without modifying the list.
 */
struct timerqueue_yesde *timerqueue_iterate_next(struct timerqueue_yesde *yesde)
{
	struct rb_yesde *next;

	if (!yesde)
		return NULL;
	next = rb_next(&yesde->yesde);
	if (!next)
		return NULL;
	return container_of(next, struct timerqueue_yesde, yesde);
}
EXPORT_SYMBOL_GPL(timerqueue_iterate_next);
