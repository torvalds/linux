// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/bounds.c */

#include <linux/bpf.h>
#include <../../../include/linux/filter.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1);
	__type(key, long long);
	__type(value, long long);
} map_hash_8b SEC(".maps");

SEC("socket")
__description("subtraction bounds (map value) variant 1")
__failure __msg("R0 max value is outside of the allowed memory range")
__failure_unpriv
__naked void bounds_map_value_variant_1(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = *(u8*)(r0 + 0);				\
	if r1 > 0xff goto l0_%=;			\
	r3 = *(u8*)(r0 + 1);				\
	if r3 > 0xff goto l0_%=;			\
	r1 -= r3;					\
	r1 >>= 56;					\
	r0 += r1;					\
	r0 = *(u8*)(r0 + 0);				\
	exit;						\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("socket")
__description("subtraction bounds (map value) variant 2")
__failure
__msg("R0 min value is negative, either use unsigned index or do a if (index >=0) check.")
__msg_unpriv("R1 has unknown scalar with mixed signed bounds")
__naked void bounds_map_value_variant_2(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = *(u8*)(r0 + 0);				\
	if r1 > 0xff goto l0_%=;			\
	r3 = *(u8*)(r0 + 1);				\
	if r3 > 0xff goto l0_%=;			\
	r1 -= r3;					\
	r0 += r1;					\
	r0 = *(u8*)(r0 + 0);				\
	exit;						\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("socket")
__description("check subtraction on pointers for unpriv")
__success __failure_unpriv __msg_unpriv("R9 pointer -= pointer prohibited")
__retval(0)
__naked void subtraction_on_pointers_for_unpriv(void)
{
	asm volatile ("					\
	r0 = 0;						\
	r1 = %[map_hash_8b] ll;				\
	r2 = r10;					\
	r2 += -8;					\
	r6 = 9;						\
	*(u64*)(r2 + 0) = r6;				\
	call %[bpf_map_lookup_elem];			\
	r9 = r10;					\
	r9 -= r0;					\
	r1 = %[map_hash_8b] ll;				\
	r2 = r10;					\
	r2 += -8;					\
	r6 = 0;						\
	*(u64*)(r2 + 0) = r6;				\
	call %[bpf_map_lookup_elem];			\
	if r0 != 0 goto l0_%=;				\
	exit;						\
l0_%=:	*(u64*)(r0 + 0) = r9;				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("socket")
__description("bounds check based on zero-extended MOV")
__success __success_unpriv __retval(0)
__naked void based_on_zero_extended_mov(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	/* r2 = 0x0000'0000'ffff'ffff */		\
	w2 = 0xffffffff;				\
	/* r2 = 0 */					\
	r2 >>= 32;					\
	/* no-op */					\
	r0 += r2;					\
	/* access at offset 0 */			\
	r0 = *(u8*)(r0 + 0);				\
l0_%=:	/* exit */					\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("socket")
__description("bounds check based on sign-extended MOV. test1")
__failure __msg("map_value pointer and 4294967295")
__failure_unpriv
__naked void on_sign_extended_mov_test1(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	/* r2 = 0xffff'ffff'ffff'ffff */		\
	r2 = 0xffffffff;				\
	/* r2 = 0xffff'ffff */				\
	r2 >>= 32;					\
	/* r0 = <oob pointer> */			\
	r0 += r2;					\
	/* access to OOB pointer */			\
	r0 = *(u8*)(r0 + 0);				\
l0_%=:	/* exit */					\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("socket")
__description("bounds check based on sign-extended MOV. test2")
__failure __msg("R0 min value is outside of the allowed memory range")
__failure_unpriv
__naked void on_sign_extended_mov_test2(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	/* r2 = 0xffff'ffff'ffff'ffff */		\
	r2 = 0xffffffff;				\
	/* r2 = 0xfff'ffff */				\
	r2 >>= 36;					\
	/* r0 = <oob pointer> */			\
	r0 += r2;					\
	/* access to OOB pointer */			\
	r0 = *(u8*)(r0 + 0);				\
l0_%=:	/* exit */					\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("tc")
__description("bounds check based on reg_off + var_off + insn_off. test1")
__failure __msg("value_size=8 off=1073741825")
__naked void var_off_insn_off_test1(void)
{
	asm volatile ("					\
	r6 = *(u32*)(r1 + %[__sk_buff_mark]);		\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r6 &= 1;					\
	r6 += %[__imm_0];				\
	r0 += r6;					\
	r0 += %[__imm_0];				\
l0_%=:	r0 = *(u8*)(r0 + 3);				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b),
	  __imm_const(__imm_0, (1 << 29) - 1),
	  __imm_const(__sk_buff_mark, offsetof(struct __sk_buff, mark))
	: __clobber_all);
}

SEC("tc")
__description("bounds check based on reg_off + var_off + insn_off. test2")
__failure __msg("value 1073741823")
__naked void var_off_insn_off_test2(void)
{
	asm volatile ("					\
	r6 = *(u32*)(r1 + %[__sk_buff_mark]);		\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r6 &= 1;					\
	r6 += %[__imm_0];				\
	r0 += r6;					\
	r0 += %[__imm_1];				\
l0_%=:	r0 = *(u8*)(r0 + 3);				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b),
	  __imm_const(__imm_0, (1 << 30) - 1),
	  __imm_const(__imm_1, (1 << 29) - 1),
	  __imm_const(__sk_buff_mark, offsetof(struct __sk_buff, mark))
	: __clobber_all);
}

SEC("socket")
__description("bounds check after truncation of non-boundary-crossing range")
__success __success_unpriv __retval(0)
__naked void of_non_boundary_crossing_range(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	/* r1 = [0x00, 0xff] */				\
	r1 = *(u8*)(r0 + 0);				\
	r2 = 1;						\
	/* r2 = 0x10'0000'0000 */			\
	r2 <<= 36;					\
	/* r1 = [0x10'0000'0000, 0x10'0000'00ff] */	\
	r1 += r2;					\
	/* r1 = [0x10'7fff'ffff, 0x10'8000'00fe] */	\
	r1 += 0x7fffffff;				\
	/* r1 = [0x00, 0xff] */				\
	w1 -= 0x7fffffff;				\
	/* r1 = 0 */					\
	r1 >>= 8;					\
	/* no-op */					\
	r0 += r1;					\
	/* access at offset 0 */			\
	r0 = *(u8*)(r0 + 0);				\
l0_%=:	/* exit */					\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("socket")
__description("bounds check after truncation of boundary-crossing range (1)")
__failure
/* not actually fully unbounded, but the bound is very high */
__msg("value -4294967168 makes map_value pointer be out of bounds")
__failure_unpriv
__naked void of_boundary_crossing_range_1(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	/* r1 = [0x00, 0xff] */				\
	r1 = *(u8*)(r0 + 0);				\
	r1 += %[__imm_0];				\
	/* r1 = [0xffff'ff80, 0x1'0000'007f] */		\
	r1 += %[__imm_0];				\
	/* r1 = [0xffff'ff80, 0xffff'ffff] or		\
	 *      [0x0000'0000, 0x0000'007f]		\
	 */						\
	w1 += 0;					\
	r1 -= %[__imm_0];				\
	/* r1 = [0x00, 0xff] or				\
	 *      [0xffff'ffff'0000'0080, 0xffff'ffff'ffff'ffff]\
	 */						\
	r1 -= %[__imm_0];				\
	/* error on OOB pointer computation */		\
	r0 += r1;					\
	/* exit */					\
	r0 = 0;						\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b),
	  __imm_const(__imm_0, 0xffffff80 >> 1)
	: __clobber_all);
}

SEC("socket")
__description("bounds check after truncation of boundary-crossing range (2)")
__failure __msg("value -4294967168 makes map_value pointer be out of bounds")
__failure_unpriv
__naked void of_boundary_crossing_range_2(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	/* r1 = [0x00, 0xff] */				\
	r1 = *(u8*)(r0 + 0);				\
	r1 += %[__imm_0];				\
	/* r1 = [0xffff'ff80, 0x1'0000'007f] */		\
	r1 += %[__imm_0];				\
	/* r1 = [0xffff'ff80, 0xffff'ffff] or		\
	 *      [0x0000'0000, 0x0000'007f]		\
	 * difference to previous test: truncation via MOV32\
	 * instead of ALU32.				\
	 */						\
	w1 = w1;					\
	r1 -= %[__imm_0];				\
	/* r1 = [0x00, 0xff] or				\
	 *      [0xffff'ffff'0000'0080, 0xffff'ffff'ffff'ffff]\
	 */						\
	r1 -= %[__imm_0];				\
	/* error on OOB pointer computation */		\
	r0 += r1;					\
	/* exit */					\
	r0 = 0;						\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b),
	  __imm_const(__imm_0, 0xffffff80 >> 1)
	: __clobber_all);
}

SEC("socket")
__description("bounds check after wrapping 32-bit addition")
__success __success_unpriv __retval(0)
__naked void after_wrapping_32_bit_addition(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	/* r1 = 0x7fff'ffff */				\
	r1 = 0x7fffffff;				\
	/* r1 = 0xffff'fffe */				\
	r1 += 0x7fffffff;				\
	/* r1 = 0 */					\
	w1 += 2;					\
	/* no-op */					\
	r0 += r1;					\
	/* access at offset 0 */			\
	r0 = *(u8*)(r0 + 0);				\
l0_%=:	/* exit */					\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("socket")
__description("bounds check after shift with oversized count operand")
__failure __msg("R0 max value is outside of the allowed memory range")
__failure_unpriv
__naked void shift_with_oversized_count_operand(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r2 = 32;					\
	r1 = 1;						\
	/* r1 = (u32)1 << (u32)32 = ? */		\
	w1 <<= w2;					\
	/* r1 = [0x0000, 0xffff] */			\
	r1 &= 0xffff;					\
	/* computes unknown pointer, potentially OOB */	\
	r0 += r1;					\
	/* potentially OOB access */			\
	r0 = *(u8*)(r0 + 0);				\
l0_%=:	/* exit */					\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("socket")
__description("bounds check after right shift of maybe-negative number")
__failure __msg("R0 unbounded memory access")
__failure_unpriv
__naked void shift_of_maybe_negative_number(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	/* r1 = [0x00, 0xff] */				\
	r1 = *(u8*)(r0 + 0);				\
	/* r1 = [-0x01, 0xfe] */			\
	r1 -= 1;					\
	/* r1 = 0 or 0xff'ffff'ffff'ffff */		\
	r1 >>= 8;					\
	/* r1 = 0 or 0xffff'ffff'ffff */		\
	r1 >>= 8;					\
	/* computes unknown pointer, potentially OOB */	\
	r0 += r1;					\
	/* potentially OOB access */			\
	r0 = *(u8*)(r0 + 0);				\
l0_%=:	/* exit */					\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("socket")
__description("bounds check after 32-bit right shift with 64-bit input")
__failure __msg("math between map_value pointer and 4294967294 is not allowed")
__failure_unpriv
__naked void shift_with_64_bit_input(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = 2;						\
	/* r1 = 1<<32 */				\
	r1 <<= 31;					\
	/* r1 = 0 (NOT 2!) */				\
	w1 >>= 31;					\
	/* r1 = 0xffff'fffe (NOT 0!) */			\
	w1 -= 2;					\
	/* error on computing OOB pointer */		\
	r0 += r1;					\
	/* exit */					\
	r0 = 0;						\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("socket")
__description("bounds check map access with off+size signed 32bit overflow. test1")
__failure __msg("map_value pointer and 2147483646")
__failure_unpriv
__naked void size_signed_32bit_overflow_test1(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 != 0 goto l0_%=;				\
	exit;						\
l0_%=:	r0 += 0x7ffffffe;				\
	r0 = *(u64*)(r0 + 0);				\
	goto l1_%=;					\
l1_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("socket")
__description("bounds check map access with off+size signed 32bit overflow. test2")
__failure __msg("pointer offset 1073741822")
__msg_unpriv("R0 pointer arithmetic of map value goes out of range")
__naked void size_signed_32bit_overflow_test2(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 != 0 goto l0_%=;				\
	exit;						\
l0_%=:	r0 += 0x1fffffff;				\
	r0 += 0x1fffffff;				\
	r0 += 0x1fffffff;				\
	r0 = *(u64*)(r0 + 0);				\
	goto l1_%=;					\
l1_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("socket")
__description("bounds check map access with off+size signed 32bit overflow. test3")
__failure __msg("pointer offset -1073741822")
__msg_unpriv("R0 pointer arithmetic of map value goes out of range")
__naked void size_signed_32bit_overflow_test3(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 != 0 goto l0_%=;				\
	exit;						\
l0_%=:	r0 -= 0x1fffffff;				\
	r0 -= 0x1fffffff;				\
	r0 = *(u64*)(r0 + 2);				\
	goto l1_%=;					\
l1_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("socket")
__description("bounds check map access with off+size signed 32bit overflow. test4")
__failure __msg("map_value pointer and 1000000000000")
__failure_unpriv
__naked void size_signed_32bit_overflow_test4(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 != 0 goto l0_%=;				\
	exit;						\
l0_%=:	r1 = 1000000;					\
	r1 *= 1000000;					\
	r0 += r1;					\
	r0 = *(u64*)(r0 + 2);				\
	goto l1_%=;					\
l1_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("socket")
__description("bounds check mixed 32bit and 64bit arithmetic. test1")
__success __success_unpriv
__retval(0)
#ifdef SPEC_V1
__xlated_unpriv("goto pc+2")
__xlated_unpriv("nospec") /* inserted to prevent `R0 invalid mem access 'scalar'` */
__xlated_unpriv("goto pc-1") /* sanitized dead code */
__xlated_unpriv("exit")
#endif
__naked void _32bit_and_64bit_arithmetic_test1(void)
{
	asm volatile ("					\
	r0 = 0;						\
	r1 = -1;					\
	r1 <<= 32;					\
	r1 += 1;					\
	/* r1 = 0xffffFFFF00000001 */			\
	if w1 > 1 goto l0_%=;				\
	/* check ALU64 op keeps 32bit bounds */		\
	r1 += 1;					\
	if w1 > 2 goto l0_%=;				\
	goto l1_%=;					\
l0_%=:	/* invalid ldx if bounds are lost above */	\
	r0 = *(u64*)(r0 - 1);				\
l1_%=:	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("bounds check mixed 32bit and 64bit arithmetic. test2")
__success __success_unpriv
__retval(0)
#ifdef SPEC_V1
__xlated_unpriv("goto pc+2")
__xlated_unpriv("nospec") /* inserted to prevent `R0 invalid mem access 'scalar'` */
__xlated_unpriv("goto pc-1") /* sanitized dead code */
__xlated_unpriv("exit")
#endif
__naked void _32bit_and_64bit_arithmetic_test2(void)
{
	asm volatile ("					\
	r0 = 0;						\
	r1 = -1;					\
	r1 <<= 32;					\
	r1 += 1;					\
	/* r1 = 0xffffFFFF00000001 */			\
	r2 = 3;						\
	/* r1 = 0x2 */					\
	w1 += 1;					\
	/* check ALU32 op zero extends 64bit bounds */	\
	if r1 > r2 goto l0_%=;				\
	goto l1_%=;					\
l0_%=:	/* invalid ldx if bounds are lost above */	\
	r0 = *(u64*)(r0 - 1);				\
l1_%=:	exit;						\
"	::: __clobber_all);
}

SEC("tc")
__description("assigning 32bit bounds to 64bit for wA = 0, wB = wA")
__success __retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void for_wa_0_wb_wa(void)
{
	asm volatile ("					\
	r8 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r7 = *(u32*)(r1 + %[__sk_buff_data]);		\
	w9 = 0;						\
	w2 = w9;					\
	r6 = r7;					\
	r6 += r2;					\
	r3 = r6;					\
	r3 += 8;					\
	if r3 > r8 goto l0_%=;				\
	r5 = *(u32*)(r6 + 0);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("socket")
__description("bounds check for reg = 0, reg xor 1")
__success __success_unpriv
__retval(0)
#ifdef SPEC_V1
__xlated_unpriv("if r1 != 0x0 goto pc+2")
__xlated_unpriv("nospec") /* inserted to prevent `R0 min value is outside of the allowed memory range` */
__xlated_unpriv("goto pc-1") /* sanitized dead code */
__xlated_unpriv("r0 = 0")
#endif
__naked void reg_0_reg_xor_1(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 != 0 goto l0_%=;				\
	exit;						\
l0_%=:	r1 = 0;						\
	r1 ^= 1;					\
	if r1 != 0 goto l1_%=;				\
	r0 = *(u64*)(r0 + 8);				\
l1_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("socket")
__description("bounds check for reg32 = 0, reg32 xor 1")
__success __success_unpriv
__retval(0)
#ifdef SPEC_V1
__xlated_unpriv("if w1 != 0x0 goto pc+2")
__xlated_unpriv("nospec") /* inserted to prevent `R0 min value is outside of the allowed memory range` */
__xlated_unpriv("goto pc-1") /* sanitized dead code */
__xlated_unpriv("r0 = 0")
#endif
__naked void reg32_0_reg32_xor_1(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 != 0 goto l0_%=;				\
	exit;						\
l0_%=:	w1 = 0;						\
	w1 ^= 1;					\
	if w1 != 0 goto l1_%=;				\
	r0 = *(u64*)(r0 + 8);				\
l1_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("socket")
__description("bounds check for reg = 2, reg xor 3")
__success __success_unpriv
__retval(0)
#ifdef SPEC_V1
__xlated_unpriv("if r1 > 0x0 goto pc+2")
__xlated_unpriv("nospec") /* inserted to prevent `R0 min value is outside of the allowed memory range` */
__xlated_unpriv("goto pc-1") /* sanitized dead code */
__xlated_unpriv("r0 = 0")
#endif
__naked void reg_2_reg_xor_3(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 != 0 goto l0_%=;				\
	exit;						\
l0_%=:	r1 = 2;						\
	r1 ^= 3;					\
	if r1 > 0 goto l1_%=;				\
	r0 = *(u64*)(r0 + 8);				\
l1_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("socket")
__description("bounds check for reg = any, reg xor 3")
__failure __msg("invalid access to map value")
__msg_unpriv("invalid access to map value")
__naked void reg_any_reg_xor_3(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 != 0 goto l0_%=;				\
	exit;						\
l0_%=:	r1 = *(u64*)(r0 + 0);				\
	r1 ^= 3;					\
	if r1 != 0 goto l1_%=;				\
	r0 = *(u64*)(r0 + 8);				\
l1_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("socket")
__description("bounds check for reg32 = any, reg32 xor 3")
__failure __msg("invalid access to map value")
__msg_unpriv("invalid access to map value")
__naked void reg32_any_reg32_xor_3(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 != 0 goto l0_%=;				\
	exit;						\
l0_%=:	r1 = *(u64*)(r0 + 0);				\
	w1 ^= 3;					\
	if w1 != 0 goto l1_%=;				\
	r0 = *(u64*)(r0 + 8);				\
l1_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("socket")
__description("bounds check for reg > 0, reg xor 3")
__success __success_unpriv
__retval(0)
#ifdef SPEC_V1
__xlated_unpriv("if r1 >= 0x0 goto pc+2")
__xlated_unpriv("nospec") /* inserted to prevent `R0 min value is outside of the allowed memory range` */
__xlated_unpriv("goto pc-1") /* sanitized dead code */
__xlated_unpriv("r0 = 0")
#endif
__naked void reg_0_reg_xor_3(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 != 0 goto l0_%=;				\
	exit;						\
l0_%=:	r1 = *(u64*)(r0 + 0);				\
	if r1 <= 0 goto l1_%=;				\
	r1 ^= 3;					\
	if r1 >= 0 goto l1_%=;				\
	r0 = *(u64*)(r0 + 8);				\
l1_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("socket")
__description("bounds check for reg32 > 0, reg32 xor 3")
__success __success_unpriv
__retval(0)
#ifdef SPEC_V1
__xlated_unpriv("if w1 >= 0x0 goto pc+2")
__xlated_unpriv("nospec") /* inserted to prevent `R0 min value is outside of the allowed memory range` */
__xlated_unpriv("goto pc-1") /* sanitized dead code */
__xlated_unpriv("r0 = 0")
#endif
__naked void reg32_0_reg32_xor_3(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 != 0 goto l0_%=;				\
	exit;						\
l0_%=:	r1 = *(u64*)(r0 + 0);				\
	if w1 <= 0 goto l1_%=;				\
	w1 ^= 3;					\
	if w1 >= 0 goto l1_%=;				\
	r0 = *(u64*)(r0 + 8);				\
l1_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("socket")
__description("bounds check for non const xor src dst")
__success __log_level(2)
__msg("5: (af) r0 ^= r6                      ; R0=scalar(smin=smin32=0,smax=umax=smax32=umax32=431,var_off=(0x0; 0x1af))")
__naked void non_const_xor_src_dst(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];                    \
	r6 = r0;					\
	call %[bpf_get_prandom_u32];                    \
	r6 &= 0xaf;					\
	r0 &= 0x1a0;					\
	r0 ^= r6;					\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	__imm_addr(map_hash_8b),
	__imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("bounds check for non const or src dst")
__success __log_level(2)
__msg("5: (4f) r0 |= r6                      ; R0=scalar(smin=smin32=0,smax=umax=smax32=umax32=431,var_off=(0x0; 0x1af))")
__naked void non_const_or_src_dst(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];                    \
	r6 = r0;					\
	call %[bpf_get_prandom_u32];                    \
	r6 &= 0xaf;					\
	r0 &= 0x1a0;					\
	r0 |= r6;					\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	__imm_addr(map_hash_8b),
	__imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("bounds check for non const mul regs")
__success __log_level(2)
__msg("5: (2f) r0 *= r6                      ; R0=scalar(smin=smin32=0,smax=umax=smax32=umax32=3825,var_off=(0x0; 0xfff))")
__naked void non_const_mul_regs(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];                    \
	r6 = r0;					\
	call %[bpf_get_prandom_u32];                    \
	r6 &= 0xff;					\
	r0 &= 0x0f;					\
	r0 *= r6;					\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	__imm_addr(map_hash_8b),
	__imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("bounds checks after 32-bit truncation. test 1")
__success __failure_unpriv __msg_unpriv("R0 leaks addr")
__retval(0)
__naked void _32_bit_truncation_test_1(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = *(u32*)(r0 + 0);				\
	/* This used to reduce the max bound to 0x7fffffff */\
	if r1 == 0 goto l1_%=;				\
	if r1 > 0x7fffffff goto l0_%=;			\
l1_%=:	r0 = 0;						\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("socket")
__description("bounds checks after 32-bit truncation. test 2")
__success __failure_unpriv __msg_unpriv("R0 leaks addr")
__retval(0)
__naked void _32_bit_truncation_test_2(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = *(u32*)(r0 + 0);				\
	if r1 s< 1 goto l1_%=;				\
	if w1 s< 0 goto l0_%=;				\
l1_%=:	r0 = 0;						\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("xdp")
__description("bound check with JMP_JLT for crossing 64-bit signed boundary")
__success __retval(0)
__naked void crossing_64_bit_signed_boundary_1(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r1 = r2;					\
	r1 += 1;					\
	if r1 > r3 goto l0_%=;				\
	r1 = *(u8*)(r2 + 0);				\
	r0 = 0x7fffffffffffff10 ll;			\
	r1 += r0;					\
	r0 = 0x8000000000000000 ll;			\
l1_%=:	r0 += 1;					\
	/* r1 unsigned range is [0x7fffffffffffff10, 0x800000000000000f] */\
	if r0 < r1 goto l1_%=;				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("xdp")
__description("bound check with JMP_JSLT for crossing 64-bit signed boundary")
__success __retval(0)
__flag(BPF_F_TEST_REG_INVARIANTS)
__naked void crossing_64_bit_signed_boundary_2(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r1 = r2;					\
	r1 += 1;					\
	if r1 > r3 goto l0_%=;				\
	r1 = *(u8*)(r2 + 0);				\
	r0 = 0x7fffffffffffff10 ll;			\
	r1 += r0;					\
	r2 = 0x8000000000000fff ll;			\
	r0 = 0x8000000000000000 ll;			\
l1_%=:	r0 += 1;					\
	if r0 s> r2 goto l0_%=;				\
	/* r1 signed range is [S64_MIN, S64_MAX] */	\
	if r0 s< r1 goto l1_%=;				\
	r0 = 1;						\
	exit;						\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("xdp")
__description("bound check for loop upper bound greater than U32_MAX")
__success __retval(0)
__naked void bound_greater_than_u32_max(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r1 = r2;					\
	r1 += 1;					\
	if r1 > r3 goto l0_%=;				\
	r1 = *(u8*)(r2 + 0);				\
	r0 = 0x100000000 ll;				\
	r1 += r0;					\
	r0 = 0x100000000 ll;				\
l1_%=:	r0 += 1;					\
	if r0 < r1 goto l1_%=;				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("xdp")
__description("bound check with JMP32_JLT for crossing 32-bit signed boundary")
__success __retval(0)
__naked void crossing_32_bit_signed_boundary_1(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r1 = r2;					\
	r1 += 1;					\
	if r1 > r3 goto l0_%=;				\
	r1 = *(u8*)(r2 + 0);				\
	w0 = 0x7fffff10;				\
	w1 += w0;					\
	w0 = 0x80000000;				\
l1_%=:	w0 += 1;					\
	/* r1 unsigned range is [0, 0x8000000f] */	\
	if w0 < w1 goto l1_%=;				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("xdp")
__description("bound check with JMP32_JSLT for crossing 32-bit signed boundary")
__success __retval(0)
__flag(!BPF_F_TEST_REG_INVARIANTS) /* known invariants violation */
__naked void crossing_32_bit_signed_boundary_2(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r1 = r2;					\
	r1 += 1;					\
	if r1 > r3 goto l0_%=;				\
	r1 = *(u8*)(r2 + 0);				\
	w0 = 0x7fffff10;				\
	w1 += w0;					\
	w2 = 0x80000fff;				\
	w0 = 0x80000000;				\
l1_%=:	w0 += 1;					\
	if w0 s> w2 goto l0_%=;				\
	/* r1 signed range is [S32_MIN, S32_MAX] */	\
	if w0 s< w1 goto l1_%=;				\
	r0 = 1;						\
	exit;						\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("tc")
__description("bounds check with JMP_NE for reg edge")
__success __retval(0)
__naked void reg_not_equal_const(void)
{
	asm volatile ("					\
	r6 = r1;					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	call %[bpf_get_prandom_u32];			\
	r4 = r0;					\
	r4 &= 7;					\
	if r4 != 0 goto l0_%=;				\
	r0 = 0;						\
	exit;						\
l0_%=:	r1 = r6;					\
	r2 = 0;						\
	r3 = r10;					\
	r3 += -8;					\
	r5 = 0;						\
	/* The 4th argument of bpf_skb_store_bytes is defined as \
	 * ARG_CONST_SIZE, so 0 is not allowed. The 'r4 != 0' \
	 * is providing us this exclusion of zero from initial \
	 * [0, 7] range.				\
	 */						\
	call %[bpf_skb_store_bytes];			\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32),
	  __imm(bpf_skb_store_bytes)
	: __clobber_all);
}

SEC("tc")
__description("bounds check with JMP_EQ for reg edge")
__success __retval(0)
__naked void reg_equal_const(void)
{
	asm volatile ("					\
	r6 = r1;					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	call %[bpf_get_prandom_u32];			\
	r4 = r0;					\
	r4 &= 7;					\
	if r4 == 0 goto l0_%=;				\
	r1 = r6;					\
	r2 = 0;						\
	r3 = r10;					\
	r3 += -8;					\
	r5 = 0;						\
	/* Just the same as what we do in reg_not_equal_const() */ \
	call %[bpf_skb_store_bytes];			\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32),
	  __imm(bpf_skb_store_bytes)
	: __clobber_all);
}

SEC("tc")
__description("multiply mixed sign bounds. test 1")
__success __log_level(2)
__msg("r6 *= r7 {{.*}}; R6=scalar(smin=umin=0x1bc16d5cd4927ee1,smax=umax=0x1bc16d674ec80000,smax32=0x7ffffeff,umax32=0xfffffeff,var_off=(0x1bc16d4000000000; 0x3ffffffeff))")
__naked void mult_mixed0_sign(void)
{
	asm volatile (
	"call %[bpf_get_prandom_u32];"
	"r6 = r0;"
	"call %[bpf_get_prandom_u32];"
	"r7 = r0;"
	"r6 &= 0xf;"
	"r6 -= 1000000000;"
	"r7 &= 0xf;"
	"r7 -= 2000000000;"
	"r6 *= r7;"
	"exit"
	:
	: __imm(bpf_get_prandom_u32),
	  __imm(bpf_skb_store_bytes)
	: __clobber_all);
}

SEC("tc")
__description("multiply mixed sign bounds. test 2")
__success __log_level(2)
__msg("r6 *= r7 {{.*}}; R6=scalar(smin=smin32=-100,smax=smax32=200)")
__naked void mult_mixed1_sign(void)
{
	asm volatile (
	"call %[bpf_get_prandom_u32];"
	"r6 = r0;"
	"call %[bpf_get_prandom_u32];"
	"r7 = r0;"
	"r6 &= 0xf;"
	"r6 -= 0xa;"
	"r7 &= 0xf;"
	"r7 -= 0x14;"
	"r6 *= r7;"
	"exit"
	:
	: __imm(bpf_get_prandom_u32),
	  __imm(bpf_skb_store_bytes)
	: __clobber_all);
}

SEC("tc")
__description("multiply negative bounds")
__success __log_level(2)
__msg("r6 *= r7 {{.*}}; R6=scalar(smin=umin=smin32=umin32=0x3ff280b0,smax=umax=smax32=umax32=0x3fff0001,var_off=(0x3ff00000; 0xf81ff))")
__naked void mult_sign_bounds(void)
{
	asm volatile (
	"r8 = 0x7fff;"
	"call %[bpf_get_prandom_u32];"
	"r6 = r0;"
	"call %[bpf_get_prandom_u32];"
	"r7 = r0;"
	"r6 &= 0xa;"
	"r6 -= r8;"
	"r7 &= 0xf;"
	"r7 -= r8;"
	"r6 *= r7;"
	"exit"
	:
	: __imm(bpf_get_prandom_u32),
	  __imm(bpf_skb_store_bytes)
	: __clobber_all);
}

SEC("tc")
__description("multiply bounds that don't cross signed boundary")
__success __log_level(2)
__msg("r8 *= r6 {{.*}}; R6=scalar(smin=smin32=0,smax=umax=smax32=umax32=11,var_off=(0x0; 0xb)) R8=scalar(smin=0,smax=umax=0x7b96bb0a94a3a7cd,var_off=(0x0; 0x7fffffffffffffff))")
__naked void mult_no_sign_crossing(void)
{
	asm volatile (
	"r6 = 0xb;"
	"r8 = 0xb3c3f8c99262687 ll;"
	"call %[bpf_get_prandom_u32];"
	"r7 = r0;"
	"r6 &= r7;"
	"r8 *= r6;"
	"exit"
	:
	: __imm(bpf_get_prandom_u32),
	  __imm(bpf_skb_store_bytes)
	: __clobber_all);
}

SEC("tc")
__description("multiplication overflow, result in unbounded reg. test 1")
__success __log_level(2)
__msg("r6 *= r7 {{.*}}; R6=scalar()")
__naked void mult_unsign_ovf(void)
{
	asm volatile (
	"r8 = 0x7ffffffffff ll;"
	"call %[bpf_get_prandom_u32];"
	"r6 = r0;"
	"call %[bpf_get_prandom_u32];"
	"r7 = r0;"
	"r6 &= 0x7fffffff;"
	"r7 &= r8;"
	"r6 *= r7;"
	"exit"
	:
	: __imm(bpf_get_prandom_u32),
	  __imm(bpf_skb_store_bytes)
	: __clobber_all);
}

SEC("tc")
__description("multiplication overflow, result in unbounded reg. test 2")
__success __log_level(2)
__msg("r6 *= r7 {{.*}}; R6=scalar()")
__naked void mult_sign_ovf(void)
{
	asm volatile (
	"r8 = 0x7ffffffff ll;"
	"call %[bpf_get_prandom_u32];"
	"r6 = r0;"
	"call %[bpf_get_prandom_u32];"
	"r7 = r0;"
	"r6 &= 0xa;"
	"r6 -= r8;"
	"r7 &= 0x7fffffff;"
	"r6 *= r7;"
	"exit"
	:
	: __imm(bpf_get_prandom_u32),
	  __imm(bpf_skb_store_bytes)
	: __clobber_all);
}

SEC("socket")
__description("64-bit addition, all outcomes overflow")
__success __log_level(2)
__msg("5: (0f) r3 += r3 {{.*}} R3=scalar(umin=0x4000000000000000,umax=0xfffffffffffffffe)")
__retval(0)
__naked void add64_full_overflow(void)
{
	asm volatile (
	"call %[bpf_get_prandom_u32];"
	"r4 = r0;"
	"r3 = 0xa000000000000000 ll;"
	"r3 |= r4;"
	"r3 += r3;"
	"r0 = 0;"
	"exit"
	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("64-bit addition, partial overflow, result in unbounded reg")
__success __log_level(2)
__msg("4: (0f) r3 += r3 {{.*}} R3=scalar()")
__retval(0)
__naked void add64_partial_overflow(void)
{
	asm volatile (
	"call %[bpf_get_prandom_u32];"
	"r4 = r0;"
	"r3 = 2;"
	"r3 |= r4;"
	"r3 += r3;"
	"r0 = 0;"
	"exit"
	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("32-bit addition overflow, all outcomes overflow")
__success __log_level(2)
__msg("4: (0c) w3 += w3 {{.*}} R3=scalar(smin=umin=umin32=0x40000000,smax=umax=umax32=0xfffffffe,var_off=(0x0; 0xffffffff))")
__retval(0)
__naked void add32_full_overflow(void)
{
	asm volatile (
	"call %[bpf_get_prandom_u32];"
	"w4 = w0;"
	"w3 = 0xa0000000;"
	"w3 |= w4;"
	"w3 += w3;"
	"r0 = 0;"
	"exit"
	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("32-bit addition, partial overflow, result in unbounded u32 bounds")
__success __log_level(2)
__msg("4: (0c) w3 += w3 {{.*}} R3=scalar(smin=0,smax=umax=0xffffffff,var_off=(0x0; 0xffffffff))")
__retval(0)
__naked void add32_partial_overflow(void)
{
	asm volatile (
	"call %[bpf_get_prandom_u32];"
	"w4 = w0;"
	"w3 = 2;"
	"w3 |= w4;"
	"w3 += w3;"
	"r0 = 0;"
	"exit"
	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("64-bit subtraction, all outcomes underflow")
__success __log_level(2)
__msg("6: (1f) r3 -= r1 {{.*}} R3=scalar(umin=1,umax=0x8000000000000000)")
__retval(0)
__naked void sub64_full_overflow(void)
{
	asm volatile (
	"call %[bpf_get_prandom_u32];"
	"r1 = r0;"
	"r2 = 0x8000000000000000 ll;"
	"r1 |= r2;"
	"r3 = 0;"
	"r3 -= r1;"
	"r0 = 0;"
	"exit"
	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("64-bit subtraction, partial overflow, result in unbounded reg")
__success __log_level(2)
__msg("3: (1f) r3 -= r2 {{.*}} R3=scalar()")
__retval(0)
__naked void sub64_partial_overflow(void)
{
	asm volatile (
	"call %[bpf_get_prandom_u32];"
	"r3 = r0;"
	"r2 = 1;"
	"r3 -= r2;"
	"r0 = 0;"
	"exit"
	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("32-bit subtraction overflow, all outcomes underflow")
__success __log_level(2)
__msg("5: (1c) w3 -= w1 {{.*}} R3=scalar(smin=umin=umin32=1,smax=umax=umax32=0x80000000,var_off=(0x0; 0xffffffff))")
__retval(0)
__naked void sub32_full_overflow(void)
{
	asm volatile (
	"call %[bpf_get_prandom_u32];"
	"w1 = w0;"
	"w2 = 0x80000000;"
	"w1 |= w2;"
	"w3 = 0;"
	"w3 -= w1;"
	"r0 = 0;"
	"exit"
	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("32-bit subtraction, partial overflow, result in unbounded u32 bounds")
__success __log_level(2)
__msg("3: (1c) w3 -= w2 {{.*}} R3=scalar(smin=0,smax=umax=0xffffffff,var_off=(0x0; 0xffffffff))")
__retval(0)
__naked void sub32_partial_overflow(void)
{
	asm volatile (
	"call %[bpf_get_prandom_u32];"
	"w3 = w0;"
	"w2 = 1;"
	"w3 -= w2;"
	"r0 = 0;"
	"exit"
	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("dead branch on jset, does not result in invariants violation error")
__success __log_level(2)
__retval(0) __flag(BPF_F_TEST_REG_INVARIANTS)
__naked void jset_range_analysis(void)
{
	asm volatile ("			\
	call %[bpf_get_netns_cookie];	\
	if r0 == 0 goto l0_%=;		\
	if r0 & 0xffffffff goto +0; 	\
l0_%=:	r0 = 0;				\
	exit;				\
"	:
	: __imm(bpf_get_netns_cookie)
	: __clobber_all);
}

/* This test covers the bounds deduction on 64bits when the s64 and u64 ranges
 * overlap on the negative side. At instruction 7, the ranges look as follows:
 *
 * 0          umin=0xfffffcf1                 umax=0xff..ff6e  U64_MAX
 * |                [xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx]        |
 * |----------------------------|------------------------------|
 * |xxxxxxxxxx]                                   [xxxxxxxxxxxx|
 * 0    smax=0xeffffeee                       smin=-655        -1
 *
 * We should therefore deduce the following new bounds:
 *
 * 0                             u64=[0xff..ffd71;0xff..ff6e]  U64_MAX
 * |                                              [xxx]        |
 * |----------------------------|------------------------------|
 * |                                              [xxx]        |
 * 0                                        s64=[-655;-146]    -1
 *
 * Without the deduction cross sign boundary, we end up with an invariant
 * violation error.
 */
SEC("socket")
__description("bounds deduction cross sign boundary, negative overlap")
__success __log_level(2) __flag(BPF_F_TEST_REG_INVARIANTS)
__msg("7: (1f) r0 -= r6 {{.*}} R0=scalar(smin=smin32=-655,smax=smax32=-146,umin=0xfffffffffffffd71,umax=0xffffffffffffff6e,umin32=0xfffffd71,umax32=0xffffff6e,var_off=(0xfffffffffffffc00; 0x3ff))")
__retval(0)
__naked void bounds_deduct_negative_overlap(void)
{
	asm volatile("			\
	call %[bpf_get_prandom_u32];	\
	w3 = w0;			\
	w6 = (s8)w0;			\
	r0 = (s8)r0;			\
	if w6 >= 0xf0000000 goto l0_%=;	\
	r0 += r6;			\
	r6 += 400;			\
	r0 -= r6;			\
	if r3 < r0 goto l0_%=;		\
l0_%=:	r0 = 0;				\
	exit;				\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/* This test covers the bounds deduction on 64bits when the s64 and u64 ranges
 * overlap on the positive side. At instruction 3, the ranges look as follows:
 *
 * 0 umin=0                      umax=0xffffffffffffff00       U64_MAX
 * [xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx]            |
 * |----------------------------|------------------------------|
 * |xxxxxxxx]                                         [xxxxxxxx|
 * 0      smax=127                                smin=-128    -1
 *
 * We should therefore deduce the following new bounds:
 *
 * 0  u64=[0;127]                                              U64_MAX
 * [xxxxxxxx]                                                  |
 * |----------------------------|------------------------------|
 * [xxxxxxxx]                                                  |
 * 0  s64=[0;127]                                              -1
 *
 * Without the deduction cross sign boundary, the program is rejected due to
 * the frame pointer write.
 */
SEC("socket")
__description("bounds deduction cross sign boundary, positive overlap")
__success __log_level(2) __flag(BPF_F_TEST_REG_INVARIANTS)
__msg("3: (2d) if r0 > r1 {{.*}} R0=scalar(smin=smin32=0,smax=umax=smax32=umax32=127,var_off=(0x0; 0x7f))")
__retval(0)
__naked void bounds_deduct_positive_overlap(void)
{
	asm volatile("			\
	call %[bpf_get_prandom_u32];	\
	r0 = (s8)r0;			\
	r1 = 0xffffffffffffff00;	\
	if r0 > r1 goto l0_%=;		\
	if r0 < 128 goto l0_%=;		\
	r10 = 0;			\
l0_%=:	r0 = 0;				\
	exit;				\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/* This test is the same as above, but the s64 and u64 ranges overlap in two
 * places. At instruction 3, the ranges look as follows:
 *
 * 0 umin=0                           umax=0xffffffffffffff80  U64_MAX
 * [xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx]        |
 * |----------------------------|------------------------------|
 * |xxxxxxxx]                                         [xxxxxxxx|
 * 0      smax=127                                smin=-128    -1
 *
 * 0xffffffffffffff80 = (u64)-128. We therefore can't deduce anything new and
 * the program should fail due to the frame pointer write.
 */
SEC("socket")
__description("bounds deduction cross sign boundary, two overlaps")
__failure __flag(BPF_F_TEST_REG_INVARIANTS)
__msg("3: (2d) if r0 > r1 {{.*}} R0=scalar(smin=smin32=-128,smax=smax32=127,umax=0xffffffffffffff80)")
__msg("frame pointer is read only")
__naked void bounds_deduct_two_overlaps(void)
{
	asm volatile("			\
	call %[bpf_get_prandom_u32];	\
	r0 = (s8)r0;			\
	r1 = 0xffffffffffffff80;	\
	if r0 > r1 goto l0_%=;		\
	if r0 < 128 goto l0_%=;		\
	r10 = 0;			\
l0_%=:	r0 = 0;				\
	exit;				\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("dead jne branch due to disagreeing tnums")
__success __log_level(2)
__naked void jne_disagreeing_tnums(void *ctx)
{
	asm volatile("			\
	call %[bpf_get_prandom_u32];	\
	w0 = w0;			\
	r0 >>= 30;			\
	r0 <<= 30;			\
	r1 = r0;			\
	r1 += 1024;			\
	if r1 != r0 goto +1;		\
	r10 = 0;			\
	exit;				\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("dead jeq branch due to disagreeing tnums")
__success __log_level(2)
__naked void jeq_disagreeing_tnums(void *ctx)
{
	asm volatile("			\
	call %[bpf_get_prandom_u32];	\
	w0 = w0;			\
	r0 >>= 30;			\
	r0 <<= 30;			\
	r1 = r0;			\
	r1 += 1024;			\
	if r1 == r0 goto +1;		\
	exit;				\
	r10 = 0;			\
	exit;				\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
