// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/spill_fill.c */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"
#include <../../../tools/include/linux/filter.h>

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
__success __retval(0)
__naked void u16_offset_to_skb_data(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r3 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	w4 = 20;					\
	*(u32*)(r10 - 8) = r4;				\
	"
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	"r4 = *(u16*)(r10 - 8);"
#else
	"r4 = *(u16*)(r10 - 6);"
#endif
	"						\
	r0 = r2;					\
	/* r0 += r4 R0=pkt R2=pkt R3=pkt_end R4=20 */\
	r0 += r4;					\
	/* if (r0 > r3) R0=pkt,off=20 R2=pkt R3=pkt_end R4=20 */\
	if r0 > r3 goto l0_%=;				\
	/* r0 = *(u32 *)r2 R0=pkt,off=20 R2=pkt R3=pkt_end R4=20 */\
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
__failure __msg("math between pkt pointer and register with unbounded min value is not allowed")
__naked void u64_offset_to_skb_data(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r3 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	w6 = 0;						\
	w7 = 20;					\
	*(u32*)(r10 - 4) = r6;				\
	*(u32*)(r10 - 8) = r7;				\
	r4 = *(u64*)(r10 - 8);				\
	r0 = r2;					\
	/* r0 += r4 R0=pkt R2=pkt R3=pkt_end R4= */	\
	r0 += r4;					\
	if r0 > r3 goto l0_%=;				\
	r0 = *(u32*)(r2 + 0);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tc")
__description("Spill a u32 const scalar.  Refill as u16 from MSB.  Offset to skb->data")
__failure __msg("invalid access to packet")
__naked void _6_offset_to_skb_data(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r3 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	w4 = 20;					\
	*(u32*)(r10 - 8) = r4;				\
	"
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	"r4 = *(u16*)(r10 - 6);"
#else
	"r4 = *(u16*)(r10 - 8);"
#endif
	"						\
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
	/* Compare r2 with another register to trigger sync_linked_regs.\
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
	/* Compare r2 with another register to trigger sync_linked_regs.\
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

SEC("raw_tp")
__log_level(2)
__success
__msg("fp-8=0m??scalar()")
__msg("fp-16=00mm??scalar()")
__msg("fp-24=00mm???scalar()")
__naked void spill_subregs_preserve_stack_zero(void)
{
	asm volatile (
		"call %[bpf_get_prandom_u32];"

		/* 32-bit subreg spill with ZERO, MISC, and INVALID */
		".8byte %[fp1_u8_st_zero];"   /* ZERO, LLVM-18+: *(u8 *)(r10 -1) = 0; */
		"*(u8 *)(r10 -2) = r0;"       /* MISC */
		/* fp-3 and fp-4 stay INVALID */
		"*(u32 *)(r10 -8) = r0;"

		/* 16-bit subreg spill with ZERO, MISC, and INVALID */
		".8byte %[fp10_u16_st_zero];" /* ZERO, LLVM-18+: *(u16 *)(r10 -10) = 0; */
		"*(u16 *)(r10 -12) = r0;"     /* MISC */
		/* fp-13 and fp-14 stay INVALID */
		"*(u16 *)(r10 -16) = r0;"

		/* 8-bit subreg spill with ZERO, MISC, and INVALID */
		".8byte %[fp18_u16_st_zero];" /* ZERO, LLVM-18+: *(u16 *)(r18 -10) = 0; */
		"*(u16 *)(r10 -20) = r0;"     /* MISC */
		/* fp-21, fp-22, and fp-23 stay INVALID */
		"*(u8 *)(r10 -24) = r0;"

		"r0 = 0;"
		"exit;"
	:
	: __imm(bpf_get_prandom_u32),
	  __imm_insn(fp1_u8_st_zero, BPF_ST_MEM(BPF_B, BPF_REG_FP, -1, 0)),
	  __imm_insn(fp10_u16_st_zero, BPF_ST_MEM(BPF_H, BPF_REG_FP, -10, 0)),
	  __imm_insn(fp18_u16_st_zero, BPF_ST_MEM(BPF_H, BPF_REG_FP, -18, 0))
	: __clobber_all);
}

char single_byte_buf[1] SEC(".data.single_byte_buf");

SEC("raw_tp")
__log_level(2)
__success
/* fp-8 is spilled IMPRECISE value zero (represented by a zero value fake reg) */
__msg("2: (7a) *(u64 *)(r10 -8) = 0          ; R10=fp0 fp-8_w=0")
/* but fp-16 is spilled IMPRECISE zero const reg */
__msg("4: (7b) *(u64 *)(r10 -16) = r0        ; R0_w=0 R10=fp0 fp-16_w=0")
/* validate that assigning R2 from STACK_SPILL with zero value  doesn't mark register
 * precise immediately; if necessary, it will be marked precise later
 */
__msg("6: (71) r2 = *(u8 *)(r10 -1)          ; R2_w=0 R10=fp0 fp-8_w=0")
/* similarly, when R2 is assigned from spilled register, it is initially
 * imprecise, but will be marked precise later once it is used in precise context
 */
__msg("10: (71) r2 = *(u8 *)(r10 -9)         ; R2_w=0 R10=fp0 fp-16_w=0")
__msg("11: (0f) r1 += r2")
__msg("mark_precise: frame0: last_idx 11 first_idx 0 subseq_idx -1")
__msg("mark_precise: frame0: regs=r2 stack= before 10: (71) r2 = *(u8 *)(r10 -9)")
__msg("mark_precise: frame0: regs= stack=-16 before 9: (bf) r1 = r6")
__msg("mark_precise: frame0: regs= stack=-16 before 8: (73) *(u8 *)(r1 +0) = r2")
__msg("mark_precise: frame0: regs= stack=-16 before 7: (0f) r1 += r2")
__msg("mark_precise: frame0: regs= stack=-16 before 6: (71) r2 = *(u8 *)(r10 -1)")
__msg("mark_precise: frame0: regs= stack=-16 before 5: (bf) r1 = r6")
__msg("mark_precise: frame0: regs= stack=-16 before 4: (7b) *(u64 *)(r10 -16) = r0")
__msg("mark_precise: frame0: regs=r0 stack= before 3: (b7) r0 = 0")
__naked void partial_stack_load_preserves_zeros(void)
{
	asm volatile (
		/* fp-8 is value zero (represented by a zero value fake reg) */
		".8byte %[fp8_st_zero];" /* LLVM-18+: *(u64 *)(r10 -8) = 0; */

		/* fp-16 is const zero register */
		"r0 = 0;"
		"*(u64 *)(r10 -16) = r0;"

		/* load single U8 from non-aligned spilled value zero slot */
		"r1 = %[single_byte_buf];"
		"r2 = *(u8 *)(r10 -1);"
		"r1 += r2;"
		"*(u8 *)(r1 + 0) = r2;" /* this should be fine */

		/* load single U8 from non-aligned ZERO REG slot */
		"r1 = %[single_byte_buf];"
		"r2 = *(u8 *)(r10 -9);"
		"r1 += r2;"
		"*(u8 *)(r1 + 0) = r2;" /* this should be fine */

		/* load single U16 from non-aligned spilled value zero slot */
		"r1 = %[single_byte_buf];"
		"r2 = *(u16 *)(r10 -2);"
		"r1 += r2;"
		"*(u8 *)(r1 + 0) = r2;" /* this should be fine */

		/* load single U16 from non-aligned ZERO REG slot */
		"r1 = %[single_byte_buf];"
		"r2 = *(u16 *)(r10 -10);"
		"r1 += r2;"
		"*(u8 *)(r1 + 0) = r2;" /* this should be fine */

		/* load single U32 from non-aligned spilled value zero slot */
		"r1 = %[single_byte_buf];"
		"r2 = *(u32 *)(r10 -4);"
		"r1 += r2;"
		"*(u8 *)(r1 + 0) = r2;" /* this should be fine */

		/* load single U32 from non-aligned ZERO REG slot */
		"r1 = %[single_byte_buf];"
		"r2 = *(u32 *)(r10 -12);"
		"r1 += r2;"
		"*(u8 *)(r1 + 0) = r2;" /* this should be fine */

		/* for completeness, load U64 from STACK_ZERO slot */
		"r1 = %[single_byte_buf];"
		"r2 = *(u64 *)(r10 -8);"
		"r1 += r2;"
		"*(u8 *)(r1 + 0) = r2;" /* this should be fine */

		/* for completeness, load U64 from ZERO REG slot */
		"r1 = %[single_byte_buf];"
		"r2 = *(u64 *)(r10 -16);"
		"r1 += r2;"
		"*(u8 *)(r1 + 0) = r2;" /* this should be fine */

		"r0 = 0;"
		"exit;"
	:
	: __imm_ptr(single_byte_buf),
	  __imm_insn(fp8_st_zero, BPF_ST_MEM(BPF_DW, BPF_REG_FP, -8, 0))
	: __clobber_common);
}

SEC("raw_tp")
__log_level(2)
__success
/* fp-4 is STACK_ZERO */
__msg("2: (62) *(u32 *)(r10 -4) = 0          ; R10=fp0 fp-8=0000????")
__msg("4: (71) r2 = *(u8 *)(r10 -1)          ; R2_w=0 R10=fp0 fp-8=0000????")
__msg("5: (0f) r1 += r2")
__msg("mark_precise: frame0: last_idx 5 first_idx 0 subseq_idx -1")
__msg("mark_precise: frame0: regs=r2 stack= before 4: (71) r2 = *(u8 *)(r10 -1)")
__naked void partial_stack_load_preserves_partial_zeros(void)
{
	asm volatile (
		/* fp-4 is value zero */
		".8byte %[fp4_st_zero];" /* LLVM-18+: *(u32 *)(r10 -4) = 0; */

		/* load single U8 from non-aligned stack zero slot */
		"r1 = %[single_byte_buf];"
		"r2 = *(u8 *)(r10 -1);"
		"r1 += r2;"
		"*(u8 *)(r1 + 0) = r2;" /* this should be fine */

		/* load single U16 from non-aligned stack zero slot */
		"r1 = %[single_byte_buf];"
		"r2 = *(u16 *)(r10 -2);"
		"r1 += r2;"
		"*(u8 *)(r1 + 0) = r2;" /* this should be fine */

		/* load single U32 from non-aligned stack zero slot */
		"r1 = %[single_byte_buf];"
		"r2 = *(u32 *)(r10 -4);"
		"r1 += r2;"
		"*(u8 *)(r1 + 0) = r2;" /* this should be fine */

		"r0 = 0;"
		"exit;"
	:
	: __imm_ptr(single_byte_buf),
	  __imm_insn(fp4_st_zero, BPF_ST_MEM(BPF_W, BPF_REG_FP, -4, 0))
	: __clobber_common);
}

char two_byte_buf[2] SEC(".data.two_byte_buf");

SEC("raw_tp")
__log_level(2) __flag(BPF_F_TEST_STATE_FREQ)
__success
/* make sure fp-8 is IMPRECISE fake register spill */
__msg("3: (7a) *(u64 *)(r10 -8) = 1          ; R10=fp0 fp-8_w=1")
/* and fp-16 is spilled IMPRECISE const reg */
__msg("5: (7b) *(u64 *)(r10 -16) = r0        ; R0_w=1 R10=fp0 fp-16_w=1")
/* validate load from fp-8, which was initialized using BPF_ST_MEM */
__msg("8: (79) r2 = *(u64 *)(r10 -8)         ; R2_w=1 R10=fp0 fp-8=1")
__msg("9: (0f) r1 += r2")
__msg("mark_precise: frame0: last_idx 9 first_idx 7 subseq_idx -1")
__msg("mark_precise: frame0: regs=r2 stack= before 8: (79) r2 = *(u64 *)(r10 -8)")
__msg("mark_precise: frame0: regs= stack=-8 before 7: (bf) r1 = r6")
/* note, fp-8 is precise, fp-16 is not yet precise, we'll get there */
__msg("mark_precise: frame0: parent state regs= stack=-8:  R0_w=1 R1=ctx() R6_r=map_value(map=.data.two_byte_,ks=4,vs=2) R10=fp0 fp-8_rw=P1 fp-16_w=1")
__msg("mark_precise: frame0: last_idx 6 first_idx 3 subseq_idx 7")
__msg("mark_precise: frame0: regs= stack=-8 before 6: (05) goto pc+0")
__msg("mark_precise: frame0: regs= stack=-8 before 5: (7b) *(u64 *)(r10 -16) = r0")
__msg("mark_precise: frame0: regs= stack=-8 before 4: (b7) r0 = 1")
__msg("mark_precise: frame0: regs= stack=-8 before 3: (7a) *(u64 *)(r10 -8) = 1")
__msg("10: R1_w=map_value(map=.data.two_byte_,ks=4,vs=2,off=1) R2_w=1")
/* validate load from fp-16, which was initialized using BPF_STX_MEM */
__msg("12: (79) r2 = *(u64 *)(r10 -16)       ; R2_w=1 R10=fp0 fp-16=1")
__msg("13: (0f) r1 += r2")
__msg("mark_precise: frame0: last_idx 13 first_idx 7 subseq_idx -1")
__msg("mark_precise: frame0: regs=r2 stack= before 12: (79) r2 = *(u64 *)(r10 -16)")
__msg("mark_precise: frame0: regs= stack=-16 before 11: (bf) r1 = r6")
__msg("mark_precise: frame0: regs= stack=-16 before 10: (73) *(u8 *)(r1 +0) = r2")
__msg("mark_precise: frame0: regs= stack=-16 before 9: (0f) r1 += r2")
__msg("mark_precise: frame0: regs= stack=-16 before 8: (79) r2 = *(u64 *)(r10 -8)")
__msg("mark_precise: frame0: regs= stack=-16 before 7: (bf) r1 = r6")
/* now both fp-8 and fp-16 are precise, very good */
__msg("mark_precise: frame0: parent state regs= stack=-16:  R0_w=1 R1=ctx() R6_r=map_value(map=.data.two_byte_,ks=4,vs=2) R10=fp0 fp-8_rw=P1 fp-16_rw=P1")
__msg("mark_precise: frame0: last_idx 6 first_idx 3 subseq_idx 7")
__msg("mark_precise: frame0: regs= stack=-16 before 6: (05) goto pc+0")
__msg("mark_precise: frame0: regs= stack=-16 before 5: (7b) *(u64 *)(r10 -16) = r0")
__msg("mark_precise: frame0: regs=r0 stack= before 4: (b7) r0 = 1")
__msg("14: R1_w=map_value(map=.data.two_byte_,ks=4,vs=2,off=1) R2_w=1")
__naked void stack_load_preserves_const_precision(void)
{
	asm volatile (
		/* establish checkpoint with state that has no stack slots;
		 * if we bubble up to this state without finding desired stack
		 * slot, then it's a bug and should be caught
		 */
		"goto +0;"

		/* fp-8 is const 1 *fake* register */
		".8byte %[fp8_st_one];" /* LLVM-18+: *(u64 *)(r10 -8) = 1; */

		/* fp-16 is const 1 register */
		"r0 = 1;"
		"*(u64 *)(r10 -16) = r0;"

		/* force checkpoint to check precision marks preserved in parent states */
		"goto +0;"

		/* load single U64 from aligned FAKE_REG=1 slot */
		"r1 = %[two_byte_buf];"
		"r2 = *(u64 *)(r10 -8);"
		"r1 += r2;"
		"*(u8 *)(r1 + 0) = r2;" /* this should be fine */

		/* load single U64 from aligned REG=1 slot */
		"r1 = %[two_byte_buf];"
		"r2 = *(u64 *)(r10 -16);"
		"r1 += r2;"
		"*(u8 *)(r1 + 0) = r2;" /* this should be fine */

		"r0 = 0;"
		"exit;"
	:
	: __imm_ptr(two_byte_buf),
	  __imm_insn(fp8_st_one, BPF_ST_MEM(BPF_DW, BPF_REG_FP, -8, 1))
	: __clobber_common);
}

SEC("raw_tp")
__log_level(2) __flag(BPF_F_TEST_STATE_FREQ)
__success
/* make sure fp-8 is 32-bit FAKE subregister spill */
__msg("3: (62) *(u32 *)(r10 -8) = 1          ; R10=fp0 fp-8=????1")
/* but fp-16 is spilled IMPRECISE zero const reg */
__msg("5: (63) *(u32 *)(r10 -16) = r0        ; R0_w=1 R10=fp0 fp-16=????1")
/* validate load from fp-8, which was initialized using BPF_ST_MEM */
__msg("8: (61) r2 = *(u32 *)(r10 -8)         ; R2_w=1 R10=fp0 fp-8=????1")
__msg("9: (0f) r1 += r2")
__msg("mark_precise: frame0: last_idx 9 first_idx 7 subseq_idx -1")
__msg("mark_precise: frame0: regs=r2 stack= before 8: (61) r2 = *(u32 *)(r10 -8)")
__msg("mark_precise: frame0: regs= stack=-8 before 7: (bf) r1 = r6")
__msg("mark_precise: frame0: parent state regs= stack=-8:  R0_w=1 R1=ctx() R6_r=map_value(map=.data.two_byte_,ks=4,vs=2) R10=fp0 fp-8_r=????P1 fp-16=????1")
__msg("mark_precise: frame0: last_idx 6 first_idx 3 subseq_idx 7")
__msg("mark_precise: frame0: regs= stack=-8 before 6: (05) goto pc+0")
__msg("mark_precise: frame0: regs= stack=-8 before 5: (63) *(u32 *)(r10 -16) = r0")
__msg("mark_precise: frame0: regs= stack=-8 before 4: (b7) r0 = 1")
__msg("mark_precise: frame0: regs= stack=-8 before 3: (62) *(u32 *)(r10 -8) = 1")
__msg("10: R1_w=map_value(map=.data.two_byte_,ks=4,vs=2,off=1) R2_w=1")
/* validate load from fp-16, which was initialized using BPF_STX_MEM */
__msg("12: (61) r2 = *(u32 *)(r10 -16)       ; R2_w=1 R10=fp0 fp-16=????1")
__msg("13: (0f) r1 += r2")
__msg("mark_precise: frame0: last_idx 13 first_idx 7 subseq_idx -1")
__msg("mark_precise: frame0: regs=r2 stack= before 12: (61) r2 = *(u32 *)(r10 -16)")
__msg("mark_precise: frame0: regs= stack=-16 before 11: (bf) r1 = r6")
__msg("mark_precise: frame0: regs= stack=-16 before 10: (73) *(u8 *)(r1 +0) = r2")
__msg("mark_precise: frame0: regs= stack=-16 before 9: (0f) r1 += r2")
__msg("mark_precise: frame0: regs= stack=-16 before 8: (61) r2 = *(u32 *)(r10 -8)")
__msg("mark_precise: frame0: regs= stack=-16 before 7: (bf) r1 = r6")
__msg("mark_precise: frame0: parent state regs= stack=-16:  R0_w=1 R1=ctx() R6_r=map_value(map=.data.two_byte_,ks=4,vs=2) R10=fp0 fp-8_r=????P1 fp-16_r=????P1")
__msg("mark_precise: frame0: last_idx 6 first_idx 3 subseq_idx 7")
__msg("mark_precise: frame0: regs= stack=-16 before 6: (05) goto pc+0")
__msg("mark_precise: frame0: regs= stack=-16 before 5: (63) *(u32 *)(r10 -16) = r0")
__msg("mark_precise: frame0: regs=r0 stack= before 4: (b7) r0 = 1")
__msg("14: R1_w=map_value(map=.data.two_byte_,ks=4,vs=2,off=1) R2_w=1")
__naked void stack_load_preserves_const_precision_subreg(void)
{
	asm volatile (
		/* establish checkpoint with state that has no stack slots;
		 * if we bubble up to this state without finding desired stack
		 * slot, then it's a bug and should be caught
		 */
		"goto +0;"

		/* fp-8 is const 1 *fake* SUB-register */
		".8byte %[fp8_st_one];" /* LLVM-18+: *(u32 *)(r10 -8) = 1; */

		/* fp-16 is const 1 SUB-register */
		"r0 = 1;"
		"*(u32 *)(r10 -16) = r0;"

		/* force checkpoint to check precision marks preserved in parent states */
		"goto +0;"

		/* load single U32 from aligned FAKE_REG=1 slot */
		"r1 = %[two_byte_buf];"
		"r2 = *(u32 *)(r10 -8);"
		"r1 += r2;"
		"*(u8 *)(r1 + 0) = r2;" /* this should be fine */

		/* load single U32 from aligned REG=1 slot */
		"r1 = %[two_byte_buf];"
		"r2 = *(u32 *)(r10 -16);"
		"r1 += r2;"
		"*(u8 *)(r1 + 0) = r2;" /* this should be fine */

		"r0 = 0;"
		"exit;"
	:
	: __imm_ptr(two_byte_buf),
	  __imm_insn(fp8_st_one, BPF_ST_MEM(BPF_W, BPF_REG_FP, -8, 1)) /* 32-bit spill */
	: __clobber_common);
}

SEC("xdp")
__description("32-bit spilled reg range should be tracked")
__success __retval(0)
__naked void spill_32bit_range_track(void)
{
	asm volatile("					\
	call %[bpf_ktime_get_ns];			\
	/* Make r0 bounded. */				\
	r0 &= 65535;					\
	/* Assign an ID to r0. */			\
	r1 = r0;					\
	/* 32-bit spill r0 to stack. */			\
	*(u32*)(r10 - 8) = r0;				\
	/* Boundary check on r0. */			\
	if r0 < 1 goto l0_%=;				\
	/* 32-bit fill r1 from stack. */		\
	r1 = *(u32*)(r10 - 8);				\
	/* r1 == r0 => r1 >= 1 always. */		\
	if r1 >= 1 goto l0_%=;				\
	/* Dead branch: the verifier should prune it.   \
	 * Do an invalid memory access if the verifier	\
	 * follows it.					\
	 */						\
	r0 = *(u64*)(r9 + 0);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

SEC("xdp")
__description("64-bit spill of 64-bit reg should assign ID")
__success __retval(0)
__naked void spill_64bit_of_64bit_ok(void)
{
	asm volatile ("					\
	/* Roll one bit to make the register inexact. */\
	call %[bpf_get_prandom_u32];			\
	r0 &= 0x80000000;				\
	r0 <<= 32;					\
	/* 64-bit spill r0 to stack - should assign an ID. */\
	*(u64*)(r10 - 8) = r0;				\
	/* 64-bit fill r1 from stack - should preserve the ID. */\
	r1 = *(u64*)(r10 - 8);				\
	/* Compare r1 with another register to trigger sync_linked_regs.\
	 * Having one random bit is important here, otherwise the verifier cuts\
	 * the corners.					\
	 */						\
	r2 = 0;						\
	if r1 != r2 goto l0_%=;				\
	/* The result of this comparison is predefined. */\
	if r0 == r2 goto l0_%=;				\
	/* Dead branch: the verifier should prune it. Do an invalid memory\
	 * access if the verifier follows it.		\
	 */						\
	r0 = *(u64*)(r9 + 0);				\
	exit;						\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("xdp")
__description("32-bit spill of 32-bit reg should assign ID")
__success __retval(0)
__naked void spill_32bit_of_32bit_ok(void)
{
	asm volatile ("					\
	/* Roll one bit to make the register inexact. */\
	call %[bpf_get_prandom_u32];			\
	w0 &= 0x80000000;				\
	/* 32-bit spill r0 to stack - should assign an ID. */\
	*(u32*)(r10 - 8) = r0;				\
	/* 32-bit fill r1 from stack - should preserve the ID. */\
	r1 = *(u32*)(r10 - 8);				\
	/* Compare r1 with another register to trigger sync_linked_regs.\
	 * Having one random bit is important here, otherwise the verifier cuts\
	 * the corners.					\
	 */						\
	r2 = 0;						\
	if r1 != r2 goto l0_%=;				\
	/* The result of this comparison is predefined. */\
	if r0 == r2 goto l0_%=;				\
	/* Dead branch: the verifier should prune it. Do an invalid memory\
	 * access if the verifier follows it.		\
	 */						\
	r0 = *(u64*)(r9 + 0);				\
	exit;						\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("xdp")
__description("16-bit spill of 16-bit reg should assign ID")
__success __retval(0)
__naked void spill_16bit_of_16bit_ok(void)
{
	asm volatile ("					\
	/* Roll one bit to make the register inexact. */\
	call %[bpf_get_prandom_u32];			\
	r0 &= 0x8000;					\
	/* 16-bit spill r0 to stack - should assign an ID. */\
	*(u16*)(r10 - 8) = r0;				\
	/* 16-bit fill r1 from stack - should preserve the ID. */\
	r1 = *(u16*)(r10 - 8);				\
	/* Compare r1 with another register to trigger sync_linked_regs.\
	 * Having one random bit is important here, otherwise the verifier cuts\
	 * the corners.					\
	 */						\
	r2 = 0;						\
	if r1 != r2 goto l0_%=;				\
	/* The result of this comparison is predefined. */\
	if r0 == r2 goto l0_%=;				\
	/* Dead branch: the verifier should prune it. Do an invalid memory\
	 * access if the verifier follows it.		\
	 */						\
	r0 = *(u64*)(r9 + 0);				\
	exit;						\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("xdp")
__description("8-bit spill of 8-bit reg should assign ID")
__success __retval(0)
__naked void spill_8bit_of_8bit_ok(void)
{
	asm volatile ("					\
	/* Roll one bit to make the register inexact. */\
	call %[bpf_get_prandom_u32];			\
	r0 &= 0x80;					\
	/* 8-bit spill r0 to stack - should assign an ID. */\
	*(u8*)(r10 - 8) = r0;				\
	/* 8-bit fill r1 from stack - should preserve the ID. */\
	r1 = *(u8*)(r10 - 8);				\
	/* Compare r1 with another register to trigger sync_linked_regs.\
	 * Having one random bit is important here, otherwise the verifier cuts\
	 * the corners.					\
	 */						\
	r2 = 0;						\
	if r1 != r2 goto l0_%=;				\
	/* The result of this comparison is predefined. */\
	if r0 == r2 goto l0_%=;				\
	/* Dead branch: the verifier should prune it. Do an invalid memory\
	 * access if the verifier follows it.		\
	 */						\
	r0 = *(u64*)(r9 + 0);				\
	exit;						\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("xdp")
__description("spill unbounded reg, then range check src")
__success __retval(0)
__naked void spill_unbounded(void)
{
	asm volatile ("					\
	/* Produce an unbounded scalar. */		\
	call %[bpf_get_prandom_u32];			\
	/* Spill r0 to stack. */			\
	*(u64*)(r10 - 8) = r0;				\
	/* Boundary check on r0. */			\
	if r0 > 16 goto l0_%=;				\
	/* Fill r0 from stack. */			\
	r0 = *(u64*)(r10 - 8);				\
	/* Boundary check on r0 with predetermined result. */\
	if r0 <= 16 goto l0_%=;				\
	/* Dead branch: the verifier should prune it. Do an invalid memory\
	 * access if the verifier follows it.		\
	 */						\
	r0 = *(u64*)(r9 + 0);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("xdp")
__description("32-bit fill after 64-bit spill")
__success __retval(0)
__naked void fill_32bit_after_spill_64bit(void)
{
	asm volatile("					\
	/* Randomize the upper 32 bits. */		\
	call %[bpf_get_prandom_u32];			\
	r0 <<= 32;					\
	/* 64-bit spill r0 to stack. */			\
	*(u64*)(r10 - 8) = r0;				\
	/* 32-bit fill r0 from stack. */		\
	"
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	"r0 = *(u32*)(r10 - 8);"
#else
	"r0 = *(u32*)(r10 - 4);"
#endif
	"						\
	/* Boundary check on r0 with predetermined result. */\
	if r0 == 0 goto l0_%=;				\
	/* Dead branch: the verifier should prune it. Do an invalid memory\
	 * access if the verifier follows it.		\
	 */						\
	r0 = *(u64*)(r9 + 0);				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("xdp")
__description("32-bit fill after 64-bit spill of 32-bit value should preserve ID")
__success __retval(0)
__naked void fill_32bit_after_spill_64bit_preserve_id(void)
{
	asm volatile ("					\
	/* Randomize the lower 32 bits. */		\
	call %[bpf_get_prandom_u32];			\
	w0 &= 0xffffffff;				\
	/* 64-bit spill r0 to stack - should assign an ID. */\
	*(u64*)(r10 - 8) = r0;				\
	/* 32-bit fill r1 from stack - should preserve the ID. */\
	"
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	"r1 = *(u32*)(r10 - 8);"
#else
	"r1 = *(u32*)(r10 - 4);"
#endif
	"						\
	/* Compare r1 with another register to trigger sync_linked_regs. */\
	r2 = 0;						\
	if r1 != r2 goto l0_%=;				\
	/* The result of this comparison is predefined. */\
	if r0 == r2 goto l0_%=;				\
	/* Dead branch: the verifier should prune it. Do an invalid memory\
	 * access if the verifier follows it.		\
	 */						\
	r0 = *(u64*)(r9 + 0);				\
	exit;						\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("xdp")
__description("32-bit fill after 64-bit spill should clear ID")
__failure __msg("math between ctx pointer and 4294967295 is not allowed")
__naked void fill_32bit_after_spill_64bit_clear_id(void)
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
	/* 64-bit spill r1 to stack - should assign an ID. */\
	*(u64*)(r10 - 8) = r1;				\
	/* 32-bit fill r2 from stack - should clear the ID. */\
	"
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	"r2 = *(u32*)(r10 - 8);"
#else
	"r2 = *(u32*)(r10 - 4);"
#endif
	"						\
	/* Compare r2 with another register to trigger sync_linked_regs.\
	 * Having one random bit is important here, otherwise the verifier cuts\
	 * the corners. If the ID was mistakenly preserved on fill, this would\
	 * cause the verifier to think that r1 is also equal to zero in one of\
	 * the branches, and equal to eight on the other branch.\
	 */						\
	r3 = 0;						\
	if r2 != r3 goto l0_%=;				\
l0_%=:	r1 >>= 32;					\
	/* The verifier shouldn't propagate r2's range to r1, so it should\
	 * still remember r1 = 0xffffffff and reject the below.\
	 */						\
	r6 += r1;					\
	r0 = *(u32*)(r6 + 0);				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/* stacksafe(): check if stack spill of an imprecise scalar in old state
 * is considered equivalent to STACK_{MISC,INVALID} in cur state.
 */
SEC("socket")
__success __log_level(2)
__msg("8: (79) r1 = *(u64 *)(r10 -8)")
__msg("8: safe")
__msg("processed 11 insns")
/* STACK_INVALID should prevent verifier in unpriv mode from
 * considering states equivalent and force an error on second
 * verification path (entry - label 1 - label 2).
 */
__failure_unpriv
__msg_unpriv("8: (79) r1 = *(u64 *)(r10 -8)")
__msg_unpriv("9: (95) exit")
__msg_unpriv("8: (79) r1 = *(u64 *)(r10 -8)")
__msg_unpriv("invalid read from stack off -8+2 size 8")
__flag(BPF_F_TEST_STATE_FREQ)
__naked void old_imprecise_scalar_vs_cur_stack_misc(void)
{
	asm volatile(
	/* get a random value for branching */
	"call %[bpf_ktime_get_ns];"
	"if r0 == 0 goto 1f;"
	/* conjure scalar at fp-8 */
	"r0 = 42;"
	"*(u64*)(r10 - 8) = r0;"
	"goto 2f;"
"1:"
	/* conjure STACK_{MISC,INVALID} at fp-8 */
	"call %[bpf_ktime_get_ns];"
	"*(u16*)(r10 - 8) = r0;"
	"*(u16*)(r10 - 4) = r0;"
"2:"
	/* read fp-8, should be considered safe on second visit */
	"r1 = *(u64*)(r10 - 8);"
	"exit;"
	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

/* stacksafe(): check that stack spill of a precise scalar in old state
 * is not considered equivalent to STACK_MISC in cur state.
 */
SEC("socket")
__success __log_level(2)
/* verifier should visit 'if r1 == 0x2a ...' two times:
 * - once for path entry - label 2;
 * - once for path entry - label 1 - label 2.
 */
__msg("if r1 == 0x2a goto pc+0")
__msg("if r1 == 0x2a goto pc+0")
__msg("processed 15 insns")
__flag(BPF_F_TEST_STATE_FREQ)
__naked void old_precise_scalar_vs_cur_stack_misc(void)
{
	asm volatile(
	/* get a random value for branching */
	"call %[bpf_ktime_get_ns];"
	"if r0 == 0 goto 1f;"
	/* conjure scalar at fp-8 */
	"r0 = 42;"
	"*(u64*)(r10 - 8) = r0;"
	"goto 2f;"
"1:"
	/* conjure STACK_MISC at fp-8 */
	"call %[bpf_ktime_get_ns];"
	"*(u64*)(r10 - 8) = r0;"
	"*(u32*)(r10 - 4) = r0;"
"2:"
	/* read fp-8, should not be considered safe on second visit */
	"r1 = *(u64*)(r10 - 8);"
	/* use r1 in precise context */
	"if r1 == 42 goto +0;"
	"exit;"
	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

/* stacksafe(): check if STACK_MISC in old state is considered
 * equivalent to stack spill of a scalar in cur state.
 */
SEC("socket")
__success  __log_level(2)
__msg("8: (79) r0 = *(u64 *)(r10 -8)")
__msg("8: safe")
__msg("processed 11 insns")
__flag(BPF_F_TEST_STATE_FREQ)
__naked void old_stack_misc_vs_cur_scalar(void)
{
	asm volatile(
	/* get a random value for branching */
	"call %[bpf_ktime_get_ns];"
	"if r0 == 0 goto 1f;"
	/* conjure STACK_{MISC,INVALID} at fp-8 */
	"call %[bpf_ktime_get_ns];"
	"*(u16*)(r10 - 8) = r0;"
	"*(u16*)(r10 - 4) = r0;"
	"goto 2f;"
"1:"
	/* conjure scalar at fp-8 */
	"r0 = 42;"
	"*(u64*)(r10 - 8) = r0;"
"2:"
	/* read fp-8, should be considered safe on second visit */
	"r0 = *(u64*)(r10 - 8);"
	"exit;"
	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

/* stacksafe(): check that STACK_MISC in old state is not considered
 * equivalent to stack spill of a non-scalar in cur state.
 */
SEC("socket")
__success  __log_level(2)
/* verifier should process exit instructions twice:
 * - once for path entry - label 2;
 * - once for path entry - label 1 - label 2.
 */
__msg("8: (79) r1 = *(u64 *)(r10 -8)")
__msg("9: (95) exit")
__msg("from 2 to 7")
__msg("8: safe")
__msg("processed 11 insns")
__flag(BPF_F_TEST_STATE_FREQ)
__naked void old_stack_misc_vs_cur_ctx_ptr(void)
{
	asm volatile(
	/* remember context pointer in r9 */
	"r9 = r1;"
	/* get a random value for branching */
	"call %[bpf_ktime_get_ns];"
	"if r0 == 0 goto 1f;"
	/* conjure STACK_MISC at fp-8 */
	"call %[bpf_ktime_get_ns];"
	"*(u64*)(r10 - 8) = r0;"
	"*(u32*)(r10 - 4) = r0;"
	"goto 2f;"
"1:"
	/* conjure context pointer in fp-8 */
	"*(u64*)(r10 - 8) = r9;"
"2:"
	/* read fp-8, should not be considered safe on second visit */
	"r1 = *(u64*)(r10 - 8);"
	"exit;"
	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
