// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"

char LICENSE[] SEC("license") = "GPL";

int pid = 0;
int fentry_cnt = 0;
int fexit_cnt = 0;

SEC("fentry/" SYS_PREFIX "sys_nanosleep")
int nanosleep_fentry(void *ctx)
{
	if (bpf_get_current_pid_tgid() >> 32 != pid)
		return 0;

	fentry_cnt++;
	return 0;
}

SEC("fexit/" SYS_PREFIX "sys_nanosleep")
int nanosleep_fexit(void *ctx)
{
	if (bpf_get_current_pid_tgid() >> 32 != pid)
		return 0;

	fexit_cnt++;
	return 0;
}
