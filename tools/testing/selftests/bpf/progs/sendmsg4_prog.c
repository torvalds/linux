// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018 Facebook

#include <linux/stddef.h>
#include <linux/bpf.h>
#include <sys/socket.h>

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#include <bpf_sockopt_helpers.h>

#define SRC1_IP4		0xAC100001U /* 172.16.0.1 */
#define SRC2_IP4		0x00000000U
#define SRC_REWRITE_IP4		0x7f000004U
#define DST_IP4			0xC0A801FEU /* 192.168.1.254 */
#define DST_REWRITE_IP4		0x7f000001U
#define DST_PORT		4040
#define DST_REWRITE_PORT4	4444

SEC("cgroup/sendmsg4")
int sendmsg_v4_prog(struct bpf_sock_addr *ctx)
{
	if (ctx->type != SOCK_DGRAM)
		return 0;

	if (!get_set_sk_priority(ctx))
		return 0;

	/* Rewrite source. */
	if (ctx->msg_src_ip4 == bpf_htonl(SRC1_IP4) ||
	    ctx->msg_src_ip4 == bpf_htonl(SRC2_IP4)) {
		ctx->msg_src_ip4 = bpf_htonl(SRC_REWRITE_IP4);
	} else {
		/* Unexpected source. Reject sendmsg. */
		return 0;
	}

	/* Rewrite destination. */
	if ((ctx->user_ip4 >> 24) == (bpf_htonl(DST_IP4) >> 24) &&
	     ctx->user_port == bpf_htons(DST_PORT)) {
		ctx->user_ip4 = bpf_htonl(DST_REWRITE_IP4);
		ctx->user_port = bpf_htons(DST_REWRITE_PORT4);
	} else {
		/* Unexpected source. Reject sendmsg. */
		return 0;
	}

	return 1;
}

SEC("cgroup/sendmsg4")
int sendmsg_v4_deny_prog(struct bpf_sock_addr *ctx)
{
	return 0;
}

char _license[] SEC("license") = "GPL";
