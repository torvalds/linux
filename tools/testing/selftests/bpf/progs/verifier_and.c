// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/and.c */

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
__description("invalid and of negative number")
__failure __msg("R0 max value is outside of the allowed memory range")
__failure_unpriv
__flag(BPF_F_ANY_ALIGNMENT)
__naked void invalid_and_of_negative_number(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = *(u8*)(r0 + 0);				\
	r1 &= -4;					\
	r1 <<= 2;					\
	r0 += r1;					\
l0_%=:	r1 = %[test_val_foo];				\
	*(u64*)(r0 + 0) = r1;				\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_48b),
	  __imm_const(test_val_foo, offsetof(struct test_val, foo))
	: __clobber_all);
}

SEC("socket")
__description("invalid range check")
__failure __msg("R0 max value is outside of the allowed memory range")
__failure_unpriv
__flag(BPF_F_ANY_ALIGNMENT)
__naked void invalid_range_check(void)
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
	r9 = 1;						\
	w1 %%= 2;					\
	w1 += 1;					\
	w9 &= w1;					\
	w9 += 1;					\
	w9 >>= 1;					\
	w3 = 1;						\
	w3 -= w9;					\
	w3 *= 0x10000000;				\
	r0 += r3;					\
	*(u32*)(r0 + 0) = r3;				\
l0_%=:	r0 = r0;					\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_48b)
	: __clobber_all);
}

SEC("socket")
__description("check known subreg with unknown reg")
__success __failure_unpriv __msg_unpriv("R1 !read_ok")
__retval(0)
__naked void known_subreg_with_unknown_reg(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r0 <<= 32;					\
	r0 += 1;					\
	r0 &= 0xFFFF1234;				\
	/* Upper bits are unknown but AND above masks out 1 zero'ing lower bits */\
	if w0 < 1 goto l0_%=;				\
	r1 = *(u32*)(r1 + 512);				\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
