/*
 * Copyright 2019 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */
#include <linux/semaphore.h>
#include <linux/atomic.h>

/*
 * Reusable 2 PHASE task barrier (randevouz point) implementation for N tasks.
 * Based on the Little book of sempahores - https://greenteapress.com/wp/semaphores/
 */



#ifndef DRM_TASK_BARRIER_H_
#define DRM_TASK_BARRIER_H_

/*
 * Represents an instance of a task barrier.
 */
struct task_barrier {
	unsigned int n;
	atomic_t count;
	struct semaphore enter_turnstile;
	struct semaphore exit_turnstile;
};

static inline void task_barrier_signal_turnstile(struct semaphore *turnstile,
						 unsigned int n)
{
	int i;

	for (i = 0 ; i < n; i++)
		up(turnstile);
}

static inline void task_barrier_init(struct task_barrier *tb)
{
	tb->n = 0;
	atomic_set(&tb->count, 0);
	sema_init(&tb->enter_turnstile, 0);
	sema_init(&tb->exit_turnstile, 0);
}

static inline void task_barrier_add_task(struct task_barrier *tb)
{
	tb->n++;
}

static inline void task_barrier_rem_task(struct task_barrier *tb)
{
	tb->n--;
}

/*
 * Lines up all the threads BEFORE the critical point.
 *
 * When all thread passed this code the entry barrier is back to locked state.
 */
static inline void task_barrier_enter(struct task_barrier *tb)
{
	if (atomic_inc_return(&tb->count) == tb->n)
		task_barrier_signal_turnstile(&tb->enter_turnstile, tb->n);

	down(&tb->enter_turnstile);
}

/*
 * Lines up all the threads AFTER the critical point.
 *
 * This function is used to avoid any one thread running ahead if the barrier is
 *  used repeatedly .
 */
static inline void task_barrier_exit(struct task_barrier *tb)
{
	if (atomic_dec_return(&tb->count) == 0)
		task_barrier_signal_turnstile(&tb->exit_turnstile, tb->n);

	down(&tb->exit_turnstile);
}

/* Convinieince function when nothing to be done in between entry and exit */
static inline void task_barrier_full(struct task_barrier *tb)
{
	task_barrier_enter(tb);
	task_barrier_exit(tb);
}

#endif
