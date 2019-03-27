/*
 * Copyright 2013-2015 Samy Al Bahra.
 * Copyright 2013 Brendon Scheinman.
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

#ifndef CK_COHORT_H
#define CK_COHORT_H

/*
 * This is an implementation of lock cohorts as described in:
 *     Dice, D.; Marathe, V.; and Shavit, N. 2012.
 *     Lock Cohorting: A General Technique for Designing NUMA Locks
 */

#include <ck_cc.h>
#include <ck_pr.h>
#include <ck_stddef.h>

enum ck_cohort_state {
	CK_COHORT_STATE_GLOBAL = 0,
	CK_COHORT_STATE_LOCAL = 1
};

#define CK_COHORT_DEFAULT_LOCAL_PASS_LIMIT 10

#define CK_COHORT_NAME(N) ck_cohort_##N
#define CK_COHORT_INSTANCE(N) struct CK_COHORT_NAME(N)
#define CK_COHORT_INIT(N, C, GL, LL, P) ck_cohort_##N##_init(C, GL, LL, P)
#define CK_COHORT_LOCK(N, C, GC, LC) ck_cohort_##N##_lock(C, GC, LC)
#define CK_COHORT_UNLOCK(N, C, GC, LC) ck_cohort_##N##_unlock(C, GC, LC)
#define CK_COHORT_TRYLOCK(N, C, GLC, LLC, LUC) ck_cohort_##N##_trylock(C, GLC, LLC, LUC)
#define CK_COHORT_LOCKED(N, C, GC, LC) ck_cohort_##N##_locked(C, GC, LC)

#define CK_COHORT_PROTOTYPE(N, GL, GU, GI, LL, LU, LI)				\
	CK_COHORT_INSTANCE(N) {							\
		void *global_lock;						\
		void *local_lock;						\
		enum ck_cohort_state release_state;				\
		unsigned int waiting_threads;					\
		unsigned int acquire_count;					\
		unsigned int local_pass_limit;					\
	};									\
										\
	CK_CC_INLINE static void						\
	ck_cohort_##N##_init(struct ck_cohort_##N *cohort,			\
	    void *global_lock, void *local_lock, unsigned int pass_limit)	\
	{									\
		cohort->global_lock = global_lock;				\
		cohort->local_lock = local_lock;				\
		cohort->release_state = CK_COHORT_STATE_GLOBAL;			\
		cohort->waiting_threads = 0;					\
		cohort->acquire_count = 0;					\
		cohort->local_pass_limit = pass_limit;				\
		ck_pr_barrier();						\
		return;								\
	}									\
										\
	CK_CC_INLINE static void						\
	ck_cohort_##N##_lock(CK_COHORT_INSTANCE(N) *cohort,			\
	    void *global_context, void *local_context)				\
	{									\
										\
		ck_pr_inc_uint(&cohort->waiting_threads);			\
		LL(cohort->local_lock, local_context);				\
		ck_pr_dec_uint(&cohort->waiting_threads);			\
										\
		if (cohort->release_state == CK_COHORT_STATE_GLOBAL) {		\
			GL(cohort->global_lock, global_context);		\
		}								\
										\
		++cohort->acquire_count;					\
		return;								\
	}									\
										\
	CK_CC_INLINE static void						\
	ck_cohort_##N##_unlock(CK_COHORT_INSTANCE(N) *cohort,			\
	    void *global_context, void *local_context)				\
	{									\
										\
		if (ck_pr_load_uint(&cohort->waiting_threads) > 0		\
		    && cohort->acquire_count < cohort->local_pass_limit) {	\
			cohort->release_state = CK_COHORT_STATE_LOCAL;		\
		} else {							\
			GU(cohort->global_lock, global_context);		\
			cohort->release_state = CK_COHORT_STATE_GLOBAL;		\
			cohort->acquire_count = 0;				\
		}								\
										\
		ck_pr_fence_release();						\
		LU(cohort->local_lock, local_context);				\
										\
		return;								\
	}									\
										\
	CK_CC_INLINE static bool						\
	ck_cohort_##N##_locked(CK_COHORT_INSTANCE(N) *cohort,			\
	    void *global_context, void *local_context)				\
	{									\
		return GI(cohort->local_lock, local_context) ||			\
		    LI(cohort->global_lock, global_context);			\
	}

#define CK_COHORT_TRYLOCK_PROTOTYPE(N, GL, GU, GI, GTL, LL, LU, LI, LTL)	\
	CK_COHORT_PROTOTYPE(N, GL, GU, GI, LL, LU, LI)				\
	CK_CC_INLINE static bool						\
	ck_cohort_##N##_trylock(CK_COHORT_INSTANCE(N) *cohort,			\
	    void *global_context, void *local_context,				\
	    void *local_unlock_context)						\
	{									\
										\
		bool trylock_result;						\
										\
		ck_pr_inc_uint(&cohort->waiting_threads);			\
		trylock_result = LTL(cohort->local_lock, local_context);	\
		ck_pr_dec_uint(&cohort->waiting_threads);			\
		if (trylock_result == false) {					\
			return false;						\
		}								\
										\
		if (cohort->release_state == CK_COHORT_STATE_GLOBAL &&		\
		    GTL(cohort->global_lock, global_context) == false) {	\
		    	LU(cohort->local_lock, local_unlock_context);		\
			return false;						\
		}								\
										\
		++cohort->acquire_count;					\
		return true;							\
	}

#define CK_COHORT_INITIALIZER {							\
	.global_lock = NULL,							\
	.local_lock = NULL,							\
	.release_state = CK_COHORT_STATE_GLOBAL,				\
	.waiting_threads = 0,							\
	.acquire_count = 0,							\
	.local_pass_limit = CK_COHORT_DEFAULT_LOCAL_PASS_LIMIT			\
}

#endif /* CK_COHORT_H */
