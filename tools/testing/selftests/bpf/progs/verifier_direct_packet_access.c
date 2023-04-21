// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/direct_packet_access.c */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

SEC("tc")
__description("pkt_end - pkt_start is allowed")
__success __retval(TEST_DATA_LEN)
__naked void end_pkt_start_is_allowed(void)
{
	asm volatile ("					\
	r0 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r0 -= r2;					\
	exit;						\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tc")
__description("direct packet access: test1")
__success __retval(0)
__naked void direct_packet_access_test1(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r3 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r0 = r2;					\
	r0 += 8;					\
	if r0 > r3 goto l0_%=;				\
	r0 = *(u8*)(r2 + 0);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tc")
__description("direct packet access: test2")
__success __retval(0)
__naked void direct_packet_access_test2(void)
{
	asm volatile ("					\
	r0 = 1;						\
	r4 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r3 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r5 = r3;					\
	r5 += 14;					\
	if r5 > r4 goto l0_%=;				\
	r0 = *(u8*)(r3 + 7);				\
	r4 = *(u8*)(r3 + 12);				\
	r4 *= 14;					\
	r3 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r3 += r4;					\
	r2 = *(u32*)(r1 + %[__sk_buff_len]);		\
	r2 <<= 49;					\
	r2 >>= 49;					\
	r3 += r2;					\
	r2 = r3;					\
	r2 += 8;					\
	r1 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	if r2 > r1 goto l1_%=;				\
	r1 = *(u8*)(r3 + 4);				\
l1_%=:	r0 = 0;						\
l0_%=:	exit;						\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end)),
	  __imm_const(__sk_buff_len, offsetof(struct __sk_buff, len))
	: __clobber_all);
}

SEC("socket")
__description("direct packet access: test3")
__failure __msg("invalid bpf_context access off=76")
__failure_unpriv
__naked void direct_packet_access_test3(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data))
	: __clobber_all);
}

SEC("tc")
__description("direct packet access: test4 (write)")
__success __retval(0)
__naked void direct_packet_access_test4_write(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r3 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r0 = r2;					\
	r0 += 8;					\
	if r0 > r3 goto l0_%=;				\
	*(u8*)(r2 + 0) = r2;				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tc")
__description("direct packet access: test5 (pkt_end >= reg, good access)")
__success __retval(0)
__naked void pkt_end_reg_good_access(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r3 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r0 = r2;					\
	r0 += 8;					\
	if r3 >= r0 goto l0_%=;				\
	r0 = 1;						\
	exit;						\
l0_%=:	r0 = *(u8*)(r2 + 0);				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tc")
__description("direct packet access: test6 (pkt_end >= reg, bad access)")
__failure __msg("invalid access to packet")
__naked void pkt_end_reg_bad_access(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r3 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r0 = r2;					\
	r0 += 8;					\
	if r3 >= r0 goto l0_%=;				\
	r0 = *(u8*)(r2 + 0);				\
	r0 = 1;						\
	exit;						\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tc")
__description("direct packet access: test7 (pkt_end >= reg, both accesses)")
__failure __msg("invalid access to packet")
__naked void pkt_end_reg_both_accesses(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r3 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r0 = r2;					\
	r0 += 8;					\
	if r3 >= r0 goto l0_%=;				\
	r0 = *(u8*)(r2 + 0);				\
	r0 = 1;						\
	exit;						\
l0_%=:	r0 = *(u8*)(r2 + 0);				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tc")
__description("direct packet access: test8 (double test, variant 1)")
__success __retval(0)
__naked void test8_double_test_variant_1(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r3 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r0 = r2;					\
	r0 += 8;					\
	if r3 >= r0 goto l0_%=;				\
	if r0 > r3 goto l1_%=;				\
	r0 = *(u8*)(r2 + 0);				\
l1_%=:	r0 = 1;						\
	exit;						\
l0_%=:	r0 = *(u8*)(r2 + 0);				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tc")
__description("direct packet access: test9 (double test, variant 2)")
__success __retval(0)
__naked void test9_double_test_variant_2(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r3 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r0 = r2;					\
	r0 += 8;					\
	if r3 >= r0 goto l0_%=;				\
	r0 = 1;						\
	exit;						\
l0_%=:	if r0 > r3 goto l1_%=;				\
	r0 = *(u8*)(r2 + 0);				\
l1_%=:	r0 = *(u8*)(r2 + 0);				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tc")
__description("direct packet access: test10 (write invalid)")
__failure __msg("invalid access to packet")
__naked void packet_access_test10_write_invalid(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r3 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r0 = r2;					\
	r0 += 8;					\
	if r0 > r3 goto l0_%=;				\
	r0 = 0;						\
	exit;						\
l0_%=:	*(u8*)(r2 + 0) = r2;				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tc")
__description("direct packet access: test11 (shift, good access)")
__success __retval(1)
__naked void access_test11_shift_good_access(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r3 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r0 = r2;					\
	r0 += 22;					\
	if r0 > r3 goto l0_%=;				\
	r3 = 144;					\
	r5 = r3;					\
	r5 += 23;					\
	r5 >>= 3;					\
	r6 = r2;					\
	r6 += r5;					\
	r0 = 1;						\
	exit;						\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tc")
__description("direct packet access: test12 (and, good access)")
__success __retval(1)
__naked void access_test12_and_good_access(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r3 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r0 = r2;					\
	r0 += 22;					\
	if r0 > r3 goto l0_%=;				\
	r3 = 144;					\
	r5 = r3;					\
	r5 += 23;					\
	r5 &= 15;					\
	r6 = r2;					\
	r6 += r5;					\
	r0 = 1;						\
	exit;						\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tc")
__description("direct packet access: test13 (branches, good access)")
__success __retval(1)
__naked void access_test13_branches_good_access(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r3 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r0 = r2;					\
	r0 += 22;					\
	if r0 > r3 goto l0_%=;				\
	r3 = *(u32*)(r1 + %[__sk_buff_mark]);		\
	r4 = 1;						\
	if r3 > r4 goto l1_%=;				\
	r3 = 14;					\
	goto l2_%=;					\
l1_%=:	r3 = 24;					\
l2_%=:	r5 = r3;					\
	r5 += 23;					\
	r5 &= 15;					\
	r6 = r2;					\
	r6 += r5;					\
	r0 = 1;						\
	exit;						\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end)),
	  __imm_const(__sk_buff_mark, offsetof(struct __sk_buff, mark))
	: __clobber_all);
}

SEC("tc")
__description("direct packet access: test14 (pkt_ptr += 0, CONST_IMM, good access)")
__success __retval(1)
__naked void _0_const_imm_good_access(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r3 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r0 = r2;					\
	r0 += 22;					\
	if r0 > r3 goto l0_%=;				\
	r5 = 12;					\
	r5 >>= 4;					\
	r6 = r2;					\
	r6 += r5;					\
	r0 = *(u8*)(r6 + 0);				\
	r0 = 1;						\
	exit;						\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tc")
__description("direct packet access: test15 (spill with xadd)")
__failure __msg("R2 invalid mem access 'scalar'")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void access_test15_spill_with_xadd(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r3 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r0 = r2;					\
	r0 += 8;					\
	if r0 > r3 goto l0_%=;				\
	r5 = 4096;					\
	r4 = r10;					\
	r4 += -8;					\
	*(u64*)(r4 + 0) = r2;				\
	lock *(u64 *)(r4 + 0) += r5;			\
	r2 = *(u64*)(r4 + 0);				\
	*(u32*)(r2 + 0) = r5;				\
	r0 = 0;						\
l0_%=:	exit;						\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tc")
__description("direct packet access: test16 (arith on data_end)")
__failure __msg("R3 pointer arithmetic on pkt_end")
__naked void test16_arith_on_data_end(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r3 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r0 = r2;					\
	r0 += 8;					\
	r3 += 16;					\
	if r0 > r3 goto l0_%=;				\
	*(u8*)(r2 + 0) = r2;				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tc")
__description("direct packet access: test17 (pruning, alignment)")
__failure __msg("misaligned packet access off 2+(0x0; 0x0)+15+-4 size 4")
__flag(BPF_F_STRICT_ALIGNMENT)
__naked void packet_access_test17_pruning_alignment(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r3 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r7 = *(u32*)(r1 + %[__sk_buff_mark]);		\
	r0 = r2;					\
	r0 += 14;					\
	if r7 > 1 goto l0_%=;				\
l2_%=:	if r0 > r3 goto l1_%=;				\
	*(u32*)(r0 - 4) = r0;				\
l1_%=:	r0 = 0;						\
	exit;						\
l0_%=:	r0 += 1;					\
	goto l2_%=;					\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end)),
	  __imm_const(__sk_buff_mark, offsetof(struct __sk_buff, mark))
	: __clobber_all);
}

SEC("tc")
__description("direct packet access: test18 (imm += pkt_ptr, 1)")
__success __retval(0)
__naked void test18_imm_pkt_ptr_1(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r3 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r0 = 8;						\
	r0 += r2;					\
	if r0 > r3 goto l0_%=;				\
	*(u8*)(r2 + 0) = r2;				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tc")
__description("direct packet access: test19 (imm += pkt_ptr, 2)")
__success __retval(0)
__naked void test19_imm_pkt_ptr_2(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r3 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r0 = r2;					\
	r0 += 8;					\
	if r0 > r3 goto l0_%=;				\
	r4 = 4;						\
	r4 += r2;					\
	*(u8*)(r4 + 0) = r4;				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tc")
__description("direct packet access: test20 (x += pkt_ptr, 1)")
__success __retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void test20_x_pkt_ptr_1(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r3 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r0 = 0xffffffff;				\
	*(u64*)(r10 - 8) = r0;				\
	r0 = *(u64*)(r10 - 8);				\
	r0 &= 0x7fff;					\
	r4 = r0;					\
	r4 += r2;					\
	r5 = r4;					\
	r4 += %[__imm_0];				\
	if r4 > r3 goto l0_%=;				\
	*(u64*)(r5 + 0) = r4;				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__imm_0, 0x7fff - 1),
	  __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tc")
__description("direct packet access: test21 (x += pkt_ptr, 2)")
__success __retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void test21_x_pkt_ptr_2(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r3 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r0 = r2;					\
	r0 += 8;					\
	if r0 > r3 goto l0_%=;				\
	r4 = 0xffffffff;				\
	*(u64*)(r10 - 8) = r4;				\
	r4 = *(u64*)(r10 - 8);				\
	r4 &= 0x7fff;					\
	r4 += r2;					\
	r5 = r4;					\
	r4 += %[__imm_0];				\
	if r4 > r3 goto l0_%=;				\
	*(u64*)(r5 + 0) = r4;				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__imm_0, 0x7fff - 1),
	  __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tc")
__description("direct packet access: test22 (x += pkt_ptr, 3)")
__success __retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void test22_x_pkt_ptr_3(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r3 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r0 = r2;					\
	r0 += 8;					\
	*(u64*)(r10 - 8) = r2;				\
	*(u64*)(r10 - 16) = r3;				\
	r3 = *(u64*)(r10 - 16);				\
	if r0 > r3 goto l0_%=;				\
	r2 = *(u64*)(r10 - 8);				\
	r4 = 0xffffffff;				\
	lock *(u64 *)(r10 - 8) += r4;			\
	r4 = *(u64*)(r10 - 8);				\
	r4 >>= 49;					\
	r4 += r2;					\
	r0 = r4;					\
	r0 += 2;					\
	if r0 > r3 goto l0_%=;				\
	r2 = 1;						\
	*(u16*)(r4 + 0) = r2;				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tc")
__description("direct packet access: test23 (x += pkt_ptr, 4)")
__failure __msg("invalid access to packet, off=0 size=8, R5(id=2,off=0,r=0)")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void test23_x_pkt_ptr_4(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r3 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r0 = *(u32*)(r1 + %[__sk_buff_mark]);		\
	*(u64*)(r10 - 8) = r0;				\
	r0 = *(u64*)(r10 - 8);				\
	r0 &= 0xffff;					\
	r4 = r0;					\
	r0 = 31;					\
	r0 += r4;					\
	r0 += r2;					\
	r5 = r0;					\
	r0 += %[__imm_0];				\
	if r0 > r3 goto l0_%=;				\
	*(u64*)(r5 + 0) = r0;				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__imm_0, 0xffff - 1),
	  __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end)),
	  __imm_const(__sk_buff_mark, offsetof(struct __sk_buff, mark))
	: __clobber_all);
}

SEC("tc")
__description("direct packet access: test24 (x += pkt_ptr, 5)")
__success __retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void test24_x_pkt_ptr_5(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r3 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r0 = 0xffffffff;				\
	*(u64*)(r10 - 8) = r0;				\
	r0 = *(u64*)(r10 - 8);				\
	r0 &= 0xff;					\
	r4 = r0;					\
	r0 = 64;					\
	r0 += r4;					\
	r0 += r2;					\
	r5 = r0;					\
	r0 += %[__imm_0];				\
	if r0 > r3 goto l0_%=;				\
	*(u64*)(r5 + 0) = r0;				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__imm_0, 0x7fff - 1),
	  __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tc")
__description("direct packet access: test25 (marking on <, good access)")
__success __retval(0)
__naked void test25_marking_on_good_access(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r3 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r0 = r2;					\
	r0 += 8;					\
	if r0 < r3 goto l0_%=;				\
l1_%=:	r0 = 0;						\
	exit;						\
l0_%=:	r0 = *(u8*)(r2 + 0);				\
	goto l1_%=;					\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tc")
__description("direct packet access: test26 (marking on <, bad access)")
__failure __msg("invalid access to packet")
__naked void test26_marking_on_bad_access(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r3 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r0 = r2;					\
	r0 += 8;					\
	if r0 < r3 goto l0_%=;				\
	r0 = *(u8*)(r2 + 0);				\
l1_%=:	r0 = 0;						\
	exit;						\
l0_%=:	goto l1_%=;					\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tc")
__description("direct packet access: test27 (marking on <=, good access)")
__success __retval(1)
__naked void test27_marking_on_good_access(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r3 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r0 = r2;					\
	r0 += 8;					\
	if r3 <= r0 goto l0_%=;				\
	r0 = *(u8*)(r2 + 0);				\
l0_%=:	r0 = 1;						\
	exit;						\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tc")
__description("direct packet access: test28 (marking on <=, bad access)")
__failure __msg("invalid access to packet")
__naked void test28_marking_on_bad_access(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r3 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r0 = r2;					\
	r0 += 8;					\
	if r3 <= r0 goto l0_%=;				\
l1_%=:	r0 = 1;						\
	exit;						\
l0_%=:	r0 = *(u8*)(r2 + 0);				\
	goto l1_%=;					\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tc")
__description("direct packet access: test29 (reg > pkt_end in subprog)")
__success __retval(0)
__naked void reg_pkt_end_in_subprog(void)
{
	asm volatile ("					\
	r6 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r2 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r3 = r6;					\
	r3 += 8;					\
	call reg_pkt_end_in_subprog__1;			\
	if r0 == 0 goto l0_%=;				\
	r0 = *(u8*)(r6 + 0);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

static __naked __noinline __attribute__((used))
void reg_pkt_end_in_subprog__1(void)
{
	asm volatile ("					\
	r0 = 0;						\
	if r3 > r2 goto l0_%=;				\
	r0 = 1;						\
l0_%=:	exit;						\
"	::: __clobber_all);
}

SEC("tc")
__description("direct packet access: test30 (check_id() in regsafe(), bad access)")
__failure __msg("invalid access to packet, off=0 size=1, R2")
__flag(BPF_F_TEST_STATE_FREQ)
__naked void id_in_regsafe_bad_access(void)
{
	asm volatile ("					\
	/* r9 = ctx */					\
	r9 = r1;					\
	/* r7 = ktime_get_ns() */			\
	call %[bpf_ktime_get_ns];			\
	r7 = r0;					\
	/* r6 = ktime_get_ns() */			\
	call %[bpf_ktime_get_ns];			\
	r6 = r0;					\
	/* r2 = ctx->data				\
	 * r3 = ctx->data				\
	 * r4 = ctx->data_end				\
	 */						\
	r2 = *(u32*)(r9 + %[__sk_buff_data]);		\
	r3 = *(u32*)(r9 + %[__sk_buff_data]);		\
	r4 = *(u32*)(r9 + %[__sk_buff_data_end]);	\
	/* if r6 > 100 goto exit			\
	 * if r7 > 100 goto exit			\
	 */						\
	if r6 > 100 goto l0_%=;				\
	if r7 > 100 goto l0_%=;				\
	/* r2 += r6              ; this forces assignment of ID to r2\
	 * r2 += 1               ; get some fixed off for r2\
	 * r3 += r7              ; this forces assignment of ID to r3\
	 * r3 += 1               ; get some fixed off for r3\
	 */						\
	r2 += r6;					\
	r2 += 1;					\
	r3 += r7;					\
	r3 += 1;					\
	/* if r6 > r7 goto +1    ; no new information about the state is derived from\
	 *                       ; this check, thus produced verifier states differ\
	 *                       ; only in 'insn_idx'	\
	 * r2 = r3               ; optionally share ID between r2 and r3\
	 */						\
	if r6 != r7 goto l1_%=;				\
	r2 = r3;					\
l1_%=:	/* if r3 > ctx->data_end goto exit */		\
	if r3 > r4 goto l0_%=;				\
	/* r5 = *(u8 *) (r2 - 1) ; access packet memory using r2,\
	 *                       ; this is not always safe\
	 */						\
	r5 = *(u8*)(r2 - 1);				\
l0_%=:	/* exit(0) */					\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_ktime_get_ns),
	  __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
