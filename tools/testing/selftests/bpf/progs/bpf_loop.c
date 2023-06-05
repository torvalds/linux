// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";

struct callback_ctx {
	int output;
};

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 32);
	__type(key, int);
	__type(value, int);
} map1 SEC(".maps");

/* These should be set by the user program */
u32 nested_callback_nr_loops;
u32 stop_index = -1;
u32 nr_loops;
int pid;
int callback_selector;

/* Making these global variables so that the userspace program
 * can verify the output through the skeleton
 */
int nr_loops_returned;
int g_output;
int err;

static int callback(__u32 index, void *data)
{
	struct callback_ctx *ctx = data;

	if (index >= stop_index)
		return 1;

	ctx->output += index;

	return 0;
}

static int empty_callback(__u32 index, void *data)
{
	return 0;
}

static int nested_callback2(__u32 index, void *data)
{
	nr_loops_returned += bpf_loop(nested_callback_nr_loops, callback, data, 0);

	return 0;
}

static int nested_callback1(__u32 index, void *data)
{
	bpf_loop(nested_callback_nr_loops, nested_callback2, data, 0);
	return 0;
}

SEC("fentry/" SYS_PREFIX "sys_nanosleep")
int test_prog(void *ctx)
{
	struct callback_ctx data = {};

	if (bpf_get_current_pid_tgid() >> 32 != pid)
		return 0;

	nr_loops_returned = bpf_loop(nr_loops, callback, &data, 0);

	if (nr_loops_returned < 0)
		err = nr_loops_returned;
	else
		g_output = data.output;

	return 0;
}

SEC("fentry/" SYS_PREFIX "sys_nanosleep")
int prog_null_ctx(void *ctx)
{
	if (bpf_get_current_pid_tgid() >> 32 != pid)
		return 0;

	nr_loops_returned = bpf_loop(nr_loops, empty_callback, NULL, 0);

	return 0;
}

SEC("fentry/" SYS_PREFIX "sys_nanosleep")
int prog_invalid_flags(void *ctx)
{
	struct callback_ctx data = {};

	if (bpf_get_current_pid_tgid() >> 32 != pid)
		return 0;

	err = bpf_loop(nr_loops, callback, &data, 1);

	return 0;
}

SEC("fentry/" SYS_PREFIX "sys_nanosleep")
int prog_nested_calls(void *ctx)
{
	struct callback_ctx data = {};

	if (bpf_get_current_pid_tgid() >> 32 != pid)
		return 0;

	nr_loops_returned = 0;
	bpf_loop(nr_loops, nested_callback1, &data, 0);

	g_output = data.output;

	return 0;
}

static int callback_set_f0(int i, void *ctx)
{
	g_output = 0xF0;
	return 0;
}

static int callback_set_0f(int i, void *ctx)
{
	g_output = 0x0F;
	return 0;
}

/*
 * non-constant callback is a corner case for bpf_loop inline logic
 */
SEC("fentry/" SYS_PREFIX "sys_nanosleep")
int prog_non_constant_callback(void *ctx)
{
	if (bpf_get_current_pid_tgid() >> 32 != pid)
		return 0;

	int (*callback)(int i, void *ctx);

	g_output = 0;

	if (callback_selector == 0x0F)
		callback = callback_set_0f;
	else
		callback = callback_set_f0;

	bpf_loop(1, callback, NULL, 0);

	return 0;
}

static int stack_check_inner_callback(void *ctx)
{
	return 0;
}

static int map1_lookup_elem(int key)
{
	int *val = bpf_map_lookup_elem(&map1, &key);

	return val ? *val : -1;
}

static void map1_update_elem(int key, int val)
{
	bpf_map_update_elem(&map1, &key, &val, BPF_ANY);
}

static int stack_check_outer_callback(void *ctx)
{
	int a = map1_lookup_elem(1);
	int b = map1_lookup_elem(2);
	int c = map1_lookup_elem(3);
	int d = map1_lookup_elem(4);
	int e = map1_lookup_elem(5);
	int f = map1_lookup_elem(6);

	bpf_loop(1, stack_check_inner_callback, NULL, 0);

	map1_update_elem(1, a + 1);
	map1_update_elem(2, b + 1);
	map1_update_elem(3, c + 1);
	map1_update_elem(4, d + 1);
	map1_update_elem(5, e + 1);
	map1_update_elem(6, f + 1);

	return 0;
}

/* Some of the local variables in stack_check and
 * stack_check_outer_callback would be allocated on stack by
 * compiler. This test should verify that stack content for these
 * variables is preserved between calls to bpf_loop (might be an issue
 * if loop inlining allocates stack slots incorrectly).
 */
SEC("fentry/" SYS_PREFIX "sys_nanosleep")
int stack_check(void *ctx)
{
	if (bpf_get_current_pid_tgid() >> 32 != pid)
		return 0;

	int a = map1_lookup_elem(7);
	int b = map1_lookup_elem(8);
	int c = map1_lookup_elem(9);
	int d = map1_lookup_elem(10);
	int e = map1_lookup_elem(11);
	int f = map1_lookup_elem(12);

	bpf_loop(1, stack_check_outer_callback, NULL, 0);

	map1_update_elem(7,  a + 1);
	map1_update_elem(8, b + 1);
	map1_update_elem(9, c + 1);
	map1_update_elem(10, d + 1);
	map1_update_elem(11, e + 1);
	map1_update_elem(12, f + 1);

	return 0;
}
