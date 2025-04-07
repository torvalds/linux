// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/stack_ptr.c */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <limits.h>
#include "bpf_misc.h"

#define MAX_ENTRIES 11

struct test_val {
	unsigned int index;
	int foo[MAX_ENTRIES];
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, struct test_val);
} map_array_48b SEC(".maps");

SEC("socket")
__description("PTR_TO_STACK store/load")
__success __success_unpriv __retval(0xfaceb00c)
__naked void ptr_to_stack_store_load(void)
{
	asm volatile ("					\
	r1 = r10;					\
	r1 += -10;					\
	r0 = 0xfaceb00c;				\
	*(u64*)(r1 + 2) = r0;				\
	r0 = *(u64*)(r1 + 2);				\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("PTR_TO_STACK store/load - bad alignment on off")
__failure __msg("misaligned stack access off 0+-8+2 size 8")
__failure_unpriv
__naked void load_bad_alignment_on_off(void)
{
	asm volatile ("					\
	r1 = r10;					\
	r1 += -8;					\
	r0 = 0xfaceb00c;				\
	*(u64*)(r1 + 2) = r0;				\
	r0 = *(u64*)(r1 + 2);				\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("PTR_TO_STACK store/load - bad alignment on reg")
__failure __msg("misaligned stack access off 0+-10+8 size 8")
__failure_unpriv
__naked void load_bad_alignment_on_reg(void)
{
	asm volatile ("					\
	r1 = r10;					\
	r1 += -10;					\
	r0 = 0xfaceb00c;				\
	*(u64*)(r1 + 8) = r0;				\
	r0 = *(u64*)(r1 + 8);				\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("PTR_TO_STACK store/load - out of bounds low")
__failure __msg("invalid write to stack R1 off=-79992 size=8")
__msg_unpriv("R1 stack pointer arithmetic goes out of range")
__naked void load_out_of_bounds_low(void)
{
	asm volatile ("					\
	r1 = r10;					\
	r1 += -80000;					\
	r0 = 0xfaceb00c;				\
	*(u64*)(r1 + 8) = r0;				\
	r0 = *(u64*)(r1 + 8);				\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("PTR_TO_STACK store/load - out of bounds high")
__failure __msg("invalid write to stack R1 off=0 size=8")
__failure_unpriv
__naked void load_out_of_bounds_high(void)
{
	asm volatile ("					\
	r1 = r10;					\
	r1 += -8;					\
	r0 = 0xfaceb00c;				\
	*(u64*)(r1 + 8) = r0;				\
	r0 = *(u64*)(r1 + 8);				\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("PTR_TO_STACK check high 1")
__success __success_unpriv __retval(42)
__naked void to_stack_check_high_1(void)
{
	asm volatile ("					\
	r1 = r10;					\
	r1 += -1;					\
	r0 = 42;					\
	*(u8*)(r1 + 0) = r0;				\
	r0 = *(u8*)(r1 + 0);				\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("PTR_TO_STACK check high 2")
__success __success_unpriv __retval(42)
__naked void to_stack_check_high_2(void)
{
	asm volatile ("					\
	r1 = r10;					\
	r0 = 42;					\
	*(u8*)(r1 - 1) = r0;				\
	r0 = *(u8*)(r1 - 1);				\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("PTR_TO_STACK check high 3")
__success __failure_unpriv
__msg_unpriv("R1 stack pointer arithmetic goes out of range")
__retval(42)
__naked void to_stack_check_high_3(void)
{
	asm volatile ("					\
	r1 = r10;					\
	r1 += 0;					\
	r0 = 42;					\
	*(u8*)(r1 - 1) = r0;				\
	r0 = *(u8*)(r1 - 1);				\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("PTR_TO_STACK check high 4")
__failure __msg("invalid write to stack R1 off=0 size=1")
__msg_unpriv("R1 stack pointer arithmetic goes out of range")
__naked void to_stack_check_high_4(void)
{
	asm volatile ("					\
	r1 = r10;					\
	r1 += 0;					\
	r0 = 42;					\
	*(u8*)(r1 + 0) = r0;				\
	r0 = *(u8*)(r1 + 0);				\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("PTR_TO_STACK check high 5")
__failure __msg("invalid write to stack R1")
__msg_unpriv("R1 stack pointer arithmetic goes out of range")
__naked void to_stack_check_high_5(void)
{
	asm volatile ("					\
	r1 = r10;					\
	r1 += %[__imm_0];				\
	r0 = 42;					\
	*(u8*)(r1 + 0) = r0;				\
	r0 = *(u8*)(r1 + 0);				\
	exit;						\
"	:
	: __imm_const(__imm_0, (1 << 29) - 1)
	: __clobber_all);
}

SEC("socket")
__description("PTR_TO_STACK check high 6")
__failure __msg("invalid write to stack")
__msg_unpriv("R1 stack pointer arithmetic goes out of range")
__naked void to_stack_check_high_6(void)
{
	asm volatile ("					\
	r1 = r10;					\
	r1 += %[__imm_0];				\
	r0 = 42;					\
	*(u8*)(r1 + %[shrt_max]) = r0;			\
	r0 = *(u8*)(r1 + %[shrt_max]);			\
	exit;						\
"	:
	: __imm_const(__imm_0, (1 << 29) - 1),
	  __imm_const(shrt_max, SHRT_MAX)
	: __clobber_all);
}

SEC("socket")
__description("PTR_TO_STACK check high 7")
__failure __msg("fp pointer offset")
__msg_unpriv("R1 stack pointer arithmetic goes out of range")
__naked void to_stack_check_high_7(void)
{
	asm volatile ("					\
	r1 = r10;					\
	r1 += %[__imm_0];				\
	r1 += %[__imm_0];				\
	r0 = 42;					\
	*(u8*)(r1 + %[shrt_max]) = r0;			\
	r0 = *(u8*)(r1 + %[shrt_max]);			\
	exit;						\
"	:
	: __imm_const(__imm_0, (1 << 29) - 1),
	  __imm_const(shrt_max, SHRT_MAX)
	: __clobber_all);
}

SEC("socket")
__description("PTR_TO_STACK check low 1")
__success __success_unpriv __retval(42)
__naked void to_stack_check_low_1(void)
{
	asm volatile ("					\
	r1 = r10;					\
	r1 += -512;					\
	r0 = 42;					\
	*(u8*)(r1 + 0) = r0;				\
	r0 = *(u8*)(r1 + 0);				\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("PTR_TO_STACK check low 2")
__success __failure_unpriv
__msg_unpriv("R1 stack pointer arithmetic goes out of range")
__retval(42)
__naked void to_stack_check_low_2(void)
{
	asm volatile ("					\
	r1 = r10;					\
	r1 += -513;					\
	r0 = 42;					\
	*(u8*)(r1 + 1) = r0;				\
	r0 = *(u8*)(r1 + 1);				\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("PTR_TO_STACK check low 3")
__failure __msg("invalid write to stack R1 off=-513 size=1")
__msg_unpriv("R1 stack pointer arithmetic goes out of range")
__naked void to_stack_check_low_3(void)
{
	asm volatile ("					\
	r1 = r10;					\
	r1 += -513;					\
	r0 = 42;					\
	*(u8*)(r1 + 0) = r0;				\
	r0 = *(u8*)(r1 + 0);				\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("PTR_TO_STACK check low 4")
__failure __msg("math between fp pointer")
__failure_unpriv
__naked void to_stack_check_low_4(void)
{
	asm volatile ("					\
	r1 = r10;					\
	r1 += %[int_min];				\
	r0 = 42;					\
	*(u8*)(r1 + 0) = r0;				\
	r0 = *(u8*)(r1 + 0);				\
	exit;						\
"	:
	: __imm_const(int_min, INT_MIN)
	: __clobber_all);
}

SEC("socket")
__description("PTR_TO_STACK check low 5")
__failure __msg("invalid write to stack")
__msg_unpriv("R1 stack pointer arithmetic goes out of range")
__naked void to_stack_check_low_5(void)
{
	asm volatile ("					\
	r1 = r10;					\
	r1 += %[__imm_0];				\
	r0 = 42;					\
	*(u8*)(r1 + 0) = r0;				\
	r0 = *(u8*)(r1 + 0);				\
	exit;						\
"	:
	: __imm_const(__imm_0, -((1 << 29) - 1))
	: __clobber_all);
}

SEC("socket")
__description("PTR_TO_STACK check low 6")
__failure __msg("invalid write to stack")
__msg_unpriv("R1 stack pointer arithmetic goes out of range")
__naked void to_stack_check_low_6(void)
{
	asm volatile ("					\
	r1 = r10;					\
	r1 += %[__imm_0];				\
	r0 = 42;					\
	*(u8*)(r1  %[shrt_min]) = r0;			\
	r0 = *(u8*)(r1  %[shrt_min]);			\
	exit;						\
"	:
	: __imm_const(__imm_0, -((1 << 29) - 1)),
	  __imm_const(shrt_min, SHRT_MIN)
	: __clobber_all);
}

SEC("socket")
__description("PTR_TO_STACK check low 7")
__failure __msg("fp pointer offset")
__msg_unpriv("R1 stack pointer arithmetic goes out of range")
__naked void to_stack_check_low_7(void)
{
	asm volatile ("					\
	r1 = r10;					\
	r1 += %[__imm_0];				\
	r1 += %[__imm_0];				\
	r0 = 42;					\
	*(u8*)(r1  %[shrt_min]) = r0;			\
	r0 = *(u8*)(r1  %[shrt_min]);			\
	exit;						\
"	:
	: __imm_const(__imm_0, -((1 << 29) - 1)),
	  __imm_const(shrt_min, SHRT_MIN)
	: __clobber_all);
}

SEC("socket")
__description("PTR_TO_STACK mixed reg/k, 1")
__success __success_unpriv __retval(42)
__naked void stack_mixed_reg_k_1(void)
{
	asm volatile ("					\
	r1 = r10;					\
	r1 += -3;					\
	r2 = -3;					\
	r1 += r2;					\
	r0 = 42;					\
	*(u8*)(r1 + 0) = r0;				\
	r0 = *(u8*)(r1 + 0);				\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("PTR_TO_STACK mixed reg/k, 2")
__success __success_unpriv __retval(42)
__naked void stack_mixed_reg_k_2(void)
{
	asm volatile ("					\
	r0 = 0;						\
	*(u64*)(r10 - 8) = r0;				\
	r0 = 0;						\
	*(u64*)(r10 - 16) = r0;				\
	r1 = r10;					\
	r1 += -3;					\
	r2 = -3;					\
	r1 += r2;					\
	r0 = 42;					\
	*(u8*)(r1 + 0) = r0;				\
	r5 = r10;					\
	r0 = *(u8*)(r5 - 6);				\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("PTR_TO_STACK mixed reg/k, 3")
__success __success_unpriv __retval(-3)
__naked void stack_mixed_reg_k_3(void)
{
	asm volatile ("					\
	r1 = r10;					\
	r1 += -3;					\
	r2 = -3;					\
	r1 += r2;					\
	r0 = 42;					\
	*(u8*)(r1 + 0) = r0;				\
	r0 = r2;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("PTR_TO_STACK reg")
__success __success_unpriv __retval(42)
__naked void ptr_to_stack_reg(void)
{
	asm volatile ("					\
	r1 = r10;					\
	r2 = -3;					\
	r1 += r2;					\
	r0 = 42;					\
	*(u8*)(r1 + 0) = r0;				\
	r0 = *(u8*)(r1 + 0);				\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("stack pointer arithmetic")
__success __success_unpriv __retval(0)
__naked void stack_pointer_arithmetic(void)
{
	asm volatile ("					\
	r1 = 4;						\
	goto l0_%=;					\
l0_%=:	r7 = r10;					\
	r7 += -10;					\
	r7 += -10;					\
	r2 = r7;					\
	r2 += r1;					\
	r0 = 0;						\
	*(u32*)(r2 + 4) = r0;				\
	r2 = r7;					\
	r2 += 8;					\
	r0 = 0;						\
	*(u32*)(r2 + 4) = r0;				\
	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

SEC("tc")
__description("store PTR_TO_STACK in R10 to array map using BPF_B")
__success __retval(42)
__naked void array_map_using_bpf_b(void)
{
	asm volatile ("					\
	/* Load pointer to map. */			\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_array_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 != 0 goto l0_%=;				\
	r0 = 2;						\
	exit;						\
l0_%=:	r1 = r0;					\
	/* Copy R10 to R9. */				\
	r9 = r10;					\
	/* Pollute other registers with unaligned values. */\
	r2 = -1;					\
	r3 = -1;					\
	r4 = -1;					\
	r5 = -1;					\
	r6 = -1;					\
	r7 = -1;					\
	r8 = -1;					\
	/* Store both R9 and R10 with BPF_B and read back. */\
	*(u8*)(r1 + 0) = r10;				\
	r2 = *(u8*)(r1 + 0);				\
	*(u8*)(r1 + 0) = r9;				\
	r3 = *(u8*)(r1 + 0);				\
	/* Should read back as same value. */		\
	if r2 == r3 goto l1_%=;				\
	r0 = 1;						\
	exit;						\
l1_%=:	r0 = 42;					\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_array_48b)
	: __clobber_all);
}

SEC("socket")
__description("PTR_TO_STACK stack size > 512")
__failure __msg("invalid write to stack R1 off=-520 size=8")
__naked void stack_check_size_gt_512(void)
{
	asm volatile ("					\
	r1 = r10;					\
	r1 += -520;					\
	r0 = 42;					\
	*(u64*)(r1 + 0) = r0;				\
	exit;						\
"	::: __clobber_all);
}

#ifdef __BPF_FEATURE_MAY_GOTO
SEC("socket")
__description("PTR_TO_STACK stack size 512 with may_goto with jit")
__load_if_JITed()
__success __retval(42)
__naked void stack_check_size_512_with_may_goto_jit(void)
{
	asm volatile ("					\
	r1 = r10;					\
	r1 += -512;					\
	r0 = 42;					\
	*(u32*)(r1 + 0) = r0;				\
	may_goto l0_%=;					\
	r2 = 100;					\
	l0_%=:						\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("PTR_TO_STACK stack size 512 with may_goto without jit")
__load_if_no_JITed()
__failure __msg("stack size 520(extra 8) is too large")
__naked void stack_check_size_512_with_may_goto(void)
{
	asm volatile ("					\
	r1 = r10;					\
	r1 += -512;					\
	r0 = 42;					\
	*(u32*)(r1 + 0) = r0;				\
	may_goto l0_%=;					\
	r2 = 100;					\
	l0_%=:						\
	exit;						\
"	::: __clobber_all);
}
#endif

char _license[] SEC("license") = "GPL";
