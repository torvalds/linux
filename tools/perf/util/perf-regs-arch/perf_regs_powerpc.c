// SPDX-License-Identifier: GPL-2.0

#ifdef HAVE_PERF_REGS_SUPPORT

#include "../perf_regs.h"
#include "../../../arch/powerpc/include/uapi/asm/perf_regs.h"

const char *__perf_reg_name_powerpc(int id)
{
	switch (id) {
	case PERF_REG_POWERPC_R0:
		return "r0";
	case PERF_REG_POWERPC_R1:
		return "r1";
	case PERF_REG_POWERPC_R2:
		return "r2";
	case PERF_REG_POWERPC_R3:
		return "r3";
	case PERF_REG_POWERPC_R4:
		return "r4";
	case PERF_REG_POWERPC_R5:
		return "r5";
	case PERF_REG_POWERPC_R6:
		return "r6";
	case PERF_REG_POWERPC_R7:
		return "r7";
	case PERF_REG_POWERPC_R8:
		return "r8";
	case PERF_REG_POWERPC_R9:
		return "r9";
	case PERF_REG_POWERPC_R10:
		return "r10";
	case PERF_REG_POWERPC_R11:
		return "r11";
	case PERF_REG_POWERPC_R12:
		return "r12";
	case PERF_REG_POWERPC_R13:
		return "r13";
	case PERF_REG_POWERPC_R14:
		return "r14";
	case PERF_REG_POWERPC_R15:
		return "r15";
	case PERF_REG_POWERPC_R16:
		return "r16";
	case PERF_REG_POWERPC_R17:
		return "r17";
	case PERF_REG_POWERPC_R18:
		return "r18";
	case PERF_REG_POWERPC_R19:
		return "r19";
	case PERF_REG_POWERPC_R20:
		return "r20";
	case PERF_REG_POWERPC_R21:
		return "r21";
	case PERF_REG_POWERPC_R22:
		return "r22";
	case PERF_REG_POWERPC_R23:
		return "r23";
	case PERF_REG_POWERPC_R24:
		return "r24";
	case PERF_REG_POWERPC_R25:
		return "r25";
	case PERF_REG_POWERPC_R26:
		return "r26";
	case PERF_REG_POWERPC_R27:
		return "r27";
	case PERF_REG_POWERPC_R28:
		return "r28";
	case PERF_REG_POWERPC_R29:
		return "r29";
	case PERF_REG_POWERPC_R30:
		return "r30";
	case PERF_REG_POWERPC_R31:
		return "r31";
	case PERF_REG_POWERPC_NIP:
		return "nip";
	case PERF_REG_POWERPC_MSR:
		return "msr";
	case PERF_REG_POWERPC_ORIG_R3:
		return "orig_r3";
	case PERF_REG_POWERPC_CTR:
		return "ctr";
	case PERF_REG_POWERPC_LINK:
		return "link";
	case PERF_REG_POWERPC_XER:
		return "xer";
	case PERF_REG_POWERPC_CCR:
		return "ccr";
	case PERF_REG_POWERPC_SOFTE:
		return "softe";
	case PERF_REG_POWERPC_TRAP:
		return "trap";
	case PERF_REG_POWERPC_DAR:
		return "dar";
	case PERF_REG_POWERPC_DSISR:
		return "dsisr";
	case PERF_REG_POWERPC_SIER:
		return "sier";
	case PERF_REG_POWERPC_MMCRA:
		return "mmcra";
	case PERF_REG_POWERPC_MMCR0:
		return "mmcr0";
	case PERF_REG_POWERPC_MMCR1:
		return "mmcr1";
	case PERF_REG_POWERPC_MMCR2:
		return "mmcr2";
	case PERF_REG_POWERPC_MMCR3:
		return "mmcr3";
	case PERF_REG_POWERPC_SIER2:
		return "sier2";
	case PERF_REG_POWERPC_SIER3:
		return "sier3";
	case PERF_REG_POWERPC_PMC1:
		return "pmc1";
	case PERF_REG_POWERPC_PMC2:
		return "pmc2";
	case PERF_REG_POWERPC_PMC3:
		return "pmc3";
	case PERF_REG_POWERPC_PMC4:
		return "pmc4";
	case PERF_REG_POWERPC_PMC5:
		return "pmc5";
	case PERF_REG_POWERPC_PMC6:
		return "pmc6";
	case PERF_REG_POWERPC_SDAR:
		return "sdar";
	case PERF_REG_POWERPC_SIAR:
		return "siar";
	default:
		break;
	}
	return NULL;
}

uint64_t __perf_reg_ip_powerpc(void)
{
	return PERF_REG_POWERPC_NIP;
}

uint64_t __perf_reg_sp_powerpc(void)
{
	return PERF_REG_POWERPC_R1;
}

#endif
