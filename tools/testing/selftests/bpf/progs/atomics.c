// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <stdbool.h>

#ifdef ENABLE_ATOMICS_TESTS
bool skip_tests __attribute((__section__(".data"))) = false;
#else
bool skip_tests = true;
#endif

__u32 pid = 0;

__u64 add64_value = 1;
__u64 add64_result = 0;
__u32 add32_value = 1;
__u32 add32_result = 0;
__u64 add_stack_value_copy = 0;
__u64 add_stack_result = 0;
__u64 add_noreturn_value = 1;

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

__s64 sub64_value = 1;
__s64 sub64_result = 0;
__s32 sub32_value = 1;
__s32 sub32_result = 0;
__s64 sub_stack_value_copy = 0;
__s64 sub_stack_result = 0;
__s64 sub_noreturn_value = 1;

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

__u64 and64_value = (0x110ull << 32);
__u64 and64_result = 0;
__u32 and32_value = 0x110;
__u32 and32_result = 0;
__u64 and_noreturn_value = (0x110ull << 32);

SEC("raw_tp/sys_enter")
int and(const void *ctx)
{
	if (pid != (bpf_get_current_pid_tgid() >> 32))
		return 0;
#ifdef ENABLE_ATOMICS_TESTS

	and64_result = __sync_fetch_and_and(&and64_value, 0x011ull << 32);
	and32_result = __sync_fetch_and_and(&and32_value, 0x011);
	__sync_fetch_and_and(&and_noreturn_value, 0x011ull << 32);
#endif

	return 0;
}

__u64 or64_value = (0x110ull << 32);
__u64 or64_result = 0;
__u32 or32_value = 0x110;
__u32 or32_result = 0;
__u64 or_noreturn_value = (0x110ull << 32);

SEC("raw_tp/sys_enter")
int or(const void *ctx)
{
	if (pid != (bpf_get_current_pid_tgid() >> 32))
		return 0;
#ifdef ENABLE_ATOMICS_TESTS
	or64_result = __sync_fetch_and_or(&or64_value, 0x011ull << 32);
	or32_result = __sync_fetch_and_or(&or32_value, 0x011);
	__sync_fetch_and_or(&or_noreturn_value, 0x011ull << 32);
#endif

	return 0;
}

__u64 xor64_value = (0x110ull << 32);
__u64 xor64_result = 0;
__u32 xor32_value = 0x110;
__u32 xor32_result = 0;
__u64 xor_noreturn_value = (0x110ull << 32);

SEC("raw_tp/sys_enter")
int xor(const void *ctx)
{
	if (pid != (bpf_get_current_pid_tgid() >> 32))
		return 0;
#ifdef ENABLE_ATOMICS_TESTS
	xor64_result = __sync_fetch_and_xor(&xor64_value, 0x011ull << 32);
	xor32_result = __sync_fetch_and_xor(&xor32_value, 0x011);
	__sync_fetch_and_xor(&xor_noreturn_value, 0x011ull << 32);
#endif

	return 0;
}

__u64 cmpxchg64_value = 1;
__u64 cmpxchg64_result_fail = 0;
__u64 cmpxchg64_result_succeed = 0;
__u32 cmpxchg32_value = 1;
__u32 cmpxchg32_result_fail = 0;
__u32 cmpxchg32_result_succeed = 0;

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

__u64 xchg64_value = 1;
__u64 xchg64_result = 0;
__u32 xchg32_value = 1;
__u32 xchg32_result = 0;

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
