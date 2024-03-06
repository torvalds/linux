/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ARCH_PERF_REGS_H
#define ARCH_PERF_REGS_H

#include <stdlib.h>
#include <linux/types.h>
#include <asm/perf_regs.h>

void perf_regs_load(u64 *regs);

#define PERF_REGS_MAX PERF_REG_X86_XMM_MAX
#ifndef HAVE_ARCH_X86_64_SUPPORT
#define PERF_REGS_MASK ((1ULL << PERF_REG_X86_32_MAX) - 1)
#define PERF_SAMPLE_REGS_ABI PERF_SAMPLE_REGS_ABI_32
#else
#define REG_NOSUPPORT ((1ULL << PERF_REG_X86_DS) | \
		       (1ULL << PERF_REG_X86_ES) | \
		       (1ULL << PERF_REG_X86_FS) | \
		       (1ULL << PERF_REG_X86_GS))
#define PERF_REGS_MASK (((1ULL << PERF_REG_X86_64_MAX) - 1) & ~REG_NOSUPPORT)
#define PERF_SAMPLE_REGS_ABI PERF_SAMPLE_REGS_ABI_64
#endif

#endif /* ARCH_PERF_REGS_H */
