// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */

#include "vmlinux.h"
#include <asm/unistd.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

#define MY_TV_NSEC 1337

bool tp_called = false;
bool raw_tp_called = false;
bool tp_btf_called = false;
bool kprobe_called = false;
bool fentry_called = false;

SEC("tp/syscalls/sys_enter_nanosleep")
int handle__tp(struct trace_event_raw_sys_enter *args)
{
	struct __kernel_timespec *ts;
	long tv_nsec;

	if (args->id != __NR_nanosleep)
		return 0;

	ts = (void *)args->args[0];
	if (bpf_probe_read_user(&tv_nsec, sizeof(ts->tv_nsec), &ts->tv_nsec) ||
	    tv_nsec != MY_TV_NSEC)
		return 0;

	tp_called = true;
	return 0;
}

SEC("raw_tp/sys_enter")
int BPF_PROG(handle__raw_tp, struct pt_regs *regs, long id)
{
	struct __kernel_timespec *ts;
	long tv_nsec;

	if (id != __NR_nanosleep)
		return 0;

	ts = (void *)PT_REGS_PARM1_CORE(regs);
	if (bpf_probe_read_user(&tv_nsec, sizeof(ts->tv_nsec), &ts->tv_nsec) ||
	    tv_nsec != MY_TV_NSEC)
		return 0;

	raw_tp_called = true;
	return 0;
}

SEC("tp_btf/sys_enter")
int BPF_PROG(handle__tp_btf, struct pt_regs *regs, long id)
{
	struct __kernel_timespec *ts;
	long tv_nsec;

	if (id != __NR_nanosleep)
		return 0;

	ts = (void *)PT_REGS_PARM1_CORE(regs);
	if (bpf_probe_read_user(&tv_nsec, sizeof(ts->tv_nsec), &ts->tv_nsec) ||
	    tv_nsec != MY_TV_NSEC)
		return 0;

	tp_btf_called = true;
	return 0;
}

SEC("kprobe/hrtimer_start_range_ns")
int BPF_KPROBE(handle__kprobe, struct hrtimer *timer, ktime_t tim, u64 delta_ns,
	       const enum hrtimer_mode mode)
{
	if (tim == MY_TV_NSEC)
		kprobe_called = true;
	return 0;
}

SEC("fentry/hrtimer_start_range_ns")
int BPF_PROG(handle__fentry, struct hrtimer *timer, ktime_t tim, u64 delta_ns,
	     const enum hrtimer_mode mode)
{
	if (tim == MY_TV_NSEC)
		fentry_called = true;
	return 0;
}

char _license[] SEC("license") = "GPL";
