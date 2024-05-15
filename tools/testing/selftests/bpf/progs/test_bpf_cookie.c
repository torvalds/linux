// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <errno.h>

int my_tid;

__u64 kprobe_res;
__u64 kprobe_multi_res;
__u64 kretprobe_res;
__u64 uprobe_res;
__u64 uretprobe_res;
__u64 tp_res;
__u64 pe_res;
__u64 raw_tp_res;
__u64 tp_btf_res;
__u64 fentry_res;
__u64 fexit_res;
__u64 fmod_ret_res;
__u64 lsm_res;

static void update(void *ctx, __u64 *res)
{
	if (my_tid != (u32)bpf_get_current_pid_tgid())
		return;

	*res |= bpf_get_attach_cookie(ctx);
}

SEC("kprobe")
int handle_kprobe(struct pt_regs *ctx)
{
	update(ctx, &kprobe_res);
	return 0;
}

SEC("kretprobe")
int handle_kretprobe(struct pt_regs *ctx)
{
	update(ctx, &kretprobe_res);
	return 0;
}

SEC("uprobe")
int handle_uprobe(struct pt_regs *ctx)
{
	update(ctx, &uprobe_res);
	return 0;
}

SEC("uretprobe")
int handle_uretprobe(struct pt_regs *ctx)
{
	update(ctx, &uretprobe_res);
	return 0;
}

/* bpf_prog_array, used by kernel internally to keep track of attached BPF
 * programs to a given BPF hook (e.g., for tracepoints) doesn't allow the same
 * BPF program to be attached multiple times. So have three identical copies
 * ready to attach to the same tracepoint.
 */
SEC("tp/syscalls/sys_enter_nanosleep")
int handle_tp1(struct pt_regs *ctx)
{
	update(ctx, &tp_res);
	return 0;
}
SEC("tp/syscalls/sys_enter_nanosleep")
int handle_tp2(struct pt_regs *ctx)
{
	update(ctx, &tp_res);
	return 0;
}
SEC("tp/syscalls/sys_enter_nanosleep")
int handle_tp3(void *ctx)
{
	update(ctx, &tp_res);
	return 1;
}

SEC("perf_event")
int handle_pe(struct pt_regs *ctx)
{
	update(ctx, &pe_res);
	return 0;
}

SEC("raw_tp/sys_enter")
int handle_raw_tp(void *ctx)
{
	update(ctx, &raw_tp_res);
	return 0;
}

SEC("tp_btf/sys_enter")
int handle_tp_btf(void *ctx)
{
	update(ctx, &tp_btf_res);
	return 0;
}

SEC("fentry/bpf_fentry_test1")
int BPF_PROG(fentry_test1, int a)
{
	update(ctx, &fentry_res);
	return 0;
}

SEC("fexit/bpf_fentry_test1")
int BPF_PROG(fexit_test1, int a, int ret)
{
	update(ctx, &fexit_res);
	return 0;
}

SEC("fmod_ret/bpf_modify_return_test")
int BPF_PROG(fmod_ret_test, int _a, int *_b, int _ret)
{
	update(ctx, &fmod_ret_res);
	return 1234;
}

SEC("lsm/file_mprotect")
int BPF_PROG(test_int_hook, struct vm_area_struct *vma,
	     unsigned long reqprot, unsigned long prot, int ret)
{
	if (my_tid != (u32)bpf_get_current_pid_tgid())
		return ret;
	update(ctx, &lsm_res);
	return -EPERM;
}

char _license[] SEC("license") = "GPL";
