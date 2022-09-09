// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/usdt.bpf.h>

int urand_pid;

int urand_read_without_sema_call_cnt;
int urand_read_without_sema_buf_sz_sum;

SEC("usdt/./urandom_read:urand:read_without_sema")
int BPF_USDT(urand_read_without_sema, int iter_num, int iter_cnt, int buf_sz)
{
	if (urand_pid != (bpf_get_current_pid_tgid() >> 32))
		return 0;

	__sync_fetch_and_add(&urand_read_without_sema_call_cnt, 1);
	__sync_fetch_and_add(&urand_read_without_sema_buf_sz_sum, buf_sz);

	return 0;
}

int urand_read_with_sema_call_cnt;
int urand_read_with_sema_buf_sz_sum;

SEC("usdt/./urandom_read:urand:read_with_sema")
int BPF_USDT(urand_read_with_sema, int iter_num, int iter_cnt, int buf_sz)
{
	if (urand_pid != (bpf_get_current_pid_tgid() >> 32))
		return 0;

	__sync_fetch_and_add(&urand_read_with_sema_call_cnt, 1);
	__sync_fetch_and_add(&urand_read_with_sema_buf_sz_sum, buf_sz);

	return 0;
}

int urandlib_read_without_sema_call_cnt;
int urandlib_read_without_sema_buf_sz_sum;

SEC("usdt/./liburandom_read.so:urandlib:read_without_sema")
int BPF_USDT(urandlib_read_without_sema, int iter_num, int iter_cnt, int buf_sz)
{
	if (urand_pid != (bpf_get_current_pid_tgid() >> 32))
		return 0;

	__sync_fetch_and_add(&urandlib_read_without_sema_call_cnt, 1);
	__sync_fetch_and_add(&urandlib_read_without_sema_buf_sz_sum, buf_sz);

	return 0;
}

int urandlib_read_with_sema_call_cnt;
int urandlib_read_with_sema_buf_sz_sum;

SEC("usdt/./liburandom_read.so:urandlib:read_with_sema")
int BPF_USDT(urandlib_read_with_sema, int iter_num, int iter_cnt, int buf_sz)
{
	if (urand_pid != (bpf_get_current_pid_tgid() >> 32))
		return 0;

	__sync_fetch_and_add(&urandlib_read_with_sema_call_cnt, 1);
	__sync_fetch_and_add(&urandlib_read_with_sema_buf_sz_sum, buf_sz);

	return 0;
}

char _license[] SEC("license") = "GPL";
