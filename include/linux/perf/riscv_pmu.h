/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 SiFive
 * Copyright (C) 2018 Andes Technology Corporation
 * Copyright (C) 2021 Western Digital Corporation or its affiliates.
 *
 */

#ifndef _ASM_RISCV_PERF_EVENT_H
#define _ASM_RISCV_PERF_EVENT_H

#include <linux/perf_event.h>
#include <linux/ptrace.h>
#include <linux/interrupt.h>

#ifdef CONFIG_RISCV_PMU

/*
 * The RISCV_MAX_COUNTERS parameter should be specified.
 */

#define RISCV_MAX_COUNTERS	64
#define RISCV_OP_UNSUPP		(-EOPNOTSUPP)
#define RISCV_PMU_PDEV_NAME	"riscv-pmu"
#define RISCV_PMU_LEGACY_PDEV_NAME	"riscv-pmu-legacy"

#define RISCV_PMU_STOP_FLAG_RESET 1

#define RISCV_PMU_CONFIG1_GUEST_EVENTS 0x1

struct cpu_hw_events {
	/* currently enabled events */
	int			n_events;
	/* Counter overflow interrupt */
	int		irq;
	/* currently enabled events */
	struct perf_event	*events[RISCV_MAX_COUNTERS];
	/* currently enabled hardware counters */
	DECLARE_BITMAP(used_hw_ctrs, RISCV_MAX_COUNTERS);
	/* currently enabled firmware counters */
	DECLARE_BITMAP(used_fw_ctrs, RISCV_MAX_COUNTERS);
};

struct riscv_pmu {
	struct pmu	pmu;
	char		*name;

	irqreturn_t	(*handle_irq)(int irq_num, void *dev);

	unsigned long	cmask;
	u64		(*ctr_read)(struct perf_event *event);
	int		(*ctr_get_idx)(struct perf_event *event);
	int		(*ctr_get_width)(int idx);
	void		(*ctr_clear_idx)(struct perf_event *event);
	void		(*ctr_start)(struct perf_event *event, u64 init_val);
	void		(*ctr_stop)(struct perf_event *event, unsigned long flag);
	int		(*event_map)(struct perf_event *event, u64 *config);

	struct cpu_hw_events	__percpu *hw_events;
	struct hlist_node	node;
	struct notifier_block   riscv_pm_nb;
};

#define to_riscv_pmu(p) (container_of(p, struct riscv_pmu, pmu))

void riscv_pmu_start(struct perf_event *event, int flags);
void riscv_pmu_stop(struct perf_event *event, int flags);
unsigned long riscv_pmu_ctr_read_csr(unsigned long csr);
int riscv_pmu_event_set_period(struct perf_event *event);
uint64_t riscv_pmu_ctr_get_width_mask(struct perf_event *event);
u64 riscv_pmu_event_update(struct perf_event *event);
#ifdef CONFIG_RISCV_PMU_LEGACY
void riscv_pmu_legacy_skip_init(void);
#else
static inline void riscv_pmu_legacy_skip_init(void) {};
#endif
struct riscv_pmu *riscv_pmu_alloc(void);
#ifdef CONFIG_RISCV_PMU_SBI
int riscv_pmu_get_hpm_info(u32 *hw_ctr_width, u32 *num_hw_ctr);
#endif

#endif /* CONFIG_RISCV_PMU */

#endif /* _ASM_RISCV_PERF_EVENT_H */
