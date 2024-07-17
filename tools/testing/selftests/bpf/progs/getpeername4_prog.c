// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Google LLC */

#include "vmlinux.h"

#include <string.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>
#include <bpf/bpf_core_read.h>
#include "bpf_kfuncs.h"

#define REWRITE_ADDRESS_IP4   0xc0a801fe // 192.168.1.254
#define REWRITE_ADDRESS_PORT4 4040

SEC("cgroup/getpeername4")
int getpeername_v4_prog(struct bpf_sock_addr *ctx)
{
	ctx->user_ip4 = bpf_htonl(REWRITE_ADDRESS_IP4);
	ctx->user_port = bpf_htons(REWRITE_ADDRESS_PORT4);

	return 1;
}

char _license[] SEC("license") = "GPL";
