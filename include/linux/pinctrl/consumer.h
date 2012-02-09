/*
 * Consumer interface the pin control subsystem
 *
 * Copyright (C) 2012 ST-Ericsson SA
 * Written on behalf of Linaro for ST-Ericsson
 * Based on bits of regulator core, gpio core and clk core
 *
 * Author: Linus Walleij <linus.walleij@linaro.org>
 *
 * License terms: GNU General Public License (GPL) version 2
 */
#ifndef __LINUX_PINCTRL_CONSUMER_H
#define __LINUX_PINCTRL_CONSUMER_H

#include <linux/list.h>
#include <linux/seq_file.h>
#include "pinctrl.h"

/* This struct is private to the core and should be regarded as a cookie */
struct pinmux;

#ifdef CONFIG_PINMUX

/* External interface to pinmux */
extern int pinmux_request_gpio(unsigned gpio);
extern void pinmux_free_gpio(unsigned gpio);
extern int pinmux_gpio_direction_input(unsigned gpio);
extern int pinmux_gpio_direction_output(unsigned gpio);
extern struct pinmux * __must_check pinmux_get(struct device *dev, const char *name);
extern void pinmux_put(struct pinmux *pmx);
extern int pinmux_enable(struct pinmux *pmx);
extern void pinmux_disable(struct pinmux *pmx);

#else /* !CONFIG_PINMUX */

static inline int pinmux_request_gpio(unsigned gpio)
{
	return 0;
}

static inline void pinmux_free_gpio(unsigned gpio)
{
}

static inline int pinmux_gpio_direction_input(unsigned gpio)
{
	return 0;
}

static inline int pinmux_gpio_direction_output(unsigned gpio)
{
	return 0;
}

static inline struct pinmux * __must_check pinmux_get(struct device *dev, const char *name)
{
	return NULL;
}

static inline void pinmux_put(struct pinmux *pmx)
{
}

static inline int pinmux_enable(struct pinmux *pmx)
{
	return 0;
}

static inline void pinmux_disable(struct pinmux *pmx)
{
}

#endif /* CONFIG_PINMUX */

#ifdef CONFIG_PINCONF

extern int pin_config_get(const char *dev_name, const char *name,
			  unsigned long *config);
extern int pin_config_set(const char *dev_name, const char *name,
			  unsigned long config);
extern int pin_config_group_get(const char *dev_name,
				const char *pin_group,
				unsigned long *config);
extern int pin_config_group_set(const char *dev_name,
				const char *pin_group,
				unsigned long config);

#else

static inline int pin_config_get(const char *dev_name, const char *name,
				 unsigned long *config)
{
	return 0;
}

static inline int pin_config_set(const char *dev_name, const char *name,
				 unsigned long config)
{
	return 0;
}

static inline int pin_config_group_get(const char *dev_name,
				       const char *pin_group,
				       unsigned long *config)
{
	return 0;
}

static inline int pin_config_group_set(const char *dev_name,
				       const char *pin_group,
				       unsigned long config)
{
	return 0;
}

#endif

#endif /* __LINUX_PINCTRL_CONSUMER_H */
