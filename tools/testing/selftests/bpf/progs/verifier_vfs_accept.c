// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Google LLC. */

#include <vmlinux.h>
#include <errno.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#include "bpf_misc.h"
#include "bpf_experimental.h"

static char buf[64];

SEC("lsm.s/file_open")
__success
int BPF_PROG(get_task_exe_file_and_put_kfunc_from_current_sleepable)
{
	struct file *acquired;

	acquired = bpf_get_task_exe_file(bpf_get_current_task_btf());
	if (!acquired)
		return 0;

	bpf_put_file(acquired);
	return 0;
}

SEC("lsm/file_open")
__success
int BPF_PROG(get_task_exe_file_and_put_kfunc_from_current_non_sleepable, struct file *file)
{
	struct file *acquired;

	acquired = bpf_get_task_exe_file(bpf_get_current_task_btf());
	if (!acquired)
		return 0;

	bpf_put_file(acquired);
	return 0;
}

SEC("lsm.s/task_alloc")
__success
int BPF_PROG(get_task_exe_file_and_put_kfunc_from_argument,
	     struct task_struct *task)
{
	struct file *acquired;

	acquired = bpf_get_task_exe_file(task);
	if (!acquired)
		return 0;

	bpf_put_file(acquired);
	return 0;
}

SEC("lsm.s/inode_getattr")
__success
int BPF_PROG(path_d_path_from_path_argument, struct path *path)
{
	int ret;

	ret = bpf_path_d_path(path, buf, sizeof(buf));
	__sink(ret);
	return 0;
}

SEC("lsm.s/file_open")
__success
int BPF_PROG(path_d_path_from_file_argument, struct file *file)
{
	int ret;
	struct path *path;

	/* The f_path member is a path which is embedded directly within a
	 * file. Therefore, a pointer to such embedded members are still
	 * recognized by the BPF verifier as being PTR_TRUSTED as it's
	 * essentially PTR_TRUSTED w/ a non-zero fixed offset.
	 */
	path = &file->f_path;
	ret = bpf_path_d_path(path, buf, sizeof(buf));
	__sink(ret);
	return 0;
}

SEC("lsm.s/inode_rename")
__success
int BPF_PROG(inode_rename, struct inode *old_dir, struct dentry *old_dentry,
	     struct inode *new_dir, struct dentry *new_dentry,
	     unsigned int flags)
{
	struct inode *inode = new_dentry->d_inode;
	ino_t ino;

	if (!inode)
		return 0;
	ino = inode->i_ino;
	if (ino == 0)
		return -EACCES;
	return 0;
}

char _license[] SEC("license") = "GPL";
