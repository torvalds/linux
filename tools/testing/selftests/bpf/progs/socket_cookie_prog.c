// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018 Facebook

#include <linux/bpf.h>
#include <sys/socket.h>

#include "bpf_helpers.h"
#include "bpf_endian.h"

struct socket_cookie {
	__u64 cookie_key;
	__u32 cookie_value;
};

struct bpf_map_def SEC("maps") socket_cookies = {
	.type = BPF_MAP_TYPE_SK_STORAGE,
	.key_size = sizeof(int),
	.value_size = sizeof(struct socket_cookie),
	.map_flags = BPF_F_NO_PREALLOC,
};

BPF_ANNOTATE_KV_PAIR(socket_cookies, int, struct socket_cookie);

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

	p->cookie_value = 0xFF;
	p->cookie_key = bpf_get_socket_cookie(ctx);

	return 1;
}

SEC("sockops")
int update_cookie(struct bpf_sock_ops *ctx)
{
	struct bpf_sock *sk;
	struct socket_cookie *p;

	if (ctx->family != AF_INET6)
		return 1;

	if (ctx->op != BPF_SOCK_OPS_TCP_CONNECT_CB)
		return 1;

	if (!ctx->sk)
		return 1;

	p = bpf_sk_storage_get(&socket_cookies, ctx->sk, 0, 0);
	if (!p)
		return 1;

	if (p->cookie_key != bpf_get_socket_cookie(ctx))
		return 1;

	p->cookie_value = (ctx->local_port << 8) | p->cookie_value;

	return 1;
}

int _version SEC("version") = 1;

char _license[] SEC("license") = "GPL";
