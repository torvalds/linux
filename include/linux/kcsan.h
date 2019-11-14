/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _LINUX_KCSAN_H
#define _LINUX_KCSAN_H

#include <linux/kcsan-checks.h>
#include <linux/types.h>

#ifdef CONFIG_KCSAN

/*
 * Context for each thread of execution: for tasks, this is stored in
 * task_struct, and interrupts access internal per-CPU storage.
 */
struct kcsan_ctx {
	int disable_count; /* disable counter */
	int atomic_next; /* number of following atomic ops */

	/*
	 * We distinguish between: (a) nestable atomic regions that may contain
	 * other nestable regions; and (b) flat atomic regions that do not keep
	 * track of nesting. Both (a) and (b) are entirely independent of each
	 * other, and a flat region may be started in a nestable region or
	 * vice-versa.
	 *
	 * This is required because, for example, in the annotations for
	 * seqlocks, we declare seqlock writer critical sections as (a) nestable
	 * atomic regions, but reader critical sections as (b) flat atomic
	 * regions, but have encountered cases where seqlock reader critical
	 * sections are contained within writer critical sections (the opposite
	 * may be possible, too).
	 *
	 * To support these cases, we independently track the depth of nesting
	 * for (a), and whether the leaf level is flat for (b).
	 */
	int atomic_nest_count;
	bool in_flat_atomic;
};

/**
 * kcsan_init - initialize KCSAN runtime
 */
void kcsan_init(void);

/**
 * kcsan_disable_current - disable KCSAN for the current context
 *
 * Supports nesting.
 */
void kcsan_disable_current(void);

/**
 * kcsan_enable_current - re-enable KCSAN for the current context
 *
 * Supports nesting.
 */
void kcsan_enable_current(void);

/**
 * kcsan_nestable_atomic_begin - begin nestable atomic region
 *
 * Accesses within the atomic region may appear to race with other accesses but
 * should be considered atomic.
 */
void kcsan_nestable_atomic_begin(void);

/**
 * kcsan_nestable_atomic_end - end nestable atomic region
 */
void kcsan_nestable_atomic_end(void);

/**
 * kcsan_flat_atomic_begin - begin flat atomic region
 *
 * Accesses within the atomic region may appear to race with other accesses but
 * should be considered atomic.
 */
void kcsan_flat_atomic_begin(void);

/**
 * kcsan_flat_atomic_end - end flat atomic region
 */
void kcsan_flat_atomic_end(void);

/**
 * kcsan_atomic_next - consider following accesses as atomic
 *
 * Force treating the next n memory accesses for the current context as atomic
 * operations.
 *
 * @n number of following memory accesses to treat as atomic.
 */
void kcsan_atomic_next(int n);

#else /* CONFIG_KCSAN */

static inline void kcsan_init(void) { }

static inline void kcsan_disable_current(void) { }

static inline void kcsan_enable_current(void) { }

static inline void kcsan_nestable_atomic_begin(void) { }

static inline void kcsan_nestable_atomic_end(void) { }

static inline void kcsan_flat_atomic_begin(void) { }

static inline void kcsan_flat_atomic_end(void) { }

static inline void kcsan_atomic_next(int n) { }

#endif /* CONFIG_KCSAN */

#endif /* _LINUX_KCSAN_H */
