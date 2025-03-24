// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/ref_tracking.c */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "../../../include/linux/filter.h"
#include "bpf_misc.h"

#define BPF_SK_LOOKUP(func) \
	/* struct bpf_sock_tuple tuple = {} */ \
	"r2 = 0;"			\
	"*(u32*)(r10 - 8) = r2;"	\
	"*(u64*)(r10 - 16) = r2;"	\
	"*(u64*)(r10 - 24) = r2;"	\
	"*(u64*)(r10 - 32) = r2;"	\
	"*(u64*)(r10 - 40) = r2;"	\
	"*(u64*)(r10 - 48) = r2;"	\
	/* sk = func(ctx, &tuple, sizeof tuple, 0, 0) */ \
	"r2 = r10;"			\
	"r2 += -48;"			\
	"r3 = %[sizeof_bpf_sock_tuple];"\
	"r4 = 0;"			\
	"r5 = 0;"			\
	"call %[" #func "];"

struct bpf_key {} __attribute__((preserve_access_index));

extern void bpf_key_put(struct bpf_key *key) __ksym;
extern struct bpf_key *bpf_lookup_system_key(__u64 id) __ksym;
extern struct bpf_key *bpf_lookup_user_key(__u32 serial, __u64 flags) __ksym;

/* BTF FUNC records are not generated for kfuncs referenced
 * from inline assembly. These records are necessary for
 * libbpf to link the program. The function below is a hack
 * to ensure that BTF FUNC records are generated.
 */
void __kfunc_btf_root(void)
{
	bpf_key_put(0);
	bpf_lookup_system_key(0);
	bpf_lookup_user_key(0, 0);
}

#define MAX_ENTRIES 11

struct test_val {
	unsigned int index;
	int foo[MAX_ENTRIES];
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, struct test_val);
} map_array_48b SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
	__uint(max_entries, 4096);
} map_ringbuf SEC(".maps");

void dummy_prog_42_tc(void);
void dummy_prog_24_tc(void);
void dummy_prog_loop1_tc(void);

struct {
	__uint(type, BPF_MAP_TYPE_PROG_ARRAY);
	__uint(max_entries, 4);
	__uint(key_size, sizeof(int));
	__array(values, void (void));
} map_prog1_tc SEC(".maps") = {
	.values = {
		[0] = (void *)&dummy_prog_42_tc,
		[1] = (void *)&dummy_prog_loop1_tc,
		[2] = (void *)&dummy_prog_24_tc,
	},
};

SEC("tc")
__auxiliary
__naked void dummy_prog_42_tc(void)
{
	asm volatile ("r0 = 42; exit;");
}

SEC("tc")
__auxiliary
__naked void dummy_prog_24_tc(void)
{
	asm volatile ("r0 = 24; exit;");
}

SEC("tc")
__auxiliary
__naked void dummy_prog_loop1_tc(void)
{
	asm volatile ("			\
	r3 = 1;				\
	r2 = %[map_prog1_tc] ll;	\
	call %[bpf_tail_call];		\
	r0 = 41;			\
	exit;				\
"	:
	: __imm(bpf_tail_call),
	  __imm_addr(map_prog1_tc)
	: __clobber_all);
}

SEC("tc")
__description("reference tracking: leak potential reference")
__failure __msg("Unreleased reference")
__naked void reference_tracking_leak_potential_reference(void)
{
	asm volatile (
	BPF_SK_LOOKUP(bpf_sk_lookup_tcp)
"	r6 = r0;		/* leak reference */	\
	exit;						\
"	:
	: __imm(bpf_sk_lookup_tcp),
	  __imm_const(sizeof_bpf_sock_tuple, sizeof(struct bpf_sock_tuple))
	: __clobber_all);
}

SEC("tc")
__description("reference tracking: leak potential reference to sock_common")
__failure __msg("Unreleased reference")
__naked void potential_reference_to_sock_common_1(void)
{
	asm volatile (
	BPF_SK_LOOKUP(bpf_skc_lookup_tcp)
"	r6 = r0;		/* leak reference */	\
	exit;						\
"	:
	: __imm(bpf_skc_lookup_tcp),
	  __imm_const(sizeof_bpf_sock_tuple, sizeof(struct bpf_sock_tuple))
	: __clobber_all);
}

SEC("tc")
__description("reference tracking: leak potential reference on stack")
__failure __msg("Unreleased reference")
__naked void leak_potential_reference_on_stack(void)
{
	asm volatile (
	BPF_SK_LOOKUP(bpf_sk_lookup_tcp)
"	r4 = r10;					\
	r4 += -8;					\
	*(u64*)(r4 + 0) = r0;				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_sk_lookup_tcp),
	  __imm_const(sizeof_bpf_sock_tuple, sizeof(struct bpf_sock_tuple))
	: __clobber_all);
}

SEC("tc")
__description("reference tracking: leak potential reference on stack 2")
__failure __msg("Unreleased reference")
__naked void potential_reference_on_stack_2(void)
{
	asm volatile (
	BPF_SK_LOOKUP(bpf_sk_lookup_tcp)
"	r4 = r10;					\
	r4 += -8;					\
	*(u64*)(r4 + 0) = r0;				\
	r0 = 0;						\
	r1 = 0;						\
	*(u64*)(r4 + 0) = r1;				\
	exit;						\
"	:
	: __imm(bpf_sk_lookup_tcp),
	  __imm_const(sizeof_bpf_sock_tuple, sizeof(struct bpf_sock_tuple))
	: __clobber_all);
}

SEC("tc")
__description("reference tracking: zero potential reference")
__failure __msg("Unreleased reference")
__naked void reference_tracking_zero_potential_reference(void)
{
	asm volatile (
	BPF_SK_LOOKUP(bpf_sk_lookup_tcp)
"	r0 = 0;			/* leak reference */	\
	exit;						\
"	:
	: __imm(bpf_sk_lookup_tcp),
	  __imm_const(sizeof_bpf_sock_tuple, sizeof(struct bpf_sock_tuple))
	: __clobber_all);
}

SEC("tc")
__description("reference tracking: zero potential reference to sock_common")
__failure __msg("Unreleased reference")
__naked void potential_reference_to_sock_common_2(void)
{
	asm volatile (
	BPF_SK_LOOKUP(bpf_skc_lookup_tcp)
"	r0 = 0;			/* leak reference */	\
	exit;						\
"	:
	: __imm(bpf_skc_lookup_tcp),
	  __imm_const(sizeof_bpf_sock_tuple, sizeof(struct bpf_sock_tuple))
	: __clobber_all);
}

SEC("tc")
__description("reference tracking: copy and zero potential references")
__failure __msg("Unreleased reference")
__naked void copy_and_zero_potential_references(void)
{
	asm volatile (
	BPF_SK_LOOKUP(bpf_sk_lookup_tcp)
"	r7 = r0;					\
	r0 = 0;						\
	r7 = 0;			/* leak reference */	\
	exit;						\
"	:
	: __imm(bpf_sk_lookup_tcp),
	  __imm_const(sizeof_bpf_sock_tuple, sizeof(struct bpf_sock_tuple))
	: __clobber_all);
}

SEC("lsm.s/bpf")
__description("reference tracking: acquire/release user key reference")
__success
__naked void acquire_release_user_key_reference(void)
{
	asm volatile ("					\
	r1 = -3;					\
	r2 = 0;						\
	call %[bpf_lookup_user_key];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	call %[bpf_key_put];				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_key_put),
	  __imm(bpf_lookup_user_key)
	: __clobber_all);
}

SEC("lsm.s/bpf")
__description("reference tracking: acquire/release system key reference")
__success
__naked void acquire_release_system_key_reference(void)
{
	asm volatile ("					\
	r1 = 1;						\
	call %[bpf_lookup_system_key];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	call %[bpf_key_put];				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_key_put),
	  __imm(bpf_lookup_system_key)
	: __clobber_all);
}

SEC("lsm.s/bpf")
__description("reference tracking: release user key reference without check")
__failure __msg("Possibly NULL pointer passed to trusted arg0")
__naked void user_key_reference_without_check(void)
{
	asm volatile ("					\
	r1 = -3;					\
	r2 = 0;						\
	call %[bpf_lookup_user_key];			\
	r1 = r0;					\
	call %[bpf_key_put];				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_key_put),
	  __imm(bpf_lookup_user_key)
	: __clobber_all);
}

SEC("lsm.s/bpf")
__description("reference tracking: release system key reference without check")
__failure __msg("Possibly NULL pointer passed to trusted arg0")
__naked void system_key_reference_without_check(void)
{
	asm volatile ("					\
	r1 = 1;						\
	call %[bpf_lookup_system_key];			\
	r1 = r0;					\
	call %[bpf_key_put];				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_key_put),
	  __imm(bpf_lookup_system_key)
	: __clobber_all);
}

SEC("lsm.s/bpf")
__description("reference tracking: release with NULL key pointer")
__failure __msg("Possibly NULL pointer passed to trusted arg0")
__naked void release_with_null_key_pointer(void)
{
	asm volatile ("					\
	r1 = 0;						\
	call %[bpf_key_put];				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_key_put)
	: __clobber_all);
}

SEC("lsm.s/bpf")
__description("reference tracking: leak potential reference to user key")
__failure __msg("Unreleased reference")
__naked void potential_reference_to_user_key(void)
{
	asm volatile ("					\
	r1 = -3;					\
	r2 = 0;						\
	call %[bpf_lookup_user_key];			\
	exit;						\
"	:
	: __imm(bpf_lookup_user_key)
	: __clobber_all);
}

SEC("lsm.s/bpf")
__description("reference tracking: leak potential reference to system key")
__failure __msg("Unreleased reference")
__naked void potential_reference_to_system_key(void)
{
	asm volatile ("					\
	r1 = 1;						\
	call %[bpf_lookup_system_key];			\
	exit;						\
"	:
	: __imm(bpf_lookup_system_key)
	: __clobber_all);
}

SEC("tc")
__description("reference tracking: release reference without check")
__failure __msg("type=sock_or_null expected=sock")
__naked void tracking_release_reference_without_check(void)
{
	asm volatile (
	BPF_SK_LOOKUP(bpf_sk_lookup_tcp)
"	/* reference in r0 may be NULL */		\
	r1 = r0;					\
	r2 = 0;						\
	call %[bpf_sk_release];				\
	exit;						\
"	:
	: __imm(bpf_sk_lookup_tcp),
	  __imm(bpf_sk_release),
	  __imm_const(sizeof_bpf_sock_tuple, sizeof(struct bpf_sock_tuple))
	: __clobber_all);
}

SEC("tc")
__description("reference tracking: release reference to sock_common without check")
__failure __msg("type=sock_common_or_null expected=sock")
__naked void to_sock_common_without_check(void)
{
	asm volatile (
	BPF_SK_LOOKUP(bpf_skc_lookup_tcp)
"	/* reference in r0 may be NULL */		\
	r1 = r0;					\
	r2 = 0;						\
	call %[bpf_sk_release];				\
	exit;						\
"	:
	: __imm(bpf_sk_release),
	  __imm(bpf_skc_lookup_tcp),
	  __imm_const(sizeof_bpf_sock_tuple, sizeof(struct bpf_sock_tuple))
	: __clobber_all);
}

SEC("tc")
__description("reference tracking: release reference")
__success __retval(0)
__naked void reference_tracking_release_reference(void)
{
	asm volatile (
	BPF_SK_LOOKUP(bpf_sk_lookup_tcp)
"	r1 = r0;					\
	if r0 == 0 goto l0_%=;				\
	call %[bpf_sk_release];				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_sk_lookup_tcp),
	  __imm(bpf_sk_release),
	  __imm_const(sizeof_bpf_sock_tuple, sizeof(struct bpf_sock_tuple))
	: __clobber_all);
}

SEC("tc")
__description("reference tracking: release reference to sock_common")
__success __retval(0)
__naked void release_reference_to_sock_common(void)
{
	asm volatile (
	BPF_SK_LOOKUP(bpf_skc_lookup_tcp)
"	r1 = r0;					\
	if r0 == 0 goto l0_%=;				\
	call %[bpf_sk_release];				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_sk_release),
	  __imm(bpf_skc_lookup_tcp),
	  __imm_const(sizeof_bpf_sock_tuple, sizeof(struct bpf_sock_tuple))
	: __clobber_all);
}

SEC("tc")
__description("reference tracking: release reference 2")
__success __retval(0)
__naked void reference_tracking_release_reference_2(void)
{
	asm volatile (
	BPF_SK_LOOKUP(bpf_sk_lookup_tcp)
"	r1 = r0;					\
	if r0 != 0 goto l0_%=;				\
	exit;						\
l0_%=:	call %[bpf_sk_release];				\
	exit;						\
"	:
	: __imm(bpf_sk_lookup_tcp),
	  __imm(bpf_sk_release),
	  __imm_const(sizeof_bpf_sock_tuple, sizeof(struct bpf_sock_tuple))
	: __clobber_all);
}

SEC("tc")
__description("reference tracking: release reference twice")
__failure __msg("type=scalar expected=sock")
__naked void reference_tracking_release_reference_twice(void)
{
	asm volatile (
	BPF_SK_LOOKUP(bpf_sk_lookup_tcp)
"	r1 = r0;					\
	r6 = r0;					\
	if r0 == 0 goto l0_%=;				\
	call %[bpf_sk_release];				\
l0_%=:	r1 = r6;					\
	call %[bpf_sk_release];				\
	exit;						\
"	:
	: __imm(bpf_sk_lookup_tcp),
	  __imm(bpf_sk_release),
	  __imm_const(sizeof_bpf_sock_tuple, sizeof(struct bpf_sock_tuple))
	: __clobber_all);
}

SEC("tc")
__description("reference tracking: release reference twice inside branch")
__failure __msg("type=scalar expected=sock")
__naked void release_reference_twice_inside_branch(void)
{
	asm volatile (
	BPF_SK_LOOKUP(bpf_sk_lookup_tcp)
"	r1 = r0;					\
	r6 = r0;					\
	if r0 == 0 goto l0_%=;		/* goto end */	\
	call %[bpf_sk_release];				\
	r1 = r6;					\
	call %[bpf_sk_release];				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_sk_lookup_tcp),
	  __imm(bpf_sk_release),
	  __imm_const(sizeof_bpf_sock_tuple, sizeof(struct bpf_sock_tuple))
	: __clobber_all);
}

SEC("tc")
__description("reference tracking: alloc, check, free in one subbranch")
__failure __msg("Unreleased reference")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void check_free_in_one_subbranch(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r3 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r0 = r2;					\
	r0 += 16;					\
	/* if (offsetof(skb, mark) > data_len) exit; */	\
	if r0 <= r3 goto l0_%=;				\
	exit;						\
l0_%=:	r6 = *(u32*)(r2 + %[__sk_buff_mark]);		\
"	BPF_SK_LOOKUP(bpf_sk_lookup_tcp)
"	if r6 == 0 goto l1_%=;		/* mark == 0? */\
	/* Leak reference in R0 */			\
	exit;						\
l1_%=:	if r0 == 0 goto l2_%=;		/* sk NULL? */	\
	r1 = r0;					\
	call %[bpf_sk_release];				\
l2_%=:	exit;						\
"	:
	: __imm(bpf_sk_lookup_tcp),
	  __imm(bpf_sk_release),
	  __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end)),
	  __imm_const(__sk_buff_mark, offsetof(struct __sk_buff, mark)),
	  __imm_const(sizeof_bpf_sock_tuple, sizeof(struct bpf_sock_tuple))
	: __clobber_all);
}

SEC("tc")
__description("reference tracking: alloc, check, free in both subbranches")
__success __retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void check_free_in_both_subbranches(void)
{
	asm volatile ("					\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r3 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r0 = r2;					\
	r0 += 16;					\
	/* if (offsetof(skb, mark) > data_len) exit; */	\
	if r0 <= r3 goto l0_%=;				\
	exit;						\
l0_%=:	r6 = *(u32*)(r2 + %[__sk_buff_mark]);		\
"	BPF_SK_LOOKUP(bpf_sk_lookup_tcp)
"	if r6 == 0 goto l1_%=;		/* mark == 0? */\
	if r0 == 0 goto l2_%=;		/* sk NULL? */	\
	r1 = r0;					\
	call %[bpf_sk_release];				\
l2_%=:	exit;						\
l1_%=:	if r0 == 0 goto l3_%=;		/* sk NULL? */	\
	r1 = r0;					\
	call %[bpf_sk_release];				\
l3_%=:	exit;						\
"	:
	: __imm(bpf_sk_lookup_tcp),
	  __imm(bpf_sk_release),
	  __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end)),
	  __imm_const(__sk_buff_mark, offsetof(struct __sk_buff, mark)),
	  __imm_const(sizeof_bpf_sock_tuple, sizeof(struct bpf_sock_tuple))
	: __clobber_all);
}

SEC("tc")
__description("reference tracking in call: free reference in subprog")
__success __retval(0)
__naked void call_free_reference_in_subprog(void)
{
	asm volatile (
	BPF_SK_LOOKUP(bpf_sk_lookup_tcp)
"	r1 = r0;	/* unchecked reference */	\
	call call_free_reference_in_subprog__1;		\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_sk_lookup_tcp),
	  __imm_const(sizeof_bpf_sock_tuple, sizeof(struct bpf_sock_tuple))
	: __clobber_all);
}

static __naked __noinline __attribute__((used))
void call_free_reference_in_subprog__1(void)
{
	asm volatile ("					\
	/* subprog 1 */					\
	r2 = r1;					\
	if r2 == 0 goto l0_%=;				\
	call %[bpf_sk_release];				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_sk_release)
	: __clobber_all);
}

SEC("tc")
__description("reference tracking in call: free reference in subprog and outside")
__failure __msg("type=scalar expected=sock")
__naked void reference_in_subprog_and_outside(void)
{
	asm volatile (
	BPF_SK_LOOKUP(bpf_sk_lookup_tcp)
"	r1 = r0;	/* unchecked reference */	\
	r6 = r0;					\
	call reference_in_subprog_and_outside__1;	\
	r1 = r6;					\
	call %[bpf_sk_release];				\
	exit;						\
"	:
	: __imm(bpf_sk_lookup_tcp),
	  __imm(bpf_sk_release),
	  __imm_const(sizeof_bpf_sock_tuple, sizeof(struct bpf_sock_tuple))
	: __clobber_all);
}

static __naked __noinline __attribute__((used))
void reference_in_subprog_and_outside__1(void)
{
	asm volatile ("					\
	/* subprog 1 */					\
	r2 = r1;					\
	if r2 == 0 goto l0_%=;				\
	call %[bpf_sk_release];				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_sk_release)
	: __clobber_all);
}

SEC("tc")
__description("reference tracking in call: alloc & leak reference in subprog")
__failure __msg("Unreleased reference")
__naked void alloc_leak_reference_in_subprog(void)
{
	asm volatile ("					\
	r4 = r10;					\
	r4 += -8;					\
	call alloc_leak_reference_in_subprog__1;	\
	r1 = r0;					\
	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

static __naked __noinline __attribute__((used))
void alloc_leak_reference_in_subprog__1(void)
{
	asm volatile ("					\
	/* subprog 1 */					\
	r6 = r4;					\
"	BPF_SK_LOOKUP(bpf_sk_lookup_tcp)
"	/* spill unchecked sk_ptr into stack of caller */\
	*(u64*)(r6 + 0) = r0;				\
	r1 = r0;					\
	exit;						\
"	:
	: __imm(bpf_sk_lookup_tcp),
	  __imm_const(sizeof_bpf_sock_tuple, sizeof(struct bpf_sock_tuple))
	: __clobber_all);
}

SEC("tc")
__description("reference tracking in call: alloc in subprog, release outside")
__success __retval(POINTER_VALUE)
__naked void alloc_in_subprog_release_outside(void)
{
	asm volatile ("					\
	r4 = r10;					\
	call alloc_in_subprog_release_outside__1;	\
	r1 = r0;					\
	if r0 == 0 goto l0_%=;				\
	call %[bpf_sk_release];				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_sk_release)
	: __clobber_all);
}

static __naked __noinline __attribute__((used))
void alloc_in_subprog_release_outside__1(void)
{
	asm volatile ("					\
	/* subprog 1 */					\
"	BPF_SK_LOOKUP(bpf_sk_lookup_tcp)
"	exit;				/* return sk */	\
"	:
	: __imm(bpf_sk_lookup_tcp),
	  __imm_const(sizeof_bpf_sock_tuple, sizeof(struct bpf_sock_tuple))
	: __clobber_all);
}

SEC("tc")
__description("reference tracking in call: sk_ptr leak into caller stack")
__failure __msg("Unreleased reference")
__naked void ptr_leak_into_caller_stack(void)
{
	asm volatile ("					\
	r4 = r10;					\
	r4 += -8;					\
	call ptr_leak_into_caller_stack__1;		\
	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

static __naked __noinline __attribute__((used))
void ptr_leak_into_caller_stack__1(void)
{
	asm volatile ("					\
	/* subprog 1 */					\
	r5 = r10;					\
	r5 += -8;					\
	*(u64*)(r5 + 0) = r4;				\
	call ptr_leak_into_caller_stack__2;		\
	/* spill unchecked sk_ptr into stack of caller */\
	r5 = r10;					\
	r5 += -8;					\
	r4 = *(u64*)(r5 + 0);				\
	*(u64*)(r4 + 0) = r0;				\
	exit;						\
"	::: __clobber_all);
}

static __naked __noinline __attribute__((used))
void ptr_leak_into_caller_stack__2(void)
{
	asm volatile ("					\
	/* subprog 2 */					\
"	BPF_SK_LOOKUP(bpf_sk_lookup_tcp)
"	exit;						\
"	:
	: __imm(bpf_sk_lookup_tcp),
	  __imm_const(sizeof_bpf_sock_tuple, sizeof(struct bpf_sock_tuple))
	: __clobber_all);
}

SEC("tc")
__description("reference tracking in call: sk_ptr spill into caller stack")
__success __retval(0)
__naked void ptr_spill_into_caller_stack(void)
{
	asm volatile ("					\
	r4 = r10;					\
	r4 += -8;					\
	call ptr_spill_into_caller_stack__1;		\
	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

static __naked __noinline __attribute__((used))
void ptr_spill_into_caller_stack__1(void)
{
	asm volatile ("					\
	/* subprog 1 */					\
	r5 = r10;					\
	r5 += -8;					\
	*(u64*)(r5 + 0) = r4;				\
	call ptr_spill_into_caller_stack__2;		\
	/* spill unchecked sk_ptr into stack of caller */\
	r5 = r10;					\
	r5 += -8;					\
	r4 = *(u64*)(r5 + 0);				\
	*(u64*)(r4 + 0) = r0;				\
	if r0 == 0 goto l0_%=;				\
	/* now the sk_ptr is verified, free the reference */\
	r1 = *(u64*)(r4 + 0);				\
	call %[bpf_sk_release];				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_sk_release)
	: __clobber_all);
}

static __naked __noinline __attribute__((used))
void ptr_spill_into_caller_stack__2(void)
{
	asm volatile ("					\
	/* subprog 2 */					\
"	BPF_SK_LOOKUP(bpf_sk_lookup_tcp)
"	exit;						\
"	:
	: __imm(bpf_sk_lookup_tcp),
	  __imm_const(sizeof_bpf_sock_tuple, sizeof(struct bpf_sock_tuple))
	: __clobber_all);
}

SEC("tc")
__description("reference tracking: allow LD_ABS")
__success __retval(0)
__naked void reference_tracking_allow_ld_abs(void)
{
	asm volatile ("					\
	r6 = r1;					\
"	BPF_SK_LOOKUP(bpf_sk_lookup_tcp)
"	r1 = r0;					\
	if r0 == 0 goto l0_%=;				\
	call %[bpf_sk_release];				\
l0_%=:	r0 = *(u8*)skb[0];				\
	r0 = *(u16*)skb[0];				\
	r0 = *(u32*)skb[0];				\
	exit;						\
"	:
	: __imm(bpf_sk_lookup_tcp),
	  __imm(bpf_sk_release),
	  __imm_const(sizeof_bpf_sock_tuple, sizeof(struct bpf_sock_tuple))
	: __clobber_all);
}

SEC("tc")
__description("reference tracking: forbid LD_ABS while holding reference")
__failure __msg("BPF_LD_[ABS|IND] would lead to reference leak")
__naked void ld_abs_while_holding_reference(void)
{
	asm volatile ("					\
	r6 = r1;					\
"	BPF_SK_LOOKUP(bpf_sk_lookup_tcp)
"	r0 = *(u8*)skb[0];				\
	r0 = *(u16*)skb[0];				\
	r0 = *(u32*)skb[0];				\
	r1 = r0;					\
	if r0 == 0 goto l0_%=;				\
	call %[bpf_sk_release];				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_sk_lookup_tcp),
	  __imm(bpf_sk_release),
	  __imm_const(sizeof_bpf_sock_tuple, sizeof(struct bpf_sock_tuple))
	: __clobber_all);
}

SEC("tc")
__description("reference tracking: allow LD_IND")
__success __retval(1)
__naked void reference_tracking_allow_ld_ind(void)
{
	asm volatile ("					\
	r6 = r1;					\
"	BPF_SK_LOOKUP(bpf_sk_lookup_tcp)
"	r1 = r0;					\
	if r0 == 0 goto l0_%=;				\
	call %[bpf_sk_release];				\
l0_%=:	r7 = 1;						\
	.8byte %[ld_ind];				\
	r0 = r7;					\
	exit;						\
"	:
	: __imm(bpf_sk_lookup_tcp),
	  __imm(bpf_sk_release),
	  __imm_const(sizeof_bpf_sock_tuple, sizeof(struct bpf_sock_tuple)),
	  __imm_insn(ld_ind, BPF_LD_IND(BPF_W, BPF_REG_7, -0x200000))
	: __clobber_all);
}

SEC("tc")
__description("reference tracking: forbid LD_IND while holding reference")
__failure __msg("BPF_LD_[ABS|IND] would lead to reference leak")
__naked void ld_ind_while_holding_reference(void)
{
	asm volatile ("					\
	r6 = r1;					\
"	BPF_SK_LOOKUP(bpf_sk_lookup_tcp)
"	r4 = r0;					\
	r7 = 1;						\
	.8byte %[ld_ind];				\
	r0 = r7;					\
	r1 = r4;					\
	if r1 == 0 goto l0_%=;				\
	call %[bpf_sk_release];				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_sk_lookup_tcp),
	  __imm(bpf_sk_release),
	  __imm_const(sizeof_bpf_sock_tuple, sizeof(struct bpf_sock_tuple)),
	  __imm_insn(ld_ind, BPF_LD_IND(BPF_W, BPF_REG_7, -0x200000))
	: __clobber_all);
}

SEC("tc")
__description("reference tracking: check reference or tail call")
__success __retval(0)
__naked void check_reference_or_tail_call(void)
{
	asm volatile ("					\
	r7 = r1;					\
"	BPF_SK_LOOKUP(bpf_sk_lookup_tcp)
"	/* if (sk) bpf_sk_release() */			\
	r1 = r0;					\
	if r1 != 0 goto l0_%=;				\
	/* bpf_tail_call() */				\
	r3 = 3;						\
	r2 = %[map_prog1_tc] ll;			\
	r1 = r7;					\
	call %[bpf_tail_call];				\
	r0 = 0;						\
	exit;						\
l0_%=:	call %[bpf_sk_release];				\
	exit;						\
"	:
	: __imm(bpf_sk_lookup_tcp),
	  __imm(bpf_sk_release),
	  __imm(bpf_tail_call),
	  __imm_addr(map_prog1_tc),
	  __imm_const(sizeof_bpf_sock_tuple, sizeof(struct bpf_sock_tuple))
	: __clobber_all);
}

SEC("tc")
__description("reference tracking: release reference then tail call")
__success __retval(0)
__naked void release_reference_then_tail_call(void)
{
	asm volatile ("					\
	r7 = r1;					\
"	BPF_SK_LOOKUP(bpf_sk_lookup_tcp)
"	/* if (sk) bpf_sk_release() */			\
	r1 = r0;					\
	if r1 == 0 goto l0_%=;				\
	call %[bpf_sk_release];				\
l0_%=:	/* bpf_tail_call() */				\
	r3 = 3;						\
	r2 = %[map_prog1_tc] ll;			\
	r1 = r7;					\
	call %[bpf_tail_call];				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_sk_lookup_tcp),
	  __imm(bpf_sk_release),
	  __imm(bpf_tail_call),
	  __imm_addr(map_prog1_tc),
	  __imm_const(sizeof_bpf_sock_tuple, sizeof(struct bpf_sock_tuple))
	: __clobber_all);
}

SEC("tc")
__description("reference tracking: leak possible reference over tail call")
__failure __msg("tail_call would lead to reference leak")
__naked void possible_reference_over_tail_call(void)
{
	asm volatile ("					\
	r7 = r1;					\
	/* Look up socket and store in REG_6 */		\
"	BPF_SK_LOOKUP(bpf_sk_lookup_tcp)
"	/* bpf_tail_call() */				\
	r6 = r0;					\
	r3 = 3;						\
	r2 = %[map_prog1_tc] ll;			\
	r1 = r7;					\
	call %[bpf_tail_call];				\
	r0 = 0;						\
	/* if (sk) bpf_sk_release() */			\
	r1 = r6;					\
	if r1 == 0 goto l0_%=;				\
	call %[bpf_sk_release];				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_sk_lookup_tcp),
	  __imm(bpf_sk_release),
	  __imm(bpf_tail_call),
	  __imm_addr(map_prog1_tc),
	  __imm_const(sizeof_bpf_sock_tuple, sizeof(struct bpf_sock_tuple))
	: __clobber_all);
}

SEC("tc")
__description("reference tracking: leak checked reference over tail call")
__failure __msg("tail_call would lead to reference leak")
__naked void checked_reference_over_tail_call(void)
{
	asm volatile ("					\
	r7 = r1;					\
	/* Look up socket and store in REG_6 */		\
"	BPF_SK_LOOKUP(bpf_sk_lookup_tcp)
"	r6 = r0;					\
	/* if (!sk) goto end */				\
	if r0 == 0 goto l0_%=;				\
	/* bpf_tail_call() */				\
	r3 = 0;						\
	r2 = %[map_prog1_tc] ll;			\
	r1 = r7;					\
	call %[bpf_tail_call];				\
	r0 = 0;						\
	r1 = r6;					\
l0_%=:	call %[bpf_sk_release];				\
	exit;						\
"	:
	: __imm(bpf_sk_lookup_tcp),
	  __imm(bpf_sk_release),
	  __imm(bpf_tail_call),
	  __imm_addr(map_prog1_tc),
	  __imm_const(sizeof_bpf_sock_tuple, sizeof(struct bpf_sock_tuple))
	: __clobber_all);
}

SEC("tc")
__description("reference tracking: mangle and release sock_or_null")
__failure __msg("R1 pointer arithmetic on sock_or_null prohibited")
__naked void and_release_sock_or_null(void)
{
	asm volatile (
	BPF_SK_LOOKUP(bpf_sk_lookup_tcp)
"	r1 = r0;					\
	r1 += 5;					\
	if r0 == 0 goto l0_%=;				\
	call %[bpf_sk_release];				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_sk_lookup_tcp),
	  __imm(bpf_sk_release),
	  __imm_const(sizeof_bpf_sock_tuple, sizeof(struct bpf_sock_tuple))
	: __clobber_all);
}

SEC("tc")
__description("reference tracking: mangle and release sock")
__failure __msg("R1 pointer arithmetic on sock prohibited")
__naked void tracking_mangle_and_release_sock(void)
{
	asm volatile (
	BPF_SK_LOOKUP(bpf_sk_lookup_tcp)
"	r1 = r0;					\
	if r0 == 0 goto l0_%=;				\
	r1 += 5;					\
	call %[bpf_sk_release];				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_sk_lookup_tcp),
	  __imm(bpf_sk_release),
	  __imm_const(sizeof_bpf_sock_tuple, sizeof(struct bpf_sock_tuple))
	: __clobber_all);
}

SEC("tc")
__description("reference tracking: access member")
__success __retval(0)
__naked void reference_tracking_access_member(void)
{
	asm volatile (
	BPF_SK_LOOKUP(bpf_sk_lookup_tcp)
"	r6 = r0;					\
	if r0 == 0 goto l0_%=;				\
	r2 = *(u32*)(r0 + 4);				\
	r1 = r6;					\
	call %[bpf_sk_release];				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_sk_lookup_tcp),
	  __imm(bpf_sk_release),
	  __imm_const(sizeof_bpf_sock_tuple, sizeof(struct bpf_sock_tuple))
	: __clobber_all);
}

SEC("tc")
__description("reference tracking: write to member")
__failure __msg("cannot write into sock")
__naked void reference_tracking_write_to_member(void)
{
	asm volatile (
	BPF_SK_LOOKUP(bpf_sk_lookup_tcp)
"	r6 = r0;					\
	if r0 == 0 goto l0_%=;				\
	r1 = r6;					\
	r2 = 42 ll;					\
	*(u32*)(r1 + %[bpf_sock_mark]) = r2;		\
	r1 = r6;					\
l0_%=:	call %[bpf_sk_release];				\
	r0 = 0 ll;					\
	exit;						\
"	:
	: __imm(bpf_sk_lookup_tcp),
	  __imm(bpf_sk_release),
	  __imm_const(bpf_sock_mark, offsetof(struct bpf_sock, mark)),
	  __imm_const(sizeof_bpf_sock_tuple, sizeof(struct bpf_sock_tuple))
	: __clobber_all);
}

SEC("tc")
__description("reference tracking: invalid 64-bit access of member")
__failure __msg("invalid sock access off=0 size=8")
__naked void _64_bit_access_of_member(void)
{
	asm volatile (
	BPF_SK_LOOKUP(bpf_sk_lookup_tcp)
"	r6 = r0;					\
	if r0 == 0 goto l0_%=;				\
	r2 = *(u64*)(r0 + 0);				\
	r1 = r6;					\
	call %[bpf_sk_release];				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_sk_lookup_tcp),
	  __imm(bpf_sk_release),
	  __imm_const(sizeof_bpf_sock_tuple, sizeof(struct bpf_sock_tuple))
	: __clobber_all);
}

SEC("tc")
__description("reference tracking: access after release")
__failure __msg("!read_ok")
__naked void reference_tracking_access_after_release(void)
{
	asm volatile (
	BPF_SK_LOOKUP(bpf_sk_lookup_tcp)
"	r1 = r0;					\
	if r0 == 0 goto l0_%=;				\
	call %[bpf_sk_release];				\
	r2 = *(u32*)(r1 + 0);				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_sk_lookup_tcp),
	  __imm(bpf_sk_release),
	  __imm_const(sizeof_bpf_sock_tuple, sizeof(struct bpf_sock_tuple))
	: __clobber_all);
}

SEC("tc")
__description("reference tracking: direct access for lookup")
__success __retval(0)
__naked void tracking_direct_access_for_lookup(void)
{
	asm volatile ("					\
	/* Check that the packet is at least 64B long */\
	r2 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r3 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r0 = r2;					\
	r0 += 64;					\
	if r0 > r3 goto l0_%=;				\
	/* sk = sk_lookup_tcp(ctx, skb->data, ...) */	\
	r3 = %[sizeof_bpf_sock_tuple];			\
	r4 = 0;						\
	r5 = 0;						\
	call %[bpf_sk_lookup_tcp];			\
	r6 = r0;					\
	if r0 == 0 goto l0_%=;				\
	r2 = *(u32*)(r0 + 4);				\
	r1 = r6;					\
	call %[bpf_sk_release];				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_sk_lookup_tcp),
	  __imm(bpf_sk_release),
	  __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end)),
	  __imm_const(sizeof_bpf_sock_tuple, sizeof(struct bpf_sock_tuple))
	: __clobber_all);
}

SEC("tc")
__description("reference tracking: use ptr from bpf_tcp_sock() after release")
__failure __msg("invalid mem access")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void bpf_tcp_sock_after_release(void)
{
	asm volatile (
	BPF_SK_LOOKUP(bpf_sk_lookup_tcp)
"	if r0 != 0 goto l0_%=;				\
	exit;						\
l0_%=:	r6 = r0;					\
	r1 = r0;					\
	call %[bpf_tcp_sock];				\
	if r0 != 0 goto l1_%=;				\
	r1 = r6;					\
	call %[bpf_sk_release];				\
	exit;						\
l1_%=:	r7 = r0;					\
	r1 = r6;					\
	call %[bpf_sk_release];				\
	r0 = *(u32*)(r7 + %[bpf_tcp_sock_snd_cwnd]);	\
	exit;						\
"	:
	: __imm(bpf_sk_lookup_tcp),
	  __imm(bpf_sk_release),
	  __imm(bpf_tcp_sock),
	  __imm_const(bpf_tcp_sock_snd_cwnd, offsetof(struct bpf_tcp_sock, snd_cwnd)),
	  __imm_const(sizeof_bpf_sock_tuple, sizeof(struct bpf_sock_tuple))
	: __clobber_all);
}

SEC("tc")
__description("reference tracking: use ptr from bpf_sk_fullsock() after release")
__failure __msg("invalid mem access")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void bpf_sk_fullsock_after_release(void)
{
	asm volatile (
	BPF_SK_LOOKUP(bpf_sk_lookup_tcp)
"	if r0 != 0 goto l0_%=;				\
	exit;						\
l0_%=:	r6 = r0;					\
	r1 = r0;					\
	call %[bpf_sk_fullsock];			\
	if r0 != 0 goto l1_%=;				\
	r1 = r6;					\
	call %[bpf_sk_release];				\
	exit;						\
l1_%=:	r7 = r0;					\
	r1 = r6;					\
	call %[bpf_sk_release];				\
	r0 = *(u32*)(r7 + %[bpf_sock_type]);		\
	exit;						\
"	:
	: __imm(bpf_sk_fullsock),
	  __imm(bpf_sk_lookup_tcp),
	  __imm(bpf_sk_release),
	  __imm_const(bpf_sock_type, offsetof(struct bpf_sock, type)),
	  __imm_const(sizeof_bpf_sock_tuple, sizeof(struct bpf_sock_tuple))
	: __clobber_all);
}

SEC("tc")
__description("reference tracking: use ptr from bpf_sk_fullsock(tp) after release")
__failure __msg("invalid mem access")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void sk_fullsock_tp_after_release(void)
{
	asm volatile (
	BPF_SK_LOOKUP(bpf_sk_lookup_tcp)
"	if r0 != 0 goto l0_%=;				\
	exit;						\
l0_%=:	r6 = r0;					\
	r1 = r0;					\
	call %[bpf_tcp_sock];				\
	if r0 != 0 goto l1_%=;				\
	r1 = r6;					\
	call %[bpf_sk_release];				\
	exit;						\
l1_%=:	r1 = r0;					\
	call %[bpf_sk_fullsock];			\
	r1 = r6;					\
	r6 = r0;					\
	call %[bpf_sk_release];				\
	if r6 != 0 goto l2_%=;				\
	exit;						\
l2_%=:	r0 = *(u32*)(r6 + %[bpf_sock_type]);		\
	exit;						\
"	:
	: __imm(bpf_sk_fullsock),
	  __imm(bpf_sk_lookup_tcp),
	  __imm(bpf_sk_release),
	  __imm(bpf_tcp_sock),
	  __imm_const(bpf_sock_type, offsetof(struct bpf_sock, type)),
	  __imm_const(sizeof_bpf_sock_tuple, sizeof(struct bpf_sock_tuple))
	: __clobber_all);
}

SEC("tc")
__description("reference tracking: use sk after bpf_sk_release(tp)")
__failure __msg("invalid mem access")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void after_bpf_sk_release_tp(void)
{
	asm volatile (
	BPF_SK_LOOKUP(bpf_sk_lookup_tcp)
"	if r0 != 0 goto l0_%=;				\
	exit;						\
l0_%=:	r6 = r0;					\
	r1 = r0;					\
	call %[bpf_tcp_sock];				\
	if r0 != 0 goto l1_%=;				\
	r1 = r6;					\
	call %[bpf_sk_release];				\
	exit;						\
l1_%=:	r1 = r0;					\
	call %[bpf_sk_release];				\
	r0 = *(u32*)(r6 + %[bpf_sock_type]);		\
	exit;						\
"	:
	: __imm(bpf_sk_lookup_tcp),
	  __imm(bpf_sk_release),
	  __imm(bpf_tcp_sock),
	  __imm_const(bpf_sock_type, offsetof(struct bpf_sock, type)),
	  __imm_const(sizeof_bpf_sock_tuple, sizeof(struct bpf_sock_tuple))
	: __clobber_all);
}

SEC("tc")
__description("reference tracking: use ptr from bpf_get_listener_sock() after bpf_sk_release(sk)")
__success __retval(0)
__naked void after_bpf_sk_release_sk(void)
{
	asm volatile (
	BPF_SK_LOOKUP(bpf_sk_lookup_tcp)
"	if r0 != 0 goto l0_%=;				\
	exit;						\
l0_%=:	r6 = r0;					\
	r1 = r0;					\
	call %[bpf_get_listener_sock];			\
	if r0 != 0 goto l1_%=;				\
	r1 = r6;					\
	call %[bpf_sk_release];				\
	exit;						\
l1_%=:	r1 = r6;					\
	r6 = r0;					\
	call %[bpf_sk_release];				\
	r0 = *(u32*)(r6 + %[bpf_sock_src_port]);	\
	exit;						\
"	:
	: __imm(bpf_get_listener_sock),
	  __imm(bpf_sk_lookup_tcp),
	  __imm(bpf_sk_release),
	  __imm_const(bpf_sock_src_port, offsetof(struct bpf_sock, src_port)),
	  __imm_const(sizeof_bpf_sock_tuple, sizeof(struct bpf_sock_tuple))
	: __clobber_all);
}

SEC("tc")
__description("reference tracking: bpf_sk_release(listen_sk)")
__failure __msg("R1 must be referenced when passed to release function")
__naked void bpf_sk_release_listen_sk(void)
{
	asm volatile (
	BPF_SK_LOOKUP(bpf_sk_lookup_tcp)
"	if r0 != 0 goto l0_%=;				\
	exit;						\
l0_%=:	r6 = r0;					\
	r1 = r0;					\
	call %[bpf_get_listener_sock];			\
	if r0 != 0 goto l1_%=;				\
	r1 = r6;					\
	call %[bpf_sk_release];				\
	exit;						\
l1_%=:	r1 = r0;					\
	call %[bpf_sk_release];				\
	r0 = *(u32*)(r6 + %[bpf_sock_type]);		\
	r1 = r6;					\
	call %[bpf_sk_release];				\
	exit;						\
"	:
	: __imm(bpf_get_listener_sock),
	  __imm(bpf_sk_lookup_tcp),
	  __imm(bpf_sk_release),
	  __imm_const(bpf_sock_type, offsetof(struct bpf_sock, type)),
	  __imm_const(sizeof_bpf_sock_tuple, sizeof(struct bpf_sock_tuple))
	: __clobber_all);
}

/* !bpf_sk_fullsock(sk) is checked but !bpf_tcp_sock(sk) is not checked */
SEC("tc")
__description("reference tracking: tp->snd_cwnd after bpf_sk_fullsock(sk) and bpf_tcp_sock(sk)")
__failure __msg("invalid mem access")
__naked void and_bpf_tcp_sock_sk(void)
{
	asm volatile (
	BPF_SK_LOOKUP(bpf_sk_lookup_tcp)
"	if r0 != 0 goto l0_%=;				\
	exit;						\
l0_%=:	r6 = r0;					\
	r1 = r0;					\
	call %[bpf_sk_fullsock];			\
	r7 = r0;					\
	r1 = r6;					\
	call %[bpf_tcp_sock];				\
	r8 = r0;					\
	if r7 != 0 goto l1_%=;				\
	r1 = r6;					\
	call %[bpf_sk_release];				\
	exit;						\
l1_%=:	r0 = *(u32*)(r8 + %[bpf_tcp_sock_snd_cwnd]);	\
	r1 = r6;					\
	call %[bpf_sk_release];				\
	exit;						\
"	:
	: __imm(bpf_sk_fullsock),
	  __imm(bpf_sk_lookup_tcp),
	  __imm(bpf_sk_release),
	  __imm(bpf_tcp_sock),
	  __imm_const(bpf_tcp_sock_snd_cwnd, offsetof(struct bpf_tcp_sock, snd_cwnd)),
	  __imm_const(sizeof_bpf_sock_tuple, sizeof(struct bpf_sock_tuple))
	: __clobber_all);
}

SEC("tc")
__description("reference tracking: branch tracking valid pointer null comparison")
__success __retval(0)
__naked void tracking_valid_pointer_null_comparison(void)
{
	asm volatile (
	BPF_SK_LOOKUP(bpf_sk_lookup_tcp)
"	r6 = r0;					\
	r3 = 1;						\
	if r6 != 0 goto l0_%=;				\
	r3 = 0;						\
l0_%=:	if r6 == 0 goto l1_%=;				\
	r1 = r6;					\
	call %[bpf_sk_release];				\
l1_%=:	exit;						\
"	:
	: __imm(bpf_sk_lookup_tcp),
	  __imm(bpf_sk_release),
	  __imm_const(sizeof_bpf_sock_tuple, sizeof(struct bpf_sock_tuple))
	: __clobber_all);
}

SEC("tc")
__description("reference tracking: branch tracking valid pointer value comparison")
__failure __msg("Unreleased reference")
__naked void tracking_valid_pointer_value_comparison(void)
{
	asm volatile (
	BPF_SK_LOOKUP(bpf_sk_lookup_tcp)
"	r6 = r0;					\
	r3 = 1;						\
	if r6 == 0 goto l0_%=;				\
	r3 = 0;						\
	if r6 == 1234 goto l0_%=;			\
	r1 = r6;					\
	call %[bpf_sk_release];				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_sk_lookup_tcp),
	  __imm(bpf_sk_release),
	  __imm_const(sizeof_bpf_sock_tuple, sizeof(struct bpf_sock_tuple))
	: __clobber_all);
}

SEC("tc")
__description("reference tracking: bpf_sk_release(btf_tcp_sock)")
__success
__retval(0)
__naked void sk_release_btf_tcp_sock(void)
{
	asm volatile (
	BPF_SK_LOOKUP(bpf_sk_lookup_tcp)
"	if r0 != 0 goto l0_%=;				\
	exit;						\
l0_%=:	r6 = r0;					\
	r1 = r0;					\
	call %[bpf_skc_to_tcp_sock];			\
	if r0 != 0 goto l1_%=;				\
	r1 = r6;					\
	call %[bpf_sk_release];				\
	exit;						\
l1_%=:	r1 = r0;					\
	call %[bpf_sk_release];				\
	exit;						\
"	:
	: __imm(bpf_sk_lookup_tcp),
	  __imm(bpf_sk_release),
	  __imm(bpf_skc_to_tcp_sock),
	  __imm_const(sizeof_bpf_sock_tuple, sizeof(struct bpf_sock_tuple))
	: __clobber_all);
}

SEC("tc")
__description("reference tracking: use ptr from bpf_skc_to_tcp_sock() after release")
__failure __msg("invalid mem access")
__naked void to_tcp_sock_after_release(void)
{
	asm volatile (
	BPF_SK_LOOKUP(bpf_sk_lookup_tcp)
"	if r0 != 0 goto l0_%=;				\
	exit;						\
l0_%=:	r6 = r0;					\
	r1 = r0;					\
	call %[bpf_skc_to_tcp_sock];			\
	if r0 != 0 goto l1_%=;				\
	r1 = r6;					\
	call %[bpf_sk_release];				\
	exit;						\
l1_%=:	r7 = r0;					\
	r1 = r6;					\
	call %[bpf_sk_release];				\
	r0 = *(u8*)(r7 + 0);				\
	exit;						\
"	:
	: __imm(bpf_sk_lookup_tcp),
	  __imm(bpf_sk_release),
	  __imm(bpf_skc_to_tcp_sock),
	  __imm_const(sizeof_bpf_sock_tuple, sizeof(struct bpf_sock_tuple))
	: __clobber_all);
}

SEC("socket")
__description("reference tracking: try to leak released ptr reg")
__success __failure_unpriv __msg_unpriv("R8 !read_ok")
__retval(0)
__naked void to_leak_released_ptr_reg(void)
{
	asm volatile ("					\
	r0 = 0;						\
	*(u32*)(r10 - 4) = r0;				\
	r2 = r10;					\
	r2 += -4;					\
	r1 = %[map_array_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 != 0 goto l0_%=;				\
	exit;						\
l0_%=:	r9 = r0;					\
	r0 = 0;						\
	r1 = %[map_ringbuf] ll;				\
	r2 = 8;						\
	r3 = 0;						\
	call %[bpf_ringbuf_reserve];			\
	if r0 != 0 goto l1_%=;				\
	exit;						\
l1_%=:	r8 = r0;					\
	r1 = r8;					\
	r2 = 0;						\
	call %[bpf_ringbuf_discard];			\
	r0 = 0;						\
	*(u64*)(r9 + 0) = r8;				\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm(bpf_ringbuf_discard),
	  __imm(bpf_ringbuf_reserve),
	  __imm_addr(map_array_48b),
	  __imm_addr(map_ringbuf)
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
