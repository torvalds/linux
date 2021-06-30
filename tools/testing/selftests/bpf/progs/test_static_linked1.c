// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

/* 8-byte aligned .bss */
static volatile long static_var1;
static volatile int static_var11;
int var1 = 0;
/* 4-byte aligned .rodata */
const volatile int rovar1;

/* same "subprog" name in both files */
static __noinline int subprog(int x)
{
	/* but different formula */
	return x * 2;
}

SEC("raw_tp/sys_enter")
int handler1(const void *ctx)
{
	var1 = subprog(rovar1) + static_var1 + static_var11;

	return 0;
}

char LICENSE[] SEC("license") = "GPL";
int VERSION SEC("version") = 1;
