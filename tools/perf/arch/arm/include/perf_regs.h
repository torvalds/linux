/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ARCH_PERF_REGS_H
#define ARCH_PERF_REGS_H

#include <stdlib.h>
#include <linux/types.h>
#include <asm/perf_regs.h>

void perf_regs_load(u64 *regs);

#define PERF_REGS_MASK	((1ULL << PERF_REG_ARM_MAX) - 1)
#define PERF_REGS_MAX	PERF_REG_ARM_MAX
#define PERF_SAMPLE_REGS_ABI	PERF_SAMPLE_REGS_ABI_32

#define PERF_REG_IP	PERF_REG_ARM_PC
#define PERF_REG_SP	PERF_REG_ARM_SP

static inline const char *__perf_reg_name(int id)
{
	switch (id) {
	case PERF_REG_ARM_R0:
		return "r0";
	case PERF_REG_ARM_R1:
		return "r1";
	case PERF_REG_ARM_R2:
		return "r2";
	case PERF_REG_ARM_R3:
		return "r3";
	case PERF_REG_ARM_R4:
		return "r4";
	case PERF_REG_ARM_R5:
		return "r5";
	case PERF_REG_ARM_R6:
		return "r6";
	case PERF_REG_ARM_R7:
		return "r7";
	case PERF_REG_ARM_R8:
		return "r8";
	case PERF_REG_ARM_R9:
		return "r9";
	case PERF_REG_ARM_R10:
		return "r10";
	case PERF_REG_ARM_FP:
		return "fp";
	case PERF_REG_ARM_IP:
		return "ip";
	case PERF_REG_ARM_SP:
		return "sp";
	case PERF_REG_ARM_LR:
		return "lr";
	case PERF_REG_ARM_PC:
		return "pc";
	default:
		return NULL;
	}

	return NULL;
}

#endif /* ARCH_PERF_REGS_H */
