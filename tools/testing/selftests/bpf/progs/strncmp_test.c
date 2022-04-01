// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2021. Huawei Technologies Co., Ltd */
#include <stdbool.h>
#include <linux/types.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#define STRNCMP_STR_SZ 8

const char target[STRNCMP_STR_SZ] = "EEEEEEE";
char str[STRNCMP_STR_SZ];
int cmp_ret = 0;
int target_pid = 0;

const char no_str_target[STRNCMP_STR_SZ] = "12345678";
char writable_target[STRNCMP_STR_SZ];
unsigned int no_const_str_size = STRNCMP_STR_SZ;

char _license[] SEC("license") = "GPL";

SEC("tp/syscalls/sys_enter_nanosleep")
int do_strncmp(void *ctx)
{
	if ((bpf_get_current_pid_tgid() >> 32) != target_pid)
		return 0;

	cmp_ret = bpf_strncmp(str, STRNCMP_STR_SZ, target);
	return 0;
}

SEC("tp/syscalls/sys_enter_nanosleep")
int strncmp_bad_not_const_str_size(void *ctx)
{
	/* The value of string size is not const, so will fail */
	cmp_ret = bpf_strncmp(str, no_const_str_size, target);
	return 0;
}

SEC("tp/syscalls/sys_enter_nanosleep")
int strncmp_bad_writable_target(void *ctx)
{
	/* Compared target is not read-only, so will fail */
	cmp_ret = bpf_strncmp(str, STRNCMP_STR_SZ, writable_target);
	return 0;
}

SEC("tp/syscalls/sys_enter_nanosleep")
int strncmp_bad_not_null_term_target(void *ctx)
{
	/* Compared target is not null-terminated, so will fail */
	cmp_ret = bpf_strncmp(str, STRNCMP_STR_SZ, no_str_target);
	return 0;
}
