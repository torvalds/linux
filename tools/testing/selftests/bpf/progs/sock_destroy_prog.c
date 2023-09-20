// SPDX-License-Identifier: GPL-2.0

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#include "bpf_tracing_net.h"

__be16 serv_port = 0;

int bpf_sock_destroy(struct sock_common *sk) __ksym;

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u64);
} tcp_conn_sockets SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u64);
} udp_conn_sockets SEC(".maps");

SEC("cgroup/connect6")
int sock_connect(struct bpf_sock_addr *ctx)
{
	__u64 sock_cookie = 0;
	int key = 0;
	__u32 keyc = 0;

	if (ctx->family != AF_INET6 || ctx->user_family != AF_INET6)
		return 1;

	sock_cookie = bpf_get_socket_cookie(ctx);
	if (ctx->protocol == IPPROTO_TCP)
		bpf_map_update_elem(&tcp_conn_sockets, &key, &sock_cookie, 0);
	else if (ctx->protocol == IPPROTO_UDP)
		bpf_map_update_elem(&udp_conn_sockets, &keyc, &sock_cookie, 0);
	else
		return 1;

	return 1;
}

SEC("iter/tcp")
int iter_tcp6_client(struct bpf_iter__tcp *ctx)
{
	struct sock_common *sk_common = ctx->sk_common;
	__u64 sock_cookie = 0;
	__u64 *val;
	int key = 0;

	if (!sk_common)
		return 0;

	if (sk_common->skc_family != AF_INET6)
		return 0;

	sock_cookie  = bpf_get_socket_cookie(sk_common);
	val = bpf_map_lookup_elem(&tcp_conn_sockets, &key);
	if (!val)
		return 0;
	/* Destroy connected client sockets. */
	if (sock_cookie == *val)
		bpf_sock_destroy(sk_common);

	return 0;
}

SEC("iter/tcp")
int iter_tcp6_server(struct bpf_iter__tcp *ctx)
{
	struct sock_common *sk_common = ctx->sk_common;
	const struct inet_connection_sock *icsk;
	const struct inet_sock *inet;
	struct tcp6_sock *tcp_sk;
	__be16 srcp;

	if (!sk_common)
		return 0;

	if (sk_common->skc_family != AF_INET6)
		return 0;

	tcp_sk = bpf_skc_to_tcp6_sock(sk_common);
	if (!tcp_sk)
		return 0;

	icsk = &tcp_sk->tcp.inet_conn;
	inet = &icsk->icsk_inet;
	srcp = inet->inet_sport;

	/* Destroy server sockets. */
	if (srcp == serv_port)
		bpf_sock_destroy(sk_common);

	return 0;
}


SEC("iter/udp")
int iter_udp6_client(struct bpf_iter__udp *ctx)
{
	struct udp_sock *udp_sk = ctx->udp_sk;
	struct sock *sk = (struct sock *) udp_sk;
	__u64 sock_cookie = 0, *val;
	int key = 0;

	if (!sk)
		return 0;

	sock_cookie  = bpf_get_socket_cookie(sk);
	val = bpf_map_lookup_elem(&udp_conn_sockets, &key);
	if (!val)
		return 0;
	/* Destroy connected client sockets. */
	if (sock_cookie == *val)
		bpf_sock_destroy((struct sock_common *)sk);

	return 0;
}

SEC("iter/udp")
int iter_udp6_server(struct bpf_iter__udp *ctx)
{
	struct udp_sock *udp_sk = ctx->udp_sk;
	struct sock *sk = (struct sock *) udp_sk;
	struct inet_sock *inet;
	__be16 srcp;

	if (!sk)
		return 0;

	inet = &udp_sk->inet;
	srcp = inet->inet_sport;
	if (srcp == serv_port)
		bpf_sock_destroy((struct sock_common *)sk);

	return 0;
}

char _license[] SEC("license") = "GPL";
