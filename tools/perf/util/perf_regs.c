// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include "perf_regs.h"
#include "event.h"

const struct sample_reg __weak sample_reg_masks[] = {
	SMPL_REG_END
};

int __weak arch_sdt_arg_parse_op(char *old_op __maybe_unused,
				 char **new_op __maybe_unused)
{
	return SDT_ARG_SKIP;
}

uint64_t __weak arch__intr_reg_mask(void)
{
	return PERF_REGS_MASK;
}

uint64_t __weak arch__user_reg_mask(void)
{
	return PERF_REGS_MASK;
}

#ifdef HAVE_PERF_REGS_SUPPORT
int perf_reg_value(u64 *valp, struct regs_dump *regs, int id)
{
	int i, idx = 0;
	u64 mask = regs->mask;

	if (regs->cache_mask & (1ULL << id))
		goto out;

	if (!(mask & (1ULL << id)))
		return -EINVAL;

	for (i = 0; i < id; i++) {
		if (mask & (1ULL << i))
			idx++;
	}

	regs->cache_mask |= (1ULL << id);
	regs->cache_regs[id] = regs->regs[idx];

out:
	*valp = regs->cache_regs[id];
	return 0;
}
#endif
