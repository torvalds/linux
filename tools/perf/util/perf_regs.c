#include <errno.h>
#include "perf_regs.h"
#include "event.h"

int perf_reg_value(u64 *valp, struct regs_dump *regs, int id)
{
	int i, idx = 0;
	u64 mask = regs->mask;

	if (regs->cache_mask & (1 << id))
		goto out;

	if (!(mask & (1 << id)))
		return -EINVAL;

	for (i = 0; i < id; i++) {
		if (mask & (1 << i))
			idx++;
	}

	regs->cache_mask |= (1 << id);
	regs->cache_regs[id] = regs->regs[idx];

out:
	*valp = regs->cache_regs[id];
	return 0;
}
