// SPDX-License-Identifier: GPL-2.0

#ifdef HAVE_PERF_REGS_SUPPORT

#include "../perf_regs.h"
#include "../../../arch/mips/include/uapi/asm/perf_regs.h"

const char *__perf_reg_name_mips(int id)
{
	switch (id) {
	case PERF_REG_MIPS_PC:
		return "PC";
	case PERF_REG_MIPS_R1:
		return "$1";
	case PERF_REG_MIPS_R2:
		return "$2";
	case PERF_REG_MIPS_R3:
		return "$3";
	case PERF_REG_MIPS_R4:
		return "$4";
	case PERF_REG_MIPS_R5:
		return "$5";
	case PERF_REG_MIPS_R6:
		return "$6";
	case PERF_REG_MIPS_R7:
		return "$7";
	case PERF_REG_MIPS_R8:
		return "$8";
	case PERF_REG_MIPS_R9:
		return "$9";
	case PERF_REG_MIPS_R10:
		return "$10";
	case PERF_REG_MIPS_R11:
		return "$11";
	case PERF_REG_MIPS_R12:
		return "$12";
	case PERF_REG_MIPS_R13:
		return "$13";
	case PERF_REG_MIPS_R14:
		return "$14";
	case PERF_REG_MIPS_R15:
		return "$15";
	case PERF_REG_MIPS_R16:
		return "$16";
	case PERF_REG_MIPS_R17:
		return "$17";
	case PERF_REG_MIPS_R18:
		return "$18";
	case PERF_REG_MIPS_R19:
		return "$19";
	case PERF_REG_MIPS_R20:
		return "$20";
	case PERF_REG_MIPS_R21:
		return "$21";
	case PERF_REG_MIPS_R22:
		return "$22";
	case PERF_REG_MIPS_R23:
		return "$23";
	case PERF_REG_MIPS_R24:
		return "$24";
	case PERF_REG_MIPS_R25:
		return "$25";
	case PERF_REG_MIPS_R28:
		return "$28";
	case PERF_REG_MIPS_R29:
		return "$29";
	case PERF_REG_MIPS_R30:
		return "$30";
	case PERF_REG_MIPS_R31:
		return "$31";
	default:
		break;
	}
	return NULL;
}

uint64_t __perf_reg_ip_mips(void)
{
	return PERF_REG_MIPS_PC;
}

uint64_t __perf_reg_sp_mips(void)
{
	return PERF_REG_MIPS_R29;
}

#endif
