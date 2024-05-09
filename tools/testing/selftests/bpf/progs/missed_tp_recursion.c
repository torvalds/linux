// SPDX-License-Identifier: GPL-2.0
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

/*
 * No tests in here, just to trigger 'bpf_fentry_test*'
 * through tracing test_run
 */
SEC("fentry/bpf_modify_return_test")
int BPF_PROG(trigger)
{
	return 0;
}

SEC("kprobe/bpf_fentry_test1")
int test1(struct pt_regs *ctx)
{
	bpf_printk("test");
	return 0;
}

SEC("tp/bpf_trace/bpf_trace_printk")
int test2(struct pt_regs *ctx)
{
	return 0;
}

SEC("tp/bpf_trace/bpf_trace_printk")
int test3(struct pt_regs *ctx)
{
	return 0;
}

SEC("tp/bpf_trace/bpf_trace_printk")
int test4(struct pt_regs *ctx)
{
	return 0;
}
