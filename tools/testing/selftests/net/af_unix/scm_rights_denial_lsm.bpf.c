// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <linux/errno.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

struct inode {
	unsigned long i_ino;
} __attribute__((preserve_access_index));

struct file {
	struct inode *f_inode;
} __attribute__((preserve_access_index));

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 16);
	__type(key, __u64);	/* inode number */
	__type(value, __u32);	/* tgid of the receiver being tested */
} denied_inodes SEC(".maps");

SEC("lsm/file_receive")
int BPF_PROG(scm_rights_deny, struct file *file)
{
	__u32 tgid = bpf_get_current_pid_tgid() >> 32;
	__u64 ino = file->f_inode->i_ino;
	__u32 *owner;

	owner = bpf_map_lookup_elem(&denied_inodes, &ino);
	if (owner && *owner == tgid)
		return -EPERM;

	return 0;
}
