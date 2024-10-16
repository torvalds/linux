// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Facebook
#include <vmlinux.h>
#include <linux/version.h>
#include <bpf/bpf_helpers.h>

struct task_struct *bpf_task_from_pid(int pid) __ksym;
void bpf_task_release(struct task_struct *p) __ksym;
int bpf_send_signal_task(struct task_struct *task, int sig, enum pid_type type, u64 value) __ksym;

__u32 sig = 0, pid = 0, status = 0, signal_thread = 0, target_pid = 0;

static __always_inline int bpf_send_signal_test(void *ctx)
{
	struct task_struct *target_task = NULL;
	int ret;
	u64 value;

	if (status != 0 || pid == 0)
		return 0;

	if ((bpf_get_current_pid_tgid() >> 32) == pid) {
		if (target_pid) {
			target_task = bpf_task_from_pid(target_pid);
			if (!target_task)
				return 0;
			value = 8;
		}

		if (signal_thread) {
			if (target_pid)
				ret = bpf_send_signal_task(target_task, sig, PIDTYPE_PID, value);
			else
				ret = bpf_send_signal_thread(sig);
		} else {
			if (target_pid)
				ret = bpf_send_signal_task(target_task, sig, PIDTYPE_TGID, value);
			else
				ret = bpf_send_signal(sig);
		}
		if (ret == 0)
			status = 1;
	}

	if (target_task)
		bpf_task_release(target_task);

	return 0;
}

SEC("tracepoint/syscalls/sys_enter_nanosleep")
int send_signal_tp(void *ctx)
{
	return bpf_send_signal_test(ctx);
}

SEC("tracepoint/sched/sched_switch")
int send_signal_tp_sched(void *ctx)
{
	return bpf_send_signal_test(ctx);
}

SEC("perf_event")
int send_signal_perf(void *ctx)
{
	return bpf_send_signal_test(ctx);
}

char __license[] SEC("license") = "GPL";
