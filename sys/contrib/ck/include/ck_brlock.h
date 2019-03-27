/*
 * Copyright 2011-2015 Samy Al Bahra.
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

#ifndef CK_BRLOCK_H
#define CK_BRLOCK_H

/*
 * Big reader spinlocks provide cache-local contention-free read
 * lock acquisition in the absence of writers. This comes at the
 * cost of O(n) write lock acquisition. They were first implemented
 * in the Linux kernel by Ingo Molnar and David S. Miller around the
 * year 2000.
 *
 * This implementation is thread-agnostic which comes at the cost
 * of larger reader objects due to necessary linkage overhead. In
 * order to cut down on TLB pressure, it is recommended to allocate
 * these objects on the same page.
 */

#include <ck_pr.h>
#include <ck_stdbool.h>
#include <ck_stddef.h>

struct ck_brlock_reader {
	unsigned int n_readers;
	struct ck_brlock_reader *previous;
	struct ck_brlock_reader *next;
};
typedef struct ck_brlock_reader ck_brlock_reader_t;

#define CK_BRLOCK_READER_INITIALIZER {0}

struct ck_brlock {
	struct ck_brlock_reader *readers;
	unsigned int writer;
};
typedef struct ck_brlock ck_brlock_t;

#define CK_BRLOCK_INITIALIZER {NULL, false}

CK_CC_INLINE static void
ck_brlock_init(struct ck_brlock *br)
{

	br->readers = NULL;
	br->writer = false;
	ck_pr_barrier();
	return;
}

CK_CC_INLINE static void
ck_brlock_write_lock(struct ck_brlock *br)
{
	struct ck_brlock_reader *cursor;

	/*
	 * As the frequency of write acquisitions should be low,
	 * there is no point to more advanced contention avoidance.
	 */
	while (ck_pr_fas_uint(&br->writer, true) == true)
		ck_pr_stall();

	ck_pr_fence_atomic_load();

	/* The reader list is protected under the writer br. */
	for (cursor = br->readers; cursor != NULL; cursor = cursor->next) {
		while (ck_pr_load_uint(&cursor->n_readers) != 0)
			ck_pr_stall();
	}

	ck_pr_fence_lock();
	return;
}

CK_CC_INLINE static void
ck_brlock_write_unlock(struct ck_brlock *br)
{

	ck_pr_fence_unlock();
	ck_pr_store_uint(&br->writer, false);
	return;
}

CK_CC_INLINE static bool
ck_brlock_write_trylock(struct ck_brlock *br, unsigned int factor)
{
	struct ck_brlock_reader *cursor;
	unsigned int steps = 0;

	while (ck_pr_fas_uint(&br->writer, true) == true) {
		if (++steps >= factor)
			return false;

		ck_pr_stall();
	}

	/*
	 * We do not require a strict fence here as atomic RMW operations
	 * are serializing.
	 */
	ck_pr_fence_atomic_load();

	for (cursor = br->readers; cursor != NULL; cursor = cursor->next) {
		while (ck_pr_load_uint(&cursor->n_readers) != 0) {
			if (++steps >= factor) {
				ck_brlock_write_unlock(br);
				return false;
			}

			ck_pr_stall();
		}
	}

	ck_pr_fence_lock();
	return true;
}

CK_CC_INLINE static void
ck_brlock_read_register(struct ck_brlock *br, struct ck_brlock_reader *reader)
{

	reader->n_readers = 0;
	reader->previous = NULL;

	/* Implicit compiler barrier. */
	ck_brlock_write_lock(br);

	reader->next = ck_pr_load_ptr(&br->readers);
	if (reader->next != NULL)
		reader->next->previous = reader;
	ck_pr_store_ptr(&br->readers, reader);

	ck_brlock_write_unlock(br);
	return;
}

CK_CC_INLINE static void
ck_brlock_read_unregister(struct ck_brlock *br, struct ck_brlock_reader *reader)
{

	ck_brlock_write_lock(br);

	if (reader->next != NULL)
		reader->next->previous = reader->previous;

	if (reader->previous != NULL)
		reader->previous->next = reader->next;
	else
		br->readers = reader->next;

	ck_brlock_write_unlock(br);
	return;
}

CK_CC_INLINE static void
ck_brlock_read_lock(struct ck_brlock *br, struct ck_brlock_reader *reader)
{

	if (reader->n_readers >= 1) {
		ck_pr_store_uint(&reader->n_readers, reader->n_readers + 1);
		return;
	}

	for (;;) {
		while (ck_pr_load_uint(&br->writer) == true)
			ck_pr_stall();

#if defined(__x86__) || defined(__x86_64__)
		ck_pr_fas_uint(&reader->n_readers, 1);

		/*
		 * Serialize reader counter update with respect to load of
		 * writer.
		 */
		ck_pr_fence_atomic_load();
#else
		ck_pr_store_uint(&reader->n_readers, 1);

		/*
		 * Serialize reader counter update with respect to load of
		 * writer.
		 */
		ck_pr_fence_store_load();
#endif

		if (ck_pr_load_uint(&br->writer) == false)
			break;

		ck_pr_store_uint(&reader->n_readers, 0);
	}

	ck_pr_fence_lock();
	return;
}

CK_CC_INLINE static bool
ck_brlock_read_trylock(struct ck_brlock *br,
		       struct ck_brlock_reader *reader,
		       unsigned int factor)
{
	unsigned int steps = 0;

	if (reader->n_readers >= 1) {
		ck_pr_store_uint(&reader->n_readers, reader->n_readers + 1);
		return true;
	}

	for (;;) {
		while (ck_pr_load_uint(&br->writer) == true) {
			if (++steps >= factor)
				return false;

			ck_pr_stall();
		}

#if defined(__x86__) || defined(__x86_64__)
		ck_pr_fas_uint(&reader->n_readers, 1);

		/*
		 * Serialize reader counter update with respect to load of
		 * writer.
		 */
		ck_pr_fence_atomic_load();
#else
		ck_pr_store_uint(&reader->n_readers, 1);

		/*
		 * Serialize reader counter update with respect to load of
		 * writer.
		 */
		ck_pr_fence_store_load();
#endif

		if (ck_pr_load_uint(&br->writer) == false)
			break;

		ck_pr_store_uint(&reader->n_readers, 0);

		if (++steps >= factor)
			return false;
	}

	ck_pr_fence_lock();
	return true;
}

CK_CC_INLINE static void
ck_brlock_read_unlock(struct ck_brlock_reader *reader)
{

	ck_pr_fence_unlock();
	ck_pr_store_uint(&reader->n_readers, reader->n_readers - 1);
	return;
}

#endif /* CK_BRLOCK_H */
