/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_DUMP_INSN_H
#define __PERF_DUMP_INSN_H 1

#define MAXINSN 15

#include <linux/types.h>

struct thread;

struct perf_insn {
	/* Initialized by callers: */
	struct thread *thread;
	u8	      cpumode;
	bool	      is64bit;
	int	      cpu;
	/* Temporary */
	char	      out[256];
};

const char *dump_insn(struct perf_insn *x, u64 ip,
		      u8 *inbuf, int inlen, int *lenp);
#endif
