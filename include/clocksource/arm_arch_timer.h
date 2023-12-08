/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 ARM Ltd.
 */
#ifndef __CLKSOURCE_ARM_ARCH_TIMER_H
#define __CLKSOURCE_ARM_ARCH_TIMER_H

#include <linux/bitops.h>
#include <linux/timecounter.h>
#include <linux/types.h>

#define ARCH_TIMER_TYPE_CP15		BIT(0)
#define ARCH_TIMER_TYPE_MEM		BIT(1)

#define ARCH_TIMER_CTRL_ENABLE		(1 << 0)
#define ARCH_TIMER_CTRL_IT_MASK		(1 << 1)
#define ARCH_TIMER_CTRL_IT_STAT		(1 << 2)

#define CNTHCTL_EL1PCTEN		(1 << 0)
#define CNTHCTL_EL1PCEN			(1 << 1)
#define CNTHCTL_EVNTEN			(1 << 2)
#define CNTHCTL_EVNTDIR			(1 << 3)
#define CNTHCTL_EVNTI			(0xF << 4)

enum arch_timer_reg {
	ARCH_TIMER_REG_CTRL,
	ARCH_TIMER_REG_CVAL,
};

enum arch_timer_ppi_nr {
	ARCH_TIMER_PHYS_SECURE_PPI,
	ARCH_TIMER_PHYS_NONSECURE_PPI,
	ARCH_TIMER_VIRT_PPI,
	ARCH_TIMER_HYP_PPI,
	ARCH_TIMER_HYP_VIRT_PPI,
	ARCH_TIMER_MAX_TIMER_PPI
};

enum arch_timer_spi_nr {
	ARCH_TIMER_PHYS_SPI,
	ARCH_TIMER_VIRT_SPI,
	ARCH_TIMER_MAX_TIMER_SPI
};

#define ARCH_TIMER_PHYS_ACCESS		0
#define ARCH_TIMER_VIRT_ACCESS		1
#define ARCH_TIMER_MEM_PHYS_ACCESS	2
#define ARCH_TIMER_MEM_VIRT_ACCESS	3

#define ARCH_TIMER_MEM_MAX_FRAMES	8

#define ARCH_TIMER_USR_PCT_ACCESS_EN	(1 << 0) /* physical counter */
#define ARCH_TIMER_USR_VCT_ACCESS_EN	(1 << 1) /* virtual counter */
#define ARCH_TIMER_VIRT_EVT_EN		(1 << 2)
#define ARCH_TIMER_EVT_TRIGGER_SHIFT	(4)
#define ARCH_TIMER_EVT_TRIGGER_MASK	(0xF << ARCH_TIMER_EVT_TRIGGER_SHIFT)
#define ARCH_TIMER_USR_VT_ACCESS_EN	(1 << 8) /* virtual timer registers */
#define ARCH_TIMER_USR_PT_ACCESS_EN	(1 << 9) /* physical timer registers */
#define ARCH_TIMER_EVT_INTERVAL_SCALE	(1 << 17) /* EVNTIS in the ARMv8 ARM */

#define ARCH_TIMER_EVT_STREAM_PERIOD_US	100
#define ARCH_TIMER_EVT_STREAM_FREQ				\
	(USEC_PER_SEC / ARCH_TIMER_EVT_STREAM_PERIOD_US)

struct arch_timer_kvm_info {
	struct timecounter timecounter;
	int virtual_irq;
	int physical_irq;
};

struct arch_timer_mem_frame {
	bool valid;
	phys_addr_t cntbase;
	size_t size;
	int phys_irq;
	int virt_irq;
};

struct arch_timer_mem {
	phys_addr_t cntctlbase;
	size_t size;
	struct arch_timer_mem_frame frame[ARCH_TIMER_MEM_MAX_FRAMES];
};

#ifdef CONFIG_ARM_ARCH_TIMER

extern u32 arch_timer_get_rate(void);
extern u64 (*arch_timer_read_counter)(void);
extern struct arch_timer_kvm_info *arch_timer_get_kvm_info(void);
extern bool arch_timer_evtstrm_available(void);

#else

static inline u32 arch_timer_get_rate(void)
{
	return 0;
}

static inline u64 arch_timer_read_counter(void)
{
	return 0;
}

static inline bool arch_timer_evtstrm_available(void)
{
	return false;
}

#endif

#endif
