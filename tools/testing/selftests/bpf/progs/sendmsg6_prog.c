// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018 Facebook

#include <linux/stddef.h>
#include <linux/bpf.h>
#include <sys/socket.h>

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#include <bpf_sockopt_helpers.h>

#define SRC_REWRITE_IP6_0	0
#define SRC_REWRITE_IP6_1	0
#define SRC_REWRITE_IP6_2	0
#define SRC_REWRITE_IP6_3	6

#define DST_REWRITE_IP6_0	0
#define DST_REWRITE_IP6_1	0
#define DST_REWRITE_IP6_2	0
#define DST_REWRITE_IP6_3	1

#define DST_REWRITE_PORT6	6666

SEC("cgroup/sendmsg6")
int sendmsg_v6_prog(struct bpf_sock_addr *ctx)
{
	if (ctx->type != SOCK_DGRAM)
		return 0;

	if (!get_set_sk_priority(ctx))
		return 0;

	/* Rewrite source. */
	if (ctx->msg_src_ip6[3] == bpf_htonl(1) ||
	    ctx->msg_src_ip6[3] == bpf_htonl(0)) {
		ctx->msg_src_ip6[0] = bpf_htonl(SRC_REWRITE_IP6_0);
		ctx->msg_src_ip6[1] = bpf_htonl(SRC_REWRITE_IP6_1);
		ctx->msg_src_ip6[2] = bpf_htonl(SRC_REWRITE_IP6_2);
		ctx->msg_src_ip6[3] = bpf_htonl(SRC_REWRITE_IP6_3);
	} else {
		/* Unexpected source. Reject sendmsg. */
		return 0;
	}

	/* Rewrite destination. */
	if (ctx->user_ip6[0] == bpf_htonl(0xFACEB00C)) {
		ctx->user_ip6[0] = bpf_htonl(DST_REWRITE_IP6_0);
		ctx->user_ip6[1] = bpf_htonl(DST_REWRITE_IP6_1);
		ctx->user_ip6[2] = bpf_htonl(DST_REWRITE_IP6_2);
		ctx->user_ip6[3] = bpf_htonl(DST_REWRITE_IP6_3);

		ctx->user_port = bpf_htons(DST_REWRITE_PORT6);
	} else {
		/* Unexpected destination. Reject sendmsg. */
		return 0;
	}

	return 1;
}

char _license[] SEC("license") = "GPL";
