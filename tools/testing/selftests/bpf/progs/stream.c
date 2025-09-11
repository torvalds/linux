// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */
#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"
#include "bpf_experimental.h"
#include "bpf_arena_common.h"

struct arr_elem {
	struct bpf_res_spin_lock lock;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, struct arr_elem);
} arrmap SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARENA);
	__uint(map_flags, BPF_F_MMAPABLE);
	__uint(max_entries, 1); /* number of pages */
} arena SEC(".maps");

struct elem {
	struct bpf_timer timer;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, struct elem);
} array SEC(".maps");

#define ENOSPC 28
#define _STR "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"

int size;
u64 fault_addr;
void *arena_ptr;

SEC("syscall")
__success __retval(0)
int stream_exhaust(void *ctx)
{
	/* Use global variable for loop convergence. */
	size = 0;
	bpf_repeat(BPF_MAX_LOOPS) {
		if (bpf_stream_printk(BPF_STDOUT, _STR) == -ENOSPC && size == 99954)
			return 0;
		size += sizeof(_STR) - 1;
	}
	return 1;
}

SEC("syscall")
__arch_x86_64
__arch_arm64
__arch_s390x
__success __retval(0)
__stderr("ERROR: Timeout detected for may_goto instruction")
__stderr("CPU: {{[0-9]+}} UID: 0 PID: {{[0-9]+}} Comm: {{.*}}")
__stderr("Call trace:\n"
"{{([a-zA-Z_][a-zA-Z0-9_]*\\+0x[0-9a-fA-F]+/0x[0-9a-fA-F]+\n"
"|[ \t]+[^\n]+\n)*}}")
int stream_cond_break(void *ctx)
{
	while (can_loop)
		;
	return 0;
}

SEC("syscall")
__success __retval(0)
__stderr("ERROR: AA or ABBA deadlock detected for bpf_res_spin_lock")
__stderr("{{Attempted lock   = (0x[0-9a-fA-F]+)\n"
"Total held locks = 1\n"
"Held lock\\[ 0\\] = \\1}}")
__stderr("...")
__stderr("CPU: {{[0-9]+}} UID: 0 PID: {{[0-9]+}} Comm: {{.*}}")
__stderr("Call trace:\n"
"{{([a-zA-Z_][a-zA-Z0-9_]*\\+0x[0-9a-fA-F]+/0x[0-9a-fA-F]+\n"
"|[ \t]+[^\n]+\n)*}}")
int stream_deadlock(void *ctx)
{
	struct bpf_res_spin_lock *lock, *nlock;

	lock = bpf_map_lookup_elem(&arrmap, &(int){0});
	if (!lock)
		return 1;
	nlock = bpf_map_lookup_elem(&arrmap, &(int){0});
	if (!nlock)
		return 1;
	if (bpf_res_spin_lock(lock))
		return 1;
	if (bpf_res_spin_lock(nlock)) {
		bpf_res_spin_unlock(lock);
		return 0;
	}
	bpf_res_spin_unlock(nlock);
	bpf_res_spin_unlock(lock);
	return 1;
}

SEC("syscall")
__success __retval(0)
int stream_syscall(void *ctx)
{
	bpf_stream_printk(BPF_STDOUT, "foo");
	return 0;
}

SEC("syscall")
__arch_x86_64
__arch_arm64
__success __retval(0)
__stderr("ERROR: Arena WRITE access at unmapped address 0x{{.*}}")
__stderr("CPU: {{[0-9]+}} UID: 0 PID: {{[0-9]+}} Comm: {{.*}}")
__stderr("Call trace:\n"
"{{([a-zA-Z_][a-zA-Z0-9_]*\\+0x[0-9a-fA-F]+/0x[0-9a-fA-F]+\n"
"|[ \t]+[^\n]+\n)*}}")
int stream_arena_write_fault(void *ctx)
{
	struct bpf_arena *ptr = (void *)&arena;
	u64 user_vm_start;

	/* Prevent GCC bounds warning: casting &arena to struct bpf_arena *
	 * triggers bounds checking since the map definition is smaller than struct
	 * bpf_arena. barrier_var() makes the pointer opaque to GCC, preventing the
	 * bounds analysis
	 */
	barrier_var(ptr);
	user_vm_start = ptr->user_vm_start;
	fault_addr = user_vm_start + 0x7fff;
	bpf_addr_space_cast(user_vm_start, 0, 1);
	asm volatile (
		"r1 = %0;"
		"r2 = 1;"
		"*(u32 *)(r1 + 0x7fff) = r2;"
		:
		: "r" (user_vm_start)
		: "r1", "r2"
	);
	return 0;
}

SEC("syscall")
__arch_x86_64
__arch_arm64
__success __retval(0)
__stderr("ERROR: Arena READ access at unmapped address 0x{{.*}}")
__stderr("CPU: {{[0-9]+}} UID: 0 PID: {{[0-9]+}} Comm: {{.*}}")
__stderr("Call trace:\n"
"{{([a-zA-Z_][a-zA-Z0-9_]*\\+0x[0-9a-fA-F]+/0x[0-9a-fA-F]+\n"
"|[ \t]+[^\n]+\n)*}}")
int stream_arena_read_fault(void *ctx)
{
	struct bpf_arena *ptr = (void *)&arena;
	u64 user_vm_start;

	/* Prevent GCC bounds warning: casting &arena to struct bpf_arena *
	 * triggers bounds checking since the map definition is smaller than struct
	 * bpf_arena. barrier_var() makes the pointer opaque to GCC, preventing the
	 * bounds analysis
	 */
	barrier_var(ptr);
	user_vm_start = ptr->user_vm_start;
	fault_addr = user_vm_start + 0x7fff;
	bpf_addr_space_cast(user_vm_start, 0, 1);
	asm volatile (
		"r1 = %0;"
		"r1 = *(u32 *)(r1 + 0x7fff);"
		:
		: "r" (user_vm_start)
		: "r1"
	);
	return 0;
}

static __noinline void subprog(void)
{
	int __arena *addr = (int __arena *)0xdeadbeef;

	arena_ptr = &arena;
	*addr = 1;
}

SEC("syscall")
__arch_x86_64
__arch_arm64
__success __retval(0)
__stderr("ERROR: Arena WRITE access at unmapped address 0x{{.*}}")
__stderr("CPU: {{[0-9]+}} UID: 0 PID: {{[0-9]+}} Comm: {{.*}}")
__stderr("Call trace:\n"
"{{([a-zA-Z_][a-zA-Z0-9_]*\\+0x[0-9a-fA-F]+/0x[0-9a-fA-F]+\n"
"|[ \t]+[^\n]+\n)*}}")
int stream_arena_subprog_fault(void *ctx)
{
	subprog();
	return 0;
}

static __noinline int timer_cb(void *map, int *key, struct bpf_timer *timer)
{
	int __arena *addr = (int __arena *)0xdeadbeef;

	arena_ptr = &arena;
	*addr = 1;
	return 0;
}

SEC("syscall")
__arch_x86_64
__arch_arm64
__success __retval(0)
__stderr("ERROR: Arena WRITE access at unmapped address 0x{{.*}}")
__stderr("CPU: {{[0-9]+}} UID: 0 PID: {{[0-9]+}} Comm: {{.*}}")
__stderr("Call trace:\n"
"{{([a-zA-Z_][a-zA-Z0-9_]*\\+0x[0-9a-fA-F]+/0x[0-9a-fA-F]+\n"
"|[ \t]+[^\n]+\n)*}}")
int stream_arena_callback_fault(void *ctx)
{
	struct bpf_timer *arr_timer;

	arr_timer = bpf_map_lookup_elem(&array, &(int){0});
	if (!arr_timer)
		return 0;
	bpf_timer_init(arr_timer, &array, 1);
	bpf_timer_set_callback(arr_timer, timer_cb);
	bpf_timer_start(arr_timer, 0, 0);
	return 0;
}

char _license[] SEC("license") = "GPL";
