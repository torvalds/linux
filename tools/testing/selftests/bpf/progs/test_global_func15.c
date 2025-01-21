// SPDX-License-Identifier: GPL-2.0-only
#include <stddef.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

__noinline int foo(unsigned int *v)
{
	if (v)
		*v = bpf_get_prandom_u32();

	return 0;
}

SEC("cgroup_skb/ingress")
__failure __msg("At program exit the register R0 has ")
int global_func15(struct __sk_buff *skb)
{
	unsigned int v = 1;

	foo(&v);

	return v;
}

SEC("cgroup_skb/ingress")
__log_level(2) __flag(BPF_F_TEST_STATE_FREQ)
__failure
/* check that fallthrough code path marks r0 as precise */
__msg("mark_precise: frame0: regs=r0 stack= before 2: (b7) r0 = 1")
/* check that branch code path marks r0 as precise */
__msg("mark_precise: frame0: regs=r0 stack= before 0: (85) call bpf_get_prandom_u32#7")
__msg("At program exit the register R0 has ")
__naked int global_func15_tricky_pruning(void)
{
	asm volatile (
		"call %[bpf_get_prandom_u32];"
		"if r0 s> 1000 goto 1f;"
		"r0 = 1;"
	"1:"
		"goto +0;" /* checkpoint */
		/* cgroup_skb/ingress program is expected to return [0, 1]
		 * values, so branch above makes sure that in a fallthrough
		 * case we have a valid 1 stored in R0 register, but in
		 * a branch case we assign some random value to R0.  So if
		 * there is something wrong with precision tracking for R0 at
		 * program exit, we might erroneously prune branch case,
		 * because R0 in fallthrough case is imprecise (and thus any
		 * value is valid from POV of verifier is_state_equal() logic)
		 */
		"exit;"
		:
		: __imm(bpf_get_prandom_u32)
		: __clobber_common
	);
}
