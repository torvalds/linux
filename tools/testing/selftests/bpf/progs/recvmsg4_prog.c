// SPDX-License-Identifier: GPL-2.0

#include <linux/stddef.h>
#include <linux/bpf.h>
#include <linux/in.h>
#include <sys/socket.h>

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#include <bpf_sockopt_helpers.h>

#define SERV4_IP		0xc0a801feU /* 192.168.1.254 */
#define SERV4_PORT		4040

SEC("cgroup/recvmsg4")
int recvmsg4_prog(struct bpf_sock_addr *ctx)
{
	struct bpf_sock *sk;
	__u32 user_ip4;
	__u16 user_port;

	sk = ctx->sk;
	if (!sk)
		return 1;

	if (sk->family != AF_INET)
		return 1;

	if (ctx->type != SOCK_STREAM && ctx->type != SOCK_DGRAM)
		return 1;

	if (!get_set_sk_priority(ctx))
		return 1;

	ctx->user_ip4 = bpf_htonl(SERV4_IP);
	ctx->user_port = bpf_htons(SERV4_PORT);

	return 1;
}

char _license[] SEC("license") = "GPL";
