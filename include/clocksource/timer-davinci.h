/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * TI DaVinci clocksource driver
 *
 * Copyright (C) 2019 Texas Instruments
 * Author: Bartosz Golaszewski <bgolaszewski@baylibre.com>
 */

#ifndef __TIMER_DAVINCI_H__
#define __TIMER_DAVINCI_H__

#include <linux/clk.h>
#include <linux/ioport.h>

enum {
	DAVINCI_TIMER_CLOCKEVENT_IRQ,
	DAVINCI_TIMER_CLOCKSOURCE_IRQ,
	DAVINCI_TIMER_NUM_IRQS,
};

/**
 * struct davinci_timer_cfg - davinci clocksource driver configuration struct
 * @reg:        register range resource
 * @irq:        clockevent and clocksource interrupt resources
 * @cmp_off:    if set - it specifies the compare register used for clockevent
 *
 * Note: if the compare register is specified, the driver will use the bottom
 * clock half for both clocksource and clockevent and the compare register
 * to generate event irqs. The user must supply the correct compare register
 * interrupt number.
 *
 * This is only used by da830 the DSP of which uses the top half. The timer
 * driver still configures the top half to run in free-run mode.
 */
struct davinci_timer_cfg {
	struct resource reg;
	struct resource irq[DAVINCI_TIMER_NUM_IRQS];
	unsigned int cmp_off;
};

int __init davinci_timer_register(struct clk *clk,
				  const struct davinci_timer_cfg *data);

#endif /* __TIMER_DAVINCI_H__ */
