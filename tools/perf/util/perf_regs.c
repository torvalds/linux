#include <errno.h>
#include "perf_regs.h"

int perf_reg_value(u64 *valp, struct regs_dump *regs, int id)
{
	int i, idx = 0;
	u64 mask = regs->mask;

	if (!(mask & (1 << id)))
		return -EINVAL;

	for (i = 0; i < id; i++) {
		if (mask & (1 << i))
			idx++;
	}

	*valp = regs->regs[idx];
	return 0;
}
