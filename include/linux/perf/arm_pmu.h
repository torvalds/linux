/*
 *  linux/arch/arm/include/asm/pmu.h
 *
 *  Copyright (C) 2009 picoChip Designs Ltd, Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef __ARM_PMU_H__
#define __ARM_PMU_H__

#include <linux/interrupt.h>
#include <linux/perf_event.h>
#include <linux/sysfs.h>
#include <asm/cputype.h>

/*
 * struct arm_pmu_platdata - ARM PMU platform data
 *
 * @handle_irq: an optional handler which will be called from the
 *	interrupt and passed the address of the low level handler,
 *	and can be used to implement any platform specific handling
 *	before or after calling it.
 *
 * @irq_flags: if non-zero, these flags will be passed to request_irq
 *             when requesting interrupts for this PMU device.
 */
struct arm_pmu_platdata {
	irqreturn_t (*handle_irq)(int irq, void *dev,
				  irq_handler_t pmu_handler);
	unsigned long irq_flags;
};

#ifdef CONFIG_ARM_PMU

/*
 * The ARMv7 CPU PMU supports up to 32 event counters.
 */
#define ARMPMU_MAX_HWEVENTS		32

#define HW_OP_UNSUPPORTED		0xFFFF
#define C(_x)				PERF_COUNT_HW_CACHE_##_x
#define CACHE_OP_UNSUPPORTED		0xFFFF

#define PERF_MAP_ALL_UNSUPPORTED					\
	[0 ... PERF_COUNT_HW_MAX - 1] = HW_OP_UNSUPPORTED

#define PERF_CACHE_MAP_ALL_UNSUPPORTED					\
[0 ... C(MAX) - 1] = {							\
	[0 ... C(OP_MAX) - 1] = {					\
		[0 ... C(RESULT_MAX) - 1] = CACHE_OP_UNSUPPORTED,	\
	},								\
}

/* The events for a given PMU register set. */
struct pmu_hw_events {
	/*
	 * The events that are active on the PMU for the given index.
	 */
	struct perf_event	*events[ARMPMU_MAX_HWEVENTS];

	/*
	 * A 1 bit for an index indicates that the counter is being used for
	 * an event. A 0 means that the counter can be used.
	 */
	DECLARE_BITMAP(used_mask, ARMPMU_MAX_HWEVENTS);

	/*
	 * Hardware lock to serialize accesses to PMU registers. Needed for the
	 * read/modify/write sequences.
	 */
	raw_spinlock_t		pmu_lock;

	/*
	 * When using percpu IRQs, we need a percpu dev_id. Place it here as we
	 * already have to allocate this struct per cpu.
	 */
	struct arm_pmu		*percpu_pmu;

	int irq;
};

enum armpmu_attr_groups {
	ARMPMU_ATTR_GROUP_COMMON,
	ARMPMU_ATTR_GROUP_EVENTS,
	ARMPMU_ATTR_GROUP_FORMATS,
	ARMPMU_NR_ATTR_GROUPS
};

struct arm_pmu {
	struct pmu	pmu;
	cpumask_t	active_irqs;
	cpumask_t	supported_cpus;
	char		*name;
	irqreturn_t	(*handle_irq)(int irq_num, void *dev);
	void		(*enable)(struct perf_event *event);
	void		(*disable)(struct perf_event *event);
	int		(*get_event_idx)(struct pmu_hw_events *hw_events,
					 struct perf_event *event);
	void		(*clear_event_idx)(struct pmu_hw_events *hw_events,
					 struct perf_event *event);
	int		(*set_event_filter)(struct hw_perf_event *evt,
					    struct perf_event_attr *attr);
	u32		(*read_counter)(struct perf_event *event);
	void		(*write_counter)(struct perf_event *event, u32 val);
	void		(*start)(struct arm_pmu *);
	void		(*stop)(struct arm_pmu *);
	void		(*reset)(void *);
	int		(*map_event)(struct perf_event *event);
	int		num_events;
	u64		max_period;
	bool		secure_access; /* 32-bit ARM only */
#define ARMV8_PMUV3_MAX_COMMON_EVENTS 0x40
	DECLARE_BITMAP(pmceid_bitmap, ARMV8_PMUV3_MAX_COMMON_EVENTS);
	struct platform_device	*plat_device;
	struct pmu_hw_events	__percpu *hw_events;
	struct hlist_node	node;
	struct notifier_block	cpu_pm_nb;
	/* the attr_groups array must be NULL-terminated */
	const struct attribute_group *attr_groups[ARMPMU_NR_ATTR_GROUPS + 1];

	/* Only to be used by ACPI probing code */
	unsigned long acpi_cpuid;
};

#define to_arm_pmu(p) (container_of(p, struct arm_pmu, pmu))

u64 armpmu_event_update(struct perf_event *event);

int armpmu_event_set_period(struct perf_event *event);

int armpmu_map_event(struct perf_event *event,
		     const unsigned (*event_map)[PERF_COUNT_HW_MAX],
		     const unsigned (*cache_map)[PERF_COUNT_HW_CACHE_MAX]
						[PERF_COUNT_HW_CACHE_OP_MAX]
						[PERF_COUNT_HW_CACHE_RESULT_MAX],
		     u32 raw_event_mask);

typedef int (*armpmu_init_fn)(struct arm_pmu *);

struct pmu_probe_info {
	unsigned int cpuid;
	unsigned int mask;
	armpmu_init_fn init;
};

#define PMU_PROBE(_cpuid, _mask, _fn)	\
{					\
	.cpuid = (_cpuid),		\
	.mask = (_mask),		\
	.init = (_fn),			\
}

#define ARM_PMU_PROBE(_cpuid, _fn) \
	PMU_PROBE(_cpuid, ARM_CPU_PART_MASK, _fn)

#define ARM_PMU_XSCALE_MASK	((0xff << 24) | ARM_CPU_XSCALE_ARCH_MASK)

#define XSCALE_PMU_PROBE(_version, _fn) \
	PMU_PROBE(ARM_CPU_IMP_INTEL << 24 | _version, ARM_PMU_XSCALE_MASK, _fn)

int arm_pmu_device_probe(struct platform_device *pdev,
			 const struct of_device_id *of_table,
			 const struct pmu_probe_info *probe_table);

#ifdef CONFIG_ACPI
int arm_pmu_acpi_probe(armpmu_init_fn init_fn);
#else
static inline int arm_pmu_acpi_probe(armpmu_init_fn init_fn) { return 0; }
#endif

/* Internal functions only for core arm_pmu code */
struct arm_pmu *armpmu_alloc(void);
void armpmu_free(struct arm_pmu *pmu);
int armpmu_register(struct arm_pmu *pmu);
int armpmu_request_irqs(struct arm_pmu *armpmu);
void armpmu_free_irqs(struct arm_pmu *armpmu);
int armpmu_request_irq(struct arm_pmu *armpmu, int cpu);
void armpmu_free_irq(struct arm_pmu *armpmu, int cpu);

#define ARMV8_PMU_PDEV_NAME "armv8-pmu"

#endif /* CONFIG_ARM_PMU */

#endif /* __ARM_PMU_H__ */
