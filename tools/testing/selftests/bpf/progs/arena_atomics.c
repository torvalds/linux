// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <stdbool.h>
#include "bpf_arena_common.h"

struct {
	__uint(type, BPF_MAP_TYPE_ARENA);
	__uint(map_flags, BPF_F_MMAPABLE);
	__uint(max_entries, 10); /* number of pages */
#ifdef __TARGET_ARCH_arm64
	__ulong(map_extra, 0x1ull << 32); /* start of mmap() region */
#else
	__ulong(map_extra, 0x1ull << 44); /* start of mmap() region */
#endif
} arena SEC(".maps");

#if defined(ENABLE_ATOMICS_TESTS) && defined(__BPF_FEATURE_ADDR_SPACE_CAST)
bool skip_tests __attribute((__section__(".data"))) = false;
#else
bool skip_tests = true;
#endif

__u32 pid = 0;

#undef __arena
#if defined(__BPF_FEATURE_ADDR_SPACE_CAST)
#define __arena __attribute__((address_space(1)))
#else
#define __arena SEC(".addr_space.1")
#endif

__u64 __arena add64_value = 1;
__u64 __arena add64_result = 0;
__u32 __arena add32_value = 1;
__u32 __arena add32_result = 0;
__u64 __arena add_stack_value_copy = 0;
__u64 __arena add_stack_result = 0;
__u64 __arena add_noreturn_value = 1;

SEC("raw_tp/sys_enter")
int add(const void *ctx)
{
	if (pid != (bpf_get_current_pid_tgid() >> 32))
		return 0;
#ifdef ENABLE_ATOMICS_TESTS
	__u64 add_stack_value = 1;

	add64_result = __sync_fetch_and_add(&add64_value, 2);
	add32_result = __sync_fetch_and_add(&add32_value, 2);
	add_stack_result = __sync_fetch_and_add(&add_stack_value, 2);
	add_stack_value_copy = add_stack_value;
	__sync_fetch_and_add(&add_noreturn_value, 2);
#endif

	return 0;
}

__s64 __arena sub64_value = 1;
__s64 __arena sub64_result = 0;
__s32 __arena sub32_value = 1;
__s32 __arena sub32_result = 0;
__s64 __arena sub_stack_value_copy = 0;
__s64 __arena sub_stack_result = 0;
__s64 __arena sub_noreturn_value = 1;

SEC("raw_tp/sys_enter")
int sub(const void *ctx)
{
	if (pid != (bpf_get_current_pid_tgid() >> 32))
		return 0;
#ifdef ENABLE_ATOMICS_TESTS
	__u64 sub_stack_value = 1;

	sub64_result = __sync_fetch_and_sub(&sub64_value, 2);
	sub32_result = __sync_fetch_and_sub(&sub32_value, 2);
	sub_stack_result = __sync_fetch_and_sub(&sub_stack_value, 2);
	sub_stack_value_copy = sub_stack_value;
	__sync_fetch_and_sub(&sub_noreturn_value, 2);
#endif

	return 0;
}

__u64 __arena and64_value = (0x110ull << 32);
__u32 __arena and32_value = 0x110;

SEC("raw_tp/sys_enter")
int and(const void *ctx)
{
	if (pid != (bpf_get_current_pid_tgid() >> 32))
		return 0;
#ifdef ENABLE_ATOMICS_TESTS

	__sync_fetch_and_and(&and64_value, 0x011ull << 32);
	__sync_fetch_and_and(&and32_value, 0x011);
#endif

	return 0;
}

__u32 __arena or32_value = 0x110;
__u64 __arena or64_value = (0x110ull << 32);

SEC("raw_tp/sys_enter")
int or(const void *ctx)
{
	if (pid != (bpf_get_current_pid_tgid() >> 32))
		return 0;
#ifdef ENABLE_ATOMICS_TESTS
	__sync_fetch_and_or(&or64_value, 0x011ull << 32);
	__sync_fetch_and_or(&or32_value, 0x011);
#endif

	return 0;
}

__u64 __arena xor64_value = (0x110ull << 32);
__u32 __arena xor32_value = 0x110;

SEC("raw_tp/sys_enter")
int xor(const void *ctx)
{
	if (pid != (bpf_get_current_pid_tgid() >> 32))
		return 0;
#ifdef ENABLE_ATOMICS_TESTS
	__sync_fetch_and_xor(&xor64_value, 0x011ull << 32);
	__sync_fetch_and_xor(&xor32_value, 0x011);
#endif

	return 0;
}

__u32 __arena cmpxchg32_value = 1;
__u32 __arena cmpxchg32_result_fail = 0;
__u32 __arena cmpxchg32_result_succeed = 0;
__u64 __arena cmpxchg64_value = 1;
__u64 __arena cmpxchg64_result_fail = 0;
__u64 __arena cmpxchg64_result_succeed = 0;

SEC("raw_tp/sys_enter")
int cmpxchg(const void *ctx)
{
	if (pid != (bpf_get_current_pid_tgid() >> 32))
		return 0;
#ifdef ENABLE_ATOMICS_TESTS
	cmpxchg64_result_fail = __sync_val_compare_and_swap(&cmpxchg64_value, 0, 3);
	cmpxchg64_result_succeed = __sync_val_compare_and_swap(&cmpxchg64_value, 1, 2);

	cmpxchg32_result_fail = __sync_val_compare_and_swap(&cmpxchg32_value, 0, 3);
	cmpxchg32_result_succeed = __sync_val_compare_and_swap(&cmpxchg32_value, 1, 2);
#endif

	return 0;
}

__u64 __arena xchg64_value = 1;
__u64 __arena xchg64_result = 0;
__u32 __arena xchg32_value = 1;
__u32 __arena xchg32_result = 0;

SEC("raw_tp/sys_enter")
int xchg(const void *ctx)
{
	if (pid != (bpf_get_current_pid_tgid() >> 32))
		return 0;
#ifdef ENABLE_ATOMICS_TESTS
	__u64 val64 = 2;
	__u32 val32 = 2;

	xchg64_result = __sync_lock_test_and_set(&xchg64_value, val64);
	xchg32_result = __sync_lock_test_and_set(&xchg32_value, val32);
#endif

	return 0;
}
