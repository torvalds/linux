/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The Kernel Concurrency Sanitizer (KCSAN) infrastructure. Public interface and
 * data structures to set up runtime. See kcsan-checks.h for explicit checks and
 * modifiers. For more info please see Documentation/dev-tools/kcsan.rst.
 *
 * Copyright (C) 2019, Google LLC.
 */

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

	/*
	 * Access mask for all accesses if non-zero.
	 */
	unsigned long access_mask;

	/* List of scoped accesses. */
	struct list_head scoped_accesses;
};

/**
 * kcsan_init - initialize KCSAN runtime
 */
void kcsan_init(void);

#else /* CONFIG_KCSAN */

static inline void kcsan_init(void)			{ }

#endif /* CONFIG_KCSAN */

#endif /* _LINUX_KCSAN_H */
