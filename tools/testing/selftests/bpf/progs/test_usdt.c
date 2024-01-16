// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/usdt.bpf.h>

int my_pid;

int usdt0_called;
u64 usdt0_cookie;
int usdt0_arg_cnt;
int usdt0_arg_ret;

SEC("usdt")
int usdt0(struct pt_regs *ctx)
{
	long tmp;

	if (my_pid != (bpf_get_current_pid_tgid() >> 32))
		return 0;

	__sync_fetch_and_add(&usdt0_called, 1);

	usdt0_cookie = bpf_usdt_cookie(ctx);
	usdt0_arg_cnt = bpf_usdt_arg_cnt(ctx);
	/* should return -ENOENT for any arg_num */
	usdt0_arg_ret = bpf_usdt_arg(ctx, bpf_get_prandom_u32(), &tmp);
	return 0;
}

int usdt3_called;
u64 usdt3_cookie;
int usdt3_arg_cnt;
int usdt3_arg_rets[3];
u64 usdt3_args[3];

SEC("usdt//proc/self/exe:test:usdt3")
int usdt3(struct pt_regs *ctx)
{
	long tmp;

	if (my_pid != (bpf_get_current_pid_tgid() >> 32))
		return 0;

	__sync_fetch_and_add(&usdt3_called, 1);

	usdt3_cookie = bpf_usdt_cookie(ctx);
	usdt3_arg_cnt = bpf_usdt_arg_cnt(ctx);

	usdt3_arg_rets[0] = bpf_usdt_arg(ctx, 0, &tmp);
	usdt3_args[0] = (int)tmp;

	usdt3_arg_rets[1] = bpf_usdt_arg(ctx, 1, &tmp);
	usdt3_args[1] = (long)tmp;

	usdt3_arg_rets[2] = bpf_usdt_arg(ctx, 2, &tmp);
	usdt3_args[2] = (uintptr_t)tmp;

	return 0;
}

int usdt12_called;
u64 usdt12_cookie;
int usdt12_arg_cnt;
u64 usdt12_args[12];

SEC("usdt//proc/self/exe:test:usdt12")
int BPF_USDT(usdt12, int a1, int a2, long a3, long a4, unsigned a5,
		     long a6, __u64 a7, uintptr_t a8, int a9, short a10,
		     short a11, signed char a12)
{
	if (my_pid != (bpf_get_current_pid_tgid() >> 32))
		return 0;

	__sync_fetch_and_add(&usdt12_called, 1);

	usdt12_cookie = bpf_usdt_cookie(ctx);
	usdt12_arg_cnt = bpf_usdt_arg_cnt(ctx);

	usdt12_args[0] = a1;
	usdt12_args[1] = a2;
	usdt12_args[2] = a3;
	usdt12_args[3] = a4;
	usdt12_args[4] = a5;
	usdt12_args[5] = a6;
	usdt12_args[6] = a7;
	usdt12_args[7] = a8;
	usdt12_args[8] = a9;
	usdt12_args[9] = a10;
	usdt12_args[10] = a11;
	usdt12_args[11] = a12;
	return 0;
}

char _license[] SEC("license") = "GPL";
