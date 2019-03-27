/*
 * Copyright 2009-2015 Samy Al Bahra.
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

#ifndef CK_STACK_H
#define CK_STACK_H

#include <ck_cc.h>
#include <ck_pr.h>
#include <ck_stdbool.h>
#include <ck_stddef.h>

struct ck_stack_entry {
	struct ck_stack_entry *next;
};
typedef struct ck_stack_entry ck_stack_entry_t;

struct ck_stack {
	struct ck_stack_entry *head;
	char *generation CK_CC_PACKED;
} CK_CC_ALIASED;
typedef struct ck_stack ck_stack_t;

#define CK_STACK_INITIALIZER { NULL, NULL }

#ifndef CK_F_STACK_PUSH_UPMC
#define CK_F_STACK_PUSH_UPMC
/*
 * Stack producer operation safe for multiple unique producers and multiple consumers.
 */
CK_CC_INLINE static void
ck_stack_push_upmc(struct ck_stack *target, struct ck_stack_entry *entry)
{
	struct ck_stack_entry *stack;

	stack = ck_pr_load_ptr(&target->head);
	entry->next = stack;
	ck_pr_fence_store();

	while (ck_pr_cas_ptr_value(&target->head, stack, entry, &stack) == false) {
		entry->next = stack;
		ck_pr_fence_store();
	}

	return;
}
#endif /* CK_F_STACK_PUSH_UPMC */

#ifndef CK_F_STACK_TRYPUSH_UPMC
#define CK_F_STACK_TRYPUSH_UPMC
/*
 * Stack producer operation for multiple unique producers and multiple consumers.
 * Returns true on success and false on failure.
 */
CK_CC_INLINE static bool
ck_stack_trypush_upmc(struct ck_stack *target, struct ck_stack_entry *entry)
{
	struct ck_stack_entry *stack;

	stack = ck_pr_load_ptr(&target->head);
	entry->next = stack;
	ck_pr_fence_store();

	return ck_pr_cas_ptr(&target->head, stack, entry);
}
#endif /* CK_F_STACK_TRYPUSH_UPMC */

#ifndef CK_F_STACK_POP_UPMC
#define CK_F_STACK_POP_UPMC
/*
 * Stack consumer operation safe for multiple unique producers and multiple consumers.
 */
CK_CC_INLINE static struct ck_stack_entry *
ck_stack_pop_upmc(struct ck_stack *target)
{
	struct ck_stack_entry *entry, *next;

	entry = ck_pr_load_ptr(&target->head);
	if (entry == NULL)
		return NULL;

	ck_pr_fence_load();
	next = entry->next;
	while (ck_pr_cas_ptr_value(&target->head, entry, next, &entry) == false) {
		if (entry == NULL)
			break;

		ck_pr_fence_load();
		next = entry->next;
	}

	return entry;
}
#endif

#ifndef CK_F_STACK_TRYPOP_UPMC
#define CK_F_STACK_TRYPOP_UPMC
/*
 * Stack production operation for multiple unique producers and multiple consumers.
 * Returns true on success and false on failure. The value pointed to by the second
 * argument is set to a valid ck_stack_entry_t reference if true is returned. If
 * false is returned, then the value pointed to by the second argument is undefined.
 */
CK_CC_INLINE static bool
ck_stack_trypop_upmc(struct ck_stack *target, struct ck_stack_entry **r)
{
	struct ck_stack_entry *entry;

	entry = ck_pr_load_ptr(&target->head);
	if (entry == NULL)
		return false;

	ck_pr_fence_load();
	if (ck_pr_cas_ptr(&target->head, entry, entry->next) == true) {
		*r = entry;
		return true;
	}

	return false;
}
#endif /* CK_F_STACK_TRYPOP_UPMC */

#ifndef CK_F_STACK_BATCH_POP_UPMC
#define CK_F_STACK_BATCH_POP_UPMC
/*
 * Pop all items off the stack.
 */
CK_CC_INLINE static struct ck_stack_entry *
ck_stack_batch_pop_upmc(struct ck_stack *target)
{
	struct ck_stack_entry *entry;

	entry = ck_pr_fas_ptr(&target->head, NULL);
	ck_pr_fence_load();
	return entry;
}
#endif /* CK_F_STACK_BATCH_POP_UPMC */

#ifndef CK_F_STACK_PUSH_MPMC
#define CK_F_STACK_PUSH_MPMC
/*
 * Stack producer operation safe for multiple producers and multiple consumers.
 */
CK_CC_INLINE static void
ck_stack_push_mpmc(struct ck_stack *target, struct ck_stack_entry *entry)
{

	ck_stack_push_upmc(target, entry);
	return;
}
#endif /* CK_F_STACK_PUSH_MPMC */

#ifndef CK_F_STACK_TRYPUSH_MPMC
#define CK_F_STACK_TRYPUSH_MPMC
/*
 * Stack producer operation safe for multiple producers and multiple consumers.
 */
CK_CC_INLINE static bool
ck_stack_trypush_mpmc(struct ck_stack *target, struct ck_stack_entry *entry)
{

	return ck_stack_trypush_upmc(target, entry);
}
#endif /* CK_F_STACK_TRYPUSH_MPMC */

#ifdef CK_F_PR_CAS_PTR_2_VALUE
#ifndef CK_F_STACK_POP_MPMC
#define CK_F_STACK_POP_MPMC
/*
 * Stack consumer operation safe for multiple producers and multiple consumers.
 */
CK_CC_INLINE static struct ck_stack_entry *
ck_stack_pop_mpmc(struct ck_stack *target)
{
	struct ck_stack original, update;

	original.generation = ck_pr_load_ptr(&target->generation);
	ck_pr_fence_load();
	original.head = ck_pr_load_ptr(&target->head);
	if (original.head == NULL)
		return NULL;

	/* Order with respect to next pointer. */
	ck_pr_fence_load();

	update.generation = original.generation + 1;
	update.head = original.head->next;

	while (ck_pr_cas_ptr_2_value(target, &original, &update, &original) == false) {
		if (original.head == NULL)
			return NULL;

		update.generation = original.generation + 1;

		/* Order with respect to next pointer. */
		ck_pr_fence_load();
		update.head = original.head->next;
	}

	return original.head;
}
#endif /* CK_F_STACK_POP_MPMC */

#ifndef CK_F_STACK_TRYPOP_MPMC
#define CK_F_STACK_TRYPOP_MPMC
CK_CC_INLINE static bool
ck_stack_trypop_mpmc(struct ck_stack *target, struct ck_stack_entry **r)
{
	struct ck_stack original, update;

	original.generation = ck_pr_load_ptr(&target->generation);
	ck_pr_fence_load();
	original.head = ck_pr_load_ptr(&target->head);
	if (original.head == NULL)
		return false;

	update.generation = original.generation + 1;
	ck_pr_fence_load();
	update.head = original.head->next;

	if (ck_pr_cas_ptr_2_value(target, &original, &update, &original) == true) {
		*r = original.head;
		return true;
	}

	return false;
}
#endif /* CK_F_STACK_TRYPOP_MPMC */
#endif /* CK_F_PR_CAS_PTR_2_VALUE */

#ifndef CK_F_STACK_BATCH_POP_MPMC
#define CK_F_STACK_BATCH_POP_MPMC
/*
 * This is equivalent to the UP/MC version as NULL does not need a
 * a generation count.
 */
CK_CC_INLINE static struct ck_stack_entry *
ck_stack_batch_pop_mpmc(struct ck_stack *target)
{

	return ck_stack_batch_pop_upmc(target);
}
#endif /* CK_F_STACK_BATCH_POP_MPMC */

#ifndef CK_F_STACK_PUSH_MPNC
#define CK_F_STACK_PUSH_MPNC
/*
 * Stack producer operation safe with no concurrent consumers.
 */
CK_CC_INLINE static void
ck_stack_push_mpnc(struct ck_stack *target, struct ck_stack_entry *entry)
{
	struct ck_stack_entry *stack;

	entry->next = NULL;
	ck_pr_fence_store_atomic();
	stack = ck_pr_fas_ptr(&target->head, entry);
	ck_pr_store_ptr(&entry->next, stack);
	ck_pr_fence_store();

	return;
}
#endif /* CK_F_STACK_PUSH_MPNC */

/*
 * Stack producer operation for single producer and no concurrent consumers.
 */
CK_CC_INLINE static void
ck_stack_push_spnc(struct ck_stack *target, struct ck_stack_entry *entry)
{

	entry->next = target->head;
	target->head = entry;
	return;
}

/*
 * Stack consumer operation for no concurrent producers and single consumer.
 */
CK_CC_INLINE static struct ck_stack_entry *
ck_stack_pop_npsc(struct ck_stack *target)
{
	struct ck_stack_entry *n;

	if (target->head == NULL)
		return NULL;

	n = target->head;
	target->head = n->next;

	return n;
}

/*
 * Pop all items off a stack.
 */
CK_CC_INLINE static struct ck_stack_entry *
ck_stack_batch_pop_npsc(struct ck_stack *target)
{
	struct ck_stack_entry *n;

	n = target->head;
	target->head = NULL;

	return n;
}

/*
 * Stack initialization function. Guarantees initialization across processors.
 */
CK_CC_INLINE static void
ck_stack_init(struct ck_stack *stack)
{

	stack->head = NULL;
	stack->generation = NULL;
	return;
}

/* Defines a container_of functions for */
#define CK_STACK_CONTAINER(T, M, N) CK_CC_CONTAINER(ck_stack_entry_t, T, M, N)

#define CK_STACK_ISEMPTY(m) ((m)->head == NULL)
#define CK_STACK_FIRST(s)   ((s)->head)
#define CK_STACK_NEXT(m)    ((m)->next)
#define CK_STACK_FOREACH(stack, entry)				\
	for ((entry) = CK_STACK_FIRST(stack);			\
	     (entry) != NULL;					\
	     (entry) = CK_STACK_NEXT(entry))
#define CK_STACK_FOREACH_SAFE(stack, entry, T)			\
	for ((entry) = CK_STACK_FIRST(stack);			\
	     (entry) != NULL && ((T) = (entry)->next, 1);	\
	     (entry) = (T))

#endif /* CK_STACK_H */
