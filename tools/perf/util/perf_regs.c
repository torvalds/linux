// SPDX-License-Identifier: GPL-2.0
#include <elf.h>
#include <errno.h>
#include <string.h>
#include "dwarf-regs.h"
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

const char *perf_reg_name(int id, uint16_t e_machine)
{
	const char *reg_name = NULL;

	switch (e_machine) {
	case EM_ARM:
		reg_name = __perf_reg_name_arm(id);
		break;
	case EM_AARCH64:
		reg_name = __perf_reg_name_arm64(id);
		break;
	case EM_CSKY:
		reg_name = __perf_reg_name_csky(id);
		break;
	case EM_LOONGARCH:
		reg_name = __perf_reg_name_loongarch(id);
		break;
	case EM_MIPS:
		reg_name = __perf_reg_name_mips(id);
		break;
	case EM_PPC:
	case EM_PPC64:
		reg_name = __perf_reg_name_powerpc(id);
		break;
	case EM_RISCV:
		reg_name = __perf_reg_name_riscv(id);
		break;
	case EM_S390:
		reg_name = __perf_reg_name_s390(id);
		break;
	case EM_386:
	case EM_X86_64:
		reg_name = __perf_reg_name_x86(id);
		break;
	default:
		break;
	}
	if (reg_name)
		return reg_name;

	pr_debug("Failed to find register %d for ELF machine type %u\n", id, e_machine);
	return "unknown";
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

uint64_t perf_arch_reg_ip(uint16_t e_machine)
{
	switch (e_machine) {
	case EM_ARM:
		return __perf_reg_ip_arm();
	case EM_AARCH64:
		return __perf_reg_ip_arm64();
	case EM_CSKY:
		return __perf_reg_ip_csky();
	case EM_LOONGARCH:
		return __perf_reg_ip_loongarch();
	case EM_MIPS:
		return __perf_reg_ip_mips();
	case EM_PPC:
	case EM_PPC64:
		return __perf_reg_ip_powerpc();
	case EM_RISCV:
		return __perf_reg_ip_riscv();
	case EM_S390:
		return __perf_reg_ip_s390();
	case EM_386:
	case EM_X86_64:
		return __perf_reg_ip_x86();
	default:
		pr_err("Failed to find IP register for ELF machine type %u\n", e_machine);
		return 0;
	}
}

uint64_t perf_arch_reg_sp(uint16_t e_machine)
{
	switch (e_machine) {
	case EM_ARM:
		return __perf_reg_sp_arm();
	case EM_AARCH64:
		return __perf_reg_sp_arm64();
	case EM_CSKY:
		return __perf_reg_sp_csky();
	case EM_LOONGARCH:
		return __perf_reg_sp_loongarch();
	case EM_MIPS:
		return __perf_reg_sp_mips();
	case EM_PPC:
	case EM_PPC64:
		return __perf_reg_sp_powerpc();
	case EM_RISCV:
		return __perf_reg_sp_riscv();
	case EM_S390:
		return __perf_reg_sp_s390();
	case EM_386:
	case EM_X86_64:
		return __perf_reg_sp_x86();
	default:
		pr_err("Failed to find SP register for ELF machine type %u\n", e_machine);
		return 0;
	}
}
