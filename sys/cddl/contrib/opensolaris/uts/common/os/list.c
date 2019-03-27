/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Generic doubly-linked list implementation
 */

#include <sys/list.h>
#include <sys/list_impl.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/debug.h>

#define	list_d2l(a, obj) ((list_node_t *)(((char *)obj) + (a)->list_offset))
#define	list_object(a, node) ((void *)(((char *)node) - (a)->list_offset))
#define	list_empty(a) ((a)->list_head.list_next == &(a)->list_head)

#define	list_insert_after_node(list, node, object) {	\
	list_node_t *lnew = list_d2l(list, object);	\
	lnew->list_prev = (node);			\
	lnew->list_next = (node)->list_next;		\
	(node)->list_next->list_prev = lnew;		\
	(node)->list_next = lnew;			\
}

#define	list_insert_before_node(list, node, object) {	\
	list_node_t *lnew = list_d2l(list, object);	\
	lnew->list_next = (node);			\
	lnew->list_prev = (node)->list_prev;		\
	(node)->list_prev->list_next = lnew;		\
	(node)->list_prev = lnew;			\
}

#define	list_remove_node(node)					\
	(node)->list_prev->list_next = (node)->list_next;	\
	(node)->list_next->list_prev = (node)->list_prev;	\
	(node)->list_next = (node)->list_prev = NULL

void
list_create(list_t *list, size_t size, size_t offset)
{
	ASSERT(list);
	ASSERT(size > 0);
	ASSERT(size >= offset + sizeof (list_node_t));

	list->list_size = size;
	list->list_offset = offset;
	list->list_head.list_next = list->list_head.list_prev =
	    &list->list_head;
}

void
list_destroy(list_t *list)
{
	list_node_t *node = &list->list_head;

	ASSERT(list);
	ASSERT(list->list_head.list_next == node);
	ASSERT(list->list_head.list_prev == node);

	node->list_next = node->list_prev = NULL;
}

void
list_insert_after(list_t *list, void *object, void *nobject)
{
	if (object == NULL) {
		list_insert_head(list, nobject);
	} else {
		list_node_t *lold = list_d2l(list, object);
		list_insert_after_node(list, lold, nobject);
	}
}

void
list_insert_before(list_t *list, void *object, void *nobject)
{
	if (object == NULL) {
		list_insert_tail(list, nobject);
	} else {
		list_node_t *lold = list_d2l(list, object);
		list_insert_before_node(list, lold, nobject);
	}
}

void
list_insert_head(list_t *list, void *object)
{
	list_node_t *lold = &list->list_head;
	list_insert_after_node(list, lold, object);
}

void
list_insert_tail(list_t *list, void *object)
{
	list_node_t *lold = &list->list_head;
	list_insert_before_node(list, lold, object);
}

void
list_remove(list_t *list, void *object)
{
	list_node_t *lold = list_d2l(list, object);
	ASSERT(!list_empty(list));
	ASSERT(lold->list_next != NULL);
	list_remove_node(lold);
}

void *
list_remove_head(list_t *list)
{
	list_node_t *head = list->list_head.list_next;
	if (head == &list->list_head)
		return (NULL);
	list_remove_node(head);
	return (list_object(list, head));
}

void *
list_remove_tail(list_t *list)
{
	list_node_t *tail = list->list_head.list_prev;
	if (tail == &list->list_head)
		return (NULL);
	list_remove_node(tail);
	return (list_object(list, tail));
}

void *
list_head(list_t *list)
{
	if (list_empty(list))
		return (NULL);
	return (list_object(list, list->list_head.list_next));
}

void *
list_tail(list_t *list)
{
	if (list_empty(list))
		return (NULL);
	return (list_object(list, list->list_head.list_prev));
}

void *
list_next(list_t *list, void *object)
{
	list_node_t *node = list_d2l(list, object);

	if (node->list_next != &list->list_head)
		return (list_object(list, node->list_next));

	return (NULL);
}

void *
list_prev(list_t *list, void *object)
{
	list_node_t *node = list_d2l(list, object);

	if (node->list_prev != &list->list_head)
		return (list_object(list, node->list_prev));

	return (NULL);
}

/*
 *  Insert src list after dst list. Empty src list thereafter.
 */
void
list_move_tail(list_t *dst, list_t *src)
{
	list_node_t *dstnode = &dst->list_head;
	list_node_t *srcnode = &src->list_head;

	ASSERT(dst->list_size == src->list_size);
	ASSERT(dst->list_offset == src->list_offset);

	if (list_empty(src))
		return;

	dstnode->list_prev->list_next = srcnode->list_next;
	srcnode->list_next->list_prev = dstnode->list_prev;
	dstnode->list_prev = srcnode->list_prev;
	srcnode->list_prev->list_next = dstnode;

	/* empty src list */
	srcnode->list_next = srcnode->list_prev = srcnode;
}

void
list_link_replace(list_node_t *lold, list_node_t *lnew)
{
	ASSERT(list_link_active(lold));
	ASSERT(!list_link_active(lnew));

	lnew->list_next = lold->list_next;
	lnew->list_prev = lold->list_prev;
	lold->list_prev->list_next = lnew;
	lold->list_next->list_prev = lnew;
	lold->list_next = lold->list_prev = NULL;
}

void
list_link_init(list_node_t *link)
{
	link->list_next = NULL;
	link->list_prev = NULL;
}

int
list_link_active(list_node_t *link)
{
	return (link->list_next != NULL);
}

int
list_is_empty(list_t *list)
{
	return (list_empty(list));
}
