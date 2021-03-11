// SPDX-License-Identifier: GPL-2.0

#include <linux/stddef.h>
#include <linux/bpf.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

static __always_inline int bind_prog(struct bpf_sock_addr *ctx, int family)
{
	struct bpf_sock *sk;

	sk = ctx->sk;
	if (!sk)
		return 0;

	if (sk->family != family)
		return 0;

	if (ctx->type != SOCK_STREAM)
		return 0;

	/* Return 1 OR'ed with the first bit set to indicate
	 * that CAP_NET_BIND_SERVICE should be bypassed.
	 */
	if (ctx->user_port == bpf_htons(111))
		return (1 | 2);

	return 1;
}

SEC("cgroup/bind4")
int bind_v4_prog(struct bpf_sock_addr *ctx)
{
	return bind_prog(ctx, AF_INET);
}

SEC("cgroup/bind6")
int bind_v6_prog(struct bpf_sock_addr *ctx)
{
	return bind_prog(ctx, AF_INET6);
}

char _license[] SEC("license") = "GPL";
