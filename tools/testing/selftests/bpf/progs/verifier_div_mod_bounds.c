// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <limits.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

/* This file contains unit tests for signed/unsigned division and modulo
 * operations (with divisor as a constant), focusing on verifying whether
 * BPF verifier's range tracking module soundly and precisely computes
 * the results.
 */

SEC("socket")
__description("UDIV32, positive divisor")
__success __retval(0) __log_level(2)
__msg("w1 /= 3 {{.*}}; R1=scalar(smin=smin32=0,smax=umax=smax32=umax32=3,var_off=(0x0; 0x3))")
__naked void udiv32_pos_divisor(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	w1 = w0;					\
	w1 &= 8;					\
	w1 |= 1;					\
	w1 /= 3;					\
	if w1 > 3 goto l0_%=;				\
	r0 = 0;						\
	exit;						\
l0_%=:	r0 = *(u64 *)(r1 + 0);				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("UDIV32, zero divisor")
__success __retval(0) __log_level(2)
__msg("w1 /= w2 {{.*}}; R1=0 R2=0")
__naked void udiv32_zero_divisor(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	w1 = w0;					\
	w1 &= 8;					\
	w1 |= 1;					\
	w2 = 0;						\
	w1 /= w2;					\
	if w1 != 0 goto l0_%=;				\
	r0 = 0;						\
	exit;						\
l0_%=:	r0 = *(u64 *)(r1 + 0);				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("UDIV64, positive divisor")
__success __retval(0) __log_level(2)
__msg("r1 /= 3 {{.*}}; R1=scalar(smin=smin32=0,smax=umax=smax32=umax32=3,var_off=(0x0; 0x3))")
__naked void udiv64_pos_divisor(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = r0;					\
	r1 &= 8;					\
	r1 |= 1;					\
	r1 /= 3;					\
	if r1 > 3 goto l0_%=;				\
	r0 = 0;						\
	exit;						\
l0_%=:	r0 = *(u64 *)(r1 + 0);				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("UDIV64, zero divisor")
__success __retval(0) __log_level(2)
__msg("r1 /= r2 {{.*}}; R1=0 R2=0")
__naked void udiv64_zero_divisor(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = r0;					\
	r1 &= 8;					\
	r1 |= 1;					\
	r2 = 0;						\
	r1 /= r2;					\
	if r1 != 0 goto l0_%=;				\
	r0 = 0;						\
	exit;						\
l0_%=:	r0 = *(u64 *)(r1 + 0);				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("SDIV32, positive divisor, positive dividend")
__success __retval(0) __log_level(2)
__msg("w1 s/= 3 {{.*}}; R1=scalar(smin=umin=smin32=umin32=2,smax=umax=smax32=umax32=3,var_off=(0x2; 0x1))")
__naked void sdiv32_pos_divisor_1(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	w1 = w0;					\
	if w1 s< 8 goto l0_%=;				\
	if w1 s> 10 goto l0_%=;				\
	w1 s/= 3;					\
	if w1 s< 2 goto l1_%=;				\
	if w1 s> 3 goto l1_%=;				\
l0_%=:	r0 = 0;						\
	exit;						\
l1_%=:	r0 = *(u64 *)(r1 + 0);				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("SDIV32, positive divisor, negative dividend")
__success __retval(0) __log_level(2)
__msg("w1 s/= 3 {{.*}}; R1=scalar(smin=umin=umin32=0xfffffffd,smax=umax=umax32=0xfffffffe,smin32=-3,smax32=-2,var_off=(0xfffffffc; 0x3))")
__naked void sdiv32_pos_divisor_2(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	w1 = w0;					\
	if w1 s> -8 goto l0_%=;				\
	if w1 s< -10 goto l0_%=;			\
	w1 s/= 3;					\
	if w1 s< -3 goto l1_%=;				\
	if w1 s> -2 goto l1_%=;				\
l0_%=:	r0 = 0;						\
	exit;						\
l1_%=:	r0 = *(u64 *)(r1 + 0);				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("SDIV32, positive divisor, mixed sign dividend")
__success __retval(0) __log_level(2)
__msg("w1 s/= 3 {{.*}}; R1=scalar(smin=0,smax=umax=0xffffffff,smin32=-2,smax32=3,var_off=(0x0; 0xffffffff))")
__naked void sdiv32_pos_divisor_3(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	w1 = w0;					\
	if w1 s< -8 goto l0_%=;				\
	if w1 s> 10 goto l0_%=;				\
	w1 s/= 3;					\
	if w1 s< -2 goto l1_%=;				\
	if w1 s> 3 goto l1_%=;				\
l0_%=:	r0 = 0;						\
	exit;						\
l1_%=:	r0 = *(u64 *)(r1 + 0);				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("SDIV32, negative divisor, positive dividend")
__success __retval(0) __log_level(2)
__msg("w1 s/= -3 {{.*}}; R1=scalar(smin=umin=umin32=0xfffffffd,smax=umax=umax32=0xfffffffe,smin32=-3,smax32=-2,var_off=(0xfffffffc; 0x3))")
__naked void sdiv32_neg_divisor_1(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	w1 = w0;					\
	if w1 s< 8 goto l0_%=;				\
	if w1 s> 10 goto l0_%=;				\
	w1 s/= -3;					\
	if w1 s< -3 goto l1_%=;				\
	if w1 s> -2 goto l1_%=;				\
l0_%=:	r0 = 0;						\
	exit;						\
l1_%=:	r0 = *(u64 *)(r1 + 0);				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("SDIV32, negative divisor, positive dividend")
__success __retval(0) __log_level(2)
__msg("w1 s/= -3 {{.*}}; R1=scalar(smin=umin=smin32=umin32=2,smax=umax=smax32=umax32=3,var_off=(0x2; 0x1))")
__naked void sdiv32_neg_divisor_2(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	w1 = w0;					\
	if w1 s> -8 goto l0_%=;				\
	if w1 s< -10 goto l0_%=;			\
	w1 s/= -3;					\
	if w1 s< 2 goto l1_%=;				\
	if w1 s> 3 goto l1_%=;				\
l0_%=:	r0 = 0;						\
	exit;						\
l1_%=:	r0 = *(u64 *)(r1 + 0);				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("SDIV32, negative divisor, mixed sign dividend")
__success __retval(0) __log_level(2)
__msg("w1 s/= -3 {{.*}}; R1=scalar(smin=0,smax=umax=0xffffffff,smin32=-3,smax32=2,var_off=(0x0; 0xffffffff))")
__naked void sdiv32_neg_divisor_3(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	w1 = w0;					\
	if w1 s< -8 goto l0_%=;				\
	if w1 s> 10 goto l0_%=;				\
	w1 s/= -3;					\
	if w1 s< -3 goto l1_%=;				\
	if w1 s> 2 goto l1_%=;				\
l0_%=:	r0 = 0;						\
	exit;						\
l1_%=:	r0 = *(u64 *)(r1 + 0);				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("SDIV32, zero divisor")
__success __retval(0) __log_level(2)
__msg("w1 s/= w2 {{.*}}; R1=0 R2=0")
__naked void sdiv32_zero_divisor(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	w1 = w0;					\
	w1 &= 8;					\
	w1 |= 1;					\
	w2 = 0;						\
	w1 s/= w2;					\
	if w1 != 0 goto l0_%=;				\
	r0 = 0;						\
	exit;						\
l0_%=:	r0 = *(u64 *)(r1 + 0);				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("SDIV32, overflow (S32_MIN/-1)")
__success __retval(0) __log_level(2)
__msg("w1 s/= -1 {{.*}}; R1=scalar(smin=0,smax=umax=0xffffffff,var_off=(0x0; 0xffffffff))")
__naked void sdiv32_overflow_1(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	w1 = w0;					\
	w2 = %[int_min];				\
	w2 += 10;					\
	if w1 s> w2 goto l0_%=;				\
	w1 s/= -1;					\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(int_min, INT_MIN),
	  __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("SDIV32, overflow (S32_MIN/-1), constant dividend")
__success __retval(0) __log_level(2)
__msg("w1 s/= -1 {{.*}}; R1=0x80000000")
__naked void sdiv32_overflow_2(void)
{
	asm volatile ("					\
	w1 = %[int_min];				\
	w1 s/= -1;					\
	if w1 != %[int_min] goto l0_%=;			\
	r0 = 0;						\
	exit;						\
l0_%=:	r0 = *(u64 *)(r1 + 0);				\
	exit;						\
"	:
	: __imm_const(int_min, INT_MIN)
	: __clobber_all);
}

SEC("socket")
__description("SDIV64, positive divisor, positive dividend")
__success __retval(0) __log_level(2)
__msg("r1 s/= 3 {{.*}}; R1=scalar(smin=umin=smin32=umin32=2,smax=umax=smax32=umax32=3,var_off=(0x2; 0x1))")
__naked void sdiv64_pos_divisor_1(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = r0;					\
	if r1 s< 8 goto l0_%=;				\
	if r1 s> 10 goto l0_%=;				\
	r1 s/= 3;					\
	if r1 s< 2 goto l1_%=;				\
	if r1 s> 3 goto l1_%=;				\
l0_%=:	r0 = 0;						\
	exit;						\
l1_%=:	r0 = *(u64 *)(r1 + 0);				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("SDIV64, positive divisor, negative dividend")
__success __retval(0) __log_level(2)
__msg("r1 s/= 3 {{.*}}; R1=scalar(smin=smin32=-3,smax=smax32=-2,umin=0xfffffffffffffffd,umax=0xfffffffffffffffe,umin32=0xfffffffd,umax32=0xfffffffe,var_off=(0xfffffffffffffffc; 0x3))")
__naked void sdiv64_pos_divisor_2(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = r0;					\
	if r1 s> -8 goto l0_%=;				\
	if r1 s< -10 goto l0_%=;			\
	r1 s/= 3;					\
	if r1 s< -3 goto l1_%=;				\
	if r1 s> -2 goto l1_%=;				\
l0_%=:	r0 = 0;						\
	exit;						\
l1_%=:	r0 = *(u64 *)(r1 + 0);				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("SDIV64, positive divisor, mixed sign dividend")
__success __retval(0) __log_level(2)
__msg("r1 s/= 3 {{.*}}; R1=scalar(smin=smin32=-2,smax=smax32=3)")
__naked void sdiv64_pos_divisor_3(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = r0;					\
	if r1 s< -8 goto l0_%=;				\
	if r1 s> 10 goto l0_%=;				\
	r1 s/= 3;					\
	if r1 s< -2 goto l1_%=;				\
	if r1 s> 3 goto l1_%=;				\
l0_%=:	r0 = 0;						\
	exit;						\
l1_%=:	r0 = *(u64 *)(r1 + 0);				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("SDIV64, negative divisor, positive dividend")
__success __retval(0) __log_level(2)
__msg("r1 s/= -3 {{.*}}; R1=scalar(smin=smin32=-3,smax=smax32=-2,umin=0xfffffffffffffffd,umax=0xfffffffffffffffe,umin32=0xfffffffd,umax32=0xfffffffe,var_off=(0xfffffffffffffffc; 0x3))")
__naked void sdiv64_neg_divisor_1(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = r0;					\
	if r1 s< 8 goto l0_%=;				\
	if r1 s> 10 goto l0_%=;				\
	r1 s/= -3;					\
	if r1 s< -3 goto l1_%=;				\
	if r1 s> -2 goto l1_%=;				\
l0_%=:	r0 = 0;						\
	exit;						\
l1_%=:	r0 = *(u64 *)(r1 + 0);				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("SDIV64, negative divisor, positive dividend")
__success __retval(0) __log_level(2)
__msg("r1 s/= -3 {{.*}}; R1=scalar(smin=umin=smin32=umin32=2,smax=umax=smax32=umax32=3,var_off=(0x2; 0x1))")
__naked void sdiv64_neg_divisor_2(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = r0;					\
	if r1 s> -8 goto l0_%=;				\
	if r1 s< -10 goto l0_%=;			\
	r1 s/= -3;					\
	if r1 s< 2 goto l1_%=;				\
	if r1 s> 3 goto l1_%=;				\
l0_%=:	r0 = 0;						\
	exit;						\
l1_%=:	r0 = *(u64 *)(r1 + 0);				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("SDIV64, negative divisor, mixed sign dividend")
__success __retval(0) __log_level(2)
__msg("r1 s/= -3 {{.*}}; R1=scalar(smin=smin32=-3,smax=smax32=2)")
__naked void sdiv64_neg_divisor_3(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = r0;					\
	if r1 s< -8 goto l0_%=;				\
	if r1 s> 10 goto l0_%=;				\
	r1 s/= -3;					\
	if r1 s< -3 goto l1_%=;				\
	if r1 s> 2 goto l1_%=;				\
l0_%=:	r0 = 0;						\
	exit;						\
l1_%=:	r0 = *(u64 *)(r1 + 0);				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("SDIV64, zero divisor")
__success __retval(0) __log_level(2)
__msg("r1 s/= r2 {{.*}}; R1=0 R2=0")
__naked void sdiv64_zero_divisor(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = r0;					\
	r1 &= 8;					\
	r1 |= 1;					\
	r2 = 0;						\
	r1 s/= r2;					\
	if r1 != 0 goto l0_%=;				\
	r0 = 0;						\
	exit;						\
l0_%=:	r0 = *(u64 *)(r1 + 0);				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("SDIV64, overflow (S64_MIN/-1)")
__success __retval(0) __log_level(2)
__msg("r1 s/= -1 {{.*}}; R1=scalar()")
__naked void sdiv64_overflow_1(void)
{
	asm volatile ("					\
	call %[bpf_ktime_get_ns];			\
	r1 = r0;					\
	r2 = %[llong_min] ll;				\
	r2 += 10;					\
	if r1 s> r2 goto l0_%=;				\
	r1 s/= -1;					\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(llong_min, LLONG_MIN),
	  __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

SEC("socket")
__description("SDIV64, overflow (S64_MIN/-1), constant dividend")
__success __retval(0) __log_level(2)
__msg("r1 s/= -1 {{.*}}; R1=0x8000000000000000")
__naked void sdiv64_overflow_2(void)
{
	asm volatile ("					\
	r1 = %[llong_min] ll;				\
	r1 s/= -1;					\
	r2 = %[llong_min] ll;				\
	if r1 != r2 goto l0_%=;				\
	r0 = 0;						\
	exit;						\
l0_%=:	r0 = *(u64 *)(r1 + 0);				\
	exit;						\
"	:
	: __imm_const(llong_min, LLONG_MIN)
	: __clobber_all);
}

SEC("socket")
__description("UMOD32, positive divisor")
__success __retval(0) __log_level(2)
__msg("w1 %= 3 {{.*}}; R1=scalar(smin=smin32=0,smax=umax=smax32=umax32=2,var_off=(0x0; 0x3))")
__naked void umod32_pos_divisor(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	w1 = w0;					\
	w1 &= 8;					\
	w1 |= 1;					\
	w1 %%= 3;					\
	if w1 > 3 goto l0_%=;				\
	r0 = 0;						\
	exit;						\
l0_%=:	r0 = *(u64 *)(r1 + 0);				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("UMOD32, positive divisor, small dividend")
__success __retval(0) __log_level(2)
__msg("w1 %= 10 {{.*}}; R1=scalar(smin=umin=smin32=umin32=1,smax=umax=smax32=umax32=9,var_off=(0x1; 0x8))")
__naked void umod32_pos_divisor_unchanged(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	w1 = w0;					\
	w1 &= 8;					\
	w1 |= 1;					\
	w1 %%= 10;					\
	if w1 < 1 goto l0_%=;				\
	if w1 > 9 goto l0_%=;				\
	if w1 & 1 != 1 goto l0_%=;			\
	r0 = 0;						\
	exit;						\
l0_%=:	r0 = *(u64 *)(r1 + 0);				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("UMOD32, zero divisor")
__success __retval(0) __log_level(2)
__msg("w1 %= w2 {{.*}}; R1=scalar(smin=umin=smin32=umin32=1,smax=umax=smax32=umax32=9,var_off=(0x1; 0x8)) R2=0")
__naked void umod32_zero_divisor(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	w1 = w0;					\
	w1 &= 8;					\
	w1 |= 1;					\
	w2 = 0;						\
	w1 %%= w2;					\
	if w1 < 1 goto l0_%=;				\
	if w1 > 9 goto l0_%=;				\
	if w1 & 1 != 1 goto l0_%=;			\
	r0 = 0;						\
	exit;						\
l0_%=:	r0 = *(u64 *)(r1 + 0);				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("UMOD64, positive divisor")
__success __retval(0) __log_level(2)
__msg("r1 %= 3 {{.*}}; R1=scalar(smin=smin32=0,smax=umax=smax32=umax32=2,var_off=(0x0; 0x3))")
__naked void umod64_pos_divisor(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = r0;					\
	r1 &= 8;					\
	r1 |= 1;					\
	r1 %%= 3;					\
	if r1 > 3 goto l0_%=;				\
	r0 = 0;						\
	exit;						\
l0_%=:	r0 = *(u64 *)(r1 + 0);				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("UMOD64, positive divisor, small dividend")
__success __retval(0) __log_level(2)
__msg("r1 %= 10 {{.*}}; R1=scalar(smin=umin=smin32=umin32=1,smax=umax=smax32=umax32=9,var_off=(0x1; 0x8))")
__naked void umod64_pos_divisor_unchanged(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = r0;					\
	r1 &= 8;					\
	r1 |= 1;					\
	r1 %%= 10;					\
	if r1 < 1 goto l0_%=;				\
	if r1 > 9 goto l0_%=;				\
	if r1 & 1 != 1 goto l0_%=;			\
	r0 = 0;						\
	exit;						\
l0_%=:	r0 = *(u64 *)(r1 + 0);				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("UMOD64, zero divisor")
__success __retval(0) __log_level(2)
__msg("r1 %= r2 {{.*}}; R1=scalar(smin=umin=smin32=umin32=1,smax=umax=smax32=umax32=9,var_off=(0x1; 0x8)) R2=0")
__naked void umod64_zero_divisor(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = r0;					\
	r1 &= 8;					\
	r1 |= 1;					\
	r2 = 0;						\
	r1 %%= r2;					\
	if r1 < 1 goto l0_%=;				\
	if r1 > 9 goto l0_%=;				\
	if r1 & 1 != 1 goto l0_%=;			\
	r0 = 0;						\
	exit;						\
l0_%=:	r0 = *(u64 *)(r1 + 0);				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("SMOD32, positive divisor, positive dividend")
__success __retval(0) __log_level(2)
__msg("w1 s%= 3 {{.*}}; R1=scalar(smin=smin32=0,smax=umax=smax32=umax32=2,var_off=(0x0; 0x3))")
__naked void smod32_pos_divisor_1(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	w1 = w0;					\
	if w1 s< 8 goto l0_%=;				\
	if w1 s> 10 goto l0_%=;				\
	w1 s%%= 3;					\
	if w1 s< 0 goto l1_%=;				\
	if w1 s> 2 goto l1_%=;				\
l0_%=:	r0 = 0;						\
	exit;						\
l1_%=:	r0 = *(u64 *)(r1 + 0);				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("SMOD32, positive divisor, negative dividend")
__success __retval(0) __log_level(2)
__msg("w1 s%= 3 {{.*}}; R1=scalar(smin=0,smax=umax=0xffffffff,smin32=-2,smax32=0,var_off=(0x0; 0xffffffff))")
__naked void smod32_pos_divisor_2(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	w1 = w0;					\
	if w1 s> -8 goto l0_%=;				\
	if w1 s< -10 goto l0_%=;			\
	w1 s%%= 3;					\
	if w1 s< -2 goto l1_%=;				\
	if w1 s> 0 goto l1_%=;				\
l0_%=:	r0 = 0;						\
	exit;						\
l1_%=:	r0 = *(u64 *)(r1 + 0);				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("SMOD32, positive divisor, mixed sign dividend")
__success __retval(0) __log_level(2)
__msg("w1 s%= 3 {{.*}}; R1=scalar(smin=0,smax=umax=0xffffffff,smin32=-2,smax32=2,var_off=(0x0; 0xffffffff))")
__naked void smod32_pos_divisor_3(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	w1 = w0;					\
	if w1 s< -8 goto l0_%=;				\
	if w1 s> 10 goto l0_%=;				\
	w1 s%%= 3;					\
	if w1 s< -2 goto l1_%=;				\
	if w1 s> 2 goto l1_%=;				\
l0_%=:	r0 = 0;						\
	exit;						\
l1_%=:	r0 = *(u64 *)(r1 + 0);				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("SMOD32, positive divisor, small dividend")
__success __retval(0) __log_level(2)
__msg("w1 s%= 11 {{.*}}; R1=scalar(smin=0,smax=umax=0xffffffff,smin32=-8,smax32=10,var_off=(0x0; 0xffffffff))")
__naked void smod32_pos_divisor_unchanged(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	w1 = w0;					\
	if w1 s< -8 goto l0_%=;				\
	if w1 s> 10 goto l0_%=;				\
	w1 s%%= 11;					\
	if w1 s< -8 goto l1_%=;				\
	if w1 s> 10 goto l1_%=;				\
l0_%=:	r0 = 0;						\
	exit;						\
l1_%=:	r0 = *(u64 *)(r1 + 0);				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("SMOD32, negative divisor, positive dividend")
__success __retval(0) __log_level(2)
__msg("w1 s%= -3 {{.*}}; R1=scalar(smin=smin32=0,smax=umax=smax32=umax32=2,var_off=(0x0; 0x3))")
__naked void smod32_neg_divisor_1(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	w1 = w0;					\
	if w1 s< 8 goto l0_%=;				\
	if w1 s> 10 goto l0_%=;				\
	w1 s%%= -3;					\
	if w1 s< 0 goto l1_%=;				\
	if w1 s> 2 goto l1_%=;				\
l0_%=:	r0 = 0;						\
	exit;						\
l1_%=:	r0 = *(u64 *)(r1 + 0);				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("SMOD32, negative divisor, negative dividend")
__success __retval(0) __log_level(2)
__msg("w1 s%= -3 {{.*}}; R1=scalar(smin=0,smax=umax=0xffffffff,smin32=-2,smax32=0,var_off=(0x0; 0xffffffff))")
__naked void smod32_neg_divisor_2(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	w1 = w0;					\
	if w1 s> -8 goto l0_%=;				\
	if w1 s< -10 goto l0_%=;			\
	w1 s%%= -3;					\
	if w1 s< -2 goto l1_%=;				\
	if w1 s> 0 goto l1_%=;				\
l0_%=:	r0 = 0;						\
	exit;						\
l1_%=:	r0 = *(u64 *)(r1 + 0);				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("SMOD32, negative divisor, mixed sign dividend")
__success __retval(0) __log_level(2)
__msg("w1 s%= -3 {{.*}}; R1=scalar(smin=0,smax=umax=0xffffffff,smin32=-2,smax32=2,var_off=(0x0; 0xffffffff))")
__naked void smod32_neg_divisor_3(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	w1 = w0;					\
	if w1 s< -8 goto l0_%=;				\
	if w1 s> 10 goto l0_%=;				\
	w1 s%%= -3;					\
	if w1 s< -2 goto l1_%=;				\
	if w1 s> 2 goto l1_%=;				\
l0_%=:	r0 = 0;						\
	exit;						\
l1_%=:	r0 = *(u64 *)(r1 + 0);				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("SMOD32, negative divisor, small dividend")
__success __retval(0) __log_level(2)
__msg("w1 s%= -11 {{.*}}; R1=scalar(smin=0,smax=umax=0xffffffff,smin32=-8,smax32=10,var_off=(0x0; 0xffffffff))")
__naked void smod32_neg_divisor_unchanged(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	w1 = w0;					\
	if w1 s< -8 goto l0_%=;				\
	if w1 s> 10 goto l0_%=;				\
	w1 s%%= -11;					\
	if w1 s< -8 goto l1_%=;				\
	if w1 s> 10 goto l1_%=;				\
l0_%=:	r0 = 0;						\
	exit;						\
l1_%=:	r0 = *(u64 *)(r1 + 0);				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("SMOD32, zero divisor")
__success __retval(0) __log_level(2)
__msg("w1 s%= w2 {{.*}}; R1=scalar(smin=0,smax=umax=0xffffffff,smin32=-8,smax32=10,var_off=(0x0; 0xffffffff)) R2=0")
__naked void smod32_zero_divisor(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	w1 = w0;					\
	if w1 s< -8 goto l0_%=;				\
	if w1 s> 10 goto l0_%=;				\
	w2 = 0;						\
	w1 s%%= w2;					\
	if w1 s< -8 goto l1_%=;				\
	if w1 s> 10 goto l1_%=;				\
l0_%=:	r0 = 0;						\
	exit;						\
l1_%=:	r0 = *(u64 *)(r1 + 0);				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("SMOD32, overflow (S32_MIN%-1)")
__success __retval(0) __log_level(2)
__msg("w1 s%= -1 {{.*}}; R1=0")
__naked void smod32_overflow_1(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	w1 = w0;					\
	w2 = %[int_min];				\
	w2 += 10;					\
	if w1 s> w2 goto l0_%=;				\
	w1 s%%= -1;					\
	if w1 != 0 goto l1_%=;				\
l0_%=:	r0 = 0;						\
	exit;						\
l1_%=:	r0 = *(u64 *)(r1 + 0);				\
	exit;						\
"	:
	: __imm_const(int_min, INT_MIN),
	  __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("SMOD32, overflow (S32_MIN%-1), constant dividend")
__success __retval(0) __log_level(2)
__msg("w1 s%= -1 {{.*}}; R1=0")
__naked void smod32_overflow_2(void)
{
	asm volatile ("					\
	w1 = %[int_min];				\
	w1 s%%= -1;					\
	if w1 != 0 goto l0_%=;				\
	r0 = 0;						\
	exit;						\
l0_%=:	r0 = *(u64 *)(r1 + 0);				\
	exit;						\
"	:
	: __imm_const(int_min, INT_MIN)
	: __clobber_all);
}

SEC("socket")
__description("SMOD64, positive divisor, positive dividend")
__success __retval(0) __log_level(2)
__msg("r1 s%= 3 {{.*}}; R1=scalar(smin=smin32=0,smax=umax=smax32=umax32=2,var_off=(0x0; 0x3))")
__naked void smod64_pos_divisor_1(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = r0;					\
	if r1 s< 8 goto l0_%=;				\
	if r1 s> 10 goto l0_%=;				\
	r1 s%%= 3;					\
	if r1 s< 0 goto l1_%=;				\
	if r1 s> 2 goto l1_%=;				\
l0_%=:	r0 = 0;						\
	exit;						\
l1_%=:	r0 = *(u64 *)(r1 + 0);				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("SMOD64, positive divisor, negative dividend")
__success __retval(0) __log_level(2)
__msg("r1 s%= 3 {{.*}}; R1=scalar(smin=smin32=-2,smax=smax32=0)")
__naked void smod64_pos_divisor_2(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = r0;					\
	if r1 s> -8 goto l0_%=;				\
	if r1 s< -10 goto l0_%=;			\
	r1 s%%= 3;					\
	if r1 s< -2 goto l1_%=;				\
	if r1 s> 0 goto l1_%=;				\
l0_%=:	r0 = 0;						\
	exit;						\
l1_%=:	r0 = *(u64 *)(r1 + 0);				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("SMOD64, positive divisor, mixed sign dividend")
__success __retval(0) __log_level(2)
__msg("r1 s%= 3 {{.*}}; R1=scalar(smin=smin32=-2,smax=smax32=2)")
__naked void smod64_pos_divisor_3(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = r0;					\
	if r1 s< -8 goto l0_%=;				\
	if r1 s> 10 goto l0_%=;				\
	r1 s%%= 3;					\
	if r1 s< -2 goto l1_%=;				\
	if r1 s> 2 goto l1_%=;				\
l0_%=:	r0 = 0;						\
	exit;						\
l1_%=:	r0 = *(u64 *)(r1 + 0);				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("SMOD64, positive divisor, small dividend")
__success __retval(0) __log_level(2)
__msg("r1 s%= 11 {{.*}}; R1=scalar(smin=smin32=-8,smax=smax32=10)")
__naked void smod64_pos_divisor_unchanged(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = r0;					\
	if r1 s< -8 goto l0_%=;				\
	if r1 s> 10 goto l0_%=;				\
	r1 s%%= 11;					\
	if r1 s< -8 goto l1_%=;				\
	if r1 s> 10 goto l1_%=;				\
l0_%=:	r0 = 0;						\
	exit;						\
l1_%=:	r0 = *(u64 *)(r1 + 0);				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("SMOD64, negative divisor, positive dividend")
__success __retval(0) __log_level(2)
__msg("r1 s%= -3 {{.*}}; R1=scalar(smin=smin32=0,smax=umax=smax32=umax32=2,var_off=(0x0; 0x3))")
__naked void smod64_neg_divisor_1(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = r0;					\
	if r1 s< 8 goto l0_%=;				\
	if r1 s> 10 goto l0_%=;				\
	r1 s%%= -3;					\
	if r1 s< 0 goto l1_%=;				\
	if r1 s> 2 goto l1_%=;				\
l0_%=:	r0 = 0;						\
	exit;						\
l1_%=:	r0 = *(u64 *)(r1 + 0);				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("SMOD64, negative divisor, negative dividend")
__success __retval(0) __log_level(2)
__msg("r1 s%= -3 {{.*}}; R1=scalar(smin=smin32=-2,smax=smax32=0)")
__naked void smod64_neg_divisor_2(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = r0;					\
	if r1 s> -8 goto l0_%=;				\
	if r1 s< -10 goto l0_%=;			\
	r1 s%%= -3;					\
	if r1 s< -2 goto l1_%=;				\
	if r1 s> 0 goto l1_%=;				\
l0_%=:	r0 = 0;						\
	exit;						\
l1_%=:	r0 = *(u64 *)(r1 + 0);				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("SMOD64, negative divisor, mixed sign dividend")
__success __retval(0) __log_level(2)
__msg("r1 s%= -3 {{.*}}; R1=scalar(smin=smin32=-2,smax=smax32=2)")
__naked void smod64_neg_divisor_3(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = r0;					\
	if r1 s< -8 goto l0_%=;				\
	if r1 s> 10 goto l0_%=;				\
	r1 s%%= -3;					\
	if r1 s< -2 goto l1_%=;				\
	if r1 s> 2 goto l1_%=;				\
l0_%=:	r0 = 0;						\
	exit;						\
l1_%=:	r0 = *(u64 *)(r1 + 0);				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("SMOD64, negative divisor, small dividend")
__success __retval(0) __log_level(2)
__msg("r1 s%= -11 {{.*}}; R1=scalar(smin=smin32=-8,smax=smax32=10)")
__naked void smod64_neg_divisor_unchanged(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = r0;					\
	if r1 s< -8 goto l0_%=;				\
	if r1 s> 10 goto l0_%=;				\
	r1 s%%= -11;					\
	if r1 s< -8 goto l1_%=;				\
	if r1 s> 10 goto l1_%=;				\
l0_%=:	r0 = 0;						\
	exit;						\
l1_%=:	r0 = *(u64 *)(r1 + 0);				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("SMOD64, zero divisor")
__success __retval(0) __log_level(2)
__msg("r1 s%= r2 {{.*}}; R1=scalar(smin=smin32=-8,smax=smax32=10) R2=0")
__naked void smod64_zero_divisor(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = r0;					\
	if r1 s< -8 goto l0_%=;				\
	if r1 s> 10 goto l0_%=;				\
	r2 = 0;						\
	r1 s%%= r2;					\
	if r1 s< -8 goto l1_%=;				\
	if r1 s> 10 goto l1_%=;				\
l0_%=:	r0 = 0;						\
	exit;						\
l1_%=:	r0 = *(u64 *)(r1 + 0);				\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("SMOD64, overflow (S64_MIN%-1)")
__success __retval(0) __log_level(2)
__msg("r1 s%= -1 {{.*}}; R1=0")
__naked void smod64_overflow_1(void)
{
	asm volatile ("					\
	call %[bpf_ktime_get_ns];			\
	r1 = r0;					\
	r2 = %[llong_min] ll;				\
	r2 += 10;					\
	if r1 s> r2 goto l0_%=;				\
	r1 s%%= -1;					\
	if r1 != 0 goto l1_%=;				\
l0_%=:	r0 = 0;						\
	exit;						\
l1_%=:	r0 = *(u64 *)(r1 + 0);				\
	exit;						\
"	:
	: __imm_const(llong_min, LLONG_MIN),
	  __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

SEC("socket")
__description("SMOD64, overflow (S64_MIN%-1), constant dividend")
__success __retval(0) __log_level(2)
__msg("r1 s%= -1 {{.*}}; R1=0")
__naked void smod64_overflow_2(void)
{
	asm volatile ("					\
	r1 = %[llong_min] ll;				\
	r1 s%%= -1;					\
	if r1 != 0 goto l0_%=;				\
	r0 = 0;						\
	exit;						\
l0_%=:	r0 = *(u64 *)(r1 + 0);				\
	exit;						\
"	:
	: __imm_const(llong_min, LLONG_MIN)
	: __clobber_all);
}
