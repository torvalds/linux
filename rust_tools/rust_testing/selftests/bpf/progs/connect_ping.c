// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright 2022 Google LLC.
 */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>
#include <netinet/in.h>
#include <sys/socket.h>

/* 2001:db8::1 */
#define BINDADDR_V6 { { { 0x20,0x01,0x0d,0xb8,0,0,0,0,0,0,0,0,0,0,0,1 } } }

__u32 do_bind = 0;
__u32 has_error = 0;
__u32 invocations_v4 = 0;
__u32 invocations_v6 = 0;

SEC("cgroup/connect4")
int connect_v4_prog(struct bpf_sock_addr *ctx)
{
	struct sockaddr_in sa = {
		.sin_family = AF_INET,
		.sin_addr.s_addr = bpf_htonl(0x01010101),
	};

	__sync_fetch_and_add(&invocations_v4, 1);

	if (do_bind && bpf_bind(ctx, (struct sockaddr *)&sa, sizeof(sa)))
		has_error = 1;

	return 1;
}

SEC("cgroup/connect6")
int connect_v6_prog(struct bpf_sock_addr *ctx)
{
	struct sockaddr_in6 sa = {
		.sin6_family = AF_INET6,
		.sin6_addr = BINDADDR_V6,
	};

	__sync_fetch_and_add(&invocations_v6, 1);

	if (do_bind && bpf_bind(ctx, (struct sockaddr *)&sa, sizeof(sa)))
		has_error = 1;

	return 1;
}

char _license[] SEC("license") = "GPL";
