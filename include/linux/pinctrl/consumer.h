/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Consumer interface the pin control subsystem
 *
 * Copyright (C) 2012 ST-Ericsson SA
 * Written on behalf of Linaro for ST-Ericsson
 * Based on bits of regulator core, gpio core and clk core
 *
 * Author: Linus Walleij <linus.walleij@linaro.org>
 */
#ifndef __LINUX_PINCTRL_CONSUMER_H
#define __LINUX_PINCTRL_CONSUMER_H

#include <linux/err.h>
#include <linux/types.h>

#include <linux/pinctrl/pinctrl-state.h>

struct device;
struct gpio_chip;

/* This struct is private to the core and should be regarded as a cookie */
struct pinctrl;
struct pinctrl_state;

#ifdef CONFIG_PINCTRL

/* External interface to pin control */
bool pinctrl_gpio_can_use_line(struct gpio_chip *gc, unsigned int offset);
int pinctrl_gpio_request(struct gpio_chip *gc, unsigned int offset);
void pinctrl_gpio_free(struct gpio_chip *gc, unsigned int offset);
int pinctrl_gpio_direction_input(struct gpio_chip *gc,
				 unsigned int offset);
int pinctrl_gpio_direction_output(struct gpio_chip *gc,
				  unsigned int offset);
int pinctrl_gpio_set_config(struct gpio_chip *gc, unsigned int offset,
				unsigned long config);

struct pinctrl * __must_check pinctrl_get(struct device *dev);
void pinctrl_put(struct pinctrl *p);
struct pinctrl_state * __must_check pinctrl_lookup_state(struct pinctrl *p,
							 const char *name);
int pinctrl_select_state(struct pinctrl *p, struct pinctrl_state *s);

struct pinctrl * __must_check devm_pinctrl_get(struct device *dev);
void devm_pinctrl_put(struct pinctrl *p);
int pinctrl_select_default_state(struct device *dev);

#ifdef CONFIG_PM
int pinctrl_pm_select_default_state(struct device *dev);
int pinctrl_pm_select_sleep_state(struct device *dev);
int pinctrl_pm_select_idle_state(struct device *dev);
#else
static inline int pinctrl_pm_select_default_state(struct device *dev)
{
	return 0;
}
static inline int pinctrl_pm_select_sleep_state(struct device *dev)
{
	return 0;
}
static inline int pinctrl_pm_select_idle_state(struct device *dev)
{
	return 0;
}
#endif

#else /* !CONFIG_PINCTRL */

static inline bool
pinctrl_gpio_can_use_line(struct gpio_chip *gc, unsigned int offset)
{
	return true;
}

static inline int
pinctrl_gpio_request(struct gpio_chip *gc, unsigned int offset)
{
	return 0;
}

static inline void
pinctrl_gpio_free(struct gpio_chip *gc, unsigned int offset)
{
}

static inline int
pinctrl_gpio_direction_input(struct gpio_chip *gc, unsigned int offset)
{
	return 0;
}

static inline int
pinctrl_gpio_direction_output(struct gpio_chip *gc, unsigned int offset)
{
	return 0;
}

static inline int
pinctrl_gpio_set_config(struct gpio_chip *gc, unsigned int offset,
			    unsigned long config)
{
	return 0;
}

static inline struct pinctrl * __must_check pinctrl_get(struct device *dev)
{
	return NULL;
}

static inline void pinctrl_put(struct pinctrl *p)
{
}

static inline struct pinctrl_state * __must_check pinctrl_lookup_state(struct pinctrl *p,
								       const char *name)
{
	return NULL;
}

static inline int pinctrl_select_state(struct pinctrl *p,
				       struct pinctrl_state *s)
{
	return 0;
}

static inline struct pinctrl * __must_check devm_pinctrl_get(struct device *dev)
{
	return NULL;
}

static inline void devm_pinctrl_put(struct pinctrl *p)
{
}

static inline int pinctrl_select_default_state(struct device *dev)
{
	return 0;
}

static inline int pinctrl_pm_select_default_state(struct device *dev)
{
	return 0;
}

static inline int pinctrl_pm_select_sleep_state(struct device *dev)
{
	return 0;
}

static inline int pinctrl_pm_select_idle_state(struct device *dev)
{
	return 0;
}

#endif /* CONFIG_PINCTRL */

static inline struct pinctrl * __must_check pinctrl_get_select(struct device *dev,
							       const char *name)
{
	struct pinctrl *p;
	struct pinctrl_state *s;
	int ret;

	p = pinctrl_get(dev);
	if (IS_ERR(p))
		return p;

	s = pinctrl_lookup_state(p, name);
	if (IS_ERR(s)) {
		pinctrl_put(p);
		return ERR_CAST(s);
	}

	ret = pinctrl_select_state(p, s);
	if (ret < 0) {
		pinctrl_put(p);
		return ERR_PTR(ret);
	}

	return p;
}

static inline struct pinctrl * __must_check pinctrl_get_select_default(struct device *dev)
{
	return pinctrl_get_select(dev, PINCTRL_STATE_DEFAULT);
}

static inline struct pinctrl * __must_check devm_pinctrl_get_select(struct device *dev,
								    const char *name)
{
	struct pinctrl *p;
	struct pinctrl_state *s;
	int ret;

	p = devm_pinctrl_get(dev);
	if (IS_ERR(p))
		return p;

	s = pinctrl_lookup_state(p, name);
	if (IS_ERR(s)) {
		devm_pinctrl_put(p);
		return ERR_CAST(s);
	}

	ret = pinctrl_select_state(p, s);
	if (ret < 0) {
		devm_pinctrl_put(p);
		return ERR_PTR(ret);
	}

	return p;
}

static inline struct pinctrl * __must_check devm_pinctrl_get_select_default(struct device *dev)
{
	return devm_pinctrl_get_select(dev, PINCTRL_STATE_DEFAULT);
}

#endif /* __LINUX_PINCTRL_CONSUMER_H */
