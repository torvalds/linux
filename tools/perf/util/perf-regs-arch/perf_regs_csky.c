// SPDX-License-Identifier: GPL-2.0
#include <elf.h>
#ifndef EF_CSKY_ABIMASK
#define EF_CSKY_ABIMASK	0XF0000000
#endif
#ifndef EF_CSKY_ABIV2
#define EF_CSKY_ABIV2	0X20000000
#endif
#include "../perf_regs.h"
#undef __CSKYABIV2__
#define __CSKYABIV2__ 1  // Always want the V2 register definitions.
#include "../../arch/csky/include/perf_regs.h"

uint64_t __perf_reg_mask_csky(bool intr __maybe_unused)
{
	return PERF_REGS_MASK;
}

const char *__perf_reg_name_csky(int id, uint32_t e_flags)
{
	if (id >= PERF_REG_CSKY_EXREGS0 && (e_flags & EF_CSKY_ABIMASK) == EF_CSKY_ABIV2)
		return NULL;

	switch (id) {
	case PERF_REG_CSKY_A0:
		return "a0";
	case PERF_REG_CSKY_A1:
		return "a1";
	case PERF_REG_CSKY_A2:
		return "a2";
	case PERF_REG_CSKY_A3:
		return "a3";
	case PERF_REG_CSKY_REGS0:
		return "regs0";
	case PERF_REG_CSKY_REGS1:
		return "regs1";
	case PERF_REG_CSKY_REGS2:
		return "regs2";
	case PERF_REG_CSKY_REGS3:
		return "regs3";
	case PERF_REG_CSKY_REGS4:
		return "regs4";
	case PERF_REG_CSKY_REGS5:
		return "regs5";
	case PERF_REG_CSKY_REGS6:
		return "regs6";
	case PERF_REG_CSKY_REGS7:
		return "regs7";
	case PERF_REG_CSKY_REGS8:
		return "regs8";
	case PERF_REG_CSKY_REGS9:
		return "regs9";
	case PERF_REG_CSKY_SP:
		return "sp";
	case PERF_REG_CSKY_LR:
		return "lr";
	case PERF_REG_CSKY_PC:
		return "pc";
	case PERF_REG_CSKY_EXREGS0:
		return "exregs0";
	case PERF_REG_CSKY_EXREGS1:
		return "exregs1";
	case PERF_REG_CSKY_EXREGS2:
		return "exregs2";
	case PERF_REG_CSKY_EXREGS3:
		return "exregs3";
	case PERF_REG_CSKY_EXREGS4:
		return "exregs4";
	case PERF_REG_CSKY_EXREGS5:
		return "exregs5";
	case PERF_REG_CSKY_EXREGS6:
		return "exregs6";
	case PERF_REG_CSKY_EXREGS7:
		return "exregs7";
	case PERF_REG_CSKY_EXREGS8:
		return "exregs8";
	case PERF_REG_CSKY_EXREGS9:
		return "exregs9";
	case PERF_REG_CSKY_EXREGS10:
		return "exregs10";
	case PERF_REG_CSKY_EXREGS11:
		return "exregs11";
	case PERF_REG_CSKY_EXREGS12:
		return "exregs12";
	case PERF_REG_CSKY_EXREGS13:
		return "exregs13";
	case PERF_REG_CSKY_EXREGS14:
		return "exregs14";
	case PERF_REG_CSKY_TLS:
		return "tls";
	case PERF_REG_CSKY_HI:
		return "hi";
	case PERF_REG_CSKY_LO:
		return "lo";
	default:
		return NULL;
	}
}

uint64_t __perf_reg_ip_csky(void)
{
	return PERF_REG_CSKY_PC;
}

uint64_t __perf_reg_sp_csky(void)
{
	return PERF_REG_CSKY_SP;
}
