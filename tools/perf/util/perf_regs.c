// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <string.h>
#include "perf_regs.h"
#include "util/sample.h"

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

const char *perf_reg_name(int id, const char *arch)
{
	const char *reg_name = NULL;

	if (!strcmp(arch, "csky"))
		reg_name = __perf_reg_name_csky(id);
	else if (!strcmp(arch, "loongarch"))
		reg_name = __perf_reg_name_loongarch(id);
	else if (!strcmp(arch, "mips"))
		reg_name = __perf_reg_name_mips(id);
	else if (!strcmp(arch, "powerpc"))
		reg_name = __perf_reg_name_powerpc(id);
	else if (!strcmp(arch, "riscv"))
		reg_name = __perf_reg_name_riscv(id);
	else if (!strcmp(arch, "s390"))
		reg_name = __perf_reg_name_s390(id);
	else if (!strcmp(arch, "x86"))
		reg_name = __perf_reg_name_x86(id);
	else if (!strcmp(arch, "arm"))
		reg_name = __perf_reg_name_arm(id);
	else if (!strcmp(arch, "arm64"))
		reg_name = __perf_reg_name_arm64(id);

	return reg_name ?: "unknown";
}

int perf_reg_value(u64 *valp, struct regs_dump *regs, int id)
{
	int i, idx = 0;
	u64 mask = regs->mask;

	if ((u64)id >= PERF_SAMPLE_REGS_CACHE_SIZE)
		return -EINVAL;

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
