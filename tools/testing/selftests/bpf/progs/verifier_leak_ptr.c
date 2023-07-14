// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/leak_ptr.c */

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
__description("leak pointer into ctx 1")
__failure __msg("BPF_ATOMIC stores into R1 ctx is not allowed")
__failure_unpriv __msg_unpriv("R2 leaks addr into mem")
__naked void leak_pointer_into_ctx_1(void)
{
	asm volatile ("					\
	r0 = 0;						\
	*(u64*)(r1 + %[__sk_buff_cb_0]) = r0;		\
	r2 = %[map_hash_8b] ll;				\
	lock *(u64 *)(r1 + %[__sk_buff_cb_0]) += r2;	\
	exit;						\
"	:
	: __imm_addr(map_hash_8b),
	  __imm_const(__sk_buff_cb_0, offsetof(struct __sk_buff, cb[0]))
	: __clobber_all);
}

SEC("socket")
__description("leak pointer into ctx 2")
__failure __msg("BPF_ATOMIC stores into R1 ctx is not allowed")
__failure_unpriv __msg_unpriv("R10 leaks addr into mem")
__naked void leak_pointer_into_ctx_2(void)
{
	asm volatile ("					\
	r0 = 0;						\
	*(u64*)(r1 + %[__sk_buff_cb_0]) = r0;		\
	lock *(u64 *)(r1 + %[__sk_buff_cb_0]) += r10;	\
	exit;						\
"	:
	: __imm_const(__sk_buff_cb_0, offsetof(struct __sk_buff, cb[0]))
	: __clobber_all);
}

SEC("socket")
__description("leak pointer into ctx 3")
__success __failure_unpriv __msg_unpriv("R2 leaks addr into ctx")
__retval(0)
__naked void leak_pointer_into_ctx_3(void)
{
	asm volatile ("					\
	r0 = 0;						\
	r2 = %[map_hash_8b] ll;				\
	*(u64*)(r1 + %[__sk_buff_cb_0]) = r2;		\
	exit;						\
"	:
	: __imm_addr(map_hash_8b),
	  __imm_const(__sk_buff_cb_0, offsetof(struct __sk_buff, cb[0]))
	: __clobber_all);
}

SEC("socket")
__description("leak pointer into map val")
__success __failure_unpriv __msg_unpriv("R6 leaks addr into mem")
__retval(0)
__naked void leak_pointer_into_map_val(void)
{
	asm volatile ("					\
	r6 = r1;					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r3 = 0;						\
	*(u64*)(r0 + 0) = r3;				\
	lock *(u64 *)(r0 + 0) += r6;			\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
