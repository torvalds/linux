// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

int call_happened = 0;

/*
 * 32765 is the exact minimum number of padding instructions needed to
 * trigger the verifier failure, because:
 * 1. Counting the wrapper instructions around the padding block (one
 *    "r0=0" and two "exit" instructions), the actual jump distance
 *    evaluates to N + 3.
 * 2. To overflow the s16 max bound (32767), we need N + 3 > 32767.
 * Thus, N = 32765 is the exact minimum padding size required.
 */
static __attribute__((noinline)) void padding_subprog(void)
{
	asm volatile (
	"r0 = 0;"
	".rept 32765;"
	"r0 += 0;"
	".endr;"
	::: __clobber_all);
}

static __attribute__((noinline)) int target_subprog(void)
{
	/* Use volatile variable here to prevent optimization. */
	volatile int magic_ret = 3;
	return magic_ret;
}

SEC("syscall")
__success __retval(3)
int call_large_imm_test(void *ctx)
{
	/*
	 * Landing pad to handle call error on kernel without the fix,
	 * preventing kernel panic.
	 */
	asm volatile (
	"r0 = 0;"
	".rept 32768;"
	"r0 += 0;"
	".endr;"
	::: __clobber_all);

	/*
	 * The call_happened variable is 1 only when the call insn wrongly
	 * go back to the landing pad above.
	 */
	if (call_happened == 1) {
		/* Use volatile variable here to prevent optimization. */
		volatile int flag = -1;
		return flag;
	}

	call_happened = 1;

	padding_subprog();

	return target_subprog();
}

char LICENSE[] SEC("license") = "GPL";
