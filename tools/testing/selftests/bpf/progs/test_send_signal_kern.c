// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Facebook
#include <linux/bpf.h>
#include <linux/version.h>
#include <bpf/bpf_helpers.h>

__u32 sig = 0, pid = 0, status = 0, signal_thread = 0;

static __always_inline int bpf_send_signal_test(void *ctx)
{
	int ret;

	if (status != 0 || pid == 0)
		return 0;

	if ((bpf_get_current_pid_tgid() >> 32) == pid) {
		if (signal_thread)
			ret = bpf_send_signal_thread(sig);
		else
			ret = bpf_send_signal(sig);
		if (ret == 0)
			status = 1;
	}

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
