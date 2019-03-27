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

#ifndef CK_BYTELOCK_H
#define CK_BYTELOCK_H

/*
 * The implementations here are derived from the work described in:
 *   Dice, D. and Shavit, N. 2010. TLRW: return of the read-write lock.
 *   In Proceedings of the 22nd ACM Symposium on Parallelism in Algorithms
 *   and Architectures (Thira, Santorini, Greece, June 13 - 15, 2010).
 *   SPAA '10. ACM, New York, NY, 284-293.
 */

#include <ck_cc.h>
#include <ck_md.h>
#include <ck_pr.h>
#include <ck_stdbool.h>
#include <ck_stddef.h>
#include <ck_limits.h>

struct ck_bytelock {
	unsigned int owner;
	unsigned int n_readers;
	uint8_t readers[CK_MD_CACHELINE - sizeof(unsigned int) * 2] CK_CC_ALIGN(8);
};
typedef struct ck_bytelock ck_bytelock_t;

#define CK_BYTELOCK_INITIALIZER { 0, 0, {0} }
#define CK_BYTELOCK_UNSLOTTED   UINT_MAX

CK_CC_INLINE static void
ck_bytelock_init(struct ck_bytelock *bytelock)
{
	unsigned int i;

	bytelock->owner = 0;
	bytelock->n_readers = 0;
	for (i = 0; i < sizeof bytelock->readers; i++)
		bytelock->readers[i] = false;

	ck_pr_barrier();
	return;
}

#ifdef CK_F_PR_LOAD_64
#define CK_BYTELOCK_LENGTH sizeof(uint64_t)
#define CK_BYTELOCK_LOAD ck_pr_load_64
#define CK_BYTELOCK_TYPE uint64_t
#elif defined(CK_F_PR_LOAD_32)
#define CK_BYTELOCK_LENGTH sizeof(uint32_t)
#define CK_BYTELOCK_LOAD ck_pr_load_32
#define CK_BYTELOCK_TYPE uint32_t
#else
#error Unsupported platform.
#endif

CK_CC_INLINE static void
ck_bytelock_write_lock(struct ck_bytelock *bytelock, unsigned int slot)
{
	CK_BYTELOCK_TYPE *readers = (void *)bytelock->readers;
	unsigned int i;

	/* Announce upcoming writer acquisition. */
	while (ck_pr_cas_uint(&bytelock->owner, 0, slot) == false)
		ck_pr_stall();

	/* If we are slotted, we might be upgrading from a read lock. */
	if (slot <= sizeof bytelock->readers)
		ck_pr_store_8(&bytelock->readers[slot - 1], false);

	/*
	 * Wait for slotted readers to drain out. This also provides the
	 * lock acquire semantics.
	 */
	ck_pr_fence_atomic_load();

	for (i = 0; i < sizeof(bytelock->readers) / CK_BYTELOCK_LENGTH; i++) {
		while (CK_BYTELOCK_LOAD(&readers[i]) != false)
			ck_pr_stall();
	}

	/* Wait for unslotted readers to drain out. */
	while (ck_pr_load_uint(&bytelock->n_readers) != 0)
		ck_pr_stall();

	ck_pr_fence_lock();
	return;
}

#undef CK_BYTELOCK_LENGTH
#undef CK_BYTELOCK_LOAD
#undef CK_BYTELOCK_TYPE

CK_CC_INLINE static void
ck_bytelock_write_unlock(struct ck_bytelock *bytelock)
{

	ck_pr_fence_unlock();
	ck_pr_store_uint(&bytelock->owner, 0);
	return;
}

CK_CC_INLINE static void
ck_bytelock_read_lock(struct ck_bytelock *bytelock, unsigned int slot)
{

	if (ck_pr_load_uint(&bytelock->owner) == slot) {
		ck_pr_store_8(&bytelock->readers[slot - 1], true);
		ck_pr_fence_strict_store();
		ck_pr_store_uint(&bytelock->owner, 0);
		return;
	}

	/* Unslotted threads will have to use the readers counter. */
	if (slot > sizeof bytelock->readers) {
		for (;;) {
			ck_pr_inc_uint(&bytelock->n_readers);
			ck_pr_fence_atomic_load();
			if (ck_pr_load_uint(&bytelock->owner) == 0)
				break;
			ck_pr_dec_uint(&bytelock->n_readers);

			while (ck_pr_load_uint(&bytelock->owner) != 0)
				ck_pr_stall();
		}

		ck_pr_fence_lock();
		return;
	}

	slot -= 1;
	for (;;) {
#ifdef CK_F_PR_FAA_8
		ck_pr_fas_8(&bytelock->readers[slot], true);
		ck_pr_fence_atomic_load();
#else
		ck_pr_store_8(&bytelock->readers[slot], true);
		ck_pr_fence_store_load();
#endif

		/*
		 * If there is no owner at this point, our slot has
		 * already been published and it is guaranteed no
		 * write acquisition will succeed until we drain out.
		 */
		if (ck_pr_load_uint(&bytelock->owner) == 0)
			break;

		ck_pr_store_8(&bytelock->readers[slot], false);
		while (ck_pr_load_uint(&bytelock->owner) != 0)
			ck_pr_stall();
	}

	ck_pr_fence_lock();
	return;
}

CK_CC_INLINE static void
ck_bytelock_read_unlock(struct ck_bytelock *bytelock, unsigned int slot)
{

	ck_pr_fence_unlock();

	if (slot > sizeof bytelock->readers)
		ck_pr_dec_uint(&bytelock->n_readers);
	else
		ck_pr_store_8(&bytelock->readers[slot - 1], false);

	return;
}

#endif /* CK_BYTELOCK_H */
