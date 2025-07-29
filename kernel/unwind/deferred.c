// SPDX-License-Identifier: GPL-2.0
/*
 * Deferred user space unwinding
 */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/unwind_deferred.h>

#define UNWIND_MAX_ENTRIES 512

/**
 * unwind_user_faultable - Produce a user stacktrace in faultable context
 * @trace: The descriptor that will store the user stacktrace
 *
 * This must be called in a known faultable context (usually when entering
 * or exiting user space). Depending on the available implementations
 * the @trace will be loaded with the addresses of the user space stacktrace
 * if it can be found.
 *
 * Return: 0 on success and negative on error
 *         On success @trace will contain the user space stacktrace
 */
int unwind_user_faultable(struct unwind_stacktrace *trace)
{
	struct unwind_task_info *info = &current->unwind_info;

	/* Should always be called from faultable context */
	might_fault();

	if (current->flags & PF_EXITING)
		return -EINVAL;

	if (!info->entries) {
		info->entries = kmalloc_array(UNWIND_MAX_ENTRIES, sizeof(long),
					      GFP_KERNEL);
		if (!info->entries)
			return -ENOMEM;
	}

	trace->nr = 0;
	trace->entries = info->entries;
	unwind_user(trace, UNWIND_MAX_ENTRIES);

	return 0;
}

void unwind_task_init(struct task_struct *task)
{
	struct unwind_task_info *info = &task->unwind_info;

	memset(info, 0, sizeof(*info));
}

void unwind_task_free(struct task_struct *task)
{
	struct unwind_task_info *info = &task->unwind_info;

	kfree(info->entries);
}
