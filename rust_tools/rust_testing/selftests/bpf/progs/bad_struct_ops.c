// SPDX-License-Identifier: GPL-2.0

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "../test_kmods/bpf_testmod.h"

char _license[] SEC("license") = "GPL";

SEC("struct_ops/test_1")
int BPF_PROG(test_1) { return 0; }

SEC("struct_ops/test_2")
int BPF_PROG(test_2) { return 0; }

SEC(".struct_ops.link")
struct bpf_testmod_ops testmod_1 = {
	.test_1 = (void *)test_1,
	.test_2 = (void *)test_2
};

SEC(".struct_ops.link")
struct bpf_testmod_ops2 testmod_2 = {
	.test_1 = (void *)test_1
};
