// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Google LLC. */

#include <vmlinux.h>
#include <errno.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <linux/limits.h>

#include "bpf_misc.h"
#include "bpf_experimental.h"

static char buf[PATH_MAX];

SEC("lsm.s/file_open")
__failure __msg("Possibly NULL pointer passed to trusted arg0")
int BPF_PROG(get_task_exe_file_kfunc_null)
{
	struct file *acquired;

	/* Can't pass a NULL pointer to bpf_get_task_exe_file(). */
	acquired = bpf_get_task_exe_file(NULL);
	if (!acquired)
		return 0;

	bpf_put_file(acquired);
	return 0;
}

SEC("lsm.s/inode_getxattr")
__failure __msg("arg#0 pointer type STRUCT task_struct must point to scalar, or struct with scalar")
int BPF_PROG(get_task_exe_file_kfunc_fp)
{
	u64 x;
	struct file *acquired;
	struct task_struct *task;

	task = (struct task_struct *)&x;
	/* Can't pass random frame pointer to bpf_get_task_exe_file(). */
	acquired = bpf_get_task_exe_file(task);
	if (!acquired)
		return 0;

	bpf_put_file(acquired);
	return 0;
}

SEC("lsm.s/file_open")
__failure __msg("R1 must be referenced or trusted")
int BPF_PROG(get_task_exe_file_kfunc_untrusted)
{
	struct file *acquired;
	struct task_struct *parent;

	/* Walking a trusted struct task_struct returned from
	 * bpf_get_current_task_btf() yields an untrusted pointer.
	 */
	parent = bpf_get_current_task_btf()->parent;
	/* Can't pass untrusted pointer to bpf_get_task_exe_file(). */
	acquired = bpf_get_task_exe_file(parent);
	if (!acquired)
		return 0;

	bpf_put_file(acquired);
	return 0;
}

SEC("lsm.s/file_open")
__failure __msg("Unreleased reference")
int BPF_PROG(get_task_exe_file_kfunc_unreleased)
{
	struct file *acquired;

	acquired = bpf_get_task_exe_file(bpf_get_current_task_btf());
	if (!acquired)
		return 0;

	/* Acquired but never released. */
	return 0;
}

SEC("lsm.s/file_open")
__failure __msg("release kernel function bpf_put_file expects")
int BPF_PROG(put_file_kfunc_unacquired, struct file *file)
{
	/* Can't release an unacquired pointer. */
	bpf_put_file(file);
	return 0;
}

SEC("lsm.s/file_open")
__failure __msg("Possibly NULL pointer passed to trusted arg0")
int BPF_PROG(path_d_path_kfunc_null)
{
	/* Can't pass NULL value to bpf_path_d_path() kfunc. */
	bpf_path_d_path(NULL, buf, sizeof(buf));
	return 0;
}

SEC("lsm.s/task_alloc")
__failure __msg("R1 must be referenced or trusted")
int BPF_PROG(path_d_path_kfunc_untrusted_from_argument, struct task_struct *task)
{
	struct path *root;

	/* Walking a trusted argument typically yields an untrusted
	 * pointer. This is one example of that.
	 */
	root = &task->fs->root;
	bpf_path_d_path(root, buf, sizeof(buf));
	return 0;
}

SEC("lsm.s/file_open")
__failure __msg("R1 must be referenced or trusted")
int BPF_PROG(path_d_path_kfunc_untrusted_from_current)
{
	struct path *pwd;
	struct task_struct *current;

	current = bpf_get_current_task_btf();
	/* Walking a trusted pointer returned from bpf_get_current_task_btf()
	 * yields an untrusted pointer.
	 */
	pwd = &current->fs->pwd;
	bpf_path_d_path(pwd, buf, sizeof(buf));
	return 0;
}

SEC("lsm.s/file_open")
__failure __msg("kernel function bpf_path_d_path args#0 expected pointer to STRUCT path but R1 has a pointer to STRUCT file")
int BPF_PROG(path_d_path_kfunc_type_mismatch, struct file *file)
{
	bpf_path_d_path((struct path *)&file->f_task_work, buf, sizeof(buf));
	return 0;
}

SEC("lsm.s/file_open")
__failure __msg("invalid access to map value, value_size=4096 off=0 size=8192")
int BPF_PROG(path_d_path_kfunc_invalid_buf_sz, struct file *file)
{
	/* bpf_path_d_path() enforces a constraint on the buffer size supplied
	 * by the BPF LSM program via the __sz annotation. buf here is set to
	 * PATH_MAX, so let's ensure that the BPF verifier rejects BPF_PROG_LOAD
	 * attempts if the supplied size and the actual size of the buffer
	 * mismatches.
	 */
	bpf_path_d_path(&file->f_path, buf, PATH_MAX * 2);
	return 0;
}

SEC("fentry/vfs_open")
__failure __msg("calling kernel function bpf_path_d_path is not allowed")
int BPF_PROG(path_d_path_kfunc_non_lsm, struct path *path, struct file *f)
{
	/* Calling bpf_path_d_path() from a non-LSM BPF program isn't permitted.
	 */
	bpf_path_d_path(path, buf, sizeof(buf));
	return 0;
}

SEC("lsm.s/inode_rename")
__failure __msg("invalid mem access 'trusted_ptr_or_null_'")
int BPF_PROG(inode_rename, struct inode *old_dir, struct dentry *old_dentry,
	     struct inode *new_dir, struct dentry *new_dentry,
	     unsigned int flags)
{
	struct inode *inode = new_dentry->d_inode;
	ino_t ino;

	ino = inode->i_ino;
	if (ino == 0)
		return -EACCES;
	return 0;
}
char _license[] SEC("license") = "GPL";
