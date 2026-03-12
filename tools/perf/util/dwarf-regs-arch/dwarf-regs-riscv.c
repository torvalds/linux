// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <dwarf-regs.h>
#include "../../../arch/riscv/include/uapi/asm/perf_regs.h"

int __get_dwarf_regnum_for_perf_regnum_riscv(int perf_regnum)
{
	if (perf_regnum < 0 || perf_regnum >= PERF_REG_RISCV_MAX)
		return -ENOENT;

	return perf_regnum;
}
