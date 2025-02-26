// SPDX-License-Identifier: GPL-2.0
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "../test_kmods/bpf_testmod_kfunc.h"

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

SEC("kprobe.multi/bpf_fentry_test1")
int test1(struct pt_regs *ctx)
{
	bpf_kfunc_common_test();
	return 0;
}

SEC("kprobe/bpf_kfunc_common_test")
int test2(struct pt_regs *ctx)
{
	return 0;
}

SEC("kprobe/bpf_kfunc_common_test")
int test3(struct pt_regs *ctx)
{
	return 0;
}

SEC("kprobe/bpf_kfunc_common_test")
int test4(struct pt_regs *ctx)
{
	return 0;
}

SEC("kprobe.multi/bpf_kfunc_common_test")
int test5(struct pt_regs *ctx)
{
	return 0;
}

SEC("kprobe.session/bpf_kfunc_common_test")
int test6(struct pt_regs *ctx)
{
	return 0;
}
