/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * DMTIMER platform data for TI OMAP platforms
 *
 * Copyright (C) 2012 Texas Instruments
 * Author: Jon Hunter <jon-hunter@ti.com>
 */

#ifndef __PLATFORM_DATA_DMTIMER_OMAP_H__
#define __PLATFORM_DATA_DMTIMER_OMAP_H__

struct omap_dm_timer_ops {
	struct omap_dm_timer *(*request_by_node)(struct device_node *np);
	struct omap_dm_timer *(*request_specific)(int timer_id);
	struct omap_dm_timer *(*request)(void);

	int	(*free)(struct omap_dm_timer *timer);

	void	(*enable)(struct omap_dm_timer *timer);
	void	(*disable)(struct omap_dm_timer *timer);

	int	(*get_irq)(struct omap_dm_timer *timer);
	int	(*set_int_enable)(struct omap_dm_timer *timer,
				  unsigned int value);
	int	(*set_int_disable)(struct omap_dm_timer *timer, u32 mask);

	struct clk *(*get_fclk)(struct omap_dm_timer *timer);

	int	(*start)(struct omap_dm_timer *timer);
	int	(*stop)(struct omap_dm_timer *timer);
	int	(*set_source)(struct omap_dm_timer *timer, int source);

	int	(*set_load)(struct omap_dm_timer *timer, int autoreload,
			    unsigned int value);
	int	(*set_match)(struct omap_dm_timer *timer, int enable,
			     unsigned int match);
	int	(*set_pwm)(struct omap_dm_timer *timer, int def_on,
			   int toggle, int trigger);
	int	(*get_pwm_status)(struct omap_dm_timer *timer);
	int	(*set_prescaler)(struct omap_dm_timer *timer, int prescaler);

	unsigned int (*read_counter)(struct omap_dm_timer *timer);
	int	(*write_counter)(struct omap_dm_timer *timer,
				 unsigned int value);
	unsigned int (*read_status)(struct omap_dm_timer *timer);
	int	(*write_status)(struct omap_dm_timer *timer,
				unsigned int value);
};

struct dmtimer_platform_data {
	/* set_timer_src - Only used for OMAP1 devices */
	int (*set_timer_src)(struct platform_device *pdev, int source);
	u32 timer_capability;
	u32 timer_errata;
	int (*get_context_loss_count)(struct device *);
	const struct omap_dm_timer_ops *timer_ops;
};

#endif /* __PLATFORM_DATA_DMTIMER_OMAP_H__ */
