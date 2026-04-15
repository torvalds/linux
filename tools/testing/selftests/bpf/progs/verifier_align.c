// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */
/* Converted from tools/testing/selftests/bpf/prog_tests/align.c */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

/* Four tests of known constants.  These aren't staggeringly
 * interesting since we track exact values now.
 */

SEC("tc")
__success __log_level(2)
__flag(BPF_F_ANY_ALIGNMENT)
__msg("0: R1=ctx() R10=fp0")
__msg("0: {{.*}} R3=2")
__msg("1: {{.*}} R3=4")
__msg("2: {{.*}} R3=8")
__msg("3: {{.*}} R3=16")
__msg("4: {{.*}} R3=32")
__naked void mov(void)
{
	asm volatile ("					\
	r3 = 2;						\
	r3 = 4;						\
	r3 = 8;						\
	r3 = 16;					\
	r3 = 32;					\
	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

SEC("tc")
__success __log_level(2)
__flag(BPF_F_ANY_ALIGNMENT)
__msg("0: R1=ctx() R10=fp0")
__msg("0: {{.*}}R3=1")
__msg("1: {{.*}}R3=2")
__msg("2: {{.*}}R3=4")
__msg("3: {{.*}}R3=8")
__msg("4: {{.*}}R3=16")
__msg("5: {{.*}}R3=1")
__msg("6: {{.*}}R4=32")
__msg("7: {{.*}}R4=16")
__msg("8: {{.*}}R4=8")
__msg("9: {{.*}}R4=4")
__msg("10: {{.*}}R4=2")
__naked void shift(void)
{
	asm volatile ("					\
	r3 = 1;						\
	r3 <<= 1;					\
	r3 <<= 1;					\
	r3 <<= 1;					\
	r3 <<= 1;					\
	r3 >>= 4;					\
	r4 = 32;					\
	r4 >>= 1;					\
	r4 >>= 1;					\
	r4 >>= 1;					\
	r4 >>= 1;					\
	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

SEC("tc")
__success __log_level(2)
__flag(BPF_F_ANY_ALIGNMENT)
__msg("0: R1=ctx() R10=fp0")
__msg("0: {{.*}}R3=4")
__msg("1: {{.*}}R3=8")
__msg("2: {{.*}}R3=10")
__msg("3: {{.*}}R4=8")
__msg("4: {{.*}}R4=12")
__msg("5: {{.*}}R4=14")
__naked void addsub(void)
{
	asm volatile ("					\
	r3 = 4;						\
	r3 += 4;					\
	r3 += 2;					\
	r4 = 8;						\
	r4 += 4;					\
	r4 += 2;					\
	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

SEC("tc")
__success __log_level(2)
__flag(BPF_F_ANY_ALIGNMENT)
__msg("0: R1=ctx() R10=fp0")
__msg("0: {{.*}}R3=7")
__msg("1: {{.*}}R3=7")
__msg("2: {{.*}}R3=14")
__msg("3: {{.*}}R3=56")
__naked void mul(void)
{
	asm volatile ("					\
	r3 = 7;						\
	r3 *= 1;					\
	r3 *= 2;					\
	r3 *= 4;					\
	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

/* Tests using unknown values */

#define PREP_PKT_POINTERS				\
	"r2 = *(u32*)(r1 + %[__sk_buff_data]);"		\
	"r3 = *(u32*)(r1 + %[__sk_buff_data_end]);"

#define __LOAD_UNKNOWN(DST_REG, LBL)			\
	"r2 = *(u32*)(r1 + %[__sk_buff_data]);"		\
	"r3 = *(u32*)(r1 + %[__sk_buff_data_end]);"	\
	"r0 = r2;"					\
	"r0 += 8;"					\
	"if r3 >= r0 goto " LBL ";"			\
	"exit;"						\
LBL ":"							\
	DST_REG " = *(u8*)(r2 + 0);"

#define LOAD_UNKNOWN(DST_REG) __LOAD_UNKNOWN(DST_REG, "l99_%=")

SEC("tc")
__success __log_level(2)
__flag(BPF_F_ANY_ALIGNMENT)
__msg("6: {{.*}} R2=pkt(r=8)")
__msg("6: {{.*}} R3={{[^)]*}}var_off=(0x0; 0xff)")
__msg("7: {{.*}} R3={{[^)]*}}var_off=(0x0; 0x1fe)")
__msg("8: {{.*}} R3={{[^)]*}}var_off=(0x0; 0x3fc)")
__msg("9: {{.*}} R3={{[^)]*}}var_off=(0x0; 0x7f8)")
__msg("10: {{.*}} R3={{[^)]*}}var_off=(0x0; 0xff0)")
__msg("12: {{.*}} R3=pkt_end()")
__msg("17: {{.*}} R4={{[^)]*}}var_off=(0x0; 0xff)")
__msg("18: {{.*}} R4={{[^)]*}}var_off=(0x0; 0x1fe0)")
__msg("19: {{.*}} R4={{[^)]*}}var_off=(0x0; 0xff0)")
__msg("20: {{.*}} R4={{[^)]*}}var_off=(0x0; 0x7f8)")
__msg("21: {{.*}} R4={{[^)]*}}var_off=(0x0; 0x3fc)")
__msg("22: {{.*}} R4={{[^)]*}}var_off=(0x0; 0x1fe)")
__naked void unknown_shift(void)
{
	asm volatile ("					\
	" __LOAD_UNKNOWN("r3", "l99_%=") "		\
	r3 <<= 1;					\
	r3 <<= 1;					\
	r3 <<= 1;					\
	r3 <<= 1;					\
	" __LOAD_UNKNOWN("r4", "l98_%=") "		\
	r4 <<= 5;					\
	r4 >>= 1;					\
	r4 >>= 1;					\
	r4 >>= 1;					\
	r4 >>= 1;					\
	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tc")
__success __log_level(2)
__flag(BPF_F_ANY_ALIGNMENT)
__msg("6: {{.*}} R3={{[^)]*}}var_off=(0x0; 0xff)")
__msg("7: {{.*}} R4={{[^)]*}}var_off=(0x0; 0xff)")
__msg("8: {{.*}} R4={{[^)]*}}var_off=(0x0; 0xff)")
__msg("9: {{.*}} R4={{[^)]*}}var_off=(0x0; 0xff)")
__msg("10: {{.*}} R4={{[^)]*}}var_off=(0x0; 0x1fe)")
__msg("11: {{.*}} R4={{[^)]*}}var_off=(0x0; 0xff)")
__msg("12: {{.*}} R4={{[^)]*}}var_off=(0x0; 0x3fc)")
__msg("13: {{.*}} R4={{[^)]*}}var_off=(0x0; 0xff)")
__msg("14: {{.*}} R4={{[^)]*}}var_off=(0x0; 0x7f8)")
__msg("15: {{.*}} R4={{[^)]*}}var_off=(0x0; 0xff0)")
__naked void unknown_mul(void)
{
	asm volatile ("					\
	" LOAD_UNKNOWN("r3") "				\
	r4 = r3;					\
	r4 *= 1;					\
	r4 = r3;					\
	r4 *= 2;					\
	r4 = r3;					\
	r4 *= 4;					\
	r4 = r3;					\
	r4 *= 8;					\
	r4 *= 2;					\
	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tc")
__success __log_level(2)
__msg("2: {{.*}} R5=pkt(r=0)")
__msg("4: {{.*}} R5=pkt(r=0,imm=14)")
__msg("5: {{.*}} R4=pkt(r=0,imm=14)")
__msg("9: {{.*}} R5=pkt(r=18,imm=14)")
__msg("10: {{.*}} R4={{[^)]*}}var_off=(0x0; 0xff){{.*}} R5=pkt(r=18,imm=14)")
__msg("13: {{.*}} R4={{[^)]*}}var_off=(0x0; 0xffff)")
__msg("14: {{.*}} R4={{[^)]*}}var_off=(0x0; 0xffff)")
__naked void packet_const_offset(void)
{
	asm volatile ("					\
	" PREP_PKT_POINTERS "				\
	r5 = r2;					\
	r0 = 0;						\
	/* Skip over ethernet header.  */		\
	r5 += 14;					\
	r4 = r5;					\
	r4 += 4;					\
	if r3 >= r4 goto l0_%=;				\
	exit;						\
l0_%=:	r4 = *(u8*)(r5 + 0);				\
	r4 = *(u8*)(r5 + 1);				\
	r4 = *(u8*)(r5 + 2);				\
	r4 = *(u8*)(r5 + 3);				\
	r4 = *(u16*)(r5 + 0);				\
	r4 = *(u16*)(r5 + 2);				\
	r4 = *(u32*)(r5 + 0);				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tc")
__success __log_level(2)
__flag(BPF_F_ANY_ALIGNMENT)
/* Calculated offset in R6 has unknown value, but known
 * alignment of 4.
 */
__msg("6: {{.*}} R2=pkt(r=8)")
__msg("7: {{.*}} R6={{[^)]*}}var_off=(0x0; 0x3fc)")
/* Offset is added to packet pointer R5, resulting in
 * known fixed offset, and variable offset from R6.
 */
__msg("11: {{.*}} R5=pkt(id=1,{{[^)]*}},var_off=(0x2; 0x7fc)")
/* At the time the word size load is performed from R5,
 * it's total offset is NET_IP_ALIGN + reg->off (0) +
 * reg->aux_off (14) which is 16.  Then the variable
 * offset is considered using reg->aux_off_align which
 * is 4 and meets the load's requirements.
 */
__msg("15: {{.*}} R5={{[^)]*}}var_off=(0x2; 0x7fc)")
/* Variable offset is added to R5 packet pointer,
 * resulting in auxiliary alignment of 4. To avoid BPF
 * verifier's precision backtracking logging
 * interfering we also have a no-op R4 = R5
 * instruction to validate R5 state. We also check
 * that R4 is what it should be in such case.
 */
__msg("18: {{.*}} R4={{[^)]*}}var_off=(0x0; 0x3fc){{.*}} R5={{[^)]*}}var_off=(0x0; 0x3fc)")
/* Constant offset is added to R5, resulting in
 * reg->off of 14.
 */
__msg("19: {{.*}} R5=pkt(id=2,{{[^)]*}}var_off=(0x2; 0x7fc)")
/* At the time the word size load is performed from R5,
 * its total fixed offset is NET_IP_ALIGN + reg->off
 * (14) which is 16.  Then the variable offset is 4-byte
 * aligned, so the total offset is 4-byte aligned and
 * meets the load's requirements.
 */
__msg("24: {{.*}} R5={{[^)]*}}var_off=(0x2; 0x7fc)")
/* Constant offset is added to R5 packet pointer,
 * resulting in reg->off value of 14.
 */
__msg("26: {{.*}} R5=pkt(r=8,imm=14)")
/* Variable offset is added to R5, resulting in a
 * variable offset of (4n). See comment for insn #18
 * for R4 = R5 trick.
 */
__msg("28: {{.*}} R4={{[^)]*}}var_off=(0x2; 0x7fc){{.*}} R5={{[^)]*}}var_off=(0x2; 0x7fc)")
/* Constant is added to R5 again, setting reg->off to 18. */
__msg("29: {{.*}} R5=pkt(id=3,{{[^)]*}}var_off=(0x2; 0x7fc)")
/* And once more we add a variable; resulting {{[^)]*}}var_off
 * is still (4n), fixed offset is not changed.
 * Also, we create a new reg->id.
 */
__msg("31: {{.*}} R4={{[^)]*}}var_off=(0x2; 0xffc){{.*}} R5={{[^)]*}}var_off=(0x2; 0xffc)")
/* At the time the word size load is performed from R5,
 * its total fixed offset is NET_IP_ALIGN + reg->off (18)
 * which is 20.  Then the variable offset is (4n), so
 * the total offset is 4-byte aligned and meets the
 * load's requirements.
 */
__msg("35: {{.*}} R5={{[^)]*}}var_off=(0x2; 0xffc)")
__naked void packet_variable_offset(void)
{
	asm volatile ("					\
	" LOAD_UNKNOWN("r6") "				\
	r6 <<= 2;					\
	/* First, add a constant to the R5 packet pointer,\
	 * then a variable with a known alignment.	\
	 */						\
	r5 = r2;					\
	r5 += 14;					\
	r5 += r6;					\
	r4 = r5;					\
	r4 += 4;					\
	if r3 >= r4 goto l0_%=;				\
	exit;						\
l0_%=:	r4 = *(u32*)(r5 + 0);				\
	/* Now, test in the other direction.  Adding first\
	 * the variable offset to R5, then the constant.\
	 */						\
	r5 = r2;					\
	r5 += r6;					\
	r4 = r5;					\
	r5 += 14;					\
	r4 = r5;					\
	r4 += 4;					\
	if r3 >= r4 goto l1_%=;				\
	exit;						\
l1_%=:	r4 = *(u32*)(r5 + 0);				\
	/* Test multiple accumulations of unknown values\
	 * into a packet pointer.			\
	 */						\
	r5 = r2;					\
	r5 += 14;					\
	r5 += r6;					\
	r4 = r5;					\
	r5 += 4;					\
	r5 += r6;					\
	r4 = r5;					\
	r4 += 4;					\
	if r3 >= r4 goto l2_%=;				\
	exit;						\
l2_%=:	r4 = *(u32*)(r5 + 0);				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tc")
__success __log_level(2)
__flag(BPF_F_ANY_ALIGNMENT)
/* Calculated offset in R6 has unknown value, but known
 * alignment of 4.
 */
__msg("6: {{.*}} R2=pkt(r=8)")
__msg("7: {{.*}} R6={{[^)]*}}var_off=(0x0; 0x3fc)")
/* Adding 14 makes R6 be (4n+2) */
__msg("8: {{.*}} R6={{[^)]*}}var_off=(0x2; 0x7fc)")
/* Packet pointer has (4n+2) offset */
__msg("11: {{.*}} R5={{[^)]*}}var_off=(0x2; 0x7fc)")
__msg("12: {{.*}} R4={{[^)]*}}var_off=(0x2; 0x7fc)")
/* At the time the word size load is performed from R5,
 * its total fixed offset is NET_IP_ALIGN + reg->off (0)
 * which is 2.  Then the variable offset is (4n+2), so
 * the total offset is 4-byte aligned and meets the
 * load's requirements.
 */
__msg("15: {{.*}} R5={{[^)]*}}var_off=(0x2; 0x7fc)")
/* Newly read value in R6 was shifted left by 2, so has
 * known alignment of 4.
 */
__msg("17: {{.*}} R6={{[^)]*}}var_off=(0x0; 0x3fc)")
/* Added (4n) to packet pointer's (4n+2) {{[^)]*}}var_off, giving
 * another (4n+2).
 */
__msg("19: {{.*}} R5={{[^)]*}}var_off=(0x2; 0xffc)")
__msg("20: {{.*}} R4={{[^)]*}}var_off=(0x2; 0xffc)")
/* At the time the word size load is performed from R5,
 * its total fixed offset is NET_IP_ALIGN + reg->off (0)
 * which is 2.  Then the variable offset is (4n+2), so
 * the total offset is 4-byte aligned and meets the
 * load's requirements.
 */
__msg("23: {{.*}} R5={{[^)]*}}var_off=(0x2; 0xffc)")
__naked void packet_variable_offset_2(void)
{
	asm volatile ("					\
	/* Create an unknown offset, (4n+2)-aligned */	\
	" LOAD_UNKNOWN("r6") "			\
	r6 <<= 2;					\
	r6 += 14;					\
	/* Add it to the packet pointer */		\
	r5 = r2;					\
	r5 += r6;					\
	/* Check bounds and perform a read */		\
	r4 = r5;					\
	r4 += 4;					\
	if r3 >= r4 goto l0_%=;				\
	exit;						\
l0_%=:	r6 = *(u32*)(r5 + 0);				\
	/* Make a (4n) offset from the value we just read */\
	r6 &= 0xff;					\
	r6 <<= 2;					\
	/* Add it to the packet pointer */		\
	r5 += r6;					\
	/* Check bounds and perform a read */		\
	r4 = r5;					\
	r4 += 4;					\
	if r3 >= r4 goto l1_%=;				\
	exit;						\
l1_%=:	r6 = *(u32*)(r5 + 0);				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tc")
__failure __log_level(2)
__msg("3: {{.*}} R5=pkt_end()")
/* (ptr - ptr) << 2 == unknown, (4n) */
__msg("5: {{.*}} R5={{[^)]*}}var_off=(0x0; 0xfffffffffffffffc)")
/* (4n) + 14 == (4n+2).  We blow our bounds, because
 * the add could overflow.
 */
__msg("6: {{.*}} R5={{[^)]*}}var_off=(0x2; 0xfffffffffffffffc)")
/* Checked s>=0 */
__msg("9: {{.*}} R5={{[^)]*}}var_off=(0x2; 0x7ffffffffffffffc)")
/* packet pointer + nonnegative (4n+2) */
__msg("11: {{.*}} R4={{[^)]*}}var_off=(0x2; 0x7ffffffffffffffc){{.*}} R6={{[^)]*}}var_off=(0x2; 0x7ffffffffffffffc)")
__msg("12: (07) r4 += 4")
/* packet smax bound overflow */
__msg("pkt pointer offset -9223372036854775808 is not allowed")
__naked void dubious_pointer_arithmetic(void)
{
	asm volatile ("					\
	" PREP_PKT_POINTERS "				\
	r0 = 0;						\
	/* (ptr - ptr) << 2 */				\
	r5 = r3;					\
	r5 -= r2;					\
	r5 <<= 2;					\
	/* We have a (4n) value.  Let's make a packet offset\
	 * out of it.  First add 14, to make it a (4n+2)\
	 */						\
	r5 += 14;					\
	/* Then make sure it's nonnegative */		\
	if r5 s>= 0 goto l0_%=;				\
	exit;						\
l0_%=:	/* Add it to packet pointer */			\
	r6 = r2;					\
	r6 += r5;					\
	/* Check bounds and perform a read */		\
	r4 = r6;					\
	r4 += 4;					\
	if r3 >= r4 goto l1_%=;				\
	exit;						\
l1_%=:	r4 = *(u32*)(r6 + 0);				\
	exit;						\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tc")
__success __log_level(2)
__flag(BPF_F_ANY_ALIGNMENT)
/* Calculated offset in R6 has unknown value, but known
 * alignment of 4.
 */
__msg("6: {{.*}} R2=pkt(r=8)")
__msg("8: {{.*}} R6={{[^)]*}}var_off=(0x0; 0x3fc)")
/* Adding 14 makes R6 be (4n+2) */
__msg("9: {{.*}} R6={{[^)]*}}var_off=(0x2; 0x7fc)")
/* New unknown value in R7 is (4n) */
__msg("10: {{.*}} R7={{[^)]*}}var_off=(0x0; 0x3fc)")
/* Subtracting it from R6 blows our unsigned bounds */
__msg("11: {{.*}} R6={{[^)]*}}var_off=(0x2; 0xfffffffffffffffc)")
/* Checked s>= 0 */
__msg("14: {{.*}} R6={{[^)]*}}var_off=(0x2; 0x7fc)")
/* At the time the word size load is performed from R5,
 * its total fixed offset is NET_IP_ALIGN + reg->off (0)
 * which is 2.  Then the variable offset is (4n+2), so
 * the total offset is 4-byte aligned and meets the
 * load's requirements.
 */
__msg("20: {{.*}} R5={{[^)]*}}var_off=(0x2; 0x7fc)")
__naked void variable_subtraction(void)
{
	asm volatile ("					\
	/* Create an unknown offset, (4n+2)-aligned */	\
	" LOAD_UNKNOWN("r6") "				\
	r7 = r6;					\
	r6 <<= 2;					\
	r6 += 14;					\
	/* Create another unknown, (4n)-aligned, and subtract\
	 * it from the first one			\
	 */						\
	r7 <<= 2;					\
	r6 -= r7;					\
	/* Bounds-check the result */			\
	if r6 s>= 0 goto l0_%=;				\
	exit;						\
l0_%=:	/* Add it to the packet pointer */		\
	r5 = r2;					\
	r5 += r6;					\
	/* Check bounds and perform a read */		\
	r4 = r5;					\
	r4 += 4;					\
	if r3 >= r4 goto l1_%=;				\
	exit;						\
l1_%=:	r6 = *(u32*)(r5 + 0);				\
	exit;						\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tc")
__success __log_level(2)
__flag(BPF_F_ANY_ALIGNMENT)
/* Calculated offset in R6 has unknown value, but known
 * alignment of 4.
 */
__msg("6: {{.*}} R2=pkt(r=8)")
__msg("9: {{.*}} R6={{[^)]*}}var_off=(0x0; 0x3c)")
/* Adding 14 makes R6 be (4n+2) */
__msg("10: {{.*}} R6={{[^)]*}}var_off=(0x2; 0x7c)")
/* Subtracting from packet pointer overflows ubounds */
__msg("13: R5={{[^)]*}}var_off=(0xffffffffffffff82; 0x7c)")
/* New unknown value in R7 is (4n), >= 76 */
__msg("14: {{.*}} R7={{[^)]*}}var_off=(0x0; 0x7fc)")
/* Adding it to packet pointer gives nice bounds again */
__msg("16: {{.*}} R5={{[^)]*}}var_off=(0x2; 0x7fc)")
/* At the time the word size load is performed from R5,
 * its total fixed offset is NET_IP_ALIGN + reg->off (0)
 * which is 2.  Then the variable offset is (4n+2), so
 * the total offset is 4-byte aligned and meets the
 * load's requirements.
 */
__msg("20: {{.*}} R5={{[^)]*}}var_off=(0x2; 0x7fc)")
__naked void pointer_variable_subtraction(void)
{
	asm volatile ("					\
	/* Create an unknown offset, (4n+2)-aligned and bounded\
	 * to [14,74]					\
	 */						\
	" LOAD_UNKNOWN("r6") "				\
	r7 = r6;					\
	r6 &= 0xf;					\
	r6 <<= 2;					\
	r6 += 14;					\
	/* Subtract it from the packet pointer */	\
	r5 = r2;					\
	r5 -= r6;					\
	/* Create another unknown, (4n)-aligned and >= 74.\
	 * That in fact means >= 76, since 74 mod 4 == 2\
	 */						\
	r7 <<= 2;					\
	r7 += 76;					\
	/* Add it to the packet pointer */		\
	r5 += r7;					\
	/* Check bounds and perform a read */		\
	r4 = r5;					\
	r4 += 4;					\
	if r3 >= r4 goto l0_%=;				\
	exit;						\
l0_%=:	r6 = *(u32*)(r5 + 0);				\
	exit;						\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
