// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/array_access.c */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

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
	__uint(map_flags, BPF_F_RDONLY_PROG);
} map_array_ro SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, struct test_val);
	__uint(map_flags, BPF_F_WRONLY_PROG);
} map_array_wo SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1);
	__type(key, long long);
	__type(value, struct test_val);
} map_hash_48b SEC(".maps");

SEC("socket")
__description("valid map access into an array with a constant")
__success __failure_unpriv __msg_unpriv("R0 leaks addr")
__retval(0)
__naked void an_array_with_a_constant_1(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = %[test_val_foo];				\
	*(u64*)(r0 + 0) = r1;				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_48b),
	  __imm_const(test_val_foo, offsetof(struct test_val, foo))
	: __clobber_all);
}

SEC("socket")
__description("valid map access into an array with a register")
__success __failure_unpriv __msg_unpriv("R0 leaks addr")
__retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void an_array_with_a_register_1(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = 4;						\
	r1 <<= 2;					\
	r0 += r1;					\
	r1 = %[test_val_foo];				\
	*(u64*)(r0 + 0) = r1;				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_48b),
	  __imm_const(test_val_foo, offsetof(struct test_val, foo))
	: __clobber_all);
}

SEC("socket")
__description("valid map access into an array with a variable")
__success __failure_unpriv __msg_unpriv("R0 leaks addr")
__retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void an_array_with_a_variable_1(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = *(u32*)(r0 + 0);				\
	if r1 >= %[max_entries] goto l0_%=;		\
	r1 <<= 2;					\
	r0 += r1;					\
	r1 = %[test_val_foo];				\
	*(u64*)(r0 + 0) = r1;				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_48b),
	  __imm_const(max_entries, MAX_ENTRIES),
	  __imm_const(test_val_foo, offsetof(struct test_val, foo))
	: __clobber_all);
}

SEC("socket")
__description("valid map access into an array with a signed variable")
__success __failure_unpriv __msg_unpriv("R0 leaks addr")
__retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void array_with_a_signed_variable(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = *(u32*)(r0 + 0);				\
	if w1 s> 0xffffffff goto l1_%=;			\
	w1 = 0;						\
l1_%=:	w2 = %[max_entries];				\
	if r2 s> r1 goto l2_%=;				\
	w1 = 0;						\
l2_%=:	w1 <<= 2;					\
	r0 += r1;					\
	r1 = %[test_val_foo];				\
	*(u64*)(r0 + 0) = r1;				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_48b),
	  __imm_const(max_entries, MAX_ENTRIES),
	  __imm_const(test_val_foo, offsetof(struct test_val, foo))
	: __clobber_all);
}

SEC("socket")
__description("invalid map access into an array with a constant")
__failure __msg("invalid access to map value, value_size=48 off=48 size=8")
__failure_unpriv
__naked void an_array_with_a_constant_2(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = %[test_val_foo];				\
	*(u64*)(r0 + %[__imm_0]) = r1;			\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_48b),
	  __imm_const(__imm_0, (MAX_ENTRIES + 1) << 2),
	  __imm_const(test_val_foo, offsetof(struct test_val, foo))
	: __clobber_all);
}

SEC("socket")
__description("invalid map access into an array with a register")
__failure __msg("R0 min value is outside of the allowed memory range")
__failure_unpriv
__flag(BPF_F_ANY_ALIGNMENT)
__naked void an_array_with_a_register_2(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = %[__imm_0];				\
	r1 <<= 2;					\
	r0 += r1;					\
	r1 = %[test_val_foo];				\
	*(u64*)(r0 + 0) = r1;				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_48b),
	  __imm_const(__imm_0, MAX_ENTRIES + 1),
	  __imm_const(test_val_foo, offsetof(struct test_val, foo))
	: __clobber_all);
}

SEC("socket")
__description("invalid map access into an array with a variable")
__failure
__msg("R0 unbounded memory access, make sure to bounds check any such access")
__failure_unpriv
__flag(BPF_F_ANY_ALIGNMENT)
__naked void an_array_with_a_variable_2(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = *(u32*)(r0 + 0);				\
	r1 <<= 2;					\
	r0 += r1;					\
	r1 = %[test_val_foo];				\
	*(u64*)(r0 + 0) = r1;				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_48b),
	  __imm_const(test_val_foo, offsetof(struct test_val, foo))
	: __clobber_all);
}

SEC("socket")
__description("invalid map access into an array with no floor check")
__failure __msg("R0 unbounded memory access")
__failure_unpriv __msg_unpriv("R0 leaks addr")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void array_with_no_floor_check(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = *(u64*)(r0 + 0);				\
	w2 = %[max_entries];				\
	if r2 s> r1 goto l1_%=;				\
	w1 = 0;						\
l1_%=:	w1 <<= 2;					\
	r0 += r1;					\
	r1 = %[test_val_foo];				\
	*(u64*)(r0 + 0) = r1;				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_48b),
	  __imm_const(max_entries, MAX_ENTRIES),
	  __imm_const(test_val_foo, offsetof(struct test_val, foo))
	: __clobber_all);
}

SEC("socket")
__description("invalid map access into an array with a invalid max check")
__failure __msg("invalid access to map value, value_size=48 off=44 size=8")
__failure_unpriv __msg_unpriv("R0 leaks addr")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void with_a_invalid_max_check_1(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = *(u32*)(r0 + 0);				\
	w2 = %[__imm_0];				\
	if r2 > r1 goto l1_%=;				\
	w1 = 0;						\
l1_%=:	w1 <<= 2;					\
	r0 += r1;					\
	r1 = %[test_val_foo];				\
	*(u64*)(r0 + 0) = r1;				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_48b),
	  __imm_const(__imm_0, MAX_ENTRIES + 1),
	  __imm_const(test_val_foo, offsetof(struct test_val, foo))
	: __clobber_all);
}

SEC("socket")
__description("invalid map access into an array with a invalid max check")
__failure __msg("R0 pointer += pointer")
__failure_unpriv
__flag(BPF_F_ANY_ALIGNMENT)
__naked void with_a_invalid_max_check_2(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r8 = r0;					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r0 += r8;					\
	r0 = *(u32*)(r0 + %[test_val_foo]);		\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_48b),
	  __imm_const(test_val_foo, offsetof(struct test_val, foo))
	: __clobber_all);
}

SEC("socket")
__description("valid read map access into a read-only array 1")
__success __success_unpriv __retval(28)
__naked void a_read_only_array_1_1(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_array_ro] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r0 = *(u32*)(r0 + 0);				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_array_ro)
	: __clobber_all);
}

SEC("tc")
__description("valid read map access into a read-only array 2")
__success __retval(65507)
__naked void a_read_only_array_2_1(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_array_ro] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	r2 = 4;						\
	r3 = 0;						\
	r4 = 0;						\
	r5 = 0;						\
	call %[bpf_csum_diff];				\
l0_%=:	r0 &= 0xffff;					\
	exit;						\
"	:
	: __imm(bpf_csum_diff),
	  __imm(bpf_map_lookup_elem),
	  __imm_addr(map_array_ro)
	: __clobber_all);
}

SEC("socket")
__description("invalid write map access into a read-only array 1")
__failure __msg("write into map forbidden")
__failure_unpriv
__naked void a_read_only_array_1_2(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_array_ro] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = 42;					\
	*(u64*)(r0 + 0) = r1;				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_array_ro)
	: __clobber_all);
}

SEC("tc")
__description("invalid write map access into a read-only array 2")
__failure __msg("write into map forbidden")
__naked void a_read_only_array_2_2(void)
{
	asm volatile ("					\
	r6 = r1;					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_array_ro] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r6;					\
	r2 = 0;						\
	r3 = r0;					\
	r4 = 8;						\
	call %[bpf_skb_load_bytes];			\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm(bpf_skb_load_bytes),
	  __imm_addr(map_array_ro)
	: __clobber_all);
}

SEC("socket")
__description("valid write map access into a write-only array 1")
__success __success_unpriv __retval(1)
__naked void a_write_only_array_1_1(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_array_wo] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = 42;					\
	*(u64*)(r0 + 0) = r1;				\
l0_%=:	r0 = 1;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_array_wo)
	: __clobber_all);
}

SEC("tc")
__description("valid write map access into a write-only array 2")
__success __retval(0)
__naked void a_write_only_array_2_1(void)
{
	asm volatile ("					\
	r6 = r1;					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_array_wo] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r6;					\
	r2 = 0;						\
	r3 = r0;					\
	r4 = 8;						\
	call %[bpf_skb_load_bytes];			\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm(bpf_skb_load_bytes),
	  __imm_addr(map_array_wo)
	: __clobber_all);
}

SEC("socket")
__description("invalid read map access into a write-only array 1")
__failure __msg("read from map forbidden")
__failure_unpriv
__naked void a_write_only_array_1_2(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_array_wo] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r0 = *(u64*)(r0 + 0);				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_array_wo)
	: __clobber_all);
}

SEC("tc")
__description("invalid read map access into a write-only array 2")
__failure __msg("read from map forbidden")
__naked void a_write_only_array_2_2(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_array_wo] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	r2 = 4;						\
	r3 = 0;						\
	r4 = 0;						\
	r5 = 0;						\
	call %[bpf_csum_diff];				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_csum_diff),
	  __imm(bpf_map_lookup_elem),
	  __imm_addr(map_array_wo)
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
