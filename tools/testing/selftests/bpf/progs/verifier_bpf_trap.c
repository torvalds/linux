// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

#if __clang_major__ >= 21 && 0
SEC("socket")
__description("__builtin_trap with simple c code")
__failure __msg("unexpected __bpf_trap() due to uninitialized variable?")
void bpf_builtin_trap_with_simple_c(void)
{
	__builtin_trap();
}
#endif

SEC("socket")
__description("__bpf_trap with simple c code")
__failure __msg("unexpected __bpf_trap() due to uninitialized variable?")
void bpf_trap_with_simple_c(void)
{
	__bpf_trap();
}

SEC("socket")
__description("__bpf_trap as the second-from-last insn")
__failure __msg("unexpected __bpf_trap() due to uninitialized variable?")
__naked void bpf_trap_at_func_end(void)
{
	asm volatile (
	"r0 = 0;"
	"call %[__bpf_trap];"
	"exit;"
	:
	: __imm(__bpf_trap)
	: __clobber_all);
}

SEC("socket")
__description("dead code __bpf_trap in the middle of code")
__success
__naked void dead_bpf_trap_in_middle(void)
{
	asm volatile (
	"r0 = 0;"
	"if r0 == 0 goto +1;"
	"call %[__bpf_trap];"
	"r0 = 2;"
	"exit;"
	:
	: __imm(__bpf_trap)
	: __clobber_all);
}

SEC("socket")
__description("reachable __bpf_trap in the middle of code")
__failure __msg("unexpected __bpf_trap() due to uninitialized variable?")
__naked void live_bpf_trap_in_middle(void)
{
	asm volatile (
	"r0 = 0;"
	"if r0 == 1 goto +1;"
	"call %[__bpf_trap];"
	"r0 = 2;"
	"exit;"
	:
	: __imm(__bpf_trap)
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
