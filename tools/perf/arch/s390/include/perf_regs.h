#ifndef ARCH_PERF_REGS_H
#define ARCH_PERF_REGS_H

#include <stdlib.h>
#include <linux/types.h>
#include <asm/perf_regs.h>

void perf_regs_load(u64 *regs);

#define PERF_REGS_MASK ((1ULL << PERF_REG_S390_MAX) - 1)
#define PERF_REGS_MAX PERF_REG_S390_MAX
#define PERF_SAMPLE_REGS_ABI PERF_SAMPLE_REGS_ABI_64

#define PERF_REG_IP PERF_REG_S390_PC
#define PERF_REG_SP PERF_REG_S390_R15

static inline const char *__perf_reg_name(int id)
{
	switch (id) {
	case PERF_REG_S390_R0:
		return "R0";
	case PERF_REG_S390_R1:
		return "R1";
	case PERF_REG_S390_R2:
		return "R2";
	case PERF_REG_S390_R3:
		return "R3";
	case PERF_REG_S390_R4:
		return "R4";
	case PERF_REG_S390_R5:
		return "R5";
	case PERF_REG_S390_R6:
		return "R6";
	case PERF_REG_S390_R7:
		return "R7";
	case PERF_REG_S390_R8:
		return "R8";
	case PERF_REG_S390_R9:
		return "R9";
	case PERF_REG_S390_R10:
		return "R10";
	case PERF_REG_S390_R11:
		return "R11";
	case PERF_REG_S390_R12:
		return "R12";
	case PERF_REG_S390_R13:
		return "R13";
	case PERF_REG_S390_R14:
		return "R14";
	case PERF_REG_S390_R15:
		return "R15";
	case PERF_REG_S390_FP0:
		return "FP0";
	case PERF_REG_S390_FP1:
		return "FP1";
	case PERF_REG_S390_FP2:
		return "FP2";
	case PERF_REG_S390_FP3:
		return "FP3";
	case PERF_REG_S390_FP4:
		return "FP4";
	case PERF_REG_S390_FP5:
		return "FP5";
	case PERF_REG_S390_FP6:
		return "FP6";
	case PERF_REG_S390_FP7:
		return "FP7";
	case PERF_REG_S390_FP8:
		return "FP8";
	case PERF_REG_S390_FP9:
		return "FP9";
	case PERF_REG_S390_FP10:
		return "FP10";
	case PERF_REG_S390_FP11:
		return "FP11";
	case PERF_REG_S390_FP12:
		return "FP12";
	case PERF_REG_S390_FP13:
		return "FP13";
	case PERF_REG_S390_FP14:
		return "FP14";
	case PERF_REG_S390_FP15:
		return "FP15";
	case PERF_REG_S390_MASK:
		return "MASK";
	case PERF_REG_S390_PC:
		return "PC";
	default:
		return NULL;
	}

	return NULL;
}

#endif /* ARCH_PERF_REGS_H */
