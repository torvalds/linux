// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/helper_access_var_len.c */

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

struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
	__uint(max_entries, 4096);
} map_ringbuf SEC(".maps");

SEC("tracepoint")
__description("helper access to variable memory: stack, bitwise AND + JMP, correct bounds")
__success
__naked void bitwise_and_jmp_correct_bounds(void)
{
	asm volatile ("					\
	r1 = r10;					\
	r1 += -64;					\
	r0 = 0;						\
	*(u64*)(r10 - 64) = r0;				\
	*(u64*)(r10 - 56) = r0;				\
	*(u64*)(r10 - 48) = r0;				\
	*(u64*)(r10 - 40) = r0;				\
	*(u64*)(r10 - 32) = r0;				\
	*(u64*)(r10 - 24) = r0;				\
	*(u64*)(r10 - 16) = r0;				\
	*(u64*)(r10 - 8) = r0;				\
	r2 = 16;					\
	*(u64*)(r1 - 128) = r2;				\
	r2 = *(u64*)(r1 - 128);				\
	r2 &= 64;					\
	r4 = 0;						\
	if r4 >= r2 goto l0_%=;				\
	r3 = 0;						\
	call %[bpf_probe_read_kernel];			\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_probe_read_kernel)
	: __clobber_all);
}

SEC("socket")
__description("helper access to variable memory: stack, bitwise AND, zero included")
/* in privileged mode reads from uninitialized stack locations are permitted */
__success __failure_unpriv
__msg_unpriv("invalid indirect read from stack R2 off -64+0 size 64")
__retval(0)
__naked void stack_bitwise_and_zero_included(void)
{
	asm volatile ("					\
	/* set max stack size */			\
	r6 = 0;						\
	*(u64*)(r10 - 128) = r6;			\
	/* set r3 to a random value */			\
	call %[bpf_get_prandom_u32];			\
	r3 = r0;					\
	/* use bitwise AND to limit r3 range to [0, 64] */\
	r3 &= 64;					\
	r1 = %[map_ringbuf] ll;				\
	r2 = r10;					\
	r2 += -64;					\
	r4 = 0;						\
	/* Call bpf_ringbuf_output(), it is one of a few helper functions with\
	 * ARG_CONST_SIZE_OR_ZERO parameter allowed in unpriv mode.\
	 * For unpriv this should signal an error, because memory at &fp[-64] is\
	 * not initialized.				\
	 */						\
	call %[bpf_ringbuf_output];			\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32),
	  __imm(bpf_ringbuf_output),
	  __imm_addr(map_ringbuf)
	: __clobber_all);
}

SEC("tracepoint")
__description("helper access to variable memory: stack, bitwise AND + JMP, wrong max")
__failure __msg("invalid indirect access to stack R1 off=-64 size=65")
__naked void bitwise_and_jmp_wrong_max(void)
{
	asm volatile ("					\
	r2 = *(u64*)(r1 + 8);				\
	r1 = r10;					\
	r1 += -64;					\
	*(u64*)(r1 - 128) = r2;				\
	r2 = *(u64*)(r1 - 128);				\
	r2 &= 65;					\
	r4 = 0;						\
	if r4 >= r2 goto l0_%=;				\
	r3 = 0;						\
	call %[bpf_probe_read_kernel];			\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_probe_read_kernel)
	: __clobber_all);
}

SEC("tracepoint")
__description("helper access to variable memory: stack, JMP, correct bounds")
__success
__naked void memory_stack_jmp_correct_bounds(void)
{
	asm volatile ("					\
	r1 = r10;					\
	r1 += -64;					\
	r0 = 0;						\
	*(u64*)(r10 - 64) = r0;				\
	*(u64*)(r10 - 56) = r0;				\
	*(u64*)(r10 - 48) = r0;				\
	*(u64*)(r10 - 40) = r0;				\
	*(u64*)(r10 - 32) = r0;				\
	*(u64*)(r10 - 24) = r0;				\
	*(u64*)(r10 - 16) = r0;				\
	*(u64*)(r10 - 8) = r0;				\
	r2 = 16;					\
	*(u64*)(r1 - 128) = r2;				\
	r2 = *(u64*)(r1 - 128);				\
	if r2 > 64 goto l0_%=;				\
	r4 = 0;						\
	if r4 >= r2 goto l0_%=;				\
	r3 = 0;						\
	call %[bpf_probe_read_kernel];			\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_probe_read_kernel)
	: __clobber_all);
}

SEC("tracepoint")
__description("helper access to variable memory: stack, JMP (signed), correct bounds")
__success
__naked void stack_jmp_signed_correct_bounds(void)
{
	asm volatile ("					\
	r1 = r10;					\
	r1 += -64;					\
	r0 = 0;						\
	*(u64*)(r10 - 64) = r0;				\
	*(u64*)(r10 - 56) = r0;				\
	*(u64*)(r10 - 48) = r0;				\
	*(u64*)(r10 - 40) = r0;				\
	*(u64*)(r10 - 32) = r0;				\
	*(u64*)(r10 - 24) = r0;				\
	*(u64*)(r10 - 16) = r0;				\
	*(u64*)(r10 - 8) = r0;				\
	r2 = 16;					\
	*(u64*)(r1 - 128) = r2;				\
	r2 = *(u64*)(r1 - 128);				\
	if r2 s> 64 goto l0_%=;				\
	r4 = 0;						\
	if r4 s>= r2 goto l0_%=;			\
	r3 = 0;						\
	call %[bpf_probe_read_kernel];			\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_probe_read_kernel)
	: __clobber_all);
}

SEC("tracepoint")
__description("helper access to variable memory: stack, JMP, bounds + offset")
__failure __msg("invalid indirect access to stack R1 off=-64 size=65")
__naked void memory_stack_jmp_bounds_offset(void)
{
	asm volatile ("					\
	r2 = *(u64*)(r1 + 8);				\
	r1 = r10;					\
	r1 += -64;					\
	*(u64*)(r1 - 128) = r2;				\
	r2 = *(u64*)(r1 - 128);				\
	if r2 > 64 goto l0_%=;				\
	r4 = 0;						\
	if r4 >= r2 goto l0_%=;				\
	r2 += 1;					\
	r3 = 0;						\
	call %[bpf_probe_read_kernel];			\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_probe_read_kernel)
	: __clobber_all);
}

SEC("tracepoint")
__description("helper access to variable memory: stack, JMP, wrong max")
__failure __msg("invalid indirect access to stack R1 off=-64 size=65")
__naked void memory_stack_jmp_wrong_max(void)
{
	asm volatile ("					\
	r2 = *(u64*)(r1 + 8);				\
	r1 = r10;					\
	r1 += -64;					\
	*(u64*)(r1 - 128) = r2;				\
	r2 = *(u64*)(r1 - 128);				\
	if r2 > 65 goto l0_%=;				\
	r4 = 0;						\
	if r4 >= r2 goto l0_%=;				\
	r3 = 0;						\
	call %[bpf_probe_read_kernel];			\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_probe_read_kernel)
	: __clobber_all);
}

SEC("tracepoint")
__description("helper access to variable memory: stack, JMP, no max check")
__failure
/* because max wasn't checked, signed min is negative */
__msg("R2 min value is negative, either use unsigned or 'var &= const'")
__naked void stack_jmp_no_max_check(void)
{
	asm volatile ("					\
	r2 = *(u64*)(r1 + 8);				\
	r1 = r10;					\
	r1 += -64;					\
	*(u64*)(r1 - 128) = r2;				\
	r2 = *(u64*)(r1 - 128);				\
	r4 = 0;						\
	if r4 >= r2 goto l0_%=;				\
	r3 = 0;						\
	call %[bpf_probe_read_kernel];			\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_probe_read_kernel)
	: __clobber_all);
}

SEC("socket")
__description("helper access to variable memory: stack, JMP, no min check")
/* in privileged mode reads from uninitialized stack locations are permitted */
__success __failure_unpriv
__msg_unpriv("invalid indirect read from stack R2 off -64+0 size 64")
__retval(0)
__naked void stack_jmp_no_min_check(void)
{
	asm volatile ("					\
	/* set max stack size */			\
	r6 = 0;						\
	*(u64*)(r10 - 128) = r6;			\
	/* set r3 to a random value */			\
	call %[bpf_get_prandom_u32];			\
	r3 = r0;					\
	/* use JMP to limit r3 range to [0, 64] */	\
	if r3 > 64 goto l0_%=;				\
	r1 = %[map_ringbuf] ll;				\
	r2 = r10;					\
	r2 += -64;					\
	r4 = 0;						\
	/* Call bpf_ringbuf_output(), it is one of a few helper functions with\
	 * ARG_CONST_SIZE_OR_ZERO parameter allowed in unpriv mode.\
	 * For unpriv this should signal an error, because memory at &fp[-64] is\
	 * not initialized.				\
	 */						\
	call %[bpf_ringbuf_output];			\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32),
	  __imm(bpf_ringbuf_output),
	  __imm_addr(map_ringbuf)
	: __clobber_all);
}

SEC("tracepoint")
__description("helper access to variable memory: stack, JMP (signed), no min check")
__failure __msg("R2 min value is negative")
__naked void jmp_signed_no_min_check(void)
{
	asm volatile ("					\
	r2 = *(u64*)(r1 + 8);				\
	r1 = r10;					\
	r1 += -64;					\
	*(u64*)(r1 - 128) = r2;				\
	r2 = *(u64*)(r1 - 128);				\
	if r2 s> 64 goto l0_%=;				\
	r3 = 0;						\
	call %[bpf_probe_read_kernel];			\
	r0 = 0;						\
l0_%=:	exit;						\
"	:
	: __imm(bpf_probe_read_kernel)
	: __clobber_all);
}

SEC("tracepoint")
__description("helper access to variable memory: map, JMP, correct bounds")
__success
__naked void memory_map_jmp_correct_bounds(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	r2 = %[sizeof_test_val];			\
	*(u64*)(r10 - 128) = r2;			\
	r2 = *(u64*)(r10 - 128);			\
	if r2 s> %[sizeof_test_val] goto l1_%=;		\
	r4 = 0;						\
	if r4 s>= r2 goto l1_%=;			\
	r3 = 0;						\
	call %[bpf_probe_read_kernel];			\
l1_%=:	r0 = 0;						\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm(bpf_probe_read_kernel),
	  __imm_addr(map_hash_48b),
	  __imm_const(sizeof_test_val, sizeof(struct test_val))
	: __clobber_all);
}

SEC("tracepoint")
__description("helper access to variable memory: map, JMP, wrong max")
__failure __msg("invalid access to map value, value_size=48 off=0 size=49")
__naked void memory_map_jmp_wrong_max(void)
{
	asm volatile ("					\
	r6 = *(u64*)(r1 + 8);				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	r2 = r6;					\
	*(u64*)(r10 - 128) = r2;			\
	r2 = *(u64*)(r10 - 128);			\
	if r2 s> %[__imm_0] goto l1_%=;			\
	r4 = 0;						\
	if r4 s>= r2 goto l1_%=;			\
	r3 = 0;						\
	call %[bpf_probe_read_kernel];			\
l1_%=:	r0 = 0;						\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm(bpf_probe_read_kernel),
	  __imm_addr(map_hash_48b),
	  __imm_const(__imm_0, sizeof(struct test_val) + 1)
	: __clobber_all);
}

SEC("tracepoint")
__description("helper access to variable memory: map adjusted, JMP, correct bounds")
__success
__naked void map_adjusted_jmp_correct_bounds(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	r1 += 20;					\
	r2 = %[sizeof_test_val];			\
	*(u64*)(r10 - 128) = r2;			\
	r2 = *(u64*)(r10 - 128);			\
	if r2 s> %[__imm_0] goto l1_%=;			\
	r4 = 0;						\
	if r4 s>= r2 goto l1_%=;			\
	r3 = 0;						\
	call %[bpf_probe_read_kernel];			\
l1_%=:	r0 = 0;						\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm(bpf_probe_read_kernel),
	  __imm_addr(map_hash_48b),
	  __imm_const(__imm_0, sizeof(struct test_val) - 20),
	  __imm_const(sizeof_test_val, sizeof(struct test_val))
	: __clobber_all);
}

SEC("tracepoint")
__description("helper access to variable memory: map adjusted, JMP, wrong max")
__failure __msg("R1 min value is outside of the allowed memory range")
__naked void map_adjusted_jmp_wrong_max(void)
{
	asm volatile ("					\
	r6 = *(u64*)(r1 + 8);				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	r1 += 20;					\
	r2 = r6;					\
	*(u64*)(r10 - 128) = r2;			\
	r2 = *(u64*)(r10 - 128);			\
	if r2 s> %[__imm_0] goto l1_%=;			\
	r4 = 0;						\
	if r4 s>= r2 goto l1_%=;			\
	r3 = 0;						\
	call %[bpf_probe_read_kernel];			\
l1_%=:	r0 = 0;						\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm(bpf_probe_read_kernel),
	  __imm_addr(map_hash_48b),
	  __imm_const(__imm_0, sizeof(struct test_val) - 19)
	: __clobber_all);
}

SEC("tc")
__description("helper access to variable memory: size = 0 allowed on NULL (ARG_PTR_TO_MEM_OR_NULL)")
__success __retval(0)
__naked void ptr_to_mem_or_null_1(void)
{
	asm volatile ("					\
	r1 = 0;						\
	r2 = 0;						\
	r3 = 0;						\
	r4 = 0;						\
	r5 = 0;						\
	call %[bpf_csum_diff];				\
	exit;						\
"	:
	: __imm(bpf_csum_diff)
	: __clobber_all);
}

SEC("tc")
__description("helper access to variable memory: size > 0 not allowed on NULL (ARG_PTR_TO_MEM_OR_NULL)")
__failure __msg("R1 type=scalar expected=fp")
__naked void ptr_to_mem_or_null_2(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + 0);				\
	r1 = 0;						\
	*(u64*)(r10 - 128) = r2;			\
	r2 = *(u64*)(r10 - 128);			\
	r2 &= 64;					\
	r3 = 0;						\
	r4 = 0;						\
	r5 = 0;						\
	call %[bpf_csum_diff];				\
	exit;						\
"	:
	: __imm(bpf_csum_diff)
	: __clobber_all);
}

SEC("tc")
__description("helper access to variable memory: size = 0 allowed on != NULL stack pointer (ARG_PTR_TO_MEM_OR_NULL)")
__success __retval(0)
__naked void ptr_to_mem_or_null_3(void)
{
	asm volatile ("					\
	r1 = r10;					\
	r1 += -8;					\
	r2 = 0;						\
	*(u64*)(r1 + 0) = r2;				\
	r2 &= 8;					\
	r3 = 0;						\
	r4 = 0;						\
	r5 = 0;						\
	call %[bpf_csum_diff];				\
	exit;						\
"	:
	: __imm(bpf_csum_diff)
	: __clobber_all);
}

SEC("tc")
__description("helper access to variable memory: size = 0 allowed on != NULL map pointer (ARG_PTR_TO_MEM_OR_NULL)")
__success __retval(0)
__naked void ptr_to_mem_or_null_4(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	r2 = 0;						\
	r3 = 0;						\
	r4 = 0;						\
	r5 = 0;						\
	call %[bpf_csum_diff];				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_csum_diff),
	  __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("tc")
__description("helper access to variable memory: size possible = 0 allowed on != NULL stack pointer (ARG_PTR_TO_MEM_OR_NULL)")
__success __retval(0)
__naked void ptr_to_mem_or_null_5(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r2 = *(u64*)(r0 + 0);				\
	if r2 > 8 goto l0_%=;				\
	r1 = r10;					\
	r1 += -8;					\
	*(u64*)(r1 + 0) = r2;				\
	r3 = 0;						\
	r4 = 0;						\
	r5 = 0;						\
	call %[bpf_csum_diff];				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_csum_diff),
	  __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("tc")
__description("helper access to variable memory: size possible = 0 allowed on != NULL map pointer (ARG_PTR_TO_MEM_OR_NULL)")
__success __retval(0)
__naked void ptr_to_mem_or_null_6(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	r2 = *(u64*)(r0 + 0);				\
	if r2 > 8 goto l0_%=;				\
	r3 = 0;						\
	r4 = 0;						\
	r5 = 0;						\
	call %[bpf_csum_diff];				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_csum_diff),
	  __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("tc")
__description("helper access to variable memory: size possible = 0 allowed on != NULL packet pointer (ARG_PTR_TO_MEM_OR_NULL)")
__success __retval(0)
/* csum_diff of 64-byte packet */
__flag(BPF_F_ANY_ALIGNMENT)
__naked void ptr_to_mem_or_null_7(void)
{
	asm volatile ("					\
	r6 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r3 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r0 = r6;					\
	r0 += 8;					\
	if r0 > r3 goto l0_%=;				\
	r1 = r6;					\
	r2 = *(u64*)(r6 + 0);				\
	if r2 > 8 goto l0_%=;				\
	r3 = 0;						\
	r4 = 0;						\
	r5 = 0;						\
	call %[bpf_csum_diff];				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_csum_diff),
	  __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tracepoint")
__description("helper access to variable memory: size = 0 not allowed on NULL (!ARG_PTR_TO_MEM_OR_NULL)")
__failure __msg("R1 type=scalar expected=fp")
__naked void ptr_to_mem_or_null_8(void)
{
	asm volatile ("					\
	r1 = 0;						\
	r2 = 0;						\
	r3 = 0;						\
	call %[bpf_probe_read_kernel];			\
	exit;						\
"	:
	: __imm(bpf_probe_read_kernel)
	: __clobber_all);
}

SEC("tracepoint")
__description("helper access to variable memory: size > 0 not allowed on NULL (!ARG_PTR_TO_MEM_OR_NULL)")
__failure __msg("R1 type=scalar expected=fp")
__naked void ptr_to_mem_or_null_9(void)
{
	asm volatile ("					\
	r1 = 0;						\
	r2 = 1;						\
	r3 = 0;						\
	call %[bpf_probe_read_kernel];			\
	exit;						\
"	:
	: __imm(bpf_probe_read_kernel)
	: __clobber_all);
}

SEC("tracepoint")
__description("helper access to variable memory: size = 0 allowed on != NULL stack pointer (!ARG_PTR_TO_MEM_OR_NULL)")
__success
__naked void ptr_to_mem_or_null_10(void)
{
	asm volatile ("					\
	r1 = r10;					\
	r1 += -8;					\
	r2 = 0;						\
	r3 = 0;						\
	call %[bpf_probe_read_kernel];			\
	exit;						\
"	:
	: __imm(bpf_probe_read_kernel)
	: __clobber_all);
}

SEC("tracepoint")
__description("helper access to variable memory: size = 0 allowed on != NULL map pointer (!ARG_PTR_TO_MEM_OR_NULL)")
__success
__naked void ptr_to_mem_or_null_11(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	r2 = 0;						\
	r3 = 0;						\
	call %[bpf_probe_read_kernel];			\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm(bpf_probe_read_kernel),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("tracepoint")
__description("helper access to variable memory: size possible = 0 allowed on != NULL stack pointer (!ARG_PTR_TO_MEM_OR_NULL)")
__success
__naked void ptr_to_mem_or_null_12(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r2 = *(u64*)(r0 + 0);				\
	if r2 > 8 goto l0_%=;				\
	r1 = r10;					\
	r1 += -8;					\
	r3 = 0;						\
	call %[bpf_probe_read_kernel];			\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm(bpf_probe_read_kernel),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("tracepoint")
__description("helper access to variable memory: size possible = 0 allowed on != NULL map pointer (!ARG_PTR_TO_MEM_OR_NULL)")
__success
__naked void ptr_to_mem_or_null_13(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	r2 = *(u64*)(r0 + 0);				\
	if r2 > 8 goto l0_%=;				\
	r3 = 0;						\
	call %[bpf_probe_read_kernel];			\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm(bpf_probe_read_kernel),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("socket")
__description("helper access to variable memory: 8 bytes leak")
/* in privileged mode reads from uninitialized stack locations are permitted */
__success __failure_unpriv
__msg_unpriv("invalid indirect read from stack R2 off -64+32 size 64")
__retval(0)
__naked void variable_memory_8_bytes_leak(void)
{
	asm volatile ("					\
	/* set max stack size */			\
	r6 = 0;						\
	*(u64*)(r10 - 128) = r6;			\
	/* set r3 to a random value */			\
	call %[bpf_get_prandom_u32];			\
	r3 = r0;					\
	r1 = %[map_ringbuf] ll;				\
	r2 = r10;					\
	r2 += -64;					\
	r0 = 0;						\
	*(u64*)(r10 - 64) = r0;				\
	*(u64*)(r10 - 56) = r0;				\
	*(u64*)(r10 - 48) = r0;				\
	*(u64*)(r10 - 40) = r0;				\
	/* Note: fp[-32] left uninitialized */		\
	*(u64*)(r10 - 24) = r0;				\
	*(u64*)(r10 - 16) = r0;				\
	*(u64*)(r10 - 8) = r0;				\
	/* Limit r3 range to [1, 64] */			\
	r3 &= 63;					\
	r3 += 1;					\
	r4 = 0;						\
	/* Call bpf_ringbuf_output(), it is one of a few helper functions with\
	 * ARG_CONST_SIZE_OR_ZERO parameter allowed in unpriv mode.\
	 * For unpriv this should signal an error, because memory region [1, 64]\
	 * at &fp[-64] is not fully initialized.	\
	 */						\
	call %[bpf_ringbuf_output];			\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32),
	  __imm(bpf_ringbuf_output),
	  __imm_addr(map_ringbuf)
	: __clobber_all);
}

SEC("tracepoint")
__description("helper access to variable memory: 8 bytes no leak (init memory)")
__success
__naked void bytes_no_leak_init_memory(void)
{
	asm volatile ("					\
	r1 = r10;					\
	r0 = 0;						\
	r0 = 0;						\
	*(u64*)(r10 - 64) = r0;				\
	*(u64*)(r10 - 56) = r0;				\
	*(u64*)(r10 - 48) = r0;				\
	*(u64*)(r10 - 40) = r0;				\
	*(u64*)(r10 - 32) = r0;				\
	*(u64*)(r10 - 24) = r0;				\
	*(u64*)(r10 - 16) = r0;				\
	*(u64*)(r10 - 8) = r0;				\
	r1 += -64;					\
	r2 = 0;						\
	r2 &= 32;					\
	r2 += 32;					\
	r3 = 0;						\
	call %[bpf_probe_read_kernel];			\
	r1 = *(u64*)(r10 - 16);				\
	exit;						\
"	:
	: __imm(bpf_probe_read_kernel)
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
