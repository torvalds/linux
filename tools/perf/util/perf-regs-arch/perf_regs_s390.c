// SPDX-License-Identifier: GPL-2.0

#include "../perf_regs.h"
#include "../../../arch/s390/include/uapi/asm/perf_regs.h"

const char *__perf_reg_name_s390(int id)
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

uint64_t __perf_reg_ip_s390(void)
{
	return PERF_REG_S390_PC;
}

uint64_t __perf_reg_sp_s390(void)
{
	return PERF_REG_S390_R15;
}
