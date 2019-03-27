/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 John Baldwin <jhb@FreeBSD.org>
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
 *
 * $FreeBSD$
 */

#ifndef _SYS_SLEEPQUEUE_H_
#define _SYS_SLEEPQUEUE_H_

/*
 * Sleep queue interface.  Sleep/wakeup, condition variables, and sx
 * locks use a sleep queue for the queue of threads blocked on a sleep
 * channel.
 *
 * A thread calls sleepq_lock() to lock the sleep queue chain associated
 * with a given wait channel.  A thread can then call call sleepq_add() to
 * add themself onto a sleep queue and call one of the sleepq_wait()
 * functions to actually go to sleep.  If a thread needs to abort a sleep
 * operation it should call sleepq_release() to unlock the associated sleep
 * queue chain lock.  If the thread also needs to remove itself from a queue
 * it just enqueued itself on, it can use sleepq_remove() instead.
 *
 * If the thread only wishes to sleep for a limited amount of time, it can
 * call sleepq_set_timeout() after sleepq_add() to setup a timeout.  It
 * should then use one of the sleepq_timedwait() functions to block.
 *
 * A thread is normally resumed from a sleep queue by either the
 * sleepq_signal() or sleepq_broadcast() functions.  Sleepq_signal() wakes
 * the thread with the highest priority that is sleeping on the specified
 * wait channel.  Sleepq_broadcast() wakes all threads that are sleeping
 * on the specified wait channel.  A thread sleeping in an interruptible
 * sleep can be interrupted by calling sleepq_abort().  A thread can also
 * be removed from a specified sleep queue using the sleepq_remove()
 * function.  Note that the sleep queue chain must first be locked via
 * sleepq_lock() before calling sleepq_abort(), sleepq_broadcast(), or
 * sleepq_signal().  These routines each return a boolean that will be true
 * if at least one swapped-out thread was resumed.  In that case, the caller
 * is responsible for waking up the swapper by calling kick_proc0() after
 * releasing the sleep queue chain lock.
 *
 * Each thread allocates a sleep queue at thread creation via sleepq_alloc()
 * and releases it at thread destruction via sleepq_free().  Note that
 * a sleep queue is not tied to a specific thread and that the sleep queue
 * released at thread destruction may not be the same sleep queue that the
 * thread allocated when it was created.
 *
 * XXX: Some other parts of the kernel such as ithread sleeping may end up
 * using this interface as well (death to TDI_IWAIT!)
 */

struct lock_object;
struct sleepqueue;
struct thread;

#ifdef _KERNEL

#define	SLEEPQ_TYPE		0x0ff		/* Mask of sleep queue types. */
#define	SLEEPQ_SLEEP		0x00		/* Used by sleep/wakeup. */
#define	SLEEPQ_CONDVAR		0x01		/* Used for a cv. */
#define	SLEEPQ_PAUSE		0x02		/* Used by pause. */
#define	SLEEPQ_SX		0x03		/* Used by an sx lock. */
#define	SLEEPQ_LK		0x04		/* Used by a lockmgr. */
#define	SLEEPQ_INTERRUPTIBLE	0x100		/* Sleep is interruptible. */

void	init_sleepqueues(void);
int	sleepq_abort(struct thread *td, int intrval);
void	sleepq_add(void *wchan, struct lock_object *lock, const char *wmesg,
	    int flags, int queue);
struct sleepqueue *sleepq_alloc(void);
int	sleepq_broadcast(void *wchan, int flags, int pri, int queue);
void	sleepq_chains_remove_matching(bool (*matches)(struct thread *));
void	sleepq_free(struct sleepqueue *sq);
void	sleepq_lock(void *wchan);
struct sleepqueue *sleepq_lookup(void *wchan);
void	sleepq_release(void *wchan);
void	sleepq_remove(struct thread *td, void *wchan);
int	sleepq_remove_matching(struct sleepqueue *sq, int queue,
	    bool (*matches)(struct thread *), int pri);
int	sleepq_signal(void *wchan, int flags, int pri, int queue);
void	sleepq_set_timeout_sbt(void *wchan, sbintime_t sbt,
	    sbintime_t pr, int flags);
#define	sleepq_set_timeout(wchan, timo)					\
    sleepq_set_timeout_sbt((wchan), tick_sbt * (timo), 0, C_HARDCLOCK)
u_int	sleepq_sleepcnt(void *wchan, int queue);
int	sleepq_timedwait(void *wchan, int pri);
int	sleepq_timedwait_sig(void *wchan, int pri);
int	sleepq_type(void *wchan);
void	sleepq_wait(void *wchan, int pri);
int	sleepq_wait_sig(void *wchan, int pri);

#ifdef STACK
struct sbuf;
int sleepq_sbuf_print_stacks(struct sbuf *sb, void *wchan, int queue,
    int *count_stacks_printed);
#endif

#endif	/* _KERNEL */
#endif	/* !_SYS_SLEEPQUEUE_H_ */
