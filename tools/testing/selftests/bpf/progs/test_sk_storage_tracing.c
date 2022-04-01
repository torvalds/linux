// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */

#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_helpers.h>

struct sk_stg {
	__u32 pid;
	__u32 last_notclose_state;
	char comm[16];
};

struct {
	__uint(type, BPF_MAP_TYPE_SK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, struct sk_stg);
} sk_stg_map SEC(".maps");

/* Testing delete */
struct {
	__uint(type, BPF_MAP_TYPE_SK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, int);
} del_sk_stg_map SEC(".maps");

char task_comm[16] = "";

SEC("tp_btf/inet_sock_set_state")
int BPF_PROG(trace_inet_sock_set_state, struct sock *sk, int oldstate,
	     int newstate)
{
	struct sk_stg *stg;

	if (newstate == BPF_TCP_CLOSE)
		return 0;

	stg = bpf_sk_storage_get(&sk_stg_map, sk, 0,
				 BPF_SK_STORAGE_GET_F_CREATE);
	if (!stg)
		return 0;

	stg->last_notclose_state = newstate;

	bpf_sk_storage_delete(&del_sk_stg_map, sk);

	return 0;
}

static void set_task_info(struct sock *sk)
{
	struct task_struct *task;
	struct sk_stg *stg;

	stg = bpf_sk_storage_get(&sk_stg_map, sk, 0,
				 BPF_SK_STORAGE_GET_F_CREATE);
	if (!stg)
		return;

	stg->pid = bpf_get_current_pid_tgid();

	task = (struct task_struct *)bpf_get_current_task();
	bpf_core_read_str(&stg->comm, sizeof(stg->comm), &task->comm);
	bpf_core_read_str(&task_comm, sizeof(task_comm), &task->comm);
}

SEC("fentry/inet_csk_listen_start")
int BPF_PROG(trace_inet_csk_listen_start, struct sock *sk)
{
	set_task_info(sk);

	return 0;
}

SEC("fentry/tcp_connect")
int BPF_PROG(trace_tcp_connect, struct sock *sk)
{
	set_task_info(sk);

	return 0;
}

SEC("fexit/inet_csk_accept")
int BPF_PROG(inet_csk_accept, struct sock *sk, int flags, int *err, bool kern,
	     struct sock *accepted_sk)
{
	set_task_info(accepted_sk);

	return 0;
}

char _license[] SEC("license") = "GPL";
