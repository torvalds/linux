// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */

#include "vmlinux.h"
#include <errno.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_kfuncs.h"

char _license[] SEC("license") = "GPL";

__u32 monitored_pid;
__u32 found_xattr_from_file;
__u32 found_xattr_from_dentry;

static const char expected_value[] = "hello";
char value1[32];
char value2[32];

SEC("lsm.s/file_open")
int BPF_PROG(test_file_open, struct file *f)
{
	struct bpf_dynptr value_ptr;
	__u32 pid;
	int ret;

	pid = bpf_get_current_pid_tgid() >> 32;
	if (pid != monitored_pid)
		return 0;

	bpf_dynptr_from_mem(value1, sizeof(value1), 0, &value_ptr);

	ret = bpf_get_file_xattr(f, "user.kfuncs", &value_ptr);
	if (ret != sizeof(expected_value))
		return 0;
	if (bpf_strncmp(value1, ret, expected_value))
		return 0;
	found_xattr_from_file = 1;
	return 0;
}

SEC("lsm.s/inode_getxattr")
int BPF_PROG(test_inode_getxattr, struct dentry *dentry, char *name)
{
	struct bpf_dynptr value_ptr;
	__u32 pid;
	int ret;

	pid = bpf_get_current_pid_tgid() >> 32;
	if (pid != monitored_pid)
		return 0;

	bpf_dynptr_from_mem(value2, sizeof(value2), 0, &value_ptr);

	ret = bpf_get_dentry_xattr(dentry, "user.kfuncs", &value_ptr);
	if (ret != sizeof(expected_value))
		return 0;
	if (bpf_strncmp(value2, ret, expected_value))
		return 0;
	found_xattr_from_dentry = 1;

	/* return non-zero to fail getxattr from user space */
	return -EINVAL;
}
