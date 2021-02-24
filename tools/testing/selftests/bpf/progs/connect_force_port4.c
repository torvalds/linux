// SPDX-License-Identifier: GPL-2.0
#include <string.h>
#include <stdbool.h>

#include <linux/bpf.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <sys/socket.h>

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#include <bpf_sockopt_helpers.h>

char _license[] SEC("license") = "GPL";
int _version SEC("version") = 1;

struct svc_addr {
	__be32 addr;
	__be16 port;
};

struct {
	__uint(type, BPF_MAP_TYPE_SK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, struct svc_addr);
} service_mapping SEC(".maps");

SEC("cgroup/connect4")
int connect4(struct bpf_sock_addr *ctx)
{
	struct sockaddr_in sa = {};
	struct svc_addr *orig;

	/* Force local address to 127.0.0.1:22222. */
	sa.sin_family = AF_INET;
	sa.sin_port = bpf_htons(22222);
	sa.sin_addr.s_addr = bpf_htonl(0x7f000001);

	if (bpf_bind(ctx, (struct sockaddr *)&sa, sizeof(sa)) != 0)
		return 0;

	/* Rewire service 1.2.3.4:60000 to backend 127.0.0.1:60123. */
	if (ctx->user_port == bpf_htons(60000)) {
		orig = bpf_sk_storage_get(&service_mapping, ctx->sk, 0,
					  BPF_SK_STORAGE_GET_F_CREATE);
		if (!orig)
			return 0;

		orig->addr = ctx->user_ip4;
		orig->port = ctx->user_port;

		ctx->user_ip4 = bpf_htonl(0x7f000001);
		ctx->user_port = bpf_htons(60123);
	}
	return 1;
}

SEC("cgroup/getsockname4")
int getsockname4(struct bpf_sock_addr *ctx)
{
	if (!get_set_sk_priority(ctx))
		return 1;

	/* Expose local server as 1.2.3.4:60000 to client. */
	if (ctx->user_port == bpf_htons(60123)) {
		ctx->user_ip4 = bpf_htonl(0x01020304);
		ctx->user_port = bpf_htons(60000);
	}
	return 1;
}

SEC("cgroup/getpeername4")
int getpeername4(struct bpf_sock_addr *ctx)
{
	struct svc_addr *orig;

	if (!get_set_sk_priority(ctx))
		return 1;

	/* Expose service 1.2.3.4:60000 as peer instead of backend. */
	if (ctx->user_port == bpf_htons(60123)) {
		orig = bpf_sk_storage_get(&service_mapping, ctx->sk, 0, 0);
		if (orig) {
			ctx->user_ip4 = orig->addr;
			ctx->user_port = orig->port;
		}
	}
	return 1;
}
