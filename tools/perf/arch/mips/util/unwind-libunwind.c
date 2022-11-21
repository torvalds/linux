// SPDX-License-Identifier: GPL-2.0

#include <errno.h>
#include <libunwind.h>
#include "perf_regs.h"
#include "../../util/unwind.h"
#include "util/debug.h"

int libunwind__arch_reg_id(int regnum)
{
	switch (regnum) {
	case UNW_MIPS_R1 ... UNW_MIPS_R25:
		return regnum - UNW_MIPS_R1 + PERF_REG_MIPS_R1;
	case UNW_MIPS_R28 ... UNW_MIPS_R31:
		return regnum - UNW_MIPS_R28 + PERF_REG_MIPS_R28;
	case UNW_MIPS_PC:
		return PERF_REG_MIPS_PC;
	default:
		pr_err("unwind: invalid reg id %d\n", regnum);
		return -EINVAL;
	}
}
