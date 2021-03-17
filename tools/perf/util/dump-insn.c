// SPDX-License-Identifier: GPL-2.0
#include <linux/compiler.h>
#include "dump-insn.h"

/* Fallback code */

__weak
const char *dump_insn(struct perf_insn *x __maybe_unused,
		      u64 ip __maybe_unused, u8 *inbuf __maybe_unused,
		      int inlen __maybe_unused, int *lenp)
{
	if (lenp)
		*lenp = 0;
	return "?";
}

__weak
int arch_is_branch(const unsigned char *buf __maybe_unused,
		   size_t len __maybe_unused,
		   int x86_64 __maybe_unused)
{
	return 0;
}
