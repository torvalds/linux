/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Per-device information from the pin control system.
 * This is the stuff that get included into the device
 * core.
 *
 * Copyright (C) 2012 ST-Ericsson SA
 * Written on behalf of Linaro for ST-Ericsson
 * This interface is used in the core to keep track of pins.
 *
 * Author: Linus Walleij <linus.walleij@linaro.org>
 */

#ifndef PINCTRL_DEVINFO_H
#define PINCTRL_DEVINFO_H

struct device;

#ifdef CONFIG_PINCTRL

#include <linux/device.h>

/* The device core acts as a consumer toward pinctrl */
#include <linux/pinctrl/consumer.h>

struct pinctrl;

/**
 * struct dev_pin_info - pin state container for devices
 * @p: pinctrl handle for the containing device
 * @default_state: the default state for the handle, if found
 * @init_state: the state at probe time, if found
 * @sleep_state: the state at suspend time, if found
 * @idle_state: the state at idle (runtime suspend) time, if found
 */
struct dev_pin_info {
	struct pinctrl *p;
	struct pinctrl_state *default_state;
	struct pinctrl_state *init_state;
#ifdef CONFIG_PM
	struct pinctrl_state *sleep_state;
	struct pinctrl_state *idle_state;
#endif
};

extern int pinctrl_bind_pins(struct device *dev);
extern int pinctrl_init_done(struct device *dev);

static inline struct pinctrl *dev_pinctrl(struct device *dev)
{
	if (!dev->pins)
		return NULL;

	return dev->pins->p;
}

#else

/* Stubs if we're not using pinctrl */

static inline int pinctrl_bind_pins(struct device *dev)
{
	return 0;
}

static inline int pinctrl_init_done(struct device *dev)
{
	return 0;
}

static inline struct pinctrl *dev_pinctrl(struct device *dev)
{
	return NULL;
}

#endif /* CONFIG_PINCTRL */
#endif /* PINCTRL_DEVINFO_H */
