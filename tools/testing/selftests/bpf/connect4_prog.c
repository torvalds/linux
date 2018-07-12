// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018 Facebook

#include <string.h>

#include <linux/stddef.h>
#include <linux/bpf.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <sys/socket.h>

#include "bpf_helpers.h"
#include "bpf_endian.h"

#define SRC_REWRITE_IP4		0x7f000004U
#define DST_REWRITE_IP4		0x7f000001U
#define DST_REWRITE_PORT4	4444

int _version SEC("version") = 1;

SEC("cgroup/connect4")
int connect_v4_prog(struct bpf_sock_addr *ctx)
{
	struct sockaddr_in sa;

	/* Rewrite destination. */
	ctx->user_ip4 = bpf_htonl(DST_REWRITE_IP4);
	ctx->user_port = bpf_htons(DST_REWRITE_PORT4);

	if (ctx->type == SOCK_DGRAM || ctx->type == SOCK_STREAM) {
		///* Rewrite source. */
		memset(&sa, 0, sizeof(sa));

		sa.sin_family = AF_INET;
		sa.sin_port = bpf_htons(0);
		sa.sin_addr.s_addr = bpf_htonl(SRC_REWRITE_IP4);

		if (bpf_bind(ctx, (struct sockaddr *)&sa, sizeof(sa)) != 0)
			return 0;
	}

	return 1;
}

char _license[] SEC("license") = "GPL";
