/*
 * Copyright 2016 Chandan Kumar, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <errno.h>
#include <libunwind.h>
#include <asm/perf_regs.h>
#include "../../util/unwind.h"
#include "../../util/debug.h"

int libunwind__arch_reg_id(int regnum)
{
	switch (regnum) {
	case UNW_PPC64_R0:
		return PERF_REG_POWERPC_R0;
	case UNW_PPC64_R1:
		return PERF_REG_POWERPC_R1;
	case UNW_PPC64_R2:
		return PERF_REG_POWERPC_R2;
	case UNW_PPC64_R3:
		return PERF_REG_POWERPC_R3;
	case UNW_PPC64_R4:
		return PERF_REG_POWERPC_R4;
	case UNW_PPC64_R5:
		return PERF_REG_POWERPC_R5;
	case UNW_PPC64_R6:
		return PERF_REG_POWERPC_R6;
	case UNW_PPC64_R7:
		return PERF_REG_POWERPC_R7;
	case UNW_PPC64_R8:
		return PERF_REG_POWERPC_R8;
	case UNW_PPC64_R9:
		return PERF_REG_POWERPC_R9;
	case UNW_PPC64_R10:
		return PERF_REG_POWERPC_R10;
	case UNW_PPC64_R11:
		return PERF_REG_POWERPC_R11;
	case UNW_PPC64_R12:
		return PERF_REG_POWERPC_R12;
	case UNW_PPC64_R13:
		return PERF_REG_POWERPC_R13;
	case UNW_PPC64_R14:
		return PERF_REG_POWERPC_R14;
	case UNW_PPC64_R15:
		return PERF_REG_POWERPC_R15;
	case UNW_PPC64_R16:
		return PERF_REG_POWERPC_R16;
	case UNW_PPC64_R17:
		return PERF_REG_POWERPC_R17;
	case UNW_PPC64_R18:
		return PERF_REG_POWERPC_R18;
	case UNW_PPC64_R19:
		return PERF_REG_POWERPC_R19;
	case UNW_PPC64_R20:
		return PERF_REG_POWERPC_R20;
	case UNW_PPC64_R21:
		return PERF_REG_POWERPC_R21;
	case UNW_PPC64_R22:
		return PERF_REG_POWERPC_R22;
	case UNW_PPC64_R23:
		return PERF_REG_POWERPC_R23;
	case UNW_PPC64_R24:
		return PERF_REG_POWERPC_R24;
	case UNW_PPC64_R25:
		return PERF_REG_POWERPC_R25;
	case UNW_PPC64_R26:
		return PERF_REG_POWERPC_R26;
	case UNW_PPC64_R27:
		return PERF_REG_POWERPC_R27;
	case UNW_PPC64_R28:
		return PERF_REG_POWERPC_R28;
	case UNW_PPC64_R29:
		return PERF_REG_POWERPC_R29;
	case UNW_PPC64_R30:
		return PERF_REG_POWERPC_R30;
	case UNW_PPC64_R31:
		return PERF_REG_POWERPC_R31;
	case UNW_PPC64_LR:
		return PERF_REG_POWERPC_LINK;
	case UNW_PPC64_CTR:
		return PERF_REG_POWERPC_CTR;
	case UNW_PPC64_XER:
		return PERF_REG_POWERPC_XER;
	case UNW_PPC64_NIP:
		return PERF_REG_POWERPC_NIP;
	default:
		pr_err("unwind: invalid reg id %d\n", regnum);
		return -EINVAL;
	}
	return -EINVAL;
}
