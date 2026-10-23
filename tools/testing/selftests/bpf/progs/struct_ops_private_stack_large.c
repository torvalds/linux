// SPDX-License-Identifier: GPL-2.0

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "../test_kmods/bpf_testmod.h"
#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";

long val;

/* On a private stack every frame gets the whole 2 KiB budget. */
__used __naked
static long frame_2048_leaf(void)
{
	asm volatile ("					\
	r1 = 30;					\
	*(u64 *)(r10 - 2048) = r1;			\
	r1 = 12;					\
	*(u64 *)(r10 - 8) = r1;				\
	r0 = *(u64 *)(r10 - 2048);			\
	r1 = *(u64 *)(r10 - 8);				\
	r0 += r1;					\
	exit;						\
"	::: __clobber_all);
}

/* test_1 is the member bpf_testmod requests a private stack for */
SEC("struct_ops")
__naked int test_1(void)
{
	asm volatile ("					\
	r1 = 100;					\
	*(u64 *)(r10 - 2048) = r1;			\
	call frame_2048_leaf;				\
	r1 = *(u64 *)(r10 - 2048);			\
	r0 += r1;					\
	r1 = %[val] ll;					\
	*(u64 *)(r1 + 0) = r0;				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm_addr(val)
	: __clobber_all);
}

SEC(".struct_ops")
struct bpf_testmod_ops3 testmod_1 = {
	.test_1 = (void *)test_1,
};
