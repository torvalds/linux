// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2020 Google LLC.
 */

#include "vmlinux.h"
#include <errno.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

#define DUMMY_STORAGE_VALUE 0xdeadbeef

int monitored_pid = 0;
int inode_storage_result = -1;
int sk_storage_result = -1;

struct local_storage {
	struct inode *exec_inode;
	__u32 value;
	struct bpf_spin_lock lock;
};

struct {
	__uint(type, BPF_MAP_TYPE_INODE_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, struct local_storage);
} inode_storage_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_SK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC | BPF_F_CLONE);
	__type(key, int);
	__type(value, struct local_storage);
} sk_storage_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_TASK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, struct local_storage);
} task_storage_map SEC(".maps");

SEC("lsm/inode_unlink")
int BPF_PROG(unlink_hook, struct inode *dir, struct dentry *victim)
{
	__u32 pid = bpf_get_current_pid_tgid() >> 32;
	struct local_storage *storage;
	bool is_self_unlink;

	if (pid != monitored_pid)
		return 0;

	storage = bpf_task_storage_get(&task_storage_map,
				       bpf_get_current_task_btf(), 0, 0);
	if (storage) {
		/* Don't let an executable delete itself */
		bpf_spin_lock(&storage->lock);
		is_self_unlink = storage->exec_inode == victim->d_inode;
		bpf_spin_unlock(&storage->lock);
		if (is_self_unlink)
			return -EPERM;
	}

	return 0;
}

SEC("lsm/inode_rename")
int BPF_PROG(inode_rename, struct inode *old_dir, struct dentry *old_dentry,
	     struct inode *new_dir, struct dentry *new_dentry,
	     unsigned int flags)
{
	__u32 pid = bpf_get_current_pid_tgid() >> 32;
	struct local_storage *storage;
	int err;

	/* new_dentry->d_inode can be NULL when the inode is renamed to a file
	 * that did not exist before. The helper should be able to handle this
	 * NULL pointer.
	 */
	bpf_inode_storage_get(&inode_storage_map, new_dentry->d_inode, 0,
			      BPF_LOCAL_STORAGE_GET_F_CREATE);

	storage = bpf_inode_storage_get(&inode_storage_map, old_dentry->d_inode,
					0, 0);
	if (!storage)
		return 0;

	bpf_spin_lock(&storage->lock);
	if (storage->value != DUMMY_STORAGE_VALUE)
		inode_storage_result = -1;
	bpf_spin_unlock(&storage->lock);

	err = bpf_inode_storage_delete(&inode_storage_map, old_dentry->d_inode);
	if (!err)
		inode_storage_result = err;

	return 0;
}

SEC("lsm/socket_bind")
int BPF_PROG(socket_bind, struct socket *sock, struct sockaddr *address,
	     int addrlen)
{
	__u32 pid = bpf_get_current_pid_tgid() >> 32;
	struct local_storage *storage;
	int err;

	if (pid != monitored_pid)
		return 0;

	storage = bpf_sk_storage_get(&sk_storage_map, sock->sk, 0,
				     BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (!storage)
		return 0;

	bpf_spin_lock(&storage->lock);
	if (storage->value != DUMMY_STORAGE_VALUE)
		sk_storage_result = -1;
	bpf_spin_unlock(&storage->lock);

	err = bpf_sk_storage_delete(&sk_storage_map, sock->sk);
	if (!err)
		sk_storage_result = err;

	return 0;
}

SEC("lsm/socket_post_create")
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

	bpf_spin_lock(&storage->lock);
	storage->value = DUMMY_STORAGE_VALUE;
	bpf_spin_unlock(&storage->lock);

	return 0;
}

/* This uses the local storage to remember the inode of the binary that a
 * process was originally executing.
 */
SEC("lsm/bprm_committed_creds")
void BPF_PROG(exec, struct linux_binprm *bprm)
{
	__u32 pid = bpf_get_current_pid_tgid() >> 32;
	struct local_storage *storage;

	if (pid != monitored_pid)
		return;

	storage = bpf_task_storage_get(&task_storage_map,
				       bpf_get_current_task_btf(), 0,
				       BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (storage) {
		bpf_spin_lock(&storage->lock);
		storage->exec_inode = bprm->file->f_inode;
		bpf_spin_unlock(&storage->lock);
	}

	storage = bpf_inode_storage_get(&inode_storage_map, bprm->file->f_inode,
					0, BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (!storage)
		return;

	bpf_spin_lock(&storage->lock);
	storage->value = DUMMY_STORAGE_VALUE;
	bpf_spin_unlock(&storage->lock);
}
