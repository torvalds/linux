// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u32);
} map_array SEC(".maps");

SEC("socket")
__description("invalid map type for tail call")
__failure __msg("expected prog array map for tail call")
__failure_unpriv
__naked void invalid_map_for_tail_call(void)
{
	asm volatile ("			\
	r2 = %[map_array] ll;	\
	r3 = 0;				\
	call %[bpf_tail_call];		\
	exit;				\
"	:
	: __imm(bpf_tail_call),
	  __imm_addr(map_array)
	: __clobber_all);
}

struct {
	__uint(type, BPF_MAP_TYPE_PROG_ARRAY);
	__uint(max_entries, 1);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(__u32));
} jmp_table SEC(".maps");

__used __naked
static int subprog_tail_call(void)
{
	asm volatile ("			\
	r2 = %[jmp_table] ll;		\
	r3 = 0;				\
	call %[bpf_tail_call];		\
	r0 = 0;				\
	exit;				\
"	:
	: __imm(bpf_tail_call),
	  __imm_addr(jmp_table)
	: __clobber_all);
}

/*
 * A tail call unwinds only the frame of the subprog doing it, so the
 * frames of its callers stay on the stack. With up to 33 tail calls in
 * a chain the verifier caps the stack those frames may add up to at
 * 256 bytes.
 */
SEC("tc")
__description("tail call from subprog with 240 bytes of caller stack")
__success
__naked void tail_call_caller_stack_ok(void)
{
	asm volatile ("			\
	r2 = 42;			\
	*(u64 *)(r10 - 240) = r2;	\
	call subprog_tail_call;		\
	r0 = 0;				\
	exit;				\
"	::: __clobber_all);
}

SEC("tc")
__description("tail call from subprog with 256 bytes of caller stack")
__failure
__msg("tail_calls are not allowed when call stack of previous frames is 256 bytes. Too large")
__naked void tail_call_caller_stack_too_large(void)
{
	asm volatile ("			\
	r2 = 42;			\
	*(u64 *)(r10 - 256) = r2;	\
	call subprog_tail_call;		\
	r0 = 0;				\
	exit;				\
"	::: __clobber_all);
}

char _license[] SEC("license") = "GPL";
