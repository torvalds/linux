// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/ctx.c */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

SEC("tc")
__description("context stores via BPF_ATOMIC")
__failure __msg("BPF_ATOMIC stores into R1 ctx is not allowed")
__naked void context_stores_via_bpf_atomic(void)
{
	asm volatile ("					\
	r0 = 0;						\
	lock *(u32 *)(r1 + %[__sk_buff_mark]) += w0;	\
	exit;						\
"	:
	: __imm_const(__sk_buff_mark, offsetof(struct __sk_buff, mark))
	: __clobber_all);
}

SEC("tc")
__description("arithmetic ops make PTR_TO_CTX unusable")
__failure __msg("dereference of modified ctx ptr")
__naked void make_ptr_to_ctx_unusable(void)
{
	asm volatile ("					\
	r1 += %[__imm_0];				\
	r0 = *(u32*)(r1 + %[__sk_buff_mark]);		\
	exit;						\
"	:
	: __imm_const(__imm_0,
		      offsetof(struct __sk_buff, data) - offsetof(struct __sk_buff, mark)),
	  __imm_const(__sk_buff_mark, offsetof(struct __sk_buff, mark))
	: __clobber_all);
}

SEC("tc")
__description("pass unmodified ctx pointer to helper")
__success __retval(0)
__naked void unmodified_ctx_pointer_to_helper(void)
{
	asm volatile ("					\
	r2 = 0;						\
	call %[bpf_csum_update];			\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_csum_update)
	: __clobber_all);
}

SEC("tc")
__description("pass modified ctx pointer to helper, 1")
__failure __msg("negative offset ctx ptr R1 off=-612 disallowed")
__naked void ctx_pointer_to_helper_1(void)
{
	asm volatile ("					\
	r1 += -612;					\
	r2 = 0;						\
	call %[bpf_csum_update];			\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_csum_update)
	: __clobber_all);
}

SEC("socket")
__description("pass modified ctx pointer to helper, 2")
__failure __msg("negative offset ctx ptr R1 off=-612 disallowed")
__failure_unpriv __msg_unpriv("negative offset ctx ptr R1 off=-612 disallowed")
__naked void ctx_pointer_to_helper_2(void)
{
	asm volatile ("					\
	r1 += -612;					\
	call %[bpf_get_socket_cookie];			\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_socket_cookie)
	: __clobber_all);
}

SEC("tc")
__description("pass modified ctx pointer to helper, 3")
__failure __msg("variable ctx access var_off=(0x0; 0x4)")
__naked void ctx_pointer_to_helper_3(void)
{
	asm volatile ("					\
	r3 = *(u32*)(r1 + 0);				\
	r3 &= 4;					\
	r1 += r3;					\
	r2 = 0;						\
	call %[bpf_csum_update];			\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_csum_update)
	: __clobber_all);
}

SEC("cgroup/sendmsg6")
__description("pass ctx or null check, 1: ctx")
__success
__naked void or_null_check_1_ctx(void)
{
	asm volatile ("					\
	call %[bpf_get_netns_cookie];			\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_netns_cookie)
	: __clobber_all);
}

SEC("cgroup/sendmsg6")
__description("pass ctx or null check, 2: null")
__success
__naked void or_null_check_2_null(void)
{
	asm volatile ("					\
	r1 = 0;						\
	call %[bpf_get_netns_cookie];			\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_netns_cookie)
	: __clobber_all);
}

SEC("cgroup/sendmsg6")
__description("pass ctx or null check, 3: 1")
__failure __msg("R1 type=scalar expected=ctx")
__naked void or_null_check_3_1(void)
{
	asm volatile ("					\
	r1 = 1;						\
	call %[bpf_get_netns_cookie];			\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_netns_cookie)
	: __clobber_all);
}

SEC("cgroup/sendmsg6")
__description("pass ctx or null check, 4: ctx - const")
__failure __msg("negative offset ctx ptr R1 off=-612 disallowed")
__naked void null_check_4_ctx_const(void)
{
	asm volatile ("					\
	r1 += -612;					\
	call %[bpf_get_netns_cookie];			\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_netns_cookie)
	: __clobber_all);
}

SEC("cgroup/connect4")
__description("pass ctx or null check, 5: null (connect)")
__success
__naked void null_check_5_null_connect(void)
{
	asm volatile ("					\
	r1 = 0;						\
	call %[bpf_get_netns_cookie];			\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_netns_cookie)
	: __clobber_all);
}

SEC("cgroup/post_bind4")
__description("pass ctx or null check, 6: null (bind)")
__success
__naked void null_check_6_null_bind(void)
{
	asm volatile ("					\
	r1 = 0;						\
	call %[bpf_get_netns_cookie];			\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_netns_cookie)
	: __clobber_all);
}

SEC("cgroup/post_bind4")
__description("pass ctx or null check, 7: ctx (bind)")
__success
__naked void null_check_7_ctx_bind(void)
{
	asm volatile ("					\
	call %[bpf_get_socket_cookie];			\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_socket_cookie)
	: __clobber_all);
}

SEC("cgroup/post_bind4")
__description("pass ctx or null check, 8: null (bind)")
__failure __msg("R1 type=scalar expected=ctx")
__naked void null_check_8_null_bind(void)
{
	asm volatile ("					\
	r1 = 0;						\
	call %[bpf_get_socket_cookie];			\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_socket_cookie)
	: __clobber_all);
}

#define narrow_load(type, ctx, field)					\
	SEC(type)							\
	__description("narrow load on field " #field " of " #ctx)	\
	__failure __msg("invalid bpf_context access")			\
	__naked void invalid_narrow_load##ctx##field(void)		\
	{								\
		asm volatile ("						\
		r1 = *(u32 *)(r1 + %[off]);				\
		r0 = 0;							\
		exit;"							\
		:							\
		: __imm_const(off, offsetof(struct ctx, field) + 4)	\
		: __clobber_all);					\
	}

narrow_load("cgroup/getsockopt", bpf_sockopt, sk);
narrow_load("cgroup/getsockopt", bpf_sockopt, optval);
narrow_load("cgroup/getsockopt", bpf_sockopt, optval_end);
narrow_load("tc", __sk_buff, sk);
narrow_load("cgroup/bind4", bpf_sock_addr, sk);
narrow_load("sockops", bpf_sock_ops, sk);
narrow_load("sockops", bpf_sock_ops, skb_data);
narrow_load("sockops", bpf_sock_ops, skb_data_end);
narrow_load("sockops", bpf_sock_ops, skb_hwtstamp);

#define unaligned_access(type, ctx, field)					\
	SEC(type)								\
	__description("unaligned access on field " #field " of " #ctx)		\
	__failure __msg("invalid bpf_context access")				\
	__naked void unaligned_ctx_access_##ctx##field(void)			\
	{									\
		asm volatile ("							\
		r1 = *(u%[size] *)(r1 + %[off]);				\
		r0 = 0;								\
		exit;"								\
		:								\
		: __imm_const(size, sizeof_field(struct ctx, field) * 8),	\
		  __imm_const(off, offsetof(struct ctx, field) + 1)		\
		: __clobber_all);						\
	}

unaligned_access("flow_dissector", __sk_buff, data);
unaligned_access("netfilter", bpf_nf_ctx, skb);

#define padding_access(type, ctx, prev_field, sz)			\
	SEC(type)							\
	__description("access on " #ctx " padding after " #prev_field)	\
	__naked void padding_ctx_access_##ctx(void)			\
	{								\
		asm volatile ("						\
		r1 = *(u%[size] *)(r1 + %[off]);			\
		r0 = 0;							\
		exit;"							\
		:							\
		: __imm_const(size, sz * 8),				\
		  __imm_const(off, offsetofend(struct ctx, prev_field))	\
		: __clobber_all);					\
	}

__failure __msg("invalid bpf_context access")
padding_access("cgroup/bind4", bpf_sock_addr, msg_src_ip6[3], 4);

__success
padding_access("sk_lookup", bpf_sk_lookup, remote_port, 2);

__failure __msg("invalid bpf_context access")
padding_access("tc", __sk_buff, tstamp_type, 2);

__failure __msg("invalid bpf_context access")
padding_access("cgroup/post_bind4", bpf_sock, dst_port, 2);

__failure __msg("invalid bpf_context access")
padding_access("sk_reuseport", sk_reuseport_md, hash, 4);

char _license[] SEC("license") = "GPL";
