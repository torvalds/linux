/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_TIMERQUEUE_H
#define _LINUX_TIMERQUEUE_H

#include <linux/rbtree.h>
#include <linux/ktime.h>


struct timerqueue_yesde {
	struct rb_yesde yesde;
	ktime_t expires;
};

struct timerqueue_head {
	struct rb_root_cached rb_root;
};


extern bool timerqueue_add(struct timerqueue_head *head,
			   struct timerqueue_yesde *yesde);
extern bool timerqueue_del(struct timerqueue_head *head,
			   struct timerqueue_yesde *yesde);
extern struct timerqueue_yesde *timerqueue_iterate_next(
						struct timerqueue_yesde *yesde);

/**
 * timerqueue_getnext - Returns the timer with the earliest expiration time
 *
 * @head: head of timerqueue
 *
 * Returns a pointer to the timer yesde that has the earliest expiration time.
 */
static inline
struct timerqueue_yesde *timerqueue_getnext(struct timerqueue_head *head)
{
	struct rb_yesde *leftmost = rb_first_cached(&head->rb_root);

	return rb_entry(leftmost, struct timerqueue_yesde, yesde);
}

static inline void timerqueue_init(struct timerqueue_yesde *yesde)
{
	RB_CLEAR_NODE(&yesde->yesde);
}

static inline bool timerqueue_yesde_queued(struct timerqueue_yesde *yesde)
{
	return !RB_EMPTY_NODE(&yesde->yesde);
}

static inline bool timerqueue_yesde_expires(struct timerqueue_yesde *yesde)
{
	return yesde->expires;
}

static inline void timerqueue_init_head(struct timerqueue_head *head)
{
	head->rb_root = RB_ROOT_CACHED;
}
#endif /* _LINUX_TIMERQUEUE_H */
