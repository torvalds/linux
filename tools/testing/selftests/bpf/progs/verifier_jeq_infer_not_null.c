// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/jeq_infer_not_null.c */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <stdbool.h>
#include "bpf_misc.h"
#include "bpf_kfuncs.h"

struct {
	__uint(type, BPF_MAP_TYPE_XSKMAP);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, int);
} map_xskmap SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, int);
} map_hash SEC(".maps");

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

/* Verified that we can detect the pointer as non_null when comparing with
 * register with value 0. JEQ test case.
 */
SEC("xdp")
__success __log_level(2)
/* to make sure the branch is not falsely predicted*/
__msg("r0 = *(u32 *)(r0 +0)")
__msg("from 7 to 9")
__naked void jeq_reg_reg_null_check(void)
{
        asm volatile ("                                 \
        *(u32*)(r10 - 8) = 0;                           \
        r1 = %[map_xskmap] ll;                          \
        r2 = r10;                                       \
        r2 += -8;                                       \
        call %[bpf_map_lookup_elem];                    \
        r1 = 0;                                         \
        if r0 == r1 goto 1f;                            \
        r0 = *(u32*)(r0 +0);                            \
1:      r0 = 0;                                         \
        exit;                                           \
"       :
        : __imm(bpf_map_lookup_elem),
          __imm_addr(map_xskmap)
        : __clobber_all);
}

/* Same as above but for JNE.
 */
SEC("xdp")
__success __log_level(2)
/* to make sure the branch is not falsely predicted*/
__msg("r0 = *(u32 *)(r0 +0)")
__msg("from 7 to 9")
__naked void jne_reg_reg_null_check(void)
{
        asm volatile ("                                 \
        *(u32*)(r10 - 8) = 0;                           \
        r1 = %[map_xskmap] ll;                          \
        r2 = r10;                                       \
        r2 += -8;                                       \
        call %[bpf_map_lookup_elem];                    \
        r1 = 0;                                         \
        if r0 != r1 goto 1f;                            \
        goto 2f;                                        \
1:      r0 = *(u32*)(r0 +0);                            \
2:      r0 = 0;                                         \
        exit;                                           \
"       :
        : __imm(bpf_map_lookup_elem),
          __imm_addr(map_xskmap)
        : __clobber_all);
}

/*
 * A comparison between PTR_TO_MEM | MEM_RDONLY | PTR_UNTRUSTED and
 * PTR_TO_MAP_VALUE_OR_NULL should not infer that map pointer is not null.
 * A bug in check_cond_jmp_op() made such inference possible.
 */
SEC("raw_tp")
__failure
__msg("error: invalid dereference of R0 (a nullable map value pointer)")
__msg(">>> 11 | (61) r0 = *(u32 *)(r0 +0)")
__naked void untrusted_mem_does_not_infer_map_value_non_null(void)
{
	asm volatile ("					\
	/* r6 = bpf_rdonly_cast(0, 0); */		\
	r1 = 0;						\
	r2 = 0;						\
	call %[bpf_rdonly_cast];			\
	r6 = r0;					\
	/* r0 = bpf_map_lookup_elem(map_hash, &key); */	\
	*(u64 *)(r10 - 8) = 0;				\
	r1 = %[map_hash] ll;				\
	r2 = r10;					\
	r2 += -8;					\
	call %[bpf_map_lookup_elem];			\
	/*						\
	 * buggy verifier assumed that r6 can't be null	\
	 * and marked r0 non-null as well.		\
	 */						\
	if r6 != r0 goto 1f;				\
	r0 = *(u32 *)(r0 + 0);				\
1:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_rdonly_cast),
	  __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash)
	: __clobber_all);
}

/*
 * A pointer with an offset that is not bounded from above may be null at
 * runtime, hence it is not a witness for the pointer it is compared with.
 */
SEC("socket")
__failure
__msg("error: invalid dereference of R7 (a nullable map value pointer)")
__naked void unbounded_offset_does_not_infer_map_value_non_null(void)
{
	asm volatile ("					\
	/* r6 = bpf_map_lookup_elem(map_hash, &0); */	\
	*(u64 *)(r10 - 8) = 0;				\
	r1 = %[map_hash] ll;				\
	r2 = r10;					\
	r2 += -8;					\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto 1f;				\
	r6 = r0;					\
	/* r7 = bpf_map_lookup_elem(map_hash, &1); */	\
	*(u64 *)(r10 - 8) = 1;				\
	r1 = %[map_hash] ll;				\
	r2 = r10;					\
	r2 += -8;					\
	call %[bpf_map_lookup_elem];			\
	r7 = r0;					\
	/* pointer - pointer is an unknown scalar */	\
	r8 = r7;					\
	r8 -= r6;					\
	/* r8 is in [0, S64_MAX] */			\
	r8 <<= 1;					\
	r8 >>= 1;					\
	/* r6 may wrap to zero at runtime */		\
	r6 += r8;					\
	if r7 != r6 goto 1f;				\
	r0 = *(u8 *)(r7 + 0);				\
1:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash)
	: __clobber_all);
}

/* Same, but the offset is bounded, so the inference is still done. */
SEC("socket")
__success
__naked void bounded_offset_infers_map_value_non_null(void)
{
	asm volatile ("					\
	/* r6 = bpf_map_lookup_elem(map_hash, &0); */	\
	*(u64 *)(r10 - 8) = 0;				\
	r1 = %[map_hash] ll;				\
	r2 = r10;					\
	r2 += -8;					\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto 1f;				\
	r6 = r0;					\
	/* r7 = bpf_map_lookup_elem(map_hash, &1); */	\
	*(u64 *)(r10 - 8) = 1;				\
	r1 = %[map_hash] ll;				\
	r2 = r10;					\
	r2 += -8;					\
	call %[bpf_map_lookup_elem];			\
	r7 = r0;					\
	/* pointer - pointer is an unknown scalar */	\
	r8 = r7;					\
	r8 -= r6;					\
	/* r8 is in [0, 3] */				\
	r8 &= 3;					\
	r6 += r8;					\
	if r7 != r6 goto 1f;				\
	r0 = *(u8 *)(r7 + 0);				\
1:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash)
	: __clobber_all);
}

/*
 * The low 32 bits of a map value pointer may be zero, hence a 32-bit
 * compare with zero cannot be predicted from the pointer being non-NULL
 * and both successors of such a jump have to be verified.
 */
SEC("socket")
__failure __msg("invalid access to map value, value_size=4 off=32 size=4")
__naked void jmp32_ptr_vs_zero_jne(void)
{
	asm volatile ("					\
	/* r0 = bpf_map_lookup_elem(map_hash, &key); */	\
	*(u64 *)(r10 - 8) = 0;				\
	r1 = %[map_hash] ll;				\
	r2 = r10;					\
	r2 += -8;					\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto 1f;				\
	if w0 != 0 goto 1f;				\
	r0 = *(u32 *)(r0 + 32);				\
1:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash)
	: __clobber_all);
}

/*
 * The below program is explored in two paths: r6 == 0 and r6 == 1.
 * On the first path comparison "if r0 == r6 goto 2f" should mark r6 as precise,
 * otherwise unsafe path with r6 == 1 would be incorrectly pruned.
 */
SEC("socket")
__failure
__flag(BPF_F_TEST_STATE_FREQ)
__msg("error: invalid dereference of R0 (a nullable map value pointer)")
__naked void imprecise_zero_does_not_infer_map_value_non_null(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	/* r6 is 0 on the path explored first, 1 on the other */\
	r6 = 1;						\
	if r0 == 0 goto 1f;				\
	r6 = 0;						\
	/* r0 = bpf_map_lookup_elem(map_hash, &0); */	\
1:	*(u64 *)(r10 - 8) = 0;				\
	r1 = %[map_hash] ll;				\
	r2 = r10;					\
	r2 += -8;					\
	call %[bpf_map_lookup_elem];			\
	if r0 == r6 goto 2f;				\
	r0 = *(u8 *)(r0 + 0);				\
2:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32),
	  __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash)
	: __clobber_all);
}

void kfunc_root(void)
{
	bpf_rdonly_cast(0, 0);
}

char _license[] SEC("license") = "GPL";
