// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018 Facebook

#include <linux/bpf.h>
#include <sys/socket.h>

#include "bpf_helpers.h"
#include "bpf_endian.h"

struct bpf_map_def SEC("maps") socket_cookies = {
	.type = BPF_MAP_TYPE_HASH,
	.key_size = sizeof(__u64),
	.value_size = sizeof(__u32),
	.max_entries = 1 << 8,
};

SEC("cgroup/connect6")
int set_cookie(struct bpf_sock_addr *ctx)
{
	__u32 cookie_value = 0xFF;
	__u64 cookie_key;

	if (ctx->family != AF_INET6 || ctx->user_family != AF_INET6)
		return 1;

	cookie_key = bpf_get_socket_cookie(ctx);
	if (bpf_map_update_elem(&socket_cookies, &cookie_key, &cookie_value, 0))
		return 0;

	return 1;
}

SEC("sockops")
int update_cookie(struct bpf_sock_ops *ctx)
{
	__u32 new_cookie_value;
	__u32 *cookie_value;
	__u64 cookie_key;

	if (ctx->family != AF_INET6)
		return 1;

	if (ctx->op != BPF_SOCK_OPS_TCP_CONNECT_CB)
		return 1;

	cookie_key = bpf_get_socket_cookie(ctx);

	cookie_value = bpf_map_lookup_elem(&socket_cookies, &cookie_key);
	if (!cookie_value)
		return 1;

	new_cookie_value = (ctx->local_port << 8) | *cookie_value;
	bpf_map_update_elem(&socket_cookies, &cookie_key, &new_cookie_value, 0);

	return 1;
}

int _version SEC("version") = 1;

char _license[] SEC("license") = "GPL";
