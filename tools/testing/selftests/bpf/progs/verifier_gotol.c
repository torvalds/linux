// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

#if (defined(__TARGET_ARCH_arm64) || defined(__TARGET_ARCH_x86) || \
	(defined(__TARGET_ARCH_riscv) && __riscv_xlen == 64) || \
	defined(__TARGET_ARCH_arm) || defined(__TARGET_ARCH_s390) || \
	defined(__TARGET_ARCH_loongarch)) && \
	__clang_major__ >= 18

SEC("socket")
__description("gotol, small_imm")
__success __success_unpriv __retval(1)
__naked void gotol_small_imm(void)
{
	asm volatile ("					\
	call %[bpf_ktime_get_ns];			\
	if r0 == 0 goto l0_%=;				\
	gotol l1_%=;					\
l2_%=:							\
	gotol l3_%=;					\
l1_%=:							\
	r0 = 1;						\
	gotol l2_%=;					\
l0_%=:							\
	r0 = 2;						\
l3_%=:							\
	exit;						\
"	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

SEC("socket")
__description("gotol, large_imm")
__success __failure_unpriv __retval(40000)
__naked void gotol_large_imm(void)
{
	asm volatile ("					\
	gotol 1f;					\
0:							\
	r0 = 0;						\
	.rept 40000;					\
	r0 += 1;					\
	.endr;						\
	exit;						\
1:	gotol 0b;					\
"	:
	:
	: __clobber_all);
}

#else

SEC("socket")
__description("cpuv4 is not supported by compiler or jit, use a dummy test")
__success
int dummy_test(void)
{
	return 0;
}

#endif

char _license[] SEC("license") = "GPL";
