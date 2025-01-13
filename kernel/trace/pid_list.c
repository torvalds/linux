// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 VMware Inc, Steven Rostedt <rostedt@goodmis.org>
 */
#include <linux/spinlock.h>
#include <linux/irq_work.h>
#include <linux/slab.h>
#include "trace.h"

/* See pid_list.h for details */

static inline union lower_chunk *get_lower_chunk(struct trace_pid_list *pid_list)
{
	union lower_chunk *chunk;

	lockdep_assert_held(&pid_list->lock);

	if (!pid_list->lower_list)
		return NULL;

	chunk = pid_list->lower_list;
	pid_list->lower_list = chunk->next;
	pid_list->free_lower_chunks--;
	WARN_ON_ONCE(pid_list->free_lower_chunks < 0);
	chunk->next = NULL;
	/*
	 * If a refill needs to happen, it can not happen here
	 * as the scheduler run queue locks are held.
	 */
	if (pid_list->free_lower_chunks <= CHUNK_REALLOC)
		irq_work_queue(&pid_list->refill_irqwork);

	return chunk;
}

static inline union upper_chunk *get_upper_chunk(struct trace_pid_list *pid_list)
{
	union upper_chunk *chunk;

	lockdep_assert_held(&pid_list->lock);

	if (!pid_list->upper_list)
		return NULL;

	chunk = pid_list->upper_list;
	pid_list->upper_list = chunk->next;
	pid_list->free_upper_chunks--;
	WARN_ON_ONCE(pid_list->free_upper_chunks < 0);
	chunk->next = NULL;
	/*
	 * If a refill needs to happen, it can not happen here
	 * as the scheduler run queue locks are held.
	 */
	if (pid_list->free_upper_chunks <= CHUNK_REALLOC)
		irq_work_queue(&pid_list->refill_irqwork);

	return chunk;
}

static inline void put_lower_chunk(struct trace_pid_list *pid_list,
				   union lower_chunk *chunk)
{
	lockdep_assert_held(&pid_list->lock);

	chunk->next = pid_list->lower_list;
	pid_list->lower_list = chunk;
	pid_list->free_lower_chunks++;
}

static inline void put_upper_chunk(struct trace_pid_list *pid_list,
				   union upper_chunk *chunk)
{
	lockdep_assert_held(&pid_list->lock);

	chunk->next = pid_list->upper_list;
	pid_list->upper_list = chunk;
	pid_list->free_upper_chunks++;
}

static inline bool upper_empty(union upper_chunk *chunk)
{
	/*
	 * If chunk->data has no lower chunks, it will be the same
	 * as a zeroed bitmask. Use find_first_bit() to test it
	 * and if it doesn't find any bits set, then the array
	 * is empty.
	 */
	int bit = find_first_bit((unsigned long *)chunk->data,
				 sizeof(chunk->data) * 8);
	return bit >= sizeof(chunk->data) * 8;
}

static inline int pid_split(unsigned int pid, unsigned int *upper1,
			     unsigned int *upper2, unsigned int *lower)
{
	/* MAX_PID should cover all pids */
	BUILD_BUG_ON(MAX_PID < PID_MAX_LIMIT);

	/* In case a bad pid is passed in, then fail */
	if (unlikely(pid >= MAX_PID))
		return -1;

	*upper1 = (pid >> UPPER1_SHIFT) & UPPER_MASK;
	*upper2 = (pid >> UPPER2_SHIFT) & UPPER_MASK;
	*lower = pid & LOWER_MASK;

	return 0;
}

static inline unsigned int pid_join(unsigned int upper1,
				    unsigned int upper2, unsigned int lower)
{
	return ((upper1 & UPPER_MASK) << UPPER1_SHIFT) |
		((upper2 & UPPER_MASK) << UPPER2_SHIFT) |
		(lower & LOWER_MASK);
}

/**
 * trace_pid_list_is_set - test if the pid is set in the list
 * @pid_list: The pid list to test
 * @pid: The pid to see if set in the list.
 *
 * Tests if @pid is set in the @pid_list. This is usually called
 * from the scheduler when a task is scheduled. Its pid is checked
 * if it should be traced or not.
 *
 * Return true if the pid is in the list, false otherwise.
 */
bool trace_pid_list_is_set(struct trace_pid_list *pid_list, unsigned int pid)
{
	union upper_chunk *upper_chunk;
	union lower_chunk *lower_chunk;
	unsigned long flags;
	unsigned int upper1;
	unsigned int upper2;
	unsigned int lower;
	bool ret = false;

	if (!pid_list)
		return false;

	if (pid_split(pid, &upper1, &upper2, &lower) < 0)
		return false;

	raw_spin_lock_irqsave(&pid_list->lock, flags);
	upper_chunk = pid_list->upper[upper1];
	if (upper_chunk) {
		lower_chunk = upper_chunk->data[upper2];
		if (lower_chunk)
			ret = test_bit(lower, lower_chunk->data);
	}
	raw_spin_unlock_irqrestore(&pid_list->lock, flags);

	return ret;
}

/**
 * trace_pid_list_set - add a pid to the list
 * @pid_list: The pid list to add the @pid to.
 * @pid: The pid to add.
 *
 * Adds @pid to @pid_list. This is usually done explicitly by a user
 * adding a task to be traced, or indirectly by the fork function
 * when children should be traced and a task's pid is in the list.
 *
 * Return 0 on success, negative otherwise.
 */
int trace_pid_list_set(struct trace_pid_list *pid_list, unsigned int pid)
{
	union upper_chunk *upper_chunk;
	union lower_chunk *lower_chunk;
	unsigned long flags;
	unsigned int upper1;
	unsigned int upper2;
	unsigned int lower;
	int ret;

	if (!pid_list)
		return -ENODEV;

	if (pid_split(pid, &upper1, &upper2, &lower) < 0)
		return -EINVAL;

	raw_spin_lock_irqsave(&pid_list->lock, flags);
	upper_chunk = pid_list->upper[upper1];
	if (!upper_chunk) {
		upper_chunk = get_upper_chunk(pid_list);
		if (!upper_chunk) {
			ret = -ENOMEM;
			goto out;
		}
		pid_list->upper[upper1] = upper_chunk;
	}
	lower_chunk = upper_chunk->data[upper2];
	if (!lower_chunk) {
		lower_chunk = get_lower_chunk(pid_list);
		if (!lower_chunk) {
			ret = -ENOMEM;
			goto out;
		}
		upper_chunk->data[upper2] = lower_chunk;
	}
	set_bit(lower, lower_chunk->data);
	ret = 0;
 out:
	raw_spin_unlock_irqrestore(&pid_list->lock, flags);
	return ret;
}

/**
 * trace_pid_list_clear - remove a pid from the list
 * @pid_list: The pid list to remove the @pid from.
 * @pid: The pid to remove.
 *
 * Removes @pid from @pid_list. This is usually done explicitly by a user
 * removing tasks from tracing, or indirectly by the exit function
 * when a task that is set to be traced exits.
 *
 * Return 0 on success, negative otherwise.
 */
int trace_pid_list_clear(struct trace_pid_list *pid_list, unsigned int pid)
{
	union upper_chunk *upper_chunk;
	union lower_chunk *lower_chunk;
	unsigned long flags;
	unsigned int upper1;
	unsigned int upper2;
	unsigned int lower;

	if (!pid_list)
		return -ENODEV;

	if (pid_split(pid, &upper1, &upper2, &lower) < 0)
		return -EINVAL;

	raw_spin_lock_irqsave(&pid_list->lock, flags);
	upper_chunk = pid_list->upper[upper1];
	if (!upper_chunk)
		goto out;

	lower_chunk = upper_chunk->data[upper2];
	if (!lower_chunk)
		goto out;

	clear_bit(lower, lower_chunk->data);

	/* if there's no more bits set, add it to the free list */
	if (find_first_bit(lower_chunk->data, LOWER_MAX) >= LOWER_MAX) {
		put_lower_chunk(pid_list, lower_chunk);
		upper_chunk->data[upper2] = NULL;
		if (upper_empty(upper_chunk)) {
			put_upper_chunk(pid_list, upper_chunk);
			pid_list->upper[upper1] = NULL;
		}
	}
 out:
	raw_spin_unlock_irqrestore(&pid_list->lock, flags);
	return 0;
}

/**
 * trace_pid_list_next - return the next pid in the list
 * @pid_list: The pid list to examine.
 * @pid: The pid to start from
 * @next: The pointer to place the pid that is set starting from @pid.
 *
 * Looks for the next consecutive pid that is in @pid_list starting
 * at the pid specified by @pid. If one is set (including @pid), then
 * that pid is placed into @next.
 *
 * Return 0 when a pid is found, -1 if there are no more pids included.
 */
int trace_pid_list_next(struct trace_pid_list *pid_list, unsigned int pid,
			unsigned int *next)
{
	union upper_chunk *upper_chunk;
	union lower_chunk *lower_chunk;
	unsigned long flags;
	unsigned int upper1;
	unsigned int upper2;
	unsigned int lower;

	if (!pid_list)
		return -ENODEV;

	if (pid_split(pid, &upper1, &upper2, &lower) < 0)
		return -EINVAL;

	raw_spin_lock_irqsave(&pid_list->lock, flags);
	for (; upper1 <= UPPER_MASK; upper1++, upper2 = 0) {
		upper_chunk = pid_list->upper[upper1];

		if (!upper_chunk)
			continue;

		for (; upper2 <= UPPER_MASK; upper2++, lower = 0) {
			lower_chunk = upper_chunk->data[upper2];
			if (!lower_chunk)
				continue;

			lower = find_next_bit(lower_chunk->data, LOWER_MAX,
					    lower);
			if (lower < LOWER_MAX)
				goto found;
		}
	}

 found:
	raw_spin_unlock_irqrestore(&pid_list->lock, flags);
	if (upper1 > UPPER_MASK)
		return -1;

	*next = pid_join(upper1, upper2, lower);
	return 0;
}

/**
 * trace_pid_list_first - return the first pid in the list
 * @pid_list: The pid list to examine.
 * @pid: The pointer to place the pid first found pid that is set.
 *
 * Looks for the first pid that is set in @pid_list, and places it
 * into @pid if found.
 *
 * Return 0 when a pid is found, -1 if there are no pids set.
 */
int trace_pid_list_first(struct trace_pid_list *pid_list, unsigned int *pid)
{
	return trace_pid_list_next(pid_list, 0, pid);
}

static void pid_list_refill_irq(struct irq_work *iwork)
{
	struct trace_pid_list *pid_list = container_of(iwork, struct trace_pid_list,
						       refill_irqwork);
	union upper_chunk *upper = NULL;
	union lower_chunk *lower = NULL;
	union upper_chunk **upper_next = &upper;
	union lower_chunk **lower_next = &lower;
	int upper_count;
	int lower_count;
	int ucnt = 0;
	int lcnt = 0;

 again:
	raw_spin_lock(&pid_list->lock);
	upper_count = CHUNK_ALLOC - pid_list->free_upper_chunks;
	lower_count = CHUNK_ALLOC - pid_list->free_lower_chunks;
	raw_spin_unlock(&pid_list->lock);

	if (upper_count <= 0 && lower_count <= 0)
		return;

	while (upper_count-- > 0) {
		union upper_chunk *chunk;

		chunk = kzalloc(sizeof(*chunk), GFP_NOWAIT);
		if (!chunk)
			break;
		*upper_next = chunk;
		upper_next = &chunk->next;
		ucnt++;
	}

	while (lower_count-- > 0) {
		union lower_chunk *chunk;

		chunk = kzalloc(sizeof(*chunk), GFP_NOWAIT);
		if (!chunk)
			break;
		*lower_next = chunk;
		lower_next = &chunk->next;
		lcnt++;
	}

	raw_spin_lock(&pid_list->lock);
	if (upper) {
		*upper_next = pid_list->upper_list;
		pid_list->upper_list = upper;
		pid_list->free_upper_chunks += ucnt;
	}
	if (lower) {
		*lower_next = pid_list->lower_list;
		pid_list->lower_list = lower;
		pid_list->free_lower_chunks += lcnt;
	}
	raw_spin_unlock(&pid_list->lock);

	/*
	 * On success of allocating all the chunks, both counters
	 * will be less than zero. If they are not, then an allocation
	 * failed, and we should not try again.
	 */
	if (upper_count >= 0 || lower_count >= 0)
		return;
	/*
	 * When the locks were released, free chunks could have
	 * been used and allocation needs to be done again. Might as
	 * well allocate it now.
	 */
	goto again;
}

/**
 * trace_pid_list_alloc - create a new pid_list
 *
 * Allocates a new pid_list to store pids into.
 *
 * Returns the pid_list on success, NULL otherwise.
 */
struct trace_pid_list *trace_pid_list_alloc(void)
{
	struct trace_pid_list *pid_list;
	int i;

	/* According to linux/thread.h, pids can be no bigger that 30 bits */
	WARN_ON_ONCE(pid_max > (1 << 30));

	pid_list = kzalloc(sizeof(*pid_list), GFP_KERNEL);
	if (!pid_list)
		return NULL;

	init_irq_work(&pid_list->refill_irqwork, pid_list_refill_irq);

	raw_spin_lock_init(&pid_list->lock);

	for (i = 0; i < CHUNK_ALLOC; i++) {
		union upper_chunk *chunk;

		chunk = kzalloc(sizeof(*chunk), GFP_KERNEL);
		if (!chunk)
			break;
		chunk->next = pid_list->upper_list;
		pid_list->upper_list = chunk;
		pid_list->free_upper_chunks++;
	}

	for (i = 0; i < CHUNK_ALLOC; i++) {
		union lower_chunk *chunk;

		chunk = kzalloc(sizeof(*chunk), GFP_KERNEL);
		if (!chunk)
			break;
		chunk->next = pid_list->lower_list;
		pid_list->lower_list = chunk;
		pid_list->free_lower_chunks++;
	}

	return pid_list;
}

/**
 * trace_pid_list_free - Frees an allocated pid_list.
 *
 * Frees the memory for a pid_list that was allocated.
 */
void trace_pid_list_free(struct trace_pid_list *pid_list)
{
	union upper_chunk *upper;
	union lower_chunk *lower;
	int i, j;

	if (!pid_list)
		return;

	irq_work_sync(&pid_list->refill_irqwork);

	while (pid_list->lower_list) {
		union lower_chunk *chunk;

		chunk = pid_list->lower_list;
		pid_list->lower_list = pid_list->lower_list->next;
		kfree(chunk);
	}

	while (pid_list->upper_list) {
		union upper_chunk *chunk;

		chunk = pid_list->upper_list;
		pid_list->upper_list = pid_list->upper_list->next;
		kfree(chunk);
	}

	for (i = 0; i < UPPER1_SIZE; i++) {
		upper = pid_list->upper[i];
		if (upper) {
			for (j = 0; j < UPPER2_SIZE; j++) {
				lower = upper->data[j];
				kfree(lower);
			}
			kfree(upper);
		}
	}
	kfree(pid_list);
}
