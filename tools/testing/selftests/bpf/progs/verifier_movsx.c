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
__description("MOV32SX, S8")
__success __success_unpriv __retval(0x23)
__naked void mov32sx_s8(void)
{
	asm volatile ("					\
	w0 = 0xff23;					\
	w0 = (s8)w0;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("MOV32SX, S16")
__success __success_unpriv __retval(0xFFFFff23)
__naked void mov32sx_s16(void)
{
	asm volatile ("					\
	w0 = 0xff23;					\
	w0 = (s16)w0;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("MOV64SX, S8")
__success __success_unpriv __retval(-2)
__naked void mov64sx_s8(void)
{
	asm volatile ("					\
	r0 = 0x1fe;					\
	r0 = (s8)r0;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("MOV64SX, S16")
__success __success_unpriv __retval(0xf23)
__naked void mov64sx_s16(void)
{
	asm volatile ("					\
	r0 = 0xf0f23;					\
	r0 = (s16)r0;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("MOV64SX, S32")
__success __success_unpriv __retval(-1)
__naked void mov64sx_s32(void)
{
	asm volatile ("					\
	r0 = 0xfffffffe;				\
	r0 = (s32)r0;					\
	r0 >>= 1;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("MOV32SX, S8, range_check")
__success __success_unpriv __retval(1)
__naked void mov32sx_s8_range(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	w1 = (s8)w0;					\
	/* w1 with s8 range */				\
	if w1 s> 0x7f goto l0_%=;			\
	if w1 s< -0x80 goto l0_%=;			\
	r0 = 1;						\
l1_%=:							\
	exit;						\
l0_%=:							\
	r0 = 2;						\
	goto l1_%=;					\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("MOV32SX, S16, range_check")
__success __success_unpriv __retval(1)
__naked void mov32sx_s16_range(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	w1 = (s16)w0;					\
	/* w1 with s16 range */				\
	if w1 s> 0x7fff goto l0_%=;			\
	if w1 s< -0x80ff goto l0_%=;			\
	r0 = 1;						\
l1_%=:							\
	exit;						\
l0_%=:							\
	r0 = 2;						\
	goto l1_%=;					\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("MOV32SX, S16, range_check 2")
__success __success_unpriv __retval(1)
__naked void mov32sx_s16_range_2(void)
{
	asm volatile ("					\
	r1 = 65535;					\
	w2 = (s16)w1;					\
	r2 >>= 1;					\
	if r2 != 0x7fffFFFF goto l0_%=;			\
	r0 = 1;						\
l1_%=:							\
	exit;						\
l0_%=:							\
	r0 = 0;						\
	goto l1_%=;					\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("MOV64SX, S8, range_check")
__success __success_unpriv __retval(1)
__naked void mov64sx_s8_range(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = (s8)r0;					\
	/* r1 with s8 range */				\
	if r1 s> 0x7f goto l0_%=;			\
	if r1 s< -0x80 goto l0_%=;			\
	r0 = 1;						\
l1_%=:							\
	exit;						\
l0_%=:							\
	r0 = 2;						\
	goto l1_%=;					\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("MOV64SX, S16, range_check")
__success __success_unpriv __retval(1)
__naked void mov64sx_s16_range(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = (s16)r0;					\
	/* r1 with s16 range */				\
	if r1 s> 0x7fff goto l0_%=;			\
	if r1 s< -0x8000 goto l0_%=;			\
	r0 = 1;						\
l1_%=:							\
	exit;						\
l0_%=:							\
	r0 = 2;						\
	goto l1_%=;					\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("MOV64SX, S32, range_check")
__success __success_unpriv __retval(1)
__naked void mov64sx_s32_range(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = (s32)r0;					\
	/* r1 with s32 range */				\
	if r1 s> 0x7fffffff goto l0_%=;			\
	if r1 s< -0x80000000 goto l0_%=;		\
	r0 = 1;						\
l1_%=:							\
	exit;						\
l0_%=:							\
	r0 = 2;						\
	goto l1_%=;					\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("MOV64SX, S16, R10 Sign Extension")
__failure __msg("R1 type=scalar expected=fp, pkt, pkt_meta, map_key, map_value, mem, ringbuf_mem, buf, trusted_ptr_")
__failure_unpriv __msg_unpriv("R10 sign-extension part of pointer")
__naked void mov64sx_s16_r10(void)
{
	asm volatile ("					\
	r1 = 553656332;					\
	*(u32 *)(r10 - 8) = r1; 			\
	r1 = (s16)r10;					\
	r1 += -8;					\
	r2 = 3;						\
	if r2 <= r1 goto l0_%=;				\
l0_%=:							\
	call %[bpf_trace_printk];			\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_trace_printk)
	: __clobber_all);
}

SEC("socket")
__description("MOV32SX, S8, var_off u32_max")
__failure __msg("infinite loop detected")
__failure_unpriv __msg_unpriv("back-edge from insn 2 to 0")
__naked void mov64sx_s32_varoff_1(void)
{
	asm volatile ("					\
l0_%=:							\
	r3 = *(u8 *)(r10 -387);				\
	w7 = (s8)w3;					\
	if w7 >= 0x2533823b goto l0_%=;			\
	w0 = 0;						\
	exit;						\
"	:
	:
	: __clobber_all);
}

SEC("socket")
__description("MOV32SX, S8, var_off not u32_max, positive after s8 extension")
__success __retval(0)
__failure_unpriv __msg_unpriv("frame pointer is read only")
__naked void mov64sx_s32_varoff_2(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r3 = r0;					\
	r3 &= 0xf;					\
	w7 = (s8)w3;					\
	if w7 s>= 16 goto l0_%=;			\
	w0 = 0;						\
	exit;						\
l0_%=:							\
	r10 = 1;					\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("MOV32SX, S8, var_off not u32_max, negative after s8 extension")
__success __retval(0)
__failure_unpriv __msg_unpriv("frame pointer is read only")
__naked void mov64sx_s32_varoff_3(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r3 = r0;					\
	r3 &= 0xf;					\
	r3 |= 0x80;					\
	w7 = (s8)w3;					\
	if w7 s>= -5 goto l0_%=;			\
	w0 = 0;						\
	exit;						\
l0_%=:							\
	r10 = 1;					\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("MOV64SX, S8, unsigned range_check")
__success __retval(0)
__naked void mov64sx_s8_range_check(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r0 &= 0x1;					\
	r0 += 0xfe;					\
	r0 = (s8)r0;					\
	if r0 < 0xfffffffffffffffe goto label_%=;	\
	r0 = 0;						\
	exit;						\
label_%=:						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("MOV32SX, S8, unsigned range_check")
__success __retval(0)
__naked void mov32sx_s8_range_check(void)
{
	asm volatile ("                                 \
	call %[bpf_get_prandom_u32];                    \
	w0 &= 0x1;                                      \
	w0 += 0xfe;                                     \
	w0 = (s8)w0;                                    \
	if w0 < 0xfffffffe goto label_%=;               \
	r0 = 0;                                         \
	exit;                                           \
label_%=: 	                                        \
	exit;                                           \
	"      :
	: __imm(bpf_get_prandom_u32)
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
