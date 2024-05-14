// SPDX-License-Identifier: GPL-2.0

#include "vmlinux.h"

#include <bpf/bpf_helpers.h>

#define AF_INET6 10

struct {
	__uint(type, BPF_MAP_TYPE_SK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, int);
} sockops_netns_cookies SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_SK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, int);
} sk_msg_netns_cookies SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_SOCKMAP);
	__uint(max_entries, 2);
	__type(key, __u32);
	__type(value, __u64);
} sock_map SEC(".maps");

SEC("sockops")
int get_netns_cookie_sockops(struct bpf_sock_ops *ctx)
{
	struct bpf_sock *sk = ctx->sk;
	int *cookie;
	__u32 key = 0;

	if (ctx->family != AF_INET6)
		return 1;

	if (!sk)
		return 1;

	switch (ctx->op) {
	case BPF_SOCK_OPS_TCP_CONNECT_CB:
		cookie = bpf_sk_storage_get(&sockops_netns_cookies, sk, 0,
					    BPF_SK_STORAGE_GET_F_CREATE);
		if (!cookie)
			return 1;

		*cookie = bpf_get_netns_cookie(ctx);
		break;
	case BPF_SOCK_OPS_ACTIVE_ESTABLISHED_CB:
		bpf_sock_map_update(ctx, &sock_map, &key, BPF_NOEXIST);
		break;
	default:
		break;
	}

	return 1;
}

SEC("sk_msg")
int get_netns_cookie_sk_msg(struct sk_msg_md *msg)
{
	struct bpf_sock *sk = msg->sk;
	int *cookie;

	if (msg->family != AF_INET6)
		return 1;

	if (!sk)
		return 1;

	cookie = bpf_sk_storage_get(&sk_msg_netns_cookies, sk, 0,
				    BPF_SK_STORAGE_GET_F_CREATE);
	if (!cookie)
		return 1;

	*cookie = bpf_get_netns_cookie(msg);

	return 1;
}

char _license[] SEC("license") = "GPL";
