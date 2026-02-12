/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Copyright (c) 2025, Google LLC.
 * Pasha Tatashin <pasha.tatashin@soleen.com>
 */
#ifndef _LINUX_LIST_PRIVATE_H
#define _LINUX_LIST_PRIVATE_H

/**
 * DOC: Private List Primitives
 *
 * Provides a set of list primitives identical in function to those in
 * ``<linux/list.h>``, but designed for cases where the embedded
 * ``&struct list_head`` is private member.
 */

#include <linux/compiler.h>
#include <linux/list.h>

#define __list_private_offset(type, member)					\
	((size_t)(&ACCESS_PRIVATE(((type *)0), member)))

/**
 * list_private_entry - get the struct for this entry
 * @ptr:	the &struct list_head pointer.
 * @type:	the type of the struct this is embedded in.
 * @member:	the identifier passed to ACCESS_PRIVATE.
 */
#define list_private_entry(ptr, type, member) ({				\
	const struct list_head *__mptr = (ptr);					\
	(type *)((char *)__mptr - __list_private_offset(type, member));		\
})

/**
 * list_private_first_entry - get the first element from a list
 * @ptr:	the list head to take the element from.
 * @type:	the type of the struct this is embedded in.
 * @member:	the identifier passed to ACCESS_PRIVATE.
 */
#define list_private_first_entry(ptr, type, member)				\
	list_private_entry((ptr)->next, type, member)

/**
 * list_private_last_entry - get the last element from a list
 * @ptr:	the list head to take the element from.
 * @type:	the type of the struct this is embedded in.
 * @member:	the identifier passed to ACCESS_PRIVATE.
 */
#define list_private_last_entry(ptr, type, member)				\
	list_private_entry((ptr)->prev, type, member)

/**
 * list_private_next_entry - get the next element in list
 * @pos:	the type * to cursor
 * @member:	the name of the list_head within the struct.
 */
#define list_private_next_entry(pos, member)					\
	list_private_entry(ACCESS_PRIVATE(pos, member).next, typeof(*(pos)), member)

/**
 * list_private_next_entry_circular - get the next element in list
 * @pos:	the type * to cursor.
 * @head:	the list head to take the element from.
 * @member:	the name of the list_head within the struct.
 *
 * Wraparound if pos is the last element (return the first element).
 * Note, that list is expected to be not empty.
 */
#define list_private_next_entry_circular(pos, head, member)			\
	(list_is_last(&ACCESS_PRIVATE(pos, member), head) ?			\
	list_private_first_entry(head, typeof(*(pos)), member) :		\
	list_private_next_entry(pos, member))

/**
 * list_private_prev_entry - get the prev element in list
 * @pos:	the type * to cursor
 * @member:	the name of the list_head within the struct.
 */
#define list_private_prev_entry(pos, member)					\
	list_private_entry(ACCESS_PRIVATE(pos, member).prev, typeof(*(pos)), member)

/**
 * list_private_prev_entry_circular - get the prev element in list
 * @pos:	the type * to cursor.
 * @head:	the list head to take the element from.
 * @member:	the name of the list_head within the struct.
 *
 * Wraparound if pos is the first element (return the last element).
 * Note, that list is expected to be not empty.
 */
#define list_private_prev_entry_circular(pos, head, member)			\
	(list_is_first(&ACCESS_PRIVATE(pos, member), head) ?			\
	list_private_last_entry(head, typeof(*(pos)), member) :			\
	list_private_prev_entry(pos, member))

/**
 * list_private_entry_is_head - test if the entry points to the head of the list
 * @pos:	the type * to cursor
 * @head:	the head for your list.
 * @member:	the name of the list_head within the struct.
 */
#define list_private_entry_is_head(pos, head, member)				\
	list_is_head(&ACCESS_PRIVATE(pos, member), (head))

/**
 * list_private_for_each_entry - iterate over list of given type
 * @pos:	the type * to use as a loop cursor.
 * @head:	the head for your list.
 * @member:	the name of the list_head within the struct.
 */
#define list_private_for_each_entry(pos, head, member)				\
	for (pos = list_private_first_entry(head, typeof(*pos), member);	\
	     !list_private_entry_is_head(pos, head, member);			\
	     pos = list_private_next_entry(pos, member))

/**
 * list_private_for_each_entry_reverse - iterate backwards over list of given type.
 * @pos:	the type * to use as a loop cursor.
 * @head:	the head for your list.
 * @member:	the name of the list_head within the struct.
 */
#define list_private_for_each_entry_reverse(pos, head, member)			\
	for (pos = list_private_last_entry(head, typeof(*pos), member);		\
	     !list_private_entry_is_head(pos, head, member);			\
	     pos = list_private_prev_entry(pos, member))

/**
 * list_private_for_each_entry_continue - continue iteration over list of given type
 * @pos:	the type * to use as a loop cursor.
 * @head:	the head for your list.
 * @member:	the name of the list_head within the struct.
 *
 * Continue to iterate over list of given type, continuing after
 * the current position.
 */
#define list_private_for_each_entry_continue(pos, head, member)			\
	for (pos = list_private_next_entry(pos, member);			\
	     !list_private_entry_is_head(pos, head, member);			\
	     pos = list_private_next_entry(pos, member))

/**
 * list_private_for_each_entry_continue_reverse - iterate backwards from the given point
 * @pos:	the type * to use as a loop cursor.
 * @head:	the head for your list.
 * @member:	the name of the list_head within the struct.
 *
 * Start to iterate over list of given type backwards, continuing after
 * the current position.
 */
#define list_private_for_each_entry_continue_reverse(pos, head, member)		\
	for (pos = list_private_prev_entry(pos, member);			\
	     !list_private_entry_is_head(pos, head, member);			\
	     pos = list_private_prev_entry(pos, member))

/**
 * list_private_for_each_entry_from - iterate over list of given type from the current point
 * @pos:	the type * to use as a loop cursor.
 * @head:	the head for your list.
 * @member:	the name of the list_head within the struct.
 *
 * Iterate over list of given type, continuing from current position.
 */
#define list_private_for_each_entry_from(pos, head, member)			\
	for (; !list_private_entry_is_head(pos, head, member);			\
	     pos = list_private_next_entry(pos, member))

/**
 * list_private_for_each_entry_from_reverse - iterate backwards over list of given type
 *                                    from the current point
 * @pos:	the type * to use as a loop cursor.
 * @head:	the head for your list.
 * @member:	the name of the list_head within the struct.
 *
 * Iterate backwards over list of given type, continuing from current position.
 */
#define list_private_for_each_entry_from_reverse(pos, head, member)		\
	for (; !list_private_entry_is_head(pos, head, member);			\
	     pos = list_private_prev_entry(pos, member))

/**
 * list_private_for_each_entry_safe - iterate over list of given type safe against removal of list entry
 * @pos:	the type * to use as a loop cursor.
 * @n:		another type * to use as temporary storage
 * @head:	the head for your list.
 * @member:	the name of the list_head within the struct.
 */
#define list_private_for_each_entry_safe(pos, n, head, member)			\
	for (pos = list_private_first_entry(head, typeof(*pos), member),	\
		n = list_private_next_entry(pos, member);			\
	     !list_private_entry_is_head(pos, head, member);			\
	     pos = n, n = list_private_next_entry(n, member))

/**
 * list_private_for_each_entry_safe_continue - continue list iteration safe against removal
 * @pos:	the type * to use as a loop cursor.
 * @n:		another type * to use as temporary storage
 * @head:	the head for your list.
 * @member:	the name of the list_head within the struct.
 *
 * Iterate over list of given type, continuing after current point,
 * safe against removal of list entry.
 */
#define list_private_for_each_entry_safe_continue(pos, n, head, member)		\
	for (pos = list_private_next_entry(pos, member),			\
		n = list_private_next_entry(pos, member);			\
	     !list_private_entry_is_head(pos, head, member);			\
	     pos = n, n = list_private_next_entry(n, member))

/**
 * list_private_for_each_entry_safe_from - iterate over list from current point safe against removal
 * @pos:	the type * to use as a loop cursor.
 * @n:		another type * to use as temporary storage
 * @head:	the head for your list.
 * @member:	the name of the list_head within the struct.
 *
 * Iterate over list of given type from current point, safe against
 * removal of list entry.
 */
#define list_private_for_each_entry_safe_from(pos, n, head, member)		\
	for (n = list_private_next_entry(pos, member);				\
	     !list_private_entry_is_head(pos, head, member);			\
	     pos = n, n = list_private_next_entry(n, member))

/**
 * list_private_for_each_entry_safe_reverse - iterate backwards over list safe against removal
 * @pos:	the type * to use as a loop cursor.
 * @n:		another type * to use as temporary storage
 * @head:	the head for your list.
 * @member:	the name of the list_head within the struct.
 *
 * Iterate backwards over list of given type, safe against removal
 * of list entry.
 */
#define list_private_for_each_entry_safe_reverse(pos, n, head, member)		\
	for (pos = list_private_last_entry(head, typeof(*pos), member),		\
		n = list_private_prev_entry(pos, member);			\
	     !list_private_entry_is_head(pos, head, member);			\
	     pos = n, n = list_private_prev_entry(n, member))

/**
 * list_private_safe_reset_next - reset a stale list_for_each_entry_safe loop
 * @pos:	the loop cursor used in the list_for_each_entry_safe loop
 * @n:		temporary storage used in list_for_each_entry_safe
 * @member:	the name of the list_head within the struct.
 *
 * list_safe_reset_next is not safe to use in general if the list may be
 * modified concurrently (eg. the lock is dropped in the loop body). An
 * exception to this is if the cursor element (pos) is pinned in the list,
 * and list_safe_reset_next is called after re-taking the lock and before
 * completing the current iteration of the loop body.
 */
#define list_private_safe_reset_next(pos, n, member)				\
	n = list_private_next_entry(pos, member)

#endif /* _LINUX_LIST_PRIVATE_H */
