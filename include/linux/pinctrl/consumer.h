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
struct pinctrl;

#ifdef CONFIG_PINCTRL

/* External interface to pin control */
extern int pinctrl_request_gpio(unsigned gpio);
extern void pinctrl_free_gpio(unsigned gpio);
extern int pinctrl_gpio_direction_input(unsigned gpio);
extern int pinctrl_gpio_direction_output(unsigned gpio);
extern struct pinctrl * __must_check pinctrl_get(struct device *dev, const char *name);
extern void pinctrl_put(struct pinctrl *p);
extern int pinctrl_enable(struct pinctrl *p);
extern void pinctrl_disable(struct pinctrl *p);

#else /* !CONFIG_PINCTRL */

static inline int pinctrl_request_gpio(unsigned gpio)
{
	return 0;
}

static inline void pinctrl_free_gpio(unsigned gpio)
{
}

static inline int pinctrl_gpio_direction_input(unsigned gpio)
{
	return 0;
}

static inline int pinctrl_gpio_direction_output(unsigned gpio)
{
	return 0;
}

static inline struct pinctrl * __must_check pinctrl_get(struct device *dev, const char *name)
{
	return NULL;
}

static inline void pinctrl_put(struct pinctrl *p)
{
}

static inline int pinctrl_enable(struct pinctrl *p)
{
	return 0;
}

static inline void pinctrl_disable(struct pinctrl *p)
{
}

#endif /* CONFIG_PINCTRL */

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
