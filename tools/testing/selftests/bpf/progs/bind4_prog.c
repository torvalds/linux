// SPDX-License-Identifier: GPL-2.0

#include <string.h>

#include <linux/stddef.h>
#include <linux/bpf.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/if.h>
#include <errno.h>

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#include "bind_prog.h"

#define SERV4_IP		0xc0a801feU /* 192.168.1.254 */
#define SERV4_PORT		4040
#define SERV4_REWRITE_IP	0x7f000001U /* 127.0.0.1 */
#define SERV4_REWRITE_PORT	4444

#ifndef IFNAMSIZ
#define IFNAMSIZ 16
#endif

static __inline int bind_to_device(struct bpf_sock_addr *ctx)
{
	char veth1[IFNAMSIZ] = "test_sock_addr1";
	char veth2[IFNAMSIZ] = "test_sock_addr2";
	char missing[IFNAMSIZ] = "nonexistent_dev";
	char del_bind[IFNAMSIZ] = "";
	int veth1_idx, veth2_idx;

	if (bpf_setsockopt(ctx, SOL_SOCKET, SO_BINDTODEVICE,
			   &veth1, sizeof(veth1)))
		return 1;
	if (bpf_getsockopt(ctx, SOL_SOCKET, SO_BINDTOIFINDEX,
			   &veth1_idx, sizeof(veth1_idx)) || !veth1_idx)
		return 1;
	if (bpf_setsockopt(ctx, SOL_SOCKET, SO_BINDTODEVICE,
			   &veth2, sizeof(veth2)))
		return 1;
	if (bpf_getsockopt(ctx, SOL_SOCKET, SO_BINDTOIFINDEX,
			   &veth2_idx, sizeof(veth2_idx)) || !veth2_idx ||
	    veth1_idx == veth2_idx)
		return 1;
	if (bpf_setsockopt(ctx, SOL_SOCKET, SO_BINDTODEVICE,
			   &missing, sizeof(missing)) != -ENODEV)
		return 1;
	if (bpf_setsockopt(ctx, SOL_SOCKET, SO_BINDTOIFINDEX,
			   &veth1_idx, sizeof(veth1_idx)))
		return 1;
	if (bpf_setsockopt(ctx, SOL_SOCKET, SO_BINDTODEVICE,
			   &del_bind, sizeof(del_bind)))
		return 1;

	return 0;
}

static __inline int bind_reuseport(struct bpf_sock_addr *ctx)
{
	int val = 1;

	if (bpf_setsockopt(ctx, SOL_SOCKET, SO_REUSEPORT,
			   &val, sizeof(val)))
		return 1;
	if (bpf_getsockopt(ctx, SOL_SOCKET, SO_REUSEPORT,
			   &val, sizeof(val)) || !val)
		return 1;
	val = 0;
	if (bpf_setsockopt(ctx, SOL_SOCKET, SO_REUSEPORT,
			   &val, sizeof(val)))
		return 1;
	if (bpf_getsockopt(ctx, SOL_SOCKET, SO_REUSEPORT,
			   &val, sizeof(val)) || val)
		return 1;

	return 0;
}

static __inline int misc_opts(struct bpf_sock_addr *ctx, int opt)
{
	int old, tmp, new = 0xeb9f;

	/* Socket in test case has guarantee that old never equals to new. */
	if (bpf_getsockopt(ctx, SOL_SOCKET, opt, &old, sizeof(old)) ||
	    old == new)
		return 1;
	if (bpf_setsockopt(ctx, SOL_SOCKET, opt, &new, sizeof(new)))
		return 1;
	if (bpf_getsockopt(ctx, SOL_SOCKET, opt, &tmp, sizeof(tmp)) ||
	    tmp != new)
		return 1;
	if (bpf_setsockopt(ctx, SOL_SOCKET, opt, &old, sizeof(old)))
		return 1;

	return 0;
}

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
	user_ip4 |= load_byte(ctx->user_ip4, 0, sizeof(user_ip4));
	user_ip4 |= load_byte(ctx->user_ip4, 1, sizeof(user_ip4));
	user_ip4 |= load_byte(ctx->user_ip4, 2, sizeof(user_ip4));
	user_ip4 |= load_byte(ctx->user_ip4, 3, sizeof(user_ip4));
	if (ctx->user_ip4 != user_ip4)
		return 0;

	user_port = 0;
	user_port |= load_byte(ctx->user_port, 0, sizeof(user_port));
	user_port |= load_byte(ctx->user_port, 1, sizeof(user_port));
	if (ctx->user_port != user_port)
		return 0;

	// u16 narrow loads:
	user_ip4 = 0;
	user_ip4 |= load_word(ctx->user_ip4, 0, sizeof(user_ip4));
	user_ip4 |= load_word(ctx->user_ip4, 1, sizeof(user_ip4));
	if (ctx->user_ip4 != user_ip4)
		return 0;

	/* Bind to device and unbind it. */
	if (bind_to_device(ctx))
		return 0;

	/* Test for misc socket options. */
	if (misc_opts(ctx, SO_MARK) || misc_opts(ctx, SO_PRIORITY))
		return 0;

	/* Set reuseport and unset */
	if (bind_reuseport(ctx))
		return 0;

	ctx->user_ip4 = bpf_htonl(SERV4_REWRITE_IP);
	ctx->user_port = bpf_htons(SERV4_REWRITE_PORT);

	return 1;
}

SEC("cgroup/bind4")
int bind_v4_deny_prog(struct bpf_sock_addr *ctx)
{
	return 0;
}

char _license[] SEC("license") = "GPL";
