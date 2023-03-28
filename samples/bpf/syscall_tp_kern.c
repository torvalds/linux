// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2017 Facebook
 */
#include <uapi/linux/bpf.h>
#include <bpf/bpf_helpers.h>

struct syscalls_enter_open_args {
	unsigned long long unused;
	long syscall_nr;
	long filename_ptr;
	long flags;
	long mode;
};

struct syscalls_exit_open_args {
	unsigned long long unused;
	long syscall_nr;
	long ret;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, u32);
	__type(value, u32);
	__uint(max_entries, 1);
} enter_open_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, u32);
	__type(value, u32);
	__uint(max_entries, 1);
} exit_open_map SEC(".maps");

static __always_inline void count(void *map)
{
	u32 key = 0;
	u32 *value, init_val = 1;

	value = bpf_map_lookup_elem(map, &key);
	if (value)
		*value += 1;
	else
		bpf_map_update_elem(map, &key, &init_val, BPF_NOEXIST);
}

SEC("tracepoint/syscalls/sys_enter_open")
int trace_enter_open(struct syscalls_enter_open_args *ctx)
{
	count(&enter_open_map);
	return 0;
}

SEC("tracepoint/syscalls/sys_enter_openat")
int trace_enter_open_at(struct syscalls_enter_open_args *ctx)
{
	count(&enter_open_map);
	return 0;
}

SEC("tracepoint/syscalls/sys_enter_openat2")
int trace_enter_open_at2(struct syscalls_enter_open_args *ctx)
{
	count(&enter_open_map);
	return 0;
}

SEC("tracepoint/syscalls/sys_exit_open")
int trace_enter_exit(struct syscalls_exit_open_args *ctx)
{
	count(&exit_open_map);
	return 0;
}

SEC("tracepoint/syscalls/sys_exit_openat")
int trace_enter_exit_at(struct syscalls_exit_open_args *ctx)
{
	count(&exit_open_map);
	return 0;
}

SEC("tracepoint/syscalls/sys_exit_openat2")
int trace_enter_exit_at2(struct syscalls_exit_open_args *ctx)
{
	count(&exit_open_map);
	return 0;
}
