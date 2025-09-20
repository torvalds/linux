// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */

#include <errno.h>
#include <string.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";

#define ITER_HELPERS						\
	  __imm(bpf_iter_num_new),				\
	  __imm(bpf_iter_num_next),				\
	  __imm(bpf_iter_num_destroy)

SEC("?raw_tp")
__success
int force_clang_to_emit_btf_for_externs(void *ctx)
{
	/* we need this as a workaround to enforce compiler emitting BTF
	 * information for bpf_iter_num_{new,next,destroy}() kfuncs,
	 * as, apparently, it doesn't emit it for symbols only referenced from
	 * assembly (or cleanup attribute, for that matter, as well)
	 */
	bpf_repeat(0);

	return 0;
}

SEC("?raw_tp")
__success
int consume_first_item_only(void *ctx)
{
	struct bpf_iter_num iter;

	asm volatile (
		/* create iterator */
		"r1 = %[iter];"
		"r2 = 0;"
		"r3 = 1000;"
		"call %[bpf_iter_num_new];"

		/* consume first item */
		"r1 = %[iter];"
		"call %[bpf_iter_num_next];"

		"if r0 == 0 goto +1;"
		"r0 = *(u32 *)(r0 + 0);"

		/* destroy iterator */
		"r1 = %[iter];"
		"call %[bpf_iter_num_destroy];"
		:
		: __imm_ptr(iter), ITER_HELPERS
		: __clobber_common
	);

	return 0;
}

SEC("?raw_tp")
__failure __msg("R0 invalid mem access 'scalar'")
int missing_null_check_fail(void *ctx)
{
	struct bpf_iter_num iter;

	asm volatile (
		/* create iterator */
		"r1 = %[iter];"
		"r2 = 0;"
		"r3 = 1000;"
		"call %[bpf_iter_num_new];"

		/* consume first element */
		"r1 = %[iter];"
		"call %[bpf_iter_num_next];"

		/* FAIL: deref with no NULL check */
		"r1 = *(u32 *)(r0 + 0);"

		/* destroy iterator */
		"r1 = %[iter];"
		"call %[bpf_iter_num_destroy];"
		:
		: __imm_ptr(iter), ITER_HELPERS
		: __clobber_common
	);

	return 0;
}

SEC("?raw_tp")
__failure
__msg("invalid access to memory, mem_size=4 off=0 size=8")
__msg("R0 min value is outside of the allowed memory range")
int wrong_sized_read_fail(void *ctx)
{
	struct bpf_iter_num iter;

	asm volatile (
		/* create iterator */
		"r1 = %[iter];"
		"r2 = 0;"
		"r3 = 1000;"
		"call %[bpf_iter_num_new];"

		/* consume first element */
		"r1 = %[iter];"
		"call %[bpf_iter_num_next];"

		"if r0 == 0 goto +1;"
		/* FAIL: deref more than available 4 bytes */
		"r0 = *(u64 *)(r0 + 0);"

		/* destroy iterator */
		"r1 = %[iter];"
		"call %[bpf_iter_num_destroy];"
		:
		: __imm_ptr(iter), ITER_HELPERS
		: __clobber_common
	);

	return 0;
}

SEC("?raw_tp")
__success __log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
int simplest_loop(void *ctx)
{
	struct bpf_iter_num iter;

	asm volatile (
		"r6 = 0;" /* init sum */

		/* create iterator */
		"r1 = %[iter];"
		"r2 = 0;"
		"r3 = 10;"
		"call %[bpf_iter_num_new];"

	"1:"
		/* consume next item */
		"r1 = %[iter];"
		"call %[bpf_iter_num_next];"

		"if r0 == 0 goto 2f;"
		"r0 = *(u32 *)(r0 + 0);"
		"r6 += r0;" /* accumulate sum */
		"goto 1b;"

	"2:"
		/* destroy iterator */
		"r1 = %[iter];"
		"call %[bpf_iter_num_destroy];"
		:
		: __imm_ptr(iter), ITER_HELPERS
		: __clobber_common, "r6"
	);

	return 0;
}
