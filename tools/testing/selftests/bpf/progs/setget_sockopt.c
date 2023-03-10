// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#include "vmlinux.h"
#include "bpf_tracing_net.h"
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

extern unsigned long CONFIG_HZ __kconfig;

const volatile char veth[IFNAMSIZ];
const volatile int veth_ifindex;

int nr_listen;
int nr_passive;
int nr_active;
int nr_connect;
int nr_binddev;
int nr_socket_post_create;
int nr_fin_wait1;

struct sockopt_test {
	int opt;
	int new;
	int restore;
	int expected;
	int tcp_expected;
	unsigned int flip:1;
};

static const char not_exist_cc[] = "not_exist";
static const char cubic_cc[] = "cubic";
static const char reno_cc[] = "reno";

static const struct sockopt_test sol_socket_tests[] = {
	{ .opt = SO_REUSEADDR, .flip = 1, },
	{ .opt = SO_SNDBUF, .new = 8123, .expected = 8123 * 2, },
	{ .opt = SO_RCVBUF, .new = 8123, .expected = 8123 * 2, },
	{ .opt = SO_KEEPALIVE, .flip = 1, },
	{ .opt = SO_PRIORITY, .new = 0xeb9f, .expected = 0xeb9f, },
	{ .opt = SO_REUSEPORT, .flip = 1, },
	{ .opt = SO_RCVLOWAT, .new = 8123, .expected = 8123, },
	{ .opt = SO_MARK, .new = 0xeb9f, .expected = 0xeb9f, },
	{ .opt = SO_MAX_PACING_RATE, .new = 0xeb9f, .expected = 0xeb9f, },
	{ .opt = SO_TXREHASH, .flip = 1, },
	{ .opt = 0, },
};

static const struct sockopt_test sol_tcp_tests[] = {
	{ .opt = TCP_NODELAY, .flip = 1, },
	{ .opt = TCP_KEEPIDLE, .new = 123, .expected = 123, .restore = 321, },
	{ .opt = TCP_KEEPINTVL, .new = 123, .expected = 123, .restore = 321, },
	{ .opt = TCP_KEEPCNT, .new = 123, .expected = 123, .restore = 124, },
	{ .opt = TCP_SYNCNT, .new = 123, .expected = 123, .restore = 124, },
	{ .opt = TCP_WINDOW_CLAMP, .new = 8123, .expected = 8123, .restore = 8124, },
	{ .opt = TCP_CONGESTION, },
	{ .opt = TCP_THIN_LINEAR_TIMEOUTS, .flip = 1, },
	{ .opt = TCP_USER_TIMEOUT, .new = 123400, .expected = 123400, },
	{ .opt = TCP_NOTSENT_LOWAT, .new = 1314, .expected = 1314, },
	{ .opt = 0, },
};

static const struct sockopt_test sol_ip_tests[] = {
	{ .opt = IP_TOS, .new = 0xe1, .expected = 0xe1, .tcp_expected = 0xe0, },
	{ .opt = 0, },
};

static const struct sockopt_test sol_ipv6_tests[] = {
	{ .opt = IPV6_TCLASS, .new = 0xe1, .expected = 0xe1, .tcp_expected = 0xe0, },
	{ .opt = IPV6_AUTOFLOWLABEL, .flip = 1, },
	{ .opt = 0, },
};

struct loop_ctx {
	void *ctx;
	struct sock *sk;
};

static int bpf_test_sockopt_flip(void *ctx, struct sock *sk,
				 const struct sockopt_test *t,
				 int level)
{
	int old, tmp, new, opt = t->opt;

	opt = t->opt;

	if (bpf_getsockopt(ctx, level, opt, &old, sizeof(old)))
		return 1;
	/* kernel initialized txrehash to 255 */
	if (level == SOL_SOCKET && opt == SO_TXREHASH && old != 0 && old != 1)
		old = 1;

	new = !old;
	if (bpf_setsockopt(ctx, level, opt, &new, sizeof(new)))
		return 1;
	if (bpf_getsockopt(ctx, level, opt, &tmp, sizeof(tmp)) ||
	    tmp != new)
		return 1;

	if (bpf_setsockopt(ctx, level, opt, &old, sizeof(old)))
		return 1;

	return 0;
}

static int bpf_test_sockopt_int(void *ctx, struct sock *sk,
				const struct sockopt_test *t,
				int level)
{
	int old, tmp, new, expected, opt;

	opt = t->opt;
	new = t->new;
	if (sk->sk_type == SOCK_STREAM && t->tcp_expected)
		expected = t->tcp_expected;
	else
		expected = t->expected;

	if (bpf_getsockopt(ctx, level, opt, &old, sizeof(old)) ||
	    old == new)
		return 1;

	if (bpf_setsockopt(ctx, level, opt, &new, sizeof(new)))
		return 1;
	if (bpf_getsockopt(ctx, level, opt, &tmp, sizeof(tmp)) ||
	    tmp != expected)
		return 1;

	if (t->restore)
		old = t->restore;
	if (bpf_setsockopt(ctx, level, opt, &old, sizeof(old)))
		return 1;

	return 0;
}

static int bpf_test_socket_sockopt(__u32 i, struct loop_ctx *lc)
{
	const struct sockopt_test *t;

	if (i >= ARRAY_SIZE(sol_socket_tests))
		return 1;

	t = &sol_socket_tests[i];
	if (!t->opt)
		return 1;

	if (t->flip)
		return bpf_test_sockopt_flip(lc->ctx, lc->sk, t, SOL_SOCKET);

	return bpf_test_sockopt_int(lc->ctx, lc->sk, t, SOL_SOCKET);
}

static int bpf_test_ip_sockopt(__u32 i, struct loop_ctx *lc)
{
	const struct sockopt_test *t;

	if (i >= ARRAY_SIZE(sol_ip_tests))
		return 1;

	t = &sol_ip_tests[i];
	if (!t->opt)
		return 1;

	if (t->flip)
		return bpf_test_sockopt_flip(lc->ctx, lc->sk, t, IPPROTO_IP);

	return bpf_test_sockopt_int(lc->ctx, lc->sk, t, IPPROTO_IP);
}

static int bpf_test_ipv6_sockopt(__u32 i, struct loop_ctx *lc)
{
	const struct sockopt_test *t;

	if (i >= ARRAY_SIZE(sol_ipv6_tests))
		return 1;

	t = &sol_ipv6_tests[i];
	if (!t->opt)
		return 1;

	if (t->flip)
		return bpf_test_sockopt_flip(lc->ctx, lc->sk, t, IPPROTO_IPV6);

	return bpf_test_sockopt_int(lc->ctx, lc->sk, t, IPPROTO_IPV6);
}

static int bpf_test_tcp_sockopt(__u32 i, struct loop_ctx *lc)
{
	const struct sockopt_test *t;
	struct sock *sk;
	void *ctx;

	if (i >= ARRAY_SIZE(sol_tcp_tests))
		return 1;

	t = &sol_tcp_tests[i];
	if (!t->opt)
		return 1;

	ctx = lc->ctx;
	sk = lc->sk;

	if (t->opt == TCP_CONGESTION) {
		char old_cc[16], tmp_cc[16];
		const char *new_cc;
		int new_cc_len;

		if (!bpf_setsockopt(ctx, IPPROTO_TCP, TCP_CONGESTION,
				    (void *)not_exist_cc, sizeof(not_exist_cc)))
			return 1;
		if (bpf_getsockopt(ctx, IPPROTO_TCP, TCP_CONGESTION, old_cc, sizeof(old_cc)))
			return 1;
		if (!bpf_strncmp(old_cc, sizeof(old_cc), cubic_cc)) {
			new_cc = reno_cc;
			new_cc_len = sizeof(reno_cc);
		} else {
			new_cc = cubic_cc;
			new_cc_len = sizeof(cubic_cc);
		}
		if (bpf_setsockopt(ctx, IPPROTO_TCP, TCP_CONGESTION, (void *)new_cc,
				   new_cc_len))
			return 1;
		if (bpf_getsockopt(ctx, IPPROTO_TCP, TCP_CONGESTION, tmp_cc, sizeof(tmp_cc)))
			return 1;
		if (bpf_strncmp(tmp_cc, sizeof(tmp_cc), new_cc))
			return 1;
		if (bpf_setsockopt(ctx, IPPROTO_TCP, TCP_CONGESTION, old_cc, sizeof(old_cc)))
			return 1;
		return 0;
	}

	if (t->flip)
		return bpf_test_sockopt_flip(ctx, sk, t, IPPROTO_TCP);

	return bpf_test_sockopt_int(ctx, sk, t, IPPROTO_TCP);
}

static int bpf_test_sockopt(void *ctx, struct sock *sk)
{
	struct loop_ctx lc = { .ctx = ctx, .sk = sk, };
	__u16 family, proto;
	int n;

	family = sk->sk_family;
	proto = sk->sk_protocol;

	n = bpf_loop(ARRAY_SIZE(sol_socket_tests), bpf_test_socket_sockopt, &lc, 0);
	if (n != ARRAY_SIZE(sol_socket_tests))
		return -1;

	if (proto == IPPROTO_TCP) {
		n = bpf_loop(ARRAY_SIZE(sol_tcp_tests), bpf_test_tcp_sockopt, &lc, 0);
		if (n != ARRAY_SIZE(sol_tcp_tests))
			return -1;
	}

	if (family == AF_INET) {
		n = bpf_loop(ARRAY_SIZE(sol_ip_tests), bpf_test_ip_sockopt, &lc, 0);
		if (n != ARRAY_SIZE(sol_ip_tests))
			return -1;
	} else {
		n = bpf_loop(ARRAY_SIZE(sol_ipv6_tests), bpf_test_ipv6_sockopt, &lc, 0);
		if (n != ARRAY_SIZE(sol_ipv6_tests))
			return -1;
	}

	return 0;
}

static int binddev_test(void *ctx)
{
	const char empty_ifname[] = "";
	int ifindex, zero = 0;

	if (bpf_setsockopt(ctx, SOL_SOCKET, SO_BINDTODEVICE,
			   (void *)veth, sizeof(veth)))
		return -1;
	if (bpf_getsockopt(ctx, SOL_SOCKET, SO_BINDTOIFINDEX,
			   &ifindex, sizeof(int)) ||
	    ifindex != veth_ifindex)
		return -1;

	if (bpf_setsockopt(ctx, SOL_SOCKET, SO_BINDTODEVICE,
			   (void *)empty_ifname, sizeof(empty_ifname)))
		return -1;
	if (bpf_getsockopt(ctx, SOL_SOCKET, SO_BINDTOIFINDEX,
			   &ifindex, sizeof(int)) ||
	    ifindex != 0)
		return -1;

	if (bpf_setsockopt(ctx, SOL_SOCKET, SO_BINDTOIFINDEX,
			   (void *)&veth_ifindex, sizeof(int)))
		return -1;
	if (bpf_getsockopt(ctx, SOL_SOCKET, SO_BINDTOIFINDEX,
			   &ifindex, sizeof(int)) ||
	    ifindex != veth_ifindex)
		return -1;

	if (bpf_setsockopt(ctx, SOL_SOCKET, SO_BINDTOIFINDEX,
			   &zero, sizeof(int)))
		return -1;
	if (bpf_getsockopt(ctx, SOL_SOCKET, SO_BINDTOIFINDEX,
			   &ifindex, sizeof(int)) ||
	    ifindex != 0)
		return -1;

	return 0;
}

static int test_tcp_maxseg(void *ctx, struct sock *sk)
{
	int val = 1314, tmp;

	if (sk->sk_state != TCP_ESTABLISHED)
		return bpf_setsockopt(ctx, IPPROTO_TCP, TCP_MAXSEG,
				      &val, sizeof(val));

	if (bpf_getsockopt(ctx, IPPROTO_TCP, TCP_MAXSEG, &tmp, sizeof(tmp)) ||
	    tmp > val)
		return -1;

	return 0;
}

static int test_tcp_saved_syn(void *ctx, struct sock *sk)
{
	__u8 saved_syn[20];
	int one = 1;

	if (sk->sk_state == TCP_LISTEN)
		return bpf_setsockopt(ctx, IPPROTO_TCP, TCP_SAVE_SYN,
				      &one, sizeof(one));

	return bpf_getsockopt(ctx, IPPROTO_TCP, TCP_SAVED_SYN,
			      saved_syn, sizeof(saved_syn));
}

SEC("lsm_cgroup/socket_post_create")
int BPF_PROG(socket_post_create, struct socket *sock, int family,
	     int type, int protocol, int kern)
{
	struct sock *sk = sock->sk;

	if (!sk)
		return 1;

	nr_socket_post_create += !bpf_test_sockopt(sk, sk);
	nr_binddev += !binddev_test(sk);

	return 1;
}

SEC("sockops")
int skops_sockopt(struct bpf_sock_ops *skops)
{
	struct bpf_sock *bpf_sk = skops->sk;
	struct sock *sk;

	if (!bpf_sk)
		return 1;

	sk = (struct sock *)bpf_skc_to_tcp_sock(bpf_sk);
	if (!sk)
		return 1;

	switch (skops->op) {
	case BPF_SOCK_OPS_TCP_LISTEN_CB:
		nr_listen += !(bpf_test_sockopt(skops, sk) ||
			       test_tcp_maxseg(skops, sk) ||
			       test_tcp_saved_syn(skops, sk));
		break;
	case BPF_SOCK_OPS_TCP_CONNECT_CB:
		nr_connect += !(bpf_test_sockopt(skops, sk) ||
				test_tcp_maxseg(skops, sk));
		break;
	case BPF_SOCK_OPS_ACTIVE_ESTABLISHED_CB:
		nr_active += !(bpf_test_sockopt(skops, sk) ||
			       test_tcp_maxseg(skops, sk));
		break;
	case BPF_SOCK_OPS_PASSIVE_ESTABLISHED_CB:
		nr_passive += !(bpf_test_sockopt(skops, sk) ||
				test_tcp_maxseg(skops, sk) ||
				test_tcp_saved_syn(skops, sk));
		bpf_sock_ops_cb_flags_set(skops,
					  skops->bpf_sock_ops_cb_flags |
					  BPF_SOCK_OPS_STATE_CB_FLAG);
		break;
	case BPF_SOCK_OPS_STATE_CB:
		if (skops->args[1] == BPF_TCP_CLOSE_WAIT)
			nr_fin_wait1 += !bpf_test_sockopt(skops, sk);
		break;
	}

	return 1;
}

char _license[] SEC("license") = "GPL";
