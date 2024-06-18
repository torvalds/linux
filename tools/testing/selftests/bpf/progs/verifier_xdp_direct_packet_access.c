// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/xdp_direct_packet_access.c */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

SEC("xdp")
__description("XDP pkt read, pkt_end mangling, bad access 1")
__failure __msg("R3 pointer arithmetic on pkt_end")
__naked void end_mangling_bad_access_1(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r1 = r2;					\
	r1 += 8;					\
	r3 += 8;					\
	if r1 > r3 goto l0_%=;				\
	r0 = *(u64*)(r1 - 8);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_end mangling, bad access 2")
__failure __msg("R3 pointer arithmetic on pkt_end")
__naked void end_mangling_bad_access_2(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r1 = r2;					\
	r1 += 8;					\
	r3 -= 8;					\
	if r1 > r3 goto l0_%=;				\
	r0 = *(u64*)(r1 - 8);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_data' > pkt_end, corner case, good access")
__success __retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void end_corner_case_good_access_1(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r1 = r2;					\
	r1 += 8;					\
	if r1 > r3 goto l0_%=;				\
	r0 = *(u64*)(r1 - 8);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_data' > pkt_end, bad access 1")
__failure __msg("R1 offset is outside of the packet")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void pkt_end_bad_access_1_1(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r1 = r2;					\
	r1 += 8;					\
	if r1 > r3 goto l0_%=;				\
	r0 = *(u64*)(r1 - 4);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_data' > pkt_end, bad access 2")
__failure __msg("R1 offset is outside of the packet")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void pkt_end_bad_access_2_1(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r1 = r2;					\
	r1 += 8;					\
	if r1 > r3 goto l0_%=;				\
l0_%=:	r0 = *(u64*)(r1 - 8);				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_data' > pkt_end, corner case +1, good access")
__success __retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void corner_case_1_good_access_1(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r1 = r2;					\
	r1 += 9;					\
	if r1 > r3 goto l0_%=;				\
	r0 = *(u64*)(r1 - 9);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_data' > pkt_end, corner case -1, bad access")
__failure __msg("R1 offset is outside of the packet")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void corner_case_1_bad_access_1(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r1 = r2;					\
	r1 += 7;					\
	if r1 > r3 goto l0_%=;				\
	r0 = *(u64*)(r1 - 7);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_end > pkt_data', good access")
__success __retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void end_pkt_data_good_access_1(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r1 = r2;					\
	r1 += 8;					\
	if r3 > r1 goto l0_%=;				\
	goto l1_%=;					\
l0_%=:	r0 = *(u32*)(r1 - 5);				\
l1_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_end > pkt_data', corner case -1, bad access")
__failure __msg("R1 offset is outside of the packet")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void corner_case_1_bad_access_2(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r1 = r2;					\
	r1 += 6;					\
	if r3 > r1 goto l0_%=;				\
	goto l1_%=;					\
l0_%=:	r0 = *(u64*)(r1 - 6);				\
l1_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_end > pkt_data', bad access 2")
__failure __msg("R1 offset is outside of the packet")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void pkt_data_bad_access_2_1(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r1 = r2;					\
	r1 += 8;					\
	if r3 > r1 goto l0_%=;				\
	r0 = *(u64*)(r1 - 8);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_end > pkt_data', corner case, good access")
__success __retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void data_corner_case_good_access_1(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r1 = r2;					\
	r1 += 7;					\
	if r3 > r1 goto l0_%=;				\
	goto l1_%=;					\
l0_%=:	r0 = *(u64*)(r1 - 7);				\
l1_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_end > pkt_data', corner case +1, good access")
__success __retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void corner_case_1_good_access_2(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r1 = r2;					\
	r1 += 8;					\
	if r3 > r1 goto l0_%=;				\
	goto l1_%=;					\
l0_%=:	r0 = *(u64*)(r1 - 8);				\
l1_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_data' < pkt_end, good access")
__success __retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void data_pkt_end_good_access_1(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r1 = r2;					\
	r1 += 8;					\
	if r1 < r3 goto l0_%=;				\
	goto l1_%=;					\
l0_%=:	r0 = *(u32*)(r1 - 5);				\
l1_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_data' < pkt_end, corner case -1, bad access")
__failure __msg("R1 offset is outside of the packet")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void corner_case_1_bad_access_3(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r1 = r2;					\
	r1 += 6;					\
	if r1 < r3 goto l0_%=;				\
	goto l1_%=;					\
l0_%=:	r0 = *(u64*)(r1 - 6);				\
l1_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_data' < pkt_end, bad access 2")
__failure __msg("R1 offset is outside of the packet")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void pkt_end_bad_access_2_2(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r1 = r2;					\
	r1 += 8;					\
	if r1 < r3 goto l0_%=;				\
	r0 = *(u64*)(r1 - 8);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_data' < pkt_end, corner case, good access")
__success __retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void end_corner_case_good_access_2(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r1 = r2;					\
	r1 += 7;					\
	if r1 < r3 goto l0_%=;				\
	goto l1_%=;					\
l0_%=:	r0 = *(u64*)(r1 - 7);				\
l1_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_data' < pkt_end, corner case +1, good access")
__success __retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void corner_case_1_good_access_3(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r1 = r2;					\
	r1 += 8;					\
	if r1 < r3 goto l0_%=;				\
	goto l1_%=;					\
l0_%=:	r0 = *(u64*)(r1 - 8);				\
l1_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_end < pkt_data', corner case, good access")
__success __retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void data_corner_case_good_access_2(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r1 = r2;					\
	r1 += 8;					\
	if r3 < r1 goto l0_%=;				\
	r0 = *(u64*)(r1 - 8);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_end < pkt_data', bad access 1")
__failure __msg("R1 offset is outside of the packet")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void pkt_data_bad_access_1_1(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r1 = r2;					\
	r1 += 8;					\
	if r3 < r1 goto l0_%=;				\
	r0 = *(u64*)(r1 - 4);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_end < pkt_data', bad access 2")
__failure __msg("R1 offset is outside of the packet")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void pkt_data_bad_access_2_2(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r1 = r2;					\
	r1 += 8;					\
	if r3 < r1 goto l0_%=;				\
l0_%=:	r0 = *(u64*)(r1 - 8);				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_end < pkt_data', corner case +1, good access")
__success __retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void corner_case_1_good_access_4(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r1 = r2;					\
	r1 += 9;					\
	if r3 < r1 goto l0_%=;				\
	r0 = *(u64*)(r1 - 9);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_end < pkt_data', corner case -1, bad access")
__failure __msg("R1 offset is outside of the packet")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void corner_case_1_bad_access_4(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r1 = r2;					\
	r1 += 7;					\
	if r3 < r1 goto l0_%=;				\
	r0 = *(u64*)(r1 - 7);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_data' >= pkt_end, good access")
__success __retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void data_pkt_end_good_access_2(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r1 = r2;					\
	r1 += 8;					\
	if r1 >= r3 goto l0_%=;				\
	r0 = *(u32*)(r1 - 5);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_data' >= pkt_end, corner case -1, bad access")
__failure __msg("R1 offset is outside of the packet")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void corner_case_1_bad_access_5(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r1 = r2;					\
	r1 += 6;					\
	if r1 >= r3 goto l0_%=;				\
	r0 = *(u64*)(r1 - 6);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_data' >= pkt_end, bad access 2")
__failure __msg("R1 offset is outside of the packet")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void pkt_end_bad_access_2_3(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r1 = r2;					\
	r1 += 8;					\
	if r1 >= r3 goto l0_%=;				\
l0_%=:	r0 = *(u32*)(r1 - 5);				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_data' >= pkt_end, corner case, good access")
__success __retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void end_corner_case_good_access_3(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r1 = r2;					\
	r1 += 7;					\
	if r1 >= r3 goto l0_%=;				\
	r0 = *(u64*)(r1 - 7);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_data' >= pkt_end, corner case +1, good access")
__success __retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void corner_case_1_good_access_5(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r1 = r2;					\
	r1 += 8;					\
	if r1 >= r3 goto l0_%=;				\
	r0 = *(u64*)(r1 - 8);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_end >= pkt_data', corner case, good access")
__success __retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void data_corner_case_good_access_3(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r1 = r2;					\
	r1 += 8;					\
	if r3 >= r1 goto l0_%=;				\
	goto l1_%=;					\
l0_%=:	r0 = *(u64*)(r1 - 8);				\
l1_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_end >= pkt_data', bad access 1")
__failure __msg("R1 offset is outside of the packet")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void pkt_data_bad_access_1_2(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r1 = r2;					\
	r1 += 8;					\
	if r3 >= r1 goto l0_%=;				\
	goto l1_%=;					\
l0_%=:	r0 = *(u64*)(r1 - 4);				\
l1_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_end >= pkt_data', bad access 2")
__failure __msg("R1 offset is outside of the packet")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void pkt_data_bad_access_2_3(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r1 = r2;					\
	r1 += 8;					\
	if r3 >= r1 goto l0_%=;				\
	r0 = *(u64*)(r1 - 8);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_end >= pkt_data', corner case +1, good access")
__success __retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void corner_case_1_good_access_6(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r1 = r2;					\
	r1 += 9;					\
	if r3 >= r1 goto l0_%=;				\
	goto l1_%=;					\
l0_%=:	r0 = *(u64*)(r1 - 9);				\
l1_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_end >= pkt_data', corner case -1, bad access")
__failure __msg("R1 offset is outside of the packet")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void corner_case_1_bad_access_6(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r1 = r2;					\
	r1 += 7;					\
	if r3 >= r1 goto l0_%=;				\
	goto l1_%=;					\
l0_%=:	r0 = *(u64*)(r1 - 7);				\
l1_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_data' <= pkt_end, corner case, good access")
__success __retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void end_corner_case_good_access_4(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r1 = r2;					\
	r1 += 8;					\
	if r1 <= r3 goto l0_%=;				\
	goto l1_%=;					\
l0_%=:	r0 = *(u64*)(r1 - 8);				\
l1_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_data' <= pkt_end, bad access 1")
__failure __msg("R1 offset is outside of the packet")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void pkt_end_bad_access_1_2(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r1 = r2;					\
	r1 += 8;					\
	if r1 <= r3 goto l0_%=;				\
	goto l1_%=;					\
l0_%=:	r0 = *(u64*)(r1 - 4);				\
l1_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_data' <= pkt_end, bad access 2")
__failure __msg("R1 offset is outside of the packet")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void pkt_end_bad_access_2_4(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r1 = r2;					\
	r1 += 8;					\
	if r1 <= r3 goto l0_%=;				\
	r0 = *(u64*)(r1 - 8);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_data' <= pkt_end, corner case +1, good access")
__success __retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void corner_case_1_good_access_7(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r1 = r2;					\
	r1 += 9;					\
	if r1 <= r3 goto l0_%=;				\
	goto l1_%=;					\
l0_%=:	r0 = *(u64*)(r1 - 9);				\
l1_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_data' <= pkt_end, corner case -1, bad access")
__failure __msg("R1 offset is outside of the packet")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void corner_case_1_bad_access_7(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r1 = r2;					\
	r1 += 7;					\
	if r1 <= r3 goto l0_%=;				\
	goto l1_%=;					\
l0_%=:	r0 = *(u64*)(r1 - 7);				\
l1_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_end <= pkt_data', good access")
__success __retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void end_pkt_data_good_access_2(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r1 = r2;					\
	r1 += 8;					\
	if r3 <= r1 goto l0_%=;				\
	r0 = *(u32*)(r1 - 5);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_end <= pkt_data', corner case -1, bad access")
__failure __msg("R1 offset is outside of the packet")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void corner_case_1_bad_access_8(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r1 = r2;					\
	r1 += 6;					\
	if r3 <= r1 goto l0_%=;				\
	r0 = *(u64*)(r1 - 6);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_end <= pkt_data', bad access 2")
__failure __msg("R1 offset is outside of the packet")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void pkt_data_bad_access_2_4(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r1 = r2;					\
	r1 += 8;					\
	if r3 <= r1 goto l0_%=;				\
l0_%=:	r0 = *(u32*)(r1 - 5);				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_end <= pkt_data', corner case, good access")
__success __retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void data_corner_case_good_access_4(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r1 = r2;					\
	r1 += 7;					\
	if r3 <= r1 goto l0_%=;				\
	r0 = *(u64*)(r1 - 7);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_end <= pkt_data', corner case +1, good access")
__success __retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void corner_case_1_good_access_8(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data_end]);		\
	r1 = r2;					\
	r1 += 8;					\
	if r3 <= r1 goto l0_%=;				\
	r0 = *(u64*)(r1 - 8);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_end, offsetof(struct xdp_md, data_end))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_meta' > pkt_data, corner case, good access")
__success __retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void data_corner_case_good_access_5(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data]);		\
	r1 = r2;					\
	r1 += 8;					\
	if r1 > r3 goto l0_%=;				\
	r0 = *(u64*)(r1 - 8);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_meta' > pkt_data, bad access 1")
__failure __msg("R1 offset is outside of the packet")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void pkt_data_bad_access_1_3(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data]);		\
	r1 = r2;					\
	r1 += 8;					\
	if r1 > r3 goto l0_%=;				\
	r0 = *(u64*)(r1 - 4);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_meta' > pkt_data, bad access 2")
__failure __msg("R1 offset is outside of the packet")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void pkt_data_bad_access_2_5(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data]);		\
	r1 = r2;					\
	r1 += 8;					\
	if r1 > r3 goto l0_%=;				\
l0_%=:	r0 = *(u64*)(r1 - 8);				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_meta' > pkt_data, corner case +1, good access")
__success __retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void corner_case_1_good_access_9(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data]);		\
	r1 = r2;					\
	r1 += 9;					\
	if r1 > r3 goto l0_%=;				\
	r0 = *(u64*)(r1 - 9);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_meta' > pkt_data, corner case -1, bad access")
__failure __msg("R1 offset is outside of the packet")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void corner_case_1_bad_access_9(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data]);		\
	r1 = r2;					\
	r1 += 7;					\
	if r1 > r3 goto l0_%=;				\
	r0 = *(u64*)(r1 - 7);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_data > pkt_meta', good access")
__success __retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void data_pkt_meta_good_access_1(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data]);		\
	r1 = r2;					\
	r1 += 8;					\
	if r3 > r1 goto l0_%=;				\
	goto l1_%=;					\
l0_%=:	r0 = *(u32*)(r1 - 5);				\
l1_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_data > pkt_meta', corner case -1, bad access")
__failure __msg("R1 offset is outside of the packet")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void corner_case_1_bad_access_10(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data]);		\
	r1 = r2;					\
	r1 += 6;					\
	if r3 > r1 goto l0_%=;				\
	goto l1_%=;					\
l0_%=:	r0 = *(u64*)(r1 - 6);				\
l1_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_data > pkt_meta', bad access 2")
__failure __msg("R1 offset is outside of the packet")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void pkt_meta_bad_access_2_1(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data]);		\
	r1 = r2;					\
	r1 += 8;					\
	if r3 > r1 goto l0_%=;				\
	r0 = *(u64*)(r1 - 8);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_data > pkt_meta', corner case, good access")
__success __retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void meta_corner_case_good_access_1(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data]);		\
	r1 = r2;					\
	r1 += 7;					\
	if r3 > r1 goto l0_%=;				\
	goto l1_%=;					\
l0_%=:	r0 = *(u64*)(r1 - 7);				\
l1_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_data > pkt_meta', corner case +1, good access")
__success __retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void corner_case_1_good_access_10(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data]);		\
	r1 = r2;					\
	r1 += 8;					\
	if r3 > r1 goto l0_%=;				\
	goto l1_%=;					\
l0_%=:	r0 = *(u64*)(r1 - 8);				\
l1_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_meta' < pkt_data, good access")
__success __retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void meta_pkt_data_good_access_1(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data]);		\
	r1 = r2;					\
	r1 += 8;					\
	if r1 < r3 goto l0_%=;				\
	goto l1_%=;					\
l0_%=:	r0 = *(u32*)(r1 - 5);				\
l1_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_meta' < pkt_data, corner case -1, bad access")
__failure __msg("R1 offset is outside of the packet")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void corner_case_1_bad_access_11(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data]);		\
	r1 = r2;					\
	r1 += 6;					\
	if r1 < r3 goto l0_%=;				\
	goto l1_%=;					\
l0_%=:	r0 = *(u64*)(r1 - 6);				\
l1_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_meta' < pkt_data, bad access 2")
__failure __msg("R1 offset is outside of the packet")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void pkt_data_bad_access_2_6(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data]);		\
	r1 = r2;					\
	r1 += 8;					\
	if r1 < r3 goto l0_%=;				\
	r0 = *(u64*)(r1 - 8);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_meta' < pkt_data, corner case, good access")
__success __retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void data_corner_case_good_access_6(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data]);		\
	r1 = r2;					\
	r1 += 7;					\
	if r1 < r3 goto l0_%=;				\
	goto l1_%=;					\
l0_%=:	r0 = *(u64*)(r1 - 7);				\
l1_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_meta' < pkt_data, corner case +1, good access")
__success __retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void corner_case_1_good_access_11(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data]);		\
	r1 = r2;					\
	r1 += 8;					\
	if r1 < r3 goto l0_%=;				\
	goto l1_%=;					\
l0_%=:	r0 = *(u64*)(r1 - 8);				\
l1_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_data < pkt_meta', corner case, good access")
__success __retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void meta_corner_case_good_access_2(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data]);		\
	r1 = r2;					\
	r1 += 8;					\
	if r3 < r1 goto l0_%=;				\
	r0 = *(u64*)(r1 - 8);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_data < pkt_meta', bad access 1")
__failure __msg("R1 offset is outside of the packet")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void pkt_meta_bad_access_1_1(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data]);		\
	r1 = r2;					\
	r1 += 8;					\
	if r3 < r1 goto l0_%=;				\
	r0 = *(u64*)(r1 - 4);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_data < pkt_meta', bad access 2")
__failure __msg("R1 offset is outside of the packet")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void pkt_meta_bad_access_2_2(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data]);		\
	r1 = r2;					\
	r1 += 8;					\
	if r3 < r1 goto l0_%=;				\
l0_%=:	r0 = *(u64*)(r1 - 8);				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_data < pkt_meta', corner case +1, good access")
__success __retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void corner_case_1_good_access_12(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data]);		\
	r1 = r2;					\
	r1 += 9;					\
	if r3 < r1 goto l0_%=;				\
	r0 = *(u64*)(r1 - 9);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_data < pkt_meta', corner case -1, bad access")
__failure __msg("R1 offset is outside of the packet")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void corner_case_1_bad_access_12(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data]);		\
	r1 = r2;					\
	r1 += 7;					\
	if r3 < r1 goto l0_%=;				\
	r0 = *(u64*)(r1 - 7);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_meta' >= pkt_data, good access")
__success __retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void meta_pkt_data_good_access_2(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data]);		\
	r1 = r2;					\
	r1 += 8;					\
	if r1 >= r3 goto l0_%=;				\
	r0 = *(u32*)(r1 - 5);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_meta' >= pkt_data, corner case -1, bad access")
__failure __msg("R1 offset is outside of the packet")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void corner_case_1_bad_access_13(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data]);		\
	r1 = r2;					\
	r1 += 6;					\
	if r1 >= r3 goto l0_%=;				\
	r0 = *(u64*)(r1 - 6);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_meta' >= pkt_data, bad access 2")
__failure __msg("R1 offset is outside of the packet")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void pkt_data_bad_access_2_7(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data]);		\
	r1 = r2;					\
	r1 += 8;					\
	if r1 >= r3 goto l0_%=;				\
l0_%=:	r0 = *(u32*)(r1 - 5);				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_meta' >= pkt_data, corner case, good access")
__success __retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void data_corner_case_good_access_7(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data]);		\
	r1 = r2;					\
	r1 += 7;					\
	if r1 >= r3 goto l0_%=;				\
	r0 = *(u64*)(r1 - 7);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_meta' >= pkt_data, corner case +1, good access")
__success __retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void corner_case_1_good_access_13(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data]);		\
	r1 = r2;					\
	r1 += 8;					\
	if r1 >= r3 goto l0_%=;				\
	r0 = *(u64*)(r1 - 8);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_data >= pkt_meta', corner case, good access")
__success __retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void meta_corner_case_good_access_3(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data]);		\
	r1 = r2;					\
	r1 += 8;					\
	if r3 >= r1 goto l0_%=;				\
	goto l1_%=;					\
l0_%=:	r0 = *(u64*)(r1 - 8);				\
l1_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_data >= pkt_meta', bad access 1")
__failure __msg("R1 offset is outside of the packet")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void pkt_meta_bad_access_1_2(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data]);		\
	r1 = r2;					\
	r1 += 8;					\
	if r3 >= r1 goto l0_%=;				\
	goto l1_%=;					\
l0_%=:	r0 = *(u64*)(r1 - 4);				\
l1_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_data >= pkt_meta', bad access 2")
__failure __msg("R1 offset is outside of the packet")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void pkt_meta_bad_access_2_3(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data]);		\
	r1 = r2;					\
	r1 += 8;					\
	if r3 >= r1 goto l0_%=;				\
	r0 = *(u64*)(r1 - 8);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_data >= pkt_meta', corner case +1, good access")
__success __retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void corner_case_1_good_access_14(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data]);		\
	r1 = r2;					\
	r1 += 9;					\
	if r3 >= r1 goto l0_%=;				\
	goto l1_%=;					\
l0_%=:	r0 = *(u64*)(r1 - 9);				\
l1_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_data >= pkt_meta', corner case -1, bad access")
__failure __msg("R1 offset is outside of the packet")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void corner_case_1_bad_access_14(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data]);		\
	r1 = r2;					\
	r1 += 7;					\
	if r3 >= r1 goto l0_%=;				\
	goto l1_%=;					\
l0_%=:	r0 = *(u64*)(r1 - 7);				\
l1_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_meta' <= pkt_data, corner case, good access")
__success __retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void data_corner_case_good_access_8(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data]);		\
	r1 = r2;					\
	r1 += 8;					\
	if r1 <= r3 goto l0_%=;				\
	goto l1_%=;					\
l0_%=:	r0 = *(u64*)(r1 - 8);				\
l1_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_meta' <= pkt_data, bad access 1")
__failure __msg("R1 offset is outside of the packet")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void pkt_data_bad_access_1_4(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data]);		\
	r1 = r2;					\
	r1 += 8;					\
	if r1 <= r3 goto l0_%=;				\
	goto l1_%=;					\
l0_%=:	r0 = *(u64*)(r1 - 4);				\
l1_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_meta' <= pkt_data, bad access 2")
__failure __msg("R1 offset is outside of the packet")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void pkt_data_bad_access_2_8(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data]);		\
	r1 = r2;					\
	r1 += 8;					\
	if r1 <= r3 goto l0_%=;				\
	r0 = *(u64*)(r1 - 8);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_meta' <= pkt_data, corner case +1, good access")
__success __retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void corner_case_1_good_access_15(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data]);		\
	r1 = r2;					\
	r1 += 9;					\
	if r1 <= r3 goto l0_%=;				\
	goto l1_%=;					\
l0_%=:	r0 = *(u64*)(r1 - 9);				\
l1_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_meta' <= pkt_data, corner case -1, bad access")
__failure __msg("R1 offset is outside of the packet")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void corner_case_1_bad_access_15(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data]);		\
	r1 = r2;					\
	r1 += 7;					\
	if r1 <= r3 goto l0_%=;				\
	goto l1_%=;					\
l0_%=:	r0 = *(u64*)(r1 - 7);				\
l1_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_data <= pkt_meta', good access")
__success __retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void data_pkt_meta_good_access_2(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data]);		\
	r1 = r2;					\
	r1 += 8;					\
	if r3 <= r1 goto l0_%=;				\
	r0 = *(u32*)(r1 - 5);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_data <= pkt_meta', corner case -1, bad access")
__failure __msg("R1 offset is outside of the packet")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void corner_case_1_bad_access_16(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data]);		\
	r1 = r2;					\
	r1 += 6;					\
	if r3 <= r1 goto l0_%=;				\
	r0 = *(u64*)(r1 - 6);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_data <= pkt_meta', bad access 2")
__failure __msg("R1 offset is outside of the packet")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void pkt_meta_bad_access_2_4(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data]);		\
	r1 = r2;					\
	r1 += 8;					\
	if r3 <= r1 goto l0_%=;				\
l0_%=:	r0 = *(u32*)(r1 - 5);				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_data <= pkt_meta', corner case, good access")
__success __retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void meta_corner_case_good_access_4(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data]);		\
	r1 = r2;					\
	r1 += 7;					\
	if r3 <= r1 goto l0_%=;				\
	r0 = *(u64*)(r1 - 7);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

SEC("xdp")
__description("XDP pkt read, pkt_data <= pkt_meta', corner case +1, good access")
__success __retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void corner_case_1_good_access_16(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[xdp_md_data_meta]);		\
	r3 = *(u32*)(r1 + %[xdp_md_data]);		\
	r1 = r2;					\
	r1 += 8;					\
	if r3 <= r1 goto l0_%=;				\
	r0 = *(u64*)(r1 - 8);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(xdp_md_data, offsetof(struct xdp_md, data)),
	  __imm_const(xdp_md_data_meta, offsetof(struct xdp_md, data_meta))
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
