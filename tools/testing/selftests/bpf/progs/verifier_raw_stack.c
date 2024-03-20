// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/raw_stack.c */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

SEC("socket")
__description("raw_stack: no skb_load_bytes")
__success
__failure_unpriv __msg_unpriv("invalid read from stack R6 off=-8 size=8")
__naked void stack_no_skb_load_bytes(void)
{
	asm volatile ("					\
	r2 = 4;						\
	r6 = r10;					\
	r6 += -8;					\
	r3 = r6;					\
	r4 = 8;						\
	/* Call to skb_load_bytes() omitted. */		\
	r0 = *(u64*)(r6 + 0);				\
	exit;						\
"	::: __clobber_all);
}

SEC("tc")
__description("raw_stack: skb_load_bytes, negative len")
__failure __msg("R4 min value is negative")
__naked void skb_load_bytes_negative_len(void)
{
	asm volatile ("					\
	r2 = 4;						\
	r6 = r10;					\
	r6 += -8;					\
	r3 = r6;					\
	r4 = -8;					\
	call %[bpf_skb_load_bytes];			\
	r0 = *(u64*)(r6 + 0);				\
	exit;						\
"	:
	: __imm(bpf_skb_load_bytes)
	: __clobber_all);
}

SEC("tc")
__description("raw_stack: skb_load_bytes, negative len 2")
__failure __msg("R4 min value is negative")
__naked void load_bytes_negative_len_2(void)
{
	asm volatile ("					\
	r2 = 4;						\
	r6 = r10;					\
	r6 += -8;					\
	r3 = r6;					\
	r4 = %[__imm_0];				\
	call %[bpf_skb_load_bytes];			\
	r0 = *(u64*)(r6 + 0);				\
	exit;						\
"	:
	: __imm(bpf_skb_load_bytes),
	  __imm_const(__imm_0, ~0)
	: __clobber_all);
}

SEC("tc")
__description("raw_stack: skb_load_bytes, zero len")
__failure __msg("invalid zero-sized read")
__naked void skb_load_bytes_zero_len(void)
{
	asm volatile ("					\
	r2 = 4;						\
	r6 = r10;					\
	r6 += -8;					\
	r3 = r6;					\
	r4 = 0;						\
	call %[bpf_skb_load_bytes];			\
	r0 = *(u64*)(r6 + 0);				\
	exit;						\
"	:
	: __imm(bpf_skb_load_bytes)
	: __clobber_all);
}

SEC("tc")
__description("raw_stack: skb_load_bytes, no init")
__success __retval(0)
__naked void skb_load_bytes_no_init(void)
{
	asm volatile ("					\
	r2 = 4;						\
	r6 = r10;					\
	r6 += -8;					\
	r3 = r6;					\
	r4 = 8;						\
	call %[bpf_skb_load_bytes];			\
	r0 = *(u64*)(r6 + 0);				\
	exit;						\
"	:
	: __imm(bpf_skb_load_bytes)
	: __clobber_all);
}

SEC("tc")
__description("raw_stack: skb_load_bytes, init")
__success __retval(0)
__naked void stack_skb_load_bytes_init(void)
{
	asm volatile ("					\
	r2 = 4;						\
	r6 = r10;					\
	r6 += -8;					\
	r3 = 0xcafe;					\
	*(u64*)(r6 + 0) = r3;				\
	r3 = r6;					\
	r4 = 8;						\
	call %[bpf_skb_load_bytes];			\
	r0 = *(u64*)(r6 + 0);				\
	exit;						\
"	:
	: __imm(bpf_skb_load_bytes)
	: __clobber_all);
}

SEC("tc")
__description("raw_stack: skb_load_bytes, spilled regs around bounds")
__success __retval(0)
__naked void bytes_spilled_regs_around_bounds(void)
{
	asm volatile ("					\
	r2 = 4;						\
	r6 = r10;					\
	r6 += -16;					\
	*(u64*)(r6 - 8) = r1;				\
	*(u64*)(r6 + 8) = r1;				\
	r3 = r6;					\
	r4 = 8;						\
	call %[bpf_skb_load_bytes];			\
	r0 = *(u64*)(r6 - 8);				\
	r2 = *(u64*)(r6 + 8);				\
	r0 = *(u32*)(r0 + %[__sk_buff_mark]);		\
	r2 = *(u32*)(r2 + %[__sk_buff_priority]);	\
	r0 += r2;					\
	exit;						\
"	:
	: __imm(bpf_skb_load_bytes),
	  __imm_const(__sk_buff_mark, offsetof(struct __sk_buff, mark)),
	  __imm_const(__sk_buff_priority, offsetof(struct __sk_buff, priority))
	: __clobber_all);
}

SEC("tc")
__description("raw_stack: skb_load_bytes, spilled regs corruption")
__failure __msg("R0 invalid mem access 'scalar'")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void load_bytes_spilled_regs_corruption(void)
{
	asm volatile ("					\
	r2 = 4;						\
	r6 = r10;					\
	r6 += -8;					\
	*(u64*)(r6 + 0) = r1;				\
	r3 = r6;					\
	r4 = 8;						\
	call %[bpf_skb_load_bytes];			\
	r0 = *(u64*)(r6 + 0);				\
	r0 = *(u32*)(r0 + %[__sk_buff_mark]);		\
	exit;						\
"	:
	: __imm(bpf_skb_load_bytes),
	  __imm_const(__sk_buff_mark, offsetof(struct __sk_buff, mark))
	: __clobber_all);
}

SEC("tc")
__description("raw_stack: skb_load_bytes, spilled regs corruption 2")
__failure __msg("R3 invalid mem access 'scalar'")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void bytes_spilled_regs_corruption_2(void)
{
	asm volatile ("					\
	r2 = 4;						\
	r6 = r10;					\
	r6 += -16;					\
	*(u64*)(r6 - 8) = r1;				\
	*(u64*)(r6 + 0) = r1;				\
	*(u64*)(r6 + 8) = r1;				\
	r3 = r6;					\
	r4 = 8;						\
	call %[bpf_skb_load_bytes];			\
	r0 = *(u64*)(r6 - 8);				\
	r2 = *(u64*)(r6 + 8);				\
	r3 = *(u64*)(r6 + 0);				\
	r0 = *(u32*)(r0 + %[__sk_buff_mark]);		\
	r2 = *(u32*)(r2 + %[__sk_buff_priority]);	\
	r0 += r2;					\
	r3 = *(u32*)(r3 + %[__sk_buff_pkt_type]);	\
	r0 += r3;					\
	exit;						\
"	:
	: __imm(bpf_skb_load_bytes),
	  __imm_const(__sk_buff_mark, offsetof(struct __sk_buff, mark)),
	  __imm_const(__sk_buff_pkt_type, offsetof(struct __sk_buff, pkt_type)),
	  __imm_const(__sk_buff_priority, offsetof(struct __sk_buff, priority))
	: __clobber_all);
}

SEC("tc")
__description("raw_stack: skb_load_bytes, spilled regs + data")
__success __retval(0)
__naked void load_bytes_spilled_regs_data(void)
{
	asm volatile ("					\
	r2 = 4;						\
	r6 = r10;					\
	r6 += -16;					\
	*(u64*)(r6 - 8) = r1;				\
	*(u64*)(r6 + 0) = r1;				\
	*(u64*)(r6 + 8) = r1;				\
	r3 = r6;					\
	r4 = 8;						\
	call %[bpf_skb_load_bytes];			\
	r0 = *(u64*)(r6 - 8);				\
	r2 = *(u64*)(r6 + 8);				\
	r3 = *(u64*)(r6 + 0);				\
	r0 = *(u32*)(r0 + %[__sk_buff_mark]);		\
	r2 = *(u32*)(r2 + %[__sk_buff_priority]);	\
	r0 += r2;					\
	r0 += r3;					\
	exit;						\
"	:
	: __imm(bpf_skb_load_bytes),
	  __imm_const(__sk_buff_mark, offsetof(struct __sk_buff, mark)),
	  __imm_const(__sk_buff_priority, offsetof(struct __sk_buff, priority))
	: __clobber_all);
}

SEC("tc")
__description("raw_stack: skb_load_bytes, invalid access 1")
__failure __msg("invalid indirect access to stack R3 off=-513 size=8")
__naked void load_bytes_invalid_access_1(void)
{
	asm volatile ("					\
	r2 = 4;						\
	r6 = r10;					\
	r6 += -513;					\
	r3 = r6;					\
	r4 = 8;						\
	call %[bpf_skb_load_bytes];			\
	r0 = *(u64*)(r6 + 0);				\
	exit;						\
"	:
	: __imm(bpf_skb_load_bytes)
	: __clobber_all);
}

SEC("tc")
__description("raw_stack: skb_load_bytes, invalid access 2")
__failure __msg("invalid indirect access to stack R3 off=-1 size=8")
__naked void load_bytes_invalid_access_2(void)
{
	asm volatile ("					\
	r2 = 4;						\
	r6 = r10;					\
	r6 += -1;					\
	r3 = r6;					\
	r4 = 8;						\
	call %[bpf_skb_load_bytes];			\
	r0 = *(u64*)(r6 + 0);				\
	exit;						\
"	:
	: __imm(bpf_skb_load_bytes)
	: __clobber_all);
}

SEC("tc")
__description("raw_stack: skb_load_bytes, invalid access 3")
__failure __msg("R4 min value is negative")
__naked void load_bytes_invalid_access_3(void)
{
	asm volatile ("					\
	r2 = 4;						\
	r6 = r10;					\
	r6 += 0xffffffff;				\
	r3 = r6;					\
	r4 = 0xffffffff;				\
	call %[bpf_skb_load_bytes];			\
	r0 = *(u64*)(r6 + 0);				\
	exit;						\
"	:
	: __imm(bpf_skb_load_bytes)
	: __clobber_all);
}

SEC("tc")
__description("raw_stack: skb_load_bytes, invalid access 4")
__failure
__msg("R4 unbounded memory access, use 'var &= const' or 'if (var < const)'")
__naked void load_bytes_invalid_access_4(void)
{
	asm volatile ("					\
	r2 = 4;						\
	r6 = r10;					\
	r6 += -1;					\
	r3 = r6;					\
	r4 = 0x7fffffff;				\
	call %[bpf_skb_load_bytes];			\
	r0 = *(u64*)(r6 + 0);				\
	exit;						\
"	:
	: __imm(bpf_skb_load_bytes)
	: __clobber_all);
}

SEC("tc")
__description("raw_stack: skb_load_bytes, invalid access 5")
__failure
__msg("R4 unbounded memory access, use 'var &= const' or 'if (var < const)'")
__naked void load_bytes_invalid_access_5(void)
{
	asm volatile ("					\
	r2 = 4;						\
	r6 = r10;					\
	r6 += -512;					\
	r3 = r6;					\
	r4 = 0x7fffffff;				\
	call %[bpf_skb_load_bytes];			\
	r0 = *(u64*)(r6 + 0);				\
	exit;						\
"	:
	: __imm(bpf_skb_load_bytes)
	: __clobber_all);
}

SEC("tc")
__description("raw_stack: skb_load_bytes, invalid access 6")
__failure __msg("invalid zero-sized read")
__naked void load_bytes_invalid_access_6(void)
{
	asm volatile ("					\
	r2 = 4;						\
	r6 = r10;					\
	r6 += -512;					\
	r3 = r6;					\
	r4 = 0;						\
	call %[bpf_skb_load_bytes];			\
	r0 = *(u64*)(r6 + 0);				\
	exit;						\
"	:
	: __imm(bpf_skb_load_bytes)
	: __clobber_all);
}

SEC("tc")
__description("raw_stack: skb_load_bytes, large access")
__success __retval(0)
__naked void skb_load_bytes_large_access(void)
{
	asm volatile ("					\
	r2 = 4;						\
	r6 = r10;					\
	r6 += -512;					\
	r3 = r6;					\
	r4 = 512;					\
	call %[bpf_skb_load_bytes];			\
	r0 = *(u64*)(r6 + 0);				\
	exit;						\
"	:
	: __imm(bpf_skb_load_bytes)
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
