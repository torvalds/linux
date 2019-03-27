/*
 * Copyright 2010-2015 Samy Al Bahra.
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

#ifndef CK_SPINLOCK_TICKET_H
#define CK_SPINLOCK_TICKET_H

#include <ck_backoff.h>
#include <ck_cc.h>
#include <ck_elide.h>
#include <ck_md.h>
#include <ck_pr.h>
#include <ck_stdbool.h>

#ifndef CK_F_SPINLOCK_TICKET
#define CK_F_SPINLOCK_TICKET
/*
 * If 16-bit or 32-bit increment is supported, implement support for
 * trylock functionality on availability of 32-bit or 64-bit fetch-and-add
 * and compare-and-swap. This code path is only applied to x86*.
 */
#if defined(CK_MD_TSO) && (defined(__x86__) || defined(__x86_64__))
#if defined(CK_F_PR_FAA_32) && defined(CK_F_PR_INC_16) && defined(CK_F_PR_CAS_32)
#define CK_SPINLOCK_TICKET_TYPE		uint32_t
#define CK_SPINLOCK_TICKET_TYPE_BASE	uint16_t
#define CK_SPINLOCK_TICKET_INC(x)	ck_pr_inc_16(x)
#define CK_SPINLOCK_TICKET_CAS(x, y, z) ck_pr_cas_32(x, y, z)
#define CK_SPINLOCK_TICKET_FAA(x, y)	ck_pr_faa_32(x, y)
#define CK_SPINLOCK_TICKET_LOAD(x)	ck_pr_load_32(x)
#define CK_SPINLOCK_TICKET_INCREMENT	(0x00010000UL)
#define CK_SPINLOCK_TICKET_MASK		(0xFFFFUL)
#define CK_SPINLOCK_TICKET_SHIFT	(16)
#elif defined(CK_F_PR_FAA_64) && defined(CK_F_PR_INC_32) && defined(CK_F_PR_CAS_64)
#define CK_SPINLOCK_TICKET_TYPE		uint64_t
#define CK_SPINLOCK_TICKET_TYPE_BASE	uint32_t
#define CK_SPINLOCK_TICKET_INC(x)	ck_pr_inc_32(x)
#define CK_SPINLOCK_TICKET_CAS(x, y, z) ck_pr_cas_64(x, y, z)
#define CK_SPINLOCK_TICKET_FAA(x, y)	ck_pr_faa_64(x, y)
#define CK_SPINLOCK_TICKET_LOAD(x)	ck_pr_load_64(x)
#define CK_SPINLOCK_TICKET_INCREMENT	(0x0000000100000000ULL)
#define CK_SPINLOCK_TICKET_MASK		(0xFFFFFFFFULL)
#define CK_SPINLOCK_TICKET_SHIFT	(32)
#endif
#endif /* CK_MD_TSO */

#if defined(CK_SPINLOCK_TICKET_TYPE)
#define CK_F_SPINLOCK_TICKET_TRYLOCK

struct ck_spinlock_ticket {
	CK_SPINLOCK_TICKET_TYPE value;
};
typedef struct ck_spinlock_ticket ck_spinlock_ticket_t;
#define CK_SPINLOCK_TICKET_INITIALIZER { .value = 0 }

CK_CC_INLINE static void
ck_spinlock_ticket_init(struct ck_spinlock_ticket *ticket)
{

	ticket->value = 0;
	ck_pr_barrier();
	return;
}

CK_CC_INLINE static bool
ck_spinlock_ticket_locked(struct ck_spinlock_ticket *ticket)
{
	CK_SPINLOCK_TICKET_TYPE request, position;

	request = CK_SPINLOCK_TICKET_LOAD(&ticket->value);
	position = request & CK_SPINLOCK_TICKET_MASK;
	request >>= CK_SPINLOCK_TICKET_SHIFT;

	ck_pr_fence_acquire();
	return request != position;
}

CK_CC_INLINE static void
ck_spinlock_ticket_lock(struct ck_spinlock_ticket *ticket)
{
	CK_SPINLOCK_TICKET_TYPE request, position;

	/* Get our ticket number and set next ticket number. */
	request = CK_SPINLOCK_TICKET_FAA(&ticket->value,
	    CK_SPINLOCK_TICKET_INCREMENT);

	position = request & CK_SPINLOCK_TICKET_MASK;
	request >>= CK_SPINLOCK_TICKET_SHIFT;

	while (request != position) {
		ck_pr_stall();
		position = CK_SPINLOCK_TICKET_LOAD(&ticket->value) &
		    CK_SPINLOCK_TICKET_MASK;
	}

	ck_pr_fence_lock();
	return;
}

CK_CC_INLINE static void
ck_spinlock_ticket_lock_pb(struct ck_spinlock_ticket *ticket, unsigned int c)
{
	CK_SPINLOCK_TICKET_TYPE request, position;
	ck_backoff_t backoff;

	/* Get our ticket number and set next ticket number. */
	request = CK_SPINLOCK_TICKET_FAA(&ticket->value,
	    CK_SPINLOCK_TICKET_INCREMENT);

	position = request & CK_SPINLOCK_TICKET_MASK;
	request >>= CK_SPINLOCK_TICKET_SHIFT;

	while (request != position) {
		ck_pr_stall();
		position = CK_SPINLOCK_TICKET_LOAD(&ticket->value) &
		    CK_SPINLOCK_TICKET_MASK;

		backoff = (request - position) & CK_SPINLOCK_TICKET_MASK;
		backoff <<= c;
		ck_backoff_eb(&backoff);
	}

	ck_pr_fence_lock();
	return;
}

CK_CC_INLINE static bool
ck_spinlock_ticket_trylock(struct ck_spinlock_ticket *ticket)
{
	CK_SPINLOCK_TICKET_TYPE snapshot, request, position;

	snapshot = CK_SPINLOCK_TICKET_LOAD(&ticket->value);
	position = snapshot & CK_SPINLOCK_TICKET_MASK;
	request = snapshot >> CK_SPINLOCK_TICKET_SHIFT;

	if (position != request)
		return false;

	if (CK_SPINLOCK_TICKET_CAS(&ticket->value,
	    snapshot, snapshot + CK_SPINLOCK_TICKET_INCREMENT) == false) {
		return false;
	}

	ck_pr_fence_lock();
	return true;
}

CK_CC_INLINE static void
ck_spinlock_ticket_unlock(struct ck_spinlock_ticket *ticket)
{

	ck_pr_fence_unlock();
	CK_SPINLOCK_TICKET_INC((CK_SPINLOCK_TICKET_TYPE_BASE *)(void *)&ticket->value);
	return;
}

#undef CK_SPINLOCK_TICKET_TYPE
#undef CK_SPINLOCK_TICKET_TYPE_BASE
#undef CK_SPINLOCK_TICKET_INC
#undef CK_SPINLOCK_TICKET_FAA
#undef CK_SPINLOCK_TICKET_LOAD
#undef CK_SPINLOCK_TICKET_INCREMENT
#undef CK_SPINLOCK_TICKET_MASK
#undef CK_SPINLOCK_TICKET_SHIFT
#else
/*
 * MESI benefits from cacheline padding between next and current. This avoids
 * invalidation of current from the cache due to incoming lock requests.
 */
struct ck_spinlock_ticket {
	unsigned int next;
	unsigned int position;
};
typedef struct ck_spinlock_ticket ck_spinlock_ticket_t;

#define CK_SPINLOCK_TICKET_INITIALIZER {.next = 0, .position = 0}

CK_CC_INLINE static void
ck_spinlock_ticket_init(struct ck_spinlock_ticket *ticket)
{

	ticket->next = 0;
	ticket->position = 0;
	ck_pr_barrier();

	return;
}

CK_CC_INLINE static bool
ck_spinlock_ticket_locked(struct ck_spinlock_ticket *ticket)
{
	bool r;

	r = ck_pr_load_uint(&ticket->position) !=
	    ck_pr_load_uint(&ticket->next);
	ck_pr_fence_acquire();
	return r;
}

CK_CC_INLINE static void
ck_spinlock_ticket_lock(struct ck_spinlock_ticket *ticket)
{
	unsigned int request;

	/* Get our ticket number and set next ticket number. */
	request = ck_pr_faa_uint(&ticket->next, 1);

	/*
	 * Busy-wait until our ticket number is current.
	 * We can get away without a fence here assuming
	 * our position counter does not overflow.
	 */
	while (ck_pr_load_uint(&ticket->position) != request)
		ck_pr_stall();

	ck_pr_fence_lock();
	return;
}

CK_CC_INLINE static void
ck_spinlock_ticket_lock_pb(struct ck_spinlock_ticket *ticket, unsigned int c)
{
	ck_backoff_t backoff;
	unsigned int request, position;

	request = ck_pr_faa_uint(&ticket->next, 1);

	for (;;) {
		position = ck_pr_load_uint(&ticket->position);
		if (position == request)
			break;

		backoff = request - position;
		backoff <<= c;

		/*
		 * Ideally, back-off from generating cache traffic for at least
		 * the amount of time necessary for the number of pending lock
		 * acquisition and relinquish operations (assuming an empty
		 * critical section).
		 */
		ck_backoff_eb(&backoff);
	}

	ck_pr_fence_lock();
	return;
}

CK_CC_INLINE static void
ck_spinlock_ticket_unlock(struct ck_spinlock_ticket *ticket)
{
	unsigned int update;

	ck_pr_fence_unlock();

	/*
	 * Update current ticket value so next lock request can proceed.
	 * Overflow behavior is assumed to be roll-over, in which case,
	 * it is only an issue if there are 2^32 pending lock requests.
	 */
	update = ck_pr_load_uint(&ticket->position);
	ck_pr_store_uint(&ticket->position, update + 1);
	return;
}
#endif /* !CK_F_SPINLOCK_TICKET_TRYLOCK */

CK_ELIDE_PROTOTYPE(ck_spinlock_ticket, ck_spinlock_ticket_t,
    ck_spinlock_ticket_locked, ck_spinlock_ticket_lock,
    ck_spinlock_ticket_locked, ck_spinlock_ticket_unlock)

CK_ELIDE_TRYLOCK_PROTOTYPE(ck_spinlock_ticket, ck_spinlock_ticket_t,
    ck_spinlock_ticket_locked, ck_spinlock_ticket_trylock)

#endif /* CK_F_SPINLOCK_TICKET */
#endif /* CK_SPINLOCK_TICKET_H */
