/*
 * Copyright (c) 2017 Facebook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#define KBUILD_MODNAME "foo"
#include <linux/ptrace.h>
#include <linux/version.h>
#include <uapi/linux/bpf.h>
#include <uapi/linux/in6.h>
#include "bpf_helpers.h"

#define MAX_NR_PORTS 65536

/* map #0 */
struct bpf_map_def SEC("maps") port_a = {
	.type = BPF_MAP_TYPE_ARRAY,
	.key_size = sizeof(u32),
	.value_size = sizeof(int),
	.max_entries = MAX_NR_PORTS,
};

/* map #1 */
struct bpf_map_def SEC("maps") port_h = {
	.type = BPF_MAP_TYPE_HASH,
	.key_size = sizeof(u32),
	.value_size = sizeof(int),
	.max_entries = 1,
};

/* map #2 */
struct bpf_map_def SEC("maps") reg_result_h = {
	.type = BPF_MAP_TYPE_HASH,
	.key_size = sizeof(u32),
	.value_size = sizeof(int),
	.max_entries = 1,
};

/* map #3 */
struct bpf_map_def SEC("maps") inline_result_h = {
	.type = BPF_MAP_TYPE_HASH,
	.key_size = sizeof(u32),
	.value_size = sizeof(int),
	.max_entries = 1,
};

/* map #4 */ /* Test case #0 */
struct bpf_map_def SEC("maps") a_of_port_a = {
	.type = BPF_MAP_TYPE_ARRAY_OF_MAPS,
	.key_size = sizeof(u32),
	.inner_map_idx = 0, /* map_fd[0] is port_a */
	.max_entries = MAX_NR_PORTS,
};

/* map #5 */ /* Test case #1 */
struct bpf_map_def SEC("maps") h_of_port_a = {
	.type = BPF_MAP_TYPE_HASH_OF_MAPS,
	.key_size = sizeof(u32),
	.inner_map_idx = 0, /* map_fd[0] is port_a */
	.max_entries = 1,
};

/* map #6 */ /* Test case #2 */
struct bpf_map_def SEC("maps") h_of_port_h = {
	.type = BPF_MAP_TYPE_HASH_OF_MAPS,
	.key_size = sizeof(u32),
	.inner_map_idx = 1, /* map_fd[1] is port_h */
	.max_entries = 1,
};

static __always_inline int do_reg_lookup(void *inner_map, u32 port)
{
	int *result;

	result = bpf_map_lookup_elem(inner_map, &port);
	return result ? *result : -ENOENT;
}

static __always_inline int do_inline_array_lookup(void *inner_map, u32 port)
{
	int *result;

	if (inner_map != &port_a)
		return -EINVAL;

	result = bpf_map_lookup_elem(&port_a, &port);
	return result ? *result : -ENOENT;
}

static __always_inline int do_inline_hash_lookup(void *inner_map, u32 port)
{
	int *result;

	if (inner_map != &port_h)
		return -EINVAL;

	result = bpf_map_lookup_elem(&port_h, &port);
	return result ? *result : -ENOENT;
}

SEC("kprobe/sys_connect")
int trace_sys_connect(struct pt_regs *ctx)
{
	struct sockaddr_in6 *in6;
	u16 test_case, port, dst6[8];
	int addrlen, ret, inline_ret, ret_key = 0;
	u32 port_key;
	void *outer_map, *inner_map;
	bool inline_hash = false;

	in6 = (struct sockaddr_in6 *)PT_REGS_PARM2(ctx);
	addrlen = (int)PT_REGS_PARM3(ctx);

	if (addrlen != sizeof(*in6))
		return 0;

	ret = bpf_probe_read(dst6, sizeof(dst6), &in6->sin6_addr);
	if (ret) {
		inline_ret = ret;
		goto done;
	}

	if (dst6[0] != 0xdead || dst6[1] != 0xbeef)
		return 0;

	test_case = dst6[7];

	ret = bpf_probe_read(&port, sizeof(port), &in6->sin6_port);
	if (ret) {
		inline_ret = ret;
		goto done;
	}

	port_key = port;

	ret = -ENOENT;
	if (test_case == 0) {
		outer_map = &a_of_port_a;
	} else if (test_case == 1) {
		outer_map = &h_of_port_a;
	} else if (test_case == 2) {
		outer_map = &h_of_port_h;
	} else {
		ret = __LINE__;
		inline_ret = ret;
		goto done;
	}

	inner_map = bpf_map_lookup_elem(outer_map, &port_key);
	if (!inner_map) {
		ret = __LINE__;
		inline_ret = ret;
		goto done;
	}

	ret = do_reg_lookup(inner_map, port_key);

	if (test_case == 0 || test_case == 1)
		inline_ret = do_inline_array_lookup(inner_map, port_key);
	else
		inline_ret = do_inline_hash_lookup(inner_map, port_key);

done:
	bpf_map_update_elem(&reg_result_h, &ret_key, &ret, BPF_ANY);
	bpf_map_update_elem(&inline_result_h, &ret_key, &inline_ret, BPF_ANY);

	return 0;
}

char _license[] SEC("license") = "GPL";
u32 _version SEC("version") = LINUX_VERSION_CODE;
