// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018 Facebook

#include "vmlinux.h"

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>
#include <bpf/bpf_tracing.h>

#define AF_INET6 10

struct socket_cookie {
	__u64 cookie_key;
	__u32 cookie_value;
};

struct {
	__uint(type, BPF_MAP_TYPE_SK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, struct socket_cookie);
} socket_cookies SEC(".maps");

/*
 * These three programs get executed in a row on connect() syscalls. The
 * userspace side of the test creates a client socket, issues a connect() on it
 * and then checks that the local storage associated with this socket has:
 * cookie_value == local_port << 8 | 0xFF
 * The different parts of this cookie_value are appended by those hooks if they
 * all agree on the output of bpf_get_socket_cookie().
 */
SEC("cgroup/connect6")
int set_cookie(struct bpf_sock_addr *ctx)
{
	struct socket_cookie *p;

	if (ctx->family != AF_INET6 || ctx->user_family != AF_INET6)
		return 1;

	p = bpf_sk_storage_get(&socket_cookies, ctx->sk, 0,
			       BPF_SK_STORAGE_GET_F_CREATE);
	if (!p)
		return 1;

	p->cookie_value = 0xF;
	p->cookie_key = bpf_get_socket_cookie(ctx);

	return 1;
}

SEC("sockops")
int update_cookie_sockops(struct bpf_sock_ops *ctx)
{
	struct bpf_sock *sk = ctx->sk;
	struct socket_cookie *p;

	if (ctx->family != AF_INET6)
		return 1;

	if (ctx->op != BPF_SOCK_OPS_TCP_CONNECT_CB)
		return 1;

	if (!sk)
		return 1;

	p = bpf_sk_storage_get(&socket_cookies, sk, 0, 0);
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

	if (uaddr->sa_family != AF_INET6)
		return 0;

	p = bpf_sk_storage_get(&socket_cookies, sock->sk, 0, 0);
	if (!p)
		return 0;

	if (p->cookie_key != bpf_get_socket_cookie(sock->sk))
		return 0;

	p->cookie_value |= 0xF0;

	return 0;
}

char _license[] SEC("license") = "GPL";
