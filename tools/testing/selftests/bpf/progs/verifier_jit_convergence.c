// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

struct value_t {
	long long a[32];
};

struct {
        __uint(type, BPF_MAP_TYPE_HASH);
        __uint(max_entries, 1);
        __type(key, long long);
        __type(value, struct value_t);
} map_hash SEC(".maps");

SEC("socket")
__description("bpf_jit_convergence je <-> jmp")
__success __retval(0)
__arch_x86_64
__jited("	pushq	%rbp")
__naked void btf_jit_convergence_je_jmp(void)
{
	asm volatile (
	"call %[bpf_get_prandom_u32];"
	"if r0 == 0 goto l20_%=;"
	"if r0 == 1 goto l21_%=;"
	"if r0 == 2 goto l22_%=;"
	"if r0 == 3 goto l23_%=;"
	"if r0 == 4 goto l24_%=;"
	"call %[bpf_get_prandom_u32];"
	"call %[bpf_get_prandom_u32];"
"l20_%=:"
"l21_%=:"
"l22_%=:"
"l23_%=:"
"l24_%=:"
	"r1 = 0;"
	"*(u64 *)(r10 - 8) = r1;"
	"r2 = r10;"
	"r2 += -8;"
	"r1 = %[map_hash] ll;"
	"call %[bpf_map_lookup_elem];"
	"if r0 == 0 goto l1_%=;"
	"r6 = r0;"
	"call %[bpf_get_prandom_u32];"
	"r7 = r0;"
	"r5 = r6;"
	"if r0 != 0x0 goto l12_%=;"
	"call %[bpf_get_prandom_u32];"
	"r1 = r0;"
	"r2 = r6;"
	"if r1 == 0x0 goto l0_%=;"
"l9_%=:"
	"r2 = *(u64 *)(r6 + 0x0);"
	"r2 += 0x1;"
	"*(u64 *)(r6 + 0x0) = r2;"
	"goto l1_%=;"
"l12_%=:"
	"r1 = r7;"
	"r1 += 0x98;"
	"r2 = r5;"
	"r2 += 0x90;"
	"r2 = *(u32 *)(r2 + 0x0);"
	"r3 = r7;"
	"r3 &= 0x1;"
	"r2 *= 0xa8;"
	"if r3 == 0x0 goto l2_%=;"
	"r1 += r2;"
	"r1 -= r7;"
	"r1 += 0x8;"
	"if r1 <= 0xb20 goto l3_%=;"
	"r1 = 0x0;"
	"goto l4_%=;"
"l3_%=:"
	"r1 += r7;"
"l4_%=:"
	"if r1 == 0x0 goto l8_%=;"
	"goto l9_%=;"
"l2_%=:"
	"r1 += r2;"
	"r1 -= r7;"
	"r1 += 0x10;"
	"if r1 <= 0xb20 goto l6_%=;"
	"r1 = 0x0;"
	"goto l7_%=;"
"l6_%=:"
	"r1 += r7;"
"l7_%=:"
	"if r1 == 0x0 goto l8_%=;"
	"goto l9_%=;"
"l0_%=:"
	"r1 = 0x3;"
	"*(u64 *)(r10 - 0x10) = r1;"
	"r2 = r1;"
	"goto l1_%=;"
"l8_%=:"
	"r1 = r5;"
	"r1 += 0x4;"
	"r1 = *(u32 *)(r1 + 0x0);"
	"*(u64 *)(r10 - 0x8) = r1;"
"l1_%=:"
	"r0 = 0;"
	"exit;"
	:
	: __imm(bpf_get_prandom_u32),
	  __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash)
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
