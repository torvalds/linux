/*	$OpenBSD: list.h,v 1.2 2021/07/27 13:36:59 mglocker Exp $	*/
/* linux list functions for the BSDs, cut down for dwctwo(4).
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

#ifndef _DWC2_LINUX_LIST_H_
#define _DWC2_LINUX_LIST_H_

/*
 * From <dev/pci/drm/include/linux/kernel.h>
 */
#define container_of(ptr, type, member) ({                      \
        const __typeof( ((type *)0)->member ) *__mptr = (ptr);  \
        (type *)( (char *)__mptr - offsetof(type,member) );})

/*
 * From <dev/pci/drm/include/linux/types.h>
 */
typedef unsigned int gfp_t;

struct list_head {
        struct list_head *next, *prev;
};

/*
 * From <dev/pci/drm/include/linux/list.h>
 */
#define list_entry(ptr, type, member) container_of(ptr, type, member)

static inline void
INIT_LIST_HEAD(struct list_head *head) {
	(head)->next = head;
	(head)->prev = head;
}

static inline int
list_empty(const struct list_head *head) {
	return (head)->next == head;
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
list_del_init(struct list_head *entry) {
	(entry)->next->prev = (entry)->prev;
	(entry)->prev->next = (entry)->next;
	INIT_LIST_HEAD(entry);
}

#define list_for_each(entry, head)				\
    for (entry = (head)->next; entry != head; entry = (entry)->next)

#define list_for_each_safe(entry, temp, head)			\
    for (entry = (head)->next, temp = (entry)->next;		\
	entry != head; 						\
	entry = temp, temp = entry->next)

#define list_for_each_entry(pos, head, member)				\
    for (pos = list_entry((head)->next, __typeof(*pos), member);	\
	&pos->member != (head);					 	\
	pos = list_entry(pos->member.next, __typeof(*pos), member))

#define list_for_each_entry_safe(pos, n, head, member)			\
	for (pos = list_entry((head)->next, __typeof(*pos), member),	\
	    n = list_entry(pos->member.next, __typeof(*pos), member);	\
	    &pos->member != (head);					\
	    pos = n, n = list_entry(n->member.next, __typeof(*n), member))

#define list_first_entry(ptr, type, member) \
        list_entry((ptr)->next, type, member)

#endif /* _DWC2_LINUX_LIST_H_ */
