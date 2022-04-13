// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#include <stdbool.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

/* volatile to force a read, compiler may assume 0 otherwise */
const volatile int rovar1;
int out1;

/* Override weak symbol in test_subskeleton_lib */
int var5 = 5;

extern volatile bool CONFIG_BPF_SYSCALL __kconfig;

extern int lib_routine(void);

SEC("raw_tp/sys_enter")
int handler1(const void *ctx)
{
	(void) CONFIG_BPF_SYSCALL;

	out1 = lib_routine() * rovar1;
	return 0;
}

char LICENSE[] SEC("license") = "GPL";
