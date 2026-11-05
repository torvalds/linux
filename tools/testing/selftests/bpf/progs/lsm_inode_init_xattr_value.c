// SPDX-License-Identifier: GPL-2.0

#include "vmlinux.h"
#include <errno.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>
#include "bpf_kfuncs.h"

char _license[] SEC("license") = "GPL";

#define VALUE_MAX	(64 * 1024 + 1)

const char xattr_value[] = "security.bpf.value";

/* Shape of the value handed to the kfunc, and what the hook returns after. */
const volatile __u32 value_len;
const volatile __u64 dynptr_flags;
const volatile __s32 hook_retval;

char value_src[VALUE_MAX];

__u32 monitored_pid;
__u32 hook_ran;
__s32 claim_err = 1;

SEC("lsm/inode_init_security")
int BPF_PROG(claim_value, struct inode *inode, struct inode *dir,
	     const struct qstr *qstr, struct xattr *xattrs, int *xattr_count)
{
	struct bpf_dynptr value;
	__u32 len = value_len;

	if ((bpf_get_current_pid_tgid() >> 32) != monitored_pid)
		return 0;
	if (len > VALUE_MAX)
		return 0;

	hook_ran = 1;
	if (!xattrs) {
		claim_err = -EOPNOTSUPP;
		return 0;
	}
	bpf_dynptr_from_mem(value_src, len, dynptr_flags, &value);
	claim_err = bpf_inode_init_xattr(xattrs, xattr_count, xattr_value,
					 &value);
	return hook_retval;
}
