// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/jeq_infer_not_null.c */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

struct {
	__uint(type, BPF_MAP_TYPE_XSKMAP);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, int);
} map_xskmap SEC(".maps");

/* This is equivalent to the following program:
 *
 *   r6 = skb->sk;
 *   r7 = sk_fullsock(r6);
 *   r0 = sk_fullsock(r6);
 *   if (r0 == 0) return 0;    (a)
 *   if (r0 != r7) return 0;   (b)
 *   *r7->type;                (c)
 *   return 0;
 *
 * It is safe to dereference r7 at point (c), because of (a) and (b).
 * The test verifies that relation r0 == r7 is propagated from (b) to (c).
 */
SEC("cgroup/skb")
__description("jne/jeq infer not null, PTR_TO_SOCKET_OR_NULL -> PTR_TO_SOCKET for JNE false branch")
__success __failure_unpriv __msg_unpriv("R7 pointer comparison")
__retval(0)
__naked void socket_for_jne_false_branch(void)
{
	asm volatile ("					\
	/* r6 = skb->sk; */				\
	r6 = *(u64*)(r1 + %[__sk_buff_sk]);		\
	/* if (r6 == 0) return 0; */			\
	if r6 == 0 goto l0_%=;				\
	/* r7 = sk_fullsock(skb); */			\
	r1 = r6;					\
	call %[bpf_sk_fullsock];			\
	r7 = r0;					\
	/* r0 = sk_fullsock(skb); */			\
	r1 = r6;					\
	call %[bpf_sk_fullsock];			\
	/* if (r0 == null) return 0; */			\
	if r0 == 0 goto l0_%=;				\
	/* if (r0 == r7) r0 = *(r7->type); */		\
	if r0 != r7 goto l0_%=;		/* Use ! JNE ! */\
	r0 = *(u32*)(r7 + %[bpf_sock_type]);		\
l0_%=:	/* return 0 */					\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_sk_fullsock),
	  __imm_const(__sk_buff_sk, offsetof(struct __sk_buff, sk)),
	  __imm_const(bpf_sock_type, offsetof(struct bpf_sock, type))
	: __clobber_all);
}

/* Same as above, but verify that another branch of JNE still
 * prohibits access to PTR_MAYBE_NULL.
 */
SEC("cgroup/skb")
__description("jne/jeq infer not null, PTR_TO_SOCKET_OR_NULL unchanged for JNE true branch")
__failure __msg("R7 invalid mem access 'sock_or_null'")
__failure_unpriv __msg_unpriv("R7 pointer comparison")
__naked void unchanged_for_jne_true_branch(void)
{
	asm volatile ("					\
	/* r6 = skb->sk */				\
	r6 = *(u64*)(r1 + %[__sk_buff_sk]);		\
	/* if (r6 == 0) return 0; */			\
	if r6 == 0 goto l0_%=;				\
	/* r7 = sk_fullsock(skb); */			\
	r1 = r6;					\
	call %[bpf_sk_fullsock];			\
	r7 = r0;					\
	/* r0 = sk_fullsock(skb); */			\
	r1 = r6;					\
	call %[bpf_sk_fullsock];			\
	/* if (r0 == null) return 0; */			\
	if r0 != 0 goto l0_%=;				\
	/* if (r0 == r7) return 0; */			\
	if r0 != r7 goto l1_%=;		/* Use ! JNE ! */\
	goto l0_%=;					\
l1_%=:	/* r0 = *(r7->type); */				\
	r0 = *(u32*)(r7 + %[bpf_sock_type]);		\
l0_%=:	/* return 0 */					\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_sk_fullsock),
	  __imm_const(__sk_buff_sk, offsetof(struct __sk_buff, sk)),
	  __imm_const(bpf_sock_type, offsetof(struct bpf_sock, type))
	: __clobber_all);
}

/* Same as a first test, but not null should be inferred for JEQ branch */
SEC("cgroup/skb")
__description("jne/jeq infer not null, PTR_TO_SOCKET_OR_NULL -> PTR_TO_SOCKET for JEQ true branch")
__success __failure_unpriv __msg_unpriv("R7 pointer comparison")
__retval(0)
__naked void socket_for_jeq_true_branch(void)
{
	asm volatile ("					\
	/* r6 = skb->sk; */				\
	r6 = *(u64*)(r1 + %[__sk_buff_sk]);		\
	/* if (r6 == null) return 0; */			\
	if r6 == 0 goto l0_%=;				\
	/* r7 = sk_fullsock(skb); */			\
	r1 = r6;					\
	call %[bpf_sk_fullsock];			\
	r7 = r0;					\
	/* r0 = sk_fullsock(skb); */			\
	r1 = r6;					\
	call %[bpf_sk_fullsock];			\
	/* if (r0 == null) return 0; */			\
	if r0 == 0 goto l0_%=;				\
	/* if (r0 != r7) return 0; */			\
	if r0 == r7 goto l1_%=;		/* Use ! JEQ ! */\
	goto l0_%=;					\
l1_%=:	/* r0 = *(r7->type); */				\
	r0 = *(u32*)(r7 + %[bpf_sock_type]);		\
l0_%=:	/* return 0; */					\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_sk_fullsock),
	  __imm_const(__sk_buff_sk, offsetof(struct __sk_buff, sk)),
	  __imm_const(bpf_sock_type, offsetof(struct bpf_sock, type))
	: __clobber_all);
}

/* Same as above, but verify that another branch of JNE still
 * prohibits access to PTR_MAYBE_NULL.
 */
SEC("cgroup/skb")
__description("jne/jeq infer not null, PTR_TO_SOCKET_OR_NULL unchanged for JEQ false branch")
__failure __msg("R7 invalid mem access 'sock_or_null'")
__failure_unpriv __msg_unpriv("R7 pointer comparison")
__naked void unchanged_for_jeq_false_branch(void)
{
	asm volatile ("					\
	/* r6 = skb->sk; */				\
	r6 = *(u64*)(r1 + %[__sk_buff_sk]);		\
	/* if (r6 == null) return 0; */			\
	if r6 == 0 goto l0_%=;				\
	/* r7 = sk_fullsock(skb); */			\
	r1 = r6;					\
	call %[bpf_sk_fullsock];			\
	r7 = r0;					\
	/* r0 = sk_fullsock(skb); */			\
	r1 = r6;					\
	call %[bpf_sk_fullsock];			\
	/* if (r0 == null) return 0; */			\
	if r0 == 0 goto l0_%=;				\
	/* if (r0 != r7) r0 = *(r7->type); */		\
	if r0 == r7 goto l0_%=;		/* Use ! JEQ ! */\
	r0 = *(u32*)(r7 + %[bpf_sock_type]);		\
l0_%=:	/* return 0; */					\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_sk_fullsock),
	  __imm_const(__sk_buff_sk, offsetof(struct __sk_buff, sk)),
	  __imm_const(bpf_sock_type, offsetof(struct bpf_sock, type))
	: __clobber_all);
}

/* Maps are treated in a different branch of `mark_ptr_not_null_reg`,
 * so separate test for maps case.
 */
SEC("xdp")
__description("jne/jeq infer not null, PTR_TO_MAP_VALUE_OR_NULL -> PTR_TO_MAP_VALUE")
__success __retval(0)
__naked void null_ptr_to_map_value(void)
{
	asm volatile ("					\
	/* r9 = &some stack to use as key */		\
	r1 = 0;						\
	*(u32*)(r10 - 8) = r1;				\
	r9 = r10;					\
	r9 += -8;					\
	/* r8 = process local map */			\
	r8 = %[map_xskmap] ll;				\
	/* r6 = map_lookup_elem(r8, r9); */		\
	r1 = r8;					\
	r2 = r9;					\
	call %[bpf_map_lookup_elem];			\
	r6 = r0;					\
	/* r7 = map_lookup_elem(r8, r9); */		\
	r1 = r8;					\
	r2 = r9;					\
	call %[bpf_map_lookup_elem];			\
	r7 = r0;					\
	/* if (r6 == 0) return 0; */			\
	if r6 == 0 goto l0_%=;				\
	/* if (r6 != r7) return 0; */			\
	if r6 != r7 goto l0_%=;				\
	/* read *r7; */					\
	r0 = *(u32*)(r7 + %[bpf_xdp_sock_queue_id]);	\
l0_%=:	/* return 0; */					\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_xskmap),
	  __imm_const(bpf_xdp_sock_queue_id, offsetof(struct bpf_xdp_sock, queue_id))
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
