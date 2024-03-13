// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

/* 4-byte aligned .data */
static volatile int static_var1 = 5;
static volatile int static_var2 = 6;
int var2 = -1;
/* 8-byte aligned .rodata */
const volatile long rovar2;

/* same "subprog" name in both files */
static __noinline int subprog(int x)
{
	/* but different formula */
	return x * 3;
}

SEC("raw_tp/sys_enter")
int handler2(const void *ctx)
{
	var2 = subprog(rovar2) + static_var1 + static_var2;

	return 0;
}

/* different name and/or type of the variable doesn't matter */
char _license[] SEC("license") = "GPL";
int _version SEC("version") = 1;
