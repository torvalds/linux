// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/bpf_get_stack.c */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

#define MAX_ENTRIES 11

struct test_val {
	unsigned int index;
	int foo[MAX_ENTRIES];
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, struct test_val);
} map_array_48b SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1);
	__type(key, long long);
	__type(value, struct test_val);
} map_hash_48b SEC(".maps");

SEC("tracepoint")
__description("bpf_get_stack return R0 within range")
__success
__naked void stack_return_r0_within_range(void)
{
	asm volatile ("					\
	r6 = r1;					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r7 = r0;					\
	r9 = %[__imm_0];				\
	r1 = r6;					\
	r2 = r7;					\
	r3 = %[__imm_0];				\
	r4 = 256;					\
	call %[bpf_get_stack];				\
	r1 = 0;						\
	r8 = r0;					\
	r8 <<= 32;					\
	r8 s>>= 32;					\
	if r1 s> r8 goto l0_%=;				\
	r9 -= r8;					\
	r2 = r7;					\
	r2 += r8;					\
	r1 = r9;					\
	r1 <<= 32;					\
	r1 s>>= 32;					\
	r3 = r2;					\
	r3 += r1;					\
	r1 = r7;					\
	r5 = %[__imm_0];				\
	r1 += r5;					\
	if r3 >= r1 goto l0_%=;				\
	r1 = r6;					\
	r3 = r9;					\
	r4 = 0;						\
	call %[bpf_get_stack];				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_get_stack),
	  __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_48b),
	  __imm_const(__imm_0, sizeof(struct test_val) / 2)
	: __clobber_all);
}

SEC("iter/task")
__description("bpf_get_task_stack return R0 range is refined")
__success
__naked void return_r0_range_is_refined(void)
{
	asm volatile ("					\
	r6 = *(u64*)(r1 + 0);				\
	r6 = *(u64*)(r6 + 0);		/* ctx->meta->seq */\
	r7 = *(u64*)(r1 + 8);		/* ctx->task */\
	r1 = %[map_array_48b] ll;	/* fixup_map_array_48b */\
	r2 = 0;						\
	*(u64*)(r10 - 8) = r2;				\
	r2 = r10;					\
	r2 += -8;					\
	call %[bpf_map_lookup_elem];			\
	if r0 != 0 goto l0_%=;				\
	r0 = 0;						\
	exit;						\
l0_%=:	if r7 != 0 goto l1_%=;				\
	r0 = 0;						\
	exit;						\
l1_%=:	r1 = r7;					\
	r2 = r0;					\
	r9 = r0;			/* keep buf for seq_write */\
	r3 = 48;					\
	r4 = 0;						\
	call %[bpf_get_task_stack];			\
	if r0 s> 0 goto l2_%=;				\
	r0 = 0;						\
	exit;						\
l2_%=:	r1 = r6;					\
	r2 = r9;					\
	r3 = r0;					\
	call %[bpf_seq_write];				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_task_stack),
	  __imm(bpf_map_lookup_elem),
	  __imm(bpf_seq_write),
	  __imm_addr(map_array_48b)
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
