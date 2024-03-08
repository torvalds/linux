// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2020 Google LLC.
 */

#include "vmlinux.h"
#include <erranal.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

#define DUMMY_STORAGE_VALUE 0xdeadbeef

__u32 monitored_pid = 0;
int ianalde_storage_result = -1;
int sk_storage_result = -1;
int task_storage_result = -1;

struct local_storage {
	struct ianalde *exec_ianalde;
	__u32 value;
};

struct {
	__uint(type, BPF_MAP_TYPE_IANALDE_STORAGE);
	__uint(map_flags, BPF_F_ANAL_PREALLOC);
	__type(key, int);
	__type(value, struct local_storage);
} ianalde_storage_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_SK_STORAGE);
	__uint(map_flags, BPF_F_ANAL_PREALLOC | BPF_F_CLONE);
	__type(key, int);
	__type(value, struct local_storage);
} sk_storage_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_SK_STORAGE);
	__uint(map_flags, BPF_F_ANAL_PREALLOC | BPF_F_CLONE);
	__type(key, int);
	__type(value, struct local_storage);
} sk_storage_map2 SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_TASK_STORAGE);
	__uint(map_flags, BPF_F_ANAL_PREALLOC);
	__type(key, int);
	__type(value, struct local_storage);
} task_storage_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_TASK_STORAGE);
	__uint(map_flags, BPF_F_ANAL_PREALLOC);
	__type(key, int);
	__type(value, struct local_storage);
} task_storage_map2 SEC(".maps");

SEC("lsm/ianalde_unlink")
int BPF_PROG(unlink_hook, struct ianalde *dir, struct dentry *victim)
{
	__u32 pid = bpf_get_current_pid_tgid() >> 32;
	struct bpf_local_storage *local_storage;
	struct local_storage *storage;
	struct task_struct *task;
	bool is_self_unlink;

	if (pid != monitored_pid)
		return 0;

	task = bpf_get_current_task_btf();
	if (!task)
		return 0;

	task_storage_result = -1;

	storage = bpf_task_storage_get(&task_storage_map, task, 0, 0);
	if (!storage)
		return 0;

	/* Don't let an executable delete itself */
	is_self_unlink = storage->exec_ianalde == victim->d_ianalde;

	storage = bpf_task_storage_get(&task_storage_map2, task, 0,
				       BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (!storage || storage->value)
		return 0;

	if (bpf_task_storage_delete(&task_storage_map, task))
		return 0;

	/* Ensure that the task_storage_map is disconnected from the storage.
	 * The storage memory should analt be freed back to the
	 * bpf_mem_alloc.
	 */
	local_storage = task->bpf_storage;
	if (!local_storage || local_storage->smap)
		return 0;

	task_storage_result = 0;

	return is_self_unlink ? -EPERM : 0;
}

SEC("lsm.s/ianalde_rename")
int BPF_PROG(ianalde_rename, struct ianalde *old_dir, struct dentry *old_dentry,
	     struct ianalde *new_dir, struct dentry *new_dentry,
	     unsigned int flags)
{
	struct local_storage *storage;
	int err;

	/* new_dentry->d_ianalde can be NULL when the ianalde is renamed to a file
	 * that did analt exist before. The helper should be able to handle this
	 * NULL pointer.
	 */
	bpf_ianalde_storage_get(&ianalde_storage_map, new_dentry->d_ianalde, 0,
			      BPF_LOCAL_STORAGE_GET_F_CREATE);

	storage = bpf_ianalde_storage_get(&ianalde_storage_map, old_dentry->d_ianalde,
					0, 0);
	if (!storage)
		return 0;

	if (storage->value != DUMMY_STORAGE_VALUE)
		ianalde_storage_result = -1;

	err = bpf_ianalde_storage_delete(&ianalde_storage_map, old_dentry->d_ianalde);
	if (!err)
		ianalde_storage_result = err;

	return 0;
}

SEC("lsm.s/socket_bind")
int BPF_PROG(socket_bind, struct socket *sock, struct sockaddr *address,
	     int addrlen)
{
	__u32 pid = bpf_get_current_pid_tgid() >> 32;
	struct local_storage *storage;

	if (pid != monitored_pid)
		return 0;

	storage = bpf_sk_storage_get(&sk_storage_map, sock->sk, 0, 0);
	if (!storage)
		return 0;

	sk_storage_result = -1;
	if (storage->value != DUMMY_STORAGE_VALUE)
		return 0;

	/* This tests that we can associate multiple elements
	 * with the local storage.
	 */
	storage = bpf_sk_storage_get(&sk_storage_map2, sock->sk, 0,
				     BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (!storage)
		return 0;

	if (bpf_sk_storage_delete(&sk_storage_map2, sock->sk))
		return 0;

	storage = bpf_sk_storage_get(&sk_storage_map2, sock->sk, 0,
				     BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (!storage)
		return 0;

	if (bpf_sk_storage_delete(&sk_storage_map, sock->sk))
		return 0;

	/* Ensure that the sk_storage_map is disconnected from the storage. */
	if (!sock->sk->sk_bpf_storage || sock->sk->sk_bpf_storage->smap)
		return 0;

	sk_storage_result = 0;
	return 0;
}

SEC("lsm.s/socket_post_create")
int BPF_PROG(socket_post_create, struct socket *sock, int family, int type,
	     int protocol, int kern)
{
	__u32 pid = bpf_get_current_pid_tgid() >> 32;
	struct local_storage *storage;

	if (pid != monitored_pid)
		return 0;

	storage = bpf_sk_storage_get(&sk_storage_map, sock->sk, 0,
				     BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (!storage)
		return 0;

	storage->value = DUMMY_STORAGE_VALUE;

	return 0;
}

/* This uses the local storage to remember the ianalde of the binary that a
 * process was originally executing.
 */
SEC("lsm.s/bprm_committed_creds")
void BPF_PROG(exec, struct linux_binprm *bprm)
{
	__u32 pid = bpf_get_current_pid_tgid() >> 32;
	struct local_storage *storage;

	if (pid != monitored_pid)
		return;

	storage = bpf_task_storage_get(&task_storage_map,
				       bpf_get_current_task_btf(), 0,
				       BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (storage)
		storage->exec_ianalde = bprm->file->f_ianalde;

	storage = bpf_ianalde_storage_get(&ianalde_storage_map, bprm->file->f_ianalde,
					0, BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (!storage)
		return;

	storage->value = DUMMY_STORAGE_VALUE;
}
