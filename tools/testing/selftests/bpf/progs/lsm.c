// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2020 Google LLC.
 */

#include "vmlinux.h"
#include <errno.h>
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u64);
} array SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u64);
} hash SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u64);
} lru_hash SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u64);
} percpu_array SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_HASH);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u64);
} percpu_hash SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_PERCPU_HASH);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u64);
} lru_percpu_hash SEC(".maps");

struct inner_map {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, __u64);
} inner_map SEC(".maps");

struct outer_arr {
	__uint(type, BPF_MAP_TYPE_ARRAY_OF_MAPS);
	__uint(max_entries, 1);
	__uint(key_size, sizeof(int));
	__uint(value_size, sizeof(int));
	__array(values, struct inner_map);
} outer_arr SEC(".maps") = {
	.values = { [0] = &inner_map },
};

struct outer_hash {
	__uint(type, BPF_MAP_TYPE_HASH_OF_MAPS);
	__uint(max_entries, 1);
	__uint(key_size, sizeof(int));
	__array(values, struct inner_map);
} outer_hash SEC(".maps") = {
	.values = { [0] = &inner_map },
};

char _license[] SEC("license") = "GPL";

int monitored_pid = 0;
int mprotect_count = 0;
int bprm_count = 0;
__u32 inode_identity_idmap_seen = 0;
__u32 inode_idmapped_mount_seen = 0;

enum {
	INODE_IDMAP_CREATE	= 1U << 0,
	INODE_IDMAP_LINK	= 1U << 1,
	INODE_IDMAP_SYMLINK	= 1U << 2,
	INODE_IDMAP_MKDIR	= 1U << 3,
	INODE_IDMAP_MKNOD	= 1U << 4,
	INODE_IDMAP_PERMISSION	= 1U << 5,
};

static __always_inline bool is_monitored_idmap(struct mnt_idmap *idmap)
{
	__u32 pid = bpf_get_current_pid_tgid() >> 32;

	return monitored_pid == pid && idmap;
}

static __always_inline bool is_identity_idmap(struct mnt_idmap *idmap)
{
	return idmap->uid_map.nr_extents == 0 &&
	       idmap->gid_map.nr_extents == 0;
}

static __always_inline int record_inode_idmap(struct mnt_idmap *idmap,
					      __u32 hook, int ret)
{
	if (ret || !is_monitored_idmap(idmap))
		return ret;
	if (is_identity_idmap(idmap))
		inode_identity_idmap_seen |= hook;
	else
		inode_idmapped_mount_seen |= hook;
	return 0;
}

SEC("lsm/inode_create")
int BPF_PROG(test_inode_create, struct mnt_idmap *idmap, struct inode *dir,
	     struct dentry *dentry, umode_t mode, int ret)
{
	return record_inode_idmap(idmap, INODE_IDMAP_CREATE, ret);
}

SEC("lsm/inode_link")
int BPF_PROG(test_inode_link, struct mnt_idmap *idmap,
	     struct dentry *old_dentry, struct inode *dir,
	     struct dentry *new_dentry, int ret)
{
	return record_inode_idmap(idmap, INODE_IDMAP_LINK, ret);
}

SEC("lsm/inode_symlink")
int BPF_PROG(test_inode_symlink, struct mnt_idmap *idmap, struct inode *dir,
	     struct dentry *dentry, const char *old_name, int ret)
{
	return record_inode_idmap(idmap, INODE_IDMAP_SYMLINK, ret);
}

SEC("lsm/inode_mkdir")
int BPF_PROG(test_inode_mkdir, struct mnt_idmap *idmap, struct inode *dir,
	     struct dentry *dentry, umode_t mode, int ret)
{
	return record_inode_idmap(idmap, INODE_IDMAP_MKDIR, ret);
}

SEC("lsm/inode_mknod")
int BPF_PROG(test_inode_mknod, struct mnt_idmap *idmap, struct inode *dir,
	     struct dentry *dentry, umode_t mode, dev_t dev, int ret)
{
	return record_inode_idmap(idmap, INODE_IDMAP_MKNOD, ret);
}

SEC("lsm/inode_permission")
int BPF_PROG(test_inode_permission, struct mnt_idmap *idmap,
	     struct inode *inode, int mask, int ret)
{
	return record_inode_idmap(idmap, INODE_IDMAP_PERMISSION, ret);
}

SEC("lsm/file_mprotect")
int BPF_PROG(test_int_hook, struct vm_area_struct *vma,
	     unsigned long reqprot, unsigned long prot, int ret)
{
	struct mm_struct *mm = vma->vm_mm;

	if (ret != 0 || !mm)
		return ret;

	__s32 pid = bpf_get_current_pid_tgid() >> 32;
	int is_stack = 0;

	is_stack = (vma->vm_start <= mm->start_stack &&
		    vma->vm_end >= mm->start_stack);

	if (is_stack && monitored_pid == pid) {
		mprotect_count++;
		ret = -EPERM;
	}

	return ret;
}

SEC("lsm.s/bprm_committed_creds")
int BPF_PROG(test_void_hook, struct linux_binprm *bprm)
{
	__u32 pid = bpf_get_current_pid_tgid() >> 32;
	struct inner_map *inner_map;
	char args[64];
	__u32 key = 0;
	__u64 *value;

	if (monitored_pid == pid)
		bprm_count++;

	bpf_copy_from_user(args, sizeof(args), (void *)bprm->vma->vm_mm->arg_start);
	bpf_copy_from_user(args, sizeof(args), (void *)bprm->mm->arg_start);

	value = bpf_map_lookup_elem(&array, &key);
	if (value)
		*value = 0;
	value = bpf_map_lookup_elem(&hash, &key);
	if (value)
		*value = 0;
	value = bpf_map_lookup_elem(&lru_hash, &key);
	if (value)
		*value = 0;
	value = bpf_map_lookup_elem(&percpu_array, &key);
	if (value)
		*value = 0;
	value = bpf_map_lookup_elem(&percpu_hash, &key);
	if (value)
		*value = 0;
	value = bpf_map_lookup_elem(&lru_percpu_hash, &key);
	if (value)
		*value = 0;
	inner_map = bpf_map_lookup_elem(&outer_arr, &key);
	if (inner_map) {
		value = bpf_map_lookup_elem(inner_map, &key);
		if (value)
			*value = 0;
	}
	inner_map = bpf_map_lookup_elem(&outer_hash, &key);
	if (inner_map) {
		value = bpf_map_lookup_elem(inner_map, &key);
		if (value)
			*value = 0;
	}

	return 0;
}
SEC("lsm/task_free") /* lsm/ is ok, lsm.s/ fails */
int BPF_PROG(test_task_free, struct task_struct *task)
{
	return 0;
}

int copy_test = 0;

SEC("fentry.s/" SYS_PREFIX "sys_setdomainname")
int BPF_PROG(test_sys_setdomainname, struct pt_regs *regs)
{
	void *ptr = (void *)PT_REGS_PARM1_SYSCALL(regs);
	int len = PT_REGS_PARM2_SYSCALL(regs);
	int buf = 0;
	long ret;

	ret = bpf_copy_from_user(&buf, sizeof(buf), ptr);
	if (len == -2 && ret == 0 && buf == 1234)
		copy_test++;
	if (len == -3 && ret == -EFAULT)
		copy_test++;
	if (len == -4 && ret == -EFAULT)
		copy_test++;
	return 0;
}
