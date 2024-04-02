// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <string.h>
#include "perf_regs.h"
#include "util/sample.h"
#include "debug.h"

int __weak arch_sdt_arg_parse_op(char *old_op __maybe_unused,
				 char **new_op __maybe_unused)
{
	return SDT_ARG_SKIP;
}

uint64_t __weak arch__intr_reg_mask(void)
{
	return 0;
}

uint64_t __weak arch__user_reg_mask(void)
{
	return 0;
}

static const struct sample_reg sample_reg_masks[] = {
	SMPL_REG_END
};

const struct sample_reg * __weak arch__sample_reg_masks(void)
{
	return sample_reg_masks;
}

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

uint64_t perf_arch_reg_ip(const char *arch)
{
	if (!strcmp(arch, "arm"))
		return __perf_reg_ip_arm();
	else if (!strcmp(arch, "arm64"))
		return __perf_reg_ip_arm64();
	else if (!strcmp(arch, "csky"))
		return __perf_reg_ip_csky();
	else if (!strcmp(arch, "loongarch"))
		return __perf_reg_ip_loongarch();
	else if (!strcmp(arch, "mips"))
		return __perf_reg_ip_mips();
	else if (!strcmp(arch, "powerpc"))
		return __perf_reg_ip_powerpc();
	else if (!strcmp(arch, "riscv"))
		return __perf_reg_ip_riscv();
	else if (!strcmp(arch, "s390"))
		return __perf_reg_ip_s390();
	else if (!strcmp(arch, "x86"))
		return __perf_reg_ip_x86();

	pr_err("Fail to find IP register for arch %s, returns 0\n", arch);
	return 0;
}

uint64_t perf_arch_reg_sp(const char *arch)
{
	if (!strcmp(arch, "arm"))
		return __perf_reg_sp_arm();
	else if (!strcmp(arch, "arm64"))
		return __perf_reg_sp_arm64();
	else if (!strcmp(arch, "csky"))
		return __perf_reg_sp_csky();
	else if (!strcmp(arch, "loongarch"))
		return __perf_reg_sp_loongarch();
	else if (!strcmp(arch, "mips"))
		return __perf_reg_sp_mips();
	else if (!strcmp(arch, "powerpc"))
		return __perf_reg_sp_powerpc();
	else if (!strcmp(arch, "riscv"))
		return __perf_reg_sp_riscv();
	else if (!strcmp(arch, "s390"))
		return __perf_reg_sp_s390();
	else if (!strcmp(arch, "x86"))
		return __perf_reg_sp_x86();

	pr_err("Fail to find SP register for arch %s, returns 0\n", arch);
	return 0;
}
