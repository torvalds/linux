/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * LoongArch PMU specific interface
 */
#ifndef SELFTEST_KVM_PMU_H
#define SELFTEST_KVM_PMU_H

#include "processor.h"

#define LOONGARCH_CPUCFG6			0x6
#define  CPUCFG6_PMP				BIT(0)
#define  CPUCFG6_PAMVER				GENMASK(3, 1)
#define  CPUCFG6_PMNUM				GENMASK(7, 4)
#define  CPUCFG6_PMNUM_SHIFT			4
#define  CPUCFG6_PMBITS				GENMASK(13, 8)
#define  CPUCFG6_PMBITS_SHIFT			8
#define  CPUCFG6_UPM				BIT(14)

/* Performance Counter registers */
#define LOONGARCH_CSR_PERFCTRL0			0x200   /* perf event 0 config */
#define LOONGARCH_CSR_PERFCNTR0			0x201   /* perf event 0 count value */
#define LOONGARCH_CSR_PERFCTRL1			0x202   /* perf event 1 config */
#define LOONGARCH_CSR_PERFCNTR1			0x203   /* perf event 1 count value */
#define LOONGARCH_CSR_PERFCTRL2			0x204   /* perf event 2 config */
#define LOONGARCH_CSR_PERFCNTR2			0x205   /* perf event 2 count value */
#define LOONGARCH_CSR_PERFCTRL3			0x206   /* perf event 3 config */
#define LOONGARCH_CSR_PERFCNTR3			0x207   /* perf event 3 count value */
#define  CSR_PERFCTRL_PLV0			BIT(16)
#define  CSR_PERFCTRL_PLV1			BIT(17)
#define  CSR_PERFCTRL_PLV2			BIT(18)
#define  CSR_PERFCTRL_PLV3			BIT(19)
#define  CSR_PERFCTRL_PMIE			BIT(20)
#define PMU_ENVENT_ENABLED	(CSR_PERFCTRL_PLV0 | CSR_PERFCTRL_PLV1 | CSR_PERFCTRL_PLV2 | CSR_PERFCTRL_PLV3)

/* Hardware event codes (from LoongArch perf_event.c */
#define LOONGARCH_PMU_EVENT_CYCLES		0x00  /* CPU cycles */
#define LOONGARCH_PMU_EVENT_INSTR_RETIRED	0x01  /* Instructions retired */
#define PERF_COUNT_HW_BRANCH_INSTRUCTIONS	0x02  /* Branch instructions */
#define PERF_COUNT_HW_BRANCH_MISSES		0x03  /* Branch misses */

#define NUM_LOOPS                               1000
#define EXPECTED_INSTR_MIN                      (NUM_LOOPS + 10)  /* Loop + overhead */
#define EXPECTED_CYCLES_MIN                     NUM_LOOPS       /* At least 1 cycle per iteration */
#define UPPER_BOUND				(10 * NUM_LOOPS)

#define PMU_OVERFLOW				(1ULL << 63)

static inline void pmu_irq_enable(void)
{
	unsigned long val;

	val = csr_read(LOONGARCH_CSR_ECFG);
	val |= ECFGF_PMU;
	csr_write(val, LOONGARCH_CSR_ECFG);
}

static inline void pmu_irq_disable(void)
{
	unsigned long val;

	val = csr_read(LOONGARCH_CSR_ECFG);
	val &= ~ECFGF_PMU;
	csr_write(val, LOONGARCH_CSR_ECFG);
}

#endif
