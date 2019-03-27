/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)queue.h	8.5 (Berkeley) 8/20/94
 * $FreeBSD$
 */

#ifndef _SYS_QUEUE_H_
#define	_SYS_QUEUE_H_

#include <sys/cdefs.h>

/*
 * This file defines four types of data structures: singly-linked lists,
 * singly-linked tail queues, lists and tail queues.
 *
 * A singly-linked list is headed by a single forward pointer. The elements
 * are singly linked for minimum space and pointer manipulation overhead at
 * the expense of O(n) removal for arbitrary elements. New elements can be
 * added to the list after an existing element or at the head of the list.
 * Elements being removed from the head of the list should use the explicit
 * macro for this purpose for optimum efficiency. A singly-linked list may
 * only be traversed in the forward direction.  Singly-linked lists are ideal
 * for applications with large datasets and few or no removals or for
 * implementing a LIFO queue.
 *
 * A singly-linked tail queue is headed by a pair of pointers, one to the
 * head of the list and the other to the tail of the list. The elements are
 * singly linked for minimum space and pointer manipulation overhead at the
 * expense of O(n) removal for arbitrary elements. New elements can be added
 * to the list after an existing element, at the head of the list, or at the
 * end of the list. Elements being removed from the head of the tail queue
 * should use the explicit macro for this purpose for optimum efficiency.
 * A singly-linked tail queue may only be traversed in the forward direction.
 * Singly-linked tail queues are ideal for applications with large datasets
 * and few or no removals or for implementing a FIFO queue.
 *
 * A list is headed by a single forward pointer (or an array of forward
 * pointers for a hash table header). The elements are doubly linked
 * so that an arbitrary element can be removed without a need to
 * traverse the list. New elements can be added to the list before
 * or after an existing element or at the head of the list. A list
 * may be traversed in either direction.
 *
 * A tail queue is headed by a pair of pointers, one to the head of the
 * list and the other to the tail of the list. The elements are doubly
 * linked so that an arbitrary element can be removed without a need to
 * traverse the list. New elements can be added to the list before or
 * after an existing element, at the head of the list, or at the end of
 * the list. A tail queue may be traversed in either direction.
 *
 * For details on the use of these macros, see the queue(3) manual page.
 *
 * Below is a summary of implemented functions where:
 *  +  means the macro is available
 *  -  means the macro is not available
 *  s  means the macro is available but is slow (runs in O(n) time)
 *
 *				SLIST	LIST	STAILQ	TAILQ
 * _HEAD			+	+	+	+
 * _CLASS_HEAD			+	+	+	+
 * _HEAD_INITIALIZER		+	+	+	+
 * _ENTRY			+	+	+	+
 * _CLASS_ENTRY			+	+	+	+
 * _INIT			+	+	+	+
 * _EMPTY			+	+	+	+
 * _FIRST			+	+	+	+
 * _NEXT			+	+	+	+
 * _PREV			-	+	-	+
 * _LAST			-	-	+	+
 * _LAST_FAST			-	-	-	+
 * _FOREACH			+	+	+	+
 * _FOREACH_FROM		+	+	+	+
 * _FOREACH_SAFE		+	+	+	+
 * _FOREACH_FROM_SAFE		+	+	+	+
 * _FOREACH_REVERSE		-	-	-	+
 * _FOREACH_REVERSE_FROM	-	-	-	+
 * _FOREACH_REVERSE_SAFE	-	-	-	+
 * _FOREACH_REVERSE_FROM_SAFE	-	-	-	+
 * _INSERT_HEAD			+	+	+	+
 * _INSERT_BEFORE		-	+	-	+
 * _INSERT_AFTER		+	+	+	+
 * _INSERT_TAIL			-	-	+	+
 * _CONCAT			s	s	+	+
 * _REMOVE_AFTER		+	-	+	-
 * _REMOVE_HEAD			+	-	+	-
 * _REMOVE			s	+	s	+
 * _SWAP			+	+	+	+
 *
 */
#ifdef QUEUE_MACRO_DEBUG
#warn Use QUEUE_MACRO_DEBUG_TRACE and/or QUEUE_MACRO_DEBUG_TRASH
#define	QUEUE_MACRO_DEBUG_TRACE
#define	QUEUE_MACRO_DEBUG_TRASH
#endif

#ifdef QUEUE_MACRO_DEBUG_TRACE
/* Store the last 2 places the queue element or head was altered */
struct qm_trace {
	unsigned long	 lastline;
	unsigned long	 prevline;
	const char	*lastfile;
	const char	*prevfile;
};

#define	TRACEBUF	struct qm_trace trace;
#define	TRACEBUF_INITIALIZER	{ __LINE__, 0, __FILE__, NULL } ,

#define	QMD_TRACE_HEAD(head) do {					\
	(head)->trace.prevline = (head)->trace.lastline;		\
	(head)->trace.prevfile = (head)->trace.lastfile;		\
	(head)->trace.lastline = __LINE__;				\
	(head)->trace.lastfile = __FILE__;				\
} while (0)

#define	QMD_TRACE_ELEM(elem) do {					\
	(elem)->trace.prevline = (elem)->trace.lastline;		\
	(elem)->trace.prevfile = (elem)->trace.lastfile;		\
	(elem)->trace.lastline = __LINE__;				\
	(elem)->trace.lastfile = __FILE__;				\
} while (0)

#else	/* !QUEUE_MACRO_DEBUG_TRACE */
#define	QMD_TRACE_ELEM(elem)
#define	QMD_TRACE_HEAD(head)
#define	TRACEBUF
#define	TRACEBUF_INITIALIZER
#endif	/* QUEUE_MACRO_DEBUG_TRACE */

#ifdef QUEUE_MACRO_DEBUG_TRASH
#define	QMD_SAVELINK(name, link)	void **name = (void *)&(link)
#define	TRASHIT(x)		do {(x) = (void *)-1;} while (0)
#define	QMD_IS_TRASHED(x)	((x) == (void *)(intptr_t)-1)
#else	/* !QUEUE_MACRO_DEBUG_TRASH */
#define	QMD_SAVELINK(name, link)
#define	TRASHIT(x)
#define	QMD_IS_TRASHED(x)	0
#endif	/* QUEUE_MACRO_DEBUG_TRASH */

#ifdef __cplusplus
/*
 * In C++ there can be structure lists and class lists:
 */
#define	QUEUE_TYPEOF(type) type
#else
#define	QUEUE_TYPEOF(type) struct type
#endif

/*
 * Singly-linked List declarations.
 */
#define	SLIST_HEAD(name, type)						\
struct name {								\
	struct type *slh_first;	/* first element */			\
}

#define	SLIST_CLASS_HEAD(name, type)					\
struct name {								\
	class type *slh_first;	/* first element */			\
}

#define	SLIST_HEAD_INITIALIZER(head)					\
	{ NULL }

#define	SLIST_ENTRY(type)						\
struct {								\
	struct type *sle_next;	/* next element */			\
}

#define	SLIST_CLASS_ENTRY(type)						\
struct {								\
	class type *sle_next;		/* next element */		\
}

/*
 * Singly-linked List functions.
 */
#if (defined(_KERNEL) && defined(INVARIANTS))
#define	QMD_SLIST_CHECK_PREVPTR(prevp, elm) do {			\
	if (*(prevp) != (elm))						\
		panic("Bad prevptr *(%p) == %p != %p",			\
		    (prevp), *(prevp), (elm));				\
} while (0)
#else
#define	QMD_SLIST_CHECK_PREVPTR(prevp, elm)
#endif

#define SLIST_CONCAT(head1, head2, type, field) do {			\
	QUEUE_TYPEOF(type) *curelm = SLIST_FIRST(head1);		\
	if (curelm == NULL) {						\
		if ((SLIST_FIRST(head1) = SLIST_FIRST(head2)) != NULL)	\
			SLIST_INIT(head2);				\
	} else if (SLIST_FIRST(head2) != NULL) {			\
		while (SLIST_NEXT(curelm, field) != NULL)		\
			curelm = SLIST_NEXT(curelm, field);		\
		SLIST_NEXT(curelm, field) = SLIST_FIRST(head2);		\
		SLIST_INIT(head2);					\
	}								\
} while (0)

#define	SLIST_EMPTY(head)	((head)->slh_first == NULL)

#define	SLIST_FIRST(head)	((head)->slh_first)

#define	SLIST_FOREACH(var, head, field)					\
	for ((var) = SLIST_FIRST((head));				\
	    (var);							\
	    (var) = SLIST_NEXT((var), field))

#define	SLIST_FOREACH_FROM(var, head, field)				\
	for ((var) = ((var) ? (var) : SLIST_FIRST((head)));		\
	    (var);							\
	    (var) = SLIST_NEXT((var), field))

#define	SLIST_FOREACH_SAFE(var, head, field, tvar)			\
	for ((var) = SLIST_FIRST((head));				\
	    (var) && ((tvar) = SLIST_NEXT((var), field), 1);		\
	    (var) = (tvar))

#define	SLIST_FOREACH_FROM_SAFE(var, head, field, tvar)			\
	for ((var) = ((var) ? (var) : SLIST_FIRST((head)));		\
	    (var) && ((tvar) = SLIST_NEXT((var), field), 1);		\
	    (var) = (tvar))

#define	SLIST_FOREACH_PREVPTR(var, varp, head, field)			\
	for ((varp) = &SLIST_FIRST((head));				\
	    ((var) = *(varp)) != NULL;					\
	    (varp) = &SLIST_NEXT((var), field))

#define	SLIST_INIT(head) do {						\
	SLIST_FIRST((head)) = NULL;					\
} while (0)

#define	SLIST_INSERT_AFTER(slistelm, elm, field) do {			\
	SLIST_NEXT((elm), field) = SLIST_NEXT((slistelm), field);	\
	SLIST_NEXT((slistelm), field) = (elm);				\
} while (0)

#define	SLIST_INSERT_HEAD(head, elm, field) do {			\
	SLIST_NEXT((elm), field) = SLIST_FIRST((head));			\
	SLIST_FIRST((head)) = (elm);					\
} while (0)

#define	SLIST_NEXT(elm, field)	((elm)->field.sle_next)

#define	SLIST_REMOVE(head, elm, type, field) do {			\
	QMD_SAVELINK(oldnext, (elm)->field.sle_next);			\
	if (SLIST_FIRST((head)) == (elm)) {				\
		SLIST_REMOVE_HEAD((head), field);			\
	}								\
	else {								\
		QUEUE_TYPEOF(type) *curelm = SLIST_FIRST(head);		\
		while (SLIST_NEXT(curelm, field) != (elm))		\
			curelm = SLIST_NEXT(curelm, field);		\
		SLIST_REMOVE_AFTER(curelm, field);			\
	}								\
	TRASHIT(*oldnext);						\
} while (0)

#define SLIST_REMOVE_AFTER(elm, field) do {				\
	SLIST_NEXT(elm, field) =					\
	    SLIST_NEXT(SLIST_NEXT(elm, field), field);			\
} while (0)

#define	SLIST_REMOVE_HEAD(head, field) do {				\
	SLIST_FIRST((head)) = SLIST_NEXT(SLIST_FIRST((head)), field);	\
} while (0)

#define	SLIST_REMOVE_PREVPTR(prevp, elm, field) do {			\
	QMD_SLIST_CHECK_PREVPTR(prevp, elm);				\
	*(prevp) = SLIST_NEXT(elm, field);				\
	TRASHIT((elm)->field.sle_next);					\
} while (0)

#define SLIST_SWAP(head1, head2, type) do {				\
	QUEUE_TYPEOF(type) *swap_first = SLIST_FIRST(head1);		\
	SLIST_FIRST(head1) = SLIST_FIRST(head2);			\
	SLIST_FIRST(head2) = swap_first;				\
} while (0)

/*
 * Singly-linked Tail queue declarations.
 */
#define	STAILQ_HEAD(name, type)						\
struct name {								\
	struct type *stqh_first;/* first element */			\
	struct type **stqh_last;/* addr of last next element */		\
}

#define	STAILQ_CLASS_HEAD(name, type)					\
struct name {								\
	class type *stqh_first;	/* first element */			\
	class type **stqh_last;	/* addr of last next element */		\
}

#define	STAILQ_HEAD_INITIALIZER(head)					\
	{ NULL, &(head).stqh_first }

#define	STAILQ_ENTRY(type)						\
struct {								\
	struct type *stqe_next;	/* next element */			\
}

#define	STAILQ_CLASS_ENTRY(type)					\
struct {								\
	class type *stqe_next;	/* next element */			\
}

/*
 * Singly-linked Tail queue functions.
 */
#define	STAILQ_CONCAT(head1, head2) do {				\
	if (!STAILQ_EMPTY((head2))) {					\
		*(head1)->stqh_last = (head2)->stqh_first;		\
		(head1)->stqh_last = (head2)->stqh_last;		\
		STAILQ_INIT((head2));					\
	}								\
} while (0)

#define	STAILQ_EMPTY(head)	((head)->stqh_first == NULL)

#define	STAILQ_FIRST(head)	((head)->stqh_first)

#define	STAILQ_FOREACH(var, head, field)				\
	for((var) = STAILQ_FIRST((head));				\
	   (var);							\
	   (var) = STAILQ_NEXT((var), field))

#define	STAILQ_FOREACH_FROM(var, head, field)				\
	for ((var) = ((var) ? (var) : STAILQ_FIRST((head)));		\
	   (var);							\
	   (var) = STAILQ_NEXT((var), field))

#define	STAILQ_FOREACH_SAFE(var, head, field, tvar)			\
	for ((var) = STAILQ_FIRST((head));				\
	    (var) && ((tvar) = STAILQ_NEXT((var), field), 1);		\
	    (var) = (tvar))

#define	STAILQ_FOREACH_FROM_SAFE(var, head, field, tvar)		\
	for ((var) = ((var) ? (var) : STAILQ_FIRST((head)));		\
	    (var) && ((tvar) = STAILQ_NEXT((var), field), 1);		\
	    (var) = (tvar))

#define	STAILQ_INIT(head) do {						\
	STAILQ_FIRST((head)) = NULL;					\
	(head)->stqh_last = &STAILQ_FIRST((head));			\
} while (0)

#define	STAILQ_INSERT_AFTER(head, tqelm, elm, field) do {		\
	if ((STAILQ_NEXT((elm), field) = STAILQ_NEXT((tqelm), field)) == NULL)\
		(head)->stqh_last = &STAILQ_NEXT((elm), field);		\
	STAILQ_NEXT((tqelm), field) = (elm);				\
} while (0)

#define	STAILQ_INSERT_HEAD(head, elm, field) do {			\
	if ((STAILQ_NEXT((elm), field) = STAILQ_FIRST((head))) == NULL)	\
		(head)->stqh_last = &STAILQ_NEXT((elm), field);		\
	STAILQ_FIRST((head)) = (elm);					\
} while (0)

#define	STAILQ_INSERT_TAIL(head, elm, field) do {			\
	STAILQ_NEXT((elm), field) = NULL;				\
	*(head)->stqh_last = (elm);					\
	(head)->stqh_last = &STAILQ_NEXT((elm), field);			\
} while (0)

#define	STAILQ_LAST(head, type, field)				\
	(STAILQ_EMPTY((head)) ? NULL :				\
	    __containerof((head)->stqh_last,			\
	    QUEUE_TYPEOF(type), field.stqe_next))

#define	STAILQ_NEXT(elm, field)	((elm)->field.stqe_next)

#define	STAILQ_REMOVE(head, elm, type, field) do {			\
	QMD_SAVELINK(oldnext, (elm)->field.stqe_next);			\
	if (STAILQ_FIRST((head)) == (elm)) {				\
		STAILQ_REMOVE_HEAD((head), field);			\
	}								\
	else {								\
		QUEUE_TYPEOF(type) *curelm = STAILQ_FIRST(head);	\
		while (STAILQ_NEXT(curelm, field) != (elm))		\
			curelm = STAILQ_NEXT(curelm, field);		\
		STAILQ_REMOVE_AFTER(head, curelm, field);		\
	}								\
	TRASHIT(*oldnext);						\
} while (0)

#define STAILQ_REMOVE_AFTER(head, elm, field) do {			\
	if ((STAILQ_NEXT(elm, field) =					\
	     STAILQ_NEXT(STAILQ_NEXT(elm, field), field)) == NULL)	\
		(head)->stqh_last = &STAILQ_NEXT((elm), field);		\
} while (0)

#define	STAILQ_REMOVE_HEAD(head, field) do {				\
	if ((STAILQ_FIRST((head)) =					\
	     STAILQ_NEXT(STAILQ_FIRST((head)), field)) == NULL)		\
		(head)->stqh_last = &STAILQ_FIRST((head));		\
} while (0)

#define STAILQ_SWAP(head1, head2, type) do {				\
	QUEUE_TYPEOF(type) *swap_first = STAILQ_FIRST(head1);		\
	QUEUE_TYPEOF(type) **swap_last = (head1)->stqh_last;		\
	STAILQ_FIRST(head1) = STAILQ_FIRST(head2);			\
	(head1)->stqh_last = (head2)->stqh_last;			\
	STAILQ_FIRST(head2) = swap_first;				\
	(head2)->stqh_last = swap_last;					\
	if (STAILQ_EMPTY(head1))					\
		(head1)->stqh_last = &STAILQ_FIRST(head1);		\
	if (STAILQ_EMPTY(head2))					\
		(head2)->stqh_last = &STAILQ_FIRST(head2);		\
} while (0)


/*
 * List declarations.
 */
#define	LIST_HEAD(name, type)						\
struct name {								\
	struct type *lh_first;	/* first element */			\
}

#define	LIST_CLASS_HEAD(name, type)					\
struct name {								\
	class type *lh_first;	/* first element */			\
}

#define	LIST_HEAD_INITIALIZER(head)					\
	{ NULL }

#define	LIST_ENTRY(type)						\
struct {								\
	struct type *le_next;	/* next element */			\
	struct type **le_prev;	/* address of previous next element */	\
}

#define	LIST_CLASS_ENTRY(type)						\
struct {								\
	class type *le_next;	/* next element */			\
	class type **le_prev;	/* address of previous next element */	\
}

/*
 * List functions.
 */

#if (defined(_KERNEL) && defined(INVARIANTS))
/*
 * QMD_LIST_CHECK_HEAD(LIST_HEAD *head, LIST_ENTRY NAME)
 *
 * If the list is non-empty, validates that the first element of the list
 * points back at 'head.'
 */
#define	QMD_LIST_CHECK_HEAD(head, field) do {				\
	if (LIST_FIRST((head)) != NULL &&				\
	    LIST_FIRST((head))->field.le_prev !=			\
	     &LIST_FIRST((head)))					\
		panic("Bad list head %p first->prev != head", (head));	\
} while (0)

/*
 * QMD_LIST_CHECK_NEXT(TYPE *elm, LIST_ENTRY NAME)
 *
 * If an element follows 'elm' in the list, validates that the next element
 * points back at 'elm.'
 */
#define	QMD_LIST_CHECK_NEXT(elm, field) do {				\
	if (LIST_NEXT((elm), field) != NULL &&				\
	    LIST_NEXT((elm), field)->field.le_prev !=			\
	     &((elm)->field.le_next))					\
	     	panic("Bad link elm %p next->prev != elm", (elm));	\
} while (0)

/*
 * QMD_LIST_CHECK_PREV(TYPE *elm, LIST_ENTRY NAME)
 *
 * Validates that the previous element (or head of the list) points to 'elm.'
 */
#define	QMD_LIST_CHECK_PREV(elm, field) do {				\
	if (*(elm)->field.le_prev != (elm))				\
		panic("Bad link elm %p prev->next != elm", (elm));	\
} while (0)
#else
#define	QMD_LIST_CHECK_HEAD(head, field)
#define	QMD_LIST_CHECK_NEXT(elm, field)
#define	QMD_LIST_CHECK_PREV(elm, field)
#endif /* (_KERNEL && INVARIANTS) */

#define LIST_CONCAT(head1, head2, type, field) do {			      \
	QUEUE_TYPEOF(type) *curelm = LIST_FIRST(head1);			      \
	if (curelm == NULL) {						      \
		if ((LIST_FIRST(head1) = LIST_FIRST(head2)) != NULL) {	      \
			LIST_FIRST(head2)->field.le_prev =		      \
			    &LIST_FIRST((head1));			      \
			LIST_INIT(head2);				      \
		}							      \
	} else if (LIST_FIRST(head2) != NULL) {				      \
		while (LIST_NEXT(curelm, field) != NULL)		      \
			curelm = LIST_NEXT(curelm, field);		      \
		LIST_NEXT(curelm, field) = LIST_FIRST(head2);		      \
		LIST_FIRST(head2)->field.le_prev = &LIST_NEXT(curelm, field); \
		LIST_INIT(head2);					      \
	}								      \
} while (0)

#define	LIST_EMPTY(head)	((head)->lh_first == NULL)

#define	LIST_FIRST(head)	((head)->lh_first)

#define	LIST_FOREACH(var, head, field)					\
	for ((var) = LIST_FIRST((head));				\
	    (var);							\
	    (var) = LIST_NEXT((var), field))

#define	LIST_FOREACH_FROM(var, head, field)				\
	for ((var) = ((var) ? (var) : LIST_FIRST((head)));		\
	    (var);							\
	    (var) = LIST_NEXT((var), field))

#define	LIST_FOREACH_SAFE(var, head, field, tvar)			\
	for ((var) = LIST_FIRST((head));				\
	    (var) && ((tvar) = LIST_NEXT((var), field), 1);		\
	    (var) = (tvar))

#define	LIST_FOREACH_FROM_SAFE(var, head, field, tvar)			\
	for ((var) = ((var) ? (var) : LIST_FIRST((head)));		\
	    (var) && ((tvar) = LIST_NEXT((var), field), 1);		\
	    (var) = (tvar))

#define	LIST_INIT(head) do {						\
	LIST_FIRST((head)) = NULL;					\
} while (0)

#define	LIST_INSERT_AFTER(listelm, elm, field) do {			\
	QMD_LIST_CHECK_NEXT(listelm, field);				\
	if ((LIST_NEXT((elm), field) = LIST_NEXT((listelm), field)) != NULL)\
		LIST_NEXT((listelm), field)->field.le_prev =		\
		    &LIST_NEXT((elm), field);				\
	LIST_NEXT((listelm), field) = (elm);				\
	(elm)->field.le_prev = &LIST_NEXT((listelm), field);		\
} while (0)

#define	LIST_INSERT_BEFORE(listelm, elm, field) do {			\
	QMD_LIST_CHECK_PREV(listelm, field);				\
	(elm)->field.le_prev = (listelm)->field.le_prev;		\
	LIST_NEXT((elm), field) = (listelm);				\
	*(listelm)->field.le_prev = (elm);				\
	(listelm)->field.le_prev = &LIST_NEXT((elm), field);		\
} while (0)

#define	LIST_INSERT_HEAD(head, elm, field) do {				\
	QMD_LIST_CHECK_HEAD((head), field);				\
	if ((LIST_NEXT((elm), field) = LIST_FIRST((head))) != NULL)	\
		LIST_FIRST((head))->field.le_prev = &LIST_NEXT((elm), field);\
	LIST_FIRST((head)) = (elm);					\
	(elm)->field.le_prev = &LIST_FIRST((head));			\
} while (0)

#define	LIST_NEXT(elm, field)	((elm)->field.le_next)

#define	LIST_PREV(elm, head, type, field)			\
	((elm)->field.le_prev == &LIST_FIRST((head)) ? NULL :	\
	    __containerof((elm)->field.le_prev,			\
	    QUEUE_TYPEOF(type), field.le_next))

#define	LIST_REMOVE(elm, field) do {					\
	QMD_SAVELINK(oldnext, (elm)->field.le_next);			\
	QMD_SAVELINK(oldprev, (elm)->field.le_prev);			\
	QMD_LIST_CHECK_NEXT(elm, field);				\
	QMD_LIST_CHECK_PREV(elm, field);				\
	if (LIST_NEXT((elm), field) != NULL)				\
		LIST_NEXT((elm), field)->field.le_prev = 		\
		    (elm)->field.le_prev;				\
	*(elm)->field.le_prev = LIST_NEXT((elm), field);		\
	TRASHIT(*oldnext);						\
	TRASHIT(*oldprev);						\
} while (0)

#define LIST_SWAP(head1, head2, type, field) do {			\
	QUEUE_TYPEOF(type) *swap_tmp = LIST_FIRST(head1);		\
	LIST_FIRST((head1)) = LIST_FIRST((head2));			\
	LIST_FIRST((head2)) = swap_tmp;					\
	if ((swap_tmp = LIST_FIRST((head1))) != NULL)			\
		swap_tmp->field.le_prev = &LIST_FIRST((head1));		\
	if ((swap_tmp = LIST_FIRST((head2))) != NULL)			\
		swap_tmp->field.le_prev = &LIST_FIRST((head2));		\
} while (0)

/*
 * Tail queue declarations.
 */
#define	TAILQ_HEAD(name, type)						\
struct name {								\
	struct type *tqh_first;	/* first element */			\
	struct type **tqh_last;	/* addr of last next element */		\
	TRACEBUF							\
}

#define	TAILQ_CLASS_HEAD(name, type)					\
struct name {								\
	class type *tqh_first;	/* first element */			\
	class type **tqh_last;	/* addr of last next element */		\
	TRACEBUF							\
}

#define	TAILQ_HEAD_INITIALIZER(head)					\
	{ NULL, &(head).tqh_first, TRACEBUF_INITIALIZER }

#define	TAILQ_ENTRY(type)						\
struct {								\
	struct type *tqe_next;	/* next element */			\
	struct type **tqe_prev;	/* address of previous next element */	\
	TRACEBUF							\
}

#define	TAILQ_CLASS_ENTRY(type)						\
struct {								\
	class type *tqe_next;	/* next element */			\
	class type **tqe_prev;	/* address of previous next element */	\
	TRACEBUF							\
}

/*
 * Tail queue functions.
 */
#if (defined(_KERNEL) && defined(INVARIANTS))
/*
 * QMD_TAILQ_CHECK_HEAD(TAILQ_HEAD *head, TAILQ_ENTRY NAME)
 *
 * If the tailq is non-empty, validates that the first element of the tailq
 * points back at 'head.'
 */
#define	QMD_TAILQ_CHECK_HEAD(head, field) do {				\
	if (!TAILQ_EMPTY(head) &&					\
	    TAILQ_FIRST((head))->field.tqe_prev !=			\
	     &TAILQ_FIRST((head)))					\
		panic("Bad tailq head %p first->prev != head", (head));	\
} while (0)

/*
 * QMD_TAILQ_CHECK_TAIL(TAILQ_HEAD *head, TAILQ_ENTRY NAME)
 *
 * Validates that the tail of the tailq is a pointer to pointer to NULL.
 */
#define	QMD_TAILQ_CHECK_TAIL(head, field) do {				\
	if (*(head)->tqh_last != NULL)					\
	    	panic("Bad tailq NEXT(%p->tqh_last) != NULL", (head)); 	\
} while (0)

/*
 * QMD_TAILQ_CHECK_NEXT(TYPE *elm, TAILQ_ENTRY NAME)
 *
 * If an element follows 'elm' in the tailq, validates that the next element
 * points back at 'elm.'
 */
#define	QMD_TAILQ_CHECK_NEXT(elm, field) do {				\
	if (TAILQ_NEXT((elm), field) != NULL &&				\
	    TAILQ_NEXT((elm), field)->field.tqe_prev !=			\
	     &((elm)->field.tqe_next))					\
		panic("Bad link elm %p next->prev != elm", (elm));	\
} while (0)

/*
 * QMD_TAILQ_CHECK_PREV(TYPE *elm, TAILQ_ENTRY NAME)
 *
 * Validates that the previous element (or head of the tailq) points to 'elm.'
 */
#define	QMD_TAILQ_CHECK_PREV(elm, field) do {				\
	if (*(elm)->field.tqe_prev != (elm))				\
		panic("Bad link elm %p prev->next != elm", (elm));	\
} while (0)
#else
#define	QMD_TAILQ_CHECK_HEAD(head, field)
#define	QMD_TAILQ_CHECK_TAIL(head, headname)
#define	QMD_TAILQ_CHECK_NEXT(elm, field)
#define	QMD_TAILQ_CHECK_PREV(elm, field)
#endif /* (_KERNEL && INVARIANTS) */

#define	TAILQ_CONCAT(head1, head2, field) do {				\
	if (!TAILQ_EMPTY(head2)) {					\
		*(head1)->tqh_last = (head2)->tqh_first;		\
		(head2)->tqh_first->field.tqe_prev = (head1)->tqh_last;	\
		(head1)->tqh_last = (head2)->tqh_last;			\
		TAILQ_INIT((head2));					\
		QMD_TRACE_HEAD(head1);					\
		QMD_TRACE_HEAD(head2);					\
	}								\
} while (0)

#define	TAILQ_EMPTY(head)	((head)->tqh_first == NULL)

#define	TAILQ_FIRST(head)	((head)->tqh_first)

#define	TAILQ_FOREACH(var, head, field)					\
	for ((var) = TAILQ_FIRST((head));				\
	    (var);							\
	    (var) = TAILQ_NEXT((var), field))

#define	TAILQ_FOREACH_FROM(var, head, field)				\
	for ((var) = ((var) ? (var) : TAILQ_FIRST((head)));		\
	    (var);							\
	    (var) = TAILQ_NEXT((var), field))

#define	TAILQ_FOREACH_SAFE(var, head, field, tvar)			\
	for ((var) = TAILQ_FIRST((head));				\
	    (var) && ((tvar) = TAILQ_NEXT((var), field), 1);		\
	    (var) = (tvar))

#define	TAILQ_FOREACH_FROM_SAFE(var, head, field, tvar)			\
	for ((var) = ((var) ? (var) : TAILQ_FIRST((head)));		\
	    (var) && ((tvar) = TAILQ_NEXT((var), field), 1);		\
	    (var) = (tvar))

#define	TAILQ_FOREACH_REVERSE(var, head, headname, field)		\
	for ((var) = TAILQ_LAST((head), headname);			\
	    (var);							\
	    (var) = TAILQ_PREV((var), headname, field))

#define	TAILQ_FOREACH_REVERSE_FROM(var, head, headname, field)		\
	for ((var) = ((var) ? (var) : TAILQ_LAST((head), headname));	\
	    (var);							\
	    (var) = TAILQ_PREV((var), headname, field))

#define	TAILQ_FOREACH_REVERSE_SAFE(var, head, headname, field, tvar)	\
	for ((var) = TAILQ_LAST((head), headname);			\
	    (var) && ((tvar) = TAILQ_PREV((var), headname, field), 1);	\
	    (var) = (tvar))

#define	TAILQ_FOREACH_REVERSE_FROM_SAFE(var, head, headname, field, tvar) \
	for ((var) = ((var) ? (var) : TAILQ_LAST((head), headname));	\
	    (var) && ((tvar) = TAILQ_PREV((var), headname, field), 1);	\
	    (var) = (tvar))

#define	TAILQ_INIT(head) do {						\
	TAILQ_FIRST((head)) = NULL;					\
	(head)->tqh_last = &TAILQ_FIRST((head));			\
	QMD_TRACE_HEAD(head);						\
} while (0)

#define	TAILQ_INSERT_AFTER(head, listelm, elm, field) do {		\
	QMD_TAILQ_CHECK_NEXT(listelm, field);				\
	if ((TAILQ_NEXT((elm), field) = TAILQ_NEXT((listelm), field)) != NULL)\
		TAILQ_NEXT((elm), field)->field.tqe_prev = 		\
		    &TAILQ_NEXT((elm), field);				\
	else {								\
		(head)->tqh_last = &TAILQ_NEXT((elm), field);		\
		QMD_TRACE_HEAD(head);					\
	}								\
	TAILQ_NEXT((listelm), field) = (elm);				\
	(elm)->field.tqe_prev = &TAILQ_NEXT((listelm), field);		\
	QMD_TRACE_ELEM(&(elm)->field);					\
	QMD_TRACE_ELEM(&(listelm)->field);				\
} while (0)

#define	TAILQ_INSERT_BEFORE(listelm, elm, field) do {			\
	QMD_TAILQ_CHECK_PREV(listelm, field);				\
	(elm)->field.tqe_prev = (listelm)->field.tqe_prev;		\
	TAILQ_NEXT((elm), field) = (listelm);				\
	*(listelm)->field.tqe_prev = (elm);				\
	(listelm)->field.tqe_prev = &TAILQ_NEXT((elm), field);		\
	QMD_TRACE_ELEM(&(elm)->field);					\
	QMD_TRACE_ELEM(&(listelm)->field);				\
} while (0)

#define	TAILQ_INSERT_HEAD(head, elm, field) do {			\
	QMD_TAILQ_CHECK_HEAD(head, field);				\
	if ((TAILQ_NEXT((elm), field) = TAILQ_FIRST((head))) != NULL)	\
		TAILQ_FIRST((head))->field.tqe_prev =			\
		    &TAILQ_NEXT((elm), field);				\
	else								\
		(head)->tqh_last = &TAILQ_NEXT((elm), field);		\
	TAILQ_FIRST((head)) = (elm);					\
	(elm)->field.tqe_prev = &TAILQ_FIRST((head));			\
	QMD_TRACE_HEAD(head);						\
	QMD_TRACE_ELEM(&(elm)->field);					\
} while (0)

#define	TAILQ_INSERT_TAIL(head, elm, field) do {			\
	QMD_TAILQ_CHECK_TAIL(head, field);				\
	TAILQ_NEXT((elm), field) = NULL;				\
	(elm)->field.tqe_prev = (head)->tqh_last;			\
	*(head)->tqh_last = (elm);					\
	(head)->tqh_last = &TAILQ_NEXT((elm), field);			\
	QMD_TRACE_HEAD(head);						\
	QMD_TRACE_ELEM(&(elm)->field);					\
} while (0)

#define	TAILQ_LAST(head, headname)					\
	(*(((struct headname *)((head)->tqh_last))->tqh_last))

/*
 * The FAST function is fast in that it causes no data access other
 * then the access to the head. The standard LAST function above
 * will cause a data access of both the element you want and 
 * the previous element. FAST is very useful for instances when
 * you may want to prefetch the last data element.
 */
#define	TAILQ_LAST_FAST(head, type, field)			\
    (TAILQ_EMPTY(head) ? NULL : __containerof((head)->tqh_last, QUEUE_TYPEOF(type), field.tqe_next))

#define	TAILQ_NEXT(elm, field) ((elm)->field.tqe_next)

#define	TAILQ_PREV(elm, headname, field)				\
	(*(((struct headname *)((elm)->field.tqe_prev))->tqh_last))

#define	TAILQ_REMOVE(head, elm, field) do {				\
	QMD_SAVELINK(oldnext, (elm)->field.tqe_next);			\
	QMD_SAVELINK(oldprev, (elm)->field.tqe_prev);			\
	QMD_TAILQ_CHECK_NEXT(elm, field);				\
	QMD_TAILQ_CHECK_PREV(elm, field);				\
	if ((TAILQ_NEXT((elm), field)) != NULL)				\
		TAILQ_NEXT((elm), field)->field.tqe_prev = 		\
		    (elm)->field.tqe_prev;				\
	else {								\
		(head)->tqh_last = (elm)->field.tqe_prev;		\
		QMD_TRACE_HEAD(head);					\
	}								\
	*(elm)->field.tqe_prev = TAILQ_NEXT((elm), field);		\
	TRASHIT(*oldnext);						\
	TRASHIT(*oldprev);						\
	QMD_TRACE_ELEM(&(elm)->field);					\
} while (0)

#define TAILQ_SWAP(head1, head2, type, field) do {			\
	QUEUE_TYPEOF(type) *swap_first = (head1)->tqh_first;		\
	QUEUE_TYPEOF(type) **swap_last = (head1)->tqh_last;		\
	(head1)->tqh_first = (head2)->tqh_first;			\
	(head1)->tqh_last = (head2)->tqh_last;				\
	(head2)->tqh_first = swap_first;				\
	(head2)->tqh_last = swap_last;					\
	if ((swap_first = (head1)->tqh_first) != NULL)			\
		swap_first->field.tqe_prev = &(head1)->tqh_first;	\
	else								\
		(head1)->tqh_last = &(head1)->tqh_first;		\
	if ((swap_first = (head2)->tqh_first) != NULL)			\
		swap_first->field.tqe_prev = &(head2)->tqh_first;	\
	else								\
		(head2)->tqh_last = &(head2)->tqh_first;		\
} while (0)

#endif /* !_SYS_QUEUE_H_ */
