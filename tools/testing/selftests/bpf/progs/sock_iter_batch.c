// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2024 Meta

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_endian.h>
#include "bpf_tracing_net.h"
#include "bpf_kfuncs.h"

#define ATTR __always_inline
#include "test_jhash.h"

static bool ipv6_addr_loopback(const struct in6_addr *a)
{
	return (a->s6_addr32[0] | a->s6_addr32[1] |
		a->s6_addr32[2] | (a->s6_addr32[3] ^ bpf_htonl(1))) == 0;
}

static bool ipv4_addr_loopback(__be32 a)
{
	return a == bpf_ntohl(0x7f000001);
}

volatile const unsigned int sf;
volatile const unsigned int ss;
volatile const __u16 ports[2];
unsigned int bucket[2];

SEC("iter/tcp")
int iter_tcp_soreuse(struct bpf_iter__tcp *ctx)
{
	struct sock *sk = (struct sock *)ctx->sk_common;
	struct inet_hashinfo *hinfo;
	unsigned int hash;
	__u64 sock_cookie;
	struct net *net;
	int idx;

	if (!sk)
		return 0;

	sock_cookie = bpf_get_socket_cookie(sk);
	sk = bpf_core_cast(sk, struct sock);
	if (sk->sk_family != sf ||
	    (ss && sk->sk_state != ss) ||
	    (sk->sk_family == AF_INET6 ?
	    !ipv6_addr_loopback(&sk->sk_v6_rcv_saddr) :
	    !ipv4_addr_loopback(sk->sk_rcv_saddr)))
		return 0;

	if (sk->sk_num == ports[0])
		idx = 0;
	else if (sk->sk_num == ports[1])
		idx = 1;
	else if (!ports[0] && !ports[1])
		idx = 0;
	else
		return 0;

	/* bucket selection as in inet_lhash2_bucket_sk() */
	net = sk->sk_net.net;
	hash = jhash2(sk->sk_v6_rcv_saddr.s6_addr32, 4, net->hash_mix);
	hash ^= sk->sk_num;
	hinfo = net->ipv4.tcp_death_row.hashinfo;
	bucket[idx] = hash & hinfo->lhash2_mask;
	bpf_seq_write(ctx->meta->seq, &idx, sizeof(idx));
	bpf_seq_write(ctx->meta->seq, &sock_cookie, sizeof(sock_cookie));

	return 0;
}

volatile const __u64 destroy_cookie;

SEC("iter/tcp")
int iter_tcp_destroy(struct bpf_iter__tcp *ctx)
{
	struct sock_common *sk_common = (struct sock_common *)ctx->sk_common;
	__u64 sock_cookie;

	if (!sk_common)
		return 0;

	sock_cookie = bpf_get_socket_cookie(sk_common);
	if (sock_cookie != destroy_cookie)
		return 0;

	bpf_sock_destroy(sk_common);
	bpf_seq_write(ctx->meta->seq, &sock_cookie, sizeof(sock_cookie));

	return 0;
}

#define udp_sk(ptr) container_of(ptr, struct udp_sock, inet.sk)

SEC("iter/udp")
int iter_udp_soreuse(struct bpf_iter__udp *ctx)
{
	struct sock *sk = (struct sock *)ctx->udp_sk;
	struct udp_table *udptable;
	__u64 sock_cookie;
	int idx;

	if (!sk)
		return 0;

	sock_cookie = bpf_get_socket_cookie(sk);
	sk = bpf_core_cast(sk, struct sock);
	if (sk->sk_family != sf ||
	    (sk->sk_family == AF_INET6 ?
	    !ipv6_addr_loopback(&sk->sk_v6_rcv_saddr) :
	    !ipv4_addr_loopback(sk->sk_rcv_saddr)))
		return 0;

	if (sk->sk_num == ports[0])
		idx = 0;
	else if (sk->sk_num == ports[1])
		idx = 1;
	else if (!ports[0] && !ports[1])
		idx = 0;
	else
		return 0;

	/* bucket selection as in udp_hashslot2() */
	udptable = sk->sk_net.net->ipv4.udp_table;
	bucket[idx] = udp_sk(sk)->udp_portaddr_hash & udptable->mask;
	bpf_seq_write(ctx->meta->seq, &idx, sizeof(idx));
	bpf_seq_write(ctx->meta->seq, &sock_cookie, sizeof(sock_cookie));

	return 0;
}

char _license[] SEC("license") = "GPL";
