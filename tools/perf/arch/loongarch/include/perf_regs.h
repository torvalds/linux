/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ARCH_PERF_REGS_H
#define ARCH_PERF_REGS_H

#include <stdlib.h>
#include <linux/types.h>
#include <asm/perf_regs.h>

#define PERF_REGS_MAX PERF_REG_LOONGARCH_MAX
#define PERF_REG_IP PERF_REG_LOONGARCH_PC
#define PERF_REG_SP PERF_REG_LOONGARCH_R3

#define PERF_REGS_MASK ((1ULL << PERF_REG_LOONGARCH_MAX) - 1)

#endif /* ARCH_PERF_REGS_H */
