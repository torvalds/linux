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
