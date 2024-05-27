// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Google LLC */

#include "vmlinux.h"

#include <string.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>
#include <bpf/bpf_core_read.h>
#include "bpf_kfuncs.h"

#define REWRITE_ADDRESS_IP6_0 0xfaceb00c
#define REWRITE_ADDRESS_IP6_1 0x12345678
#define REWRITE_ADDRESS_IP6_2 0x00000000
#define REWRITE_ADDRESS_IP6_3 0x0000abcd

#define REWRITE_ADDRESS_PORT6 6060

SEC("cgroup/getsockname6")
int getsockname_v6_prog(struct bpf_sock_addr *ctx)
{
	ctx->user_ip6[0] = bpf_htonl(REWRITE_ADDRESS_IP6_0);
	ctx->user_ip6[1] = bpf_htonl(REWRITE_ADDRESS_IP6_1);
	ctx->user_ip6[2] = bpf_htonl(REWRITE_ADDRESS_IP6_2);
	ctx->user_ip6[3] = bpf_htonl(REWRITE_ADDRESS_IP6_3);
	ctx->user_port = bpf_htons(REWRITE_ADDRESS_PORT6);

	return 1;
}

char _license[] SEC("license") = "GPL";
