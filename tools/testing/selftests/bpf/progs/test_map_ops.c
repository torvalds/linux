// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

char _license[] SEC("license") = "GPL";

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, int);
} hash_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_STACK);
	__uint(max_entries, 1);
	__type(value, int);
} stack_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, int);
} array_map SEC(".maps");

const volatile pid_t pid;
long err = 0;

static u64 callback(u64 map, u64 key, u64 val, u64 ctx, u64 flags)
{
	return 0;
}

SEC("tp/syscalls/sys_enter_getpid")
int map_update(void *ctx)
{
	const int key = 0;
	const int val = 1;

	if (pid != (bpf_get_current_pid_tgid() >> 32))
		return 0;

	err = bpf_map_update_elem(&hash_map, &key, &val, BPF_NOEXIST);

	return 0;
}

SEC("tp/syscalls/sys_enter_getppid")
int map_delete(void *ctx)
{
	const int key = 0;

	if (pid != (bpf_get_current_pid_tgid() >> 32))
		return 0;

	err = bpf_map_delete_elem(&hash_map, &key);

	return 0;
}

SEC("tp/syscalls/sys_enter_getuid")
int map_push(void *ctx)
{
	const int val = 1;

	if (pid != (bpf_get_current_pid_tgid() >> 32))
		return 0;

	err = bpf_map_push_elem(&stack_map, &val, 0);

	return 0;
}

SEC("tp/syscalls/sys_enter_geteuid")
int map_pop(void *ctx)
{
	int val;

	if (pid != (bpf_get_current_pid_tgid() >> 32))
		return 0;

	err = bpf_map_pop_elem(&stack_map, &val);

	return 0;
}

SEC("tp/syscalls/sys_enter_getgid")
int map_peek(void *ctx)
{
	int val;

	if (pid != (bpf_get_current_pid_tgid() >> 32))
		return 0;

	err = bpf_map_peek_elem(&stack_map, &val);

	return 0;
}

SEC("tp/syscalls/sys_enter_gettid")
int map_for_each_pass(void *ctx)
{
	const int key = 0;
	const int val = 1;
	const u64 flags = 0;
	int callback_ctx;

	if (pid != (bpf_get_current_pid_tgid() >> 32))
		return 0;

	bpf_map_update_elem(&array_map, &key, &val, flags);

	err = bpf_for_each_map_elem(&array_map, callback, &callback_ctx, flags);

	return 0;
}

SEC("tp/syscalls/sys_enter_getpgid")
int map_for_each_fail(void *ctx)
{
	const int key = 0;
	const int val = 1;
	const u64 flags = BPF_NOEXIST;
	int callback_ctx;

	if (pid != (bpf_get_current_pid_tgid() >> 32))
		return 0;

	bpf_map_update_elem(&array_map, &key, &val, flags);

	/* calling for_each with non-zero flags will return error */
	err = bpf_for_each_map_elem(&array_map, callback, &callback_ctx, flags);

	return 0;
}
