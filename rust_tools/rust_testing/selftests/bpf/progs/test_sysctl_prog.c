// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Facebook

#include <stdint.h>
#include <string.h>

#include <linux/stddef.h>
#include <linux/bpf.h>

#include <bpf/bpf_helpers.h>

#include "bpf_compiler.h"
#include "bpf_misc.h"

/* Max supported length of a string with unsigned long in base 10 (pow2 - 1). */
#define MAX_ULONG_STR_LEN 0xF

/* Max supported length of sysctl value string (pow2). */
#define MAX_VALUE_STR_LEN 0x40

const char tcp_mem_name[] = "net/ipv4/tcp_mem";
static __always_inline int is_tcp_mem(struct bpf_sysctl *ctx)
{
	unsigned char i;
	char name[sizeof(tcp_mem_name)];
	int ret;

	memset(name, 0, sizeof(name));
	ret = bpf_sysctl_get_name(ctx, name, sizeof(name), 0);
	if (ret < 0 || ret != sizeof(tcp_mem_name) - 1)
		return 0;

	__pragma_loop_unroll_full
	for (i = 0; i < sizeof(tcp_mem_name); ++i)
		if (name[i] != tcp_mem_name[i])
			return 0;

	return 1;
}

SEC("cgroup/sysctl")
int sysctl_tcp_mem(struct bpf_sysctl *ctx)
{
	unsigned long tcp_mem[3] = {0, 0, 0};
	char value[MAX_VALUE_STR_LEN];
	unsigned char i, off = 0;
	volatile int ret;

	if (ctx->write)
		return 0;

	if (!is_tcp_mem(ctx))
		return 0;

	ret = bpf_sysctl_get_current_value(ctx, value, MAX_VALUE_STR_LEN);
	if (ret < 0 || ret >= MAX_VALUE_STR_LEN)
		return 0;

	__pragma_loop_unroll_full
	for (i = 0; i < ARRAY_SIZE(tcp_mem); ++i) {
		ret = bpf_strtoul(value + off, MAX_ULONG_STR_LEN, 0,
				  tcp_mem + i);
		if (ret <= 0 || ret > MAX_ULONG_STR_LEN)
			return 0;
		off += ret & MAX_ULONG_STR_LEN;
	}


	return tcp_mem[0] < tcp_mem[1] && tcp_mem[1] < tcp_mem[2];
}

char _license[] SEC("license") = "GPL";
