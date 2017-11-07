// SPDX-License-Identifier: GPL-2.0

#include <errno.h>
#include <libunwind.h>
#include "perf_regs.h"
#include "../../util/unwind.h"
#include "../../util/debug.h"

int libunwind__arch_reg_id(int regnum)
{
	switch (regnum) {
	case UNW_ARM_R0:
		return PERF_REG_ARM_R0;
	case UNW_ARM_R1:
		return PERF_REG_ARM_R1;
	case UNW_ARM_R2:
		return PERF_REG_ARM_R2;
	case UNW_ARM_R3:
		return PERF_REG_ARM_R3;
	case UNW_ARM_R4:
		return PERF_REG_ARM_R4;
	case UNW_ARM_R5:
		return PERF_REG_ARM_R5;
	case UNW_ARM_R6:
		return PERF_REG_ARM_R6;
	case UNW_ARM_R7:
		return PERF_REG_ARM_R7;
	case UNW_ARM_R8:
		return PERF_REG_ARM_R8;
	case UNW_ARM_R9:
		return PERF_REG_ARM_R9;
	case UNW_ARM_R10:
		return PERF_REG_ARM_R10;
	case UNW_ARM_R11:
		return PERF_REG_ARM_FP;
	case UNW_ARM_R12:
		return PERF_REG_ARM_IP;
	case UNW_ARM_R13:
		return PERF_REG_ARM_SP;
	case UNW_ARM_R14:
		return PERF_REG_ARM_LR;
	case UNW_ARM_R15:
		return PERF_REG_ARM_PC;
	default:
		pr_err("unwind: invalid reg id %d\n", regnum);
		return -EINVAL;
	}

	return -EINVAL;
}
