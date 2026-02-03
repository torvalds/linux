// SPDX-License-Identifier: GPL-2.0

#include <errno.h>
#include <string.h>
#include <regex.h>
#include <linux/kernel.h>
#include <linux/zalloc.h>

#include "../debug.h"
#include "../event.h"
#include "../pmu.h"
#include "../pmus.h"
#include "../perf_regs.h"
#include "../../perf-sys.h"
#include "../../arch/x86/include/perf_regs.h"

uint64_t __perf_reg_mask_x86(bool intr)
{
	struct perf_event_attr attr = {
		.type			= PERF_TYPE_HARDWARE,
		.config			= PERF_COUNT_HW_CPU_CYCLES,
		.sample_type		= PERF_SAMPLE_REGS_INTR,
		.sample_regs_intr	= PERF_REG_EXTENDED_MASK,
		.precise_ip		= 1,
		.disabled		= 1,
		.exclude_kernel		= 1,
	};
	int fd;

	if (!intr)
		return PERF_REGS_MASK;

	/*
	 * In an unnamed union, init it here to build on older gcc versions
	 */
	attr.sample_period = 1;

	if (perf_pmus__num_core_pmus() > 1) {
		struct perf_pmu *pmu = NULL;
		__u64 type = PERF_TYPE_RAW;

		/*
		 * The same register set is supported among different hybrid PMUs.
		 * Only check the first available one.
		 */
		while ((pmu = perf_pmus__scan_core(pmu)) != NULL) {
			type = pmu->type;
			break;
		}
		attr.config |= type << PERF_PMU_TYPE_SHIFT;
	}

	event_attr_init(&attr);
	fd = sys_perf_event_open(&attr, /*pid=*/0, /*cpu=*/-1,
				 /*group_fd=*/-1, /*flags=*/0);
	if (fd != -1) {
		close(fd);
		return (PERF_REG_EXTENDED_MASK | PERF_REGS_MASK);
	}

	return PERF_REGS_MASK;
}

const char *__perf_reg_name_x86(int id)
{
	switch (id) {
	case PERF_REG_X86_AX:
		return "AX";
	case PERF_REG_X86_BX:
		return "BX";
	case PERF_REG_X86_CX:
		return "CX";
	case PERF_REG_X86_DX:
		return "DX";
	case PERF_REG_X86_SI:
		return "SI";
	case PERF_REG_X86_DI:
		return "DI";
	case PERF_REG_X86_BP:
		return "BP";
	case PERF_REG_X86_SP:
		return "SP";
	case PERF_REG_X86_IP:
		return "IP";
	case PERF_REG_X86_FLAGS:
		return "FLAGS";
	case PERF_REG_X86_CS:
		return "CS";
	case PERF_REG_X86_SS:
		return "SS";
	case PERF_REG_X86_DS:
		return "DS";
	case PERF_REG_X86_ES:
		return "ES";
	case PERF_REG_X86_FS:
		return "FS";
	case PERF_REG_X86_GS:
		return "GS";
	case PERF_REG_X86_R8:
		return "R8";
	case PERF_REG_X86_R9:
		return "R9";
	case PERF_REG_X86_R10:
		return "R10";
	case PERF_REG_X86_R11:
		return "R11";
	case PERF_REG_X86_R12:
		return "R12";
	case PERF_REG_X86_R13:
		return "R13";
	case PERF_REG_X86_R14:
		return "R14";
	case PERF_REG_X86_R15:
		return "R15";

#define XMM(x) \
	case PERF_REG_X86_XMM ## x:	\
	case PERF_REG_X86_XMM ## x + 1:	\
		return "XMM" #x;
	XMM(0)
	XMM(1)
	XMM(2)
	XMM(3)
	XMM(4)
	XMM(5)
	XMM(6)
	XMM(7)
	XMM(8)
	XMM(9)
	XMM(10)
	XMM(11)
	XMM(12)
	XMM(13)
	XMM(14)
	XMM(15)
#undef XMM
	default:
		return NULL;
	}

	return NULL;
}

uint64_t __perf_reg_ip_x86(void)
{
	return PERF_REG_X86_IP;
}

uint64_t __perf_reg_sp_x86(void)
{
	return PERF_REG_X86_SP;
}
