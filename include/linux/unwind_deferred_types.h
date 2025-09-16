/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_UNWIND_USER_DEFERRED_TYPES_H
#define _LINUX_UNWIND_USER_DEFERRED_TYPES_H

struct unwind_cache {
	unsigned long		unwind_completed;
	unsigned int		nr_entries;
	unsigned long		entries[];
};

/*
 * The unwind_task_id is a unique identifier that maps to a user space
 * stacktrace. It is generated the first time a deferred user space
 * stacktrace is requested after a task has entered the kerenl and
 * is cleared to zero when it exits. The mapped id will be a non-zero
 * number.
 *
 * To simplify the generation of the 64 bit number, 32 bits will be
 * the CPU it was generated on, and the other 32 bits will be a per
 * cpu counter that gets incremented by two every time a new identifier
 * is generated. The LSB will always be set to keep the value
 * from being zero.
 */
union unwind_task_id {
	struct {
		u32		cpu;
		u32		cnt;
	};
	u64			id;
};

struct unwind_task_info {
	unsigned long		unwind_mask;
	struct unwind_cache	*cache;
	struct callback_head	work;
	union unwind_task_id	id;
};

#endif /* _LINUX_UNWIND_USER_DEFERRED_TYPES_H */
