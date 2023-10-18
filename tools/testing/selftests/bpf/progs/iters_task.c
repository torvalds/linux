// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023 Chuyi Zhou <zhouchuyi@bytedance.com> */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"
#include "bpf_experimental.h"

char _license[] SEC("license") = "GPL";

pid_t target_pid;
int procs_cnt, threads_cnt, proc_threads_cnt;

void bpf_rcu_read_lock(void) __ksym;
void bpf_rcu_read_unlock(void) __ksym;

SEC("fentry.s/" SYS_PREFIX "sys_getpgid")
int iter_task_for_each_sleep(void *ctx)
{
	struct task_struct *cur_task = bpf_get_current_task_btf();
	struct task_struct *pos;

	if (cur_task->pid != target_pid)
		return 0;
	procs_cnt = threads_cnt = proc_threads_cnt = 0;

	bpf_rcu_read_lock();
	bpf_for_each(task, pos, NULL, BPF_TASK_ITER_ALL_PROCS)
		if (pos->pid == target_pid)
			procs_cnt++;

	bpf_for_each(task, pos, cur_task, BPF_TASK_ITER_PROC_THREADS)
		proc_threads_cnt++;

	bpf_for_each(task, pos, NULL, BPF_TASK_ITER_ALL_THREADS)
		if (pos->tgid == target_pid)
			threads_cnt++;
	bpf_rcu_read_unlock();
	return 0;
}
