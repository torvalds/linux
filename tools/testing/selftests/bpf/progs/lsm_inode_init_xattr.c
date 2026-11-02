// SPDX-License-Identifier: GPL-2.0

#include "vmlinux.h"
#include <errno.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>
#include "bpf_kfuncs.h"

char _license[] SEC("license") = "GPL";

#define LABEL_MAX	32

struct label {
	char	v[LABEL_MAX];
	__u32	len;
};

struct {
	__uint(type, BPF_MAP_TYPE_INODE_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, struct label);
} inode_zone SEC(".maps");

__u32 monitored_pid;

const char xattr_zone[]     = "security.bpf.zone";
const char xattr_origin[]   = "security.bpf.origin";
const char xattr_overflow[] = "security.bpf.overflow";
/* One byte over XATTR_NAME_MAX once "security." is prepended again. */
const char xattr_toolong[] = "security.bpf." "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
const char xattr_selinux[]  = "security.selinux";
const char xattr_user[]     = "user.zone";

char zone_scratch[LABEL_MAX];
char origin_value[] = "created";

__u32 hook_ran;
__s32 zone_err = 1;
__s32 origin_err = 1;
__s32 overflow_err = 1;
__s32 toolong_err = 1;
__s32 selinux_err = 1;
__s32 user_err = 1;
__u32 inherited;

SEC("lsm/inode_init_security")
int BPF_PROG(init_label, struct inode *inode, struct inode *dir,
	     const struct qstr *qstr, struct xattr *xattrs, int *xattr_count)
{
	struct bpf_dynptr value;
	struct label *parent;
	int len = 0;

	if ((bpf_get_current_pid_tgid() >> 32) != monitored_pid)
		return 0;

	hook_ran = 1;
	if (!xattrs) {
		/* The filesystem takes no xattrs at inode creation. */
		zone_err = -EOPNOTSUPP;
		return 0;
	}
	__builtin_memset(zone_scratch, 0, LABEL_MAX);

	if (dir) {
		parent = bpf_inode_storage_get(&inode_zone, dir, 0, 0);
		if (parent && parent->len > 0 && parent->len <= LABEL_MAX) {
			len = parent->len;
			__builtin_memcpy(zone_scratch, parent->v, LABEL_MAX);
			inherited = 1;
		}
	}
	if (!len) {
		__builtin_memcpy(zone_scratch, "default", sizeof("default"));
		len = sizeof("default");
	}

	bpf_dynptr_from_mem(zone_scratch, len, 0, &value);
	/* Refused before a slot is claimed, so the budget below is intact. */
	toolong_err = bpf_inode_init_xattr(xattrs, xattr_count, xattr_toolong,
					   &value);
	zone_err = bpf_inode_init_xattr(xattrs, xattr_count, xattr_zone,
					&value);

	bpf_dynptr_from_mem(origin_value, sizeof(origin_value), 0, &value);
	origin_err = bpf_inode_init_xattr(xattrs, xattr_count, xattr_origin,
					  &value);

	overflow_err = bpf_inode_init_xattr(xattrs, xattr_count,
					    xattr_overflow, &value);

	selinux_err = bpf_inode_init_xattr(xattrs, xattr_count, xattr_selinux,
					   &value);
	user_err = bpf_inode_init_xattr(xattrs, xattr_count, xattr_user,
					&value);
	return 0;
}
