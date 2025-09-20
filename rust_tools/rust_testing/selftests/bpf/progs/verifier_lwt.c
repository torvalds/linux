// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/lwt.c */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

SEC("lwt_in")
__description("invalid direct packet write for LWT_IN")
__failure __msg("cannot write into packet")
__naked void packet_write_for_lwt_in(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r3 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r0 = r2;					\
	r0 += 8;					\
	if r0 > r3 goto l0_%=;				\
	*(u8*)(r2 + 0) = r2;				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("lwt_out")
__description("invalid direct packet write for LWT_OUT")
__failure __msg("cannot write into packet")
__naked void packet_write_for_lwt_out(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r3 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r0 = r2;					\
	r0 += 8;					\
	if r0 > r3 goto l0_%=;				\
	*(u8*)(r2 + 0) = r2;				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("lwt_xmit")
__description("direct packet write for LWT_XMIT")
__success __retval(0)
__naked void packet_write_for_lwt_xmit(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r3 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r0 = r2;					\
	r0 += 8;					\
	if r0 > r3 goto l0_%=;				\
	*(u8*)(r2 + 0) = r2;				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("lwt_in")
__description("direct packet read for LWT_IN")
__success __retval(0)
__naked void packet_read_for_lwt_in(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r3 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r0 = r2;					\
	r0 += 8;					\
	if r0 > r3 goto l0_%=;				\
	r0 = *(u8*)(r2 + 0);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("lwt_out")
__description("direct packet read for LWT_OUT")
__success __retval(0)
__naked void packet_read_for_lwt_out(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r3 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r0 = r2;					\
	r0 += 8;					\
	if r0 > r3 goto l0_%=;				\
	r0 = *(u8*)(r2 + 0);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("lwt_xmit")
__description("direct packet read for LWT_XMIT")
__success __retval(0)
__naked void packet_read_for_lwt_xmit(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r3 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r0 = r2;					\
	r0 += 8;					\
	if r0 > r3 goto l0_%=;				\
	r0 = *(u8*)(r2 + 0);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("lwt_xmit")
__description("overlapping checks for direct packet access")
__success __retval(0)
__naked void checks_for_direct_packet_access(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r3 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r0 = r2;					\
	r0 += 8;					\
	if r0 > r3 goto l0_%=;				\
	r1 = r2;					\
	r1 += 6;					\
	if r1 > r3 goto l0_%=;				\
	r0 = *(u16*)(r2 + 6);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("lwt_xmit")
__description("make headroom for LWT_XMIT")
__success __retval(0)
__naked void make_headroom_for_lwt_xmit(void)
{
	asm volatile ("					\
	r6 = r1;					\
	r2 = 34;					\
	r3 = 0;						\
	call %[bpf_skb_change_head];			\
	/* split for s390 to succeed */			\
	r1 = r6;					\
	r2 = 42;					\
	r3 = 0;						\
	call %[bpf_skb_change_head];			\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_skb_change_head)
	: __clobber_all);
}

SEC("socket")
__description("invalid access of tc_classid for LWT_IN")
__failure __msg("invalid bpf_context access")
__failure_unpriv
__naked void tc_classid_for_lwt_in(void)
{
	asm volatile ("					\
	r0 = *(u32*)(r1 + %[__sk_buff_tc_classid]);	\
	exit;						\
"	:
	: __imm_const(__sk_buff_tc_classid, offsetof(struct __sk_buff, tc_classid))
	: __clobber_all);
}

SEC("socket")
__description("invalid access of tc_classid for LWT_OUT")
__failure __msg("invalid bpf_context access")
__failure_unpriv
__naked void tc_classid_for_lwt_out(void)
{
	asm volatile ("					\
	r0 = *(u32*)(r1 + %[__sk_buff_tc_classid]);	\
	exit;						\
"	:
	: __imm_const(__sk_buff_tc_classid, offsetof(struct __sk_buff, tc_classid))
	: __clobber_all);
}

SEC("socket")
__description("invalid access of tc_classid for LWT_XMIT")
__failure __msg("invalid bpf_context access")
__failure_unpriv
__naked void tc_classid_for_lwt_xmit(void)
{
	asm volatile ("					\
	r0 = *(u32*)(r1 + %[__sk_buff_tc_classid]);	\
	exit;						\
"	:
	: __imm_const(__sk_buff_tc_classid, offsetof(struct __sk_buff, tc_classid))
	: __clobber_all);
}

SEC("lwt_in")
__description("check skb->tc_classid half load not permitted for lwt prog")
__failure __msg("invalid bpf_context access")
__naked void not_permitted_for_lwt_prog(void)
{
	asm volatile (
	"r0 = 0;"
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	"r0 = *(u16*)(r1 + %[__sk_buff_tc_classid]);"
#else
	"r0 = *(u16*)(r1 + %[__imm_0]);"
#endif
	"exit;"
	:
	: __imm_const(__imm_0, offsetof(struct __sk_buff, tc_classid) + 2),
	  __imm_const(__sk_buff_tc_classid, offsetof(struct __sk_buff, tc_classid))
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
