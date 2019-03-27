/*	$NetBSD: list.h,v 1.5 2014/08/20 15:26:52 riastradh Exp $	*/

/*-
 * Copyright (c) 2013 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Taylor R. Campbell.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Notes on porting:
 *
 * - LIST_HEAD(x) means a declaration `struct list_head x =
 *   LIST_HEAD_INIT(x)' in Linux, but something else in NetBSD.
 *   Replace by the expansion.
 *
 * - The `_rcu' routines here are not actually pserialize(9)-safe.
 *   They need dependent read memory barriers added.  Please fix this
 *   if you need to use them with pserialize(9).
 */

#ifndef _LINUX_LIST_H_
#define _LINUX_LIST_H_

#include <sys/queue.h>

#define container_of(ptr, type, member)				\
({								\
	__typeof(((type *)0)->member) *_p = (ptr);		\
	(type *)((char *)_p - offsetof(type, member));		\
})

/*
 * Doubly-linked lists.
 */

struct list_head {
	struct list_head *prev;
	struct list_head *next;
};

#define	LIST_HEAD_INIT(name)	{ .prev = &(name), .next = &(name) }

static inline void
INIT_LIST_HEAD(struct list_head *head)
{
	head->prev = head;
	head->next = head;
}

static inline struct list_head *
list_first(const struct list_head *head)
{
	return head->next;
}

static inline struct list_head *
list_last(const struct list_head *head)
{
	return head->prev;
}

static inline struct list_head *
list_next(const struct list_head *node)
{
	return node->next;
}

static inline struct list_head *
list_prev(const struct list_head *node)
{
	return node->prev;
}

static inline int
list_empty(const struct list_head *head)
{
	return (head->next == head);
}

static inline int
list_is_singular(const struct list_head *head)
{

	if (list_empty(head))
		return false;
	if (head->next != head->prev)
		return false;
	return true;
}

static inline void
__list_add_between(struct list_head *prev, struct list_head *node,
    struct list_head *next)
{
	prev->next = node;
	node->prev = prev;
	node->next = next;
	next->prev = node;
}

static inline void
list_add(struct list_head *node, struct list_head *head)
{
	__list_add_between(head, node, head->next);
}

static inline void
list_add_tail(struct list_head *node, struct list_head *head)
{
	__list_add_between(head->prev, node, head);
}

static inline void
list_del(struct list_head *entry)
{
	entry->prev->next = entry->next;
	entry->next->prev = entry->prev;
}

static inline void
__list_splice_between(struct list_head *prev, const struct list_head *list,
    struct list_head *next)
{
	struct list_head *first = list->next;
	struct list_head *last = list->prev;

	first->prev = prev;
	prev->next = first;

	last->next = next;
	next->prev = last;
}

static inline void
list_splice(const struct list_head *list, struct list_head *head)
{
	if (!list_empty(list))
		__list_splice_between(head, list, head->next);
}

static inline void
list_splice_tail(const struct list_head *list, struct list_head *head)
{
	if (!list_empty(list))
		__list_splice_between(head->prev, list, head);
}

static inline void
list_move(struct list_head *node, struct list_head *head)
{
	list_del(node);
	list_add(node, head);
}

static inline void
list_move_tail(struct list_head *node, struct list_head *head)
{
	list_del(node);
	list_add_tail(node, head);
}

static inline void
list_replace(struct list_head *old, struct list_head *new)
{
	new->prev = old->prev;
	old->prev->next = new;
	new->next = old->next;
	old->next->prev = new;
}

static inline void
list_del_init(struct list_head *node)
{
	list_del(node);
	INIT_LIST_HEAD(node);
}

#define	list_entry(PTR, TYPE, FIELD)	container_of(PTR, TYPE, FIELD)
#define	list_first_entry(PTR, TYPE, FIELD)				\
	list_entry(list_first((PTR)), TYPE, FIELD)
#define	list_last_entry(PTR, TYPE, FIELD)				\
	list_entry(list_last((PTR)), TYPE, FIELD)
#define	list_next_entry(ENTRY, FIELD)					\
	list_entry(list_next(&(ENTRY)->FIELD), typeof(*(ENTRY)), FIELD)
#define	list_prev_entry(ENTRY, FIELD)					\
	list_entry(list_prev(&(ENTRY)->FIELD), typeof(*(ENTRY)), FIELD)

#define	list_for_each(VAR, HEAD)					\
	for ((VAR) = list_first((HEAD));				\
		(VAR) != (HEAD);					\
		(VAR) = list_next((VAR)))

#define	list_for_each_safe(VAR, NEXT, HEAD)				\
	for ((VAR) = list_first((HEAD));				\
		((VAR) != (HEAD)) && ((NEXT) = list_next((VAR)), 1);	\
		(VAR) = (NEXT))

#define	list_for_each_entry(VAR, HEAD, FIELD)				\
	for ((VAR) = list_entry(list_first((HEAD)), typeof(*(VAR)), FIELD); \
		&(VAR)->FIELD != (HEAD);				\
		(VAR) = list_entry(list_next(&(VAR)->FIELD), typeof(*(VAR)), \
		    FIELD))

#define	list_for_each_entry_reverse(VAR, HEAD, FIELD)			\
	for ((VAR) = list_entry(list_last((HEAD)), typeof(*(VAR)), FIELD); \
		&(VAR)->FIELD != (HEAD);				\
		(VAR) = list_entry(list_prev(&(VAR)->FIELD), typeof(*(VAR)), \
		    FIELD))

#define	list_for_each_entry_safe(VAR, NEXT, HEAD, FIELD)		\
	for ((VAR) = list_entry(list_first((HEAD)), typeof(*(VAR)), FIELD); \
		(&(VAR)->FIELD != (HEAD)) &&				\
		    ((NEXT) = list_entry(list_next(&(VAR)->FIELD),	\
			typeof(*(VAR)), FIELD), 1);			\
		(VAR) = (NEXT))

#define	list_for_each_entry_continue(VAR, HEAD, FIELD)			\
	for ((VAR) = list_next_entry((VAR), FIELD);			\
		&(VAR)->FIELD != (HEAD);				\
		(VAR) = list_next_entry((VAR), FIELD))

#define	list_for_each_entry_continue_reverse(VAR, HEAD, FIELD)		\
	for ((VAR) = list_prev_entry((VAR), FIELD);			\
		&(VAR)->FIELD != (HEAD);				\
		(VAR) = list_prev_entry((VAR), FIELD))

#define	list_for_each_entry_safe_from(VAR, NEXT, HEAD, FIELD)		\
	for (;								\
		(&(VAR)->FIELD != (HEAD)) &&				\
		    ((NEXT) = list_next_entry((VAR), FIELD));		\
		(VAR) = (NEXT))

#endif  /* _LINUX_LIST_H_ */
