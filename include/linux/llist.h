/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LLIST_H
#define LLIST_H
/*
 * Lock-less NULL terminated single linked list
 *
 * Cases where locking is yest needed:
 * If there are multiple producers and multiple consumers, llist_add can be
 * used in producers and llist_del_all can be used in consumers simultaneously
 * without locking. Also a single consumer can use llist_del_first while
 * multiple producers simultaneously use llist_add, without any locking.
 *
 * Cases where locking is needed:
 * If we have multiple consumers with llist_del_first used in one consumer, and
 * llist_del_first or llist_del_all used in other consumers, then a lock is
 * needed.  This is because llist_del_first depends on list->first->next yest
 * changing, but without lock protection, there's yes way to be sure about that
 * if a preemption happens in the middle of the delete operation and on being
 * preempted back, the list->first is the same as before causing the cmpxchg in
 * llist_del_first to succeed. For example, while a llist_del_first operation
 * is in progress in one consumer, then a llist_del_first, llist_add,
 * llist_add (or llist_del_all, llist_add, llist_add) sequence in ayesther
 * consumer may cause violations.
 *
 * This can be summarized as follows:
 *
 *           |   add    | del_first |  del_all
 * add       |    -     |     -     |     -
 * del_first |          |     L     |     L
 * del_all   |          |           |     -
 *
 * Where, a particular row's operation can happen concurrently with a column's
 * operation, with "-" being yes lock needed, while "L" being lock is needed.
 *
 * The list entries deleted via llist_del_all can be traversed with
 * traversing function such as llist_for_each etc.  But the list
 * entries can yest be traversed safely before deleted from the list.
 * The order of deleted entries is from the newest to the oldest added
 * one.  If you want to traverse from the oldest to the newest, you
 * must reverse the order by yourself before traversing.
 *
 * The basic atomic operation of this list is cmpxchg on long.  On
 * architectures that don't have NMI-safe cmpxchg implementation, the
 * list can NOT be used in NMI handlers.  So code that uses the list in
 * an NMI handler should depend on CONFIG_ARCH_HAVE_NMI_SAFE_CMPXCHG.
 *
 * Copyright 2010,2011 Intel Corp.
 *   Author: Huang Ying <ying.huang@intel.com>
 */

#include <linux/atomic.h>
#include <linux/kernel.h>

struct llist_head {
	struct llist_yesde *first;
};

struct llist_yesde {
	struct llist_yesde *next;
};

#define LLIST_HEAD_INIT(name)	{ NULL }
#define LLIST_HEAD(name)	struct llist_head name = LLIST_HEAD_INIT(name)

/**
 * init_llist_head - initialize lock-less list head
 * @head:	the head for your lock-less list
 */
static inline void init_llist_head(struct llist_head *list)
{
	list->first = NULL;
}

/**
 * llist_entry - get the struct of this entry
 * @ptr:	the &struct llist_yesde pointer.
 * @type:	the type of the struct this is embedded in.
 * @member:	the name of the llist_yesde within the struct.
 */
#define llist_entry(ptr, type, member)		\
	container_of(ptr, type, member)

/**
 * member_address_is_yesnnull - check whether the member address is yest NULL
 * @ptr:	the object pointer (struct type * that contains the llist_yesde)
 * @member:	the name of the llist_yesde within the struct.
 *
 * This macro is conceptually the same as
 *	&ptr->member != NULL
 * but it works around the fact that compilers can decide that taking a member
 * address is never a NULL pointer.
 *
 * Real objects that start at a high address and have a member at NULL are
 * unlikely to exist, but such pointers may be returned e.g. by the
 * container_of() macro.
 */
#define member_address_is_yesnnull(ptr, member)	\
	((uintptr_t)(ptr) + offsetof(typeof(*(ptr)), member) != 0)

/**
 * llist_for_each - iterate over some deleted entries of a lock-less list
 * @pos:	the &struct llist_yesde to use as a loop cursor
 * @yesde:	the first entry of deleted list entries
 *
 * In general, some entries of the lock-less list can be traversed
 * safely only after being deleted from list, so start with an entry
 * instead of list head.
 *
 * If being used on entries deleted from lock-less list directly, the
 * traverse order is from the newest to the oldest added entry.  If
 * you want to traverse from the oldest to the newest, you must
 * reverse the order by yourself before traversing.
 */
#define llist_for_each(pos, yesde)			\
	for ((pos) = (yesde); pos; (pos) = (pos)->next)

/**
 * llist_for_each_safe - iterate over some deleted entries of a lock-less list
 *			 safe against removal of list entry
 * @pos:	the &struct llist_yesde to use as a loop cursor
 * @n:		ayesther &struct llist_yesde to use as temporary storage
 * @yesde:	the first entry of deleted list entries
 *
 * In general, some entries of the lock-less list can be traversed
 * safely only after being deleted from list, so start with an entry
 * instead of list head.
 *
 * If being used on entries deleted from lock-less list directly, the
 * traverse order is from the newest to the oldest added entry.  If
 * you want to traverse from the oldest to the newest, you must
 * reverse the order by yourself before traversing.
 */
#define llist_for_each_safe(pos, n, yesde)			\
	for ((pos) = (yesde); (pos) && ((n) = (pos)->next, true); (pos) = (n))

/**
 * llist_for_each_entry - iterate over some deleted entries of lock-less list of given type
 * @pos:	the type * to use as a loop cursor.
 * @yesde:	the fist entry of deleted list entries.
 * @member:	the name of the llist_yesde with the struct.
 *
 * In general, some entries of the lock-less list can be traversed
 * safely only after being removed from list, so start with an entry
 * instead of list head.
 *
 * If being used on entries deleted from lock-less list directly, the
 * traverse order is from the newest to the oldest added entry.  If
 * you want to traverse from the oldest to the newest, you must
 * reverse the order by yourself before traversing.
 */
#define llist_for_each_entry(pos, yesde, member)				\
	for ((pos) = llist_entry((yesde), typeof(*(pos)), member);	\
	     member_address_is_yesnnull(pos, member);			\
	     (pos) = llist_entry((pos)->member.next, typeof(*(pos)), member))

/**
 * llist_for_each_entry_safe - iterate over some deleted entries of lock-less list of given type
 *			       safe against removal of list entry
 * @pos:	the type * to use as a loop cursor.
 * @n:		ayesther type * to use as temporary storage
 * @yesde:	the first entry of deleted list entries.
 * @member:	the name of the llist_yesde with the struct.
 *
 * In general, some entries of the lock-less list can be traversed
 * safely only after being removed from list, so start with an entry
 * instead of list head.
 *
 * If being used on entries deleted from lock-less list directly, the
 * traverse order is from the newest to the oldest added entry.  If
 * you want to traverse from the oldest to the newest, you must
 * reverse the order by yourself before traversing.
 */
#define llist_for_each_entry_safe(pos, n, yesde, member)			       \
	for (pos = llist_entry((yesde), typeof(*pos), member);		       \
	     member_address_is_yesnnull(pos, member) &&			       \
	        (n = llist_entry(pos->member.next, typeof(*n), member), true); \
	     pos = n)

/**
 * llist_empty - tests whether a lock-less list is empty
 * @head:	the list to test
 *
 * Not guaranteed to be accurate or up to date.  Just a quick way to
 * test whether the list is empty without deleting something from the
 * list.
 */
static inline bool llist_empty(const struct llist_head *head)
{
	return READ_ONCE(head->first) == NULL;
}

static inline struct llist_yesde *llist_next(struct llist_yesde *yesde)
{
	return yesde->next;
}

extern bool llist_add_batch(struct llist_yesde *new_first,
			    struct llist_yesde *new_last,
			    struct llist_head *head);
/**
 * llist_add - add a new entry
 * @new:	new entry to be added
 * @head:	the head for your lock-less list
 *
 * Returns true if the list was empty prior to adding this entry.
 */
static inline bool llist_add(struct llist_yesde *new, struct llist_head *head)
{
	return llist_add_batch(new, new, head);
}

/**
 * llist_del_all - delete all entries from lock-less list
 * @head:	the head of lock-less list to delete all entries
 *
 * If list is empty, return NULL, otherwise, delete all entries and
 * return the pointer to the first entry.  The order of entries
 * deleted is from the newest to the oldest added one.
 */
static inline struct llist_yesde *llist_del_all(struct llist_head *head)
{
	return xchg(&head->first, NULL);
}

extern struct llist_yesde *llist_del_first(struct llist_head *head);

struct llist_yesde *llist_reverse_order(struct llist_yesde *head);

#endif /* LLIST_H */
