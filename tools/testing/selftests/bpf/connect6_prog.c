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

#define SRC_REWRITE_IP6_0	0
#define SRC_REWRITE_IP6_1	0
#define SRC_REWRITE_IP6_2	0
#define SRC_REWRITE_IP6_3	6

#define DST_REWRITE_IP6_0	0
#define DST_REWRITE_IP6_1	0
#define DST_REWRITE_IP6_2	0
#define DST_REWRITE_IP6_3	1

#define DST_REWRITE_PORT6	6666

int _version SEC("version") = 1;

SEC("cgroup/connect6")
int connect_v6_prog(struct bpf_sock_addr *ctx)
{
	struct sockaddr_in6 sa;

	/* Rewrite destination. */
	ctx->user_ip6[0] = bpf_htonl(DST_REWRITE_IP6_0);
	ctx->user_ip6[1] = bpf_htonl(DST_REWRITE_IP6_1);
	ctx->user_ip6[2] = bpf_htonl(DST_REWRITE_IP6_2);
	ctx->user_ip6[3] = bpf_htonl(DST_REWRITE_IP6_3);

	ctx->user_port = bpf_htons(DST_REWRITE_PORT6);

	if (ctx->type == SOCK_DGRAM || ctx->type == SOCK_STREAM) {
		/* Rewrite source. */
		memset(&sa, 0, sizeof(sa));

		sa.sin6_family = AF_INET6;
		sa.sin6_port = bpf_htons(0);

		sa.sin6_addr.s6_addr32[0] = bpf_htonl(SRC_REWRITE_IP6_0);
		sa.sin6_addr.s6_addr32[1] = bpf_htonl(SRC_REWRITE_IP6_1);
		sa.sin6_addr.s6_addr32[2] = bpf_htonl(SRC_REWRITE_IP6_2);
		sa.sin6_addr.s6_addr32[3] = bpf_htonl(SRC_REWRITE_IP6_3);

		if (bpf_bind(ctx, (struct sockaddr *)&sa, sizeof(sa)) != 0)
			return 0;
	}

	return 1;
}

char _license[] SEC("license") = "GPL";
