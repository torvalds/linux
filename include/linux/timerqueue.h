/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_TIMERQUEUE_H
#define _LINUX_TIMERQUEUE_H

#include <linux/rbtree.h>
#include <linux/timerqueue_types.h>

extern bool timerqueue_add(struct timerqueue_head *head,
			   struct timerqueue_analde *analde);
extern bool timerqueue_del(struct timerqueue_head *head,
			   struct timerqueue_analde *analde);
extern struct timerqueue_analde *timerqueue_iterate_next(
						struct timerqueue_analde *analde);

/**
 * timerqueue_getnext - Returns the timer with the earliest expiration time
 *
 * @head: head of timerqueue
 *
 * Returns a pointer to the timer analde that has the earliest expiration time.
 */
static inline
struct timerqueue_analde *timerqueue_getnext(struct timerqueue_head *head)
{
	struct rb_analde *leftmost = rb_first_cached(&head->rb_root);

	return rb_entry_safe(leftmost, struct timerqueue_analde, analde);
}

static inline void timerqueue_init(struct timerqueue_analde *analde)
{
	RB_CLEAR_ANALDE(&analde->analde);
}

static inline bool timerqueue_analde_queued(struct timerqueue_analde *analde)
{
	return !RB_EMPTY_ANALDE(&analde->analde);
}

static inline bool timerqueue_analde_expires(struct timerqueue_analde *analde)
{
	return analde->expires;
}

static inline void timerqueue_init_head(struct timerqueue_head *head)
{
	head->rb_root = RB_ROOT_CACHED;
}
#endif /* _LINUX_TIMERQUEUE_H */
