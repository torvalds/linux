// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include "../test_kmods/bpf_testmod.h"

__u32 sz = 0;

SEC("?raw_tp/bpf_testmod_test_read")
int BPF_PROG(handle_raw_tp,
	     struct task_struct *task, struct bpf_testmod_test_read_ctx *read_ctx)
{
	sz = BPF_CORE_READ(read_ctx, len);
	return 0;
}

SEC("?raw_tp/bpf_testmod_test_write_bare_tp")
int BPF_PROG(handle_raw_tp_bare,
	     struct task_struct *task, struct bpf_testmod_test_write_ctx *write_ctx)
{
	sz = BPF_CORE_READ(write_ctx, len);
	return 0;
}

int raw_tp_writable_bare_in_val = 0;
int raw_tp_writable_bare_early_ret = 0;
int raw_tp_writable_bare_out_val = 0;

SEC("?raw_tp.w/bpf_testmod_test_writable_bare_tp")
int BPF_PROG(handle_raw_tp_writable_bare,
	     struct bpf_testmod_test_writable_ctx *writable)
{
	raw_tp_writable_bare_in_val = writable->val;
	writable->early_ret = raw_tp_writable_bare_early_ret;
	writable->val = raw_tp_writable_bare_out_val;
	return 0;
}

SEC("?tp_btf/bpf_testmod_test_read")
int BPF_PROG(handle_tp_btf,
	     struct task_struct *task, struct bpf_testmod_test_read_ctx *read_ctx)
{
	sz = read_ctx->len;
	return 0;
}

SEC("?fentry/bpf_testmod_test_read")
int BPF_PROG(handle_fentry,
	     struct file *file, struct kobject *kobj,
	     struct bin_attribute *bin_attr, char *buf, loff_t off, size_t len)
{
	sz = len;
	return 0;
}

SEC("?fentry")
int BPF_PROG(handle_fentry_manual,
	     struct file *file, struct kobject *kobj,
	     struct bin_attribute *bin_attr, char *buf, loff_t off, size_t len)
{
	sz = len;
	return 0;
}

SEC("?fentry/bpf_testmod:bpf_testmod_test_read")
int BPF_PROG(handle_fentry_explicit,
	     struct file *file, struct kobject *kobj,
	     struct bin_attribute *bin_attr, char *buf, loff_t off, size_t len)
{
	sz = len;
	return 0;
}


SEC("?fentry")
int BPF_PROG(handle_fentry_explicit_manual,
	     struct file *file, struct kobject *kobj,
	     struct bin_attribute *bin_attr, char *buf, loff_t off, size_t len)
{
	sz = len;
	return 0;
}

int retval = 0;

SEC("?fexit/bpf_testmod_test_read")
int BPF_PROG(handle_fexit,
	     struct file *file, struct kobject *kobj,
	     struct bin_attribute *bin_attr, char *buf, loff_t off, size_t len,
	     int ret)
{
	sz = len;
	retval = ret;
	return 0;
}

SEC("?fexit/bpf_testmod_return_ptr")
int BPF_PROG(handle_fexit_ret, int arg, struct file *ret)
{
	long buf = 0;

	bpf_probe_read_kernel(&buf, 8, ret);
	bpf_probe_read_kernel(&buf, 8, (char *)ret + 256);
	*(volatile int *)ret;
	*(volatile int *)&ret->f_mode;
	return 0;
}

SEC("?fmod_ret/bpf_testmod_test_read")
int BPF_PROG(handle_fmod_ret,
	     struct file *file, struct kobject *kobj,
	     struct bin_attribute *bin_attr, char *buf, loff_t off, size_t len)
{
	sz = len;
	return 0; /* don't override the exit code */
}

SEC("?kprobe.multi/bpf_testmod_test_read")
int BPF_PROG(kprobe_multi)
{
	return 0;
}

char _license[] SEC("license") = "GPL";
