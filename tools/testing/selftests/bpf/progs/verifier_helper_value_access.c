// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/helper_value_access.c */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

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

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1);
	__type(key, long long);
	__type(value, long long);
} map_hash_8b SEC(".maps");

SEC("tracepoint")
__description("helper access to map: full range")
__success
__naked void access_to_map_full_range(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	r2 = %[sizeof_test_val];			\
	r3 = 0;						\
	call %[bpf_probe_read_kernel];			\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm(bpf_probe_read_kernel),
	  __imm_addr(map_hash_48b),
	  __imm_const(sizeof_test_val, sizeof(struct test_val))
	: __clobber_all);
}

SEC("tracepoint")
__description("helper access to map: partial range")
__success
__naked void access_to_map_partial_range(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	r2 = 8;						\
	r3 = 0;						\
	call %[bpf_probe_read_kernel];			\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm(bpf_probe_read_kernel),
	  __imm_addr(map_hash_48b)
	: __clobber_all);
}

SEC("tracepoint")
__description("helper access to map: empty range")
__failure __msg("invalid access to map value, value_size=48 off=0 size=0")
__naked void access_to_map_empty_range(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	r2 = 0;						\
	call %[bpf_trace_printk];			\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm(bpf_trace_printk),
	  __imm_addr(map_hash_48b)
	: __clobber_all);
}

SEC("tracepoint")
__description("helper access to map: out-of-bound range")
__failure __msg("invalid access to map value, value_size=48 off=0 size=56")
__naked void map_out_of_bound_range(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	r2 = %[__imm_0];				\
	r3 = 0;						\
	call %[bpf_probe_read_kernel];			\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm(bpf_probe_read_kernel),
	  __imm_addr(map_hash_48b),
	  __imm_const(__imm_0, sizeof(struct test_val) + 8)
	: __clobber_all);
}

SEC("tracepoint")
__description("helper access to map: negative range")
__failure __msg("R2 min value is negative")
__naked void access_to_map_negative_range(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	r2 = -8;					\
	r3 = 0;						\
	call %[bpf_probe_read_kernel];			\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm(bpf_probe_read_kernel),
	  __imm_addr(map_hash_48b)
	: __clobber_all);
}

SEC("tracepoint")
__description("helper access to adjusted map (via const imm): full range")
__success
__naked void via_const_imm_full_range(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	r1 += %[test_val_foo];				\
	r2 = %[__imm_0];				\
	r3 = 0;						\
	call %[bpf_probe_read_kernel];			\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm(bpf_probe_read_kernel),
	  __imm_addr(map_hash_48b),
	  __imm_const(__imm_0, sizeof(struct test_val) - offsetof(struct test_val, foo)),
	  __imm_const(test_val_foo, offsetof(struct test_val, foo))
	: __clobber_all);
}

SEC("tracepoint")
__description("helper access to adjusted map (via const imm): partial range")
__success
__naked void via_const_imm_partial_range(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	r1 += %[test_val_foo];				\
	r2 = 8;						\
	r3 = 0;						\
	call %[bpf_probe_read_kernel];			\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm(bpf_probe_read_kernel),
	  __imm_addr(map_hash_48b),
	  __imm_const(test_val_foo, offsetof(struct test_val, foo))
	: __clobber_all);
}

SEC("tracepoint")
__description("helper access to adjusted map (via const imm): empty range")
__failure __msg("invalid access to map value, value_size=48 off=4 size=0")
__naked void via_const_imm_empty_range(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	r1 += %[test_val_foo];				\
	r2 = 0;						\
	call %[bpf_trace_printk];			\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm(bpf_trace_printk),
	  __imm_addr(map_hash_48b),
	  __imm_const(test_val_foo, offsetof(struct test_val, foo))
	: __clobber_all);
}

SEC("tracepoint")
__description("helper access to adjusted map (via const imm): out-of-bound range")
__failure __msg("invalid access to map value, value_size=48 off=4 size=52")
__naked void imm_out_of_bound_range(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	r1 += %[test_val_foo];				\
	r2 = %[__imm_0];				\
	r3 = 0;						\
	call %[bpf_probe_read_kernel];			\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm(bpf_probe_read_kernel),
	  __imm_addr(map_hash_48b),
	  __imm_const(__imm_0, sizeof(struct test_val) - offsetof(struct test_val, foo) + 8),
	  __imm_const(test_val_foo, offsetof(struct test_val, foo))
	: __clobber_all);
}

SEC("tracepoint")
__description("helper access to adjusted map (via const imm): negative range (> adjustment)")
__failure __msg("R2 min value is negative")
__naked void const_imm_negative_range_adjustment_1(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	r1 += %[test_val_foo];				\
	r2 = -8;					\
	r3 = 0;						\
	call %[bpf_probe_read_kernel];			\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm(bpf_probe_read_kernel),
	  __imm_addr(map_hash_48b),
	  __imm_const(test_val_foo, offsetof(struct test_val, foo))
	: __clobber_all);
}

SEC("tracepoint")
__description("helper access to adjusted map (via const imm): negative range (< adjustment)")
__failure __msg("R2 min value is negative")
__naked void const_imm_negative_range_adjustment_2(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	r1 += %[test_val_foo];				\
	r2 = -1;					\
	r3 = 0;						\
	call %[bpf_probe_read_kernel];			\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm(bpf_probe_read_kernel),
	  __imm_addr(map_hash_48b),
	  __imm_const(test_val_foo, offsetof(struct test_val, foo))
	: __clobber_all);
}

SEC("tracepoint")
__description("helper access to adjusted map (via const reg): full range")
__success
__naked void via_const_reg_full_range(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	r3 = %[test_val_foo];				\
	r1 += r3;					\
	r2 = %[__imm_0];				\
	r3 = 0;						\
	call %[bpf_probe_read_kernel];			\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm(bpf_probe_read_kernel),
	  __imm_addr(map_hash_48b),
	  __imm_const(__imm_0, sizeof(struct test_val) - offsetof(struct test_val, foo)),
	  __imm_const(test_val_foo, offsetof(struct test_val, foo))
	: __clobber_all);
}

SEC("tracepoint")
__description("helper access to adjusted map (via const reg): partial range")
__success
__naked void via_const_reg_partial_range(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	r3 = %[test_val_foo];				\
	r1 += r3;					\
	r2 = 8;						\
	r3 = 0;						\
	call %[bpf_probe_read_kernel];			\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm(bpf_probe_read_kernel),
	  __imm_addr(map_hash_48b),
	  __imm_const(test_val_foo, offsetof(struct test_val, foo))
	: __clobber_all);
}

SEC("tracepoint")
__description("helper access to adjusted map (via const reg): empty range")
__failure __msg("R1 min value is outside of the allowed memory range")
__naked void via_const_reg_empty_range(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	r3 = 0;						\
	r1 += r3;					\
	r2 = 0;						\
	call %[bpf_trace_printk];			\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm(bpf_trace_printk),
	  __imm_addr(map_hash_48b)
	: __clobber_all);
}

SEC("tracepoint")
__description("helper access to adjusted map (via const reg): out-of-bound range")
__failure __msg("invalid access to map value, value_size=48 off=4 size=52")
__naked void reg_out_of_bound_range(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	r3 = %[test_val_foo];				\
	r1 += r3;					\
	r2 = %[__imm_0];				\
	r3 = 0;						\
	call %[bpf_probe_read_kernel];			\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm(bpf_probe_read_kernel),
	  __imm_addr(map_hash_48b),
	  __imm_const(__imm_0, sizeof(struct test_val) - offsetof(struct test_val, foo) + 8),
	  __imm_const(test_val_foo, offsetof(struct test_val, foo))
	: __clobber_all);
}

SEC("tracepoint")
__description("helper access to adjusted map (via const reg): negative range (> adjustment)")
__failure __msg("R2 min value is negative")
__naked void const_reg_negative_range_adjustment_1(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	r3 = %[test_val_foo];				\
	r1 += r3;					\
	r2 = -8;					\
	r3 = 0;						\
	call %[bpf_probe_read_kernel];			\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm(bpf_probe_read_kernel),
	  __imm_addr(map_hash_48b),
	  __imm_const(test_val_foo, offsetof(struct test_val, foo))
	: __clobber_all);
}

SEC("tracepoint")
__description("helper access to adjusted map (via const reg): negative range (< adjustment)")
__failure __msg("R2 min value is negative")
__naked void const_reg_negative_range_adjustment_2(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	r3 = %[test_val_foo];				\
	r1 += r3;					\
	r2 = -1;					\
	r3 = 0;						\
	call %[bpf_probe_read_kernel];			\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm(bpf_probe_read_kernel),
	  __imm_addr(map_hash_48b),
	  __imm_const(test_val_foo, offsetof(struct test_val, foo))
	: __clobber_all);
}

SEC("tracepoint")
__description("helper access to adjusted map (via variable): full range")
__success
__naked void map_via_variable_full_range(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	r3 = *(u32*)(r0 + 0);				\
	if r3 > %[test_val_foo] goto l0_%=;		\
	r1 += r3;					\
	r2 = %[__imm_0];				\
	r3 = 0;						\
	call %[bpf_probe_read_kernel];			\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm(bpf_probe_read_kernel),
	  __imm_addr(map_hash_48b),
	  __imm_const(__imm_0, sizeof(struct test_val) - offsetof(struct test_val, foo)),
	  __imm_const(test_val_foo, offsetof(struct test_val, foo))
	: __clobber_all);
}

SEC("tracepoint")
__description("helper access to adjusted map (via variable): partial range")
__success
__naked void map_via_variable_partial_range(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	r3 = *(u32*)(r0 + 0);				\
	if r3 > %[test_val_foo] goto l0_%=;		\
	r1 += r3;					\
	r2 = 8;						\
	r3 = 0;						\
	call %[bpf_probe_read_kernel];			\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm(bpf_probe_read_kernel),
	  __imm_addr(map_hash_48b),
	  __imm_const(test_val_foo, offsetof(struct test_val, foo))
	: __clobber_all);
}

SEC("tracepoint")
__description("helper access to adjusted map (via variable): empty range")
__failure __msg("R1 min value is outside of the allowed memory range")
__naked void map_via_variable_empty_range(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	r3 = *(u32*)(r0 + 0);				\
	if r3 > %[test_val_foo] goto l0_%=;		\
	r1 += r3;					\
	r2 = 0;						\
	call %[bpf_trace_printk];			\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm(bpf_trace_printk),
	  __imm_addr(map_hash_48b),
	  __imm_const(test_val_foo, offsetof(struct test_val, foo))
	: __clobber_all);
}

SEC("tracepoint")
__description("helper access to adjusted map (via variable): no max check")
__failure __msg("R1 unbounded memory access")
__naked void via_variable_no_max_check_1(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	r3 = *(u32*)(r0 + 0);				\
	r1 += r3;					\
	r2 = 1;						\
	r3 = 0;						\
	call %[bpf_probe_read_kernel];			\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm(bpf_probe_read_kernel),
	  __imm_addr(map_hash_48b)
	: __clobber_all);
}

SEC("tracepoint")
__description("helper access to adjusted map (via variable): wrong max check")
__failure __msg("invalid access to map value, value_size=48 off=4 size=45")
__naked void via_variable_wrong_max_check_1(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	r3 = *(u32*)(r0 + 0);				\
	if r3 > %[test_val_foo] goto l0_%=;		\
	r1 += r3;					\
	r2 = %[__imm_0];				\
	r3 = 0;						\
	call %[bpf_probe_read_kernel];			\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm(bpf_probe_read_kernel),
	  __imm_addr(map_hash_48b),
	  __imm_const(__imm_0, sizeof(struct test_val) - offsetof(struct test_val, foo) + 1),
	  __imm_const(test_val_foo, offsetof(struct test_val, foo))
	: __clobber_all);
}

SEC("tracepoint")
__description("helper access to map: bounds check using <, good access")
__success
__naked void bounds_check_using_good_access_1(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	r3 = *(u32*)(r0 + 0);				\
	if r3 < 32 goto l1_%=;				\
	r0 = 0;						\
l0_%=:	exit;						\
l1_%=:	r1 += r3;					\
	r0 = 0;						\
	*(u8*)(r1 + 0) = r0;				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_48b)
	: __clobber_all);
}

SEC("tracepoint")
__description("helper access to map: bounds check using <, bad access")
__failure __msg("R1 unbounded memory access")
__naked void bounds_check_using_bad_access_1(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	r3 = *(u32*)(r0 + 0);				\
	if r3 < 32 goto l1_%=;				\
	r1 += r3;					\
l0_%=:	r0 = 0;						\
	*(u8*)(r1 + 0) = r0;				\
	r0 = 0;						\
	exit;						\
l1_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_48b)
	: __clobber_all);
}

SEC("tracepoint")
__description("helper access to map: bounds check using <=, good access")
__success
__naked void bounds_check_using_good_access_2(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	r3 = *(u32*)(r0 + 0);				\
	if r3 <= 32 goto l1_%=;				\
	r0 = 0;						\
l0_%=:	exit;						\
l1_%=:	r1 += r3;					\
	r0 = 0;						\
	*(u8*)(r1 + 0) = r0;				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_48b)
	: __clobber_all);
}

SEC("tracepoint")
__description("helper access to map: bounds check using <=, bad access")
__failure __msg("R1 unbounded memory access")
__naked void bounds_check_using_bad_access_2(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	r3 = *(u32*)(r0 + 0);				\
	if r3 <= 32 goto l1_%=;				\
	r1 += r3;					\
l0_%=:	r0 = 0;						\
	*(u8*)(r1 + 0) = r0;				\
	r0 = 0;						\
	exit;						\
l1_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_48b)
	: __clobber_all);
}

SEC("tracepoint")
__description("helper access to map: bounds check using s<, good access")
__success
__naked void check_using_s_good_access_1(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	r3 = *(u32*)(r0 + 0);				\
	if r3 s< 32 goto l1_%=;				\
l2_%=:	r0 = 0;						\
l0_%=:	exit;						\
l1_%=:	if r3 s< 0 goto l2_%=;				\
	r1 += r3;					\
	r0 = 0;						\
	*(u8*)(r1 + 0) = r0;				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_48b)
	: __clobber_all);
}

SEC("tracepoint")
__description("helper access to map: bounds check using s<, good access 2")
__success
__naked void using_s_good_access_2_1(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	r3 = *(u32*)(r0 + 0);				\
	if r3 s< 32 goto l1_%=;				\
l2_%=:	r0 = 0;						\
l0_%=:	exit;						\
l1_%=:	if r3 s< -3 goto l2_%=;				\
	r1 += r3;					\
	r0 = 0;						\
	*(u8*)(r1 + 0) = r0;				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_48b)
	: __clobber_all);
}

SEC("tracepoint")
__description("helper access to map: bounds check using s<, bad access")
__failure __msg("R1 min value is negative")
__naked void check_using_s_bad_access_1(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	r3 = *(u64*)(r0 + 0);				\
	if r3 s< 32 goto l1_%=;				\
l2_%=:	r0 = 0;						\
l0_%=:	exit;						\
l1_%=:	if r3 s< -3 goto l2_%=;				\
	r1 += r3;					\
	r0 = 0;						\
	*(u8*)(r1 + 0) = r0;				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_48b)
	: __clobber_all);
}

SEC("tracepoint")
__description("helper access to map: bounds check using s<=, good access")
__success
__naked void check_using_s_good_access_2(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	r3 = *(u32*)(r0 + 0);				\
	if r3 s<= 32 goto l1_%=;			\
l2_%=:	r0 = 0;						\
l0_%=:	exit;						\
l1_%=:	if r3 s<= 0 goto l2_%=;				\
	r1 += r3;					\
	r0 = 0;						\
	*(u8*)(r1 + 0) = r0;				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_48b)
	: __clobber_all);
}

SEC("tracepoint")
__description("helper access to map: bounds check using s<=, good access 2")
__success
__naked void using_s_good_access_2_2(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	r3 = *(u32*)(r0 + 0);				\
	if r3 s<= 32 goto l1_%=;			\
l2_%=:	r0 = 0;						\
l0_%=:	exit;						\
l1_%=:	if r3 s<= -3 goto l2_%=;			\
	r1 += r3;					\
	r0 = 0;						\
	*(u8*)(r1 + 0) = r0;				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_48b)
	: __clobber_all);
}

SEC("tracepoint")
__description("helper access to map: bounds check using s<=, bad access")
__failure __msg("R1 min value is negative")
__naked void check_using_s_bad_access_2(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	r3 = *(u64*)(r0 + 0);				\
	if r3 s<= 32 goto l1_%=;			\
l2_%=:	r0 = 0;						\
l0_%=:	exit;						\
l1_%=:	if r3 s<= -3 goto l2_%=;			\
	r1 += r3;					\
	r0 = 0;						\
	*(u8*)(r1 + 0) = r0;				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_48b)
	: __clobber_all);
}

SEC("tracepoint")
__description("map lookup helper access to map")
__success
__naked void lookup_helper_access_to_map(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_16b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r2 = r0;					\
	r1 = %[map_hash_16b] ll;			\
	call %[bpf_map_lookup_elem];			\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_16b)
	: __clobber_all);
}

SEC("tracepoint")
__description("map update helper access to map")
__success
__naked void update_helper_access_to_map(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_16b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r4 = 0;						\
	r3 = r0;					\
	r2 = r0;					\
	r1 = %[map_hash_16b] ll;			\
	call %[bpf_map_update_elem];			\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm(bpf_map_update_elem),
	  __imm_addr(map_hash_16b)
	: __clobber_all);
}

SEC("tracepoint")
__description("map update helper access to map: wrong size")
__failure __msg("invalid access to map value, value_size=8 off=0 size=16")
__naked void access_to_map_wrong_size(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r4 = 0;						\
	r3 = r0;					\
	r2 = r0;					\
	r1 = %[map_hash_16b] ll;			\
	call %[bpf_map_update_elem];			\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm(bpf_map_update_elem),
	  __imm_addr(map_hash_16b),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("tracepoint")
__description("map helper access to adjusted map (via const imm)")
__success
__naked void adjusted_map_via_const_imm(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_16b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r2 = r0;					\
	r2 += %[other_val_bar];				\
	r1 = %[map_hash_16b] ll;			\
	call %[bpf_map_lookup_elem];			\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_16b),
	  __imm_const(other_val_bar, offsetof(struct other_val, bar))
	: __clobber_all);
}

SEC("tracepoint")
__description("map helper access to adjusted map (via const imm): out-of-bound 1")
__failure __msg("invalid access to map value, value_size=16 off=12 size=8")
__naked void imm_out_of_bound_1(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_16b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r2 = r0;					\
	r2 += %[__imm_0];				\
	r1 = %[map_hash_16b] ll;			\
	call %[bpf_map_lookup_elem];			\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_16b),
	  __imm_const(__imm_0, sizeof(struct other_val) - 4)
	: __clobber_all);
}

SEC("tracepoint")
__description("map helper access to adjusted map (via const imm): out-of-bound 2")
__failure __msg("invalid access to map value, value_size=16 off=-4 size=8")
__naked void imm_out_of_bound_2(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_16b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r2 = r0;					\
	r2 += -4;					\
	r1 = %[map_hash_16b] ll;			\
	call %[bpf_map_lookup_elem];			\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_16b)
	: __clobber_all);
}

SEC("tracepoint")
__description("map helper access to adjusted map (via const reg)")
__success
__naked void adjusted_map_via_const_reg(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_16b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r2 = r0;					\
	r3 = %[other_val_bar];				\
	r2 += r3;					\
	r1 = %[map_hash_16b] ll;			\
	call %[bpf_map_lookup_elem];			\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_16b),
	  __imm_const(other_val_bar, offsetof(struct other_val, bar))
	: __clobber_all);
}

SEC("tracepoint")
__description("map helper access to adjusted map (via const reg): out-of-bound 1")
__failure __msg("invalid access to map value, value_size=16 off=12 size=8")
__naked void reg_out_of_bound_1(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_16b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r2 = r0;					\
	r3 = %[__imm_0];				\
	r2 += r3;					\
	r1 = %[map_hash_16b] ll;			\
	call %[bpf_map_lookup_elem];			\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_16b),
	  __imm_const(__imm_0, sizeof(struct other_val) - 4)
	: __clobber_all);
}

SEC("tracepoint")
__description("map helper access to adjusted map (via const reg): out-of-bound 2")
__failure __msg("invalid access to map value, value_size=16 off=-4 size=8")
__naked void reg_out_of_bound_2(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_16b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r2 = r0;					\
	r3 = -4;					\
	r2 += r3;					\
	r1 = %[map_hash_16b] ll;			\
	call %[bpf_map_lookup_elem];			\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_16b)
	: __clobber_all);
}

SEC("tracepoint")
__description("map helper access to adjusted map (via variable)")
__success
__naked void to_adjusted_map_via_variable(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_16b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r2 = r0;					\
	r3 = *(u32*)(r0 + 0);				\
	if r3 > %[other_val_bar] goto l0_%=;		\
	r2 += r3;					\
	r1 = %[map_hash_16b] ll;			\
	call %[bpf_map_lookup_elem];			\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_16b),
	  __imm_const(other_val_bar, offsetof(struct other_val, bar))
	: __clobber_all);
}

SEC("tracepoint")
__description("map helper access to adjusted map (via variable): no max check")
__failure
__msg("R2 unbounded memory access, make sure to bounds check any such access")
__naked void via_variable_no_max_check_2(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_16b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r2 = r0;					\
	r3 = *(u32*)(r0 + 0);				\
	r2 += r3;					\
	r1 = %[map_hash_16b] ll;			\
	call %[bpf_map_lookup_elem];			\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_16b)
	: __clobber_all);
}

SEC("tracepoint")
__description("map helper access to adjusted map (via variable): wrong max check")
__failure __msg("invalid access to map value, value_size=16 off=9 size=8")
__naked void via_variable_wrong_max_check_2(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_16b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r2 = r0;					\
	r3 = *(u32*)(r0 + 0);				\
	if r3 > %[__imm_0] goto l0_%=;			\
	r2 += r3;					\
	r1 = %[map_hash_16b] ll;			\
	call %[bpf_map_lookup_elem];			\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_16b),
	  __imm_const(__imm_0, offsetof(struct other_val, bar) + 1)
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
