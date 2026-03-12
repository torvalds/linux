// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/subreg.c */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

/* This file contains sub-register zero extension checks for insns defining
 * sub-registers, meaning:
 *   - All insns under BPF_ALU class. Their BPF_ALU32 variants or narrow width
 *     forms (BPF_END) could define sub-registers.
 *   - Narrow direct loads, BPF_B/H/W | BPF_LDX.
 *   - BPF_LD is not exposed to JIT back-ends, so no need for testing.
 *
 * "get_prandom_u32" is used to initialize low 32-bit of some registers to
 * prevent potential optimizations done by verifier or JIT back-ends which could
 * optimize register back into constant when range info shows one register is a
 * constant.
 */

SEC("socket")
__description("add32 reg zero extend check")
__success __success_unpriv __retval(0)
__naked void add32_reg_zero_extend_check(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = r0;					\
	r0 = 0x100000000 ll;				\
	w0 += w1;					\
	r0 >>= 32;					\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("add32 imm zero extend check")
__success __success_unpriv __retval(0)
__naked void add32_imm_zero_extend_check(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = 0x1000000000 ll;				\
	r0 |= r1;					\
	/* An insn could have no effect on the low 32-bit, for example:\
	 *   a = a + 0					\
	 *   a = a | 0					\
	 *   a = a & -1					\
	 * But, they should still zero high 32-bit.	\
	 */						\
	w0 += 0;					\
	r0 >>= 32;					\
	r6 = r0;					\
	call %[bpf_get_prandom_u32];			\
	r1 = 0x1000000000 ll;				\
	r0 |= r1;					\
	w0 += -2;					\
	r0 >>= 32;					\
	r0 |= r6;					\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("sub32 reg zero extend check")
__success __success_unpriv __retval(0)
__naked void sub32_reg_zero_extend_check(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = r0;					\
	r0 = 0x1ffffffff ll;				\
	w0 -= w1;					\
	r0 >>= 32;					\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("sub32 imm zero extend check")
__success __success_unpriv __retval(0)
__naked void sub32_imm_zero_extend_check(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = 0x1000000000 ll;				\
	r0 |= r1;					\
	w0 -= 0;					\
	r0 >>= 32;					\
	r6 = r0;					\
	call %[bpf_get_prandom_u32];			\
	r1 = 0x1000000000 ll;				\
	r0 |= r1;					\
	w0 -= 1;					\
	r0 >>= 32;					\
	r0 |= r6;					\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("mul32 reg zero extend check")
__success __success_unpriv __retval(0)
__naked void mul32_reg_zero_extend_check(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = r0;					\
	r0 = 0x100000001 ll;				\
	w0 *= w1;					\
	r0 >>= 32;					\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("mul32 imm zero extend check")
__success __success_unpriv __retval(0)
__naked void mul32_imm_zero_extend_check(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = 0x1000000000 ll;				\
	r0 |= r1;					\
	w0 *= 1;					\
	r0 >>= 32;					\
	r6 = r0;					\
	call %[bpf_get_prandom_u32];			\
	r1 = 0x1000000000 ll;				\
	r0 |= r1;					\
	w0 *= -1;					\
	r0 >>= 32;					\
	r0 |= r6;					\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("div32 reg zero extend check")
__success __success_unpriv __retval(0)
__naked void div32_reg_zero_extend_check(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = r0;					\
	r0 = -1;					\
	w0 /= w1;					\
	r0 >>= 32;					\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("div32 imm zero extend check")
__success __success_unpriv __retval(0)
__naked void div32_imm_zero_extend_check(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = 0x1000000000 ll;				\
	r0 |= r1;					\
	w0 /= 1;					\
	r0 >>= 32;					\
	r6 = r0;					\
	call %[bpf_get_prandom_u32];			\
	r1 = 0x1000000000 ll;				\
	r0 |= r1;					\
	w0 /= 2;					\
	r0 >>= 32;					\
	r0 |= r6;					\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("or32 reg zero extend check")
__success __success_unpriv __retval(0)
__naked void or32_reg_zero_extend_check(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = r0;					\
	r0 = 0x100000001 ll;				\
	w0 |= w1;					\
	r0 >>= 32;					\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("or32 imm zero extend check")
__success __success_unpriv __retval(0)
__naked void or32_imm_zero_extend_check(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = 0x1000000000 ll;				\
	r0 |= r1;					\
	w0 |= 0;					\
	r0 >>= 32;					\
	r6 = r0;					\
	call %[bpf_get_prandom_u32];			\
	r1 = 0x1000000000 ll;				\
	r0 |= r1;					\
	w0 |= 1;					\
	r0 >>= 32;					\
	r0 |= r6;					\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("and32 reg zero extend check")
__success __success_unpriv __retval(0)
__naked void and32_reg_zero_extend_check(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = 0x100000000 ll;				\
	r1 |= r0;					\
	r0 = 0x1ffffffff ll;				\
	w0 &= w1;					\
	r0 >>= 32;					\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("and32 imm zero extend check")
__success __success_unpriv __retval(0)
__naked void and32_imm_zero_extend_check(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = 0x1000000000 ll;				\
	r0 |= r1;					\
	w0 &= -1;					\
	r0 >>= 32;					\
	r6 = r0;					\
	call %[bpf_get_prandom_u32];			\
	r1 = 0x1000000000 ll;				\
	r0 |= r1;					\
	w0 &= -2;					\
	r0 >>= 32;					\
	r0 |= r6;					\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("lsh32 reg zero extend check")
__success __success_unpriv __retval(0)
__naked void lsh32_reg_zero_extend_check(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = 0x100000000 ll;				\
	r0 |= r1;					\
	r1 = 1;						\
	w0 <<= w1;					\
	r0 >>= 32;					\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("lsh32 imm zero extend check")
__success __success_unpriv __retval(0)
__naked void lsh32_imm_zero_extend_check(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = 0x1000000000 ll;				\
	r0 |= r1;					\
	w0 <<= 0;					\
	r0 >>= 32;					\
	r6 = r0;					\
	call %[bpf_get_prandom_u32];			\
	r1 = 0x1000000000 ll;				\
	r0 |= r1;					\
	w0 <<= 1;					\
	r0 >>= 32;					\
	r0 |= r6;					\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("rsh32 reg zero extend check")
__success __success_unpriv __retval(0)
__naked void rsh32_reg_zero_extend_check(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = 0x1000000000 ll;				\
	r0 |= r1;					\
	r1 = 1;						\
	w0 >>= w1;					\
	r0 >>= 32;					\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("rsh32 imm zero extend check")
__success __success_unpriv __retval(0)
__naked void rsh32_imm_zero_extend_check(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = 0x1000000000 ll;				\
	r0 |= r1;					\
	w0 >>= 0;					\
	r0 >>= 32;					\
	r6 = r0;					\
	call %[bpf_get_prandom_u32];			\
	r1 = 0x1000000000 ll;				\
	r0 |= r1;					\
	w0 >>= 1;					\
	r0 >>= 32;					\
	r0 |= r6;					\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("neg32 reg zero extend check")
__success __success_unpriv __retval(0)
__naked void neg32_reg_zero_extend_check(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = 0x1000000000 ll;				\
	r0 |= r1;					\
	w0 = -w0;					\
	r0 >>= 32;					\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("mod32 reg zero extend check")
__success __success_unpriv __retval(0)
__naked void mod32_reg_zero_extend_check(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = r0;					\
	r0 = -1;					\
	w0 %%= w1;					\
	r0 >>= 32;					\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("mod32 imm zero extend check")
__success __success_unpriv __retval(0)
__naked void mod32_imm_zero_extend_check(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = 0x1000000000 ll;				\
	r0 |= r1;					\
	w0 %%= 1;					\
	r0 >>= 32;					\
	r6 = r0;					\
	call %[bpf_get_prandom_u32];			\
	r1 = 0x1000000000 ll;				\
	r0 |= r1;					\
	w0 %%= 2;					\
	r0 >>= 32;					\
	r0 |= r6;					\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("xor32 reg zero extend check")
__success __success_unpriv __retval(0)
__naked void xor32_reg_zero_extend_check(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = r0;					\
	r0 = 0x100000000 ll;				\
	w0 ^= w1;					\
	r0 >>= 32;					\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("xor32 imm zero extend check")
__success __success_unpriv __retval(0)
__naked void xor32_imm_zero_extend_check(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = 0x1000000000 ll;				\
	r0 |= r1;					\
	w0 ^= 1;					\
	r0 >>= 32;					\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("mov32 reg zero extend check")
__success __success_unpriv __retval(0)
__naked void mov32_reg_zero_extend_check(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = 0x100000000 ll;				\
	r1 |= r0;					\
	r0 = 0x100000000 ll;				\
	w0 = w1;					\
	r0 >>= 32;					\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("mov32 imm zero extend check")
__success __success_unpriv __retval(0)
__naked void mov32_imm_zero_extend_check(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = 0x1000000000 ll;				\
	r0 |= r1;					\
	w0 = 0;						\
	r0 >>= 32;					\
	r6 = r0;					\
	call %[bpf_get_prandom_u32];			\
	r1 = 0x1000000000 ll;				\
	r0 |= r1;					\
	w0 = 1;						\
	r0 >>= 32;					\
	r0 |= r6;					\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("arsh32 reg zero extend check")
__success __success_unpriv __retval(0)
__naked void arsh32_reg_zero_extend_check(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = 0x1000000000 ll;				\
	r0 |= r1;					\
	r1 = 1;						\
	w0 s>>= w1;					\
	r0 >>= 32;					\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("arsh32 imm zero extend check")
__success __success_unpriv __retval(0)
__naked void arsh32_imm_zero_extend_check(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = 0x1000000000 ll;				\
	r0 |= r1;					\
	w0 s>>= 0;					\
	r0 >>= 32;					\
	r6 = r0;					\
	call %[bpf_get_prandom_u32];			\
	r1 = 0x1000000000 ll;				\
	r0 |= r1;					\
	w0 s>>= 1;					\
	r0 >>= 32;					\
	r0 |= r6;					\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("arsh32 imm sign positive extend check")
__success __retval(0)
__log_level(2)
__msg("2: (57) r6 &= 4095                    ; R6=scalar(smin=smin32=0,smax=umax=smax32=umax32=4095,var_off=(0x0; 0xfff))")
__msg("3: (67) r6 <<= 32                     ; R6=scalar(smin=smin32=0,smax=umax=0xfff00000000,smax32=umax32=0,var_off=(0x0; 0xfff00000000))")
__msg("4: (c7) r6 s>>= 32                    ; R6=scalar(smin=smin32=0,smax=umax=smax32=umax32=4095,var_off=(0x0; 0xfff))")
__naked void arsh32_imm_sign_extend_positive_check(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r6 = r0;					\
	r6 &= 4095;					\
	r6 <<= 32;					\
	r6 s>>= 32;					\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("arsh32 imm sign negative extend check")
__success __retval(0)
__log_level(2)
__msg("3: (17) r6 -= 4095                    ; R6=scalar(smin=smin32=-4095,smax=smax32=0)")
__msg("4: (67) r6 <<= 32                     ; R6=scalar(smin=0xfffff00100000000,smax=smax32=umax32=0,umax=0xffffffff00000000,smin32=0,var_off=(0x0; 0xffffffff00000000))")
__msg("5: (c7) r6 s>>= 32                    ; R6=scalar(smin=smin32=-4095,smax=smax32=0)")
__naked void arsh32_imm_sign_extend_negative_check(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r6 = r0;					\
	r6 &= 4095;					\
	r6 -= 4095;					\
	r6 <<= 32;					\
	r6 s>>= 32;					\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("arsh32 imm sign extend check")
__success __retval(0)
__log_level(2)
__msg("3: (17) r6 -= 2047                    ; R6=scalar(smin=smin32=-2047,smax=smax32=2048)")
__msg("4: (67) r6 <<= 32                     ; R6=scalar(smin=0xfffff80100000000,smax=0x80000000000,umax=0xffffffff00000000,smin32=0,smax32=umax32=0,var_off=(0x0; 0xffffffff00000000))")
__msg("5: (c7) r6 s>>= 32                    ; R6=scalar(smin=smin32=-2047,smax=smax32=2048)")
__naked void arsh32_imm_sign_extend_check(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r6 = r0;					\
	r6 &= 4095;					\
	r6 -= 2047;					\
	r6 <<= 32;					\
	r6 s>>= 32;					\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("end16 (to_le) reg zero extend check")
__success __success_unpriv __retval(0)
__naked void le_reg_zero_extend_check_1(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r6 = r0;					\
	r6 <<= 32;					\
	call %[bpf_get_prandom_u32];			\
	r0 |= r6;					\
	r0 = le16 r0;					\
	r0 >>= 32;					\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("end32 (to_le) reg zero extend check")
__success __success_unpriv __retval(0)
__naked void le_reg_zero_extend_check_2(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r6 = r0;					\
	r6 <<= 32;					\
	call %[bpf_get_prandom_u32];			\
	r0 |= r6;					\
	r0 = le32 r0;					\
	r0 >>= 32;					\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("end16 (to_be) reg zero extend check")
__success __success_unpriv __retval(0)
__naked void be_reg_zero_extend_check_1(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r6 = r0;					\
	r6 <<= 32;					\
	call %[bpf_get_prandom_u32];			\
	r0 |= r6;					\
	r0 = be16 r0;					\
	r0 >>= 32;					\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("end32 (to_be) reg zero extend check")
__success __success_unpriv __retval(0)
__naked void be_reg_zero_extend_check_2(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r6 = r0;					\
	r6 <<= 32;					\
	call %[bpf_get_prandom_u32];			\
	r0 |= r6;					\
	r0 = be32 r0;					\
	r0 >>= 32;					\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("ldx_b zero extend check")
__success __success_unpriv __retval(0)
__naked void ldx_b_zero_extend_check(void)
{
	asm volatile ("					\
	r6 = r10;					\
	r6 += -4;					\
	r7 = 0xfaceb00c;				\
	*(u32*)(r6 + 0) = r7;				\
	call %[bpf_get_prandom_u32];			\
	r1 = 0x1000000000 ll;				\
	r0 |= r1;					\
	r0 = *(u8*)(r6 + 0);				\
	r0 >>= 32;					\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("ldx_h zero extend check")
__success __success_unpriv __retval(0)
__naked void ldx_h_zero_extend_check(void)
{
	asm volatile ("					\
	r6 = r10;					\
	r6 += -4;					\
	r7 = 0xfaceb00c;				\
	*(u32*)(r6 + 0) = r7;				\
	call %[bpf_get_prandom_u32];			\
	r1 = 0x1000000000 ll;				\
	r0 |= r1;					\
	r0 = *(u16*)(r6 + 0);				\
	r0 >>= 32;					\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("ldx_w zero extend check")
__success __success_unpriv __retval(0)
__naked void ldx_w_zero_extend_check(void)
{
	asm volatile ("					\
	r6 = r10;					\
	r6 += -4;					\
	r7 = 0xfaceb00c;				\
	*(u32*)(r6 + 0) = r7;				\
	call %[bpf_get_prandom_u32];			\
	r1 = 0x1000000000 ll;				\
	r0 |= r1;					\
	r0 = *(u32*)(r6 + 0);				\
	r0 >>= 32;					\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__success __success_unpriv __retval(0)
__naked void arsh_31_and(void)
{
	/* Below is what LLVM generates in cilium's bpf_wiregard.o */
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	w2 = w0;					\
	w2 s>>= 31;					\
	w2 &= -134; /* w2 becomes 0 or -134 */		\
	if w2 s> -1 goto +2;				\
	/* Branch always taken because w2 = -134 */	\
	if w2 != -136 goto +1;				\
	w0 /= 0;					\
	w0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__success __success_unpriv __retval(0)
__naked void arsh_63_and(void)
{
	/* Copy of arsh_31 with s/w/r/ */
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r2 = r0;					\
	r2 <<= 32;					\
	r2 s>>= 63;					\
	r2 &= -134;					\
	if r2 s> -1 goto +2;				\
	/* Branch always taken because w2 = -134 */	\
	if r2 != -136 goto +1;				\
	r0 /= 0;					\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__success __success_unpriv __retval(0)
__naked void arsh_31_or(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	w2 = w0;					\
	w2 s>>= 31;					\
	w2 |= 134; /* w2 becomes -1 or 134 */		\
	if w2 s> -1 goto +2;				\
	/* Branch always taken because w2 = -1 */	\
	if w2 == -1 goto +1;				\
	w0 /= 0;					\
	w0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__success __success_unpriv __retval(0)
__naked void arsh_63_or(void)
{
	/* Copy of arsh_31 with s/w/r/ */
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r2 = r0;					\
	r2 <<= 32;					\
	r2 s>>= 63;					\
	r2 |= 134; /* r2 becomes -1 or 134 */		\
	if r2 s> -1 goto +2;				\
	/* Branch always taken because w2 = -1 */	\
	if r2 == -1 goto +1;				\
	r0 /= 0;					\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
