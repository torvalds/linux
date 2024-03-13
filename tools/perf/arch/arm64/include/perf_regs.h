/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ARCH_PERF_REGS_H
#define ARCH_PERF_REGS_H

#include <stdlib.h>
#include <linux/types.h>
#define perf_event_arm_regs perf_event_arm64_regs
#include <asm/perf_regs.h>
#undef perf_event_arm_regs

void perf_regs_load(u64 *regs);

#define PERF_REGS_MASK	((1ULL << PERF_REG_ARM64_MAX) - 1)
#define PERF_REGS_MAX	PERF_REG_ARM64_MAX
#define PERF_SAMPLE_REGS_ABI	PERF_SAMPLE_REGS_ABI_64

#define PERF_REG_IP	PERF_REG_ARM64_PC
#define PERF_REG_SP	PERF_REG_ARM64_SP

#endif /* ARCH_PERF_REGS_H */
