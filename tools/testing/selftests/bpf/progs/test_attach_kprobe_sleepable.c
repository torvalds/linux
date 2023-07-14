// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2017 Facebook

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include "bpf_misc.h"

int kprobe_res = 0;

/**
 * This program will be manually made sleepable on the userspace side
 * and should thus be unattachable.
 */
SEC("kprobe/" SYS_PREFIX "sys_nanosleep")
int handle_kprobe_sleepable(struct pt_regs *ctx)
{
	kprobe_res = 1;
	return 0;
}

char _license[] SEC("license") = "GPL";
