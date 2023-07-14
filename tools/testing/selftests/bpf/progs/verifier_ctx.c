// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/ctx.c */

#include <linux/bpf.h>
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

char _license[] SEC("license") = "GPL";
