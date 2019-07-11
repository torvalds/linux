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
	struct bpf_sock_tuple tuple = {};
	struct sockaddr_in6 sa;
	struct bpf_sock *sk;

	/* Verify that new destination is available. */
	memset(&tuple.ipv6.saddr, 0, sizeof(tuple.ipv6.saddr));
	memset(&tuple.ipv6.sport, 0, sizeof(tuple.ipv6.sport));

	tuple.ipv6.daddr[0] = bpf_htonl(DST_REWRITE_IP6_0);
	tuple.ipv6.daddr[1] = bpf_htonl(DST_REWRITE_IP6_1);
	tuple.ipv6.daddr[2] = bpf_htonl(DST_REWRITE_IP6_2);
	tuple.ipv6.daddr[3] = bpf_htonl(DST_REWRITE_IP6_3);

	tuple.ipv6.dport = bpf_htons(DST_REWRITE_PORT6);

	if (ctx->type != SOCK_STREAM && ctx->type != SOCK_DGRAM)
		return 0;
	else if (ctx->type == SOCK_STREAM)
		sk = bpf_sk_lookup_tcp(ctx, &tuple, sizeof(tuple.ipv6),
				       BPF_F_CURRENT_NETNS, 0);
	else
		sk = bpf_sk_lookup_udp(ctx, &tuple, sizeof(tuple.ipv6),
				       BPF_F_CURRENT_NETNS, 0);

	if (!sk)
		return 0;

	if (sk->src_ip6[0] != tuple.ipv6.daddr[0] ||
	    sk->src_ip6[1] != tuple.ipv6.daddr[1] ||
	    sk->src_ip6[2] != tuple.ipv6.daddr[2] ||
	    sk->src_ip6[3] != tuple.ipv6.daddr[3] ||
	    sk->src_port != DST_REWRITE_PORT6) {
		bpf_sk_release(sk);
		return 0;
	}

	bpf_sk_release(sk);

	/* Rewrite destination. */
	ctx->user_ip6[0] = bpf_htonl(DST_REWRITE_IP6_0);
	ctx->user_ip6[1] = bpf_htonl(DST_REWRITE_IP6_1);
	ctx->user_ip6[2] = bpf_htonl(DST_REWRITE_IP6_2);
	ctx->user_ip6[3] = bpf_htonl(DST_REWRITE_IP6_3);

	ctx->user_port = bpf_htons(DST_REWRITE_PORT6);

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

	return 1;
}

char _license[] SEC("license") = "GPL";
