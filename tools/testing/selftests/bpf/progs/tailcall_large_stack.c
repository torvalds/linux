// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

struct {
	__uint(type, BPF_MAP_TYPE_PROG_ARRAY);
	__uint(max_entries, 1);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(__u32));
} jmp_table SEC(".maps");

/* The tail call target sets up a 2 KiB frame of its own and uses both of its ends. */
SEC("tc")
__naked int classifier_0(void)
{
	asm volatile ("					\
	r1 = 42;					\
	*(u64 *)(r10 - 2048) = r1;			\
	r1 = 7;						\
	*(u64 *)(r10 - 8) = r1;				\
	r0 = *(u64 *)(r10 - 2048);			\
	r1 = *(u64 *)(r10 - 8);				\
	r0 += r1;					\
	exit;						\
"	::: __clobber_all);
}

/*
 * The frame of the subprog doing the tail call is unwound by it, so it may be
 * large; only the frames of its callers stay behind and are limited to 256
 * bytes in total. Returns 1 when the tail call falls through.
 */
__used __naked
static int subprog_tail(void)
{
	asm volatile ("					\
	r2 = 1;						\
	*(u64 *)(r10 - 1536) = r2;			\
	r2 = %[jmp_table] ll;				\
	r3 = 0;						\
	call %[bpf_tail_call];				\
	r0 = 1;						\
	exit;						\
"	:
	: __imm(bpf_tail_call),
	  __imm_addr(jmp_table)
	: __clobber_all);
}

SEC("tc")
__naked int entry(void)
{
	asm volatile ("					\
	r2 = 2;						\
	*(u64 *)(r10 - 240) = r2;			\
	call subprog_tail;				\
	exit;						\
"	::: __clobber_all);
}

char _license[] SEC("license") = "GPL";
