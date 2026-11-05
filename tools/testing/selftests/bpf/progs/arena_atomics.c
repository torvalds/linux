// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <bpf_arena_common.h>
#include "../../../include/linux/filter.h"
#include "bpf_misc.h"

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
bool skip_all_tests __attribute((__section__(".data"))) = false;
#else
bool skip_all_tests = true;
#endif

#if defined(ENABLE_ATOMICS_TESTS) &&		  \
	defined(__BPF_FEATURE_ADDR_SPACE_CAST) && \
	(defined(__TARGET_ARCH_arm64) || \
	 defined(__TARGET_ARCH_x86) || \
	 (defined(__TARGET_ARCH_riscv) && __riscv_xlen == 64) || \
	 defined(__TARGET_ARCH_s390))
bool skip_lacq_srel_tests __attribute((__section__(".data"))) = false;
#else
bool skip_lacq_srel_tests = true;
#endif

__u32 pid = 0;

__u64 __arena_global add64_value = 1;
__u64 __arena_global add64_result = 0;
__u32 __arena_global add32_value = 1;
__u32 __arena_global add32_result = 0;
__u64 __arena_global add_stack_value_copy = 0;
__u64 __arena_global add_stack_result = 0;
__u64 __arena_global add_noreturn_value = 1;

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

__s64 __arena_global sub64_value = 1;
__s64 __arena_global sub64_result = 0;
__s32 __arena_global sub32_value = 1;
__s32 __arena_global sub32_result = 0;
__s64 __arena_global sub_stack_value_copy = 0;
__s64 __arena_global sub_stack_result = 0;
__s64 __arena_global sub_noreturn_value = 1;

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

/*
 * A relaxed __c11_atomic_fetch_*() whose result is discarded is what selects
 * the non-fetching insn, so each operation keeps a *_noreturn_value of each
 * width to cover that lowering as well.
 */
__u64 __arena_global and64_value = (0x110ull << 32);
__u64 __arena_global and64_result = 0;
_Atomic __u64 __arena_global and64_noreturn_value = (0x110ull << 32);
__u32 __arena_global and32_value = 0x110;
__u32 __arena_global and32_result = 0;
_Atomic __u32 __arena_global and32_noreturn_value = 0x110;

SEC("raw_tp/sys_enter")
int and(const void *ctx)
{
	if (pid != (bpf_get_current_pid_tgid() >> 32))
		return 0;
#ifdef ENABLE_ATOMICS_TESTS
	and64_result = __sync_fetch_and_and(&and64_value, 0x011ull << 32);
	and32_result = __sync_fetch_and_and(&and32_value, 0x011);
	__c11_atomic_fetch_and(&and64_noreturn_value, 0x011ull << 32, memory_order_relaxed);
	__c11_atomic_fetch_and(&and32_noreturn_value, 0x011, memory_order_relaxed);
#endif

	return 0;
}

/* u32s first: .addr_space.1 must not have holes, see the clang-17 note below */
__u32 __arena_global or32_value = 0x110;
__u32 __arena_global or32_result = 0;
_Atomic __u32 __arena_global or32_noreturn_value = 0x110;
__u64 __arena_global or64_value = (0x110ull << 32);
__u64 __arena_global or64_result = 0;
_Atomic __u64 __arena_global or64_noreturn_value = (0x110ull << 32);

SEC("raw_tp/sys_enter")
int or(const void *ctx)
{
	if (pid != (bpf_get_current_pid_tgid() >> 32))
		return 0;
#ifdef ENABLE_ATOMICS_TESTS
	or64_result = __sync_fetch_and_or(&or64_value, 0x011ull << 32);
	or32_result = __sync_fetch_and_or(&or32_value, 0x011);
	__c11_atomic_fetch_or(&or64_noreturn_value, 0x011ull << 32, memory_order_relaxed);
	__c11_atomic_fetch_or(&or32_noreturn_value, 0x011, memory_order_relaxed);
#endif

	return 0;
}

__u64 __arena_global xor64_value = (0x110ull << 32);
__u64 __arena_global xor64_result = 0;
_Atomic __u64 __arena_global xor64_noreturn_value = (0x110ull << 32);
__u32 __arena_global xor32_value = 0x110;
__u32 __arena_global xor32_result = 0;
_Atomic __u32 __arena_global xor32_noreturn_value = 0x110;

SEC("raw_tp/sys_enter")
int xor(const void *ctx)
{
	if (pid != (bpf_get_current_pid_tgid() >> 32))
		return 0;
#ifdef ENABLE_ATOMICS_TESTS
	xor64_result = __sync_fetch_and_xor(&xor64_value, 0x011ull << 32);
	xor32_result = __sync_fetch_and_xor(&xor32_value, 0x011);
	__c11_atomic_fetch_xor(&xor64_noreturn_value, 0x011ull << 32, memory_order_relaxed);
	__c11_atomic_fetch_xor(&xor32_noreturn_value, 0x011, memory_order_relaxed);
#endif

	return 0;
}

__u32 __arena_global cmpxchg32_value = 1;
__u32 __arena_global cmpxchg32_result_fail = 0;
__u32 __arena_global cmpxchg32_result_succeed = 0;
__u64 __arena_global cmpxchg64_value = 1;
__u64 __arena_global cmpxchg64_result_fail = 0;
__u64 __arena_global cmpxchg64_result_succeed = 0;

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

__u64 __arena_global xchg64_value = 1;
__u64 __arena_global xchg64_result = 0;
__u32 __arena_global xchg32_value = 1;
__u32 __arena_global xchg32_result = 0;

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

__u64 __arena_global fetch_src_r0_value = 0x11000000110ull;
__u64 __arena_global fetch_src_r0_result = 0;
__u64 __arena_global fetch_dst_r0_value = 0x11000000110ull;
__u64 __arena_global fetch_dst_r0_result = 0;
__u64 __arena_global fetch_dst_r0_readback = 0;

/*
 * A fetching OR with the operand in r0, and one with the arena pointer in r0.
 * The x86 JIT needs RAX for its CMPXCHG loop and substitutes BPF_REG_AX for
 * whichever of the two is r0, so both have to keep working. Hand-written
 * because clang picks its own registers and will not reliably emit either.
 */
SEC("raw_tp/sys_enter")
int fetch_r0(const void *ctx)
{
	if (pid != (bpf_get_current_pid_tgid() >> 32))
		return 0;
#if defined(ENABLE_ATOMICS_TESTS) && defined(__BPF_FEATURE_ADDR_SPACE_CAST)
	asm volatile (
	"r1 = %[fetch_src_r0_value] ll;"
	"r1 = addr_space_cast(r1, 0x0, 0x1);"
	"r0 = 0x1100000011 ll;"
	".8byte %[fetch_src_r0_insn];"
	"r2 = %[fetch_src_r0_result] ll;"
	"r2 = addr_space_cast(r2, 0x0, 0x1);"
	"*(u64 *)(r2 + 0) = r0;"
	:
	: __imm_addr(fetch_src_r0_value),
	  __imm_insn(fetch_src_r0_insn,
		     BPF_ATOMIC_OP(BPF_DW, BPF_OR | BPF_FETCH, BPF_REG_1, BPF_REG_0, 0)),
	  __imm_addr(fetch_src_r0_result)
	: __clobber_all);

	asm volatile (
	"r0 = %[fetch_dst_r0_value] ll;"
	"r0 = addr_space_cast(r0, 0x0, 0x1);"
	"r1 = 0x1100000011 ll;"
	".8byte %[fetch_dst_r0_insn];"
	"r2 = %[fetch_dst_r0_result] ll;"
	"r2 = addr_space_cast(r2, 0x0, 0x1);"
	"*(u64 *)(r2 + 0) = r1;"
	/* r0 is only read by the atomic, so it must still be the pointer. */
	"r3 = *(u64 *)(r0 + 0);"
	"r2 = %[fetch_dst_r0_readback] ll;"
	"r2 = addr_space_cast(r2, 0x0, 0x1);"
	"*(u64 *)(r2 + 0) = r3;"
	:
	: __imm_addr(fetch_dst_r0_value),
	  __imm_insn(fetch_dst_r0_insn,
		     BPF_ATOMIC_OP(BPF_DW, BPF_OR | BPF_FETCH, BPF_REG_0, BPF_REG_1, 0)),
	  __imm_addr(fetch_dst_r0_result),
	  __imm_addr(fetch_dst_r0_readback)
	: __clobber_all);
#endif

	return 0;
}

__u64 __arena_global uaf_sink;
volatile __u64 __arena_global uaf_recovery_fails;
/* Register state after the r0 forms below fault, checked by the test */
__u64 __arena_global uaf_r1_after_dst_r0;
__u64 __arena_global uaf_r0_before_dst_r0;
__u64 __arena_global uaf_r0_after_dst_r0;
__u64 __arena_global uaf_r0_after_src_r0;
/* What reading the faulted address again gives, see the end of uaf() */
__u64 __arena_global uaf_reread;

SEC("syscall")
int uaf(const void *ctx)
{
	if (pid != (bpf_get_current_pid_tgid() >> 32))
		return 0;
/*
 * __BPF_FEATURE_ADDR_SPACE_CAST is required by the asm below, which spells
 * addr_space_cast textually. That and the mnemonic both arrived in LLVM 19.
 */
#if defined(ENABLE_ATOMICS_TESTS) && !defined(__TARGET_ARCH_arm64) && \
	defined(__BPF_FEATURE_ADDR_SPACE_CAST)
	__u32 __arena *page32;
	__u64 __arena *page64;
	void __arena *page;

	page = bpf_arena_alloc_pages(&arena, NULL, 1, NUMA_NO_NODE, 0);
	bpf_arena_free_pages(&arena, page, 1);
	uaf_recovery_fails = 26;
	uaf_r1_after_dst_r0 = 1;
	uaf_r0_after_dst_r0 = 1;
	uaf_r0_after_src_r0 = 1;
	uaf_reread = 1;

	page32 = (__u32 __arena *)page;
	uaf_sink += __sync_fetch_and_add(page32, 1);
	uaf_recovery_fails -= 1;
	__sync_add_and_fetch(page32, 1);
	uaf_recovery_fails -= 1;
	uaf_sink += __sync_fetch_and_sub(page32, 1);
	uaf_recovery_fails -= 1;
	__sync_sub_and_fetch(page32, 1);
	uaf_recovery_fails -= 1;
	uaf_sink += __sync_fetch_and_and(page32, 1);
	uaf_recovery_fails -= 1;
	__sync_and_and_fetch(page32, 1);
	uaf_recovery_fails -= 1;
	uaf_sink += __sync_fetch_and_or(page32, 1);
	uaf_recovery_fails -= 1;
	__sync_or_and_fetch(page32, 1);
	uaf_recovery_fails -= 1;
	uaf_sink += __sync_fetch_and_xor(page32, 1);
	uaf_recovery_fails -= 1;
	__sync_xor_and_fetch(page32, 1);
	uaf_recovery_fails -= 1;
	uaf_sink += __sync_val_compare_and_swap(page32, 0, 1);
	uaf_recovery_fails -= 1;
	uaf_sink += __sync_lock_test_and_set(page32, 1);
	uaf_recovery_fails -= 1;

	page64 = (__u64 __arena *)page;
	uaf_sink += __sync_fetch_and_add(page64, 1);
	uaf_recovery_fails -= 1;
	__sync_add_and_fetch(page64, 1);
	uaf_recovery_fails -= 1;
	uaf_sink += __sync_fetch_and_sub(page64, 1);
	uaf_recovery_fails -= 1;
	__sync_sub_and_fetch(page64, 1);
	uaf_recovery_fails -= 1;
	uaf_sink += __sync_fetch_and_and(page64, 1);
	uaf_recovery_fails -= 1;
	__sync_and_and_fetch(page64, 1);
	uaf_recovery_fails -= 1;
	uaf_sink += __sync_fetch_and_or(page64, 1);
	uaf_recovery_fails -= 1;
	__sync_or_and_fetch(page64, 1);
	uaf_recovery_fails -= 1;
	uaf_sink += __sync_fetch_and_xor(page64, 1);
	uaf_recovery_fails -= 1;
	__sync_xor_and_fetch(page64, 1);
	uaf_recovery_fails -= 1;
	uaf_sink += __sync_val_compare_and_swap(page64, 0, 1);
	uaf_recovery_fails -= 1;
	uaf_sink += __sync_lock_test_and_set(page64, 1);
	uaf_recovery_fails -= 1;

	/*
	 * The x86 JIT needs RAX for the CMPXCHG loop it lowers a fetching
	 * AND/OR/XOR into, and substitutes BPF_REG_AX for r0 when r0 is either
	 * operand. Only then does the fault fixup have to name that register,
	 * so spell both forms out: clang picks its own and never produces them.
	 *
	 * The freed page is the first of the arena, whose 32-bit address is 0,
	 * which would make "the address register was left alone" an empty
	 * check. Use the page after it, which is unmapped as well.
	 */
	asm volatile (
	"r0 = %[page];"
	"r0 += 4096;"
	"r0 = addr_space_cast(r0, 0x0, 0x1);"
	"r1 = 1;"
	"r2 = %[uaf_r0_before_dst_r0] ll;"
	"r2 = addr_space_cast(r2, 0x0, 0x1);"
	"*(u64 *)(r2 + 0) = r0;"
	".8byte %[fetch_dst_r0];"
	"r2 = %[uaf_r0_after_dst_r0] ll;"
	"r2 = addr_space_cast(r2, 0x0, 0x1);"
	"*(u64 *)(r2 + 0) = r0;"
	"r2 = %[uaf_r1_after_dst_r0] ll;"
	"r2 = addr_space_cast(r2, 0x0, 0x1);"
	"*(u64 *)(r2 + 0) = r1;"
	: : [page]"r"(page64),
	    __imm_insn(fetch_dst_r0,
		       BPF_ATOMIC_OP(BPF_DW, BPF_OR | BPF_FETCH, BPF_REG_0, BPF_REG_1, 0)),
	    __imm_addr(uaf_r0_before_dst_r0),
	    __imm_addr(uaf_r0_after_dst_r0),
	    __imm_addr(uaf_r1_after_dst_r0)
	: "r0", "r1", "r2", "memory");
	uaf_recovery_fails -= 1;

	asm volatile (
	"r1 = %[page];"
	"r1 += 4096;"
	"r1 = addr_space_cast(r1, 0x0, 0x1);"
	"r0 = 1;"
	".8byte %[fetch_src_r0];"
	"r2 = %[uaf_r0_after_src_r0] ll;"
	"r2 = addr_space_cast(r2, 0x0, 0x1);"
	"*(u64 *)(r2 + 0) = r0;"
	: : [page]"r"(page64),
	    __imm_insn(fetch_src_r0,
		       BPF_ATOMIC_OP(BPF_DW, BPF_OR | BPF_FETCH, BPF_REG_1, BPF_REG_0, 0)),
	    __imm_addr(uaf_r0_after_src_r0)
	: "r0", "r1", "r2", "memory");
	uaf_recovery_fails -= 1;

	/*
	 * If a fault were recovered by the arena mapping its zeroed scratch
	 * page rather than by the JIT's exception table entry, the access above
	 * would have succeeded and written there. The page has to still be
	 * unmapped, so that the read faults and gives 0.
	 */
	asm volatile (
	"r1 = %[page];"
	"r1 += 4096;"
	"r1 = addr_space_cast(r1, 0x0, 0x1);"
	"r3 = *(u64 *)(r1 + 0);"
	"r2 = %[uaf_reread] ll;"
	"r2 = addr_space_cast(r2, 0x0, 0x1);"
	"*(u64 *)(r2 + 0) = r3;"
	: : [page]"r"(page64),
	    __imm_addr(uaf_reread)
	: "r1", "r2", "r3", "memory");
#endif

	return 0;
}

#if __clang_major__ >= 18
__u8 __arena_global load_acquire8_value = 0x12;
__u16 __arena_global load_acquire16_value = 0x1234;
__u32 __arena_global load_acquire32_value = 0x12345678;
__u64 __arena_global load_acquire64_value = 0x1234567890abcdef;

__u8 __arena_global load_acquire8_result = 0;
__u16 __arena_global load_acquire16_result = 0;
__u32 __arena_global load_acquire32_result = 0;
__u64 __arena_global load_acquire64_result = 0;
#else
/* clang-17 crashes if the .addr_space.1 ELF section has holes. Work around
 * this issue by defining the below variables as 64-bit.
 */
__u64 __arena_global load_acquire8_value;
__u64 __arena_global load_acquire16_value;
__u64 __arena_global load_acquire32_value;
__u64 __arena_global load_acquire64_value;

__u64 __arena_global load_acquire8_result;
__u64 __arena_global load_acquire16_result;
__u64 __arena_global load_acquire32_result;
__u64 __arena_global load_acquire64_result;
#endif

SEC("raw_tp/sys_enter")
int load_acquire(const void *ctx)
{
#if defined(ENABLE_ATOMICS_TESTS) &&		  \
	defined(__BPF_FEATURE_ADDR_SPACE_CAST) && \
	(defined(__TARGET_ARCH_arm64) || \
	 defined(__TARGET_ARCH_x86) || \
	 (defined(__TARGET_ARCH_riscv) && __riscv_xlen == 64) || \
	 defined(__TARGET_ARCH_s390))

#define LOAD_ACQUIRE_ARENA(SIZEOP, SIZE, SRC, DST)	\
	{ asm volatile (				\
	"r1 = %[" #SRC "] ll;"				\
	"r1 = addr_space_cast(r1, 0x0, 0x1);"		\
	".8byte %[load_acquire_insn];"			\
	"r3 = %[" #DST "] ll;"				\
	"r3 = addr_space_cast(r3, 0x0, 0x1);"		\
	"*(" #SIZE " *)(r3 + 0) = r2;"			\
	:						\
	: __imm_addr(SRC),				\
	  __imm_insn(load_acquire_insn,			\
		     BPF_ATOMIC_OP(BPF_##SIZEOP, BPF_LOAD_ACQ,	\
				   BPF_REG_2, BPF_REG_1, 0)),	\
	  __imm_addr(DST)				\
	: __clobber_all); }				\

	LOAD_ACQUIRE_ARENA(B, u8, load_acquire8_value, load_acquire8_result)
	LOAD_ACQUIRE_ARENA(H, u16, load_acquire16_value,
			   load_acquire16_result)
	LOAD_ACQUIRE_ARENA(W, u32, load_acquire32_value,
			   load_acquire32_result)
	LOAD_ACQUIRE_ARENA(DW, u64, load_acquire64_value,
			   load_acquire64_result)
#undef LOAD_ACQUIRE_ARENA

#endif
	return 0;
}

#if __clang_major__ >= 18
__u8 __arena_global store_release8_result = 0;
__u16 __arena_global store_release16_result = 0;
__u32 __arena_global store_release32_result = 0;
__u64 __arena_global store_release64_result = 0;
#else
/* clang-17 crashes if the .addr_space.1 ELF section has holes. Work around
 * this issue by defining the below variables as 64-bit.
 */
__u64 __arena_global store_release8_result;
__u64 __arena_global store_release16_result;
__u64 __arena_global store_release32_result;
__u64 __arena_global store_release64_result;
#endif

SEC("raw_tp/sys_enter")
int store_release(const void *ctx)
{
#if defined(ENABLE_ATOMICS_TESTS) &&		  \
	defined(__BPF_FEATURE_ADDR_SPACE_CAST) && \
	(defined(__TARGET_ARCH_arm64) || \
	 defined(__TARGET_ARCH_x86) || \
	 (defined(__TARGET_ARCH_riscv) && __riscv_xlen == 64) || \
	 defined(__TARGET_ARCH_s390))

#define STORE_RELEASE_ARENA(SIZEOP, DST, VAL)	\
	{ asm volatile (			\
	"r1 = " VAL ";"				\
	"r2 = %[" #DST "] ll;"			\
	"r2 = addr_space_cast(r2, 0x0, 0x1);"	\
	".8byte %[store_release_insn];"		\
	:					\
	: __imm_addr(DST),			\
	  __imm_insn(store_release_insn,	\
		     BPF_ATOMIC_OP(BPF_##SIZEOP, BPF_STORE_REL,	\
				   BPF_REG_2, BPF_REG_1, 0))	\
	: __clobber_all); }			\

	STORE_RELEASE_ARENA(B, store_release8_result, "0x12")
	STORE_RELEASE_ARENA(H, store_release16_result, "0x1234")
	STORE_RELEASE_ARENA(W, store_release32_result, "0x12345678")
	STORE_RELEASE_ARENA(DW, store_release64_result,
			    "0x1234567890abcdef ll")
#undef STORE_RELEASE_ARENA

#endif
	return 0;
}

char _license[] SEC("license") = "GPL";
