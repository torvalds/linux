// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 VMware Inc, Steven Rostedt <rostedt@goodmis.org>
 */
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include "trace.h"

/**
 * trace_pid_list_is_set - test if the pid is set in the list
 * @pid_list: The pid list to test
 * @pid: The pid to to see if set in the list.
 *
 * Tests if @pid is is set in the @pid_list. This is usually called
 * from the scheduler when a task is scheduled. Its pid is checked
 * if it should be traced or not.
 *
 * Return true if the pid is in the list, false otherwise.
 */
bool trace_pid_list_is_set(struct trace_pid_list *pid_list, unsigned int pid)
{
	/*
	 * If pid_max changed after filtered_pids was created, we
	 * by default ignore all pids greater than the previous pid_max.
	 */
	if (pid >= pid_list->pid_max)
		return false;

	return test_bit(pid, pid_list->pids);
}

/**
 * trace_pid_list_set - add a pid to the list
 * @pid_list: The pid list to add the @pid to.
 * @pid: The pid to add.
 *
 * Adds @pid to @pid_list. This is usually done explicitly by a user
 * adding a task to be traced, or indirectly by the fork function
 * when children should be traced and a task's pid is in the list.
 *
 * Return 0 on success, negative otherwise.
 */
int trace_pid_list_set(struct trace_pid_list *pid_list, unsigned int pid)
{
	/* Sorry, but we don't support pid_max changing after setting */
	if (pid >= pid_list->pid_max)
		return -EINVAL;

	set_bit(pid, pid_list->pids);

	return 0;
}

/**
 * trace_pid_list_clear - remove a pid from the list
 * @pid_list: The pid list to remove the @pid from.
 * @pid: The pid to remove.
 *
 * Removes @pid from @pid_list. This is usually done explicitly by a user
 * removing tasks from tracing, or indirectly by the exit function
 * when a task that is set to be traced exits.
 *
 * Return 0 on success, negative otherwise.
 */
int trace_pid_list_clear(struct trace_pid_list *pid_list, unsigned int pid)
{
	/* Sorry, but we don't support pid_max changing after setting */
	if (pid >= pid_list->pid_max)
		return -EINVAL;

	clear_bit(pid, pid_list->pids);

	return 0;
}

/**
 * trace_pid_list_next - return the next pid in the list
 * @pid_list: The pid list to examine.
 * @pid: The pid to start from
 * @next: The pointer to place the pid that is set starting from @pid.
 *
 * Looks for the next consecutive pid that is in @pid_list starting
 * at the pid specified by @pid. If one is set (including @pid), then
 * that pid is placed into @next.
 *
 * Return 0 when a pid is found, -1 if there are no more pids included.
 */
int trace_pid_list_next(struct trace_pid_list *pid_list, unsigned int pid,
			unsigned int *next)
{
	pid = find_next_bit(pid_list->pids, pid_list->pid_max, pid);

	if (pid < pid_list->pid_max) {
		*next = pid;
		return 0;
	}
	return -1;
}

/**
 * trace_pid_list_first - return the first pid in the list
 * @pid_list: The pid list to examine.
 * @pid: The pointer to place the pid first found pid that is set.
 *
 * Looks for the first pid that is set in @pid_list, and places it
 * into @pid if found.
 *
 * Return 0 when a pid is found, -1 if there are no pids set.
 */
int trace_pid_list_first(struct trace_pid_list *pid_list, unsigned int *pid)
{
	unsigned int first;

	first = find_first_bit(pid_list->pids, pid_list->pid_max);

	if (first < pid_list->pid_max) {
		*pid = first;
		return 0;
	}
	return -1;
}

/**
 * trace_pid_list_alloc - create a new pid_list
 *
 * Allocates a new pid_list to store pids into.
 *
 * Returns the pid_list on success, NULL otherwise.
 */
struct trace_pid_list *trace_pid_list_alloc(void)
{
	struct trace_pid_list *pid_list;

	pid_list = kmalloc(sizeof(*pid_list), GFP_KERNEL);
	if (!pid_list)
		return NULL;

	pid_list->pid_max = READ_ONCE(pid_max);

	pid_list->pids = vzalloc((pid_list->pid_max + 7) >> 3);
	if (!pid_list->pids) {
		kfree(pid_list);
		return NULL;
	}
	return pid_list;
}

/**
 * trace_pid_list_free - Frees an allocated pid_list.
 *
 * Frees the memory for a pid_list that was allocated.
 */
void trace_pid_list_free(struct trace_pid_list *pid_list)
{
	if (!pid_list)
		return;

	vfree(pid_list->pids);
	kfree(pid_list);
}
