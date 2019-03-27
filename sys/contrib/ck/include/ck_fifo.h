/*
 * Copyright 2010-2015 Samy Al Bahra.
 * Copyright 2011 David Joseph.
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef CK_FIFO_H
#define CK_FIFO_H

#include <ck_cc.h>
#include <ck_md.h>
#include <ck_pr.h>
#include <ck_spinlock.h>
#include <ck_stddef.h>

#ifndef CK_F_FIFO_SPSC
#define CK_F_FIFO_SPSC
struct ck_fifo_spsc_entry {
	void *value;
	struct ck_fifo_spsc_entry *next;
};
typedef struct ck_fifo_spsc_entry ck_fifo_spsc_entry_t;

struct ck_fifo_spsc {
	ck_spinlock_t m_head;
	struct ck_fifo_spsc_entry *head;
	char pad[CK_MD_CACHELINE - sizeof(struct ck_fifo_spsc_entry *) - sizeof(ck_spinlock_t)];
	ck_spinlock_t m_tail;
	struct ck_fifo_spsc_entry *tail;
	struct ck_fifo_spsc_entry *head_snapshot;
	struct ck_fifo_spsc_entry *garbage;
};
typedef struct ck_fifo_spsc ck_fifo_spsc_t;

CK_CC_INLINE static bool
ck_fifo_spsc_enqueue_trylock(struct ck_fifo_spsc *fifo)
{

	return ck_spinlock_trylock(&fifo->m_tail);
}

CK_CC_INLINE static void
ck_fifo_spsc_enqueue_lock(struct ck_fifo_spsc *fifo)
{

	ck_spinlock_lock(&fifo->m_tail);
	return;
}

CK_CC_INLINE static void
ck_fifo_spsc_enqueue_unlock(struct ck_fifo_spsc *fifo)
{

	ck_spinlock_unlock(&fifo->m_tail);
	return;
}

CK_CC_INLINE static bool
ck_fifo_spsc_dequeue_trylock(struct ck_fifo_spsc *fifo)
{

	return ck_spinlock_trylock(&fifo->m_head);
}

CK_CC_INLINE static void
ck_fifo_spsc_dequeue_lock(struct ck_fifo_spsc *fifo)
{

	ck_spinlock_lock(&fifo->m_head);
	return;
}

CK_CC_INLINE static void
ck_fifo_spsc_dequeue_unlock(struct ck_fifo_spsc *fifo)
{

	ck_spinlock_unlock(&fifo->m_head);
	return;
}

CK_CC_INLINE static void
ck_fifo_spsc_init(struct ck_fifo_spsc *fifo, struct ck_fifo_spsc_entry *stub)
{

	ck_spinlock_init(&fifo->m_head);
	ck_spinlock_init(&fifo->m_tail);

	stub->next = NULL;
	fifo->head = fifo->tail = fifo->head_snapshot = fifo->garbage = stub;
	return;
}

CK_CC_INLINE static void
ck_fifo_spsc_deinit(struct ck_fifo_spsc *fifo, struct ck_fifo_spsc_entry **garbage)
{

	*garbage = fifo->head;
	fifo->head = fifo->tail = NULL;
	return;
}

CK_CC_INLINE static void
ck_fifo_spsc_enqueue(struct ck_fifo_spsc *fifo,
		     struct ck_fifo_spsc_entry *entry,
		     void *value)
{

	entry->value = value;
	entry->next = NULL;

	/* If stub->next is visible, guarantee that entry is consistent. */
	ck_pr_fence_store();
	ck_pr_store_ptr(&fifo->tail->next, entry);
	fifo->tail = entry;
	return;
}

CK_CC_INLINE static bool
ck_fifo_spsc_dequeue(struct ck_fifo_spsc *fifo, void *value)
{
	struct ck_fifo_spsc_entry *entry;

	/*
	 * The head pointer is guaranteed to always point to a stub entry.
	 * If the stub entry does not point to an entry, then the queue is
	 * empty.
	 */
	entry = ck_pr_load_ptr(&fifo->head->next);
	if (entry == NULL)
		return false;

	/* If entry is visible, guarantee store to value is visible. */
	ck_pr_store_ptr_unsafe(value, entry->value);
	ck_pr_fence_store();
	ck_pr_store_ptr(&fifo->head, entry);
	return true;
}

/*
 * Recycle a node. This technique for recycling nodes is based on
 * Dmitriy Vyukov's work.
 */
CK_CC_INLINE static struct ck_fifo_spsc_entry *
ck_fifo_spsc_recycle(struct ck_fifo_spsc *fifo)
{
	struct ck_fifo_spsc_entry *garbage;

	if (fifo->head_snapshot == fifo->garbage) {
		fifo->head_snapshot = ck_pr_load_ptr(&fifo->head);
		if (fifo->head_snapshot == fifo->garbage)
			return NULL;
	}

	garbage = fifo->garbage;
	fifo->garbage = garbage->next;
	return garbage;
}

CK_CC_INLINE static bool
ck_fifo_spsc_isempty(struct ck_fifo_spsc *fifo)
{
	struct ck_fifo_spsc_entry *head = ck_pr_load_ptr(&fifo->head);
	return ck_pr_load_ptr(&head->next) == NULL;
}

#define CK_FIFO_SPSC_ISEMPTY(f)	((f)->head->next == NULL)
#define CK_FIFO_SPSC_FIRST(f)	((f)->head->next)
#define CK_FIFO_SPSC_NEXT(m)	((m)->next)
#define CK_FIFO_SPSC_SPARE(f)	((f)->head)
#define CK_FIFO_SPSC_FOREACH(fifo, entry)			\
	for ((entry) = CK_FIFO_SPSC_FIRST(fifo);		\
	     (entry) != NULL;					\
	     (entry) = CK_FIFO_SPSC_NEXT(entry))
#define CK_FIFO_SPSC_FOREACH_SAFE(fifo, entry, T)		\
	for ((entry) = CK_FIFO_SPSC_FIRST(fifo);		\
	     (entry) != NULL && ((T) = (entry)->next, 1);	\
	     (entry) = (T))

#endif /* CK_F_FIFO_SPSC */

#ifdef CK_F_PR_CAS_PTR_2
#ifndef CK_F_FIFO_MPMC
#define CK_F_FIFO_MPMC
struct ck_fifo_mpmc_entry;
struct ck_fifo_mpmc_pointer {
	struct ck_fifo_mpmc_entry *pointer;
	char *generation CK_CC_PACKED;
} CK_CC_ALIGN(16);

struct ck_fifo_mpmc_entry {
	void *value;
	struct ck_fifo_mpmc_pointer next;
};
typedef struct ck_fifo_mpmc_entry ck_fifo_mpmc_entry_t;

struct ck_fifo_mpmc {
	struct ck_fifo_mpmc_pointer head;
	char pad[CK_MD_CACHELINE - sizeof(struct ck_fifo_mpmc_pointer)];
	struct ck_fifo_mpmc_pointer tail;
};
typedef struct ck_fifo_mpmc ck_fifo_mpmc_t;

CK_CC_INLINE static void
ck_fifo_mpmc_init(struct ck_fifo_mpmc *fifo, struct ck_fifo_mpmc_entry *stub)
{

	stub->next.pointer = NULL;
	stub->next.generation = NULL;
	fifo->head.pointer = fifo->tail.pointer = stub;
	fifo->head.generation = fifo->tail.generation = NULL;
	return;
}

CK_CC_INLINE static void
ck_fifo_mpmc_deinit(struct ck_fifo_mpmc *fifo, struct ck_fifo_mpmc_entry **garbage)
{

	*garbage = fifo->head.pointer;
	fifo->head.pointer = fifo->tail.pointer = NULL;
	return;
}

CK_CC_INLINE static void
ck_fifo_mpmc_enqueue(struct ck_fifo_mpmc *fifo,
		     struct ck_fifo_mpmc_entry *entry,
		     void *value)
{
	struct ck_fifo_mpmc_pointer tail, next, update;

	/*
	 * Prepare the upcoming node and make sure to commit the updates
	 * before publishing.
	 */
	entry->value = value;
	entry->next.pointer = NULL;
	entry->next.generation = 0;
	ck_pr_fence_store_atomic();

	for (;;) {
		tail.generation = ck_pr_load_ptr(&fifo->tail.generation);
		ck_pr_fence_load();
		tail.pointer = ck_pr_load_ptr(&fifo->tail.pointer);
		next.generation = ck_pr_load_ptr(&tail.pointer->next.generation);
		ck_pr_fence_load();
		next.pointer = ck_pr_load_ptr(&tail.pointer->next.pointer);

		if (ck_pr_load_ptr(&fifo->tail.generation) != tail.generation)
			continue;

		if (next.pointer != NULL) {
			/*
			 * If the tail pointer has an entry following it then
			 * it needs to be forwarded to the next entry. This
			 * helps us guarantee we are always operating on the
			 * last entry.
			 */
			update.pointer = next.pointer;
			update.generation = tail.generation + 1;
			ck_pr_cas_ptr_2(&fifo->tail, &tail, &update);
		} else {
			/*
			 * Attempt to commit new entry to the end of the
			 * current tail.
			 */
			update.pointer = entry;
			update.generation = next.generation + 1;
			if (ck_pr_cas_ptr_2(&tail.pointer->next, &next, &update) == true)
				break;
		}
	}

	ck_pr_fence_atomic();

	/* After a successful insert, forward the tail to the new entry. */
	update.generation = tail.generation + 1;
	ck_pr_cas_ptr_2(&fifo->tail, &tail, &update);
	return;
}

CK_CC_INLINE static bool
ck_fifo_mpmc_tryenqueue(struct ck_fifo_mpmc *fifo,
		        struct ck_fifo_mpmc_entry *entry,
		        void *value)
{
	struct ck_fifo_mpmc_pointer tail, next, update;

	entry->value = value;
	entry->next.pointer = NULL;
	entry->next.generation = 0;

	ck_pr_fence_store_atomic();

	tail.generation = ck_pr_load_ptr(&fifo->tail.generation);
	ck_pr_fence_load();
	tail.pointer = ck_pr_load_ptr(&fifo->tail.pointer);
	next.generation = ck_pr_load_ptr(&tail.pointer->next.generation);
	ck_pr_fence_load();
	next.pointer = ck_pr_load_ptr(&tail.pointer->next.pointer);

	if (ck_pr_load_ptr(&fifo->tail.generation) != tail.generation)
		return false;

	if (next.pointer != NULL) {
		/*
		 * If the tail pointer has an entry following it then
		 * it needs to be forwarded to the next entry. This
		 * helps us guarantee we are always operating on the
		 * last entry.
		 */
		update.pointer = next.pointer;
		update.generation = tail.generation + 1;
		ck_pr_cas_ptr_2(&fifo->tail, &tail, &update);
		return false;
	} else {
		/*
		 * Attempt to commit new entry to the end of the
		 * current tail.
		 */
		update.pointer = entry;
		update.generation = next.generation + 1;
		if (ck_pr_cas_ptr_2(&tail.pointer->next, &next, &update) == false)
			return false;
	}

	ck_pr_fence_atomic();

	/* After a successful insert, forward the tail to the new entry. */
	update.generation = tail.generation + 1;
	ck_pr_cas_ptr_2(&fifo->tail, &tail, &update);
	return true;
}

CK_CC_INLINE static bool
ck_fifo_mpmc_dequeue(struct ck_fifo_mpmc *fifo,
		     void *value,
		     struct ck_fifo_mpmc_entry **garbage)
{
	struct ck_fifo_mpmc_pointer head, tail, next, update;

	for (;;) {
		head.generation = ck_pr_load_ptr(&fifo->head.generation);
		ck_pr_fence_load();
		head.pointer = ck_pr_load_ptr(&fifo->head.pointer);
		tail.generation = ck_pr_load_ptr(&fifo->tail.generation);
		ck_pr_fence_load();
		tail.pointer = ck_pr_load_ptr(&fifo->tail.pointer);

		next.generation = ck_pr_load_ptr(&head.pointer->next.generation);
		ck_pr_fence_load();
		next.pointer = ck_pr_load_ptr(&head.pointer->next.pointer);

		update.pointer = next.pointer;
		if (head.pointer == tail.pointer) {
			/*
			 * The head is guaranteed to always point at a stub
			 * entry. If the stub entry has no references then the
			 * queue is empty.
			 */
			if (next.pointer == NULL)
				return false;

			/* Forward the tail pointer if necessary. */
			update.generation = tail.generation + 1;
			ck_pr_cas_ptr_2(&fifo->tail, &tail, &update);
		} else {
			/*
			 * It is possible for head snapshot to have been
			 * re-used. Avoid deferencing during enqueue
			 * re-use.
			 */
			if (next.pointer == NULL)
				continue;

			/* Save value before commit. */
			*(void **)value = ck_pr_load_ptr(&next.pointer->value);

			/* Forward the head pointer to the next entry. */
			update.generation = head.generation + 1;
			if (ck_pr_cas_ptr_2(&fifo->head, &head, &update) == true)
				break;
		}
	}

	*garbage = head.pointer;
	return true;
}

CK_CC_INLINE static bool
ck_fifo_mpmc_trydequeue(struct ck_fifo_mpmc *fifo,
			void *value,
			struct ck_fifo_mpmc_entry **garbage)
{
	struct ck_fifo_mpmc_pointer head, tail, next, update;

	head.generation = ck_pr_load_ptr(&fifo->head.generation);
	ck_pr_fence_load();
	head.pointer = ck_pr_load_ptr(&fifo->head.pointer);

	tail.generation = ck_pr_load_ptr(&fifo->tail.generation);
	ck_pr_fence_load();
	tail.pointer = ck_pr_load_ptr(&fifo->tail.pointer);

	next.generation = ck_pr_load_ptr(&head.pointer->next.generation);
	ck_pr_fence_load();
	next.pointer = ck_pr_load_ptr(&head.pointer->next.pointer);

	update.pointer = next.pointer;
	if (head.pointer == tail.pointer) {
		/*
		 * The head is guaranteed to always point at a stub
		 * entry. If the stub entry has no references then the
		 * queue is empty.
		 */
		if (next.pointer == NULL)
			return false;

		/* Forward the tail pointer if necessary. */
		update.generation = tail.generation + 1;
		ck_pr_cas_ptr_2(&fifo->tail, &tail, &update);
		return false;
	} else {
		/*
		 * It is possible for head snapshot to have been
		 * re-used. Avoid deferencing during enqueue.
		 */
		if (next.pointer == NULL)
			return false;

		/* Save value before commit. */
		*(void **)value = ck_pr_load_ptr(&next.pointer->value);

		/* Forward the head pointer to the next entry. */
		update.generation = head.generation + 1;
		if (ck_pr_cas_ptr_2(&fifo->head, &head, &update) == false)
			return false;
	}

	*garbage = head.pointer;
	return true;
}

#define CK_FIFO_MPMC_ISEMPTY(f)	((f)->head.pointer->next.pointer == NULL)
#define CK_FIFO_MPMC_FIRST(f)	((f)->head.pointer->next.pointer)
#define CK_FIFO_MPMC_NEXT(m)	((m)->next.pointer)
#define CK_FIFO_MPMC_FOREACH(fifo, entry)				\
	for ((entry) = CK_FIFO_MPMC_FIRST(fifo);			\
	     (entry) != NULL;						\
	     (entry) = CK_FIFO_MPMC_NEXT(entry))
#define CK_FIFO_MPMC_FOREACH_SAFE(fifo, entry, T)			\
	for ((entry) = CK_FIFO_MPMC_FIRST(fifo);			\
	     (entry) != NULL && ((T) = (entry)->next.pointer, 1);	\
	     (entry) = (T))

#endif /* CK_F_FIFO_MPMC */
#endif /* CK_F_PR_CAS_PTR_2 */

#endif /* CK_FIFO_H */
