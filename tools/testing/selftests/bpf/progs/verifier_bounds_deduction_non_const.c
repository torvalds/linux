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

char _license[] SEC("license") = "GPL";
