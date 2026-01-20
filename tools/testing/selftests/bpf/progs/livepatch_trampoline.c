// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

int fentry_hit;
int fexit_hit;
int my_pid;

SEC("fentry/cmdline_proc_show")
int BPF_PROG(fentry_cmdline)
{
	if (my_pid != (bpf_get_current_pid_tgid() >> 32))
		return 0;

	fentry_hit = 1;
	return 0;
}

SEC("fexit/cmdline_proc_show")
int BPF_PROG(fexit_cmdline)
{
	if (my_pid != (bpf_get_current_pid_tgid() >> 32))
		return 0;

	fexit_hit = 1;
	return 0;
}
