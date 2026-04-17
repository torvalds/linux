// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020, Oracle and/or its affiliates.

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";

int trace_printk_ret = 0;
int trace_printk_ran = 0;
int trace_printk_invalid_spec_ret = 0;
int trace_printk_utf8_ret = 0;
int trace_printk_utf8_ran = 0;

const char fmt[] = "Testing,testing %d\n";
static const char utf8_fmt[] = "中文,测试 %d\n";
/* Non-ASCII bytes after '%' must still be rejected. */
static const char invalid_spec_fmt[] = "%\x80\n";

SEC("fentry/" SYS_PREFIX "sys_nanosleep")
int sys_enter(void *ctx)
{
	trace_printk_ret = bpf_trace_printk(fmt, sizeof(fmt),
					    ++trace_printk_ran);
	trace_printk_utf8_ret = bpf_trace_printk(utf8_fmt, sizeof(utf8_fmt),
						 ++trace_printk_utf8_ran);
	trace_printk_invalid_spec_ret = bpf_trace_printk(invalid_spec_fmt,
							 sizeof(invalid_spec_fmt));
	return 0;
}
