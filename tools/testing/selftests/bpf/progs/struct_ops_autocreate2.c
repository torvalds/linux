// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

int test_1_result = 0;

SEC("?struct_ops/test_1")
int BPF_PROG(foo)
{
	test_1_result = 42;
	return 0;
}

SEC("?struct_ops/test_1")
int BPF_PROG(bar)
{
	test_1_result = 24;
	return 0;
}

struct bpf_testmod_ops {
	int (*test_1)(void);
};

SEC(".struct_ops.link")
struct bpf_testmod_ops testmod_1 = {
	.test_1 = (void *)bar
};
