// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/value_or_null.c */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

#define MAX_ENTRIES 11

struct test_val {
	unsigned int index;
	int foo[MAX_ENTRIES];
};

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1);
	__type(key, long long);
	__type(value, struct test_val);
} map_hash_48b SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1);
	__type(key, long long);
	__type(value, long long);
} map_hash_8b SEC(".maps");

SEC("tc")
__description("multiple registers share map_lookup_elem result")
__success __retval(0)
__naked void share_map_lookup_elem_result(void)
{
	asm volatile ("					\
	r1 = 10;					\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	r4 = r0;					\
	if r0 == 0 goto l0_%=;				\
	r1 = 0;						\
	*(u64*)(r4 + 0) = r1;				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("tc")
__description("alu ops on ptr_to_map_value_or_null, 1")
__failure __msg("R4 pointer arithmetic on map_value_or_null")
__naked void map_value_or_null_1(void)
{
	asm volatile ("					\
	r1 = 10;					\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	r4 = r0;					\
	r4 += -2;					\
	r4 += 2;					\
	if r0 == 0 goto l0_%=;				\
	r1 = 0;						\
	*(u64*)(r4 + 0) = r1;				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("tc")
__description("alu ops on ptr_to_map_value_or_null, 2")
__failure __msg("R4 pointer arithmetic on map_value_or_null")
__naked void map_value_or_null_2(void)
{
	asm volatile ("					\
	r1 = 10;					\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	r4 = r0;					\
	r4 &= -1;					\
	if r0 == 0 goto l0_%=;				\
	r1 = 0;						\
	*(u64*)(r4 + 0) = r1;				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("tc")
__description("alu ops on ptr_to_map_value_or_null, 3")
__failure __msg("R4 pointer arithmetic on map_value_or_null")
__naked void map_value_or_null_3(void)
{
	asm volatile ("					\
	r1 = 10;					\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	r4 = r0;					\
	r4 <<= 1;					\
	if r0 == 0 goto l0_%=;				\
	r1 = 0;						\
	*(u64*)(r4 + 0) = r1;				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("tc")
__description("invalid memory access with multiple map_lookup_elem calls")
__failure __msg("R4 !read_ok")
__naked void multiple_map_lookup_elem_calls(void)
{
	asm volatile ("					\
	r1 = 10;					\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_8b] ll;				\
	r8 = r1;					\
	r7 = r2;					\
	call %[bpf_map_lookup_elem];			\
	r4 = r0;					\
	r1 = r8;					\
	r2 = r7;					\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = 0;						\
	*(u64*)(r4 + 0) = r1;				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("tc")
__description("valid indirect map_lookup_elem access with 2nd lookup in branch")
__success __retval(0)
__naked void with_2nd_lookup_in_branch(void)
{
	asm volatile ("					\
	r1 = 10;					\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_8b] ll;				\
	r8 = r1;					\
	r7 = r2;					\
	call %[bpf_map_lookup_elem];			\
	r2 = 10;					\
	if r2 != 0 goto l0_%=;				\
	r1 = r8;					\
	r2 = r7;					\
	call %[bpf_map_lookup_elem];			\
l0_%=:	r4 = r0;					\
	if r0 == 0 goto l1_%=;				\
	r1 = 0;						\
	*(u64*)(r4 + 0) = r1;				\
l1_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("socket")
__description("invalid map access from else condition")
__failure __msg("R0 unbounded memory access")
__failure_unpriv __msg_unpriv("R0 leaks addr")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void map_access_from_else_condition(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = *(u32*)(r0 + 0);				\
	if r1 >= %[__imm_0] goto l1_%=;			\
	r1 += 1;					\
l1_%=:	r1 <<= 2;					\
	r0 += r1;					\
	r1 = %[test_val_foo];				\
	*(u64*)(r0 + 0) = r1;				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_48b),
	  __imm_const(__imm_0, MAX_ENTRIES-1),
	  __imm_const(test_val_foo, offsetof(struct test_val, foo))
	: __clobber_all);
}

SEC("tc")
__description("map lookup and null branch prediction")
__success __retval(0)
__naked void lookup_and_null_branch_prediction(void)
{
	asm volatile ("					\
	r1 = 10;					\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	r6 = r0;					\
	if r6 == 0 goto l0_%=;				\
	if r6 != 0 goto l0_%=;				\
	r10 += 10;					\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("cgroup/skb")
__description("MAP_VALUE_OR_NULL check_ids() in regsafe()")
__failure __msg("R8 invalid mem access 'map_value_or_null'")
__failure_unpriv __msg_unpriv("")
__flag(BPF_F_TEST_STATE_FREQ)
__naked void null_check_ids_in_regsafe(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	/* r9 = map_lookup_elem(...) */			\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	r9 = r0;					\
	/* r8 = map_lookup_elem(...) */			\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	r8 = r0;					\
	/* r7 = ktime_get_ns() */			\
	call %[bpf_ktime_get_ns];			\
	r7 = r0;					\
	/* r6 = ktime_get_ns() */			\
	call %[bpf_ktime_get_ns];			\
	r6 = r0;					\
	/* if r6 > r7 goto +1    ; no new information about the state is derived from\
	 *                       ; this check, thus produced verifier states differ\
	 *                       ; only in 'insn_idx'	\
	 * r9 = r8               ; optionally share ID between r9 and r8\
	 */						\
	if r6 > r7 goto l0_%=;				\
	r9 = r8;					\
l0_%=:	/* if r9 == 0 goto <exit> */			\
	if r9 == 0 goto l1_%=;				\
	/* read map value via r8, this is not always	\
	 * safe because r8 might be not equal to r9.	\
	 */						\
	r0 = *(u64*)(r8 + 0);				\
l1_%=:	/* exit 0 */					\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_ktime_get_ns),
	  __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
