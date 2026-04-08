// SPDX-License-Identifier: GPL-2.0

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

void *user_ptr = 0;

SEC("kprobe.multi")
int handle_kprobe_multi_sleepable(struct pt_regs *ctx)
{
	int a, err;

	err = bpf_copy_from_user(&a, sizeof(a), user_ptr);
	barrier_var(a);
	return err;
}

SEC("fentry/bpf_fentry_test1")
int BPF_PROG(fentry)
{
	return 0;
}

char _license[] SEC("license") = "GPL";
