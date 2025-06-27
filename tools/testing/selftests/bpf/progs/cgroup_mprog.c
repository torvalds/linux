// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>

char _license[] SEC("license") = "GPL";

SEC("cgroup/getsockopt")
int getsockopt_1(struct bpf_sockopt *ctx)
{
	return 1;
}

SEC("cgroup/getsockopt")
int getsockopt_2(struct bpf_sockopt *ctx)
{
	return 1;
}

SEC("cgroup/getsockopt")
int getsockopt_3(struct bpf_sockopt *ctx)
{
	return 1;
}

SEC("cgroup/getsockopt")
int getsockopt_4(struct bpf_sockopt *ctx)
{
	return 1;
}
