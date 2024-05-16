// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

int test_1_result = 0;

SEC("struct_ops/test_1")
int BPF_PROG(test_1)
{
	test_1_result = 42;
	return 0;
}

SEC("struct_ops/test_1")
int BPF_PROG(test_2)
{
	return 0;
}

struct bpf_testmod_ops___v1 {
	int (*test_1)(void);
};

struct bpf_testmod_ops___v2 {
	int (*test_1)(void);
	int (*does_not_exist)(void);
};

SEC(".struct_ops.link")
struct bpf_testmod_ops___v1 testmod_1 = {
	.test_1 = (void *)test_1
};

SEC(".struct_ops.link")
struct bpf_testmod_ops___v2 testmod_2 = {
	.test_1 = (void *)test_1,
	.does_not_exist = (void *)test_2
};

SEC("?.struct_ops")
struct bpf_testmod_ops___v1 optional_map = {
	.test_1 = (void *)test_1,
};

SEC("?.struct_ops.link")
struct bpf_testmod_ops___v1 optional_map2 = {
	.test_1 = (void *)test_1,
};
