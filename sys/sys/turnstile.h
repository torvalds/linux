/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 John Baldwin <jhb@FreeBSD.org>
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

#ifndef _SYS_TURNSTILE_H_
#define _SYS_TURNSTILE_H_

/*
 * Turnstile interface.  Non-sleepable locks use a turnstile for the
 * queue of threads blocked on them when they are contested.  Each
 * turnstile contains two sub-queues: one for threads waiting for a
 * shared, or read, lock, and one for threads waiting for an
 * exclusive, or write, lock.
 *
 * A thread calls turnstile_chain_lock() to lock the turnstile chain
 * associated with a given lock.  A thread calls turnstile_wait() when
 * the lock is contested to be put on the queue and block.  If a thread
 * calls turnstile_trywait() and decides to retry a lock operation instead
 * of blocking, it should call turnstile_cancel() to unlock the associated
 * turnstile chain lock.
 *
 * When a lock is released, the thread calls turnstile_lookup() to look
 * up the turnstile associated with the given lock in the hash table.  Then
 * it calls either turnstile_signal() or turnstile_broadcast() to mark
 * blocked threads for a pending wakeup.  turnstile_signal() marks the
 * highest priority blocked thread while turnstile_broadcast() marks all
 * blocked threads.  The turnstile_signal() function returns true if the
 * turnstile became empty as a result.  After the higher level code finishes
 * releasing the lock, turnstile_unpend() must be called to wake up the
 * pending thread(s) and give up ownership of the turnstile.
 *
 * Alternatively, if a thread wishes to relinquish ownership of a lock
 * without waking up any waiters, it may call turnstile_disown().
 *
 * When a lock is acquired that already has at least one thread contested
 * on it, the new owner of the lock must claim ownership of the turnstile
 * via turnstile_claim().
 *
 * Each thread allocates a turnstile at thread creation via turnstile_alloc()
 * and releases it at thread destruction via turnstile_free().  Note that
 * a turnstile is not tied to a specific thread and that the turnstile
 * released at thread destruction may not be the same turnstile that the
 * thread allocated when it was created.
 *
 * The highest priority thread blocked on a specified queue of a
 * turnstile can be obtained via turnstile_head().  A given queue can
 * also be queried to see if it is empty via turnstile_empty().
 */

struct lock_object;
struct thread;
struct turnstile;

#ifdef _KERNEL

/* Which queue to block on or which queue to wakeup one or more threads from. */
#define	TS_EXCLUSIVE_QUEUE	0
#define	TS_SHARED_QUEUE		1

void	init_turnstiles(void);
void	turnstile_adjust(struct thread *, u_char);
struct turnstile *turnstile_alloc(void);
void	turnstile_broadcast(struct turnstile *, int);
void	turnstile_cancel(struct turnstile *);
void	turnstile_chain_lock(struct lock_object *);
void	turnstile_chain_unlock(struct lock_object *);
void	turnstile_claim(struct turnstile *);
void	turnstile_disown(struct turnstile *);
int	turnstile_empty(struct turnstile *ts, int queue);
void	turnstile_free(struct turnstile *);
struct thread *turnstile_head(struct turnstile *, int);
struct turnstile *turnstile_lookup(struct lock_object *);
int	turnstile_signal(struct turnstile *, int);
struct turnstile *turnstile_trywait(struct lock_object *);
void	turnstile_unpend(struct turnstile *);
void	turnstile_wait(struct turnstile *, struct thread *, int);
struct thread *turnstile_lock(struct turnstile *, struct lock_object **);
void	turnstile_unlock(struct turnstile *, struct lock_object *);
void	turnstile_assert(struct turnstile *);
#endif	/* _KERNEL */
#endif	/* _SYS_TURNSTILE_H_ */
