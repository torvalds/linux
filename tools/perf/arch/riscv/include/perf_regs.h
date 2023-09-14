/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2019 Hangzhou C-SKY Microsystems co.,ltd. */

#ifndef ARCH_PERF_REGS_H
#define ARCH_PERF_REGS_H

#include <stdlib.h>
#include <linux/types.h>
#include <asm/perf_regs.h>

#define PERF_REGS_MASK	((1ULL << PERF_REG_RISCV_MAX) - 1)
#define PERF_REGS_MAX	PERF_REG_RISCV_MAX
#if __riscv_xlen == 64
#define PERF_SAMPLE_REGS_ABI    PERF_SAMPLE_REGS_ABI_64
#else
#define PERF_SAMPLE_REGS_ABI	PERF_SAMPLE_REGS_ABI_32
#endif

#endif /* ARCH_PERF_REGS_H */
