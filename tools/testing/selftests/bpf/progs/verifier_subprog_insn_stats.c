// SPDX-License-Identifier: GPL-2.0

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

struct timer_value {
	struct bpf_timer timer;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, struct timer_value);
} timer_map SEC(".maps");

SEC("?raw_tp")
__success __log_level(4)
__msg("subprog 0 (stats_main_only) main insns_self 2 insns_total 2 stack 0")
__msg("processed 2 insns")
__naked int stats_main_only(void)
{
	asm volatile (
		"r0 = 0;"
		"exit;"
	);
}

__naked __noinline __used
static int stats_chain_leaf(void)
{
	asm volatile (
		"r0 = 0;"
		"exit;"
	);
}

__naked __noinline __used
static int stats_chain_parent(void)
{
	asm volatile (
		"call stats_chain_leaf;"
		"exit;"
	);
}

SEC("?raw_tp")
__success __log_level(4)
/*
 * self: 2 + 2 + 2 = 6
 * totals: leaf 2, parent 2 + 2 = 4, main 2 + 4 = 6
 */
__msg("subprog 0 (stats_static_chain) main insns_self 2 insns_total 6 stack 0")
__msg("subprog {{[0-9]+}} (stats_chain_parent) static insns_self 2 insns_total 4 stack 0")
__msg("subprog {{[0-9]+}} (stats_chain_leaf) static insns_self 2 insns_total 2 stack 0")
__msg("processed 6 insns")
__naked int stats_static_chain(void)
{
	asm volatile (
		"call stats_chain_parent;"
		"exit;"
	);
}

__naked __noinline __used
static int stats_shared_leaf(void)
{
	asm volatile (
		"r0 = 0;"
		"exit;"
	);
}

__naked __noinline __used
int stats_global_root(void)
{
	asm volatile (
		"call stats_shared_leaf;"
		"exit;"
	);
}

SEC("?raw_tp")
__success __log_level(4)
/*
 * stats_shared_leaf is explored once under each independent root.
 * self: main 3 + leaf 4 + global 2 = 9
 * root totals: main 5 + global 4 = 9
 */
__msg("subprog 0 (stats_shared_roots) main insns_self 3 insns_total 5 stack 0")
__msg("subprog {{[0-9]+}} (stats_shared_leaf) static insns_self 4 insns_total 4 stack 0")
__msg("subprog {{[0-9]+}} (stats_global_root) global insns_self 2 insns_total 4 stack 0")
__msg("processed 9 insns")
__naked int stats_shared_roots(void)
{
	asm volatile (
		"call stats_shared_leaf;"
		"call stats_global_root;"
		"exit;"
	);
}

__noinline __used
static int stats_async_leaf(void *map, __u32 *key, struct bpf_timer *timer)
{
	return 0;
}

__noinline __used
static __u64 stats_async_schedule(struct bpf_map *map, __u32 *key,
				  struct timer_value *value, void *ctx)
{
	asm volatile (
		"r1 = %[timer];"
		"r2 = %[stats_async_leaf];"
		"call %[bpf_timer_set_callback];"
		:
		: [timer] "r" (value),
		  __imm_ptr(stats_async_leaf),
		  __imm(bpf_timer_set_callback)
		: __clobber_common
	);
	return 0;
}

SEC("?raw_tp")
__success __log_level(4)
/*
 * self: 9 + 7 + 2 = 18
 * totals: leaf 2, scheduler 7, main root 18
 */
__msg("subprog 0 (stats_async_direct) main insns_self 9 insns_total 18 stack 0")
__msg("subprog {{[0-9]+}} (stats_async_schedule) static insns_self 7 insns_total 7 stack 0")
__msg("subprog {{[0-9]+}} (stats_async_leaf) static insns_self 2 insns_total 2 stack 0")
__msg("processed 18 insns")
__naked int stats_async_direct(void)
{
	asm volatile (
		"r1 = %[timer_map] ll;"
		"r2 = %[stats_async_schedule];"
		"r3 = 0;"
		"r4 = 0;"
		"call %[bpf_for_each_map_elem];"
		"r0 = 0;"
		"exit;"
		:
		: __imm_addr(timer_map),
		  __imm_ptr(stats_async_schedule),
		  __imm(bpf_for_each_map_elem)
		: __clobber_common
	);
}

__noinline __used
static int stats_async_nested_leaf(void *map, __u32 *key, struct bpf_timer *timer)
{
	return 0;
}

__noinline __used
static int stats_async_outer(void *map, __u32 *key, struct bpf_timer *timer)
{
	asm volatile (
		"r1 = %[timer];"
		"r2 = %[stats_async_nested_leaf];"
		"call %[bpf_timer_set_callback];"
		:
		: [timer] "r" (timer),
		  __imm_ptr(stats_async_nested_leaf),
		  __imm(bpf_timer_set_callback)
		: __clobber_common
	);
	return 0;
}

__noinline __used
static __u64 stats_async_nested_schedule(struct bpf_map *map, __u32 *key,
					 struct timer_value *value, void *ctx)
{
	asm volatile (
		"r1 = %[timer];"
		"r2 = %[stats_async_outer];"
		"call %[bpf_timer_set_callback];"
		:
		: [timer] "r" (value),
		  __imm_ptr(stats_async_outer),
		  __imm(bpf_timer_set_callback)
		: __clobber_common
	);
	return 0;
}

SEC("?raw_tp")
__success __log_level(4)
/*
 * self: 9 + 7 + 7 + 2 = 25
 * totals: leaf 2, outer 7, scheduler 7, main root 25
 */
__msg("subprog 0 (stats_async_nested) main insns_self 9 insns_total 25 stack 0")
__msg("subprog {{[0-9]+}} (stats_async_nested_schedule) static insns_self 7 insns_total 7 stack 0")
__msg("subprog {{[0-9]+}} (stats_async_outer) static insns_self 7 insns_total 7 stack 0")
__msg("subprog {{[0-9]+}} (stats_async_nested_leaf) static insns_self 2 insns_total 2 stack 0")
__msg("processed 25 insns")
__naked int stats_async_nested(void)
{
	asm volatile (
		"r1 = %[timer_map] ll;"
		"r2 = %[stats_async_nested_schedule];"
		"r3 = 0;"
		"r4 = 0;"
		"call %[bpf_for_each_map_elem];"
		"r0 = 0;"
		"exit;"
		:
		: __imm_addr(timer_map),
		  __imm_ptr(stats_async_nested_schedule),
		  __imm(bpf_for_each_map_elem)
		: __clobber_common
	);
}

char _license[] SEC("license") = "GPL";
