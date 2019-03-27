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

#ifndef CK_RWCOHORT_H
#define CK_RWCOHORT_H

/*
 * This is an implementation of NUMA-aware reader-writer locks as described in:
 *     Calciu, I.; Dice, D.; Lev, Y.; Luchangco, V.; Marathe, V.; and Shavit, N. 2014.
 *     NUMA-Aware Reader-Writer Locks
 */

#include <ck_cc.h>
#include <ck_pr.h>
#include <ck_stddef.h>
#include <ck_cohort.h>

#define CK_RWCOHORT_WP_NAME(N) ck_rwcohort_wp_##N
#define CK_RWCOHORT_WP_INSTANCE(N) struct CK_RWCOHORT_WP_NAME(N)
#define CK_RWCOHORT_WP_INIT(N, RW, WL) ck_rwcohort_wp_##N##_init(RW, WL)
#define CK_RWCOHORT_WP_READ_LOCK(N, RW, C, GC, LC)	\
	ck_rwcohort_wp_##N##_read_lock(RW, C, GC, LC)
#define CK_RWCOHORT_WP_READ_UNLOCK(N, RW, C, GC, LC)	\
	ck_rwcohort_wp_##N##_read_unlock(RW)
#define CK_RWCOHORT_WP_WRITE_LOCK(N, RW, C, GC, LC)	\
	ck_rwcohort_wp_##N##_write_lock(RW, C, GC, LC)
#define CK_RWCOHORT_WP_WRITE_UNLOCK(N, RW, C, GC, LC)	\
	ck_rwcohort_wp_##N##_write_unlock(RW, C, GC, LC)
#define CK_RWCOHORT_WP_DEFAULT_WAIT_LIMIT 1000

#define CK_RWCOHORT_WP_PROTOTYPE(N)							\
	CK_RWCOHORT_WP_INSTANCE(N) {							\
		unsigned int read_counter;						\
		unsigned int write_barrier;						\
		unsigned int wait_limit;						\
	};										\
	CK_CC_INLINE static void							\
	ck_rwcohort_wp_##N##_init(CK_RWCOHORT_WP_INSTANCE(N) *rw_cohort,		\
	    unsigned int wait_limit)							\
	{										\
											\
		rw_cohort->read_counter = 0;						\
		rw_cohort->write_barrier = 0;						\
		rw_cohort->wait_limit = wait_limit;					\
		ck_pr_barrier();							\
		return;									\
	}										\
	CK_CC_INLINE static void							\
	ck_rwcohort_wp_##N##_write_lock(CK_RWCOHORT_WP_INSTANCE(N) *rw_cohort,		\
	    CK_COHORT_INSTANCE(N) *cohort, void *global_context,			\
	    void *local_context)							\
	{										\
											\
		while (ck_pr_load_uint(&rw_cohort->write_barrier) > 0)			\
			ck_pr_stall();							\
											\
		CK_COHORT_LOCK(N, cohort, global_context, local_context);		\
											\
		while (ck_pr_load_uint(&rw_cohort->read_counter) > 0) 			\
			ck_pr_stall();							\
											\
		return;									\
	}										\
	CK_CC_INLINE static void							\
	ck_rwcohort_wp_##N##_write_unlock(CK_RWCOHORT_WP_INSTANCE(N) *rw_cohort,	\
	    CK_COHORT_INSTANCE(N) *cohort, void *global_context,			\
	    void *local_context)							\
	{										\
											\
		(void)rw_cohort;							\
		CK_COHORT_UNLOCK(N, cohort, global_context, local_context);		\
		return;									\
	}										\
	CK_CC_INLINE static void							\
	ck_rwcohort_wp_##N##_read_lock(CK_RWCOHORT_WP_INSTANCE(N) *rw_cohort,		\
	    CK_COHORT_INSTANCE(N) *cohort, void *global_context,			\
	    void *local_context)							\
	{										\
		unsigned int wait_count = 0;						\
		bool raised = false;							\
											\
		for (;;) {								\
			ck_pr_inc_uint(&rw_cohort->read_counter);			\
			ck_pr_fence_atomic_load();					\
			if (CK_COHORT_LOCKED(N, cohort, global_context,			\
			    local_context) == false)					\
				break;							\
											\
			ck_pr_dec_uint(&rw_cohort->read_counter);			\
			while (CK_COHORT_LOCKED(N, cohort, global_context,		\
			    local_context) == true) {					\
				ck_pr_stall();						\
				if (++wait_count > rw_cohort->wait_limit &&		\
				    raised == false) {					\
					ck_pr_inc_uint(&rw_cohort->write_barrier);	\
					raised = true;					\
				}							\
			}								\
		}									\
											\
		if (raised == true)							\
			ck_pr_dec_uint(&rw_cohort->write_barrier);			\
											\
		ck_pr_fence_load();							\
		return;									\
	}										\
	CK_CC_INLINE static void							\
	ck_rwcohort_wp_##N##_read_unlock(CK_RWCOHORT_WP_INSTANCE(N) *cohort)		\
	{										\
											\
		ck_pr_fence_load_atomic();						\
		ck_pr_dec_uint(&cohort->read_counter);					\
		return;									\
	}

#define CK_RWCOHORT_WP_INITIALIZER {							\
	.read_counter = 0,								\
	.write_barrier = 0,								\
	.wait_limit = 0									\
}

#define CK_RWCOHORT_RP_NAME(N) ck_rwcohort_rp_##N
#define CK_RWCOHORT_RP_INSTANCE(N) struct CK_RWCOHORT_RP_NAME(N)
#define CK_RWCOHORT_RP_INIT(N, RW, WL) ck_rwcohort_rp_##N##_init(RW, WL)
#define CK_RWCOHORT_RP_READ_LOCK(N, RW, C, GC, LC)	\
	ck_rwcohort_rp_##N##_read_lock(RW, C, GC, LC)
#define CK_RWCOHORT_RP_READ_UNLOCK(N, RW, C, GC, LC)	\
	ck_rwcohort_rp_##N##_read_unlock(RW)
#define CK_RWCOHORT_RP_WRITE_LOCK(N, RW, C, GC, LC)	\
	ck_rwcohort_rp_##N##_write_lock(RW, C, GC, LC)
#define CK_RWCOHORT_RP_WRITE_UNLOCK(N, RW, C, GC, LC)	\
	ck_rwcohort_rp_##N##_write_unlock(RW, C, GC, LC)
#define CK_RWCOHORT_RP_DEFAULT_WAIT_LIMIT 1000

#define CK_RWCOHORT_RP_PROTOTYPE(N)							\
	CK_RWCOHORT_RP_INSTANCE(N) {							\
		unsigned int read_counter;						\
		unsigned int read_barrier;						\
		unsigned int wait_limit;						\
	};										\
	CK_CC_INLINE static void							\
	ck_rwcohort_rp_##N##_init(CK_RWCOHORT_RP_INSTANCE(N) *rw_cohort,		\
	    unsigned int wait_limit)							\
	{										\
											\
		rw_cohort->read_counter = 0;						\
		rw_cohort->read_barrier = 0;						\
		rw_cohort->wait_limit = wait_limit;					\
		ck_pr_barrier();							\
		return;									\
	}										\
	CK_CC_INLINE static void							\
	ck_rwcohort_rp_##N##_write_lock(CK_RWCOHORT_RP_INSTANCE(N) *rw_cohort,		\
	    CK_COHORT_INSTANCE(N) *cohort, void *global_context,			\
	    void *local_context)							\
	{										\
		unsigned int wait_count = 0;						\
		bool raised = false;							\
											\
		for (;;) {								\
			CK_COHORT_LOCK(N, cohort, global_context, local_context);	\
			if (ck_pr_load_uint(&rw_cohort->read_counter) == 0)		\
				break;							\
											\
			CK_COHORT_UNLOCK(N, cohort, global_context, local_context);	\
			while (ck_pr_load_uint(&rw_cohort->read_counter) > 0) {		\
				ck_pr_stall();						\
				if (++wait_count > rw_cohort->wait_limit &&		\
				    raised == false) {					\
					ck_pr_inc_uint(&rw_cohort->read_barrier);	\
					raised = true;					\
				}							\
			}								\
		}									\
											\
		if (raised == true)							\
			ck_pr_dec_uint(&rw_cohort->read_barrier);			\
											\
		return;									\
	}										\
	CK_CC_INLINE static void							\
	ck_rwcohort_rp_##N##_write_unlock(CK_RWCOHORT_RP_INSTANCE(N) *rw_cohort,	\
	    CK_COHORT_INSTANCE(N) *cohort, void *global_context, void *local_context)	\
	{										\
											\
		(void)rw_cohort;							\
		CK_COHORT_UNLOCK(N, cohort, global_context, local_context);		\
		return;									\
	}										\
	CK_CC_INLINE static void							\
	ck_rwcohort_rp_##N##_read_lock(CK_RWCOHORT_RP_INSTANCE(N) *rw_cohort,		\
	    CK_COHORT_INSTANCE(N) *cohort, void *global_context,			\
	    void *local_context)							\
	{										\
											\
		while (ck_pr_load_uint(&rw_cohort->read_barrier) > 0)			\
			ck_pr_stall();							\
											\
		ck_pr_inc_uint(&rw_cohort->read_counter);				\
		ck_pr_fence_atomic_load();						\
											\
		while (CK_COHORT_LOCKED(N, cohort, global_context,			\
		    local_context) == true)						\
			ck_pr_stall();							\
											\
		return;									\
	}										\
	CK_CC_INLINE static void							\
	ck_rwcohort_rp_##N##_read_unlock(CK_RWCOHORT_RP_INSTANCE(N) *cohort)		\
	{										\
											\
		ck_pr_fence_load_atomic();						\
		ck_pr_dec_uint(&cohort->read_counter);					\
		return;									\
	}

#define CK_RWCOHORT_RP_INITIALIZER {							\
	.read_counter = 0,								\
	.read_barrier = 0,								\
	.wait_limit = 0									\
}

#define CK_RWCOHORT_NEUTRAL_NAME(N) ck_rwcohort_neutral_##N
#define CK_RWCOHORT_NEUTRAL_INSTANCE(N) struct CK_RWCOHORT_NEUTRAL_NAME(N)
#define CK_RWCOHORT_NEUTRAL_INIT(N, RW) ck_rwcohort_neutral_##N##_init(RW)
#define CK_RWCOHORT_NEUTRAL_READ_LOCK(N, RW, C, GC, LC)		\
	ck_rwcohort_neutral_##N##_read_lock(RW, C, GC, LC)
#define CK_RWCOHORT_NEUTRAL_READ_UNLOCK(N, RW, C, GC, LC)	\
	ck_rwcohort_neutral_##N##_read_unlock(RW)
#define CK_RWCOHORT_NEUTRAL_WRITE_LOCK(N, RW, C, GC, LC)	\
	ck_rwcohort_neutral_##N##_write_lock(RW, C, GC, LC)
#define CK_RWCOHORT_NEUTRAL_WRITE_UNLOCK(N, RW, C, GC, LC)	\
	ck_rwcohort_neutral_##N##_write_unlock(RW, C, GC, LC)
#define CK_RWCOHORT_NEUTRAL_DEFAULT_WAIT_LIMIT 1000

#define CK_RWCOHORT_NEUTRAL_PROTOTYPE(N)						\
	CK_RWCOHORT_NEUTRAL_INSTANCE(N) {						\
		unsigned int read_counter;						\
	};										\
	CK_CC_INLINE static void							\
	ck_rwcohort_neutral_##N##_init(CK_RWCOHORT_NEUTRAL_INSTANCE(N) *rw_cohort)	\
	{										\
											\
		rw_cohort->read_counter = 0;						\
		ck_pr_barrier();							\
		return;									\
	}										\
	CK_CC_INLINE static void							\
	ck_rwcohort_neutral_##N##_write_lock(CK_RWCOHORT_NEUTRAL_INSTANCE(N) *rw_cohort,\
	    CK_COHORT_INSTANCE(N) *cohort, void *global_context,			\
	    void *local_context)							\
	{										\
											\
		CK_COHORT_LOCK(N, cohort, global_context, local_context);		\
		while (ck_pr_load_uint(&rw_cohort->read_counter) > 0) {			\
			ck_pr_stall();							\
		}									\
		return;									\
	}										\
	CK_CC_INLINE static void							\
	ck_rwcohort_neutral_##N##_write_unlock(CK_RWCOHORT_NEUTRAL_INSTANCE(N) *rw_cohort,\
	    CK_COHORT_INSTANCE(N) *cohort, void *global_context, void *local_context)	\
	{										\
											\
		(void)rw_cohort;							\
		CK_COHORT_UNLOCK(N, cohort, global_context, local_context);		\
		return;									\
	}										\
	CK_CC_INLINE static void							\
	ck_rwcohort_neutral_##N##_read_lock(CK_RWCOHORT_NEUTRAL_INSTANCE(N) *rw_cohort,	\
	    CK_COHORT_INSTANCE(N) *cohort, void *global_context,			\
	    void *local_context)							\
	{										\
											\
		CK_COHORT_LOCK(N, cohort, global_context, local_context);		\
		ck_pr_inc_uint(&rw_cohort->read_counter);				\
		CK_COHORT_UNLOCK(N, cohort, global_context, local_context);		\
		return;									\
	}										\
	CK_CC_INLINE static void							\
	ck_rwcohort_neutral_##N##_read_unlock(CK_RWCOHORT_NEUTRAL_INSTANCE(N) *cohort)	\
	{										\
											\
		ck_pr_fence_load_atomic();						\
		ck_pr_dec_uint(&cohort->read_counter);					\
		return;									\
	}

#define CK_RWCOHORT_NEUTRAL_INITIALIZER {						\
	.read_counter = 0,								\
}

#endif /* CK_RWCOHORT_H */
