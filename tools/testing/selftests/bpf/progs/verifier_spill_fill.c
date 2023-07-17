// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/spill_fill.c */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
	__uint(max_entries, 4096);
} map_ringbuf SEC(".maps");

SEC("socket")
__description("check valid spill/fill")
__success __failure_unpriv __msg_unpriv("R0 leaks addr")
__retval(POINTER_VALUE)
__naked void check_valid_spill_fill(void)
{
	asm volatile ("					\
	/* spill R1(ctx) into stack */			\
	*(u64*)(r10 - 8) = r1;				\
	/* fill it back into R2 */			\
	r2 = *(u64*)(r10 - 8);				\
	/* should be able to access R0 = *(R2 + 8) */	\
	/* BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_2, 8), */\
	r0 = r2;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("check valid spill/fill, skb mark")
__success __success_unpriv __retval(0)
__naked void valid_spill_fill_skb_mark(void)
{
	asm volatile ("					\
	r6 = r1;					\
	*(u64*)(r10 - 8) = r6;				\
	r0 = *(u64*)(r10 - 8);				\
	r0 = *(u32*)(r0 + %[__sk_buff_mark]);		\
	exit;						\
"	:
	: __imm_const(__sk_buff_mark, offsetof(struct __sk_buff, mark))
	: __clobber_all);
}

SEC("socket")
__description("check valid spill/fill, ptr to mem")
__success __success_unpriv __retval(0)
__naked void spill_fill_ptr_to_mem(void)
{
	asm volatile ("					\
	/* reserve 8 byte ringbuf memory */		\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r1 = %[map_ringbuf] ll;				\
	r2 = 8;						\
	r3 = 0;						\
	call %[bpf_ringbuf_reserve];			\
	/* store a pointer to the reserved memory in R6 */\
	r6 = r0;					\
	/* check whether the reservation was successful */\
	if r0 == 0 goto l0_%=;				\
	/* spill R6(mem) into the stack */		\
	*(u64*)(r10 - 8) = r6;				\
	/* fill it back in R7 */			\
	r7 = *(u64*)(r10 - 8);				\
	/* should be able to access *(R7) = 0 */	\
	r1 = 0;						\
	*(u64*)(r7 + 0) = r1;				\
	/* submit the reserved ringbuf memory */	\
	r1 = r7;					\
	r2 = 0;						\
	call %[bpf_ringbuf_submit];			\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_ringbuf_reserve),
	  __imm(bpf_ringbuf_submit),
	  __imm_addr(map_ringbuf)
	: __clobber_all);
}

SEC("socket")
__description("check with invalid reg offset 0")
__failure __msg("R0 pointer arithmetic on ringbuf_mem_or_null prohibited")
__failure_unpriv
__naked void with_invalid_reg_offset_0(void)
{
	asm volatile ("					\
	/* reserve 8 byte ringbuf memory */		\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r1 = %[map_ringbuf] ll;				\
	r2 = 8;						\
	r3 = 0;						\
	call %[bpf_ringbuf_reserve];			\
	/* store a pointer to the reserved memory in R6 */\
	r6 = r0;					\
	/* add invalid offset to memory or NULL */	\
	r0 += 1;					\
	/* check whether the reservation was successful */\
	if r0 == 0 goto l0_%=;				\
	/* should not be able to access *(R7) = 0 */	\
	r1 = 0;						\
	*(u32*)(r6 + 0) = r1;				\
	/* submit the reserved ringbuf memory */	\
	r1 = r6;					\
	r2 = 0;						\
	call %[bpf_ringbuf_submit];			\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_ringbuf_reserve),
	  __imm(bpf_ringbuf_submit),
	  __imm_addr(map_ringbuf)
	: __clobber_all);
}

SEC("socket")
__description("check corrupted spill/fill")
__failure __msg("R0 invalid mem access 'scalar'")
__msg_unpriv("attempt to corrupt spilled")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void check_corrupted_spill_fill(void)
{
	asm volatile ("					\
	/* spill R1(ctx) into stack */			\
	*(u64*)(r10 - 8) = r1;				\
	/* mess up with R1 pointer on stack */		\
	r0 = 0x23;					\
	*(u8*)(r10 - 7) = r0;				\
	/* fill back into R0 is fine for priv.		\
	 * R0 now becomes SCALAR_VALUE.			\
	 */						\
	r0 = *(u64*)(r10 - 8);				\
	/* Load from R0 should fail. */			\
	r0 = *(u64*)(r0 + 8);				\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("check corrupted spill/fill, LSB")
__success __failure_unpriv __msg_unpriv("attempt to corrupt spilled")
__retval(POINTER_VALUE)
__naked void check_corrupted_spill_fill_lsb(void)
{
	asm volatile ("					\
	*(u64*)(r10 - 8) = r1;				\
	r0 = 0xcafe;					\
	*(u16*)(r10 - 8) = r0;				\
	r0 = *(u64*)(r10 - 8);				\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("check corrupted spill/fill, MSB")
__success __failure_unpriv __msg_unpriv("attempt to corrupt spilled")
__retval(POINTER_VALUE)
__naked void check_corrupted_spill_fill_msb(void)
{
	asm volatile ("					\
	*(u64*)(r10 - 8) = r1;				\
	r0 = 0x12345678;				\
	*(u32*)(r10 - 4) = r0;				\
	r0 = *(u64*)(r10 - 8);				\
	exit;						\
"	::: __clobber_all);
}

SEC("tc")
__description("Spill and refill a u32 const scalar.  Offset to skb->data")
__success __retval(0)
__naked void scalar_offset_to_skb_data_1(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r3 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	w4 = 20;					\
	*(u32*)(r10 - 8) = r4;				\
	r4 = *(u32*)(r10 - 8);				\
	r0 = r2;					\
	/* r0 += r4 R0=pkt R2=pkt R3=pkt_end R4=20 */	\
	r0 += r4;					\
	/* if (r0 > r3) R0=pkt,off=20 R2=pkt R3=pkt_end R4=20 */\
	if r0 > r3 goto l0_%=;				\
	/* r0 = *(u32 *)r2 R0=pkt,off=20,r=20 R2=pkt,r=20 R3=pkt_end R4=20 */\
	r0 = *(u32*)(r2 + 0);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("socket")
__description("Spill a u32 const, refill from another half of the uninit u32 from the stack")
/* in privileged mode reads from uninitialized stack locations are permitted */
__success __failure_unpriv
__msg_unpriv("invalid read from stack off -4+0 size 4")
__retval(0)
__naked void uninit_u32_from_the_stack(void)
{
	asm volatile ("					\
	w4 = 20;					\
	*(u32*)(r10 - 8) = r4;				\
	/* r4 = *(u32 *)(r10 -4) fp-8=????rrrr*/	\
	r4 = *(u32*)(r10 - 4);				\
	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

SEC("tc")
__description("Spill a u32 const scalar.  Refill as u16.  Offset to skb->data")
__failure __msg("invalid access to packet")
__naked void u16_offset_to_skb_data(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r3 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	w4 = 20;					\
	*(u32*)(r10 - 8) = r4;				\
	r4 = *(u16*)(r10 - 8);				\
	r0 = r2;					\
	/* r0 += r4 R0=pkt R2=pkt R3=pkt_end R4=umax=65535 */\
	r0 += r4;					\
	/* if (r0 > r3) R0=pkt,umax=65535 R2=pkt R3=pkt_end R4=umax=65535 */\
	if r0 > r3 goto l0_%=;				\
	/* r0 = *(u32 *)r2 R0=pkt,umax=65535 R2=pkt R3=pkt_end R4=20 */\
	r0 = *(u32*)(r2 + 0);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tc")
__description("Spill u32 const scalars.  Refill as u64.  Offset to skb->data")
__failure __msg("invalid access to packet")
__naked void u64_offset_to_skb_data(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r3 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	w6 = 0;						\
	w7 = 20;					\
	*(u32*)(r10 - 4) = r6;				\
	*(u32*)(r10 - 8) = r7;				\
	r4 = *(u16*)(r10 - 8);				\
	r0 = r2;					\
	/* r0 += r4 R0=pkt R2=pkt R3=pkt_end R4=umax=65535 */\
	r0 += r4;					\
	/* if (r0 > r3) R0=pkt,umax=65535 R2=pkt R3=pkt_end R4=umax=65535 */\
	if r0 > r3 goto l0_%=;				\
	/* r0 = *(u32 *)r2 R0=pkt,umax=65535 R2=pkt R3=pkt_end R4=20 */\
	r0 = *(u32*)(r2 + 0);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tc")
__description("Spill a u32 const scalar.  Refill as u16 from fp-6.  Offset to skb->data")
__failure __msg("invalid access to packet")
__naked void _6_offset_to_skb_data(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r3 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	w4 = 20;					\
	*(u32*)(r10 - 8) = r4;				\
	r4 = *(u16*)(r10 - 6);				\
	r0 = r2;					\
	/* r0 += r4 R0=pkt R2=pkt R3=pkt_end R4=umax=65535 */\
	r0 += r4;					\
	/* if (r0 > r3) R0=pkt,umax=65535 R2=pkt R3=pkt_end R4=umax=65535 */\
	if r0 > r3 goto l0_%=;				\
	/* r0 = *(u32 *)r2 R0=pkt,umax=65535 R2=pkt R3=pkt_end R4=20 */\
	r0 = *(u32*)(r2 + 0);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tc")
__description("Spill and refill a u32 const scalar at non 8byte aligned stack addr.  Offset to skb->data")
__failure __msg("invalid access to packet")
__naked void addr_offset_to_skb_data(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r3 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	w4 = 20;					\
	*(u32*)(r10 - 8) = r4;				\
	*(u32*)(r10 - 4) = r4;				\
	r4 = *(u32*)(r10 - 4);				\
	r0 = r2;					\
	/* r0 += r4 R0=pkt R2=pkt R3=pkt_end R4=umax=U32_MAX */\
	r0 += r4;					\
	/* if (r0 > r3) R0=pkt,umax=U32_MAX R2=pkt R3=pkt_end R4= */\
	if r0 > r3 goto l0_%=;				\
	/* r0 = *(u32 *)r2 R0=pkt,umax=U32_MAX R2=pkt R3=pkt_end R4= */\
	r0 = *(u32*)(r2 + 0);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tc")
__description("Spill and refill a umax=40 bounded scalar.  Offset to skb->data")
__success __retval(0)
__naked void scalar_offset_to_skb_data_2(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r3 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r4 = *(u64*)(r1 + %[__sk_buff_tstamp]);		\
	if r4 <= 40 goto l0_%=;				\
	r0 = 0;						\
	exit;						\
l0_%=:	/* *(u32 *)(r10 -8) = r4 R4=umax=40 */		\
	*(u32*)(r10 - 8) = r4;				\
	/* r4 = (*u32 *)(r10 - 8) */			\
	r4 = *(u32*)(r10 - 8);				\
	/* r2 += r4 R2=pkt R4=umax=40 */		\
	r2 += r4;					\
	/* r0 = r2 R2=pkt,umax=40 R4=umax=40 */		\
	r0 = r2;					\
	/* r2 += 20 R0=pkt,umax=40 R2=pkt,umax=40 */	\
	r2 += 20;					\
	/* if (r2 > r3) R0=pkt,umax=40 R2=pkt,off=20,umax=40 */\
	if r2 > r3 goto l1_%=;				\
	/* r0 = *(u32 *)r0 R0=pkt,r=20,umax=40 R2=pkt,off=20,r=20,umax=40 */\
	r0 = *(u32*)(r0 + 0);				\
l1_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end)),
	  __imm_const(__sk_buff_tstamp, offsetof(struct __sk_buff, tstamp))
	: __clobber_all);
}

SEC("tc")
__description("Spill a u32 scalar at fp-4 and then at fp-8")
__success __retval(0)
__naked void and_then_at_fp_8(void)
{
	asm volatile ("					\
	w4 = 4321;					\
	*(u32*)(r10 - 4) = r4;				\
	*(u32*)(r10 - 8) = r4;				\
	r4 = *(u64*)(r10 - 8);				\
	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

SEC("xdp")
__description("32-bit spill of 64-bit reg should clear ID")
__failure __msg("math between ctx pointer and 4294967295 is not allowed")
__naked void spill_32bit_of_64bit_fail(void)
{
	asm volatile ("					\
	r6 = r1;					\
	/* Roll one bit to force the verifier to track both branches. */\
	call %[bpf_get_prandom_u32];			\
	r0 &= 0x8;					\
	/* Put a large number into r1. */		\
	r1 = 0xffffffff;				\
	r1 <<= 32;					\
	r1 += r0;					\
	/* Assign an ID to r1. */			\
	r2 = r1;					\
	/* 32-bit spill r1 to stack - should clear the ID! */\
	*(u32*)(r10 - 8) = r1;				\
	/* 32-bit fill r2 from stack. */		\
	r2 = *(u32*)(r10 - 8);				\
	/* Compare r2 with another register to trigger find_equal_scalars.\
	 * Having one random bit is important here, otherwise the verifier cuts\
	 * the corners. If the ID was mistakenly preserved on spill, this would\
	 * cause the verifier to think that r1 is also equal to zero in one of\
	 * the branches, and equal to eight on the other branch.\
	 */						\
	r3 = 0;						\
	if r2 != r3 goto l0_%=;				\
l0_%=:	r1 >>= 32;					\
	/* At this point, if the verifier thinks that r1 is 0, an out-of-bounds\
	 * read will happen, because it actually contains 0xffffffff.\
	 */						\
	r6 += r1;					\
	r0 = *(u32*)(r6 + 0);				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("xdp")
__description("16-bit spill of 32-bit reg should clear ID")
__failure __msg("dereference of modified ctx ptr R6 off=65535 disallowed")
__naked void spill_16bit_of_32bit_fail(void)
{
	asm volatile ("					\
	r6 = r1;					\
	/* Roll one bit to force the verifier to track both branches. */\
	call %[bpf_get_prandom_u32];			\
	r0 &= 0x8;					\
	/* Put a large number into r1. */		\
	w1 = 0xffff0000;				\
	r1 += r0;					\
	/* Assign an ID to r1. */			\
	r2 = r1;					\
	/* 16-bit spill r1 to stack - should clear the ID! */\
	*(u16*)(r10 - 8) = r1;				\
	/* 16-bit fill r2 from stack. */		\
	r2 = *(u16*)(r10 - 8);				\
	/* Compare r2 with another register to trigger find_equal_scalars.\
	 * Having one random bit is important here, otherwise the verifier cuts\
	 * the corners. If the ID was mistakenly preserved on spill, this would\
	 * cause the verifier to think that r1 is also equal to zero in one of\
	 * the branches, and equal to eight on the other branch.\
	 */						\
	r3 = 0;						\
	if r2 != r3 goto l0_%=;				\
l0_%=:	r1 >>= 16;					\
	/* At this point, if the verifier thinks that r1 is 0, an out-of-bounds\
	 * read will happen, because it actually contains 0xffff.\
	 */						\
	r6 += r1;					\
	r0 = *(u32*)(r6 + 0);				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
