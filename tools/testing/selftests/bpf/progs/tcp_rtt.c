// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

char _license[] SEC("license") = "GPL";
__u32 _version SEC("version") = 1;

struct tcp_rtt_storage {
	__u32 invoked;
	__u32 dsack_dups;
	__u32 delivered;
	__u32 delivered_ce;
	__u32 icsk_retransmits;
};

struct {
	__uint(type, BPF_MAP_TYPE_SK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, struct tcp_rtt_storage);
} socket_storage_map SEC(".maps");

SEC("sockops")
int _sockops(struct bpf_sock_ops *ctx)
{
	struct tcp_rtt_storage *storage;
	struct bpf_tcp_sock *tcp_sk;
	int op = (int) ctx->op;
	struct bpf_sock *sk;

	sk = ctx->sk;
	if (!sk)
		return 1;

	storage = bpf_sk_storage_get(&socket_storage_map, sk, 0,
				     BPF_SK_STORAGE_GET_F_CREATE);
	if (!storage)
		return 1;

	if (op == BPF_SOCK_OPS_TCP_CONNECT_CB) {
		bpf_sock_ops_cb_flags_set(ctx, BPF_SOCK_OPS_RTT_CB_FLAG);
		return 1;
	}

	if (op != BPF_SOCK_OPS_RTT_CB)
		return 1;

	tcp_sk = bpf_tcp_sock(sk);
	if (!tcp_sk)
		return 1;

	storage->invoked++;

	storage->dsack_dups = tcp_sk->dsack_dups;
	storage->delivered = tcp_sk->delivered;
	storage->delivered_ce = tcp_sk->delivered_ce;
	storage->icsk_retransmits = tcp_sk->icsk_retransmits;

	return 1;
}
