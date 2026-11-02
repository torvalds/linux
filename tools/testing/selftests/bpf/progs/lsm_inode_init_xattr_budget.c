// SPDX-License-Identifier: GPL-2.0

#include "vmlinux.h"
#include <errno.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>
#include "bpf_kfuncs.h"

char _license[] SEC("license") = "GPL";

const char xattr_name[32] = "security.bpf.slot";

__u32 monitored_pid;
__u32 hook_ran;
__s32 claim_err = 1;

char claim_value[] = "v";

SEC("lsm/inode_init_security")
int BPF_PROG(claim_one, struct inode *inode, struct inode *dir,
	     const struct qstr *qstr, struct xattr *xattrs, int *xattr_count)
{
	struct bpf_dynptr value;

	if ((bpf_get_current_pid_tgid() >> 32) != monitored_pid)
		return 0;

	hook_ran = 1;
	if (!xattrs) {
		claim_err = -EOPNOTSUPP;
		return 0;
	}
	bpf_dynptr_from_mem(claim_value, sizeof(claim_value), 0, &value);
	claim_err = bpf_inode_init_xattr(xattrs, xattr_count, xattr_name,
					 &value);
	return 0;
}
