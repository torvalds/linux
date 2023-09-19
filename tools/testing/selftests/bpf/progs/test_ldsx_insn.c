// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#if (defined(__TARGET_ARCH_arm64) || defined(__TARGET_ARCH_x86) || \
     (defined(__TARGET_ARCH_riscv) && __riscv_xlen == 64) ||       \
     defined(__TARGET_ARCH_s390)) && __clang_major__ >= 18
const volatile int skip = 0;
#else
const volatile int skip = 1;
#endif

volatile const short val1 = -1;
volatile const int val2 = -1;
short val3 = -1;
int val4 = -1;
int done1, done2, ret1, ret2;

SEC("?raw_tp/sys_enter")
int rdonly_map_prog(const void *ctx)
{
	if (done1)
		return 0;

	done1 = 1;
	/* val1/val2 readonly map */
	if (val1 == val2)
		ret1 = 1;
	return 0;

}

SEC("?raw_tp/sys_enter")
int map_val_prog(const void *ctx)
{
	if (done2)
		return 0;

	done2 = 1;
	/* val1/val2 regular read/write map */
	if (val3 == val4)
		ret2 = 1;
	return 0;

}

struct bpf_testmod_struct_arg_1 {
	int a;
};

long long int_member;

SEC("?fentry/bpf_testmod_test_arg_ptr_to_struct")
int BPF_PROG2(test_ptr_struct_arg, struct bpf_testmod_struct_arg_1 *, p)
{
	/* probed memory access */
	int_member = p->a;
        return 0;
}

long long set_optlen, set_retval;

SEC("?cgroup/getsockopt")
int _getsockopt(volatile struct bpf_sockopt *ctx)
{
	int old_optlen, old_retval;

	old_optlen = ctx->optlen;
	old_retval = ctx->retval;

	ctx->optlen = -1;
	ctx->retval = -1;

	/* sign extension for ctx member */
	set_optlen = ctx->optlen;
	set_retval = ctx->retval;

	ctx->optlen = old_optlen;
	ctx->retval = old_retval;

	return 0;
}

long long set_mark;

SEC("?tc")
int _tc(volatile struct __sk_buff *skb)
{
	long long tmp_mark;
	int old_mark;

	old_mark = skb->mark;

	skb->mark = 0xf6fe;

	/* narrowed sign extension for ctx member */
#if __clang_major__ >= 18
	/* force narrow one-byte signed load. Otherwise, compiler may
	 * generate a 32-bit unsigned load followed by an s8 movsx.
	 */
	asm volatile ("r1 = *(s8 *)(%[ctx] + %[off_mark])\n\t"
		      "%[tmp_mark] = r1"
		      : [tmp_mark]"=r"(tmp_mark)
		      : [ctx]"r"(skb),
			[off_mark]"i"(offsetof(struct __sk_buff, mark)
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
			+ sizeof(skb->mark) - 1
#endif
			)
		      : "r1");
#else
	tmp_mark = (char)skb->mark;
#endif
	set_mark = tmp_mark;

	skb->mark = old_mark;

	return 0;
}

char _license[] SEC("license") = "GPL";
