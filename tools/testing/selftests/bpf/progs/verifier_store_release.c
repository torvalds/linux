// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Google LLC. */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "../../../include/linux/filter.h"
#include "bpf_misc.h"

#ifdef CAN_USE_LOAD_ACQ_STORE_REL

SEC("socket")
__description("store-release, 8-bit")
__success __success_unpriv __retval(0)
__naked void store_release_8(void)
{
	asm volatile (
	"r0 = 0;"
	"w1 = 0x12;"
	".8byte %[store_release_insn];" // store_release((u8 *)(r10 - 1), w1);
	"w2 = *(u8 *)(r10 - 1);"
	"if r2 == r1 goto 1f;"
	"r0 = 1;"
"1:"
	"exit;"
	:
	: __imm_insn(store_release_insn,
		     BPF_ATOMIC_OP(BPF_B, BPF_STORE_REL, BPF_REG_10, BPF_REG_1, -1))
	: __clobber_all);
}

SEC("socket")
__description("store-release, 16-bit")
__success __success_unpriv __retval(0)
__naked void store_release_16(void)
{
	asm volatile (
	"r0 = 0;"
	"w1 = 0x1234;"
	".8byte %[store_release_insn];" // store_release((u16 *)(r10 - 2), w1);
	"w2 = *(u16 *)(r10 - 2);"
	"if r2 == r1 goto 1f;"
	"r0 = 1;"
"1:"
	"exit;"
	:
	: __imm_insn(store_release_insn,
		     BPF_ATOMIC_OP(BPF_H, BPF_STORE_REL, BPF_REG_10, BPF_REG_1, -2))
	: __clobber_all);
}

SEC("socket")
__description("store-release, 32-bit")
__success __success_unpriv __retval(0)
__naked void store_release_32(void)
{
	asm volatile (
	"r0 = 0;"
	"w1 = 0x12345678;"
	".8byte %[store_release_insn];" // store_release((u32 *)(r10 - 4), w1);
	"w2 = *(u32 *)(r10 - 4);"
	"if r2 == r1 goto 1f;"
	"r0 = 1;"
"1:"
	"exit;"
	:
	: __imm_insn(store_release_insn,
		     BPF_ATOMIC_OP(BPF_W, BPF_STORE_REL, BPF_REG_10, BPF_REG_1, -4))
	: __clobber_all);
}

SEC("socket")
__description("store-release, 64-bit")
__success __success_unpriv __retval(0)
__naked void store_release_64(void)
{
	asm volatile (
	"r0 = 0;"
	"r1 = 0x1234567890abcdef ll;"
	".8byte %[store_release_insn];" // store_release((u64 *)(r10 - 8), r1);
	"r2 = *(u64 *)(r10 - 8);"
	"if r2 == r1 goto 1f;"
	"r0 = 1;"
"1:"
	"exit;"
	:
	: __imm_insn(store_release_insn,
		     BPF_ATOMIC_OP(BPF_DW, BPF_STORE_REL, BPF_REG_10, BPF_REG_1, -8))
	: __clobber_all);
}

SEC("socket")
__description("store-release with uninitialized src_reg")
__failure __failure_unpriv __msg("R2 !read_ok")
__naked void store_release_with_uninitialized_src_reg(void)
{
	asm volatile (
	".8byte %[store_release_insn];" // store_release((u64 *)(r10 - 8), r2);
	"exit;"
	:
	: __imm_insn(store_release_insn,
		     BPF_ATOMIC_OP(BPF_DW, BPF_STORE_REL, BPF_REG_10, BPF_REG_2, -8))
	: __clobber_all);
}

SEC("socket")
__description("store-release with uninitialized dst_reg")
__failure __failure_unpriv __msg("R2 !read_ok")
__naked void store_release_with_uninitialized_dst_reg(void)
{
	asm volatile (
	"r1 = 0;"
	".8byte %[store_release_insn];" // store_release((u64 *)(r2 - 8), r1);
	"exit;"
	:
	: __imm_insn(store_release_insn,
		     BPF_ATOMIC_OP(BPF_DW, BPF_STORE_REL, BPF_REG_2, BPF_REG_1, -8))
	: __clobber_all);
}

SEC("socket")
__description("store-release with non-pointer dst_reg")
__failure __failure_unpriv __msg("R1 invalid mem access 'scalar'")
__naked void store_release_with_non_pointer_dst_reg(void)
{
	asm volatile (
	"r1 = 0;"
	".8byte %[store_release_insn];" // store_release((u64 *)(r1 + 0), r1);
	"exit;"
	:
	: __imm_insn(store_release_insn,
		     BPF_ATOMIC_OP(BPF_DW, BPF_STORE_REL, BPF_REG_1, BPF_REG_1, 0))
	: __clobber_all);
}

SEC("socket")
__description("misaligned store-release")
__failure __failure_unpriv __msg("misaligned stack access off")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void store_release_misaligned(void)
{
	asm volatile (
	"w0 = 0;"
	".8byte %[store_release_insn];" // store_release((u32 *)(r10 - 5), w0);
	"exit;"
	:
	: __imm_insn(store_release_insn,
		     BPF_ATOMIC_OP(BPF_W, BPF_STORE_REL, BPF_REG_10, BPF_REG_0, -5))
	: __clobber_all);
}

SEC("socket")
__description("store-release to ctx pointer")
__failure __failure_unpriv __msg("BPF_ATOMIC stores into R1 ctx is not allowed")
__naked void store_release_to_ctx_pointer(void)
{
	asm volatile (
	"w0 = 0;"
	// store_release((u8 *)(r1 + offsetof(struct __sk_buff, cb[0])), w0);
	".8byte %[store_release_insn];"
	"exit;"
	:
	: __imm_insn(store_release_insn,
		     BPF_ATOMIC_OP(BPF_B, BPF_STORE_REL, BPF_REG_1, BPF_REG_0,
				   offsetof(struct __sk_buff, cb[0])))
	: __clobber_all);
}

SEC("xdp")
__description("store-release to pkt pointer")
__failure __msg("BPF_ATOMIC stores into R2 pkt is not allowed")
__naked void store_release_to_pkt_pointer(void)
{
	asm volatile (
	"w0 = 0;"
	"r2 = *(u32 *)(r1 + %[xdp_md_data]);"
	"r3 = *(u32 *)(r1 + %[xdp_md_data_end]);"
	"r1 = r2;"
	"r1 += 8;"
	"if r1 >= r3 goto l0_%=;"
	".8byte %[store_release_insn];" // store_release((u8 *)(r2 + 0), w0);
"l0_%=:  r0 = 0;"
	"exit;"
	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end)),
	  __imm_insn(store_release_insn,
		     BPF_ATOMIC_OP(BPF_B, BPF_STORE_REL, BPF_REG_2, BPF_REG_0, 0))
	: __clobber_all);
}

SEC("flow_dissector")
__description("store-release to flow_keys pointer")
__failure __msg("BPF_ATOMIC stores into R2 flow_keys is not allowed")
__naked void store_release_to_flow_keys_pointer(void)
{
	asm volatile (
	"w0 = 0;"
	"r2 = *(u64 *)(r1 + %[__sk_buff_flow_keys]);"
	".8byte %[store_release_insn];" // store_release((u8 *)(r2 + 0), w0);
	"exit;"
	:
	: __imm_const(__sk_buff_flow_keys,
		      offsetof(struct __sk_buff, flow_keys)),
	  __imm_insn(store_release_insn,
		     BPF_ATOMIC_OP(BPF_B, BPF_STORE_REL, BPF_REG_2, BPF_REG_0, 0))
	: __clobber_all);
}

SEC("sk_reuseport")
__description("store-release to sock pointer")
__failure __msg("R2 cannot write into sock")
__naked void store_release_to_sock_pointer(void)
{
	asm volatile (
	"w0 = 0;"
	"r2 = *(u64 *)(r1 + %[sk_reuseport_md_sk]);"
	".8byte %[store_release_insn];" // store_release((u8 *)(r2 + 0), w0);
	"exit;"
	:
	: __imm_const(sk_reuseport_md_sk, offsetof(struct sk_reuseport_md, sk)),
	  __imm_insn(store_release_insn,
		     BPF_ATOMIC_OP(BPF_B, BPF_STORE_REL, BPF_REG_2, BPF_REG_0, 0))
	: __clobber_all);
}

SEC("socket")
__description("store-release, leak pointer to stack")
__success __success_unpriv __retval(0)
__naked void store_release_leak_pointer_to_stack(void)
{
	asm volatile (
	".8byte %[store_release_insn];" // store_release((u64 *)(r10 - 8), r1);
	"r0 = 0;"
	"exit;"
	:
	: __imm_insn(store_release_insn,
		     BPF_ATOMIC_OP(BPF_DW, BPF_STORE_REL, BPF_REG_10, BPF_REG_1, -8))
	: __clobber_all);
}

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1);
	__type(key, long long);
	__type(value, long long);
} map_hash_8b SEC(".maps");

SEC("socket")
__description("store-release, leak pointer to map")
__success __retval(0)
__failure_unpriv __msg_unpriv("R6 leaks addr into map")
__naked void store_release_leak_pointer_to_map(void)
{
	asm volatile (
	"r6 = r1;"
	"r1 = %[map_hash_8b] ll;"
	"r2 = 0;"
	"*(u64 *)(r10 - 8) = r2;"
	"r2 = r10;"
	"r2 += -8;"
	"call %[bpf_map_lookup_elem];"
	"if r0 == 0 goto l0_%=;"
	".8byte %[store_release_insn];" // store_release((u64 *)(r0 + 0), r6);
"l0_%=:"
	"r0 = 0;"
	"exit;"
	:
	: __imm_addr(map_hash_8b),
	  __imm(bpf_map_lookup_elem),
	  __imm_insn(store_release_insn,
		     BPF_ATOMIC_OP(BPF_DW, BPF_STORE_REL, BPF_REG_0, BPF_REG_6, 0))
	: __clobber_all);
}

SEC("socket")
__description("store-release with invalid register R15")
__failure __failure_unpriv __msg("R15 is invalid")
__naked void store_release_with_invalid_reg(void)
{
	asm volatile (
	".8byte %[store_release_insn];" // store_release((u64 *)(r15 + 0), r1);
	"exit;"
	:
	: __imm_insn(store_release_insn,
		     BPF_ATOMIC_OP(BPF_DW, BPF_STORE_REL, 15 /* invalid reg */, BPF_REG_1, 0))
	: __clobber_all);
}

#else /* CAN_USE_LOAD_ACQ_STORE_REL */

SEC("socket")
__description("Clang version < 18, ENABLE_ATOMICS_TESTS not defined, and/or JIT doesn't support store-release, use a dummy test")
__success
int dummy_test(void)
{
	return 0;
}

#endif /* CAN_USE_LOAD_ACQ_STORE_REL */

char _license[] SEC("license") = "GPL";
