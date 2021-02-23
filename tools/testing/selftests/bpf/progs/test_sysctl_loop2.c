// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Facebook

#include <stdint.h>
#include <string.h>

#include <linux/stddef.h>
#include <linux/bpf.h>

#include <bpf/bpf_helpers.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

/* tcp_mem sysctl has only 3 ints, but this test is doing TCP_MEM_LOOPS */
#define TCP_MEM_LOOPS 20  /* because 30 doesn't fit into 512 bytes of stack */
#define MAX_ULONG_STR_LEN 7
#define MAX_VALUE_STR_LEN (TCP_MEM_LOOPS * MAX_ULONG_STR_LEN)

const char tcp_mem_name[] = "net/ipv4/tcp_mem/very_very_very_very_long_pointless_string_to_stress_byte_loop";
static __attribute__((noinline)) int is_tcp_mem(struct bpf_sysctl *ctx)
{
	unsigned char i;
	char name[sizeof(tcp_mem_name)];
	int ret;

	memset(name, 0, sizeof(name));
	ret = bpf_sysctl_get_name(ctx, name, sizeof(name), 0);
	if (ret < 0 || ret != sizeof(tcp_mem_name) - 1)
		return 0;

#pragma clang loop unroll(disable)
	for (i = 0; i < sizeof(tcp_mem_name); ++i)
		if (name[i] != tcp_mem_name[i])
			return 0;

	return 1;
}


SEC("cgroup/sysctl")
int sysctl_tcp_mem(struct bpf_sysctl *ctx)
{
	unsigned long tcp_mem[TCP_MEM_LOOPS] = {};
	char value[MAX_VALUE_STR_LEN];
	unsigned char i, off = 0;
	int ret;

	if (ctx->write)
		return 0;

	if (!is_tcp_mem(ctx))
		return 0;

	ret = bpf_sysctl_get_current_value(ctx, value, MAX_VALUE_STR_LEN);
	if (ret < 0 || ret >= MAX_VALUE_STR_LEN)
		return 0;

#pragma clang loop unroll(disable)
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
