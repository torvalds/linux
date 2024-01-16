// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#include <stdbool.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

/* volatile to force a read */
const volatile int var1;
volatile int var2 = 1;
struct {
	int var3_1;
	__s64 var3_2;
} var3;
int libout1;

extern volatile bool CONFIG_BPF_SYSCALL __kconfig;

int var4[4];

__weak int var5 SEC(".data");

/* Fully contained within library extern-and-definition */
extern int var6;

int var7 SEC(".data.custom");

int (*fn_ptr)(void);

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__type(key, __u32);
	__type(value, __u32);
	__uint(max_entries, 16);
} map1 SEC(".maps");

extern struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__type(key, __u32);
	__type(value, __u32);
	__uint(max_entries, 16);
} map2 SEC(".maps");

int lib_routine(void)
{
	__u32 key = 1, value = 2;

	(void) CONFIG_BPF_SYSCALL;
	bpf_map_update_elem(&map2, &key, &value, BPF_ANY);

	libout1 = var1 + var2 + var3.var3_1 + var3.var3_2 + var5 + var6;
	return libout1;
}

SEC("perf_event")
int lib_perf_handler(struct pt_regs *ctx)
{
	return 0;
}

char LICENSE[] SEC("license") = "GPL";
