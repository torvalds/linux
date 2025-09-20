// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>

char _license[] SEC("license") = "GPL";

unsigned int idx;
__u8 result[4];

SEC("cgroup/getsockopt")
int child(struct bpf_sockopt *ctx)
{
	if (idx < 4)
		result[idx++] = 1;
	return 1;
}

SEC("cgroup/getsockopt")
int child_2(struct bpf_sockopt *ctx)
{
	if (idx < 4)
		result[idx++] = 2;
	return 1;
}

SEC("cgroup/getsockopt")
int parent(struct bpf_sockopt *ctx)
{
	if (idx < 4)
		result[idx++] = 3;
	return 1;
}

SEC("cgroup/getsockopt")
int parent_2(struct bpf_sockopt *ctx)
{
	if (idx < 4)
		result[idx++] = 4;
	return 1;
}
