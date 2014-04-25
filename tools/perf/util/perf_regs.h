#ifndef __PERF_REGS_H
#define __PERF_REGS_H

#include <linux/types.h>
#include "event.h"

#ifdef HAVE_PERF_REGS_SUPPORT
#include <perf_regs.h>

int perf_reg_value(u64 *valp, struct regs_dump *regs, int id);

#else
#define PERF_REGS_MASK	0

static inline const char *perf_reg_name(int id __maybe_unused)
{
	return NULL;
}

static inline int perf_reg_value(u64 *valp __maybe_unused,
				 struct regs_dump *regs __maybe_unused,
				 int id __maybe_unused)
{
	return 0;
}
#endif /* HAVE_PERF_REGS_SUPPORT */
#endif /* __PERF_REGS_H */
