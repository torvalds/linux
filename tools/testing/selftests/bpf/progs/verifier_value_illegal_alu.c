// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/value_illegal_alu.c */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "../../../include/linux/filter.h"
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

SEC("socket")
__description("map element value illegal alu op, 1")
__failure __msg("R0 bitwise operator &= on pointer")
__failure_unpriv
__naked void value_illegal_alu_op_1(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r0 &= 8;					\
	r1 = 22;					\
	*(u64*)(r0 + 0) = r1;				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_48b)
	: __clobber_all);
}

SEC("socket")
__description("map element value illegal alu op, 2")
__failure __msg("R0 32-bit pointer arithmetic prohibited")
__failure_unpriv
__naked void value_illegal_alu_op_2(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	w0 += 0;					\
	r1 = 22;					\
	*(u64*)(r0 + 0) = r1;				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_48b)
	: __clobber_all);
}

SEC("socket")
__description("map element value illegal alu op, 3")
__failure __msg("R0 pointer arithmetic with /= operator")
__failure_unpriv
__naked void value_illegal_alu_op_3(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r0 /= 42;					\
	r1 = 22;					\
	*(u64*)(r0 + 0) = r1;				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_48b)
	: __clobber_all);
}

SEC("socket")
__description("map element value illegal alu op, 4")
__failure __msg("invalid mem access 'scalar'")
__failure_unpriv __msg_unpriv("R0 pointer arithmetic prohibited")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void value_illegal_alu_op_4(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r0 = be64 r0;					\
	r1 = 22;					\
	*(u64*)(r0 + 0) = r1;				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_48b)
	: __clobber_all);
}

SEC("socket")
__description("map element value illegal alu op, 5")
__failure __msg("R0 invalid mem access 'scalar'")
__msg_unpriv("leaking pointer from stack off -8")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void value_illegal_alu_op_5(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r3 = 4096;					\
	r2 = r10;					\
	r2 += -8;					\
	*(u64*)(r2 + 0) = r0;				\
	lock *(u64 *)(r2 + 0) += r3;			\
	r0 = *(u64*)(r2 + 0);				\
	r1 = 22;					\
	*(u64*)(r0 + 0) = r1;				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_48b)
	: __clobber_all);
}

SEC("socket")
__description("map_ptr illegal alu op, map_ptr = -map_ptr")
__failure __msg("R0 invalid mem access 'scalar'")
__failure_unpriv __msg_unpriv("R0 pointer arithmetic prohibited")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void map_ptr_illegal_alu_op(void)
{
	asm volatile ("					\
	r0 = %[map_hash_48b] ll;			\
	r0 = -r0;					\
	r1 = 22;					\
	*(u64*)(r0 + 0) = r1;				\
	exit;						\
"	:
	: __imm_addr(map_hash_48b)
	: __clobber_all);
}

SEC("flow_dissector")
__description("flow_keys illegal alu op with variable offset")
__failure __msg("R7 pointer arithmetic on flow_keys prohibited")
__naked void flow_keys_illegal_variable_offset_alu(void)
{
	asm volatile("					\
	r6 = r1;					\
	r7 = *(u64*)(r6 + %[flow_keys_off]);		\
	r8 = 8;						\
	r8 /= 1;					\
	r8 &= 8;					\
	r7 += r8;					\
	r0 = *(u64*)(r7 + 0);				\
	exit;						\
"	:
	: __imm_const(flow_keys_off, offsetof(struct __sk_buff, flow_keys))
	: __clobber_all);
}

#define DEFINE_BAD_OFFSET_TEST(name, op, off, imm)	\
	SEC("socket")					\
	__failure __msg("BPF_ALU uses reserved fields") \
	__naked void name(void)				\
	{						\
		asm volatile(				\
			"r0 = 1;"			\
			".8byte %[insn];"		\
			"r0 = 0;"			\
			"exit;"				\
		:					\
		: __imm_insn(insn, BPF_RAW_INSN((op), 0, 0, (off), (imm))) \
		: __clobber_all);			\
	}

/*
 * Offset fields of 0 and 1 are legal for BPF_{DIV,MOD} instructions.
 * Offset fields of 0 are legal for the rest of ALU instructions.
 * Test that error is reported for illegal offsets, assuming that tests
 * for legal offsets exist.
 */
DEFINE_BAD_OFFSET_TEST(bad_offset_divx, BPF_ALU64 | BPF_DIV | BPF_X, -1, 0)
DEFINE_BAD_OFFSET_TEST(bad_offset_modk, BPF_ALU64 | BPF_MOD | BPF_K, -1, 1)
DEFINE_BAD_OFFSET_TEST(bad_offset_addx, BPF_ALU64 | BPF_ADD | BPF_X, -1, 0)
DEFINE_BAD_OFFSET_TEST(bad_offset_divx2, BPF_ALU64 | BPF_DIV | BPF_X, 2, 0)
DEFINE_BAD_OFFSET_TEST(bad_offset_modk2, BPF_ALU64 | BPF_MOD | BPF_K, 2, 1)
DEFINE_BAD_OFFSET_TEST(bad_offset_addx2, BPF_ALU64 | BPF_ADD | BPF_X, 1, 0)

char _license[] SEC("license") = "GPL";
