// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2020 Google LLC.
 */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include  <errno.h>

char _license[] SEC("license") = "GPL";

int monitored_pid = 0;
int mprotect_count = 0;
int bprm_count = 0;

SEC("lsm/file_mprotect")
int BPF_PROG(test_int_hook, struct vm_area_struct *vma,
	     unsigned long reqprot, unsigned long prot, int ret)
{
	if (ret != 0)
		return ret;

	__u32 pid = bpf_get_current_pid_tgid() >> 32;
	int is_stack = 0;

	is_stack = (vma->vm_start <= vma->vm_mm->start_stack &&
		    vma->vm_end >= vma->vm_mm->start_stack);

	if (is_stack && monitored_pid == pid) {
		mprotect_count++;
		ret = -EPERM;
	}

	return ret;
}

SEC("lsm/bprm_committed_creds")
int BPF_PROG(test_void_hook, struct linux_binprm *bprm)
{
	__u32 pid = bpf_get_current_pid_tgid() >> 32;

	if (monitored_pid == pid)
		bprm_count++;

	return 0;
}
