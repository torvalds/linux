// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/map_ret_val.c */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "../../../include/linux/filter.h"
#include "bpf_misc.h"

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1);
	__type(key, long long);
	__type(value, long long);
} map_hash_8b SEC(".maps");

SEC("socket")
__description("invalid map_fd for function call")
__failure __msg("fd 0 is not pointing to valid bpf_map")
__failure_unpriv
__naked void map_fd_for_function_call(void)
{
	asm volatile ("					\
	r2 = 0;						\
	*(u64*)(r10 - 8) = r2;				\
	r2 = r10;					\
	r2 += -8;					\
	.8byte %[ld_map_fd];				\
	.8byte 0;					\
	call %[bpf_map_delete_elem];			\
	exit;						\
"	:
	: __imm(bpf_map_delete_elem),
	  __imm_insn(ld_map_fd, BPF_RAW_INSN(BPF_LD | BPF_DW | BPF_IMM, BPF_REG_1, BPF_PSEUDO_MAP_FD, 0, 0))
	: __clobber_all);
}

SEC("socket")
__description("don't check return value before access")
__failure __msg("R0 invalid mem access 'map_value_or_null'")
__failure_unpriv
__naked void check_return_value_before_access(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	r1 = 0;						\
	*(u64*)(r0 + 0) = r1;				\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("socket")
__description("access memory with incorrect alignment")
__failure __msg("misaligned value access")
__failure_unpriv
__flag(BPF_F_STRICT_ALIGNMENT)
__naked void access_memory_with_incorrect_alignment_1(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = 0;						\
	*(u64*)(r0 + 4) = r1;				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("socket")
__description("sometimes access memory with incorrect alignment")
__failure __msg("R0 invalid mem access")
__msg_unpriv("R0 leaks addr")
__flag(BPF_F_STRICT_ALIGNMENT)
__naked void access_memory_with_incorrect_alignment_2(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = 0;						\
	*(u64*)(r0 + 0) = r1;				\
	exit;						\
l0_%=:	r1 = 1;						\
	*(u64*)(r0 + 0) = r1;				\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
