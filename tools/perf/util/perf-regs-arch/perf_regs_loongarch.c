// SPDX-License-Identifier: GPL-2.0

#ifdef HAVE_PERF_REGS_SUPPORT

#include "../perf_regs.h"
#include "../../../arch/loongarch/include/uapi/asm/perf_regs.h"

const char *__perf_reg_name_loongarch(int id)
{
	switch (id) {
	case PERF_REG_LOONGARCH_PC:
		return "PC";
	case PERF_REG_LOONGARCH_R1:
		return "%r1";
	case PERF_REG_LOONGARCH_R2:
		return "%r2";
	case PERF_REG_LOONGARCH_R3:
		return "%r3";
	case PERF_REG_LOONGARCH_R4:
		return "%r4";
	case PERF_REG_LOONGARCH_R5:
		return "%r5";
	case PERF_REG_LOONGARCH_R6:
		return "%r6";
	case PERF_REG_LOONGARCH_R7:
		return "%r7";
	case PERF_REG_LOONGARCH_R8:
		return "%r8";
	case PERF_REG_LOONGARCH_R9:
		return "%r9";
	case PERF_REG_LOONGARCH_R10:
		return "%r10";
	case PERF_REG_LOONGARCH_R11:
		return "%r11";
	case PERF_REG_LOONGARCH_R12:
		return "%r12";
	case PERF_REG_LOONGARCH_R13:
		return "%r13";
	case PERF_REG_LOONGARCH_R14:
		return "%r14";
	case PERF_REG_LOONGARCH_R15:
		return "%r15";
	case PERF_REG_LOONGARCH_R16:
		return "%r16";
	case PERF_REG_LOONGARCH_R17:
		return "%r17";
	case PERF_REG_LOONGARCH_R18:
		return "%r18";
	case PERF_REG_LOONGARCH_R19:
		return "%r19";
	case PERF_REG_LOONGARCH_R20:
		return "%r20";
	case PERF_REG_LOONGARCH_R21:
		return "%r21";
	case PERF_REG_LOONGARCH_R22:
		return "%r22";
	case PERF_REG_LOONGARCH_R23:
		return "%r23";
	case PERF_REG_LOONGARCH_R24:
		return "%r24";
	case PERF_REG_LOONGARCH_R25:
		return "%r25";
	case PERF_REG_LOONGARCH_R26:
		return "%r26";
	case PERF_REG_LOONGARCH_R27:
		return "%r27";
	case PERF_REG_LOONGARCH_R28:
		return "%r28";
	case PERF_REG_LOONGARCH_R29:
		return "%r29";
	case PERF_REG_LOONGARCH_R30:
		return "%r30";
	case PERF_REG_LOONGARCH_R31:
		return "%r31";
	default:
		break;
	}
	return NULL;
}

uint64_t __perf_reg_ip_loongarch(void)
{
	return PERF_REG_LOONGARCH_PC;
}

uint64_t __perf_reg_sp_loongarch(void)
{
	return PERF_REG_LOONGARCH_R3;
}

#endif
