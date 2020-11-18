// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020, Oracle and/or its affiliates.

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

int trace_printk_ret = 0;
int trace_printk_ran = 0;

SEC("tp/raw_syscalls/sys_enter")
int sys_enter(void *ctx)
{
	static const char fmt[] = "testing,testing %d\n";

	trace_printk_ret = bpf_trace_printk(fmt, sizeof(fmt),
					    ++trace_printk_ran);
	return 0;
}
