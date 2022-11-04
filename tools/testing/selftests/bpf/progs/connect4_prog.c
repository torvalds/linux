// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018 Facebook

#include <string.h>

#include <linux/stddef.h>
#include <linux/bpf.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/tcp.h>
#include <linux/if.h>
#include <errno.h>

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#include "bpf_tcp_helpers.h"

#define SRC_REWRITE_IP4		0x7f000004U
#define DST_REWRITE_IP4		0x7f000001U
#define DST_REWRITE_PORT4	4444

#ifndef TCP_CA_NAME_MAX
#define TCP_CA_NAME_MAX 16
#endif

#ifndef TCP_NOTSENT_LOWAT
#define TCP_NOTSENT_LOWAT 25
#endif

#ifndef IFNAMSIZ
#define IFNAMSIZ 16
#endif

__attribute__ ((noinline))
int do_bind(struct bpf_sock_addr *ctx)
{
	struct sockaddr_in sa = {};

	sa.sin_family = AF_INET;
	sa.sin_port = bpf_htons(0);
	sa.sin_addr.s_addr = bpf_htonl(SRC_REWRITE_IP4);

	if (bpf_bind(ctx, (struct sockaddr *)&sa, sizeof(sa)) != 0)
		return 0;

	return 1;
}

static __inline int verify_cc(struct bpf_sock_addr *ctx,
			      char expected[TCP_CA_NAME_MAX])
{
	char buf[TCP_CA_NAME_MAX];
	int i;

	if (bpf_getsockopt(ctx, SOL_TCP, TCP_CONGESTION, &buf, sizeof(buf)))
		return 1;

	for (i = 0; i < TCP_CA_NAME_MAX; i++) {
		if (buf[i] != expected[i])
			return 1;
		if (buf[i] == 0)
			break;
	}

	return 0;
}

static __inline int set_cc(struct bpf_sock_addr *ctx)
{
	char reno[TCP_CA_NAME_MAX] = "reno";
	char cubic[TCP_CA_NAME_MAX] = "cubic";

	if (bpf_setsockopt(ctx, SOL_TCP, TCP_CONGESTION, &reno, sizeof(reno)))
		return 1;
	if (verify_cc(ctx, reno))
		return 1;

	if (bpf_setsockopt(ctx, SOL_TCP, TCP_CONGESTION, &cubic, sizeof(cubic)))
		return 1;
	if (verify_cc(ctx, cubic))
		return 1;

	return 0;
}

static __inline int bind_to_device(struct bpf_sock_addr *ctx)
{
	char veth1[IFNAMSIZ] = "test_sock_addr1";
	char veth2[IFNAMSIZ] = "test_sock_addr2";
	char missing[IFNAMSIZ] = "nonexistent_dev";
	char del_bind[IFNAMSIZ] = "";

	if (bpf_setsockopt(ctx, SOL_SOCKET, SO_BINDTODEVICE,
				&veth1, sizeof(veth1)))
		return 1;
	if (bpf_setsockopt(ctx, SOL_SOCKET, SO_BINDTODEVICE,
				&veth2, sizeof(veth2)))
		return 1;
	if (bpf_setsockopt(ctx, SOL_SOCKET, SO_BINDTODEVICE,
				&missing, sizeof(missing)) != -ENODEV)
		return 1;
	if (bpf_setsockopt(ctx, SOL_SOCKET, SO_BINDTODEVICE,
				&del_bind, sizeof(del_bind)))
		return 1;

	return 0;
}

static __inline int set_keepalive(struct bpf_sock_addr *ctx)
{
	int zero = 0, one = 1;

	if (bpf_setsockopt(ctx, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one)))
		return 1;
	if (ctx->type == SOCK_STREAM) {
		if (bpf_setsockopt(ctx, SOL_TCP, TCP_KEEPIDLE, &one, sizeof(one)))
			return 1;
		if (bpf_setsockopt(ctx, SOL_TCP, TCP_KEEPINTVL, &one, sizeof(one)))
			return 1;
		if (bpf_setsockopt(ctx, SOL_TCP, TCP_KEEPCNT, &one, sizeof(one)))
			return 1;
		if (bpf_setsockopt(ctx, SOL_TCP, TCP_SYNCNT, &one, sizeof(one)))
			return 1;
		if (bpf_setsockopt(ctx, SOL_TCP, TCP_USER_TIMEOUT, &one, sizeof(one)))
			return 1;
	}
	if (bpf_setsockopt(ctx, SOL_SOCKET, SO_KEEPALIVE, &zero, sizeof(zero)))
		return 1;

	return 0;
}

static __inline int set_notsent_lowat(struct bpf_sock_addr *ctx)
{
	int lowat = 65535;

	if (ctx->type == SOCK_STREAM) {
		if (bpf_setsockopt(ctx, SOL_TCP, TCP_NOTSENT_LOWAT, &lowat, sizeof(lowat)))
			return 1;
	}

	return 0;
}

SEC("cgroup/connect4")
int connect_v4_prog(struct bpf_sock_addr *ctx)
{
	struct bpf_sock_tuple tuple = {};
	struct bpf_sock *sk;

	/* Verify that new destination is available. */
	memset(&tuple.ipv4.saddr, 0, sizeof(tuple.ipv4.saddr));
	memset(&tuple.ipv4.sport, 0, sizeof(tuple.ipv4.sport));

	tuple.ipv4.daddr = bpf_htonl(DST_REWRITE_IP4);
	tuple.ipv4.dport = bpf_htons(DST_REWRITE_PORT4);

	/* Bind to device and unbind it. */
	if (bind_to_device(ctx))
		return 0;

	if (set_keepalive(ctx))
		return 0;

	if (set_notsent_lowat(ctx))
		return 0;

	if (ctx->type != SOCK_STREAM && ctx->type != SOCK_DGRAM)
		return 0;
	else if (ctx->type == SOCK_STREAM)
		sk = bpf_sk_lookup_tcp(ctx, &tuple, sizeof(tuple.ipv4),
				       BPF_F_CURRENT_NETNS, 0);
	else
		sk = bpf_sk_lookup_udp(ctx, &tuple, sizeof(tuple.ipv4),
				       BPF_F_CURRENT_NETNS, 0);

	if (!sk)
		return 0;

	if (sk->src_ip4 != tuple.ipv4.daddr ||
	    sk->src_port != DST_REWRITE_PORT4) {
		bpf_sk_release(sk);
		return 0;
	}

	bpf_sk_release(sk);

	/* Rewrite congestion control. */
	if (ctx->type == SOCK_STREAM && set_cc(ctx))
		return 0;

	/* Rewrite destination. */
	ctx->user_ip4 = bpf_htonl(DST_REWRITE_IP4);
	ctx->user_port = bpf_htons(DST_REWRITE_PORT4);

	return do_bind(ctx) ? 1 : 0;
}

char _license[] SEC("license") = "GPL";
