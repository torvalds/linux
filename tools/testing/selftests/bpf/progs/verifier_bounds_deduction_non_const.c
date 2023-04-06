// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

SEC("socket")
__description("check deducing bounds from non-const, jmp64, <non_const> == <const>, 1")
__success __retval(0)
__naked void deducing_bounds_from_non_const_1(void)
{
	asm volatile ("					\
	call %[bpf_ktime_get_ns];			\
	if r0 < 3 goto l0_%=;				\
	r2 = 2;						\
	if r0 == r2 goto l1_%=;				\
l0_%=:							\
	r0 = 0;						\
	exit;						\
l1_%=:							\
	r0 -= r1;					\
	exit;						\
"	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

SEC("socket")
__description("check deducing bounds from non-const, jmp64, <non_const> == <const>, 2")
__success __retval(0)
__naked void deducing_bounds_from_non_const_2(void)
{
	asm volatile ("					\
	call %[bpf_ktime_get_ns];			\
	if r0 > 3 goto l0_%=;				\
	r2 = 4;						\
	if r0 == r2 goto l1_%=;				\
l0_%=:							\
	r0 = 0;						\
	exit;						\
l1_%=:							\
	r0 -= r1;					\
	exit;						\
"	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

SEC("socket")
__description("check deducing bounds from non-const, jmp64, <non_const> != <const>, 1")
__success __retval(0)
__naked void deducing_bounds_from_non_const_3(void)
{
	asm volatile ("					\
	call %[bpf_ktime_get_ns];			\
	if r0 < 3 goto l0_%=;				\
	r2 = 2;						\
	if r0 != r2 goto l0_%=;				\
	goto l1_%=;					\
l0_%=:							\
	r0 = 0;						\
	exit;						\
l1_%=:							\
	r0 -= r1;					\
	exit;						\
"	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

SEC("socket")
__description("check deducing bounds from non-const, jmp64, <non_const> != <const>, 2")
__success __retval(0)
__naked void deducing_bounds_from_non_const_4(void)
{
	asm volatile ("					\
	call %[bpf_ktime_get_ns];			\
	if r0 > 3 goto l0_%=;				\
	r2 = 4;						\
	if r0 != r2 goto l0_%=;				\
	goto l1_%=;					\
l0_%=:							\
	r0 = 0;						\
	exit;						\
l1_%=:							\
	r0 -= r1;					\
	exit;						\
"	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

SEC("socket")
__description("check deducing bounds from non-const, jmp32, <non_const> == <const>, 1")
__success __retval(0)
__naked void deducing_bounds_from_non_const_5(void)
{
	asm volatile ("					\
	call %[bpf_ktime_get_ns];			\
	if w0 < 4 goto l0_%=;				\
	w2 = 3;						\
	if w0 == w2 goto l1_%=;				\
l0_%=:							\
	r0 = 0;						\
	exit;						\
l1_%=:							\
	r0 -= r1;					\
	exit;						\
"	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

SEC("socket")
__description("check deducing bounds from non-const, jmp32, <non_const> == <const>, 2")
__success __retval(0)
__naked void deducing_bounds_from_non_const_6(void)
{
	asm volatile ("					\
	call %[bpf_ktime_get_ns];			\
	if w0 > 4 goto l0_%=;				\
	w2 = 5;						\
	if w0 == w2 goto l1_%=;				\
l0_%=:							\
	r0 = 0;						\
	exit;						\
l1_%=:							\
	r0 -= r1;					\
	exit;						\
"	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

SEC("socket")
__description("check deducing bounds from non-const, jmp32, <non_const> != <const>, 1")
__success __retval(0)
__naked void deducing_bounds_from_non_const_7(void)
{
	asm volatile ("					\
	call %[bpf_ktime_get_ns];			\
	if w0 < 3 goto l0_%=;				\
	w2 = 2;						\
	if w0 != w2 goto l0_%=;				\
	goto l1_%=;					\
l0_%=:							\
	r0 = 0;						\
	exit;						\
l1_%=:							\
	r0 -= r1;					\
	exit;						\
"	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

SEC("socket")
__description("check deducing bounds from non-const, jmp32, <non_const> != <const>, 2")
__success __retval(0)
__naked void deducing_bounds_from_non_const_8(void)
{
	asm volatile ("					\
	call %[bpf_ktime_get_ns];			\
	if w0 > 3 goto l0_%=;				\
	w2 = 4;						\
	if w0 != w2 goto l0_%=;				\
	goto l1_%=;					\
l0_%=:							\
	r0 = 0;						\
	exit;						\
l1_%=:							\
	r0 -= r1;					\
	exit;						\
"	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

SEC("socket")
__description("check deducing bounds from non-const, jmp64, <const> > <non_const>, 1")
__success __retval(0)
__naked void deducing_bounds_from_non_const_9(void)
{
	asm volatile ("					\
	call %[bpf_ktime_get_ns];			\
	r2 = 0;						\
	if r2 > r0 goto l0_%=;				\
	r0 = 0;						\
	exit;						\
l0_%=:							\
	r0 -= r1;					\
	exit;						\
"	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

SEC("socket")
__description("check deducing bounds from non-const, jmp64, <const> > <non_const>, 2")
__success __retval(0)
__naked void deducing_bounds_from_non_const_10(void)
{
	asm volatile ("					\
	call %[bpf_ktime_get_ns];			\
	if r0 < 4 goto l0_%=;				\
	r2 = 4;						\
	if r2 > r0 goto l1_%=;				\
l0_%=:							\
	r0 = 0;						\
	exit;						\
l1_%=:							\
	r0 -= r1;					\
	exit;						\
"	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

SEC("socket")
__description("check deducing bounds from non-const, jmp64, <const> >= <non_const>")
__success __retval(0)
__naked void deducing_bounds_from_non_const_11(void)
{
	asm volatile ("					\
	call %[bpf_ktime_get_ns];			\
	if r0 < 4 goto l0_%=;				\
	r2 = 3;						\
	if r2 >= r0 goto l1_%=;				\
l0_%=:							\
	r0 = 0;						\
	exit;						\
l1_%=:							\
	r0 -= r1;					\
	exit;						\
"	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

SEC("socket")
__description("check deducing bounds from non-const, jmp64, <const> < <non_const>")
__success __retval(0)
__naked void deducing_bounds_from_non_const_12(void)
{
	asm volatile ("					\
	call %[bpf_ktime_get_ns];			\
	if r0 > 4 goto l0_%=;				\
	r2 = 4;						\
	if r2 < r0 goto l1_%=;				\
l0_%=:							\
	r0 = 0;						\
	exit;						\
l1_%=:							\
	r0 -= r1;					\
	exit;						\
"	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

SEC("socket")
__description("check deducing bounds from non-const, jmp64, <const> <= <non_const>")
__success __retval(0)
__naked void deducing_bounds_from_non_const_13(void)
{
	asm volatile ("					\
	call %[bpf_ktime_get_ns];			\
	if r0 >= 4 goto l0_%=;				\
	r2 = 4;						\
	if r2 <= r0 goto l1_%=;				\
l0_%=:							\
	r0 = 0;						\
	exit;						\
l1_%=:							\
	r0 -= r1;					\
	exit;						\
"	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

SEC("socket")
__description("check deducing bounds from non-const, jmp64, <const> == <non_const>")
__success __retval(0)
__naked void deducing_bounds_from_non_const_14(void)
{
	asm volatile ("					\
	call %[bpf_ktime_get_ns];			\
	if r0 < 3 goto l0_%=;				\
	r2 = 2;						\
	if r2 == r0 goto l1_%=;				\
l0_%=:							\
	r0 = 0;						\
	exit;						\
l1_%=:							\
	r0 -= r1;					\
	exit;						\
"	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

SEC("socket")
__description("check deducing bounds from non-const, jmp64, <const> s> <non_const>")
__success __retval(0)
__naked void deducing_bounds_from_non_const_15(void)
{
	asm volatile ("					\
	call %[bpf_ktime_get_ns];			\
	if r0 s< 4 goto l0_%=;				\
	r2 = 4;						\
	if r2 s> r0 goto l1_%=;				\
l0_%=:							\
	r0 = 0;						\
	exit;						\
l1_%=:							\
	r0 -= r1;					\
	exit;						\
"	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

SEC("socket")
__description("check deducing bounds from non-const, jmp64, <const> s>= <non_const>")
__success __retval(0)
__naked void deducing_bounds_from_non_const_16(void)
{
	asm volatile ("					\
	call %[bpf_ktime_get_ns];			\
	if r0 s< 4 goto l0_%=;				\
	r2 = 3;						\
	if r2 s>= r0 goto l1_%=;				\
l0_%=:							\
	r0 = 0;						\
	exit;						\
l1_%=:							\
	r0 -= r1;					\
	exit;						\
"	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

SEC("socket")
__description("check deducing bounds from non-const, jmp64, <const> s< <non_const>")
__success __retval(0)
__naked void deducing_bounds_from_non_const_17(void)
{
	asm volatile ("					\
	call %[bpf_ktime_get_ns];			\
	if r0 s> 4 goto l0_%=;				\
	r2 = 4;						\
	if r2 s< r0 goto l1_%=;				\
l0_%=:							\
	r0 = 0;						\
	exit;						\
l1_%=:							\
	r0 -= r1;					\
	exit;						\
"	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

SEC("socket")
__description("check deducing bounds from non-const, jmp64, <const> s<= <non_const>")
__success __retval(0)
__naked void deducing_bounds_from_non_const_18(void)
{
	asm volatile ("					\
	call %[bpf_ktime_get_ns];			\
	if r0 s> 4 goto l0_%=;				\
	r2 = 5;						\
	if r2 s<= r0 goto l1_%=;				\
l0_%=:							\
	r0 = 0;						\
	exit;						\
l1_%=:							\
	r0 -= r1;					\
	exit;						\
"	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

SEC("socket")
__description("check deducing bounds from non-const, jmp64, <const> != <non_const>")
__success __retval(0)
__naked void deducing_bounds_from_non_const_19(void)
{
	asm volatile ("					\
	call %[bpf_ktime_get_ns];			\
	if r0 < 3 goto l0_%=;				\
	r2 = 2;						\
	if r2 != r0 goto l0_%=;				\
	goto l1_%=;					\
l0_%=:							\
	r0 = 0;						\
	exit;						\
l1_%=:							\
	r0 -= r1;					\
	exit;						\
"	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

SEC("socket")
__description("check deducing bounds from non-const, jmp32, <const> > <non_const>, 1")
__success __retval(0)
__naked void deducing_bounds_from_non_const_20(void)
{
	asm volatile ("					\
	call %[bpf_ktime_get_ns];			\
	w2 = 0;						\
	if w2 > w0 goto l0_%=;				\
	r0 = 0;						\
	exit;						\
l0_%=:							\
	r0 -= r1;					\
	exit;						\
"	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

SEC("socket")
__description("check deducing bounds from non-const, jmp32, <const> > <non_const>, 2")
__success __retval(0)
__naked void deducing_bounds_from_non_const_21(void)
{
	asm volatile ("					\
	call %[bpf_ktime_get_ns];			\
	if w0 < 4 goto l0_%=;				\
	w2 = 4;						\
	if w2 > w0 goto l1_%=;				\
l0_%=:							\
	r0 = 0;						\
	exit;						\
l1_%=:							\
	r0 -= r1;					\
	exit;						\
"	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

SEC("socket")
__description("check deducing bounds from non-const, jmp32, <const> >= <non_const>")
__success __retval(0)
__naked void deducing_bounds_from_non_const_22(void)
{
	asm volatile ("					\
	call %[bpf_ktime_get_ns];			\
	if w0 < 4 goto l0_%=;				\
	w2 = 3;						\
	if w2 >= w0 goto l1_%=;				\
l0_%=:							\
	r0 = 0;						\
	exit;						\
l1_%=:							\
	r0 -= r1;					\
	exit;						\
"	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

SEC("socket")
__description("check deducing bounds from non-const, jmp32, <const> < <non_const>")
__success __retval(0)
__naked void deducing_bounds_from_non_const_23(void)
{
	asm volatile ("					\
	call %[bpf_ktime_get_ns];			\
	if w0 > 4 goto l0_%=;				\
	w2 = 4;						\
	if w2 < w0 goto l1_%=;				\
l0_%=:							\
	r0 = 0;						\
	exit;						\
l1_%=:							\
	r0 -= r1;					\
	exit;						\
"	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

SEC("socket")
__description("check deducing bounds from non-const, jmp32, <const> <= <non_const>")
__success __retval(0)
__naked void deducing_bounds_from_non_const_24(void)
{
	asm volatile ("					\
	call %[bpf_ktime_get_ns];			\
	if w0 >= 4 goto l0_%=;				\
	w2 = 4;						\
	if w2 <= w0 goto l1_%=;				\
l0_%=:							\
	r0 = 0;						\
	exit;						\
l1_%=:							\
	r0 -= r1;					\
	exit;						\
"	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

SEC("socket")
__description("check deducing bounds from non-const, jmp32, <const> == <non_const>")
__success __retval(0)
__naked void deducing_bounds_from_non_const_25(void)
{
	asm volatile ("					\
	call %[bpf_ktime_get_ns];			\
	if w0 < 4 goto l0_%=;				\
	w2 = 3;						\
	if w2 == w0 goto l1_%=;				\
l0_%=:							\
	r0 = 0;						\
	exit;						\
l1_%=:							\
	r0 -= r1;					\
	exit;						\
"	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

SEC("socket")
__description("check deducing bounds from non-const, jmp32, <const> s> <non_const>")
__success __retval(0)
__naked void deducing_bounds_from_non_const_26(void)
{
	asm volatile ("					\
	call %[bpf_ktime_get_ns];			\
	if w0 s< 4 goto l0_%=;				\
	w2 = 4;						\
	if w2 s> w0 goto l1_%=;				\
l0_%=:							\
	r0 = 0;						\
	exit;						\
l1_%=:							\
	r0 -= r1;					\
	exit;						\
"	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

SEC("socket")
__description("check deducing bounds from non-const, jmp32, <const> s>= <non_const>")
__success __retval(0)
__naked void deducing_bounds_from_non_const_27(void)
{
	asm volatile ("					\
	call %[bpf_ktime_get_ns];			\
	if w0 s< 4 goto l0_%=;				\
	w2 = 3;						\
	if w2 s>= w0 goto l1_%=;				\
l0_%=:							\
	r0 = 0;						\
	exit;						\
l1_%=:							\
	r0 -= r1;					\
	exit;						\
"	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

SEC("socket")
__description("check deducing bounds from non-const, jmp32, <const> s< <non_const>")
__success __retval(0)
__naked void deducing_bounds_from_non_const_28(void)
{
	asm volatile ("					\
	call %[bpf_ktime_get_ns];			\
	if w0 s> 4 goto l0_%=;				\
	w2 = 5;						\
	if w2 s< w0 goto l1_%=;				\
l0_%=:							\
	r0 = 0;						\
	exit;						\
l1_%=:							\
	r0 -= r1;					\
	exit;						\
"	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

SEC("socket")
__description("check deducing bounds from non-const, jmp32, <const> s<= <non_const>")
__success __retval(0)
__naked void deducing_bounds_from_non_const_29(void)
{
	asm volatile ("					\
	call %[bpf_ktime_get_ns];			\
	if w0 s>= 4 goto l0_%=;				\
	w2 = 4;						\
	if w2 s<= w0 goto l1_%=;				\
l0_%=:							\
	r0 = 0;						\
	exit;						\
l1_%=:							\
	r0 -= r1;					\
	exit;						\
"	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

SEC("socket")
__description("check deducing bounds from non-const, jmp32, <const> != <non_const>")
__success __retval(0)
__naked void deducing_bounds_from_non_const_30(void)
{
	asm volatile ("					\
	call %[bpf_ktime_get_ns];			\
	if w0 < 3 goto l0_%=;				\
	w2 = 2;						\
	if w2 != w0 goto l0_%=;				\
	goto l1_%=;					\
l0_%=:							\
	r0 = 0;						\
	exit;						\
l1_%=:							\
	r0 -= r1;					\
	exit;						\
"	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
