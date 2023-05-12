// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/prevent_map_lookup.c */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

struct {
	__uint(type, BPF_MAP_TYPE_STACK_TRACE);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u64);
} map_stacktrace SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PROG_ARRAY);
	__uint(max_entries, 8);
	__uint(key_size, sizeof(int));
	__array(values, void (void));
} map_prog2_socket SEC(".maps");

SEC("perf_event")
__description("prevent map lookup in stack trace")
__failure __msg("cannot pass map_type 7 into func bpf_map_lookup_elem")
__naked void map_lookup_in_stack_trace(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_stacktrace] ll;			\
	call %[bpf_map_lookup_elem];			\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_stacktrace)
	: __clobber_all);
}

SEC("socket")
__description("prevent map lookup in prog array")
__failure __msg("cannot pass map_type 3 into func bpf_map_lookup_elem")
__failure_unpriv
__naked void map_lookup_in_prog_array(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_prog2_socket] ll;			\
	call %[bpf_map_lookup_elem];			\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_prog2_socket)
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
