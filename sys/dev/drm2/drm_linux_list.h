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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#ifndef _DRM_LINUX_LIST_H_
#define _DRM_LINUX_LIST_H_

struct list_head {
	struct list_head *next, *prev;
};

#define list_entry(ptr, type, member) container_of(ptr,type,member)

static __inline__ void
INIT_LIST_HEAD(struct list_head *head) {
	(head)->next = head;
	(head)->prev = head;
}

#define LIST_HEAD_INIT(name) { &(name), &(name) }

#define DRM_LIST_HEAD(name) \
	struct list_head name = LIST_HEAD_INIT(name)

static __inline__ int
list_empty(const struct list_head *head) {
	return (head)->next == head;
}

static __inline__ void
list_add(struct list_head *new, struct list_head *head) {
        (head)->next->prev = new;
        (new)->next = (head)->next;
        (new)->prev = head;
        (head)->next = new;
}

static __inline__ void
list_add_tail(struct list_head *entry, struct list_head *head) {
	(entry)->prev = (head)->prev;
	(entry)->next = head;
	(head)->prev->next = entry;
	(head)->prev = entry;
}

static __inline__ void
list_del(struct list_head *entry) {
	(entry)->next->prev = (entry)->prev;
	(entry)->prev->next = (entry)->next;
}

static inline void list_replace(struct list_head *old,
				struct list_head *new)
{
	new->next = old->next;
	new->next->prev = new;
	new->prev = old->prev;
	new->prev->next = new;
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

static __inline__ void
list_del_init(struct list_head *entry) {
	(entry)->next->prev = (entry)->prev;
	(entry)->prev->next = (entry)->next;
	INIT_LIST_HEAD(entry);
}

#define list_for_each(entry, head)				\
    for (entry = (head)->next; entry != head; entry = (entry)->next)

#define list_for_each_prev(entry, head) \
        for (entry = (head)->prev; entry != (head); \
                entry = entry->prev)

#define list_for_each_safe(entry, temp, head)			\
    for (entry = (head)->next, temp = (entry)->next;		\
	entry != head; 						\
	entry = temp, temp = entry->next)

#define list_for_each_entry(pos, head, member)				\
    for (pos = list_entry((head)->next, __typeof(*pos), member);	\
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

#define list_for_each_entry_safe_from(pos, n, head, member) 			\
	for (n = list_entry(pos->member.next, __typeof(*pos), member);		\
	     &pos->member != (head);						\
	     pos = n, n = list_entry(n->member.next, __typeof(*n), member))

#define list_first_entry(ptr, type, member) \
	list_entry((ptr)->next, type, member)


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

void drm_list_sort(void *priv, struct list_head *head, int (*cmp)(void *priv,
    struct list_head *a, struct list_head *b));

/* hlist, copied from sys/dev/ofed/linux/list.h */

struct hlist_head {
	struct hlist_node *first;
};

struct hlist_node {
	struct hlist_node *next, **pprev;
};

#define	HLIST_HEAD_INIT { }
#define	HLIST_HEAD(name) struct hlist_head name = HLIST_HEAD_INIT
#define	INIT_HLIST_HEAD(head) (head)->first = NULL
#define	INIT_HLIST_NODE(node)						\
do {									\
	(node)->next = NULL;						\
	(node)->pprev = NULL;						\
} while (0)

static inline int
hlist_unhashed(const struct hlist_node *h)
{

	return !h->pprev;
}

static inline int
hlist_empty(const struct hlist_head *h)
{

	return !h->first;
}

static inline void
hlist_del(struct hlist_node *n)
{

        if (n->next)
                n->next->pprev = n->pprev;
        *n->pprev = n->next;
}

static inline void
hlist_del_init(struct hlist_node *n)
{

	if (hlist_unhashed(n))
		return;
	hlist_del(n);
	INIT_HLIST_NODE(n);
}

static inline void
hlist_add_head(struct hlist_node *n, struct hlist_head *h)
{

	n->next = h->first;
	if (h->first)
		h->first->pprev = &n->next;
	h->first = n;
	n->pprev = &h->first;
}

static inline void
hlist_add_before(struct hlist_node *n, struct hlist_node *next)
{

	n->pprev = next->pprev;
	n->next = next;
	next->pprev = &n->next;
	*(n->pprev) = n;
}

static inline void
hlist_add_after(struct hlist_node *n, struct hlist_node *next)
{

	next->next = n->next;
	n->next = next;
	next->pprev = &n->next;
	if (next->next)
		next->next->pprev = &next->next;
}

static inline void
hlist_move_list(struct hlist_head *old, struct hlist_head *new)
{

	new->first = old->first;
	if (new->first)
		new->first->pprev = &new->first;
	old->first = NULL;
}

#define	hlist_entry(ptr, type, field)	container_of(ptr, type, field)

#define	hlist_for_each(p, head)						\
	for (p = (head)->first; p; p = p->next)

#define	hlist_for_each_safe(p, n, head)					\
	for (p = (head)->first; p && ({ n = p->next; 1; }); p = n)

#define	hlist_for_each_entry(tp, p, head, field)			\
	for (p = (head)->first;						\
	    p ? (tp = hlist_entry(p, typeof(*tp), field)): NULL; p = p->next)

#define hlist_for_each_entry_continue(tp, p, field)			\
	for (p = (p)->next;						\
	    p ? (tp = hlist_entry(p, typeof(*tp), field)): NULL; p = p->next)

#define	hlist_for_each_entry_from(tp, p, field)				\
	for (; p ? (tp = hlist_entry(p, typeof(*tp), field)): NULL; p = p->next)

#define hlist_for_each_entry_safe(tpos, pos, n, head, member) 		 \
	for (pos = (head)->first;					 \
	     (pos) != 0 && ({ n = (pos)->next; \
		 tpos = hlist_entry((pos), typeof(*(tpos)), member); 1;}); \
	     pos = (n))

#endif /* _DRM_LINUX_LIST_H_ */
