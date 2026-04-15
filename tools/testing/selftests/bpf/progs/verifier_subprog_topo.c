// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

/* linear chain main -> A -> B */
__naked __noinline __used
static unsigned long linear_b(void)
{
	asm volatile (
		"r0 = 42;"
		"exit;"
	);
}

__naked __noinline __used
static unsigned long linear_a(void)
{
	asm volatile (
		"call linear_b;"
		"exit;"
	);
}

SEC("?raw_tp")
__success __log_level(2)
__msg("topo_order[0] = linear_b")
__msg("topo_order[1] = linear_a")
__msg("topo_order[2] = topo_linear")
__naked int topo_linear(void)
{
	asm volatile (
		"call linear_a;"
		"exit;"
	);
}

/* diamond main -> A, main -> B, A -> C, B -> C */
__naked __noinline __used
static unsigned long diamond_c(void)
{
	asm volatile (
		"r0 = 1;"
		"exit;"
	);
}

__naked __noinline __used
static unsigned long diamond_b(void)
{
	asm volatile (
		"call diamond_c;"
		"exit;"
	);
}

__naked __noinline __used
static unsigned long diamond_a(void)
{
	asm volatile (
		"call diamond_c;"
		"exit;"
	);
}

SEC("?raw_tp")
__success __log_level(2)
__msg("topo_order[0] = diamond_c")
__msg("topo_order[3] = topo_diamond")
__naked int topo_diamond(void)
{
	asm volatile (
		"call diamond_a;"
		"call diamond_b;"
		"exit;"
	);
}

/* main -> global_a (global) -> static_leaf (static, leaf) */
__naked __noinline __used
static unsigned long static_leaf(void)
{
	asm volatile (
		"r0 = 7;"
		"exit;"
	);
}

__noinline __used
int global_a(int x)
{
	return static_leaf();
}

SEC("?raw_tp")
__success __log_level(2)
__msg("topo_order[0] = static_leaf")
__msg("topo_order[1] = global_a")
__msg("topo_order[2] = topo_mixed")
__naked int topo_mixed(void)
{
	asm volatile (
		"r1 = 0;"
		"call global_a;"
		"exit;"
	);
}

/*
 * shared static callee from global and main:
 * main -> shared_leaf (static)
 * main -> global_b (global) -> shared_leaf (static)
 */
__naked __noinline __used
static unsigned long shared_leaf(void)
{
	asm volatile (
		"r0 = 99;"
		"exit;"
	);
}

__noinline __used
int global_b(int x)
{
	return shared_leaf();
}

SEC("?raw_tp")
__success __log_level(2)
__msg("topo_order[0] = shared_leaf")
__msg("topo_order[1] = global_b")
__msg("topo_order[2] = topo_shared")
__naked int topo_shared(void)
{
	asm volatile (
		"call shared_leaf;"
		"r1 = 0;"
		"call global_b;"
		"exit;"
	);
}

/* duplicate calls to the same subprog */
__naked __noinline __used
static unsigned long dup_leaf(void)
{
	asm volatile (
		"r0 = 0;"
		"exit;"
	);
}

SEC("?raw_tp")
__success __log_level(2)
__msg("topo_order[0] = dup_leaf")
__msg("topo_order[1] = topo_dup_calls")
__naked int topo_dup_calls(void)
{
	asm volatile (
		"call dup_leaf;"
		"call dup_leaf;"
		"exit;"
	);
}

/* main calls bpf_loop() with loop_cb as the callback */
static int loop_cb(int idx, void *ctx)
{
	return 0;
}

SEC("?raw_tp")
__success __log_level(2)
__msg("topo_order[0] = loop_cb")
__msg("topo_order[1] = topo_loop_cb")
int topo_loop_cb(void)
{
	bpf_loop(1, loop_cb, NULL, 0);
	return 0;
}

/*
 * bpf_loop callback calling another subprog
 * main -> bpf_loop(callback=loop_cb2) -> loop_cb2 -> loop_cb2_leaf
 */
__naked __noinline __used
static unsigned long loop_cb2_leaf(void)
{
	asm volatile (
		"r0 = 0;"
		"exit;"
	);
}

static int loop_cb2(int idx, void *ctx)
{
	return loop_cb2_leaf();
}

SEC("?raw_tp")
__success __log_level(2)
__msg("topo_order[0] = loop_cb2_leaf")
__msg("topo_order[1] = loop_cb2")
__msg("topo_order[2] = topo_loop_cb_chain")
int topo_loop_cb_chain(void)
{
	bpf_loop(1, loop_cb2, NULL, 0);
	return 0;
}

/* no calls (single subprog) */
SEC("?raw_tp")
__success __log_level(2)
__msg("topo_order[0] = topo_no_calls")
__naked int topo_no_calls(void)
{
	asm volatile (
		"r0 = 0;"
		"exit;"
	);
}

char _license[] SEC("license") = "GPL";
