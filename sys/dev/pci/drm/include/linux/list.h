/*	$OpenBSD: list.h,v 1.8 2024/01/16 23:38:13 jsg Exp $	*/
/* drm_linux_list.h -- linux list functions for the BSDs.
 * Created: Mon Apr 7 14:30:16 1999 by anholt@FreeBSD.org
 */
/*-
 * Copyright 2003 Eric Anholt
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Eric Anholt <anholt@FreeBSD.org>
 *
 */

#ifndef _DRM_LINUX_LIST_H_
#define _DRM_LINUX_LIST_H_

#include <sys/param.h>
#include <linux/container_of.h>
#include <linux/types.h>
#include <linux/poison.h>

#define list_entry(ptr, type, member) container_of(ptr, type, member)

static inline void
INIT_LIST_HEAD(struct list_head *head) {
	(head)->next = head;
	(head)->prev = head;
}

#define LIST_HEAD_INIT(name) { &(name), &(name) }

#define DRM_LIST_HEAD(name) \
	struct list_head name = LIST_HEAD_INIT(name)

static inline int
list_empty(const struct list_head *head) {
	return (head)->next == head;
}

static inline int
list_is_singular(const struct list_head *head) {
	return !list_empty(head) && ((head)->next == (head)->prev);
}

static inline int
list_is_first(const struct list_head *list,
    const struct list_head *head)
{
	return list->prev == head;
}

static inline int
list_is_last(const struct list_head *list,
    const struct list_head *head)
{
	return list->next == head;
}

static inline void
__list_add(struct list_head *new, struct list_head *prev,
    struct list_head *next)
{
	next->prev = new;
	new->next = next;
	new->prev = prev;
	prev->next = new;
}

static inline void
list_add(struct list_head *new, struct list_head *head) {
        (head)->next->prev = new;
        (new)->next = (head)->next;
        (new)->prev = head;
        (head)->next = new;
}

static inline void
list_add_tail(struct list_head *entry, struct list_head *head) {
	(entry)->prev = (head)->prev;
	(entry)->next = head;
	(head)->prev->next = entry;
	(head)->prev = entry;
}

static inline void
list_del(struct list_head *entry) {
	(entry)->next->prev = (entry)->prev;
	(entry)->prev->next = (entry)->next;
}

#define __list_del_entry(x) list_del(x)

static inline void list_replace(struct list_head *old,
				struct list_head *new)
{
	new->next = old->next;
	new->next->prev = new;
	new->prev = old->prev;
	new->prev->next = new;
}

static inline void list_replace_init(struct list_head *old,
				     struct list_head *new)
{
	list_replace(old, new);
	INIT_LIST_HEAD(old);
}

static inline void list_move(struct list_head *list, struct list_head *head)
{
	list_del(list);
	list_add(list, head);
}

static inline void list_move_tail(struct list_head *list,
    struct list_head *head)
{
	list_del(list);
	list_add_tail(list, head);
}

static inline void
list_rotate_to_front(struct list_head *list, struct list_head *head)
{
	list_del(head);
	list_add_tail(head, list);
}

static inline void
list_bulk_move_tail(struct list_head *head, struct list_head *first,
    struct list_head *last)
{
	first->prev->next = last->next;
	last->next->prev = first->prev;
	head->prev->next = first;
	first->prev = head->prev;
	last->next = head;
	head->prev = last;
}

static inline void
list_del_init(struct list_head *entry) {
	(entry)->next->prev = (entry)->prev;
	(entry)->prev->next = (entry)->next;
	INIT_LIST_HEAD(entry);
}

#define list_next_entry(pos, member)				\
	list_entry(((pos)->member.next), typeof(*(pos)), member)

#define list_prev_entry(pos, member)				\
	list_entry(((pos)->member.prev), typeof(*(pos)), member)

#define list_safe_reset_next(pos, n, member)			\
	n = list_next_entry(pos, member)

#define list_for_each(entry, head)				\
    for (entry = (head)->next; entry != head; entry = (entry)->next)

#define list_for_each_prev(entry, head) \
        for (entry = (head)->prev; entry != (head); \
                entry = entry->prev)

#define list_for_each_safe(entry, temp, head)			\
    for (entry = (head)->next, temp = (entry)->next;		\
	entry != head; 						\
	entry = temp, temp = entry->next)

#define list_for_each_entry_safe_reverse(pos, n, head, member)		\
	for (pos = list_entry((head)->prev, __typeof(*pos), member),	\
	    n = list_entry((pos)->member.prev, __typeof(*pos), member);	\
	    &(pos)->member != (head);					\
	    pos = n, n = list_entry(n->member.prev, __typeof(*n), member))

#define list_for_each_entry_safe_from(pos, n, head, member) 		\
	for (n = list_entry(pos->member.next, __typeof(*pos), member);	\
	     &pos->member != (head);					\
	     pos = n, n = list_entry(n->member.next, __typeof(*n), member))

#define list_for_each_entry(pos, head, member)				\
    for (pos = list_entry((head)->next, __typeof(*pos), member);	\
	&pos->member != (head);					 	\
	pos = list_entry(pos->member.next, __typeof(*pos), member))

#define list_for_each_entry_from(pos, head, member)			\
    for (;								\
	&pos->member != (head);					 	\
	pos = list_entry(pos->member.next, __typeof(*pos), member))

#define list_for_each_entry_reverse(pos, head, member)			\
    for (pos = list_entry((head)->prev, __typeof(*pos), member);	\
	&pos->member != (head);					 	\
	pos = list_entry(pos->member.prev, __typeof(*pos), member))

#define list_for_each_entry_from_reverse(pos, head, member)		\
    for (;								\
	&pos->member != (head);					 	\
	pos = list_entry(pos->member.prev, __typeof(*pos), member))

#define list_for_each_entry_continue(pos, head, member)				\
    for (pos = list_entry((pos)->member.next, __typeof(*pos), member);	\
	&pos->member != (head);					 	\
	pos = list_entry(pos->member.next, __typeof(*pos), member))

#define list_for_each_entry_continue_reverse(pos, head, member)         \
        for (pos = list_entry(pos->member.prev, __typeof(*pos), member);  \
             &pos->member != (head);    				\
             pos = list_entry(pos->member.prev, __typeof(*pos), member))

/**
 * list_for_each_entry_safe - iterate over list of given type safe against removal of list entry
 * @pos:        the type * to use as a loop cursor.
 * @n:          another type * to use as temporary storage
 * @head:       the head for your list.
 * @member:     the name of the list_struct within the struct.
 */
#define list_for_each_entry_safe(pos, n, head, member)			\
	for (pos = list_entry((head)->next, __typeof(*pos), member),	\
	    n = list_entry(pos->member.next, __typeof(*pos), member);	\
	    &pos->member != (head);					\
	    pos = n, n = list_entry(n->member.next, __typeof(*n), member))

#define list_first_entry(ptr, type, member) \
	list_entry((ptr)->next, type, member)

#define list_first_entry_or_null(ptr, type, member) \
	(list_empty(ptr) ? NULL : list_first_entry(ptr, type, member))

#define list_last_entry(ptr, type, member) \
	list_entry((ptr)->prev, type, member)

static inline void
__list_splice(const struct list_head *list, struct list_head *prev,
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
	if (list_empty(list))
		return;

	__list_splice(list, head, head->next);
}

static inline void
list_splice_init(struct list_head *list, struct list_head *head)
{
	if (list_empty(list))
		return;

	__list_splice(list, head, head->next);
	INIT_LIST_HEAD(list);
}

static inline void
list_splice_tail(const struct list_head *list, struct list_head *head)
{
	if (list_empty(list))
		return;

	__list_splice(list, head->prev, head);
}

static inline void
list_splice_tail_init(struct list_head *list, struct list_head *head)
{
	if (list_empty(list))
		return;

	__list_splice(list, head->prev, head);
	INIT_LIST_HEAD(list);
}

static inline size_t
list_count_nodes(struct list_head *head)
{
	struct list_head *entry;
	size_t n = 0;

	list_for_each(entry, head)
		n++;

	return n;
}

void	list_sort(void *, struct list_head *,
	    int (*)(void *, const struct list_head *, const struct list_head *));

#define hlist_entry(ptr, type, member) \
	((ptr) ? container_of(ptr, type, member) : NULL)

static inline void
INIT_HLIST_HEAD(struct hlist_head *head) {
	head->first = NULL;
}

static inline int
hlist_empty(const struct hlist_head *head) {
	return head->first == NULL;
}

static inline void
hlist_add_head(struct hlist_node *new, struct hlist_head *head)
{
	if ((new->next = head->first) != NULL)
		head->first->prev = &new->next;
	head->first = new;
	new->prev = &head->first;
}

static inline void
hlist_del_init(struct hlist_node *node)
{
	if (node->next != NULL)
		node->next->prev = node->prev;
	*(node->prev) = node->next;
	node->next = NULL;
	node->prev = NULL;
}

#define hlist_for_each(pos, head) \
	for (pos = (head)->first; pos != NULL; pos = pos->next)

#define hlist_for_each_entry(pos, head, member)				\
	for (pos = hlist_entry((head)->first, __typeof(*pos), member);	\
	     pos != NULL;						\
	     pos = hlist_entry((pos)->member.next, __typeof(*pos), member))

#define hlist_for_each_entry_safe(pos, n, head, member)			\
	for (pos = hlist_entry((head)->first, __typeof(*pos), member);	\
	     pos != NULL && (n = pos->member.next, 1);			\
	     pos = hlist_entry(n, __typeof(*pos), member))

#endif /* _DRM_LINUX_LIST_H_ */
