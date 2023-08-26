// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

#if (defined(__TARGET_ARCH_arm64) || defined(__TARGET_ARCH_x86) || \
     (defined(__TARGET_ARCH_riscv) && __riscv_xlen == 64)) && __clang_major__ >= 18

SEC("socket")
__description("SDIV32, non-zero imm divisor, check 1")
__success __success_unpriv __retval(-20)
__naked void sdiv32_non_zero_imm_1(void)
{
	asm volatile ("					\
	w0 = -41;					\
	w0 s/= 2;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SDIV32, non-zero imm divisor, check 2")
__success __success_unpriv __retval(-20)
__naked void sdiv32_non_zero_imm_2(void)
{
	asm volatile ("					\
	w0 = 41;					\
	w0 s/= -2;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SDIV32, non-zero imm divisor, check 3")
__success __success_unpriv __retval(20)
__naked void sdiv32_non_zero_imm_3(void)
{
	asm volatile ("					\
	w0 = -41;					\
	w0 s/= -2;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SDIV32, non-zero imm divisor, check 4")
__success __success_unpriv __retval(-21)
__naked void sdiv32_non_zero_imm_4(void)
{
	asm volatile ("					\
	w0 = -42;					\
	w0 s/= 2;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SDIV32, non-zero imm divisor, check 5")
__success __success_unpriv __retval(-21)
__naked void sdiv32_non_zero_imm_5(void)
{
	asm volatile ("					\
	w0 = 42;					\
	w0 s/= -2;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SDIV32, non-zero imm divisor, check 6")
__success __success_unpriv __retval(21)
__naked void sdiv32_non_zero_imm_6(void)
{
	asm volatile ("					\
	w0 = -42;					\
	w0 s/= -2;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SDIV32, non-zero imm divisor, check 7")
__success __success_unpriv __retval(21)
__naked void sdiv32_non_zero_imm_7(void)
{
	asm volatile ("					\
	w0 = 42;					\
	w0 s/= 2;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SDIV32, non-zero imm divisor, check 8")
__success __success_unpriv __retval(20)
__naked void sdiv32_non_zero_imm_8(void)
{
	asm volatile ("					\
	w0 = 41;					\
	w0 s/= 2;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SDIV32, non-zero reg divisor, check 1")
__success __success_unpriv __retval(-20)
__naked void sdiv32_non_zero_reg_1(void)
{
	asm volatile ("					\
	w0 = -41;					\
	w1 = 2;						\
	w0 s/= w1;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SDIV32, non-zero reg divisor, check 2")
__success __success_unpriv __retval(-20)
__naked void sdiv32_non_zero_reg_2(void)
{
	asm volatile ("					\
	w0 = 41;					\
	w1 = -2;					\
	w0 s/= w1;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SDIV32, non-zero reg divisor, check 3")
__success __success_unpriv __retval(20)
__naked void sdiv32_non_zero_reg_3(void)
{
	asm volatile ("					\
	w0 = -41;					\
	w1 = -2;					\
	w0 s/= w1;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SDIV32, non-zero reg divisor, check 4")
__success __success_unpriv __retval(-21)
__naked void sdiv32_non_zero_reg_4(void)
{
	asm volatile ("					\
	w0 = -42;					\
	w1 = 2;						\
	w0 s/= w1;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SDIV32, non-zero reg divisor, check 5")
__success __success_unpriv __retval(-21)
__naked void sdiv32_non_zero_reg_5(void)
{
	asm volatile ("					\
	w0 = 42;					\
	w1 = -2;					\
	w0 s/= w1;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SDIV32, non-zero reg divisor, check 6")
__success __success_unpriv __retval(21)
__naked void sdiv32_non_zero_reg_6(void)
{
	asm volatile ("					\
	w0 = -42;					\
	w1 = -2;					\
	w0 s/= w1;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SDIV32, non-zero reg divisor, check 7")
__success __success_unpriv __retval(21)
__naked void sdiv32_non_zero_reg_7(void)
{
	asm volatile ("					\
	w0 = 42;					\
	w1 = 2;						\
	w0 s/= w1;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SDIV32, non-zero reg divisor, check 8")
__success __success_unpriv __retval(20)
__naked void sdiv32_non_zero_reg_8(void)
{
	asm volatile ("					\
	w0 = 41;					\
	w1 = 2;						\
	w0 s/= w1;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SDIV64, non-zero imm divisor, check 1")
__success __success_unpriv __retval(-20)
__naked void sdiv64_non_zero_imm_1(void)
{
	asm volatile ("					\
	r0 = -41;					\
	r0 s/= 2;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SDIV64, non-zero imm divisor, check 2")
__success __success_unpriv __retval(-20)
__naked void sdiv64_non_zero_imm_2(void)
{
	asm volatile ("					\
	r0 = 41;					\
	r0 s/= -2;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SDIV64, non-zero imm divisor, check 3")
__success __success_unpriv __retval(20)
__naked void sdiv64_non_zero_imm_3(void)
{
	asm volatile ("					\
	r0 = -41;					\
	r0 s/= -2;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SDIV64, non-zero imm divisor, check 4")
__success __success_unpriv __retval(-21)
__naked void sdiv64_non_zero_imm_4(void)
{
	asm volatile ("					\
	r0 = -42;					\
	r0 s/= 2;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SDIV64, non-zero imm divisor, check 5")
__success __success_unpriv __retval(-21)
__naked void sdiv64_non_zero_imm_5(void)
{
	asm volatile ("					\
	r0 = 42;					\
	r0 s/= -2;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SDIV64, non-zero imm divisor, check 6")
__success __success_unpriv __retval(21)
__naked void sdiv64_non_zero_imm_6(void)
{
	asm volatile ("					\
	r0 = -42;					\
	r0 s/= -2;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SDIV64, non-zero reg divisor, check 1")
__success __success_unpriv __retval(-20)
__naked void sdiv64_non_zero_reg_1(void)
{
	asm volatile ("					\
	r0 = -41;					\
	r1 = 2;						\
	r0 s/= r1;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SDIV64, non-zero reg divisor, check 2")
__success __success_unpriv __retval(-20)
__naked void sdiv64_non_zero_reg_2(void)
{
	asm volatile ("					\
	r0 = 41;					\
	r1 = -2;					\
	r0 s/= r1;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SDIV64, non-zero reg divisor, check 3")
__success __success_unpriv __retval(20)
__naked void sdiv64_non_zero_reg_3(void)
{
	asm volatile ("					\
	r0 = -41;					\
	r1 = -2;					\
	r0 s/= r1;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SDIV64, non-zero reg divisor, check 4")
__success __success_unpriv __retval(-21)
__naked void sdiv64_non_zero_reg_4(void)
{
	asm volatile ("					\
	r0 = -42;					\
	r1 = 2;						\
	r0 s/= r1;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SDIV64, non-zero reg divisor, check 5")
__success __success_unpriv __retval(-21)
__naked void sdiv64_non_zero_reg_5(void)
{
	asm volatile ("					\
	r0 = 42;					\
	r1 = -2;					\
	r0 s/= r1;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SDIV64, non-zero reg divisor, check 6")
__success __success_unpriv __retval(21)
__naked void sdiv64_non_zero_reg_6(void)
{
	asm volatile ("					\
	r0 = -42;					\
	r1 = -2;					\
	r0 s/= r1;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SMOD32, non-zero imm divisor, check 1")
__success __success_unpriv __retval(-1)
__naked void smod32_non_zero_imm_1(void)
{
	asm volatile ("					\
	w0 = -41;					\
	w0 s%%= 2;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SMOD32, non-zero imm divisor, check 2")
__success __success_unpriv __retval(1)
__naked void smod32_non_zero_imm_2(void)
{
	asm volatile ("					\
	w0 = 41;					\
	w0 s%%= -2;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SMOD32, non-zero imm divisor, check 3")
__success __success_unpriv __retval(-1)
__naked void smod32_non_zero_imm_3(void)
{
	asm volatile ("					\
	w0 = -41;					\
	w0 s%%= -2;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SMOD32, non-zero imm divisor, check 4")
__success __success_unpriv __retval(0)
__naked void smod32_non_zero_imm_4(void)
{
	asm volatile ("					\
	w0 = -42;					\
	w0 s%%= 2;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SMOD32, non-zero imm divisor, check 5")
__success __success_unpriv __retval(0)
__naked void smod32_non_zero_imm_5(void)
{
	asm volatile ("					\
	w0 = 42;					\
	w0 s%%= -2;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SMOD32, non-zero imm divisor, check 6")
__success __success_unpriv __retval(0)
__naked void smod32_non_zero_imm_6(void)
{
	asm volatile ("					\
	w0 = -42;					\
	w0 s%%= -2;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SMOD32, non-zero reg divisor, check 1")
__success __success_unpriv __retval(-1)
__naked void smod32_non_zero_reg_1(void)
{
	asm volatile ("					\
	w0 = -41;					\
	w1 = 2;						\
	w0 s%%= w1;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SMOD32, non-zero reg divisor, check 2")
__success __success_unpriv __retval(1)
__naked void smod32_non_zero_reg_2(void)
{
	asm volatile ("					\
	w0 = 41;					\
	w1 = -2;					\
	w0 s%%= w1;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SMOD32, non-zero reg divisor, check 3")
__success __success_unpriv __retval(-1)
__naked void smod32_non_zero_reg_3(void)
{
	asm volatile ("					\
	w0 = -41;					\
	w1 = -2;					\
	w0 s%%= w1;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SMOD32, non-zero reg divisor, check 4")
__success __success_unpriv __retval(0)
__naked void smod32_non_zero_reg_4(void)
{
	asm volatile ("					\
	w0 = -42;					\
	w1 = 2;						\
	w0 s%%= w1;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SMOD32, non-zero reg divisor, check 5")
__success __success_unpriv __retval(0)
__naked void smod32_non_zero_reg_5(void)
{
	asm volatile ("					\
	w0 = 42;					\
	w1 = -2;					\
	w0 s%%= w1;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SMOD32, non-zero reg divisor, check 6")
__success __success_unpriv __retval(0)
__naked void smod32_non_zero_reg_6(void)
{
	asm volatile ("					\
	w0 = -42;					\
	w1 = -2;					\
	w0 s%%= w1;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SMOD64, non-zero imm divisor, check 1")
__success __success_unpriv __retval(-1)
__naked void smod64_non_zero_imm_1(void)
{
	asm volatile ("					\
	r0 = -41;					\
	r0 s%%= 2;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SMOD64, non-zero imm divisor, check 2")
__success __success_unpriv __retval(1)
__naked void smod64_non_zero_imm_2(void)
{
	asm volatile ("					\
	r0 = 41;					\
	r0 s%%= -2;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SMOD64, non-zero imm divisor, check 3")
__success __success_unpriv __retval(-1)
__naked void smod64_non_zero_imm_3(void)
{
	asm volatile ("					\
	r0 = -41;					\
	r0 s%%= -2;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SMOD64, non-zero imm divisor, check 4")
__success __success_unpriv __retval(0)
__naked void smod64_non_zero_imm_4(void)
{
	asm volatile ("					\
	r0 = -42;					\
	r0 s%%= 2;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SMOD64, non-zero imm divisor, check 5")
__success __success_unpriv __retval(-0)
__naked void smod64_non_zero_imm_5(void)
{
	asm volatile ("					\
	r0 = 42;					\
	r0 s%%= -2;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SMOD64, non-zero imm divisor, check 6")
__success __success_unpriv __retval(0)
__naked void smod64_non_zero_imm_6(void)
{
	asm volatile ("					\
	r0 = -42;					\
	r0 s%%= -2;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SMOD64, non-zero imm divisor, check 7")
__success __success_unpriv __retval(0)
__naked void smod64_non_zero_imm_7(void)
{
	asm volatile ("					\
	r0 = 42;					\
	r0 s%%= 2;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SMOD64, non-zero imm divisor, check 8")
__success __success_unpriv __retval(1)
__naked void smod64_non_zero_imm_8(void)
{
	asm volatile ("					\
	r0 = 41;					\
	r0 s%%= 2;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SMOD64, non-zero reg divisor, check 1")
__success __success_unpriv __retval(-1)
__naked void smod64_non_zero_reg_1(void)
{
	asm volatile ("					\
	r0 = -41;					\
	r1 = 2;						\
	r0 s%%= r1;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SMOD64, non-zero reg divisor, check 2")
__success __success_unpriv __retval(1)
__naked void smod64_non_zero_reg_2(void)
{
	asm volatile ("					\
	r0 = 41;					\
	r1 = -2;					\
	r0 s%%= r1;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SMOD64, non-zero reg divisor, check 3")
__success __success_unpriv __retval(-1)
__naked void smod64_non_zero_reg_3(void)
{
	asm volatile ("					\
	r0 = -41;					\
	r1 = -2;					\
	r0 s%%= r1;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SMOD64, non-zero reg divisor, check 4")
__success __success_unpriv __retval(0)
__naked void smod64_non_zero_reg_4(void)
{
	asm volatile ("					\
	r0 = -42;					\
	r1 = 2;						\
	r0 s%%= r1;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SMOD64, non-zero reg divisor, check 5")
__success __success_unpriv __retval(0)
__naked void smod64_non_zero_reg_5(void)
{
	asm volatile ("					\
	r0 = 42;					\
	r1 = -2;					\
	r0 s%%= r1;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SMOD64, non-zero reg divisor, check 6")
__success __success_unpriv __retval(0)
__naked void smod64_non_zero_reg_6(void)
{
	asm volatile ("					\
	r0 = -42;					\
	r1 = -2;					\
	r0 s%%= r1;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SMOD64, non-zero reg divisor, check 7")
__success __success_unpriv __retval(0)
__naked void smod64_non_zero_reg_7(void)
{
	asm volatile ("					\
	r0 = 42;					\
	r1 = 2;						\
	r0 s%%= r1;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SMOD64, non-zero reg divisor, check 8")
__success __success_unpriv __retval(1)
__naked void smod64_non_zero_reg_8(void)
{
	asm volatile ("					\
	r0 = 41;					\
	r1 = 2;						\
	r0 s%%= r1;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SDIV32, zero divisor")
__success __success_unpriv __retval(0)
__naked void sdiv32_zero_divisor(void)
{
	asm volatile ("					\
	w0 = 42;					\
	w1 = 0;						\
	w2 = -1;					\
	w2 s/= w1;					\
	w0 = w2;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SDIV64, zero divisor")
__success __success_unpriv __retval(0)
__naked void sdiv64_zero_divisor(void)
{
	asm volatile ("					\
	r0 = 42;					\
	r1 = 0;						\
	r2 = -1;					\
	r2 s/= r1;					\
	r0 = r2;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SMOD32, zero divisor")
__success __success_unpriv __retval(-1)
__naked void smod32_zero_divisor(void)
{
	asm volatile ("					\
	w0 = 42;					\
	w1 = 0;						\
	w2 = -1;					\
	w2 s%%= w1;					\
	w0 = w2;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("SMOD64, zero divisor")
__success __success_unpriv __retval(-1)
__naked void smod64_zero_divisor(void)
{
	asm volatile ("					\
	r0 = 42;					\
	r1 = 0;						\
	r2 = -1;					\
	r2 s%%= r1;					\
	r0 = r2;					\
	exit;						\
"	::: __clobber_all);
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
