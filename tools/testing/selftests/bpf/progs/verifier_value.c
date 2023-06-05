// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/value.c */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

#define MAX_ENTRIES 11

struct test_val {
	unsigned int index;
	int foo[MAX_ENTRIES];
};

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1);
	__type(key, long long);
	__type(value, struct test_val);
} map_hash_48b SEC(".maps");

SEC("socket")
__description("map element value store of cleared call register")
__failure __msg("R1 !read_ok")
__failure_unpriv __msg_unpriv("R1 !read_ok")
__naked void store_of_cleared_call_register(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	*(u64*)(r0 + 0) = r1;				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_48b)
	: __clobber_all);
}

SEC("socket")
__description("map element value with unaligned store")
__success __failure_unpriv __msg_unpriv("R0 leaks addr")
__retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void element_value_with_unaligned_store(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r0 += 3;					\
	r1 = 42;					\
	*(u64*)(r0 + 0) = r1;				\
	r1 = 43;					\
	*(u64*)(r0 + 2) = r1;				\
	r1 = 44;					\
	*(u64*)(r0 - 2) = r1;				\
	r8 = r0;					\
	r1 = 32;					\
	*(u64*)(r8 + 0) = r1;				\
	r1 = 33;					\
	*(u64*)(r8 + 2) = r1;				\
	r1 = 34;					\
	*(u64*)(r8 - 2) = r1;				\
	r8 += 5;					\
	r1 = 22;					\
	*(u64*)(r8 + 0) = r1;				\
	r1 = 23;					\
	*(u64*)(r8 + 4) = r1;				\
	r1 = 24;					\
	*(u64*)(r8 - 7) = r1;				\
	r7 = r8;					\
	r7 += 3;					\
	r1 = 22;					\
	*(u64*)(r7 + 0) = r1;				\
	r1 = 23;					\
	*(u64*)(r7 + 4) = r1;				\
	r1 = 24;					\
	*(u64*)(r7 - 4) = r1;				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_48b)
	: __clobber_all);
}

SEC("socket")
__description("map element value with unaligned load")
__success __failure_unpriv __msg_unpriv("R0 leaks addr")
__retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void element_value_with_unaligned_load(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = *(u32*)(r0 + 0);				\
	if r1 >= %[max_entries] goto l0_%=;		\
	r0 += 3;					\
	r7 = *(u64*)(r0 + 0);				\
	r7 = *(u64*)(r0 + 2);				\
	r8 = r0;					\
	r7 = *(u64*)(r8 + 0);				\
	r7 = *(u64*)(r8 + 2);				\
	r0 += 5;					\
	r7 = *(u64*)(r0 + 0);				\
	r7 = *(u64*)(r0 + 4);				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_48b),
	  __imm_const(max_entries, MAX_ENTRIES)
	: __clobber_all);
}

SEC("socket")
__description("map element value is preserved across register spilling")
__success __failure_unpriv __msg_unpriv("R0 leaks addr")
__retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void is_preserved_across_register_spilling(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r0 += %[test_val_foo];				\
	r1 = 42;					\
	*(u64*)(r0 + 0) = r1;				\
	r1 = r10;					\
	r1 += -184;					\
	*(u64*)(r1 + 0) = r0;				\
	r3 = *(u64*)(r1 + 0);				\
	r1 = 42;					\
	*(u64*)(r3 + 0) = r1;				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_48b),
	  __imm_const(test_val_foo, offsetof(struct test_val, foo))
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
