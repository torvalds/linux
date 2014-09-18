
#include <errno.h>
#include <libunwind.h>
#include "perf_regs.h"
#include "../../util/unwind.h"

int libunwind__arch_reg_id(int regnum)
{
	switch (regnum) {
	case UNW_AARCH64_X0:
		return PERF_REG_ARM64_X0;
	case UNW_AARCH64_X1:
		return PERF_REG_ARM64_X1;
	case UNW_AARCH64_X2:
		return PERF_REG_ARM64_X2;
	case UNW_AARCH64_X3:
		return PERF_REG_ARM64_X3;
	case UNW_AARCH64_X4:
		return PERF_REG_ARM64_X4;
	case UNW_AARCH64_X5:
		return PERF_REG_ARM64_X5;
	case UNW_AARCH64_X6:
		return PERF_REG_ARM64_X6;
	case UNW_AARCH64_X7:
		return PERF_REG_ARM64_X7;
	case UNW_AARCH64_X8:
		return PERF_REG_ARM64_X8;
	case UNW_AARCH64_X9:
		return PERF_REG_ARM64_X9;
	case UNW_AARCH64_X10:
		return PERF_REG_ARM64_X10;
	case UNW_AARCH64_X11:
		return PERF_REG_ARM64_X11;
	case UNW_AARCH64_X12:
		return PERF_REG_ARM64_X12;
	case UNW_AARCH64_X13:
		return PERF_REG_ARM64_X13;
	case UNW_AARCH64_X14:
		return PERF_REG_ARM64_X14;
	case UNW_AARCH64_X15:
		return PERF_REG_ARM64_X15;
	case UNW_AARCH64_X16:
		return PERF_REG_ARM64_X16;
	case UNW_AARCH64_X17:
		return PERF_REG_ARM64_X17;
	case UNW_AARCH64_X18:
		return PERF_REG_ARM64_X18;
	case UNW_AARCH64_X19:
		return PERF_REG_ARM64_X19;
	case UNW_AARCH64_X20:
		return PERF_REG_ARM64_X20;
	case UNW_AARCH64_X21:
		return PERF_REG_ARM64_X21;
	case UNW_AARCH64_X22:
		return PERF_REG_ARM64_X22;
	case UNW_AARCH64_X23:
		return PERF_REG_ARM64_X23;
	case UNW_AARCH64_X24:
		return PERF_REG_ARM64_X24;
	case UNW_AARCH64_X25:
		return PERF_REG_ARM64_X25;
	case UNW_AARCH64_X26:
		return PERF_REG_ARM64_X26;
	case UNW_AARCH64_X27:
		return PERF_REG_ARM64_X27;
	case UNW_AARCH64_X28:
		return PERF_REG_ARM64_X28;
	case UNW_AARCH64_X29:
		return PERF_REG_ARM64_X29;
	case UNW_AARCH64_X30:
		return PERF_REG_ARM64_LR;
	case UNW_AARCH64_SP:
		return PERF_REG_ARM64_SP;
	case UNW_AARCH64_PC:
		return PERF_REG_ARM64_PC;
	default:
		pr_err("unwind: invalid reg id %d\n", regnum);
		return -EINVAL;
	}

	return -EINVAL;
}
