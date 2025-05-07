// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Google LLC. */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "../../../include/linux/filter.h"
#include "bpf_misc.h"

#ifdef CAN_USE_LOAD_ACQ_STORE_REL

SEC("socket")
__description("load-acquire, 8-bit")
__success __success_unpriv __retval(0)
__naked void load_acquire_8(void)
{
	asm volatile (
	"r0 = 0;"
	"w1 = 0x12;"
	"*(u8 *)(r10 - 1) = w1;"
	".8byte %[load_acquire_insn];" // w2 = load_acquire((u8 *)(r10 - 1));
	"if r2 == r1 goto 1f;"
	"r0 = 1;"
"1:"
	"exit;"
	:
	: __imm_insn(load_acquire_insn,
		     BPF_ATOMIC_OP(BPF_B, BPF_LOAD_ACQ, BPF_REG_2, BPF_REG_10, -1))
	: __clobber_all);
}

SEC("socket")
__description("load-acquire, 16-bit")
__success __success_unpriv __retval(0)
__naked void load_acquire_16(void)
{
	asm volatile (
	"r0 = 0;"
	"w1 = 0x1234;"
	"*(u16 *)(r10 - 2) = w1;"
	".8byte %[load_acquire_insn];" // w2 = load_acquire((u16 *)(r10 - 2));
	"if r2 == r1 goto 1f;"
	"r0 = 1;"
"1:"
	"exit;"
	:
	: __imm_insn(load_acquire_insn,
		     BPF_ATOMIC_OP(BPF_H, BPF_LOAD_ACQ, BPF_REG_2, BPF_REG_10, -2))
	: __clobber_all);
}

SEC("socket")
__description("load-acquire, 32-bit")
__success __success_unpriv __retval(0)
__naked void load_acquire_32(void)
{
	asm volatile (
	"r0 = 0;"
	"w1 = 0x12345678;"
	"*(u32 *)(r10 - 4) = w1;"
	".8byte %[load_acquire_insn];" // w2 = load_acquire((u32 *)(r10 - 4));
	"if r2 == r1 goto 1f;"
	"r0 = 1;"
"1:"
	"exit;"
	:
	: __imm_insn(load_acquire_insn,
		     BPF_ATOMIC_OP(BPF_W, BPF_LOAD_ACQ, BPF_REG_2, BPF_REG_10, -4))
	: __clobber_all);
}

SEC("socket")
__description("load-acquire, 64-bit")
__success __success_unpriv __retval(0)
__naked void load_acquire_64(void)
{
	asm volatile (
	"r0 = 0;"
	"r1 = 0x1234567890abcdef ll;"
	"*(u64 *)(r10 - 8) = r1;"
	".8byte %[load_acquire_insn];" // r2 = load_acquire((u64 *)(r10 - 8));
	"if r2 == r1 goto 1f;"
	"r0 = 1;"
"1:"
	"exit;"
	:
	: __imm_insn(load_acquire_insn,
		     BPF_ATOMIC_OP(BPF_DW, BPF_LOAD_ACQ, BPF_REG_2, BPF_REG_10, -8))
	: __clobber_all);
}

SEC("socket")
__description("load-acquire with uninitialized src_reg")
__failure __failure_unpriv __msg("R2 !read_ok")
__naked void load_acquire_with_uninitialized_src_reg(void)
{
	asm volatile (
	".8byte %[load_acquire_insn];" // r0 = load_acquire((u64 *)(r2 + 0));
	"exit;"
	:
	: __imm_insn(load_acquire_insn,
		     BPF_ATOMIC_OP(BPF_DW, BPF_LOAD_ACQ, BPF_REG_0, BPF_REG_2, 0))
	: __clobber_all);
}

SEC("socket")
__description("load-acquire with non-pointer src_reg")
__failure __failure_unpriv __msg("R1 invalid mem access 'scalar'")
__naked void load_acquire_with_non_pointer_src_reg(void)
{
	asm volatile (
	"r1 = 0;"
	".8byte %[load_acquire_insn];" // r0 = load_acquire((u64 *)(r1 + 0));
	"exit;"
	:
	: __imm_insn(load_acquire_insn,
		     BPF_ATOMIC_OP(BPF_DW, BPF_LOAD_ACQ, BPF_REG_0, BPF_REG_1, 0))
	: __clobber_all);
}

SEC("socket")
__description("misaligned load-acquire")
__failure __failure_unpriv __msg("misaligned stack access off")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void load_acquire_misaligned(void)
{
	asm volatile (
	"r1 = 0;"
	"*(u64 *)(r10 - 8) = r1;"
	".8byte %[load_acquire_insn];" // w0 = load_acquire((u32 *)(r10 - 5));
	"exit;"
	:
	: __imm_insn(load_acquire_insn,
		     BPF_ATOMIC_OP(BPF_W, BPF_LOAD_ACQ, BPF_REG_0, BPF_REG_10, -5))
	: __clobber_all);
}

SEC("socket")
__description("load-acquire from ctx pointer")
__failure __failure_unpriv __msg("BPF_ATOMIC loads from R1 ctx is not allowed")
__naked void load_acquire_from_ctx_pointer(void)
{
	asm volatile (
	".8byte %[load_acquire_insn];" // w0 = load_acquire((u8 *)(r1 + 0));
	"exit;"
	:
	: __imm_insn(load_acquire_insn,
		     BPF_ATOMIC_OP(BPF_B, BPF_LOAD_ACQ, BPF_REG_0, BPF_REG_1, 0))
	: __clobber_all);
}

SEC("xdp")
__description("load-acquire from pkt pointer")
__failure __msg("BPF_ATOMIC loads from R2 pkt is not allowed")
__naked void load_acquire_from_pkt_pointer(void)
{
	asm volatile (
	"r2 = *(u32 *)(r1 + %[xdp_md_data]);"
	"r3 = *(u32 *)(r1 + %[xdp_md_data_end]);"
	"r1 = r2;"
	"r1 += 8;"
	"if r1 >= r3 goto l0_%=;"
	".8byte %[load_acquire_insn];" // w0 = load_acquire((u8 *)(r2 + 0));
"l0_%=:  r0 = 0;"
	"exit;"
	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end)),
	  __imm_insn(load_acquire_insn,
		     BPF_ATOMIC_OP(BPF_B, BPF_LOAD_ACQ, BPF_REG_0, BPF_REG_2, 0))
	: __clobber_all);
}

SEC("flow_dissector")
__description("load-acquire from flow_keys pointer")
__failure __msg("BPF_ATOMIC loads from R2 flow_keys is not allowed")
__naked void load_acquire_from_flow_keys_pointer(void)
{
	asm volatile (
	"r2 = *(u64 *)(r1 + %[__sk_buff_flow_keys]);"
	".8byte %[load_acquire_insn];" // w0 = load_acquire((u8 *)(r2 + 0));
	"exit;"
	:
	: __imm_const(__sk_buff_flow_keys,
		      offsetof(struct __sk_buff, flow_keys)),
	  __imm_insn(load_acquire_insn,
		     BPF_ATOMIC_OP(BPF_B, BPF_LOAD_ACQ, BPF_REG_0, BPF_REG_2, 0))
	: __clobber_all);
}

SEC("sk_reuseport")
__description("load-acquire from sock pointer")
__failure __msg("BPF_ATOMIC loads from R2 sock is not allowed")
__naked void load_acquire_from_sock_pointer(void)
{
	asm volatile (
	"r2 = *(u64 *)(r1 + %[sk_reuseport_md_sk]);"
	// w0 = load_acquire((u8 *)(r2 + offsetof(struct bpf_sock, family)));
	".8byte %[load_acquire_insn];"
	"exit;"
	:
	: __imm_const(sk_reuseport_md_sk, offsetof(struct sk_reuseport_md, sk)),
	  __imm_insn(load_acquire_insn,
		     BPF_ATOMIC_OP(BPF_B, BPF_LOAD_ACQ, BPF_REG_0, BPF_REG_2,
				   offsetof(struct bpf_sock, family)))
	: __clobber_all);
}

SEC("socket")
__description("load-acquire with invalid register R15")
__failure __failure_unpriv __msg("R15 is invalid")
__naked void load_acquire_with_invalid_reg(void)
{
	asm volatile (
	".8byte %[load_acquire_insn];" // r0 = load_acquire((u64 *)(r15 + 0));
	"exit;"
	:
	: __imm_insn(load_acquire_insn,
		     BPF_ATOMIC_OP(BPF_DW, BPF_LOAD_ACQ, BPF_REG_0, 15 /* invalid reg */, 0))
	: __clobber_all);
}

#else /* CAN_USE_LOAD_ACQ_STORE_REL */

SEC("socket")
__description("Clang version < 18, ENABLE_ATOMICS_TESTS not defined, and/or JIT doesn't support load-acquire, use a dummy test")
__success
int dummy_test(void)
{
	return 0;
}

#endif /* CAN_USE_LOAD_ACQ_STORE_REL */

char _license[] SEC("license") = "GPL";
