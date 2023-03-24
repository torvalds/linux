// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <errno.h>
#include <linux/capability.h>

typedef struct { unsigned long long val; } kernel_cap_t;

struct cred {
	kernel_cap_t cap_effective;
} __attribute__((preserve_access_index));

char _license[] SEC("license") = "GPL";

SEC("lsm.s/userns_create")
int BPF_PROG(test_userns_create, const struct cred *cred, int ret)
{
	kernel_cap_t caps = cred->cap_effective;
	__u64 cap_mask = 1ULL << CAP_SYS_ADMIN;

	if (ret)
		return 0;

	ret = -EPERM;
	if (caps.val & cap_mask)
		return 0;

	return -EPERM;
}
