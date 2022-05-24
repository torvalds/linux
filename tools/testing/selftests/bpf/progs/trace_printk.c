// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020, Oracle and/or its affiliates.

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";

int trace_printk_ret = 0;
int trace_printk_ran = 0;

const char fmt[] = "Testing,testing %d\n";

SEC("fentry/" SYS_PREFIX "sys_nanosleep")
int sys_enter(void *ctx)
{
	trace_printk_ret = bpf_trace_printk(fmt, sizeof(fmt),
					    ++trace_printk_ran);
	return 0;
}
