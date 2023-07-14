// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/meta_access.c */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

SEC("xdp")
__description("meta access, test1")
__success __retval(0)
__naked void meta_access_test1(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data]);		\
	r0 = r2;					\
	r0 += 8;					\
	if r0 > r3 goto l0_%=;				\
	r0 = *(u8*)(r2 + 0);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

SEC("xdp")
__description("meta access, test2")
__failure __msg("invalid access to packet, off=-8")
__naked void meta_access_test2(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data]);		\
	r0 = r2;					\
	r0 -= 8;					\
	r4 = r2;					\
	r4 += 8;					\
	if r4 > r3 goto l0_%=;				\
	r0 = *(u8*)(r0 + 0);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

SEC("xdp")
__description("meta access, test3")
__failure __msg("invalid access to packet")
__naked void meta_access_test3(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r0 = r2;					\
	r0 += 8;					\
	if r0 > r3 goto l0_%=;				\
	r0 = *(u8*)(r2 + 0);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

SEC("xdp")
__description("meta access, test4")
__failure __msg("invalid access to packet")
__naked void meta_access_test4(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r4 = *(u32*)(r1 + %[xdp_md_data]);		\
	r0 = r4;					\
	r0 += 8;					\
	if r0 > r3 goto l0_%=;				\
	r0 = *(u8*)(r2 + 0);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

SEC("xdp")
__description("meta access, test5")
__failure __msg("R3 !read_ok")
__naked void meta_access_test5(void)
{
	asm volatile ("					\
	r3 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r4 = *(u32*)(r1 + %[xdp_md_data]);		\
	r0 = r3;					\
	r0 += 8;					\
	if r0 > r4 goto l0_%=;				\
	r2 = -8;					\
	call %[bpf_xdp_adjust_meta];			\
	r0 = *(u8*)(r3 + 0);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_xdp_adjust_meta),
	  __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

SEC("xdp")
__description("meta access, test6")
__failure __msg("invalid access to packet")
__naked void meta_access_test6(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data]);		\
	r0 = r3;					\
	r0 += 8;					\
	r4 = r2;					\
	r4 += 8;					\
	if r4 > r0 goto l0_%=;				\
	r0 = *(u8*)(r2 + 0);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

SEC("xdp")
__description("meta access, test7")
__success __retval(0)
__naked void meta_access_test7(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data]);		\
	r0 = r3;					\
	r0 += 8;					\
	r4 = r2;					\
	r4 += 8;					\
	if r4 > r3 goto l0_%=;				\
	r0 = *(u8*)(r2 + 0);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

SEC("xdp")
__description("meta access, test8")
__success __retval(0)
__naked void meta_access_test8(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data]);		\
	r4 = r2;					\
	r4 += 0xFFFF;					\
	if r4 > r3 goto l0_%=;				\
	r0 = *(u8*)(r2 + 0);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

SEC("xdp")
__description("meta access, test9")
__failure __msg("invalid access to packet")
__naked void meta_access_test9(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data]);		\
	r4 = r2;					\
	r4 += 0xFFFF;					\
	r4 += 1;					\
	if r4 > r3 goto l0_%=;				\
	r0 = *(u8*)(r2 + 0);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

SEC("xdp")
__description("meta access, test10")
__failure __msg("invalid access to packet")
__naked void meta_access_test10(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data]);		\
	r4 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r5 = 42;					\
	r6 = 24;					\
	*(u64*)(r10 - 8) = r5;				\
	lock *(u64 *)(r10 - 8) += r6;			\
	r5 = *(u64*)(r10 - 8);				\
	if r5 > 100 goto l0_%=;				\
	r3 += r5;					\
	r5 = r3;					\
	r6 = r2;					\
	r6 += 8;					\
	if r6 > r5 goto l0_%=;				\
	r2 = *(u8*)(r2 + 0);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

SEC("xdp")
__description("meta access, test11")
__success __retval(0)
__naked void meta_access_test11(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data]);		\
	r5 = 42;					\
	r6 = 24;					\
	*(u64*)(r10 - 8) = r5;				\
	lock *(u64 *)(r10 - 8) += r6;			\
	r5 = *(u64*)(r10 - 8);				\
	if r5 > 100 goto l0_%=;				\
	r2 += r5;					\
	r5 = r2;					\
	r6 = r2;					\
	r6 += 8;					\
	if r6 > r3 goto l0_%=;				\
	r5 = *(u8*)(r5 + 0);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

SEC("xdp")
__description("meta access, test12")
__success __retval(0)
__naked void meta_access_test12(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data]);		\
	r4 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r5 = r3;					\
	r5 += 16;					\
	if r5 > r4 goto l0_%=;				\
	r0 = *(u8*)(r3 + 0);				\
	r5 = r2;					\
	r5 += 16;					\
	if r5 > r3 goto l0_%=;				\
	r0 = *(u8*)(r2 + 0);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
