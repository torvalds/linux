// SPDX-License-Identifier: GPL-2.0

#include "vmlinux.h"

#include <bpf/bpf_helpers.h>

#define AF_INET6 10

struct {
	__uint(type, BPF_MAP_TYPE_SK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, int);
} netns_cookies SEC(".maps");

SEC("sockops")
int get_netns_cookie_sockops(struct bpf_sock_ops *ctx)
{
	struct bpf_sock *sk = ctx->sk;
	int *cookie;

	if (ctx->family != AF_INET6)
		return 1;

	if (ctx->op != BPF_SOCK_OPS_TCP_CONNECT_CB)
		return 1;

	if (!sk)
		return 1;

	cookie = bpf_sk_storage_get(&netns_cookies, sk, 0,
				BPF_SK_STORAGE_GET_F_CREATE);
	if (!cookie)
		return 1;

	*cookie = bpf_get_netns_cookie(ctx);

	return 1;
}
