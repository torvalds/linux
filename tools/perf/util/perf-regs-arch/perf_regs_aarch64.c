// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <regex.h>
#include <string.h>
#include <sys/auxv.h>
#include <linux/kernel.h>
#include <linux/zalloc.h>

#include "../debug.h"
#include "../event.h"
#include "../perf_regs.h"
#include "../../perf-sys.h"
#include "../../arch/arm64/include/perf_regs.h"

#define SMPL_REG_MASK(b) (1ULL << (b))

#ifndef HWCAP_SVE
#define HWCAP_SVE	(1 << 22)
#endif

uint64_t __perf_reg_mask_arm64(bool intr)
{
	struct perf_event_attr attr = {
		.type                   = PERF_TYPE_HARDWARE,
		.config                 = PERF_COUNT_HW_CPU_CYCLES,
		.sample_type            = PERF_SAMPLE_REGS_USER,
		.disabled               = 1,
		.exclude_kernel         = 1,
		.sample_period		= 1,
		.sample_regs_user	= PERF_REGS_MASK
	};
	int fd;

	if (intr)
		return PERF_REGS_MASK;

	if (getauxval(AT_HWCAP) & HWCAP_SVE)
		attr.sample_regs_user |= SMPL_REG_MASK(PERF_REG_ARM64_VG);

	/*
	 * Check if the pmu supports perf extended regs, before
	 * returning the register mask to sample. Open the event
	 * on the perf process to check this.
	 */
	if (attr.sample_regs_user != PERF_REGS_MASK) {
		event_attr_init(&attr);
		fd = sys_perf_event_open(&attr, /*pid=*/0, /*cpu=*/-1,
					 /*group_fd=*/-1, /*flags=*/0);
		if (fd != -1) {
			close(fd);
			return attr.sample_regs_user;
		}
	}
	return PERF_REGS_MASK;
}

const char *__perf_reg_name_arm64(int id)
{
	switch (id) {
	case PERF_REG_ARM64_X0:
		return "x0";
	case PERF_REG_ARM64_X1:
		return "x1";
	case PERF_REG_ARM64_X2:
		return "x2";
	case PERF_REG_ARM64_X3:
		return "x3";
	case PERF_REG_ARM64_X4:
		return "x4";
	case PERF_REG_ARM64_X5:
		return "x5";
	case PERF_REG_ARM64_X6:
		return "x6";
	case PERF_REG_ARM64_X7:
		return "x7";
	case PERF_REG_ARM64_X8:
		return "x8";
	case PERF_REG_ARM64_X9:
		return "x9";
	case PERF_REG_ARM64_X10:
		return "x10";
	case PERF_REG_ARM64_X11:
		return "x11";
	case PERF_REG_ARM64_X12:
		return "x12";
	case PERF_REG_ARM64_X13:
		return "x13";
	case PERF_REG_ARM64_X14:
		return "x14";
	case PERF_REG_ARM64_X15:
		return "x15";
	case PERF_REG_ARM64_X16:
		return "x16";
	case PERF_REG_ARM64_X17:
		return "x17";
	case PERF_REG_ARM64_X18:
		return "x18";
	case PERF_REG_ARM64_X19:
		return "x19";
	case PERF_REG_ARM64_X20:
		return "x20";
	case PERF_REG_ARM64_X21:
		return "x21";
	case PERF_REG_ARM64_X22:
		return "x22";
	case PERF_REG_ARM64_X23:
		return "x23";
	case PERF_REG_ARM64_X24:
		return "x24";
	case PERF_REG_ARM64_X25:
		return "x25";
	case PERF_REG_ARM64_X26:
		return "x26";
	case PERF_REG_ARM64_X27:
		return "x27";
	case PERF_REG_ARM64_X28:
		return "x28";
	case PERF_REG_ARM64_X29:
		return "x29";
	case PERF_REG_ARM64_SP:
		return "sp";
	case PERF_REG_ARM64_LR:
		return "lr";
	case PERF_REG_ARM64_PC:
		return "pc";
	case PERF_REG_ARM64_VG:
		return "vg";
	default:
		return NULL;
	}

	return NULL;
}

uint64_t __perf_reg_ip_arm64(void)
{
	return PERF_REG_ARM64_PC;
}

uint64_t __perf_reg_sp_arm64(void)
{
	return PERF_REG_ARM64_SP;
}
