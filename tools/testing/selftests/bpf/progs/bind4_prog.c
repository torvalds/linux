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

#define SERV4_IP		0xc0a801feU /* 192.168.1.254 */
#define SERV4_PORT		4040
#define SERV4_REWRITE_IP	0x7f000001U /* 127.0.0.1 */
#define SERV4_REWRITE_PORT	4444

SEC("cgroup/bind4")
int bind_v4_prog(struct bpf_sock_addr *ctx)
{
	struct bpf_sock *sk;
	__u32 user_ip4;
	__u16 user_port;

	sk = ctx->sk;
	if (!sk)
		return 0;

	if (sk->family != AF_INET)
		return 0;

	if (ctx->type != SOCK_STREAM && ctx->type != SOCK_DGRAM)
		return 0;

	if (ctx->user_ip4 != bpf_htonl(SERV4_IP) ||
	    ctx->user_port != bpf_htons(SERV4_PORT))
		return 0;

	// u8 narrow loads:
	user_ip4 = 0;
	user_ip4 |= ((volatile __u8 *)&ctx->user_ip4)[0] << 0;
	user_ip4 |= ((volatile __u8 *)&ctx->user_ip4)[1] << 8;
	user_ip4 |= ((volatile __u8 *)&ctx->user_ip4)[2] << 16;
	user_ip4 |= ((volatile __u8 *)&ctx->user_ip4)[3] << 24;
	if (ctx->user_ip4 != user_ip4)
		return 0;

	user_port = 0;
	user_port |= ((volatile __u8 *)&ctx->user_port)[0] << 0;
	user_port |= ((volatile __u8 *)&ctx->user_port)[1] << 8;
	if (ctx->user_port != user_port)
		return 0;

	// u16 narrow loads:
	user_ip4 = 0;
	user_ip4 |= ((volatile __u16 *)&ctx->user_ip4)[0] << 0;
	user_ip4 |= ((volatile __u16 *)&ctx->user_ip4)[1] << 16;
	if (ctx->user_ip4 != user_ip4)
		return 0;

	ctx->user_ip4 = bpf_htonl(SERV4_REWRITE_IP);
	ctx->user_port = bpf_htons(SERV4_REWRITE_PORT);

	return 1;
}

char _license[] SEC("license") = "GPL";
