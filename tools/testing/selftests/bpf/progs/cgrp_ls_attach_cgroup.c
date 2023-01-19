// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_tracing_net.h"

char _license[] SEC("license") = "GPL";

struct socket_cookie {
	__u64 cookie_key;
	__u64 cookie_value;
};

struct {
	__uint(type, BPF_MAP_TYPE_CGRP_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, struct socket_cookie);
} socket_cookies SEC(".maps");

SEC("cgroup/connect6")
int set_cookie(struct bpf_sock_addr *ctx)
{
	struct socket_cookie *p;
	struct tcp_sock *tcp_sk;
	struct bpf_sock *sk;

	if (ctx->family != AF_INET6 || ctx->user_family != AF_INET6)
		return 1;

	sk = ctx->sk;
	if (!sk)
		return 1;

	tcp_sk = bpf_skc_to_tcp_sock(sk);
	if (!tcp_sk)
		return 1;

	p = bpf_cgrp_storage_get(&socket_cookies,
		tcp_sk->inet_conn.icsk_inet.sk.sk_cgrp_data.cgroup, 0,
		BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (!p)
		return 1;

	p->cookie_value = 0xF;
	p->cookie_key = bpf_get_socket_cookie(ctx);
	return 1;
}

SEC("sockops")
int update_cookie_sockops(struct bpf_sock_ops *ctx)
{
	struct socket_cookie *p;
	struct tcp_sock *tcp_sk;
	struct bpf_sock *sk;

	if (ctx->family != AF_INET6 || ctx->op != BPF_SOCK_OPS_TCP_CONNECT_CB)
		return 1;

	sk = ctx->sk;
	if (!sk)
		return 1;

	tcp_sk = bpf_skc_to_tcp_sock(sk);
	if (!tcp_sk)
		return 1;

	p = bpf_cgrp_storage_get(&socket_cookies,
		tcp_sk->inet_conn.icsk_inet.sk.sk_cgrp_data.cgroup, 0, 0);
	if (!p)
		return 1;

	if (p->cookie_key != bpf_get_socket_cookie(ctx))
		return 1;

	p->cookie_value |= (ctx->local_port << 8);
	return 1;
}

SEC("fexit/inet_stream_connect")
int BPF_PROG(update_cookie_tracing, struct socket *sock,
	     struct sockaddr *uaddr, int addr_len, int flags)
{
	struct socket_cookie *p;
	struct tcp_sock *tcp_sk;

	if (uaddr->sa_family != AF_INET6)
		return 0;

	p = bpf_cgrp_storage_get(&socket_cookies, sock->sk->sk_cgrp_data.cgroup, 0, 0);
	if (!p)
		return 0;

	if (p->cookie_key != bpf_get_socket_cookie(sock->sk))
		return 0;

	p->cookie_value |= 0xF0;
	return 0;
}
