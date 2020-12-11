// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include "../bpf_testmod/bpf_testmod.h"

__u32 raw_tp_read_sz = 0;

SEC("raw_tp/bpf_testmod_test_read")
int BPF_PROG(handle_raw_tp,
	     struct task_struct *task, struct bpf_testmod_test_read_ctx *read_ctx)
{
	raw_tp_read_sz = BPF_CORE_READ(read_ctx, len);
	return 0;
}

__u32 tp_btf_read_sz = 0;

SEC("tp_btf/bpf_testmod_test_read")
int BPF_PROG(handle_tp_btf,
	     struct task_struct *task, struct bpf_testmod_test_read_ctx *read_ctx)
{
	tp_btf_read_sz = read_ctx->len;
	return 0;
}

__u32 fentry_read_sz = 0;

SEC("fentry/bpf_testmod_test_read")
int BPF_PROG(handle_fentry,
	     struct file *file, struct kobject *kobj,
	     struct bin_attribute *bin_attr, char *buf, loff_t off, size_t len)
{
	fentry_read_sz = len;
	return 0;
}

__u32 fentry_manual_read_sz = 0;

SEC("fentry/placeholder")
int BPF_PROG(handle_fentry_manual,
	     struct file *file, struct kobject *kobj,
	     struct bin_attribute *bin_attr, char *buf, loff_t off, size_t len)
{
	fentry_manual_read_sz = len;
	return 0;
}

__u32 fexit_read_sz = 0;
int fexit_ret = 0;

SEC("fexit/bpf_testmod_test_read")
int BPF_PROG(handle_fexit,
	     struct file *file, struct kobject *kobj,
	     struct bin_attribute *bin_attr, char *buf, loff_t off, size_t len,
	     int ret)
{
	fexit_read_sz = len;
	fexit_ret = ret;
	return 0;
}

__u32 fmod_ret_read_sz = 0;

SEC("fmod_ret/bpf_testmod_test_read")
int BPF_PROG(handle_fmod_ret,
	     struct file *file, struct kobject *kobj,
	     struct bin_attribute *bin_attr, char *buf, loff_t off, size_t len)
{
	fmod_ret_read_sz = len;
	return 0; /* don't override the exit code */
}

char _license[] SEC("license") = "GPL";
