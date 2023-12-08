// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Google LLC. */
#include "bpf_iter.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

struct {
	__uint(type, BPF_MAP_TYPE_SK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, int);
} sk_stg_map SEC(".maps");

SEC("iter/bpf_sk_storage_map")
int delete_bpf_sk_storage_map(struct bpf_iter__bpf_sk_storage_map *ctx)
{
	if (ctx->sk)
		bpf_sk_storage_delete(&sk_stg_map, ctx->sk);

	return 0;
}

SEC("iter/task_file")
int fill_socket_owner(struct bpf_iter__task_file *ctx)
{
	struct task_struct *task = ctx->task;
	struct file *file = ctx->file;
	struct socket *sock;
	int *sock_tgid;

	if (!task || !file)
		return 0;

	sock = bpf_sock_from_file(file);
	if (!sock)
		return 0;

	sock_tgid = bpf_sk_storage_get(&sk_stg_map, sock->sk, 0, 0);
	if (!sock_tgid)
		return 0;

	*sock_tgid = task->tgid;

	return 0;
}

SEC("iter/tcp")
int negate_socket_local_storage(struct bpf_iter__tcp *ctx)
{
	struct sock_common *sk_common = ctx->sk_common;
	int *sock_tgid;

	if (!sk_common)
		return 0;

	sock_tgid = bpf_sk_storage_get(&sk_stg_map, sk_common, 0, 0);
	if (!sock_tgid)
		return 0;

	*sock_tgid = -*sock_tgid;

	return 0;
}
