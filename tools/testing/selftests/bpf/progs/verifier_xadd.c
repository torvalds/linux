// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/xadd.c */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1);
	__type(key, long long);
	__type(value, long long);
} map_hash_8b SEC(".maps");

SEC("tc")
__description("xadd/w check unaligned stack")
__failure __msg("misaligned stack access off")
__naked void xadd_w_check_unaligned_stack(void)
{
	asm volatile ("					\
	r0 = 1;						\
	*(u64*)(r10 - 8) = r0;				\
	lock *(u32 *)(r10 - 7) += w0;			\
	r0 = *(u64*)(r10 - 8);				\
	exit;						\
"	::: __clobber_all);
}

SEC("tc")
__description("xadd/w check unaligned map")
__failure __msg("misaligned value access off")
__naked void xadd_w_check_unaligned_map(void)
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
l0_%=:	r1 = 1;						\
	lock *(u32 *)(r0 + 3) += w1;			\
	r0 = *(u32*)(r0 + 3);				\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("xdp")
__description("xadd/w check unaligned pkt")
__failure __msg("BPF_ATOMIC stores into R2 pkt is not allowed")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void xadd_w_check_unaligned_pkt(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r1 = r2;					\
	r1 += 8;					\
	if r1 < r3 goto l0_%=;				\
	r0 = 99;					\
	goto l1_%=;					\
l0_%=:	r0 = 1;						\
	r1 = 0;						\
	*(u32*)(r2 + 0) = r1;				\
	r1 = 0;						\
	*(u32*)(r2 + 3) = r1;				\
	lock *(u32 *)(r2 + 1) += w0;			\
	lock *(u32 *)(r2 + 2) += w0;			\
	r0 = *(u32*)(r2 + 1);				\
l1_%=:	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("tc")
__description("xadd/w check whether src/dst got mangled, 1")
__success __retval(3)
__naked void src_dst_got_mangled_1(void)
{
	asm volatile ("					\
	r0 = 1;						\
	r6 = r0;					\
	r7 = r10;					\
	*(u64*)(r10 - 8) = r0;				\
	lock *(u64 *)(r10 - 8) += r0;			\
	lock *(u64 *)(r10 - 8) += r0;			\
	if r6 != r0 goto l0_%=;				\
	if r7 != r10 goto l0_%=;			\
	r0 = *(u64*)(r10 - 8);				\
	exit;						\
l0_%=:	r0 = 42;					\
	exit;						\
"	::: __clobber_all);
}

SEC("tc")
__description("xadd/w check whether src/dst got mangled, 2")
__success __retval(3)
__naked void src_dst_got_mangled_2(void)
{
	asm volatile ("					\
	r0 = 1;						\
	r6 = r0;					\
	r7 = r10;					\
	*(u32*)(r10 - 8) = r0;				\
	lock *(u32 *)(r10 - 8) += w0;			\
	lock *(u32 *)(r10 - 8) += w0;			\
	if r6 != r0 goto l0_%=;				\
	if r7 != r10 goto l0_%=;			\
	r0 = *(u32*)(r10 - 8);				\
	exit;						\
l0_%=:	r0 = 42;					\
	exit;						\
"	::: __clobber_all);
}

char _license[] SEC("license") = "GPL";
