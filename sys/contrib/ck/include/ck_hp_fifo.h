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

#ifndef CK_HP_FIFO_H
#define CK_HP_FIFO_H

#include <ck_cc.h>
#include <ck_hp.h>
#include <ck_pr.h>
#include <ck_stddef.h>

#define CK_HP_FIFO_SLOTS_COUNT (2)
#define CK_HP_FIFO_SLOTS_SIZE  (sizeof(void *) * CK_HP_FIFO_SLOTS_COUNT)

/*
 * Though it is possible to embed the data structure, measurements need
 * to be made for the cost of this. If we were to embed the hazard pointer
 * state into the data structure, this means every deferred reclamation
 * will also include a cache invalidation when linking into the hazard pointer
 * pending queue. This may lead to terrible cache line bouncing.
 */
struct ck_hp_fifo_entry {
	void *value;
	ck_hp_hazard_t hazard;
	struct ck_hp_fifo_entry *next;
};
typedef struct ck_hp_fifo_entry ck_hp_fifo_entry_t;

struct ck_hp_fifo {
	struct ck_hp_fifo_entry *head;
	struct ck_hp_fifo_entry *tail;
};
typedef struct ck_hp_fifo ck_hp_fifo_t;

CK_CC_INLINE static void
ck_hp_fifo_init(struct ck_hp_fifo *fifo, struct ck_hp_fifo_entry *stub)
{

	fifo->head = fifo->tail = stub;
	stub->next = NULL;
	return;
}

CK_CC_INLINE static void
ck_hp_fifo_deinit(struct ck_hp_fifo *fifo, struct ck_hp_fifo_entry **stub)
{

	*stub = fifo->head;
	fifo->head = fifo->tail = NULL;
	return;
}

CK_CC_INLINE static void
ck_hp_fifo_enqueue_mpmc(ck_hp_record_t *record,
			struct ck_hp_fifo *fifo,
			struct ck_hp_fifo_entry *entry,
			void *value)
{
	struct ck_hp_fifo_entry *tail, *next;

	entry->value = value;
	entry->next = NULL;
	ck_pr_fence_store_atomic();

	for (;;) {
		tail = ck_pr_load_ptr(&fifo->tail);
		ck_hp_set_fence(record, 0, tail);
		if (tail != ck_pr_load_ptr(&fifo->tail))
			continue;

		next = ck_pr_load_ptr(&tail->next);
		if (next != NULL) {
			ck_pr_cas_ptr(&fifo->tail, tail, next);
			continue;
		} else if (ck_pr_cas_ptr(&fifo->tail->next, next, entry) == true)
			break;
	}

	ck_pr_fence_atomic();
	ck_pr_cas_ptr(&fifo->tail, tail, entry);
	return;
}

CK_CC_INLINE static bool
ck_hp_fifo_tryenqueue_mpmc(ck_hp_record_t *record,
			   struct ck_hp_fifo *fifo,
			   struct ck_hp_fifo_entry *entry,
			   void *value)
{
	struct ck_hp_fifo_entry *tail, *next;

	entry->value = value;
	entry->next = NULL;
	ck_pr_fence_store_atomic();

	tail = ck_pr_load_ptr(&fifo->tail);
	ck_hp_set_fence(record, 0, tail);
	if (tail != ck_pr_load_ptr(&fifo->tail))
		return false;

	next = ck_pr_load_ptr(&tail->next);
	if (next != NULL) {
		ck_pr_cas_ptr(&fifo->tail, tail, next);
		return false;
	} else if (ck_pr_cas_ptr(&fifo->tail->next, next, entry) == false)
		return false;

	ck_pr_fence_atomic();
	ck_pr_cas_ptr(&fifo->tail, tail, entry);
	return true;
}

CK_CC_INLINE static struct ck_hp_fifo_entry *
ck_hp_fifo_dequeue_mpmc(ck_hp_record_t *record,
			struct ck_hp_fifo *fifo,
			void *value)
{
	struct ck_hp_fifo_entry *head, *tail, *next;

	for (;;) {
		head = ck_pr_load_ptr(&fifo->head);
		ck_pr_fence_load();
		tail = ck_pr_load_ptr(&fifo->tail);
		ck_hp_set_fence(record, 0, head);
		if (head != ck_pr_load_ptr(&fifo->head))
			continue;

		next = ck_pr_load_ptr(&head->next);
		ck_hp_set_fence(record, 1, next);
		if (head != ck_pr_load_ptr(&fifo->head))
			continue;

		if (head == tail) {
			if (next == NULL)
				return NULL;

			ck_pr_cas_ptr(&fifo->tail, tail, next);
			continue;
		} else if (ck_pr_cas_ptr(&fifo->head, head, next) == true)
			break;
	}

	ck_pr_store_ptr_unsafe(value, next->value);
	return head;
}

CK_CC_INLINE static struct ck_hp_fifo_entry *
ck_hp_fifo_trydequeue_mpmc(ck_hp_record_t *record,
			   struct ck_hp_fifo *fifo,
			   void *value)
{
	struct ck_hp_fifo_entry *head, *tail, *next;

	head = ck_pr_load_ptr(&fifo->head);
	ck_pr_fence_load();
	tail = ck_pr_load_ptr(&fifo->tail);
	ck_hp_set_fence(record, 0, head);
	if (head != ck_pr_load_ptr(&fifo->head))
		return NULL;

	next = ck_pr_load_ptr(&head->next);
	ck_hp_set_fence(record, 1, next);
	if (head != ck_pr_load_ptr(&fifo->head))
		return NULL;

	if (head == tail) {
		if (next == NULL)
			return NULL;

		ck_pr_cas_ptr(&fifo->tail, tail, next);
		return NULL;
	} else if (ck_pr_cas_ptr(&fifo->head, head, next) == false)
		return NULL;

	ck_pr_store_ptr_unsafe(value, next->value);
	return head;
}

#define CK_HP_FIFO_ISEMPTY(f) ((f)->head->next == NULL)
#define CK_HP_FIFO_FIRST(f)   ((f)->head->next)
#define CK_HP_FIFO_NEXT(m)    ((m)->next)
#define CK_HP_FIFO_FOREACH(fifo, entry)                       	\
        for ((entry) = CK_HP_FIFO_FIRST(fifo);                	\
             (entry) != NULL;                                   \
             (entry) = CK_HP_FIFO_NEXT(entry))
#define CK_HP_FIFO_FOREACH_SAFE(fifo, entry, T)			\
        for ((entry) = CK_HP_FIFO_FIRST(fifo);			\
             (entry) != NULL && ((T) = (entry)->next, 1);	\
             (entry) = (T))

#endif /* CK_HP_FIFO_H */
