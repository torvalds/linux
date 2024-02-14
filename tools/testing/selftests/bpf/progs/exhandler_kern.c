// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021, Oracle and/or its affiliates. */

#include "vmlinux.h"

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

char _license[] SEC("license") = "GPL";

unsigned int exception_triggered;
int test_pid;

/* TRACE_EVENT(task_newtask,
 *         TP_PROTO(struct task_struct *p, u64 clone_flags)
 */
SEC("tp_btf/task_newtask")
int BPF_PROG(trace_task_newtask, struct task_struct *task, u64 clone_flags)
{
	int pid = bpf_get_current_pid_tgid() >> 32;
	struct callback_head *work;
	void *func;

	if (test_pid != pid)
		return 0;

	/* To verify we hit an exception we dereference task->task_works->func.
	 * If task work has been added,
	 * - task->task_works is non-NULL; and
	 * - task->task_works->func is non-NULL also (the callback function
	 *   must be specified for the task work.
	 *
	 * However, for a newly-created task, task->task_works is NULLed,
	 * so we know the exception handler triggered if task_works is
	 * NULL and func is NULL.
	 */
	work = task->task_works;
	func = work->func;
	/* Currently verifier will fail for `btf_ptr |= btf_ptr` * instruction.
	 * To workaround the issue, use barrier_var() and rewrite as below to
	 * prevent compiler from generating verifier-unfriendly code.
	 */
	barrier_var(work);
	if (work)
		return 0;
	barrier_var(func);
	if (func)
		return 0;
	exception_triggered++;
	return 0;
}
