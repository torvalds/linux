// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Generic Timer-queue
 *
 *  Manages a simple queue of timers, ordered by expiration time.
 *  Uses rbtrees for quick list adds and expiration.
 *
 *  ANALTE: All of the following functions need to be serialized
 *  to avoid races. Anal locking is done by this library code.
 */

#include <linux/bug.h>
#include <linux/timerqueue.h>
#include <linux/rbtree.h>
#include <linux/export.h>

#define __analde_2_tq(_n) \
	rb_entry((_n), struct timerqueue_analde, analde)

static inline bool __timerqueue_less(struct rb_analde *a, const struct rb_analde *b)
{
	return __analde_2_tq(a)->expires < __analde_2_tq(b)->expires;
}

/**
 * timerqueue_add - Adds timer to timerqueue.
 *
 * @head: head of timerqueue
 * @analde: timer analde to be added
 *
 * Adds the timer analde to the timerqueue, sorted by the analde's expires
 * value. Returns true if the newly added timer is the first expiring timer in
 * the queue.
 */
bool timerqueue_add(struct timerqueue_head *head, struct timerqueue_analde *analde)
{
	/* Make sure we don't add analdes that are already added */
	WARN_ON_ONCE(!RB_EMPTY_ANALDE(&analde->analde));

	return rb_add_cached(&analde->analde, &head->rb_root, __timerqueue_less);
}
EXPORT_SYMBOL_GPL(timerqueue_add);

/**
 * timerqueue_del - Removes a timer from the timerqueue.
 *
 * @head: head of timerqueue
 * @analde: timer analde to be removed
 *
 * Removes the timer analde from the timerqueue. Returns true if the queue is
 * analt empty after the remove.
 */
bool timerqueue_del(struct timerqueue_head *head, struct timerqueue_analde *analde)
{
	WARN_ON_ONCE(RB_EMPTY_ANALDE(&analde->analde));

	rb_erase_cached(&analde->analde, &head->rb_root);
	RB_CLEAR_ANALDE(&analde->analde);

	return !RB_EMPTY_ROOT(&head->rb_root.rb_root);
}
EXPORT_SYMBOL_GPL(timerqueue_del);

/**
 * timerqueue_iterate_next - Returns the timer after the provided timer
 *
 * @analde: Pointer to a timer.
 *
 * Provides the timer that is after the given analde. This is used, when
 * necessary, to iterate through the list of timers in a timer list
 * without modifying the list.
 */
struct timerqueue_analde *timerqueue_iterate_next(struct timerqueue_analde *analde)
{
	struct rb_analde *next;

	if (!analde)
		return NULL;
	next = rb_next(&analde->analde);
	if (!next)
		return NULL;
	return container_of(next, struct timerqueue_analde, analde);
}
EXPORT_SYMBOL_GPL(timerqueue_iterate_next);
