/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_GENERIC_GPIO_H
#define _ASM_GENERIC_GPIO_H

#include <linux/types.h>
#include <linux/errno.h>

#ifdef CONFIG_GPIOLIB

#include <linux/compiler.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/consumer.h>

/*
 * Platforms may implement their GPIO interface with library code,
 * at a small performance cost for non-inlined operations and some
 * extra memory (for code and for per-GPIO table entries).
 */

/*
 * At the end we want all GPIOs to be dynamically allocated from 0.
 * However, some legacy drivers still perform fixed allocation.
 * Until they are all fixed, leave 0-512 space for them.
 */
#define GPIO_DYNAMIC_BASE	512

struct device;
struct gpio;
struct seq_file;
struct module;
struct device_node;
struct gpio_desc;

/* caller holds gpio_lock *OR* gpio is marked as requested */
static inline struct gpio_chip *gpio_to_chip(unsigned gpio)
{
	return gpiod_to_chip(gpio_to_desc(gpio));
}

/* Always use the library code for GPIO management calls,
 * or when sleeping may be involved.
 */
extern int gpio_request(unsigned gpio, const char *label);
extern void gpio_free(unsigned gpio);

static inline int gpio_direction_input(unsigned gpio)
{
	return gpiod_direction_input(gpio_to_desc(gpio));
}
static inline int gpio_direction_output(unsigned gpio, int value)
{
	return gpiod_direction_output_raw(gpio_to_desc(gpio), value);
}

static inline int gpio_set_debounce(unsigned gpio, unsigned debounce)
{
	return gpiod_set_debounce(gpio_to_desc(gpio), debounce);
}

static inline int gpio_get_value_cansleep(unsigned gpio)
{
	return gpiod_get_raw_value_cansleep(gpio_to_desc(gpio));
}
static inline void gpio_set_value_cansleep(unsigned gpio, int value)
{
	return gpiod_set_raw_value_cansleep(gpio_to_desc(gpio), value);
}


/* A platform's <asm/gpio.h> code may want to inline the I/O calls when
 * the GPIO is constant and refers to some always-present controller,
 * giving direct access to chip registers and tight bitbanging loops.
 */
static inline int __gpio_get_value(unsigned gpio)
{
	return gpiod_get_raw_value(gpio_to_desc(gpio));
}
static inline void __gpio_set_value(unsigned gpio, int value)
{
	return gpiod_set_raw_value(gpio_to_desc(gpio), value);
}

static inline int __gpio_cansleep(unsigned gpio)
{
	return gpiod_cansleep(gpio_to_desc(gpio));
}

static inline int __gpio_to_irq(unsigned gpio)
{
	return gpiod_to_irq(gpio_to_desc(gpio));
}

extern int gpio_request_one(unsigned gpio, unsigned long flags, const char *label);
extern int gpio_request_array(const struct gpio *array, size_t num);
extern void gpio_free_array(const struct gpio *array, size_t num);

/*
 * A sysfs interface can be exported by individual drivers if they want,
 * but more typically is configured entirely from userspace.
 */
static inline int gpio_export(unsigned gpio, bool direction_may_change)
{
	return gpiod_export(gpio_to_desc(gpio), direction_may_change);
}

static inline int gpio_export_link(struct device *dev, const char *name,
				   unsigned gpio)
{
	return gpiod_export_link(dev, name, gpio_to_desc(gpio));
}

static inline void gpio_unexport(unsigned gpio)
{
	gpiod_unexport(gpio_to_desc(gpio));
}

#else	/* !CONFIG_GPIOLIB */

#include <linux/kernel.h>

/* platforms that don't directly support access to GPIOs through I2C, SPI,
 * or other blocking infrastructure can use these wrappers.
 */

static inline int gpio_cansleep(unsigned gpio)
{
	return 0;
}

static inline int gpio_get_value_cansleep(unsigned gpio)
{
	might_sleep();
	return __gpio_get_value(gpio);
}

static inline void gpio_set_value_cansleep(unsigned gpio, int value)
{
	might_sleep();
	__gpio_set_value(gpio, value);
}

#endif /* !CONFIG_GPIOLIB */

/*
 * "valid" GPIO numbers are nonnegative and may be passed to
 * setup routines like gpio_request().  only some valid numbers
 * can successfully be requested and used.
 *
 * Invalid GPIO numbers are useful for indicating no-such-GPIO in
 * platform data and other tables.
 */

static inline bool gpio_is_valid(int number)
{
	/* only non-negative numbers are valid */
	return number >= 0;
}

#endif /* _ASM_GENERIC_GPIO_H */
