// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <dwarf-regs.h>
#include "../../../arch/s390/include/uapi/asm/perf_regs.h"

int __get_dwarf_regnum_for_perf_regnum_s390(int perf_regnum)
{
	static const int dwarf_s390_regnums[] = {
		[PERF_REG_S390_R0] = 0,
		[PERF_REG_S390_R1] = 1,
		[PERF_REG_S390_R2] = 2,
		[PERF_REG_S390_R3] = 3,
		[PERF_REG_S390_R4] = 4,
		[PERF_REG_S390_R5] = 5,
		[PERF_REG_S390_R6] = 6,
		[PERF_REG_S390_R7] = 7,
		[PERF_REG_S390_R8] = 8,
		[PERF_REG_S390_R9] = 9,
		[PERF_REG_S390_R10] = 10,
		[PERF_REG_S390_R11] = 11,
		[PERF_REG_S390_R12] = 12,
		[PERF_REG_S390_R13] = 13,
		[PERF_REG_S390_R14] = 14,
		[PERF_REG_S390_R15] = 15,
		[PERF_REG_S390_FP0] = 16,
		[PERF_REG_S390_FP1] = 20,
		[PERF_REG_S390_FP2] = 17,
		[PERF_REG_S390_FP3] = 21,
		[PERF_REG_S390_FP4] = 18,
		[PERF_REG_S390_FP5] = 22,
		[PERF_REG_S390_FP6] = 19,
		[PERF_REG_S390_FP7] = 23,
		[PERF_REG_S390_FP8] = 24,
		[PERF_REG_S390_FP9] = 28,
		[PERF_REG_S390_FP10] = 25,
		[PERF_REG_S390_FP11] = 29,
		[PERF_REG_S390_FP12] = 26,
		[PERF_REG_S390_FP13] = 30,
		[PERF_REG_S390_FP14] = 27,
		[PERF_REG_S390_FP15] = 31,
		[PERF_REG_S390_MASK] = 64,
		[PERF_REG_S390_PC] = 65,
	};

	if (perf_regnum == 0)
		return 0;

	if (perf_regnum <  0 || perf_regnum > (int)ARRAY_SIZE(dwarf_s390_regnums) ||
	    dwarf_s390_regnums[perf_regnum] == 0)
		return -ENOENT;

	return dwarf_s390_regnums[perf_regnum];
}
