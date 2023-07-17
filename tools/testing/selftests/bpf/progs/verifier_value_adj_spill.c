// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/value_adj_spill.c */

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
__description("map element value is preserved across register spilling")
__success __failure_unpriv __msg_unpriv("R0 leaks addr")
__retval(0)
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
	  __imm_addr(map_hash_48b)
	: __clobber_all);
}

SEC("socket")
__description("map element value or null is marked on register spilling")
__success __failure_unpriv __msg_unpriv("R0 leaks addr")
__retval(0)
__naked void is_marked_on_register_spilling(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	r1 = r10;					\
	r1 += -152;					\
	*(u64*)(r1 + 0) = r0;				\
	if r0 == 0 goto l0_%=;				\
	r3 = *(u64*)(r1 + 0);				\
	r1 = 42;					\
	*(u64*)(r3 + 0) = r1;				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_48b)
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
