#ifndef __PERF_REGS_H
#define __PERF_REGS_H

#ifndef NO_PERF_REGS
#include <perf_regs.h>
#else
#define PERF_REGS_MASK	0

static inline const char *perf_reg_name(int id __maybe_unused)
{
	return NULL;
}
#endif /* NO_PERF_REGS */
#endif /* __PERF_REGS_H */
