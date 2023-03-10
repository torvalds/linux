// SPDX-License-Identifier: GPL-2.0

#include <linux/stddef.h>
#include <linux/bpf.h>
#include <linux/in6.h>
#include <sys/socket.h>

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#include <bpf_sockopt_helpers.h>

#define SERV6_IP_0		0xfaceb00c /* face:b00c:1234:5678::abcd */
#define SERV6_IP_1		0x12345678
#define SERV6_IP_2		0x00000000
#define SERV6_IP_3		0x0000abcd
#define SERV6_PORT		6060

SEC("cgroup/recvmsg6")
int recvmsg6_prog(struct bpf_sock_addr *ctx)
{
	struct bpf_sock *sk;

	sk = ctx->sk;
	if (!sk)
		return 1;

	if (sk->family != AF_INET6)
		return 1;

	if (ctx->type != SOCK_STREAM && ctx->type != SOCK_DGRAM)
		return 1;

	if (!get_set_sk_priority(ctx))
		return 1;

	ctx->user_ip6[0] = bpf_htonl(SERV6_IP_0);
	ctx->user_ip6[1] = bpf_htonl(SERV6_IP_1);
	ctx->user_ip6[2] = bpf_htonl(SERV6_IP_2);
	ctx->user_ip6[3] = bpf_htonl(SERV6_IP_3);
	ctx->user_port = bpf_htons(SERV6_PORT);

	return 1;
}

char _license[] SEC("license") = "GPL";
