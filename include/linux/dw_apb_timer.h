/*
 * (C) Copyright 2009 Intel Corporation
 * Author: Jacob Pan (jacob.jun.pan@intel.com)
 *
 * Shared with ARM platforms, Jamie Iles, Picochip 2011
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Support for the Synopsys DesignWare APB Timers.
 */
#ifndef __DW_APB_TIMER_H__
#define __DW_APB_TIMER_H__

#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/interrupt.h>

#define APBTMR_N_LOAD_COUNT		0x00
#define APBTMR_N_CURRENT_VALUE		0x04
#define APBTMR_N_CONTROL		0x08
#define APBTMR_N_EOI			0x0c
#define APBTMR_N_INT_STATUS		0x10

#define APBTMRS_REG_SIZE       0x14

struct dw_apb_timer {
	void __iomem				*base;
	unsigned long				freq;
	int					irq;
};

struct dw_apb_clock_event_device {
	struct clock_event_device		ced;
	struct dw_apb_timer			timer;
	struct irqaction			irqaction;
	void					(*eoi)(struct dw_apb_timer *);
};

struct dw_apb_clocksource {
	struct dw_apb_timer			timer;
	struct clocksource			cs;
};

void dw_apb_clockevent_register(struct dw_apb_clock_event_device *dw_ced);
void dw_apb_clockevent_pause(struct dw_apb_clock_event_device *dw_ced);
void dw_apb_clockevent_resume(struct dw_apb_clock_event_device *dw_ced);
void dw_apb_clockevent_stop(struct dw_apb_clock_event_device *dw_ced);

struct dw_apb_clock_event_device *
dw_apb_clockevent_init(int cpu, const char *name, unsigned rating,
		       void __iomem *base, int irq, unsigned long freq);
struct dw_apb_clocksource *
dw_apb_clocksource_init(unsigned rating, const char *name, void __iomem *base,
			unsigned long freq);
void dw_apb_clocksource_register(struct dw_apb_clocksource *dw_cs);
void dw_apb_clocksource_start(struct dw_apb_clocksource *dw_cs);
cycle_t dw_apb_clocksource_read(struct dw_apb_clocksource *dw_cs);
void dw_apb_clocksource_unregister(struct dw_apb_clocksource *dw_cs);

extern void dw_apb_timer_init(void);
#endif /* __DW_APB_TIMER_H__ */
