// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */

#include "vmlinux.h"
#include <errno.h>
#include <bpf/bpf_tracing.h>
#include "bpf_kfuncs.h"
#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";

__u32 monitored_pid;

const char xattr_foo[] = "security.bpf.foo";
const char xattr_bar[] = "security.bpf.bar";
static const char xattr_selinux[] = "security.selinux";
char value_bar[] = "world";
char read_value[32];

bool set_security_bpf_bar_success;
bool remove_security_bpf_bar_success;
bool set_security_selinux_fail;
bool remove_security_selinux_fail;

char name_buf[32];

static inline bool name_match_foo(const char *name)
{
	bpf_probe_read_kernel(name_buf, sizeof(name_buf), name);

	return !bpf_strncmp(name_buf, sizeof(xattr_foo), xattr_foo);
}

/* Test bpf_set_dentry_xattr and bpf_remove_dentry_xattr */
SEC("lsm.s/inode_getxattr")
int BPF_PROG(test_inode_getxattr, struct dentry *dentry, char *name)
{
	struct bpf_dynptr value_ptr;
	__u32 pid;
	int ret;

	pid = bpf_get_current_pid_tgid() >> 32;
	if (pid != monitored_pid)
		return 0;

	/* Only do the following for security.bpf.foo */
	if (!name_match_foo(name))
		return 0;

	bpf_dynptr_from_mem(read_value, sizeof(read_value), 0, &value_ptr);

	/* read security.bpf.bar */
	ret = bpf_get_dentry_xattr(dentry, xattr_bar, &value_ptr);

	if (ret < 0) {
		/* If security.bpf.bar doesn't exist, set it */
		bpf_dynptr_from_mem(value_bar, sizeof(value_bar), 0, &value_ptr);

		ret = bpf_set_dentry_xattr(dentry, xattr_bar, &value_ptr, 0);
		if (!ret)
			set_security_bpf_bar_success = true;
		ret = bpf_set_dentry_xattr(dentry, xattr_selinux, &value_ptr, 0);
		if (ret)
			set_security_selinux_fail = true;
	} else {
		/* If security.bpf.bar exists, remove it */
		ret = bpf_remove_dentry_xattr(dentry, xattr_bar);
		if (!ret)
			remove_security_bpf_bar_success = true;

		ret = bpf_remove_dentry_xattr(dentry, xattr_selinux);
		if (ret)
			remove_security_selinux_fail = true;
	}

	return 0;
}

bool locked_set_security_bpf_bar_success;
bool locked_remove_security_bpf_bar_success;
bool locked_set_security_selinux_fail;
bool locked_remove_security_selinux_fail;

/* Test bpf_set_dentry_xattr_locked and bpf_remove_dentry_xattr_locked.
 * It not necessary to differentiate the _locked version and the
 * not-_locked version in the BPF program. The verifier will fix them up
 * properly.
 */
SEC("lsm.s/inode_setxattr")
int BPF_PROG(test_inode_setxattr, struct mnt_idmap *idmap,
	     struct dentry *dentry, const char *name,
	     const void *value, size_t size, int flags)
{
	struct bpf_dynptr value_ptr;
	__u32 pid;
	int ret;

	pid = bpf_get_current_pid_tgid() >> 32;
	if (pid != monitored_pid)
		return 0;

	/* Only do the following for security.bpf.foo */
	if (!name_match_foo(name))
		return 0;

	bpf_dynptr_from_mem(read_value, sizeof(read_value), 0, &value_ptr);

	/* read security.bpf.bar */
	ret = bpf_get_dentry_xattr(dentry, xattr_bar, &value_ptr);

	if (ret < 0) {
		/* If security.bpf.bar doesn't exist, set it */
		bpf_dynptr_from_mem(value_bar, sizeof(value_bar), 0, &value_ptr);

		ret = bpf_set_dentry_xattr(dentry, xattr_bar, &value_ptr, 0);
		if (!ret)
			locked_set_security_bpf_bar_success = true;
		ret = bpf_set_dentry_xattr(dentry, xattr_selinux, &value_ptr, 0);
		if (ret)
			locked_set_security_selinux_fail = true;
	} else {
		/* If security.bpf.bar exists, remove it */
		ret = bpf_remove_dentry_xattr(dentry, xattr_bar);
		if (!ret)
			locked_remove_security_bpf_bar_success = true;

		ret = bpf_remove_dentry_xattr(dentry, xattr_selinux);
		if (ret)
			locked_remove_security_selinux_fail = true;
	}

	return 0;
}
