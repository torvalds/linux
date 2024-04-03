// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/basic_stack.c */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1);
	__type(key, long long);
	__type(value, long long);
} map_hash_8b SEC(".maps");

SEC("socket")
__description("stack out of bounds")
__failure __msg("invalid write to stack")
__failure_unpriv
__naked void stack_out_of_bounds(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 + 8) = r1;				\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("uninitialized stack1")
__success __log_level(4) __msg("stack depth 8")
__failure_unpriv __msg_unpriv("invalid indirect read from stack")
__naked void uninitialized_stack1(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("socket")
__description("uninitialized stack2")
__success __log_level(4) __msg("stack depth 8")
__failure_unpriv __msg_unpriv("invalid read from stack")
__naked void uninitialized_stack2(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r0 = *(u64*)(r2 - 8);				\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("invalid fp arithmetic")
__failure __msg("R1 subtraction from stack pointer")
__failure_unpriv
__naked void invalid_fp_arithmetic(void)
{
	/* If this gets ever changed, make sure JITs can deal with it. */
	asm volatile ("					\
	r0 = 0;						\
	r1 = r10;					\
	r1 -= 8;					\
	*(u64*)(r1 + 0) = r0;				\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("non-invalid fp arithmetic")
__success __success_unpriv __retval(0)
__naked void non_invalid_fp_arithmetic(void)
{
	asm volatile ("					\
	r0 = 0;						\
	*(u64*)(r10 - 8) = r0;				\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("misaligned read from stack")
__failure __msg("misaligned stack access")
__failure_unpriv
__naked void misaligned_read_from_stack(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r0 = *(u64*)(r2 - 4);				\
	exit;						\
"	::: __clobber_all);
}

char _license[] SEC("license") = "GPL";
