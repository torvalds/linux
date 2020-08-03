/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_GPIO_MACHINE_H
#define __LINUX_GPIO_MACHINE_H

#include <linux/types.h>
#include <linux/list.h>

enum gpio_lookup_flags {
	GPIO_ACTIVE_HIGH		= (0 << 0),
	GPIO_ACTIVE_LOW			= (1 << 0),
	GPIO_OPEN_DRAIN			= (1 << 1),
	GPIO_OPEN_SOURCE		= (1 << 2),
	GPIO_PERSISTENT			= (0 << 3),
	GPIO_TRANSITORY			= (1 << 3),
	GPIO_PULL_UP			= (1 << 4),
	GPIO_PULL_DOWN			= (1 << 5),

	GPIO_LOOKUP_FLAGS_DEFAULT	= GPIO_ACTIVE_HIGH | GPIO_PERSISTENT,
};

/**
 * struct gpiod_lookup - lookup table
 * @key: either the name of the chip the GPIO belongs to, or the GPIO line name
 *       Note that GPIO line names are not guaranteed to be globally unique,
 *       so this will use the first match found!
 * @chip_hwnum: hardware number (i.e. relative to the chip) of the GPIO, or
 *              U16_MAX to indicate that @key is a GPIO line name
 * @con_id: name of the GPIO from the device's point of view
 * @idx: index of the GPIO in case several GPIOs share the same name
 * @flags: bitmask of gpio_lookup_flags GPIO_* values
 *
 * gpiod_lookup is a lookup table for associating GPIOs to specific devices and
 * functions using platform data.
 */
struct gpiod_lookup {
	const char *key;
	u16 chip_hwnum;
	const char *con_id;
	unsigned int idx;
	unsigned long flags;
};

struct gpiod_lookup_table {
	struct list_head list;
	const char *dev_id;
	struct gpiod_lookup table[];
};

/**
 * struct gpiod_hog - GPIO line hog table
 * @chip_label: name of the chip the GPIO belongs to
 * @chip_hwnum: hardware number (i.e. relative to the chip) of the GPIO
 * @line_name: consumer name for the hogged line
 * @lflags: bitmask of gpio_lookup_flags GPIO_* values
 * @dflags: GPIO flags used to specify the direction and value
 */
struct gpiod_hog {
	struct list_head list;
	const char *chip_label;
	u16 chip_hwnum;
	const char *line_name;
	unsigned long lflags;
	int dflags;
};

/*
 * Simple definition of a single GPIO under a con_id
 */
#define GPIO_LOOKUP(_key, _chip_hwnum, _con_id, _flags) \
	GPIO_LOOKUP_IDX(_key, _chip_hwnum, _con_id, 0, _flags)

/*
 * Use this macro if you need to have several GPIOs under the same con_id.
 * Each GPIO needs to use a different index and can be accessed using
 * gpiod_get_index()
 */
#define GPIO_LOOKUP_IDX(_key, _chip_hwnum, _con_id, _idx, _flags)         \
{                                                                         \
	.key = _key,                                                      \
	.chip_hwnum = _chip_hwnum,                                        \
	.con_id = _con_id,                                                \
	.idx = _idx,                                                      \
	.flags = _flags,                                                  \
}

/*
 * Simple definition of a single GPIO hog in an array.
 */
#define GPIO_HOG(_chip_label, _chip_hwnum, _line_name, _lflags, _dflags)  \
{                                                                         \
	.chip_label = _chip_label,                                        \
	.chip_hwnum = _chip_hwnum,                                        \
	.line_name = _line_name,                                          \
	.lflags = _lflags,                                                \
	.dflags = _dflags,                                                \
}

#ifdef CONFIG_GPIOLIB
void gpiod_add_lookup_table(struct gpiod_lookup_table *table);
void gpiod_add_lookup_tables(struct gpiod_lookup_table **tables, size_t n);
void gpiod_remove_lookup_table(struct gpiod_lookup_table *table);
void gpiod_add_hogs(struct gpiod_hog *hogs);
#else /* ! CONFIG_GPIOLIB */
static inline
void gpiod_add_lookup_table(struct gpiod_lookup_table *table) {}
static inline
void gpiod_add_lookup_tables(struct gpiod_lookup_table **tables, size_t n) {}
static inline
void gpiod_remove_lookup_table(struct gpiod_lookup_table *table) {}
static inline void gpiod_add_hogs(struct gpiod_hog *hogs) {}
#endif /* CONFIG_GPIOLIB */

#endif /* __LINUX_GPIO_MACHINE_H */
