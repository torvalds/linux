// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

extern const void bpf_fentry_test1 __ksym;
extern const void bpf_fentry_test2 __ksym;
extern const void bpf_fentry_test3 __ksym;
extern const void bpf_fentry_test4 __ksym;
extern const void bpf_modify_return_test __ksym;
extern const void bpf_fentry_test6 __ksym;

__u64 test1_result = 0;
SEC("fentry/bpf_fentry_test1")
int BPF_PROG(test1, int a)
{
	__u64 addr = bpf_get_func_ip(ctx);

	test1_result = (const void *) addr == &bpf_fentry_test1;
	return 0;
}

__u64 test2_result = 0;
SEC("fexit/bpf_fentry_test2")
int BPF_PROG(test2, int a)
{
	__u64 addr = bpf_get_func_ip(ctx);

	test2_result = (const void *) addr == &bpf_fentry_test2;
	return 0;
}

__u64 test3_result = 0;
SEC("kprobe/bpf_fentry_test3")
int test3(struct pt_regs *ctx)
{
	__u64 addr = bpf_get_func_ip(ctx);

	test3_result = (const void *) addr == &bpf_fentry_test3;
	return 0;
}

__u64 test4_result = 0;
SEC("kretprobe/bpf_fentry_test4")
int BPF_KRETPROBE(test4)
{
	__u64 addr = bpf_get_func_ip(ctx);

	test4_result = (const void *) addr == &bpf_fentry_test4;
	return 0;
}

__u64 test5_result = 0;
SEC("fmod_ret/bpf_modify_return_test")
int BPF_PROG(test5, int a, int *b, int ret)
{
	__u64 addr = bpf_get_func_ip(ctx);

	test5_result = (const void *) addr == &bpf_modify_return_test;
	return ret;
}

__u64 test6_result = 0;
SEC("kprobe/bpf_fentry_test6+0x5")
int test6(struct pt_regs *ctx)
{
	__u64 addr = bpf_get_func_ip(ctx);

	test6_result = (const void *) addr == &bpf_fentry_test6 + 5;
	return 0;
}
