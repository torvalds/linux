// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Google LLC */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf_sockopt_helpers.h>
#include "bpf_misc.h"

SEC("cgroup/recvmsg4")
__success
int recvmsg4_good_return_code(struct bpf_sock_addr *ctx)
{
	return 1;
}

SEC("cgroup/recvmsg4")
__failure __msg("At program exit the register R0 has smin=0 smax=0 should have been in [1, 1]")
int recvmsg4_bad_return_code(struct bpf_sock_addr *ctx)
{
	return 0;
}

SEC("cgroup/recvmsg6")
__success
int recvmsg6_good_return_code(struct bpf_sock_addr *ctx)
{
	return 1;
}

SEC("cgroup/recvmsg6")
__failure __msg("At program exit the register R0 has smin=0 smax=0 should have been in [1, 1]")
int recvmsg6_bad_return_code(struct bpf_sock_addr *ctx)
{
	return 0;
}

char _license[] SEC("license") = "GPL";
