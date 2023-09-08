// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/helper_packet_access.c */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1);
	__type(key, long long);
	__type(value, long long);
} map_hash_8b SEC(".maps");

SEC("xdp")
__description("helper access to packet: test1, valid packet_ptr range")
__success __retval(0)
__naked void test1_valid_packet_ptr_range(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r1 = r2;					\
	r1 += 8;					\
	if r1 > r3 goto l0_%=;				\
	r1 = %[map_hash_8b] ll;				\
	r3 = r2;					\
	r4 = 0;						\
	call %[bpf_map_update_elem];			\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_update_elem),
	  __imm_addr(map_hash_8b),
	  __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("xdp")
__description("helper access to packet: test2, unchecked packet_ptr")
__failure __msg("invalid access to packet")
__naked void packet_test2_unchecked_packet_ptr(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b),
	  __imm_const(xdp_md_data, offsetof(struct xdp_md, data))
	: __clobber_all);
}

SEC("xdp")
__description("helper access to packet: test3, variable add")
__success __retval(0)
__naked void to_packet_test3_variable_add(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r4 = r2;					\
	r4 += 8;					\
	if r4 > r3 goto l0_%=;				\
	r5 = *(u8*)(r2 + 0);				\
	r4 = r2;					\
	r4 += r5;					\
	r5 = r4;					\
	r5 += 8;					\
	if r5 > r3 goto l0_%=;				\
	r1 = %[map_hash_8b] ll;				\
	r2 = r4;					\
	call %[bpf_map_lookup_elem];			\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b),
	  __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("xdp")
__description("helper access to packet: test4, packet_ptr with bad range")
__failure __msg("invalid access to packet")
__naked void packet_ptr_with_bad_range_1(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r4 = r2;					\
	r4 += 4;					\
	if r4 > r3 goto l0_%=;				\
	r0 = 0;						\
	exit;						\
l0_%=:	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b),
	  __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("xdp")
__description("helper access to packet: test5, packet_ptr with too short range")
__failure __msg("invalid access to packet")
__naked void ptr_with_too_short_range_1(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r2 += 1;					\
	r4 = r2;					\
	r4 += 7;					\
	if r4 > r3 goto l0_%=;				\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b),
	  __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("tc")
__description("helper access to packet: test6, cls valid packet_ptr range")
__success __retval(0)
__naked void cls_valid_packet_ptr_range(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r3 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r1 = r2;					\
	r1 += 8;					\
	if r1 > r3 goto l0_%=;				\
	r1 = %[map_hash_8b] ll;				\
	r3 = r2;					\
	r4 = 0;						\
	call %[bpf_map_update_elem];			\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_update_elem),
	  __imm_addr(map_hash_8b),
	  __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tc")
__description("helper access to packet: test7, cls unchecked packet_ptr")
__failure __msg("invalid access to packet")
__naked void test7_cls_unchecked_packet_ptr(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b),
	  __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data))
	: __clobber_all);
}

SEC("tc")
__description("helper access to packet: test8, cls variable add")
__success __retval(0)
__naked void packet_test8_cls_variable_add(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r3 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r4 = r2;					\
	r4 += 8;					\
	if r4 > r3 goto l0_%=;				\
	r5 = *(u8*)(r2 + 0);				\
	r4 = r2;					\
	r4 += r5;					\
	r5 = r4;					\
	r5 += 8;					\
	if r5 > r3 goto l0_%=;				\
	r1 = %[map_hash_8b] ll;				\
	r2 = r4;					\
	call %[bpf_map_lookup_elem];			\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b),
	  __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tc")
__description("helper access to packet: test9, cls packet_ptr with bad range")
__failure __msg("invalid access to packet")
__naked void packet_ptr_with_bad_range_2(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r3 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r4 = r2;					\
	r4 += 4;					\
	if r4 > r3 goto l0_%=;				\
	r0 = 0;						\
	exit;						\
l0_%=:	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b),
	  __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tc")
__description("helper access to packet: test10, cls packet_ptr with too short range")
__failure __msg("invalid access to packet")
__naked void ptr_with_too_short_range_2(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r3 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r2 += 1;					\
	r4 = r2;					\
	r4 += 7;					\
	if r4 > r3 goto l0_%=;				\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b),
	  __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tc")
__description("helper access to packet: test11, cls unsuitable helper 1")
__failure __msg("helper access to the packet")
__naked void test11_cls_unsuitable_helper_1(void)
{
	asm volatile ("					\
	r6 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r7 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r6 += 1;					\
	r3 = r6;					\
	r3 += 7;					\
	if r3 > r7 goto l0_%=;				\
	r2 = 0;						\
	r4 = 42;					\
	r5 = 0;						\
	call %[bpf_skb_store_bytes];			\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_skb_store_bytes),
	  __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tc")
__description("helper access to packet: test12, cls unsuitable helper 2")
__failure __msg("helper access to the packet")
__naked void test12_cls_unsuitable_helper_2(void)
{
	asm volatile ("					\
	r6 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r7 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r3 = r6;					\
	r6 += 8;					\
	if r6 > r7 goto l0_%=;				\
	r2 = 0;						\
	r4 = 4;						\
	call %[bpf_skb_load_bytes];			\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_skb_load_bytes),
	  __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tc")
__description("helper access to packet: test13, cls helper ok")
__success __retval(0)
__naked void packet_test13_cls_helper_ok(void)
{
	asm volatile ("					\
	r6 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r7 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r6 += 1;					\
	r1 = r6;					\
	r1 += 7;					\
	if r1 > r7 goto l0_%=;				\
	r1 = r6;					\
	r2 = 4;						\
	r3 = 0;						\
	r4 = 0;						\
	r5 = 0;						\
	call %[bpf_csum_diff];				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_csum_diff),
	  __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tc")
__description("helper access to packet: test14, cls helper ok sub")
__success __retval(0)
__naked void test14_cls_helper_ok_sub(void)
{
	asm volatile ("					\
	r6 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r7 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r6 += 1;					\
	r1 = r6;					\
	r1 += 7;					\
	if r1 > r7 goto l0_%=;				\
	r1 -= 4;					\
	r2 = 4;						\
	r3 = 0;						\
	r4 = 0;						\
	r5 = 0;						\
	call %[bpf_csum_diff];				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_csum_diff),
	  __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tc")
__description("helper access to packet: test15, cls helper fail sub")
__failure __msg("invalid access to packet")
__naked void test15_cls_helper_fail_sub(void)
{
	asm volatile ("					\
	r6 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r7 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r6 += 1;					\
	r1 = r6;					\
	r1 += 7;					\
	if r1 > r7 goto l0_%=;				\
	r1 -= 12;					\
	r2 = 4;						\
	r3 = 0;						\
	r4 = 0;						\
	r5 = 0;						\
	call %[bpf_csum_diff];				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_csum_diff),
	  __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tc")
__description("helper access to packet: test16, cls helper fail range 1")
__failure __msg("invalid access to packet")
__naked void cls_helper_fail_range_1(void)
{
	asm volatile ("					\
	r6 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r7 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r6 += 1;					\
	r1 = r6;					\
	r1 += 7;					\
	if r1 > r7 goto l0_%=;				\
	r1 = r6;					\
	r2 = 8;						\
	r3 = 0;						\
	r4 = 0;						\
	r5 = 0;						\
	call %[bpf_csum_diff];				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_csum_diff),
	  __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tc")
__description("helper access to packet: test17, cls helper fail range 2")
__failure __msg("R2 min value is negative")
__naked void cls_helper_fail_range_2(void)
{
	asm volatile ("					\
	r6 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r7 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r6 += 1;					\
	r1 = r6;					\
	r1 += 7;					\
	if r1 > r7 goto l0_%=;				\
	r1 = r6;					\
	r2 = -9;					\
	r3 = 0;						\
	r4 = 0;						\
	r5 = 0;						\
	call %[bpf_csum_diff];				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_csum_diff),
	  __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tc")
__description("helper access to packet: test18, cls helper fail range 3")
__failure __msg("R2 min value is negative")
__naked void cls_helper_fail_range_3(void)
{
	asm volatile ("					\
	r6 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r7 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r6 += 1;					\
	r1 = r6;					\
	r1 += 7;					\
	if r1 > r7 goto l0_%=;				\
	r1 = r6;					\
	r2 = %[__imm_0];				\
	r3 = 0;						\
	r4 = 0;						\
	r5 = 0;						\
	call %[bpf_csum_diff];				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_csum_diff),
	  __imm_const(__imm_0, ~0),
	  __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tc")
__description("helper access to packet: test19, cls helper range zero")
__success __retval(0)
__naked void test19_cls_helper_range_zero(void)
{
	asm volatile ("					\
	r6 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r7 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r6 += 1;					\
	r1 = r6;					\
	r1 += 7;					\
	if r1 > r7 goto l0_%=;				\
	r1 = r6;					\
	r2 = 0;						\
	r3 = 0;						\
	r4 = 0;						\
	r5 = 0;						\
	call %[bpf_csum_diff];				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_csum_diff),
	  __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tc")
__description("helper access to packet: test20, pkt end as input")
__failure __msg("R1 type=pkt_end expected=fp")
__naked void test20_pkt_end_as_input(void)
{
	asm volatile ("					\
	r6 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r7 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r6 += 1;					\
	r1 = r6;					\
	r1 += 7;					\
	if r1 > r7 goto l0_%=;				\
	r1 = r7;					\
	r2 = 4;						\
	r3 = 0;						\
	r4 = 0;						\
	r5 = 0;						\
	call %[bpf_csum_diff];				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_csum_diff),
	  __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tc")
__description("helper access to packet: test21, wrong reg")
__failure __msg("invalid access to packet")
__naked void to_packet_test21_wrong_reg(void)
{
	asm volatile ("					\
	r6 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r7 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r6 += 1;					\
	r1 = r6;					\
	r1 += 7;					\
	if r1 > r7 goto l0_%=;				\
	r2 = 4;						\
	r3 = 0;						\
	r4 = 0;						\
	r5 = 0;						\
	call %[bpf_csum_diff];				\
	r0 = 0;						\
l0_%=:	exit;						\
"	:
	: __imm(bpf_csum_diff),
	  __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
