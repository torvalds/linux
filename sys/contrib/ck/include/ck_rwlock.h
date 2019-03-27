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

#ifndef CK_RWLOCK_H
#define CK_RWLOCK_H

#include <ck_elide.h>
#include <ck_pr.h>
#include <ck_stdbool.h>
#include <ck_stddef.h>

struct ck_rwlock {
	unsigned int writer;
	unsigned int n_readers;
};
typedef struct ck_rwlock ck_rwlock_t;

#define CK_RWLOCK_INITIALIZER {0, 0}

CK_CC_INLINE static void
ck_rwlock_init(struct ck_rwlock *rw)
{

	rw->writer = 0;
	rw->n_readers = 0;
	ck_pr_barrier();
	return;
}

CK_CC_INLINE static void
ck_rwlock_write_unlock(ck_rwlock_t *rw)
{

	ck_pr_fence_unlock();
	ck_pr_store_uint(&rw->writer, 0);
	return;
}

CK_CC_INLINE static bool
ck_rwlock_locked_writer(ck_rwlock_t *rw)
{
	bool r;

	r = ck_pr_load_uint(&rw->writer);
	ck_pr_fence_acquire();
	return r;
}

CK_CC_INLINE static void
ck_rwlock_write_downgrade(ck_rwlock_t *rw)
{

	ck_pr_inc_uint(&rw->n_readers);
	ck_rwlock_write_unlock(rw);
	return;
}

CK_CC_INLINE static bool
ck_rwlock_locked(ck_rwlock_t *rw)
{
	bool l;

	l = ck_pr_load_uint(&rw->n_readers) |
	    ck_pr_load_uint(&rw->writer);
	ck_pr_fence_acquire();
	return l;
}

CK_CC_INLINE static bool
ck_rwlock_write_trylock(ck_rwlock_t *rw)
{

	if (ck_pr_fas_uint(&rw->writer, 1) != 0)
		return false;

	ck_pr_fence_atomic_load();

	if (ck_pr_load_uint(&rw->n_readers) != 0) {
		ck_rwlock_write_unlock(rw);
		return false;
	}

	ck_pr_fence_lock();
	return true;
}

CK_ELIDE_TRYLOCK_PROTOTYPE(ck_rwlock_write, ck_rwlock_t,
    ck_rwlock_locked, ck_rwlock_write_trylock)

CK_CC_INLINE static void
ck_rwlock_write_lock(ck_rwlock_t *rw)
{

	while (ck_pr_fas_uint(&rw->writer, 1) != 0)
		ck_pr_stall();

	ck_pr_fence_atomic_load();

	while (ck_pr_load_uint(&rw->n_readers) != 0)
		ck_pr_stall();

	ck_pr_fence_lock();
	return;
}

CK_ELIDE_PROTOTYPE(ck_rwlock_write, ck_rwlock_t,
    ck_rwlock_locked, ck_rwlock_write_lock,
    ck_rwlock_locked_writer, ck_rwlock_write_unlock)

CK_CC_INLINE static bool
ck_rwlock_read_trylock(ck_rwlock_t *rw)
{

	if (ck_pr_load_uint(&rw->writer) != 0)
		return false;

	ck_pr_inc_uint(&rw->n_readers);

	/*
	 * Serialize with respect to concurrent write
	 * lock operation.
	 */
	ck_pr_fence_atomic_load();

	if (ck_pr_load_uint(&rw->writer) == 0) {
		ck_pr_fence_lock();
		return true;
	}

	ck_pr_dec_uint(&rw->n_readers);
	return false;
}

CK_ELIDE_TRYLOCK_PROTOTYPE(ck_rwlock_read, ck_rwlock_t,
    ck_rwlock_locked_writer, ck_rwlock_read_trylock)

CK_CC_INLINE static void
ck_rwlock_read_lock(ck_rwlock_t *rw)
{

	for (;;) {
		while (ck_pr_load_uint(&rw->writer) != 0)
			ck_pr_stall();

		ck_pr_inc_uint(&rw->n_readers);

		/*
		 * Serialize with respect to concurrent write
		 * lock operation.
		 */
		ck_pr_fence_atomic_load();

		if (ck_pr_load_uint(&rw->writer) == 0)
			break;

		ck_pr_dec_uint(&rw->n_readers);
	}

	/* Acquire semantics are necessary. */
	ck_pr_fence_load();
	return;
}

CK_CC_INLINE static bool
ck_rwlock_locked_reader(ck_rwlock_t *rw)
{

	ck_pr_fence_load();
	return ck_pr_load_uint(&rw->n_readers);
}

CK_CC_INLINE static void
ck_rwlock_read_unlock(ck_rwlock_t *rw)
{

	ck_pr_fence_load_atomic();
	ck_pr_dec_uint(&rw->n_readers);
	return;
}

CK_ELIDE_PROTOTYPE(ck_rwlock_read, ck_rwlock_t,
    ck_rwlock_locked_writer, ck_rwlock_read_lock,
    ck_rwlock_locked_reader, ck_rwlock_read_unlock)

/*
 * Recursive writer reader-writer lock implementation.
 */
struct ck_rwlock_recursive {
	struct ck_rwlock rw;
	unsigned int wc;
};
typedef struct ck_rwlock_recursive ck_rwlock_recursive_t;

#define CK_RWLOCK_RECURSIVE_INITIALIZER {CK_RWLOCK_INITIALIZER, 0}

CK_CC_INLINE static void
ck_rwlock_recursive_write_lock(ck_rwlock_recursive_t *rw, unsigned int tid)
{
	unsigned int o;

	o = ck_pr_load_uint(&rw->rw.writer);
	if (o == tid)
		goto leave;

	while (ck_pr_cas_uint(&rw->rw.writer, 0, tid) == false)
		ck_pr_stall();

	ck_pr_fence_atomic_load();

	while (ck_pr_load_uint(&rw->rw.n_readers) != 0)
		ck_pr_stall();

	ck_pr_fence_lock();
leave:
	rw->wc++;
	return;
}

CK_CC_INLINE static bool
ck_rwlock_recursive_write_trylock(ck_rwlock_recursive_t *rw, unsigned int tid)
{
	unsigned int o;

	o = ck_pr_load_uint(&rw->rw.writer);
	if (o == tid)
		goto leave;

	if (ck_pr_cas_uint(&rw->rw.writer, 0, tid) == false)
		return false;

	ck_pr_fence_atomic_load();

	if (ck_pr_load_uint(&rw->rw.n_readers) != 0) {
		ck_pr_store_uint(&rw->rw.writer, 0);
		return false;
	}

	ck_pr_fence_lock();
leave:
	rw->wc++;
	return true;
}

CK_CC_INLINE static void
ck_rwlock_recursive_write_unlock(ck_rwlock_recursive_t *rw)
{

	if (--rw->wc == 0) {
		ck_pr_fence_unlock();
		ck_pr_store_uint(&rw->rw.writer, 0);
	}

	return;
}

CK_CC_INLINE static void
ck_rwlock_recursive_read_lock(ck_rwlock_recursive_t *rw)
{

	ck_rwlock_read_lock(&rw->rw);
	return;
}

CK_CC_INLINE static bool
ck_rwlock_recursive_read_trylock(ck_rwlock_recursive_t *rw)
{

	return ck_rwlock_read_trylock(&rw->rw);
}

CK_CC_INLINE static void
ck_rwlock_recursive_read_unlock(ck_rwlock_recursive_t *rw)
{

	ck_rwlock_read_unlock(&rw->rw);
	return;
}

#endif /* CK_RWLOCK_H */
