// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020, Tessares SA. */
/* Copyright (c) 2022, SUSE. */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_tcp_helpers.h"

char _license[] SEC("license") = "GPL";

struct mptcp_storage {
	__u32 invoked;
	__u32 is_mptcp;
};

struct {
	__uint(type, BPF_MAP_TYPE_SK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, struct mptcp_storage);
} socket_storage_map SEC(".maps");

SEC("sockops")
int _sockops(struct bpf_sock_ops *ctx)
{
	struct mptcp_storage *storage;
	int op = (int)ctx->op;
	struct tcp_sock *tsk;
	struct bpf_sock *sk;
	bool is_mptcp;

	if (op != BPF_SOCK_OPS_TCP_CONNECT_CB)
		return 1;

	sk = ctx->sk;
	if (!sk)
		return 1;

	tsk = bpf_skc_to_tcp_sock(sk);
	if (!tsk)
		return 1;

	is_mptcp = bpf_core_field_exists(tsk->is_mptcp) ? tsk->is_mptcp : 0;
	storage = bpf_sk_storage_get(&socket_storage_map, sk, 0,
				     BPF_SK_STORAGE_GET_F_CREATE);
	if (!storage)
		return 1;

	storage->invoked++;
	storage->is_mptcp = is_mptcp;

	return 1;
}
