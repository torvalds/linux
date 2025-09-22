/*	$OpenBSD: smr.h,v 1.9 2022/07/25 08:06:44 visa Exp $	*/

/*
 * Copyright (c) 2019 Visa Hankala
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _SYS_SMR_H_
#define _SYS_SMR_H_

#include <sys/queue.h>

struct smr_entry {
	SIMPLEQ_ENTRY(smr_entry)	smr_list;
	void				(*smr_func)(void *);
	void				*smr_arg;
};

SIMPLEQ_HEAD(smr_entry_list, smr_entry);

#ifdef _KERNEL

#include <sys/atomic.h>

void	smr_startup(void);
void	smr_startup_thread(void);
void	smr_idle(void);
void	smr_read_enter(void);
void	smr_read_leave(void);

void	smr_call_impl(struct smr_entry *, void (*)(void *), void *, int);
void	smr_barrier_impl(int);

#define smr_call(entry, func, arg)	smr_call_impl(entry, func, arg, 0)
#define smr_barrier()			smr_barrier_impl(0)
#define smr_flush()			smr_barrier_impl(1)

static inline void
smr_init(struct smr_entry *smr)
{
	smr->smr_func = NULL;
	smr->smr_arg = NULL;
}

#ifdef DIAGNOSTIC
#define SMR_ASSERT_CRITICAL() do {					\
	if (panicstr == NULL && !db_active)				\
		KASSERT(curcpu()->ci_schedstate.spc_smrdepth > 0);	\
} while (0)
#define SMR_ASSERT_NONCRITICAL() do {					\
	if (panicstr == NULL && !db_active)				\
		KASSERT(curcpu()->ci_schedstate.spc_smrdepth == 0);	\
} while (0)
#else
#define SMR_ASSERT_CRITICAL()		do {} while (0)
#define SMR_ASSERT_NONCRITICAL()	do {} while (0)
#endif

#endif /* _KERNEL */

#define SMR_PTR_GET(pptr)		READ_ONCE(*pptr)

#define SMR_PTR_GET_LOCKED(pptr)	(*(pptr))

#define SMR_PTR_SET_LOCKED(pptr, val) do {				\
	membar_producer();						\
	WRITE_ONCE(*pptr, val);						\
} while (0)

/*
 * List implementations for use with safe memory reclamation.
 */

/*
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
 */

#include <sys/_null.h>

/*
 * This file defines three types of data structures: singly-linked lists,
 * lists, and tail queues.
 *
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
 * A list is headed by a single forward pointer (or an array of forward
 * pointers for a hash table header). The elements are doubly linked
 * so that an arbitrary element can be removed without a need to
 * traverse the list. New elements can be added to the list before
 * or after an existing element or at the head of the list. A list
 * may only be traversed in the forward direction.
 *
 * A tail queue is headed by a pair of pointers, one to the head of the
 * list and the other to the tail of the list. The elements are doubly
 * linked so that an arbitrary element can be removed without a need to
 * traverse the list. New elements can be added to the list before or
 * after an existing element, at the head of the list, or at the end of
 * the list. A tail queue may only be traversed in the forward direction
 * by lock-free readers.
 */

/*
 * Singly-linked List definitions.
 */
#define SMR_SLIST_HEAD(name, type)					\
struct name {								\
	struct type *smr_slh_first;	/* first element, SMR-protected */\
}

#define	SMR_SLIST_HEAD_INITIALIZER(head)				\
	{ .smr_slh_first = NULL }

#define SMR_SLIST_ENTRY(type)						\
struct {								\
	struct type *smr_sle_next;	/* next element, SMR-protected */\
}

/*
 * Singly-linked List access methods.
 */
#define	SMR_SLIST_END(head)	NULL

#define	SMR_SLIST_FIRST(head) \
	SMR_PTR_GET(&(head)->smr_slh_first)
#define	SMR_SLIST_NEXT(elm, field) \
	SMR_PTR_GET(&(elm)->field.smr_sle_next)

#define	SMR_SLIST_FIRST_LOCKED(head) \
	SMR_PTR_GET_LOCKED(&(head)->smr_slh_first)
#define	SMR_SLIST_EMPTY_LOCKED(head) \
	(SMR_SLIST_FIRST_LOCKED(head) == SMR_SLIST_END(head))
#define	SMR_SLIST_NEXT_LOCKED(elm, field) \
	SMR_PTR_GET_LOCKED(&(elm)->field.smr_sle_next)

#define	SMR_SLIST_FOREACH(var, head, field)				\
	for ((var) = SMR_SLIST_FIRST(head);				\
	    (var) != SMR_SLIST_END(head);				\
	    (var) = SMR_SLIST_NEXT(var, field))

#define	SMR_SLIST_FOREACH_LOCKED(var, head, field)			\
	for ((var) = SMR_SLIST_FIRST_LOCKED(head);			\
	    (var) != SMR_SLIST_END(head);				\
	    (var) = SMR_SLIST_NEXT_LOCKED(var, field))

#define	SMR_SLIST_FOREACH_SAFE_LOCKED(var, head, field, tvar)		\
	for ((var) = SMR_SLIST_FIRST_LOCKED(head);			\
	    (var) && ((tvar) = SMR_SLIST_NEXT_LOCKED(var, field), 1);	\
	    (var) = (tvar))

/*
 * Singly-linked List functions.
 */
#define	SMR_SLIST_INIT(head) do {					\
	(head)->smr_slh_first = SMR_SLIST_END(head);			\
} while (0)

#define	SMR_SLIST_INSERT_AFTER_LOCKED(slistelm, elm, field) do {	\
	(elm)->field.smr_sle_next = (slistelm)->field.smr_sle_next;	\
	membar_producer();						\
	(slistelm)->field.smr_sle_next = (elm);				\
} while (0)

#define	SMR_SLIST_INSERT_HEAD_LOCKED(head, elm, field) do {		\
	(elm)->field.smr_sle_next = (head)->smr_slh_first;		\
	membar_producer();						\
	(head)->smr_slh_first = (elm);					\
} while (0)

#define	SMR_SLIST_REMOVE_AFTER_LOCKED(elm, field) do {			\
	(elm)->field.smr_sle_next =					\
	    (elm)->field.smr_sle_next->field.smr_sle_next;		\
} while (0)

#define	SMR_SLIST_REMOVE_HEAD_LOCKED(head, field) do {			\
	(head)->smr_slh_first = (head)->smr_slh_first->field.smr_sle_next;\
} while (0)

#define	SMR_SLIST_REMOVE_LOCKED(head, elm, type, field) do {		\
	if ((head)->smr_slh_first == (elm)) {				\
		SMR_SLIST_REMOVE_HEAD_LOCKED((head), field);		\
	} else {							\
		struct type *curelm = (head)->smr_slh_first;		\
									\
		while (curelm->field.smr_sle_next != (elm))		\
			curelm = curelm->field.smr_sle_next;		\
		curelm->field.smr_sle_next =				\
		    curelm->field.smr_sle_next->field.smr_sle_next;	\
	}								\
	/* (elm)->field.smr_sle_next must be left intact to allow	\
	 * any concurrent readers to proceed iteration. */		\
} while (0)

/*
 * List definitions.
 */
#define	SMR_LIST_HEAD(name, type)					\
struct name {								\
	struct type *smr_lh_first;	/* first element, SMR-protected */\
}

#define	SMR_LIST_HEAD_INITIALIZER(head)					\
	{ .smr_lh_first = NULL }

#define	SMR_LIST_ENTRY(type)						\
struct {								\
	struct type *smr_le_next;	/* next element, SMR-protected */\
	struct type **smr_le_prev;	/* address of previous next element */\
}

/*
 * List access methods.
 */
#define	SMR_LIST_END(head)	NULL

#define	SMR_LIST_FIRST(head) \
	SMR_PTR_GET(&(head)->smr_lh_first)
#define	SMR_LIST_NEXT(elm, field) \
	SMR_PTR_GET(&(elm)->field.smr_le_next)

#define	SMR_LIST_FIRST_LOCKED(head)		((head)->smr_lh_first)
#define	SMR_LIST_NEXT_LOCKED(elm, field)	((elm)->field.smr_le_next)
#define	SMR_LIST_EMPTY_LOCKED(head) \
	(SMR_LIST_FIRST_LOCKED(head) == SMR_LIST_END(head))

#define	SMR_LIST_FOREACH(var, head, field)				\
	for((var) = SMR_LIST_FIRST(head);				\
	    (var)!= SMR_LIST_END(head);					\
	    (var) = SMR_LIST_NEXT(var, field))

#define	SMR_LIST_FOREACH_LOCKED(var, head, field)			\
	for((var) = SMR_LIST_FIRST_LOCKED(head);			\
	    (var)!= SMR_LIST_END(head);					\
	    (var) = SMR_LIST_NEXT_LOCKED(var, field))

#define	SMR_LIST_FOREACH_SAFE_LOCKED(var, head, field, tvar)		\
	for ((var) = SMR_LIST_FIRST_LOCKED(head);			\
	    (var) && ((tvar) = SMR_LIST_NEXT_LOCKED(var, field), 1);	\
	    (var) = (tvar))

/*
 * List functions.
 */
#define	SMR_LIST_INIT(head) do {					\
	(head)->smr_lh_first = SMR_LIST_END(head);			\
} while (0)

#define	SMR_LIST_INSERT_AFTER_LOCKED(listelm, elm, field) do {		\
	(elm)->field.smr_le_next = (listelm)->field.smr_le_next;	\
	if ((listelm)->field.smr_le_next != NULL)			\
		(listelm)->field.smr_le_next->field.smr_le_prev =	\
		    &(elm)->field.smr_le_next;				\
	(elm)->field.smr_le_prev = &(listelm)->field.smr_le_next;	\
	membar_producer();						\
	(listelm)->field.smr_le_next = (elm);				\
} while (0)

#define	SMR_LIST_INSERT_BEFORE_LOCKED(listelm, elm, field) do {		\
	(elm)->field.smr_le_prev = (listelm)->field.smr_le_prev;	\
	(elm)->field.smr_le_next = (listelm);				\
	membar_producer();						\
	*(listelm)->field.smr_le_prev = (elm);				\
	(listelm)->field.smr_le_prev = &(elm)->field.smr_le_next;	\
} while (0)

#define	SMR_LIST_INSERT_HEAD_LOCKED(head, elm, field) do {		\
	(elm)->field.smr_le_next = (head)->smr_lh_first;		\
	(elm)->field.smr_le_prev = &(head)->smr_lh_first;		\
	if ((head)->smr_lh_first != NULL)				\
		(head)->smr_lh_first->field.smr_le_prev =		\
		    &(elm)->field.smr_le_next;				\
	membar_producer();						\
	(head)->smr_lh_first = (elm);					\
} while (0)

#define	SMR_LIST_REMOVE_LOCKED(elm, field) do {				\
	if ((elm)->field.smr_le_next != NULL)				\
		(elm)->field.smr_le_next->field.smr_le_prev =		\
		    (elm)->field.smr_le_prev;				\
	*(elm)->field.smr_le_prev = (elm)->field.smr_le_next;		\
	/* (elm)->field.smr_le_next must be left intact to allow	\
	 * any concurrent readers to proceed iteration. */		\
} while (0)

/*
 * Tail queue definitions.
 */
#define	SMR_TAILQ_HEAD(name, type)					\
struct name {								\
	struct type *smr_tqh_first;	/* first element, SMR-protected */\
	struct type **smr_tqh_last;	/* last element */		\
}

#define	SMR_TAILQ_HEAD_INITIALIZER(head)				\
	{ .smr_tqh_first = NULL, .smr_tqh_last = &(head).smr_tqh_first }

#define	SMR_TAILQ_ENTRY(type)						\
struct {								\
	struct type *smr_tqe_next;	/* next element, SMR-protected */\
	struct type **smr_tqe_prev;	/* address of previous next element */\
}

/*
 * Tail queue access methods.
 */
#define	SMR_TAILQ_END(head)	NULL

#define	SMR_TAILQ_FIRST(head) \
	SMR_PTR_GET(&(head)->smr_tqh_first)
#define	SMR_TAILQ_NEXT(elm, field) \
	SMR_PTR_GET(&(elm)->field.smr_tqe_next)

#define	SMR_TAILQ_FIRST_LOCKED(head)		((head)->smr_tqh_first)
#define	SMR_TAILQ_NEXT_LOCKED(elm, field)	((elm)->field.smr_tqe_next)
#define	SMR_TAILQ_LAST_LOCKED(head, headname) \
	(*(((struct headname *)((head)->smr_tqh_last))->smr_tqh_last))
#define	SMR_TAILQ_EMPTY_LOCKED(head) \
	(SMR_TAILQ_FIRST_LOCKED(head) == SMR_TAILQ_END(head))

#define	SMR_TAILQ_FOREACH(var, head, field)				\
	for((var) = SMR_TAILQ_FIRST(head);				\
	    (var)!= SMR_TAILQ_END(head);				\
	    (var) = SMR_TAILQ_NEXT(var, field))

#define	SMR_TAILQ_FOREACH_LOCKED(var, head, field)			\
	for((var) = SMR_TAILQ_FIRST_LOCKED(head);			\
	    (var)!= SMR_TAILQ_END(head);				\
	    (var) = SMR_TAILQ_NEXT_LOCKED(var, field))

#define	SMR_TAILQ_FOREACH_SAFE_LOCKED(var, head, field, tvar)		\
	for ((var) = SMR_TAILQ_FIRST_LOCKED(head);			\
	    (var) && ((tvar) = SMR_TAILQ_NEXT_LOCKED(var, field), 1);	\
	    (var) = (tvar))

/*
 * Tail queue functions.
 */
#define	SMR_TAILQ_INIT(head) do {					\
	(head)->smr_tqh_first = SMR_TAILQ_END(head);			\
	(head)->smr_tqh_last = &(head)->smr_tqh_first;			\
} while (0)

#define	SMR_TAILQ_INSERT_AFTER_LOCKED(head, listelm, elm, field) do {	\
	(elm)->field.smr_tqe_next = (listelm)->field.smr_tqe_next;	\
	if ((listelm)->field.smr_tqe_next != NULL)			\
		(listelm)->field.smr_tqe_next->field.smr_tqe_prev =	\
		    &(elm)->field.smr_tqe_next;				\
	else								\
		(head)->smr_tqh_last = &(elm)->field.smr_tqe_next;	\
	(elm)->field.smr_tqe_prev = &(listelm)->field.smr_tqe_next;	\
	membar_producer();						\
	(listelm)->field.smr_tqe_next = (elm);				\
} while (0)

#define	SMR_TAILQ_INSERT_BEFORE_LOCKED(listelm, elm, field) do {	\
	(elm)->field.smr_tqe_prev = (listelm)->field.smr_tqe_prev;	\
	(elm)->field.smr_tqe_next = (listelm);				\
	membar_producer();						\
	*(listelm)->field.smr_tqe_prev = (elm);				\
	(listelm)->field.smr_tqe_prev = &(elm)->field.smr_tqe_next;	\
} while (0)

#define	SMR_TAILQ_INSERT_HEAD_LOCKED(head, elm, field) do {		\
	(elm)->field.smr_tqe_next = (head)->smr_tqh_first;		\
	(elm)->field.smr_tqe_prev = &(head)->smr_tqh_first;		\
	if ((head)->smr_tqh_first != NULL)				\
		(head)->smr_tqh_first->field.smr_tqe_prev =		\
		    &(elm)->field.smr_tqe_next;				\
	else								\
		(head)->smr_tqh_last = &(elm)->field.smr_tqe_next;	\
	membar_producer();						\
	(head)->smr_tqh_first = (elm);					\
} while (0)

#define	SMR_TAILQ_INSERT_TAIL_LOCKED(head, elm, field) do {		\
	(elm)->field.smr_tqe_next = NULL;				\
	(elm)->field.smr_tqe_prev = (head)->smr_tqh_last;		\
	membar_producer();						\
	*(head)->smr_tqh_last = (elm);					\
	(head)->smr_tqh_last = &(elm)->field.smr_tqe_next;		\
} while (0)

#define	SMR_TAILQ_REMOVE_LOCKED(head, elm, field) do {			\
	if ((elm)->field.smr_tqe_next != NULL)				\
		(elm)->field.smr_tqe_next->field.smr_tqe_prev =		\
		    (elm)->field.smr_tqe_prev;				\
	else								\
		(head)->smr_tqh_last = (elm)->field.smr_tqe_prev;	\
	*(elm)->field.smr_tqe_prev = (elm)->field.smr_tqe_next;		\
	/* (elm)->field.smr_tqe_next must be left intact to allow	\
	 * any concurrent readers to proceed iteration. */		\
} while (0)

#endif /* !_SYS_SMR_ */
