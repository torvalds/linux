// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */

#include "vmlinux.h"
#include "bpf_tracing_net.h"
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>

long create_errs = 0;
long create_cnts = 0;
long kmalloc_cnts = 0;
__u32 bench_pid = 0;

struct storage {
	__u8 data[64];
};

struct {
	__uint(type, BPF_MAP_TYPE_SK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, struct storage);
} sk_storage_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_TASK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, struct storage);
} task_storage_map SEC(".maps");

SEC("raw_tp/kmalloc")
int BPF_PROG(kmalloc, unsigned long call_site, const void *ptr,
	     size_t bytes_req, size_t bytes_alloc, gfp_t gfp_flags,
	     int node)
{
	__sync_fetch_and_add(&kmalloc_cnts, 1);

	return 0;
}

SEC("tp_btf/sched_process_fork")
int BPF_PROG(sched_process_fork, struct task_struct *parent, struct task_struct *child)
{
	struct storage *stg;

	if (parent->tgid != bench_pid)
		return 0;

	stg = bpf_task_storage_get(&task_storage_map, child, NULL,
				   BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (stg)
		__sync_fetch_and_add(&create_cnts, 1);
	else
		__sync_fetch_and_add(&create_errs, 1);

	return 0;
}

SEC("lsm.s/socket_post_create")
int BPF_PROG(socket_post_create, struct socket *sock, int family, int type,
	     int protocol, int kern)
{
	struct storage *stg;
	__u32 pid;

	pid = bpf_get_current_pid_tgid() >> 32;
	if (pid != bench_pid)
		return 0;

	stg = bpf_sk_storage_get(&sk_storage_map, sock->sk, NULL,
				 BPF_LOCAL_STORAGE_GET_F_CREATE);

	if (stg)
		__sync_fetch_and_add(&create_cnts, 1);
	else
		__sync_fetch_and_add(&create_errs, 1);

	return 0;
}

char __license[] SEC("license") = "GPL";
