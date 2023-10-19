// SPDX-License-Identifier: GPL-2.0
#include <string.h>

#include <linux/bpf.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <sys/socket.h>

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#include <bpf_sockopt_helpers.h>

char _license[] SEC("license") = "GPL";

struct svc_addr {
	__be32 addr[4];
	__be16 port;
};

struct {
	__uint(type, BPF_MAP_TYPE_SK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, struct svc_addr);
} service_mapping SEC(".maps");

SEC("cgroup/connect6")
int connect6(struct bpf_sock_addr *ctx)
{
	struct sockaddr_in6 sa = {};
	struct svc_addr *orig;

	/* Force local address to [::1]:22223. */
	sa.sin6_family = AF_INET6;
	sa.sin6_port = bpf_htons(22223);
	sa.sin6_addr.s6_addr32[3] = bpf_htonl(1);

	if (bpf_bind(ctx, (struct sockaddr *)&sa, sizeof(sa)) != 0)
		return 0;

	/* Rewire service [fc00::1]:60000 to backend [::1]:60124. */
	if (ctx->user_port == bpf_htons(60000)) {
		orig = bpf_sk_storage_get(&service_mapping, ctx->sk, 0,
					  BPF_SK_STORAGE_GET_F_CREATE);
		if (!orig)
			return 0;

		orig->addr[0] = ctx->user_ip6[0];
		orig->addr[1] = ctx->user_ip6[1];
		orig->addr[2] = ctx->user_ip6[2];
		orig->addr[3] = ctx->user_ip6[3];
		orig->port = ctx->user_port;

		ctx->user_ip6[0] = 0;
		ctx->user_ip6[1] = 0;
		ctx->user_ip6[2] = 0;
		ctx->user_ip6[3] = bpf_htonl(1);
		ctx->user_port = bpf_htons(60124);
	}
	return 1;
}

SEC("cgroup/getsockname6")
int getsockname6(struct bpf_sock_addr *ctx)
{
	if (!get_set_sk_priority(ctx))
		return 1;

	/* Expose local server as [fc00::1]:60000 to client. */
	if (ctx->user_port == bpf_htons(60124)) {
		ctx->user_ip6[0] = bpf_htonl(0xfc000000);
		ctx->user_ip6[1] = 0;
		ctx->user_ip6[2] = 0;
		ctx->user_ip6[3] = bpf_htonl(1);
		ctx->user_port = bpf_htons(60000);
	}
	return 1;
}

SEC("cgroup/getpeername6")
int getpeername6(struct bpf_sock_addr *ctx)
{
	struct svc_addr *orig;

	if (!get_set_sk_priority(ctx))
		return 1;

	/* Expose service [fc00::1]:60000 as peer instead of backend. */
	if (ctx->user_port == bpf_htons(60124)) {
		orig = bpf_sk_storage_get(&service_mapping, ctx->sk, 0, 0);
		if (orig) {
			ctx->user_ip6[0] = orig->addr[0];
			ctx->user_ip6[1] = orig->addr[1];
			ctx->user_ip6[2] = orig->addr[2];
			ctx->user_ip6[3] = orig->addr[3];
			ctx->user_port = orig->port;
		}
	}
	return 1;
}
