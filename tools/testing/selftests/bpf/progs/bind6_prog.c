// SPDX-License-Identifier: GPL-2.0

#include <string.h>

#include <linux/stddef.h>
#include <linux/bpf.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <linux/if.h>
#include <errno.h>

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#define SERV6_IP_0		0xfaceb00c /* face:b00c:1234:5678::abcd */
#define SERV6_IP_1		0x12345678
#define SERV6_IP_2		0x00000000
#define SERV6_IP_3		0x0000abcd
#define SERV6_PORT		6060
#define SERV6_REWRITE_IP_0	0x00000000
#define SERV6_REWRITE_IP_1	0x00000000
#define SERV6_REWRITE_IP_2	0x00000000
#define SERV6_REWRITE_IP_3	0x00000001
#define SERV6_REWRITE_PORT	6666

SEC("cgroup/bind6")
int bind_v6_prog(struct bpf_sock_addr *ctx)
{
	struct bpf_sock *sk;
	__u32 user_ip6;
	__u16 user_port;
	int i;

	sk = ctx->sk;
	if (!sk)
		return 0;

	if (sk->family != AF_INET6)
		return 0;

	if (ctx->type != SOCK_STREAM && ctx->type != SOCK_DGRAM)
		return 0;

	if (ctx->user_ip6[0] != bpf_htonl(SERV6_IP_0) ||
	    ctx->user_ip6[1] != bpf_htonl(SERV6_IP_1) ||
	    ctx->user_ip6[2] != bpf_htonl(SERV6_IP_2) ||
	    ctx->user_ip6[3] != bpf_htonl(SERV6_IP_3) ||
	    ctx->user_port != bpf_htons(SERV6_PORT))
		return 0;

	// u8 narrow loads:
	for (i = 0; i < 4; i++) {
		user_ip6 = 0;
		user_ip6 |= ((volatile __u8 *)&ctx->user_ip6[i])[0] << 0;
		user_ip6 |= ((volatile __u8 *)&ctx->user_ip6[i])[1] << 8;
		user_ip6 |= ((volatile __u8 *)&ctx->user_ip6[i])[2] << 16;
		user_ip6 |= ((volatile __u8 *)&ctx->user_ip6[i])[3] << 24;
		if (ctx->user_ip6[i] != user_ip6)
			return 0;
	}

	user_port = 0;
	user_port |= ((volatile __u8 *)&ctx->user_port)[0] << 0;
	user_port |= ((volatile __u8 *)&ctx->user_port)[1] << 8;
	if (ctx->user_port != user_port)
		return 0;

	// u16 narrow loads:
	for (i = 0; i < 4; i++) {
		user_ip6 = 0;
		user_ip6 |= ((volatile __u16 *)&ctx->user_ip6[i])[0] << 0;
		user_ip6 |= ((volatile __u16 *)&ctx->user_ip6[i])[1] << 16;
		if (ctx->user_ip6[i] != user_ip6)
			return 0;
	}

	ctx->user_ip6[0] = bpf_htonl(SERV6_REWRITE_IP_0);
	ctx->user_ip6[1] = bpf_htonl(SERV6_REWRITE_IP_1);
	ctx->user_ip6[2] = bpf_htonl(SERV6_REWRITE_IP_2);
	ctx->user_ip6[3] = bpf_htonl(SERV6_REWRITE_IP_3);
	ctx->user_port = bpf_htons(SERV6_REWRITE_PORT);

	return 1;
}

char _license[] SEC("license") = "GPL";
