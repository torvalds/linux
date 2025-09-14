// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/map_ptr.c */

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
} map_array_48b SEC(".maps");

struct other_val {
	long long foo;
	long long bar;
};

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1);
	__type(key, long long);
	__type(value, struct other_val);
} map_hash_16b SEC(".maps");

SEC("socket")
__description("bpf_map_ptr: read with negative offset rejected")
__failure __msg("R1 is bpf_array invalid negative access: off=-8")
__failure_unpriv
__msg_unpriv("access is allowed only to CAP_PERFMON and CAP_SYS_ADMIN")
__naked void read_with_negative_offset_rejected(void)
{
	asm volatile ("					\
	r1 = r10;					\
	r1 = %[map_array_48b] ll;			\
	r6 = *(u64*)(r1 - 8);				\
	r0 = 1;						\
	exit;						\
"	:
	: __imm_addr(map_array_48b)
	: __clobber_all);
}

SEC("socket")
__description("bpf_map_ptr: write rejected")
__failure __msg("only read from bpf_array is supported")
__failure_unpriv
__msg_unpriv("access is allowed only to CAP_PERFMON and CAP_SYS_ADMIN")
__naked void bpf_map_ptr_write_rejected(void)
{
	asm volatile ("					\
	r0 = 0;						\
	*(u64*)(r10 - 8) = r0;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_array_48b] ll;			\
	*(u64*)(r1 + 0) = r2;				\
	r0 = 1;						\
	exit;						\
"	:
	: __imm_addr(map_array_48b)
	: __clobber_all);
}

/* The first element of struct bpf_map is a SHA256 hash of 32 bytes, accessing
 * into this array is valid. The opts field is now at offset 33.
 */
SEC("socket")
__description("bpf_map_ptr: read non-existent field rejected")
__failure
__msg("cannot access ptr member ops with moff 32 in struct bpf_map with off 33 size 4")
__failure_unpriv
__msg_unpriv("access is allowed only to CAP_PERFMON and CAP_SYS_ADMIN")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void read_non_existent_field_rejected(void)
{
	asm volatile ("					\
	r6 = 0;						\
	r1 = %[map_array_48b] ll;			\
	r6 = *(u32*)(r1 + 33);				\
	r0 = 1;						\
	exit;						\
"	:
	: __imm_addr(map_array_48b)
	: __clobber_all);
}

SEC("socket")
__description("bpf_map_ptr: read ops field accepted")
__success __failure_unpriv
__msg_unpriv("access is allowed only to CAP_PERFMON and CAP_SYS_ADMIN")
__retval(1)
__naked void ptr_read_ops_field_accepted(void)
{
	asm volatile ("					\
	r6 = 0;						\
	r1 = %[map_array_48b] ll;			\
	r6 = *(u64*)(r1 + 0);				\
	r0 = 1;						\
	exit;						\
"	:
	: __imm_addr(map_array_48b)
	: __clobber_all);
}

SEC("socket")
__description("bpf_map_ptr: r = 0, map_ptr = map_ptr + r")
__success __failure_unpriv
__msg_unpriv("R1 has pointer with unsupported alu operation")
__retval(0)
__naked void map_ptr_map_ptr_r(void)
{
	asm volatile ("					\
	r0 = 0;						\
	*(u64*)(r10 - 8) = r0;				\
	r2 = r10;					\
	r2 += -8;					\
	r0 = 0;						\
	r1 = %[map_hash_16b] ll;			\
	r1 += r0;					\
	call %[bpf_map_lookup_elem];			\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_16b)
	: __clobber_all);
}

SEC("socket")
__description("bpf_map_ptr: r = 0, r = r + map_ptr")
__success __failure_unpriv
__msg_unpriv("R0 has pointer with unsupported alu operation")
__retval(0)
__naked void _0_r_r_map_ptr(void)
{
	asm volatile ("					\
	r0 = 0;						\
	*(u64*)(r10 - 8) = r0;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	r0 = %[map_hash_16b] ll;			\
	r1 += r0;					\
	call %[bpf_map_lookup_elem];			\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_16b)
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
